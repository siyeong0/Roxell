#include "TaskCompletion.h"
#include <TaskHandle.h>
#include <Assertion.h>
#include <MemoryOperations.hpp>
#include <Utility.hpp>

namespace rox::core::detail
{
	thread_local CleanupScope* CleanupScope::s_Current = nullptr;
	thread_local TaskCompletion::DispatchScope* TaskCompletion::s_Dispatch = nullptr;

	CleanupScope::CleanupScope() noexcept
	    : m_Previous(s_Current)
	{
		s_Current = this;
	}

	CleanupScope::~CleanupScope() noexcept
	{
		s_Current = m_Previous;
		while (m_Head)
		{
			DeferredCleanup* const item = m_Head;
			m_Head = item->Next;
			item->Run(item->Context);
		}
	}

	void CleanupScope::Defer(DeferredCleanup& item) noexcept
	{
		if (!s_Current)
		{
			item.Run(item.Context);

			return;
		}

		CleanupScope* outer = s_Current;
		while (outer->m_Previous)
		{
			outer = outer->m_Previous;
		}

		item.Next = outer->m_Head;
		outer->m_Head = &item;
	}

	bool CleanupScope::IsActive() noexcept
	{
		return s_Current != nullptr;
	}

	void TaskRetirement::Finish() noexcept
	{
		Finished.store(true, MemoryOrderRelease);
		Finished.notify_all();
	}

	void TaskRetirement::Wait() const noexcept
	{
		while (!Finished.load(MemoryOrderAcquire))
		{
			Finished.wait(false, MemoryOrderAcquire);
		}
	}

	void TaskRetirement::DeferRelease() noexcept
	{
		ASSERT(CleanupScope::IsActive(), "Group retirement requires a notification cleanup scope.");

		Cleanup.Run = [](void* context) noexcept
		{
			TaskRetirement* const retirement = static_cast<TaskRetirement*>(context);
			retirement->Finish();
			retirement->Pending.Reset();
		};
		Cleanup.Context = this;
		CleanupScope::Defer(Cleanup);
	}

	void TaskCompletion::Observe(Callback callback) noexcept
	{
		CleanupScope cleanup;
		ASSERT(callback, "Task completion requires a callable observer.");
		ASSERT(!m_bObserved.exchange(true), "Task completion permits one observer per result owner.");

		if (m_Observer.load(MemoryOrderAcquire) == completed())
		{
			m_bDispatching.store(true, MemoryOrderRelease);
			DispatchScope scope(this);
			callback();
			callback.Reset();
			m_bDispatching.store(false, MemoryOrderRelease);
			m_bDispatching.notify_all();

			return;
		}

		Callback* const node = CreateObjectRequired<Callback>(*callback.GetResource(), Move(callback));
		Callback* expected = nullptr;
		if (!m_Observer.compare_exchange_strong(expected, node, MemoryOrderAcqRel, MemoryOrderAcquire))
		{
			ASSERT(expected == completed(), "Task completion observer registration is exclusive.");

			m_bDispatching.store(true, MemoryOrderRelease);
			DispatchScope scope(this);
			dispatch(node);
			m_bDispatching.store(false, MemoryOrderRelease);
			m_bDispatching.notify_all();
		}
	}

	bool TaskCompletion::IsRetired() const noexcept
	{
		return m_bPublished.load(MemoryOrderAcquire) && !m_bDispatching.load(MemoryOrderAcquire);
	}

	void TaskCompletion::WaitForRetirement() const noexcept
	{
		ASSERT(!CleanupScope::IsActive(), "Retirement waits cannot run inside callbacks.");

		while (!m_bPublished.load(MemoryOrderAcquire))
		{
			m_bPublished.wait(false, MemoryOrderAcquire);
		}
		while (m_bDispatching.load(MemoryOrderAcquire))
		{
			m_bDispatching.wait(true, MemoryOrderAcquire);
		}
	}

	void TaskCompletion::Publish() noexcept
	{
		CleanupScope cleanup;
		// Most results have no observer. Publish atomically without entering the
		// dispatch/join protocol unless a notification was actually registered.
		Callback* expected = nullptr;
		if (m_Observer.compare_exchange_strong(expected, completed(), MemoryOrderAcqRel, MemoryOrderAcquire))
		{
			m_bPublished.store(true, MemoryOrderRelease);
			m_bPublished.notify_all();

			return;
		}

		if (expected == detached())
		{
			m_bPublished.store(true, MemoryOrderRelease);
			m_bPublished.notify_all();

			return;
		}

		ASSERT(expected != completed(), "Task completion publishes once.");

		m_bDispatching.store(true, MemoryOrderRelease);
		DispatchScope scope(this);
		Callback* const observer = m_Observer.exchange(completed(), MemoryOrderAcqRel);
		ASSERT(observer != completed(), "Task completion publishes once.");

		if (observer != nullptr && observer != detached())
		{
			dispatch(observer);
		}

		m_bDispatching.store(false, MemoryOrderRelease);
		m_bDispatching.notify_all();
		m_bPublished.store(true, MemoryOrderRelease);
		m_bPublished.notify_all();
	}

	void TaskCompletion::Detach() noexcept
	{
		CleanupScope cleanup;
		Callback* const observer = m_Observer.exchange(detached(), MemoryOrderAcqRel);
		if (observer != nullptr && observer != completed() && observer != detached())
		{
			MemoryResource* const resource = observer->GetResource();
			DestroyObject(*resource, observer);
		}

		for (DispatchScope* scope = s_Dispatch; scope != nullptr; scope = scope->Previous)
		{
			if (scope->Owner == this)
			{
				return;
			}
		}
		while (m_bDispatching.load(MemoryOrderAcquire))
		{
			m_bDispatching.wait(true, MemoryOrderAcquire);
		}
	}

	TaskCompletion::DispatchScope::DispatchScope(TaskCompletion* owner) noexcept
		    : Owner(owner)
		    , Previous(s_Dispatch)
		{
			s_Dispatch = this;
		}

	TaskCompletion::DispatchScope::~DispatchScope() noexcept
		{
			s_Dispatch = Previous;
		}

	TaskCompletion::Callback* TaskCompletion::detached() noexcept
	{
		alignas(Callback) static Byte marker;

		return reinterpret_cast<Callback*>(&marker);
	}

	TaskCompletion::Callback* TaskCompletion::completed() noexcept
	{
		alignas(Callback) static Byte marker;

		return reinterpret_cast<Callback*>(&marker);
	}

	void TaskCompletion::dispatch(Callback* callback) noexcept
	{
		MemoryResource* const resource = callback->GetResource();
		(*callback)();
		DestroyObject(*resource, callback);
	}
}
