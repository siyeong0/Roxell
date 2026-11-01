#include "DeviceState.h"
#include <Assertion.h>
#include <Vocabulary.hpp>
namespace rox::rhi
{
	using namespace core;

#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	thread_local bool g_bFailNextNativePresent = false;

	void FailNextNativePresentForTesting() noexcept
	{
		g_bFailNextNativePresent = true;
	}
#endif
	[[nodiscard]] Result<SwapChainHandle> Device::CreateSwapChain(const SwapChainDesc& desc)
	{
		CallScope call(*this, false);

		ASSERT(desc.WindowKind <= ENativeWindowKind::Android && desc.Window != 0 && desc.Width != 0 && desc.Height != 0 && desc.BufferCount >= 2 && desc.BufferCount <= 3, "Swapchain creation requires a valid window, nonzero extent, and two or three buffers.");
		ASSERT(desc.Format > EFormat::UNKNOWN && desc.Format < EFormat::COUNT && desc.ColorSpace <= EColorSpace::Hdr10PqRec2020, "Swapchain creation requires valid format and color-space enumerations.");
		ASSERT((desc.ColorSpace != EColorSpace::LinearSrgb || desc.Format == EFormat::RGBA16_FLOAT) &&
			(desc.ColorSpace != EColorSpace::Hdr10PqRec2020 || desc.Format == EFormat::R10G10B10A2_UNORM) &&
			(!desc.bEnableHdrMetadata || desc.ColorSpace == EColorSpace::Hdr10PqRec2020),
			"Swapchain format, color space, and HDR metadata must be compatible.");
		if (desc.Width > 16384 || desc.Height > 16384)
		{
			return Result<SwapChainHandle>::Failure(EErrorCode::Unsupported, "Rhi.SwapChainDesc", "The swapchain extent exceeds the supported limit.");
		}

		if (desc.Format != EFormat::RGBA8_UNORM && desc.Format != EFormat::BGRA8_UNORM && desc.Format != EFormat::R10G10B10A2_UNORM && desc.Format != EFormat::RGBA16_FLOAT)
		{
			return Result<SwapChainHandle>::Failure(EErrorCode::Unsupported, "Rhi.SwapChainDesc", "The swapchain format is unsupported.");
		}

		if (desc.bEnableHdrMetadata)
		{
			const HdrMetadata& metadata = desc.Metadata;
			const float* primaries[]{ metadata.RedPrimary, metadata.GreenPrimary, metadata.BluePrimary, metadata.WhitePoint };
			for (const float* xy : primaries)
			{
				ASSERT(xy[0] >= 0.0f && xy[0] <= 1.0f && xy[1] >= 0.0f && xy[1] <= 1.0f, "HDR chromaticity must contain finite normalized coordinates.");
			}

			ASSERT(metadata.MaximumLuminance > 0.0f &&
				metadata.MaximumLuminance <= 10000.0f &&
				metadata.MinimumLuminance >= 0.0f &&
				metadata.MinimumLuminance <= metadata.MaximumLuminance &&
				metadata.MaximumContentLightLevel >= 0.0f &&
				metadata.MaximumContentLightLevel <= 65535.0f &&
				metadata.MaximumFrameAverageLightLevel >= 0.0f &&
				metadata.MaximumFrameAverageLightLevel <= 65535.0f,
				"HDR luminance metadata must be finite and within its valid range.");
		}

		Result<void*> created = backend::CreateSwapChain(m_State->m_Backend.State, desc);

		if (!created)
		{
			return Result<SwapChainHandle>::Failure(created.GetError());
		}

		const bool bNewSlot = m_State->m_FreeSwapChain == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_SwapChains.Size()) : m_State->m_FreeSwapChain;

		if (bNewSlot && (m_State->m_SwapChains.Size() >= NumericLimits<uint32>::max() || !m_State->m_SwapChains.Add(SwapChainSlot{})))
		{
			backend::DestroySwapChainNow(m_State->m_Backend.State, created.Value());
			return Result<SwapChainHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a swapchain handle slot.");
		}
		SwapChainSlot& slot = m_State->m_SwapChains[index];

		if (!bNewSlot)
		{
			m_State->m_FreeSwapChain = slot.NextFree;
		}
		slot.Native = created.Value();
		slot.Descriptor = desc;
		slot.AcquiredImage = {};
		slot.FailedAcquisitionImage = {};
		slot.OwnerGeneration = 1;
		slot.AcquireGeneration = 0;
		slot.NextFree = NumericLimits<uint32>::max();
		slot.bLive = true;

