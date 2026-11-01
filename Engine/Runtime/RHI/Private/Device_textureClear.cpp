#include "DeviceState.h"
namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::ClearDepthTexture(CommandListHandle list, TextureHandle texture, float depth)

	{
		CallScope call(*this, true);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(slot->Descriptor.MipLevels == 1 && slot->Descriptor.ArrayLayers == 1, "Use a subresource clear for a texture with multiple mips or layers.");

		return ClearDepthTextureSubresource(list, texture, {}, depth);
	}

	[[nodiscard]] Result<void> Device::ClearDepthTextureSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, float depth)
	{
		CallScope call(*this, true);
		return ClearDepthStencilTextureSubresource(list, texture, subresource, { true, depth, false, 0 });
	}

	[[nodiscard]] Result<void> Device::ClearDepthStencilTexture(CommandListHandle list, TextureHandle texture, const DepthStencilClear& clear)
	{
		CallScope call(*this, true);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(slot->Descriptor.MipLevels == 1 && slot->Descriptor.ArrayLayers == 1, "Use a subresource clear for a texture with multiple mips or layers.");

		return ClearDepthStencilTextureSubresource(list, texture, {}, clear);
	}

	[[nodiscard]] Result<void> Device::ClearDepthStencilTextureSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, const DepthStencilClear& clear)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");
		ASSERT(!(command->Queue != EQueue::Graphics ||
				!HasUsage(slot->Descriptor.Usage, ETextureUsage::DepthStencil) ||
				!IsDepthFormat(slot->Descriptor.Format) ||
				(!clear.bClearDepth && !clear.bClearStencil) ||
				(subresource.Aspect != ETextureAspect::Default &&
				 !(subresource.Aspect == ETextureAspect::Depth && !clear.bClearStencil) &&
				 !(subresource.Aspect == ETextureAspect::Stencil && !clear.bClearDepth)) ||
				(clear.bClearStencil && !GetFormatInfo(slot->Descriptor.Format).bHasStencil) ||
				subresource.MipLevel >= slot->Descriptor.MipLevels ||
				subresource.ArrayLayer >= slot->Descriptor.ArrayLayers ||
				(clear.bClearDepth && !(clear.Depth >= 0.0f && clear.Depth <= 1.0f))),
			"A graphics list, depth target, and depth in [0, 1] are required.");

		ASSERT(!command->bRendering, "Clear commands cannot be recorded inside a rendering pass.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the depth target.");
		}

		if (!command->TextureUses.Add({ texture, slot->Native, false, m_State->m_Kind == EBackend::Vulkan ? ETrackedResourceState::CopyDestination : ETrackedResourceState::DepthWrite }))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "The device could not track the depth clear.");
		}

		Result<void> recorded = backend::RecordDepthClear(m_State->m_Backend.State, command->Native, slot->Native, slot->Descriptor, subresource, clear);

		if (!recorded)
		{
			poisonCommandList(list);
			return recorded;
		}
		slot->RecordingSlot = list.Slot;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::ClearTextureUInt(CommandListHandle list, TextureHandle texture, uint32 value)

	{
		CallScope call(*this, true);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->Descriptor.MipLevels == 1 && slot->Descriptor.ArrayLayers == 1, "Use a subresource clear for a texture with multiple mips or layers.");

		return ClearTextureUIntSubresource(list, texture, {}, value);
	}

	[[nodiscard]] Result<void> Device::ClearTextureUIntSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, uint32 value)
	{
		CallScope call(*this, true);
		return clearIntegerTextureSubresource(list, texture, subresource, value, false);
	}

	[[nodiscard]] Result<void> Device::ClearTextureInt(CommandListHandle list, TextureHandle texture, int32 value)
	{
		CallScope call(*this, true);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(slot->Descriptor.MipLevels == 1 && slot->Descriptor.ArrayLayers == 1, "Use a subresource clear for a texture with multiple mips or layers.");

		return ClearTextureIntSubresource(list, texture, {}, value);
	}

	[[nodiscard]] Result<void> Device::ClearTextureIntSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, int32 value)
	{
		CallScope call(*this, true);
		return clearIntegerTextureSubresource(list, texture, subresource, static_cast<uint32>(value), true);
	}

	[[nodiscard]] Result<void> Device::clearIntegerTextureSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, uint32 value, bool bSigned)
	{
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");
		ASSERT(!command->bRendering, "Clear commands cannot be recorded inside a rendering pass.");

		const TextureDesc& desc = slot->Descriptor;
		const FormatInfo& format = GetFormatInfo(desc.Format);
		const uint32 channels = static_cast<uint32>(format.bHasRed) + format.bHasGreen + format.bHasBlue + format.bHasAlpha;
		const uint32 bytesPerChannel = channels == 0 ? 0 : format.BytesPerBlock / channels;
		const int32 signedValue = static_cast<int32>(value);

		ASSERT(!((!HasUsage(desc.Usage, ETextureUsage::Storage) && !HasUsage(desc.Usage, ETextureUsage::RenderTarget)) ||
				!IsIntegerStorageFormat(desc.Format) ||
				format.bIsSigned != bSigned ||
				(bSigned ? ((bytesPerChannel == 1 && (signedValue < -128 || signedValue > 127)) ||
				(bytesPerChannel == 2 && (signedValue < -32768 || signedValue > 32767)))
				: ((bytesPerChannel == 1 && value > 255) || (bytesPerChannel == 2 && value > 65535))) ||
				(subresource.Aspect != ETextureAspect::Default && subresource.Aspect != ETextureAspect::Color) ||
				subresource.MipLevel >= desc.MipLevels ||
				subresource.ArrayLayer >= desc.ArrayLayers),
			"The integer storage texture, value, or subresource is invalid.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the texture.");
		}

		if (!command->TextureUses.Add({ texture, slot->Native, false, (m_State->m_Kind == EBackend::Vulkan || !HasUsage(desc.Usage, ETextureUsage::Storage)) ? ETrackedResourceState::CopyDestination : ETrackedResourceState::Storage }))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "The device could not track the integer texture clear.");
		}

		const ClearColor typed = bSigned ? ClearColor::Sint(signedValue, signedValue, signedValue, signedValue) : ClearColor::Uint(value, value, value, value);
		Result<void> recorded =
			desc.SampleCount > 1
			? backend::RecordTextureClear(m_State->m_Backend.State, command->Native, slot->Native, desc, subresource, typed)
			: backend::RecordTextureUIntClear(m_State->m_Backend.State, command->Native, slot->Native, desc, subresource, value);

		if (!recorded)
		{
			poisonCommandList(list);
			return recorded;
		}
		slot->RecordingSlot = list.Slot;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::ClearColorTexture(CommandListHandle list, TextureHandle texture, const ClearColor& color)
	{
		CallScope call(*this, true);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(slot->Descriptor.MipLevels == 1 && slot->Descriptor.ArrayLayers == 1, "Use a subresource clear for a texture with multiple mips or layers.");

		return ClearColorTextureSubresource(list, texture, {}, color);
	}

	[[nodiscard]] Result<void> Device::ClearColorTextureSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, const ClearColor& color)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		if (slot->bSwapChainImage && slot->bSubmitted)
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.SwapChainImage", "The acquired image was already submitted.");
		}

		ASSERT(!command->bRendering, "Clear commands cannot be recorded inside a rendering pass.");

		ASSERT(!((!HasUsage(slot->Descriptor.Usage, ETextureUsage::RenderTarget) && !HasUsage(slot->Descriptor.Usage, ETextureUsage::Storage)) ||
				!IsColorTargetFormat(slot->Descriptor.Format) ||
				!ValidAttachmentClear(slot->Descriptor.Format, color) ||
				(subresource.Aspect != ETextureAspect::Default && subresource.Aspect != ETextureAspect::Color) ||
				subresource.MipLevel >= slot->Descriptor.MipLevels ||
				subresource.ArrayLayer >= slot->Descriptor.ArrayLayers),
			"The texture, clear value, or subresource is invalid.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the texture.");
		}

		if (!command->TextureUses.Add({ texture, slot->Native, false,
									   m_State->m_Kind == EBackend::Vulkan ? ETrackedResourceState::CopyDestination
									   : HasUsage(slot->Descriptor.Usage, ETextureUsage::RenderTarget) ? ETrackedResourceState::RenderTarget
																									   : ETrackedResourceState::Storage }))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "The device could not track the render target clear.");
		}

		Result<void> recorded = backend::RecordTextureClear(m_State->m_Backend.State, command->Native, slot->Native, slot->Descriptor, subresource, color);

		if (!recorded)
		{
			poisonCommandList(list);
			return recorded;
		}
		slot->RecordingSlot = list.Slot;

		return Result<void>::Success();
	}
}
