#include "JobState.h"
#include <Synchronization.hpp>
#include <StringView.hpp>
#include <Types.h>
#include <MemoryOperations.hpp>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include <Cancellation.h>
#include <Assertion.h>
#include <UniquePtr.hpp>
#include <Utf8.h>
#include <Trace.h>

#include <limits>

namespace rox::core
{
	namespace
	{
		MemoryResource& validateInputs(MemoryResource& resource, const SharedPtr<JobSchedulerIdentity>& identity,
		                               StringView name, const UniqueFunction<void(JobContext&) noexcept>& target,
		                               Span<const TaskHandle> prerequisites) noexcept
		{
			ASSERT(resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Job scheduler resource must support concurrent access.");
			ASSERT(static_cast<bool>(identity), "Job state requires a live scheduler identity.");
			ASSERT(identity.GetResource()->GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Job scheduler resource must support concurrent access.");
			ASSERT(static_cast<bool>(target), "Job submission requires a callable target.");
			ASSERT(target.GetResource()->GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Job callable resource must support concurrent access.");
			ASSERT(IsValidUtf8(name), "Job name must be well-formed UTF-8.");

			for (const TaskHandle& prerequisite : prerequisites)
			{
				ASSERT(JobState::From(prerequisite).IsOwnedBy(*identity), "Job prerequisite must belong to this scheduler instance.");
			}

			return resource;
		}

		SmallVector<TaskHandle, 4> copyPrerequisites(MemoryResource& resource, Span<const TaskHandle> prerequisites) noexcept
		{
			SmallVector<TaskHandle, 4> result(resource);
			result.ReserveRequired(prerequisites.Size());

			for (const TaskHandle& prerequisite : prerequisites)
			{
				result.AddRequired(prerequisite);
			}

			return result;
		}
	}

	void JobState::ValidateSubmission(MemoryResource& resource, const SharedPtr<JobSchedulerIdentity>& identity,
	                                  StringView name, const UniqueFunction<void(JobContext&) noexcept>& target,
	                                  Span<const TaskHandle> prerequisites) noexcept
	{
		validateInputs(resource, identity, name, target, prerequisites);
	}

	TaskHandle JobState::Create(MemoryResource& resource, const SharedPtr<JobSchedulerIdentity>& identity,
	                           StringView name, UniqueFunction<void(JobContext&) noexcept>&& target,
	                           Span<const TaskHandle> prerequisites, EJobPriority priority,
	                           const CancellationToken& cancellation, EJobExecutionContext executionContext) noexcept
	{
		ASSERT(priority >= EJobPriority::Low && priority <= EJobPriority::High, "Job priority must be a supported value.");
		ASSERT(executionContext == EJobExecutionContext::AnyThread || executionContext == EJobExecutionContext::MainThread, "Job execution context must be a supported value.");
		ASSERT(static_cast<bool>(identity), "Job state requires a live scheduler identity.");

		return TaskHandle(rox::core::ConstructAt(static_cast<JobState*>(identity->Acquire(resource)), resource, identity,
		                                   name, rox::core::Move(target), prerequisites, priority, cancellation,
		                                   executionContext));
	}

	JobState& JobState::From(const TaskHandle& handle) noexcept
	{
		ASSERT(handle.IsValid(), "Job handle must contain completion state.");
		return *handle.m_State;
	}

	void JobState::Retain() noexcept
	{
		const usize previous = m_ReferenceCount.fetch_add(1, rox::core::MemoryOrderRelaxed);
		ASSERT(previous != 0, "Job completion state must retain a live reference.");
		ASSERT(previous != rox::core::NumericLimits<rox::core::usize>::max(), "Job completion reference count must not overflow.");
	}

	void JobState::Release() noexcept
	{
		const usize previous = m_ReferenceCount.fetch_sub(1, rox::core::MemoryOrderAcqRel);
		ASSERT(previous != 0, "Job completion state must retain a live reference.");

		if (previous == 1)
		{
			// Keep the pool alive while destruction releases the state's identity reference.
			SharedPtr<JobSchedulerIdentity> identity = m_Identity;
			MemoryResource* const resource = m_Resource;
			rox::core::DestroyAt(this);
			identity->Recycle(this, *resource);
		}
	}

	void JobState::PrepareExternal() noexcept
	{
		ASSERT(m_QueueState == EQueueState::Unregistered, "External completion enters once.");

		m_QueueState = EQueueState::Dispatched;
	}

	void JobState::CompleteExternal() noexcept
	{
		ASSERT(!IsComplete(), "External completion publishes once.");

		m_Target.Reset();
		m_bComplete.store(true, MemoryOrderRelease);
	}

	void JobState::PrepareQueueAdmission(const rox::core::usize sequence) noexcept
	{
		ASSERT(m_QueueState == EQueueState::Unregistered && !IsComplete(), "A job enters its queue exactly once before execution.");

		m_DependencyLinks.ResizeRequired(m_Prerequisites.Size());
		m_Sequence = sequence;
		m_QueueState = EQueueState::Prepared;
	}

	bool JobState::RegisterQueueDependencies() noexcept
	{
		ASSERT(m_QueueState == EQueueState::Prepared && m_RemainingPrerequisites == 0, "Dependency linking requires a freshly prepared queue admission.");

		for (rox::core::usize index = 0; index < m_Prerequisites.Size(); ++index)
		{
			JobState& prerequisite = From(m_Prerequisites[index]);
			if (prerequisite.IsComplete())
			{
				continue;
			}

			DependencyLink& link = m_DependencyLinks[index];
			link.Dependent = this;
			link.Next = prerequisite.m_Dependents;
			prerequisite.m_Dependents = &link;
			++m_RemainingPrerequisites;
		}

		Retain();
		m_QueueState = EQueueState::Waiting;

		return m_RemainingPrerequisites == 0;
	}

	TaskHandle JobState::TakeQueueHandle() noexcept
	{
		ASSERT(m_QueueState == EQueueState::Waiting && m_RemainingPrerequisites == 0 && !IsComplete(), "Only ready queued work transfers its retained reference to execution.");

		m_QueueState = EQueueState::Dispatched;

		return TaskHandle(this);
	}

	JobState* JobState::RetireReadyDependent() noexcept
	{
		ASSERT(m_QueueState == EQueueState::Dispatched && IsComplete(), "Only completed dispatched jobs retire dependent prerequisite links once.");

		while (m_Dependents != nullptr)
		{
			DependencyLink* const link = m_Dependents;
			m_Dependents = link->Next;
			JobState& dependent = *link->Dependent;
			ASSERT(dependent.m_QueueState == EQueueState::Waiting && dependent.m_RemainingPrerequisites != 0, "Dependency notification requires a pending queued prerequisite.");

			--dependent.m_RemainingPrerequisites;
			if (dependent.m_RemainingPrerequisites == 0)
			{
				return &dependent;
			}
		}

		m_QueueState = EQueueState::Retired;

		return nullptr;
	}

	JobState::JobState(MemoryResource& resource, const SharedPtr<JobSchedulerIdentity>& identity, StringView name,
	                   UniqueFunction<void(JobContext&) noexcept>&& target, Span<const TaskHandle> prerequisites,
	                   EJobPriority priority, const CancellationToken& cancellation,
	                   EJobExecutionContext executionContext) noexcept
	    : m_Resource(&validateInputs(resource, identity, name, target, prerequisites))
	    , m_Priority(priority)
	    , m_ExecutionContext(executionContext)
	    , m_Cancellation(cancellation)
	    , m_Identity(identity)
	    , m_Name(name, resource)
	    , m_Prerequisites(copyPrerequisites(resource, prerequisites))
	    , m_DependencyLinks(resource)
	    , m_Target(rox::core::Move(target))
	{
	}

	bool JobState::IsReady() const noexcept
	{
		for (const TaskHandle& prerequisite : m_Prerequisites)
		{
			if (!prerequisite.IsComplete())
			{
				return false;
			}
		}

		return true;
	}

	void JobState::Execute(LinearMemoryResource& scratch, rox::core::usize workerIndex) noexcept
	{
		detail::CleanupScope cleanup;
		ASSERT(!IsComplete(), "Job execution requires incomplete state.");
		ASSERT(IsReady(), "Job execution requires completed prerequisites.");

		{
			const StringView name = m_Name.View();
			ROX_PROFILE_SCOPE("Job", (rox::core::StringView{name.Data(), name.Size()}));
			JobExecutionScope scope(scratch, workerIndex, m_Cancellation, m_ExecutionContext);
			m_bSkipped = m_Cancellation.IsCancellationRequested();

			if (m_bSkipped)
			{
				TraceCounter("Job.Skipped", {name.Data(), name.Size()}, 1);
			}

			scope.ExecuteTarget(m_Target, m_bSkipped);
			m_Prerequisites.Clear();
		}

		m_bComplete.store(true, rox::core::MemoryOrderRelease);
	}

	bool JobState::WasSkipped() const noexcept
	{
		ASSERT(IsComplete(), "Job skip observation requires completed execution.");
		return m_bSkipped;
	}

}
