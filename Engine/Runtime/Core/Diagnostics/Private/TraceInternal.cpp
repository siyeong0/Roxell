#include "TraceInternal.h"

#include <Assertion.h>
#include <ClockPrimitives.hpp>
#include <NumericLimits.hpp>
#include <Synchronization.hpp>

#if ROXELL_ENABLE_PROFILING
namespace rox::core
{
	thread_local rox::core::uint64 s_ParentScope = 0;
	thread_local rox::core::uint64 s_ParentCapture = 0;

	namespace
	{
		using Clock = rox::core::MonotonicClock;
		rox::core::Atomic<ITraceSink*> s_Sink = nullptr;
		thread_local bool s_bInSink = false;
		rox::core::Atomic<rox::core::uint64> s_ScopeSequence = 0;
		rox::core::Atomic<rox::core::uint64> s_Frame = 0;
	}

	rox::core::uint64 nextScopeId() noexcept
	{
		const rox::core::uint64 id = s_ScopeSequence.fetch_add(1, rox::core::MemoryOrderRelaxed);
		ASSERT(id != rox::core::NumericLimits<rox::core::uint64>::max(), "Trace scope IDs must remain representable.");
		return id + 1;
	}

	rox::core::uint64 currentFrame() noexcept { return s_Frame.load(rox::core::MemoryOrderRelaxed); }

	rox::core::uint64 timestamp() noexcept
	{
		static const Clock::time_point s_Epoch = Clock::now();
		return static_cast<rox::core::uint64>(
		    rox::core::DurationCast<rox::core::Nanoseconds>(Clock::now() - s_Epoch).count());
	}

	bool isInSink() noexcept { return s_bInSink; }

	ITraceSink* exchangeSink(ITraceSink* sink) noexcept
	{
		return s_Sink.exchange(sink, rox::core::MemoryOrderAcqRel);
	}

	void setCurrentFrame(rox::core::uint64 frameId) noexcept
	{
		s_Frame.store(frameId, rox::core::MemoryOrderRelaxed);
	}

	ITraceSink* activeSink() noexcept
	{
		return s_bInSink ? nullptr : s_Sink.load(rox::core::MemoryOrderAcquire);
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

	rox::core::uint64 queryCaptureId(ITraceSink& sink) noexcept
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
