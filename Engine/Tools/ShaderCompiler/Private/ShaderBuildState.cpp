#include "ShaderBuildState.h"

#include <Assertion.h>
#include <NumericLimits.hpp>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	BuildResultStorage::BuildResultStorage(SharedPtr<ShaderResultMemoryResource> resource) noexcept
		: Resource(Move(resource))
		, Output(*Resource)
		, Report(*Resource)
		, Error(EErrorCode::None, String(*Resource), String(*Resource), String(*Resource))
	{
	}

	BuildResultStorage::~BuildResultStorage() noexcept
	{
		Output.Artifact = {};
		Output.Dependencies.Reset();
		Report.Diagnostics.Reset();
		Error.DiagnosticID.Reset();
		Error.Message.Reset();
		Error.RecoveryHint.Reset();
		Resource->ReleaseOwner();
	}

	BuildExecution::BuildExecution(const ShaderBuildPlan& plan, MemoryResource& resource) noexcept
		: Plan(plan)
		, Cancellation(CancellationSource::Create(resource))
		, Subscribers(resource)
	{
	}

	BuildServiceState::BuildServiceState(MemoryResource& resource) noexcept
		: Threads(resource)
		, Executions(resource)
		, Toolchain(resource)
		, Profiles(resource)
		, StartupError(EErrorCode::None, String(resource), String(resource), String(resource))
	{
	}

	bool BuildServiceState::CancelLocked(BuildSubscriber& subscriber) noexcept
	{
		if (subscriber.Phase != EShaderBuildJobState::Queued && subscriber.Phase != EShaderBuildJobState::Running)
		{
			return false;
		}

		if (subscriber.Phase == EShaderBuildJobState::Queued)
		{
			ASSERT(Statistics.CancelledQueuedSubscribers != NumericLimits<uint64>::max(), "Queued cancellation counter cannot overflow.");
			++Statistics.CancelledQueuedSubscribers;
		}
		else
		{
			ASSERT(Statistics.CancelledRunningSubscribers != NumericLimits<uint64>::max(), "Running cancellation counter cannot overflow.");
			++Statistics.CancelledRunningSubscribers;
		}

		subscriber.Phase = EShaderBuildJobState::Cancelled;
		ASSERT(subscriber.Execution->Interests != 0, "Active subscriber interest must exist.");
		--subscriber.Execution->Interests;
		if (subscriber.Execution->Interests == 0 && !subscriber.Execution->bCancellationCommitted)
		{
			subscriber.Execution->bCancellationCommitted = true;
			if (!subscriber.Execution->bRunning)
			{
				Executions.RemoveIf([&](const SharedPtr<BuildExecution>& execution) noexcept { return execution.Get() == subscriber.Execution.Get(); });
				ASSERT(Statistics.QueuedJobs != 0 && Statistics.AdmittedSourceBytes >= subscriber.Execution->SourceBytes, "Queued execution accounting cannot underflow.");
				--Statistics.QueuedJobs;
				Statistics.AdmittedSourceBytes -= subscriber.Execution->SourceBytes;
				subscriber.Execution->Plan = {};
				subscriber.Execution->Result.Reset();
			}

			return true;
		}

		return false;
	}

	void BuildServiceState::PublishLocked(BuildExecution& execution) noexcept
	{
		ASSERT(execution.Result && !execution.Result->bPublished, "Execution results are published once under the service mutex.");
		execution.Result->bPublished = true;

		for (BuildSubscriber* subscriber : execution.Subscribers)
		{
			if (subscriber->Phase == EShaderBuildJobState::Queued || subscriber->Phase == EShaderBuildJobState::Running)
			{
				subscriber->Result = ShaderBuildResult::Publish(execution.Result);
				subscriber->Phase = execution.Result->bHasOutput ? EShaderBuildJobState::Succeeded : EShaderBuildJobState::Failed;
			}
		}

		execution.Interests = 0;
	}

	BuildSubscriber::BuildSubscriber(SharedPtr<BuildServiceState> state, SharedPtr<BuildExecution> execution, ShaderBuildJobOptions options) noexcept
		: State(Move(state))
		, Execution(Move(execution))
		, Options(options)
	{
	}

	BuildSubscriber::~BuildSubscriber() noexcept
	{
		{
			LockGuard lock(State->Mutex);
			if (bAttached)
			{
				static_cast<void>(State->CancelLocked(*this));
				Execution->Subscribers.RemoveIf([this](const BuildSubscriber* subscriber) noexcept { return subscriber == this; });
				ASSERT(State->Statistics.Subscribers != 0, "Attached subscriber accounting cannot underflow.");
				--State->Statistics.Subscribers;
			}
		}

		State->Changed.notify_all();
	}
} // namespace rox::shader
