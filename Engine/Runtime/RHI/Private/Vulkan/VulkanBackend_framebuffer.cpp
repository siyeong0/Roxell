#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<VkRenderPass> CreateAttachmentRenderPass(
		State& state,
		Span<const EFormat> colors,
		EFormat depthFormat,
		uint32 sampleCount,
		bool bLoadDepth,
		bool bShadingRate,
		bool bReadOnlyDepth,
		bool bReadOnlyStencil)
	{
		VkAttachmentDescription attachments[10]{};
		VkAttachmentReference references[8]{};
		for (usize color = 0; color < colors.Size(); ++color)
		{
			attachments[color].format = ConvertVulkanFormat(colors[color]);
			attachments[color].samples = static_cast<VkSampleCountFlagBits>(sampleCount);
			attachments[color].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachments[color].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[color].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachments[color].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			references[color] = { static_cast<uint32>(color), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		}
		VkAttachmentReference depthReference{};
		if (depthFormat != EFormat::UNKNOWN)
		{
			VkAttachmentDescription& depth = attachments[colors.Size()];
			depth.format = ConvertVulkanFormat(depthFormat);
			depth.samples = static_cast<VkSampleCountFlagBits>(sampleCount);
			depth.loadOp = bLoadDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
			depth.storeOp = bReadOnlyDepth && state.bLoadStoreOpNone ? VK_ATTACHMENT_STORE_OP_NONE : VK_ATTACHMENT_STORE_OP_STORE;
			depth.stencilLoadOp = GetFormatInfo(depthFormat).bHasStencil ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depth.stencilStoreOp =
				GetFormatInfo(depthFormat).bHasStencil
				? (bReadOnlyStencil && state.bLoadStoreOpNone ? VK_ATTACHMENT_STORE_OP_NONE : VK_ATTACHMENT_STORE_OP_STORE)
				: VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depth.initialLayout = DepthAttachmentLayout(depthFormat, bReadOnlyDepth, bReadOnlyStencil);
			depth.finalLayout = depth.initialLayout;
			depthReference = { static_cast<uint32>(colors.Size()), depth.initialLayout };
		}

		if (bShadingRate)
		{
			VkAttachmentDescription& rate = attachments[colors.Size() + (depthFormat != EFormat::UNKNOWN ? 1u : 0u)];
			rate.format = VK_FORMAT_R8_UINT;
			rate.samples = VK_SAMPLE_COUNT_1_BIT;
			rate.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			rate.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			rate.initialLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
			rate.finalLayout = rate.initialLayout;
		}
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = static_cast<uint32>(colors.Size());
		subpass.pColorAttachments = colors.IsEmpty() ? nullptr : references;
		subpass.pDepthStencilAttachment = depthFormat != EFormat::UNKNOWN ? &depthReference : nullptr;
		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		if (depthFormat != EFormat::UNKNOWN)
		{
			dependency.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}

		if (bShadingRate)
		{
			dependency.dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
			dependency.dstAccessMask |= VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
		}
		VkSubpassDependency dependencies[1]{ dependency };
		VkRenderPassCreateInfo info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		info.attachmentCount = static_cast<uint32>(colors.Size()) + (depthFormat != EFormat::UNKNOWN ? 1u : 0u) + (bShadingRate ? 1u : 0u);
		info.pAttachments = attachments;
		info.subpassCount = 1;
		info.pSubpasses = &subpass;
		info.dependencyCount = 1;
		info.pDependencies = dependencies;
		VkRenderPass result = VK_NULL_HANDLE;

		if (bShadingRate)
		{
			VkAttachmentDescription2 descriptions[10]{};
			for (uint32 index = 0; index < info.attachmentCount; ++index)
			{
				const VkAttachmentDescription& source = attachments[index];
				VkAttachmentDescription2& destination = descriptions[index];
				destination.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
				destination.flags = source.flags;
				destination.format = source.format;
				destination.samples = source.samples;
				destination.loadOp = source.loadOp;
				destination.storeOp = source.storeOp;
				destination.stencilLoadOp = source.stencilLoadOp;
				destination.stencilStoreOp = source.stencilStoreOp;
				destination.initialLayout = source.initialLayout;
				destination.finalLayout = source.finalLayout;
			}
			VkAttachmentReference2 colorReferences[8]{};
			for (uint32 color = 0; color < colors.Size(); ++color)
			{
				colorReferences[color] = { VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2 };
				colorReferences[color].attachment = color;
				colorReferences[color].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				colorReferences[color].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			}
			VkAttachmentReference2 depthReference2{ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2 };
			depthReference2.attachment = static_cast<uint32>(colors.Size());
			depthReference2.layout = depthReference.layout;
			depthReference2.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | (GetFormatInfo(depthFormat).bHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
			VkAttachmentReference2 rateReference{ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2 };
			rateReference.attachment = info.attachmentCount - 1;
			rateReference.layout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
			rateReference.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			VkFragmentShadingRateAttachmentInfoKHR rateInfo{ VK_STRUCTURE_TYPE_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR };
			rateInfo.pFragmentShadingRateAttachment = &rateReference;
			rateInfo.shadingRateAttachmentTexelSize = state.ShadingRateTile;
			VkSubpassDescription2 subpass2{ VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2 };
			subpass2.pNext = &rateInfo;
			subpass2.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass2.colorAttachmentCount = static_cast<uint32>(colors.Size());
			subpass2.pColorAttachments = colors.IsEmpty() ? nullptr : colorReferences;
			subpass2.pDepthStencilAttachment = depthFormat != EFormat::UNKNOWN ? &depthReference2 : nullptr;
			VkSubpassDependency2 dependencies2[1]{};
			for (uint32 index = 0; index < 1; ++index)
			{
				dependencies2[index] = { VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2 };
				dependencies2[index].srcSubpass = dependencies[index].srcSubpass;
				dependencies2[index].dstSubpass = dependencies[index].dstSubpass;
				dependencies2[index].srcStageMask = dependencies[index].srcStageMask;
				dependencies2[index].dstStageMask = dependencies[index].dstStageMask;
				dependencies2[index].srcAccessMask = dependencies[index].srcAccessMask;
				dependencies2[index].dstAccessMask = dependencies[index].dstAccessMask;
			}
			VkRenderPassCreateInfo2 create{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2 };
			create.attachmentCount = info.attachmentCount;
			create.pAttachments = descriptions;
			create.subpassCount = 1;
			create.pSubpasses = &subpass2;
			create.dependencyCount = 1;
			create.pDependencies = dependencies2;

			if (state.F.CreateRenderPass2(state.Device, &create, nullptr, &result) != VK_SUCCESS)
			{
				return Result<VkRenderPass>::Failure(Failure("Rhi.VulkanRenderPass", "Could not create the shading rate render pass."));
			}

			return Result<VkRenderPass>::Success(result);
		}

		if (state.F.CreateRenderPass(state.Device, &info, nullptr, &result) != VK_SUCCESS)
		{
			return Result<VkRenderPass>::Failure(Failure("Rhi.VulkanRenderPass", "Could not create attachment render pass."));
		}

		return Result<VkRenderPass>::Success(result);
	}

	[[nodiscard]] Result<VkRenderPass> GetDirectRenderPass(
		State& state,
		EFormat colorFormat,
		EFormat depthFormat,
		uint32 sampleCount,
		bool bLoadDepth,
		bool bReadOnlyDepth,
		bool bReadOnlyStencil)
	{
		for (const DirectRenderPass& pass : state.DirectRenderPasses)
		{
			if (pass.ColorFormat == colorFormat &&
				pass.DepthFormat == depthFormat &&
				pass.SampleCount == sampleCount &&
				pass.bLoadDepth == bLoadDepth &&
				pass.bReadOnlyDepth == bReadOnlyDepth &&
				pass.bReadOnlyStencil == bReadOnlyStencil)
			{
				return Result<VkRenderPass>::Success(pass.Handle);
			}
		}

		Result<VkRenderPass> created = CreateAttachmentRenderPass(state, { &colorFormat, 1 }, depthFormat, sampleCount, bLoadDepth,
			false, bReadOnlyDepth, bReadOnlyStencil);

		if (!created)
		{
			return created;
		}

		if (!state.DirectRenderPasses.Add(
			{ colorFormat, depthFormat, sampleCount, bLoadDepth, bReadOnlyDepth, bReadOnlyStencil, created.Value() }))
		{
			state.F.DestroyRenderPass(state.Device, created.Value(), nullptr);

			return Result<VkRenderPass>::Failure(Failure("Rhi.Allocation", "Could not retain the direct render pass."));
		}

		return created;
	}

	void FlushGraphicsStorageWrites(State& state, Pending& pending) noexcept
	{
		if (!pending.bGraphicsStorageWritten)
		{
			return;
		}
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT |
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, static_cast<uint32>(pending.GraphicsStorageBuffers.Size()),
			pending.GraphicsStorageBuffers.Data(), static_cast<uint32>(pending.GraphicsStorageImages.Size()),
			pending.GraphicsStorageImages.Data());
		pending.GraphicsStorageBuffers.Clear();
		pending.GraphicsStorageImages.Clear();
		pending.bGraphicsStorageWritten = false;
	}

	void SynchronizeGraphicsStorage(State& state, Pending& pending) noexcept
	{
		if (!pending.bGraphicsStorageWritten || !pending.bAutomaticBarriers)
		{
			return;
		}
		// Fragment writes may feed a later draw's vertex or indirect input. Such a
		// framebuffer-global dependency cannot be expressed inside one subpass.
		state.F.CmdEndRenderPass(pending.Commands);
		FlushGraphicsStorageWrites(state, pending);
		state.F.CmdBeginRenderPass(pending.Commands, &pending.ResumeRendering, VK_SUBPASS_CONTENTS_INLINE);
	}

	[[nodiscard]] Result<void*> CreateFramebuffer(State* opaque, const NativeFramebufferDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}
		if (GetFormatInfo(desc.DepthFormat).bHasStencil && desc.DepthSubresource.MipLevel != 0 && !SupportsNonzeroMipStencilViews(state))
		{
			return Result<void*>::Failure(
				EErrorCode::Unsupported,
				"Rhi.VulkanStencilMipViews",
				"NVIDIA RTX 5070 driver 617.14 cannot address nonzero-mip depth/stencil attachments correctly.");
		}
		Framebuffer* framebuffer = CreateObject<Framebuffer>(state.Resource);

		if (framebuffer == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a framebuffer."));
		}
		framebuffer->ColorCount = desc.ColorCount;
		framebuffer->Width = desc.Width;
		framebuffer->Height = desc.Height;
		framebuffer->Depth = static_cast<Texture*>(desc.Depth);
		framebuffer->ShadingRateImage = static_cast<Texture*>(desc.ShadingRateImage);
		VkImageView views[10]{};
		for (uint32 color = 0; color < desc.ColorCount; ++color)
		{
			framebuffer->Colors[color] = static_cast<Texture*>(desc.Colors[color]);
			framebuffer->ColorSubresources[color] =
				desc.ColorSubresources[color].MipLevel +
				(framebuffer->Colors[color]->Dimension == ETextureDimension::Texture3D
					? 0
					: desc.ColorSubresources[color].ArrayLayer * framebuffer->Colors[color]->MipLevels);
			VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			view.image = framebuffer->Colors[color]->Handle;
			view.viewType = framebuffer->Colors[color]->Dimension == ETextureDimension::Texture1D ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_2D;
			view.format = ConvertVulkanFormat(desc.ColorFormats[color]);
			view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			view.subresourceRange.baseMipLevel = desc.ColorSubresources[color].MipLevel;
			view.subresourceRange.levelCount = 1;
			view.subresourceRange.baseArrayLayer = desc.ColorSubresources[color].ArrayLayer;
			view.subresourceRange.layerCount = 1;

			if (state.F.CreateImageView(state.Device, &view, nullptr, &framebuffer->AttachmentViews[color]) != VK_SUCCESS)
			{
				FreeFramebuffer(state, framebuffer);

				return Result<void*>::Failure(Failure("Rhi.VulkanFramebuffer", "Could not create a color attachment view."));
			}
			views[color] = framebuffer->AttachmentViews[color];
		}

		if (framebuffer->Depth != nullptr)
		{
			framebuffer->DepthSubresource = desc.DepthSubresource.MipLevel + desc.DepthSubresource.ArrayLayer * framebuffer->Depth->MipLevels;
			VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			view.image = framebuffer->Depth->Handle;
			view.viewType = framebuffer->Depth->Dimension == ETextureDimension::Texture1D ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_2D;
			view.format = ConvertVulkanFormat(desc.DepthFormat);
			view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | (GetFormatInfo(desc.DepthFormat).bHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
			view.subresourceRange.baseMipLevel = desc.DepthSubresource.MipLevel;
			view.subresourceRange.levelCount = 1;
			view.subresourceRange.baseArrayLayer = desc.DepthSubresource.ArrayLayer;
			view.subresourceRange.layerCount = 1;

			if (state.F.CreateImageView(state.Device, &view, nullptr, &framebuffer->AttachmentViews[desc.ColorCount]) != VK_SUCCESS)
			{
				FreeFramebuffer(state, framebuffer);

				return Result<void*>::Failure(Failure("Rhi.VulkanFramebuffer", "Could not create a depth attachment view."));
			}
			views[desc.ColorCount] = framebuffer->AttachmentViews[desc.ColorCount];
		}

		if (framebuffer->ShadingRateImage != nullptr)
		{
			const uint32 index = desc.ColorCount + (framebuffer->Depth != nullptr ? 1u : 0u);
			VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			view.image = framebuffer->ShadingRateImage->Handle;
			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.format = VK_FORMAT_R8_UINT;
			view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			view.subresourceRange.levelCount = 1;
			view.subresourceRange.layerCount = 1;

			if (state.F.CreateImageView(state.Device, &view, nullptr, &framebuffer->AttachmentViews[index]) != VK_SUCCESS)
			{
				FreeFramebuffer(state, framebuffer);

				return Result<void*>::Failure(Failure("Rhi.VulkanFramebuffer", "Could not create a shading rate image view."));
			}
			views[index] = framebuffer->AttachmentViews[index];
		}

		if (framebuffer->ShadingRateImage == nullptr &&
			desc.ColorCount == 1 &&
			desc.SampleCount == 1 &&
			desc.ColorFormats[0] == EFormat::RGBA8_UNORM &&
			(desc.DepthFormat == EFormat::UNKNOWN || desc.DepthFormat == EFormat::D32))
		{
			framebuffer->RenderPass = framebuffer->Depth != nullptr ? state.DepthRenderPass : state.RenderPass;

			if (framebuffer->Depth != nullptr)
			{
				framebuffer->DepthLoadRenderPass = state.DepthLoadRenderPass;
			}
		}
		else
		{
			Result<VkRenderPass> created =
				CreateAttachmentRenderPass(state, { desc.ColorFormats, desc.ColorCount }, desc.DepthFormat, desc.SampleCount, false,
					framebuffer->ShadingRateImage != nullptr);

			if (!created)
			{
				FreeFramebuffer(state, framebuffer);

				return Result<void*>::Failure(created.GetError());
			}
			framebuffer->RenderPass = created.Value();
			framebuffer->bOwnRenderPass = true;

			if (framebuffer->Depth != nullptr)
			{
				Result<VkRenderPass> loadPass =
					CreateAttachmentRenderPass(state, { desc.ColorFormats, desc.ColorCount }, desc.DepthFormat, desc.SampleCount, true,
						framebuffer->ShadingRateImage != nullptr);

				if (!loadPass)
				{
					FreeFramebuffer(state, framebuffer);

					return Result<void*>::Failure(loadPass.GetError());
				}
				framebuffer->DepthLoadRenderPass = loadPass.Value();
			}
		}

		if (framebuffer->Depth != nullptr)
		{
			Result<VkRenderPass> readOnlyPass =
				CreateAttachmentRenderPass(state, { desc.ColorFormats, desc.ColorCount }, desc.DepthFormat, desc.SampleCount, true,
					framebuffer->ShadingRateImage != nullptr, true, true);

			if (!readOnlyPass)
			{
				FreeFramebuffer(state, framebuffer);

				return Result<void*>::Failure(readOnlyPass.GetError());
			}
			framebuffer->DepthReadOnlyRenderPass = readOnlyPass.Value();
			if (GetFormatInfo(desc.DepthFormat).bHasStencil)
			{
				for (uint32 mask = 0; mask < 2; ++mask)
				{
					Result<VkRenderPass> mixed =
						CreateAttachmentRenderPass(state, { desc.ColorFormats, desc.ColorCount }, desc.DepthFormat, desc.SampleCount, true,
							framebuffer->ShadingRateImage != nullptr, mask == 0, mask == 1);
					if (!mixed)
					{
						FreeFramebuffer(state, framebuffer);

						return Result<void*>::Failure(mixed.GetError());
					}
					framebuffer->MixedDepthStencilRenderPasses[mask] = mixed.Value();
				}
			}
		}
		VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		info.renderPass = framebuffer->RenderPass;
		info.attachmentCount = desc.ColorCount + (framebuffer->Depth != nullptr ? 1u : 0u) + (framebuffer->ShadingRateImage != nullptr ? 1u : 0u);
		info.pAttachments = views;
		info.width = desc.Width;
		info.height = desc.Height;
		info.layers = 1;

		if (state.F.CreateFramebuffer(state.Device, &info, nullptr, &framebuffer->Handle) != VK_SUCCESS)
		{
			FreeFramebuffer(state, framebuffer);

			return Result<void*>::Failure(Failure("Rhi.VulkanFramebuffer", "Could not create a framebuffer."));
		}

		return Result<void*>::Success(framebuffer);
	}

	void DestroyFramebufferNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		FreeFramebuffer(state, static_cast<Framebuffer*>(native));
	}

	[[nodiscard]] Result<void> DestroyFramebuffer(State* opaque, void* native)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Framebuffer& framebuffer = *static_cast<Framebuffer*>(native);
		uint64 complete = 0;

		if (state.F.GetSemaphoreCounterValue(state.Device, state.Timeline, &complete) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanTimeline", "Could not inspect completion."));
		}

		if (framebuffer.LastUse > complete)
		{
			if (!state.RetiredFramebuffers.Add({ &framebuffer, framebuffer.LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retire a framebuffer."));
			}
		}
		else
		{
			FreeFramebuffer(state, &framebuffer);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBeginFramebuffer(State* opaque, void* native, void* target, DepthAttachmentOptions depthOptions)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Framebuffer& framebuffer = *static_cast<Framebuffer*>(target);

		for (uint32 color = 0; color < framebuffer.ColorCount; ++color)
		{
			Texture& texture = *framebuffer.Colors[color];
			if (!pending.bPrepared)
			{
				Result<void> acquired = PrepareSwapChainTarget(pending, texture);
				if (!acquired)
				{
					return acquired;
				}
			}
			RequireTextureLayout(state, pending, texture, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, framebuffer.ColorSubresources[color]);
		}

		if (framebuffer.Depth != nullptr)
		{
			Texture& texture = *framebuffer.Depth;
			RequireTextureLayout(state, pending, texture,
				DepthAttachmentLayout(texture.Format, depthOptions.bReadOnly, depthOptions.bReadOnlyStencil),
				framebuffer.DepthSubresource);
		}

		if (framebuffer.ShadingRateImage != nullptr)
		{
			RequireTextureLayout(state, pending, *framebuffer.ShadingRateImage,
				VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR);
		}
		VkRenderPassBeginInfo begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		begin.renderPass =
			framebuffer.Depth != nullptr
			? (GetFormatInfo(framebuffer.Depth->Format).bHasStencil && depthOptions.bReadOnly != depthOptions.bReadOnlyStencil
				? framebuffer.MixedDepthStencilRenderPasses[depthOptions.bReadOnly ? 0 : 1]
				: (depthOptions.bReadOnly ? framebuffer.DepthReadOnlyRenderPass : framebuffer.DepthLoadRenderPass))
			: framebuffer.RenderPass;
		begin.framebuffer = framebuffer.Handle;
		begin.renderArea.extent = { framebuffer.Width, framebuffer.Height };
		pending.ResumeRendering = begin;
		pending.bRangeStorageBarrierPending = true;
		pending.LastTexturePipeline = nullptr;
		state.F.CmdBeginRenderPass(pending.Commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
		if (framebuffer.Depth != nullptr)
		{
			VkClearAttachment clear{};
			clear.aspectMask = (depthOptions.LoadOp == EDepthLoadOp::Clear ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
				(GetFormatInfo(framebuffer.Depth->Format).bHasStencil && depthOptions.StencilLoadOp == EDepthLoadOp::Clear
					? VK_IMAGE_ASPECT_STENCIL_BIT
					: 0);
			clear.clearValue.depthStencil = { depthOptions.ClearValue, depthOptions.ClearStencil };
			if (clear.aspectMask != 0)
			{
				VkClearRect rect{ {{0, 0}, {framebuffer.Width, framebuffer.Height}}, 0, 1 };
				state.F.CmdClearAttachments(pending.Commands, 1, &clear, 1, &rect);
			}
		}
		pending.bShadingRateImageActive = framebuffer.ShadingRateImage != nullptr;
		VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(framebuffer.Width), static_cast<float>(framebuffer.Height), 0.0f, 1.0f };
		VkRect2D scissor{ {}, {framebuffer.Width, framebuffer.Height} };
		state.F.CmdSetViewport(pending.Commands, 0, 1, &viewport);
		state.F.CmdSetScissor(pending.Commands, 0, 1, &scissor);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordEndFramebuffer(State* opaque, void* native, void* target)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Framebuffer& framebuffer = *static_cast<Framebuffer*>(target);
		if (pending.bPrepared)
		{
			state.F.CmdEndRenderPass(pending.Commands);
			pending.LastPreparedDraw = UINT32_MAX;

			return Result<void>::Success();
		}
		state.F.CmdEndRenderPass(pending.Commands);
		pending.bShadingRateImageActive = false;
		FlushGraphicsStorageWrites(state, pending);

		if (!pending.bAutomaticBarriers)
		{
			return Result<void>::Success();
		}

		for (uint32 color = 0; color < framebuffer.ColorCount; ++color)
		{
			Texture& texture = *framebuffer.Colors[color];

			if (texture.SwapChainOwner != nullptr)
			{
				RequireTextureLayout(state, pending, texture, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
				continue;
			}

			if (!texture.bShaderRead && !texture.bStorage)
			{
				continue;
			}
			RequireTextureLayout(state, pending, texture, VK_IMAGE_LAYOUT_GENERAL);
		}

		if (framebuffer.Depth != nullptr && framebuffer.Depth->bShaderRead)
		{
			Texture& depth = *framebuffer.Depth;
			RequireTextureLayout(state, pending, depth, VK_IMAGE_LAYOUT_GENERAL);
		}

		return Result<void>::Success();
	}
}
