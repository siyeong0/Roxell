#pragma once

#include "TaskCompletion.h"

#include <TaskHandle.h>

namespace rox::core::detail
{
	// Independent dependencies share completion without sharing result consumption.
	class TaskSignal final
	{
	public:
		struct Observer final
		{
			TaskCompletion Completion;
			Observer* Previous = nullptr;
			SharedPtr<Observer> Next;
			bool bLinked = false;
		};

	public:
		explicit TaskSignal(MemoryResource& resource) noexcept;
		SharedPtr<Observer> Observe(UniqueFunction<void() noexcept> callback) noexcept;
		void Detach(const SharedPtr<Observer>& observer) noexcept;
		void Publish() noexcept;
		bool IsComplete() const noexcept;
		bool IsRetired() const noexcept;
		void WaitForRetirement() const noexcept;

	public:
		TaskCompletion ResultCompletion;
		SharedPtr<TaskRetirement> Retirement;
		Atomic<bool> bCompletionClaimed = false;
		Atomic<bool> bReady = false;
		Atomic<bool> bConsumed = false;
		Atomic<bool> bTaskClaimed = false;

	private:
		MemoryResource* m_Resource;
		Mutex m_Mutex;
		SharedPtr<Observer> m_Head;
		Atomic<bool> m_bComplete = false;
		Atomic<bool> m_bPublished = false;
		Atomic<usize> m_Active = 0;
	};
}
