#include "JobQueue.h"
#include <Synchronization.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include <Cancellation.h>
#include "JobExecution.h"
#include <Assertion.h>
#include <algorithm>
#include <limits>

namespace rox::core
{
	namespace
	{
		MemoryResource& validateQueueInputs(MemoryResource& resource, const SharedPtr<JobSchedulerIdentity>& identity,
		                                    usize capacity) noexcept
		{
			ASSERT(resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Job scheduler resource must support concurrent access.");
			ASSERT(static_cast<bool>(identity), "Job state requires a live scheduler identity.");
			ASSERT(identity.GetResource()->GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Job scheduler resource must support concurrent access.");
			ASSERT(capacity > 0 && capacity <= NumericLimits<usize>::max() / sizeof(TaskHandle), "Job scheduler configuration requires positive representable storage capacities.");

			return resource;
		}
	}

	JobQueue::JobQueue(MemoryResource& resource, const SharedPtr<JobSchedulerIdentity>& identity, usize capacity) noexcept
	    : m_Resource(&validateQueueInputs(resource, identity, capacity))
	    , m_Identity(identity)
	    , m_AnyReady(resource)
	    , m_MainReady(resource)
	    , m_Capacity(capacity)
	{
		m_AnyReady.ReserveRequired(capacity);
		m_MainReady.ReserveRequired(capacity);
	}

	JobQueue::~JobQueue() noexcept
	{
		const bool bIdle = IsIdle();
		ASSERT(bIdle, "Job queue destruction requires idle ownership.");
	}

	bool JobQueue::TrySubmit(TaskHandle* out, StringView name, UniqueFunction<void(JobContext&) noexcept>&& target,
	                         Span<const TaskHandle> prerequisites, EJobPriority priority,
	                         const CancellationToken& cancellation, EJobExecutionContext executionContext) noexcept
	{
		JobState::ValidateSubmission(*m_Resource, m_Identity, name, target, prerequisites);
		auto factory = [&target]() noexcept { return core::Move(target); };

		return TrySubmitFactory(out, name, factory, prerequisites, priority, cancellation, executionContext);
	}

	bool JobQueue::TrySubmitFactory(TaskHandle* out, StringView name,
	                               FunctionRef<UniqueFunction<void(JobContext&) noexcept>() noexcept> factory,
	                               Span<const TaskHandle> prerequisites, EJobPriority priority,
	                               const CancellationToken& cancellation, EJobExecutionContext executionContext) noexcept
	{
		ASSERT(out != nullptr, "TrySubmit requires a valid output pointer.");
		ASSERT(priority >= EJobPriority::Low && priority <= EJobPriority::High, "Job priority must be a supported value.");
		ASSERT(executionContext == EJobExecutionContext::AnyThread || executionContext == EJobExecutionContext::MainThread, "Job execution context must be a supported value.");

		usize sequence;
		bool ready = false;
		{
			core::LockGuard lock(m_Mutex);
			ASSERT(!m_bStopped, "Job queue submission requires open admission.");

			if (m_Queued == m_Capacity)
			{
				return false;
			}

			ASSERT(m_NextSequence != NumericLimits<usize>::max(), "Job admission order must remain representable.");

			sequence = m_NextSequence++;
			++m_Queued;
		}

		UniqueFunction<void(JobContext&) noexcept> target = factory();
		JobState::ValidateSubmission(*m_Resource, m_Identity, name, target, prerequisites);
		TaskHandle accepted = JobState::Create(*m_Resource, m_Identity, name, core::Move(target), prerequisites, priority, cancellation, executionContext);
		JobState& state = JobState::From(accepted);
		state.PrepareQueueAdmission(sequence);
		{
			core::LockGuard lock(m_Mutex);

			if (state.RegisterQueueDependencies())
			{
				enqueueReadyLocked(state);
				ready = true;
			}
		}

		if (ready)
		{
			if (executionContext == EJobExecutionContext::AnyThread)
			{
				m_WorkReady.notify_one();
			}
			else
			{
				m_Changed.notify_all();
			}
		}

		(*out) = core::Move(accepted);

		return true;
	}

	TaskHandle JobQueue::TakeReady(EJobExecutionContext executionContext) noexcept
	{
		core::LockGuard lock(m_Mutex);
		return m_bEnabled && !m_bStopped ? takeReadyLocked(executionContext) : TaskHandle{};
	}

	TaskHandle JobQueue::takeReadyLocked(EJobExecutionContext executionContext) noexcept
	{
		Array<JobState*>& ready = executionContext == EJobExecutionContext::MainThread ? m_MainReady : m_AnyReady;

		if (ready.IsEmpty())
		{
			return {};
		}

		core::PopHeap(ready.begin(), ready.end(), comesAfter);
		JobState* const state = ready.Back();
		ready.PopBack();
		--m_Queued;
		++m_Active;

		return state->TakeQueueHandle();
	}

	bool JobQueue::comesAfter(const JobState* left, const JobState* right) noexcept
	{
		return left->GetPriority() != right->GetPriority() ? left->GetPriority() < right->GetPriority()
		                                                   : left->GetSequence() > right->GetSequence();
	}

	void JobQueue::enqueueReadyLocked(JobState& state) noexcept
	{
		Array<JobState*>& ready = state.GetExecutionContext() == EJobExecutionContext::MainThread ? m_MainReady : m_AnyReady;
		ready.AddRequired(&state);
		core::PushHeap(ready.begin(), ready.end(), comesAfter);
	}

	void JobQueue::Finish(const TaskHandle& handle) noexcept
	{
		ASSERT(JobState::From(handle).IsOwnedBy(*m_Identity), "Job prerequisite must belong to this scheduler instance.");

		{
			core::LockGuard lock(m_Mutex);
			ASSERT(handle.IsComplete() && m_Active > 0, "Job completion requires active execution.");

			JobState& completed = JobState::From(handle);

			while (JobState* const dependent = completed.RetireReadyDependent())
			{
				enqueueReadyLocked(*dependent);
				if (dependent->GetExecutionContext() == EJobExecutionContext::AnyThread)
				{
					m_WorkReady.notify_one();
				}
			}

			--m_Active;
		}

		m_Changed.notify_all();
		// Observers may activate this queue, so dispatch outside its mutex.
		JobState::From(handle).m_Completion.Publish();
	}

	TaskHandle JobQueue::CreateExternal() noexcept
	{
		TaskHandle handle = JobState::Create(*m_Resource, m_Identity, "external-completion", UniqueFunction<void(JobContext&) noexcept>(*m_Resource, [](JobContext&) noexcept {}));
		JobState::From(handle).PrepareExternal();
		LockGuard lock(m_Mutex);
		++m_Active;

		return handle;
	}

	void JobQueue::CompleteExternal(const TaskHandle& handle) noexcept
	{
		JobState::From(handle).CompleteExternal();
		Finish(handle);
	}

	void JobQueue::Enable() noexcept
	{
		{
			core::LockGuard lock(m_Mutex);
			ASSERT(!m_bEnabled && !m_bStopped, "Job queue enable requires parked ownership.");

			m_bEnabled = true;
		}

		m_WorkReady.notify_all();
	}

	TaskHandle JobQueue::WaitTakeReady() noexcept
	{
		ASSERT(CurrentJobExecution() == nullptr, "Job waits and shutdown are forbidden from job execution or target destruction.");

		TaskHandle result;
		core::UniqueLock lock(m_Mutex);

		while (!m_bStopped)
		{
			if (m_bEnabled)
			{
				result = takeReadyLocked();

				if (result.IsValid())
				{
					break;
				}
			}

			m_WorkReady.wait(lock);
		}

		return result;
	}

	void JobQueue::WaitForCompletion(const TaskHandle& handle) noexcept
	{
		ASSERT(CurrentJobExecution() == nullptr, "Job waits and shutdown are forbidden from job execution or target destruction.");
		ASSERT(JobState::From(handle).IsOwnedBy(*m_Identity), "Job prerequisite must belong to this scheduler instance.");

		core::UniqueLock lock(m_Mutex);
		ASSERT(m_bEnabled || handle.IsComplete(), "Job completion wait requires enabled dispatch or completed work.");

		m_Changed.wait(lock,
		               [&handle]
		               {
			               return handle.IsComplete();
		               });
	}

	void JobQueue::Drain() noexcept
	{
		ASSERT(CurrentJobExecution() == nullptr, "Job waits and shutdown are forbidden from job execution or target destruction.");

		core::UniqueLock lock(m_Mutex);
		ASSERT(m_bEnabled || ((m_Queued == 0) && m_Active == 0), "Job queue drain requires enabled dispatch or idle ownership.");

		m_Changed.wait(lock, [this] { return (m_Queued == 0) && m_Active == 0; });
	}

	TaskHandle JobQueue::WaitTakeMainThread(const TaskHandle* until, bool includeAny) noexcept
	{
		ASSERT(CurrentJobExecution() == nullptr, "Job waits and shutdown are forbidden from job execution or target destruction.");
		ASSERT(until == nullptr || JobState::From(*until).IsOwnedBy(*m_Identity), "Job prerequisite must belong to this scheduler instance.");

		core::UniqueLock lock(m_Mutex);
		ASSERT(m_bEnabled, "Main-thread job wait requires enabled dispatch.");

		for (;;)
		{
			if (until != nullptr ? until->IsComplete() : ((m_Queued == 0) && m_Active == 0))
			{
				return {};
			}

			TaskHandle ready = takeReadyLocked(EJobExecutionContext::MainThread);
			if (!ready.IsValid() && includeAny)
			{
				ready = takeReadyLocked();
			}

			if (ready.IsValid())
			{
				return ready;
			}

			m_Changed.wait(lock);
		}
	}

	void JobQueue::DrainAndStop() noexcept
	{
		ASSERT(CurrentJobExecution() == nullptr, "Job waits and shutdown are forbidden from job execution or target destruction.");

		{
			core::UniqueLock lock(m_Mutex);
			ASSERT(m_bEnabled || ((m_Queued == 0) && m_Active == 0), "Job queue drain requires enabled dispatch or idle ownership.");

			m_Changed.wait(lock, [this]{ return (m_Queued == 0) && m_Active == 0; });
			m_bStopped = true;
		}

		m_WorkReady.notify_all();
	}

	bool JobQueue::IsIdle() noexcept
	{
		core::LockGuard lock(m_Mutex);
		return (m_Queued == 0) && m_Active == 0;
	}

	bool JobQueue::IsParked() noexcept
	{
		core::LockGuard lock(m_Mutex);
		return !m_bEnabled && !m_bStopped;
	}
}
