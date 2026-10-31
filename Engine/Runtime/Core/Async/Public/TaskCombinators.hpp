#pragma once

#include <Array.hpp>
#include <TaskSource.hpp>

namespace rox::core
{
	template <typename TValue>
	struct WhenAnyResult final
	{
		usize Index;
		Result<TValue> Value;
	};

	// Collect every outcome in input order, including failures and cancellation. Empty input succeeds.
	template <typename TValue>
	[[nodiscard]] Task<Array<Result<TValue>>> WhenAll(MemoryResource& resource, Array<Task<TValue>> tasks) noexcept;

	// First observed completion wins. Already-ready inputs prefer the lowest index.
	// Losing producers are detached, not cancelled, and retain their completion obligation.
	template <typename TValue>
	[[nodiscard]] Task<WhenAnyResult<TValue>> WhenAny(MemoryResource& resource, Array<Task<TValue>> tasks) noexcept;

	namespace detail
	{
		template <typename TValue, bool bAny>
		class TaskGroup final
		{
		public:
			using Output = Conditional<bAny, WhenAnyResult<TValue>, Array<Result<TValue>>>;

			TaskGroup(MemoryResource& resource, Array<Task<TValue>> tasks) noexcept
			    : m_Resource(&resource)
			    , m_Source(InPlace, resource)
			    , m_Tasks(Move(tasks))
			    , m_Remaining(m_Tasks.Size())
			{
			}

			~TaskGroup() noexcept
			{
				m_Retirement.DeferRelease();
			}

			static Task<Output> Compose(MemoryResource& resource, Array<Task<TValue>> tasks) noexcept
			{
				Optional<Task<Output>> result;
				auto composeTask = [&]() noexcept { result.emplace(compose(resource, Move(tasks))); };
				RunTaskCleanup(composeTask);
				return Move(*result);
			}

		private:
			static Task<Output> compose(MemoryResource& resource, Array<Task<TValue>> tasks) noexcept
			{
				ASSERT(resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Task composition requires a thread-safe resource.");
				if constexpr (bAny)
				{
					ASSERT(!tasks.IsEmpty(), "WhenAny requires at least one input.");
				}
				bool bAllReady = true;
				for (const Task<TValue>& task : tasks)
				{
					ASSERT(task.IsValid() && !task.IsConsumed(), "Task composition requires unconsumed owners.");
					bAllReady = bAllReady && task.IsReady();
				}
				if constexpr (bAny)
				{
					for (usize index = 0; index < tasks.Size(); ++index)
					{
						if (tasks[index].IsReady())
						{
							return MakeReadyTask(Result<Output>::Success(Output{index, tasks[index].TakeResult()}));
						}
					}
				}
				else if (bAllReady)
				{
					Output values(resource);
					values.ReserveRequired(tasks.Size());
					for (Task<TValue>& task : tasks)
					{
						values.AddRequired(task.TakeResult());
					}
					return MakeReadyTask(Result<Output>::Success(Move(values)));
				}

				SharedPtr<TaskGroup> group = MakeSharedRequired<TaskGroup>(resource, resource, Move(tasks));
				group->m_Retirement.Initialize(resource);
				group->m_Source->setRetirement(group->m_Retirement);
				Task<Output> result = group->m_Source->GetTask();
				for (usize index = 0; index < group->m_Tasks.Size(); ++index)
				{
					auto notify = TaskAccess::Notifier(group->m_Tasks[index]);
					notify(UniqueFunction<void() noexcept>(resource, [group, index]() noexcept { group->observe(index); }));
				}
				group->arm();
				return result;
			}

		private:
			void observe(usize index) noexcept
			{
				{
					LockGuard lock(m_Mutex);
					if (m_bDone)
					{
						return;
					}
					if (index < m_Tasks.Size())
					{
						--m_Remaining;
						if (index < m_Winner)
						{
							m_Winner = index;
						}
					}
					if (!m_bArmed || (bAny ? m_Remaining == m_Tasks.Size() : m_Remaining != 0))
					{
						return;
					}
					m_bDone = true;
				}

				// Detachment may join a racing callback; never hold the group lock here.
				for (const Task<TValue>& task : m_Tasks)
				{
					TaskAccess::Disconnect(task);
				}
				Optional<Output> completed;
				if constexpr (bAny)
				{
					completed.emplace(Output{m_Winner, m_Tasks[m_Winner].TakeResult()});
				}
				else
				{
					completed.emplace(*m_Resource);
					completed->ReserveRequired(m_Tasks.Size());
					for (Task<TValue>& task : m_Tasks)
					{
						completed->AddRequired(task.TakeResult());
					}
				}
				m_Tasks.Reset();
				m_Source->SetResult(Result<Output>::Success(Move(*completed)));
				m_Source.reset();
			}

			void arm() noexcept
			{
				usize count;
				{
					LockGuard lock(m_Mutex);
					count = m_Tasks.Size();
					m_bArmed = true;
				}
				observe(count);
			}

		private:
			MemoryResource* m_Resource;
			TaskRetirementToken m_Retirement;
			Optional<TaskSource<Output>> m_Source;
			Array<Task<TValue>> m_Tasks;
			usize m_Remaining;
			usize m_Winner = static_cast<usize>(-1);
			Mutex m_Mutex;
			bool m_bArmed = false;
			bool m_bDone = false;
		};
	}

	template <typename TValue>
	Task<Array<Result<TValue>>> WhenAll(MemoryResource& resource, Array<Task<TValue>> tasks) noexcept
	{
		return detail::TaskGroup<TValue, false>::Compose(resource, Move(tasks));
	}

	template <typename TValue>
	Task<WhenAnyResult<TValue>> WhenAny(MemoryResource& resource, Array<Task<TValue>> tasks) noexcept
	{
		return detail::TaskGroup<TValue, true>::Compose(resource, Move(tasks));
	}
}
