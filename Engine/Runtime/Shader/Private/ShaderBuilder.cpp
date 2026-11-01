#include <ShaderBuilder.h>

#include "ShaderDescription.h"
#include "ShaderMetadata.h"
#include "ShaderSerialization.h"
#include "ShaderWire.hpp"

#include <Algorithms.hpp>
#include <Comparison.hpp>
#include <Types.h>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		bool nameValid(StringView text, const ShaderLimits& limits)
		{
			return !text.IsEmpty() && text.Size() <= limits.MaximumNameBytes && !text.Contains('\0') && IsValidUtf8(text);
		}

		bool accessValid(EShaderResourceAccess access)
		{
			return access == EShaderResourceAccess::Read || access == EShaderResourceAccess::Write || access == EShaderResourceAccess::ReadWrite || access == EShaderResourceAccess::ReadWriteAtomic;
		}

		struct LayoutVisit
		{
			uint32 Height = 0;
			bool bVisiting = false;
		};

		bool isLayoutAcyclicRecursive(const ShaderVariant& variant, uint32 index, uint32 depth, uint32 maximum, uint64& budget, Array<LayoutVisit>& visits)
		{
			LayoutVisit& visit = visits[index];
			if (depth > maximum || visit.bVisiting)
			{
				return false;
			}

			if (visit.Height != 0)
			{
				return visit.Height <= maximum - depth + 1;
			}

			if (budget == 0)
			{
				return false;
			}
			--budget;
			visit.bVisiting = true;
			uint32 height = 1;
			for (const ShaderDataField& field : variant.Layouts[index].Fields)
			{
				if (field.NestedLayout == NO_DATA_LAYOUT)
				{
					continue;
				}

				if (budget == 0)
				{
					return false;
				}
				--budget;
				if (depth == maximum || !isLayoutAcyclicRecursive(variant, field.NestedLayout, depth + 1, maximum, budget, visits))
				{
					return false;
				}

				height = Max(height, visits[field.NestedLayout].Height + 1);
			}

			visit.bVisiting = false;
			visit.Height = height;
			return height <= maximum - depth + 1;
		}

		uint32 word(Span<const Byte> bytes, usize offset)
		{
			ByteReader reader({bytes.Data() + offset, 4});
			return reader.ReadU32LE();
		}

		bool codeValid(const ShaderVariant& variant)
		{
			Span<const Byte> bytes = variant.Bytecode.View();
			if (variant.Target.Target == EShaderTarget::SpirV)
			{
				if (bytes.Size() < 20 || bytes.Size() % 4 != 0 || word(bytes, 0) != 0x07230203 || word(bytes, 12) == 0 || word(bytes, 16) != 0 ||
					word(bytes, 4) != ((variant.Target.SpirVMajor << 16) | (variant.Target.SpirVMinor << 8)))
				{
					return false;
				}

				for (usize offset = 20; offset < bytes.Size();)
				{
					const uint32 count = word(bytes, offset) >> 16;
					if (count == 0 || count > (bytes.Size() - offset) / 4)
					{
						return false;
					}

					offset += count * 4;
				}

				return true;
			}

			if (bytes.Size() < 32 || word(bytes, 0) != 0x43425844 || word(bytes, 20) != 1 || word(bytes, 24) != bytes.Size())
			{
				return false;
			}

			const uint32 count = word(bytes, 28);
			// Bound overlap verification independently of the serialized byte limit.
			if (count == 0 || count > 1024 || count > (bytes.Size() - 32) / 4)
			{
				return false;
			}

			bool bDxil = false;
			for (uint32 i = 0; i < count; ++i)
			{
				const uint32 offset = word(bytes, 32 + i * 4);
				if (offset < 32 + count * 4 || offset > bytes.Size() - 8 || word(bytes, offset + 4) > bytes.Size() - offset - 8)
				{
					return false;
				}

				const uint64 end = offset + 8ull + word(bytes, offset + 4);
				for (uint32 j = 0; j < i; ++j)
				{
					const uint32 other = word(bytes, 32 + j * 4);
					if (offset < other + 8ull + word(bytes, other + 4) && other < end)
					{
						return false;
					}
				}

				if (word(bytes, offset) == 0x4c495844)
				{
					bDxil = word(bytes, offset + 4) >= 24;
				}
			}

			return bDxil;
		}
	} // namespace

	Result<void> ShaderBuilder::Validate(const ShaderLimits& limits) const
	{
		const ShaderBuilder& artifact = *this;

		auto fail = [](StringView text) { return Result<void>::Failure(MakeArtifactError(text)); };
		if (limits.MaximumTotalBytes > 64u * 1024u * 1024u || limits.MaximumNestingDepth > 32 || limits.MaximumStageIO > 4096 || limits.MaximumPushConstantRanges > 256 ||
			limits.MaximumSpecializationParameters > 1024 ||
			limits.MaximumPayloadTypes > 4096 ||
			limits.MaximumPayloadMembers > 16384 ||
			limits.MaximumPayloadNestingDepth > 16 ||
			limits.MaximumRayDataSignatures > 1024)
		{
			return fail("Shader limits exceed the supported hard processing bounds.");
		}

		if ((artifact.AbiVersion != SHADER_ABI_VERSION) || artifact.EntryPoints.IsEmpty() || artifact.Variants.IsEmpty() ||
			artifact.EntryPoints.Size() > limits.MaximumEntryPoints ||
			artifact.Variants.Size() > limits.MaximumVariants)
		{
			return fail("Invalid shader ABI or entry/variant count.");
		}

		uint64 totalBindings = 0;
		uint64 totalLayouts = 0;
		uint64 totalFields = 0;
		uint64 totalStageIO = 0;
		uint64 totalPushConstants = 0;
		uint64 totalSpecialization = 0;
		uint64 totalRayDataSignatures = 0;
		uint64 totalPayloadTypes = 0;
		uint64 totalPayloadMembers = 0;
		uint64 totalBytes = 60;
		uint64 traversalBudget = uint64(limits.MaximumFields) + limits.MaximumLayouts;
		uint64 comparisonBudget = 8u * 1024u * 1024u;
		for (usize i = 0; i < artifact.EntryPoints.Size(); ++i)
		{
			const ShaderEntryPoint& entry = artifact.EntryPoints[i];
			if (!nameValid(entry.Name, limits) || entry.Stage > EShaderStage::Callable)
			{
				return fail("Invalid shader entry point.");
			}

			const bool bWorkgroup = entry.Stage == EShaderStage::Compute || entry.Stage == EShaderStage::Task || entry.Stage == EShaderStage::Mesh;
			for (uint32 size : entry.ThreadGroupSize)
			{
				if (bWorkgroup ? size == 0 || size > 65535 : size != 0)
				{
					return fail("Invalid shader thread group dimensions.");
				}
			}

			if (bWorkgroup && uint64(entry.ThreadGroupSize[0]) * entry.ThreadGroupSize[1] * entry.ThreadGroupSize[2] > 1024)
			{
				return fail("Shader thread group exceeds its bounded size.");
			}

			for (usize j = 0; j < i; ++j)
			{
				if (artifact.EntryPoints[j].Name == entry.Name)
				{
					return fail("Duplicate shader entry name.");
				}
			}

			totalBytes += 20 + entry.Name.Size();
		}

		for (usize variantIndex = 0; variantIndex < artifact.Variants.Size(); ++variantIndex)
		{
			const ShaderVariant& variant = artifact.Variants[variantIndex];
			const ShaderTargetProfile& profile = variant.Target;
			if (profile.Target > EShaderTarget::SpirV || profile.ProfileVersion != 1 || profile.ShaderModelMajor != 6 || profile.ShaderModelMinor > 9 || profile.SpirVMajor != 1 ||
				profile.SpirVMinor > 6 ||
				profile.EnvironmentMajor != 1 ||
				profile.EnvironmentMinor > 4 ||
				variant.EntryPointIndex >= artifact.EntryPoints.Size() ||
				!nameValid(variant.EmittedEntryPoint, limits) ||
				(variant.RequiredCapabilities & ~uint64(511)) != 0 ||
				variant.Bytecode.Size() > limits.MaximumCodeBytes ||
				!codeValid(variant))
			{
				return fail("Invalid shader variant profile, entry reference or code framing.");
			}

			for (usize j = 0; j < variantIndex; ++j)
			{
				if (artifact.Variants[j].EntryPointIndex == variant.EntryPointIndex && artifact.Variants[j].Target == profile)
				{
					return fail("Duplicate shader entry-target pair.");
				}
			}

			totalStageIO += variant.StageInputs.Size() + variant.StageOutputs.Size();
			totalPushConstants += variant.PushConstants.Size();
			totalSpecialization += variant.SpecializationParameters.Size();
			totalPayloadTypes += variant.TaskPayload.Types.Size();
			for (const ShaderPayloadType& type : variant.TaskPayload.Types)
			{
				totalPayloadMembers += type.MemberTypes.Size();
			}

			totalRayDataSignatures += variant.RayInterface.Data.Size();
			for (const ShaderRayDataSignature& data : variant.RayInterface.Data)
			{
				totalPayloadTypes += data.Types.Size();
				for (const ShaderPayloadType& type : data.Types)
				{
					totalPayloadMembers += type.MemberTypes.Size();
				}
			}

			if (totalRayDataSignatures > limits.MaximumRayDataSignatures || totalStageIO > limits.MaximumStageIO || totalPushConstants > limits.MaximumPushConstantRanges ||
				totalSpecialization > limits.MaximumSpecializationParameters ||
				totalPayloadTypes > limits.MaximumPayloadTypes ||
				totalPayloadMembers > limits.MaximumPayloadMembers)
			{
				return fail("Extended shader metadata exceeds aggregate limits.");
			}

			Result<void> metadata = ValidateVariantMetadata(variant, artifact.EntryPoints[variant.EntryPointIndex], artifact.AbiVersion, limits, totalBytes, comparisonBudget);
			if (!metadata)
			{
				return metadata;
			}

			totalBindings += variant.Bindings.Size();
			totalLayouts += variant.Layouts.Size();
			if (totalBindings > limits.MaximumBindings || totalLayouts > limits.MaximumLayouts || variant.ResourceUses.Size() != variant.Bindings.Size())
			{
				return fail("Shader reflection exceeds aggregate limits or omits resource uses.");
			}

			totalBytes += 64 + variant.EmittedEntryPoint.Size() + variant.Bytecode.Size() + 8 * variant.ResourceUses.Size();
			for (const ShaderDataLayout& layout : variant.Layouts)
			{
				totalFields += layout.Fields.Size();
				totalBytes += 56;
				if (totalFields > limits.MaximumFields || layout.ByteSize == 0 || layout.ByteSize > limits.MaximumTotalBytes || layout.Alignment == 0 ||
					(layout.Alignment & (layout.Alignment - 1)) != 0 ||
					layout.Alignment > 4096 ||
					(layout.ElementStride != 0 && (layout.ElementStride < layout.ByteSize || layout.ElementStride % layout.Alignment != 0)))
				{
					return fail("Invalid shader data layout extent or alignment.");
				}

				for (usize fieldIndex = 0; fieldIndex < layout.Fields.Size(); ++fieldIndex)
				{
					const ShaderDataField& field = layout.Fields[fieldIndex];
					totalBytes += 68 + field.Name.Size();
					if (!nameValid(field.Name, limits) || field.ByteSize == 0 || field.ByteOffset > layout.ByteSize || field.ByteSize > layout.ByteSize - field.ByteOffset ||
						field.Scalar > EShaderScalar::Float ||
						field.MatrixOrder > EShaderMatrixOrder::ColumnMajor ||
						field.ArrayCount == 0 ||
						field.VectorWidth == 0 ||
						field.VectorWidth > 4 ||
						field.MatrixRows == 0 ||
						field.MatrixRows > 4 ||
						field.MatrixColumns == 0 ||
						field.MatrixColumns > 4 ||
						(field.NestedLayout != NO_DATA_LAYOUT && field.NestedLayout >= variant.Layouts.Size()))
					{
						return fail("Invalid shader data field shape, extent or reference.");
					}

					uint64 element = 0;
					if (field.NestedLayout != NO_DATA_LAYOUT)
					{
						if (field.Scalar != EShaderScalar::None || field.ScalarWidth != 0 || field.VectorWidth != 1 || field.MatrixRows != 1 || field.MatrixColumns != 1 ||
							field.MatrixOrder != EShaderMatrixOrder::None ||
							field.MatrixStride != 0)
						{
							return fail("Nested shader field also declares a scalar, vector or matrix shape.");
						}

						element = variant.Layouts[field.NestedLayout].ByteSize;
					}
					else
					{
						if (field.Scalar == EShaderScalar::None || (field.ScalarWidth != 8 && field.ScalarWidth != 16 && field.ScalarWidth != 32 && field.ScalarWidth != 64))
						{
							return fail("Invalid shader scalar width.");
						}

						element = field.ScalarWidth / 8 * field.VectorWidth;
						if (field.MatrixOrder != EShaderMatrixOrder::None)
						{
							const uint32 major = field.MatrixOrder == EShaderMatrixOrder::RowMajor ? field.MatrixRows : field.MatrixColumns;
							const uint32 minor = field.MatrixOrder == EShaderMatrixOrder::RowMajor ? field.MatrixColumns : field.MatrixRows;
							const uint64 vectorBytes = uint64(minor) * (field.ScalarWidth / 8);
							if (field.VectorWidth != 1 || vectorBytes > field.ByteSize || field.MatrixStride < vectorBytes ||
								(major > 1 && field.MatrixStride > (field.ByteSize - vectorBytes) / (major - 1)))
							{
								return fail("Invalid shader matrix stride.");
							}

							element = field.MatrixStride * (major - 1) + minor * (field.ScalarWidth / 8);
						}
						else if (field.MatrixRows != 1 || field.MatrixColumns != 1 || field.MatrixStride != 0)
						{
							return fail("Non-matrix shader field declares matrix dimensions.");
						}

						if (((field.Scalar == EShaderScalar::Float && field.ScalarWidth != 16 && field.ScalarWidth != 32 && field.ScalarWidth != 64) ||
							 (field.Scalar == EShaderScalar::Bool && field.ScalarWidth != 32) ||
						     (field.Scalar == EShaderScalar::Float && field.ScalarWidth == 16 && (variant.Requirements.Features & 1) == 0) ||
						     (field.Scalar == EShaderScalar::Float && field.ScalarWidth == 64 && (variant.Requirements.Features & 2) == 0) ||
						     ((field.Scalar == EShaderScalar::SignedInteger || field.Scalar == EShaderScalar::UnsignedInteger) && field.ScalarWidth == 64 && (variant.Requirements.Features & 4) == 0)))
						{
							return fail("Shader data scalar representation omits its required numeric feature.");
						}
					}

					if (element > field.ByteSize ||
						(field.ArrayCount > 1 && (field.ArrayStride < element || field.ArrayStride > (field.ByteSize - element) / (field.ArrayCount - 1))))
					{
						return fail("Invalid shader array stride or extent.");
					}
				}

				Array<uint32> fieldOrder(layout.Fields.GetResource());
				bool bComparisonBound = false;
				auto compare = [&](const ShaderDataField& left, const ShaderDataField& right, auto less)
				{
					if (comparisonBudget == 0)
					{
						bComparisonBound = true;
					}
					else
					{
						--comparisonBudget;
					}

					return less(left, right);
				};
				if (!OrderIndices(layout.Fields, fieldOrder, [&](const ShaderDataField& left, const ShaderDataField& right) { return compare(left, right, FieldLess); }))
				{
					return Result<void>::Failure(AllocationError());
				}

				for (usize fieldIndex = 1; fieldIndex < fieldOrder.Size(); ++fieldIndex)
				{
					const ShaderDataField& previous = layout.Fields[fieldOrder[fieldIndex - 1]];
					const ShaderDataField& current = layout.Fields[fieldOrder[fieldIndex]];
					if (current.ByteOffset < previous.ByteOffset + previous.ByteSize)
					{
						return fail("Duplicate or overlapping shader fields.");
					}
				}

				if (fieldOrder.Size() > 1)
				{
					Sort(fieldOrder.begin(), fieldOrder.end(),
					    [&](uint32 left, uint32 right) { return compare(layout.Fields[left], layout.Fields[right], [](const ShaderDataField& first, const ShaderDataField& second) { return first.Name.View() < second.Name.View(); }); });
				}

				if (bComparisonBound)
				{
					return fail("Shader reflection exceeds its validation comparison bound.");
				}

				for (usize fieldIndex = 1; fieldIndex < fieldOrder.Size(); ++fieldIndex)
				{
					if (layout.Fields[fieldOrder[fieldIndex - 1]].Name == layout.Fields[fieldOrder[fieldIndex]].Name)
					{
						return fail("Duplicate or overlapping shader fields.");
					}
				}
			}

			Array<LayoutVisit> visits(variant.Layouts.GetResource());
			if (!visits.Resize(variant.Layouts.Size()))
			{
				return Result<void>::Failure(AllocationError());
			}

			for (uint32 layoutIndex = 0; layoutIndex < variant.Layouts.Size(); ++layoutIndex)
			{
				if (!isLayoutAcyclicRecursive(variant, layoutIndex, 1, limits.MaximumNestingDepth, traversalBudget, visits))
				{
					return fail("Cyclic or excessively nested shader layout.");
				}
			}

			for (usize bindingIndex = 0; bindingIndex < variant.Bindings.Size(); ++bindingIndex)
			{
				const ShaderBinding& binding = variant.Bindings[bindingIndex];
				totalBytes += 44 + binding.Name.Size() + 4 + 4;
				const bool bRuntime = binding.CountMode == EShaderDescriptorCountMode::Runtime;
				const uint64 extent = GetBindingRegisterExtent(binding);
				if (!nameValid(binding.Name, limits) || binding.Kind > EShaderBindingKind::AccelerationStructure || binding.RegisterNamespace > EShaderRegisterClass::Sampler ||
					binding.TextureShape > EShaderTextureShape::Buffer ||
				    !accessValid(binding.Access) ||
					binding.CountMode > EShaderDescriptorCountMode::Runtime ||
				    (bRuntime ? binding.ArrayCount != 0 || (variant.Requirements.Features & static_cast<uint64>(EShaderFeature::RuntimeDescriptorArray)) == 0 : binding.ArrayCount == 0 || binding.ArrayCount > 65536) ||
				    uint64(binding.Binding) + extent > uint64{0x100000000} || (binding.DataLayout != NO_DATA_LAYOUT && binding.DataLayout >= variant.Layouts.Size()) ||
				    (profile.Target == EShaderTarget::SpirV && binding.RegisterNamespace != EShaderRegisterClass::Unified) || (binding.Kind == EShaderBindingKind::ConstantBuffer && binding.DataLayout == NO_DATA_LAYOUT))
				{
					return fail("Invalid shader binding.");
				}

				if (profile.Target == EShaderTarget::Dxil)
				{
					EShaderRegisterClass expected = EShaderRegisterClass::ShaderResource;
					switch (binding.Kind)
					{
					case EShaderBindingKind::ConstantBuffer:
						expected = EShaderRegisterClass::ConstantBuffer;
						break;

					case EShaderBindingKind::Sampler:
						expected = EShaderRegisterClass::Sampler;
						break;

					case EShaderBindingKind::ReadWriteBuffer:
					case EShaderBindingKind::StorageTexture:
						expected = EShaderRegisterClass::UnorderedAccess;
						break;

					default:
						break;
					}

					if (binding.RegisterNamespace != expected)
					{
						return fail("Shader binding kind disagrees with its register class.");
					}
				}

				const bool bTexture = binding.Kind == EShaderBindingKind::SampledTexture || binding.Kind == EShaderBindingKind::StorageTexture;
				if (bTexture ? binding.TextureShape == EShaderTextureShape::None : binding.TextureShape != EShaderTextureShape::None || binding.bTextureArray || binding.bMultisampled)
				{
					return fail("Shader texture shape disagrees with its binding kind.");
				}

				if ((binding.Kind == EShaderBindingKind::ConstantBuffer || binding.Kind == EShaderBindingKind::ReadOnlyBuffer || binding.Kind == EShaderBindingKind::SampledTexture || binding.Kind == EShaderBindingKind::Sampler || binding.Kind == EShaderBindingKind::AccelerationStructure) &&
					binding.Access != EShaderResourceAccess::Read)
				{
					return fail("Read-only shader binding declares a write access.");
				}
			}

			Array<uint32> bindingOrder(variant.Bindings.GetResource());
			bool bBindingComparisonBound = false;
			auto compareBinding = [&](const ShaderBinding& left, const ShaderBinding& right, auto less)
			{
				if (comparisonBudget == 0)
				{
					bBindingComparisonBound = true;
				}
				else
				{
					--comparisonBudget;
				}

				return less(left, right);
			};
			if (!OrderIndices(variant.Bindings, bindingOrder, [&](const ShaderBinding& left, const ShaderBinding& right)
			        {
				        return compareBinding(left, right, [](const ShaderBinding& first, const ShaderBinding& second)
				            {
					            if (first.Space != second.Space)
					            {
						            return first.Space < second.Space;
					            }

					            if (first.RegisterNamespace != second.RegisterNamespace)
					            {
						            return first.RegisterNamespace < second.RegisterNamespace;
					            }

					            return first.Binding < second.Binding;
				            });
			        }))
			{
				return Result<void>::Failure(AllocationError());
			}

			for (usize bindingIndex = 1; bindingIndex < bindingOrder.Size(); ++bindingIndex)
			{
				const ShaderBinding& previous = variant.Bindings[bindingOrder[bindingIndex - 1]];
				const ShaderBinding& current = variant.Bindings[bindingOrder[bindingIndex]];
				const uint64 extent = GetBindingRegisterExtent(previous);
				if (previous.Space == current.Space && previous.RegisterNamespace == current.RegisterNamespace && uint64(current.Binding) < uint64(previous.Binding) + extent)
				{
					return fail("Duplicate or overlapping shader bindings.");
				}
			}

			if (bindingOrder.Size() > 1)
			{
				Sort(
					bindingOrder.begin(),
					bindingOrder.end(),
					[&](uint32 left, uint32 right) { return compareBinding(variant.Bindings[left], variant.Bindings[right], [](const ShaderBinding& first, const ShaderBinding& second) { return first.Name.View() < second.Name.View(); }); });
			}

			if (bBindingComparisonBound)
			{
				return fail("Shader reflection exceeds its validation comparison bound.");
			}

			for (usize bindingIndex = 1; bindingIndex < bindingOrder.Size(); ++bindingIndex)
			{
				if (variant.Bindings[bindingOrder[bindingIndex - 1]].Name == variant.Bindings[bindingOrder[bindingIndex]].Name)
				{
					return fail("Duplicate or overlapping shader bindings.");
				}
			}

			Array<uint8> seenUses(variant.ResourceUses.GetResource());
			if (!seenUses.Resize(variant.Bindings.Size()))
			{
				return Result<void>::Failure(AllocationError());
			}

			for (usize useIndex = 0; useIndex < variant.ResourceUses.Size(); ++useIndex)
			{
				const ShaderResourceUse& use = variant.ResourceUses[useIndex];
				if (use.BindingIndex >= variant.Bindings.Size() || !accessValid(use.Access) ||
					(static_cast<uint32>(use.Access) & ~static_cast<uint32>(variant.Bindings[use.BindingIndex].Access)) != 0)
				{
					return fail("Invalid shader resource use.");
				}

				if (seenUses[use.BindingIndex] != 0)
				{
					return fail("Duplicate shader resource use.");
				}

				seenUses[use.BindingIndex] = 1;
			}

			if (totalBytes > limits.MaximumTotalBytes)
			{
				return fail("Shader artifact exceeds total byte limit.");
			}
		}

		Result<void> description = ValidateDescription(artifact, limits);
		if (!description)
		{
			return description;
		}
		if (HasDescription(artifact))
		{
			Writer writer(artifact.Properties.GetResource(), true);
			Result<void> written = WriteDescription(writer, artifact);
			if (!written)
			{
				return written;
			}
			if (totalBytes + 16 + writer.GetByteCount() > limits.MaximumTotalBytes)
			{
				return fail("Shader description exceeds the total byte budget.");
			}
		}

		return Result<void>::Success();
	}

	Result<Hash256> ComputeShaderLayoutIdentity(const ShaderDataLayout& layout, MemoryResource& resource)
	{
		Writer writer(resource);
		writer.Raw("ROX-SHADER-LAYOUT", 17);
		writer.U32(SHADER_ABI_VERSION);
		WriteLayoutBody(writer, layout);
		if (!writer.IsValid())
		{
			return Result<Hash256>::Failure(AllocationError());
		}

		return Result<Hash256>::Success(ComputeSha256({writer.GetBytes().Data(), writer.GetBytes().Size()}));
	}

	namespace
	{
		template <class T>
		Result<void> append(Array<T>& array, T value)
		{
			if (!array.Add(Move(value)))
			{
				return Result<void>::Failure(AllocationError());
			}

			return Result<void>::Success();
		}
	}

	Result<void> ShaderBuilder::AddProperty(ShaderProperty value)
	{
		return append(Properties, Move(value));
	}

	Result<void> ShaderBuilder::AddPass(ShaderPass value)
	{
		return append(Passes, Move(value));
	}

	Result<void> ShaderBuilder::AddKernel(ShaderKernel value)
	{
		return append(Kernels, Move(value));
	}

	Result<void> ShaderBuilder::AddVariant(ShaderVariant value)
	{
		return append(Variants, Move(value));
	}

	Result<void> ShaderBuilder::AddPropertyBinding(ShaderPropertyBinding value)
	{
		return append(PropertyBindings, Move(value));
	}

	Result<void> ShaderBuilder::AddKeyword(StringView name)
	{
		String key(Keywords.GetResource());
		if (!key.Assign(name))
		{
			return Result<void>::Failure(AllocationError());
		}

		return append(Keywords, Move(key));
	}

	Result<uint32> ShaderBuilder::AddCompiledEntry(ShaderEntryPoint value)
	{
		const uint32 index = static_cast<uint32>(EntryPoints.Size());
		if (!EntryPoints.Add(Move(value)))
		{
			return Result<uint32>::Failure(AllocationError());
		}

		return Result<uint32>::Success(index);
	}

	Result<Shader> ShaderBuilder::Build(MemoryResource& memory, const ShaderLimits& limits, CancellationToken cancellation) &&
	{
		return Shader::Build(Move(*this), memory, limits, cancellation);
	}

	Result<Array<Byte>> ShaderBuilder::Save(MemoryResource& memory, CancellationToken token) const
	{
		return SaveShader(*this, memory, token);
	}

	Result<Array<Byte>> ShaderBuilder::Save(EShaderFormat format, MemoryResource& memory, CancellationToken token) const
	{
		return SaveShader(*this, format, memory, token);
	}

	Result<Hash256> ShaderBuilder::ComputeIdentity(MemoryResource& memory) const
	{
		return ComputeShaderIdentity(*this, memory);
	}

} // namespace rox::shader
