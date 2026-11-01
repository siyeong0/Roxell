#include <Assertion.h>
#include <Synchronization.hpp>
#include <Log.h>

#include <atomic>
#include <cstdlib>

namespace rox::core
{
	namespace
	{
		Atomic<AssertionFailureHandler> s_AssertionFailureHandler = nullptr;
		thread_local bool s_bReportingAssertion = false;
	} // namespace

	AssertionFailureHandler SetAssertionFailureHandler(const AssertionFailureHandler handler) noexcept
	{
		return s_AssertionFailureHandler.exchange(handler, core::MemoryOrderAcqRel);
	}

	void ReportAssertionFailure(const AssertionFailure& failure) noexcept
	{
		if (s_bReportingAssertion)
		{
			std::_Exit(EXIT_FAILURE);
		}

		s_bReportingAssertion = true;

		WriteLog({ELogLevel::Critical, "Assert", failure.Message, failure.Expression, failure.Source});
		FlushLog();

		if (const AssertionFailureHandler handler = s_AssertionFailureHandler.load(core::MemoryOrderAcquire))
		{
			handler(failure);
		}

		s_bReportingAssertion = false;
	}

	[[noreturn]] void TerminateAfterAssertion() noexcept
	{
		std::abort();
	}
} // namespace rox::core
