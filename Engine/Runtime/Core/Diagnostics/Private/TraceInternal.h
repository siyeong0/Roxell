#pragma once

#include <ITraceSink.h>
#include <Types.h>

namespace rox::core
{
	bool isInSink() noexcept;
	ITraceSink* exchangeSink(ITraceSink* sink) noexcept;
	ITraceSink* activeSink() noexcept;
	uint64 queryCaptureId(ITraceSink& sink) noexcept;
	bool queryEventEnabled(ITraceSink& sink, ETraceEventType type, ETraceChannel channel) noexcept;
	void dispatch(ITraceSink& sink, const TraceEvent& event) noexcept;

	void setCurrentFrame(uint64 frameId) noexcept;
	uint64 currentFrame() noexcept;

	uint64 nextScopeId() noexcept;
	extern thread_local uint64 s_ParentScope;
	extern thread_local uint64 s_ParentCapture;

	uint64 timestamp() noexcept;
}
