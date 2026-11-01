#pragma once

#include <ITraceSink.h>
#include <TextPrimitives.hpp>
#include <Types.h>

namespace rox::core
{
	// Labels outlive the scope. Active scopes end on their creating thread in reverse construction order,
	// even across capture generations. Filtered or inactive scopes do not participate in that order.
	class TraceScope final
	{
	public:
#if ROXELL_ENABLE_PROFILING
		TraceScope(DiagnosticText category, DiagnosticText name, ETraceChannel channel = ETraceChannel::Default) noexcept;
		~TraceScope() noexcept;
#else
		TraceScope(DiagnosticText, DiagnosticText, ETraceChannel = ETraceChannel::Default) noexcept {}
		~TraceScope() noexcept = default;
#endif

		TraceScope(const TraceScope&) = delete;
		TraceScope& operator=(const TraceScope&) = delete;
		TraceScope(TraceScope&&) = delete;
		TraceScope& operator=(TraceScope&&) = delete;

	private:
#if ROXELL_ENABLE_PROFILING
		ITraceSink* m_Sink = nullptr;
		TraceEvent m_Event;

		uint64 m_PreviousScope = 0;
		uint64 m_PreviousCapture = 0;
#endif
	};
}
