#pragma once

#include <MemoryResource.h>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Cancellation.h>

#include <cstddef>
#include <limits>

namespace rox::core
{
	enum class EJobExecutionContext
	{
		AnyThread,
		MainThread
	};

	// The scratch resource and its allocations are borrowed only within the current execution scope.
	struct JobContext final
	{
		static constexpr rox::core::usize INLINE_WORKER_INDEX = rox::core::NumericLimits<rox::core::usize>::max();
		MemoryResource& Scratch;
		rox::core::usize WorkerIndex;
		CancellationToken Cancellation;
		EJobExecutionContext ExecutionContext = EJobExecutionContext::AnyThread;
	};
}
