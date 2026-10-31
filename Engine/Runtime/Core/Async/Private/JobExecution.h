#pragma once

#include <Cancellation.h>
#include <Types.h>
#include <JobContext.h>
#include <LinearMemoryResource.h>
#include <UniqueFunction.hpp>

namespace rox::core
{
	struct JobExecutionState final
	{
		JobContext* Context;
		LinearMemoryResource* Scratch;
		bool bIsDestroyingTarget = false;
	};

	const JobExecutionState* CurrentJobExecution() noexcept;

	class JobExecutionScope final
	{
	public:
		JobExecutionScope(LinearMemoryResource& scratch, rox::core::usize workerIndex,
		                  const CancellationToken& cancellation = {},
		                  EJobExecutionContext executionContext = EJobExecutionContext::AnyThread) noexcept;
		~JobExecutionScope() noexcept;
		JobExecutionScope(const JobExecutionScope&) = delete;
		JobExecutionScope& operator=(const JobExecutionScope&) = delete;
		void ExecuteTarget(UniqueFunction<void(JobContext&) noexcept>& target, bool bSkip = false) noexcept;

	private:
		LinearMemoryResource::Marker m_Marker;
		JobContext m_Context;
		JobExecutionState m_State;
		JobExecutionState* m_Previous;
	};
}
