#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::QueueWaitForPreparedSubmission(CommandListHandle handle, SubmissionToken token)
	{
		CallScope call(*this, false, true);
		if (m_State->m_bSubmissionFaulted)
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}
		CommandSlot* command = findCommand(handle);
		PreparedRecordingList* list = preparedList(command);
		ASSERT(list != nullptr && !list->bConsumed, "Prepared queue waits require a live prepared command list.");
		if (list->FailureCode != EErrorCode::None || command->PreparedGroup->bSubmissionFailed)
		{
			return preparedFailure(list->FailureCode != EErrorCode::None ? list->FailureCode : EErrorCode::ExternalFailure);
		}
		ASSERT(command->PreparedGroup->bRecorded && list->bFinished && command->PreparedIndex == command->PreparedGroup->NextSubmit && isKnownToken(token),
			"Prepared queue waits require finished recording, submission order, and a known prerequisite token.");

		if (hasNativeQueueAccess(command->Queue))
		{
			return preparedFailure(EErrorCode::Conflict);
		}

		Result<void> waited = backend::QueueWaitForPreparedSubmission(m_State->m_Backend.State, command->Queue, token.Queue, token.Value >> 2);
		if (!waited)
		{
			recordSubmissionFault(waited.GetError());
		}

		return waited;
	}

	[[nodiscard]] SubmitOutcome Device::SubmitPrepared(CommandListHandle handle, Span<const SubmissionToken> prerequisites)
	{
		CallScope call(*this, false, true);
		const NativeSubmissionOutcome result = submitPreparedWithAdmission(handle, prerequisites, nullptr);
		if (result.Disposition == ESubmissionDisposition::Accepted && result.ErrorCode == EErrorCode::None)
		{
			return { ESubmitStatus::Submitted, result.Token };
		}
		if (result.InputState == ESubmissionInputState::Retained && result.ErrorCode == EErrorCode::Conflict)
		{
			return { ESubmitStatus::Busy, {} };
		}
		if (result.InputState == ESubmissionInputState::Retained)
		{
			DiscardCommandList(handle);
		}
		Error error;
		error.Code = result.ErrorCode;
		recordSubmissionFault(error);

		return { ESubmitStatus::Failed, {} };
	}

	[[nodiscard]] NativeSubmissionOutcome Device::submitPreparedWithAdmission(
		CommandListHandle handle,
		Span<const SubmissionToken> prerequisites,
		const NativeQueueAccessHandle* admission)
	{
		CallScope call(*this, false, true);
		NativeSubmissionOutcome outcome;
		if (m_State->m_bSubmissionFaulted)
		{
			ASSERT(handle.IsValid() &&
				handle.Owner == m_State->m_Owner &&
				handle.Slot < m_State->m_Commands.Size() &&
				m_State->m_Commands[handle.Slot].bLive &&
				m_State->m_Commands[handle.Slot].Generation == handle.Generation &&
				m_State->m_Commands[handle.Slot].PreparedGroup,
				"Prepared submission requires a live prepared command list.");
			DiscardCommandList(handle);
			outcome.InputState = ESubmissionInputState::Consumed;
			outcome.ErrorCode = m_State->m_FirstSubmissionFailure;

			return outcome;
		}
		CommandSlot* command = findCommand(handle);
		PreparedRecordingList* list = preparedList(command);
		ASSERT(list != nullptr && !list->bConsumed, "Prepared submission requires a live prepared command list.");
		ASSERT(command->PreparedGroup->bRecorded &&
			list->bFinished &&
			list->FailureCode == EErrorCode::None &&
			! command->PreparedGroup->bSubmissionFailed &&
			command->PreparedIndex == command->PreparedGroup->NextSubmit,
			"Prepared submission requires a finished list in submission order.");

		if (hasNativeQueueAccess(command->Queue) && (admission == nullptr || !ownsNativeQueueAccess(*admission, command->Queue)))
		{
			outcome.ErrorCode = EErrorCode::Conflict;

			return outcome;
		}
		SharedPtr<PreparedRecordingGroup> group = command->PreparedGroup;
		for (const rhi::SubmissionToken prerequisite : prerequisites)
		{
			ASSERT(isKnownToken(prerequisite) && (prerequisite.Value >> 2) <= backend::GetQueueLastSubmittedSerial(m_State->m_Backend.State, prerequisite.Queue),
				"Prepared prerequisites must name submitted work on this device.");
		}
		NativePreparedSubmissionWait waits[3]{};
		uint64 required[3]{};
		for (const rhi::SubmissionToken prerequisite : prerequisites)
		{
			uint64& serial = required[static_cast<uint32>(prerequisite.Queue)];
			serial = Max(serial, prerequisite.Value >> 2);
		}
		for (uint32 index = 0; index < list->PredecessorCount; ++index)
		{
			const uint32 predecessorIndex = group->Predecessors[list->PredecessorOffset + index];
			rhi::PreparedRecordingList const& predecessor = group->Lists[predecessorIndex];
			ASSERT(predecessor.Disposition == ESubmissionDisposition::Accepted && predecessor.AcceptedToken.IsValid(), "Prepared predecessors must have been submitted.");
			uint64& serial = required[static_cast<uint32>(predecessor.Queue)];
			serial = Max(serial, predecessor.AcceptedToken.Value >> 2);
		}
		uint32 waitCount = 0;
		for (uint32 lane = 0; lane < 3; ++lane)
		{
			if (required[lane] != 0 && static_cast<EQueue>(lane) != command->Queue)
			{
				waits[waitCount++] = { static_cast<EQueue>(lane), required[lane] };
			}
		}
		if (waitCount != 0 && backend::HAS_SUBMIT_PREPARED_COMMAND_LIST == false)
		{
			Error error;
			error.Code = EErrorCode::Unsupported;
			recordSubmissionFault(error);
			DiscardCommandList(handle);
			outcome.InputState = ESubmissionInputState::Consumed;
			outcome.ErrorCode = EErrorCode::Unsupported;

			return outcome;
		}
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
		if (g_PreparedFailureCountdown == 0 && g_bRejectPreparedFailureBeforeAcceptance)
		{
			g_PreparedFailureCountdown = NumericLimits<uint32>::max();
			Error error;
			error.Code = EErrorCode::ExternalFailure;
			recordSubmissionFault(error);
			DiscardCommandList(handle);
			outcome.InputState = ESubmissionInputState::Consumed;
			outcome.ErrorCode = EErrorCode::ExternalFailure;

			return outcome;
		}
#endif
		NativePreparedSubmissionResult native;
		Result<uint64> submitted = [&]() -> Result<uint64>
			{
				if constexpr (backend::HAS_SUBMIT_PREPARED_COMMAND_LIST)
				{
					NativePreparedSubmissionDesc preparation{ {waits, waitCount}, group->QueuePolicy == EPreparedQueuePolicy::MultiQueue };
					if (const AcquiredImagePlan::Entry* image = group->AcquiredImages.GetListImage(command->PreparedIndex))
					{
						preparation.bAcquiredFirstUse = command->PreparedIndex == image->FirstList;
						preparation.bAcquiredRelease = command->PreparedIndex == image->LastList;
					}

					native = backend::SubmitPreparedCommandList(
						m_State->m_Backend.State, command->Native, preparation, command->TextureUses.View(), command->BufferUses.View(),
						command->AccelUses.View(), command->PipelineUses.View(), command->GraphicsPipelineUses.View(),
						command->RayPipelineUses.View(), command->FramebufferUses.View());
					outcome.QueueWaitCount = native.QueueWaitCount;
					if (native.ErrorCode != EErrorCode::None)
					{
						Error error;
						error.Code = native.ErrorCode;

						return Result<uint64>::Failure(Move(error));
					}

					return Result<uint64>::Success(native.Serial);
				}

				return backend::SubmitCommandList(m_State->m_Backend.State, command->Native, {}, {}, command->TextureUses.View(),
					command->BufferUses.View(), command->AccelUses.View(), command->PipelineUses.View(),
					command->GraphicsPipelineUses.View(), command->RayPipelineUses.View(),
					command->FramebufferUses.View());
			}();
		bool bInjectedFailure = false;
		if (submitted)
		{
			outcome.Token = MakeSubmissionToken(m_State->m_Owner, command->Queue, submitted.Value());
			for (rhi::GpuTimerHandle const timerHandle : command->TimerUses)
			{
				rhi::TimerSlot& timer = m_State->m_Timers[timerHandle.Slot];
				ASSERT(timer.State == ETimerState::Recorded, "Submitted prepared timers have both timestamps.");
				timer.State = ETimerState::Submitted;
				timer.RecordingSlot = UINT32_MAX;
				timer.Token = outcome.Token;
			}
			for (rhi::SamplerHandle const sampler : command->SamplerUses)
			{
				m_State->m_Samplers[sampler.Slot].LastUse = submitted.Value();
			}
			for (rhi::TextureViewHandle const view : command->TextureViewUses)
			{
				m_State->m_TextureViews[view.Slot].LastUse = submitted.Value();
			}
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
			if (g_PreparedFailureCountdown != NumericLimits<uint32>::max())
			{
				if (g_PreparedFailureCountdown == 0)
				{
					g_PreparedFailureCountdown = NumericLimits<uint32>::max();
					bInjectedFailure = true;
				}
				else
				{
					--g_PreparedFailureCountdown;
				}
			}
			if (g_bFailNextAcceptedSubmit)
			{
				g_bFailNextAcceptedSubmit = false;
				bInjectedFailure = true;
			}
#endif
		}

		const bool bUncertain = bInjectedFailure || (!submitted && native.bAccepted) || (!submitted && backend::HAS_WAS_SUBMISSION_ACCEPTED && backend::WasSubmissionAccepted(m_State->m_Backend.State));
		outcome.Disposition = bUncertain ? ESubmissionDisposition::Indeterminate : submitted ? ESubmissionDisposition::Accepted : ESubmissionDisposition::NotAccepted;
		if (!submitted || bInjectedFailure)
		{
			outcome.ErrorCode = submitted ? EErrorCode::ExternalFailure : submitted.GetError().Code;
			if (!m_State->m_bSubmissionFaulted)
			{
				m_State->m_FirstSubmissionQueueWaitCount = native.QueueWaitCount;
				m_State->m_bFirstSubmissionMayBeAccepted = bUncertain;
				m_State->m_FirstSubmissionNativeCode = native.NativeCode;
				m_State->m_FirstSubmissionFailureStage = native.FailureStage;
			}
			Error error;
			error.Code = outcome.ErrorCode;
			recordSubmissionFault(error);
			outcome.Diagnostic = bInjectedFailure ? 1u : (native.FailureStage != 0 ? native.FailureStage : 2u);
			group->bSubmissionFailed = true;
		}

		if (outcome.Disposition != ESubmissionDisposition::NotAccepted)
		{
			if (const AcquiredImagePlan::Entry* entry = group->AcquiredImages.GetListImage(command->PreparedIndex))
			{
				rhi::TextureSlot& image = m_State->m_Textures[entry->Image.Slot];
				const bool bAdded = m_State->m_AcquiredImageUses.Add({ entry->Image, outcome.Token });
				ASSERT(bAdded, "Prepared image completion owners reserve storage before acceptance.");
				++image.AcquiredAcceptedUses;
				for (rhi::PreparedRecordingPhase const& phase : list->Phases)
				{
					for (rhi::PreparedTextureAccessDesc const& access : phase.Textures)
					{
						if (access.Texture == entry->Image)
						{
							ASSERT(image.AcquiredUnsubmittedUses != 0, "Each accepted image phase closes its CPU owner once.");
							--image.AcquiredUnsubmittedUses;
						}
					}
				}
			}
		}

		if (command->PreparedIndex + 1 == group->Lists.Size() && outcome.Disposition == ESubmissionDisposition::Accepted && !group->bSubmissionFailed)
		{
			for (const rhi::AcquiredImagePlan::Entry& image : group->AcquiredImages.GetEntries())
			{
				m_State->m_Textures[image.Image.Slot].bAcquisitionReleaseAccepted = true;
			}
		}

		list->Disposition = outcome.Disposition;
		list->AcceptedToken = outcome.Token;
		outcome.InputState = ESubmissionInputState::Consumed;
		list->bConsumed = true;
		if (submitted)
		{
			for (rhi::RecordedDescriptorTableUse const& use : command->DescriptorTableUses)
			{
				findDescriptorTableVersion(use).LastUse = submitted.Value();
			}
		}

		commitPreparedAccelMetadata(*command, outcome.Disposition, submitted ? submitted.Value() : 0);
		command->Native = nullptr;
		command->bLive = false;
		Advance(*command);
		if (bUncertain)
		{
			for (rhi::GpuTimerHandle const timerHandle : command->TimerUses)
			{
				// The quarantined list still owns this handle even when a token was returned.
				rhi::TimerSlot& timer = m_State->m_Timers[timerHandle.Slot];
				timer.State = ETimerState::Recorded;
				timer.RecordingSlot = handle.Slot;
			}
			command->bQuarantined = true;
			command->QuarantineToken = outcome.Token;

			return outcome;
		}
		if (!submitted)
		{
			resetRecordedTimers(*command);
		}
		releaseRecordedUses(*command);
		command->TimerUses.Clear();
		command->BufferUses.Clear();
		command->TextureUses.Clear();
		command->PipelineUses.Clear();
		command->GraphicsPipelineUses.Clear();
		command->RayPipelineUses.Clear();
		command->AccelUses.Clear();
		command->FramebufferUses.Clear();
		command->SamplerUses.Clear();
		command->TextureViewUses.Clear();
		command->DescriptorTableUses.Clear();
		++group->NextSubmit;
		if (!command->bExhausted)
		{
			command->NextFree = m_State->m_FreeCommand;
			m_State->m_FreeCommand = handle.Slot;
		}

		return outcome;
	}

	NativeSubmissionOutcome Device::submitPreparedManaged(CommandListHandle handle, Span<const SubmissionToken> prerequisites)
	{
		return submitPreparedWithAdmission(handle, prerequisites, nullptr);
	}
}
