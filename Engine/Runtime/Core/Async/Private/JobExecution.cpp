#include "JobExecution.h"
#include <Types.h>
#include <Cancellation.h>
#include <Assertion.h>

namespace rox::core
{
	namespace
	{
		thread_local JobExecutionState* s_CurrentExecution = nullptr;

		LinearMemoryResource::Marker beginScratch(LinearMemoryResource& scratch) noexcept
		{
			ASSERT(s_CurrentExecution == nullptr || !s_CurrentExecution->bIsDestroyingTarget, "Job target destruction cannot submit work or enter ParallelFor.");

			return scratch.Mark();
		}
	}

	const JobExecutionState* CurrentJobExecution() noexcept
	{
		return s_CurrentExecution;
	}

	JobExecutionScope::JobExecutionScope(LinearMemoryResource& scratch, usize workerIndex, const CancellationToken& cancellation, EJobExecutionContext executionContext) noexcept
	    : m_Marker(beginScratch(scratch))
	    , m_Context{scratch, workerIndex, cancellation, executionContext}
	    , m_State{&m_Context, &scratch}
	    , m_Previous(s_CurrentExecution)
	{
		s_CurrentExecution = &m_State;
	}

	JobExecutionScope::~JobExecutionScope() noexcept
	{
		ASSERT(s_CurrentExecution == &m_State, "Job execution scopes must unwind in stack order.");

		m_State.Scratch->Rewind(m_Marker);
		s_CurrentExecution = m_Previous;
	}

	void JobExecutionScope::ExecuteTarget(UniqueFunction<void(JobContext&) noexcept>& target, bool bSkip) noexcept
	{
		ASSERT(s_CurrentExecution == &m_State, "Job execution must use the current scope.");
		ASSERT(!m_State.bIsDestroyingTarget, "Job target destruction cannot submit work or enter ParallelFor.");
		ASSERT(static_cast<bool>(target), "Job submission requires a callable target.");

		if (!bSkip)
		{
			target(m_Context);
		}

		m_State.bIsDestroyingTarget = true;
		target.Reset();
	}
}
