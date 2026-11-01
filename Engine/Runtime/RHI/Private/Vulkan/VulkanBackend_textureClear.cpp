#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] VkClearColorValue MakeClearColor(const ClearColor& color) noexcept
	{
		VkClearColorValue value{};
		if (color.GetType() == EClearColorType::Uint)
		{
			for (uint32 index = 0; index < 4; ++index)
			{
				value.uint32[index] = color.GetUint()[index];
			}
		}
		else if (color.GetType() == EClearColorType::Sint)
		{
			for (uint32 index = 0; index < 4; ++index)
			{
				value.int32[index] = color.GetSint()[index];
			}
		}
		else
		{
			value.float32[0] = color.GetFloat()[0];
			value.float32[1] = color.GetFloat()[1];
			value.float32[2] = color.GetFloat()[2];
			value.float32[3] = color.GetFloat()[3];
		}

		return value;
	}

	[[nodiscard]] Result<void> RecordColorClearValue(State& state, Pending& pending, Texture& texture, TextureSubresource subresource, const VkClearColorValue& value)
	{
		Result<void> prepared = PrepareSwapChainTarget(pending, texture);

		if (!prepared)
		{
			return prepared;
		}

		const uint32 subresourceIndex = subresource.MipLevel + subresource.ArrayLayer * texture.MipLevels;
		RequireTextureLayout(state, pending, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceIndex);
		// The poisoned list is rejected at submission; do not emit an invalid native command.
		if (pending.bInvalidTextureLayouts)
		{
			return Result<void>::Success();
		}
		VkImageSubresourceRange range{};
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = subresource.MipLevel;
		range.levelCount = 1;
		range.baseArrayLayer = subresource.ArrayLayer;
		range.layerCount = 1;
		state.F.CmdClearColorImage(pending.Commands, texture.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);

		if (pending.bAutomaticBarriers && texture.SwapChainOwner != nullptr)
		{
			RequireTextureLayout(state, pending, texture, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, subresourceIndex);
		}
		else if (pending.bAutomaticBarriers && (texture.bShaderRead || texture.bStorage))
		{
			RequireTextureLayout(state, pending, texture, VK_IMAGE_LAYOUT_GENERAL);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureClear(State* opaque, void* native, void* target, const TextureDesc&, TextureSubresource subresource, const ClearColor& color)
	{
		const VkClearColorValue value = MakeClearColor(color);

		return RecordColorClearValue(*opaque, *static_cast<Pending*>(native), *static_cast<Texture*>(target),
			subresource, value);
	}

	[[nodiscard]] Result<void> RecordTextureUIntClear(State* opaque, void* native, void* target, const TextureDesc&, TextureSubresource subresource, uint32 clearValue)
	{
		VkClearColorValue value{};
		value.uint32[0] = clearValue;
		value.uint32[1] = clearValue;
		value.uint32[2] = clearValue;
		value.uint32[3] = clearValue;

		return RecordColorClearValue(*opaque, *static_cast<Pending*>(native), *static_cast<Texture*>(target),
			subresource, value);
	}

	[[nodiscard]] Result<void> RecordDepthClear(State* opaque, void* native, void* target, const TextureDesc&, TextureSubresource subresource, DepthStencilClear clear)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(target);
		const uint32 subresourceIndex = subresource.MipLevel + subresource.ArrayLayer * texture.MipLevels;
		RequireTextureLayout(state, pending, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceIndex);
		// The poisoned list is rejected at submission; do not emit an invalid native command.
		if (pending.bInvalidTextureLayouts)
		{
			return Result<void>::Success();
		}

		const VkClearDepthStencilValue value{ clear.Depth, clear.Stencil };
		VkImageSubresourceRange range{};
		range.aspectMask = (clear.bClearDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) | (clear.bClearStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
		range.baseMipLevel = subresource.MipLevel;
		range.levelCount = 1;
		range.baseArrayLayer = subresource.ArrayLayer;
		range.layerCount = 1;
		state.F.CmdClearDepthStencilImage(pending.Commands, texture.Handle, TextureLayout(texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
			&value, 1, &range);
		const VkImageLayout restored = texture.bShaderRead ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		if (pending.bAutomaticBarriers)
		{
			RequireTextureLayout(state, pending, texture, restored, texture.bShaderRead ? UINT32_MAX : subresourceIndex);
		}

		return Result<void>::Success();
	}
}
