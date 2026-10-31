#pragma once

#include <SharedPtr.hpp>
#include <Synchronization.hpp>
#include <UniqueFunction.hpp>

namespace rox::core::detail
{
	struct DeferredCleanup final
	{
		void (*Run)(void*) noexcept = nullptr;
		void* Context = nullptr;
		DeferredCleanup* Next = nullptr;
	};

	class CleanupScope final
	{
	public:
		CleanupScope() noexcept;
		~CleanupScope() noexcept;
		CleanupScope(const CleanupScope&) = delete;
		CleanupScope& operator=(const CleanupScope&) = delete;
		static void Defer(DeferredCleanup& item) noexcept;
		static bool IsActive() noexcept;

	private:
		static thread_local CleanupScope* s_Current;
		CleanupScope* m_Previous;
		DeferredCleanup* m_Head = nullptr;
	};

	struct TaskRetirement final
	{
		void Finish() noexcept;
		void Wait() const noexcept;
		void DeferRelease() noexcept;

		SharedPtr<TaskRetirement> Pending;
		SharedPtr<TaskRetirement> Next;
		Atomic<bool> Finished = false;
		DeferredCleanup Cleanup;
	};

	// One detachable notification per result consumer or dependency subscription.
	class TaskCompletion final
	{
	private:
		using Callback = UniqueFunction<void() noexcept>;

	public:
		void Observe(Callback callback) noexcept;
		bool IsRetired() const noexcept;
		void WaitForRetirement() const noexcept;
		void Publish() noexcept;
		void Detach() noexcept;

	private:
		struct DispatchScope final
		{
			explicit DispatchScope(TaskCompletion* owner) noexcept;
			~DispatchScope() noexcept;
			TaskCompletion* Owner;
			DispatchScope* Previous;
		};

		static Callback* detached() noexcept;
		static Callback* completed() noexcept;
		static void dispatch(Callback* callback) noexcept;

	private:
		static thread_local DispatchScope* s_Dispatch;
		Atomic<Callback*> m_Observer = nullptr;
		Atomic<bool> m_bObserved = false;
		Atomic<bool> m_bDispatching = false;
		Atomic<bool> m_bPublished = false;
	};

}
