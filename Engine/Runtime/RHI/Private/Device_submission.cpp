#include "DeviceState.h"
#include <Log.h>
namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] SubmitOutcome Device::Submit(CommandListHandle list)
	{
		CallScope call(*this, false);
		SubmissionResult result = submitInternal(list);
		if (result.Completion)
		{
			return { ESubmitStatus::Submitted, result.Completion.Value() };
		}
		if (result.InputState == ESubmissionInputState::Retained)
		{
			return { ESubmitStatus::Busy, {} };
		}
		recordSubmissionFault(result.Completion.GetError());
		return { ESubmitStatus::Failed, {} };
	}

	[[nodiscard]] SubmissionResult Device::submitInternal(CommandListHandle list)
	{
		ESubmissionDisposition disposition = ESubmissionDisposition::NotAccepted;
		ESubmissionInputState inputState = ESubmissionInputState::Retained;
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			DiscardCommandList(list);
			Error error;
			error.Code = m_State->m_FirstSubmissionFailure;
			return { Result<SubmissionToken>::Failure(Move(error)), disposition, ESubmissionInputState::Consumed };
		}
		CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(!command->bPoisoned, "Discard a command list after recording fails.");

		if (hasNativeQueueAccess(command->Queue))
		{
			Error error;
			error.Code = EErrorCode::Conflict;
			return { Result<SubmissionToken>::Failure(Move(error)), disposition, inputState };
		}
		inputState = ESubmissionInputState::Consumed;
		ASSERT(!command->bRendering, "Submit requires every rendering pass to be ended.");
		ASSERT(command->MarkerDepth == 0, "Submit requires every debug marker to be ended.");

		for (GpuTimerHandle timer : command->TimerUses)
		{
			ASSERT(m_State->m_Timers[timer.Slot].State == ETimerState::Recorded, "Submit requires every GPU timer interval to be ended.");
		}

		if (command->bHasManualBufferUses)
		{
			HashMap<uint32, ETrackedResourceState> states(m_State->m_Resource);

			if (!states.Reserve(Min(command->BufferUses.Size(), m_State->m_Buffers.Size())))
			{
				Error error;
				error.Code = EErrorCode::Conflict;

				return { Result<SubmissionToken>::Failure(Move(error)), disposition, ESubmissionInputState::Retained };
			}

			for (const RecordedBufferUse& use : command->BufferUses)
			{
				if (use.State == ETrackedResourceState::Unknown)
				{
					continue;
				}

				const BufferSlot& buffer = m_State->m_Buffers[use.Handle.Slot];
				rhi::ETrackedResourceState* previous = states.Find(use.Handle.Slot);
				const ETrackedResourceState before = previous != nullptr ? *previous : buffer.PermanentState;

				ASSERT(use.bAutomaticBarriers || use.bExplicitStateRequest || buffer.Memory != EBufferMemory::Device || before == use.State, "Set the buffer state before use with automatic barriers disabled.");

				if (previous != nullptr)
				{
					*previous = use.State;
				}
				else
				{
					(void)states.EmplaceRequired(use.Handle.Slot, use.State);
				}
			}
		}

		ASSERT(!backend::HAS_HAS_MISSING_MANUAL_STATE || !backend::HasMissingManualState(command->Native), "Set resource states before use with automatic barriers disabled.");

		for (usize index = 0; index < command->TextureUses.Size(); ++index)
		{
			const RecordedTextureUse& use = command->TextureUses[index];
			const TextureSlot& texture = m_State->m_Textures[use.Handle.Slot];

			ASSERT(texture.PermanentState == ETrackedResourceState::Unknown || use.State == ETrackedResourceState::Unknown || use.State == texture.PermanentState,
				"A texture use must match its permanent state.");

			if (!use.bPermanentTransition)
			{
				continue;
			}

			for (usize other = index + 1; other < command->TextureUses.Size(); ++other)
			{
				const RecordedTextureUse& candidate = command->TextureUses[other];
				ASSERT(candidate.Handle != use.Handle || candidate.State == ETrackedResourceState::Unknown || candidate.State == use.State, "A texture cannot change state after its permanent transition.");
			}
		}

		for (usize index = 0; index < command->BufferUses.Size(); ++index)
		{
			const RecordedBufferUse& use = command->BufferUses[index];
			const BufferSlot& buffer = m_State->m_Buffers[use.Handle.Slot];

			ASSERT(buffer.PermanentState == ETrackedResourceState::Unknown || use.State == ETrackedResourceState::Unknown || use.State == buffer.PermanentState, "A buffer use must match its permanent state.");

			if (!use.bPermanentTransition)
			{
				continue;
			}

			usize ownUses = 0;

			for (usize other = 0; other < command->BufferUses.Size(); ++other)
			{
				const RecordedBufferUse& candidate = command->BufferUses[other];

				if (candidate.Handle != use.Handle)
				{
					continue;
				}
				++ownUses;

				ASSERT(other <= index || candidate.State == ETrackedResourceState::Unknown || candidate.State == use.State, "A buffer cannot change state after its permanent transition.");
			}

			ASSERT(buffer.RecordedUses == ownUses && (buffer.PermanentState == ETrackedResourceState::Unknown || buffer.PermanentState == use.State),
				"Permanent transition requires exclusive recorded use and a matching state.");
		}

		bool bReserved = true;
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
		if (g_bFailNextSubmissionReservation)
		{
			g_bFailNextSubmissionReservation = false;
			bReserved = false;
		}
