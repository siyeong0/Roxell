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
		static constexpr usize INLINE_WORKER_INDEX = NumericLimits<usize>::max();
		MemoryResource& Scratch;
		usize WorkerIndex;
		CancellationToken Cancellation;
		EJobExecutionContext ExecutionContext = EJobExecutionContext::AnyThread;
	};
}
