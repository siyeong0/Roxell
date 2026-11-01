#include "RayLocalRootRecipe.h"

#include <Utility.hpp>

namespace rox::rhi
{
	using namespace core;

	RayLocalRootRecipe::RayLocalRootRecipe(MemoryResource& memory) noexcept
		: m_Layouts(memory)
		, m_Arguments(memory)
	{
	}

	Result<RayLocalRootRecipe> RayLocalRootRecipe::Create(
		Span<const BindingLayoutHandle> handles,
		Span<const RayLocalRootArgumentDesc> declarations,
		Span<const NativeBindingLayoutDesc> layouts,
		MemoryResource& memory)
	{
		using R = Result<RayLocalRootRecipe>;
		const auto invalid = []
			{
				return R::Failure(EErrorCode::InvalidArgument, "Rhi.LocalRoot", "The ordered local root recipe does not match its fixed layouts.");
			};
		constexpr uint32 MAXIMUM_BYTES = 4096 - 32;
		if (handles.Size() != layouts.Size() || layouts.Size() > 4 || declarations.Size() > MAXIMUM_BYTES / 4 || (declarations.IsEmpty() && !handles.IsEmpty()))
		{
			return invalid();
		}

		RayLocalRootRecipe output(memory);
		if (!output.m_Layouts.Reserve(handles.Size()) || !output.m_Arguments.Reserve(declarations.Size()))
		{
			return R::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not capture the local root recipe.");
		}

		uint32 used[4][8]{};
		for (uint32 index = 0; index < layouts.Size(); ++index)
		{
			if (layouts[index].Ranges.IsEmpty() || layouts[index].Ranges.Size() > 8)
			{
				return invalid();
			}

			output.m_Layouts.AddRequired(handles[index]);
		}

		const auto registerClass = [](EDescriptorType type)
			{
				return type == EDescriptorType::UniformBuffer ? 0u : type == EDescriptorType::Sampler ? 1u : type == EDescriptorType::StorageBuffer || type == EDescriptorType::StorageTexture ? 2u : 3u;
			};
		for (const rhi::RayLocalRootArgumentDesc& declaration : declarations)
		{
			NativeRayLocalRootArgument argument;
			argument.Kind = declaration.Kind;
			argument.LocalLayoutIndex = declaration.LocalLayoutIndex;
			argument.ValueCount = declaration.ValueCount;
			if (declaration.Kind > ERayLocalRootArgumentKind::DescriptorTable)
			{
				return invalid();
			}

			const bool bConstants = declaration.Kind == ERayLocalRootArgumentKind::Constants32;
			if (bConstants)
			{
				if (declaration.LocalLayoutIndex != UINT32_MAX || declaration.RangeIndex != UINT32_MAX || declaration.ValueCount == 0 || declaration.ValueCount > MAXIMUM_BYTES / 4)
				{
					return invalid();
				}

				argument.Range.Type = EDescriptorType::UniformBuffer;
				argument.Range.Binding = declaration.Binding;
				argument.Space = declaration.Space;
			}
			else
			{
				if (declaration.LocalLayoutIndex >= layouts.Size() ||
					declaration.RangeIndex >= layouts[declaration.LocalLayoutIndex].Ranges.Size() ||
					declaration.ValueCount != 0 ||
					declaration.Binding != 0 ||
					declaration.Space != 0 ||
					used[declaration.LocalLayoutIndex][declaration.RangeIndex]++ != 0)
				{
					return invalid();
				}

				const rhi::NativeBindingLayoutDesc& layout = layouts[declaration.LocalLayoutIndex];
				argument.Range = layout.Range(declaration.RangeIndex);
				argument.Space = layout.Space;
				for (uint32 prior = 0; prior < declaration.RangeIndex; ++prior)
				{
					argument.DescriptorIndex += layout.Ranges[prior].DescriptorCount;
				}

				if (declaration.Kind != ERayLocalRootArgumentKind::DescriptorTable &&
					(argument.Range.DescriptorCount != 1 ||
					 argument.Range.BufferFormat != EFormat::UNKNOWN ||
					 (declaration.Kind == ERayLocalRootArgumentKind::ConstantBufferAddress
						 ? argument.Range.Type != EDescriptorType::UniformBuffer
						 : declaration.Kind == ERayLocalRootArgumentKind::ShaderResourceAddress
							 ? argument.Range.Type != EDescriptorType::ReadOnlyBuffer && argument.Range.Type != EDescriptorType::AccelerationStructure
							 : argument.Range.Type != EDescriptorType::StorageBuffer)))
				{
					return invalid();
				}
			}

			for (rhi::NativeRayLocalRootArgument const& previous : output.m_Arguments)
			{
				if (previous.Space == argument.Space &&
					registerClass(previous.Range.Type) == registerClass(argument.Range.Type) &&
					uint64(previous.Range.Binding) < uint64(argument.Range.Binding) + argument.Range.DescriptorCount &&
					uint64(argument.Range.Binding) < uint64(previous.Range.Binding) + previous.Range.DescriptorCount)
				{
					return invalid();
				}
			}

			const uint32 alignment = bConstants ? 4 : 8;
			output.m_ByteSize = (output.m_ByteSize + alignment - 1) & ~(alignment - 1);
			argument.ByteOffset = output.m_ByteSize;
			const uint32 bytes = bConstants ? declaration.ValueCount * 4 : 8;
			if (output.m_ByteSize > MAXIMUM_BYTES || bytes > MAXIMUM_BYTES - output.m_ByteSize)
			{
				return invalid();
			}

			output.m_ByteSize += bytes;
			output.m_Arguments.AddRequired(argument);
		}

		for (uint32 index = 0; index < layouts.Size(); ++index)
		{
			for (uint32 range = 0; range < layouts[index].Ranges.Size(); ++range)
			{
				if (used[index][range] != 1)
				{
					return invalid();
				}
			}
		}

		return R::Success(Move(output));
	}

	Span<const BindingLayoutHandle> RayLocalRootRecipe::GetLayouts() const noexcept
	{
		return m_Layouts.View();
	}

	Span<const NativeRayLocalRootArgument> RayLocalRootRecipe::GetArguments() const noexcept
	{
		return m_Arguments.View();
	}

	uint32 RayLocalRootRecipe::GetByteSize() const noexcept
	{
		return m_ByteSize;
	}
}
