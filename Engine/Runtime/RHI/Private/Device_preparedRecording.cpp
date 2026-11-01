#include "DeviceState.h"
#include "PreparedProgramCache.h"

#include <Trace.h>

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] CommandSlot* Device::findPreparedControlCommand(CommandListHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Commands.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		rhi::CommandSlot& command = m_State->m_Commands[handle.Slot];
		ASSERT(command.bLive && command.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &command;
	}

	[[nodiscard]] Result<void> Device::preparedFailure(EErrorCode code) noexcept
	{
		Error error;
		error.Code = code;

		return Result<void>::Failure(Move(error));
	}

	[[nodiscard]] PreparedRecordingList* Device::preparedList(CommandSlot* command) noexcept
	{
		return command != nullptr && command->PreparedGroup ? &command->PreparedGroup->Lists[command->PreparedIndex] : nullptr;
	}

	[[nodiscard]] PreparedRecordingPhase* Device::preparedPhase(CommandSlot* command) noexcept
	{
		PreparedRecordingList* list = preparedList(command);

		return list != nullptr && command->PreparedGroup->bRecording && !list->bFinished && list->FailureCode == EErrorCode::None &&
			   list->ActivePhase < list->Phases.Size()
			? &list->Phases[list->ActivePhase]
			: nullptr;
	}

	[[nodiscard]] Result<void> Device::preparedEmission(CommandSlot& command, Result<void> result) noexcept
	{
		if (!result)
		{
			rhi::PreparedRecordingList& list = command.PreparedGroup->Lists[command.PreparedIndex];
			list.FailureCode = result.GetError().Code;
			list.Diagnostic = 4;

			return preparedFailure(list.FailureCode);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::BeginPreparedRecording(Span<const CommandListHandle> lists, EPreparedRecordingMode mode)
	{
		CallScope call(*this, true, true);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot record after submission failure.");
		}
		ASSERT(!m_State->m_bParallelRecording && m_State->m_ActiveCalls == 1 && !lists.IsEmpty() && lists.Data() != nullptr && mode <= EPreparedRecordingMode::Parallel,
			"Beginning prepared recording requires an idle device and a nonempty list group.");
		CommandSlot* first = findCommand(lists[0]);
		ASSERT(first != nullptr && first->PreparedGroup && !first->PreparedGroup->bRecorded && first->PreparedGroup->Lists.Size() == lists.Size(),
			"Beginning prepared recording requires the complete unrecorded group.");
		for (uint32 index = 0; index < lists.Size(); ++index)
		{
			CommandSlot* command = findPreparedControlCommand(lists[index]);
			ASSERT(first->PreparedGroup->Lists[index].Handle == lists[index] &&
				command != nullptr &&
				command->PreparedGroup.Get() == first->PreparedGroup.Get() &&
				!first->PreparedGroup->Lists[index].bConsumed,
				"Beginning prepared recording requires the unconsumed group in its declared order.");
		}

		if (mode == EPreparedRecordingMode::Parallel && m_State->m_Resource.GetThreadSafety() != EMemoryResourceThreadSafety::ThreadSafe)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		m_State->m_ParallelStatistics = {};
		m_State->m_bParallelRecording = true;
		first->PreparedGroup->bRecording = true;
		first->PreparedGroup->Mode = mode;
		first->PreparedGroup->RecordingThread = CurrentExecutionThreadId();

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::BeginPreparedAccessPhase(CommandListHandle handle, uint32 phase)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingList* list = preparedList(command);
		ASSERT(list != nullptr && command->PreparedGroup->bRecording && !list->bFinished && list->FailureCode == EErrorCode::None && !command->bRendering,
			"Prepared phase transitions require a live unpoisoned recording outside rendering.");
		ASSERT(phase < list->Phases.Size() && phase == (list->ActivePhase == UINT32_MAX ? 0u : list->ActivePhase + 1), "Prepared access phases visit every captured phase exactly once in order.");
		ASSERT(list->ActivePhase == UINT32_MAX || preparedAccelOperationsComplete(list->Phases[list->ActivePhase]), "Prepared phase transitions complete every promised acceleration operation.");
		if (list->ActivePhase != UINT32_MAX)
		{
			Result<void> ended = recordPreparedGpuTimer(*command, list->Phases[list->ActivePhase], false);
			if (!ended)
			{
				return ended;
			}
		}

		Result<void> result =
			recordNative([&] { return backend::RecordPreparedPhase(m_State->m_Backend.State, command->Native, list->NativePhases[phase]); });
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}
		list->ActivePhase = phase;

		return recordPreparedGpuTimer(*command, list->Phases[phase], true);
	}

	[[nodiscard]] PreparedBindingRef Device::GetPreparedBindings(CommandListHandle handle, uint32 phase, uint32 slot)
	{
		CallScope call(*this, true, true);
		PreparedRecordingList* list = preparedList(findCommand(handle));
		ASSERT(list != nullptr && phase < list->Phases.Size() && slot < list->Phases[phase].Bindings.Size(), "Prepared selectors require a valid list, phase, and slot.");

		return PreparedBindingRef{ handle, phase, slot };
	}

	[[nodiscard]] PreparedAttachmentRef Device::GetPreparedAttachment(CommandListHandle handle, uint32 phase, uint32 slot)
	{
		CallScope call(*this, true, true);
		PreparedRecordingList* list = preparedList(findCommand(handle));
		ASSERT(list != nullptr && phase < list->Phases.Size() && slot < list->Phases[phase].Attachments.Size(), "Prepared selectors require a valid list, phase, and slot.");

		return PreparedAttachmentRef{ handle, phase, slot };
	}

	[[nodiscard]] PreparedResolveRef Device::GetPreparedResolve(CommandListHandle handle, uint32 phase, uint32 slot) const
	{
		CallScope call(*this, true, true);
		const CommandSlot* command = findCommand(handle);
		rhi::PreparedRecordingList const* list = command != nullptr && command->PreparedGroup ? &command->PreparedGroup->Lists[command->PreparedIndex] : nullptr;
		ASSERT(list != nullptr && phase < list->Phases.Size() && slot < list->Phases[phase].Resolves.Size(), "Prepared selectors require a valid list, phase, and slot.");

		return PreparedResolveRef{ handle, phase, slot };
	}

	[[nodiscard]] PreparedTextureBufferCopyRef Device::GetPreparedTextureBufferCopy(CommandListHandle handle, uint32 phase, uint32 slot) const
	{
		CallScope call(*this, true, true);
		const CommandSlot* command = findCommand(handle);
		rhi::PreparedRecordingList const* list = command != nullptr && command->PreparedGroup ? &command->PreparedGroup->Lists[command->PreparedIndex] : nullptr;
		ASSERT(list != nullptr && phase < list->Phases.Size() && slot < list->Phases[phase].TextureBufferCopies.Size(), "Prepared selectors require a valid list, phase, and slot.");

		return PreparedTextureBufferCopyRef{ handle, phase, slot };
	}

	[[nodiscard]] PreparedIndirectRef Device::GetPreparedIndirect(CommandListHandle handle, uint32 phase, uint32 slot) const
	{
		CallScope call(*this, true, true);
		const CommandSlot* command = findCommand(handle);
		rhi::PreparedRecordingList const* list = command != nullptr && command->PreparedGroup ? &command->PreparedGroup->Lists[command->PreparedIndex] : nullptr;
		ASSERT(list != nullptr && phase < list->Phases.Size() && slot < list->Phases[phase].Indirects.Size(), "Prepared selectors require a valid list, phase, and slot.");

		return PreparedIndirectRef{ handle, phase, slot };
	}

	[[nodiscard]] Result<void> Device::FinishPreparedCommandList(CommandListHandle handle)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingList* list = preparedList(command);
		ASSERT(list != nullptr && command->PreparedGroup->bRecording && !list->bFinished && !command->bRendering, "Prepared finish requires an unfinished recording outside rendering.");
		if (list->FailureCode != EErrorCode::None)
		{
			return preparedFailure(list->FailureCode);
		}
		ASSERT(list->ActivePhase < list->Phases.Size() && list->ActivePhase + 1 == list->Phases.Size(), "Prepared finish follows the last captured access phase.");
		ASSERT(preparedAccelOperationsComplete(list->Phases[list->ActivePhase]), "Prepared finish emits every promised acceleration operation.");
		Result<void> ended = recordPreparedGpuTimer(*command, list->Phases[list->ActivePhase], false);
		if (!ended)
		{
			return ended;
		}

		Result<void> result =
			recordNative([&] { return backend::FinishPreparedCommandList(m_State->m_Backend.State, command->Native); });
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}
		list->bFinished = true;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<ParallelRecordingStatistics> Device::EndPreparedRecording()
	{
		CallScope call(*this, true, true);
		Error error;
		ASSERT(m_State->m_bParallelRecording && m_State->m_ActiveCalls == 1, "Ending prepared recording requires an active session and joined workers.");
		EErrorCode failed = EErrorCode::None;
		bool bFoundGroup = false;
		for (rhi::CommandSlot& command : m_State->m_Commands)
		{
			if (!command.PreparedGroup || !command.PreparedGroup->bRecording)
			{
				continue;
			}
			bFoundGroup = true;
			for (rhi::PreparedRecordingList const& list : command.PreparedGroup->Lists)
			{
				if (list.FailureCode != EErrorCode::None)
				{
					if (failed == EErrorCode::None)
					{
						failed = list.FailureCode;
					}
				}
				else if (list.FailureCode == EErrorCode::None)
				{
					ASSERT(list.bFinished, "End prepared recording requires every healthy list to be finished.");
				}
			}
		}
		ASSERT(bFoundGroup, "An active prepared recording session owns a recording group.");

		for (rhi::CommandSlot& command : m_State->m_Commands)
		{
			if (command.PreparedGroup && command.PreparedGroup->bRecording)
			{
				command.PreparedGroup->bRecording = false;
				command.PreparedGroup->bRecorded = true;
				command.PreparedGroup->bSubmissionFailed = failed != EErrorCode::None;
			}
		}
		m_State->m_bParallelRecording = false;
		if (failed != EErrorCode::None)
		{
			error.Code = failed;

			return Result<ParallelRecordingStatistics>::Failure(Move(error));
		}

		return Result<ParallelRecordingStatistics>::Success(m_State->m_ParallelStatistics);
	}

	void Device::CancelPreparedRecording()
	{
		CallScope call(*this, true, true);
		ASSERT(m_State->m_bParallelRecording && m_State->m_ActiveCalls == 1, "Cancelling prepared recording requires an active session and joined workers.");
		bool bFoundGroup = false;
		for (rhi::CommandSlot& command : m_State->m_Commands)
		{
			if (command.PreparedGroup && command.PreparedGroup->bRecording)
			{
				bFoundGroup = true;
				command.PreparedGroup->bRecording = false;
				command.PreparedGroup->bRecorded = true;
				command.PreparedGroup->bSubmissionFailed = true;
			}
		}
		ASSERT(bFoundGroup, "An active prepared recording session owns a recording group.");
		m_State->m_bParallelRecording = false;
	}
}
