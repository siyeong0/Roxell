#pragma once

#include <TraceTypes.h>
#include <Types.h>

namespace rox::core
{
	class ITraceSink
	{
	public:
		virtual ~ITraceSink() = default;

		// Callbacks may overlap; queries must not change capture or filters. Recursive tracing is suppressed.
		// Callbacks cannot replace the sink or alter active scopes. Zero disables collection; IDs reject stale scopes.
		virtual rox::core::uint64 GetCaptureId() const noexcept { return 1; }

		// Filters run before clock and ID reads; scopes retain their initial decision.
		virtual bool IsChannelEnabled(ETraceChannel) const noexcept { return true; }
		virtual bool IsEventEnabled(ETraceEventType) const noexcept { return true; }

		// The record and labels are borrowed for this call. Write may run concurrently.
		virtual void Write(const TraceEvent& event) noexcept = 0;
	};
}
