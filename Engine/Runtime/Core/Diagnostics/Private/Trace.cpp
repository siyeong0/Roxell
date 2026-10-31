#include <Trace.h>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Numeric.hpp>
#include "TraceInternal.h"
#include "DiagnosticThread.h"
#include <Assertion.h>
#include <cmath>

#if ROXELL_ENABLE_PROFILING
namespace rox::core
{

	namespace
	{
		void traceMemory(ETraceEventType type, const void* resource, const void* address, rox::core::usize bytes) noexcept
		{
			ASSERT(resource != nullptr && bytes > 0, "Memory tracing requires a resource and positive byte count.");

			if (ITraceSink* const sink = activeSink())
			{
				if (!queryEventEnabled(*sink, type, ETraceChannel::Default))
				{
					return;
				}

				TraceEvent event;
				event.CaptureId = queryCaptureId(*sink);

				if (event.CaptureId == 0)
				{
					return;
				}

				event.FrameId = currentFrame();
				event.Type = type;
				event.Name = type == ETraceEventType::Allocation ? "Allocate" : "Deallocate";
				event.Category = "Memory";
				event.TimestampNanoseconds = timestamp();
				event.ThreadId = diagnosticThreadId();
				event.ResourceId = reinterpret_cast<rox::core::uintptr>(resource);
				event.Address = reinterpret_cast<rox::core::uintptr>(address);
				event.SizeBytes = bytes;
				dispatch(*sink, event);
			}
		}

	} // namespace

	ITraceSink* SetTraceSink(ITraceSink* sink) noexcept
	{
		ASSERT(!isInSink(), "Trace sinks cannot be replaced from a trace callback.");
		return exchangeSink(sink);
	}

	bool IsTracingEnabled() noexcept
	{
		ITraceSink* const sink = activeSink();
		return sink != nullptr && queryCaptureId(*sink) != 0;
	}

	rox::core::uint64 GetTraceCaptureId() noexcept
	{
		ITraceSink* const sink = activeSink();
		return sink != nullptr ? queryCaptureId(*sink) : 0;
	}

	rox::core::uint64 GetTraceFrameId() noexcept
	{
		return currentFrame();
	}

	void TraceCounter(rox::core::DiagnosticText category, rox::core::DiagnosticText name, double value) noexcept
	{
		ASSERT(rox::core::IsFinite(value), "Trace counters require a finite value.");

		if (ITraceSink* const sink = activeSink())
		{
			if (!queryEventEnabled(*sink, ETraceEventType::Counter, ETraceChannel::Default))
			{
				return;
			}

			TraceEvent event;
			event.CaptureId = queryCaptureId(*sink);

			if (event.CaptureId == 0)
			{
				return;
			}

			event.FrameId = currentFrame();
			event.Type = ETraceEventType::Counter;
			event.Name = name;
			event.Category = category;
			event.TimestampNanoseconds = timestamp();
			event.ThreadId = diagnosticThreadId();
			event.Value = value;
			dispatch(*sink, event);
		}
	}

	void TraceAllocation(const void* resource, const void* address, rox::core::usize bytes) noexcept
	{
		traceMemory(ETraceEventType::Allocation, resource, address, bytes);
	}

	void TraceDeallocation(const void* resource, const void* address, rox::core::usize bytes) noexcept
	{
		ASSERT(address != nullptr, "Deallocation tracing requires a live allocation address.");
		traceMemory(ETraceEventType::Deallocation, resource, address, bytes);
	}

	void TraceFrame(rox::core::uint64 frameId) noexcept
	{
		if (isInSink())
		{
			return;
		}

		setCurrentFrame(frameId);

		if (ITraceSink* const sink = activeSink())
		{
			if (!queryEventEnabled(*sink, ETraceEventType::Frame, ETraceChannel::Default))
			{
				return;
			}

			TraceEvent event;
			event.CaptureId = queryCaptureId(*sink);

			if (event.CaptureId == 0)
			{
				return;
			}

			event.Type = ETraceEventType::Frame;
			event.Name = "Frame";
			event.Category = "Frame";
			event.FrameId = frameId;
			event.TimestampNanoseconds = timestamp();
			event.ThreadId = diagnosticThreadId();
			dispatch(*sink, event);
		}
	}

	void TraceGpuDuration(rox::core::DiagnosticText category, rox::core::DiagnosticText name, rox::core::uint64 frameId,
	                      rox::core::uint32 queue, rox::core::uint64 durationNanoseconds, rox::core::uint64 captureId) noexcept
	{
		if (ITraceSink* const sink = activeSink())
		{
			if (!queryEventEnabled(*sink, ETraceEventType::GpuDuration, ETraceChannel::Default))
			{
				return;
			}

			TraceEvent event;
			event.CaptureId = queryCaptureId(*sink);

			if (event.CaptureId == 0 || (captureId != 0 && captureId != event.CaptureId))
			{
				return;
			}

			event.Type = ETraceEventType::GpuDuration;
			event.Name = name;
			event.Category = category;
			event.FrameId = frameId;
			event.Queue = queue;
			event.DurationNanoseconds = durationNanoseconds;
			dispatch(*sink, event);
		}
	}

}

#else
namespace rox::core
{
	ITraceSink* SetTraceSink(ITraceSink*) noexcept { return nullptr; }
	bool IsTracingEnabled() noexcept { return false; }
	rox::core::uint64 GetTraceCaptureId() noexcept { return 0; }
	rox::core::uint64 GetTraceFrameId() noexcept { return 0; }
}
#endif
