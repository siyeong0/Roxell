#include <WindowsProcessService.h>
#include <Types.h>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>
#include <TypeVocabulary.hpp>
#include <WindowsExecutionEnvironment.h>
#include <CoreString.hpp>
#include <Sha256.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	std::filesystem::path getChildPath()
	{
		wchar_t executable[32768] = {};
		const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(rox::core::Extent<decltype(executable)>));
		if (length == 0 || length >= rox::core::Extent<decltype(executable)>)
		{
			return {};
		}
		return std::filesystem::path(executable).parent_path() / "Test-PlatformProcessChild.exe";
	}

	std::string pathToUtf8(const std::filesystem::path& path)
	{
		const std::u8string encoded = path.u8string();
		return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
	}

	bool waitForExit(core::Task<os::ProcessExit>& task)
	{
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!task.IsReady() && rox::core::MonotonicClock::now() < deadline)
		{
			rox::core::YieldThread();
		}
		return task.IsReady();
	}

	TEST(WindowsProcessServiceArgumentsTests, ChildReceivesEmptySpacesQuotesAndBackslashesExactly)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = pathToUtf8(getChildPath());
			ASSERT_FALSE(executable.empty());
			const core::StringView arguments[] = { "echo", "", "space value", "quote\"value", "trailing\\", "slashes\\\\\"quote" };
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = { executable.data(), executable.size() };
			desc.Arguments = { arguments, 6 };
			core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
			ASSERT_TRUE(launch.IsReady());
			core::Result<os::ProcessHandle> process = launch.TakeResult();
			ASSERT_TRUE(process);
			core::Task<os::ProcessExit> wait = service.Wait(process.Value(), tracking, {});
			ASSERT_TRUE(waitForExit(wait));
			core::Result<os::ProcessExit> exited = wait.TakeResult();
			ASSERT_TRUE(exited);
			EXPECT_EQ(exited.Value().ExitCode, 0u);
			const core::Array<core::Byte>& output = exited.Value().StandardOutput.Bytes;
			const core::Array<core::Byte>& error = exited.Value().StandardError.Bytes;
			EXPECT_EQ(std::string(reinterpret_cast<const char*>(output.Data()), output.Size()), "0:\n11:space value\n11:quote\"value\n9:trailing\\\n15:slashes\\\\\"quote\n");
			EXPECT_EQ(std::string(reinterpret_cast<const char*>(error.Data()), error.Size()), "stderr-line\n");
			EXPECT_TRUE(service.Release(process.Value()));
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

	TEST(WindowsProcessServiceArgumentsTests, CopiedAndModifiedChildReportsExactImageBytes)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		const rox::core::MonotonicClock::duration::rep nonce = rox::core::MonotonicClock::now().time_since_epoch().count();
		const std::filesystem::path copy = std::filesystem::temp_directory_path() / ("RoxellProcessImage-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(nonce) + ".exe");
		struct Cleanup final
		{
			std::filesystem::path Path;
			~Cleanup() { std::error_code ignored; std::filesystem::remove(Path, ignored); }
		} cleanup{ copy };
		std::error_code error;
		ASSERT_TRUE(std::filesystem::copy_file(getChildPath(), copy, error));
		ASSERT_FALSE(error);
		{
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = pathToUtf8(copy);
			const core::StringView arguments[] = { "image-hash" };
			for (core::uint32 iteration = 0; iteration < 2; ++iteration)
			{
				if (iteration == 1)
				{
					std::ofstream append(copy, std::ios::binary | std::ios::app);
					ASSERT_TRUE(append.put('X'));
				}
				std::ifstream input(copy, std::ios::binary | std::ios::ate);
				ASSERT_TRUE(input.good());
				const std::streampos size = input.tellg();
				ASSERT_GT(size, 0);
				std::vector<core::Byte> bytes(static_cast<rox::core::usize>(size));
				input.seekg(0);
				ASSERT_TRUE(input.read(reinterpret_cast<char*>(bytes.data()), size));
				input.close();
				const core::String expected = core::ToString(core::ComputeSha256(bytes));
				os::ProcessLaunchDesc desc;
				desc.ExecutablePath = { executable.data(), executable.size() };
				desc.Arguments = { arguments, 1 };
				core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
				ASSERT_TRUE(launch.IsReady());
				core::Result<os::ProcessHandle> process = launch.TakeResult();
				ASSERT_TRUE(process);
				core::Task<os::ProcessExit> wait = service.Wait(process.Value(), tracking, {});
				ASSERT_TRUE(waitForExit(wait));
				core::Result<os::ProcessExit> exited = wait.TakeResult();
				ASSERT_TRUE(exited);
				EXPECT_EQ(exited.Value().ExitCode, 0u);
				const core::Array<core::Byte>& output = exited.Value().StandardOutput.Bytes;
				EXPECT_EQ(std::string(reinterpret_cast<const char*>(output.Data()), output.Size()), std::string(expected.Data(), expected.Size()));
				EXPECT_FALSE(exited.Value().StandardOutput.bTruncated);
				EXPECT_FALSE(exited.Value().StandardOutput.bReadFailed);
				EXPECT_TRUE(service.Release(process.Value()));
			}
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}
	TEST(WindowsProcessServiceArgumentsTests, ChildReportsMatchingExecutionEnvironmentFingerprint)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			const core::Result<os::WindowsExecutionEnvironment> environment = os::ReadWindowsExecutionEnvironment();
			ASSERT_TRUE(environment);
			ASSERT_TRUE(environment.Value().bDefaultFloatingPointMode);
			ASSERT_TRUE(environment.Value().Fingerprint.IsValid());
			const core::String expected = core::ToString(environment.Value().Fingerprint);
			os::WindowsProcessService service;
			ASSERT_TRUE(os::WindowsProcessService::TryCreate(tracking, { 1 }, &service));
			const std::string executable = pathToUtf8(getChildPath());
			ASSERT_FALSE(executable.empty());
			const core::StringView arguments[] = { "environment-hash" };
			os::ProcessLaunchDesc desc;
			desc.ExecutablePath = { executable.data(), executable.size() };
			desc.Arguments = { arguments, 1 };
			core::Task<os::ProcessHandle> launch = service.Launch(desc, {});
			ASSERT_TRUE(launch.IsReady());
			core::Result<os::ProcessHandle> process = launch.TakeResult();
			ASSERT_TRUE(process);
			core::Task<os::ProcessExit> wait = service.Wait(process.Value(), tracking, {});
			ASSERT_TRUE(waitForExit(wait));
			core::Result<os::ProcessExit> exited = wait.TakeResult();
			ASSERT_TRUE(exited);
			EXPECT_EQ(exited.Value().ExitCode, 0u);
			const core::Array<core::Byte>& output = exited.Value().StandardOutput.Bytes;
			EXPECT_EQ(std::string(reinterpret_cast<const char*>(output.Data()), output.Size()), std::string(expected.Data(), expected.Size()));
			EXPECT_FALSE(exited.Value().StandardOutput.bTruncated);
			EXPECT_FALSE(exited.Value().StandardOutput.bReadFailed);
			EXPECT_FALSE(exited.Value().StandardOutput.bAllocationFailed);
			EXPECT_TRUE(service.Release(process.Value()));
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
		EXPECT_EQ(tracking.GetStatistics().LiveByteCount, 0u);
	}

} // namespace
