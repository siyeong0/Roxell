#include <Task.hpp>
#include <ThreadPrimitives.hpp>
#include "TaskSignal.h"

namespace rox::core::detail
{
	TaskRetirementToken::TaskRetirementToken() noexcept = default;
	TaskRetirementToken::~TaskRetirementToken() noexcept = default;

	void TaskRetirementToken::Initialize(MemoryResource& resource) noexcept
	{
		ASSERT(!m_State, "Group retirement initializes once.");
		m_State = MakeSharedRequired<TaskRetirement>(resource);
		m_State->Pending = m_State;
	}

	void TaskRetirementToken::DeferRelease() noexcept
	{
		m_State->DeferRelease();
	}

	void RunTaskCleanup(FunctionRef<void() noexcept> action) noexcept
	{
		CleanupScope cleanup;
		action();
	}

	TaskState::TaskState(TaskHandle signal) noexcept
	    : m_Signal(Move(signal))
	{
	}

	bool TaskState::IsConsumed() const noexcept
	{
		return m_Signal.m_Signal->bConsumed.load(MemoryOrderAcquire);
	}

	bool TaskState::IsReady() const noexcept
	{
		return m_Signal.m_Signal->bReady.load(MemoryOrderAcquire);
	}

	void TaskState::ValidateRelease() const noexcept
	{
		ASSERT(!m_Signal.m_Signal->bTaskClaimed.load(MemoryOrderAcquire) || IsReady(), "TaskSource must complete its issued Task before destruction or replacement.");
	}

	void TaskState::ClaimTask() noexcept
	{
		bool bExpected = false;
		const bool bTaskClaimed = m_Signal.m_Signal->bTaskClaimed.compare_exchange_strong(bExpected, true, MemoryOrderAcqRel, MemoryOrderAcquire);
		ASSERT(bTaskClaimed, "TaskSource may create only one Task.");
	}

	bool TaskState::TryClaimCompletion() noexcept
	{
		bool bExpected = false;
		return m_Signal.m_Signal->bCompletionClaimed.compare_exchange_strong(bExpected, true, MemoryOrderAcqRel, MemoryOrderAcquire);
	}

	void TaskState::Publish() noexcept
	{
		TaskSignal& signal = *m_Signal.m_Signal;
		signal.bReady.store(true, MemoryOrderRelease);
		signal.Publish();
		signal.ResultCompletion.Publish();
	}

	void TaskState::ClaimConsumption() noexcept
	{
		ASSERT(IsReady(), "Task result is not ready.");
		bool bExpected = false;
		const bool bConsumptionClaimed = m_Signal.m_Signal->bConsumed.compare_exchange_strong(bExpected, true, MemoryOrderAcqRel, MemoryOrderAcquire);
		ASSERT(bConsumptionClaimed, "Task result may be consumed only once.");
	}

	void TaskState::SetRetirement(const TaskRetirementToken& retirement) noexcept
	{
		TaskSignal& signal = *m_Signal.m_Signal;
		ASSERT(retirement.m_State && !signal.Retirement && !signal.bTaskClaimed.load(MemoryOrderAcquire) &&
		       !signal.bCompletionClaimed.load(MemoryOrderAcquire),
		       "Task retirement must be attached once before issuing the consumer.");
		signal.Retirement = retirement.m_State;
	}

	bool TaskState::IsRetired() const noexcept
	{
		const TaskSignal& signal = *m_Signal.m_Signal;
		return signal.IsRetired() && signal.ResultCompletion.IsRetired() && (!signal.Retirement || signal.Retirement.UseCount() == 1);
	}

	void TaskState::ValidateWait() noexcept
	{
		ASSERT(!CleanupScope::IsActive(), "Retirement waits cannot run inside notification callbacks.");
	}

	void TaskState::WaitForRetirement() const noexcept
	{
		ValidateWait();
		const TaskSignal& signal = *m_Signal.m_Signal;
		signal.WaitForRetirement();
		signal.ResultCompletion.WaitForRetirement();
		if (signal.Retirement)
		{
			signal.Retirement->Wait();
		}
		while (!IsRetired())
		{
			YieldThread();
		}
	}

	void TaskState::Observe(UniqueFunction<void() noexcept> callback) const noexcept
	{
		m_Signal.m_Signal->ResultCompletion.Observe(Move(callback));
	}

	void TaskState::Detach() const noexcept
	{
		m_Signal.m_Signal->ResultCompletion.Detach();
	}

	TaskHandle TaskState::GetHandle() const noexcept
	{
		return m_Signal;
	}
}
