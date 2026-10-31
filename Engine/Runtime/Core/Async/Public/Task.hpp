#pragma once

#include <Result.hpp>
#include <TaskHandle.h>
#include <FunctionRef.hpp>
#include <TypeVocabulary.hpp>

namespace rox::core
{
	template <typename TValue> class TaskSource;
	namespace detail
	{
		struct TaskAccess;
		struct TaskRetirement;

		// Opaque support for result templates; not a caller API.
		class TaskRetirementToken final
		{
			friend class TaskState;
		public:
			TaskRetirementToken() noexcept;
			~TaskRetirementToken() noexcept;
			TaskRetirementToken(const TaskRetirementToken&) = delete;
			TaskRetirementToken& operator=(const TaskRetirementToken&) = delete;
			void Initialize(MemoryResource& resource) noexcept;
			void DeferRelease() noexcept;
		private:
			SharedPtr<TaskRetirement> m_State;
		};

		void RunTaskCleanup(FunctionRef<void() noexcept> action) noexcept;

		class TaskState
		{
		public:
			explicit TaskState(TaskHandle signal) noexcept;
			bool IsConsumed() const noexcept;
			bool IsReady() const noexcept;
			bool IsRetired() const noexcept;
			void WaitForRetirement() const noexcept;
			static void ValidateWait() noexcept;
			void ValidateRelease() const noexcept;
			void ClaimTask() noexcept;
			bool TryClaimCompletion() noexcept;
			void Publish() noexcept;
			void ClaimConsumption() noexcept;
			void SetRetirement(const TaskRetirementToken& retirement) noexcept;
			void Observe(UniqueFunction<void() noexcept> callback) const noexcept;
			void Detach() const noexcept;
			TaskHandle GetHandle() const noexcept;
		private:
			TaskHandle m_Signal;
		};
	}

	// Move-only, single-consumer result. TakeResult never waits; GetHandle does not consume.
	template <typename TValue>
	class [[nodiscard]] Task
	{
	private:
		static_assert(!rox::core::IsReference<TValue>);
		static_assert(rox::core::IsNothrowMoveConstructible<Result<TValue>>, "Task results must be nothrow move constructible so completion and consumption remain terminal.");

		friend class TaskSource<TValue>;
		friend class JobScheduler;
		friend struct detail::TaskAccess;

	public:
		explicit Task(Result<TValue> result) noexcept;
		Task(const Task&) = delete;
		Task& operator=(const Task&) = delete;
		Task(Task&& other) noexcept;
		Task& operator=(Task&& other) noexcept;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] bool IsReady() const noexcept;
		// Result readiness permits consumption. Retirement also joins internal notification/group cleanup.
		// Producer owners, this owner, returned values and unrelated work still retain their resources.
		[[nodiscard]] bool IsRetired() const noexcept;

		// Does not execute jobs or stop providers; call outside callbacks and pump required work first.
		void WaitForRetirement() const noexcept;

		[[nodiscard]] bool IsConsumed() const noexcept;

		// Nonblocking, single consumption; requires readiness and exclusive access to this Task.
		[[nodiscard]] Result<TValue> TakeResult();
		[[nodiscard]] TaskHandle GetHandle() const noexcept;

	private:
		void disconnect() const noexcept;
		auto getNotifier() const noexcept;

		class SharedState;

		explicit Task(SharedPtr<SharedState> state) noexcept;

