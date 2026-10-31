#pragma once

#include <ITraceSink.h>
#include <Types.h>

namespace rox::core
{
	bool isInSink() noexcept;
	ITraceSink* exchangeSink(ITraceSink* sink) noexcept;
	ITraceSink* activeSink() noexcept;
	rox::core::uint64 queryCaptureId(ITraceSink& sink) noexcept;
	bool queryEventEnabled(ITraceSink& sink, ETraceEventType type, ETraceChannel channel) noexcept;
	void dispatch(ITraceSink& sink, const TraceEvent& event) noexcept;

	void setCurrentFrame(rox::core::uint64 frameId) noexcept;
	rox::core::uint64 currentFrame() noexcept;

	rox::core::uint64 nextScopeId() noexcept;
	extern thread_local rox::core::uint64 s_ParentScope;
	extern thread_local rox::core::uint64 s_ParentCapture;

	rox::core::uint64 timestamp() noexcept;
}
