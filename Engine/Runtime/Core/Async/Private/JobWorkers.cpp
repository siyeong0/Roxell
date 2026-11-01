#include "JobWorkers.h"
#include <Types.h>
#include <NumericLimits.hpp>
#include <Assertion.h>
#include <limits>

namespace rox::core
{
	JobWorkers::Worker::Worker(MemoryResource& resource, IThreadBackend& backend, JobQueue& queue, usize index,
	                           usize scratchBytes) noexcept
	    : Owner(resource, backend)
	    , Scratch(MemoryBlock::CreateRequired(resource, {scratchBytes, DEFAULT_MEMORY_ALIGNMENT}))
	    , Queue(&queue)
	    , Index(index)
	{
	}

	MemoryResource& JobWorkers::ValidateConfiguration(MemoryResource& resource, usize count,
	                                                  usize scratchBytes) noexcept
	{
		ASSERT(resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Job scheduler resource must support concurrent access.");
		ASSERT(count > 0 && count <= NumericLimits<usize>::max() / sizeof(UniquePtr<Worker>) && scratchBytes > 0, "Job scheduler configuration requires positive representable storage capacities.");

		return resource;
	}

	JobWorkers::JobWorkers(MemoryResource& resource, IThreadBackend& backend, JobQueue& queue, usize workerCount, usize scratchBytes) noexcept
	    : m_Queue(&queue)
	    , m_Workers(ValidateConfiguration(resource, workerCount, scratchBytes))
	{
		m_Workers.ReserveRequired(workerCount);

		for (usize index = 0; index < workerCount; ++index)
		{
			m_Workers.AddRequired(MakeUniqueRequired<Worker>(resource, resource, backend, queue, index, scratchBytes));
		}
	}

	ThreadStartResult JobWorkers::Start() noexcept
	{
		ASSERT(!m_Workers.IsEmpty() && !m_Workers[0]->Owner.IsJoinable(), "Job workers start requires unstarted ownership.");

		const bool bParked = m_Queue->IsParked();
		const bool bIdle = m_Queue->IsIdle();
		ASSERT(bParked && bIdle, "Job workers start requires an idle parked queue.");

		for (UniquePtr<Worker>& worker : m_Workers)
		{
			const ThreadStartResult started = worker->Owner.Start(
			    [entry = worker.Get()]() noexcept
			    {
				    workerMain(*entry);
			    });
			if (started.Status != EThreadStartStatus::Started)
			{
				Join();

				return started;
			}
		}

		m_Queue->Enable();

		return {EThreadStartStatus::Started, 0};
	}

	void JobWorkers::Join() noexcept
	{
		m_Queue->DrainAndStop();

		for (UniquePtr<Worker>& worker : m_Workers)
		{
			if (worker->Owner.IsJoinable())
			{
				worker->Owner.Join();
			}
		}

		m_Workers.Clear();
	}

	void JobWorkers::workerMain(Worker& worker) noexcept
	{
		for (;;)
		{
			TaskHandle job = worker.Queue->WaitTakeReady();

			if (!job.IsValid())
			{
				return;
			}

			JobState::From(job).Execute(worker.Scratch.GetResource(), worker.Index);
			worker.Queue->Finish(job);
		}
	}
}
