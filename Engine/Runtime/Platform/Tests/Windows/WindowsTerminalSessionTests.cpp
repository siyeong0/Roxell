#include <WindowsTerminalSession.h>
#include <Types.h>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <string>

namespace
{
	TEST(WindowsTerminalCellWidthTests, ReportsCombiningAndWideCodePoints)
	{
		EXPECT_EQ(rox::os::WindowsTerminalCellWidth('A'), 1);
		EXPECT_EQ(rox::os::WindowsTerminalCellWidth(0x0301), 0);
		EXPECT_EQ(rox::os::WindowsTerminalCellWidth(0x200D), 0);
		EXPECT_EQ(rox::os::WindowsTerminalCellWidth(0xFE0F), 0);
		EXPECT_EQ(rox::os::WindowsTerminalCellWidth(0xAC00), 2);
		EXPECT_EQ(rox::os::WindowsTerminalCellWidth(0x4E00), 2);
		EXPECT_EQ(rox::os::WindowsTerminalCellWidth(0x1F600), 2);
		EXPECT_EQ(rox::os::WindowsTerminalCellWidth(0x20000), 2);
		EXPECT_EQ(rox::os::WindowsTerminalCellWidth(0xFF71), 1);
	}

	TEST(WindowsTerminalCellWidthTests, InvalidScalarsUseFallbackWidth)
	{
		EXPECT_EQ(rox::os::WindowsTerminalCellWidth(0xD800), 1);
		EXPECT_EQ(rox::os::WindowsTerminalCellWidth(0x110000), 1);
	}

	TEST(WindowsTerminalCellWidthTests, ReportsUnicodeVersion)
	{
		const rox::os::WindowsTerminalUnicodeVersion version = rox::os::GetWindowsTerminalUnicodeVersion();
		EXPECT_GE(version.Major, 9);
	}
	TEST(WindowsTerminalSessionTests, InvalidBoundsRejectBeforeAllocating)
	{
		rox::core::TrackingMemoryResource memory(rox::core::SystemMemoryResource::Get());
		rox::os::TerminalDesc desc;
		desc.ExecutablePath = "unused.exe";
		desc.Size.Columns = 0;
		const rox::core::Result<rox::core::InterfaceOwner<rox::os::ITerminalSession>> result = rox::os::CreateWindowsTerminalSession(desc, memory);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().Code, rox::core::EErrorCode::InvalidArgument);
		EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-TERMINAL");
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(WindowsTerminalSessionTests, OwnerAllocationFailureReleasesMemory)
	{
		rox::core::TrackingMemoryResource tracking(rox::core::SystemMemoryResource::Get());
		rox::core::FailingMemoryResource failing(tracking, 0);
		rox::os::TerminalDesc desc;
		desc.ExecutablePath = "unused.exe";
		const rox::core::Result<rox::core::InterfaceOwner<rox::os::ITerminalSession>> result = rox::os::CreateWindowsTerminalSession(desc, failing);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().Code, rox::core::EErrorCode::ExternalFailure);
		EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-TERMINAL");
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	}
	TEST(WindowsTerminalSessionTests, CapturesOutputAndExitCodeFromChild)
	{
		wchar_t directory[MAX_PATH]{};
		const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
		ASSERT_GT(length, 0);
		ASSERT_LT(length, MAX_PATH);
		std::wstring path(directory, length);
		path += L"\\WindowsPowerShell\\v1.0\\powershell.exe";
		const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
		ASSERT_GT(required, 0);
		std::string executable(static_cast<rox::core::usize>(required), '\0');
		ASSERT_EQ(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), executable.data(), required, nullptr, nullptr), required);
		const rox::core::StringView arguments[] = { "-NoProfile", "-Command", "[Console]::WriteLine('ROXELL-TERMINAL-MARKER'); exit 37" };
		rox::os::TerminalDesc desc;
		desc.ExecutablePath = rox::core::StringView(executable);
		desc.Arguments = arguments;
		rox::core::TrackingMemoryResource memory(rox::core::SystemMemoryResource::Get());
		{
			rox::core::Result<rox::core::InterfaceOwner<rox::os::ITerminalSession>> created = rox::os::CreateWindowsTerminalSession(desc, memory);
			ASSERT_TRUE(created);
			rox::core::InterfaceOwner<rox::os::ITerminalSession> session = rox::core::Move(created.Value());
			std::string bytes;
			bool bClosed = false;
			bool bExited = false;
			rox::core::uint32 exitCode = 0;
			const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
			while (!bClosed && rox::core::MonotonicClock::now() < deadline)
			{
				rox::core::Result<rox::os::TerminalOutput> output = session->Poll(memory);
				ASSERT_TRUE(output);
				ASSERT_EQ(output.Value().StreamFailure.Code, rox::core::EErrorCode::None);
				bytes.append(reinterpret_cast<const char*>(output.Value().Bytes.Data()), output.Value().Bytes.Size());
				bExited = output.Value().bExited;
				exitCode = output.Value().ExitCode;
				bClosed = output.Value().bClosed;
				if (!bClosed)
				{
					rox::core::SleepFor(rox::core::Milliseconds(2));
				}
			}
			EXPECT_TRUE(bClosed);
			EXPECT_TRUE(bExited);
			EXPECT_EQ(exitCode, 37);
			EXPECT_NE(bytes.find("ROXELL-TERMINAL-MARKER"), std::string::npos);
			session->BeginClose();
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}
}