	private:
		rox::core::Optional<Result<TValue>> m_InlineResult;
		SharedPtr<SharedState> m_State;
		TaskHandle m_Job;
		bool m_bInline = false;
	};

	template <typename TValue>
	class Task<TValue>::SharedState final : public detail::TaskState
	{
	public:
		explicit SharedState(TaskHandle signal) noexcept
		    : detail::TaskState(Move(signal))
		{
		}

		[[nodiscard]] bool TryComplete(Result<TValue> result) noexcept
		{
			if (!TryClaimCompletion())
			{
				return false;
			}
			m_Result.emplace(Move(result));
			Publish();
			return true;
		}

		void Complete(Result<TValue> result) noexcept
		{
			const bool bCompleted = TryComplete(Move(result));
			ASSERT(bCompleted, "Task state has already been completed.");
		}

		[[nodiscard]] Result<TValue> Consume() noexcept
		{
			ClaimConsumption();
			ASSERT(m_Result.has_value(), "A ready Task must contain a result.");
			return Move(*m_Result);
		}

	private:
		Optional<Result<TValue>> m_Result;
	};

	template <typename TValue>
	Task<TValue>::Task(Result<TValue> result) noexcept
	    : m_InlineResult(rox::core::Move(result))
	    , m_bInline(true)
	{
	}

	template <typename TValue>
	Task<TValue>::Task(Task&& other) noexcept
	    : m_InlineResult(rox::core::Move(other.m_InlineResult))
	    , m_State(rox::core::Move(other.m_State))
	    , m_Job(Move(other.m_Job))
	    , m_bInline(other.m_bInline)
	{
		other.m_InlineResult.reset();
		other.m_bInline = false;
	}

	template <typename TValue>
	Task<TValue>& Task<TValue>::operator=(Task&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}

		m_InlineResult.reset();

		if (other.m_InlineResult.has_value())
		{
			m_InlineResult.emplace(rox::core::Move(*other.m_InlineResult));
		}

		m_State = rox::core::Move(other.m_State);
		m_Job = Move(other.m_Job);
		m_bInline = other.m_bInline;
		other.m_InlineResult.reset();
		other.m_bInline = false;

		return *this;
	}

	template <typename TValue>
	bool Task<TValue>::IsValid() const noexcept
	{
		return m_bInline || m_State.Get() != nullptr;
	}

	template <typename TValue>
	bool Task<TValue>::IsReady() const noexcept
	{
		if (m_bInline)
		{
			return true;
		}

		ASSERT(m_State.Get() != nullptr, "Task has no shared state.");

		return m_Job.IsValid() ? m_Job.IsComplete() : m_State->IsReady();
	}

	template <typename TValue>
	bool Task<TValue>::IsRetired() const noexcept
	{
		ASSERT(IsValid(), "Task retirement requires a valid owner.");

		return m_bInline || ((!m_Job.IsValid() || m_Job.IsRetired()) && m_State->IsRetired());
	}

	template <typename TValue>
	void Task<TValue>::WaitForRetirement() const noexcept
	{
		detail::TaskState::ValidateWait();
		ASSERT(IsValid(), "Task retirement requires a valid owner.");
		if (m_Job.IsValid())
		{
			m_Job.WaitForRetirement();
		}
		if (m_State)
		{
			m_State->WaitForRetirement();
		}
	}

	template <typename TValue>
	bool Task<TValue>::IsConsumed() const noexcept
	{
		ASSERT(IsValid(), "Task consumption query requires a valid owner.");

		return m_bInline ? !m_InlineResult.has_value() : m_State->IsConsumed();
	}

	template <typename TValue>
	Result<TValue> Task<TValue>::TakeResult()
	{
		ASSERT(IsReady(), "Task result requires completed execution.");
		if (m_bInline)
		{
			ASSERT(m_InlineResult.has_value(), "Ready Task result may be consumed only once.");

			Result<TValue> result(rox::core::Move(*m_InlineResult));
			m_InlineResult.reset();

			return result;
		}

		ASSERT(m_State.Get() != nullptr, "Task has no shared state.");

		return m_State->Consume();
	}

	template <typename TValue>
	void Task<TValue>::disconnect() const noexcept
	{
		if (m_Job.IsValid())
		{
			m_Job.disconnectCompletion();
		}
		else if (m_State)
		{
			m_State->Detach();
		}
	}

	template <typename TValue>
	auto Task<TValue>::getNotifier() const noexcept
	{
		ASSERT(IsValid() && !IsConsumed(), "Task composition requires an unconsumed owner.");
		ASSERT(!m_State || m_State.GetResource()->GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Observed Task storage must support concurrent access.");

		struct Notifier final
		{
			decltype(m_State) State;
			TaskHandle Job;
			void operator()(UniqueFunction<void() noexcept> callback) const noexcept
			{
				if (Job.IsValid())
				{
					Job.observeCompletion(Move(callback));
				}
				else if (State)
				{
					State->Observe(Move(callback));
				}
				else
				{
					callback();
				}
			}

			void Detach() const noexcept
			{
				if (Job.IsValid())
				{
					Job.disconnectCompletion();
				}
				else if (State)
				{
					State->Detach();
				}
			}
		};

		return Notifier{m_State, m_Job};
	}

	template <typename TValue>
	Task<TValue>::Task(SharedPtr<SharedState> state) noexcept
	    : m_State(rox::core::Move(state))
	{
	}


	template <typename TValue>
	TaskHandle Task<TValue>::GetHandle() const noexcept
	{
		ASSERT(IsValid(), "Task handle requires a valid result owner.");

		return m_Job.IsValid() ? m_Job : (m_bInline ? TaskHandle::ready() : m_State->GetHandle());
	}

	namespace detail
	{
		struct TaskAccess final
		{
			template <typename TOwner> static void Disconnect(const TOwner& task) noexcept
			{
				task.disconnect();
			}

			template <typename TOwner> static auto Notifier(const TOwner& task) noexcept
			{
				return task.getNotifier();
			}
		};
	} // namespace detail

	template <typename TValue>
	[[nodiscard]] Task<TValue> MakeReadyTask(Result<TValue> result) noexcept
	{
		return Task<TValue>(Move(result));
	}
}
