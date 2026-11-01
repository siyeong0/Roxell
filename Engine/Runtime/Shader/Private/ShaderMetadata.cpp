#include "ShaderMetadata.h"

#include "ShaderLocalRootMetadata.h"
#include "ShaderPayloadCanonicalizer.h"
#include "ShaderRayMetadata.h"
#include "ShaderWire.hpp"

#include <Algorithms.hpp>
#include <Comparison.hpp>
#include <ComparisonTypes.hpp>
#include <Numeric.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		bool validName(StringView text, const ShaderLimits& limits)
		{
			return !text.IsEmpty() && text.Size() <= limits.MaximumNameBytes && !text.Contains('\0') && IsValidUtf8(text);
		}

		bool ioLess(const ShaderStageIO& a, const ShaderStageIO& b)
		{
			if (a.Rate != b.Rate)
			{
				return a.Rate < b.Rate;
			}

			if (a.Builtin != b.Builtin)
			{
				return a.Builtin < b.Builtin;
			}

			if (a.Location != b.Location)
			{
				return a.Location < b.Location;
			}

			if (a.Component != b.Component)
			{
				return a.Component < b.Component;
			}

			if (a.Semantic != b.Semantic)
			{
				return a.Semantic.View() < b.Semantic.View();
			}

			return a.SemanticIndex < b.SemanticIndex;
		}

		bool pushLess(const ShaderPushConstantRange& a, const ShaderPushConstantRange& b)
		{
			if (a.Lowering != b.Lowering)
			{
				return a.Lowering < b.Lowering;
			}

			if (a.Space != b.Space)
			{
				return a.Space < b.Space;
			}

			if (a.Binding != b.Binding)
			{
				return a.Binding < b.Binding;
			}

			return a.ByteOffset < b.ByteOffset;
		}

		bool specLess(const ShaderSpecializationParameter& a, const ShaderSpecializationParameter& b)
		{
			return a.ID < b.ID;
		}

		int64 signedValue(uint64 bits, uint32 width)
		{
			if (width == 64)
			{
				return BitCast<int64>(bits);
			}

			const uint64 sign = uint64(1) << (width - 1);
			return static_cast<int64>(bits) - ((bits & sign) != 0 ? static_cast<int64>(uint64(1) << width)
				: 0);
		}

		double floatValue(uint64 bits, uint32 width)
		{
			return width == 32 ? static_cast<double>(BitCast<float>(static_cast<uint32>(bits)))
				: BitCast<double>(bits);
		}

		bool bitsValid(const ShaderSpecializationParameter& p, uint64 bits)
		{
			if (p.ScalarWidth != 8 && p.ScalarWidth != 16 && p.ScalarWidth != 32 && p.ScalarWidth != 64)
			{
				return false;
			}

			if (p.ScalarWidth != 64 && bits >= (uint64(1) << p.ScalarWidth))
			{
				return false;
			}

			if (p.Scalar == EShaderScalar::Bool)
			{
				return p.ScalarWidth == 32 && bits <= 1;
			}

			if (p.Scalar == EShaderScalar::Float)
			{
				return (p.ScalarWidth == 32 || p.ScalarWidth == 64) && IsFinite(floatValue(bits, p.ScalarWidth));
			}

			return p.Scalar == EShaderScalar::SignedInteger || p.Scalar == EShaderScalar::UnsignedInteger;
		}

		bool lessEqual(const ShaderSpecializationParameter& p, uint64 a, uint64 b)
		{
			if (p.Scalar == EShaderScalar::SignedInteger)
			{
				return signedValue(a, p.ScalarWidth) <= signedValue(b, p.ScalarWidth);
			}

			if (p.Scalar == EShaderScalar::Float)
			{
				return floatValue(a, p.ScalarWidth) <= floatValue(b, p.ScalarWidth);
			}

			return a <= b;
		}

		template <class T, class Less>
		bool reorder(Array<T>& values, MemoryResource& scratch, Less less)
		{
			Array<uint32> indices(scratch);
			Array<T> ordered(values.GetResource());
			if (!OrderIndices(values, indices, less) || !ordered.Reserve(values.Size()))
			{
				return false;
			}

			for (uint32 index : indices)
			{
				ordered.AddRequired(Move(values[index]));
			}

			values = Move(ordered);
			return true;
		}

		void writeIO(Writer& writer, const ShaderStageIO& io, [[maybe_unused]] uint32 abi)
		{
			writer.Text(io.Name);
			writer.Text(io.Semantic);
			writer.U32(io.SemanticIndex);
			writer.U32(io.Location);
			writer.U32(io.Component);
			writer.Enum(io.Builtin);
			writer.Enum(io.Scalar);
			writer.U32(io.ScalarWidth);
			writer.U32(io.VectorWidth);
			writer.U32(io.ArrayCount);
			writer.Enum(io.Interpolation);
			writer.U32(io.bCentroid ? 1 : 0);
			writer.U32(io.bSample ? 1 : 0);
			{
				writer.Enum(io.Rate);
			}
		}

		void writeSpec(Writer& writer, const ShaderSpecializationParameter& p)
		{
			writer.U32(p.ID);
			writer.Text(p.Name);
			writer.Enum(p.Scalar);
			writer.U32(p.ScalarWidth);
			writer.Enum(p.Phase);
			writer.U64(p.DefaultBits);
			writer.U64(p.MinimumBits);
			writer.U64(p.MaximumBits);
			writer.U32(p.bHasDefault ? 1 : 0);
			writer.U32(p.bHasRange ? 1 : 0);
			writer.U32(p.bAffectsLayout ? 1 : 0);
			writer.U32(p.WorkgroupAxis);
		}

		bool hasMeshMetadata(const ShaderVariant& variant) noexcept
		{
			if (variant.MeshOutput != ShaderMeshOutput{} || variant.TaskPayload.Proof != EShaderTaskPayloadProof::None || variant.TaskPayload.RootType != NO_PAYLOAD_TYPE || !variant.TaskPayload.Types.IsEmpty() ||
			    variant.TaskPayload.bHasProvenByteSize || variant.TaskPayload.ProvenByteSize != 0)
			{
				return true;
			}

			for (const Array<ShaderStageIO>* table : {&variant.StageInputs, &variant.StageOutputs})
			{
				for (const ShaderStageIO& io : *table)
				{
					if (io.Rate != EShaderStageIORate::Unspecified)
					{
						return true;
					}
				}
			}

			return false;
		}
	} // namespace

	bool SpecializationValueValid(const ShaderSpecializationParameter& p, uint64 bits) noexcept
	{
		return bitsValid(p, bits) && (!p.bHasRange || (lessEqual(p, p.MinimumBits, bits) && lessEqual(p, bits, p.MaximumBits)));
	}

	bool SpecializationShapeEqual(const ShaderSpecializationParameter& a, const ShaderSpecializationParameter& b) noexcept
	{
		return a.ID == b.ID &&
			   a.Name == b.Name &&
			   a.Scalar == b.Scalar &&
			   a.ScalarWidth == b.ScalarWidth &&
			   a.Phase == b.Phase &&
			   a.DefaultBits == b.DefaultBits &&
			   a.MinimumBits == b.MinimumBits &&
			   a.MaximumBits == b.MaximumBits &&
		       a.bHasDefault == b.bHasDefault &&
			   a.bHasRange == b.bHasRange &&
			   a.bAffectsLayout == b.bAffectsLayout &&
			   a.WorkgroupAxis == b.WorkgroupAxis;
	}

	Result<ShaderSpecializationParameter> CloneSpecializationParameter(const ShaderSpecializationParameter& p, MemoryResource& resource)
	{
		ShaderSpecializationParameter result(resource);
		result.ID = p.ID;
		result.Scalar = p.Scalar;
		result.ScalarWidth = p.ScalarWidth;
		result.Phase = p.Phase;
		result.DefaultBits = p.DefaultBits;
		result.MinimumBits = p.MinimumBits;
		result.MaximumBits = p.MaximumBits;
		result.bHasDefault = p.bHasDefault;
		result.bHasRange = p.bHasRange;
		result.bAffectsLayout = p.bAffectsLayout;
		result.WorkgroupAxis = p.WorkgroupAxis;
		if (!result.Name.Assign(p.Name))
		{
			return Result<ShaderSpecializationParameter>::Failure(AllocationError());
		}

		return Result<ShaderSpecializationParameter>::Success(Move(result));
	}

	bool StageIOSlotEqual(const ShaderStageIO& a, const ShaderStageIO& b, EShaderTarget target) noexcept
	{
		if (a.Builtin != EShaderBuiltin::None || b.Builtin != EShaderBuiltin::None)
		{
			return a.Builtin == b.Builtin;
		}

		if (target == EShaderTarget::SpirV)
		{
			return a.Location == b.Location && a.Component == b.Component;
		}

		return a.Semantic == b.Semantic && a.SemanticIndex == b.SemanticIndex;
	}

	bool StageIORepresentationEqual(const ShaderStageIO& a, const ShaderStageIO& b) noexcept
	{
		return a.Scalar == b.Scalar &&
			   a.ScalarWidth == b.ScalarWidth &&
			   a.VectorWidth == b.VectorWidth &&
			   a.ArrayCount == b.ArrayCount &&
			   a.Interpolation == b.Interpolation &&
			   a.bCentroid == b.bCentroid &&
			   a.bSample == b.bSample;
	}

	Result<void> ValidateVariantMetadata(
		const ShaderVariant& variant,
		const ShaderEntryPoint& entry,
		uint32 abi,
		const ShaderLimits& limits,
		uint64& totalBytes,
		uint64& comparisonBudget)
	{
		using R = Result<void>;
		auto fail = [](StringView text) { return R::Failure(MakeArtifactError(text)); };
		{
			Result<void> local = ValidateLocalRootMetadata(variant, entry.Stage, limits, totalBytes);
			if (!local)
			{
				return local;
			}

			totalBytes += 8;
		}

		const ShaderLocalRecord& record = variant.LocalRecord;
		if (record != ShaderLocalRecord{} && record.Lowering != EShaderLocalRecordLowering::D3D12LocalRootArguments)
		{
			if (!IsRayStage(entry.Stage) || variant.Target.Target != EShaderTarget::SpirV || record.Lowering != EShaderLocalRecordLowering::VulkanShaderRecord ||
				record.DataLayout >= variant.Layouts.Size() ||
				record.ByteSize == 0 ||
				record.ByteSize > limits.MaximumTotalBytes ||
				record.Alignment == 0 ||
				(record.Alignment & (record.Alignment - 1)) != 0 ||
				variant.Layouts[record.DataLayout].ByteSize != record.ByteSize ||
				variant.Layouts[record.DataLayout].Alignment != record.Alignment)
			{
				return fail("Invalid target-specific local shader record.");
			}
		}
		{
			totalBytes += 16 + (IsRayStage(entry.Stage) ? 32 : 20);
			const uint64 dynamicArrays = static_cast<uint64>(EShaderFeature::UniformBufferArrayDynamicIndexing) | static_cast<uint64>(EShaderFeature::SampledImageArrayDynamicIndexing) |
			                           static_cast<uint64>(EShaderFeature::StorageBufferArrayDynamicIndexing) | static_cast<uint64>(EShaderFeature::StorageImageArrayDynamicIndexing);
			if (variant.Target.Target != EShaderTarget::SpirV && (variant.Requirements.Features & dynamicArrays) != 0)
			{
				return fail("Core descriptor array dynamic-indexing requirements are SPIR-V-specific.");
			}

			const uint64 heaps = static_cast<uint64>(EShaderFeature::ResourceDescriptorHeap) | static_cast<uint64>(EShaderFeature::SamplerDescriptorHeap);
			if (variant.Target.Target != EShaderTarget::Dxil && (variant.Requirements.Features & heaps) != 0)
			{
				return fail("Device descriptor heap requirements are DXIL-specific.");
			}

			if (!IsRayStage(entry.Stage) && !IsDefaultRayInterface(variant.RayInterface))
			{
				return fail("Nonray exports require absent ray metadata.");
			}
		}

		if (abi != SHADER_ABI_VERSION)
		{
			return fail("Unknown shader ABI version.");
		}

		const bool bRayStage = (IsRayStage(entry.Stage));
		const bool bRasterContracts = !bRayStage;
		const bool bMesh = entry.Stage == EShaderStage::Mesh;
		const bool bTask = entry.Stage == EShaderStage::Task;
		if ((!bRasterContracts && hasMeshMetadata(variant)) ||
			(entry.Stage != EShaderStage::Compute && entry.Stage != EShaderStage::Vertex && entry.Stage != EShaderStage::Pixel && !(bRasterContracts && (bMesh || bTask)) && !(bRayStage && IsRayStage(entry.Stage))))
		{
			return R::Failure(MakeArtifactError("The current shader contract has no metadata representation for this stage.", EErrorCode::Unsupported));
		}

		if (bRayStage)
		{
			if (!IsRayStage(entry.Stage) || hasMeshMetadata(variant) || !variant.StageInputs.IsEmpty() || !variant.StageOutputs.IsEmpty() || !variant.PushConstants.IsEmpty() ||
				!variant.SpecializationParameters.IsEmpty() ||
				(variant.RequiredCapabilities & static_cast<uint64>(EShaderCapability::RayTracing)) == 0 ||
				!variant.bStageIOComplete)
			{
				return fail("A ray stage requires a complete ray export with no raster or unrepresented " "native parameters.");
			}

			Result<void> ray = ValidateRayInterface(variant.RayInterface, entry, variant.Target.Target, limits, totalBytes);
			if (!ray)
			{
				return ray;
			}
		}

		const ShaderFeatureRequirements& features = variant.Requirements;
		const uint64 subgroup = static_cast<uint64>(EShaderFeature::Subgroup);
		const uint64 demote = static_cast<uint64>(EShaderFeature::DemoteToHelperInvocation);
		const uint64 sampleRate = static_cast<uint64>(EShaderFeature::SampleRateShading);
		const uint64 typedUav = static_cast<uint64>(EShaderFeature::TypedUavLoadAdditionalFormats);
		const uint64 minimumPrecision = static_cast<uint64>(EShaderFeature::MinimumPrecision);
		const uint64 gatherExtended = static_cast<uint64>(EShaderFeature::ImageGatherExtended);
		const uint64 storageExtended = static_cast<uint64>(EShaderFeature::StorageImageExtendedFormats);
		const uint64 readWithoutFormat = static_cast<uint64>(EShaderFeature::StorageImageReadWithoutFormat);
		const uint64 writeWithoutFormat = static_cast<uint64>(EShaderFeature::StorageImageWriteWithoutFormat);
		const uint64 subgroupOperations = static_cast<uint64>(EShaderFeature::SubgroupVote) | static_cast<uint64>(EShaderFeature::SubgroupArithmetic) | static_cast<uint64>(EShaderFeature::SubgroupBallot) |
		                                static_cast<uint64>(EShaderFeature::SubgroupShuffle) | static_cast<uint64>(EShaderFeature::SubgroupShuffleRelative) | static_cast<uint64>(EShaderFeature::SubgroupClustered);
		const uint64 allowedFeatures = uint64(4194303) | demote | sampleRate | typedUav | minimumPrecision | gatherExtended | storageExtended | readWithoutFormat | writeWithoutFormat | subgroupOperations;
		if (features.Version != 1 || (features.Features & ~allowedFeatures) != 0 ||
			((features.Features & subgroupOperations) != 0 && (variant.Target.Target != EShaderTarget::SpirV || entry.Stage != EShaderStage::Compute || (features.Features & subgroup) == 0)) ||
			(variant.Target.Target == EShaderTarget::SpirV &&
				(features.Features & (subgroup | static_cast<uint64>(EShaderFeature::SubgroupQuad))) != 0 &&
				(entry.Stage != EShaderStage::Compute && entry.Stage != EShaderStage::Pixel)) ||
			((features.Features & (typedUav | minimumPrecision)) != 0 && variant.Target.Target != EShaderTarget::Dxil) ||
			((features.Features & (gatherExtended | storageExtended | readWithoutFormat | writeWithoutFormat)) != 0 && variant.Target.Target != EShaderTarget::SpirV) ||
			((features.Features & (demote | sampleRate)) != 0 && (variant.Target.Target != EShaderTarget::SpirV || entry.Stage != EShaderStage::Pixel)) ||
			(features.Features & 511) != variant.RequiredCapabilities ||
			features.MinimumWorkgroupInvocations > 1024 ||
			features.MinimumPushConstantBytes > limits.MaximumTotalBytes ||
			((features.MinimumSubgroupSize != 0 || features.MaximumSubgroupSize != 0) && (features.Features & subgroup) == 0) ||
			(features.MinimumSubgroupSize == 0) != (features.MaximumSubgroupSize == 0) ||
			features.MinimumSubgroupSize > features.MaximumSubgroupSize ||
			features.MaximumSubgroupSize > 128 ||
			(features.MinimumSubgroupSize != 0 && (features.MinimumSubgroupSize & (features.MinimumSubgroupSize - 1)) != 0) ||
			(features.MaximumSubgroupSize != 0 && (features.MaximumSubgroupSize & (features.MaximumSubgroupSize - 1)) != 0) ||
			((features.Features & static_cast<uint64>(EShaderFeature::SubgroupQuad)) != 0 && (features.Features & subgroup) == 0) ||
			((features.Features & static_cast<uint64>(EShaderFeature::Int64Atomics)) != 0 && (features.Features & 12) != 12))
		{
			return fail("Invalid exact shader feature requirements.");
		}

		const uint64 workgroup = uint64(entry.ThreadGroupSize[0]) * entry.ThreadGroupSize[1] * entry.ThreadGroupSize[2];
		if (features.MinimumWorkgroupInvocations != workgroup)
		{
			return fail("Workgroup requirement disagrees with the emitted entry.");
		}

		if (bRasterContracts)
		{
			const ShaderMeshOutput& output = variant.MeshOutput;
			bool bInvalidOutput = output != ShaderMeshOutput{};
			if (bMesh)
			{
				bInvalidOutput = output.Topology != EShaderMeshTopology::Triangles ||
					output.MaximumVertices == 0 || output.MaximumVertices > 256 ||
					output.MaximumPrimitives == 0 || output.MaximumPrimitives > 256;
			}

			if (bInvalidOutput)
			{
				return fail("A mesh stage requires a bounded triangle mesh output or default nonmesh metadata.");
			}

			if ((bMesh || bTask) && ((features.Features & static_cast<uint64>(EShaderFeature::Mesh)) == 0 || workgroup > 128))
			{
				return fail("Mesh and task entries require the Mesh feature and at most 128 invocations.");
			}

			if (((features.Features & static_cast<uint64>(EShaderFeature::Task)) != 0) != bTask)
			{
				return fail("Task feature requirement disagrees with the actual entry stage.");
			}

			ShaderPayloadCanonicalizer payload(variant.TaskPayload, variant.TaskPayload.Types.GetResource());
			Result<void> validated = payload.Validate(limits);
			if (!validated)
			{
				return validated;
			}

			const EShaderTaskPayloadProof proof = variant.TaskPayload.Proof;
			if ((variant.TaskPayload.RootType != NO_PAYLOAD_TYPE && proof != EShaderTaskPayloadProof::Structural) ||
				((variant.TaskPayload.RootType != NO_PAYLOAD_TYPE || proof != EShaderTaskPayloadProof::None) && (!(bTask || bMesh) || (variant.Target.Target == EShaderTarget::SpirV && variant.TaskPayload.bHasProvenByteSize))))
			{
				return fail("Task payload stage or target size proof is unrepresented.");
			}

			totalBytes += (32) + 24 * variant.TaskPayload.Types.Size();
			for (const ShaderPayloadType& type : variant.TaskPayload.Types)
			{
				totalBytes += 4 * type.MemberTypes.Size();
			}
		}

		if (variant.StageInputs.Size() + variant.StageOutputs.Size() > limits.MaximumStageIO || variant.PushConstants.Size() > limits.MaximumPushConstantRanges ||
			variant.SpecializationParameters.Size() > limits.MaximumSpecializationParameters)
		{
			return fail("Extended shader metadata exceeds its record limits.");
		}

		if ((entry.Stage == EShaderStage::Compute || bTask) && (!variant.StageInputs.IsEmpty() || !variant.StageOutputs.IsEmpty()))
		{
			return fail("Compute entry declares raster stage I/O.");
		}

		if (bMesh && !variant.StageInputs.IsEmpty())
		{
			return fail("Mesh payload and invocation inputs are not raster varyings.");
		}

		for (const Array<ShaderStageIO>* table : {&variant.StageInputs, &variant.StageOutputs})
		{
			const bool bInput = table == &variant.StageInputs;
			for (usize index = 0; index < table->Size(); ++index)
			{
				const ShaderStageIO& io = (*table)[index];
				totalBytes += 52 + io.Name.Size() + io.Semantic.Size();
				if (bRasterContracts)
				{
					totalBytes += 4;
					if (io.Rate > EShaderStageIORate::PerPrimitive || ((bMesh || io.Rate == EShaderStageIORate::PerPrimitive) && (io.ScalarWidth != 32 || io.ArrayCount != 1)) ||
					    (bMesh && (bInput || io.Rate == EShaderStageIORate::Unspecified || (io.Builtin == EShaderBuiltin::Position && io.Rate != EShaderStageIORate::PerVertex))) ||
					    (!bMesh && !(entry.Stage == EShaderStage::Pixel && bInput) && io.Rate != EShaderStageIORate::Unspecified) ||
					    (entry.Stage == EShaderStage::Pixel && bInput && (variant.Target.Target == EShaderTarget::Dxil ? io.Rate != EShaderStageIORate::Unspecified : io.Rate == EShaderStageIORate::Unspecified)))
					{
						return fail("Stage I/O requires a rate and scalar representation supported by its stage.");
					}
				}

				if (!validName(io.Name, limits) || io.Builtin > EShaderBuiltin::DrawIndex || io.Scalar == EShaderScalar::None || io.Scalar > EShaderScalar::Float ||
					(io.ScalarWidth != 16 && io.ScalarWidth != 32 && io.ScalarWidth != 64) ||
					io.VectorWidth == 0 ||
					io.VectorWidth > 4 ||
					io.ArrayCount == 0 ||
					io.ArrayCount > 4096 ||
					io.Component > 3 ||
					io.VectorWidth > 4 - io.Component ||
					io.Interpolation > EShaderInterpolation::NoPerspective ||
					(io.bSample && io.bCentroid) ||
					((io.bSample || io.bCentroid) && io.Interpolation != EShaderInterpolation::Linear && io.Interpolation != EShaderInterpolation::NoPerspective))
				{
					return fail("Invalid raster stage I/O representation.");
				}

				if (io.ScalarWidth == 64)
				{
					return R::Failure(MakeArtifactError("64-bit raster location packing is not represented by this profile.", EErrorCode::Unsupported));
				}

				if (io.Scalar == EShaderScalar::Bool && io.ScalarWidth != 32)
				{
					return fail("Boolean stage I/O requires an explicit 32-bit representation.");
				}

				if (io.ScalarWidth == 16 && (io.Scalar != EShaderScalar::Float || (features.Features & 1) == 0))
				{
					return fail("16-bit stage I/O requires the declared Float16 feature.");
				}

				if (bInput && entry.Stage == EShaderStage::Pixel && io.Builtin == EShaderBuiltin::None && io.Scalar != EShaderScalar::Float &&
					io.Interpolation != EShaderInterpolation::Flat)
				{
					return fail("Integer and boolean pixel varyings require flat interpolation.");
				}

				if (io.Builtin == EShaderBuiltin::None)
				{
					bool bValidLocation;
					if (variant.Target.Target == EShaderTarget::SpirV)
					{
						bValidLocation = io.Location != NO_STAGE_LOCATION && io.Location <= 4095 &&
							io.ArrayCount <= 4096 - io.Location && io.Semantic.IsEmpty() && io.SemanticIndex == 0;
					}
					else
					{
						bValidLocation = validName(io.Semantic, limits);
					}

					if (!bValidLocation)
					{
						return fail("Stage I/O location does not match the target namespace.");
					}

					if (variant.Target.Target == EShaderTarget::Dxil)
					{
						if (io.SemanticIndex > 4095 || io.ArrayCount > 4096 - io.SemanticIndex)
						{
							return fail("D3D semantic array exceeds its bounded index domain.");
						}

						for (char character : io.Semantic.View())
						{
							if ((character < 'A' || character > 'Z') && (character < '0' || character > '9') && character != '_')
							{
								return fail("D3D semantics require canonical uppercase ASCII spelling.");
							}
						}

						if (io.Semantic.View().StartsWith("SV_") && (io.Semantic != "SV_TARGET" || entry.Stage != EShaderStage::Pixel || bInput))
						{
							return fail("System semantics require their represented builtin contract.");
						}
					}
				}
				else if (!io.Semantic.IsEmpty() || io.SemanticIndex != 0 || (variant.Target.Target == EShaderTarget::SpirV && (io.Location != NO_STAGE_LOCATION || io.Component != 0)))
				{
					return fail("Builtin stage I/O also declares a user location.");
				}

				if (variant.Target.Target == EShaderTarget::Dxil)
				{
					const bool bPacked = io.Builtin != EShaderBuiltin::FragDepth && io.Builtin != EShaderBuiltin::SampleMask && !(bInput && io.Builtin == EShaderBuiltin::SampleIndex);
					if (io.Location == NO_STAGE_LOCATION)
					{
						if (io.Component != 0 || (variant.bStageIOComplete && bPacked))
						{
							return fail("Complete DXIL stage I/O requires its allocated native signature footprint.");
						}
					}
					else if (!bPacked || io.Location >= 32 || io.ArrayCount > 32 - io.Location)
					{
						return fail("DXIL stage I/O exceeds its allocated native register domain.");
					}

					if (io.Semantic == "SV_TARGET" && (io.SemanticIndex >= 8 || io.ArrayCount != 1 || io.Component != 0 || (io.Location != NO_STAGE_LOCATION && io.Location != io.SemanticIndex)))
					{
						return fail("DXIL render-target output does not match its native register.");
					}
				}

				if (io.Builtin != EShaderBuiltin::None)
				{
					bool bAllowed = false;
					if (io.Builtin == EShaderBuiltin::Position)
					{
						bAllowed = (((entry.Stage == EShaderStage::Vertex || bMesh) && !bInput) || (entry.Stage == EShaderStage::Pixel && bInput)) && io.Scalar == EShaderScalar::Float && io.ScalarWidth == 32 && io.VectorWidth == 4;
					}
					else if (io.Builtin == EShaderBuiltin::FrontFace)
					{
						bAllowed = entry.Stage == EShaderStage::Pixel && bInput && io.Scalar == EShaderScalar::Bool && io.ScalarWidth == 32 && io.VectorWidth == 1;
					}
					else if (io.Builtin == EShaderBuiltin::FragDepth)
					{
						bAllowed = entry.Stage == EShaderStage::Pixel && !bInput && io.Scalar == EShaderScalar::Float && io.ScalarWidth == 32 && io.VectorWidth == 1;
					}
					else
					{
						bAllowed = ((entry.Stage == EShaderStage::Vertex && bInput && (io.Builtin == EShaderBuiltin::VertexIndex || io.Builtin == EShaderBuiltin::InstanceIndex ||
										 io.Builtin == EShaderBuiltin::BaseVertex ||
										 io.Builtin == EShaderBuiltin::BaseInstance ||
						                 io.Builtin == EShaderBuiltin::DrawIndex)) ||
						               (entry.Stage == EShaderStage::Pixel && bInput && (io.Builtin == EShaderBuiltin::SampleIndex || io.Builtin == EShaderBuiltin::PrimitiveIndex || io.Builtin == EShaderBuiltin::SampleMask)) ||
						               (entry.Stage == EShaderStage::Pixel && !bInput && io.Builtin == EShaderBuiltin::SampleMask) ||
						               (entry.Stage == EShaderStage::Vertex && !bInput && (io.Builtin == EShaderBuiltin::RenderTargetArrayIndex || io.Builtin == EShaderBuiltin::ViewportIndex))) &&
						           (io.Scalar == EShaderScalar::SignedInteger || io.Scalar == EShaderScalar::UnsignedInteger) && io.ScalarWidth == 32 && io.VectorWidth == 1;
					}

					if (variant.Target.Target == EShaderTarget::Dxil && io.Builtin == EShaderBuiltin::SampleIndex && io.Scalar != EShaderScalar::UnsignedInteger)
					{
						bAllowed = false;
					}

					if (!bAllowed || io.ArrayCount != 1)
					{
						return fail("Builtin representation or stage direction is unsupported.");
					}

					if ((io.Builtin == EShaderBuiltin::BaseVertex || io.Builtin == EShaderBuiltin::BaseInstance || io.Builtin == EShaderBuiltin::DrawIndex) &&
						(variant.Target.Target != EShaderTarget::SpirV || (features.Features & static_cast<uint64>(EShaderFeature::DrawParameters)) == 0))
					{
						return fail("Draw-parameter builtins require the SPIR-V DrawParameters feature.");
					}
				}

				for (usize prior = 0; prior < index; ++prior)
				{
					if (comparisonBudget == 0)
					{
						return fail("Shader metadata exceeds its comparison bound.");
					}
					--comparisonBudget;
					const ShaderStageIO& other = (*table)[prior];
					if (StageIOSlotEqual(io, other, variant.Target.Target) || io.Name == other.Name)
					{
						return fail("Duplicate stage I/O name or location.");
					}

					if (variant.Target.Target == EShaderTarget::SpirV && io.Builtin == EShaderBuiltin::None && other.Builtin == EShaderBuiltin::None &&
						uint64(io.Location) < uint64(other.Location) + other.ArrayCount &&
						uint64(other.Location) < uint64(io.Location) + io.ArrayCount &&
						io.Component < other.Component + other.VectorWidth &&
						other.Component < io.Component + io.VectorWidth)
					{
						return fail("Overlapping stage I/O components or arrays.");
					}

					if (variant.Target.Target == EShaderTarget::Dxil && io.Location != NO_STAGE_LOCATION && io.Rate == other.Rate && other.Location != NO_STAGE_LOCATION &&
						uint64(io.Location) < uint64(other.Location) + other.ArrayCount &&
						uint64(other.Location) < uint64(io.Location) + io.ArrayCount &&
						io.Component < other.Component + other.VectorWidth &&
						other.Component < io.Component + io.VectorWidth)
					{
						return fail("Overlapping DXIL native signature registers or components.");
					}

					if (variant.Target.Target == EShaderTarget::Dxil && io.Builtin == EShaderBuiltin::None && other.Builtin == EShaderBuiltin::None &&
						io.Semantic == other.Semantic &&
						uint64(io.SemanticIndex) < uint64(other.SemanticIndex) + other.ArrayCount &&
						uint64(other.SemanticIndex) < uint64(io.SemanticIndex) + io.ArrayCount)
					{
						return fail("Overlapping D3D semantic arrays.");
					}
				}
			}
		}

		uint32 pushExtent = 0;
		for (usize index = 0; index < variant.PushConstants.Size(); ++index)
		{
			const ShaderPushConstantRange& range = variant.PushConstants[index];
			if (range.DataLayout >= variant.Layouts.Size() || range.ByteSize == 0 || range.ByteOffset % 4 != 0 || range.ByteSize % 4 != 0 ||
				range.ByteOffset > limits.MaximumTotalBytes ||
			    range.ByteSize > limits.MaximumTotalBytes - range.ByteOffset ||
				range.ByteSize != variant.Layouts[range.DataLayout].ByteSize ||
				range.StageMask != (uint64(1) << static_cast<uint32>(entry.Stage)) ||
			    range.Lowering != (variant.Target.Target == EShaderTarget::SpirV ? EShaderPushConstantLowering::VulkanPushConstant : EShaderPushConstantLowering::D3D12RootConstants) ||
			    (variant.Target.Target == EShaderTarget::SpirV && (range.Space != 0 || range.Binding != 0)))
			{
				return fail("Invalid target push-constant range.");
			}

			for (usize prior = 0; prior < index; ++prior)
			{
				if (comparisonBudget == 0)
				{
					return fail("Shader metadata exceeds its comparison bound.");
				}
				--comparisonBudget;
				const ShaderPushConstantRange& other = variant.PushConstants[prior];
				if (other.Space == range.Space && other.Binding == range.Binding && uint64(range.ByteOffset) < uint64(other.ByteOffset) + other.ByteSize &&
					uint64(other.ByteOffset) < uint64(range.ByteOffset) + range.ByteSize)
				{
					return fail("Overlapping push-constant ranges.");
				}
			}

			for (const ShaderBinding& binding : variant.Bindings)
			{
				if (comparisonBudget == 0)
				{
					return fail("Shader metadata exceeds its comparison bound.");
				}
				--comparisonBudget;
				if (range.Lowering == EShaderPushConstantLowering::D3D12RootConstants && binding.RegisterNamespace == EShaderRegisterClass::ConstantBuffer && binding.Space == range.Space && range.Binding >= binding.Binding &&
				    uint64(range.Binding) < uint64(binding.Binding) + (binding.CountMode == EShaderDescriptorCountMode::Runtime ? uint64{0x100000000} - binding.Binding : binding.ArrayCount))
				{
					return fail("Root constants collide with a constant-buffer register.");
				}
			}

			pushExtent = Max(pushExtent, range.ByteOffset + range.ByteSize);
		}

		if (features.MinimumPushConstantBytes != pushExtent)
		{
			return fail("Push-constant byte requirement disagrees with represented ranges.");
		}

		for (usize index = 0; index < variant.SpecializationParameters.Size(); ++index)
		{
			const ShaderSpecializationParameter& p = variant.SpecializationParameters[index];
			if (p.ScalarWidth != 32 && p.ScalarWidth != 64)
			{
				return R::Failure(MakeArtifactError("This specialization profile supports exact 32-bit and 64-bit scalar values.", EErrorCode::Unsupported));
			}

			totalBytes += 60 + p.Name.Size();
			if (!validName(p.Name, limits) || p.Phase > EShaderSpecializationPhase::PipelineTime || !bitsValid(p, 0) || (!p.bHasDefault && p.DefaultBits != 0) ||
				(!p.bHasRange && (p.MinimumBits != 0 || p.MaximumBits != 0)) ||
				(p.bHasRange && (!bitsValid(p, p.MinimumBits) || !bitsValid(p, p.MaximumBits) || !lessEqual(p, p.MinimumBits, p.MaximumBits))) ||
				(p.bHasDefault && !SpecializationValueValid(p, p.DefaultBits)) ||
				(p.WorkgroupAxis != NO_STAGE_LOCATION && (p.WorkgroupAxis > 2 || entry.Stage != EShaderStage::Compute || p.Scalar != EShaderScalar::UnsignedInteger || p.ScalarWidth != 32)))
			{
				return fail("Invalid typed specialization parameter or value domain.");
			}

			if ((p.Scalar == EShaderScalar::Float && p.ScalarWidth == 64 && (features.Features & 2) == 0) ||
				((p.Scalar == EShaderScalar::SignedInteger || p.Scalar == EShaderScalar::UnsignedInteger) && p.ScalarWidth == 64 && (features.Features & 4) == 0))
			{
				return fail("Specialization scalar width omits its required numeric feature.");
			}

			for (usize prior = 0; prior < index; ++prior)
			{
				if (comparisonBudget == 0)
				{
					return fail("Shader metadata exceeds its comparison bound.");
				}
				--comparisonBudget;
				const ShaderSpecializationParameter& other = variant.SpecializationParameters[prior];
				if (p.ID == other.ID || p.Name == other.Name || (p.WorkgroupAxis != NO_STAGE_LOCATION && p.WorkgroupAxis == other.WorkgroupAxis))
				{
					return fail("Duplicate specialization ID, name or workgroup axis.");
				}
			}
		}

		totalBytes += 48 + 32 * variant.PushConstants.Size();
		if (totalBytes > limits.MaximumTotalBytes)
		{
			return fail("Extended shader metadata exceeds its byte limit.");
		}

		return R::Success();
	}

	Result<void> CanonicalizeVariantMetadata(ShaderVariant& variant, MemoryResource& resource, const Array<uint32>& layoutMap)
	{
		if (variant.TaskPayload.RootType != NO_PAYLOAD_TYPE)
		{
			ShaderPayloadCanonicalizer payload(variant.TaskPayload, variant.TaskPayload.Types.GetResource());
			Result<ShaderTaskPayloadSignature> canonical = payload.Canonicalize();
			if (!canonical)
			{
				return Result<void>::Failure(Move(canonical.GetError()));
			}

			variant.TaskPayload = Move(canonical).TakeValue();
		}

		for (ShaderPushConstantRange& range : variant.PushConstants)
		{
			range.DataLayout = layoutMap[range.DataLayout];
		}

		Result<void> ray = CanonicalizeRayInterface(variant.RayInterface);
		if (!ray)
		{
			return ray;
		}

		if (!reorder(variant.StageInputs, resource, ioLess) || !reorder(variant.StageOutputs, resource, ioLess) || !reorder(variant.PushConstants, resource, pushLess) ||
			!reorder(variant.SpecializationParameters, resource, specLess))
		{
			return Result<void>::Failure(AllocationError());
		}

		return Result<void>::Success();
	}

	bool MetadataIsCanonical(const ShaderVariant& variant) noexcept
	{
		auto sorted = [](const auto& values, auto less) { return values.Size() < 2 || IsSorted(values.begin(), values.end(), less); };

		return IsCanonicalRayInterface(variant.RayInterface) && ShaderPayloadCanonicalizer::IsCanonical(variant.TaskPayload) && sorted(variant.StageInputs, ioLess) && sorted(variant.StageOutputs, ioLess) &&
		       sorted(variant.PushConstants, pushLess) && sorted(variant.SpecializationParameters, specLess);
	}

	Result<void> WriteVariantMetadata(
	    Writer& writer, const ShaderVariant& variant, MemoryResource& resource, const Array<uint32>* layoutMap, [[maybe_unused]] uint32 abi, const Array<uint32>* bindingMap)
	{
		const ShaderFeatureRequirements& f = variant.Requirements;
		writer.U32(f.Version);
		writer.U64(f.Features);
		writer.U32(f.MinimumSubgroupSize);
		writer.U32(f.MaximumSubgroupSize);
		writer.U32(f.MinimumPushConstantBytes);
		writer.U32(f.MinimumWorkgroupInvocations);
		writer.U32(variant.bStageIOComplete ? 1 : 0);
		auto writeTable = [&](const auto& values, auto less, auto write)
		{
			writer.U32(static_cast<uint32>(values.Size()));
			if (layoutMap == nullptr)
			{
				for (const auto& value : values)
				{
					write(value);
				}

				return true;
			}

			Array<uint32> order(resource);
			if (!OrderIndices(values, order, less))
			{
				return false;
			}

			for (uint32 index : order)
			{
				write(values[index]);
			}

			return true;
		};
		if (!writeTable(variant.StageInputs, ioLess, [&](const ShaderStageIO& io) { writeIO(writer, io, abi); }) ||
			!writeTable(variant.StageOutputs, ioLess, [&](const ShaderStageIO& io) { writeIO(writer, io, abi); }) ||
		    !writeTable(variant.PushConstants, pushLess, [&](const ShaderPushConstantRange& range)
		        {
			        writer.U32(range.ByteOffset);
			        writer.U32(range.ByteSize);
			        writer.U32(layoutMap == nullptr ? range.DataLayout : (*layoutMap)[range.DataLayout]);
			        writer.U64(range.StageMask);
			        writer.Enum(range.Lowering);
			        writer.U32(range.Space);
			        writer.U32(range.Binding);
		        }) ||
		    !writeTable(variant.SpecializationParameters, specLess, [&](const ShaderSpecializationParameter& p) { writeSpec(writer, p); }))
		{
			return Result<void>::Failure(AllocationError());
		}

		{
			writer.Enum(variant.MeshOutput.Topology);
			writer.U32(variant.MeshOutput.MaximumVertices);
			writer.U32(variant.MeshOutput.MaximumPrimitives);
			ShaderTaskPayloadSignature canonical(resource);
			const ShaderTaskPayloadSignature* payload = &variant.TaskPayload;
			if (layoutMap != nullptr)
			{
				ShaderPayloadCanonicalizer order(variant.TaskPayload, resource);
				Result<ShaderTaskPayloadSignature> ordered = order.Canonicalize();
				if (!ordered)
				{
					return Result<void>::Failure(Move(ordered.GetError()));
				}

				canonical = Move(ordered).TakeValue();
				payload = &canonical;
			}
			{
				writer.Enum(payload->Proof);
			}

			writer.U32(payload->RootType);
			writer.U32(payload->bHasProvenByteSize ? 1 : 0);
			writer.U32(payload->ProvenByteSize);
			writer.U32(static_cast<uint32>(payload->Types.Size()));
			for (const ShaderPayloadType& type : payload->Types)
			{
				writer.Enum(type.Kind);
				writer.Enum(type.Scalar);
				writer.U32(type.ScalarWidth);
				writer.U32(type.Count);
				writer.U32(type.ElementType);
				writer.U32(static_cast<uint32>(type.MemberTypes.Size()));
				for (uint32 member : type.MemberTypes)
				{
					writer.U32(member);
				}
			}
		}
		{
			Result<void> ray = WriteRayInterface(writer, variant.RayInterface, resource, layoutMap == nullptr);
			if (!ray)
			{
				return ray;
			}
		}
		{
			writer.Enum(variant.LocalRecord.Lowering);
			writer.U32(variant.LocalRecord.DataLayout == NO_DATA_LAYOUT || layoutMap == nullptr ? variant.LocalRecord.DataLayout : (*layoutMap)[variant.LocalRecord.DataLayout]);
			writer.U32(variant.LocalRecord.ByteSize);
			writer.U32(variant.LocalRecord.Alignment);
		}
		{
			return WriteLocalRootMetadata(writer, variant, bindingMap);
		}
	}

	Result<void> ReadVariantMetadata( Reader& reader, ShaderVariant& variant, const ShaderLimits& limits, uint64& totalStageIO, uint64& totalPushConstants,
		uint64& totalSpecializationParameters,
	    [[maybe_unused]] uint32 abi,
		uint64& totalPayloadTypes,
		uint64& totalPayloadMembers,
		uint64& totalRayDataSignatures)
	{
		using R = Result<void>;
		auto fail = [&]() { return R::Failure(reader.HasAllocationFailure() ? AllocationError() : MakeArtifactError("Malformed extended shader metadata.")); };
		ShaderFeatureRequirements& f = variant.Requirements;
		f.Version = reader.U32();
		f.Features = reader.U64();
		f.MinimumSubgroupSize = reader.U32();
		f.MaximumSubgroupSize = reader.U32();
		f.MinimumPushConstantBytes = reader.U32();
		f.MinimumWorkgroupInvocations = reader.U32();
		const uint32 complete = reader.U32();
		if (complete > 1)
		{
			return fail();
		}

		variant.bStageIOComplete = complete != 0;
		for (Array<ShaderStageIO>* table : {&variant.StageInputs, &variant.StageOutputs})
		{
			const uint32 count = reader.U32();
			if (!reader.Count(count, totalStageIO, limits.MaximumStageIO, 56))
			{
				return fail();
			}

			if (!table->Reserve(count))
			{
				return R::Failure(AllocationError());
			}

			for (uint32 index = 0; index < count; ++index)
			{
				ShaderStageIO io(table->GetResource());
				reader.Text(io.Name, limits.MaximumNameBytes);
				reader.Text(io.Semantic, limits.MaximumNameBytes);
				io.SemanticIndex = reader.U32();
				io.Location = reader.U32();
				io.Component = reader.U32();
				io.Builtin = reader.Enum<EShaderBuiltin>();
				io.Scalar = reader.Enum<EShaderScalar>();
				io.ScalarWidth = reader.U32();
				io.VectorWidth = reader.U32();
				io.ArrayCount = reader.U32();
				io.Interpolation = reader.Enum<EShaderInterpolation>();
				const uint32 centroid = reader.U32();
				const uint32 sample = reader.U32();
				{
					io.Rate = reader.Enum<EShaderStageIORate>();
				}

				if (!reader.IsValid() || centroid > 1 || sample > 1)
				{
					return fail();
				}

				io.bCentroid = centroid != 0;
				io.bSample = sample != 0;
				if (!table->Add(Move(io)))
				{
					return R::Failure(AllocationError());
				}
			}
		}

		const uint32 pushCount = reader.U32();
		if (!reader.Count(pushCount, totalPushConstants, limits.MaximumPushConstantRanges, 32))
		{
			return fail();
		}

		if (!variant.PushConstants.Reserve(pushCount))
		{
			return R::Failure(AllocationError());
		}

		for (uint32 index = 0; index < pushCount; ++index)
		{
			ShaderPushConstantRange range;
			range.ByteOffset = reader.U32();
			range.ByteSize = reader.U32();
			range.DataLayout = reader.U32();
			range.StageMask = reader.U64();
			range.Lowering = reader.Enum<EShaderPushConstantLowering>();
			range.Space = reader.U32();
			range.Binding = reader.U32();
			if (!reader.IsValid())
			{
				return fail();
			}

			if (!variant.PushConstants.Add(range))
			{
				return R::Failure(AllocationError());
			}
		}

		const uint32 specCount = reader.U32();
		if (!reader.Count(specCount, totalSpecializationParameters, limits.MaximumSpecializationParameters, 60))
		{
			return fail();
		}

		if (!variant.SpecializationParameters.Reserve(specCount))
		{
			return R::Failure(AllocationError());
		}

		for (uint32 index = 0; index < specCount; ++index)
		{
			ShaderSpecializationParameter p(variant.SpecializationParameters.GetResource());
			p.ID = reader.U32();
			reader.Text(p.Name, limits.MaximumNameBytes);
			p.Scalar = reader.Enum<EShaderScalar>();
			p.ScalarWidth = reader.U32();
			p.Phase = reader.Enum<EShaderSpecializationPhase>();
			p.DefaultBits = reader.U64();
			p.MinimumBits = reader.U64();
			p.MaximumBits = reader.U64();
			const uint32 hasDefault = reader.U32();
			const uint32 hasRange = reader.U32();
			const uint32 affectsLayout = reader.U32();
			p.WorkgroupAxis = reader.U32();
			if (!reader.IsValid() || hasDefault > 1 || hasRange > 1 || affectsLayout > 1)
			{
				return fail();
			}

			p.bHasDefault = hasDefault != 0;
			p.bHasRange = hasRange != 0;
			p.bAffectsLayout = affectsLayout != 0;
			if (!variant.SpecializationParameters.Add(Move(p)))
			{
				return R::Failure(AllocationError());
			}
		}
		{
			variant.MeshOutput.Topology = reader.Enum<EShaderMeshTopology>();
			variant.MeshOutput.MaximumVertices = reader.U32();
			variant.MeshOutput.MaximumPrimitives = reader.U32();
			ShaderTaskPayloadSignature& payload = variant.TaskPayload;
			{
				payload.Proof = reader.Enum<EShaderTaskPayloadProof>();
				if (payload.Proof > EShaderTaskPayloadProof::NativeSize)
				{
					return fail();
				}
			}

			payload.RootType = reader.U32();

			const uint32 encodedSizeProof = reader.U32();
			payload.ProvenByteSize = reader.U32();
			const uint32 typeCount = reader.U32();
			if (encodedSizeProof > 1 || typeCount > 128 ||
				(payload.Proof == EShaderTaskPayloadProof::NativeSize && (payload.RootType != NO_PAYLOAD_TYPE || typeCount != 0 || encodedSizeProof != 1 || payload.ProvenByteSize == 0 || payload.ProvenByteSize > 16384)) ||
				!reader.Count(typeCount, totalPayloadTypes, limits.MaximumPayloadTypes, 24))
			{
				return fail();
			}

			payload.bHasProvenByteSize = encodedSizeProof != 0;
			if (!payload.Types.Reserve(typeCount))
			{
				return R::Failure(AllocationError());
			}

			for (uint32 index = 0; index < typeCount; ++index)
			{
				ShaderPayloadType type(payload.Types.GetResource());
				type.Kind = reader.Enum<EShaderPayloadTypeKind>();
				type.Scalar = reader.Enum<EShaderScalar>();
				type.ScalarWidth = reader.U32();
				type.Count = reader.U32();
				type.ElementType = reader.U32();
				const uint32 memberCount = reader.U32();
				if (memberCount > 32 || !reader.Count(memberCount, totalPayloadMembers, limits.MaximumPayloadMembers, 4))
				{
					return fail();
				}

				if (!type.MemberTypes.Reserve(memberCount))
				{
					return R::Failure(AllocationError());
				}

				for (uint32 member = 0; member < memberCount; ++member)
				{
					type.MemberTypes.AddRequired(reader.U32());
				}

				payload.Types.AddRequired(Move(type));
			}
		}
		{
			Result<void> ray = ReadRayInterface(reader, variant.RayInterface, limits, totalRayDataSignatures, totalPayloadTypes, totalPayloadMembers);
			if (!ray)
			{
				return ray;
			}
		}
		{
			variant.LocalRecord.Lowering = reader.Enum<EShaderLocalRecordLowering>();
			variant.LocalRecord.DataLayout = reader.U32();
			variant.LocalRecord.ByteSize = reader.U32();
			variant.LocalRecord.Alignment = reader.U32();
		}
		{
			return ReadLocalRootMetadata(reader, variant, limits);
		}
	}
} // namespace rox::shader
