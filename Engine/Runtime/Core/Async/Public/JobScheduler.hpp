#pragma once

#include <Cancellation.h>
#include <FunctionRef.hpp>
#include <IThreadBackend.h>
#include <JobContext.h>
#include <Span.hpp>
#include <TaskSource.hpp>
#include <UniquePtr.hpp>

namespace rox::core
{
	enum class EJobPriority
	{
		Low,
		Normal,
		High
	};

	struct JobSchedulerConfig final
	{
		usize WorkerScratchBytes = 65536;
		// Zero workers execute ready work inline; pending dependencies require owner pumping.
		usize WorkerCount = 0;
		usize QueueCapacity = 1024;
	};

	struct JobOptions final
	{
		EJobPriority Priority = EJobPriority::Normal;
		CancellationToken Cancellation;
		EJobExecutionContext ExecutionContext = EJobExecutionContext::AnyThread;
	};

	struct JobSchedulerState;
	namespace detail { struct ExternalConnection; }

	// The thread-safe resource outlives shutdown and all tasks/handles; the backend outlives shutdown.
	// Lifecycle calls require the initialization thread and exclusion from concurrent API calls.
	class JobScheduler final
	{
	public:
		JobScheduler(MemoryResource& resource, IThreadBackend& backend) noexcept;
		~JobScheduler() noexcept;
		JobScheduler(const JobScheduler&) = delete;
		JobScheduler& operator=(const JobScheduler&) = delete;

		[[nodiscard]] ThreadStartResult Start(const JobSchedulerConfig& config) noexcept;
		void Shutdown() noexcept;
		[[nodiscard]] bool IsRunning() const noexcept;

		// Owns the callable even on refusal. Queue saturation returns a ready Conflict result;
		// skipped execution returns Cancelled. A void callable is shorthand for successful Task<void>.
		// May execute inline before return. Other allocation is mandatory.
		template <typename TValue = void, typename TTarget>
		[[nodiscard]] Task<TValue> Submit(StringView name, TTarget&& target,
		                                 Span<const TaskHandle> prerequisites = {}, const JobOptions& options = {}) noexcept
		    requires(IsNothrowConstructible<Decay<TTarget>, TTarget&&> && IsNothrowMoveConstructible<Decay<TTarget>> &&
		             IsNothrowDestructible<Decay<TTarget>> &&
		             (IsNothrowInvocableR<Result<TValue>, Decay<TTarget>&, JobContext&> ||
		              (IsVoid<TValue> && IsNothrowInvocableR<void, Decay<TTarget>&, JobContext&> &&
		               IsVoid<InvokeResult<Decay<TTarget>&, JobContext&>>)));

		template <typename TValue = void, typename TTarget>
		[[nodiscard]] Task<TValue> Submit(StringView name, TTarget&& target, const JobOptions& options) noexcept
		{
			return Submit<TValue>(name, Forward<TTarget>(target), {}, options);
		}

		// Queue saturation preserves the named callable. Accepted work consumes it and may run inline.
		template <typename TValue = void, typename TTarget>
		[[nodiscard]] Optional<Task<TValue>> TrySubmit(StringView name, TTarget& target,
		                                              Span<const TaskHandle> prerequisites = {}, const JobOptions& options = {}) noexcept;

		// Consumes the previous result and callback, including on refusal. The callback receives Result<T>.
		template <typename TNext, typename TValue, typename TTarget>
		[[nodiscard]] Task<TNext> ContinueWith(Task<TValue> prerequisite, StringView name, TTarget&& target,
		                                      const JobOptions& options = {}) noexcept
		    requires(IsNothrowInvocableR<Result<TNext>, Decay<TTarget>&, Result<TValue>, JobContext&>);

		template <typename TNext, typename TValue, typename TTarget>
		[[nodiscard]] Optional<Task<TNext>> TryContinueWith(Task<TValue>& prerequisite, StringView name, TTarget& target,
		                                                   const JobOptions& options = {}) noexcept;

		// Completion-only continuation does not consume the prerequisite's result.
		template <typename TValue = void, typename TTarget>
		[[nodiscard]] Task<TValue> ContinueWith(const TaskHandle& prerequisite, StringView name, TTarget&& target,
		                                       const JobOptions& options = {}) noexcept
		{
			return Submit<TValue>(name, Forward<TTarget>(target), {&prerequisite, 1}, options);
		}

