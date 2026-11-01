#include "TraceInternal.h"

#include <Assertion.h>
#include <ClockPrimitives.hpp>
#include <NumericLimits.hpp>
#include <Synchronization.hpp>

#if ROXELL_ENABLE_PROFILING
namespace rox::core
{
	thread_local uint64 s_ParentScope = 0;
	thread_local uint64 s_ParentCapture = 0;

	namespace
	{
		using Clock = core::MonotonicClock;
		Atomic<ITraceSink*> s_Sink = nullptr;
		thread_local bool s_bInSink = false;
		Atomic<uint64> s_ScopeSequence = 0;
		Atomic<uint64> s_Frame = 0;
	}

	uint64 nextScopeId() noexcept
	{
		const uint64 id = s_ScopeSequence.fetch_add(1, core::MemoryOrderRelaxed);
		ASSERT(id != NumericLimits<uint64>::max(), "Trace scope IDs must remain representable.");
		return id + 1;
	}

	uint64 currentFrame() noexcept { return s_Frame.load(core::MemoryOrderRelaxed); }

	uint64 timestamp() noexcept
	{
		static const Clock::time_point s_Epoch = Clock::now();
		return static_cast<uint64>(
		    core::DurationCast<core::Nanoseconds>(Clock::now() - s_Epoch).count());
	}

	bool isInSink() noexcept { return s_bInSink; }

	ITraceSink* exchangeSink(ITraceSink* sink) noexcept
	{
		return s_Sink.exchange(sink, core::MemoryOrderAcqRel);
	}

	void setCurrentFrame(uint64 frameId) noexcept
	{
		s_Frame.store(frameId, core::MemoryOrderRelaxed);
	}

	ITraceSink* activeSink() noexcept
	{
		return s_bInSink ? nullptr : s_Sink.load(core::MemoryOrderAcquire);
	}

	namespace
	{
		class SinkCallbackScope final
		{
		public:
			SinkCallbackScope() noexcept
			{
				ASSERT(!s_bInSink, "Trace callbacks must not recursively enter a sink.");
				s_bInSink = true;
			}

			~SinkCallbackScope() noexcept { s_bInSink = false; }

			SinkCallbackScope(const SinkCallbackScope&) = delete;
			SinkCallbackScope& operator=(const SinkCallbackScope&) = delete;
		};
	}

	uint64 queryCaptureId(ITraceSink& sink) noexcept
	{
		const SinkCallbackScope callback;
		return sink.GetCaptureId();
	}

	bool queryEventEnabled(ITraceSink& sink, ETraceEventType type, ETraceChannel channel) noexcept
	{
		const SinkCallbackScope callback;
		return sink.IsEventEnabled(type) && sink.IsChannelEnabled(channel);
	}

	void dispatch(ITraceSink& sink, const TraceEvent& event) noexcept
	{
		const SinkCallbackScope callback;
		sink.Write(event);
	}

} // namespace rox::core
#endif
