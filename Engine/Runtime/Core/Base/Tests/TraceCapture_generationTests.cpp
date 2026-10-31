#include <TraceCapture.h>
#include <SystemMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(TraceCaptureGenerationTests, CaptureRejectsStoppedAndPreviousGenerations)
{
	TraceCapture capture(SystemMemoryResource::Get(), 1);
	TraceEvent event;
	event.Type = ETraceEventType::Counter;
	event.Name = "counter";
	event.CaptureId = capture.GetCaptureId();
	ASSERT_TRUE(event.CaptureId != 0);
	capture.Write(event);
	capture.Write(event);
	ASSERT_EQ(capture.GetDroppedCount(), 1);
	capture.StopCapture();
	capture.Write(event);
	ASSERT_EQ(capture.GetEvents().Size(), 1);
	ASSERT_EQ(capture.GetDroppedCount(), 1);
	const uint64 next = capture.BeginCapture();
	ASSERT_TRUE(next != 0);
	ASSERT_TRUE(next != event.CaptureId);
	capture.Write(event);
	TraceSnapshot snapshot(SystemMemoryResource::Get());
	ASSERT_TRUE(capture.TryCopySnapshot(snapshot));
	ASSERT_TRUE(snapshot.Events.IsEmpty());
	ASSERT_EQ(snapshot.Dropped, 0);
	ASSERT_TRUE(snapshot.bRecording);
	event.CaptureId = next;
	capture.Write(event);
	capture.StopCapture();
	ASSERT_TRUE(capture.TryCopySnapshot(snapshot));
	ASSERT_FALSE(snapshot.bRecording);
	ASSERT_EQ(snapshot.CaptureId, next);
	ASSERT_EQ(snapshot.Events.Size(), 1);
	ASSERT_EQ(snapshot.Events[0].CaptureId, next);
}
