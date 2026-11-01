#include "DevicePipelines.h"

#if ROXELL_RHI_BACKEND == 1
#include "D3D12/D3D12Backend.h"
#elif ROXELL_RHI_BACKEND == 2
#include "Vulkan/VulkanBackend.h"
#endif

#include <Assertion.h>

namespace rox::rhi
{
	using namespace core;

	PipelineSlot::PipelineSlot(MemoryResource& resource) noexcept
		: Bindings(resource)
	{
	}

	RayTracingPipelineSlot::RayTracingPipelineSlot(MemoryResource& resource) noexcept
		: LocalRootRecipes(resource)
	{
	}

	PreparedShaderTable::PreparedShaderTable(SelectedBackendState* backend, void* native) noexcept
		: m_Backend(backend)
		, m_Native(native)
	{
		ASSERT(backend && native, "An immutable native shader table retains its backend destruction contract.");
	}

	PreparedShaderTable::~PreparedShaderTable() noexcept
	{
#if ROXELL_RHI_BACKEND == 1
		d3d12::DestroyPreparedShaderTable(m_Backend, m_Native);
#elif ROXELL_RHI_BACKEND == 2
		vulkan::DestroyPreparedShaderTable(m_Backend, m_Native);
#else
		ASSERT(false, "The Null backend does not create prepared native owners.");
#endif
	}

	ShaderTableSlot::ShaderTableSlot(MemoryResource& resource) noexcept
		: LocalData(resource)
		, TypedLocalSets(resource)
	{
	}

	DescriptorTableVersion::DescriptorTableVersion(MemoryResource& resource) noexcept
		: Entries(resource)
	{
	}

	DescriptorTableSlot::DescriptorTableSlot(MemoryResource& resource) noexcept
		: DescriptorTableVersion(resource)
		, RetiredVersions(resource)
	{
	}

	[[nodiscard]] ComputeBindingDesc BindingLayoutSlot::ComputeBuffer(uint32 index) const noexcept
	{
		if (RangeCount == 0)
		{
			const BindingLayoutEntry& entry = Entries[index];
			const EComputeBindingType type = entry.Type == EDescriptorType::UniformBuffer ? EComputeBindingType::UniformBuffer
				: entry.Type == EDescriptorType::ReadOnlyBuffer ? EComputeBindingType::ReadOnlyBuffer
				: EComputeBindingType::StorageBuffer;

			return { entry.Slot, type, entry.BufferFormat };
		}
		const GraphicsBufferBindingDesc binding = GraphicsBuffer(index);
		return { binding.Slot, static_cast<EComputeBindingType>(binding.Type), binding.Format };
	}

	[[nodiscard]] ComputeTextureBindingDesc BindingLayoutSlot::ComputeTexture(uint32 index) const noexcept
	{
		if (RangeCount == 0)
		{
			const BindingLayoutEntry& entry = Entries[ComputeCount + index];
			return { entry.Slot, entry.Type == EDescriptorType::SampledTexture ? EComputeTextureBindingType::SampledTexture : EComputeTextureBindingType::StorageTexture, entry.Sampler };
		}
		if (Kind == EBindingLayoutKind::RayTracing)
		{
			uint32 ordinal = 0;
			for (uint32 rangeIndex = 0; rangeIndex < RangeCount; ++rangeIndex)
			{
				const BindingRangeDesc range = Range(rangeIndex);
				if (range.Source != EBindingRangeSource::Fixed || (range.Type != EDescriptorType::SampledTexture && range.Type != EDescriptorType::StorageTexture))
				{
					continue;
				}
				if (index < ordinal + range.DescriptorCount)
				{
					return { range.Binding + index - ordinal, range.Type == EDescriptorType::SampledTexture ? EComputeTextureBindingType::SampledTexture : EComputeTextureBindingType::StorageTexture };
				}
				ordinal += range.DescriptorCount;
			}
			ASSERT(false, "A ray texture ordinal belongs to a fixed range.");
			return {};
		}
		const BindingRangeDesc range = Range(TextureEntries[index]);
		return { range.Binding, range.Type == EDescriptorType::SampledTexture ? EComputeTextureBindingType::SampledTexture : EComputeTextureBindingType::StorageTexture };
	}