#endif
		if (bReserved && backend::HAS_RESERVE_SUBMISSION)
		{
			bReserved = backend::ReserveSubmission (m_State->m_Backend.State);
		}
		if (!bReserved)
		{
			Error error;
			error.Code = EErrorCode::Conflict;
			inputState = ESubmissionInputState::Retained;

			return { Result<SubmissionToken>::Failure(Move(error)), disposition, inputState };
		}
		Result<void> indirectUses = trackBottomLevelSubmissionUses (*command);

		if (!indirectUses)
		{
			Error error;
			error.Code = EErrorCode::Conflict;

			return { Result<SubmissionToken>::Failure(Move(error)), disposition, ESubmissionInputState::Retained };
		}
		if (command->SubmitPrerequisite.IsValid())
		{
			Result<void> waited = QueueWaitForSubmission(command->Queue, command->SubmitPrerequisite);

			if (!waited)
			{
				DiscardCommandList(list);
				return { Result<SubmissionToken>::Failure(Move(waited.GetError())), disposition, ESubmissionInputState::Consumed };
			}
		}

		Result<uint64> submitted = backend::SubmitCommandList(
			m_State->m_Backend.State,
			command->Native,
			command->Copies.View(),
			command->TextureCopies.View(),
			command->TextureUses.View(),
			command->BufferUses.View(),
			command->AccelUses.View(),
			command->PipelineUses.View(),
			command->GraphicsPipelineUses.View(),
			command->RayPipelineUses.View(),
			command->FramebufferUses.View());
		const uint64 acceptedSerial = submitted ? submitted.Value() : 0;
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
		if (submitted && g_bFailNextAcceptedSubmit)
		{
			g_bFailNextAcceptedSubmit = false;
			submitted = Result<uint64>::Failure(EErrorCode::ExternalFailure, "Rhi.TestAcceptedSubmit", "Injected failure after native command submission.");
		}
