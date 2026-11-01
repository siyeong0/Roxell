#include <ShaderLayout.h>

#include "ShaderMetadata.h"
#include "ShaderPayloadCanonicalizer.h"
#include "ShaderRayMetadata.h"
#include "ShaderWire.hpp"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <Comparison.hpp>
#include <HashMap.hpp>
#include <Shader.h>
#include <ShaderBuilder.h>
#include <Types.h>
#include <Utility.hpp>
#include <Vocabulary.hpp>

namespace rox::shader
{
	using namespace core;

	struct ShaderLayout::Storage
	{
		Array<ShaderEntrySelection> Selections;

		Array<ShaderProgramBinding> Bindings;
		Array<uint32> ParameterOrder;

		ShaderTargetProfile Target;

		Hash256 PhysicalLayoutHash;
		Hash256 ProgramInterfaceHash;
		Hash256 SelectedCodeHash;

		uint64 RequiredCapabilities = 0;
		ShaderFeatureRequirements Requirements;

		Array<ShaderProgramPushConstant> PushConstants;
		Array<ShaderSpecializationParameter> SpecializationParameters;

		EShaderStageIOStatus StageIOStatus = EShaderStageIOStatus::Unverified;
		EShaderTaskPayloadCompatibility TaskPayloadCompatibility = EShaderTaskPayloadCompatibility::Absent;

		explicit Storage(MemoryResource& resource) noexcept
			: Selections(resource)
			, Bindings(resource)
			, ParameterOrder(resource)
			, PushConstants(resource)
			, SpecializationParameters(resource)
		{
		}
	};

	namespace
	{
		struct LayoutKeys
		{
			Array<Hash256> Physical;
			Array<Hash256> Logical;
			Array<uint8> Complete;

			explicit LayoutKeys(MemoryResource& resource) noexcept
				: Physical(resource)
				, Logical(resource)
				, Complete(resource)
			{
			}
		};

		Result<void> hashLayoutRecursive(const ShaderVariant& variant, uint32 index, LayoutKeys& keys, MemoryResource& resource)
		{
			if (keys.Complete[index] != 0)
			{
				return Result<void>::Success();
			}

			const ShaderDataLayout& layout = variant.Layouts[index];
			Writer physical(resource, true);
			Writer logical(resource, true);
			for (Writer* writer : {&physical, &logical})
			{
				writer->U64(layout.ByteSize);
				writer->U32(layout.Alignment);
				writer->U64(layout.ElementStride);
				writer->U32(static_cast<uint32>(layout.Fields.Size()));
			}

			for (const ShaderDataField& field : layout.Fields)
			{
				logical.Text(field.Name);
				for (Writer* writer : {&physical, &logical})
				{
					writer->U64(field.ByteOffset);
					writer->U64(field.ByteSize);
					writer->Enum(field.Scalar);
					writer->U32(field.ScalarWidth);
					writer->U32(field.VectorWidth);
					writer->U32(field.MatrixRows);
					writer->U32(field.MatrixColumns);
					writer->Enum(field.MatrixOrder);
					writer->U64(field.MatrixStride);
					writer->U32(field.ArrayCount);
					writer->U64(field.ArrayStride);
					writer->U32(field.NestedLayout == NO_DATA_LAYOUT ? 0 : 1);
				}

				if (field.NestedLayout != NO_DATA_LAYOUT)
				{
					Result<void> child = hashLayoutRecursive(variant, field.NestedLayout, keys, resource);
					if (!child)
					{
						return child;
					}

					physical.Hash(keys.Physical[field.NestedLayout]);
					logical.Hash(keys.Logical[field.NestedLayout]);
				}
			}

			if (!physical.IsValid() || !logical.IsValid())
			{
				return Result<void>::Failure(AllocationError());
			}

			keys.Physical[index] = physical.GetHash();
			keys.Logical[index] = logical.GetHash();
			keys.Complete[index] = 1;

			return Result<void>::Success();
		}

		Result<bool> equalLayoutsRecursive(
		    const ShaderVariant& left, uint32 leftIndex, const ShaderVariant& right, uint32 rightIndex, bool bNames, bool bOffsets, HashMap<uint64, uint8>& visited, uint64& budget)
		{
			using R = Result<bool>;
			if (leftIndex == NO_DATA_LAYOUT || rightIndex == NO_DATA_LAYOUT)
			{
				return R::Success(leftIndex == rightIndex);
			}

			const uint64 pair = (uint64(leftIndex) << 32) | rightIndex;
			if (visited.Contains(pair))
			{
				return R::Success(true);
			}

			if (budget == 0)
			{
				return R::Failure(MakeArtifactError("Shader interface layout comparison exceeds its bound."));
			}
			--budget;
			if (!visited.Reserve(visited.Size() + 1))
			{
				return R::Failure(AllocationError());
			}

			visited.EmplaceRequired(pair, uint8(1));
			const ShaderDataLayout& first = left.Layouts[leftIndex];
			const ShaderDataLayout& second = right.Layouts[rightIndex];
			if (first.Fields.Size() != second.Fields.Size() ||
				(bOffsets && (first.ByteSize != second.ByteSize || first.Alignment != second.Alignment || first.ElementStride != second.ElementStride)))
			{
				return R::Success(false);
			}

			Array<uint32> nameOrder(visited.GetResource());
			if (bNames && !bOffsets && !OrderIndices(second.Fields, nameOrder, [](const ShaderDataField& leftField, const ShaderDataField& rightField) { return leftField.Name.View() < rightField.Name.View(); }))
			{
				return R::Failure(AllocationError());
			}

			for (usize index = 0; index < first.Fields.Size(); ++index)
			{
				if (budget == 0)
				{
					return R::Failure(MakeArtifactError("Shader interface layout comparison exceeds its bound."));
				}
				--budget;
				const ShaderDataField& field = first.Fields[index];
				uint32 otherIndex = static_cast<uint32>(index);
				if (bNames && !bOffsets)
				{
					uint32* const found = LowerBound(nameOrder.begin(), nameOrder.end(), field.Name.View(), [&](uint32 fieldIndex, StringView name) { return second.Fields[fieldIndex].Name.View() < name; });
					if (found == nameOrder.end() || second.Fields[*found].Name != field.Name)
					{
						return R::Success(false);
					}

					otherIndex = *found;
				}

				const ShaderDataField& other = second.Fields[otherIndex];
				if ((bNames && field.Name != other.Name) ||
					field.Scalar != other.Scalar ||
					field.ScalarWidth != other.ScalarWidth ||
					field.VectorWidth != other.VectorWidth ||
					field.MatrixRows != other.MatrixRows ||
					field.MatrixColumns != other.MatrixColumns ||
					field.ArrayCount != other.ArrayCount ||
					((field.MatrixOrder == EShaderMatrixOrder::None) != (other.MatrixOrder == EShaderMatrixOrder::None)) ||
					(bOffsets &&
						(field.ByteOffset != other.ByteOffset ||
						 field.ByteSize != other.ByteSize ||
						 field.MatrixOrder != other.MatrixOrder ||
						 field.MatrixStride != other.MatrixStride ||
						 field.ArrayStride != other.ArrayStride)))
				{
					return R::Success(false);
				}

				Result<bool> child = equalLayoutsRecursive(left, field.NestedLayout, right, other.NestedLayout, bNames, bOffsets, visited, budget);
				if (!child || !child.Value())
				{
					return child;
				}
			}

			return R::Success(true);
		}

