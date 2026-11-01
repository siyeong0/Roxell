#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<FramebufferHandle> Device::CreateFramebuffer(const FramebufferDesc& desc)
	{
		CallScope call(*this, false);

		if (!m_State->m_Backend.Capabilities.Features.bGraphics)
		{
			return Result<FramebufferHandle>::Failure(EErrorCode::Unsupported, "Rhi.Framebuffer", "Graphics execution is unavailable.");
		}

		const bool bHasDepth = desc.Depth.Owner != 0 || desc.Depth.Slot != 0 || desc.Depth.Generation != 0;
		const bool bHasRateImage = desc.ShadingRateImage.Owner != 0 || desc.ShadingRateImage.Slot != 0 || desc.ShadingRateImage.Generation != 0;

		ASSERT(!((desc.Colors.IsEmpty() && !bHasDepth) || desc.Colors.Size() > 8), "A framebuffer requires a color or depth attachment.");
		ASSERT(!(!desc.ColorSubresources.IsEmpty() && desc.ColorSubresources.Size() != desc.Colors.Size()), "Color subresources must match the color attachment count.");
		ASSERT(!(!bHasDepth && (desc.DepthSubresource.MipLevel != 0 || desc.DepthSubresource.ArrayLayer != 0 || desc.DepthSubresource.Aspect != ETextureAspect::Default)),
			"A depth subresource requires a depth attachment.");

		if (desc.Colors.Size() > m_State->m_Backend.Capabilities.Limits.MaximumColorAttachments)
		{
			return Result<FramebufferHandle>::Failure(EErrorCode::Unsupported, "Rhi.Framebuffer", "The device has fewer color attachments.");
		}
		NativeFramebufferDesc native{};
		for (usize index = 0; index < desc.Colors.Size(); ++index)
		{
			TextureSlot* const texture = findTexture(desc.Colors[index]);

			ASSERT(texture != nullptr, "RHI operations require valid handles for this device.");

			const TextureSubresource subresource = desc.ColorSubresources.IsEmpty() ? TextureSubresource{} : desc.ColorSubresources[index];
			ASSERT(!((subresource.Aspect != ETextureAspect::Default && subresource.Aspect != ETextureAspect::Color) ||
					!HasUsage(texture->Descriptor.Usage, ETextureUsage::RenderTarget) ||
					subresource.MipLevel >= texture->Descriptor.MipLevels ||
					subresource.ArrayLayer >= (texture->Descriptor.Dimension == ETextureDimension::Texture3D ? MipExtent(texture->Descriptor.Depth, subresource.MipLevel) : texture->Descriptor.ArrayLayers)), "A color attachment is invalid.");
			if (
				texture->FramebufferRefs == NumericLimits<uint32>::max())
			{
				return Result<FramebufferHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Framebuffer", "A color attachment has too many references.");
			}

			if (index == 0)
			{
				native.Width = MipExtent(texture->Descriptor.Width, subresource.MipLevel);
				native.Height = MipExtent(texture->Descriptor.Height, subresource.MipLevel);
				native.SampleCount = texture->Descriptor.SampleCount;
			}
			else
			{
				ASSERT(MipExtent(texture->Descriptor.Width, subresource.MipLevel) == native.Width &&
					MipExtent(texture->Descriptor.Height, subresource.MipLevel) == native.Height &&
					texture->Descriptor.SampleCount == native.SampleCount,
					"Attachment extents differ.");
			}

			for (usize previous = 0; previous < index; ++previous)
			{
				ASSERT(!(desc.Colors[previous] == desc.Colors[index]), "An attachment is repeated.");
			}
			native.Colors[index] = texture->Native;
			native.ColorFormats[index] = texture->Descriptor.Format;
			native.ColorSubresources[index] = subresource;
		}

		native.ColorCount = static_cast<uint32>(desc.Colors.Size());

		if (bHasDepth)
		{
			TextureSlot* const depth = findTexture(desc.Depth);

			ASSERT(depth != nullptr, "RHI operations require valid handles for this device.");

			ASSERT(!(desc.DepthSubresource.Aspect != ETextureAspect::Default ||
					desc.DepthSubresource.MipLevel >= depth->Descriptor.MipLevels ||
					desc.DepthSubresource.ArrayLayer >= depth->Descriptor.ArrayLayers),
				"The depth subresource is invalid.");

			if (desc.Colors.IsEmpty())
			{
				native.Width = MipExtent(depth->Descriptor.Width, desc.DepthSubresource.MipLevel);
				native.Height = MipExtent(depth->Descriptor.Height, desc.DepthSubresource.MipLevel);
				native.SampleCount = depth->Descriptor.SampleCount;
			}

			ASSERT(HasUsage(depth->Descriptor.Usage, ETextureUsage::DepthStencil) &&
				MipExtent(depth->Descriptor.Width, desc.DepthSubresource.MipLevel) == native.Width &&
				MipExtent(depth->Descriptor.Height, desc.DepthSubresource.MipLevel) == native.Height &&
				depth->Descriptor.SampleCount == native.SampleCount, "The depth attachment is invalid.");
			if (
				depth->FramebufferRefs == NumericLimits<uint32>::max())
			{
				return Result<FramebufferHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Framebuffer", "The depth attachment has too many references.");
			}
			native.Depth = depth->Native;
			native.DepthFormat = depth->Descriptor.Format;
			native.DepthSubresource = desc.DepthSubresource;
		}

		if (bHasRateImage)
		{
			ASSERT(desc.ShadingRateImage.IsValid(), "RHI operations require valid handles for this device.");

			if (!m_State->m_Backend.Capabilities.Features.bShadingRateImage || m_State->m_Backend.Capabilities.Limits.ShadingRateImageTileWidth == 0 || m_State->m_Backend.Capabilities.Limits.ShadingRateImageTileHeight == 0)
			{
				return Result<FramebufferHandle>::Failure(EErrorCode::Unsupported, "Rhi.ShadingRateImage", "The device does not support shading rate images.");
			}
			TextureSlot* const image = findTexture(desc.ShadingRateImage);

			ASSERT(image != nullptr, "RHI operations require valid handles for this device.");

			const uint32 tileWidth = m_State->m_Backend.Capabilities.Limits.ShadingRateImageTileWidth;
			const uint32 tileHeight = m_State->m_Backend.Capabilities.Limits.ShadingRateImageTileHeight;

			ASSERT(HasUsage(image->Descriptor.Usage, ETextureUsage::ShadingRateSource) &&
				image->Descriptor.Width == (native.Width - 1) / tileWidth + 1 &&
				image->Descriptor.Height == (native.Height - 1) / tileHeight + 1 &&
				desc.ShadingRateImage != desc.Depth, "The shading rate image usage, extent, or attachment is invalid.");
			if (
				image->FramebufferRefs == NumericLimits<uint32>::max())
			{
				return Result<FramebufferHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.ShadingRateImage", "The shading rate image has too many references.");
			}

			for (const TextureHandle color : desc.Colors)
			{
				ASSERT(!(color == desc.ShadingRateImage), "The shading rate image cannot also be a color attachment.");
			}
			native.ShadingRateImage = image->Native;
		}

		const bool bNewSlot = m_State->m_FreeFramebuffer == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Framebuffers.Size()) : m_State->m_FreeFramebuffer;

		if (bNewSlot && (m_State->m_Framebuffers.Size() >= NumericLimits<uint32>::max() || !m_State->m_Framebuffers.Add(FramebufferSlot{})))
		{
			return Result<FramebufferHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a framebuffer slot.");
		}

		Result<void*> created = backend::CreateFramebuffer(m_State->m_Backend.State, native);

		if (!created)
		{
			if (bNewSlot)
			{
				m_State->m_Framebuffers[index].NextFree = m_State->m_FreeFramebuffer;
				m_State->m_FreeFramebuffer = index;
			}

			return Result<FramebufferHandle>::Failure(created.GetError());
		}
		FramebufferSlot& slot = m_State->m_Framebuffers[index];

		if (!bNewSlot)
		{
			m_State->m_FreeFramebuffer = slot.NextFree;
		}

		slot.NextFree = NumericLimits<uint32>::max();
		slot.Native = created.Value();
		slot.ColorCount = native.ColorCount;
		slot.Width = native.Width;
		slot.Height = native.Height;

		for (uint32 color = 0; color < native.ColorCount; ++color)
		{
			slot.Colors[color] = desc.Colors[color];
			slot.ColorSubresources[color] = native.ColorSubresources[color];
			++m_State->m_Textures[desc.Colors[color].Slot].FramebufferRefs;
		}
		slot.Depth = desc.Depth;
		slot.DepthSubresource = desc.DepthSubresource;

		if (bHasDepth)
		{
			++m_State->m_Textures[desc.Depth.Slot].FramebufferRefs;
		}
		slot.ShadingRateImage = desc.ShadingRateImage;

		if (desc.ShadingRateImage.IsValid())
		{
			++m_State->m_Textures[desc.ShadingRateImage.Slot].FramebufferRefs;
		}
		slot.RecordedUses = 0;
		slot.bLive = true;

		return Result<FramebufferHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyFramebuffer(FramebufferHandle handle)
	{
		CallScope call(*this, false);
		FramebufferSlot* const slot = findFramebuffer(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->RecordedUses != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "A recording command list references the framebuffer.");
		}

		Result<void> destroyed = backend::DestroyFramebuffer(m_State->m_Backend.State, slot->Native);

		if (!destroyed)
		{
			return destroyed;
		}

		for (uint32 color = 0; color < slot->ColorCount; ++color)
		{
			--m_State->m_Textures[slot->Colors[color].Slot].FramebufferRefs;
		}

		if (slot->Depth.IsValid())
		{
			--m_State->m_Textures[slot->Depth.Slot].FramebufferRefs;
		}

		if (slot->ShadingRateImage.IsValid())
		{
			--m_State->m_Textures[slot->ShadingRateImage.Slot].FramebufferRefs;
		}
		slot->Native = nullptr;
		slot->ColorCount = 0;
		slot->ShadingRateImage = {};
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeFramebuffer;
			m_State->m_FreeFramebuffer = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::SetResourceStatesForFramebuffer(CommandListHandle list, const RenderPassLayout& layout)
	{
		CallScope call(*this, true);
		const FramebufferHandle handle = layout.Framebuffer;
		DepthAttachmentOptions depthOptions;
		depthOptions.LoadOp = layout.DepthLoad == EAttachmentLoad::Clear ? EDepthLoadOp::Clear : EDepthLoadOp::Load;
		depthOptions.StencilLoadOp = layout.StencilLoad == EAttachmentLoad::Clear ? EDepthLoadOp::Clear : EDepthLoadOp::Load;
		depthOptions.bReadOnly = layout.bReadOnlyDepth;
		depthOptions.bReadOnlyStencil = layout.bReadOnlyStencil;
		ASSERT(layout.DepthLoad <= EAttachmentLoad::Clear && layout.StencilLoad <= EAttachmentLoad::Clear, "Render pass depth and stencil load operations are valid.");
		CommandSlot* const command = findCommand(list);
		FramebufferSlot* const framebuffer = findFramebuffer(handle);

		ASSERT(!(command == nullptr || framebuffer == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!(command->Queue != EQueue::Graphics ||
				command->bRendering ||
				depthOptions.LoadOp > EDepthLoadOp::Load ||
				depthOptions.StencilLoadOp > EDepthLoadOp::Load ||
				(!framebuffer->Depth.IsValid() && depthOptions.LoadOp != EDepthLoadOp::Load) ||
				(depthOptions.bReadOnlyStencil && depthOptions.StencilLoadOp != EDepthLoadOp::Load) ||
				((!framebuffer->Depth.IsValid() || !GetFormatInfo(m_State->m_Textures[framebuffer->Depth.Slot].Descriptor.Format).bHasStencil) &&
				 (depthOptions.StencilLoadOp != EDepthLoadOp::Load || depthOptions.bReadOnlyStencil)) ||
				(depthOptions.bReadOnly && (!framebuffer->Depth.IsValid() || depthOptions.LoadOp != EDepthLoadOp::Load))),
			"Framebuffer states require a valid graphics list and attachment options.");

		const auto validate = [&](TextureHandle texture, ETrackedResourceState state) -> Result<void>
			{
				const TextureSlot* const slot = findTexture(texture);

				ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

				if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
				{
					poisonCommandList(list);

					return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list references a framebuffer attachment.");
				}

				if (slot->PermanentState != ETrackedResourceState::Unknown && slot->PermanentState != state)
				{
					poisonCommandList(list);

					return Result<void>::Failure(EErrorCode::Conflict, "Rhi.PermanentTextureState", "An attachment conflicts with its permanent state.");
				}

				return Result<void>::Success();
			};
		for (uint32 color = 0; color < framebuffer->ColorCount; ++color)
		{
			Result<void> result = validate(framebuffer->Colors[color], ETrackedResourceState::RenderTarget);

			if (!result)
			{
				poisonCommandList(list);

				return result;
			}
		}

		if (framebuffer->Depth.IsValid())
		{
			Result<void> result = validate(framebuffer->Depth, depthOptions.bReadOnly ? ETrackedResourceState::DepthRead : ETrackedResourceState::DepthWrite);

			if (!result)
			{
				poisonCommandList(list);

				return result;
			}
		}

		if (framebuffer->ShadingRateImage.IsValid())
		{
			Result<void> result = validate(framebuffer->ShadingRateImage, ETrackedResourceState::ShadingRateSource);

			if (!result)
			{
				poisonCommandList(list);

				return result;
			}
		}

		const usize useCount = static_cast<usize>(framebuffer->ColorCount) + (framebuffer->Depth.IsValid() ? 1 : 0) + (framebuffer->ShadingRateImage.IsValid() ? 1 : 0);

		if (command->TextureUses.Size() > NumericLimits<usize>::max() - useCount || !command->TextureUses.Reserve(command->TextureUses.Size() + useCount))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not reserve framebuffer state records.");
		}

		for (uint32 color = 0; color < framebuffer->ColorCount; ++color)
		{
			const TextureHandle texture = framebuffer->Colors[color];
			TextureSubresource subresource = framebuffer->ColorSubresources[color];
			// A 3D render target selects a depth slice, but its state covers the whole mip.
			if (m_State->m_Textures[texture.Slot].Descriptor.Dimension == ETextureDimension::Texture3D)
			{
				subresource.ArrayLayer = 0;
			}

			Result<void> result = SetTextureState(list, texture, subresource, ETextureState::RenderTarget);

			if (!result)
			{
				poisonCommandList(list);

				return result;
			}
		}

		if (framebuffer->Depth.IsValid())
		{
			TextureSubresource plane = framebuffer->DepthSubresource;
			plane.Aspect = ETextureAspect::Depth;
			Result<void> result = SetTextureState(list, framebuffer->Depth, plane, depthOptions.bReadOnly ? ETextureState::DepthRead : ETextureState::DepthWrite);
			if (result && GetFormatInfo(m_State->m_Textures[framebuffer->Depth.Slot].Descriptor.Format).bHasStencil)
			{
				plane.Aspect = ETextureAspect::Stencil;
				result = SetTextureState(list, framebuffer->Depth, plane, depthOptions.bReadOnlyStencil ? ETextureState::DepthRead : ETextureState::DepthWrite);
			}

			if (!result)
			{
				poisonCommandList(list);

				return result;
			}
		}

		if (framebuffer->ShadingRateImage.IsValid())
		{
			Result<void> result = SetTextureState(list, framebuffer->ShadingRateImage, ETextureState::ShadingRateSource);
			if (!result)
			{
				poisonCommandList(list);
			}

			return result;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::BeginRendering(CommandListHandle list, const RenderPassDesc& pass)
	{
		CallScope call(*this, true); const FramebufferHandle handle = pass.Layout.Framebuffer;
		DepthAttachmentOptions depthOptions;
		depthOptions.LoadOp = pass.Layout.DepthLoad == EAttachmentLoad::Clear ? EDepthLoadOp::Clear : EDepthLoadOp::Load;
		depthOptions.StencilLoadOp = pass.Layout.StencilLoad == EAttachmentLoad::Clear ? EDepthLoadOp::Clear : EDepthLoadOp::Load;
		depthOptions.ClearValue = pass.ClearValues.Depth;
		depthOptions.ClearStencil = pass.ClearValues.Stencil;
		depthOptions.bReadOnly = pass.Layout.bReadOnlyDepth;
		depthOptions.bReadOnlyStencil = pass.Layout.bReadOnlyStencil;
		CommandSlot* const command = findCommand(list);
		FramebufferSlot* const framebuffer = findFramebuffer(handle);

		ASSERT(!(command == nullptr || framebuffer == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(command->Queue == EQueue::Graphics && !command->bRendering, "Rendering begins on a graphics list outside an active pass.");
		ASSERT(depthOptions.ClearValue >= 0.0f && depthOptions.ClearValue <= 1.0f, "Render pass depth clear is finite and within [0, 1].");
		ASSERT(!depthOptions.bReadOnly || (framebuffer->Depth.IsValid() && depthOptions.LoadOp == EDepthLoadOp::Load), "Read-only depth requires an existing attachment and load operation.");
		ASSERT(framebuffer->Depth.IsValid() || depthOptions.LoadOp == EDepthLoadOp::Load, "A render pass without depth cannot clear depth.");
		ASSERT(!depthOptions.bReadOnlyStencil || depthOptions.StencilLoadOp == EDepthLoadOp::Load, "Read-only stencil requires a load operation.");
		ASSERT
		((framebuffer->Depth.IsValid() && GetFormatInfo(m_State->m_Textures[framebuffer->Depth.Slot].Descriptor.Format).bHasStencil) ||
			(depthOptions.StencilLoadOp == EDepthLoadOp::Load && !depthOptions.bReadOnlyStencil),
			"Stencil operations require a stencil attachment.");

		ASSERT(pass.Layout.DepthLoad <= EAttachmentLoad::Clear && pass.Layout.StencilLoad <= EAttachmentLoad::Clear, "Render pass depth and stencil load operations are valid.");
		ASSERT(pass.Layout.ColorLoads.Size() == framebuffer->ColorCount && (pass.ClearValues.Colors.IsEmpty() || pass.ClearValues.Colors.Size() == framebuffer->ColorCount),
			"Render pass color loads and clear values match the framebuffer.");
		for (uint32 color = 0; color < framebuffer->ColorCount; ++color)
		{
			ASSERT(pass.Layout.ColorLoads[color] <= EAttachmentLoad::Clear, "Render pass color load operations are valid.");
			ASSERT(pass.Layout.ColorLoads[color] != EAttachmentLoad::Clear || pass.ClearValues.Colors.Size() == framebuffer->ColorCount,
				"Cleared color attachments require a complete clear value array.");
			ASSERT(!(findTexture(framebuffer->Colors[color]) == nullptr), "RHI operations require valid handles for this device.");
			if
				(m_State->m_Textures[framebuffer->Colors[color].Slot].bSwapChainImage && m_State->m_Textures[framebuffer->Colors[color].Slot].bSubmitted)
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.SwapChainImage", "The acquired image was already submitted.");
			}
			ASSERT(pass.Layout.ColorLoads[color] != EAttachmentLoad::Clear || ValidAttachmentClear(m_State->m_Textures[framebuffer->Colors[color].Slot].Descriptor.Format, pass.ClearValues.Colors[color]),
				"Render pass clear values match their attachment formats.");

			const uint32 recording = m_State->m_Textures[framebuffer->Colors[color].Slot].RecordingSlot;

			if (recording != NumericLimits<uint32>::max() && recording != list.Slot)
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list references an attachment.");
			}
		}

		if (framebuffer->Depth.IsValid())
		{
			ASSERT(!(findTexture(framebuffer->Depth) == nullptr), "RHI operations require valid handles for this device.");

			const uint32 recording = m_State->m_Textures[framebuffer->Depth.Slot].RecordingSlot;

			if (recording != NumericLimits<uint32>::max() && recording != list.Slot)
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list references the depth attachment.");
			}
		}

		if (framebuffer->ShadingRateImage.IsValid())
		{
			ASSERT(!(findTexture(framebuffer->ShadingRateImage) == nullptr), "RHI operations require valid handles for this device.");

			const uint32 recording = m_State->m_Textures[framebuffer->ShadingRateImage.Slot].RecordingSlot;

			if (recording != NumericLimits<uint32>::max() && recording != list.Slot)
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list references the shading rate image.");
			}
		}

		const usize useCount = framebuffer->ColorCount + (framebuffer->Depth.IsValid() ? 1 : 0) + (framebuffer->ShadingRateImage.IsValid() ? 1 : 0);
		if (command->TextureUses.Size() > NumericLimits<usize>::max() - useCount ||
			!command->TextureUses.Reserve(command->TextureUses.Size() + useCount) ||
			!command->FramebufferUses.Add({ handle, framebuffer->Native }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track framebuffer attachments.");
		}

		for (uint32 color = 0; color < framebuffer->ColorCount; ++color)
		{
			const TextureHandle texture = framebuffer->Colors[color];
			(void)command->TextureUses.Add({ texture, m_State->m_Textures[texture.Slot].Native, false, ETrackedResourceState::RenderTarget });
			m_State->m_Textures[texture.Slot].RecordingSlot = list.Slot;
			command->ColorTargets[color] = texture;
		}

		if (framebuffer->Depth.IsValid())
		{
			(void)command->TextureUses.Add({ framebuffer->Depth, m_State->m_Textures[framebuffer->Depth.Slot].Native, false,
											depthOptions.bReadOnly ? ETrackedResourceState::DepthRead : ETrackedResourceState::DepthWrite });
			m_State->m_Textures[framebuffer->Depth.Slot].RecordingSlot = list.Slot;
		}

		if (framebuffer->ShadingRateImage.IsValid())
		{
			(void)command->TextureUses.Add({ framebuffer->ShadingRateImage, m_State->m_Textures[framebuffer->ShadingRateImage.Slot].Native, false,
											ETrackedResourceState::ShadingRateSource });
			m_State->m_Textures[framebuffer->ShadingRateImage.Slot].RecordingSlot = list.Slot;
		}
		++framebuffer->RecordedUses;
		Result<void> recorded = backend::RecordPreparedRendering(m_State->m_Backend.State, command->Native, framebuffer->Native, pass.Layout.ColorLoads, pass.ClearValues.Colors, depthOptions);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}
		command->ColorTarget = framebuffer->ColorCount != 0 ? framebuffer->Colors[0] : TextureHandle{};
		command->ColorCount = framebuffer->ColorCount;
		command->DepthTarget = framebuffer->Depth;
		command->bReadOnlyDepth = depthOptions.bReadOnly;
		command->bReadOnlyStencil = depthOptions.bReadOnlyStencil;
		command->ActiveFramebuffer = handle;
		command->bRendering = true;
		command->ViewportCount = 1;
		command->ScissorCount = 1;

		return Result<void>::Success();
	}

	[[nodiscard]] bool Device::matchesRendering(const CommandSlot& command, const GraphicsPipelineSlot& pipeline) const noexcept
	{
		const TextureHandle sampleTarget = command.ColorCount != 0 ? command.ColorTargets[0] : command.DepthTarget;

		if (((command.bReadOnlyDepth && pipeline.bWritesDepth) || (command.bReadOnlyStencil && pipeline.bWritesStencil)) ||
			pipeline.ColorCount != command.ColorCount ||
			(command.ActiveFramebuffer.IsValid() && m_State->m_Framebuffers[command.ActiveFramebuffer.Slot].ShadingRateImage.IsValid() && !pipeline.bSupportsShadingRateImage) ||
			pipeline.ViewportCount != command.ViewportCount ||
			pipeline.ViewportCount != command.ScissorCount ||
			pipeline.SampleCount != m_State->m_Textures[sampleTarget.Slot].Descriptor.SampleCount ||
			pipeline.DepthFormat != (command.DepthTarget.IsValid() ? m_State->m_Textures[command.DepthTarget.Slot].Descriptor.Format : EFormat::UNKNOWN))
		{
			return false;
		}

		for (uint32 color = 0; color < command.ColorCount; ++color)
		{
			if (pipeline.ColorFormats[color] != m_State->m_Textures[command.ColorTargets[color].Slot].Descriptor.Format)
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] FramebufferSlot* Device::findFramebuffer(FramebufferHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Framebuffers.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		FramebufferSlot& slot = m_State->m_Framebuffers[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	[[nodiscard]] Result<void> Device::SetViewport(CommandListHandle list, const ViewportDesc& viewport)
	{
		CallScope call(*this, true, true);
		CommandSlot* const command = findCommand(list);
		if (command != nullptr && command->PreparedGroup)
		{
			return setPreparedViewport(*command, viewport);
		}

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(!command->bRendering), "A viewport requires an active rendering section.");

		const TextureHandle targetHandle = command->ColorCount != 0 ? command->ColorTargets[0] : command->DepthTarget;
		const TextureDesc& target = m_State->m_Textures[targetHandle.Slot].Descriptor;
		const uint32 targetWidth = command->ActiveFramebuffer.IsValid() ? m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].Width : target.Width;
		const uint32 targetHeight = command->ActiveFramebuffer.IsValid() ? m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].Height : target.Height;

		ASSERT(!(!IsFinite(viewport.X) ||
				!IsFinite(viewport.Y) ||
				!IsFinite(viewport.Width) ||
				!IsFinite(viewport.Height) ||
				!IsFinite(viewport.MinDepth) ||
				!IsFinite(viewport.MaxDepth) ||
				viewport.X < 0.0f ||
				viewport.Y < 0.0f ||
				viewport.Width <= 0.0f ||
				viewport.Height == 0.0f ||
				viewport.X + viewport.Width> static_cast<float>(targetWidth) ||
				viewport.Y> static_cast<float>(targetHeight) ||
				viewport.Y + viewport.Height < 0.0f ||
				viewport.Y + viewport.Height> static_cast<float>(targetHeight) ||
				viewport.MinDepth < 0.0f ||
				viewport.MaxDepth> 1.0f ||
				viewport.MinDepth > viewport.MaxDepth),
			"The viewport is outside the rendering target or depth range.");

		if (viewport.Height < 0.0f && m_State->m_Kind != EBackend::Vulkan)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.Viewport", "A negative viewport height requires Vulkan.");
		}

		Result<void> recorded = backend::RecordSetViewport(m_State->m_Backend.State, command->Native, { &viewport, 1 });
		if (!recorded)
		{
			poisonCommandList(list);
		}
		else
		{
			command->ViewportCount = 1;
		}

		return recorded;
	}

	[[nodiscard]] Result<void> Device::SetViewports(CommandListHandle list, Span<const ViewportDesc> viewports)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(!command->bRendering || viewports.IsEmpty() || viewports.Size() > m_State->m_Backend.Capabilities.Limits.MaximumViewports), "A supported number of viewports requires active rendering.");

		const TextureHandle targetHandle = command->ColorCount != 0 ? command->ColorTargets[0] : command->DepthTarget;
		const TextureDesc& target = m_State->m_Textures[targetHandle.Slot].Descriptor;
		const uint32 width = command->ActiveFramebuffer.IsValid() ? m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].Width : target.Width;
		const uint32 height = command->ActiveFramebuffer.IsValid() ? m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].Height : target.Height;

		for (const ViewportDesc& viewport : viewports)
		{
			ASSERT(!(!IsFinite(viewport.X) ||
					!IsFinite(viewport.Y) ||
					!IsFinite(viewport.Width) ||
					!IsFinite(viewport.Height) ||
					!IsFinite(viewport.MinDepth) ||
					!IsFinite(viewport.MaxDepth) ||
					viewport.X < 0.0f ||
					viewport.Y < 0.0f ||
					viewport.Width <= 0.0f ||
					viewport.Height == 0.0f ||
					viewport.X + viewport.Width> static_cast<float>(width) ||
					viewport.Y> static_cast<float>(height) ||
					viewport.Y + viewport.Height < 0.0f ||
					viewport.Y + viewport.Height> static_cast<float>(height) ||
					viewport.MinDepth < 0.0f ||
					viewport.MaxDepth> 1.0f ||
					viewport.MinDepth > viewport.MaxDepth),
				"A viewport is outside the rendering target or depth range.");

			if (viewport.Height < 0.0f && m_State->m_Kind != EBackend::Vulkan)
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.Viewport", "A negative viewport height requires Vulkan.");
			}
		}

		Result<void> recorded = backend::RecordSetViewport(m_State->m_Backend.State, command->Native, viewports);

		if (!recorded)
		{
			poisonCommandList(list);
		}
		else
		{
			command->ViewportCount = static_cast<uint32>(viewports.Size());
		}

		return recorded;
	}

	[[nodiscard]] Result<void> Device::SetScissor(CommandListHandle list, const ScissorDesc& scissor)
	{
		CallScope call(*this, true, true);
		CommandSlot* const command = findCommand(list);
		if (command != nullptr && command->PreparedGroup)
		{
			return setPreparedScissor(*command, scissor);
		}

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(!command->bRendering), "A scissor requires an active rendering section.");

		const TextureHandle targetHandle = command->ColorCount != 0 ? command->ColorTargets[0] : command->DepthTarget;
		const TextureDesc& target = m_State->m_Textures[targetHandle.Slot].Descriptor;
		const uint32 targetWidth = command->ActiveFramebuffer.IsValid() ? m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].Width : target.Width;
		const uint32 targetHeight = command->ActiveFramebuffer.IsValid() ? m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].Height : target.Height;

		ASSERT(!(scissor.Width == 0 || scissor.Height == 0 || scissor.X >= targetWidth || scissor.Y >= targetHeight || scissor.Width > targetWidth - scissor.X || scissor.Height > targetHeight - scissor.Y),
			"The scissor is empty or outside the rendering target.");

		Result<void> recorded = backend::RecordSetScissor(m_State->m_Backend.State, command->Native, { &scissor, 1 });
		if (!recorded)
		{
			poisonCommandList(list);
		}
		else
		{
			command->ScissorCount = 1;
		}

		return recorded;
	}

	[[nodiscard]] Result<void> Device::SetScissors(CommandListHandle list, Span<const ScissorDesc> scissors)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(!command->bRendering || scissors.IsEmpty() || scissors.Size() > m_State->m_Backend.Capabilities.Limits.MaximumViewports), "A supported number of scissors requires active rendering.");

		const TextureHandle targetHandle = command->ColorCount != 0 ? command->ColorTargets[0] : command->DepthTarget;
		const TextureDesc& target = m_State->m_Textures[targetHandle.Slot].Descriptor;
		const uint32 width = command->ActiveFramebuffer.IsValid() ? m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].Width : target.Width;
		const uint32 height = command->ActiveFramebuffer.IsValid() ? m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].Height : target.Height;

		for (const ScissorDesc& scissor : scissors)
		{
			ASSERT(!(scissor.Width == 0 || scissor.Height == 0 || scissor.X >= width || scissor.Y >= height || scissor.Width > width - scissor.X || scissor.Height > height - scissor.Y),
				"A scissor is empty or outside the rendering target.");
		}

		Result<void> recorded = backend::RecordSetScissor(m_State->m_Backend.State, command->Native, scissors);

		if (!recorded)
		{
			poisonCommandList(list);
		}
		else
		{
			command->ScissorCount = static_cast<uint32>(scissors.Size());
		}

		return recorded;
	}

	[[nodiscard]]

	[[nodiscard]] Result<void> Device::BeginRendering(CommandListHandle handle, PreparedAttachmentRef ref, const RenderPassClearValues& clearValues)
	{
		const
			Span<const ClearColor> colors = clearValues.Colors;
		const float32 clearDepth = clearValues.Depth;
		const
			uint8 clearStencil = clearValues.Stencil;
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(!(phase == nullptr ||
				ref.List != handle ||
				ref.Phase != preparedList(command)->ActivePhase ||
				ref.Slot >= phase->Attachments.Size() ||
				command->bRendering ||
				!(clearDepth >= 0.0f && clearDepth <= 1.0f)),
			"Prepared recording requires valid admitted input and capacity.");

		rhi::PreparedRecordingAttachment const& attachment = phase->Attachments[ref.Slot];
		ASSERT(colors.IsEmpty() || colors.Size() == attachment.ColorCount, "Prepared recording requires valid admitted input and capacity.");
		for (uint32 index = 0; index < attachment.ColorCount; ++index)
		{
			ASSERT(attachment.ColorLoads[index] != EAttachmentLoad::Clear || colors.Size() == attachment.ColorCount, "Cleared color attachments require a complete clear value array.");
			ASSERT(!(attachment.ColorLoads[index] == EAttachmentLoad::Clear &&
					!ValidAttachmentClear(m_State->m_Textures[m_State->m_Framebuffers[attachment.Framebuffer.Slot].Colors[index].Slot].Descriptor.Format, colors[index])),
				"Prepared recording requires valid admitted input and capacity.");
		}

		ASSERT(!(phase->Used.RenderPasses >= phase->Capacity.RenderPasses), "Prepared recording requires valid admitted input and capacity.");

		rhi::FramebufferSlot& framebuffer = m_State->m_Framebuffers[attachment.Framebuffer.Slot];
		DepthAttachmentOptions depthOptions;
		depthOptions.LoadOp = attachment.DepthLoad;
		depthOptions.ClearValue = clearDepth;
		depthOptions.bReadOnly = attachment.bReadOnlyDepth;
		depthOptions.StencilLoadOp = attachment.StencilLoad;
		depthOptions.ClearStencil = clearStencil;
		depthOptions.bReadOnlyStencil = attachment.bReadOnlyStencil;
		Result<void> result = recordNative(
			[&]
			{
				return backend::RecordPreparedRendering(m_State->m_Backend.State, command->Native, framebuffer.Native,
					{ attachment.ColorLoads, attachment.ColorCount }, colors, depthOptions);
			});
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}

		command->ColorTarget = framebuffer.ColorCount != 0 ? framebuffer.Colors[0] : TextureHandle{};
		command->ColorCount = framebuffer.ColorCount;
		for (uint32 color = 0; color < framebuffer.ColorCount; ++color)
		{
			command->ColorTargets[color] = framebuffer.Colors[color];
		}
		command->DepthTarget = framebuffer.Depth;
		command->bReadOnlyDepth = attachment.bReadOnlyDepth;
		command->bReadOnlyStencil = attachment.bReadOnlyStencil;
		command->ActiveFramebuffer = attachment.Framebuffer;
		command->bRendering = true;
		command->ViewportCount = 0;
		command->ScissorCount = 0;
		preparedList(command)->ActiveAttachment = ref.Slot;
		++phase->Used.RenderPasses;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::endPreparedRendering(CommandSlot& command)
	{
		PreparedRecordingList* list = preparedList(&command);
		ASSERT(list != nullptr && command.bRendering, "Prepared rendering end requires an active prepared pass.");
		if (list->FailureCode != EErrorCode::None)
		{
			return preparedFailure(list->FailureCode);
		}
		ASSERT(preparedPhase(&command) != nullptr, "Prepared rendering end retains its active access phase.");

		Result<void> result = recordNative(
			[&]
			{
				return backend::RecordEndFramebuffer(m_State->m_Backend.State, command.Native,
					m_State->m_Framebuffers[command.ActiveFramebuffer.Slot].Native);
			});
		if (!result)
		{
			return preparedEmission(command, Move(result));
		}
		command.bRendering = false;
		command.ColorCount = 0;
		command.ColorTarget = {};
		command.DepthTarget = {};
		command.bReadOnlyDepth = false;
		command.bReadOnlyStencil = false;
		command.ActiveFramebuffer = {};

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::setPreparedViewport(CommandSlot& command, const ViewportDesc& viewport)
	{
		PreparedRecordingPhase* phase = preparedPhase(&command);
		ASSERT(!(phase == nullptr || !command.bRendering), "Prepared recording requires valid admitted input and capacity.");

		rhi::FramebufferSlot const& target = m_State->m_Framebuffers[command.ActiveFramebuffer.Slot];
		ASSERT(!(!IsFinite(viewport.X) ||
				!IsFinite(viewport.Y) ||
				!IsFinite(viewport.Width) ||
				!IsFinite(viewport.Height) ||
				!IsFinite(viewport.MinDepth) ||
				!IsFinite(viewport.MaxDepth) ||
				viewport.X < 0 ||
				viewport.Y < 0 ||
				viewport.Width <= 0 ||
				viewport.Height == 0 ||
				static_cast<double>(viewport.X) + viewport.Width> target.Width ||
				viewport.Y> target.Height ||
				static_cast<double>(viewport.Y) + viewport.Height < 0 ||
				static_cast<double>(viewport.Y) + viewport.Height> target.Height ||
				viewport.MinDepth < 0 ||
				viewport.MaxDepth> 1 ||
				viewport.MinDepth > viewport.MaxDepth),
			"Prepared recording requires valid admitted input and capacity.");

		if (viewport.Height < 0 && m_State->m_Kind != EBackend::Vulkan)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}

		ASSERT(!(phase->Used.ViewportChanges >= phase->Capacity.ViewportChanges), "Prepared recording requires valid admitted input and capacity.");

		Result<void> result =
			recordNative([&] { return backend::RecordSetViewport(m_State->m_Backend.State, command.Native, { &viewport, 1 }); });
		if (!result)
		{
			return preparedEmission(command, Move(result));
		}
		command.ViewportCount = 1;
		++phase->Used.ViewportChanges;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::setPreparedScissor(CommandSlot& command, const ScissorDesc& scissor)
	{
		PreparedRecordingPhase* phase = preparedPhase(&command);
		ASSERT(!(phase == nullptr || !command.bRendering), "Prepared recording requires valid admitted input and capacity.");

		rhi::FramebufferSlot const& target = m_State->m_Framebuffers[command.ActiveFramebuffer.Slot];
		ASSERT(!(scissor.Width == 0 ||
				scissor.Height == 0 ||
				scissor.X >= target.Width ||
				scissor.Y >= target.Height ||
				scissor.Width > target.Width - scissor.X ||
				scissor.Height > target.Height - scissor.Y),
			"Prepared recording requires valid admitted input and capacity.");

		ASSERT(!(phase->Used.ScissorChanges >= phase->Capacity.ScissorChanges), "Prepared recording requires valid admitted input and capacity.");

		Result<void> result =
			recordNative([&] { return backend::RecordSetScissor(m_State->m_Backend.State, command.Native, { &scissor, 1 }); });
		if (!result)
		{
			return preparedEmission(command, Move(result));
		}
		command.ScissorCount = 1;
		++phase->Used.ScissorChanges;

		return Result<void>::Success();
	}

	Result<void> Device::EndRendering(CommandListHandle list)
	{
		CallScope call(*this, true, true);
		CommandSlot* const command = findCommand(list);
		if (command != nullptr && command->PreparedGroup)
		{
			return endPreparedRendering(*command);
		}

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(command->bRendering, "EndRendering requires an active rendering pass.");
		ASSERT(command->ActiveFramebuffer.IsValid(), "An active render pass retains its framebuffer.");
		Result<void> recorded = backend::RecordEndFramebuffer( m_State->m_Backend.State, command->Native, m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].Native);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}
		command->ColorTarget = {};
		command->ColorCount = 0;
		command->DepthTarget = {};
		command->ActiveFramebuffer = {};
		command->bRendering = false;
		command->bReadOnlyDepth = false;
		command->bReadOnlyStencil = false;

		return Result<void>::Success();
	}
}