		return Result<SwapChainHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::ResizeSwapChain(SwapChainHandle handle, uint32 width, uint32 height)
	{
		return resizeSwapChainWithPolicy(handle, width, height, false);
	}

	[[nodiscard]] Result<void> Device::TryResizeSwapChain(SwapChainHandle handle, uint32 width, uint32 height)
	{
		return resizeSwapChainWithPolicy(handle, width, height, true);
	}

	[[nodiscard]] Result<void> Device::resizeSwapChainWithPolicy(SwapChainHandle handle, uint32 width, uint32 height, bool bPoll)
	{
		CallScope call(*this, false);
		SwapChainSlot* const slot = findSwapChain(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->AcquiredImage.IsValid())
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.SwapChainImage", "Present the acquired image before resizing.");
		}

		ASSERT(width != 0 && height != 0 && width <= 16384 && height <= 16384, "The swapchain extent is invalid.");

		collectAcquiredImageUses(false);
		if (hasRetainedAcquiredImages(handle))
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.SwapChainImage", "Retire all prior acquired image owners before changing the swapchain.");
		}
		if (hasAnyNativeQueueAccess())
		{
			return Result<void>::Failure(nativeQueueAccessConflict());
		}
		if (slot->OwnerGeneration == UINT64_MAX)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.SwapChainImage", "The swapchain generation is exhausted.");
		}
		Result<void> resized = bPoll ? backend::TryResizeSwapChain(m_State->m_Backend.State, slot->Native, width, height) : backend::ResizeSwapChain(m_State->m_Backend.State, slot->Native, width, height);

		if (!resized)
		{
			return resized;
		}
		++slot->OwnerGeneration;
		slot->Descriptor.Width = width;
		slot->Descriptor.Height = height;

		return Result<void>::Success();
	}

	[[nodiscard]] PresentOutcome Device::CopyAndPresent(SwapChainHandle handle, TextureHandle source)
	{
		CallScope call(*this, false);
		TextureSlot* const sourceSlot = findTexture(source);
		ASSERT(findSwapChain(handle) != nullptr && sourceSlot != nullptr, "Copy presentation requires valid handles from this device.");
		if (m_State->m_bSubmissionFaulted)
		{
			return { EPresentStatus::Failed, {} };
		}
		if (hasNativeQueueAccess(EQueue::Graphics))
		{
			return { EPresentStatus::Busy, {} };
		}
		if (sourceSlot->PreparedReferences != 0 || sourceSlot->bSparseMappingPrepared || sourceSlot->bSparseMappingUnavailable)
		{
			return { EPresentStatus::Busy, {} };
		}
		Result<SubmissionToken> result = copyAndPresentInternal(handle, source);
		if (result)
		{
			return { EPresentStatus::Presented, result.Value() };
		}
		if (result.GetError().DiagnosticID == "Rhi.RecreateRequired")
		{
			const uint64 serial = backend::HAS_GET_PRESENT_COPY_SERIAL ? backend::GetPresentCopySerial(m_State->m_Backend.State) : 0;
			if (serial != 0)
			{
				releasePresentRetention(false);
			}
			return { EPresentStatus::RecreateRequired, serial != 0 ? MakeSubmissionToken(m_State->m_Owner, EQueue::Graphics, serial) : SubmissionToken{} };
		}
		recordSubmissionFault(result.GetError());
		return { EPresentStatus::Failed, {} };
	}

	[[nodiscard]] Result < SubmissionToken > Device::copyAndPresentInternal(SwapChainHandle handle, TextureHandle source)
	{
		ROX_PROFILE_SCOPE("RHI", "Present");
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot present after submission failure.");
		}
		SwapChainSlot* const swapChain = findSwapChain(handle);
		TextureSlot* const texture = findTexture(source);

		ASSERT(!(swapChain == nullptr || texture == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!swapChain->AcquiredImage.IsValid(), "Close the current acquisition before copy presentation.");

		const TextureDesc& desc = texture->Descriptor;

		ASSERT(desc.Width == swapChain->Descriptor.Width &&
			desc.Height == swapChain->Descriptor.Height &&
			AreTextureViewFormatsCompatible(desc.Format, swapChain->Descriptor.Format) &&
			desc.SampleCount == 1 &&
			desc.MipLevels == 1 &&
			desc.ArrayLayers == 1 &&
			HasUsage(desc.Usage, ETextureUsage::CopySource),
			"Copy presentation requires a compatible single-sample source.");

		ASSERT(texture->RecordingSlot == NumericLimits<uint32>::max(), "Submit or discard recording references before copy presentation.");

		ASSERT(texture->PermanentState == ETrackedResourceState::Unknown || texture->PermanentState == ETrackedResourceState::CopySource, "The permanent texture state must permit copy presentation.");

		if (hasNativeQueueAccess(EQueue::Graphics))
		{
			return Result<SubmissionToken>::Failure(nativeQueueAccessConflict());
		}
		ASSERT(!m_State->m_RetainedPresentSource.IsValid(), "Only one terminal presentation retains an uncertain source.");
		++texture->PreparedReferences;
		Result<uint64> presented = backend::Present(m_State->m_Backend.State, swapChain->Native, texture->Native);

		if (!presented)
		{
			const uint64 serial = backend::HAS_GET_PRESENT_COPY_SERIAL ? backend::GetPresentCopySerial(m_State->m_Backend.State) : 0;
			if (serial != 0 || (backend::HAS_WAS_SUBMISSION_ACCEPTED && backend::WasSubmissionAccepted(m_State->m_Backend.State)))
			{
				m_State->m_RetainedPresentSource = source;
				if (serial != 0)
				{
					m_State->m_RetainedPresentToken = MakeSubmissionToken(m_State->m_Owner, EQueue::Graphics, serial);
				}
			}
			else
			{
				--texture->PreparedReferences;
			}
			return Result<SubmissionToken>::Failure(presented.GetError());
		}
		--texture->PreparedReferences;

		return Result<SubmissionToken>::Success(MakeSubmissionToken(m_State->m_Owner, EQueue::Graphics, presented.Value()));
	}

	[[nodiscard]] Result<void> Device::DestroySwapChain(SwapChainHandle handle)
	{
		return destroySwapChainWithPolicy(handle, false);
	}

	[[nodiscard]] Result<void> Device::TryDestroySwapChain(SwapChainHandle handle)
	{
		return destroySwapChainWithPolicy(handle, true);
	}

	[[nodiscard]] Result<void> Device::destroySwapChainWithPolicy(SwapChainHandle handle, bool bPoll)
	{
		CallScope call(*this, false);
		SwapChainSlot* const slot = findSwapChain(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->AcquiredImage.IsValid())
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.SwapChainImage", "Present the acquired image before destroying the swapchain.");
		}

		collectAcquiredImageUses(false);
		if (hasRetainedAcquiredImages(handle))
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.SwapChainImage", "Retire all prior acquired image owners before changing the swapchain.");
		}
		if (hasAnyNativeQueueAccess())
		{
			return Result<void>::Failure(nativeQueueAccessConflict());
		}
		Result<void> destroyed = bPoll ? backend::TryDestroySwapChain(m_State->m_Backend.State, slot->Native) : backend::DestroySwapChain(m_State->m_Backend.State, slot->Native);

		if (!destroyed)
		{
			return destroyed;
		}
		slot->Native = nullptr;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeSwapChain;
			m_State->m_FreeSwapChain = handle.Slot;
		}

		return Result<void>::Success();
	}

	void Device::recycleClosedAcquiredImages() noexcept
	{
		for (uint32 index = 0; index < m_State->m_Textures.Size(); ++index)
		{
			rhi::TextureSlot& slot = m_State->m_Textures[index];
			if (!slot.bLive ||
				!slot.bSwapChainImage ||
				!slot.bAcquisitionClosed ||
				slot.PreparedReferences ||
				slot.AcquiredAcceptedUses ||
				slot.FramebufferRefs ||
				slot.BindingSetRefs ||
				slot.ViewRefs ||
				slot.SharedReadUses ||
				slot.RecordingSlot != UINT32_MAX)
			{
				continue;
			}
			slot.Native = nullptr;
			slot.SwapChainOwner = {};
			slot.bSwapChainImage = false;
			slot.bSubmitted = false;
			slot.bLive = false;
			Advance(slot);
			if (!slot.bExhausted)
			{
				slot.NextFree = m_State->m_FreeTexture;
				m_State->m_FreeTexture = index;
			}
		}
	}

	void Device::collectAcquiredImageUses(bool bOwnedIdle) noexcept
	{
		for (usize index = m_State->m_AcquiredImageUses.Size(); index != 0;)
		{
			--index;
			rhi::AcquiredImageUse const& use = m_State->m_AcquiredImageUses[index];
			bool bComplete = bOwnedIdle;
			if (!bComplete && use.Token.IsValid())
			{
				Result<bool> complete = backend::IsQueueComplete(m_State->m_Backend.State, use.Token.Queue, use.Token.Value >> 2);
				bComplete = complete && complete.Value();
			}
			if (!bComplete)
			{
				continue;
			}
			rhi::TextureSlot& image = m_State->m_Textures[use.Image.Slot];
			ASSERT(image.Generation == use.Image.Generation && image.AcquiredAcceptedUses != 0, "Accepted acquisition owners retain the exact unrecycled native slot.");
			--image.AcquiredAcceptedUses;
			m_State->m_AcquiredImageUses.RemoveAtSwap(index);
		}
		recycleClosedAcquiredImages();
	}

	[[nodiscard]] bool Device::hasRetainedAcquiredImages(SwapChainHandle owner) const noexcept
	{
		for (rhi::TextureSlot const& slot : m_State->m_Textures)
		{
			if (slot.bLive && slot.bSwapChainImage && slot.SwapChainOwner == owner)
			{
				return true;
			}
		}

		return false;
	}

	void Device::retireSwapChainImage(SwapChainSlot& swapChain) noexcept
	{
		if (!swapChain.AcquiredImage.IsValid())
		{
			return;
		}
		m_State->m_Textures[swapChain.AcquiredImage.Slot].bAcquisitionClosed = true;
		swapChain.AcquiredImage = {};
		recycleClosedAcquiredImages();
	}

	[[nodiscard]] Result<TextureDesc> Device::QueryAcquiredSwapChainImage(AcquiredSwapChainImage acquisition)

	{
		CallScope call(*this, false);
		SwapChainSlot* swapChain = findSwapChain(acquisition.SwapChain);
		TextureSlot* texture = findTexture(acquisition.Image);
		ASSERT(swapChain != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(acquisition.IsValid() &&
			texture != nullptr &&
			swapChain->AcquiredImage == acquisition.Image &&
			texture->bSwapChainImage &&
			texture->SwapChainOwner == acquisition.SwapChain &&
			swapChain->OwnerGeneration == acquisition.OwnerGeneration &&
			swapChain->AcquireGeneration == acquisition.AcquireGeneration,
			"The acquisition is stale or does not belong to this swapchain.");

		return Result<TextureDesc>::Success(texture->Descriptor);
	}

	[[nodiscard]] Result<void> Device::DiscardAcquiredSwapChainImage(AcquiredSwapChainImage acquisition)

	{
		CallScope call(*this, false);
		SwapChainSlot* swapChain = findSwapChain(acquisition.SwapChain);
		TextureSlot* texture = findTexture(acquisition.Image);
		ASSERT(swapChain != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(acquisition.IsValid() &&
			texture != nullptr &&
			swapChain->AcquiredImage == acquisition.Image &&
			texture->bSwapChainImage &&
			texture->SwapChainOwner == acquisition.SwapChain &&
			swapChain->OwnerGeneration == acquisition.OwnerGeneration &&
			swapChain->AcquireGeneration == acquisition.AcquireGeneration,
			"RHI operations require the exact current swapchain acquisition.");
		collectAcquiredImageUses(false);
		if (texture->PreparedReferences ||
			texture->AcquiredUnsubmittedUses ||
			texture->AcquiredAcceptedUses ||
			texture->FramebufferRefs ||
			texture->BindingSetRefs ||
			texture->ViewRefs ||
			texture->SharedReadUses ||
			texture->RecordingSlot != UINT32_MAX ||
			hasAnyNativeQueueAccess())
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.SwapChainImage", "Retire recorded and accepted image owners before abandoning the acquisition.");
		}
		if constexpr (backend::HAS_DISCARD_ACQUIRED_SWAP_CHAIN_IMAGE == false)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.SwapChainImage", "The backend cannot abandon an acquired image.");
		}
		Result<void> discarded = backend::DiscardAcquiredSwapChainImage(m_State->m_Backend.State, swapChain->Native);
		if (!discarded)
		{
			return discarded;
		}
		retireSwapChainImage(*swapChain);

		return Result<void>::Success();
	}

	[[nodiscard]] AcquireOutcome Device::AcquireSwapChainImage(SwapChainHandle handle, uint32 timeoutMilliseconds)
	{
		CallScope call(*this, false);
		ASSERT(findSwapChain(handle) != nullptr, "Acquisition requires a swapchain from this device.");
		Result<AcquiredSwapChainImage> result = acquireSwapChainImageInternal(handle, timeoutMilliseconds);
		if (result)
		{
			return { EAcquireStatus::Acquired, result.Value() };
		}
		if (result.GetError().Code == EErrorCode::Timeout)
		{
			return { EAcquireStatus::NotReady, {} };
		}
		if (result.GetError().DiagnosticID == "Rhi.RecreateRequired")
		{
			return { EAcquireStatus::RecreateRequired, {} };
		}
		recordSubmissionFault(result.GetError());
		return { EAcquireStatus::Failed, {} };
	}

	[[nodiscard]] Result<AcquiredSwapChainImage> Device::acquireSwapChainImageInternal(SwapChainHandle handle, uint32 timeoutMilliseconds)
	{
		ROX_PROFILE_SCOPE("RHI", "AcquireSwapChainImage");
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<AcquiredSwapChainImage>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot acquire images after submission failure.");
		}
		SwapChainSlot* const swapChain = findSwapChain(handle);

		ASSERT(swapChain != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(!swapChain->AcquiredImage.IsValid(), "A swapchain can own only one current acquisition.");

		if (swapChain->AcquireGeneration == UINT64_MAX)
		{
			return Result<AcquiredSwapChainImage>::Failure(EErrorCode::Conflict, "Rhi.SwapChainImage", "The acquisition generation is exhausted.");
		}
		collectAcquiredImageUses(false);
		const bool bNewSlot = m_State->m_FreeTexture == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Textures.Size()) : m_State->m_FreeTexture;

		if (bNewSlot && (m_State->m_Textures.Size() >= NumericLimits<uint32>::max() || !m_State->m_Textures.Add(TextureSlot{})))
		{
			return Result<AcquiredSwapChainImage>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a swapchain image handle.");
		}

		Result<void*> acquired = backend::AcquireSwapChainImage(m_State->m_Backend.State, swapChain->Native, timeoutMilliseconds);

		if (!acquired)
		{
			if (bNewSlot)
			{
				m_State->m_Textures[index].NextFree = m_State->m_FreeTexture;
				m_State->m_FreeTexture = index;
			}

			return Result<AcquiredSwapChainImage>::Failure(acquired.GetError());
		}
		TextureSlot& slot = m_State->m_Textures[index];

		if (!bNewSlot)
		{
			m_State->m_FreeTexture = slot.NextFree;
		}
		slot.Native = acquired.Value();
		slot.Descriptor = { swapChain->Descriptor.Width, swapChain->Descriptor.Height, swapChain->Descriptor.Format, ETextureUsage::RenderTarget };
		slot.ByteCount = static_cast<uint64>(swapChain->Descriptor.Width) * swapChain->Descriptor.Height * GetFormatInfo(swapChain->Descriptor.Format).BytesPerBlock;
		slot.RecordingSlot = NumericLimits<uint32>::max();
		slot.SharedReadUses = 0;
		slot.PreparedReferences = 0;
		slot.ViewRefs = 0;
		slot.AcquiredUnsubmittedUses = 0;
		slot.AcquiredAcceptedUses = 0;
		slot.bAcquisitionClosed = false;
		slot.bAcquisitionPrepared = false;
		slot.bAcquisitionReleaseAccepted = false;
		slot.bSharedReadPrepared = false;
		slot.FramebufferRefs = 0;
		slot.BindingSetRefs = 0;
		slot.SwapChainOwner = handle;
		slot.bSwapChainImage = true;
		slot.bSubmitted = false;
		slot.bLive = true;
		slot.NextFree = NumericLimits<uint32>::max();
		const TextureHandle image{ m_State->m_Owner, index, slot.Generation };
		++swapChain->AcquireGeneration;
		swapChain->AcquiredImage = image;
		return Result<AcquiredSwapChainImage>::Success({ handle, image, swapChain->OwnerGeneration, swapChain->AcquireGeneration });
	}

	[[nodiscard]] PresentOutcome Device::PresentSwapChainImage(AcquiredSwapChainImage acquisition)
	{
		CallScope call(*this, false);
		SwapChainSlot* const current = findSwapChain(acquisition.SwapChain);
		ASSERT(current != nullptr &&
			acquisition.IsValid() &&
			current->AcquiredImage == acquisition.Image &&
			current->OwnerGeneration == acquisition.OwnerGeneration &&
			current->AcquireGeneration == acquisition.AcquireGeneration,
			"Presentation requires the exact current acquisition.");

		TextureSlot* const image = findTexture(acquisition.Image);
		ASSERT(image != nullptr && image->bSwapChainImage && image->SwapChainOwner == acquisition.SwapChain, "Presentation requires the acquired image from this swapchain.");
		ASSERT(image->RecordingSlot == UINT32_MAX && !image->AcquiredUnsubmittedUses && (!image->bAcquisitionPrepared || image->bAcquisitionReleaseAccepted), "Submit or discard every acquired image use before presentation.");
		if (m_State->m_bSubmissionFaulted)
		{
			SwapChainSlot* const swapChain = current;
			ASSERT(!swapChain->FailedAcquisitionImage.IsValid(), "A swapchain retains one failed acquisition.");
			++image->PreparedReferences;
			swapChain->FailedAcquisitionImage = acquisition.Image;
			retireSwapChainImage(*swapChain);
			return { EPresentStatus::Failed, {} };
		}
		if (hasNativeQueueAccess(EQueue::Graphics))
		{
			return { EPresentStatus::Busy, {} };
		}
		Result<SubmissionToken> result = presentSwapChainImageInternal(acquisition);
		if (result)
		{
			return { EPresentStatus::Presented, result.Value() };
		}
		if (result.GetError().DiagnosticID == "Rhi.RecreateRequired")
		{
			const uint64 serial = backend::HAS_GET_PRESENT_COPY_SERIAL ? backend::GetPresentCopySerial(m_State->m_Backend.State) : 0;

			return{ EPresentStatus::RecreateRequired, serial != 0 ? MakeSubmissionToken(m_State->m_Owner, EQueue::Graphics, serial) : SubmissionToken{} };
		}
		recordSubmissionFault(result.GetError());
		return { EPresentStatus::Failed, {} };
	}

	[[nodiscard]] Result<SubmissionToken> Device::presentSwapChainImageInternal(AcquiredSwapChainImage acquisition)
	{
		ROX_PROFILE_SCOPE("RHI", "PresentSwapChainImage");
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot present after submission failure.");
		}
		SwapChainSlot* const swapChain = findSwapChain(acquisition.SwapChain);

		ASSERT(swapChain != nullptr, "RHI operations require valid handles for this device.");
		TextureSlot* const image = findTexture(acquisition.Image);

		ASSERT(acquisition.IsValid() &&
			image != nullptr &&
			image->bSwapChainImage &&
			image->SwapChainOwner == acquisition.SwapChain &&
			swapChain->AcquiredImage == acquisition.Image &&
			swapChain->OwnerGeneration == acquisition.OwnerGeneration &&
			swapChain->AcquireGeneration == acquisition.AcquireGeneration,
			"RHI operations require the exact current swapchain acquisition.");

		ASSERT(image->RecordingSlot == NumericLimits<uint32>::max() && !image->AcquiredUnsubmittedUses && (!image->bAcquisitionPrepared || image->bAcquisitionReleaseAccepted),
			"Submit or discard every acquired image use before presentation.");

		if (hasNativeQueueAccess(EQueue::Graphics))
		{
			return Result<SubmissionToken>::Failure(nativeQueueAccessConflict());
		}
		Result<uint64> presented = backend::PresentSwapChainImage(m_State->m_Backend.State, swapChain->Native);
		retireSwapChainImage(*swapChain);
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
		if (presented && g_bFailNextNativePresent)
		{
			g_bFailNextNativePresent = false;
			return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.TestPresent", "Injected failure after native presentation.");
		}
#endif
		if (!presented)
		{
			return Result<SubmissionToken>::Failure(presented.GetError());
		}

		return Result<SubmissionToken>::Success(MakeSubmissionToken(m_State->m_Owner, EQueue::Graphics, presented.Value()));
	}
}
