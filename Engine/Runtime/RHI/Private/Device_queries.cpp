#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::recordPreparedGpuTimer(CommandSlot& command, PreparedRecordingPhase& phase, bool bBegin)
	{
		if (!phase.GpuTimer.IsValid())
		{
			return Result<void>::Success();
		}
		rhi::TimerSlot& timer = m_State->m_Timers[phase.GpuTimer.Slot];
		ASSERT(timer.State == (bBegin ? ETimerState::Prepared : ETimerState::Recording), "A prepared phase records its exclusive timer exactly once.");
		Result<void> recorded = recordNative(
			[&]
			{
				return bBegin ? backend::RecordGpuTimerBegin(m_State->m_Backend.State, command.Native, timer.Native)
					: backend::RecordGpuTimerEnd(m_State->m_Backend.State, command.Native, timer.Native);
			});
		if (!recorded)
		{
			return preparedEmission(command, Move(recorded));
		}
		timer.State = bBegin ? ETimerState::Recording : ETimerState::Recorded;

		return Result<void>::Success();
	}

	[[nodiscard]] const TimerSlot* Device::findGpuTimer(GpuTimerHandle handle) const noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Timers.Size(), "A nonempty handle must belong to this device and name an allocated slot.");

		const TimerSlot& slot = m_State->m_Timers[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	[[nodiscard]] TimerSlot* Device::findGpuTimer(GpuTimerHandle handle) noexcept
	{
		return AsConst(*this).findGpuTimer(handle) != nullptr ? &m_State->m_Timers[handle.Slot] : nullptr;
	}

	[[nodiscard]] Result<GpuTimerHandle> Device::CreateGpuTimer()
	{
		CallScope call(*this, false);
		const bool bNewSlot = m_State->m_FreeTimer == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Timers.Size()) : m_State->m_FreeTimer;

		if (bNewSlot && (m_State->m_Timers.Size() >= NumericLimits<uint32>::max() || !m_State->m_Timers.Add(TimerSlot{})))
		{
			return Result<GpuTimerHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a GPU timer handle slot.");
		}

		Result<void*> created = backend::CreateGpuTimer(m_State->m_Backend.State);

		if (!created)
		{
			if (bNewSlot)
			{
				m_State->m_Timers[index].NextFree = m_State->m_FreeTimer;
				m_State->m_FreeTimer = index;
			}

			return Result<GpuTimerHandle>::Failure(created.GetError());
		}
		TimerSlot& slot = m_State->m_Timers[index];

		if (!bNewSlot)
		{
			m_State->m_FreeTimer = slot.NextFree;
		}
		slot.Native = created.Value();
		slot.Token = {};
		slot.State = ETimerState::Idle;
		slot.bRetired = false;
		slot.RecordingSlot = NumericLimits<uint32>::max();
		slot.NextFree = NumericLimits<uint32>::max();
		slot.bLive = true;

		return Result<GpuTimerHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyGpuTimer(GpuTimerHandle handle)
	{
		CallScope call(*this, false);
		TimerSlot* const slot = findGpuTimer(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->State == ETimerState::Prepared || slot->State == ETimerState::Recording || slot->State == ETimerState::Recorded)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.GpuTimer", "A recording command list references the timer.");
		}

		if (slot->State == ETimerState::Submitted && !slot->bRetired)
		{
			Result<bool> complete = IsSubmissionComplete(slot->Token);

			if (!complete)
			{
				return Result<void>::Failure(complete.GetError());
			}

			if (!complete.Value())
			{
				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.GpuTimer", "The GPU timer has not completed.");
			}
		}

		backend::DestroyGpuTimer(m_State->m_Backend.State, slot->Native);
		slot->Native = nullptr;
		slot->Token = {};
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeTimer;
			m_State->m_FreeTimer = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::BeginGpuTimer(CommandListHandle list, GpuTimerHandle timer)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TimerSlot* const slot = findGpuTimer(timer);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!command->bRendering, "BeginGpuTimer requires a command list outside rendering.");
		if (slot->State != ETimerState::Idle)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.GpuTimer", "The timer is busy.");
		}

		if (!command->TimerUses.Add(timer))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track the GPU timer.");
		}

		slot->State = ETimerState::Recording;
		slot->RecordingSlot = list.Slot;
		Result<void> recorded = backend::RecordGpuTimerBegin(m_State->m_Backend.State, command->Native, slot->Native);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::EndGpuTimer(CommandListHandle list, GpuTimerHandle timer)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TimerSlot* const slot = findGpuTimer(timer);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!command->bRendering && slot->State == ETimerState::Recording && slot->RecordingSlot == list.Slot, "EndGpuTimer closes an interval started on this command list outside rendering.");

		Result<void> recorded = backend::RecordGpuTimerEnd(m_State->m_Backend.State, command->Native, slot->Native);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}
		slot->State = ETimerState::Recorded;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<bool> Device::PollGpuTimer(GpuTimerHandle timer) const
	{
		CallScope call(*this, false);
		const TimerSlot* const slot = findGpuTimer(timer);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->State != ETimerState::Submitted)
		{
			return Result<bool>::Success(false);
		}

		return IsSubmissionComplete(slot->Token);
	}

	[[nodiscard]] Result<float64> Device::ReadGpuTimerSeconds(GpuTimerHandle timer, uint32 timeoutMilliseconds)
	{
		CallScope call(*this, false);
		TimerSlot* const slot = findGpuTimer(timer);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->State == ETimerState::Submitted, "ReadGpuTimerSeconds requires a submitted timer.");

		Result<void> waited = WaitForSubmission(slot->Token, timeoutMilliseconds);

		if (!waited)
		{
			return Result<float64>::Failure(waited.GetError());
		}

		return backend::ReadGpuTimer(m_State->m_Backend.State, slot->Native);
	}

	[[nodiscard]] Result<void> Device::ResetGpuTimer(GpuTimerHandle timer)
	{
		CallScope call(*this, false);
		TimerSlot* const slot = findGpuTimer(timer);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->State == ETimerState::Prepared || slot->State == ETimerState::Recording || slot->State == ETimerState::Recorded)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.GpuTimer", "A recording command list references the timer.");
		}

		if (slot->State == ETimerState::Submitted && !slot->bRetired)
		{
			Result<bool> complete = IsSubmissionComplete(slot->Token);

			if (!complete)
			{
				return Result<void>::Failure(complete.GetError());
			}

			if (!complete.Value())
			{
				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.GpuTimer", "The GPU timer has not completed.");
			}
		}
		slot->Token = {};
		slot->State = ETimerState::Idle;
		slot->bRetired = false;

		return Result<void>::Success();
	}

	[[nodiscard]] const EventQuerySlot* Device::findEventQuery(EventQueryHandle handle) const noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_EventQueries.Size(), "A nonempty handle must belong to this device and name an allocated slot.");

		const EventQuerySlot& slot = m_State->m_EventQueries[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	[[nodiscard]] EventQuerySlot* Device::findEventQuery(EventQueryHandle handle) noexcept
	{
		return AsConst(*this).findEventQuery(handle) != nullptr ? &m_State->m_EventQueries[handle.Slot] : nullptr;
	}

	[[nodiscard]] Result<EventQueryHandle> Device::CreateEventQuery()
	{
		CallScope call(*this, false);
		const bool bNewSlot = m_State->m_FreeEventQuery == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_EventQueries.Size()) : m_State->m_FreeEventQuery;

		if (bNewSlot && (m_State->m_EventQueries.Size() >= NumericLimits<uint32>::max() || !m_State->m_EventQueries.Add(EventQuerySlot{})))
		{
			return Result<EventQueryHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate an event query handle slot.");
		}
		EventQuerySlot& slot = m_State->m_EventQueries[index];

		if (!bNewSlot)
		{
			m_State->m_FreeEventQuery = slot.NextFree;
		}
		slot.Token = {};
		slot.NextFree = NumericLimits<uint32>::max();
		slot.bLive = true;
		slot.bArmed = false;

		return Result<EventQueryHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyEventQuery(EventQueryHandle query)
	{
		CallScope call(*this, false);
		EventQuerySlot* const slot = findEventQuery(query);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");
		slot->Token = {};
		slot->bArmed = false;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeEventQuery;
			m_State->m_FreeEventQuery = query.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::SetEventQuery(EventQueryHandle query, EQueue queue)
	{
		CallScope call(*this, false);
		EventQuerySlot* const slot = findEventQuery(query);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(queue != EQueue::Graphics && queue != EQueue::Compute && queue != EQueue::Copy), "The queue kind is invalid.");

		ASSERT(!slot->bArmed, "Reset the event query before setting it again.");

		if ((queue == EQueue::Graphics && !m_State->m_Backend.Capabilities.Features.bGraphics) ||
			(queue == EQueue::Compute && !m_State->m_Backend.Capabilities.Features.bCompute) ||
			(queue == EQueue::Copy && !m_State->m_Backend.Capabilities.Features.bCopy))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.EventQuery", "The selected queue is unavailable.");
		}

		const uint64 serial = backend::GetQueueLastSubmittedSerial(m_State->m_Backend.State, queue);
		slot->Token = serial != 0 ? MakeSubmissionToken(m_State->m_Owner, queue, serial) : SubmissionToken{};
		slot->bArmed = true;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<bool> Device::PollEventQuery(EventQueryHandle query) const
	{
		CallScope call(*this, false);
		const EventQuerySlot* const slot = findEventQuery(query);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (!slot->bArmed)
		{
			return Result<bool>::Success(false);
		}

		if (!slot->Token.IsValid())
		{
			return Result<bool>::Success(true);
		}

		return IsSubmissionComplete(slot->Token);
	}

	[[nodiscard]] Result<void> Device::WaitEventQuery(EventQueryHandle query, uint32 timeoutMilliseconds)
	{
		CallScope call(*this, false);
		const EventQuerySlot* const slot = findEventQuery(query);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (!slot->bArmed || !slot->Token.IsValid())
		{
			return Result<void>::Success();
		}

		return WaitForSubmission(slot->Token, timeoutMilliseconds);
	}

	[[nodiscard]] Result<void> Device::ResetEventQuery(EventQueryHandle query)
	{
		CallScope call(*this, false);
		EventQuerySlot* const slot = findEventQuery(query);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");
		slot->Token = {};
		slot->bArmed = false;

		return Result<void>::Success();
	}
}