#endif
		if (!submitted)
		{
			if (!m_State->m_bSubmissionFaulted)
			{
				m_State->m_bFirstSubmissionMayBeAccepted = acceptedSerial != 0 || (backend::HAS_WAS_SUBMISSION_ACCEPTED && backend::WasSubmissionAccepted(m_State->m_Backend.State));
			}
			recordSubmissionFault(submitted.GetError());
		}
		if (submitted)
		{
			for (const RecordedTextureCopy& copy : command->TextureCopies)
			{
				m_State->m_Textures[copy.SourceHandle.Slot].bSharedReadPrepared = false;
				m_State->m_Textures[copy.DestinationHandle.Slot].bSharedReadPrepared = false;
			}

			for (const RecordedTextureUse& use : command->TextureUses)
			{
				TextureSlot& texture = m_State->m_Textures[use.Handle.Slot];

				if (use.bPermanentTransition)
				{
					texture.PermanentState = use.State;
				}

				if (use.State != ETrackedResourceState::ShaderRead)
				{
					texture.bSharedReadPrepared = false;
				}
				else if (use.bPreparedForSharedRead || use.bPermanentTransition)
				{
					texture.bSharedReadPrepared = true;
				}
			}

			for (const RecordedBufferUse& use : command->BufferUses)
			{
				if (use.bPermanentTransition)
				{
					m_State->m_Buffers[use.Handle.Slot].PermanentState = use.State;
				}
			}

			for (const RecordedAccelBuild& build : command->AccelBuildTargets)
			{
				AccelStructSlot& target = m_State->m_AccelStructs[build.Handle.Slot];
				target.Signature = build.Signature;
				target.bAllowUpdate = build.bAllowUpdate;
				target.bAllowCompaction = build.bAllowCompaction;
				target.CompactedBytes = 0;
				target.bBuilt = true;
				if (target.BuildRevision != UINT64_MAX)
				{
					++target.BuildRevision;
				}
				target.bUnavailable = false;
				target.bQueryUnavailable = true;
				target.bPreparedBuildShape = false;
				for (rhi::AccelStructHandle const child : target.PreparedBottomReferences)
				{
					--m_State->m_AccelStructs[child.Slot].PreparedParentRefs;
				}
				target.PreparedBottomReferences.Clear();
			}

			for (OpacityMicromapHandle handle : command->OpacityMicromapBuildTargets)
			{
				m_State->m_OpacityMicromaps[handle.Slot].bBuilt = true;
			}

			for (SamplerHandle sampler : command->SamplerUses)
			{
				m_State->m_Samplers[sampler.Slot].LastUse = submitted.Value();
			}

			for (TextureViewHandle view : command->TextureViewUses)
			{
				m_State->m_TextureViews[view.Slot].LastUse = submitted.Value();
			}

			for (const RecordedDescriptorTableUse& use : command->DescriptorTableUses)
			{
				findDescriptorTableVersion(use).LastUse = submitted.Value();
			}

			for (GpuTimerHandle timer : command->TimerUses)
			{
				TimerSlot& slot = m_State->m_Timers[timer.Slot];
				slot.State = ETimerState::Submitted;
				slot.RecordingSlot = NumericLimits<uint32>::max();
				slot.Token = MakeSubmissionToken(m_State->m_Owner, command->Queue, submitted.Value());
			}

			for (const RecordedTextureUse& use : command->TextureUses)
			{
				// External acquisition changes ownership only after this submission succeeds.
				TextureSlot* const texture = findTexture(use.Handle, true);

				if (texture != nullptr && texture->bSwapChainImage)
				{
					texture->bSubmitted = true;
				}
			}
		}
		else if (acceptedSerial != 0 || (backend::HAS_WAS_SUBMISSION_ACCEPTED && backend::WasSubmissionAccepted(m_State->m_Backend.State)))
		{
			// The backend consumes the native list even on failure, but execution may
			// already have been accepted. Keep its resource references until global drain.
			for (const RecordedTextureUse& use : command->TextureUses)
			{
				m_State->m_Textures[use.Handle.Slot].bSharedReadPrepared = false;
			}

			for (const RecordedTextureCopy& copy : command->TextureCopies)
			{
				m_State->m_Textures[copy.SourceHandle.Slot].bSharedReadPrepared = false;
				m_State->m_Textures[copy.DestinationHandle.Slot].bSharedReadPrepared = false;
			}
			command->Native = nullptr;
			disposition = ESubmissionDisposition::Indeterminate;
			command->bQuarantined = true;
			if (acceptedSerial != 0)
			{
				command->QuarantineToken = MakeSubmissionToken(m_State->m_Owner, command->Queue, acceptedSerial);
			}
			command->bLive = false;
			Advance(*command);

			return { Result<SubmissionToken>::Failure(submitted.GetError()), disposition, inputState };
		}
		else
		{
			resetRecordedTimers(*command);
		}
		releaseRecordedUses(*command);
		command->Native = nullptr;
		command->Copies.Clear();
		command->TextureCopies.Clear();
		command->TextureUses.Clear();
		command->BufferUses.Clear();
		command->bHasManualBufferUses = false;
		command->AccelUses.Clear();
		command->AccelBuildTargets.Clear();
		command->OpacityMicromapUses.Clear();
		command->OpacityMicromapBuildTargets.Clear();
		command->PipelineUses.Clear();
		command->GraphicsPipelineUses.Clear();
		command->RayPipelineUses.Clear();
		command->FramebufferUses.Clear();
		command->TimerUses.Clear();
		command->SamplerUses.Clear();
		command->TextureViewUses.Clear();
		command->DescriptorTableUses.Clear();
		command->MarkerDepth = 0;
		command->bLive = false;
		Advance(*command);

		if (!command->bExhausted)
		{
			command->NextFree = m_State->m_FreeCommand;
			m_State->m_FreeCommand = list.Slot;
		}

		if (!submitted)
		{
			return { Result<SubmissionToken>::Failure(submitted.GetError()), disposition, inputState };
		}

		disposition = ESubmissionDisposition::Accepted;
		const SubmissionToken token = MakeSubmissionToken(m_State->m_Owner, command->Queue, submitted.Value());

		return { Result<SubmissionToken>::Success(token), disposition, inputState };
	}

	[[nodiscard]] bool Device::isKnownToken(SubmissionToken token) const noexcept
	{
		return token.IsValid() &&
			token.Owner == m_State->m_Owner &&
			(token.Queue == EQueue::Copy || token.Queue == EQueue::Compute || token.Queue == EQueue::Graphics) &&
			(token.Value >> 2) != 0 &&
			(token.Value & 3u) == static_cast<uint64>(token.Queue);
	}

	[[nodiscard]] Result<bool> Device::IsSubmissionComplete(SubmissionToken token) const
	{
		CallScope call(*this, false);

		ASSERT(isKnownToken(token) && (token.Value >> 2) <= backend::GetQueueLastSubmittedSerial(m_State->m_Backend.State, token.Queue),
			"Submission operations require a previously submitted token from this device.");

		return backend::IsQueueComplete(m_State->m_Backend.State, token.Queue, token.Value >> 2);
	}

	[[nodiscard]] Result<void> Device::WaitForSubmission(SubmissionToken token, uint32 timeoutMilliseconds)
	{
		ROX_PROFILE_SCOPE("RHI", "Wait");
		CallScope call(*this, false);

		ASSERT(isKnownToken(token) && (token.Value >> 2) <= backend::GetQueueLastSubmittedSerial(m_State->m_Backend.State, token.Queue),
			"Submission operations require a previously submitted token from this device.");

		return backend::Wait(m_State->m_Backend.State, token.Queue, token.Value >> 2, timeoutMilliseconds);
	}

	[[nodiscard]] Result<void> Device::QueueWaitForSubmission(EQueue waitingQueue, SubmissionToken token)
	{
		ROX_PROFILE_SCOPE("RHI", "QueueWaitForSubmission");
		CallScope call(*this, false);

		if (m_State->m_bSubmissionFaulted)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot admit queue work after submission failure.");
		}

		ASSERT(waitingQueue == EQueue::Copy || waitingQueue == EQueue::Compute || waitingQueue == EQueue::Graphics, "Queue waits require a valid queue.");

		if ((waitingQueue == EQueue::Compute && !m_State->m_Backend.Capabilities.Features.bCompute) || (waitingQueue == EQueue::Graphics && !m_State->m_Backend.Capabilities.Features.bGraphics))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.Queue", "The waiting queue is unavailable.");
		}

		ASSERT(isKnownToken(token) && (token.Value >> 2) <= backend::GetQueueLastSubmittedSerial(m_State->m_Backend.State, token.Queue), "Queue waits require a previously submitted token from this device.");

		if (hasNativeQueueAccess(waitingQueue))
		{
			Error error;
			error.Code = EErrorCode::Conflict;

			return Result<void>::Failure(Move(error));
		}
		Result<void> waited = backend::QueueWaitForSubmission(m_State->m_Backend.State, waitingQueue, token.Queue, token.Value >> 2);
		if (!waited)
		{
			if (!m_State->m_bSubmissionFaulted)
			{
				m_State->m_bFirstSubmissionMayBeAccepted = true;
			}
			recordSubmissionFault(waited.GetError());
		}
		return waited;
	}

	[[nodiscard]] Result<void> Device::WaitIdle()
	{
		ROX_PROFILE_SCOPE("RHI", "WaitIdle");
		CallScope call(*this, false);
		flushSubmissionFault();
		if (hasAnyNativeQueueAccess())
		{
			Error error;
			error.Code = EErrorCode::Conflict;

			return Result<void>::Failure(Move(error));
		}
		Result<void> drained = backend::WaitIdle(m_State->m_Backend.State);

		if (drained || drained.GetError().DiagnosticID == "Rhi.DeviceLost")
		{
			retireGpuTimersAfterIdle();
			releaseQuarantinedCommands();
			releasePresentRetention(false);
			releaseFailedAcquisition();
			releaseFailedReadback();
			collectPreparedTextureMappings(true);
			collectAcquiredImageUses(true);
		}

		return drained;
	}

	[[nodiscard]] Result<void> Device::WaitIdle(uint32 timeoutMilliseconds)
	{
		ROX_PROFILE_SCOPE("RHI", "WaitIdle");
		CallScope call(*this, false);
		flushSubmissionFault();
		if (hasAnyNativeQueueAccess())
		{
			Error error;
			error.Code = EErrorCode::Conflict;

			return Result<void>::Failure(Move(error));
		}
		Result<void> drained = backend::WaitIdleBounded(m_State->m_Backend.State, timeoutMilliseconds);

		if (drained || drained.GetError().DiagnosticID == "Rhi.DeviceLost")
		{
			retireGpuTimersAfterIdle();
			releaseQuarantinedCommands();
			releasePresentRetention(false);
			releaseFailedAcquisition();
			releaseFailedReadback();
			collectPreparedTextureMappings(true);
			collectAcquiredImageUses(true);
		}

		return drained;
	}

	void Device::Collect() noexcept
	{
		CallScope call(*this, false);
		flushSubmissionFault();
		releaseQuarantinedCommands(true);
		releasePresentRetention(true);
		(void)collectRetiredRanges();
		collectPreparedTextureMappings(false);
		collectAcquiredImageUses(false);

		for (DescriptorTableSlot& table : m_State->m_DescriptorTables)
		{
			(void)collectDescriptorTableVersions(table);
		}
	}

	void Device::retireGpuTimersAfterIdle() noexcept
	{
		for (rhi::TimerSlot& timer : m_State->m_Timers)
		{
			if (timer.bLive && timer.State == ETimerState::Submitted)
			{
				timer.bRetired = true;
			}
		}
	}

	void Device::releaseQuarantinedCommands(bool bRequireCompletion) noexcept
	{
		for (uint32 index = 0; index < m_State->m_Commands.Size(); ++index)
		{
			CommandSlot& command = m_State->m_Commands[index];

			if (!command.bQuarantined)
			{
				continue;
			}
			if (bRequireCompletion)
			{
				if (!command.QuarantineToken.IsValid())
				{
					continue;
				}
				const Result<bool> completed = backend::IsQueueComplete( m_State->m_Backend.State, command.QuarantineToken.Queue, command.QuarantineToken.Value >> 2);
				if (!completed || !completed.Value())
				{
					continue;
				}
			}
			resetRecordedTimers(command);
			releaseRecordedUses(command);
			command.Copies.Clear();
			command.TextureCopies.Clear();
			command.TextureUses.Clear();
			command.BufferUses.Clear();
			command.AccelUses.Clear();
			command.AccelBuildTargets.Clear();
			command.OpacityMicromapUses.Clear();
			command.OpacityMicromapBuildTargets.Clear();
			command.PipelineUses.Clear();
			command.GraphicsPipelineUses.Clear();
			command.RayPipelineUses.Clear();
			command.FramebufferUses.Clear();
			command.TimerUses.Clear();
			command.SamplerUses.Clear();
			command.TextureViewUses.Clear();
			command.DescriptorTableUses.Clear();
			command.bHasManualBufferUses = false;
			command.MarkerDepth = 0;
			command.bQuarantined = false;
			command.QuarantineToken = {};

			if (!command.bExhausted)
			{
				command.NextFree = m_State->m_FreeCommand;
				m_State->m_FreeCommand = index;
			}
		}
	}

	void Device::recordSubmissionFault(const Error& error) noexcept
	{
		if (m_State->m_bSubmissionFaulted)
		{
			return;
		}
		m_State->m_bSubmissionFaulted = true;
		// Other caller-owned lists can no longer record on this device. Their native
		// objects and recorded references remain owned until explicit discard.
		for (CommandSlot& command : m_State->m_Commands)
		{
			if (command.bLive)
			{
				command.bPoisoned = true;
			}
		}
		m_State->m_FirstSubmissionFailure = error.Code;
		const auto copy = [](auto& destination, StringView source)
			{
				const usize count = Min(source.Size(), sizeof(destination) - 1);
				for (usize index = 0; index < count; ++index)
				{
					destination[index] = source[index];
				}
				destination[count] = '\0';
			};
		copy(m_State->m_FirstSubmissionDiagnostic, error.DiagnosticID.View());
		copy(m_State->m_FirstSubmissionMessage, error.Message.View());
	}

	void Device::flushSubmissionFault() noexcept
	{
		if (!m_State->m_bSubmissionFaulted || m_State->m_bSubmissionFaultLogged)
		{
			return;
		}
		m_State->m_bSubmissionFaultLogged = true;
		char fallback[128]{};
		const int fallbackLength = FormatText(fallback, sizeof(fallback),
			"GPU submission failed (code %u, native %d, stage %u, waits %u, possible acceptance %u).",
			static_cast<unsigned int>(m_State->m_FirstSubmissionFailure),
			static_cast<int>(m_State->m_FirstSubmissionNativeCode),
			static_cast<unsigned int>(m_State->m_FirstSubmissionFailureStage),
			static_cast<unsigned int>(m_State->m_FirstSubmissionQueueWaitCount),
			m_State->m_bFirstSubmissionMayBeAccepted ? 1u : 0u);
		LOG_ERROR(m_State->m_FirstSubmissionDiagnostic[0] != '\0'
			? StringView(m_State->m_FirstSubmissionDiagnostic, CStringLength(m_State->m_FirstSubmissionDiagnostic))
			: StringView("Rhi.Submission"),
			m_State->m_FirstSubmissionMessage[0] != '\0'
			? StringView(m_State->m_FirstSubmissionMessage, CStringLength(m_State->m_FirstSubmissionMessage))
			: StringView(fallback, fallbackLength > 0
				? Min(static_cast<usize>(fallbackLength), sizeof(fallback) - 1)
				: 0));
	}

	void Device::releasePresentRetention(bool bRequireCompletion) noexcept
	{
		if (!m_State->m_RetainedPresentSource.IsValid())
		{
			return;
		}
		if (bRequireCompletion)
		{
			if (!m_State->m_RetainedPresentToken.IsValid())
			{
				return;
			}
			Result<bool> completed = backend::IsQueueComplete(m_State->m_Backend.State, m_State->m_RetainedPresentToken.Queue, m_State->m_RetainedPresentToken.Value >> 2);
			if (!completed || !completed.Value())
			{
				return;
			}
		}
		TextureSlot& source = m_State->m_Textures[m_State->m_RetainedPresentSource.Slot];
		ASSERT(source.PreparedReferences != 0, "Presentation retains its source until retirement.");
		--source.PreparedReferences;
		m_State->m_RetainedPresentSource = {};
		m_State->m_RetainedPresentToken = {};
	}

	void Device::releaseFailedAcquisition() noexcept
	{
		for (SwapChainSlot& swapChain : m_State->m_SwapChains)
		{
			if (!swapChain.FailedAcquisitionImage.IsValid())
			{
				continue;
			}
			if constexpr (backend::HAS_DISCARD_ACQUIRED_SWAP_CHAIN_IMAGE)
			{
				(void)backend::DiscardAcquiredSwapChainImage(m_State->m_Backend.State, swapChain.Native);
			}
			TextureSlot& image = m_State->m_Textures[swapChain.FailedAcquisitionImage.Slot];
			ASSERT(image.PreparedReferences != 0, "Failed acquisition retention is released once.");
			--image.PreparedReferences;
			swapChain.FailedAcquisitionImage = {};
		}
		recycleClosedAcquiredImages();
	}

	void Device::releaseFailedReadback() noexcept
	{
		if (m_State->m_RetainedReadBuffer.IsValid())
		{
			BufferSlot& buffer = m_State->m_Buffers[m_State->m_RetainedReadBuffer.Slot];
			ASSERT(buffer.RecordedUses != 0, "Failed readback retains its buffer once.");
			--buffer.RecordedUses;
			m_State->m_RetainedReadBuffer = {};
		}
		if (m_State->m_RetainedReadTexture.IsValid())
		{
			TextureSlot& texture = m_State->m_Textures[m_State->m_RetainedReadTexture.Slot];
			ASSERT(texture.PreparedReferences != 0, "Failed readback retains its texture once.");
			--texture.PreparedReferences;
			m_State->m_RetainedReadTexture = {};
		}
	}

	void Device::releaseRecordedUses(CommandSlot& command) noexcept
	{
		if (command.PreparedGroup)
		{
			rhi::PreparedRecordingList& list = command.PreparedGroup->Lists[command.PreparedIndex];
			for (rhi::PreparedRecordingPhase const& phase : list.Phases)
			{
				for (rhi::PreparedAccelStructAccessDesc const& access : phase.AccelStructs)
				{
					ASSERT(m_State->m_AccelStructs[access.AccelStruct.Slot].PreparedReferences != 0, "Prepared AS owners are released once.");
					--m_State->m_AccelStructs[access.AccelStruct.Slot].PreparedReferences;
				}
				for (rhi::PreparedRecordingAccelQuery const& query : phase.AccelCompactionQueries)
				{
					backend::CancelPreparedAccelQuery(m_State->m_Backend.State, m_State->m_AccelStructs[query.Description.Source.Slot].Native, query.QueryGeneration);
				}

				for (rhi::PreparedBinding const& binding : phase.Bindings)
				{
					if (binding.ShaderTable.IsValid())
					{
						ASSERT(m_State->m_ShaderTables[binding.ShaderTable.Slot].PreparedReferences != 0, "Prepared tables are released once.");
						--m_State->m_ShaderTables[binding.ShaderTable.Slot].PreparedReferences;
					}
					for (uint32 set = 0; set < binding.SetCount; ++set)
					{
						rhi::BindingSetSlot& owner = m_State->m_BindingSets[binding.Sets[set].Slot];
						ASSERT(owner.PreparedReferences != 0, "Prepared bindings are released once.");
						--owner.PreparedReferences;
					}
				}
			}
			if (list.Disposition == ESubmissionDisposition::NotAccepted)
			{
				for (rhi::PreparedRecordingPhase const& phase : list.Phases)
				{
					for (rhi::PreparedTextureAccessDesc const& access : phase.Textures)
					{
						rhi::TextureSlot& image = m_State->m_Textures[access.Texture.Slot];
						if (image.bSwapChainImage)
						{
							ASSERT(image.AcquiredUnsubmittedUses != 0, "Unsubmitted acquired uses are released once.");
							--image.AcquiredUnsubmittedUses;
						}
					}
				}
			}
			list.bConsumed = true;
		}

		for (const RecordedTextureCopy& copy : command.TextureCopies)
		{
			m_State->m_Textures[copy.SourceHandle.Slot].RecordingSlot = NumericLimits<uint32>::max();
			m_State->m_Textures[copy.DestinationHandle.Slot].RecordingSlot = NumericLimits<uint32>::max();
		}

		for (const RecordedTextureUse& use : command.TextureUses)
		{
			TextureSlot& texture = m_State->m_Textures[use.Handle.Slot];
			if (command.PreparedGroup)
			{
				ASSERT(texture.PreparedReferences != 0, "A prepared texture owner is released once.");
				--texture.PreparedReferences;
				continue;
			}

			if (use.bSharedRead)
			{
				ASSERT(texture.SharedReadUses != 0, "Shared reads must be retained before release.");
				--texture.SharedReadUses;

				if (texture.SharedReadUses != 0)
				{
					continue;
				}
			}
			texture.RecordingSlot = UINT32_MAX;
		}

		for (const RecordedBufferUse& use : command.BufferUses)
		{
			--m_State->m_Buffers[use.Handle.Slot].RecordedUses;
		}

		for (const RecordedAccelUse& use : command.AccelUses)
		{
			--m_State->m_AccelStructs[use.Handle.Slot].RecordedUses;
		}

		for (OpacityMicromapHandle handle : command.OpacityMicromapUses)
		{
			--m_State->m_OpacityMicromaps[handle.Slot].RecordedUses;
		}

		for (const RecordedPipelineUse& use : command.PipelineUses)
		{
			--m_State->m_Pipelines[use.Handle.Slot].RecordedUses;
		}

		for (const RecordedGraphicsPipelineUse& use : command.GraphicsPipelineUses)
		{
			--m_State->m_GraphicsPipelines[use.Handle.Slot].RecordedUses;
		}

		for (const RecordedRayPipelineUse& use : command.RayPipelineUses)
		{
			--m_State->m_RayTracingPipelines[use.Handle.Slot].RecordedUses;
		}

		for (const RecordedFramebufferUse& use : command.FramebufferUses)
		{
			--m_State->m_Framebuffers[use.Handle.Slot].RecordedUses;
		}

		for (SamplerHandle handle : command.SamplerUses)
		{
			--m_State->m_Samplers[handle.Slot].RecordedUses;
		}

		for (TextureViewHandle handle : command.TextureViewUses)
		{
			--m_State->m_TextureViews[handle.Slot].RecordedUses;
		}

		for (const RecordedDescriptorTableUse& use : command.DescriptorTableUses)
		{
			--findDescriptorTableVersion(use).RecordedUses;
		}
		command.PreparedGroup.Reset();
	}
}
