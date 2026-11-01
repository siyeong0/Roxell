#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::ResolveTexture(CommandListHandle handle, PreparedResolveRef ref)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(phase != nullptr && ref.List == handle && ref.Phase == preparedList(command)->ActivePhase && ref.Slot < phase->Resolves.Size() && !command->bRendering,
			"Prepared texture operations require a matching selector outside rendering.");
		ASSERT(phase->Used.TextureCopies < phase->Capacity.TextureCopies, "Prepared texture operations stay within their admitted capacity.");

		Result<void> result = recordNative([&] { return backend::RecordTextureResolve(m_State->m_Backend.State, command->Native, phase->Resolves[ref.Slot].Native); });
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}
		++phase->Used.TextureCopies;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::CopyBufferToTexture(CommandListHandle handle, PreparedTextureBufferCopyRef ref)
	{
		return copyPreparedTextureBuffer(handle, ref, ETextureBufferCopyDirection::BufferToTexture);
	}

	[[nodiscard]] Result<void> Device::CopyTextureToBuffer(CommandListHandle handle, PreparedTextureBufferCopyRef ref)
	{
		return copyPreparedTextureBuffer(handle, ref, ETextureBufferCopyDirection::TextureToBuffer);
	}

	[[nodiscard]] Result<void> Device::copyPreparedTexture(CommandSlot& command, const TextureCopy& copy)
	{
		PreparedRecordingPhase* phase = preparedPhase(&command);
		const TextureSlot* source = findTexture(copy.Source);
		const TextureSlot* destination = findTexture(copy.Destination);
		ASSERT(!(phase == nullptr || command.bRendering || source == nullptr || destination == nullptr || copy.Source == copy.Destination), "Prepared recording requires valid admitted input and capacity.");

		const TextureDesc& src = source->Descriptor;
		const TextureDesc& dst = destination->Descriptor;
		ASSERT(!(src.Width != dst.Width ||
				src.Height != dst.Height ||
				src.Depth != dst.Depth ||
				!AreTextureCopyFormatsCompatible(src.Format, dst.Format) ||
				src.Dimension != dst.Dimension ||
				src.SampleCount != dst.SampleCount ||
				src.MipLevels != dst.MipLevels ||
				src.ArrayLayers != dst.ArrayLayers ||
				!HasUsage(src.Usage, ETextureUsage::CopySource) ||
				!HasUsage(dst.Usage, ETextureUsage::CopyDestination) ||
				(IsDepthFormat(src.Format) && command.Queue != EQueue::Graphics)),
			"Prepared recording requires valid admitted input and capacity.");
		if (GetFormatInfo(src.Format).bHasStencil)
		{
			TextureViewDesc stencilExtent;
			stencilExtent.MipLevelCount = src.MipLevels;
			stencilExtent.ArrayLayerCount = src.ArrayLayers;
			stencilExtent.Aspect = ETextureAspect::Stencil;
			ASSERT(!(!preparedTextureContains(*phase, copy.Source, ETextureState::CopySource, &stencilExtent) ||
					!preparedTextureContains(*phase, copy.Destination, ETextureState::CopyDestination, &stencilExtent)),
				"Prepared recording requires valid admitted input and capacity.");
		}
		ASSERT(!(!preparedTextureContains(*phase, copy.Source, ETextureState::CopySource) || !preparedTextureContains(*phase, copy.Destination, ETextureState::CopyDestination)),
			"Prepared recording requires valid admitted input and capacity.");

		ASSERT(!(phase->Used.TextureCopies >= phase->Capacity.TextureCopies), "Prepared recording requires valid admitted input and capacity.");
		NativeTextureCopy native{ source->Native, destination->Native, src.Width, src.Height, src.MipLevels, src.ArrayLayers };
		native.Depth = src.Depth;
		Result<void> result = recordNative([&] { return backend::RecordTextureCopy(m_State->m_Backend.State, command.Native, native); });
		if (!result)
		{
			return preparedEmission(command, Move(result));
		}

		++phase->Used.TextureCopies;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::copyPreparedTextureRegion(CommandSlot& command, const TextureCopyRegion& copy)
	{
		PreparedRecordingPhase* phase = preparedPhase(&command);
		const TextureSlot* source = findTexture(copy.Source);
		const TextureSlot* destination = findTexture(copy.Destination);
		ASSERT(!(phase == nullptr || command.bRendering || source == nullptr || destination == nullptr), "Prepared recording requires valid admitted input and capacity.");

		const TextureDesc& src = source->Descriptor;
		const TextureDesc& dst = destination->Descriptor;
		if (!preparedTextureAspectSupported(src, copy.SourceSubresource.Aspect) || !preparedTextureAspectSupported(dst, copy.DestinationSubresource.Aspect))
		{
			return preparedFailure(EErrorCode::Unsupported);
		}

		ASSERT(!((IsDepthFormat(src.Format) && command.Queue != EQueue::Graphics) ||
				GetTextureTransferFormat(src.Format, copy.SourceSubresource.Aspect) == EFormat::UNKNOWN ||
				!AreTextureCopyFormatsCompatible(GetTextureTransferFormat(src.Format, copy.SourceSubresource.Aspect), GetTextureTransferFormat(dst.Format, copy.DestinationSubresource.Aspect)) ||
				copy.Source == copy.Destination ||
				!AreTextureCopyFormatsCompatible(src.Format, dst.Format) ||
				((src.Dimension == ETextureDimension::Texture3D) != (dst.Dimension == ETextureDimension::Texture3D)) ||
				src.SampleCount != dst.SampleCount ||
				copy.SourceSubresource.MipLevel >= src.MipLevels ||
				copy.SourceSubresource.ArrayLayer >= src.ArrayLayers ||
				copy.DestinationSubresource.MipLevel >= dst.MipLevels ||
				copy.DestinationSubresource.ArrayLayer >= dst.ArrayLayers ||
				!HasUsage(src.Usage, ETextureUsage::CopySource) ||
				!HasUsage(dst.Usage, ETextureUsage::CopyDestination)),
			"Prepared recording requires valid admitted input and capacity.");

		const uint32 sourceWidth = MipExtent(src.Width, copy.SourceSubresource.MipLevel);
		const uint32 sourceHeight = MipExtent(src.Height, copy.SourceSubresource.MipLevel);
		const uint32 destinationWidth = MipExtent(dst.Width, copy.DestinationSubresource.MipLevel);
		const uint32 destinationHeight = MipExtent(dst.Height, copy.DestinationSubresource.MipLevel);
		const uint32 sourceDepth = MipExtent(src.Depth, copy.SourceSubresource.MipLevel);
		const uint32 destinationDepth = MipExtent(dst.Depth, copy.DestinationSubresource.MipLevel);

		ASSERT(!(copy.Width == 0 ||
				copy.Height == 0 ||
				copy.Depth == 0 ||
				copy.SourceX > sourceWidth ||
				copy.SourceY > sourceHeight ||
				copy.SourceZ > sourceDepth ||
				copy.DestinationX > destinationWidth ||
				copy.DestinationY > destinationHeight ||
				copy.DestinationZ > destinationDepth ||
				copy.Width > sourceWidth - copy.SourceX ||
				copy.Height > sourceHeight - copy.SourceY ||
				copy.Depth > sourceDepth - copy.SourceZ ||
				copy.Width > destinationWidth - copy.DestinationX ||
				copy.Height > destinationHeight - copy.DestinationY ||
				copy.Depth > destinationDepth - copy.DestinationZ),
			"Prepared recording requires valid admitted input and capacity.");

		ASSERT(!((IsDepthFormat(src.Format) || src.SampleCount != 1) &&
				(copy.SourceX != 0 ||
				 copy.SourceY != 0 ||
				 copy.DestinationX != 0 ||
				 copy.DestinationY != 0 ||
				 copy.Width != sourceWidth ||
				 copy.Width != destinationWidth ||
				 copy.Height != sourceHeight ||
				 copy.Height != destinationHeight)),
			"Prepared recording requires valid admitted input and capacity.");

		const uint32 block = GetFormatInfo(src.Format).BlockSize;

		ASSERT(!(copy.SourceX % block || copy.SourceY % block || copy.DestinationX % block || copy.DestinationY % block || copy.Width % block || copy.Height % block),
			"Prepared recording requires valid admitted input and capacity.");
		RecordedTextureCopy recorded{ copy.Source,
									  copy.Destination,
									  {source->Native,
									   destination->Native,
									   copy.Width, copy.Height, 1, 1,
									   copy.SourceSubresource.MipLevel + copy.SourceSubresource.ArrayLayer * src.MipLevels,
									   copy.DestinationSubresource.MipLevel + copy.DestinationSubresource.ArrayLayer * dst.MipLevels,
									   copy.SourceSubresource.MipLevel,
									   copy.SourceSubresource.ArrayLayer,
									   copy.DestinationSubresource.MipLevel,
									   copy.DestinationSubresource.ArrayLayer,
									   copy.SourceX,
									   copy.SourceY,
									   copy.DestinationX,
									   copy.DestinationY,
									   true} };
		recorded.Native.Aspect = copy.SourceSubresource.Aspect;
		recorded.Native.Depth = copy.Depth;
		recorded.Native.SourceZ = copy.SourceZ;
		recorded.Native.DestinationZ = copy.DestinationZ;

		TextureViewDesc sourceExtent;
		sourceExtent.BaseMipLevel = copy.SourceSubresource.MipLevel;
		sourceExtent.BaseArrayLayer = copy.SourceSubresource.ArrayLayer;
		sourceExtent.Aspect = copy.SourceSubresource.Aspect;
		TextureViewDesc destinationExtent;
		destinationExtent.BaseMipLevel = copy.DestinationSubresource.MipLevel;
		destinationExtent.BaseArrayLayer = copy.DestinationSubresource.ArrayLayer;
		destinationExtent.Aspect = copy.DestinationSubresource.Aspect;
		ASSERT(!(!preparedTextureContains(*phase, copy.Source, ETextureState::CopySource, &sourceExtent) ||
				!preparedTextureContains(*phase, copy.Destination, ETextureState::CopyDestination, &destinationExtent)),
			"Prepared recording requires valid admitted input and capacity.");

		ASSERT(!(phase->Used.TextureCopies >= phase->Capacity.TextureCopies), "Prepared recording requires valid admitted input and capacity.");

		Result<void> result = recordNative([&] { return backend::RecordTextureCopy(m_State->m_Backend.State, command.Native, recorded.Native); });
		if (!result)
		{
			return preparedEmission(command, Move(result));
		}

		++phase->Used.TextureCopies;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<SubmissionToken> Device::UploadTexture(TextureHandle destination, Span<const Byte> data)
	{
		CallScope call(*this, false);
		return UploadTextureSubresource(destination, {}, data);
	}

	[[nodiscard]] Result<SubmissionToken> Device::UploadTextureSubresource(TextureHandle destination, TextureSubresource subresource, Span<const Byte> data)
	{
		CallScope call(*this, false);
		TextureSlot* const slot = findTexture(destination);
		if (slot != nullptr && slot->PreparedReferences != 0)
		{
			Error error;
			error.Code = EErrorCode::Conflict;

			return Result<SubmissionToken>::Failure(Move(error));
		}

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max())
		{
			return Result<SubmissionToken>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "A recording command list already references the texture.");
		}

		ASSERT(slot->PermanentState == ETrackedResourceState::Unknown || slot->PermanentState == ETrackedResourceState::CopyDestination, "The permanent texture state does not allow upload.");

		ASSERT(!(slot->Descriptor.SampleCount != 1 ||
				!HasUsage(slot->Descriptor.Usage, ETextureUsage::CopyDestination) ||
				subresource.MipLevel >= slot->Descriptor.MipLevels ||
				subresource.ArrayLayer >= slot->Descriptor.ArrayLayers ||
				GetTextureTransferFormat(slot->Descriptor.Format, subresource.Aspect) == EFormat::UNKNOWN ||
				data.Size() != SubresourceBytes(slot->Descriptor, subresource.MipLevel, subresource.Aspect) ||
				!ValidTextureUploadData(slot->Descriptor.Format, subresource.Aspect, data)),
				"Texture upload usage or tightly packed byte count is invalid.");
		const EQueue queue = IsDepthFormat(slot->Descriptor.Format) ? EQueue::Graphics : EQueue::Copy;
		Result<CommandListHandle> list = CreateCommandList(queue);
		if (!list)
		{
			return Result<SubmissionToken>::Failure(list.GetError());
		}
		Result<void> recorded = WriteTexture(list.Value(), destination, subresource, data);

		if (!recorded)
		{
			DiscardCommandList(list.Value());
			return Result<SubmissionToken>::Failure(recorded.GetError());
		}
		const SubmitOutcome submitted = Submit(list.Value());
		if (submitted.Status == ESubmitStatus::Busy)
		{
			DiscardCommandList(list.Value());
			return Result<SubmissionToken>::Failure(nativeQueueAccessConflict());
		}
		if (submitted.Status == ESubmitStatus::Failed)
		{
			return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.Submission", "The upload submission failed.");
		}
		return Result<SubmissionToken>::Success(submitted.Token);
	}

	[[nodiscard]] Result<void> Device::ReadTextureBlocking(TextureHandle source, Span<Byte> output, SubmissionToken after, uint32 timeoutMilliseconds)
	{
		CallScope call(*this, false);
		return ReadTextureSubresourceBlocking(source, {}, output, after, timeoutMilliseconds);
	}

	[[nodiscard]] Result<void> Device::ReadTextureSubresourceBlocking(
		TextureHandle source,
		TextureSubresource subresource,
		Span<Byte> output,
		SubmissionToken after,
		uint32 timeoutMilliseconds)
	{
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot admit readback after submission failure.");
		}
		TextureSlot* const slot = findTexture(source);
		if (slot != nullptr && slot->PreparedReferences != 0)
		{
			Error error;
			error.Code = EErrorCode::Conflict;
			return Result<void>::Failure(Move(error));
		}

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max())
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "A recording command list already references the texture.");
		}

		ASSERT(slot->PermanentState == ETrackedResourceState::Unknown || slot->PermanentState == ETrackedResourceState::CopySource, "The permanent texture state does not allow readback.");

		ASSERT(!(slot->Descriptor.SampleCount != 1 ||
				!HasUsage(slot->Descriptor.Usage, ETextureUsage::CopySource) ||
				subresource.MipLevel >= slot->Descriptor.MipLevels ||
				subresource.ArrayLayer >= slot->Descriptor.ArrayLayers ||
				GetTextureTransferFormat(slot->Descriptor.Format, subresource.Aspect) == EFormat::UNKNOWN ||
				output.Size() != SubresourceBytes(slot->Descriptor, subresource.MipLevel, subresource.Aspect) ||
				output.Data() == nullptr),
			"Texture readback usage or tightly packed byte count is invalid.");

		const MonotonicClock::time_point started = MonotonicClock::now();

		if (after.Owner != 0 || after.Value != 0)
		{
			Result<void> waited = WaitForSubmission(after, timeoutMilliseconds);
			if (!waited)
			{
				return waited;
			}
		}

		const int64 elapsed = DurationCast<Milliseconds>(MonotonicClock::now() - started).count();
		const uint32 remaining = elapsed >= timeoutMilliseconds ? 0u : timeoutMilliseconds - static_cast<uint32>(elapsed);
		slot->bSharedReadPrepared = false;
		const EQueue queue = IsDepthFormat(slot->Descriptor.Format) ? EQueue::Graphics : EQueue::Copy;
		const uint64 before = backend::GetQueueLastSubmittedSerial(m_State->m_Backend.State, queue);
		Result<void> read = backend::ReadTexture(m_State->m_Backend.State, slot->Native, slot->Descriptor, subresource, output, remaining);
		if (!read && backend::HAS_WAS_SUBMISSION_ACCEPTED && backend::WasSubmissionAccepted(m_State->m_Backend.State))
		{
			const uint64 afterSerial = backend::GetQueueLastSubmittedSerial(m_State->m_Backend.State, queue);
			if (afterSerial == before)
			{
				ASSERT(!m_State->m_RetainedReadTexture.IsValid(), "A terminal device retains one uncertain readback texture.");
				++slot->PreparedReferences;
				m_State->m_RetainedReadTexture = source;
				recordSubmissionFault(read.GetError());
			}
			else if (read.GetError().DiagnosticID == "Rhi.DeviceLost" || read.GetError().DiagnosticID == "Rhi.D3D12Wait" || read.GetError().DiagnosticID == "Rhi.VulkanWait")
			{
				recordSubmissionFault(read.GetError());
			}
		}
		return read;
	}

	[[nodiscard]] Result<void> Device::CopyTexture(CommandListHandle list, const TextureCopy& copy)
	{
		CallScope call(*this, true, true);
		CommandSlot* const command = findCommand(list);
		if (command != nullptr && command->PreparedGroup)
		{
			return copyPreparedTexture(*command, copy);
		}

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!command->bRendering, "Copy commands cannot be recorded inside a rendering pass.");
		TextureSlot* const source = findTexture(copy.Source);
		TextureSlot* const destination = findTexture(copy.Destination);

		ASSERT(!(source == nullptr || destination == nullptr), "RHI operations require valid handles for this device.");

		if ((source->RecordingSlot != NumericLimits<uint32>::max() && source->RecordingSlot != list.Slot) ||
			(destination->RecordingSlot != NumericLimits<uint32>::max() && destination->RecordingSlot != list.Slot))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the texture.");
		}

		ASSERT(!((IsDepthFormat(source->Descriptor.Format) && command->Queue != EQueue::Graphics) ||
				copy.Source == copy.Destination ||
				source->Descriptor.Width != destination->Descriptor.Width ||
				source->Descriptor.Height != destination->Descriptor.Height ||
				source->Descriptor.Depth != destination->Descriptor.Depth ||
				!AreTextureCopyFormatsCompatible(source->Descriptor.Format, destination->Descriptor.Format) ||
				source->Descriptor.MipLevels != destination->Descriptor.MipLevels ||
				source->Descriptor.ArrayLayers != destination->Descriptor.ArrayLayers ||
				source->Descriptor.Dimension != destination->Descriptor.Dimension ||
				source->Descriptor.SampleCount != destination->Descriptor.SampleCount ||
				!HasUsage(source->Descriptor.Usage, ETextureUsage::CopySource) ||
				!HasUsage(destination->Descriptor.Usage, ETextureUsage::CopyDestination)),
			"Texture copy requires distinct equal images and matching copy usage.");
		RecordedTextureCopy recorded{ copy.Source,
									  copy.Destination,
									  {source->Native, destination->Native,
									   source->Descriptor.Width,
									   source->Descriptor.Height,
									   source->Descriptor.MipLevels,
									   source->Descriptor.ArrayLayers} };
		recorded.Native.Depth = source->Descriptor.Depth;

		if (!command->TextureUses.Reserve(command->TextureUses.Size() + 2) || !command->TextureCopies.Add(recorded))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "The device could not track the texture copy.");
		}
		(void)command->TextureUses.Add({ copy.Source, source->Native, false, ETrackedResourceState::CopySource });
		(void)command->TextureUses.Add({ copy.Destination, destination->Native, false, ETrackedResourceState::CopyDestination });
		source->RecordingSlot = list.Slot;
		destination->RecordingSlot = list.Slot;
		Result<void> nativeResult = backend::RecordTextureCopy(m_State->m_Backend.State, command->Native, recorded.Native);

		if (!nativeResult)
		{
			poisonCommandList(list);

			return nativeResult;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::CopyTextureRegion(CommandListHandle list, const TextureCopyRegion& copy)
	{
		CallScope call(*this, true, true);
		CommandSlot* const command = findCommand(list);
		if (command != nullptr && command->PreparedGroup)
		{
			return copyPreparedTextureRegion(*command, copy);
		}
		TextureSlot* const source = findTexture(copy.Source);
		TextureSlot* const destination = findTexture(copy.Destination);

		ASSERT(!(command == nullptr || source == nullptr || destination == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!command->bRendering, "Texture copies cannot be recorded inside a rendering pass.");

		if ((source->RecordingSlot != NumericLimits<uint32>::max() && source->RecordingSlot != list.Slot) ||
			(destination->RecordingSlot != NumericLimits<uint32>::max() && destination->RecordingSlot != list.Slot))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the texture.");
		}

		const TextureDesc& src = source->Descriptor;
		const TextureDesc& dst = destination->Descriptor;

		ASSERT(!((IsDepthFormat(src.Format) && command->Queue != EQueue::Graphics) ||
				GetTextureTransferFormat(src.Format, copy.SourceSubresource.Aspect) == EFormat::UNKNOWN ||
				!AreTextureCopyFormatsCompatible(GetTextureTransferFormat(src.Format, copy.SourceSubresource.Aspect), GetTextureTransferFormat(dst.Format, copy.DestinationSubresource.Aspect)) ||
				copy.Source == copy.Destination ||
				!AreTextureCopyFormatsCompatible(src.Format, dst.Format) ||
				((src.Dimension == ETextureDimension::Texture3D) != (dst.Dimension == ETextureDimension::Texture3D)) ||
				src.SampleCount != dst.SampleCount ||
				copy.SourceSubresource.MipLevel >= src.MipLevels ||
				copy.SourceSubresource.ArrayLayer >= src.ArrayLayers ||
				copy.DestinationSubresource.MipLevel >= dst.MipLevels ||
				copy.DestinationSubresource.ArrayLayer >= dst.ArrayLayers ||
				!HasUsage(src.Usage, ETextureUsage::CopySource) ||
				!HasUsage(dst.Usage, ETextureUsage::CopyDestination)),
			"Texture format, subresource, sample count, or usage is invalid.");

		const uint32 sourceWidth = MipExtent(src.Width, copy.SourceSubresource.MipLevel);
		const uint32 sourceHeight = MipExtent(src.Height, copy.SourceSubresource.MipLevel);
		const uint32 destinationWidth = MipExtent(dst.Width, copy.DestinationSubresource.MipLevel);
		const uint32 destinationHeight = MipExtent(dst.Height, copy.DestinationSubresource.MipLevel);
		const uint32 sourceDepth = MipExtent(src.Depth, copy.SourceSubresource.MipLevel);
		const uint32 destinationDepth = MipExtent(dst.Depth, copy.DestinationSubresource.MipLevel);

		ASSERT(!(copy.Width == 0 ||
				copy.Height == 0 ||
				copy.Depth == 0 ||
				copy.SourceX > sourceWidth ||
				copy.SourceY > sourceHeight ||
				copy.SourceZ > sourceDepth ||
				copy.DestinationX > destinationWidth ||
				copy.DestinationY > destinationHeight ||
				copy.DestinationZ > destinationDepth ||
				copy.Width > sourceWidth - copy.SourceX ||
				copy.Height > sourceHeight - copy.SourceY ||
				copy.Depth > sourceDepth - copy.SourceZ ||
				copy.Width > destinationWidth - copy.DestinationX ||
				copy.Height > destinationHeight - copy.DestinationY ||
				copy.Depth > destinationDepth - copy.DestinationZ),
			"The texture region is empty or outside a subresource.");

		if ((IsDepthFormat(src.Format) || src.SampleCount != 1) &&
			(copy.SourceX != 0 ||
			 copy.SourceY != 0 ||
			 copy.DestinationX != 0 ||
			 copy.DestinationY != 0 ||
			 copy.Width != sourceWidth ||
			 copy.Width != destinationWidth ||
			 copy.Height != sourceHeight ||
			 copy.Height != destinationHeight))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.DepthCopy", "Depth/stencil copies require a complete plane subresource.");
		}

		const uint32 block = GetFormatInfo(src.Format).BlockSize;

		ASSERT(!(copy.SourceX % block || copy.SourceY % block || copy.DestinationX % block || copy.DestinationY % block || copy.Width % block || copy.Height % block),
			"Compressed regions must follow block boundaries.");
		RecordedTextureCopy recorded{ copy.Source,
									 copy.Destination,
									 {source->Native, destination->Native, copy.Width, copy.Height, 1, 1,
									  copy.SourceSubresource.MipLevel + copy.SourceSubresource.ArrayLayer * src.MipLevels,
									  copy.DestinationSubresource.MipLevel + copy.DestinationSubresource.ArrayLayer * dst.MipLevels,
									  copy.SourceSubresource.MipLevel, copy.SourceSubresource.ArrayLayer,
									  copy.DestinationSubresource.MipLevel, copy.DestinationSubresource.ArrayLayer, copy.SourceX,
									  copy.SourceY, copy.DestinationX, copy.DestinationY, true} };
		recorded.Native.Aspect = copy.SourceSubresource.Aspect;
		recorded.Native.Depth = copy.Depth;
		recorded.Native.SourceZ = copy.SourceZ;
		recorded.Native.DestinationZ = copy.DestinationZ;

		if (!command->TextureUses.Reserve(command->TextureUses.Size() + 2) || !command->TextureCopies.Add(recorded))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "The device could not track the texture region copy.");
		}
		(void)command->TextureUses.Add({ copy.Source, source->Native, false, ETrackedResourceState::CopySource });
		(void)command->TextureUses.Add({ copy.Destination, destination->Native, false, ETrackedResourceState::CopyDestination });
		source->RecordingSlot = list.Slot;
		destination->RecordingSlot = list.Slot;
		Result<void> result = backend::RecordTextureCopy(m_State->m_Backend.State, command->Native, recorded.Native);

		if (!result)
		{
			poisonCommandList(list);

			return result;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<TextureBufferFootprint> Device::calculateTextureBufferFootprint(const TextureDesc& desc, TextureSubresource subresource) const
	{
		ASSERT(subresource.MipLevel < desc.MipLevels &&
			subresource.ArrayLayer < desc.ArrayLayers &&
			desc.SampleCount == 1 &&
			GetTextureTransferFormat(desc.Format, subresource.Aspect) != EFormat::UNKNOWN,
			"A single-sample texture with a valid subresource and aspect is required.");

		if constexpr (backend::HAS_GET_TEXTURE_BUFFER_FOOTPRINT)
		{
			return backend::GetTextureBufferFootprint(m_State->m_Backend.State, desc, subresource);
		}

		const FormatInfo& format = GetFormatInfo(GetTextureTransferFormat(desc.Format, subresource.Aspect));
		const uint32 width = MipExtent(desc.Width, subresource.MipLevel);
		const uint32 height = MipExtent(desc.Height, subresource.MipLevel);
		const uint32 depth = MipExtent(desc.Depth, subresource.MipLevel);
		const uint64 blocksWide = (static_cast<uint64>(width) + format.BlockSize - 1) / format.BlockSize;
		const uint32 rows = (height + format.BlockSize - 1) / format.BlockSize;
		const uint64 rowBytes = blocksWide * format.BytesPerBlock;
		const uint64 pitch = (rowBytes + 255u) & ~uint64{ 255 };
		if (pitch > NumericLimits<uint32>::max() || pitch > NumericLimits<uint64>::max() / rows / depth)
		{
			return Result<TextureBufferFootprint>::Failure(EErrorCode::Unsupported, "Rhi.TextureBufferFootprint", "The texture footprint exceeds the supported buffer range.");
		}

		return Result<TextureBufferFootprint>::Success({ pitch * rows * depth, static_cast<uint32>(pitch), rows, width, height, depth });
	}

	[[nodiscard]] Result<TextureBufferFootprint> Device::GetTextureBufferFootprint(TextureHandle handle, TextureSubresource subresource)
	{
		CallScope call(*this, true);
		TextureSlot* const texture = findTexture(handle);

		ASSERT(texture != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(subresource.MipLevel < texture->Descriptor.MipLevels &&
			subresource.ArrayLayer < texture->Descriptor.ArrayLayers &&
			GetTextureTransferFormat(texture->Descriptor.Format, subresource.Aspect) != EFormat::UNKNOWN,
			"The texture footprint subresource must be valid.");
		if (texture->Descriptor.SampleCount != 1)
		{
			return Result<TextureBufferFootprint>::Failure(EErrorCode::Unsupported, "Rhi.TextureBufferFootprint", "Texture buffer footprints require a single-sample texture.");
		}

		return calculateTextureBufferFootprint(texture->Descriptor, subresource);
	}

	[[nodiscard]] Result<void> Device::CopyBufferToTexture(CommandListHandle list, const TextureBufferCopy& copy)
	{
		CallScope call(*this, true);
		return copyTextureBuffer(list, copy, true);
	}

	[[nodiscard]] Result<void> Device::CopyTextureToBuffer(CommandListHandle list, const TextureBufferCopy& copy)
	{
		CallScope call(*this, true);
		return copyTextureBuffer(list, copy, false);
	}

	[[nodiscard]] Result<void> Device::copyTextureBuffer(CommandListHandle list, const TextureBufferCopy& copy, bool bUpload)
	{
		CommandSlot* const command = findCommand(list);
		if (command != nullptr && command->PreparedGroup)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		BufferSlot* const buffer = findBuffer(copy.Buffer);
		TextureSlot* const texture = findTexture(copy.Texture);

		ASSERT(!(command == nullptr || buffer == nullptr || texture == nullptr), "RHI operations require valid handles for this device.");

		if (hasPreparedBuffer(copy.Buffer) || texture->PreparedReferences != 0)
		{
			poisonCommandList(list);
			return preparedFailure(EErrorCode::Conflict);
		}

		ASSERT(!(command->bRendering ||
				buffer->bMapped ||
				(IsDepthFormat(texture->Descriptor.Format) && command->Queue != EQueue::Graphics) ||
				!HasUsage(buffer->Usage, bUpload ? EBufferUsage::CopySource : EBufferUsage::CopyDestination) ||
				!HasUsage(texture->Descriptor.Usage, bUpload ? ETextureUsage::CopyDestination : ETextureUsage::CopySource) ||
				copy.BufferOffsetBytes % 512 != 0),
			"Rendering state, usage, mapping, or buffer alignment is invalid.");

		if (texture->RecordingSlot != NumericLimits<uint32>::max() && texture->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the texture.");
		}

		Result<TextureBufferFootprint> footprint = GetTextureBufferFootprint(copy.Texture, copy.Subresource);

		if (!footprint)
		{
			ASSERT(footprint.GetError().Code != EErrorCode::InvalidArgument, "The texture copy subresource must be valid.");
			poisonCommandList(list);
			return Result<void>::Failure(footprint.GetError());
		}

		ASSERT(!(copy.BufferOffsetBytes > buffer->ByteCount || footprint.Value().ByteCount > buffer->ByteCount - copy.BufferOffsetBytes), "The texture copy footprint must fit in the buffer.");

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + 1) || !command->TextureUses.Reserve(command->TextureUses.Size() + 1))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain texture buffer copy resources.");
		}
		(void)command->AddBufferUse({ copy.Buffer, buffer->Native, bUpload ? ETrackedResourceState::CopySource : ETrackedResourceState::CopyDestination });
		(void)command->TextureUses.Add({ copy.Texture, texture->Native, false, bUpload ? ETrackedResourceState::CopyDestination : ETrackedResourceState::CopySource });
		++buffer->RecordedUses;
		texture->RecordingSlot = list.Slot;
		const NativeTextureBufferCopy native{ buffer->Native, texture->Native, copy.Subresource, footprint.Value(), copy.BufferOffsetBytes };
		Result<void> recorded = bUpload ? backend::RecordBufferToTexture(m_State->m_Backend.State, command->Native, native) : backend::RecordTextureToBuffer(m_State->m_Backend.State, command->Native, native);

		if (!recorded)
		{
			poisonCommandList(list);
		}

		return recorded;
	}

	[[nodiscard]] Result<Device::StagingSubresource> Device::describeStagingSubresource(const TextureDesc& desc, TextureSubresource requested) const
	{
		ASSERT(requested.MipLevel < desc.MipLevels &&
			requested.ArrayLayer < desc.ArrayLayers &&
			GetTextureTransferFormat(desc.Format, requested.Aspect) != EFormat::UNKNOWN,
			"The staging subresource is outside the texture.");

		const uint32 planeCount = GetFormatInfo(desc.Format).bHasStencil ? 2u : 1u;
		const uint32 requestedPlane = requested.Aspect == ETextureAspect::Stencil ? 1u : 0u;
		uint64 offset = 0;

		for (uint32 plane = 0; plane < planeCount; ++plane)
		{
			for (uint32 layer = 0; layer < desc.ArrayLayers; ++layer)
			{
				for (uint32 mip = 0; mip < desc.MipLevels; ++mip)
				{
					Result<TextureBufferFootprint> footprint = calculateTextureBufferFootprint(desc, { mip, layer, plane == 1 ? ETextureAspect::Stencil : ETextureAspect::Default });
					if (!footprint)
					{
						return Result<StagingSubresource>::Failure(footprint.GetError());
					}

					const uint64 maximum = m_State->m_Backend.Capabilities.Limits.MaximumBufferByteCount;

					if (offset > maximum || footprint.Value().ByteCount > maximum - offset)
					{
						return Result<StagingSubresource>::Failure(EErrorCode::Unsupported, "Rhi.StagingTexture", "The staging texture exceeds the supported buffer size.");
					}

					if (mip == requested.MipLevel && layer == requested.ArrayLayer && plane == requestedPlane)
					{
						return Result<StagingSubresource>::Success({ offset, footprint.Value() });
					}

					const uint64 end = offset + footprint.Value().ByteCount;
					const uint64 padding = (512u - (end & 511u)) & 511u;

					if (padding > maximum - end)
					{
						return Result<StagingSubresource>::Failure(EErrorCode::Unsupported, "Rhi.StagingTexture", "The staging texture exceeds the supported buffer size.");
					}
					offset = end + padding;
				}
			}
		}

		ASSERT(false, "A validated staging subresource must be found in its texture layout.");
	}

	[[nodiscard]] Result<StagingTextureHandle> Device::CreateStagingTexture(const TextureDesc& desc, EBufferMemory access)
	{
		CallScope call(*this, false);

		ASSERT(access == EBufferMemory::CpuWrite || access == EBufferMemory::CpuRead, "Staging textures require CPU read or write access.");
		TextureDesc validated = desc;
		validated.Usage = ETextureUsage::CopySource | ETextureUsage::CopyDestination;

		if (IsDepthFormat(desc.Format))
		{
			validated.Usage = validated.Usage | ETextureUsage::DepthStencil;
		}

		Result<uint64> valid = validateTextureDesc(validated);

		if (!valid)
		{
			ASSERT(valid.GetError().Code != EErrorCode::InvalidArgument, "A valid texture descriptor is required.");
			return Result<StagingTextureHandle>::Failure(valid.GetError());
		}

		if (desc.SampleCount != 1)
		{
			return Result<StagingTextureHandle>::Failure(EErrorCode::Unsupported, "Rhi.StagingTexture", "Staging textures require a single-sample format.");
		}

		Result<Device::StagingSubresource> last = describeStagingSubresource(validated, { validated.MipLevels - 1, validated.ArrayLayers - 1, GetFormatInfo(desc.Format).bHasStencil ? ETextureAspect::Stencil : ETextureAspect::Default });
		if (!last)
		{
			return Result<StagingTextureHandle>::Failure(last.GetError());
		}

		const uint64 bytes = last.Value().Offset + last.Value().Footprint.ByteCount;
		const bool bNewSlot = m_State->m_FreeStagingTexture == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_StagingTextures.Size()) : m_State->m_FreeStagingTexture;

		if (bNewSlot && (m_State->m_StagingTextures.Size() >= NumericLimits<uint32>::max() || !m_State->m_StagingTextures.Add(StagingTextureSlot{})))
		{
			return Result<StagingTextureHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "The device could not allocate a staging texture handle.");
		}

		Result<BufferHandle> buffer = CreateBuffer({ bytes, access == EBufferMemory::CpuWrite ? EBufferUsage::CopySource : EBufferUsage::CopyDestination, access });
		if (!buffer)
		{
			if (bNewSlot)
			{
				m_State->m_StagingTextures[index].NextFree = m_State->m_FreeStagingTexture;
				m_State->m_FreeStagingTexture = index;
			}

			return Result<StagingTextureHandle>::Failure(buffer.GetError());
		}
		StagingTextureSlot& slot = m_State->m_StagingTextures[index];

		if (!bNewSlot)
		{
			m_State->m_FreeStagingTexture = slot.NextFree;
		}
		slot.Buffer = buffer.Value();
		slot.Descriptor = validated;
		slot.Access = access;
		slot.NextFree = NumericLimits<uint32>::max();
		slot.bLive = true;

		return Result<StagingTextureHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyStagingTexture(StagingTextureHandle handle)
	{
		CallScope call(*this, false);
		StagingTextureSlot* const slot = findStagingTexture(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		Result<void> destroyed = DestroyBuffer(slot->Buffer);

		if (!destroyed)
		{
			return destroyed;
		}
		slot->Buffer = {};
		slot->Descriptor = {};
		slot->Access = EBufferMemory::Device;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeStagingTexture;
			m_State->m_FreeStagingTexture = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<StagingTextureMapping> Device::MapStagingTexture(StagingTextureHandle handle, TextureSubresource subresource, uint32 timeoutMilliseconds)
	{
		CallScope call(*this, false);
		StagingTextureSlot* const slot = findStagingTexture(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		Result<Device::StagingSubresource> layout = describeStagingSubresource(slot->Descriptor, subresource);

		if (!layout)
		{
			ASSERT(layout.GetError().Code != EErrorCode::InvalidArgument, "The staging subresource must be valid.");
			return Result<StagingTextureMapping>::Failure(layout.GetError());
		}

		Result<Span<Byte>> mapped = MapBuffer(slot->Buffer, layout.Value().Offset, layout.Value().Footprint.ByteCount, timeoutMilliseconds);

		if (!mapped)
		{
			return Result<StagingTextureMapping>::Failure(mapped.GetError());
		}

		return Result<StagingTextureMapping>::Success({ mapped.Value(), layout.Value().Footprint });
	}

	[[nodiscard]] Result<void> Device::UnmapStagingTexture(StagingTextureHandle handle)
	{
		CallScope call(*this, false);
		StagingTextureSlot* const slot = findStagingTexture(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		return UnmapBuffer(slot->Buffer);
	}

	[[nodiscard]] Result<void> Device::copyStagingTexture(
		CommandListHandle list,
		StagingTextureHandle staging,
		TextureSubresource stagingSubresource,
		TextureHandle texture,
		TextureSubresource textureSubresource,
		bool bUpload)
	{
		StagingTextureSlot* const slot = findStagingTexture(staging);
		TextureSlot* const image = findTexture(texture);

		ASSERT(!(slot == nullptr || image == nullptr), "RHI operations require valid handles for this device.");
		ASSERT(!(slot->Access != (bUpload ? EBufferMemory::CpuWrite : EBufferMemory::CpuRead) || slot->Descriptor.Format != image->Descriptor.Format), "The staging access or format does not match the texture.");

		Result<Device::StagingSubresource> layout = describeStagingSubresource(slot->Descriptor, stagingSubresource);

		if (!layout)
		{
			ASSERT(layout.GetError().Code != EErrorCode::InvalidArgument, "The staging subresource must be valid.");
			poisonCommandList(list);
			return Result<void>::Failure(layout.GetError());
		}

		Result<TextureBufferFootprint> footprint = GetTextureBufferFootprint(texture, textureSubresource);

		if (!footprint)
		{
			ASSERT(footprint.GetError().Code != EErrorCode::InvalidArgument, "The texture copy subresource must be valid.");
			poisonCommandList(list);
			return Result<void>::Failure(footprint.GetError());
		}

		ASSERT(!(layout.Value().Footprint.ByteCount != footprint.Value().ByteCount ||
				layout.Value().Footprint.Width != footprint.Value().Width ||
				layout.Value().Footprint.Height != footprint.Value().Height ||
				layout.Value().Footprint.Depth != footprint.Value().Depth),
			"The staging and GPU subresource extents differ.");

		return copyTextureBuffer(list, { slot->Buffer, texture, textureSubresource, layout.Value().Offset }, bUpload);
	}

	[[nodiscard]] Result<void> Device::CopyStagingTextureToTexture(
		CommandListHandle list,
		StagingTextureHandle source,
		TextureSubresource sourceSubresource,
		TextureHandle destination,
		TextureSubresource destinationSubresource)
	{
		CallScope call(*this, true);
		return copyStagingTexture(list, source, sourceSubresource, destination, destinationSubresource, true);
	}

	[[nodiscard]] Result<void> Device::CopyTextureToStagingTexture(
		CommandListHandle list,
		TextureHandle source,
		TextureSubresource sourceSubresource,
		StagingTextureHandle destination,
		TextureSubresource destinationSubresource)
	{
		CallScope call(*this, true);
		return copyStagingTexture(list, destination, destinationSubresource, source, sourceSubresource, false);
	}

	[[nodiscard]] Result<bool> Device::QueryTextureResolveSupport(const TextureDesc& source, ETextureResolveMode mode, ETextureResolveMode stencilMode)
	{
		CallScope call(*this, true);
		ASSERT(source.Format > EFormat::UNKNOWN &&
			source.Format < EFormat::COUNT &&
			mode <= ETextureResolveMode::Maximum &&
			stencilMode <= ETextureResolveMode::Maximum &&
			(mode != ETextureResolveMode::None || stencilMode != ETextureResolveMode::None) &&
			(GetFormatInfo(source.Format).bHasStencil || stencilMode == ETextureResolveMode::None),
			"The resolve query format and modes must name compatible active planes.");
		Result<uint64> validated = validateTextureDesc(source);
		if (!validated)
		{
			ASSERT(validated.GetError().Code != EErrorCode::InvalidArgument, "A valid texture descriptor is required.");
			return Result<bool>::Failure(validated.GetError());
		}
		Result<uint32> samples = queryNativeTextureSampleCounts(source);
		if (!samples)
		{
			return Result<bool>::Failure(samples.GetError());
		}

		return Result<bool>::Success( (source.SampleCount == 2 || source.SampleCount == 4 || source.SampleCount == 8) && (samples.Value() & source.SampleCount) != 0 && backend::HAS_SUPPORTS_TEXTURE_RESOLVE && backend::SupportsTextureResolve(m_State->m_Backend.State, source, mode, stencilMode));
	}

	[[nodiscard]] Result<void> Device::validateTextureResolve(const TextureResolve& resolve, const TextureSlot& source, const TextureSlot& destination, NativeTextureResolve* outNative)
	{
		ASSERT(outNative != nullptr, "The resolve native output must be present.");
		const TextureDesc& src = source.Descriptor;
		const TextureDesc& dst = destination.Descriptor;
		const FormatInfo& info = GetFormatInfo(src.Format);
		const TextureSubresource& a = resolve.SourceSubresource;
		const TextureSubresource& b = resolve.DestinationSubresource;
		ASSERT(resolve.Source != resolve.Destination &&
			src.Format == dst.Format &&
			src.SampleCount > 1 &&
			dst.SampleCount == 1 &&
			src.Dimension == ETextureDimension::Texture2D &&
			dst.Dimension == ETextureDimension::Texture2D &&
			a.MipLevel < src.MipLevels &&
			b.MipLevel < dst.MipLevels &&
			a.ArrayLayer < src.ArrayLayers &&
			b.ArrayLayer < dst.ArrayLayers &&
			preparedTextureAspectSupported(src, a.Aspect) &&
			preparedTextureAspectSupported(dst, b.Aspect) &&
			a.Aspect == b.Aspect &&
			resolve.Mode <= ETextureResolveMode::Maximum &&
			resolve.StencilMode <= ETextureResolveMode::Maximum &&
			(resolve.Mode != ETextureResolveMode::None || resolve.StencilMode != ETextureResolveMode::None) &&
			(info.bHasStencil || resolve.StencilMode == ETextureResolveMode::None) &&
			(a.Aspect != ETextureAspect::Depth || resolve.StencilMode == ETextureResolveMode::None) &&
			(a.Aspect != ETextureAspect::Stencil || resolve.Mode == ETextureResolveMode::None) &&
			MipExtent(src.Width, a.MipLevel) == MipExtent(dst.Width, b.MipLevel) &&
			MipExtent(src.Height, a.MipLevel) == MipExtent(dst.Height, b.MipLevel) &&
			HasUsage(src.Usage, ETextureUsage::CopySource) &&
			HasUsage(dst.Usage, ETextureUsage::CopyDestination) &&
			(!info.bHasDepth || (HasUsage(src.Usage, ETextureUsage::DepthStencil) && HasUsage(dst.Usage, ETextureUsage::DepthStencil))) &&
			(m_State->m_Kind != EBackend::Vulkan || info.Kind != EFormatKind::Integer || (HasUsage(src.Usage, ETextureUsage::RenderTarget) && HasUsage(dst.Usage, ETextureUsage::RenderTarget))),
			"Prepared command arguments must match their declared resources, ranges, and execution order.");
		Result<bool> supported = QueryTextureResolveSupport(src, resolve.Mode, resolve.StencilMode);
		if (!supported || !supported.Value())
		{
			return preparedFailure(supported ? EErrorCode::Unsupported : supported.GetError().Code);
		}
		*outNative = { source.Native,
					  destination.Native,
					  MipExtent(src.Width, a.MipLevel),
					  MipExtent(src.Height, a.MipLevel),
					  src.Format,
					  resolve.Mode,
					  resolve.StencilMode,
					  a,
					  b };

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::ResolveTexture(CommandListHandle list, const TextureResolve& resolve)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		if (command != nullptr && command->PreparedGroup)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		TextureSlot* const source = findTexture(resolve.Source);
		TextureSlot* const destination = findTexture(resolve.Destination);

		ASSERT(!(command == nullptr || source == nullptr || destination == nullptr), "RHI operations require valid handles for this device.");

		if (source->PreparedReferences != 0 || destination->PreparedReferences != 0)
		{
			poisonCommandList(list);
			return preparedFailure(EErrorCode::Conflict);
		}

		ASSERT(!(command->Queue != EQueue::Graphics || command->bRendering), "Texture resolve requires a graphics list outside rendering.");
		NativeTextureResolve native;
		Result<void> valid = validateTextureResolve(resolve, *source, *destination, &native);
		if (!valid)
		{
			ASSERT(!(valid.GetError().Code == EErrorCode::InvalidArgument), "Texture resolve arguments must match the source and destination.");
			poisonCommandList(list);
			return valid;
		}

		if ((source->RecordingSlot != NumericLimits<uint32>::max() && source->RecordingSlot != list.Slot) ||
			(destination->RecordingSlot != NumericLimits<uint32>::max() && destination->RecordingSlot != list.Slot))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list references a resolve texture.");
		}

		if (!command->TextureUses.Reserve(command->TextureUses.Size() + 2))
		{
			poisonCommandList(list);
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track resolve textures.");
		}
		(void)command->TextureUses.Add({ resolve.Source, source->Native, false, ETrackedResourceState::ResolveSource });
		(void)command->TextureUses.Add({ resolve.Destination, destination->Native, false, ETrackedResourceState::ResolveDestination });
		source->RecordingSlot = list.Slot;
		destination->RecordingSlot = list.Slot;
		Result<void> recorded = backend::RecordTextureResolve(m_State->m_Backend.State, command->Native, native);

		if (!recorded)
		{
			poisonCommandList(list);
			return recorded;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::copyPreparedTextureBuffer(CommandListHandle handle, PreparedTextureBufferCopyRef ref, ETextureBufferCopyDirection direction)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(phase != nullptr && ref.List == handle && ref.Phase == preparedList(command)->ActivePhase && ref.Slot < phase->TextureBufferCopies.Size() && !command->bRendering,
			"Prepared texture operations require a matching selector outside rendering.");
		ASSERT(phase->Used.TextureCopies < phase->Capacity.TextureCopies, "Prepared texture operations stay within their admitted capacity.");

		rhi::PreparedRecordingTextureBufferTransfer const& transfer = phase->TextureBufferCopies[ref.Slot];
		ASSERT(transfer.Description.Direction == direction, "Prepared texture copies require the declared transfer direction.");
		Result<void> result = recordNative(
			[&]
			{
				return transfer.Description.Direction == ETextureBufferCopyDirection::BufferToTexture
					? backend::RecordBufferToTexture(m_State->m_Backend.State, command->Native, transfer.Native)
					: backend::RecordTextureToBuffer(m_State->m_Backend.State, command->Native, transfer.Native);
			});
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}
		++phase->Used.TextureCopies;

		return Result<void>::Success();
	}
}
