#include <ShaderBuildService.h>

#include "ShaderBuildState.h"
#include "ShaderCompilerInternal.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <ClockPrimitives.hpp>
#include <Comparison.hpp>
#include <NumericLimits.hpp>
#include <Sha256.h>
#include <ShaderCompiler.h>
#include <Synchronization.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		Hash256 executionKey(const ShaderBuildPlan& plan) noexcept
		{
			Sha256 hash;
			constexpr char domain[] = "ROX-SHADER-EXECUTION-1";
			hash.Update({reinterpret_cast<const Byte*>(domain), sizeof(domain) - 1});
			const Hash256 identity = plan.GetBuildIdentity();
			hash.Update({reinterpret_cast<const Byte*>(identity.Bytes.data()), identity.Bytes.size()});
			Byte policy[8];
			ByteWriter writer({policy, sizeof(policy)});
			writer.WriteU32LE(plan.GetRequest().Limits.MaximumDiagnostics);
			writer.WriteU32LE(plan.GetRequest().Limits.MaximumDiagnosticBytes);
			hash.Update({policy, sizeof(policy)});

			return hash.Finalize();
		}

	} // namespace

	Error ShaderBuildService::error(EErrorCode code, StringView message)
	{
		return MakeError(code, "ROX-SHADER-BUILD-SERVICE", message);
	}

	ShaderBuildService::ShaderBuildService(MemoryResource& resource) noexcept
		: m_Resource(resource)
	{
	}

	ShaderBuildService::~ShaderBuildService()
	{
		ASSERT(m_bDrained, "Shader build service requires completed drain before destruction.");
	}

	Result<void> ShaderBuildService::Start(const ShaderBuildServiceDesc& desc)
	{
		using R = Result<void>;

		ASSERT(m_bDrained, "Shader service must be stopped before startup.");
		ASSERT(m_Resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Shader workers require a thread-safe memory resource.");
		ASSERT(desc.ThreadBackend != nullptr, "Shader worker startup requires a thread backend.");

		if (desc.WorkerCount == 0 ||
			desc.WorkerCount > 32 ||
			desc.MaximumQueuedJobs == 0 ||
			desc.MaximumQueuedJobs > 1024 ||
			desc.MaximumSubscribers == 0 ||
			desc.MaximumSubscribers > 4096 ||
			desc.MaximumAdmittedSourceBytes == 0 ||
			desc.MaximumResultBytesPerJob < 4096 ||
			desc.MaximumResultBytesPerJob > NumericLimits<usize>::max() ||
			desc.MaximumResultBytesPerJob > desc.MaximumRetainedResultBytes ||
			(desc.Compiler != EShaderCompiler::Slang && desc.Compiler != EShaderCompiler::Dxc))
		{
			return R::Failure(error(EErrorCode::InvalidArgument, "Invalid shader service limits or compiler selection."));
		}

		if (desc.CompilerOptions.ShaderAbiVersion != shader::SHADER_ABI_VERSION)
		{
			return R::Failure(error(EErrorCode::Unsupported, "Unknown shader compiler output ABI."));
		}

		m_Desc = desc;
		m_State.Reset();
		Result<void> started = initialize();
		if (!started)
		{
			m_State.Reset();
		}

		return started;
	}

	Result<void> ShaderBuildService::initialize()
	{
		using R = Result<void>;

		if (!MakeSharedInto(m_Resource, &m_State, m_Resource) ||
			!MakeSharedInto(m_Resource, &m_State->Ledger, m_Desc.MaximumRetainedResultBytes) ||
			!m_State->Executions.Reserve(m_Desc.MaximumQueuedJobs + m_Desc.WorkerCount) ||
			!m_State->Threads.Resize(m_Desc.WorkerCount + 1))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		for (uint32 i = 0; i < m_State->Threads.Size(); ++i)
		{
			BuildThread& thread = m_State->Threads[i];
			thread.Entry.Context = this;
			thread.Entry.Function = i == m_Desc.WorkerCount
				? [](void* context) noexcept { static_cast<ShaderBuildService*>(context)->deadlines(); }
				: [](void* context) noexcept { static_cast<ShaderBuildService*>(context)->worker(); };
			const NativeThreadStartResult started = m_Desc.ThreadBackend->Start(thread.Entry);
			ASSERT(
				started.Result.Status != EThreadStartStatus::Started ||
				(started.Thread.Handle != nullptr && started.Thread.Id.Value != 0 && started.Result.NativeError == 0),
				"Successful shader service thread startup requires valid native ownership.");
			if (started.Result.Status != EThreadStartStatus::Started)
			{
				{
					LockGuard lock(m_State->Mutex);
					m_State->bStopWorkers = true;
					m_State->bStartReady = true;
				}

				m_State->Changed.notify_all();

				for (BuildThread& previous : m_State->Threads)
				{
					if (previous.Native.Handle != nullptr)
					{
						m_Desc.ThreadBackend->Join(previous.Native);
						previous.Native = {};
					}
				}

				return R::Failure(error(EErrorCode::ExternalFailure, "Shader service thread startup failed."));
			}

			thread.Native = started.Thread;
		}

		{
			UniqueLock lock(m_State->Mutex);
			m_State->Changed.wait(lock, [&] { return m_State->StartedWorkers == m_Desc.WorkerCount; });
			m_State->bStartReady = true;
			m_State->bStopWorkers = m_State->StartupError.Code != EErrorCode::None;
		}

		m_State->Changed.notify_all();
		if (m_State->bStopWorkers)
		{
			for (BuildThread& thread : m_State->Threads)
			{
				m_Desc.ThreadBackend->Join(thread.Native);
				thread.Native = {};
			}

			return R::Failure(Move(m_State->StartupError));
		}

		m_bDrained = false;

		return R::Success();
	}

	Result<ShaderBuildJob> ShaderBuildService::Submit(const ShaderBuildPlan& plan, const ShaderBuildJobOptions& options)
	{
		using R = Result<ShaderBuildJob>;

		if (!m_State)
		{
			return R::Failure(error(EErrorCode::Conflict, "Shader service has not started."));
		}

		ASSERT(plan.IsValid(), "Submission requires a valid prepared shader plan.");

		if (!plan.CanShareAcrossThreads() || static_cast<uint32>(options.Priority) > static_cast<uint32>(EShaderBuildPriority::High))
		{
			return R::Failure(error(EErrorCode::InvalidArgument, "Submission requires a valid thread-safe plan and priority."));
		}

		if (plan.GetKind() == EShaderBuildPlanKind::SourceModule)
		{
			return R::Failure(error(EErrorCode::Unsupported, "Shader build service jobs publish final artifacts, not module IR."));
		}

		const Hash256 key = executionKey(plan);
		UniqueLock lock(m_State->Mutex);
		if (m_State->Statistics.bAdmissionClosed)
		{
			return R::Failure(error(EErrorCode::Conflict, "Shader service admission is closed."));
		}

		if (!(plan.GetToolchainIdentity() == m_State->Toolchain))
		{
			return R::Failure(error(EErrorCode::Unsupported, "Prepared plan toolchain does not match service workers."));
		}

		Result<void> supported = ValidateProfiles(plan.GetRequest(), m_State->Profiles.View(), m_Desc.CompilerOptions.ShaderAbiVersion);
		if (!supported)
		{
			return R::Failure(Move(supported.GetError()));
		}

		if (plan.GetKind() == EShaderBuildPlanKind::LinkedProgram)
		{
			for (const ShaderTargetProfile& target : plan.GetRequest().Targets)
			{
				const auto found = FindIf(
					m_State->Profiles.begin(), m_State->Profiles.end(),
					[&](const ShaderCompilerProfile& profile)
					{
						return profile.Language == plan.GetRequest().Language && profile.Target == target && profile.bLinking &&
							profile.ShaderAbiVersion == m_Desc.CompilerOptions.ShaderAbiVersion;
					});
				if (found == m_State->Profiles.end())
				{
					return R::Failure(error(EErrorCode::Unsupported, "Service workers do not support this link profile."));
				}

				if (!found->bGenericEntrySpecialization)
				{
					for (const ShaderLinkEntrySelection& selected : plan.GetLinkRequest().EntryPoints)
					{
						if (!selected.Arguments.IsEmpty())
						{
							return R::Failure(error(EErrorCode::Unsupported, "Service workers do not support typed generic entries."));
						}
					}
				}
			}
		}

		if (m_State->Statistics.Subscribers == m_Desc.MaximumSubscribers)
		{
			return R::Failure(error(EErrorCode::Conflict, "Shader service subscriber capacity is exhausted."));
		}

		SharedPtr<BuildExecution> execution;

		for (const SharedPtr<BuildExecution>& candidate : m_State->Executions)
		{
			if (!candidate->bCancellationCommitted && candidate->ExecutionKey == key && candidate->Plan.HasSameInputs(plan))
			{
				execution = candidate;
				break;
			}
		}

		const bool bShared = static_cast<bool>(execution);
		if (!execution)
		{
			const uint64 bytes = plan.GetRetainedByteCount();
			if (m_State->Statistics.QueuedJobs == m_Desc.MaximumQueuedJobs || bytes > m_Desc.MaximumAdmittedSourceBytes - m_State->Statistics.AdmittedSourceBytes)
			{
				return R::Failure(error(EErrorCode::Conflict, "Shader service queue or source budget is exhausted."));
			}

			if (!m_State->Ledger->TryReserve(m_Desc.MaximumResultBytesPerJob))
			{
				return R::Failure(error(EErrorCode::Conflict, "Retained shader result budget is exhausted."));
			}

			SharedPtr<ShaderResultMemoryResource> resultResource;
			if (!MakeSharedInto(m_Resource, &resultResource, m_Resource, m_State->Ledger, m_Desc.MaximumResultBytesPerJob))
			{
				m_State->Ledger->Release(m_Desc.MaximumResultBytesPerJob);

				return R::Failure(MakeBuildAllocationError());
			}

			resultResource->SetAllocationLease(resultResource);
			if (!MakeSharedInto(m_Resource, &execution, plan, m_Resource) ||
				!execution->Cancellation.IsValid() ||
				!execution->Subscribers.Reserve(m_Desc.MaximumSubscribers) ||
				!MakeSharedInto(*resultResource, &execution->Result, resultResource))
			{
				resultResource->ReleaseOwner();

				return R::Failure(MakeBuildAllocationError());
			}

			execution->ExecutionKey = key;
			execution->SourceBytes = bytes;
			execution->QueuedAt = MonotonicClock::now();
		}

		Result<ShaderBuildJob> attached = ShaderBuildJob::Attach(m_State, execution, options, m_Resource);
		if (!attached)
		{
			return attached;
		}

		ShaderBuildJob job = Move(attached).TakeValue();
		if (bShared)
		{
			++m_State->Statistics.DeduplicatedSubscribers;
		}
		else
		{
			const bool bAdded = m_State->Executions.Add(execution);
			ASSERT(bAdded, "Execution storage is reserved before admission.");
			++m_State->Statistics.QueuedJobs;
			m_State->Statistics.AdmittedSourceBytes += execution->SourceBytes;
		}

		lock.unlock();
		if (options.bHasDeadline && MonotonicClock::now() >= options.Deadline)
		{
			job.Cancel();
		}

		m_State->Changed.notify_all();

		return R::Success(Move(job));
	}

	Result<void> ShaderBuildService::Drain(Milliseconds timeout)
	{
		return drain(timeout, false);
	}

	Result<void> ShaderBuildService::CancelAndDrain(Milliseconds timeout)
	{
		return drain(timeout, true);
	}

	Result<void> ShaderBuildService::drain(Milliseconds timeout, bool bCancel)
	{
		using R = Result<void>;

		if (timeout.count() < 0)
		{
			return R::Failure(error(EErrorCode::InvalidArgument, "Drain timeout cannot be negative."));
		}

		if (m_bDrained)
		{
			return R::Success();
		}

		for (const BuildThread& thread : m_State->Threads)
		{
			ASSERT(thread.Native.Id != m_Desc.ThreadBackend->CurrentThreadId(), "Shader service cannot drain from its own workers.");
		}

		const MonotonicClock::time_point now = MonotonicClock::now();
		const Milliseconds maximum = DurationCast<Milliseconds>(MonotonicClock::time_point::max() - now);
		const MonotonicClock::time_point deadline = timeout >= maximum ? MonotonicClock::time_point::max() : now + timeout;
		UniqueLock lock(m_State->Mutex);
		m_State->Statistics.bAdmissionClosed = true;
		m_State->bStopWorkers = true;
		if (bCancel)
		{
			for (;;)
			{
				SharedPtr<BuildExecution> execution;

				for (const SharedPtr<BuildExecution>& candidate : m_State->Executions)
				{
					if (!candidate->bCancellationCommitted)
					{
						execution = candidate;
						break;
					}
				}

				if (!execution)
				{
					break;
				}

				for (BuildSubscriber* subscriber : execution->Subscribers)
				{
					static_cast<void>(m_State->CancelLocked(*subscriber));
				}
			}
		}

		m_State->Changed.notify_all();
		if (!m_State->Changed.wait_until(lock, deadline, [&] { return m_State->ExitedThreads == m_Desc.WorkerCount + 1; }))
		{
			return R::Failure(error(EErrorCode::Timeout, "Shader service drain timed out; retain dependencies and retry."));
		}

		lock.unlock();

		for (BuildThread& thread : m_State->Threads)
		{
			m_Desc.ThreadBackend->Join(thread.Native);
			thread.Native = {};
		}

		lock.lock();
		m_State->Statistics.bDrained = true;
		m_bDrained = true;

		return R::Success();
	}

	ShaderBuildStatistics ShaderBuildService::GetStatistics() const noexcept
	{
		if (!m_State)
		{
			ShaderBuildStatistics statistics;
			statistics.bDrained = true;
			statistics.bAdmissionClosed = true;

			return statistics;
		}

		LockGuard lock(m_State->Mutex);
		ShaderBuildStatistics statistics = m_State->Statistics;
		statistics.RetainedResultBytes = m_State->Ledger->Used.load(MemoryOrderAcquire);

		return statistics;
	}
} // namespace rox::shader
