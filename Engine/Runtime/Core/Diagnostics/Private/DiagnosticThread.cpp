#include "DiagnosticThread.h"

#include <Assertion.h>
#include <Synchronization.hpp>

namespace rox::core
{
	namespace
	{
		rox::core::Atomic<rox::core::uint64> s_DiagnosticThreadSequence = 0;
	}

	rox::core::uint64 diagnosticThreadId() noexcept
	{
		thread_local const rox::core::uint64 threadID = s_DiagnosticThreadSequence.fetch_add(1, rox::core::MemoryOrderRelaxed) + 1;
		ASSERT(threadID != 0, "The binary-image-local diagnostic thread sequence must not wrap.");
		return threadID;
	}

} // namespace rox::core