	[[nodiscard]] GraphicsBufferBindingDesc BindingLayoutSlot::GraphicsBuffer(uint32 index) const noexcept
	{
		if (RangeCount == 0)
		{
			const BindingLayoutEntry& entry = Entries[GraphicsCount + GraphicsStorageCount + index];
			const EGraphicsBufferBindingType type = entry.Type == EDescriptorType::UniformBuffer ? EGraphicsBufferBindingType::UniformBuffer :
				entry.Type == EDescriptorType::ReadOnlyBuffer ? EGraphicsBufferBindingType::ReadOnlyBuffer
				: EGraphicsBufferBindingType::StorageBuffer;
			return { entry.Slot, type, entry.RegisterSlot, entry.BufferFormat };
		}
		const BindingRangeDesc range = Range(BufferEntries[index]);
		const EGraphicsBufferBindingType type = range.Type == EDescriptorType::UniformBuffer ? EGraphicsBufferBindingType::UniformBuffer : range.Type == EDescriptorType::ReadOnlyBuffer ? EGraphicsBufferBindingType::ReadOnlyBuffer : EGraphicsBufferBindingType::StorageBuffer;
		return { range.Binding, type, range.Binding, range.BufferFormat };
	}

	[[nodiscard]] GraphicsTextureBindingDesc BindingLayoutSlot::GraphicsTexture(uint32 index) const noexcept
	{
		if (RangeCount == 0)
		{
			const BindingLayoutEntry& entry = Entries[index];

			return { entry.Slot, entry.Sampler, entry.SamplerSlot };
		}
		GraphicsTextureBindingDesc result{};
		result.Slot = Range(TextureEntries[index]).Binding;
		return result;
	}

	[[nodiscard]] GraphicsStorageTextureBindingDesc BindingLayoutSlot::GraphicsStorageTexture(uint32 index) const noexcept
	{
		if (RangeCount == 0)
		{
			return { Entries[GraphicsCount + index].Slot };
		}

		return { Range(StorageTextureEntries[index]).Binding };
	}

	[[nodiscard]] BindingRangeDesc BindingLayoutSlot::Range(uint32 index) const noexcept
	{
		const BindingLayoutEntry& entry = Entries[index];

		return { entry.Type,
				entry.RegisterSlot == 0xffffffffu ? entry.Slot : entry.RegisterSlot,
				entry.DescriptorCount,
				entry.BufferFormat,
				static_cast<EShaderVisibilityFlags>(entry.StageMask),
				entry.StructureStrideBytes,
				entry.Source, entry.BufferMode,
				entry.bImmutableSampler ? &entry.Sampler : nullptr
			};
	}

	void BindingLayoutScratch::Capture(const BindingLayoutSlot& layout) noexcept
	{
		for (uint32 index = 0; index < layout.ComputeCount; ++index)
		{
			ComputeBuffers[index] = layout.ComputeBuffer(index);
		}
		for (uint32 index = 0; index < layout.ComputeTextureCount; ++index)
		{
			ComputeTextures[index] = layout.ComputeTexture(index);
		}
		for (uint32 index = 0; index < layout.GraphicsCount; ++index)
		{
			GraphicsTextures[index] = layout.GraphicsTexture(index);
		}
		for (uint32 index = 0; index < layout.GraphicsStorageCount; ++index)
		{
			GraphicsStorageTextures[index] = layout.GraphicsStorageTexture(index);
		}
		for (uint32 index = 0; index < layout.GraphicsBufferCount; ++index)
		{
			GraphicsBuffers[index] = layout.GraphicsBuffer(index);
		}
	}
}
