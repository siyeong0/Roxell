#pragma once

#include <Cancellation.h>
#include <Synchronization.hpp>
#include <Types.h>
#include <FunctionRef.hpp>
#include "JobState.h"
#include <condition_variable>
#include <mutex>

namespace rox::core
{
	class JobQueue final
	{
	public:
		JobQueue(MemoryResource& resource, const SharedPtr<JobSchedulerIdentity>& identity,
		         rox::core::usize capacity) noexcept;
		~JobQueue() noexcept;
		JobQueue(const JobQueue&) = delete;
		JobQueue& operator=(const JobQueue&) = delete;

		bool TrySubmit(TaskHandle* out, StringView name, UniqueFunction<void(JobContext&) noexcept>&& target,
		               Span<const TaskHandle> prerequisites = {}, EJobPriority priority = EJobPriority::Normal,
		               const CancellationToken& cancellation = {},
		               EJobExecutionContext executionContext = EJobExecutionContext::AnyThread) noexcept;
		bool TrySubmitFactory(TaskHandle* out, StringView name,
		                      FunctionRef<UniqueFunction<void(JobContext&) noexcept>() noexcept> factory,
		                      Span<const TaskHandle> prerequisites, EJobPriority priority,
		                      const CancellationToken& cancellation, EJobExecutionContext executionContext) noexcept;

		TaskHandle TakeReady(EJobExecutionContext executionContext = EJobExecutionContext::AnyThread) noexcept;

		TaskHandle CreateExternal() noexcept;
		void CompleteExternal(const TaskHandle& handle) noexcept;

		void Enable() noexcept;

		TaskHandle WaitTakeReady() noexcept;
		void WaitForCompletion(const TaskHandle& handle) noexcept;
		TaskHandle WaitTakeMainThread(const TaskHandle* until = nullptr, bool includeAny = false) noexcept;

		void Drain() noexcept;
		void DrainAndStop() noexcept;

		void Finish(const TaskHandle& handle) noexcept;

		bool IsIdle() noexcept;
		bool IsParked() noexcept;

	private:
		TaskHandle takeReadyLocked(EJobExecutionContext executionContext = EJobExecutionContext::AnyThread) noexcept;
		void enqueueReadyLocked(JobState& state) noexcept;
		static bool comesAfter(const JobState* left, const JobState* right) noexcept;

	private:
		MemoryResource* m_Resource;
		SharedPtr<JobSchedulerIdentity> m_Identity;

		Array<JobState*> m_AnyReady;
		Array<JobState*> m_MainReady;

		const rox::core::usize m_Capacity;
		rox::core::usize m_Queued = 0;
		rox::core::usize m_NextSequence = 0;

		rox::core::Mutex m_Mutex;

		rox::core::ConditionVariable m_Changed;
		rox::core::ConditionVariable m_WorkReady;

		rox::core::usize m_Active = 0;

		bool m_bEnabled = false;
		bool m_bStopped = false;
	};
}
