#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	void Barrier(State& state, Pending& pending) noexcept
	{
		const VkQueueFlags flags = QueueFlags(state, pending.QueueKind);
		VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		VkPipelineStageFlags destinationStages = VK_PIPELINE_STAGE_TRANSFER_BIT;

		if ((flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0)
		{
			barrier.dstAccessMask |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			destinationStages |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		}

		if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0)
		{
			barrier.dstAccessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
			destinationStages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		}
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_TRANSFER_BIT, destinationStages, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	}

	void BufferTransferBarrier(State& state, Pending& pending, VkBuffer buffer) noexcept
	{
		const VkQueueFlags flags = QueueFlags(state, pending.QueueKind);
		VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		VkPipelineStageFlags destinationStages = VK_PIPELINE_STAGE_TRANSFER_BIT;

		if ((flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0)
		{
			barrier.dstAccessMask |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			destinationStages |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		}

		if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0)
		{
			barrier.dstAccessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
			destinationStages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		}
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = buffer;
		barrier.offset = 0;
		barrier.size = VK_WHOLE_SIZE;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_TRANSFER_BIT, destinationStages, 0, 0, nullptr, 1, &barrier, 0, nullptr);
	}

	void BufferTransferAccessBarrier(State& state, Pending& pending, VkBuffer buffer, VkAccessFlags access) noexcept
	{
		VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.dstAccessMask = access;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = buffer;
		barrier.size = VK_WHOLE_SIZE;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &barrier, 0,
			nullptr);
	}

	void BufferHostReadBarrier(State& state, Pending& pending, VkBuffer buffer) noexcept
	{
		VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = buffer;
		barrier.size = VK_WHOLE_SIZE;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0,
			nullptr);
	}

	void EmitTextureBarrier(
		State& state,
		Pending& pending,
		const Texture& texture,
		VkImageLayout before,
		VkImageLayout next,
		uint32 baseMip,
		uint32 mipCount,
		uint32 baseLayer,
		uint32 layerCount) noexcept
	{
		if (before == next &&
			(next == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ||
			 next == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
			 next == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
			 next == VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR))
		{
			return;
		}

		const VkQueueFlags flags = QueueFlags(state, pending.QueueKind);
		const auto supportsAccess = [flags](VkImageLayout layout) noexcept
			{
				if (layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
				{
					return (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0;
				}

				if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
					layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
					layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
					layout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL ||
					layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL ||
					layout == VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR)
				{
					return (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
				}

				return true;
			};
		const auto access = [supportsAccess, flags, &texture](VkImageLayout layout) noexcept -> VkAccessFlags
			{
				if (!supportsAccess(layout))
				{
					return 0;
				}

				switch (layout)
				{
				case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
					return VK_ACCESS_TRANSFER_WRITE_BIT;
				case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
					return VK_ACCESS_TRANSFER_READ_BIT;
				case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
					return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
					return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
				case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
				case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
					// A preserving render pass still performs an attachment store operation.
					return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
						(texture.bShaderRead ? VK_ACCESS_SHADER_READ_BIT : 0);
				case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
					return VK_ACCESS_SHADER_READ_BIT;
				case VK_IMAGE_LAYOUT_GENERAL:
					return VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
						((flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0
							? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
							: 0) |
						((flags & VK_QUEUE_GRAPHICS_BIT) != 0 ? VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
							: 0) |
						(texture.bDepth && (flags & VK_QUEUE_GRAPHICS_BIT) != 0
							? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
							VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
							: 0);
				case VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR:
					return VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
				default:
					return 0;
				}
			};
		const VkPipelineStageFlags rayStage = state.bRayTracingPipeline ? VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR : 0;
		const auto stage = [flags, supportsAccess, rayStage](VkImageLayout layout) noexcept -> VkPipelineStageFlags
			{
				if (!supportsAccess(layout))
				{
					return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
				}

				switch (layout)
				{
				case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
					return VK_PIPELINE_STAGE_TRANSFER_BIT;
				case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
					return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
					return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
				case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
				case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
					return VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | rayStage;
				case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
					return VK_PIPELINE_STAGE_TRANSFER_BIT |
						((flags & VK_QUEUE_GRAPHICS_BIT) != 0
							? VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | rayStage
							: VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | rayStage);
				case VK_IMAGE_LAYOUT_GENERAL:
					return VK_PIPELINE_STAGE_TRANSFER_BIT | ((flags & VK_QUEUE_GRAPHICS_BIT) != 0 ? VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT : 0) |
						((flags & VK_QUEUE_COMPUTE_BIT) != 0 ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : 0) |
						((flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0 ? rayStage : 0);
				case VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR:
					return VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
				case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
					return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
				default:
					return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
				}
			};
		VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.oldLayout = before;
		barrier.newLayout = next;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = texture.Handle;
		barrier.subresourceRange.aspectMask = texture.bDepth ? VK_IMAGE_ASPECT_DEPTH_BIT | (GetFormatInfo(texture.Format).bHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0) : VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = baseMip;
		barrier.subresourceRange.levelCount = mipCount;
		barrier.subresourceRange.baseArrayLayer = baseLayer;
		barrier.subresourceRange.layerCount = layerCount;
		barrier.srcAccessMask = access(before);
		barrier.dstAccessMask = access(next);
		RecordBarriers(state, pending, stage(before), stage(next), 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	[[nodiscard]] Pending::TextureLayoutState* TrackTextureLayout(Pending& pending, Texture& texture) noexcept
	{
		for (vulkan::Pending::TextureLayoutState& tracked : pending.TextureLayouts)
		{
			if (tracked.Item == &texture)
			{
				return &tracked;
			}
		}
		Pending::TextureLayoutState tracked(pending.TextureLayouts.GetResource());
		tracked.Item = &texture;
		tracked.WholeLayout = texture.Layout;

		if (!texture.SubresourceLayouts.IsEmpty())
		{
			if (!tracked.SubresourceLayouts.ResizeUninitialized(texture.SubresourceLayouts.Size()))
			{
				pending.bInvalidTextureLayouts = true;

				return nullptr;
			}

			for (usize index = 0; index < texture.SubresourceLayouts.Size(); ++index)
			{
				tracked.SubresourceLayouts[index] = texture.SubresourceLayouts[index];
			}
		}

		if (!pending.TextureLayouts.Add(Move(tracked)))
		{
			pending.bInvalidTextureLayouts = true;

			return nullptr;
		}

		return &pending.TextureLayouts.Back();
	}

	[[nodiscard]] bool TextureHasLayout(const Pending& pending, const Texture& texture, VkImageLayout layout, const TextureViewDesc* range) noexcept
	{
		const uint32 baseMip = range != nullptr ? range->BaseMipLevel : 0;
		const uint32 mipCount = range != nullptr ? range->MipLevelCount : texture.MipLevels;
		const uint32 baseLayer = range != nullptr ? range->BaseArrayLayer : 0;
		const uint32 layerCount = range != nullptr ? range->ArrayLayerCount : texture.ArrayLayers;
		const auto matches = [&](const Array<VkImageLayout>& layouts) noexcept
			{
				for (uint32 layer = baseLayer; layer < baseLayer + layerCount; ++layer)
				{
					for (uint32 mip = baseMip; mip < baseMip + mipCount; ++mip)
					{
						if (layouts[layer * texture.MipLevels + mip] != layout)
						{
							return false;
						}
					}
				}

				return true;
			};
		for (vulkan::Pending::TextureLayoutState const& tracked : pending.TextureLayouts)
		{
			if (tracked.Item != &texture)
			{
				continue;
			}

			if (tracked.SubresourceLayouts.IsEmpty())
			{
				return tracked.WholeLayout == layout;
			}

			return matches(tracked.SubresourceLayouts);
		}

		if (texture.SubresourceLayouts.IsEmpty())
		{
			return texture.Layout == layout;
		}

		return matches(texture.SubresourceLayouts);
	}

	[[nodiscard]] VkImageLayout SampledTextureLayout(const Pending& pending, const Texture& texture, const TextureViewDesc* range) noexcept
	{
		if (texture.bDepth)
		{
			for (VkImageLayout const layout :
			{VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL})
			{
				if (TextureHasLayout(pending, texture, layout, range))
				{
					return layout;
				}
			}
		}
		if (texture.bDepth && TextureHasLayout(pending, texture, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, range))
		{
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}

		return TextureHasLayout(pending, texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range)
			? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			: VK_IMAGE_LAYOUT_GENERAL;
	}

	[[nodiscard]] VkImageLayout TextureLayout(const Texture& texture, VkImageLayout layout) noexcept
	{
		return GetFormatInfo(texture.Format).bHasStencil && layout != VK_IMAGE_LAYOUT_UNDEFINED &&
			layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
			layout != VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL &&
			layout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL
			? VK_IMAGE_LAYOUT_GENERAL
			: layout;
	}

	[[nodiscard]] VkImageLayout DepthAttachmentLayout(EFormat format, bool bDepthReadOnly, bool bStencilReadOnly) noexcept
	{
		if (!GetFormatInfo(format).bHasStencil)
		{
			return bDepthReadOnly ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
		if (bDepthReadOnly && bStencilReadOnly)
		{
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}
		if (bDepthReadOnly)
		{
			return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
		}
		if (bStencilReadOnly)
		{
			return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
		}

		return VK_IMAGE_LAYOUT_GENERAL;
	}

	[[nodiscard]] bool SupportsAttachmentSamplingLayout(const State& state, VkImageLayout layout) noexcept
	{
		const bool bMixed = layout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL || layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;

		return (layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && !bMixed) || (state.bLoadStoreOpNone && (!bMixed || state.bSeparateDepthStencilAttachmentAccess));
	}

	[[nodiscard]] VkImageLayout MapTextureState(ETextureState requested) noexcept
	{
		VkImageLayout next = VK_IMAGE_LAYOUT_UNDEFINED;

		switch (requested)
		{
		case ETextureState::CopySource:
			next = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			break;
		case ETextureState::CopyDestination:
			next = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			break;
		case ETextureState::ResolveSource:
			next = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			break;
		case ETextureState::ResolveDestination:
			next = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			break;
		case ETextureState::ShaderRead:
			next = VK_IMAGE_LAYOUT_GENERAL;
			break;
		case ETextureState::Storage:
			next = VK_IMAGE_LAYOUT_GENERAL;
			break;
		case ETextureState::Present:
			next = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			break;
		case ETextureState::RenderTarget:
			next = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			break;
		case ETextureState::DepthWrite:
			next = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			break;
		case ETextureState::DepthRead:
			next = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			break;
		case ETextureState::ShadingRateSource:
			next = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
			break;

		default:
		{
			break;
		}
		}

		return next;
	}

	[[nodiscard]] VkImageLayout PreparedTextureLayout(const Texture& texture, Span<const NativePreparedTextureAccess> intents, const PreparedTextureAccessDesc& requested) noexcept
	{
		if ((texture.bDepth || GetFormatInfo(texture.Format).Kind == EFormatKind::Integer) && (requested.State == ETextureState::ResolveSource || requested.State == ETextureState::ResolveDestination))
		{
			return VK_IMAGE_LAYOUT_GENERAL;
		}
		if (texture.bDepth && (requested.State == ETextureState::DepthRead || requested.State == ETextureState::DepthWrite))
		{
			bool bDepthReadOnly = true, bStencilReadOnly = true;
			for (const rhi::NativePreparedTextureAccess& access : intents)
			{
				const PreparedTextureAccessDesc& row = access.Intent;
				if (access.Resource != &texture ||
					row.BaseMipLevel >= requested.BaseMipLevel + requested.MipLevelCount ||
					requested.BaseMipLevel >= row.BaseMipLevel + row.MipLevelCount ||
					row.BaseArrayLayer >= requested.BaseArrayLayer + requested.ArrayLayerCount ||
					requested.BaseArrayLayer >= row.BaseArrayLayer + row.ArrayLayerCount)
				{
					continue;
				}
				if (row.State != ETextureState::DepthRead && row.State != ETextureState::DepthWrite)
				{
					continue;
				}
				if (row.Aspect != ETextureAspect::Stencil)
				{
					bDepthReadOnly &= row.State == ETextureState::DepthRead;
				}
				if (row.Aspect != ETextureAspect::Depth)
				{
					bStencilReadOnly &= row.State == ETextureState::DepthRead;
				}
			}

			return DepthAttachmentLayout(texture.Format, bDepthReadOnly, bStencilReadOnly);
		}

		return TextureLayout(texture, MapTextureState(requested.State));
	}

	void RequireTextureLayout(State& state, Pending& pending, Texture& texture, VkImageLayout next, uint32 subresource) noexcept
	{
		next = TextureLayout(texture, next);
		if (pending.bInvalidTextureLayouts)
		{
			return;
		}

		const uint32 count = texture.MipLevels * texture.ArrayLayers;

		if (subresource != UINT32_MAX && subresource >= count)
		{
			pending.bInvalidTextureLayouts = true;

			return;
		}

		Pending::TextureLayoutState* const tracked = TrackTextureLayout(pending, texture);

		if (tracked == nullptr)
		{
			return;
		}

		if (!pending.bAutomaticBarriers && !pending.bExplicitStateRequest)
		{
			if (subresource != UINT32_MAX)
			{
				const VkImageLayout current = tracked->SubresourceLayouts.IsEmpty() ? tracked->WholeLayout : tracked->SubresourceLayouts[subresource];

				if (current != next)
				{
					pending.bMissingManualState = true;
					pending.bInvalidTextureLayouts = true;
				}

				return;
			}

			if (tracked->SubresourceLayouts.IsEmpty())
			{
				pending.bMissingManualState |= tracked->WholeLayout != next;
				pending.bInvalidTextureLayouts |= pending.bMissingManualState;

				return;
			}

			for (VkImageLayout current : tracked->SubresourceLayouts)
			{
				if (current != next)
				{
					pending.bMissingManualState = true;
					pending.bInvalidTextureLayouts = true;

					return;
				}
			}

			return;
		}
		pending.LastComputePipeline = nullptr;
		pending.LastTexturePipeline = nullptr;

		if (subresource != UINT32_MAX)
		{
			if (tracked->SubresourceLayouts.IsEmpty())
			{
				if (!tracked->SubresourceLayouts.ResizeUninitialized(count))
				{
					pending.bInvalidTextureLayouts = true;

					return;
				}

				for (VkImageLayout& layout : tracked->SubresourceLayouts)
				{
					layout = tracked->WholeLayout;
				}
			}

			const VkImageLayout before = tracked->SubresourceLayouts[subresource];
			EmitTextureBarrier(state, pending, texture, before, next, subresource % texture.MipLevels, 1, subresource / texture.MipLevels,
				1);
			tracked->SubresourceLayouts[subresource] = next;

			return;
		}

		if (tracked->SubresourceLayouts.IsEmpty())
		{
			EmitTextureBarrier(state, pending, texture, tracked->WholeLayout, next, 0, texture.MipLevels, 0, texture.ArrayLayers);
			tracked->WholeLayout = next;

			return;
		}

		const VkImageLayout first = tracked->SubresourceLayouts[0];
		bool bUniform = true;

		for (uint32 index = 1; index < count; ++index)
		{
			if (tracked->SubresourceLayouts[index] != first)
			{
				bUniform = false;
				break;
			}
		}

		if (bUniform)
		{
			EmitTextureBarrier(state, pending, texture, first, next, 0, texture.MipLevels, 0, texture.ArrayLayers);
			if (pending.bPrepared)
			{
				tracked->SubresourceLayouts.Clear();
			}
			else
			{
				tracked->SubresourceLayouts.Reset();
			}
			tracked->WholeLayout = next;

			return;
		}

		for (uint32 index = 0; index < count; ++index)
		{
			const VkImageLayout before = tracked->SubresourceLayouts[index];
			EmitTextureBarrier(state, pending, texture, before, next, index % texture.MipLevels, 1, index / texture.MipLevels, 1);
		}
		if (pending.bPrepared)
		{
			tracked->SubresourceLayouts.Clear();
		}
		else
		{
			tracked->SubresourceLayouts.Reset();
		}
		tracked->WholeLayout = next;
	}

	void RequireTextureLayoutRange(State& state, Pending& pending, Texture& texture, VkImageLayout next, const TextureViewDesc& range) noexcept
	{
		for (uint32 layer = range.BaseArrayLayer; layer < range.BaseArrayLayer + range.ArrayLayerCount; ++layer)
		{
			for (uint32 mip = range.BaseMipLevel; mip < range.BaseMipLevel + range.MipLevelCount; ++mip)
			{
				RequireTextureLayout(state, pending, texture, next, layer * texture.MipLevels + mip);

				if (pending.bInvalidTextureLayouts)
				{
					return;
				}
			}
		}
	}

	[[nodiscard]] bool DescriptorTableTexturesHaveLayout(const Pending& pending, const DescriptorTable& table, VkImageLayout layout) noexcept
	{
		for (usize index = 0; index < table.Textures.Size(); ++index)
		{
			const Texture* const texture = table.Textures[index];

			if (texture == nullptr)
			{
				continue;
			}

			const TextureView* const view = table.TextureViews[index];
			const TextureViewDesc* const range = view != nullptr ? &view->Descriptor : nullptr;

			if (!TextureHasLayout(pending, *texture, layout, range))
			{
				return false;
			}
		}

		return true;
	}

	void RequireDescriptorTableTextureLayouts(State& state, Pending& pending, const DescriptorTable& table, VkImageLayout layout) noexcept
	{
		for (usize index = 0; index < table.Textures.Size(); ++index)
		{
			Texture* const texture = table.Textures[index];

			if (texture == nullptr)
			{
				continue;
			}

			const TextureView* const view = table.TextureViews[index];
			const TextureViewDesc* const range = view != nullptr ? &view->Descriptor : nullptr;

			if (TextureHasLayout(pending, *texture, layout, range))
			{
				continue;
			}

			if (range != nullptr)
			{
				RequireTextureLayoutRange(state, pending, *texture, layout, *range);
			}
			else
			{
				RequireTextureLayout(state, pending, *texture, layout);
			}

			if (pending.bInvalidTextureLayouts)
			{
				return;
			}
		}
	}

	void CommitTextureLayouts(Pending& pending) noexcept
	{
		if (pending.bPrepared)
		{
			for (rhi::NativePreparedTextureAccess const& access : pending.PreparedTextures)
			{
				Texture& texture = *static_cast<Texture*>(access.Resource);
				const Pending::TextureLayoutState* captured = nullptr;
				for (vulkan::Pending::TextureLayoutState const& tracked : pending.TextureLayouts)
				{
					if (tracked.Item == &texture)
					{
						captured = &tracked;
						break;
					}
				}
				ASSERT(captured != nullptr, "Every prepared texture intent has a captured native tracker.");
				if (texture.SubresourceLayouts.IsEmpty())
				{
					const bool bResized = texture.SubresourceLayouts.Resize(texture.MipLevels * texture.ArrayLayers);
					ASSERT(bResized, "Global texture layout storage is reserved cold.");
					for (VkImageLayout& layout : texture.SubresourceLayouts)
					{
						layout = texture.Layout;
					}
				}
				for (uint32 layer = 0; layer < access.Intent.ArrayLayerCount; ++layer)
				{
					for (uint32 mip = 0; mip < access.Intent.MipLevelCount; ++mip)
					{
						const uint32 index = access.Intent.BaseMipLevel + mip + (access.Intent.BaseArrayLayer + layer) * texture.MipLevels;
						texture.SubresourceLayouts[index] = captured->SubresourceLayouts.IsEmpty() ? captured->WholeLayout : captured->SubresourceLayouts[index];
					}
				}
				texture.Layout = texture.SubresourceLayouts[0];
			}

			return;
		}
		for (vulkan::Pending::TextureLayoutState& tracked : pending.TextureLayouts)
		{
			Texture& texture = *tracked.Item;
			texture.Layout = tracked.SubresourceLayouts.IsEmpty() ? tracked.WholeLayout : tracked.SubresourceLayouts[0];
			texture.SubresourceLayouts = Move(tracked.SubresourceLayouts);
		}
	}

	[[nodiscard]] Result<void> RecordShaderMemoryBarrier(State* opaque, void* native)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr,
			0, nullptr);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordShaderBufferBarrier(State* opaque, void* native, void* bufferNative)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& buffer = *static_cast<Buffer*>(bufferNative);
		VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = buffer.Handle;
		barrier.offset = 0;
		barrier.size = VK_WHOLE_SIZE;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &barrier,
			0, nullptr);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordShaderTextureBarrier(State* opaque, void* native, void* textureNative)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(textureNative);

		ASSERT(TextureHasLayout(pending, texture, VK_IMAGE_LAYOUT_GENERAL), "Prepare the storage texture for shader access first.");
		VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = texture.Handle;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = texture.MipLevels;
		barrier.subresourceRange.layerCount = texture.ArrayLayers;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
			&barrier);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordPrepareTextureForShaderAccess(State* opaque, void* native, void* textureNative, const TextureViewDesc* range, ETextureState)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(textureNative);
		pending.bExplicitStateRequest = true;

		if (!TextureHasLayout(pending, texture, VK_IMAGE_LAYOUT_GENERAL, range))
		{
			if (range != nullptr)
			{
				RequireTextureLayoutRange(state, pending, texture, VK_IMAGE_LAYOUT_GENERAL, *range);
			}
			else
			{
				RequireTextureLayout(state, pending, texture, VK_IMAGE_LAYOUT_GENERAL);
			}
		}
		pending.bExplicitStateRequest = false;

		return Result<void>::Success();
	}

	[[nodiscard]] VkImageLayout ExplicitTextureLayout(const Pending& pending, const Texture& texture, const TextureSubresource* subresource, ETextureState requested) noexcept
	{
		if (GetFormatInfo(texture.Format).bHasStencil &&
			subresource != nullptr &&
			(subresource->Aspect == ETextureAspect::Depth || subresource->Aspect == ETextureAspect::Stencil) &&
			(requested == ETextureState::DepthRead || requested == ETextureState::DepthWrite))
		{
			const uint32 index = subresource->MipLevel + subresource->ArrayLayer * texture.MipLevels;
			VkImageLayout current = texture.SubresourceLayouts.IsEmpty() ? texture.Layout : texture.SubresourceLayouts[index];
			for (vulkan::Pending::TextureLayoutState const& tracked : pending.TextureLayouts)
			{
				if (tracked.Item == &texture)
				{
					current = tracked.SubresourceLayouts.IsEmpty() ? tracked.WholeLayout : tracked.SubresourceLayouts[index];
					break;
				}
			}
			bool bDepthReadOnly = current == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL || current == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
			bool bStencilReadOnly = current == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL || current == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
			if (subresource->Aspect == ETextureAspect::Depth)
			{
				bDepthReadOnly = requested == ETextureState::DepthRead;
			}
			else
			{
				bStencilReadOnly = requested == ETextureState::DepthRead;
			}

			return DepthAttachmentLayout(texture.Format, bDepthReadOnly, bStencilReadOnly);
		}

		return TextureLayout(texture, (texture.bDepth || GetFormatInfo(texture.Format).Kind == EFormatKind::Integer) && (requested == ETextureState::ResolveSource || requested == ETextureState::ResolveDestination) ? VK_IMAGE_LAYOUT_GENERAL : MapTextureState(requested));
	}

	[[nodiscard]] Result<void> RecordBeginTrackingTextureState(void*, void* native, void* textureNative, const TextureSubresource* subresource, ETextureState requested)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(textureNative);
		Pending::TextureLayoutState* const tracked = TrackTextureLayout(pending, texture);

		if (tracked == nullptr)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.VulkanTextureState", "Could not seed the texture layout.");
		}

		const VkImageLayout layout = ExplicitTextureLayout(pending, texture, subresource, requested);

		if (subresource == nullptr)
		{
			tracked->WholeLayout = layout;
			tracked->SubresourceLayouts.Clear();
		}
		else
		{
			if (tracked->SubresourceLayouts.IsEmpty())
			{
				if (!tracked->SubresourceLayouts.ResizeUninitialized(texture.MipLevels * texture.ArrayLayers))
				{
					pending.bInvalidTextureLayouts = true;
				}
				else
				{
					for (VkImageLayout& current : tracked->SubresourceLayouts)
					{
						current = tracked->WholeLayout;
					}
				}
			}

			if (!pending.bInvalidTextureLayouts)
			{
				tracked->SubresourceLayouts[subresource->MipLevel + subresource->ArrayLayer * texture.MipLevels] = layout;
			}
		}

		if (pending.bInvalidTextureLayouts)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.VulkanTextureState", "Could not seed the texture layout.");
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordSetTextureState(State* opaque, void* native, void* textureNative, const TextureSubresource* subresource, ETextureState requested)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(textureNative);
		const VkImageLayout next = ExplicitTextureLayout(pending, texture, subresource, requested);
		pending.bExplicitStateRequest = true;

		if (subresource == nullptr)
		{
			RequireTextureLayout(state, pending, texture, next);
		}
		else
		{
			RequireTextureLayout(state, pending, texture, next, subresource->MipLevel + subresource->ArrayLayer * texture.MipLevels);
		}
		pending.bExplicitStateRequest = false;

		if (pending.bInvalidTextureLayouts)
		{
			return Result<void>::Failure(NativeError(EErrorCode::ExternalFailure, "Rhi.VulkanTextureState", "Could not track the requested texture layout."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] ETrackedResourceState GetTextureSubresourceState(const void*, const void* native, const void* textureNative, TextureSubresource subresource) noexcept
	{
		const Pending& pending = *static_cast<const Pending*>(native);
		const Texture& texture = *static_cast<const Texture*>(textureNative);
		const uint32 index = subresource.MipLevel + subresource.ArrayLayer * texture.MipLevels;
		VkImageLayout layout = texture.SubresourceLayouts.IsEmpty() ? texture.Layout : texture.SubresourceLayouts[index];

		for (const Pending::TextureLayoutState& tracked : pending.TextureLayouts)
		{
			if (tracked.Item != &texture)
			{
				continue;
			}
			layout = tracked.SubresourceLayouts.IsEmpty() ? tracked.WholeLayout : tracked.SubresourceLayouts[index];
			break;
		}

		switch (layout)
		{
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			return ETrackedResourceState::CopySource;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			return ETrackedResourceState::CopyDestination;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			return ETrackedResourceState::RenderTarget;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			return ETrackedResourceState::DepthWrite;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			return ETrackedResourceState::DepthRead |
				(texture.bShaderRead ? ETrackedResourceState::ShaderRead : ETrackedResourceState::Unknown);
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			return ETrackedResourceState::ShaderRead;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			return ETrackedResourceState::Present;
		case VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR:
			return ETrackedResourceState::ShadingRateSource;
		case VK_IMAGE_LAYOUT_GENERAL:
			return (texture.bDepth ? ETrackedResourceState::DepthRead : ETrackedResourceState::Unknown) |
				(texture.bShaderRead ? ETrackedResourceState::ShaderRead : ETrackedResourceState::Unknown) |
				(texture.bStorage ? ETrackedResourceState::Storage : ETrackedResourceState::Unknown);
		default:
			return ETrackedResourceState::Unknown;
		}
	}

	[[nodiscard]] Result<void> RecordBeginTrackingBufferState(void*, void*, void*, EBufferState)
	{
		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordSetBufferState(State* opaque, void* native, void* bufferNative, EBufferState requested)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& buffer = *static_cast<Buffer*>(bufferNative);
		VkAccessFlags access = 0;

		switch (requested)
		{
		case EBufferState::CopySource:
			access = VK_ACCESS_TRANSFER_READ_BIT;
			break;
		case EBufferState::CopyDestination:
			access = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case EBufferState::Uniform:
			access = VK_ACCESS_UNIFORM_READ_BIT;
			break;
		case EBufferState::Storage:
			access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			break;
		case EBufferState::ReadOnlyStorage:
			access = VK_ACCESS_SHADER_READ_BIT;
			break;
		case EBufferState::Vertex:
			access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
			break;
		case EBufferState::Index:
			access = VK_ACCESS_INDEX_READ_BIT;
			break;
		case EBufferState::VertexAndReadOnlyStorage:
			access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
			break;
		case EBufferState::VertexAndIndex:
			access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
			break;
		case EBufferState::Indirect:
			access = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
			break;
		case EBufferState::AccelBuildInput:
			access = VK_ACCESS_SHADER_READ_BIT;
			break;
		case EBufferState::AccelScratch:
		case EBufferState::AccelStorage:
			access = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			break;

		default:
		{
			break;
		}
		}
		VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.dstAccessMask = access;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = buffer.Handle;
		barrier.offset = 0;
		barrier.size = VK_WHOLE_SIZE;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &barrier,
			0, nullptr);

		return Result<void>::Success();
	}
}
