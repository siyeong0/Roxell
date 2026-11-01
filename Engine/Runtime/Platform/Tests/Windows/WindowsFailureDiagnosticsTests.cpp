#include <WindowsFailureDiagnostics.h>
#include <WindowsLogSink.h>
#include <Result.hpp>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace rox::core;
using namespace rox::os;

namespace
{
	class EnvironmentValue final
	{
	public:
		EnvironmentValue(const char* name, const char* value) : m_Name(name)
		{
			const DWORD size = GetEnvironmentVariableA(name, nullptr, 0);
			if (size != 0)
			{
				m_Value.resize(size);
				const DWORD length = GetEnvironmentVariableA(name, m_Value.data(), size);
				m_Value.resize(length);
				m_bPresent = true;
			}
			SetEnvironmentVariableA(name, value);
		}

		~EnvironmentValue()
		{
			SetEnvironmentVariableA(m_Name, m_bPresent ? m_Value.c_str() : nullptr);
		}

	private:
		const char* m_Name;
		std::string m_Value;
		bool m_bPresent = false;
	};

	class WindowsFailureDiagnosticsTests : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			m_Directory = std::filesystem::path(ROXELL_TEST_CACHE) /
				("failure-diagnostics-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
			ASSERT_TRUE(std::filesystem::create_directories(m_Directory));
			m_LogPath = (m_Directory / "diagnostics.jsonl").string();
			ASSERT_TRUE(m_Log.TryOpen(m_LogPath));
			m_PreviousSink = SetLogSink(&m_Log);
			m_bSinkInstalled = true;
		}

		void TearDown() override
		{
			m_Diagnostics.Stop();
			FlushLog();
			if (m_bSinkInstalled)
			{
				SetLogSink(m_PreviousSink);
			}
			m_Log.Close();
			std::error_code error;
			std::filesystem::remove_all(m_Directory, error);
			EXPECT_FALSE(error);
		}

		std::string readLog()
		{
			FlushLog();
			std::ifstream stream(m_LogPath);
			return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
		}

	protected:
		EnvironmentValue m_Enable{ "ROXELL_FAILURE_DIAGNOSTICS", "1" };
		EnvironmentValue m_FullDump{ "ROXELL_FULL_MEMORY_DUMPS", "0" };
		std::filesystem::path m_Directory;
		std::string m_LogPath;
		WindowsLogSink m_Log;
		WindowsFailureDiagnostics m_Diagnostics;
		ILogSink* m_PreviousSink = nullptr;
		bool m_bSinkInstalled = false;
	};
}

TEST_F(WindowsFailureDiagnosticsTests, ConcurrentErrorsCaptureStackAndBoundedDumps)
{
	m_Diagnostics.Start(m_LogPath, false);
	LOG_ERROR("Test.Sequential", "First diagnostic.");
	LOG_ERROR("Test.Sequential", "Second diagnostic.");
	std::thread workers[4];
	for (int32 index = 0; index < 4; ++index)
	{
		workers[index] = std::thread([] { LOG_ERROR("Test.Concurrent", "Expected concurrent diagnostic."); });
	}
	for (std::thread& worker : workers)
	{
		worker.join();
	}
	ReportAssertionFailure({ "false", "Expected assertion diagnostic.", SourceLocation::current() });
	m_Diagnostics.Stop();
	const std::string contents = readLog();
	EXPECT_NE(contents.find("frame=0"), std::string::npos);
	EXPECT_NE(contents.find("privateBytes="), std::string::npos);
	EXPECT_EQ(contents.find("dump=failed"), std::string::npos);
	usize dumpCount = 0;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(m_Directory))
	{
		if (entry.path().extension() == ".dmp")
		{
			std::ifstream dump(entry.path(), std::ios::binary);
			char signature[4]{};
			dump.read(signature, 4);
			EXPECT_EQ(std::string(signature, 4), "MDMP");
			++dumpCount;
		}
	}
	EXPECT_EQ(dumpCount, 3u);
}

TEST_F(WindowsFailureDiagnosticsTests, DisabledCaptureStillWritesTheOriginalError)
{
	m_Diagnostics.Start(m_LogPath, false);
	SetFailureDiagnosticsEnabled(false);
	LOG_ERROR("Test.Disabled", "Disabled capture still logs the original error.");
	m_Diagnostics.Stop();
	const std::string contents = readLog();
	EXPECT_NE(contents.find("Test.Disabled"), std::string::npos);
	EXPECT_EQ(contents.find("\"category\":\"FailureDiagnostics\""), std::string::npos);
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(m_Directory))
	{
		EXPECT_NE(entry.path().extension(), ".dmp");
	}
}

TEST_F(WindowsFailureDiagnosticsTests, UnwritableDumpIsReportedWithoutLosingOriginalError)
{
	m_Diagnostics.Start(m_LogPath + ".missing/diagnostics.jsonl", false);
	LOG_ERROR("Test.Unwritable", "A dump failure remains an ordinary diagnostic.");
	m_Diagnostics.Stop();
	const std::string contents = readLog();
	EXPECT_NE(contents.find("Test.Unwritable"), std::string::npos);
	EXPECT_NE(contents.find("dump=failed"), std::string::npos);
	EXPECT_FALSE(std::filesystem::exists(m_Directory / "diagnostics.jsonl.missing"));
}

TEST_F(WindowsFailureDiagnosticsTests, ResultFailureTracingRetainsDiagnosticIdentity)
{
	m_Diagnostics.Start(m_LogPath, false);
	const bool bPrevious = SetResultFailureTracingEnabled(true);
	const Result<int32> result = Result<int32>::Failure(EErrorCode::Unsupported, "Test.Failure", "Expected operational failure.");
	SetResultFailureTracingEnabled(bPrevious);
	ASSERT_FALSE(result);
	EXPECT_EQ(result.GetError().Code, EErrorCode::Unsupported);
	EXPECT_EQ(result.GetError().DiagnosticID, "Test.Failure");
	m_Diagnostics.Stop();
	const std::string contents = readLog();
	EXPECT_NE(contents.find("Test.Failure"), std::string::npos);
}
