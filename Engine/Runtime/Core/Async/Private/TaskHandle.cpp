#include <TaskHandle.h>
#include <Assertion.h>
#include <ThreadPrimitives.hpp>
#include <Utility.hpp>
#include "JobState.h"
#include "TaskSignal.h"

namespace rox::core
{
	TaskHandle::TaskHandle() noexcept = default;

	TaskHandle::TaskHandle(JobState* state) noexcept
	    : m_State(state)
	{
	}

	TaskHandle::TaskHandle(SharedPtr<detail::TaskSignal> signal) noexcept
	    : m_Signal(Move(signal))
	{
	}

	TaskHandle TaskHandle::createSignal(MemoryResource& resource) noexcept
	{
		return TaskHandle(MakeSharedRequired<detail::TaskSignal>(resource, resource));
	}

	TaskHandle TaskHandle::tryCreateSignal(MemoryResource& resource) noexcept
	{
		SharedPtr<detail::TaskSignal> signal;
		(void)MakeSharedInto<detail::TaskSignal>(resource, &signal, resource);
		return TaskHandle(Move(signal));
	}

	TaskHandle TaskHandle::ready() noexcept
	{
		TaskHandle handle;
		handle.m_bReady = true;
		return handle;
	}

	TaskHandle::~TaskHandle() noexcept
	{
		Reset();
	}

	TaskHandle::TaskHandle(const TaskHandle& other) noexcept
	    : m_State(other.m_State)
	    , m_Signal(other.m_Signal)
	    , m_bReady(other.m_bReady)
	{
		if (m_State != nullptr)
		{
			m_State->Retain();
		}
	}

	TaskHandle::TaskHandle(TaskHandle&& other) noexcept
	    : m_State(Exchange(other.m_State, nullptr))
	    , m_Signal(Move(other.m_Signal))
	    , m_bReady(Exchange(other.m_bReady, false))
	{
	}

	TaskHandle& TaskHandle::operator=(const TaskHandle& other) noexcept
	{
		if (this != &other)
		{
			TaskHandle replacement(other);
			*this = Move(replacement);
		}
		return *this;
	}

	TaskHandle& TaskHandle::operator=(TaskHandle&& other) noexcept
	{
		if (this != &other)
		{
			JobState* const released = m_State;
			SharedPtr<detail::TaskSignal> releasedSignal = Move(m_Signal);
			m_State = Exchange(other.m_State, nullptr);
			m_Signal = Move(other.m_Signal);
			m_bReady = Exchange(other.m_bReady, false);
			if (released != nullptr)
			{
				released->Release();
			}
		}
		return *this;
	}

	void TaskHandle::Reset() noexcept
	{
		JobState* const released = Exchange(m_State, nullptr);
		m_Signal.Reset();
		m_bReady = false;
		if (released != nullptr)
		{
			released->Release();
		}
	}

	bool TaskHandle::IsValid() const noexcept
	{
		return m_State != nullptr || m_Signal || m_bReady;
	}

	bool TaskHandle::IsComplete() const noexcept
	{
		ASSERT(IsValid(), "Completion requires a valid task handle.");
		return m_State != nullptr ? m_State->IsComplete() : (m_bReady || m_Signal->IsComplete());
	}

	bool TaskHandle::IsRetired() const noexcept
	{
		ASSERT(IsValid(), "Retirement requires a valid task handle.");
		if (m_State == nullptr)
		{
			return m_bReady || m_Signal->IsRetired();
		}
		if (!m_State->m_Completion.IsRetired())
		{
			return false;
		}
		for (const SharedPtr<detail::TaskRetirement>* fence = &m_State->Retirement; *fence; fence = &(*fence)->Next)
		{
			if (!(*fence)->Finished.load(MemoryOrderAcquire) || fence->UseCount() != 1)
			{
				return false;
			}
		}
		return true;
	}

	void TaskHandle::WaitForRetirement() const noexcept
	{
		ASSERT(IsValid() && !detail::CleanupScope::IsActive(), "Retirement waits require a valid handle outside callbacks.");
		if (m_State != nullptr)
		{
			m_State->m_Completion.WaitForRetirement();
			for (const SharedPtr<detail::TaskRetirement>* fence = &m_State->Retirement; *fence; fence = &(*fence)->Next)
			{
				(*fence)->Wait();
			}
		}
		else if (m_Signal)
		{
			m_Signal->WaitForRetirement();
		}
		while (!IsRetired())
		{
			YieldThread();
		}
	}

	bool TaskHandle::WasSkipped() const noexcept
	{
		ASSERT(IsComplete(), "Skip observation requires completion.");
		return m_State != nullptr && m_State->WasSkipped();
	}

	StringView TaskHandle::GetName() const noexcept
	{
		ASSERT(IsValid(), "Name observation requires a valid handle.");
		return m_State != nullptr ? m_State->GetName() : StringView{};
	}

	void TaskHandle::observeCompletion(UniqueFunction<void() noexcept> callback) const noexcept
	{
		ASSERT(m_State != nullptr, "Scheduled result notification requires a job.");
		m_State->m_Completion.Observe(Move(callback));
	}

	void TaskHandle::disconnectCompletion() const noexcept
	{
		ASSERT(m_State != nullptr, "Scheduled result notification requires a job.");
		m_State->m_Completion.Detach();
	}

	UniqueFunction<void() noexcept> TaskHandle::observeSignal(MemoryResource& resource, UniqueFunction<void() noexcept> callback) const noexcept
	{
		ASSERT(m_Signal, "External dependency requires a completion signal.");
		SharedPtr<detail::TaskSignal::Observer> observer = m_Signal->Observe(Move(callback));
		return UniqueFunction<void() noexcept>(resource, [signal = m_Signal, observer = Move(observer)]() noexcept
		{
			signal->Detach(observer);
		});
	}
}