		bool bindingShapeEqual(const ShaderBinding& left, const ShaderBinding& right)
		{
			return left.Kind == right.Kind &&
				   left.CountMode == right.CountMode &&
				   left.ArrayCount == right.ArrayCount &&
				   left.TextureShape == right.TextureShape &&
				   left.bTextureArray == right.bTextureArray &&
				   left.bMultisampled == right.bMultisampled;
		}

		bool bindingSlotEqual(const ShaderBinding& left, const ShaderBinding& right)
		{
			return left.Space == right.Space && left.RegisterNamespace == right.RegisterNamespace && left.Binding == right.Binding;
		}

		Result<bool> localRecordsEqual(const ShaderVariant& left, const ShaderVariant& right, MemoryResource& resource, uint64& budget)
		{
			using R = Result<bool>;
			if (left.LocalRecord.Lowering != right.LocalRecord.Lowering || left.LocalRecord.ByteSize != right.LocalRecord.ByteSize ||
				left.LocalRecord.Alignment != right.LocalRecord.Alignment ||
				left.LocalRootArguments.Size() != right.LocalRootArguments.Size())
			{
				return R::Success(false);
			}

			HashMap<uint64, uint8> visited(resource);
			Result<bool> record = equalLayoutsRecursive(left, left.LocalRecord.DataLayout, right, right.LocalRecord.DataLayout, false, true, visited, budget);
			if (!record || !record.Value())
			{
				return record;
			}

			for (usize index = 0; index < left.LocalRootArguments.Size(); ++index)
			{
				if (budget == 0)
				{
					return R::Failure(MakeArtifactError("Local record comparison exceeds its bound."));
				}
				--budget;
				const ShaderLocalRootArgument& first = left.LocalRootArguments[index];
				const ShaderLocalRootArgument& second = right.LocalRootArguments[index];
				const ShaderBinding& firstBinding = left.Bindings[first.BindingIndex];
				const ShaderBinding& secondBinding = right.Bindings[second.BindingIndex];
				if (first.Kind != second.Kind || first.ByteOffset != second.ByteOffset || first.ValueCount != second.ValueCount ||
					!bindingShapeEqual(firstBinding, secondBinding) ||
					!bindingSlotEqual(firstBinding, secondBinding) ||
					firstBinding.Scope != secondBinding.Scope ||
					firstBinding.Access != secondBinding.Access)
				{
					return R::Success(false);
				}

				Result<bool> layout = equalLayoutsRecursive(left, firstBinding.DataLayout, right, secondBinding.DataLayout, false, true, visited, budget);
				if (!layout || !layout.Value())
				{
					return layout;
				}
			}

			return R::Success(true);
		}

		bool bindingLess(const ShaderProgramBinding& left, const ShaderProgramBinding& right)
		{
			if (left.Binding.Space != right.Binding.Space)
			{
				return left.Binding.Space < right.Binding.Space;
			}

			if (left.Binding.RegisterNamespace != right.Binding.RegisterNamespace)
			{
				return left.Binding.RegisterNamespace < right.Binding.RegisterNamespace;
			}

			return left.Binding.Binding < right.Binding.Binding;
		}

		void writeFeatureRequirements(Writer& writer, const ShaderFeatureRequirements& requirements)
		{
			writer.U32(requirements.Version);
			writer.U64(requirements.Features);
			writer.U32(requirements.MinimumSubgroupSize);
			writer.U32(requirements.MaximumSubgroupSize);
			writer.U32(requirements.MinimumPushConstantBytes);
			writer.U32(requirements.MinimumWorkgroupInvocations);
		}

		void writeStageIO(Writer& writer, const Array<ShaderStageIO>& table, bool bRasterContracts)
		{
			writer.U32(static_cast<uint32>(table.Size()));
			for (const ShaderStageIO& io : table)
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
				if (bRasterContracts)
				{
					writer.Enum(io.Rate);
				}
			}
		}

		bool rayInterfacesEqual(const ShaderRayInterface& left, const ShaderRayInterface& right)
		{
			if (left.DeclaredLimits != right.DeclaredLimits || left.bOutgoingDataComplete != right.bOutgoingDataComplete || left.Data.Size() != right.Data.Size())
			{
				return false;
			}

			for (usize index = 0; index < left.Data.Size(); ++index)
			{
				const ShaderRayDataSignature& first = left.Data[index];
				const ShaderRayDataSignature& second = right.Data[index];
				if (first.Kind != second.Kind || first.Direction != second.Direction || first.Location != second.Location || first.RootType != second.RootType ||
					first.bHasProvenByteSize != second.bHasProvenByteSize ||
					first.ProvenByteSize != second.ProvenByteSize ||
					first.Types.Size() != second.Types.Size())
				{
					return false;
				}

				for (usize typeIndex = 0; typeIndex < first.Types.Size(); ++typeIndex)
				{
					const ShaderPayloadType& firstType = first.Types[typeIndex];
					const ShaderPayloadType& secondType = second.Types[typeIndex];
					if (firstType.Kind != secondType.Kind || firstType.Scalar != secondType.Scalar || firstType.ScalarWidth != secondType.ScalarWidth ||
						firstType.Count != secondType.Count ||
						firstType.ElementType != secondType.ElementType ||
						firstType.MemberTypes.Size() != secondType.MemberTypes.Size())
					{
						return false;
					}

					for (usize member = 0; member < firstType.MemberTypes.Size(); ++member)
					{
						if (firstType.MemberTypes[member] != secondType.MemberTypes[member])
						{
							return false;
						}
					}
				}
			}

			return true;
		}

