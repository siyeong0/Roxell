#include <ThreadPrimitives.hpp>
#include <JobScheduler.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include "JobSchedulerState.h"
#include "ExternalConnection.h"
#include <Assertion.h>
#include <Log.h>
#include <Utf8.h>
#include <limits>

namespace rox::core
{
	namespace
	{
		void executeMainThread(JobSchedulerState& state, const TaskHandle& job) noexcept
		{
			JobState::From(job).Execute(state.Scratch->GetResource(), JobContext::INLINE_WORKER_INDEX);
			state.Queue->Finish(job);
		}

		void waitOnMainThread(JobSchedulerState& state, const TaskHandle* until = nullptr) noexcept
		{
			for (;;)
			{
				TaskHandle ready = state.Queue->WaitTakeMainThread(until, !state.Workers);

				if (!ready.IsValid())
				{
					return;
				}

				executeMainThread(state, ready);
			}
		}
	} // namespace

	JobScheduler::JobScheduler(MemoryResource& resource, IThreadBackend& backend) noexcept
	    : m_Resource(&resource)
	    , m_Backend(&backend)
	{
		ASSERT(resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Job scheduler resource must support concurrent access.");
	}

	JobScheduler::~JobScheduler() noexcept
	{
		ASSERT(!m_State, "Job scheduler must be shut down before destruction.");
	}

	ThreadStartResult JobScheduler::Start(const JobSchedulerConfig& config) noexcept
	{
		ASSERT(CurrentJobExecution() == nullptr, "Job scheduler start is forbidden from job execution or target destruction.");
		ASSERT(!m_State, "Job scheduler start requires empty ownership.");
		ASSERT(config.WorkerScratchBytes > 0 && config.QueueCapacity > 0 && config.QueueCapacity <= rox::core::NumericLimits<rox::core::usize>::max() / sizeof(TaskHandle), "Job scheduler configuration requires positive representable storage capacities.");

		if (config.WorkerCount > 0)
		{
			JobWorkers::ValidateConfiguration(*m_Resource, config.WorkerCount, config.WorkerScratchBytes);
		}

		UniquePtr<JobSchedulerState> state = MakeUniqueRequired<JobSchedulerState>(*m_Resource, *m_Resource, *m_Backend, config);

		if (state->Workers)
		{
			const ThreadStartResult started = state->Workers->Start();

			if (started.Status != EThreadStartStatus::Started)
			{
				state.Reset();
				LOG_WARNING("Core.JobScheduler", "Native job scheduler startup failed.");

				return started;
			}
		}

		m_State = rox::core::Move(state);

		if (m_State->Workers)
		{
			LOG_INFO("Core.JobScheduler", "Started native job scheduler.");
		}
		else
		{
			LOG_INFO("Core.JobScheduler", "Started synchronous job scheduler.");
		}

		return {EThreadStartStatus::Started, 0};
	}

	void JobScheduler::Shutdown() noexcept
	{
		ASSERT(CurrentJobExecution() == nullptr, "Job waits and shutdown are forbidden from job execution or target destruction.");

		if (!m_State)
		{
			return;
		}

		ASSERT(m_Backend->CurrentThreadId() == m_State->OwnerId, "Job scheduler shutdown requires the initialization thread.");

		m_State->bClosingExternal.store(true, MemoryOrderRelease);
		retireExternal(true);
		Drain();
		retireExternal(true);
		if (m_State->Workers)
		{
			m_State->Workers->Join();
		}

		if (m_State->Queue && !m_State->Workers)
		{
			m_State->Queue->DrainAndStop();
		}

		m_State.Reset();
		LOG_INFO("Core.JobScheduler", "Stopped job scheduler.");
	}

	bool JobScheduler::IsRunning() const noexcept
	{
		ASSERT(!m_State || m_Backend->CurrentThreadId() == m_State->OwnerId, "Job scheduler lifecycle requires the initialization thread.");

		return static_cast<bool>(m_State);
	}

	JobSchedulerState& JobScheduler::requireRunning() const noexcept
	{
		ASSERT(m_State, "Job scheduler operation requires running ownership.");
		ASSERT(m_State->Workers || m_Backend->CurrentThreadId() == m_State->OwnerId, "Synchronous job scheduling requires the initialization thread.");

		return *m_State;
	}

	void JobScheduler::validateSubmission(StringView name, Span<const TaskHandle> prerequisites,
	                                      const JobOptions& options) const noexcept
	{
		ASSERT(options.Priority >= EJobPriority::Low && options.Priority <= EJobPriority::High, "Job priority must be a supported value.");
		ASSERT(options.ExecutionContext == EJobExecutionContext::AnyThread || options.ExecutionContext == EJobExecutionContext::MainThread, "Job execution context must be a supported value.");

		const JobExecutionState* const execution = CurrentJobExecution();
		ASSERT(execution == nullptr || !execution->bIsDestroyingTarget, "Job target destruction cannot submit work or enter ParallelFor.");

		JobSchedulerState& state = requireRunning();
		ASSERT(IsValidUtf8(name), "Job name must be well-formed UTF-8.");

		for (const TaskHandle& prerequisite : prerequisites)
		{
			ASSERT(prerequisite.IsValid(), "Prerequisites require valid completion handles.");
			ASSERT(prerequisite.m_State == nullptr || JobState::From(prerequisite).IsOwnedBy(*state.Identity),
			       "Scheduled prerequisites must belong to this scheduler run.");
		}
	}

	TaskHandle JobScheduler::submitOwned(StringView name, UniqueFunction<void(JobContext&) noexcept>&& target, Span<const TaskHandle> prerequisites, const JobOptions& options) noexcept
	{
		validateSubmission(name, prerequisites, options);
		JobSchedulerState& state = requireRunning();

		if (state.Queue)
		{
			TaskHandle accepted;
			if (!state.Queue->TrySubmit(&accepted, name, rox::core::Move(target), prerequisites, options.Priority, options.Cancellation, options.ExecutionContext))
			{
				return {};
			}

			pumpInlineReady();

			return accepted;
		}

		TaskHandle accepted = JobState::Create(*m_Resource, state.Identity, name, Move(target), prerequisites,
		                                      options.Priority, options.Cancellation, options.ExecutionContext);
		JobState::From(accepted).Execute(state.Scratch->GetResource(), JobContext::INLINE_WORKER_INDEX);
		JobState::From(accepted).m_Completion.Publish();

		return accepted;
	}

	TaskHandle JobScheduler::submitFactory(StringView name, FunctionRef<UniqueFunction<void(JobContext&) noexcept>() noexcept> factory,
	                                      Span<const TaskHandle> prerequisites, const JobOptions& options) noexcept
	{
		validateSubmission(name, prerequisites, options);
		struct Dependency final
		{
			TaskHandle Prerequisite;
			SharedPtr<detail::ExternalConnection> Connection;
		};
		struct Dependencies final
		{
			explicit Dependencies(MemoryResource& resource) noexcept : Items(resource) {}
			Array<Dependency> Items;
		};

		SharedPtr<Dependencies> external;
		SmallVector<TaskHandle, 4> prepared(*m_Resource);
		for (const TaskHandle& prerequisite : prerequisites)
		{
			if (prerequisite.m_State != nullptr)
			{
				prepared.AddRequired(prerequisite);
			}
			else if (!prerequisite.IsComplete())
			{
				if (!external)
				{
					external = MakeSharedRequired<Dependencies>(*m_Resource, *m_Resource);
				}
				SharedPtr<detail::ExternalConnection> connection = MakeSharedRequired<detail::ExternalConnection>(*m_Resource);
				connection->Disconnect = UniqueFunction<void() noexcept>(*m_Resource, []() noexcept {});
				connection->Gate = createExternal();
				connection->Complete = externalCompletion(connection->Gate);
				prepared.AddRequired(connection->Gate);
				external->Items.AddRequired(Dependency{prerequisite, Move(connection)});
			}
		}
		if (!external)
		{
			return submitPrepared(name, factory, {prepared.Data(), prepared.Size()}, options);
		}

		auto guardedFactory = [&]() noexcept
		{
			return UniqueFunction<void(JobContext&) noexcept>(*m_Resource,
			    [target = factory(), dependencies = external](JobContext& context) mutable noexcept
			    {
				    for (const Dependency& dependency : dependencies->Items)
				    {
					    if (dependency.Connection->bCancelled.load(MemoryOrderAcquire))
					    {
						    return;
					    }
				    }
				    target(context);
			    });
		};
		TaskHandle job = submitPrepared(name, guardedFactory, {prepared.Data(), prepared.Size()}, options);
		if (!job.IsValid())
		{
			for (const Dependency& dependency : external->Items)
			{
				dependency.Connection->Close(true);
			}
			return {};
		}

		for (const Dependency& dependency : external->Items)
		{
			detail::ExternalConnection& connection = *dependency.Connection;
			connection.Retirement = MakeSharedRequired<detail::TaskRetirement>(*m_Resource);
			connection.Retirement->Next = Move(JobState::From(job).Retirement);
			JobState::From(job).Retirement = connection.Retirement;
		}
		for (const Dependency& dependency : external->Items)
		{
			SharedPtr<detail::ExternalConnection> connection = dependency.Connection;
			connection->Disconnect = dependency.Prerequisite.observeSignal(*m_Resource, UniqueFunction<void() noexcept>(*m_Resource, [connection]() noexcept { connection->Close(false); }));
			(void)connection->Registration.Register(options.Cancellation, [](void* context) noexcept { static_cast<detail::ExternalConnection*>(context)->Close(true); }, connection.Get());
			retainExternal(Move(connection));
		}
		pumpInlineReady();
		return job;
	}

	TaskHandle JobScheduler::submitPrepared(StringView name, FunctionRef<UniqueFunction<void(JobContext&) noexcept>() noexcept> factory, Span<const TaskHandle> prerequisites, const JobOptions& options) noexcept
	{
		validateSubmission(name, prerequisites, options);
		JobSchedulerState& state = requireRunning();
		if (state.Queue)
		{
			TaskHandle accepted;
			if (!state.Queue->TrySubmitFactory(&accepted, name, factory, prerequisites, options.Priority, options.Cancellation, options.ExecutionContext))
			{
				return {};
			}

			pumpInlineReady();

			return accepted;
		}

		UniqueFunction<void(JobContext&) noexcept> target = factory();
		TaskHandle accepted = JobState::Create(*m_Resource, state.Identity, name, Move(target), prerequisites, options.Priority, options.Cancellation, options.ExecutionContext);
		JobState::From(accepted).Execute(state.Scratch->GetResource(), JobContext::INLINE_WORKER_INDEX);
		JobState::From(accepted).m_Completion.Publish();

		return accepted;
	}

	void JobScheduler::Join(const TaskHandle& handle) noexcept
	{
		Join(Span<const TaskHandle>(&handle, 1));
	}

	void JobScheduler::Join(Span<const TaskHandle> handles) noexcept
	{
		ASSERT(CurrentJobExecution() == nullptr && !detail::CleanupScope::IsActive(), "Job waits cannot run inside callbacks or target destruction.");
		JobSchedulerState& state = requireRunning();
		for (const TaskHandle& handle : handles)
		{
			ASSERT(handle.IsValid(), "Join requires valid completion handles.");
			ASSERT(handle.m_State == nullptr || JobState::From(handle).IsOwnedBy(*state.Identity), "Scheduled waits require this scheduler run.");
		}
		for (const TaskHandle& handle : handles)
		{
			if (handle.IsComplete())
			{
				continue;
			}
			TaskHandle pending = handle;
			UniqueFunction<void() noexcept> disconnect;
			if (handle.m_Signal)
			{
				pending = createExternal();
				disconnect = handle.observeSignal(*m_Resource, externalCompletion(pending));
			}
			if (m_Backend->CurrentThreadId() == state.OwnerId)
			{
				waitOnMainThread(state, &pending);
			}
			else
			{
				state.Queue->WaitForCompletion(pending);
			}
			if (disconnect)
			{
				disconnect();
			}
		}
	}

	void JobScheduler::Drain() noexcept
	{
		ASSERT(CurrentJobExecution() == nullptr, "Job waits and shutdown are forbidden from job execution or target destruction.");

		JobSchedulerState& state = requireRunning();

		if (state.Queue)
		{
			if (m_Backend->CurrentThreadId() == state.OwnerId)
			{
				waitOnMainThread(state);
			}
			else
			{
				state.Queue->Drain();
			}
		}
	}

	rox::core::usize JobScheduler::PumpMainThread(rox::core::usize maxJobs) noexcept
	{
		ASSERT(CurrentJobExecution() == nullptr, "Job waits and shutdown are forbidden from job execution or target destruction.");

		JobSchedulerState& state = requireRunning();
		ASSERT(m_Backend->CurrentThreadId() == state.OwnerId, "Main-thread job dispatch requires the initialization thread.");

		rox::core::usize executed = 0;

		if (state.Queue)
		{
			while (executed < maxJobs)
			{
				TaskHandle ready = state.Queue->TakeReady(EJobExecutionContext::MainThread);
				if (!ready.IsValid() && !state.Workers)
				{
					ready = state.Queue->TakeReady();
				}

				if (!ready.IsValid())
				{
					break;
				}

				executeMainThread(state, ready);
				++executed;
			}
		}

		return executed;
	}

	void JobScheduler::pumpInlineReady() noexcept
	{
		JobSchedulerState& state = requireRunning();
		if (state.Workers || !state.Queue)
		{
			return;
		}

		for (;;)
		{
			TaskHandle ready = state.Queue->TakeReady();
			if (!ready.IsValid())
			{
				ready = state.Queue->TakeReady(EJobExecutionContext::MainThread);
			}

			if (!ready.IsValid())
			{
				return;
			}

			executeMainThread(state, ready);
		}
	}

	TaskHandle JobScheduler::createExternal() noexcept
	{
		JobSchedulerState& state = requireRunning();
		if (!state.Queue)
		{
			state.Queue = MakeSharedRequired<JobQueue>(*m_Resource, *m_Resource, state.Identity, state.Config.QueueCapacity);
			state.Queue->Enable();
		}

		return state.Queue->CreateExternal();
	}

	UniqueFunction<void() noexcept> JobScheduler::externalCompletion(TaskHandle handle) noexcept
	{
		return UniqueFunction<void() noexcept>(*m_Resource, [queue = requireRunning().Queue, owned = Move(handle)]() noexcept
		{
			queue->CompleteExternal(owned);
		});
	}

	void detail::ExternalConnection::Close(bool cancelled) noexcept
	{
		if (bClosing.exchange(true, MemoryOrderAcqRel))
		{
			return;
		}

		bCancelled.store(cancelled, MemoryOrderRelease);
		if (cancelled)
		{
			Disconnect();
		}

		Complete();
		Complete.Reset();
		bClosed.store(true, MemoryOrderRelease);
		bClosed.notify_all();
		RequestRetirement();
	}

	void detail::ExternalConnection::Retire() noexcept
	{
		while (!bClosed.load(MemoryOrderAcquire))
		{
			bClosed.wait(false, MemoryOrderAcquire);
		}

		Registration.Reset();
		Disconnect();
		Disconnect.Reset();
		Gate.Reset();
	}

	void detail::ExternalConnection::RequestRetirement() noexcept
	{
		if (!Owner.load(MemoryOrderAcquire) || !bClosed.load(MemoryOrderAcquire))
		{
			return;
		}

		if (bRetirementClaimed.exchange(true, MemoryOrderAcqRel))
		{
			return;
		}

		Cleanup.Run = &Reclaim;
		Cleanup.Context = this;
		detail::CleanupScope::Defer(Cleanup);
	}

	void detail::ExternalConnection::Reclaim(void* context) noexcept
	{
		detail::ExternalConnection* const connection = static_cast<detail::ExternalConnection*>(context);
		JobSchedulerState& state = *connection->Owner.load(MemoryOrderAcquire);
		connection->Retire();
		// The registry lock covers final releases, so shutdown cannot pass an empty registry
		// while its allocator is still being used by reclamation.
		LockGuard lock(state.ExternalMutex);
		SharedPtr<detail::ExternalConnection>* const link = connection->Previous ? &connection->Previous->Next : &state.External;
		SharedPtr<detail::ExternalConnection> owned = Move(*link);
		*link = Move(connection->Next);
		if (*link)
		{
			(*link)->Previous = connection->Previous;
		}

		SharedPtr<detail::TaskRetirement> retirement = Move(connection->Retirement);
		connection->bRetired.store(true, MemoryOrderRelease);
		owned.Reset();
		retirement->Finish();
		retirement.Reset();
	}

	void JobScheduler::retainExternal(SharedPtr<detail::ExternalConnection> connection) noexcept
	{
		JobSchedulerState& state = requireRunning();
		{
			LockGuard lock(state.ExternalMutex);
			connection->Next = Move(state.External);
			if (connection->Next)
			{
				connection->Next->Previous = connection.Get();
			}

			state.External = connection;
			connection->Owner.store(&state, MemoryOrderRelease);
		}

		if (state.bClosingExternal.load(MemoryOrderAcquire))
		{
			connection->Close(true);
		}

		connection->RequestRetirement();
	}

	void JobScheduler::retireExternal(bool cancelPending) noexcept
	{
		if (!cancelPending)
		{
			return;
		}

		ASSERT(!detail::CleanupScope::IsActive(), "Shutdown must run outside notification callbacks.");

		JobSchedulerState& state = requireRunning();
		for (;;)
		{
			SharedPtr<detail::ExternalConnection> connection;
			{
				LockGuard lock(state.ExternalMutex);
				connection = state.External;
			}

			if (!connection)
			{
				return;
			}

			connection->Close(true);
			while (!connection->bRetired.load(MemoryOrderAcquire))
			{
				YieldThread();
			}
		}
	}

} // namespace rox::core
