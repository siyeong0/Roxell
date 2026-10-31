#include <Trace.h>

#include <gtest/gtest.h>

using namespace rox::core;

#if ROXELL_ENABLE_PROFILING
namespace
{
	class TraceSink final : public ITraceSink
	{
	public:
		bool IsChannelEnabled(const ETraceChannel channel) const noexcept override
		{
			return channel != ETraceChannel::Deep;
		}
		bool IsEventEnabled(const ETraceEventType type) const noexcept override
		{
			return type != ETraceEventType::Frame;
		}
		void Write(const TraceEvent& event) noexcept override
		{
			if (m_Count < 4)
			{
				m_Events[m_Count++] = event;
			}
			m_bCallbackSuppressed = m_bCallbackSuppressed && !IsTracingEnabled() && GetTraceCaptureId() == 0;
			TraceCounter("Recursive", "ignored", 1);
		}

	public:
		TraceEvent m_Events[4]{};
		usize m_Count = 0;
		bool m_bCallbackSuppressed = true;
	};

	class TraceSettings final
	{
	public:
		explicit TraceSettings(ITraceSink& sink) noexcept
			: m_Previous(SetTraceSink(&sink))
		{
		}
		~TraceSettings() noexcept
		{
			SetTraceSink(m_Previous);
		}

	private:
		ITraceSink* m_Previous;
	};
}

TEST(TraceTests, FilteringAndScope)
{
	TraceSink sink;
	TraceSettings settings(sink);
	TraceFrame(42);
	ASSERT_TRUE(GetTraceFrameId() == 42 && sink.m_Count == 0);
	{
		TraceScope outer("Unit", "outer");
		TraceScope hidden("Unit", "hidden", ETraceChannel::Deep);
		{
			TraceScope child("Unit", "child");
		}
		TraceCounter("Unit", "count", 3.0);
	}
	ASSERT_TRUE(sink.m_Count == 3 && sink.m_bCallbackSuppressed);
	ASSERT_TRUE(sink.m_Events[0].Type == ETraceEventType::Scope && sink.m_Events[0].Name == "child");
	ASSERT_TRUE(sink.m_Events[1].Type == ETraceEventType::Counter && sink.m_Events[1].Value == 3.0);
	ASSERT_TRUE(sink.m_Events[2].Type == ETraceEventType::Scope && sink.m_Events[2].Name == "outer");
	ASSERT_TRUE(sink.m_Events[0].ParentScopeId == sink.m_Events[2].ScopeId);
	ASSERT_TRUE(sink.m_Events[2].ScopeId != 0 && sink.m_Events[2].ParentScopeId == 0);
	ASSERT_TRUE(sink.m_Events[0].FrameId == 42 && sink.m_Events[2].FrameId == 42);
}
#endif
