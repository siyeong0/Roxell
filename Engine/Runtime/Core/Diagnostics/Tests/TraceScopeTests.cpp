#include <Trace.h>
#include <TraceScope.h>

#include <gtest/gtest.h>

using namespace rox::core;

#if ROXELL_ENABLE_PROFILING
namespace
{
	class ScopeSink final : public ITraceSink
	{
	public:
		bool IsEventEnabled(const ETraceEventType type) const noexcept override
		{
			return type != ETraceEventType::Scope || m_bScopeEnabled;
		}

		void Write(const TraceEvent& event) noexcept override
		{
			m_LastEvent = event;
			++m_Count;
		}

	public:
		bool m_bScopeEnabled = true;
		usize m_Count = 0;
		TraceEvent m_LastEvent{};
	};

	class SinkBinding final
	{
	public:
		explicit SinkBinding(ITraceSink& sink) noexcept
			: m_Previous(SetTraceSink(&sink))
		{
		}

		~SinkBinding() noexcept
		{
			SetTraceSink(m_Previous);
		}

	private:
		ITraceSink* m_Previous;
	};
}

TEST(TraceScopeTests, RetainsFilterDecisionMadeAtConstruction)
{
	ScopeSink sink;
	SinkBinding binding(sink);
	{
		TraceScope accepted("Unit", "accepted");
		sink.m_bScopeEnabled = false;
	}
	ASSERT_EQ(sink.m_Count, 1);
	EXPECT_EQ(sink.m_LastEvent.Type, ETraceEventType::Scope);
	EXPECT_EQ(sink.m_LastEvent.Name, "accepted");
	EXPECT_NE(sink.m_LastEvent.ScopeId, 0);
	{
		TraceScope filtered("Unit", "filtered");
		sink.m_bScopeEnabled = true;
	}
	EXPECT_EQ(sink.m_Count, 1);
}
#endif
