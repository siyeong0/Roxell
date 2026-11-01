#pragma once

#include "TaskCompletion.h"

#include <Cancellation.h>
#include <TextPrimitives.hpp>
#include <Synchronization.hpp>
#include <Types.h>

#include "JobExecution.h"
#include "JobSchedulerIdentity.h"
#include <Array.hpp>
#include <Task.hpp>
#include <CoreString.hpp>
#include <TaskHandle.h>
#include <JobScheduler.hpp>
#include <SharedPtr.hpp>
#include <SmallVector.hpp>
#include <atomic>
#include <cstddef>
#include <mutex>

namespace rox::core
{
	class JobState final
	{
	public:
		void PrepareExternal() noexcept;
		void CompleteExternal() noexcept;

		static void ValidateSubmission(MemoryResource& resource, const SharedPtr<JobSchedulerIdentity>& identity,
		                               StringView name, const UniqueFunction<void(JobContext&) noexcept>& target,
		                               Span<const TaskHandle> prerequisites) noexcept;
		static TaskHandle Create(MemoryResource& resource, const SharedPtr<JobSchedulerIdentity>& identity,
		                        StringView name, UniqueFunction<void(JobContext&) noexcept>&& target,
		                        Span<const TaskHandle> prerequisites = {}, EJobPriority priority = EJobPriority::Normal,
		                        const CancellationToken& cancellation = {},
		                        EJobExecutionContext executionContext = EJobExecutionContext::AnyThread) noexcept;
		static JobState& From(const TaskHandle& handle) noexcept;

		JobState(MemoryResource& resource, const SharedPtr<JobSchedulerIdentity>& identity, StringView name,
		         UniqueFunction<void(JobContext&) noexcept>&& target, Span<const TaskHandle> prerequisites = {},
		         EJobPriority priority = EJobPriority::Normal, const CancellationToken& cancellation = {},
		         EJobExecutionContext executionContext = EJobExecutionContext::AnyThread) noexcept;
		JobState(const JobState&) = delete;
		JobState& operator=(const JobState&) = delete;

		StringView GetName() const noexcept { return m_Name.View(); }
		MemoryResource& GetResource() const noexcept { return *m_Resource; }

		bool IsComplete() const noexcept { return m_bComplete.load(core::MemoryOrderAcquire); }
		bool IsOwnedBy(const JobSchedulerIdentity& identity) const noexcept { return m_Identity.Get() == &identity; }
		bool IsReady() const noexcept;

		EJobPriority GetPriority() const noexcept { return m_Priority; }

		EJobExecutionContext GetExecutionContext() const noexcept { return m_ExecutionContext; }

		bool WasSkipped() const noexcept;
		void Execute(LinearMemoryResource& scratch, usize workerIndex) noexcept;

		void Retain() noexcept;
		void Release() noexcept;

		// Preparation is caller-confined; dependency linking and retirement require the owning queue's mutex.
		void PrepareQueueAdmission(usize sequence) noexcept;
		[[nodiscard]] bool RegisterQueueDependencies() noexcept;
		[[nodiscard]] TaskHandle TakeQueueHandle() noexcept;
		[[nodiscard]] JobState* RetireReadyDependent() noexcept;
		[[nodiscard]] usize GetSequence() const noexcept { return m_Sequence; }

	public:
		detail::TaskCompletion m_Completion;
		SharedPtr<detail::TaskRetirement> Retirement;

	private:
		enum class EQueueState
		{
			Unregistered,
			Prepared,
			Waiting,
			Dispatched,
			Retired
		};

		struct DependencyLink
		{
			JobState* Dependent = nullptr;
			DependencyLink* Next = nullptr;
		};

	private:
		MemoryResource* m_Resource;
		const EJobPriority m_Priority;
		const EJobExecutionContext m_ExecutionContext;
		CancellationToken m_Cancellation;
		bool m_bSkipped = false;
		SharedPtr<JobSchedulerIdentity> m_Identity;

		String m_Name;

		SmallVector<TaskHandle, 4> m_Prerequisites;
		SmallVector<DependencyLink, 4> m_DependencyLinks;
		DependencyLink* m_Dependents = nullptr;
		usize m_RemainingPrerequisites = 0;
		usize m_Sequence = 0;
		EQueueState m_QueueState = EQueueState::Unregistered;

		UniqueFunction<void(JobContext&) noexcept> m_Target;
		Atomic<bool> m_bComplete = false;
		Atomic<usize> m_ReferenceCount = 1;
	};
}
