#pragma once

#include "JobQueue.h"
#include <Types.h>
#include <ScratchMemory.hpp>
#include <Thread.hpp>
#include <UniquePtr.hpp>

namespace rox::core
{
	class JobWorkers final
	{
	private:
		struct Worker final
		{
			Worker(MemoryResource& resource, IThreadBackend& backend, JobQueue& queue, rox::core::usize index, rox::core::usize scratchBytes) noexcept;

			Thread Owner;
			ScratchMemory Scratch;
			JobQueue* Queue;
			rox::core::usize Index;
		};

	public:
		JobWorkers(MemoryResource& resource, IThreadBackend& backend, JobQueue& queue, rox::core::usize workerCount, rox::core::usize scratchBytes) noexcept;
		JobWorkers(const JobWorkers&) = delete;
		JobWorkers& operator=(const JobWorkers&) = delete;
		ThreadStartResult Start() noexcept;
		void Join() noexcept;
		static MemoryResource& ValidateConfiguration(MemoryResource& resource, rox::core::usize count, rox::core::usize scratchBytes) noexcept;

	private:
		static void workerMain(Worker& worker) noexcept;

	private:
		JobQueue* m_Queue;
		Array<UniquePtr<Worker>> m_Workers;
	};
}