		// Waits run outside callbacks and capture destruction. Owner-thread waits pump main-thread jobs.
		void Join(const TaskHandle& handle) noexcept;
		void Join(Span<const TaskHandle> handles) noexcept;
		template <typename TValue> void Join(const Task<TValue>& task) noexcept
		{
			Join(task.GetHandle());
		}
		void Drain() noexcept;
		usize PumpMainThread(usize maxJobs = 64) noexcept;
		void ParallelFor(usize begin, usize end, usize grain, FunctionRef<void(usize, JobContext&) noexcept> body) noexcept;

	private:
		template <typename TValue, typename TTarget> struct ResultTarget;
		template <typename TTarget> static void validateTarget(const TTarget& target) noexcept;
		void validateSubmission(StringView name, Span<const TaskHandle> prerequisites, const JobOptions& options) const noexcept;
		TaskHandle createExternal() noexcept;
		UniqueFunction<void() noexcept> externalCompletion(TaskHandle handle) noexcept;
		void retainExternal(SharedPtr<detail::ExternalConnection> connection) noexcept;
		void retireExternal(bool cancelPending = false) noexcept;
		TaskHandle submitOwned(StringView name, UniqueFunction<void(JobContext&) noexcept>&& target,
		                       Span<const TaskHandle> prerequisites, const JobOptions& options) noexcept;
		TaskHandle submitFactory(StringView name, FunctionRef<UniqueFunction<void(JobContext&) noexcept>() noexcept> factory,
		                         Span<const TaskHandle> prerequisites, const JobOptions& options) noexcept;
		TaskHandle submitPrepared(StringView name, FunctionRef<UniqueFunction<void(JobContext&) noexcept>() noexcept> factory,
		                          Span<const TaskHandle> prerequisites, const JobOptions& options) noexcept;
		void pumpInlineReady() noexcept;
		JobSchedulerState& requireRunning() const noexcept;

	private:
		MemoryResource* m_Resource;
		IThreadBackend* m_Backend;
		UniquePtr<JobSchedulerState> m_State;
	};

	template <typename TValue, typename TTarget>
	struct JobScheduler::ResultTarget final
	{
		TaskSource<TValue> Source;
		TTarget Target;
		bool bCompleted = false;

		ResultTarget(TaskSource<TValue>&& source, TTarget target) noexcept
		    : Source(Move(source))
		    , Target(Move(target))
		{
		}
		ResultTarget(ResultTarget&&) noexcept = default;
		~ResultTarget() noexcept
		{
			if (Source.IsValid() && !bCompleted)
			{
				Source.SetResult(Result<TValue>::Failure(EErrorCode::Cancelled, "ROX-CORE-JOB-CANCELLED", "The job was cancelled before execution."));
			}
		}
		void operator()(JobContext& context) noexcept
		{
			if constexpr (IsVoid<InvokeResult<TTarget&, JobContext&>>)
			{
				static_assert(IsVoid<TValue>);
				Target(context);
				Source.SetResult(Result<void>::Success());
			}
			else
			{
				Source.SetResult(Target(context));
			}
			bCompleted = true;
		}
	};

	template <typename TValue, typename TTarget>
	Task<TValue> JobScheduler::Submit(StringView name, TTarget&& target, Span<const TaskHandle> prerequisites, const JobOptions& options) noexcept
	    requires(IsNothrowConstructible<Decay<TTarget>, TTarget&&> && IsNothrowMoveConstructible<Decay<TTarget>> &&
	             IsNothrowDestructible<Decay<TTarget>> &&
	             (IsNothrowInvocableR<Result<TValue>, Decay<TTarget>&, JobContext&> ||
	              (IsVoid<TValue> && IsNothrowInvocableR<void, Decay<TTarget>&, JobContext&> &&
		               IsVoid<InvokeResult<Decay<TTarget>&, JobContext&>>)))
	{
		validateTarget(target);
		validateSubmission(name, prerequisites, options);
		Decay<TTarget> owned(Forward<TTarget>(target));
		Optional<Task<TValue>> task;
		auto factory = [&]() noexcept
		{
			TaskSource<TValue> source(*m_Resource);
			task.emplace(source.GetTask());
			return UniqueFunction<void(JobContext&) noexcept>(*m_Resource,
			    ResultTarget<TValue, Decay<TTarget>>(Move(source), Move(owned)));
		};
		TaskHandle job = submitFactory(name, factory, prerequisites, options);
		if (!job.IsValid())
		{
			return MakeReadyTask(Result<TValue>::Failure(EErrorCode::Conflict, "ROX-CORE-JOB-QUEUE-FULL", "The job queue is full."));
		}
		task->m_Job = Move(job);
		return Move(*task);
	}

