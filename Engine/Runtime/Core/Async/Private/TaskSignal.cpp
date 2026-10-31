#include "TaskSignal.h"
#include <Assertion.h>
#include <Utility.hpp>

namespace rox::core::detail
{
	TaskSignal::TaskSignal(MemoryResource& resource) noexcept
	    : m_Resource(&resource)
	{
	}

	SharedPtr<TaskSignal::Observer> TaskSignal::Observe(UniqueFunction<void() noexcept> callback) noexcept
	{
		ASSERT(m_Resource->GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Observed Task storage must support concurrent access.");
		SharedPtr<Observer> observer = MakeSharedRequired<Observer>(*m_Resource);
		observer->Completion.Observe(Move(callback));
		{
			LockGuard lock(m_Mutex);
			if (!m_bComplete.load(MemoryOrderAcquire))
			{
				observer->Next = Move(m_Head);
				if (observer->Next)
				{
					observer->Next->Previous = observer.Get();
				}
				observer->bLinked = true;
				m_Head = observer;
				return observer;
			}
			m_Active.fetch_add(1, MemoryOrderAcqRel);
		}
		observer->Completion.Publish();
		m_Active.fetch_sub(1, MemoryOrderAcqRel);
		m_Active.notify_all();
		return observer;
	}

	void TaskSignal::Detach(const SharedPtr<Observer>& observer) noexcept
	{
		{
			LockGuard lock(m_Mutex);
			if (observer->bLinked)
			{
				SharedPtr<Observer>* const link = observer->Previous ? &observer->Previous->Next : &m_Head;
				*link = Move(observer->Next);
				if (*link)
				{
					(*link)->Previous = observer->Previous;
				}
				observer->Previous = nullptr;
				observer->bLinked = false;
			}
		}
		observer->Completion.Detach();
	}

	void TaskSignal::Publish() noexcept
	{
		CleanupScope cleanup;
		{
			LockGuard lock(m_Mutex);
			ASSERT(!m_bComplete.load(MemoryOrderAcquire), "A completion signal publishes once.");
			m_bComplete.store(true, MemoryOrderRelease);
		}
		for (;;)
		{
			SharedPtr<Observer> observer;
			{
				LockGuard lock(m_Mutex);
				if (!m_Head)
				{
					break;
				}
				observer = Move(m_Head);
				m_Head = Move(observer->Next);
				if (m_Head)
				{
					m_Head->Previous = nullptr;
				}
				observer->bLinked = false;
				observer->Previous = nullptr;
				m_Active.fetch_add(1, MemoryOrderAcqRel);
			}
			observer->Completion.Publish();
			m_Active.fetch_sub(1, MemoryOrderAcqRel);
			m_Active.notify_all();
		}
		m_bPublished.store(true, MemoryOrderRelease);
		m_bPublished.notify_all();
	}

	bool TaskSignal::IsComplete() const noexcept
	{
		return m_bComplete.load(MemoryOrderAcquire);
	}

	bool TaskSignal::IsRetired() const noexcept
	{
		return m_bPublished.load(MemoryOrderAcquire) && m_Active.load(MemoryOrderAcquire) == 0;
	}

	void TaskSignal::WaitForRetirement() const noexcept
	{
		ASSERT(!CleanupScope::IsActive(), "Signal retirement waits cannot run inside callbacks.");
		while (!m_bPublished.load(MemoryOrderAcquire))
		{
			m_bPublished.wait(false, MemoryOrderAcquire);
		}
		usize active = m_Active.load(MemoryOrderAcquire);
		while (active != 0)
		{
			m_Active.wait(active, MemoryOrderAcquire);
			active = m_Active.load(MemoryOrderAcquire);
		}
	}
}
