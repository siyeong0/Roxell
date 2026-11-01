#include "DeviceState.h"
namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<uint64> Device::validateTextureDesc(const TextureDesc& desc) const
	{
		if (desc.Width == 0 ||
			desc.Height == 0 ||
			desc.Depth == 0 ||
			desc.MipLevels == 0 ||
			desc.ArrayLayers == 0 ||
			(desc.SampleCount != 1 && desc.SampleCount != 2 && desc.SampleCount != 4 && desc.SampleCount != 8) ||
			desc.Format == EFormat::UNKNOWN ||
			desc.Format >= EFormat::COUNT ||
			desc.Usage == ETextureUsage::None ||
			desc.Dimension > ETextureDimension::Texture1D ||
			(desc.Dimension != ETextureDimension::Texture3D && desc.Depth != 1) ||
			(desc.Dimension == ETextureDimension::Texture1D && desc.Height != 1) ||
			(desc.Dimension == ETextureDimension::Texture3D && desc.ArrayLayers != 1) ||
			(desc.Dimension == ETextureDimension::Cube && (desc.Width != desc.Height || desc.ArrayLayers % 6 != 0)) ||
			(static_cast<uint32>(desc.Usage) & ~KNOWN_TEXTURE_USAGE_BITS) != 0)
		{
			return Result<uint64>::Failure(EErrorCode::InvalidArgument, "Rhi.TextureDesc", "The texture descriptor has an invalid extent, format, or usage.");
		}

		if (desc.Depth > m_State->m_Backend.Capabilities.Limits.MaximumTexture3DDimension ||
			desc.MipLevels > m_State->m_Backend.Capabilities.Limits.MaximumTextureMipLevels ||
			desc.ArrayLayers > m_State->m_Backend.Capabilities.Limits.MaximumTextureArrayLayers)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.TextureExtent", "The texture exceeds the device limits.");
		}

		if ((static_cast<uint32>(desc.Usage) & ~TEXTURE_CLEAR_BITS) != 0)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.TextureUsage", "The requested texture use is not implemented.");
		}

		if (desc.bSparse &&
			((m_State->m_Kind != EBackend::D3D12 && m_State->m_Kind != EBackend::Vulkan) || !m_State->m_Backend.Capabilities.Features.bSparseTextures || desc.Dimension != ETextureDimension::Texture2D || desc.SampleCount != 1))
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.SparseTexture", "Sparse textures require D3D12 or Vulkan and a single-sample 2D texture.");
		}

		if (m_State->m_Kind == EBackend::Null &&
			(desc.SampleCount != 1 || HasUsage(desc.Usage, ETextureUsage::ShaderRead) || HasUsage(desc.Usage, ETextureUsage::Storage) || HasUsage(desc.Usage, ETextureUsage::DepthStencil)))
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.TextureUsage", "The Null backend does not support this texture usage or sample count.");
		}

		const FormatInfo& info = GetFormatInfo(desc.Format);
		const bool bDepth = HasUsage(desc.Usage, ETextureUsage::DepthStencil);

		if (HasUsage(desc.Usage, ETextureUsage::ShadingRateSource) &&
			(!m_State->m_Backend.Capabilities.Features.bShadingRateImage ||
			 desc.Dimension != ETextureDimension::Texture2D ||
			 desc.Format != EFormat::R8_UINT ||
			 desc.MipLevels != 1 ||
			 desc.ArrayLayers != 1 ||
			 desc.SampleCount != 1 ||
			 (static_cast<uint32>(desc.Usage) & ~(TEXTURE_COPY_BITS | static_cast<uint32>(ETextureUsage::ShadingRateSource))) != 0))
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.ShadingRateImage", "The device or texture descriptor does not support a shading rate image.");
		}

		if (desc.Dimension == ETextureDimension::Texture1D && (desc.Height != 1 || desc.SampleCount != 1 || info.BlockSize != 1))
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.Texture1D", "One-dimensional textures require height one, one sample, and an uncompressed format.");
		}

		if (desc.Dimension == ETextureDimension::Texture3D &&
			(desc.ArrayLayers != 1 ||
			 desc.SampleCount != 1 ||
			 info.BlockSize != 1 ||
			 (m_State->m_Kind == EBackend::Null && HasUsage(desc.Usage, ETextureUsage::RenderTarget)) ||
			 (static_cast<uint32>(desc.Usage) & ~(TEXTURE_COPY_BITS | static_cast<uint32>(ETextureUsage::ShaderRead) | static_cast<uint32>(ETextureUsage::Storage) | static_cast<uint32>(ETextureUsage::RenderTarget))) != 0))
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.Texture3D", "Three-dimensional textures require uncompressed copy, shader, or native render target usage.");
		}

		if (desc.Dimension == ETextureDimension::Cube &&
			(desc.Width != desc.Height || desc.ArrayLayers % 6 != 0 || desc.SampleCount != 1 || (HasUsage(desc.Usage, ETextureUsage::Storage) && desc.ArrayLayers != 6)))
		{
			return Result<uint64>::Failure(
				EErrorCode::Unsupported,
				"Rhi.TextureCube",
				"Cube textures require square, single-sample faces in groups of six; storage requires exactly six faces.");
		}

		if (desc.SampleCount != 1 &&
			(desc.MipLevels != 1 ||
					!HasUsage(desc.Usage, bDepth ? ETextureUsage::DepthStencil : ETextureUsage::RenderTarget) ||
					(static_cast<uint32>(desc.Usage) & ~(bDepth ? static_cast<uint32>(ETextureUsage::DepthStencil) | TEXTURE_COPY_BITS | static_cast<uint32>(ETextureUsage::ShaderRead) : static_cast<uint32>(ETextureUsage::RenderTarget) | static_cast<uint32>(ETextureUsage::CopySource) | static_cast<uint32>(ETextureUsage::CopyDestination) | static_cast<uint32>(ETextureUsage::ShaderRead))) != 0))
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.Multisample", "Multisampled textures require one mip level and attachment usage.");
		}

		if (bDepth &&
			(!IsDepthFormat(desc.Format) ||
			 (static_cast<uint32>(desc.Usage) & ~(TEXTURE_COPY_BITS | static_cast<uint32>(ETextureUsage::DepthStencil) | static_cast<uint32>(ETextureUsage::ShaderRead))) != 0))
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.DepthTexture", "Depth textures require a depth format and depth attachment or sampling usage.");
		}

		if (HasUsage(desc.Usage, ETextureUsage::RenderTarget) && (!IsColorTargetFormat(desc.Format) || (m_State->m_Kind == EBackend::Null && desc.Format != EFormat::RGBA8_UNORM)))
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.RenderTargetFormat", "This render target format is unavailable.");
		}

		if (HasUsage(desc.Usage, ETextureUsage::Storage) && (!(IsStorageColorFormat(desc.Format) || IsIntegerStorageFormat(desc.Format)) || bDepth))
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.StorageTexture", "Storage textures require a supported color format.");
		}

		if (info.BlockSize == 0 || (info.Kind == EFormatKind::DepthStencil && !bDepth) || info.BytesPerBlock == 0)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.TextureFormat", "This texture format is unavailable for copy operations.");
		}

		uint32 maximumMips = 1;

		for (uint32 extent = desc.Width > desc.Height ? (desc.Width > desc.Depth ? desc.Width : desc.Depth)
			: (desc.Height > desc.Depth ? desc.Height : desc.Depth);
			extent > 1; extent >>= 1)
		{
			++maximumMips;
		}

		if (desc.MipLevels > maximumMips)
		{
			return Result<uint64>::Failure(EErrorCode::InvalidArgument, "Rhi.TextureDesc", "The mip count exceeds the full mip chain.");
		}

		const uint32 maximumDimension = desc.Dimension == ETextureDimension::Texture1D ? m_State->m_Backend.Capabilities.Limits.MaximumTexture1DDimension
			: desc.Dimension == ETextureDimension::Texture3D ? m_State->m_Backend.Capabilities.Limits.MaximumTexture3DDimension
			: m_State->m_Backend.Capabilities.Limits.MaximumTexture2DDimension;

		if (desc.Width > maximumDimension || desc.Height > maximumDimension)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.TextureExtent", "The texture exceeds the device dimension limit.");
		}

		const uint64 maximumBytes = desc.bSparse ? m_State->m_Backend.Capabilities.Limits.MaximumSparseTextureByteCount : m_State->m_Backend.Capabilities.Limits.MaximumTextureByteCount;
		uint64 totalBytes = 0;

		for (uint32 level = 0; level < desc.MipLevels; ++level)
		{
			const uint64 blocksWide = (static_cast<uint64>(MipExtent(desc.Width, level)) + info.BlockSize - 1) / info.BlockSize;
			const uint64 blocksHigh = (static_cast<uint64>(MipExtent(desc.Height, level)) + info.BlockSize - 1) / info.BlockSize;
			const uint64 blocks = blocksWide * blocksHigh;
			const uint64 depthSlices = MipExtent(desc.Depth, level);

			if (blocks > maximumBytes / info.BytesPerBlock / desc.ArrayLayers / desc.SampleCount / depthSlices ||
				totalBytes > maximumBytes - blocks * info.BytesPerBlock * desc.ArrayLayers * desc.SampleCount * depthSlices)
			{
				return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.TextureSize", "The texture exceeds the supported byte count.");
			}
			totalBytes += blocks * info.BytesPerBlock * desc.ArrayLayers * desc.SampleCount * depthSlices;
		}

		const EFormatSupport support = queryNativeFormatSupport(desc.Format);

		if (m_State->m_Kind != EBackend::Null &&
			(!HasFormatSupport(support, EFormatSupport::Texture) ||
			 (HasUsage(desc.Usage, ETextureUsage::ShaderRead) && !HasFormatSupport(support, EFormatSupport::ShaderLoad)) ||
			 (HasUsage(desc.Usage, ETextureUsage::Storage) && !HasFormatSupport(support, EFormatSupport::ShaderUavStore)) ||
			 (HasUsage(desc.Usage, ETextureUsage::RenderTarget) && !HasFormatSupport(support, EFormatSupport::RenderTarget)) ||
			 (bDepth && !HasFormatSupport(support, EFormatSupport::DepthStencil))))
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.TextureFormat", "The device does not support this texture format.");
		}

		return Result<uint64>::Success(totalBytes);
	}

	[[nodiscard]] Result<TextureHandle> Device::CreateTexture(const TextureDesc& desc)
	{
		CallScope call(*this, false);
		return createTextureInternal(desc, {}, 0, 0);
	}

	[[nodiscard]] Result<TextureHandle> Device::ImportTexture(const TextureDesc& desc, void* nativeTexture, ENativeTextureContents contents)

	{
		CallScope call(*this, false);
		ASSERT(nativeTexture != nullptr && contents <= ENativeTextureContents::Preserve && !desc.bSparse, "The native texture handle and contents mode must be valid.");
		return createTextureInternal(desc, {}, 0, 0, nativeTexture, contents);
	}

	[[nodiscard]] Result<NativeResourceHandle> Device::GetNativeTexture(TextureHandle handle)
	{
		CallScope call(*this, true);
		TextureSlot* const slot = findTexture(handle);
		if (slot != nullptr && slot->PreparedReferences != 0)
		{
			Error error;
			error.Code = EErrorCode::Conflict;

			return Result<NativeResourceHandle>::Failure(Move(error));
		}

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (m_State->m_Kind == EBackend::Null)
		{
			return Result<NativeResourceHandle>::Failure(EErrorCode::Unsupported, "Rhi.NativeTexture", "The Null backend has no native textures.");
		}

		return Result<NativeResourceHandle>::Success({ m_State->m_Kind, backend::GetNativeTexture(slot->Native) });
	}

	[[nodiscard]] Result<TextureTiling> Device::GetTextureTiling(TextureHandle handle, Span<TextureSubresourceTiling> subresources)
	{
		CallScope call(*this, true);
		TextureSlot* const slot = findTexture(handle);

		ASSERT(!(slot == nullptr || slot->bSamplerFeedback), "RHI operations require valid handles for this device.");

		if (!slot->Descriptor.bSparse)
		{
			return Result<TextureTiling>::Failure(EErrorCode::Unsupported, "Rhi.TextureTiling", "Texture tiling is available only for sparse textures.");
		}

		if constexpr (backend::HAS_GET_TEXTURE_TILING == false)
		{
			return Result<TextureTiling>::Failure(EErrorCode::Unsupported, "Rhi.TextureTiling", "The backend does not expose texture tiling.");
		}

		const uint32 count = slot->Descriptor.MipLevels * slot->Descriptor.ArrayLayers;

		ASSERT(!(subresources.Size() != 0 && (subresources.Data() == nullptr || subresources.Size() < count)), "The output span must cover every texture subresource.");

		return backend::GetTextureTiling(m_State->m_Backend.State, slot->Native, subresources);
	}

	[[nodiscard]] Result<MemoryRequirements> Device::GetTextureMemoryRequirements(const TextureDesc& desc) const
	{
		CallScope call(*this, true);
		Result<uint64> valid = validateTextureDesc(desc);

		if (!valid)
		{
			ASSERT(valid.GetError().Code != EErrorCode::InvalidArgument, "A valid texture descriptor is required.");
			return Result<MemoryRequirements>::Failure(valid.GetError());
		}

		if (desc.bSparse)
		{
			return Result<MemoryRequirements>::Failure(EErrorCode::Unsupported, "Rhi.SparseTexture", "Sparse textures have no committed allocation requirements.");
		}

		for (uint32 index = 0; index < m_State->m_TextureRequirementsCount; ++index)
		{
			const TextureRequirementsEntry& cached = m_State->m_TextureRequirements[index];
			const TextureDesc& prior = cached.Description;
			if (sameTextureDescriptor(prior, desc))
			{
				return Result<MemoryRequirements>::Success(cached.Requirements);
			}
		}
		Result<MemoryRequirements> requirements = backend::GetTextureMemoryRequirements(m_State->m_Backend.State, desc);

		if (!requirements)
		{
			return requirements;
		}

		if (requirements.Value().ByteCount == 0 || requirements.Value().AlignmentBytes == 0)
		{
			return Result<MemoryRequirements>::Failure(EErrorCode::ExternalFailure, "Rhi.MemoryRequirements", "The backend returned invalid texture memory requirements.");
		}

		const uint32 index = m_State->m_TextureRequirementsCount < 128 ? m_State->m_TextureRequirementsCount++ : m_State->m_TextureRequirementsCursor++ % 128;
		m_State->m_TextureRequirements[index] = { desc, requirements.Value() };

		return requirements;
	}

	[[nodiscard]] Result<TextureHandle> Device::CreatePlacedTexture(const TextureDesc& desc, HeapHandle heap, uint64 offset)
	{
		CallScope call(*this, false);
		Result<uint64> valid = validateTextureDesc(desc);

		if (!valid)
		{
			ASSERT(valid.GetError().Code != EErrorCode::InvalidArgument, "A valid texture descriptor is required.");
			return Result<TextureHandle>::Failure(valid.GetError());
		}
		HeapSlot* const owner = getHeap(heap);

		const EHeapUsage required = HasUsage(desc.Usage, ETextureUsage::RenderTarget) || HasUsage(desc.Usage, ETextureUsage::DepthStencil) ? EHeapUsage::Attachments : EHeapUsage::Textures;

		ASSERT(!(owner->Descriptor.Usage != required), "The heap resource class does not match the texture.");

		Result<MemoryRequirements> requirements = GetTextureMemoryRequirements(desc);

		if (!requirements)
		{
			return Result<TextureHandle>::Failure(requirements.GetError());
		}

		if (requirements.Value().bDedicatedRequired)
		{
			return Result<TextureHandle>::Failure(EErrorCode::Unsupported, "Rhi.DedicatedAllocation", "The texture requires a dedicated allocation and cannot be placed in a heap.");
		}

		const uint64 size = requirements.Value().ByteCount;
		const uint64 alignment = requirements.Value().AlignmentBytes;

		ASSERT(!(offset % alignment != 0 || offset > owner->Descriptor.CapacityBytes || size > owner->Descriptor.CapacityBytes - offset),
			"The texture placement is misaligned or outside heap capacity.");

		if (owner->LiveTextures == NumericLimits<uint32>::max())
		{
			return Result<TextureHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Heap", "The heap texture count is exhausted.");
		}

		Result<void> collected = collectRetiredRanges();

		if (!collected)
		{
			return Result<TextureHandle>::Failure(collected.GetError());
		}

		for (const TextureSlot& other : m_State->m_Textures)
		{
			if (!other.bLive || other.Heap != heap)
			{
				continue;
			}

			if (offset < other.HeapOffset + other.AllocationSize && other.HeapOffset < offset + size)
			{
				return Result<TextureHandle>::Failure(EErrorCode::Conflict, "Rhi.Heap", "A live texture already occupies this heap range.");
			}
		}

		for (const RetiredHeapRange& other : m_State->m_RetiredRanges)
		{
			if (other.Heap == heap && offset < other.Offset + other.Size && other.Offset < offset + size)
			{
				return Result<TextureHandle>::Failure(EErrorCode::Conflict, "Rhi.Heap", "GPU work still occupies this heap range.");
			}
		}

		return createTextureInternal(desc, heap, offset, size);
	}

	[[nodiscard]] Result<TextureHandle> Device::createTextureInternal(
		const TextureDesc& desc,
		HeapHandle heap,
		uint64 offset,
		uint64 allocationSize,
		void* nativeTexture,
		ENativeTextureContents contents,
		void* adoptedTexture)
	{
		Result<uint64> validated = validateTextureDesc(desc);

		if (!validated)
		{
			ASSERT(adoptedTexture != nullptr || validated.GetError().Code != EErrorCode::InvalidArgument, "A valid texture descriptor is required.");
			return Result<TextureHandle>::Failure(validated.GetError());
		}

		const uint64 totalBytes = validated.Value();
		const bool bNewSlot = m_State->m_FreeTexture == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Textures.Size()) : m_State->m_FreeTexture;

		if (bNewSlot && (m_State->m_Textures.Size() >= NumericLimits<uint32>::max() || !m_State->m_Textures.Add(TextureSlot{})))
		{
			return Result<TextureHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "The device could not allocate a texture handle slot.");
		}

		Result<void*> created =
			adoptedTexture != nullptr ? Result<void*>::Success(adoptedTexture)
			: nativeTexture != nullptr ? backend::ImportTexture(m_State->m_Backend.State, desc, nativeTexture, contents)
			: heap.IsValid() ? backend::CreatePlacedTexture(m_State->m_Backend.State, desc, m_State->m_Heaps[heap.Slot].Native, offset)
			: backend::CreateTexture(m_State->m_Backend.State, desc);

		if (!created)
		{
			if (bNewSlot)
			{
				m_State->m_Textures[index].NextFree = m_State->m_FreeTexture;
				m_State->m_FreeTexture = index;
			}

			return Result<TextureHandle>::Failure(created.GetError());
		}
		TextureSlot& slot = m_State->m_Textures[index];

		if (!bNewSlot)
		{
			m_State->m_FreeTexture = slot.NextFree;
		}
		slot.NextFree = NumericLimits<uint32>::max();
		slot.Native = created.Value();
		slot.Descriptor = desc;
		slot.PairedTexture = {};
		slot.FeedbackDescriptor = {};
		slot.bSamplerFeedback = false;
		slot.ByteCount = totalBytes;
		slot.Heap = heap;
		slot.HeapOffset = offset;
		slot.AllocationSize = allocationSize;
		slot.RecordingSlot = NumericLimits<uint32>::max();
		slot.SharedReadUses = 0;
		slot.PreparedReferences = 0;
		slot.bSharedReadPrepared = false;
		slot.SwapChainOwner = {};
		slot.bSwapChainImage = false;
		slot.bAcquisitionClosed = false;
		slot.bAcquisitionPrepared = false;
		slot.bAcquisitionReleaseAccepted = false;
		slot.bSubmitted = false;
		slot.PermanentState = ETrackedResourceState::Unknown;
		slot.bExternalOwned = false;
		slot.bExternalShareable = false;
		slot.bLive = true;

		if (heap.IsValid())
		{
			++m_State->m_Heaps[heap.Slot].LiveTextures;
		}

		return Result<TextureHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyTexture(TextureHandle handle)
	{
		CallScope call(*this, false);
		collectPreparedTextureMappings(false);
		TextureSlot* const slot = findTexture(handle, true);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->bSwapChainImage)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.SwapChainImage", "The acquired image is owned by its swapchain.");
		}

		if (slot->RecordingSlot != NumericLimits<uint32>::max() ||
			slot->PreparedReferences != uncertainTextureMappingPins(handle) ||
			slot->FramebufferRefs != 0 ||
			slot->BindingSetRefs != 0 ||
			slot->ViewRefs != 0 ||
			slot->FeedbackRefs != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "A recording command list still references the texture.");
		}
		if (slot->bSparseMappingUnavailable)
		{
			if (hasAnyNativeQueueAccess())
			{
				return Result<void>::Failure(nativeQueueAccessConflict());
			}
			Result<void> idle = backend::WaitIdle(m_State->m_Backend.State);
			if (!idle && idle.GetError().DiagnosticID != "Rhi.DeviceLost")
			{
				return idle;
			}
		}
		bool bRetired = false;

		if (slot->Heap.IsValid())
		{
			const uint64 serial = backend::GetTextureLastUse(slot->Native);

			if (serial != 0)
			{
				bRetired = m_State->m_RetiredRanges.Add({ slot->Heap, slot->HeapOffset, slot->AllocationSize, serial });
				if (!bRetired)
				{
					if (hasAnyNativeQueueAccess())
					{
						return Result<void>::Failure(nativeQueueAccessConflict());
					}
					Result<void> idle = backend::WaitIdle(m_State->m_Backend.State);

					if (!idle)
					{
						return idle;
					}
				}
			}
		}

		Result<void> destroyed = backend::DestroyTexture(m_State->m_Backend.State, slot->Native);

		if (!destroyed)
		{
			if (bRetired)
			{
				m_State->m_RetiredRanges.PopBack();
			}

			return destroyed;
		}

		releaseUncertainTextureMappings(handle);
		if (slot->Heap.IsValid())
		{
			--m_State->m_Heaps[slot->Heap.Slot].LiveTextures;
		}

		if (slot->Descriptor.bSparse)
		{
			(void)m_State->m_SparseTiles.EraseIf(
				[&](const auto& entry) noexcept
				{
					if (entry.GetKey().Texture != handle)
					{
						return false;
					}
					--m_State->m_Heaps[entry.Value.Slot].SparseTileRefs;

					return true;
				});
		}

		if (slot->bSamplerFeedback)
		{
			--m_State->m_Textures[slot->PairedTexture.Slot].FeedbackRefs;
		}
		slot->Native = nullptr;
		slot->PermanentState = ETrackedResourceState::Unknown;
		slot->PairedTexture = {};
		slot->bSamplerFeedback = false;
		slot->Heap = {};
		slot->HeapOffset = 0;
		slot->AllocationSize = 0;
		slot->bSparseMappingPrepared = false;
		slot->bSparseMappingUnavailable = false;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeTexture;
			m_State->m_FreeTexture = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::WriteTexture(CommandListHandle list, TextureHandle destination, TextureSubresource subresource, Span<const Byte> data)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findTexture(destination);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		if (m_State->m_bSubmissionFaulted)
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot record after submission failure.");
		}

		ASSERT(!command->bRendering && (!IsDepthFormat(slot->Descriptor.Format) || command->Queue == EQueue::Graphics), "Texture writes require a compatible queue outside rendering.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the texture.");
		}

		ASSERT(!(slot->Descriptor.SampleCount != 1 ||
				!HasUsage(slot->Descriptor.Usage, ETextureUsage::CopyDestination) ||
				subresource.MipLevel >= slot->Descriptor.MipLevels ||
				subresource.ArrayLayer >= slot->Descriptor.ArrayLayers ||
				GetTextureTransferFormat(slot->Descriptor.Format, subresource.Aspect) == EFormat::UNKNOWN ||
				data.Data() == nullptr ||
				data.Size() != SubresourceBytes(slot->Descriptor, subresource.MipLevel, subresource.Aspect) ||
				!ValidTextureUploadData(slot->Descriptor.Format, subresource.Aspect, data)),
			"Texture write usage, subresource and byte count must be valid.");

		if (!command->TextureUses.Add({ destination, slot->Native, false, ETrackedResourceState::CopyDestination }))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track the texture write.");
		}

		Result<void> recorded = backend::RecordTextureWrite(m_State->m_Backend.State, command->Native, slot->Native, slot->Descriptor, subresource, data);

		if (!recorded)
		{
			poisonCommandList(list);
			return recorded;
		}
		slot->RecordingSlot = list.Slot;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<TextureViewHandle> Device::CreateTextureView(const TextureViewDesc& desc)
	{
		CallScope call(*this, false);
		TextureSlot* const texture = findTexture(desc.Texture);

		ASSERT(texture != nullptr, "RHI operations require valid handles for this device.");

		const TextureDesc& source = texture->Descriptor;
		const EFormat viewFormat = desc.Format == EFormat::UNKNOWN ? source.Format : desc.Format;

		ASSERT(!(!AreTextureViewFormatsCompatible(source.Format, viewFormat) || (viewFormat != source.Format && !source.bMutableFormat)), "The view format is incompatible or the texture is not mutable.");

		const FormatInfo& formatInfo = GetFormatInfo(viewFormat);
		ASSERT(!(desc.Aspect > ETextureAspect::Stencil ||
				(desc.Aspect == ETextureAspect::Color && (formatInfo.bHasDepth || formatInfo.bHasStencil)) ||
				(desc.Aspect == ETextureAspect::Depth && !formatInfo.bHasDepth) ||
				(desc.Aspect == ETextureAspect::Stencil && !formatInfo.bHasStencil)),
			"The selected view aspect does not match its format.");
		if ((formatInfo.bHasDepth || formatInfo.bHasStencil) && desc.Type == ETextureViewType::Storage)
		{
			return Result<TextureViewHandle>::Failure(EErrorCode::Unsupported, "Rhi.TextureViewAspect", "Depth/stencil storage views are unsupported.");
		}

		const EFormatSupport support = queryNativeFormatSupport(viewFormat);

		if (!HasFormatSupport(support, desc.Type == ETextureViewType::Storage ? EFormatSupport::ShaderUavStore : EFormatSupport::ShaderLoad))
		{
			return Result<TextureViewHandle>::Failure(EErrorCode::Unsupported, "Rhi.TextureViewFormat", "The view format does not support its shader access.");
		}

		uint32 mipDepth = desc.BaseMipLevel < 32 ? source.Depth >> desc.BaseMipLevel : 0;

		if (mipDepth == 0)
		{
			mipDepth = 1;
		}

		ASSERT(!(texture->bSwapChainImage ||
				desc.Type > ETextureViewType::Storage ||
				desc.Dimension > ETextureViewDimension::Texture1DArray ||
				desc.MipLevelCount == 0 ||
				desc.ArrayLayerCount == 0 ||
				desc.BaseMipLevel >= source.MipLevels ||
				desc.MipLevelCount > source.MipLevels - desc.BaseMipLevel ||
				desc.BaseArrayLayer >= source.ArrayLayers ||
				desc.ArrayLayerCount > source.ArrayLayers - desc.BaseArrayLayer ||
				(source.SampleCount != 1 &&
						(desc.Type != ETextureViewType::Sampled ||
								(desc.Dimension != ETextureViewDimension::Texture2D && desc.Dimension != ETextureViewDimension::Texture2DArray) ||
								desc.BaseMipLevel != 0 ||
								desc.MipLevelCount != 1)) ||
				(desc.Dimension == ETextureViewDimension::Texture2D && (source.ArrayLayers != 1 || desc.BaseArrayLayer != 0 || desc.ArrayLayerCount != 1)) ||
				((source.Dimension == ETextureDimension::Texture1D) != (desc.Dimension == ETextureViewDimension::Texture1D || desc.Dimension == ETextureViewDimension::Texture1DArray)) ||
				(desc.Dimension == ETextureViewDimension::Texture1D && (source.ArrayLayers != 1 || desc.BaseArrayLayer != 0 || desc.ArrayLayerCount != 1)) ||
				((source.Dimension == ETextureDimension::Texture3D) != (desc.Dimension == ETextureViewDimension::Texture3D)) ||
				(desc.Dimension != ETextureViewDimension::Texture3D && (desc.BaseDepthSlice != 0 || desc.DepthSliceCount != 0)) ||
				(desc.Dimension == ETextureViewDimension::Texture3D &&
						(desc.Type == ETextureViewType::Sampled
					? (desc.BaseDepthSlice != 0 || desc.DepthSliceCount != 0)
					: (desc.BaseDepthSlice >= mipDepth ||
						(desc.DepthSliceCount != 0 && desc.DepthSliceCount > mipDepth - desc.BaseDepthSlice)))) ||
				((desc.Dimension == ETextureViewDimension::Cube || desc.Dimension == ETextureViewDimension::CubeArray) &&
						(source.Dimension != ETextureDimension::Cube ||
								desc.BaseArrayLayer % 6 != 0 ||
								desc.ArrayLayerCount % 6 != 0 ||
								(desc.Dimension == ETextureViewDimension::Cube && (source.ArrayLayers != 6 || desc.BaseArrayLayer != 0 || desc.ArrayLayerCount != 6)))) ||
				(desc.Type == ETextureViewType::Storage && (desc.MipLevelCount != 1 || desc.Dimension == ETextureViewDimension::Cube || desc.Dimension == ETextureViewDimension::CubeArray)) ||
				!HasUsage(source.Usage, desc.Type == ETextureViewType::Sampled ? ETextureUsage::ShaderRead : ETextureUsage::Storage)),
			"The view range, dimension, or texture usage is invalid.");

		if (texture->ViewRefs == NumericLimits<uint32>::max())
		{
			return Result<TextureViewHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.TextureView", "The texture has too many views.");
		}

		Result<void*> created = backend::CreateTextureView(m_State->m_Backend.State, texture->Native, source, desc);

		if (!created)
		{
			return Result<TextureViewHandle>::Failure(created.GetError());
		}

		const bool bNewSlot = m_State->m_FreeTextureView == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_TextureViews.Size()) : m_State->m_FreeTextureView;

		if (bNewSlot && (m_State->m_TextureViews.Size() >= NumericLimits<uint32>::max() || !m_State->m_TextureViews.Add(TextureViewSlot{})))
		{
			backend::DestroyTextureView(m_State->m_Backend.State, created.Value());

			return Result<TextureViewHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a texture view slot.");
		}
		TextureViewSlot& slot = m_State->m_TextureViews[index];

		if (!bNewSlot)
		{
			m_State->m_FreeTextureView = slot.NextFree;
		}
		slot.Native = created.Value();
		slot.Descriptor = desc;
		slot.LastUse = 0;
		slot.RecordedUses = 0;
		slot.BindingSetRefs = 0;
		slot.NextFree = NumericLimits<uint32>::max();
		slot.bLive = true;
		++texture->ViewRefs;

		return Result<TextureViewHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyTextureView(TextureViewHandle handle)
	{
		CallScope call(*this, false);
		TextureViewSlot* const slot = findTextureView(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->BindingSetRefs != 0 || slot->RecordedUses != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.TextureView", "A binding set or recorded command still uses the view.");
		}

		if (slot->LastUse != 0)
		{
			Result<bool> complete = backend::IsComplete(m_State->m_Backend.State, slot->LastUse);

			if (!complete)
			{
				return Result<void>::Failure(complete.GetError());
			}

			if (!complete.Value())
			{
				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.TextureView", "The GPU has not completed use of the view.");
			}
		}

		backend::DestroyTextureView(m_State->m_Backend.State, slot->Native);
		--m_State->m_Textures[slot->Descriptor.Texture.Slot].ViewRefs;
		slot->Native = nullptr;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeTextureView;
			m_State->m_FreeTextureView = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] TextureViewSlot* Device::findTextureView(TextureViewHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_TextureViews.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		TextureViewSlot& slot = m_State->m_TextureViews[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	EFormatSupport Device::queryNativeFormatSupport(EFormat format) const noexcept
	{
		ASSERT(format < EFormat::COUNT, "Native format queries require a validated enumeration.");
		EFormatSupport& cached = m_State->m_NativeFormatSupport[static_cast<usize>(format)];
		// None can also mean a failed native query; retry that case instead of caching it.
		if (cached == EFormatSupport::None)
		{
			cached = backend::QueryFormatSupport(m_State->m_Backend.State, format);
		}

		return cached;
	}

	Result<uint32> Device::queryNativeTextureSampleCounts(const TextureDesc& description) const
	{
		for (uint32 index = 0; index < m_State->m_TextureSamplesCount; ++index)
		{
			if (sameTextureDescriptor(m_State->m_TextureSamples[index].Description, description))
			{
				return Result<uint32>::Success(m_State->m_TextureSamples[index].Samples);
			}
		}
		Result<uint32> result = backend::QueryTextureSampleCounts(m_State->m_Backend.State, description);
		if (result && result.Value() != 0)
		{
			const uint32 index = m_State->m_TextureSamplesCount < 128 ? m_State->m_TextureSamplesCount++ : m_State->m_TextureSamplesCursor++ % 128;
			m_State->m_TextureSamples[index] = { description, result.Value() };
		}

		return result;
	}

	bool Device::sameTextureDescriptor(const TextureDesc& left, const TextureDesc& right) noexcept
	{
		return left.Width == right.Width &&
			left.Height == right.Height &&
			left.Depth == right.Depth &&
			left.Format == right.Format &&
			left.Usage == right.Usage &&
			left.MipLevels == right.MipLevels &&
			left.ArrayLayers == right.ArrayLayers &&
			left.SampleCount == right.SampleCount &&
			left.Dimension == right.Dimension &&
			left.bSparse == right.bSparse &&
			left.bMutableFormat == right.bMutableFormat;
	}
}