	template <typename TValue, typename TTarget>
	Optional<Task<TValue>> JobScheduler::TrySubmit(StringView name, TTarget& target, Span<const TaskHandle> prerequisites, const JobOptions& options) noexcept
	{
		static_assert(!IsConst<TTarget> && IsNothrowMoveConstructible<TTarget> && IsNothrowDestructible<TTarget>);
		static_assert(IsNothrowInvocableR<Result<TValue>, TTarget&, JobContext&> || (IsVoid<TValue> && IsNothrowInvocableR<void, TTarget&, JobContext&> && IsVoid<InvokeResult<TTarget&, JobContext&>>));
		validateTarget(target);
		Optional<Task<TValue>> result;
		auto factory = [&]() noexcept
		{
			TaskSource<TValue> source(*m_Resource);
			result.emplace(source.GetTask());
			return UniqueFunction<void(JobContext&) noexcept>(*m_Resource, ResultTarget<TValue, TTarget>(Move(source), Move(target)));
		};
		TaskHandle job = submitFactory(name, factory, prerequisites, options);
		if (!job.IsValid())
		{
			return {};
		}
		result->m_Job = Move(job);
		return result;
	}

	template <typename TNext, typename TValue, typename TTarget>
	Task<TNext> JobScheduler::ContinueWith(Task<TValue> prerequisite, StringView name, TTarget&& target, const JobOptions& options) noexcept
	    requires(IsNothrowInvocableR<Result<TNext>, Decay<TTarget>&, Result<TValue>, JobContext&>)
	{
		static_assert(IsNothrowInvocableR<Result<TNext>, Decay<TTarget>&, Result<TValue>, JobContext&>);
		ASSERT(prerequisite.IsValid() && !prerequisite.IsConsumed(), "Continuation requires an unconsumed result owner.");
		validateTarget(target);
		const TaskHandle handle = prerequisite.GetHandle();
		return Submit<TNext>(name,
		    [owned = Move(prerequisite), callback = Forward<TTarget>(target)](JobContext& context) mutable noexcept
		    {
			    return callback(owned.TakeResult(), context);
		    }, {&handle, 1}, options);
	}

	template <typename TNext, typename TValue, typename TTarget>
	Optional<Task<TNext>> JobScheduler::TryContinueWith(Task<TValue>& prerequisite, StringView name, TTarget& target, const JobOptions& options) noexcept
	{
		static_assert(!IsConst<TTarget> && IsNothrowInvocableR<Result<TNext>, TTarget&, Result<TValue>, JobContext&>);
		ASSERT(prerequisite.IsValid() && !prerequisite.IsConsumed(), "Continuation requires an unconsumed result owner.");
		validateTarget(target);
		
		const TaskHandle handle = prerequisite.GetHandle();
		Optional<Task<TNext>> result;
		auto factory = [&]() noexcept
		{
			TaskSource<TNext> source(*m_Resource);
			result.emplace(source.GetTask());
			auto callback = [owned = Move(prerequisite), callable = Move(target)](JobContext& context) mutable noexcept
			{
				return callable(owned.TakeResult(), context);
			};
			return UniqueFunction<void(JobContext&) noexcept>(*m_Resource, ResultTarget<TNext, decltype(callback)>(Move(source), Move(callback)));
		};

		TaskHandle job = submitFactory(name, factory, {&handle, 1}, options);
		if (!job.IsValid())
		{
			return {};
		}
		result->m_Job = Move(job);
		return result;
	}

	template <typename TTarget>
	void JobScheduler::validateTarget(const TTarget& target) noexcept
	{
		if constexpr (IsPointer<Decay<TTarget>> && IsFunction<RemovePointer<Decay<TTarget>>>)
		{
			ASSERT(target != nullptr, "Job submission requires a callable target.");
		}
		else if constexpr (requires { target.operator bool(); })
		{
			ASSERT(static_cast<bool>(target), "Job submission requires a callable target.");
		}
		if constexpr (requires { target.GetResource(); })
		{
			ASSERT(target.GetResource() != nullptr && target.GetResource()->GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Job callable resource must support concurrent access.");
		}
	}
}
