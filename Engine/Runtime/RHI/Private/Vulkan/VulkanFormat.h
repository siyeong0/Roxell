#pragma once

#include <RhiFormat.h>

#include <vulkan/vulkan.h>

namespace rox::rhi
{
	[[nodiscard]] VkFormat ConvertVulkanFormat(EFormat format) noexcept;
}
