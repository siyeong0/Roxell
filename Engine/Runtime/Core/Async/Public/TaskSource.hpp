#pragma once

#include <Task.hpp>

namespace rox::core
{
	namespace detail { template <typename, bool> class TaskGroup; }
	// The resource outlives source and task. Concurrent completion may use TrySetResult;
	// moves/destruction require exclusive access. Use a thread-safe resource across threads.
	// Create returns an invalid source on allocation failure; construction uses mandatory allocation.
	template <typename TValue>
	class TaskSource
	{
		template <typename, bool> friend class detail::TaskGroup;
	public:
		explicit TaskSource(MemoryResource& resource) noexcept;

		[[nodiscard]] static TaskSource Create(MemoryResource& resource) noexcept;

		TaskSource(const TaskSource&) = delete;
		TaskSource& operator=(const TaskSource&) = delete;
		TaskSource(TaskSource&&) noexcept = default;
		TaskSource& operator=(TaskSource&& other) noexcept;

		~TaskSource() noexcept;

		[[nodiscard]] bool IsValid() const noexcept;

		// Issues exactly one consumer; the source must complete it before releasing ownership.
		[[nodiscard]] Task<TValue> GetTask();
		void SetResult(Result<TValue> result);
		// Only for competing completion paths. A losing call still consumes its by-value argument.
		[[nodiscard]] bool TrySetResult(Result<TValue> result);

	private:
		void setRetirement(const detail::TaskRetirementToken& retirement) noexcept;
		using SharedState = typename Task<TValue>::SharedState;

		explicit TaskSource(SharedPtr<SharedState>&& state) noexcept;

	private:
		SharedPtr<SharedState> m_State;
	};

}

namespace rox::core
{
	template <typename TValue>
	TaskSource<TValue>::TaskSource(MemoryResource& resource) noexcept
	    : m_State(MakeSharedRequired<SharedState>(resource, TaskHandle::createSignal(resource)))
	{
	}

	template <typename TValue>
	TaskSource<TValue> TaskSource<TValue>::Create(MemoryResource& resource) noexcept
	{
		SharedPtr<SharedState> state;
		TaskHandle signal = TaskHandle::tryCreateSignal(resource);
		if (signal.IsValid())
		{
			(void)MakeSharedInto<SharedState>(resource, &state, Move(signal));
		}

		return TaskSource(core::Move(state));
	}

	template <typename TValue>
	TaskSource<TValue>& TaskSource<TValue>::operator=(TaskSource&& other) noexcept
	{
		if (this != &other)
		{
			if (m_State)
			{
				m_State->ValidateRelease();
			}

			m_State = core::Move(other.m_State);
		}

		return *this;
	}

	template <typename TValue>
	TaskSource<TValue>::~TaskSource() noexcept
	{
		if (m_State)
		{
			m_State->ValidateRelease();
		}
	}

	template <typename TValue>
	bool TaskSource<TValue>::IsValid() const noexcept
	{
		return static_cast<bool>(m_State);
	}

	template <typename TValue>
	void TaskSource<TValue>::setRetirement(const detail::TaskRetirementToken& retirement) noexcept
	{
		ASSERT(m_State, "TaskSource has no shared state.");
		m_State->SetRetirement(retirement);
	}

	template <typename TValue>
	Task<TValue> TaskSource<TValue>::GetTask()
	{
		ASSERT(m_State.Get() != nullptr, "TaskSource has no shared state.");

		m_State->ClaimTask();

		return Task<TValue>(m_State);
	}

	template <typename TValue>
	void TaskSource<TValue>::SetResult(Result<TValue> result)
	{
		ASSERT(m_State.Get() != nullptr, "TaskSource has no shared state.");

		m_State->Complete(core::Move(result));
	}

	template <typename TValue>
	bool TaskSource<TValue>::TrySetResult(Result<TValue> result)
	{
		ASSERT(m_State.Get() != nullptr, "TaskSource has no shared state.");

		return m_State->TryComplete(core::Move(result));
	}

	template <typename TValue>
	TaskSource<TValue>::TaskSource(SharedPtr<SharedState>&& state) noexcept
	    : m_State(core::Move(state))
	{
	}

}
