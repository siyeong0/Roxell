#pragma once

#include "ShaderResultMemoryResource.h"

#include <ShaderBuildService.h>
#include <Synchronization.hpp>

namespace rox::shader
{
	struct BuildSubscriber;
	struct BuildExecution;
	struct BuildServiceState;

	struct BuildResultStorage
	{
		explicit BuildResultStorage(core::SharedPtr<ShaderResultMemoryResource> resource) noexcept;
		~BuildResultStorage() noexcept;

		core::SharedPtr<ShaderResultMemoryResource> Resource;
		ShaderBuildOutput Output;
		ShaderBuildReport Report;
		core::Error Error;

		bool bHasOutput = false;
		bool bPublished = false;
		core::uint64 RetainedBytes = 0;
	};

	struct BuildExecution
	{
		BuildExecution(const ShaderBuildPlan&, core::MemoryResource&) noexcept;

		ShaderBuildPlan Plan;
		core::CancellationSource Cancellation;
		core::Array<BuildSubscriber*> Subscribers;
		core::SharedPtr<BuildResultStorage> Result;

		core::Hash256 ExecutionKey;
		core::uint64 SourceBytes = 0;
		core::uint32 Interests = 0;
		bool bRunning = false;
		bool bCancellationCommitted = false;
		bool bCancellationDispatched = false;
		core::MonotonicClock::time_point QueuedAt;
	};

	struct BuildThread
	{
		core::NativeThreadEntry Entry;
		core::NativeThread Native;
	};

	struct BuildServiceState
	{
		explicit BuildServiceState(core::MemoryResource&) noexcept;
		bool CancelLocked(BuildSubscriber&) noexcept;
		void PublishLocked(BuildExecution&) noexcept;

		core::Array<BuildThread> Threads;

		core::Mutex Mutex;
		core::ConditionVariable Changed;

		core::Array<core::SharedPtr<BuildExecution>> Executions;
		core::SharedPtr<BuildBudgetLedger> Ledger;
		ShaderToolchainIdentity Toolchain;
		core::Array<ShaderCompilerProfile> Profiles;
		core::Error StartupError;

		ShaderBuildStatistics Statistics;
		core::uint32 StartedWorkers = 0;
		core::uint32 ExitedThreads = 0;
		bool bStopWorkers = false;
		bool bStartReady = false;
	};

	struct BuildSubscriber
	{
		BuildSubscriber(core::SharedPtr<BuildServiceState>, core::SharedPtr<BuildExecution>, ShaderBuildJobOptions) noexcept;
		~BuildSubscriber() noexcept;

		core::SharedPtr<BuildServiceState> State;
		core::SharedPtr<BuildExecution> Execution;

		ShaderBuildJobOptions Options;
		EShaderBuildJobState Phase = EShaderBuildJobState::Queued;
		ShaderBuildResult Result;
		bool bAttached = false;
	};

} // namespace rox::shader
