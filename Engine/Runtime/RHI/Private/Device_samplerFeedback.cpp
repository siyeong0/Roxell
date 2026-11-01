#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] TextureSlot* Device::findSamplerFeedback(SamplerFeedbackHandle handle) noexcept
	{
		TextureSlot* const slot = findTexture({ handle.Owner, handle.Slot, handle.Generation });

		return slot != nullptr && slot->bSamplerFeedback ? slot : nullptr;
	}

	[[nodiscard]] Result<SamplerFeedbackHandle> Device::CreateSamplerFeedbackTexture(TextureHandle pairedTexture, const SamplerFeedbackDesc& desc)
	{
		CallScope call(*this, false);

		return createSamplerFeedbackTextureInternal(pairedTexture, desc, nullptr);
	}

	[[nodiscard]] Result<SamplerFeedbackHandle> Device::ImportSamplerFeedbackTexture(TextureHandle pairedTexture, const SamplerFeedbackDesc& desc, void* nativeFeedbackTexture)
	{
		CallScope call(*this, false);

		ASSERT(!(nativeFeedbackTexture == nullptr), "The native sampler feedback texture is null.");

		return createSamplerFeedbackTextureInternal(pairedTexture, desc, nativeFeedbackTexture);
	}

	[[nodiscard]] Result<SamplerFeedbackHandle> Device::createSamplerFeedbackTextureInternal(TextureHandle pairedTexture, const SamplerFeedbackDesc& desc, void* nativeFeedbackTexture)
	{
		TextureSlot* const pair = findTexture(pairedTexture);

		ASSERT(!(pair == nullptr || pair->bSamplerFeedback), "RHI operations require valid handles for this device.");

		if ((nativeFeedbackTexture == nullptr && backend::HAS_CREATE_SAMPLER_FEEDBACK_TEXTURE == false) || (nativeFeedbackTexture != nullptr && backend::HAS_IMPORT_SAMPLER_FEEDBACK_TEXTURE == false))
		{
			return Result<SamplerFeedbackHandle>::Failure(EErrorCode::Unsupported, "Rhi.SamplerFeedback", "The backend does not support sampler feedback textures.");
		}

		const TextureDesc pairedDesc = pair->Descriptor;
		const auto validRegion = [](uint32 size, uint32 limit) noexcept
			{ return size >= 4 && size <= limit / 2 && (size & (size - 1)) == 0; };
		ASSERT(!(desc.Format > ESamplerFeedbackFormat::MipRegionUsed), "The feedback format is invalid.");

		ASSERT(!(pairedDesc.Dimension != ETextureDimension::Texture2D || pairedDesc.SampleCount != 1 || !HasUsage(pairedDesc.Usage, ETextureUsage::ShaderRead)),
			"The paired texture must be a shader-readable, single-sample 2D texture.");

		ASSERT(validRegion(desc.RegionWidth, pairedDesc.Width) && validRegion(desc.RegionHeight, pairedDesc.Height), "The feedback mip region is invalid for the paired texture.");

		if (pair->FeedbackRefs == NumericLimits<uint32>::max())
		{
			return Result<SamplerFeedbackHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.SamplerFeedback", "The paired texture has too many feedback references.");
		}

		const bool bNewSlot = m_State->m_FreeTexture == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Textures.Size()) : m_State->m_FreeTexture;

		if (bNewSlot && (m_State->m_Textures.Size() >= NumericLimits<uint32>::max() || !m_State->m_Textures.Add(TextureSlot{})))
		{
			return Result<SamplerFeedbackHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a sampler feedback handle slot.");
		}
		// Adding a texture slot can move the paired slot in the array.
		TextureSlot& currentPair = m_State->m_Textures[pairedTexture.Slot];
		Result<void*> created =
			nativeFeedbackTexture == nullptr
			? backend::CreateSamplerFeedbackTexture(m_State->m_Backend.State, currentPair.Native, pairedDesc, desc)
			: backend::ImportSamplerFeedbackTexture(m_State->m_Backend.State, currentPair.Native, pairedDesc, desc, nativeFeedbackTexture);

		if (!created)
		{
			if (bNewSlot)
			{
				m_State->m_Textures[index].NextFree = m_State->m_FreeTexture;
				m_State->m_FreeTexture = index;
			}

			return Result<SamplerFeedbackHandle>::Failure(created.GetError());
		}
		TextureSlot& slot = m_State->m_Textures[index];

		if (!bNewSlot)
		{
			m_State->m_FreeTexture = slot.NextFree;
		}

		slot.NextFree = NumericLimits<uint32>::max();
		slot.Native = created.Value();
		slot.Descriptor = pairedDesc;
		slot.Descriptor.Usage = ETextureUsage::None;
		slot.PairedTexture = pairedTexture;
		slot.FeedbackDescriptor = desc;
		slot.RecordingSlot = NumericLimits<uint32>::max();
		slot.SharedReadUses = 0;
		slot.bSharedReadPrepared = false;
		slot.bSamplerFeedback = true;
		slot.bLive = true;
		++currentPair.FeedbackRefs;

		return Result<SamplerFeedbackHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroySamplerFeedbackTexture(SamplerFeedbackHandle handle)
	{
		CallScope call(*this, false);

		ASSERT(!(findSamplerFeedback(handle) == nullptr), "RHI operations require valid handles for this device.");

		return DestroyTexture({ handle.Owner, handle.Slot, handle.Generation });
	}

	[[nodiscard]] Result<NativeResourceHandle> Device::GetNativeSamplerFeedbackTexture(SamplerFeedbackHandle handle)
	{
		CallScope call(*this, true);
		TextureSlot* const slot = findSamplerFeedback(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		return Result<NativeResourceHandle>::Success({ m_State->m_Kind, backend::GetNativeTexture(slot->Native) });
	}

	[[nodiscard]] Result<void> Device::ReferenceSamplerFeedbackTexture(CommandListHandle list, SamplerFeedbackHandle feedback)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findSamplerFeedback(feedback);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the feedback texture.");
		}

		const TextureHandle texture{ feedback.Owner, feedback.Slot, feedback.Generation };
		if (!command->TextureUses.Add({ texture, slot->Native }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain the feedback texture for native GPU work.");
		}
		slot->RecordingSlot = list.Slot;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::SetSamplerFeedbackTextureState(CommandListHandle list, SamplerFeedbackHandle feedback, ESamplerFeedbackState state)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);

		ASSERT(!(command == nullptr || findSamplerFeedback(feedback) == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(state <= ESamplerFeedbackState::CopyDestination &&
			!command->bRendering &&
			(command->Queue != EQueue::Copy || state == ESamplerFeedbackState::CopySource || state == ESamplerFeedbackState::CopyDestination),
			"The feedback state or command queue is invalid.");

		Result<void> referenced = ReferenceSamplerFeedbackTexture(list, feedback);

		if (!referenced)
		{
			return referenced;
		}

		Result<void> recorded = backend::RecordSetSamplerFeedbackState(m_State->m_Backend.State, command->Native, m_State->m_Textures[feedback.Slot].Native, state);

		if (!recorded)
		{
			poisonCommandList(list);
		}

		return recorded;
	}

	[[nodiscard]] Result<void> Device::ClearSamplerFeedbackTexture(CommandListHandle list, SamplerFeedbackHandle feedback)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findSamplerFeedback(feedback);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(command->Queue == EQueue::Graphics && !command->bRendering, "Feedback clear requires a graphics command list outside rendering.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the feedback texture.");
		}

		const TextureHandle texture{ feedback.Owner, feedback.Slot, feedback.Generation };
		if (!command->TextureUses.Add({ texture, slot->Native }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain the feedback texture for clear.");
		}

		slot->RecordingSlot = list.Slot;
		Result<void> recorded = backend::RecordSamplerFeedbackClear(m_State->m_Backend.State, command->Native, slot->Native);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::DecodeSamplerFeedbackTexture(CommandListHandle list, SamplerFeedbackHandle feedback, BufferHandle destination)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findSamplerFeedback(feedback);
		BufferSlot* const buffer = findBuffer(destination);

		ASSERT(!(command == nullptr || slot == nullptr || buffer == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(command->Queue == EQueue::Graphics && !command->bRendering, "Feedback decode requires a graphics command list outside rendering.");
		if (slot->FeedbackDescriptor.Format != ESamplerFeedbackFormat::MinMip || slot->Descriptor.ArrayLayers != 1)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.SamplerFeedback", "Buffer decode requires a single-layer MinMip map.");
		}

		const uint64 width = (slot->Descriptor.Width + slot->FeedbackDescriptor.RegionWidth - 1) / slot->FeedbackDescriptor.RegionWidth;
		const uint64 height = (slot->Descriptor.Height + slot->FeedbackDescriptor.RegionHeight - 1) / slot->FeedbackDescriptor.RegionHeight;

		ASSERT(buffer->Memory == EBufferMemory::Device && HasUsage(buffer->Usage, EBufferUsage::CopyDestination) && buffer->ByteCount >= width * height,
			"The decode buffer must be a large enough device copy destination.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the feedback texture.");
		}

		if (!command->TextureUses.Reserve(command->TextureUses.Size() + 1) || !command->BufferUses.Reserve(command->BufferUses.Size() + 1))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain sampler feedback decode resources.");
		}

		const TextureHandle texture{ feedback.Owner, feedback.Slot, feedback.Generation };
		(void)command->TextureUses.Add({ texture, slot->Native });
		(void)command->AddBufferUse({ destination, buffer->Native, ETrackedResourceState::CopyDestination });
		++buffer->RecordedUses;
		slot->RecordingSlot = list.Slot;
		Result<void> recorded = backend::RecordSamplerFeedbackDecode(m_State->m_Backend.State, command->Native, slot->Native, buffer->Native);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::DecodeSamplerFeedbackTexture(CommandListHandle list, SamplerFeedbackHandle feedback, TextureHandle destination)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findSamplerFeedback(feedback);
		TextureSlot* const target = findTexture(destination);

		ASSERT(!(command == nullptr || slot == nullptr || target == nullptr || target->bSamplerFeedback), "RHI operations require valid handles for this device.");

		ASSERT(command->Queue == EQueue::Graphics && !command->bRendering, "Feedback decode requires a graphics command list outside rendering.");

		const TextureDesc& desc = target->Descriptor;
		const TextureDesc& paired = slot->Descriptor;
		const uint32 mipCount = slot->FeedbackDescriptor.Format == ESamplerFeedbackFormat::MinMip ? 1 : paired.MipLevels;
		bool bDimensionsValid = true;

		for (uint32 mip = 0; mip < mipCount; ++mip)
		{
			const uint32 pairedWidth = paired.Width >> mip ? paired.Width >> mip : 1;
			const uint32 pairedHeight = paired.Height >> mip ? paired.Height >> mip : 1;
			const uint32 width = (pairedWidth + slot->FeedbackDescriptor.RegionWidth - 1) / slot->FeedbackDescriptor.RegionWidth;
			const uint32 height = (pairedHeight + slot->FeedbackDescriptor.RegionHeight - 1) / slot->FeedbackDescriptor.RegionHeight;

			if ((desc.Width >> mip ? desc.Width >> mip : 1) < width || (desc.Height >> mip ? desc.Height >> mip : 1) < height)
			{
				bDimensionsValid = false;
				break;
			}
		}

		ASSERT(desc.Format == EFormat::R8_UINT &&
			HasUsage(desc.Usage, ETextureUsage::CopyDestination) &&
			desc.Dimension == ETextureDimension::Texture2D &&
			desc.ArrayLayers == paired.ArrayLayers &&
			desc.SampleCount == 1 &&
			desc.MipLevels == mipCount &&
			bDimensionsValid &&
			destination != slot->PairedTexture,
			"The decoded texture must be an R8_UINT copy destination with matching mips and region dimensions.");

		if ((slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot) ||
			(target->RecordingSlot != NumericLimits<uint32>::max() && target->RecordingSlot != list.Slot))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references a feedback decode texture.");
		}

		if (!command->TextureUses.Reserve(command->TextureUses.Size() + 2))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain sampler feedback decode textures.");
		}

		const TextureHandle source{ feedback.Owner, feedback.Slot, feedback.Generation };
		(void)command->TextureUses.Add({ source, slot->Native });
		(void)command->TextureUses.Add({ destination, target->Native, false, ETrackedResourceState::CopyDestination });
		slot->RecordingSlot = list.Slot;
		target->RecordingSlot = list.Slot;
		Result<void> recorded = backend::RecordSamplerFeedbackDecodeTexture(m_State->m_Backend.State, command->Native, slot->Native, target->Native);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}
}
