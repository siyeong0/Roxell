#include <ShaderBuildJob.h>

#include "ShaderBuildState.h"

#include <Assertion.h>
#include <ClockPrimitives.hpp>
#include <NumericLimits.hpp>
#include <Synchronization.hpp>

namespace rox::shader
{
	using namespace core;

	Result<ShaderBuildJob> ShaderBuildJob::Attach(
		const SharedPtr<BuildServiceState>& state,
		const SharedPtr<BuildExecution>& execution,
		const ShaderBuildJobOptions& options,
		MemoryResource& resource)
	{
		ASSERT(state && execution && execution->Result, "Job attachment requires live service and execution ownership.");
		ASSERT(!execution->bCancellationCommitted && !execution->Result->bPublished, "Job attachment requires an active execution.");
		ASSERT(resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Job storage must be thread-safe.");
		ShaderBuildJob job;
		if (!MakeSharedInto(resource, &job.m_Storage, state, execution, options))
		{
			return Result<ShaderBuildJob>::Failure(EErrorCode::ExternalFailure, "ROX-SHADER-BUILD", "Job subscriber allocation failed.");
		}

		const bool bAdded = execution->Subscribers.Add(job.m_Storage.Get());
		ASSERT(bAdded, "Subscriber storage is reserved before attachment.");
		job.m_Storage->bAttached = true;
		job.m_Storage->Phase = execution->bRunning ? EShaderBuildJobState::Running : EShaderBuildJobState::Queued;
		++execution->Interests;
		++state->Statistics.Subscribers;
		ASSERT(state->Statistics.SubmittedSubscribers != NumericLimits<uint64>::max(), "Subscriber counter cannot overflow.");
		++state->Statistics.SubmittedSubscribers;

		return Result<ShaderBuildJob>::Success(Move(job));
	}

	ShaderBuildJob::ShaderBuildJob() noexcept = default;
	ShaderBuildJob::~ShaderBuildJob() = default;
	ShaderBuildJob::ShaderBuildJob(const ShaderBuildJob&) noexcept = default;
	ShaderBuildJob& ShaderBuildJob::operator=(const ShaderBuildJob&) noexcept = default;
	ShaderBuildJob::ShaderBuildJob(ShaderBuildJob&&) noexcept = default;
	ShaderBuildJob& ShaderBuildJob::operator=(ShaderBuildJob&&) noexcept = default;

	bool ShaderBuildJob::IsValid() const noexcept
	{
		return static_cast<bool>(m_Storage);
	}

	ShaderBuildJobSnapshot ShaderBuildJob::Poll() const noexcept
	{
		if (!m_Storage)
		{
			return {};
		}

		if (m_Storage->Options.bHasDeadline && MonotonicClock::now() >= m_Storage->Options.Deadline)
		{
			Cancel();
		}

		LockGuard lock(m_Storage->State->Mutex);

		return {m_Storage->Phase, m_Storage->Result};
	}

	void ShaderBuildJob::Cancel() const noexcept
	{
		if (!m_Storage)
		{
			return;
		}

		{
			LockGuard lock(m_Storage->State->Mutex);
			static_cast<void>(m_Storage->State->CancelLocked(*m_Storage));
		}

		m_Storage->State->Changed.notify_all();
	}
} // namespace rox::shader
