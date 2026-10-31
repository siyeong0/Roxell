#pragma once

#include <ITraceSink.h>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <TraceScope.h>

namespace rox::core
{
	// The borrowed sink must outlive callbacks and scopes. Replace it only when tracing is quiescent and no scopes live.
	ITraceSink* SetTraceSink(ITraceSink* sink) noexcept;
	bool IsTracingEnabled() noexcept;
	rox::core::uint64 GetTraceCaptureId() noexcept;
	rox::core::uint64 GetTraceFrameId() noexcept;

	// Labels must be valid UTF-8. Sink callbacks suppress recursive tracing, including frame updates.

#if ROXELL_ENABLE_PROFILING
	void TraceCounter(rox::core::DiagnosticText category, rox::core::DiagnosticText name, double value) noexcept;
	void TraceAllocation(const void* resource, const void* address, rox::core::usize bytes) noexcept;
	void TraceDeallocation(const void* resource, const void* address, rox::core::usize bytes) noexcept;
	void TraceFrame(rox::core::uint64 frameId) noexcept;

	// GPU duration has no position on the CPU clock; queue intervals may overlap.
	void TraceGpuDuration(rox::core::DiagnosticText category, rox::core::DiagnosticText name, rox::core::uint64 frameId,
	                      rox::core::uint32 queue, rox::core::uint64 durationNanoseconds, rox::core::uint64 captureId = 0) noexcept;
#else
	inline void TraceCounter(rox::core::DiagnosticText, rox::core::DiagnosticText, double) noexcept {}
	inline void TraceAllocation(const void*, const void*, rox::core::usize) noexcept {}
	inline void TraceDeallocation(const void*, const void*, rox::core::usize) noexcept {}
	inline void TraceFrame(rox::core::uint64) noexcept {}
	inline void TraceGpuDuration(rox::core::DiagnosticText, rox::core::DiagnosticText, rox::core::uint64, rox::core::uint32, rox::core::uint64, rox::core::uint64 = 0) noexcept {}
#endif
}

#define ROX_TRACE_JOIN_IMPL(left, right) left##right
#define ROX_TRACE_JOIN(left, right) ROX_TRACE_JOIN_IMPL(left, right)
#if ROXELL_ENABLE_PROFILING
#define ROX_PROFILE_SCOPE(category, name) ::rox::core::TraceScope ROX_TRACE_JOIN(chrTraceScope, __LINE__)(category, name)
#define ROX_PROFILE_DEEP_SCOPE(category, name) ::rox::core::TraceScope ROX_TRACE_JOIN(chrTraceScope, __LINE__)(category, name, ::rox::core::ETraceChannel::Deep)
#define ROX_PROFILE_COUNTER(category, name, value) ::rox::core::TraceCounter(category, name, value)
#define ROX_PROFILE_FRAME(frameId) ::rox::core::TraceFrame(frameId)
#define ROX_PROFILE_GPU(category, name, frameId, queue, duration) ::rox::core::TraceGpuDuration(category, name, frameId, queue, duration)
#else
#define ROX_PROFILE_SCOPE(category, name) ((void)0)
#define ROX_PROFILE_DEEP_SCOPE(category, name) ((void)0)
#define ROX_PROFILE_COUNTER(category, name, value) ((void)0)
#define ROX_PROFILE_FRAME(frameId) ((void)0)
#define ROX_PROFILE_GPU(category, name, frameId, queue, duration) ((void)0)
#endif
