#include <WindowsProcessService.h>
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
	namespace core = rox::core;
	namespace os = rox::os;

	std::string getPowerShell()
	{
		wchar_t directory[MAX_PATH] = {};
		const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
		{
			return {};
		}
		const std::wstring path = std::wstring(directory, length) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
		const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
		if (required <= 0)
		{
			return {};
		}
		std::string result(static_cast<rox::core::usize>(required), '\0');
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), result.data(), required, nullptr, nullptr);
		return result;
	}

	bool waitForTask(core::Task<os::ProcessExit>& task)
	{
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!task.IsReady() && rox::core::MonotonicClock::now() < deadline)
		{
			rox::core::YieldThread();
		}
		return task.IsReady();
	}

	TEST(WindowsProcessServiceTests, RejectsOutOfRangeCapacityWithoutPublishingService)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		for (const core::uint32 capacity : { 0u, os::MAXIMUM_PROCESS_COUNT + 1 })
		{
			os::WindowsProcessService service;
			const core::Result<void> result = os::WindowsProcessService::TryCreate(tracking, { capacity }, &service);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.GetError().Code, core::EErrorCode::InvalidArgument);
			EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-DESC");
			EXPECT_FALSE(service.IsValid());
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceTests, AllocationFailureLeavesServiceEmptyAndReleasesStorage)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		core::FailingMemoryResource failing(tracking, 0);
		os::WindowsProcessService service;
		const core::Result<void> result = os::WindowsProcessService::TryCreate(failing, { 1 }, &service);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().Code, core::EErrorCode::ExternalFailure);
		EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-MEMORY");
		EXPECT_FALSE(service.IsValid());
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceTests, MoveTransfersOwnershipAndDestructionReleasesStorage)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			os::WindowsProcessService original;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &original));
			os::WindowsProcessService moved(core::Move(original));
			EXPECT_FALSE(original.IsValid());
			EXPECT_TRUE(moved.IsValid());
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceTests, RejectsRelativeExecutableWithoutInvalidatingService)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = "relative.exe";
			core::Task<os::ProcessHandle> task = service.Launch(desc, {});
			ASSERT_TRUE(task.IsReady());
			const core::Result<os::ProcessHandle> result = task.TakeResult();
			ASSERT_FALSE(result);
			EXPECT_EQ(result.GetError().Code, core::EErrorCode::InvalidArgument);
			EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-DESC");
			EXPECT_TRUE(service.IsValid());
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}
	TEST(WindowsProcessServiceTests, CapturesChildOutputAndExitCodeBeforeRelease)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = getPowerShell();
			ASSERT_FALSE(executable.empty());
			const core::StringView arguments[] = { "-NoProfile", "-NonInteractive", "-Command", "[Console]::Out.WriteLine('roxell-test'); exit 37" };
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = core::StringView(executable.data(), executable.size());
			desc.Arguments = { arguments, 4 };
			core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
			ASSERT_TRUE(launch.IsReady());
			core::Result<os::ProcessHandle> launched = launch.TakeResult();
			ASSERT_TRUE(launched);
			core::Task<os::ProcessExit> wait = service.Wait(launched.Value(), tracking, {});
			ASSERT_TRUE(waitForTask(wait));
			core::Result<os::ProcessExit> exited = wait.TakeResult();
			ASSERT_TRUE(exited);
			const core::Array<core::Byte>& errorBytes = exited.Value().StandardError.Bytes;
			const std::string errorText(reinterpret_cast<const char*>(errorBytes.Data()), errorBytes.Size());
			EXPECT_EQ(exited.Value().ExitCode, 37u) << errorText;
			EXPECT_FALSE(exited.Value().bTerminationRequested);
			const core::Array<core::Byte>& bytes = exited.Value().StandardOutput.Bytes;
			const std::string output(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
			EXPECT_EQ(output, "roxell-test\r\n") << errorText;
			EXPECT_TRUE(exited.Value().StandardError.Bytes.IsEmpty());
			EXPECT_TRUE(service.Release(launched.Value()));
			const core::Result<void> stale = service.Release(launched.Value());
			ASSERT_FALSE(stale);
			EXPECT_EQ(stale.GetError().Code, core::EErrorCode::Conflict);
			EXPECT_EQ(stale.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-HANDLE");
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

} // namespace
