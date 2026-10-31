#include <Log.h>

#include <cstring>
#include <gtest/gtest.h>
#include <string_view>

using namespace rox::core;

namespace
{
	class LogSink final : public ILogSink
	{
	public:
		void Write(const LogEvent& event, const DiagnosticText line) noexcept override
		{
			++m_Count;
			m_LastEvent = event;
			m_Size = line.size() < sizeof(m_Line) ? line.size() : 0;
			if (m_Size != 0)
			{
				std::memcpy(m_Line, line.data(), m_Size);
				m_Line[m_Size] = '\0';
			}
		}
		void Flush() noexcept override
		{
			++m_FlushCount;
		}

	public:
		char m_Line[8193]{};
		usize m_Size = 0;
		int32 m_Count = 0;
		int32 m_FlushCount = 0;
		LogEvent m_LastEvent{};
	};

	class LogSettings final
	{
	public:
		explicit LogSettings(ILogSink& sink) noexcept
			: m_PreviousSink(SetLogSink(&sink))
			, m_PreviousLevel(SetMinimumLogLevel(ELogLevel::Warning))
			, m_PreviousPolicy(SetLogStderrPolicy(ELogStderrPolicy::Disabled))
		{
		}
		~LogSettings() noexcept
		{
			SetLogSink(m_PreviousSink);
			SetMinimumLogLevel(m_PreviousLevel);
			SetLogStderrPolicy(m_PreviousPolicy);
		}

	private:
		ILogSink* m_PreviousSink;
		ELogLevel m_PreviousLevel;
		ELogStderrPolicy m_PreviousPolicy;
	};

	class FailureDiagnostics final : public IFailureDiagnostics
	{
	public:
		void Capture(const LogEvent& event) noexcept override
		{
			++m_Count;
			m_Level = event.Record.Level;
		}

	public:
		int32 m_Count = 0;
		ELogLevel m_Level = ELogLevel::Trace;
	};

	class FailureDiagnosticsBinding final
	{
	public:
		explicit FailureDiagnosticsBinding(IFailureDiagnostics& diagnostics) noexcept
			: m_Previous(SetFailureDiagnostics(&diagnostics))
			, m_bPreviouslyEnabled(SetFailureDiagnosticsEnabled(true))
		{
		}

		~FailureDiagnosticsBinding() noexcept
		{
			SetFailureDiagnosticsEnabled(m_bPreviouslyEnabled);
			SetFailureDiagnostics(m_Previous);
		}

	private:
		IFailureDiagnostics* m_Previous;
		bool m_bPreviouslyEnabled;
	};
}

TEST(LogTests, FilteringAndFlush)
{
	LogSink sink;
	LogSettings settings(sink);
	int32 evaluations = 0;
	LOG_INFO((++evaluations, "ignored"), (++evaluations, "ignored"));
	ASSERT_TRUE(evaluations == 0 && sink.m_Count == 0);
	ASSERT_FALSE(IsLogEnabled(ELogLevel::Information));
	ASSERT_TRUE(IsLogEnabled(ELogLevel::Warning));
	LOG_WARNING((++evaluations, "Unit"), (++evaluations, "visible"));
	ASSERT_TRUE(evaluations == 2 && sink.m_Count == 1);
	ASSERT_TRUE(sink.m_LastEvent.Record.Level == ELogLevel::Warning);
	ASSERT_TRUE(sink.m_LastEvent.Sequence != 0 && sink.m_LastEvent.ThreadID != 0);
	ASSERT_TRUE(sink.m_LastEvent.TimestampUnixMicroseconds != 0);
	const uint64 previousSequence = sink.m_LastEvent.Sequence;
	LOG_ERROR("Unit", "second");
	ASSERT_TRUE(sink.m_Count == 2 && sink.m_LastEvent.Sequence > previousSequence);
	FlushLog();
	ASSERT_EQ(sink.m_FlushCount, 1);
}

TEST(LogTests, Encoding)
{
	LogSink sink;
	LogSettings settings(sink);
	const char message[] = "quote\" slash\\ newline\n malformed\xff";
	WriteLog({ELogLevel::Error, "Encode", message, {}, SourceLocation::current()});
	const std::string_view line(sink.m_Line, sink.m_Size);
	ASSERT_TRUE(sink.m_Count == 1 && sink.m_Size > 0 && sink.m_Size < 8192);
	ASSERT_TRUE(line.find("\"schema\":\"roxell.log.v1\"") != line.npos);
	ASSERT_TRUE(line.find("\"category\":\"Encode\"") != line.npos);
	ASSERT_TRUE(line.find("\"message\":\"quote\\\" slash\\\\ newline\\n malformed?\"") != line.npos);
	ASSERT_TRUE(line.find("\"truncated\":true") != line.npos && sink.m_LastEvent.bTruncated);
	ASSERT_TRUE(line.back() == '\n' && line.find('\n') == line.size() - 1);
	ASSERT_TRUE(static_cast<unsigned char>(message[sizeof(message) - 2]) == 0xff);
}

TEST(LogTests, FailureDiagnosticsOnlyCapturesEnabledErrors)
{
	LogSink sink;
	LogSettings settings(sink);
	FailureDiagnostics diagnostics;
	FailureDiagnosticsBinding binding(diagnostics);

	LOG_WARNING("Unit", "warning");
	EXPECT_EQ(diagnostics.m_Count, 0);
	LOG_ERROR("Unit", "error");
	EXPECT_EQ(diagnostics.m_Count, 1);
	EXPECT_EQ(diagnostics.m_Level, ELogLevel::Error);

	EXPECT_TRUE(SetFailureDiagnosticsEnabled(false));
	EXPECT_FALSE(IsFailureDiagnosticsEnabled());
	LOG_ERROR("Unit", "disabled");
	EXPECT_EQ(diagnostics.m_Count, 1);
	EXPECT_FALSE(SetFailureDiagnosticsEnabled(true));
	EXPECT_TRUE(IsFailureDiagnosticsEnabled());
	LOG_CRITICAL("Unit", "critical");
	EXPECT_EQ(diagnostics.m_Count, 2);
	EXPECT_EQ(diagnostics.m_Level, ELogLevel::Critical);
}

TEST(LogTests, MinimumLevelCanBeChangedAndRestored)
{
	LogSink sink;
	LogSettings settings(sink);

	EXPECT_EQ(GetMinimumLogLevel(), ELogLevel::Warning);
	EXPECT_EQ(SetMinimumLogLevel(ELogLevel::Debug), ELogLevel::Warning);
	EXPECT_TRUE(IsLogEnabled(ELogLevel::Debug));
	EXPECT_FALSE(IsLogEnabled(ELogLevel::Trace));
	EXPECT_EQ(SetMinimumLogLevel(ELogLevel::Error), ELogLevel::Debug);
	EXPECT_FALSE(IsLogEnabled(ELogLevel::Warning));
	EXPECT_TRUE(IsLogEnabled(ELogLevel::Error));
}
