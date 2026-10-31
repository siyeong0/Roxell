#include <ITraceSink.h>
#include <TraceTypes.h>

#include <gtest/gtest.h>

using namespace rox::core;

namespace
{
	class DefaultTraceSink final : public ITraceSink
	{
	public:
		void Write(const TraceEvent& event) noexcept override
		{
			m_Event = event;
		}

	public:
		TraceEvent m_Event{};
	};
}

TEST(TraceTypesTests, DefaultRecordAndSinkFilters)
{
	const TraceEvent event{};
	EXPECT_EQ(event.Type, ETraceEventType::Scope);
	EXPECT_TRUE(event.Name.empty());
	EXPECT_TRUE(event.Category.empty());
	EXPECT_EQ(event.CaptureId, 1);
	EXPECT_EQ(event.ScopeId, 0);
	EXPECT_EQ(event.ParentScopeId, 0);

	DefaultTraceSink sink;
	EXPECT_EQ(sink.GetCaptureId(), 1);
	EXPECT_TRUE(sink.IsChannelEnabled(ETraceChannel::Deep));
	EXPECT_TRUE(sink.IsEventEnabled(ETraceEventType::GpuDuration));
	sink.Write(event);
	EXPECT_EQ(sink.m_Event.CaptureId, event.CaptureId);
}
