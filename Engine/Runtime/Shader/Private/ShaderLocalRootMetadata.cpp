#include "ShaderLocalRootMetadata.h"

#include "ShaderRayMetadata.h"
#include "ShaderWire.hpp"

#include <Comparison.hpp>
#include <ShaderBuilder.h>
#include <Types.h>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		Result<void> hashPhysicalLayout(
			const ShaderVariant& variant,
			uint32 index,
			Array<Hash256>& hashes,
			Array<uint8>& states,
			MemoryResource& resource,
			uint32 depth,
			uint64& budget)
		{
			using R = Result<void>;
			if (index >= variant.Layouts.Size() || depth > 64 || states[index] == 1 || budget == 0)
			{
				return R::Failure(MakeArtifactError("Invalid local record layout graph."));
			}

			if (states[index] == 2)
			{
				return R::Success();
			}

			states[index] = 1;
			--budget;
			const ShaderDataLayout& layout = variant.Layouts[index];
			Writer writer(resource, true);
			writer.U64(layout.ByteSize);
			writer.U32(layout.Alignment);
			writer.U64(layout.ElementStride);
			writer.U32(static_cast<uint32>(layout.Fields.Size()));
			for (const ShaderDataField& field : layout.Fields)
			{
				if (budget == 0)
				{
					return R::Failure(MakeArtifactError("Local record layout exceeds its traversal bound."));
				}
				--budget;
				writer.U64(field.ByteOffset);
				writer.U64(field.ByteSize);
				writer.Enum(field.Scalar);
				writer.U32(field.ScalarWidth);
				writer.U32(field.VectorWidth);
				writer.U32(field.MatrixRows);
				writer.U32(field.MatrixColumns);
				writer.Enum(field.MatrixOrder);
				writer.U64(field.MatrixStride);
				writer.U32(field.ArrayCount);
				writer.U64(field.ArrayStride);
				writer.U32(field.NestedLayout != NO_DATA_LAYOUT ? 1 : 0);
				if (field.NestedLayout != NO_DATA_LAYOUT)
				{
					Result<void> child = hashPhysicalLayout(variant, field.NestedLayout, hashes, states, resource, depth + 1, budget);
					if (!child)
					{
						return child;
					}

					writer.Hash(hashes[field.NestedLayout]);
				}
			}

			if (!writer.IsValid())
			{
				return R::Failure(AllocationError());
			}

			hashes[index] = writer.GetHash();
			states[index] = 2;
			return R::Success();
		}
	} // namespace

	Result<Hash256> ComputeShaderLocalRecordPhysicalIdentity(const ShaderVariant& variant, MemoryResource& resource)
	{
		using R = Result<Hash256>;
		const ShaderLimits limits;
		if (variant.Layouts.Size() > limits.MaximumLayouts || variant.LocalRootArguments.Size() > limits.MaximumBindings)
		{
			return R::Failure(MakeArtifactError("Local record identity exceeds its input bound."));
		}

		Array<Hash256> hashes(resource);
		Array<uint8> states(resource);
		if (!hashes.Resize(variant.Layouts.Size()) || !states.Resize(variant.Layouts.Size()))
		{
			return R::Failure(AllocationError());
		}

		uint64 budget = 8u * 1024u * 1024u;
		Writer writer(resource, true);
		writer.Raw("ROX-SHADER-LOCAL-PHYSICAL", 25);
		writer.U32(1);
		writer.Enum(variant.LocalRecord.Lowering);
		writer.U32(variant.LocalRecord.ByteSize);
		writer.U32(variant.LocalRecord.Alignment);
		auto writeLayout = [&](uint32 index) -> Result<void>
		{
			writer.U32(index == NO_DATA_LAYOUT ? 0 : 1);
			if (index == NO_DATA_LAYOUT)
			{
				return Result<void>::Success();
			}

			Result<void> hashed = hashPhysicalLayout(variant, index, hashes, states, resource, 1, budget);
			if (hashed)
			{
				writer.Hash(hashes[index]);
			}

			return hashed;
		};
		Result<void> recordLayout = writeLayout(variant.LocalRecord.DataLayout);
		if (!recordLayout)
		{
			return R::Failure(recordLayout.GetError());
		}

		writer.U32(static_cast<uint32>(variant.LocalRootArguments.Size()));
		for (const ShaderLocalRootArgument& argument : variant.LocalRootArguments)
		{
			if (argument.BindingIndex >= variant.Bindings.Size())
			{
				return R::Failure(MakeArtifactError("Invalid local record binding index."));
			}

			const ShaderBinding& binding = variant.Bindings[argument.BindingIndex];
			writer.Enum(argument.Kind);
			writer.U32(argument.ByteOffset);
			writer.U32(argument.ValueCount);
			writer.Enum(binding.Kind);
			writer.U32(binding.Space);
			writer.Enum(binding.RegisterNamespace);
			writer.U32(binding.Binding);
			writer.U32(binding.ArrayCount);
			writer.Enum(binding.CountMode);
			writer.Enum(binding.Access);
			writer.Enum(binding.TextureShape);
			writer.U32(binding.bTextureArray ? 1 : 0);
			writer.U32(binding.bMultisampled ? 1 : 0);
			Result<void> layout = writeLayout(binding.DataLayout);
			if (!layout)
			{
				return R::Failure(layout.GetError());
			}
		}

		if (!writer.IsValid())
		{
			return R::Failure(AllocationError());
		}

		return R::Success(writer.GetHash());
	}

	Result<void> ValidateLocalRootMetadata(const ShaderVariant& variant, EShaderStage stage, const ShaderLimits& limits, uint64& totalBytes)
	{
		using R = Result<void>;
		auto fail = [] { return R::Failure(MakeArtifactError("Invalid declared native local root recipe.")); };
		const ShaderLocalRecord& record = variant.LocalRecord;
		uint32 localCount = 0;
		for (const ShaderBinding& binding : variant.Bindings)
		{
			if (binding.Scope > EShaderBindingScope::LocalRecord)
			{
				return fail();
			}

			localCount += binding.Scope == EShaderBindingScope::LocalRecord ? 1u : 0u;
		}

		if (record.Lowering != EShaderLocalRecordLowering::D3D12LocalRootArguments)
		{
			if (localCount != 0 || !variant.LocalRootArguments.IsEmpty() || record.Proof != (record.Lowering == EShaderLocalRecordLowering::VulkanShaderRecord ? EShaderLocalRecordProof::FinalSpirVStorage : EShaderLocalRecordProof::None))
			{
				return fail();
			}

			return R::Success();
		}

		if (!IsRayStage(stage) || variant.Target.Target != EShaderTarget::Dxil || record.Proof != EShaderLocalRecordProof::DeclaredDxilRecipe ||
			record.DataLayout != NO_DATA_LAYOUT ||
			variant.LocalRootArguments.IsEmpty() ||
			variant.LocalRootArguments.Size() > limits.MaximumBindings ||
			localCount != variant.LocalRootArguments.Size() ||
			record.ByteSize > MAXIMUM_DXIL_LOCAL_ROOT_BYTES)
		{
			return fail();
		}

		uint32 extent = 0;
		uint32 alignment = 4;
		for (usize index = 0; index < variant.LocalRootArguments.Size(); ++index)
		{
			const ShaderLocalRootArgument& argument = variant.LocalRootArguments[index];
			if (argument.Kind > EShaderLocalRootArgumentKind::DescriptorTable || argument.BindingIndex >= variant.Bindings.Size())
			{
				return fail();
			}

			const ShaderBinding& binding = variant.Bindings[argument.BindingIndex];
			if (binding.Scope != EShaderBindingScope::LocalRecord || binding.CountMode != EShaderDescriptorCountMode::Fixed)
			{
				return fail();
			}

			for (usize previous = 0; previous < index; ++previous)
			{
				if (variant.LocalRootArguments[previous].BindingIndex == argument.BindingIndex)
				{
					return fail();
				}
			}

			const bool bConstants = argument.Kind == EShaderLocalRootArgumentKind::Constants32;
			const uint32 argumentAlignment = bConstants ? 4u : 8u;
			extent = (extent + argumentAlignment - 1) & ~(argumentAlignment - 1);
			if (argument.ByteOffset != extent || (!bConstants && argument.ValueCount != 0))
			{
				return fail();
			}

			uint32 bytes = 8;
			switch (argument.Kind)
			{
			case EShaderLocalRootArgumentKind::Constants32:
				if (binding.Kind != EShaderBindingKind::ConstantBuffer || binding.ArrayCount != 1 || binding.DataLayout >= variant.Layouts.Size() || argument.ValueCount == 0 ||
					argument.ValueCount > MAXIMUM_DXIL_LOCAL_ROOT_BYTES / 4 ||
					variant.Layouts[binding.DataLayout].ByteSize > uint64(argument.ValueCount) * 4)
				{
					return fail();
				}

				bytes = argument.ValueCount * 4;
				break;
			case EShaderLocalRootArgumentKind::ConstantBufferAddress:
				if (binding.Kind != EShaderBindingKind::ConstantBuffer || binding.ArrayCount != 1)
				{
					return fail();
				}
				break;
			case EShaderLocalRootArgumentKind::ShaderResourceAddress:
				if ((binding.Kind != EShaderBindingKind::ReadOnlyBuffer && binding.Kind != EShaderBindingKind::AccelerationStructure) || binding.ArrayCount != 1)
				{
					return fail();
				}
				break;
			case EShaderLocalRootArgumentKind::UnorderedAccessAddress:
				if (binding.Kind != EShaderBindingKind::ReadWriteBuffer || binding.ArrayCount != 1)
				{
					return fail();
				}
				break;
			case EShaderLocalRootArgumentKind::DescriptorTable:
				break;

			default:
			{
				break;
			}
			}

			if (extent > MAXIMUM_DXIL_LOCAL_ROOT_BYTES || bytes > MAXIMUM_DXIL_LOCAL_ROOT_BYTES - extent)
			{
				return fail();
			}

			extent += bytes;
			alignment = Max(alignment, argumentAlignment);
		}

		if (record.ByteSize != extent || record.Alignment != alignment)
		{
			return fail();
		}

		totalBytes += 16 * variant.LocalRootArguments.Size();
		return R::Success();
	}

	Result<void> WriteLocalRootMetadata(Writer& writer, const ShaderVariant& variant, const Array<uint32>* bindingMap)
	{
		writer.Enum(variant.LocalRecord.Proof);
		writer.U32(static_cast<uint32>(variant.LocalRootArguments.Size()));
		for (const ShaderLocalRootArgument& argument : variant.LocalRootArguments)
		{
			writer.Enum(argument.Kind);
			writer.U32(bindingMap == nullptr ? argument.BindingIndex : (*bindingMap)[argument.BindingIndex]);
			writer.U32(argument.ByteOffset);
			writer.U32(argument.ValueCount);
		}

		if (!writer.IsValid())
		{
			return Result<void>::Failure(AllocationError());
		}

		return Result<void>::Success();
	}

	Result<void> ReadLocalRootMetadata(Reader& reader, ShaderVariant& variant, const ShaderLimits& limits)
	{
		using R = Result<void>;
		variant.LocalRecord.Proof = reader.Enum<EShaderLocalRecordProof>();
		const uint32 count = reader.U32();
		uint64 total = 0;
		if (!reader.Count(count, total, limits.MaximumBindings, 16))
		{
			return R::Failure(MakeArtifactError("Invalid local root argument count."));
		}

		if (!variant.LocalRootArguments.Reserve(count))
		{
			return R::Failure(AllocationError());
		}

		for (uint32 index = 0; index < count; ++index)
		{
			ShaderLocalRootArgument argument;
			argument.Kind = reader.Enum<EShaderLocalRootArgumentKind>();
			argument.BindingIndex = reader.U32();
			argument.ByteOffset = reader.U32();
			argument.ValueCount = reader.U32();
			variant.LocalRootArguments.AddRequired(argument);
		}

		if (!reader.IsValid())
		{
			return R::Failure(MakeArtifactError("Truncated local root metadata."));
		}

		return R::Success();
	}

} // namespace rox::shader
