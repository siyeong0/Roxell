#pragma once

#include "ExternalConnection.h"
#include "JobState.h"
#include "JobWorkers.h"
#include <JobScheduler.hpp>
#include <ScratchMemory.hpp>

namespace rox::core
{
	struct JobSchedulerState final
	{
		JobSchedulerState(MemoryResource& resource, IThreadBackend& backend, const JobSchedulerConfig& config) noexcept
		    : Config(config)
		    , Identity(MakeSharedRequired<JobSchedulerIdentity>(resource, resource))
		    , OwnerId(backend.CurrentThreadId())
		{
			Scratch = MakeUniqueRequired<ScratchMemory>(
			    resource, MemoryBlock::CreateRequired(resource, {config.WorkerScratchBytes, DEFAULT_MEMORY_ALIGNMENT}));

			if (config.WorkerCount != 0)
			{
				Queue = MakeSharedRequired<JobQueue>(resource, resource, Identity, config.QueueCapacity);
				Workers = MakeUniqueRequired<JobWorkers>(resource, resource, backend, *Queue, config.WorkerCount,
				                                 config.WorkerScratchBytes);
			}
		}

		const JobSchedulerConfig Config;
		SharedPtr<JobSchedulerIdentity> Identity;
		ThreadId OwnerId;

		UniquePtr<ScratchMemory> Scratch;
		SharedPtr<JobQueue> Queue;
		UniquePtr<JobWorkers> Workers;

		Mutex ExternalMutex;
		Atomic<bool> bClosingExternal = false;
		SharedPtr<detail::ExternalConnection> External;
	};
}
