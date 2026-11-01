#include "VulkanResources.h"
#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	Texture::Texture(MemoryResource& resource) noexcept
		: SubresourceLayouts(resource)
	{
	}

	[[nodiscard]] VkImageView Texture::GetStorageView() const noexcept
	{
		return StorageView != VK_NULL_HANDLE ? StorageView : View;
	}

	DescriptorTable::DescriptorTable(MemoryResource& resource) noexcept
		: Textures(resource)
		, TextureViews(resource)
		, Buffers(resource)
	{
	}

	DescriptorSetRealization::DescriptorSetRealization(MemoryResource& resource) noexcept
		: Images(resource)
	{
	}

	RayTracingPipeline::RayTracingPipeline(MemoryResource& resource) noexcept
		: GroupHandles(resource)
	{
	}
}
