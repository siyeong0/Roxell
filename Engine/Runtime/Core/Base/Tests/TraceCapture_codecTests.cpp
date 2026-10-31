#include <TraceCapture.h>
#include <SystemMemoryResource.h>

#include <gtest/gtest.h>

#include <string>

using namespace rox::core;

namespace
{
	bool appendBytes(void* context, StringView bytes) noexcept
	{
		static_cast<std::string*>(context)->append(bytes.Data(), bytes.Size());
		return true;
	}
}

TEST(TraceCaptureCodecTests, TraceReaderRejectsTruncatedInputWithoutPublishing)
{
	TraceSnapshot source(SystemMemoryResource::Get());
	source.Metadata.Width = 1280;
	source.Metadata.Height = 720;
	source.CaptureId = 42;
	std::string bytes;
	ASSERT_TRUE(WriteTraceCapture(source, appendBytes, &bytes));
	ASSERT_FALSE(bytes.empty());
	TraceSnapshot output(SystemMemoryResource::Get());
	output.Metadata.Width = 99;
	output.CaptureId = 77;
	for (usize length = 0; length < bytes.size(); ++length)
	{
		ASSERT_FALSE(ReadTraceCapture(StringView(bytes.data(), length), output, 0)) << "length=" << length;
		ASSERT_EQ(output.Metadata.Width, 99u);
		ASSERT_EQ(output.CaptureId, 77u);
		ASSERT_TRUE(output.Events.IsEmpty());
	}
	ASSERT_TRUE(ReadTraceCapture(StringView(bytes.data(), bytes.size()), output, 0));
	ASSERT_EQ(output.Metadata.Width, 1280u);
	ASSERT_EQ(output.Metadata.Height, 720u);
	ASSERT_EQ(output.CaptureId, 42u);
}
