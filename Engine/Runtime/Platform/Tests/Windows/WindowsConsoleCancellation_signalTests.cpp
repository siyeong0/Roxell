#include <gtest/gtest.h>
#include <TypeVocabulary.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <filesystem>
#include <string>

namespace
{
	TEST(WindowsConsoleCancellationSignalTests, CtrlBreakCancelsOnlyTheIsolatedChildSubscription)
	{
		wchar_t currentExecutable[32768] = {};
		const DWORD length = GetModuleFileNameW(nullptr, currentExecutable, static_cast<DWORD>(rox::core::Extent<decltype(currentExecutable)>));
		ASSERT_GT(length, 0u);
		ASSERT_LT(length, rox::core::Extent<decltype(currentExecutable)>);
		const std::filesystem::path child = std::filesystem::path(currentExecutable).parent_path() / "Test-ConsoleCancellationChild.exe";
		ASSERT_TRUE(std::filesystem::exists(child));
		std::wstring command = L"\"" + child.wstring() + L"\"";
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESHOWWINDOW;
		startup.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION process{};
		ASSERT_TRUE(CreateProcessW(child.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &startup, &process));
		struct ChildGuard final
		{
			PROCESS_INFORMATION Process;

			~ChildGuard()
			{
				if (WaitForSingleObject(Process.hProcess, 0) == WAIT_TIMEOUT)
				{
					TerminateProcess(Process.hProcess, 99);
					WaitForSingleObject(Process.hProcess, 10'000);
				}
				CloseHandle(Process.hThread);
				CloseHandle(Process.hProcess);
			}
		} guard{ process };
		ASSERT_EQ(WaitForSingleObject(process.hProcess, 15'000), WAIT_OBJECT_0) << "Isolated console child did not finish";
		DWORD exitCode = 0;
		ASSERT_TRUE(GetExitCodeProcess(process.hProcess, &exitCode));
		EXPECT_EQ(exitCode, 0u) << "Child exit: 10=no console, 11=source, 12=subscription, 13=premature cancellation, 14=GenerateConsoleCtrlEvent, 15=delivery timeout, 16=FreeConsole";
	}
} // namespace
