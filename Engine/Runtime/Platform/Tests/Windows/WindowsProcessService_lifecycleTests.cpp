#include <WindowsProcessService.h>
#include <Vocabulary.hpp>
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

#include <filesystem>
#include <fstream>
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

	TEST(WindowsProcessServiceLifecycleTests, PrecancelledLaunchLeavesCapacityAvailable)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = getPowerShell();
			ASSERT_FALSE(executable.empty());
			const core::StringView arguments[] = { "-NoProfile", "-NonInteractive", "-Command", "exit 0" };
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = { executable.data(), executable.size() };
			desc.Arguments = { arguments, 4 };
			core::CancellationSource cancellation;
			ASSERT_TRUE(cancellation.IsValid());
			cancellation.RequestCancellation();
			core::Task<os::ProcessHandle> cancelledTask = service.Launch(desc, cancellation.GetToken());
			ASSERT_TRUE(cancelledTask.IsReady());
			core::Result<os::ProcessHandle> cancelled = cancelledTask.TakeResult();
			ASSERT_FALSE(cancelled);
			EXPECT_EQ(cancelled.GetError().Code, core::EErrorCode::Cancelled);
			core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
			ASSERT_TRUE(launch.IsReady());
			core::Result<os::ProcessHandle> process = launch.TakeResult();
			ASSERT_TRUE(process);
			core::Task<os::ProcessExit> wait = service.Wait(process.Value(), tracking, {});
			ASSERT_TRUE(waitForTask(wait));
			core::Result<os::ProcessExit> exited = wait.TakeResult();
			ASSERT_TRUE(exited);
			EXPECT_EQ(exited.Value().ExitCode, 0u);
			EXPECT_TRUE(service.Release(process.Value()));
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceLifecycleTests, ActiveChildRejectsReleaseAndTerminationReleasesCapacity)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			os::WindowsProcessService service;
			os::WindowsProcessService other;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &other));
			const std::string executable = getPowerShell();
			ASSERT_FALSE(executable.empty());
			const core::StringView arguments[] = { "-NoProfile", "-NonInteractive", "-Command", "Start-Sleep -Seconds 30" };
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = { executable.data(), executable.size() };
			desc.Arguments = { arguments, 4 };
			core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
			ASSERT_TRUE(launch.IsReady());
			core::Result<os::ProcessHandle> process = launch.TakeResult();
			ASSERT_TRUE(process);
			const core::Result<void> wrongOwner = other.RequestTermination(process.Value());
			ASSERT_FALSE(wrongOwner);
			EXPECT_EQ(wrongOwner.GetError().Code, core::EErrorCode::PermissionDenied);
			EXPECT_EQ(wrongOwner.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-HANDLE");
			const core::Result<void> activeRelease = service.Release(process.Value());
			ASSERT_FALSE(activeRelease);
			EXPECT_EQ(activeRelease.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-RELEASE-ACTIVE");
			core::Task<os::ProcessHandle> capacityTask = service.Launch(desc, {});
			ASSERT_TRUE(capacityTask.IsReady());
			core::Result<os::ProcessHandle> capacity = capacityTask.TakeResult();
			ASSERT_FALSE(capacity);
			EXPECT_EQ(capacity.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-CAPACITY");
			ASSERT_TRUE(service.RequestTermination(process.Value()));
			EXPECT_TRUE(service.RequestTermination(process.Value()));
			core::Task<os::ProcessExit> wait = service.Wait(process.Value(), tracking, {});
			ASSERT_TRUE(waitForTask(wait));
			core::Result<os::ProcessExit> exited = wait.TakeResult();
			ASSERT_TRUE(exited);
			EXPECT_TRUE(exited.Value().bTerminationRequested);
			ASSERT_TRUE(service.Release(process.Value()));
			core::Task<os::ProcessExit> staleTask = service.Wait(process.Value(), tracking, {});
			ASSERT_TRUE(staleTask.IsReady());
			core::Result<os::ProcessExit> stale = staleTask.TakeResult();
			ASSERT_FALSE(stale);
			EXPECT_EQ(stale.GetError().Code, core::EErrorCode::Conflict);
			EXPECT_EQ(stale.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-HANDLE");
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceLifecycleTests, CaptureLimitTruncatesBothStreamsWithoutReadFailure)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = getPowerShell();
			ASSERT_FALSE(executable.empty());
			const core::StringView arguments[] = { "-NoProfile", "-NonInteractive", "-Command", "[Console]::Out.Write(('o' * 65536)); [Console]::Error.Write(('e' * 65536)); exit 0" };
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = { executable.data(), executable.size() };
			desc.Arguments = { arguments, 4 };
			desc.MaximumCapturedOutputBytesPerStream = 1024;
			core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
			ASSERT_TRUE(launch.IsReady());
			core::Result<os::ProcessHandle> process = launch.TakeResult();
			ASSERT_TRUE(process);
			core::Task<os::ProcessExit> wait = service.Wait(process.Value(), tracking, {});
			ASSERT_TRUE(waitForTask(wait));
			core::Result<os::ProcessExit> exited = wait.TakeResult();
			ASSERT_TRUE(exited);
			EXPECT_EQ(exited.Value().ExitCode, 0u);
			EXPECT_EQ(exited.Value().StandardOutput.Bytes.Size(), 1024u);
			EXPECT_EQ(exited.Value().StandardError.Bytes.Size(), 1024u);
			EXPECT_TRUE(exited.Value().StandardOutput.bTruncated);
			EXPECT_TRUE(exited.Value().StandardError.bTruncated);
			EXPECT_FALSE(exited.Value().StandardOutput.bReadFailed);
			EXPECT_FALSE(exited.Value().StandardError.bReadFailed);
			EXPECT_TRUE(service.Release(process.Value()));
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}
	TEST(WindowsProcessServiceLifecycleTests, FailedResultAllocationCanBeRetried)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = getPowerShell();
			ASSERT_FALSE(executable.empty());
			const core::StringView arguments[] = { "-NoProfile", "-NonInteractive", "-Command", "[Console]::Out.Write('allocation'); exit 0" };
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = { executable.data(), executable.size() };
			desc.Arguments = { arguments, 4 };
			core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
			ASSERT_TRUE(launch.IsReady());
			core::Result<os::ProcessHandle> process = launch.TakeResult();
			ASSERT_TRUE(process);
			core::FailingMemoryResource failing(tracking, 0);
			core::Task<os::ProcessExit> failedTask = service.Wait(process.Value(), failing, {});
			ASSERT_TRUE(waitForTask(failedTask));
			core::Result<os::ProcessExit> failed = failedTask.TakeResult();
			ASSERT_FALSE(failed);
			EXPECT_EQ(failed.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-RESULT-MEMORY");
			core::Task<os::ProcessExit> retryTask = service.Wait(process.Value(), tracking, {});
			ASSERT_TRUE(waitForTask(retryTask));
			core::Result<os::ProcessExit> exited = retryTask.TakeResult();
			ASSERT_TRUE(exited);
			EXPECT_EQ(exited.Value().ExitCode, 0u);
			const core::Array<core::Byte>& bytes = exited.Value().StandardOutput.Bytes;
			EXPECT_EQ(std::string(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()), "allocation");
			EXPECT_TRUE(service.Release(process.Value()));
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceLifecycleTests, CancelledWaitLeavesChildAvailableForLaterWait)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = getPowerShell();
			ASSERT_FALSE(executable.empty());
			const core::StringView arguments[] = { "-NoProfile", "-NonInteractive", "-Command", "Start-Sleep -Seconds 2; exit 0" };
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = { executable.data(), executable.size() };
			desc.Arguments = { arguments, 4 };
			core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
			ASSERT_TRUE(launch.IsReady());
			core::Result<os::ProcessHandle> process = launch.TakeResult();
			ASSERT_TRUE(process);
			core::CancellationSource cancellation;
			ASSERT_TRUE(cancellation.IsValid());
			core::Task<os::ProcessExit> cancelledTask = service.Wait(process.Value(), tracking, cancellation.GetToken());
			cancellation.RequestCancellation();
			ASSERT_TRUE(waitForTask(cancelledTask));
			core::Result<os::ProcessExit> cancelled = cancelledTask.TakeResult();
			ASSERT_FALSE(cancelled);
			EXPECT_EQ(cancelled.GetError().Code, core::EErrorCode::Cancelled);
			EXPECT_EQ(cancelled.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-WAIT-CANCELLED");
			core::Task<os::ProcessExit> retryTask = service.Wait(process.Value(), tracking, {});
			ASSERT_TRUE(waitForTask(retryTask));
			core::Result<os::ProcessExit> exited = retryTask.TakeResult();
			ASSERT_TRUE(exited);
			EXPECT_EQ(exited.Value().ExitCode, 0u);
			EXPECT_FALSE(exited.Value().bTerminationRequested);
			EXPECT_TRUE(service.Release(process.Value()));
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceLifecycleTests, TerminationAlsoRetiresChildProcessTree)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		const rox::core::MonotonicClock::duration::rep nonce = rox::core::MonotonicClock::now().time_since_epoch().count();
		const std::filesystem::path pidFile = std::filesystem::temp_directory_path() / ("RoxellProcessTree-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(nonce) + ".txt");
		struct Cleanup final
		{
			std::filesystem::path Path;
			~Cleanup() { std::error_code ignored; std::filesystem::remove(Path, ignored); }
		} cleanup{ pidFile };
		const std::u8string encoded = pidFile.u8string();
		const std::string pidText(reinterpret_cast<const char*>(encoded.data()), encoded.size());
		std::string escapedPath;
		for (const char character : pidText)
		{
			escapedPath += character;
			if (character == '\'')
			{
				escapedPath += character;
			}
		}
		const std::string script =
			"$p = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') "
			"-ArgumentList '-NoProfile -NonInteractive -Command \"Start-Sleep -Seconds 30\"' -PassThru; "
			"[IO.File]::WriteAllText('" + escapedPath + "', [string]$p.Id); Start-Sleep -Seconds 30";
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = getPowerShell();
			ASSERT_FALSE(executable.empty());
			const core::StringView arguments[] = { "-NoProfile", "-NonInteractive", "-Command", { script.data(), script.size() } };
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = { executable.data(), executable.size() };
			desc.Arguments = { arguments, 4 };
			core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
			ASSERT_TRUE(launch.IsReady());
			core::Result<os::ProcessHandle> process = launch.TakeResult();
			ASSERT_TRUE(process);
			DWORD childID = 0;
			const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
			while (childID == 0 && rox::core::MonotonicClock::now() < deadline)
			{
				std::ifstream input(pidFile);
				input >> childID;
				rox::core::YieldThread();
			}
			ASSERT_NE(childID, 0u);
			HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, childID);
			ASSERT_NE(child, nullptr);
			struct ChildHandle final
			{
				HANDLE Value;
				~ChildHandle() { CloseHandle(Value); }
			} childGuard{ child };
			ASSERT_EQ(WaitForSingleObject(child, 0), WAIT_TIMEOUT);
			ASSERT_TRUE(service.RequestTermination(process.Value()));
			core::Task<os::ProcessExit> wait = service.Wait(process.Value(), tracking, {});
			ASSERT_TRUE(waitForTask(wait));
			core::Result<os::ProcessExit> exited = wait.TakeResult();
			ASSERT_TRUE(exited);
			EXPECT_TRUE(exited.Value().bTerminationRequested);
			EXPECT_EQ(WaitForSingleObject(child, 10'000), WAIT_OBJECT_0);
			EXPECT_TRUE(service.Release(process.Value()));
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceLifecycleTests, DestroyingServiceCancelsPendingWait)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			rox::core::Optional<core::Task<os::ProcessExit>> pending;
			{
				os::WindowsProcessService service;
				ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
				const std::string executable = getPowerShell();
				ASSERT_FALSE(executable.empty());
				const core::StringView arguments[] = { "-NoProfile", "-NonInteractive", "-Command", "Start-Sleep -Seconds 30" };
				os::ProcessLaunchDesc desc;
				desc.ExecutablePath = { executable.data(), executable.size() };
				desc.Arguments = { arguments, 4 };
				core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
				ASSERT_TRUE(launch.IsReady());
				core::Result<os::ProcessHandle> process = launch.TakeResult();
				ASSERT_TRUE(process);
				pending.emplace(service.Wait(process.Value(), tracking, {}));
				ASSERT_FALSE(pending->IsReady());
			}
			ASSERT_TRUE(waitForTask(*pending));
			core::Result<os::ProcessExit> cancelled = pending->TakeResult();
			ASSERT_FALSE(cancelled);
			EXPECT_EQ(cancelled.GetError().Code, core::EErrorCode::Cancelled);
			EXPECT_EQ(cancelled.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-WAIT-CANCELLED");
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceLifecycleTests, ReusedSlotHasNewGenerationAndOldHandleStaysInvalid)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = getPowerShell();
			ASSERT_FALSE(executable.empty());
			const core::StringView arguments[] = { "-NoProfile", "-NonInteractive", "-Command", "exit 0" };
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = { executable.data(), executable.size() };
			desc.Arguments = { arguments, 4 };
			core::Task<os::ProcessHandle> firstLaunch = service.Launch(desc, {});
			ASSERT_TRUE(firstLaunch.IsReady());
			core::Result<os::ProcessHandle> first = firstLaunch.TakeResult();
			ASSERT_TRUE(first);
			core::Task<os::ProcessExit> firstWait = service.Wait(first.Value(), tracking, {});
			ASSERT_TRUE(waitForTask(firstWait));
			core::Result<os::ProcessExit> firstExit = firstWait.TakeResult();
			ASSERT_TRUE(firstExit);
			ASSERT_TRUE(service.Release(first.Value()));
			core::Task<os::ProcessHandle> nextLaunch = service.Launch(desc, {});
			ASSERT_TRUE(nextLaunch.IsReady());
			core::Result<os::ProcessHandle> next = nextLaunch.TakeResult();
			ASSERT_TRUE(next);
			EXPECT_EQ(next.Value().Slot, first.Value().Slot);
			EXPECT_NE(next.Value().Generation, first.Value().Generation);
			const core::Result<void> stale = service.RequestTermination(first.Value());
			ASSERT_FALSE(stale);
			EXPECT_EQ(stale.GetError().Code, core::EErrorCode::Conflict);
			EXPECT_EQ(stale.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-HANDLE");
			core::Task<os::ProcessExit> nextWait = service.Wait(next.Value(), tracking, {});
			ASSERT_TRUE(waitForTask(nextWait));
			core::Result<os::ProcessExit> nextExit = nextWait.TakeResult();
			ASSERT_TRUE(nextExit);
			EXPECT_EQ(nextExit.Value().ExitCode, 0u);
			EXPECT_TRUE(service.Release(next.Value()));
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceLifecycleTests, RejectsInvalidDescriptorsAndMissingExecutable)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = getPowerShell();
			ASSERT_FALSE(executable.empty());
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = { executable.data(), executable.size() };
			desc.MaximumCapturedOutputBytesPerStream = 0;
			core::Task<os::ProcessHandle> zeroTask = service.Launch(desc, {});
			ASSERT_TRUE(zeroTask.IsReady());
			core::Result<os::ProcessHandle> zero = zeroTask.TakeResult();
			ASSERT_FALSE(zero);
			EXPECT_EQ(zero.GetError().Code, core::EErrorCode::InvalidArgument);
			EXPECT_EQ(zero.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-DESC");
			constexpr char INVALID_UTF8[] = { static_cast<char>(0xc0), static_cast<char>(0xaf) };
			desc.MaximumCapturedOutputBytesPerStream = os::DEFAULT_MAXIMUM_CAPTURED_PROCESS_OUTPUT_BYTES;
			desc.ExecutablePath = { INVALID_UTF8, sizeof(INVALID_UTF8) };
			core::Task<os::ProcessHandle> utf8Task = service.Launch(desc, {});
			ASSERT_TRUE(utf8Task.IsReady());
			core::Result<os::ProcessHandle> utf8 = utf8Task.TakeResult();
			ASSERT_FALSE(utf8);
			EXPECT_EQ(utf8.GetError().Code, core::EErrorCode::InvalidArgument);
			EXPECT_EQ(utf8.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-DESC");
			const std::string missing = executable + ".missing.exe";
			desc.ExecutablePath = { missing.data(), missing.size() };
			core::Task<os::ProcessHandle> missingTask = service.Launch(desc, {});
			ASSERT_TRUE(missingTask.IsReady());
			core::Result<os::ProcessHandle> absent = missingTask.TakeResult();
			ASSERT_FALSE(absent);
			EXPECT_EQ(absent.GetError().Code, core::EErrorCode::NotFound);
			EXPECT_EQ(absent.GetError().DiagnosticID, "ROX-PLATFORM-PROCESS-NOT-FOUND");
			EXPECT_TRUE(service.IsValid());
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceLifecycleTests, DestroyingServiceTerminatesOwnedChild)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		const rox::core::MonotonicClock::duration::rep nonce = rox::core::MonotonicClock::now().time_since_epoch().count();
		const std::filesystem::path pidFile = std::filesystem::temp_directory_path() / ("RoxellProcessOwner-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(nonce) + ".txt");
		struct Cleanup final
		{
			std::filesystem::path Path;
			~Cleanup() { std::error_code ignored; std::filesystem::remove(Path, ignored); }
		} cleanup{ pidFile };
		const std::u8string encoded = pidFile.u8string();
		const std::string pidText(reinterpret_cast<const char*>(encoded.data()), encoded.size());
		std::string escapedPath;
		for (const char character : pidText)
		{
			escapedPath += character;
			if (character == '\'')
			{
				escapedPath += character;
			}
		}
		const std::string script = "[IO.File]::WriteAllText('" + escapedPath + "', [string]$PID); Start-Sleep -Seconds 30";
		HANDLE child = nullptr;
		struct ChildHandle final
		{
			HANDLE* Value;
			~ChildHandle() { if (*Value != nullptr) { CloseHandle(*Value); } }
		} guard{ &child };
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = getPowerShell();
			ASSERT_FALSE(executable.empty());
			const core::StringView arguments[] = { "-NoProfile", "-NonInteractive", "-Command", { script.data(), script.size() } };
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = { executable.data(), executable.size() };
			desc.Arguments = { arguments, 4 };
			core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
			ASSERT_TRUE(launch.IsReady());
			core::Result<os::ProcessHandle> process = launch.TakeResult();
			ASSERT_TRUE(process);
			DWORD childID = 0;
			const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
			while (childID == 0 && rox::core::MonotonicClock::now() < deadline)
			{
				std::ifstream input(pidFile);
				input >> childID;
				rox::core::YieldThread();
			}
			ASSERT_NE(childID, 0u);
			child = OpenProcess(SYNCHRONIZE, FALSE, childID);
			ASSERT_NE(child, nullptr);
			ASSERT_EQ(WaitForSingleObject(child, 0), WAIT_TIMEOUT);
		}
		EXPECT_EQ(WaitForSingleObject(child, 10'000), WAIT_OBJECT_0);
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

} // namespace
