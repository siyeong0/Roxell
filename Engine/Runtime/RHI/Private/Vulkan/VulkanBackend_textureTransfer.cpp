#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<uint64> UploadTexture(State* opaque, void* native, const TextureDesc& desc, TextureSubresource subresource, Span<const Byte> data)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<uint64>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Result<Buffer*> staged = NewBuffer(state, data.Size(), true);

		if (!staged)
		{
			return Result<uint64>::Failure(staged.GetError());
		}

		Buffer* staging = staged.Value();
		void* mapped = nullptr;

		if (state.F.MapMemory(state.Device, staging->Memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
		{
			FreeBuffer(state, staging);

			return Result<uint64>::Failure(Failure("Rhi.VulkanMap", "Could not map texture upload memory."));
		}

		CopyBytes(mapped, data.Data(), data.Size());
		VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.memory = staging->Memory;
		range.size = VK_WHOLE_SIZE;
		const bool bFlushed = staging->Coherent || state.F.FlushMappedMemoryRanges(state.Device, 1, &range) == VK_SUCCESS;
		state.F.UnmapMemory(state.Device, staging->Memory);

		if (!bFlushed)
		{
			FreeBuffer(state, staging);

			return Result<uint64>::Failure(Failure("Rhi.VulkanFlush", "Could not flush texture upload memory."));
		}

		const EQueue queue = GetFormatInfo(desc.Format).bHasDepth ? EQueue::Graphics : EQueue::Copy;
		Result<Pending> started = Begin(state, queue);

		if (!started)
		{
			FreeBuffer(state, staging);

			return Result<uint64>::Failure(started.GetError());
		}

		Pending pending = Move(started).TakeValue();
		pending.QueueKind = queue;

		if (!pending.Temporary.Add(staging))
		{
			state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);
			FreeBuffer(state, staging);

			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not track texture upload memory."));
		}

		Texture& destination = *static_cast<Texture*>(native);
		const uint32 subresourceIndex = subresource.MipLevel + subresource.ArrayLayer * destination.MipLevels;
		RequireTextureLayout(state, pending, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceIndex);
		VkBufferImageCopy copy{};
		copy.imageSubresource.aspectMask = subresource.Aspect == ETextureAspect::Stencil ? VK_IMAGE_ASPECT_STENCIL_BIT
			: GetFormatInfo(desc.Format).bHasDepth ? VK_IMAGE_ASPECT_DEPTH_BIT
			: VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.mipLevel = subresource.MipLevel;
		copy.imageSubresource.baseArrayLayer = subresource.ArrayLayer;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent = { (desc.Width >> subresource.MipLevel) ? (desc.Width >> subresource.MipLevel) : 1,
							(desc.Height >> subresource.MipLevel) ? (desc.Height >> subresource.MipLevel) : 1,
							(desc.Depth >> subresource.MipLevel) ? (desc.Depth >> subresource.MipLevel) : 1 };
		state.F.CmdCopyBufferToImage(pending.Commands, staging->Handle, destination.Handle,
			TextureLayout(destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), 1, &copy);

		if ((destination.bShaderRead || destination.bStorage))
		{
			RequireTextureLayout(state, pending, destination, VK_IMAGE_LAYOUT_GENERAL);
		}

		Result<uint64> submitted = Execute(state, Move(pending));

		if (submitted)
		{
			destination.LastUse = submitted.Value();
			destination.LastQueue = queue;
		}

		return submitted;
	}

	[[nodiscard]] Result<void> ReadTexture(State* opaque, void* native, const TextureDesc& desc, TextureSubresource subresource, Span<Byte> output, uint32 timeout)
	{
		State& state = *opaque;
		state.bSubmissionMayBePending = false;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Result<Buffer*> staged = NewBuffer(state, output.Size(), true);

		if (!staged)
		{
			return Result<void>::Failure(staged.GetError());
		}

		Buffer* staging = staged.Value();
		const EQueue queue = GetFormatInfo(desc.Format).bHasDepth ? EQueue::Graphics : EQueue::Copy;
		Result<Pending> started = Begin(state, queue);

		if (!started)
		{
			FreeBuffer(state, staging);

			return Result<void>::Failure(started.GetError());
		}

		Pending pending = Move(started).TakeValue();
		pending.QueueKind = queue;

		if (!pending.Temporary.Add(staging))
		{
			state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);
			FreeBuffer(state, staging);

			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track texture readback memory."));
		}

		Texture& source = *static_cast<Texture*>(native);
		const uint32 subresourceIndex = subresource.MipLevel + subresource.ArrayLayer * source.MipLevels;
		RequireTextureLayout(state, pending, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresourceIndex);
		VkBufferImageCopy copy{};
		copy.imageSubresource.aspectMask = subresource.Aspect == ETextureAspect::Stencil ? VK_IMAGE_ASPECT_STENCIL_BIT
			: GetFormatInfo(desc.Format).bHasDepth ? VK_IMAGE_ASPECT_DEPTH_BIT
			: VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.mipLevel = subresource.MipLevel;
		copy.imageSubresource.baseArrayLayer = subresource.ArrayLayer;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent = { (desc.Width >> subresource.MipLevel) ? (desc.Width >> subresource.MipLevel) : 1,
							(desc.Height >> subresource.MipLevel) ? (desc.Height >> subresource.MipLevel) : 1,
							(desc.Depth >> subresource.MipLevel) ? (desc.Depth >> subresource.MipLevel) : 1 };
		state.F.CmdCopyImageToBuffer(pending.Commands, source.Handle, TextureLayout(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			staging->Handle, 1, &copy);
		BufferHostReadBarrier(state, pending, staging->Handle);

		if ((source.bShaderRead || source.bStorage))
		{
			RequireTextureLayout(state, pending, source, VK_IMAGE_LAYOUT_GENERAL);
		}

		Result<uint64> submitted = Execute(state, Move(pending));

		if (!submitted)
		{
			return Result<void>::Failure(submitted.GetError());
		}

		source.LastUse = submitted.Value();
		source.LastQueue = queue;
		Result<void> waited = Wait(&state, queue, submitted.Value(), timeout);

		if (!waited)
		{
			return waited;
		}

		void* mapped = nullptr;

		if (state.F.MapMemory(state.Device, staging->Memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanMap", "Could not map texture readback memory."));
		}

		VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.memory = staging->Memory;
		range.size = VK_WHOLE_SIZE;
		const bool bInvalidated = staging->Coherent || state.F.InvalidateMappedMemoryRanges(state.Device, 1, &range) == VK_SUCCESS;

		if (bInvalidated)
		{
			CopyBytes(output.Data(), mapped, output.Size());
		}

		state.F.UnmapMemory(state.Device, staging->Memory);

		if (!bInvalidated)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanInvalidate", "Could not invalidate texture readback memory."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureCopy(State* opaque, void* native, const NativeTextureCopy& copy)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Texture& source = *static_cast<Texture*>(copy.Source);
		Texture& destination = *static_cast<Texture*>(copy.Destination);
		const uint32 sourceSubresource = copy.bRegion ? copy.SourceSubresource : UINT32_MAX;
		const uint32 destinationSubresource = copy.bRegion ? copy.DestinationSubresource : UINT32_MAX;
		RequireTextureLayout(state, pending, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sourceSubresource);
		RequireTextureLayout(state, pending, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, destinationSubresource);

		for (uint32 layer = 0; layer < copy.ArrayLayers; ++layer)
		{
			for (uint32 mip = 0; mip < copy.MipLevels; ++mip)
			{
				VkImageCopy region{};
				region.srcSubresource.aspectMask =
					!source.bDepth ? VK_IMAGE_ASPECT_COLOR_BIT
					: copy.bRegion
					? (copy.Aspect == ETextureAspect::Stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT)
					: VK_IMAGE_ASPECT_DEPTH_BIT | (GetFormatInfo(source.Format).bHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
				region.srcSubresource.mipLevel = copy.bRegion ? copy.SourceMip : mip;
				region.srcSubresource.baseArrayLayer = copy.bRegion ? copy.SourceLayer : layer;
				region.srcSubresource.layerCount = 1;
				region.dstSubresource.aspectMask = region.srcSubresource.aspectMask;
				region.dstSubresource.mipLevel = copy.bRegion ? copy.DestinationMip : mip;
				region.dstSubresource.baseArrayLayer = copy.bRegion ? copy.DestinationLayer : layer;
				region.dstSubresource.layerCount = 1;
				region.srcOffset = { static_cast<int32>(copy.SourceX), static_cast<int32>(copy.SourceY),
									static_cast<int32>(copy.SourceZ) };
				region.dstOffset = { static_cast<int32>(copy.DestinationX), static_cast<int32>(copy.DestinationY),
									static_cast<int32>(copy.DestinationZ) };
				region.extent = copy.bRegion ? VkExtent3D{ copy.Width, copy.Height, copy.Depth }
					: VkExtent3D{ (copy.Width >> mip) ? (copy.Width >> mip) : 1,
								 (copy.Height >> mip) ? (copy.Height >> mip) : 1,
								 (copy.Depth >> mip) ? (copy.Depth >> mip) : 1 };
				if (pending.bInvalidTextureLayouts)
				{
					return Result<void>::Success();
				}

				state.F.CmdCopyImage(pending.Commands, source.Handle, TextureLayout(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
					destination.Handle, TextureLayout(destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), 1, &region);
			}
		}

		if (!pending.bPrepared && pending.bAutomaticBarriers && (source.bShaderRead || source.bStorage))
		{
			RequireTextureLayout(state, pending, source, VK_IMAGE_LAYOUT_GENERAL);
		}

		if (!pending.bPrepared && pending.bAutomaticBarriers && (destination.bShaderRead || destination.bStorage))
		{
			RequireTextureLayout(state, pending, destination, VK_IMAGE_LAYOUT_GENERAL);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureWrite(
		State* opaque,
		void* native,
		void* destinationNative,
		const TextureDesc& desc,
		TextureSubresource subresource,
		Span<const Byte> data)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Texture& destination = *static_cast<Texture*>(destinationNative);
		Result<Buffer*> staged = NewBuffer(state, data.Size(), true);

		if (!staged)
		{
			return Result<void>::Failure(staged.GetError());
		}
		Buffer* staging = staged.Value();
		void* mapped = nullptr;

		if (state.F.MapMemory(state.Device, staging->Memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
		{
			FreeBuffer(state, staging);

			return Result<void>::Failure(Failure("Rhi.VulkanMap", "Could not map texture write staging."));
		}

		CopyBytes(mapped, data.Data(), data.Size());
		VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.memory = staging->Memory;
		range.size = VK_WHOLE_SIZE;
		const bool bFlushed = staging->Coherent || state.F.FlushMappedMemoryRanges(state.Device, 1, &range) == VK_SUCCESS;
		state.F.UnmapMemory(state.Device, staging->Memory);

		if (!bFlushed)
		{
			FreeBuffer(state, staging);

			return Result<void>::Failure(Failure("Rhi.VulkanFlush", "Could not flush texture write staging."));
		}

		if (!pending.Temporary.Add(staging))
		{
			FreeBuffer(state, staging);

			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain texture write staging."));
		}

		const uint32 subresourceIndex = subresource.MipLevel + subresource.ArrayLayer * destination.MipLevels;
		RequireTextureLayout(state, pending, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceIndex);
		// The poisoned list is rejected at submission; do not emit an invalid native command.
		if (pending.bInvalidTextureLayouts)
		{
			return Result<void>::Success();
		}
		VkBufferImageCopy copy{};
		copy.imageSubresource.aspectMask = subresource.Aspect == ETextureAspect::Stencil ? VK_IMAGE_ASPECT_STENCIL_BIT
			: GetFormatInfo(desc.Format).bHasDepth ? VK_IMAGE_ASPECT_DEPTH_BIT
			: VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.mipLevel = subresource.MipLevel;
		copy.imageSubresource.baseArrayLayer = subresource.ArrayLayer;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent = { (desc.Width >> subresource.MipLevel) ? (desc.Width >> subresource.MipLevel) : 1,
							(desc.Height >> subresource.MipLevel) ? (desc.Height >> subresource.MipLevel) : 1,
							(desc.Depth >> subresource.MipLevel) ? (desc.Depth >> subresource.MipLevel) : 1 };
		state.F.CmdCopyBufferToImage(pending.Commands, staging->Handle, destination.Handle,
			TextureLayout(destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), 1, &copy);

		if (pending.bAutomaticBarriers && (destination.bShaderRead || destination.bStorage))
		{
			RequireTextureLayout(state, pending, destination, VK_IMAGE_LAYOUT_GENERAL);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> PrepareTextureBufferCopy(void*, NativeTextureBufferCopy& copy)
	{
		const Texture& texture = *static_cast<Texture*>(copy.Texture);
		const FormatInfo& format = GetFormatInfo(GetTextureTransferFormat(texture.Format, copy.Subresource.Aspect));
		if (format.BytesPerBlock == 0 || copy.Footprint.RowPitchBytes % format.BytesPerBlock != 0 || uint64(copy.Footprint.RowPitchBytes / format.BytesPerBlock) * format.BlockSize > UINT32_MAX)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTexture", "The buffer row pitch cannot be represented as a Vulkan row length.");
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureBuffer(State* opaque, void* native, const NativeTextureBufferCopy& copy, bool bUpload)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& buffer = *static_cast<Buffer*>(copy.Buffer);
		Texture& texture = *static_cast<Texture*>(copy.Texture);
		const VkImageLayout transferLayout = TextureLayout(texture, bUpload ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		const uint32 subresourceIndex = copy.Subresource.MipLevel + copy.Subresource.ArrayLayer * texture.MipLevels;
		RequireTextureLayout(state, pending, texture, transferLayout, subresourceIndex);
		// The poisoned list is rejected at submission; do not emit an invalid native command.
		if (pending.bInvalidTextureLayouts)
		{
			return Result<void>::Success();
		}

		const FormatInfo& format = GetFormatInfo(GetTextureTransferFormat(texture.Format, copy.Subresource.Aspect));
		VkBufferImageCopy region{};
		region.bufferOffset = copy.BufferOffsetBytes;
		region.bufferRowLength = copy.Footprint.RowPitchBytes / format.BytesPerBlock * format.BlockSize;
		region.imageSubresource.aspectMask = copy.Subresource.Aspect == ETextureAspect::Stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : texture.bDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = copy.Subresource.MipLevel;
		region.imageSubresource.baseArrayLayer = copy.Subresource.ArrayLayer;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = { copy.Footprint.Width, copy.Footprint.Height, copy.Footprint.Depth };
		if (bUpload)
		{
			state.F.CmdCopyBufferToImage(pending.Commands, buffer.Handle, texture.Handle, transferLayout, 1, &region);
		}
		else
		{
			state.F.CmdCopyImageToBuffer(pending.Commands, texture.Handle, transferLayout, buffer.Handle, 1, &region);
		}

		if (!pending.bPrepared && pending.bAutomaticBarriers && (texture.bShaderRead || texture.bStorage))
		{
			RequireTextureLayout(state, pending, texture, VK_IMAGE_LAYOUT_GENERAL);
		}

		if (pending.bAutomaticBarriers)
		{
			Barrier(state, pending);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBufferToTexture(State* opaque, void* native, const NativeTextureBufferCopy& copy)
	{
		return RecordTextureBuffer(opaque, native, copy, true);
	}

	[[nodiscard]] Result<void> RecordTextureToBuffer(State* opaque, void* native, const NativeTextureBufferCopy& copy)
	{
		return RecordTextureBuffer(opaque, native, copy, false);
	}

	[[nodiscard]] VkResolveModeFlagBits ResolveMode(ETextureResolveMode mode) noexcept
	{
		switch (mode)
		{
		case ETextureResolveMode::Average:
			return VK_RESOLVE_MODE_AVERAGE_BIT;
		case ETextureResolveMode::SampleZero:
			return VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
		case ETextureResolveMode::Minimum:
			return VK_RESOLVE_MODE_MIN_BIT;
		case ETextureResolveMode::Maximum:
			return VK_RESOLVE_MODE_MAX_BIT;
		default:
			return VK_RESOLVE_MODE_NONE;
		}
	}

	[[nodiscard]] bool SupportsTextureResolve(const State* opaque, const TextureDesc& desc, ETextureResolveMode mode, ETextureResolveMode stencilMode) noexcept
	{
		const State& state = *opaque;
		const FormatInfo& info = GetFormatInfo(desc.Format);
		if (!info.bHasDepth)
		{
			return stencilMode == ETextureResolveMode::None &&
				(info.Kind != EFormatKind::Integer
					? mode == ETextureResolveMode::Average && SupportsColorResolve(opaque, desc.Format)
					: mode == ETextureResolveMode::SampleZero && state.bDynamicRendering && state.F.CmdBeginRendering != nullptr &&
					state.F.CmdEndRendering != nullptr && HasUsage(desc.Usage, ETextureUsage::RenderTarget) &&
					HasFormatSupport(QueryFormatSupport(opaque, desc.Format), EFormatSupport::RenderTarget));
		}
		if (state.ApiVersion < VK_API_VERSION_1_2 || state.F.CreateRenderPass2 == nullptr || !state.bLoadStoreOpNone)
		{
			return false;
		}
		VkPhysicalDeviceDepthStencilResolveProperties resolve{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES };
		VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		properties.pNext = &resolve;
		state.F.GetPhysicalDeviceProperties2(state.Physical, &properties);
		const VkResolveModeFlagBits depth = ResolveMode(mode);
		const VkResolveModeFlagBits stencil = ResolveMode(stencilMode);
		if ((depth != VK_RESOLVE_MODE_NONE && (resolve.supportedDepthResolveModes & depth) == 0) ||
			(stencil != VK_RESOLVE_MODE_NONE && (resolve.supportedStencilResolveModes & stencil) == 0) ||
			!HasFormatSupport(QueryFormatSupport(opaque, desc.Format), EFormatSupport::DepthStencil))
		{
			return false;
		}

		return !info.bHasStencil ||
			depth == stencil ||
			resolve.independentResolve == VK_TRUE ||
			(resolve.independentResolveNone == VK_TRUE && (depth == VK_RESOLVE_MODE_NONE || stencil == VK_RESOLVE_MODE_NONE));
	}

	[[nodiscard]] Result<void> PrepareTextureResolve(State* opaque, void* native, NativeTextureResolve& resolve)
	{
		if (!GetFormatInfo(resolve.Format).bHasDepth && GetFormatInfo(resolve.Format).Kind != EFormatKind::Integer)
		{
			return Result<void>::Success();
		}
		State& state = *opaque;
		if (GetFormatInfo(resolve.Format).bHasStencil && resolve.StencilMode != ETextureResolveMode::None && resolve.DestinationSubresource.MipLevel != 0 && !SupportsNonzeroMipStencilViews(state))
		{
			return Result<void>::Failure(
				EErrorCode::Unsupported,
				"Rhi.VulkanStencilMipViews",
				"NVIDIA RTX 5070 driver 617.14 cannot address nonzero-mip stencil resolve destinations correctly.");
		}
		Pending& pending = *static_cast<Pending*>(native);
		UniquePtr<DepthResolve> owner = MakeUnique<DepthResolve>(state.Resource);
		if (!owner || !pending.DepthResolves.Reserve(pending.DepthResolves.Size() + 1))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not capture the depth resolve owner."));
		}
		owner->Device = state.Device;
		owner->DestroyPass = state.F.DestroyRenderPass;
		owner->DestroyFramebuffer = state.F.DestroyFramebuffer;
		owner->DestroyView = state.F.DestroyImageView;
		const Texture& source = *static_cast<const Texture*>(resolve.Source);
		const Texture& destination = *static_cast<const Texture*>(resolve.Destination);
		const FormatInfo& info = GetFormatInfo(resolve.Format);
		const VkImageAspectFlags aspects = info.bHasDepth ? VK_IMAGE_ASPECT_DEPTH_BIT | (info.bHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0) : VK_IMAGE_ASPECT_COLOR_BIT;
		for (uint32 index = 0; index < 2; ++index)
		{
			const TextureSubresource& selected = index == 0 ? resolve.SourceSubresource : resolve.DestinationSubresource;
			VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			view.image = index == 0 ? source.Handle : destination.Handle;
			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.format = ConvertVulkanFormat(resolve.Format);
			view.subresourceRange = { aspects, selected.MipLevel, 1, selected.ArrayLayer, 1 };
			if (state.F.CreateImageView(state.Device, &view, nullptr, &owner->Views[index]) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.DepthResolve", "Could not capture a depth resolve view."));
			}
		}
		if (!info.bHasDepth)
		{
			resolve.Prepared = owner.Get();
			const bool bAdded = pending.DepthResolves.Add(Move(owner));
			ASSERT(bAdded, "Cold integer resolve views retain their reserved ownership slot.");

			return Result<void>::Success();
		}
		VkAttachmentDescription2 attachments[2]{};
		for (uint32 index = 0; index < 2; ++index)
		{
			VkAttachmentDescription2& attachment = attachments[index];
			attachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
			attachment.format = ConvertVulkanFormat(resolve.Format);
			attachment.samples = index == 0 ? static_cast<VkSampleCountFlagBits>(source.SampleCount) : VK_SAMPLE_COUNT_1_BIT;
			const bool bDepthActive = resolve.Mode != ETextureResolveMode::None;
			const bool bStencilActive = resolve.StencilMode != ETextureResolveMode::None;
			// Only active source planes load. No source plane stores, and inactive destination planes are untouched.
			attachment.loadOp = index == 0 && bDepthActive ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_NONE_EXT;
			attachment.storeOp = index == 1 && bDepthActive ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_NONE;
			attachment.stencilLoadOp = index == 0 && bStencilActive ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_NONE_EXT;
			attachment.stencilStoreOp = index == 1 && bStencilActive ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_NONE;
			attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
		}
		VkAttachmentReference2 sourceRef{ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2 };
		sourceRef.attachment = 0;
		sourceRef.layout = VK_IMAGE_LAYOUT_GENERAL;
		sourceRef.aspectMask = aspects;
		VkAttachmentReference2 destinationRef = sourceRef;
		destinationRef.attachment = 1;
		VkSubpassDescriptionDepthStencilResolve depthResolve{ VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE };
		depthResolve.depthResolveMode = ResolveMode(resolve.Mode);
		depthResolve.stencilResolveMode = ResolveMode(resolve.StencilMode);
		depthResolve.pDepthStencilResolveAttachment = &destinationRef;
		VkSubpassDescription2 subpass{ VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2 };
		subpass.pNext = info.bHasDepth ? &depthResolve : nullptr;
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.pDepthStencilAttachment = info.bHasDepth ? &sourceRef : nullptr;
		subpass.colorAttachmentCount = info.bHasDepth ? 0u : 1u;
		subpass.pColorAttachments = info.bHasDepth ? nullptr : &sourceRef;
		subpass.pResolveAttachments = info.bHasDepth ? nullptr : &destinationRef;
		VkSubpassDependency2 dependency{ VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2 };
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		VkRenderPassCreateInfo2 pass{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2 };
		pass.attachmentCount = 2;
		pass.pAttachments = attachments;
		pass.subpassCount = 1;
		pass.pSubpasses = &subpass;
		pass.dependencyCount = 1;
		pass.pDependencies = &dependency;
		if (state.F.CreateRenderPass2(state.Device, &pass, nullptr, &owner->Pass) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.DepthResolve", "Could not capture the depth resolve render pass."));
		}
		VkFramebufferCreateInfo framebuffer{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		framebuffer.renderPass = owner->Pass;
		framebuffer.attachmentCount = 2;
		framebuffer.pAttachments = owner->Views;
		framebuffer.width = resolve.Width;
		framebuffer.height = resolve.Height;
		framebuffer.layers = 1;
		if (state.F.CreateFramebuffer(state.Device, &framebuffer, nullptr, &owner->Framebuffer) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.DepthResolve", "Could not capture the depth resolve framebuffer."));
		}
		resolve.Prepared = owner.Get();
		const bool bAdded = pending.DepthResolves.Add(Move(owner));
		ASSERT(bAdded, "Cold depth resolve ownership capacity must be reserved.");

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureResolve(State* opaque, void* native, const NativeTextureResolve& resolve)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Texture& source = *static_cast<Texture*>(resolve.Source);
		Texture& destination = *static_cast<Texture*>(resolve.Destination);
		if (source.bDepth || GetFormatInfo(source.Format).Kind == EFormatKind::Integer)
		{
			NativeTextureResolve captured = resolve;
			if (captured.Prepared == nullptr)
			{
				if (pending.bPrepared)
				{
					return Result<void>::Failure(NativeError(EErrorCode::Conflict, "Rhi.ResolveCapture", "The depth resolve was not captured."));
				}
				Result<void> result = PrepareTextureResolve(opaque, native, captured);
				if (!result)
				{
					return result;
				}
			}
			RequireTextureLayout(state, pending, source, VK_IMAGE_LAYOUT_GENERAL,
				captured.SourceSubresource.MipLevel + captured.SourceSubresource.ArrayLayer * source.MipLevels);
			RequireTextureLayout(state, pending, destination, VK_IMAGE_LAYOUT_GENERAL,
				captured.DestinationSubresource.MipLevel +
				captured.DestinationSubresource.ArrayLayer * destination.MipLevels);
			if (pending.bInvalidTextureLayouts)
			{
				return Result<void>::Failure(NativeError(EErrorCode::Conflict, "Rhi.ResolveLayout", "The depth resolve attachment layouts were not established."));
			}
			const DepthResolve& owner = *static_cast<const DepthResolve*>(captured.Prepared);
			if (!source.bDepth)
			{
				VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				color.imageView = owner.Views[0];
				color.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				color.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
				color.resolveImageView = owner.Views[1];
				color.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL;
				color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
				color.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
				VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO };
				rendering.renderArea.extent = { captured.Width, captured.Height };
				rendering.layerCount = 1;
				rendering.colorAttachmentCount = 1;
				rendering.pColorAttachments = &color;
				state.F.CmdBeginRendering(pending.Commands, &rendering);
				state.F.CmdEndRendering(pending.Commands);
				pending.LastPreparedDraw = UINT32_MAX;
				pending.LastTexturePipeline = nullptr;

				return Result<void>::Success();
			}
			VkRenderPassBeginInfo begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
			begin.renderPass = owner.Pass;
			begin.framebuffer = owner.Framebuffer;
			begin.renderArea.extent = { captured.Width, captured.Height };
			state.F.CmdBeginRenderPass(pending.Commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
			state.F.CmdEndRenderPass(pending.Commands);
			pending.LastPreparedDraw = UINT32_MAX;
			pending.LastTexturePipeline = nullptr;

			return Result<void>::Success();
		}
		RequireTextureLayout(state, pending, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			resolve.SourceSubresource.MipLevel + resolve.SourceSubresource.ArrayLayer * source.MipLevels);
		RequireTextureLayout(state, pending, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			resolve.DestinationSubresource.MipLevel + resolve.DestinationSubresource.ArrayLayer * destination.MipLevels);
		if (pending.bInvalidTextureLayouts)
		{
			return Result<void>::Failure(NativeError(EErrorCode::ExternalFailure, "Rhi.VulkanResolve", "Could not establish resolve layouts."));
		}
		VkImageResolve region{};
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 1;
		region.srcSubresource.mipLevel = resolve.SourceSubresource.MipLevel;
		region.srcSubresource.baseArrayLayer = resolve.SourceSubresource.ArrayLayer;
		region.dstSubresource = region.srcSubresource;
		region.dstSubresource.mipLevel = resolve.DestinationSubresource.MipLevel;
		region.dstSubresource.baseArrayLayer = resolve.DestinationSubresource.ArrayLayer;
		region.extent = { resolve.Width, resolve.Height, 1 };
		state.F.CmdResolveImage(pending.Commands, source.Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination.Handle,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		if (!pending.bPrepared && pending.bAutomaticBarriers && (source.bShaderRead || source.bStorage))
		{
			RequireTextureLayout(state, pending, source, VK_IMAGE_LAYOUT_GENERAL);
		}

		if (!pending.bPrepared && pending.bAutomaticBarriers && (destination.bShaderRead || destination.bStorage))
		{
			RequireTextureLayout(state, pending, destination, VK_IMAGE_LAYOUT_GENERAL);
		}

		return Result<void>::Success();
	}
}
