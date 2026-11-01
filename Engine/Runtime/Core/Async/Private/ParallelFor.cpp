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
		void executeRange(usize begin, usize end, FunctionRef<void(usize, JobContext&) noexcept> body,
		                  LinearMemoryResource& scratch, usize workerIndex,
		                  const CancellationToken& cancellation = {},
		                  EJobExecutionContext executionContext = EJobExecutionContext::AnyThread) noexcept
		{
			for (usize index = begin; index < end; ++index)
			{
				JobExecutionScope scope(scratch, workerIndex, cancellation, executionContext);
				body(index, *CurrentJobExecution()->Context);
			}
		}
	}

	void JobScheduler::ParallelFor(usize begin, usize end, usize grain,
	                               FunctionRef<void(usize, JobContext&) noexcept> body) noexcept
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
		Atomic<usize> next = 0;
		auto runner = [&](JobContext& context) noexcept
		{
			usize offset = next.load(core::MemoryOrderRelaxed);

			while (offset < count)
			{
				const usize chunkEnd = offset + core::Min(grain, count - offset);

				if (!next.compare_exchange_weak(offset, chunkEnd, core::MemoryOrderRelaxed))
				{
					continue;
				}

				executeRange(begin + offset, begin + chunkEnd, body, *CurrentJobExecution()->Scratch, context.WorkerIndex);
				offset = next.load(core::MemoryOrderRelaxed);
			}
		};

		const usize runnerCount = core::Min(state.Config.WorkerCount, count / grain + (count % grain != 0));
		SmallVector<TaskHandle, 16> accepted(*m_Resource);
		accepted.ReserveRequired(runnerCount);

		for (usize index = 0; index < runnerCount; ++index)
		{
			TaskHandle handle = submitOwned("ParallelFor", UniqueFunction<void(JobContext&) noexcept>(*m_Resource, runner), {}, {});

			if (!handle.IsValid())
			{
				break;
			}

			accepted.AddRequired(core::Move(handle));
		}

		{
			JobExecutionScope scope(scratch->GetResource(), JobContext::INLINE_WORKER_INDEX);
			runner(*CurrentJobExecution()->Context);
		}

		Join({accepted.Data(), accepted.Size()});
	}
}