		bool rasterizerSupplies(EShaderBuiltin builtin)
		{
			return builtin == EShaderBuiltin::FrontFace || builtin == EShaderBuiltin::SampleIndex || builtin == EShaderBuiltin::PrimitiveIndex || builtin == EShaderBuiltin::SampleMask;
		}
	} // namespace

	bool ShaderLayout::IsValid() const noexcept
	{
		return static_cast<bool>(m_Storage);
	}

	Span<const ShaderEntrySelection> ShaderLayout::GetSelections() const noexcept
	{
		ASSERT(IsValid(), "Shader program interface must be valid.");
		return m_Storage->Selections.View();
	}

	Span<const ShaderProgramBinding> ShaderLayout::GetBindings() const noexcept
	{
		ASSERT(IsValid(), "Shader program interface must be valid.");
		return m_Storage->Bindings.View();
	}

	ShaderTargetProfile ShaderLayout::GetTarget() const noexcept
	{
		ASSERT(IsValid(), "Shader program interface must be valid.");
		return m_Storage->Target;
	}

	Hash256 ShaderLayout::GetPhysicalLayoutHash() const noexcept
	{
		ASSERT(IsValid(), "Shader program interface must be valid.");
		return m_Storage->PhysicalLayoutHash;
	}

	Hash256 ShaderLayout::GetLayoutHash() const noexcept
	{
		ASSERT(IsValid(), "Shader program interface must be valid.");
		return m_Storage->ProgramInterfaceHash;
	}

	Hash256 ShaderLayout::GetSelectedCodeHash() const noexcept
	{
		ASSERT(IsValid(), "Shader program interface must be valid.");
		return m_Storage->SelectedCodeHash;
	}

	uint64 ShaderLayout::GetRequiredCapabilities() const noexcept
	{
		ASSERT(IsValid(), "Shader program interface must be valid.");
		return m_Storage->RequiredCapabilities;
	}

	ShaderFeatureRequirements ShaderLayout::GetFeatureRequirements() const noexcept
	{
		ASSERT(IsValid(), "Shader program interface must be valid.");
		return m_Storage->Requirements;
	}

	Span<const ShaderProgramPushConstant> ShaderLayout::GetPushConstants() const noexcept
	{
		ASSERT(IsValid(), "Shader program interface must be valid.");
		return m_Storage->PushConstants.View();
	}

	Span<const ShaderSpecializationParameter> ShaderLayout::GetSpecializationParameters() const noexcept
	{
		ASSERT(IsValid(), "Shader program interface must be valid.");
		return m_Storage->SpecializationParameters.View();
	}

	EShaderTaskPayloadCompatibility ShaderLayout::GetTaskPayloadCompatibility() const noexcept
	{
		ASSERT(IsValid(), "Invalid shader program interface.");
		return m_Storage->TaskPayloadCompatibility;
	}

	EShaderStageIOStatus ShaderLayout::GetStageIOStatus() const noexcept
	{
		ASSERT(IsValid(), "Shader program interface must be valid.");
		return m_Storage->StageIOStatus;
	}

	Result<uint32> ShaderLayout::FindParameter(const Hash256& id) const
	{
		using R = Result<uint32>;
		if (!IsValid())
		{
			return R::Failure(MakeArtifactError("Invalid shader program interface handle."));
		}

		const Array<uint32>& order = m_Storage->ParameterOrder;
		if (order.IsEmpty())
		{
			return R::Failure(MakeArtifactError("Shader parameter was not found.", EErrorCode::NotFound));
		}

		const uint32* const found = LowerBound(order.begin(), order.end(), id, [&](uint32 index, const Hash256& value) { return m_Storage->Bindings[index].ParameterID < value; });

		if (found == order.end() || m_Storage->Bindings[*found].ParameterID != id)
		{
			return R::Failure(MakeArtifactError("Shader parameter was not found.", EErrorCode::NotFound));
		}

		return R::Success(*found);
	}

	Result<ShaderLayout> ShaderLayout::Create(Span<const ShaderEntrySelection> selections, MemoryResource& resource, CancellationToken cancellation)
	{
		using R = Result<ShaderLayout>;
		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(CancelledError());
		}

		if (selections.IsEmpty() || selections.Size() > 128)
		{
			return R::Failure(MakeArtifactError("Program selections exceed the bounded compute, raster or ray inventory contract."));
		}

		const bool bRayABI = selections[0].Artifact.IsValid() && selections[0].VariantIndex < selections[0].Artifact.GetVariants().Size() &&
		                     IsRayStage(selections[0].Artifact.GetEntryPoints()[selections[0].Artifact.GetVariants()[selections[0].VariantIndex].EntryPointIndex].Stage);
		if (!bRayABI && selections.Size() > 3)
		{
			return R::Failure(MakeArtifactError("Raster program selection count exceeds its bound."));
		}

		ShaderLayout result;
		if (!MakeSharedInto(resource, &result.m_Storage, resource))
		{
			return R::Failure(AllocationError());
		}

		Storage& storage = *result.m_Storage;
		uint64 stages = 0;
		bool bCompleteStageIO = true;

		const bool bRasterContracts = !bRayABI;

		for (const ShaderEntrySelection& selection : selections)
		{
			if (!selection.Artifact.IsValid() || selection.VariantIndex >= selection.Artifact.GetVariants().Size())
			{
				return R::Failure(MakeArtifactError("Invalid shader entry selection."));
			}

			const ShaderVariant& variant = selection.Artifact.GetVariants()[selection.VariantIndex];
			const EShaderStage stage = selection.Artifact.GetEntryPoints()[variant.EntryPointIndex].Stage;
			if (bRayABI != IsRayStage(stage))
			{
				return R::Failure(MakeArtifactError("Ray and nonray program families cannot be combined."));
			}

			if (bRayABI)
			{
				const ShaderEntryPoint& entry = selection.Artifact.GetEntryPoints()[variant.EntryPointIndex];
				for (const ShaderEntrySelection& prior : storage.Selections)
				{
					const ShaderVariant& oldVariant = prior.Artifact.GetVariants()[prior.VariantIndex];
					if (prior.Artifact.GetEntryPoints()[oldVariant.EntryPointIndex].Name == entry.Name || oldVariant.EmittedEntryPoint == variant.EmittedEntryPoint)
					{
						return R::Failure(MakeArtifactError("Ray exports must have unique selected logical and emitted names.", EErrorCode::Conflict));
					}
				}
			}

			if (!(bRayABI && IsRayStage(stage)) && stage != EShaderStage::Compute && stage != EShaderStage::Vertex && stage != EShaderStage::Pixel &&
				!((stage == EShaderStage::Task || stage == EShaderStage::Mesh)))
			{
				return R::Failure(MakeArtifactError("The selected artifact ABI has no program interface contract for this stage.", EErrorCode::Unsupported));
			}

			const uint64 bit = uint64(1) << static_cast<uint32>(stage);
			if ((!bRayABI && (stages & bit) != 0) || (stage == EShaderStage::Compute && selections.Size() != 1))
			{
				return R::Failure(MakeArtifactError("Duplicate shader stages or mixed compute/graphics selection."));
			}

			stages |= bit;
			if (!storage.Selections.IsEmpty() && storage.Target != variant.Target)
			{
				return R::Failure(MakeArtifactError("Shader program selections must use one exact target profile."));
			}

			storage.Target = variant.Target;
			storage.RequiredCapabilities |= variant.RequiredCapabilities;

			bCompleteStageIO &= variant.bStageIOComplete;

			const ShaderFeatureRequirements& requirements = variant.Requirements;
			storage.Requirements.Features |= requirements.Features | variant.RequiredCapabilities;
			storage.Requirements.MinimumSubgroupSize = Max(storage.Requirements.MinimumSubgroupSize, requirements.MinimumSubgroupSize);
			if (requirements.MaximumSubgroupSize != 0)
			{
				storage.Requirements.MaximumSubgroupSize = storage.Requirements.MaximumSubgroupSize == 0 ? requirements.MaximumSubgroupSize : Min(storage.Requirements.MaximumSubgroupSize, requirements.MaximumSubgroupSize);
			}

			storage.Requirements.MinimumPushConstantBytes = Max(storage.Requirements.MinimumPushConstantBytes, requirements.MinimumPushConstantBytes);
			storage.Requirements.MinimumWorkgroupInvocations = Max(storage.Requirements.MinimumWorkgroupInvocations,
			    static_cast<uint32>(uint64(selection.Artifact.GetEntryPoints()[variant.EntryPointIndex].ThreadGroupSize[0]) * selection.Artifact.GetEntryPoints()[variant.EntryPointIndex].ThreadGroupSize[1] *
			                              selection.Artifact.GetEntryPoints()[variant.EntryPointIndex].ThreadGroupSize[2]));
			if (!storage.Selections.Add(selection))
			{
				return R::Failure(AllocationError());
			}
		}

		const auto stageBit = [](EShaderStage stage) { return uint64(1) << static_cast<uint32>(stage); };
		if ((stages & stageBit(EShaderStage::Mesh)) != 0)
		{
			const uint64 allowed = stageBit(EShaderStage::Task) | stageBit(EShaderStage::Mesh) | stageBit(EShaderStage::Pixel);
			if ((stages & ~allowed) != 0)
			{
				return R::Failure(MakeArtifactError("Mesh programs cannot combine traditional raster stages."));
			}
		}
		else if (!bRayABI && ((stages & stageBit(EShaderStage::Task)) != 0 || selections.Size() > 2))
		{
			return R::Failure(MakeArtifactError("A task entry requires its selected mesh consumer."));
		}

		if (storage.Requirements.MaximumSubgroupSize != 0 && storage.Requirements.MinimumSubgroupSize > storage.Requirements.MaximumSubgroupSize)
		{
			return R::Failure(MakeArtifactError("Selected shader subgroup constraints disagree.", EErrorCode::Conflict));
		}

		if (storage.Selections.Size() > 1)
		{
			Sort(storage.Selections.begin(), storage.Selections.end(), [](const ShaderEntrySelection& left, const ShaderEntrySelection& right)
			    {
				    const ShaderEntryPoint& leftEntry = left.Artifact.GetEntryPoints()[left.Artifact.GetVariants()[left.VariantIndex].EntryPointIndex];
				    const ShaderEntryPoint& rightEntry = right.Artifact.GetEntryPoints()[right.Artifact.GetVariants()[right.VariantIndex].EntryPointIndex];

				    return leftEntry.Stage != rightEntry.Stage ? leftEntry.Stage < rightEntry.Stage : leftEntry.Name.View() < rightEntry.Name.View();
			    });
		}

		LayoutKeys fixedKeys[] = {LayoutKeys(resource), LayoutKeys(resource), LayoutKeys(resource)};
		Array<LayoutKeys> rayKeys(resource);
		LayoutKeys* keys = fixedKeys;
		if (bRayABI)
		{
			if (!rayKeys.Reserve(storage.Selections.Size()))
			{
				return R::Failure(AllocationError());
			}

			for (usize index = 0; index < storage.Selections.Size(); ++index)
			{
				rayKeys.AddRequired(LayoutKeys(resource));
			}

			keys = rayKeys.Data();
		}

		const ShaderVariant* rasterProducer = nullptr;
		const ShaderVariant* rasterConsumer = nullptr;
		const ShaderVariant* taskProducer = nullptr;
		const ShaderVariant* meshConsumer = nullptr;
		for (const ShaderEntrySelection& selection : storage.Selections)
		{
			const ShaderVariant& variant = selection.Artifact.GetVariants()[selection.VariantIndex];
			const EShaderStage stage = selection.Artifact.GetEntryPoints()[variant.EntryPointIndex].Stage;
			if (stage == EShaderStage::Vertex || stage == EShaderStage::Mesh)
			{
				rasterProducer = &variant;
			}

			if (stage == EShaderStage::Pixel)
			{
				rasterConsumer = &variant;
			}

			if (stage == EShaderStage::Task)
			{
				taskProducer = &variant;
			}

			if (stage == EShaderStage::Mesh)
			{
				meshConsumer = &variant;
			}
		}

		if (meshConsumer != nullptr)
		{
			const ShaderTaskPayloadSignature& consumer = meshConsumer->TaskPayload;
			const bool bConsumerPresent = consumer.RootType != NO_PAYLOAD_TYPE || consumer.Proof != EShaderTaskPayloadProof::None;
			bool bCompatible = !bConsumerPresent;
			if (taskProducer != nullptr)
			{
				const ShaderTaskPayloadSignature& producer = taskProducer->TaskPayload;
				const bool bProducerPresent = producer.RootType != NO_PAYLOAD_TYPE || producer.Proof != EShaderTaskPayloadProof::None;
				if (!bProducerPresent && !bConsumerPresent)
				{
					bCompatible = true;
				}
				else if (producer.RootType != NO_PAYLOAD_TYPE && consumer.RootType != NO_PAYLOAD_TYPE)
				{
					bCompatible = ShaderPayloadCanonicalizer::Equal(producer, consumer);
					storage.TaskPayloadCompatibility = EShaderTaskPayloadCompatibility::StructuralVerified;
				}
				else
				{
					bCompatible = bProducerPresent && bConsumerPresent && producer.bHasProvenByteSize && consumer.bHasProvenByteSize && producer.ProvenByteSize == consumer.ProvenByteSize;
					storage.TaskPayloadCompatibility = EShaderTaskPayloadCompatibility::NativeSizeVerified;
				}
			}

			if (!bCompatible)
			{
				return R::Failure(MakeArtifactError("Task producer and mesh consumer payload proofs disagree.", EErrorCode::Conflict));
			}
		}

		uint64 comparisonBudget = 8u * 1024u * 1024u;
		if (bCompleteStageIO && !bRayABI)
		{
			storage.StageIOStatus = EShaderStageIOStatus::Verified;
			if (rasterProducer != nullptr && rasterConsumer != nullptr)
			{
				const ShaderVariant& producer = *rasterProducer;
				const ShaderVariant& consumer = *rasterConsumer;
				for (const ShaderStageIO& input : consumer.StageInputs)
				{
					if (rasterizerSupplies(input.Builtin))
					{
						continue;
					}

					bool bFound = false;
					for (const ShaderStageIO& output : producer.StageOutputs)
					{
						if (comparisonBudget == 0)
						{
							return R::Failure(MakeArtifactError("Stage I/O linking exceeds its comparison bound."));
						}
						--comparisonBudget;
						if (!StageIOSlotEqual(input, output, storage.Target.Target))
						{
							continue;
						}

						// Vertex output rate is implicit; only mesh outputs encode their raster rate.
						const EShaderStageIORate outputRate = meshConsumer == nullptr && output.Rate == EShaderStageIORate::Unspecified ? EShaderStageIORate::PerVertex : output.Rate;
						if (!StageIORepresentationEqual(input, output) ||
							(storage.Target.Target == EShaderTarget::Dxil && meshConsumer == nullptr && (input.Location != output.Location || input.Component != output.Component)) ||
							(storage.Target.Target == EShaderTarget::SpirV && bRasterContracts && input.Rate != outputRate))
						{
							return R::Failure(MakeArtifactError("Raster producer and consumer representations or "
							                                    "native signature footprints disagree.",
							    EErrorCode::Conflict));
						}

						bFound = true;
						break;
					}

					if (!bFound)
					{
						return R::Failure(MakeArtifactError("Raster consumer input has no producer output.", EErrorCode::Conflict));
					}
				}
			}
		}

		for (uint32 selectionIndex = 0; selectionIndex < storage.Selections.Size(); ++selectionIndex)
		{
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(CancelledError());
			}

			const ShaderEntrySelection& selection = storage.Selections[selectionIndex];
			const ShaderVariant& variant = selection.Artifact.GetVariants()[selection.VariantIndex];
			const EShaderStage stage = selection.Artifact.GetEntryPoints()[variant.EntryPointIndex].Stage;
			LayoutKeys& layoutKeys = keys[selectionIndex];
			if (!layoutKeys.Physical.Resize(variant.Layouts.Size()) || !layoutKeys.Logical.Resize(variant.Layouts.Size()) || !layoutKeys.Complete.Resize(variant.Layouts.Size()))
			{
				return R::Failure(AllocationError());
			}

			for (uint32 layoutIndex = 0; layoutIndex < variant.Layouts.Size(); ++layoutIndex)
			{
				Result<void> hashed = hashLayoutRecursive(variant, layoutIndex, layoutKeys, resource);
				if (!hashed)
				{
					return R::Failure(hashed.GetError());
				}
			}

			for (const ShaderPushConstantRange& range : variant.PushConstants)
			{
				bool bMerged = false;
				for (ShaderProgramPushConstant& prior : storage.PushConstants)
				{
					if (comparisonBudget == 0)
					{
						return R::Failure(MakeArtifactError("Push-range composition exceeds its comparison bound."));
					}
					--comparisonBudget;
					const ShaderPushConstantRange& other = prior.Range;
					if (range.Space != other.Space || range.Binding != other.Binding || range.Lowering != other.Lowering ||
						uint64(range.ByteOffset) >= uint64(other.ByteOffset) + other.ByteSize ||
						uint64(other.ByteOffset) >= uint64(range.ByteOffset) + range.ByteSize)
					{
						continue;
					}

					if (range.ByteOffset != other.ByteOffset || range.ByteSize != other.ByteSize)
					{
						return R::Failure(MakeArtifactError("Overlapping program push ranges disagree.", EErrorCode::Conflict));
					}

					const ShaderEntrySelection& previousSelection = storage.Selections[prior.SelectionIndex];
					HashMap<uint64, uint8> visited(resource);
					Result<bool> equal = equalLayoutsRecursive(previousSelection.Artifact.GetVariants()[previousSelection.VariantIndex], other.DataLayout, variant, range.DataLayout, true, true, visited, comparisonBudget);
					if (!equal)
					{
						return R::Failure(equal.GetError());
					}

					if (!equal.Value())
					{
						return R::Failure(MakeArtifactError("Shared program push layouts disagree.", EErrorCode::Conflict));
					}

					prior.Range.StageMask |= range.StageMask;
					bMerged = true;
					break;
				}

				if (!bMerged && storage.PushConstants.Size() == 256)
				{
					return R::Failure(MakeArtifactError("Program push ranges exceed their aggregate limit."));
				}

				if (!bMerged && !storage.PushConstants.Add(ShaderProgramPushConstant{range, selectionIndex}))
				{
					return R::Failure(AllocationError());
				}
			}

			for (const ShaderSpecializationParameter& parameter : variant.SpecializationParameters)
			{
				if (parameter.Phase != EShaderSpecializationPhase::PipelineTime || parameter.bAffectsLayout)
				{
					return R::Failure(MakeArtifactError("Unresolved build-time or layout-affecting specialization "
					                                    "requires emitted compatible code and interface.",
					    EErrorCode::Unsupported));
				}

				bool bMerged = false;
				for (const ShaderSpecializationParameter& prior : storage.SpecializationParameters)
				{
					if (comparisonBudget == 0)
					{
						return R::Failure(MakeArtifactError("Specialization composition exceeds its comparison bound."));
					}
					--comparisonBudget;
					if (prior.ID != parameter.ID && prior.Name != parameter.Name)
					{
						continue;
					}

					if (!SpecializationShapeEqual(prior, parameter))
					{
						return R::Failure(MakeArtifactError("Shared specialization schemas disagree.", EErrorCode::Conflict));
					}

					bMerged = true;
					break;
				}

				if (!bMerged)
				{
					if (storage.SpecializationParameters.Size() == 1024)
					{
						return R::Failure(MakeArtifactError("Program specialization parameters exceed their aggregate limit."));
					}

					Result<ShaderSpecializationParameter> copy = CloneSpecializationParameter(parameter, resource);
					if (!copy)
					{
						return R::Failure(copy.GetError());
					}

					if (!storage.SpecializationParameters.Add(Move(copy).TakeValue()))
					{
						return R::Failure(AllocationError());
					}
				}
			}

			for (const ShaderBinding& binding : variant.Bindings)
			{
				if (binding.Scope == EShaderBindingScope::LocalRecord)
				{
					continue;
				}

				Writer parameter(resource, true);
				parameter.Raw("ROX-SHADER-PARAMETER", 20);
				parameter.U32(1);
				parameter.Text(binding.Name);
				const Hash256 id = parameter.GetHash();
				bool bMerged = false;
				for (ShaderProgramBinding& existing : storage.Bindings)
				{
					if (comparisonBudget == 0)
					{
						return R::Failure(MakeArtifactError("Shader interface binding comparison exceeds its bound."));
					}
					--comparisonBudget;
					if ((existing.ParameterID == id && existing.Binding.Name != binding.Name) ||
						(existing.Binding.Name == binding.Name && !bindingSlotEqual(existing.Binding, binding)))
					{
						return R::Failure(MakeArtifactError("Shader parameter identity collision or inconsistent binding location.", EErrorCode::Conflict));
					}

					const uint64 extent = GetBindingRegisterExtent(binding);
					const uint64 otherExtent = GetBindingRegisterExtent(existing.Binding);
					const bool bOverlap = binding.Space == existing.Binding.Space && binding.RegisterNamespace == existing.Binding.RegisterNamespace && uint64(binding.Binding) < uint64(existing.Binding.Binding) + otherExtent &&
					                      uint64(existing.Binding.Binding) < uint64(binding.Binding) + extent;
					if (!bOverlap)
					{
						continue;
					}

					if (!bindingSlotEqual(existing.Binding, binding) || existing.Binding.Name != binding.Name || !bindingShapeEqual(existing.Binding, binding))
					{
						return R::Failure(MakeArtifactError("Overlapping shader program bindings have incompatible contracts.", EErrorCode::Conflict));
					}

					const ShaderEntrySelection& prior = storage.Selections[existing.SelectionIndex];
					const ShaderVariant& priorVariant = prior.Artifact.GetVariants()[prior.VariantIndex];
					HashMap<uint64, uint8> visited(resource);
					Result<bool> equal = equalLayoutsRecursive(priorVariant, existing.Binding.DataLayout, variant, binding.DataLayout, true, true, visited, comparisonBudget);
					if (!equal)
					{
						return R::Failure(equal.GetError());
					}

					if (!equal.Value())
					{
						return R::Failure(MakeArtifactError("Shared shader binding data layouts disagree.", EErrorCode::Conflict));
					}

					existing.StageMask |= uint64(1) << static_cast<uint32>(stage);
					existing.Binding.Access = static_cast<EShaderResourceAccess>(static_cast<uint32>(existing.Binding.Access) | static_cast<uint32>(binding.Access));
					bMerged = true;
					break;
				}

				if (bMerged)
				{
					continue;
				}

				if (storage.Bindings.Size() == 4096)
				{
					return R::Failure(MakeArtifactError("Shader program bindings exceed their aggregate limit."));
				}

				ShaderProgramBinding added(resource);
				added.ParameterID = id;
				added.SelectionIndex = selectionIndex;
				added.StageMask = uint64(1) << static_cast<uint32>(stage);
				added.Binding.Kind = binding.Kind;
				added.Binding.Space = binding.Space;
				added.Binding.Binding = binding.Binding;
				added.Binding.RegisterNamespace = binding.RegisterNamespace;
				added.Binding.ArrayCount = binding.ArrayCount;
				added.Binding.CountMode = binding.CountMode;
				added.Binding.Scope = binding.Scope;
				added.Binding.DataLayout = binding.DataLayout;
				added.Binding.TextureShape = binding.TextureShape;
				added.Binding.bTextureArray = binding.bTextureArray;
				added.Binding.bMultisampled = binding.bMultisampled;
				added.Binding.Access = binding.Access;
				if (!added.Binding.Name.Assign(binding.Name) || !storage.Bindings.Add(Move(added)))
				{
					return R::Failure(AllocationError());
				}
			}
		}

		if (storage.Bindings.Size() > 1)
		{
			Sort(storage.Bindings.begin(), storage.Bindings.end(), bindingLess);
		}

		for (const ShaderProgramPushConstant& push : storage.PushConstants)
		{
			for (const ShaderProgramBinding& binding : storage.Bindings)
			{
				if (comparisonBudget == 0)
				{
					return R::Failure(MakeArtifactError("Root-constant composition exceeds its comparison bound."));
				}
				--comparisonBudget;
				if (push.Range.Lowering == EShaderPushConstantLowering::D3D12RootConstants && binding.Binding.RegisterNamespace == EShaderRegisterClass::ConstantBuffer &&
					push.Range.Space == binding.Binding.Space &&
				    push.Range.Binding >= binding.Binding.Binding &&
				    uint64(push.Range.Binding) < uint64(binding.Binding.Binding) + (binding.Binding.CountMode == EShaderDescriptorCountMode::Runtime ? uint64{0x100000000} - binding.Binding.Binding : binding.Binding.ArrayCount))
				{
					return R::Failure(MakeArtifactError("Program root constants overlap a constant-buffer register.", EErrorCode::Conflict));
				}
			}
		}

		if (storage.SpecializationParameters.Size() > 1)
		{
			Sort(storage.SpecializationParameters.begin(), storage.SpecializationParameters.end(), [](const ShaderSpecializationParameter& a, const ShaderSpecializationParameter& b) { return a.ID < b.ID; });
		}

		if (storage.PushConstants.Size() > 1)
		{
			Sort(storage.PushConstants.begin(), storage.PushConstants.end(), [](const ShaderProgramPushConstant& a, const ShaderProgramPushConstant& b)
			    {
				    if (a.Range.Space != b.Range.Space)
				    {
					    return a.Range.Space < b.Range.Space;
				    }

				    if (a.Range.Binding != b.Range.Binding)
				    {
					    return a.Range.Binding < b.Range.Binding;
				    }

				    return a.Range.ByteOffset < b.Range.ByteOffset;
			    });
		}

		if (!OrderIndices(storage.Bindings, storage.ParameterOrder, [](const ShaderProgramBinding& left, const ShaderProgramBinding& right) { return left.ParameterID < right.ParameterID; }))
		{
			return R::Failure(AllocationError());
		}

		Writer physical(resource, true);
		physical.Raw("RX-SHADER-PHYSICAL", 18);
		physical.U32(1);
		WriteProfile(physical, storage.Target);
		Writer logical(resource, true);
		logical.Raw("RX-SHADER-INTERFACE", 19);
		logical.U32(1);
		WriteProfile(logical, storage.Target);
		Writer code(resource, true);
		code.Raw("RX-SHADER-PROGRAM-CODE", 22);
		code.U32(1);
		{
			physical.U64(storage.Requirements.Features & uint64{0x30000});
		}

		physical.U32(static_cast<uint32>(storage.Bindings.Size()));
		logical.U64(storage.RequiredCapabilities);
		logical.U32(static_cast<uint32>(storage.Selections.Size()));
		code.U32(static_cast<uint32>(storage.Selections.Size()));
		for (const ShaderEntrySelection& selected : storage.Selections)
		{
			const ShaderVariant& variant = selected.Artifact.GetVariants()[selected.VariantIndex];
			const ShaderEntryPoint& entry = selected.Artifact.GetEntryPoints()[variant.EntryPointIndex];
			logical.Text(entry.Name);
			logical.Enum(entry.Stage);
			for (uint32 size : entry.ThreadGroupSize)
			{
				logical.U32(size);
			}

			code.Enum(entry.Stage);
			code.Text(variant.EmittedEntryPoint);
			code.Hash(selected.Artifact.GetVariantCodeHash(selected.VariantIndex));
		}

		logical.U32(static_cast<uint32>(storage.Bindings.Size()));
		for (const ShaderProgramBinding& value : storage.Bindings)
		{
			const ShaderBinding& binding = value.Binding;
			logical.Text(binding.Name);
			logical.Hash(value.ParameterID);
			logical.Enum(binding.Access);
			for (Writer* writer : {&physical, &logical})
			{
				writer->Enum(binding.Kind);
				writer->U32(binding.Space);
				writer->Enum(binding.RegisterNamespace);
				writer->U32(binding.Binding);
				writer->U32(binding.ArrayCount);
				{
					writer->Enum(binding.CountMode);
				}

				writer->Enum(binding.TextureShape);
				writer->U32(binding.bTextureArray ? 1 : 0);
				writer->U32(binding.bMultisampled ? 1 : 0);
				writer->U64(value.StageMask);
				writer->U32(binding.DataLayout == NO_DATA_LAYOUT ? 0 : 1);
			}

			if (binding.DataLayout != NO_DATA_LAYOUT)
			{
				physical.Hash(keys[value.SelectionIndex].Physical[binding.DataLayout]);
				logical.Hash(keys[value.SelectionIndex].Logical[binding.DataLayout]);
			}
		}
		{
			logical.Raw("RX-SHADER-INTERFACE", 19);
			if (bRasterContracts)
			{
				logical.Enum(storage.TaskPayloadCompatibility);
			}

			writeFeatureRequirements(logical, storage.Requirements);
			logical.Enum(storage.StageIOStatus);
			for (const ShaderEntrySelection& selected : storage.Selections)
			{
				const ShaderVariant& variant = selected.Artifact.GetVariants()[selected.VariantIndex];
				{
					for (Writer* writer : {&physical, &logical})
					{
						writer->Enum(variant.LocalRecord.Lowering);
						writer->U32(variant.LocalRecord.ByteSize);
						writer->U32(variant.LocalRecord.Alignment);
						if (variant.LocalRecord.DataLayout != NO_DATA_LAYOUT)
						{
							writer->Hash(writer == &physical ? keys[&selected - storage.Selections.Data()].Physical[variant.LocalRecord.DataLayout] : keys[&selected - storage.Selections.Data()].Logical[variant.LocalRecord.DataLayout]);
						}
					}
					{
						Result<Hash256> localIdentity = ComputeShaderLocalRecordPhysicalIdentity(variant, resource);
						if (!localIdentity)
						{
							return R::Failure(localIdentity.GetError());
						}

						physical.Hash(localIdentity.Value());
						logical.Hash(localIdentity.Value());
						logical.Enum(variant.LocalRecord.Proof);
						for (const ShaderLocalRootArgument& argument : variant.LocalRootArguments)
						{
							const ShaderBinding& binding = variant.Bindings[argument.BindingIndex];
							logical.Text(binding.Name);
							if (binding.DataLayout != NO_DATA_LAYOUT)
							{
								logical.Hash(keys[&selected - storage.Selections.Data()].Logical[binding.DataLayout]);
							}
						}
					}
				}

				if (bRayABI)
				{
					Result<void> rayWritten = WriteRayInterface(logical, variant.RayInterface, resource, true);
					if (!rayWritten)
					{
						return R::Failure(Move(rayWritten.GetError()));
					}
				}

				logical.U32(variant.bStageIOComplete ? 1 : 0);
				writeStageIO(logical, variant.StageInputs, bRasterContracts);
				writeStageIO(logical, variant.StageOutputs, bRasterContracts);
				if (bRasterContracts)
				{
					logical.Enum(variant.MeshOutput.Topology);
					logical.U32(variant.MeshOutput.MaximumVertices);
					logical.U32(variant.MeshOutput.MaximumPrimitives);
					const ShaderTaskPayloadSignature& payload = variant.TaskPayload;
					if (bRasterContracts)
					{
						logical.Enum(payload.Proof);
					}

					logical.U32(payload.RootType);
					logical.U32(payload.bHasProvenByteSize ? 1 : 0);
					logical.U32(payload.ProvenByteSize);
					logical.U32(static_cast<uint32>(payload.Types.Size()));
					for (const ShaderPayloadType& type : payload.Types)
					{
						logical.Enum(type.Kind);
						logical.Enum(type.Scalar);
						logical.U32(type.ScalarWidth);
						logical.U32(type.Count);
						logical.U32(type.ElementType);
						logical.U32(static_cast<uint32>(type.MemberTypes.Size()));
						for (uint32 member : type.MemberTypes)
						{
							logical.U32(member);
						}
					}
				}
			}

			logical.U32(static_cast<uint32>(storage.SpecializationParameters.Size()));
			for (const ShaderSpecializationParameter& parameter : storage.SpecializationParameters)
			{
				logical.U32(parameter.ID);
				logical.Text(parameter.Name);
				logical.Enum(parameter.Scalar);
				logical.U32(parameter.ScalarWidth);
				logical.Enum(parameter.Phase);
				logical.U64(parameter.DefaultBits);
				logical.U64(parameter.MinimumBits);
				logical.U64(parameter.MaximumBits);
				logical.U32(parameter.bHasDefault ? 1 : 0);
				logical.U32(parameter.bHasRange ? 1 : 0);
				logical.U32(parameter.bAffectsLayout ? 1 : 0);
				logical.U32(parameter.WorkgroupAxis);
			}

			if (!storage.PushConstants.IsEmpty())
			{
				physical.Raw("RX-SHADER-PUSH", 14);
			}

			for (Writer* writer : {&physical, &logical})
			{
				if (writer == &physical && storage.PushConstants.IsEmpty())
				{
					continue;
				}

				writer->U32(static_cast<uint32>(storage.PushConstants.Size()));
				for (const ShaderProgramPushConstant& push : storage.PushConstants)
				{
					writer->U32(push.Range.ByteOffset);
					writer->U32(push.Range.ByteSize);
					writer->U64(push.Range.StageMask);
					writer->Enum(push.Range.Lowering);
					writer->U32(push.Range.Space);
					writer->U32(push.Range.Binding);
					writer->Hash(writer == &physical ? keys[push.SelectionIndex].Physical[push.Range.DataLayout] : keys[push.SelectionIndex].Logical[push.Range.DataLayout]);
				}
			}
		}

		if (!physical.IsValid() || !logical.IsValid() || !code.IsValid())
		{
			return R::Failure(AllocationError());
		}

		storage.PhysicalLayoutHash = physical.GetHash();
		storage.ProgramInterfaceHash = logical.GetHash();
		storage.SelectedCodeHash = code.GetHash();
		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(CancelledError());
		}

		return R::Success(Move(result));
	}

	Result<ShaderInterfaceCompatibility> CompareShaderLayouts(const ShaderLayout& original, const ShaderLayout& replacement, MemoryResource& resource)
	{
		using R = Result<ShaderInterfaceCompatibility>;
		if (!original.IsValid() || !replacement.IsValid())
		{
			return R::Failure(MakeArtifactError("Invalid shader program interface handle."));
		}

		ShaderInterfaceCompatibility result(resource);
		result.bCodeChanged = original.GetSelectedCodeHash() != replacement.GetSelectedCodeHash();
		result.bRequirementsChanged = original.GetRequiredCapabilities() != replacement.GetRequiredCapabilities();
		result.bRequirementsChanged |= original.GetFeatureRequirements() != replacement.GetFeatureRequirements();
		result.bRequirementsChanged |= original.GetTaskPayloadCompatibility() != replacement.GetTaskPayloadCompatibility();
		const Span<const ShaderEntrySelection> oldSelections = original.GetSelections();
		const Span<const ShaderEntrySelection> newSelections = replacement.GetSelections();
		result.bRequirementsChanged |= oldSelections.Size() != newSelections.Size();
		bool bMeshContractCompatible = oldSelections.Size() == newSelections.Size() && (original.GetFeatureRequirements().Features & uint64{0x30000}) == (replacement.GetFeatureRequirements().Features & uint64{0x30000});
		uint64 localComparisonBudget = 8u * 1024u * 1024u;
		for (usize index = 0; index < Min(oldSelections.Size(), newSelections.Size()); ++index)
		{
			const ShaderVariant& oldVariant = oldSelections[index].Artifact.GetVariants()[oldSelections[index].VariantIndex];
			const ShaderVariant& newVariant = newSelections[index].Artifact.GetVariants()[newSelections[index].VariantIndex];
			const ShaderEntryPoint& oldEntry = oldSelections[index].Artifact.GetEntryPoints()[oldVariant.EntryPointIndex];
			const ShaderEntryPoint& newEntry = newSelections[index].Artifact.GetEntryPoints()[newVariant.EntryPointIndex];
			result.bRequirementsChanged |= oldEntry.Stage != newEntry.Stage;
			{
				const bool bSameRay = oldEntry.Stage == newEntry.Stage && (!IsRayStage(oldEntry.Stage) || oldEntry.Name == newEntry.Name) && rayInterfacesEqual(oldVariant.RayInterface, newVariant.RayInterface);
				bMeshContractCompatible &= bSameRay;
				result.bRequirementsChanged |= !bSameRay;
				Result<bool> same = localRecordsEqual(oldVariant, newVariant, resource, localComparisonBudget);
				if (!same)
				{
					return R::Failure(same.GetError());
				}

				const bool bSameRecord = same.Value();

				bMeshContractCompatible &= bSameRecord;
				result.bRequirementsChanged |= !bSameRecord;
			}

			if ((!IsRayStage(oldEntry.Stage)) || (!IsRayStage(newEntry.Stage)))
			{
				const bool bSameMesh = oldEntry.Stage == newEntry.Stage && oldVariant.MeshOutput == newVariant.MeshOutput && ShaderPayloadCanonicalizer::Equal(oldVariant.TaskPayload, newVariant.TaskPayload);
				bMeshContractCompatible &= bSameMesh;
				result.bRequirementsChanged |= !bSameMesh;
				for (auto tables : {Pair(&oldVariant.StageInputs, &newVariant.StageInputs), Pair(&oldVariant.StageOutputs, &newVariant.StageOutputs)})
				{
					if (tables.first->Size() != tables.second->Size())
					{
						bMeshContractCompatible = false;
						continue;
					}

					for (usize ioIndex = 0; ioIndex < tables.first->Size(); ++ioIndex)
					{
						const ShaderStageIO& first = (*tables.first)[ioIndex];
						const ShaderStageIO& second = (*tables.second)[ioIndex];
						bMeshContractCompatible &= first.Rate == second.Rate && StageIOSlotEqual(first, second, original.GetTarget().Target) && StageIORepresentationEqual(first, second);
					}
				}
			}

			for (uint32 axis = 0; axis < 3; ++axis)
			{
				result.bRequirementsChanged |= oldEntry.ThreadGroupSize[axis] != newEntry.ThreadGroupSize[axis];
			}
		}

		const Span<const ShaderProgramBinding> prior = original.GetBindings();
		const Span<const ShaderProgramBinding> next = replacement.GetBindings();
		result.bAccessChanged = prior.Size() != next.Size();
		bool bSameShape = prior.Size() == next.Size() && original.GetTarget() == replacement.GetTarget();
		uint64 budget = 8u * 1024u * 1024u;
		for (usize index = 0; index < Min(prior.Size(), next.Size()); ++index)
		{
			const ShaderProgramBinding& oldValue = prior[index];
			const ShaderProgramBinding& newValue = next[index];
			result.bAccessChanged |= oldValue.Binding.Access != newValue.Binding.Access;
			const bool bBindingShape = oldValue.ParameterID == newValue.ParameterID && oldValue.Binding.Name == newValue.Binding.Name && oldValue.StageMask == newValue.StageMask && bindingSlotEqual(oldValue.Binding, newValue.Binding) &&
			                           bindingShapeEqual(oldValue.Binding, newValue.Binding);
			if (!bBindingShape)
			{
				bSameShape = false;
				continue;
			}

			const ShaderEntrySelection& oldSelection = original.GetSelections()[oldValue.SelectionIndex];
			const ShaderEntrySelection& newSelection = replacement.GetSelections()[newValue.SelectionIndex];
			HashMap<uint64, uint8> visited(resource);
			Result<bool> equal = equalLayoutsRecursive(
				oldSelection.Artifact.GetVariants()[oldSelection.VariantIndex],
				oldValue.Binding.DataLayout,
				newSelection.Artifact.GetVariants()[newSelection.VariantIndex],
				newValue.Binding.DataLayout,
				true,
				false,
				visited,
				budget);
			if (!equal)
			{
				return R::Failure(equal.GetError());
			}

			bSameShape &= equal.Value();
		}

		const Span<const ShaderProgramPushConstant> oldPush = original.GetPushConstants();
		const Span<const ShaderProgramPushConstant> newPush = replacement.GetPushConstants();
		bSameShape &= oldPush.Size() == newPush.Size();
		for (usize index = 0; index < Min(oldPush.Size(), newPush.Size()); ++index)
		{
			const ShaderProgramPushConstant& first = oldPush[index];
			const ShaderProgramPushConstant& second = newPush[index];
			if (first.Range.Lowering != second.Range.Lowering || first.Range.Space != second.Range.Space || first.Range.Binding != second.Range.Binding ||
				first.Range.StageMask != second.Range.StageMask)
			{
				bSameShape = false;
				continue;
			}

			const ShaderEntrySelection& oldSelection = original.GetSelections()[first.SelectionIndex];
			const ShaderEntrySelection& newSelection = replacement.GetSelections()[second.SelectionIndex];
			HashMap<uint64, uint8> visited(resource);
			Result<bool> equal = equalLayoutsRecursive(
			    oldSelection.Artifact.GetVariants()[oldSelection.VariantIndex], first.Range.DataLayout, newSelection.Artifact.GetVariants()[newSelection.VariantIndex], second.Range.DataLayout, true, false, visited, budget);
			if (!equal)
			{
				return R::Failure(equal.GetError());
			}

			bSameShape &= equal.Value();
		}

		if (original.GetLayoutHash() == replacement.GetLayoutHash())
		{
			result.Level = EShaderInterfaceCompatibility::Identical;
		}
		else if (bMeshContractCompatible && original.GetPhysicalLayoutHash() == replacement.GetPhysicalLayoutHash())
		{
			result.Level = EShaderInterfaceCompatibility::BindingCompatible;
		}
		else if (bMeshContractCompatible && bSameShape)
		{
			result.Level = EShaderInterfaceCompatibility::RepackRequired;
		}

		String reason(resource);
		StringView explanation;
		switch (result.Level)
		{
		case EShaderInterfaceCompatibility::Identical:
			explanation = "Structural interfaces are identical; parameter semantics remain unproven.";
			break;

		case EShaderInterfaceCompatibility::BindingCompatible:
			explanation = "Native binding layouts match; logical changes and parameter semantics require caller review.";
			break;

		case EShaderInterfaceCompatibility::RepackRequired:
			explanation = "Parameter shapes match but physical layouts require repacking; parameter semantics remain unproven.";
			break;

		default:
			explanation = "Shader program interfaces have incompatible structural contracts.";
			break;
		}

		if (!reason.Assign(explanation) || !result.Reasons.Add(Move(reason)))
		{
			return R::Failure(AllocationError());
		}

		return R::Success(Move(result));
	}

	ShaderLayout::ShaderLayout() noexcept = default;
	ShaderLayout::~ShaderLayout() = default;
	ShaderLayout::ShaderLayout(const ShaderLayout&) noexcept = default;
	ShaderLayout::ShaderLayout(ShaderLayout&&) noexcept = default;
	ShaderLayout& ShaderLayout::operator=(const ShaderLayout&) noexcept = default;
	ShaderLayout& ShaderLayout::operator=(ShaderLayout&&) noexcept = default;

	Result<Array<ShaderPropertyLocation>> ShaderLayout::FindPropertyBindings(StringView property, MemoryResource& memory) const
	{
		using R = Result<Array<ShaderPropertyLocation>>;
		Array<ShaderPropertyLocation> locations(memory);
		const Span<const ShaderEntrySelection> selections = GetSelections();
		for (usize i = 0; i < selections.Size(); ++i)
		{
			const ShaderEntrySelection& selection = selections[i];
			const ShaderVariant& variant = selection.Artifact.GetVariants()[selection.VariantIndex];
			const ShaderEntryPoint& entry = selection.Artifact.GetEntryPoints()[variant.EntryPointIndex];
			for (const ShaderPropertyBinding& mapping : selection.Artifact.GetPropertyBindings())
			{
				if (mapping.Property != property || mapping.Entry != entry.Name || mapping.Target != variant.Target)
				{
					continue;
				}
				for (usize bindingIndex = 0; bindingIndex < variant.Bindings.Size(); ++bindingIndex)
				{
					const ShaderBinding& binding = variant.Bindings[bindingIndex];
					if (binding.Name != mapping.Binding)
					{
						continue;
					}

					ShaderPropertyLocation location{static_cast<uint32>(i), static_cast<uint32>(bindingIndex)};
					if (!mapping.Field.IsEmpty())
					{
						ASSERT(binding.DataLayout < variant.Layouts.Size(), "Validated property fields refer to an existing data layout.");

						const Array<ShaderDataField>& fields = variant.Layouts[binding.DataLayout].Fields;
						for (usize f = 0; f < fields.Size(); ++f)
						{
							if (fields[f].Name == mapping.Field)
							{
								location.FieldIndex = static_cast<uint32>(f);
								break;
							}
						}

						ASSERT(location.FieldIndex != NO_DATA_LAYOUT, "Validated property mappings resolve their named field.");
					}

					if (!locations.Add(location))
					{
						return R::Failure(AllocationError());
					}
				}
			}
		}
		if (locations.IsEmpty())
		{
			return R::Failure(MakeArtifactError("Property is not used by this selection.", EErrorCode::NotFound));
		}

		return R::Success(Move(locations));
	}

} // namespace rox::shader
