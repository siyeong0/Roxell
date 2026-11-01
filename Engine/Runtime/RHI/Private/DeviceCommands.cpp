#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	CommandSlot::CommandSlot(MemoryResource& resource) noexcept
		: Copies(resource)
		, TextureCopies(resource)
		, TextureUses(resource)
		, BufferUses(resource)
		, AccelUses(resource)
		, AccelBuildTargets(resource)
		, OpacityMicromapUses(resource)
		, OpacityMicromapBuildTargets(resource)
		, PipelineUses(resource)
		, GraphicsPipelineUses(resource)
		, RayPipelineUses(resource)
		, FramebufferUses(resource)
		, TimerUses(resource)
		, SamplerUses(resource)
		, TextureViewUses(resource)
		, DescriptorTableUses(resource)
	{
	}

	[[nodiscard]] bool CommandSlot::HasBufferState(BufferHandle buffer, ETrackedResourceState expected) const noexcept
	{
		for (usize index = BufferUses.Size(); index != 0; --index)
		{
			const RecordedBufferUse& use = BufferUses[index - 1];

			if (use.Handle == buffer && use.State != ETrackedResourceState::Unknown)
			{
				return use.State == expected;
			}
		}

		return false;
	}

	[[nodiscard]] bool CommandSlot::AddBufferUse(RecordedBufferUse use) noexcept
	{
		use.bAutomaticBarriers = bAutomaticBarriers;

		if (!BufferUses.Add(use))
		{
			return false;
		}
		bHasManualBufferUses |= !use.bAutomaticBarriers && !use.bExplicitStateRequest && use.State != ETrackedResourceState::Unknown;

		return true;
	}
}

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] BindingRangeDesc NativeBindingLayoutDesc::Range(uint32 index) const noexcept
	{
		const BindingLayoutEntry& entry = Ranges[index];
		return { entry.Type, entry.RegisterSlot == 0xffffffffu ? entry.Slot : entry.RegisterSlot,
			entry.DescriptorCount, entry.BufferFormat, static_cast<EShaderVisibilityFlags>(entry.StageMask),
			entry.StructureStrideBytes, entry.Source, entry.BufferMode,
			entry.bImmutableSampler ? &entry.Sampler : nullptr };
	}

	[[nodiscard]] BindingRangeDesc NativeGraphicsBindingLayoutDesc::Range(uint32 index) const noexcept
	{
		const NativeBindingLayoutDesc layout{ {}, {}, Ranges, Space };
		return layout.Range(index);
	}

	[[nodiscard]] uint32 NativeBindingLayoutDesc::BufferCount() const noexcept
	{
		return ScalarEntries.IsEmpty() ? static_cast<uint32>(Buffers.Size()) : ScalarBufferCount;
	}

	[[nodiscard]] uint32 NativeBindingLayoutDesc::TextureCount() const noexcept
	{
		return ScalarEntries.IsEmpty() ? static_cast<uint32>(Textures.Size()) : static_cast<uint32>(ScalarEntries.Size()) - ScalarBufferCount;
	}

	[[nodiscard]] ComputeBindingDesc NativeBindingLayoutDesc::Buffer(uint32 index) const noexcept
	{
		if (ScalarEntries.IsEmpty())
		{
			return Buffers[index];
		}
		const BindingLayoutEntry& entry = ScalarEntries[index];
		const EComputeBindingType type = entry.Type == EDescriptorType::UniformBuffer ? EComputeBindingType::UniformBuffer : entry.Type == EDescriptorType::ReadOnlyBuffer ? EComputeBindingType::ReadOnlyBuffer : EComputeBindingType::StorageBuffer;
		return { entry.Slot, type, entry.BufferFormat };
	}

	[[nodiscard]] ComputeTextureBindingDesc NativeBindingLayoutDesc::Texture(uint32 index) const noexcept
	{
		if (ScalarEntries.IsEmpty())
		{
			return Textures[index];
		}
		const BindingLayoutEntry& entry = ScalarEntries[ScalarBufferCount + index];
		return { entry.Slot, entry.Type == EDescriptorType::SampledTexture ? EComputeTextureBindingType::SampledTexture : EComputeTextureBindingType::StorageTexture, entry.Sampler };
	}

	[[nodiscard]] uint32 NativeGraphicsBindingLayoutDesc::SampledCount() const noexcept
	{
		return ScalarEntries.IsEmpty() ? static_cast<uint32>(SampledTextures.Size()) : ScalarSampledCount;
	}

	[[nodiscard]] uint32 NativeGraphicsBindingLayoutDesc::StorageCount() const noexcept
	{
		return ScalarEntries.IsEmpty() ? static_cast<uint32>(StorageTextures.Size()) : ScalarStorageCount;
	}

	[[nodiscard]] uint32 NativeGraphicsBindingLayoutDesc::BufferCount() const noexcept
	{
		return ScalarEntries.IsEmpty() ? static_cast<uint32>(Buffers.Size()) : static_cast<uint32>(ScalarEntries.Size()) - ScalarSampledCount - ScalarStorageCount;
	}

	[[nodiscard]] GraphicsTextureBindingDesc NativeGraphicsBindingLayoutDesc::Sampled(uint32 index) const noexcept
	{
		if (ScalarEntries.IsEmpty())
		{
			return SampledTextures[index];
		}
		const BindingLayoutEntry& entry = ScalarEntries[index];
		return { entry.Slot, entry.Sampler, entry.SamplerSlot };
	}

	[[nodiscard]] GraphicsStorageTextureBindingDesc NativeGraphicsBindingLayoutDesc::Storage(uint32 index) const noexcept
	{
		return ScalarEntries.IsEmpty() ? StorageTextures[index] : GraphicsStorageTextureBindingDesc{ ScalarEntries[ScalarSampledCount + index].Slot };
	}

	[[nodiscard]] GraphicsBufferBindingDesc NativeGraphicsBindingLayoutDesc::Buffer(uint32 index) const noexcept
	{
		if (ScalarEntries.IsEmpty())
		{
			return Buffers[index];
		}
		const BindingLayoutEntry& entry = ScalarEntries[ScalarSampledCount + ScalarStorageCount + index];
		const EGraphicsBufferBindingType type = entry.Type == EDescriptorType::UniformBuffer ? EGraphicsBufferBindingType::UniformBuffer : entry.Type == EDescriptorType::ReadOnlyBuffer ? EGraphicsBufferBindingType::ReadOnlyBuffer : EGraphicsBufferBindingType::StorageBuffer;
		return { entry.Slot, type, entry.RegisterSlot, entry.BufferFormat };
	}
} // namespace rox::rhi
