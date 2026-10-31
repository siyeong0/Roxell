#include <JobScheduler.hpp>
#include <Synchronization.hpp>
#include <Types.h>
#include <Comparison.hpp>
#include <Utility.hpp>
#include <Cancellation.h>
#include "JobSchedulerState.h"
#include <Assertion.h>
#include <algorithm>
#include <atomic>

namespace rox::core
{
	namespace
	{
		void executeRange(rox::core::usize begin, rox::core::usize end, FunctionRef<void(rox::core::usize, JobContext&) noexcept> body,
		                  LinearMemoryResource& scratch, rox::core::usize workerIndex,
		                  const CancellationToken& cancellation = {},
		                  EJobExecutionContext executionContext = EJobExecutionContext::AnyThread) noexcept
		{
			for (rox::core::usize index = begin; index < end; ++index)
			{
				JobExecutionScope scope(scratch, workerIndex, cancellation, executionContext);
				body(index, *CurrentJobExecution()->Context);
			}
		}
	}

	void JobScheduler::ParallelFor(rox::core::usize begin, rox::core::usize end, rox::core::usize grain,
	                               FunctionRef<void(rox::core::usize, JobContext&) noexcept> body) noexcept
	{
		ASSERT(begin <= end && grain > 0, "ParallelFor requires an ordered range and positive grain.");

		const JobExecutionState* const execution = CurrentJobExecution();
		ASSERT(execution == nullptr || !execution->bIsDestroyingTarget, "Job target destruction cannot submit work or enter ParallelFor.");

		JobSchedulerState& state = requireRunning();

		if (begin == end)
		{
			return;
		}

		if (execution)
		{
			executeRange(begin, end, body, *execution->Scratch, execution->Context->WorkerIndex, execution->Context->Cancellation, execution->Context->ExecutionContext);

			return;
		}

		if (!state.Workers)
		{
			executeRange(begin, end, body, state.Scratch->GetResource(), JobContext::INLINE_WORKER_INDEX);

			return;
		}

		UniquePtr<ScratchMemory> callerScratch;
		ScratchMemory* scratch = state.Scratch.Get();

		if (m_Backend->CurrentThreadId() != state.OwnerId)
		{
			callerScratch = MakeUniqueRequired<ScratchMemory>(*m_Resource, MemoryBlock::CreateRequired(*m_Resource, {state.Config.WorkerScratchBytes, DEFAULT_MEMORY_ALIGNMENT}));
			scratch = callerScratch.Get();
		}

		const usize count = end - begin;
		rox::core::Atomic<rox::core::usize> next = 0;
		auto runner = [&](JobContext& context) noexcept
		{
			usize offset = next.load(rox::core::MemoryOrderRelaxed);

			while (offset < count)
			{
				const usize chunkEnd = offset + rox::core::Min(grain, count - offset);

				if (!next.compare_exchange_weak(offset, chunkEnd, rox::core::MemoryOrderRelaxed))
				{
					continue;
				}

				executeRange(begin + offset, begin + chunkEnd, body, *CurrentJobExecution()->Scratch, context.WorkerIndex);
				offset = next.load(rox::core::MemoryOrderRelaxed);
			}
		};

		const usize runnerCount = rox::core::Min(state.Config.WorkerCount, count / grain + (count % grain != 0));
		SmallVector<TaskHandle, 16> accepted(*m_Resource);
		accepted.ReserveRequired(runnerCount);

		for (rox::core::usize index = 0; index < runnerCount; ++index)
		{
			TaskHandle handle = submitOwned("ParallelFor", UniqueFunction<void(JobContext&) noexcept>(*m_Resource, runner), {}, {});

			if (!handle.IsValid())
			{
				break;
			}

			accepted.AddRequired(rox::core::Move(handle));
		}

		{
			JobExecutionScope scope(scratch->GetResource(), JobContext::INLINE_WORKER_INDEX);
			runner(*CurrentJobExecution()->Context);
		}

		Join({accepted.Data(), accepted.Size()});
	}
}
