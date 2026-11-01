#include <WindowsTerminalSession.h>
#include <Types.h>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>
#include <SystemMemoryResource.h>
#include <FailingMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <string>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	core::Span<const core::Byte> bytes(const core::StringView text)
	{
		return { reinterpret_cast<const core::Byte*>(text.Data()), text.Size() };
	}

	class WindowsTerminalIoTests : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			wchar_t executable[32768]{};
			const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
			ASSERT_GT(length, 0);
			ASSERT_LT(length, 32768);
			std::wstring path(executable, length);
			const rox::core::usize separator = path.find_last_of(L"\\/");
			ASSERT_NE(separator, std::wstring::npos);
			path.resize(separator + 1);
			path += L"Test-PlatformTerminalFixture.exe";
			const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
			ASSERT_GT(needed, 0);
			m_Executable.resize(static_cast<rox::core::usize>(needed));
			ASSERT_EQ(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), m_Executable.data(), needed, nullptr, nullptr), needed);
			os::TerminalDesc desc;
			desc.ExecutablePath = core::StringView(m_Executable);
			desc.MaximumInputBytes = 64;
			desc.MaximumOutputBytes = 8192;
			desc.MaximumPollBytes = 127;
			core::Result<core::InterfaceOwner<os::ITerminalSession>> created = os::CreateWindowsTerminalSession(desc, m_Memory);
			ASSERT_TRUE(created);
			m_Session = core::Move(created.Value());
			ASSERT_TRUE(awaitText("READY"));
		}

		void TearDown() override
		{
			if (m_Session)
			{
				m_Session->BeginClose();
				const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
				while (!m_Session->IsClosed() && rox::core::MonotonicClock::now() < deadline)
				{
					rox::core::SleepFor(rox::core::Milliseconds(2));
				}
				EXPECT_TRUE(m_Session->IsClosed());
				m_Session.Reset();
			}
			EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0);
		}

		bool awaitText(const char* expected)
		{
			const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
			while (rox::core::MonotonicClock::now() < deadline)
			{
				core::Result<os::TerminalOutput> output = m_Session->Poll(m_Memory);
				if (!output || output.Value().StreamFailure.Code != core::EErrorCode::None)
				{
					return false;
				}
				const bool bHasBytes = !output.Value().Bytes.IsEmpty();
				if (bHasBytes)
				{
					m_Output.append(reinterpret_cast<const char*>(output.Value().Bytes.Data()), output.Value().Bytes.Size());
				}
				if (m_Output.find(expected) != std::string::npos)
				{
					return true;
				}
				if (output.Value().bClosed)
				{
					break;
				}
				if (!bHasBytes)
				{
					rox::core::SleepFor(rox::core::Milliseconds(2));
				}
			}
			ADD_FAILURE() << "Expected " << expected << " in terminal output: " << m_Output;
			return false;
		}

	protected:
		core::TrackingMemoryResource m_Memory{ core::SystemMemoryResource::Get() };
		core::InterfaceOwner<os::ITerminalSession> m_Session;
		std::string m_Executable;
		std::string m_Output;
	};

	TEST_F(WindowsTerminalIoTests, WritesUtf8InputToChildAndReadsEcho)
	{
		ASSERT_TRUE(m_Session->Write(bytes("\xED\x95\x9C\xEA\xB8\x80\r")));
		EXPECT_TRUE(awaitText("ECHO:\xED\x95\x9C\xEA\xB8\x80"));
	}

	TEST_F(WindowsTerminalIoTests, ResizeIsObservedByChildAndInvalidSizeIsRejected)
	{
		const core::Result<void> invalid = m_Session->Resize({ 0, 20 });
		ASSERT_FALSE(invalid);
		EXPECT_EQ(invalid.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(invalid.GetError().DiagnosticID, "ROX-PLATFORM-TERMINAL");
		ASSERT_TRUE(m_Session->Resize({ 97, 31 }));
		ASSERT_TRUE(m_Session->Write(bytes("size\r")));
		EXPECT_TRUE(awaitText("SIZE:97:31"));
	}

	TEST_F(WindowsTerminalIoTests, OversizedInputIsRejectedWithoutConsumingFollowingInput)
	{
		const core::Result<void> invalid = m_Session->Write(bytes("012345678901234567890123456789012345678901234567890123456789012345678"));
		ASSERT_FALSE(invalid);
		EXPECT_EQ(invalid.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(invalid.GetError().DiagnosticID, "ROX-PLATFORM-TERMINAL");
		ASSERT_TRUE(m_Session->Write(bytes("valid\r")));
		EXPECT_TRUE(awaitText("ECHO:valid"));
		EXPECT_EQ(m_Output.find("ECHO:0123"), std::string::npos);
	}
	TEST_F(WindowsTerminalIoTests, ChildExitDrainsOutputAndRejectsFurtherWrites)
	{
		ASSERT_TRUE(m_Session->Write(bytes("exit\r")));
		bool bDrained = false;
		bool bExited = false;
		core::uint32 exitCode = 0;
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!bDrained && rox::core::MonotonicClock::now() < deadline)
		{
			core::Result<os::TerminalOutput> output = m_Session->Poll(m_Memory);
			ASSERT_TRUE(output);
			ASSERT_EQ(output.Value().StreamFailure.Code, core::EErrorCode::None);
			bExited = output.Value().bExited;
			exitCode = output.Value().ExitCode;
			bDrained = output.Value().bClosed;
			if (!bDrained)
			{
				rox::core::SleepFor(rox::core::Milliseconds(2));
			}
		}
		ASSERT_TRUE(bDrained);
		EXPECT_TRUE(bExited);
		EXPECT_EQ(exitCode, 37);
		const core::Result<void> rejected = m_Session->Write(bytes("after-exit"));
		ASSERT_FALSE(rejected);
		EXPECT_EQ(rejected.GetError().Code, core::EErrorCode::Cancelled);
		EXPECT_EQ(rejected.GetError().DiagnosticID, "ROX-PLATFORM-TERMINAL");
		m_Session->BeginClose();
		m_Session->BeginClose();
	}

	TEST_F(WindowsTerminalIoTests, CloseCompletesWithQueuedIoAndUnreadFloodOutput)
	{
		ASSERT_TRUE(m_Session->Write(bytes("flood\r")));
		ASSERT_TRUE(awaitText("FLOODING"));
		const rox::core::MonotonicClock::time_point outputDeadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (m_Output.size() < 16384 && rox::core::MonotonicClock::now() < outputDeadline)
		{
			core::Result<os::TerminalOutput> output = m_Session->Poll(m_Memory);
			ASSERT_TRUE(output);
			ASSERT_EQ(output.Value().StreamFailure.Code, core::EErrorCode::None);
			if (!output.Value().Bytes.IsEmpty())
			{
				m_Output.append(reinterpret_cast<const char*>(output.Value().Bytes.Data()), output.Value().Bytes.Size());
			}
			else
			{
				rox::core::SleepFor(rox::core::Milliseconds(2));
			}
		}
		ASSERT_GE(m_Output.size(), 16384);
		bool bSaturated = false;
		for (core::uint32 index = 0; index < 4096 && !bSaturated; ++index)
		{
			const core::Result<void> resized = m_Session->Resize({ 80 + index % 2, 24 });
			if (!resized)
			{
				EXPECT_EQ(resized.GetError().Code, core::EErrorCode::Conflict);
				EXPECT_EQ(resized.GetError().DiagnosticID, "ROX-PLATFORM-TERMINAL");
				bSaturated = true;
			}
		}
		ASSERT_TRUE(bSaturated);
		const rox::core::MonotonicClock::time_point start = rox::core::MonotonicClock::now();
		m_Session->BeginClose();
		EXPECT_LT(rox::core::MonotonicClock::now() - start, rox::core::Seconds(1));
		const rox::core::MonotonicClock::time_point closeDeadline = start + rox::core::Seconds(10);
		while (!m_Session->IsClosed() && rox::core::MonotonicClock::now() < closeDeadline)
		{
			rox::core::SleepFor(rox::core::Milliseconds(2));
		}
		ASSERT_TRUE(m_Session->IsClosed());
		bool bDrained = false;
		core::usize total = 0;
		for (core::uint32 index = 0; index < 128 && !bDrained; ++index)
		{
			core::Result<os::TerminalOutput> output = m_Session->Poll(m_Memory);
			ASSERT_TRUE(output);
			EXPECT_TRUE(output.Value().bClosing);
			EXPECT_LE(output.Value().Bytes.Size(), 127);
			total += output.Value().Bytes.Size();
			bDrained = output.Value().bClosed;
		}
		EXPECT_TRUE(bDrained);
		EXPECT_LE(total, 8192);
	}

	TEST_F(WindowsTerminalIoTests, PollAllocationFailurePreservesPendingOutput)
	{
		ASSERT_TRUE(m_Session->Write(bytes("allocation-check\r")));
		core::FailingMemoryResource failing(m_Memory, 0);
		bool bFailed = false;
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!bFailed && rox::core::MonotonicClock::now() < deadline)
		{
			core::Result<os::TerminalOutput> output = m_Session->Poll(failing);
			if (!output)
			{
				EXPECT_EQ(output.GetError().Code, core::EErrorCode::ExternalFailure);
				EXPECT_EQ(output.GetError().DiagnosticID, "ROX-PLATFORM-TERMINAL");
				bFailed = true;
			}
			else
			{
				rox::core::SleepFor(rox::core::Milliseconds(2));
			}
		}
		ASSERT_TRUE(bFailed);
		EXPECT_TRUE(awaitText("ECHO:allocation-check"));
	}

	TEST_F(WindowsTerminalIoTests, BurstOutputBackfillsInOrderAcrossBoundedPolls)
	{
		ASSERT_TRUE(m_Session->Write(bytes("burst\r")));
		ASSERT_TRUE(awaitText("BURST-END"));
		const rox::core::usize start = m_Output.find("BURST-START");
		const rox::core::usize end = m_Output.find("BURST-END");
		ASSERT_NE(start, std::string::npos);
		ASSERT_NE(end, std::string::npos);
		EXPECT_LT(start, end);
		EXPECT_GT(end - start, 8192);
	}
}
