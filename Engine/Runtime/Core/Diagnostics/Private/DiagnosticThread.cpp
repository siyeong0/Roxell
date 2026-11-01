#include "DiagnosticThread.h"

#include <Assertion.h>
#include <Synchronization.hpp>

namespace rox::core
{
	namespace
	{
		Atomic<uint64> s_DiagnosticThreadSequence = 0;
	}

	uint64 diagnosticThreadId() noexcept
	{
		thread_local const uint64 threadID = s_DiagnosticThreadSequence.fetch_add(1, core::MemoryOrderRelaxed) + 1;
		ASSERT(threadID != 0, "The binary-image-local diagnostic thread sequence must not wrap.");
		return threadID;
	}

} // namespace rox::core
