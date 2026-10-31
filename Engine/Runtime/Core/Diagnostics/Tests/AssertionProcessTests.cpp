#include <Assertion.h>

#include <cstdio>
#include <cstring>
#include <gtest/gtest.h>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
	void triggerAssertion() noexcept
	{
		ASSERT(false, "Assertion child probe.");
	}

	TEST(AssertionProcessTests, TerminatesAndReports)
	{
		std::string diagnostic;
		bool bFailed = false;
#if defined(_WIN32)
		SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
		HANDLE reader = nullptr;
		HANDLE writer = nullptr;
		ASSERT_TRUE(CreatePipe(&reader, &writer, &security, 0));
		if (!SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0))
		{
			CloseHandle(reader);
			CloseHandle(writer);
			FAIL() << "Assertion child setup failed.";
		}
		wchar_t executable[32768]{};
		const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
		if (length == 0 || length >= 32768)
		{
			CloseHandle(reader);
			CloseHandle(writer);
			FAIL() << "Assertion child setup failed.";
		}
		std::wstring command = L"\"" + std::wstring(executable, length) + L"\" --assert-child";
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
		                           FILE_ATTRIBUTE_NORMAL, nullptr);
		if (input == INVALID_HANDLE_VALUE)
		{
			CloseHandle(reader);
			CloseHandle(writer);
			FAIL() << "Assertion child setup failed.";
		}
		startup.hStdInput = input;
		startup.hStdOutput = writer;
		startup.hStdError = writer;
		PROCESS_INFORMATION process{};
		const BOOL bStarted = CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
		                                    nullptr, nullptr, &startup, &process);
		const DWORD startError = bStarted ? 0 : GetLastError();
		CloseHandle(input);
		CloseHandle(writer);
		if (!bStarted)
		{
			CloseHandle(reader);
			std::fprintf(stderr, "Unable to start assertion child (error=%lu).\n", static_cast<unsigned long>(startError));
			FAIL() << "Assertion child setup failed.";
		}
		const DWORD waitResult = WaitForSingleObject(process.hProcess, 10000);
		if (waitResult != WAIT_OBJECT_0)
		{
			TerminateProcess(process.hProcess, 1);
			WaitForSingleObject(process.hProcess, 5000);
		}
		DWORD exitCode = 0;
		GetExitCodeProcess(process.hProcess, &exitCode);
		bFailed = waitResult == WAIT_OBJECT_0 && exitCode != 0;
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		char buffer[1024];
		DWORD count = 0;
		while (ReadFile(reader, buffer, sizeof(buffer), &count, nullptr) && count != 0)
		{
			diagnostic.append(buffer, count);
		}
		CloseHandle(reader);
#else
		int handles[2];
		ASSERT_TRUE(pipe(handles) == 0);
		const pid_t child = fork();
		if (child == 0)
		{
			const rlimit noCore{0, 0};
			setrlimit(RLIMIT_CORE, &noCore);
			close(handles[0]);
			dup2(handles[1], STDERR_FILENO);
			close(handles[1]);
			triggerAssertion();
			_exit(0);
		}
		close(handles[1]);
		if (child < 0)
		{
			close(handles[0]);
			FAIL() << "Assertion child setup failed.";
		}
		char buffer[1024];
		ssize_t count = 0;
		while ((count = read(handles[0], buffer, sizeof(buffer))) > 0)
		{
			diagnostic.append(buffer, static_cast<std::size_t>(count));
		}
		close(handles[0]);
		int status = 0;
		const pid_t waited = waitpid(child, &status, 0);
		bFailed = waited == child && (WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0));
#endif
		const bool bHasDiagnostic = diagnostic.find("\"level\":\"critical\"") != std::string::npos &&
		                            diagnostic.find("\"category\":\"Assert\"") != std::string::npos &&
		                            diagnostic.find("\"expression\":\"false\"") != std::string::npos &&
		                            diagnostic.find("Assertion child probe.") != std::string::npos &&
		                            diagnostic.find("AssertionProcessTests.cpp") != std::string::npos;
		if (!bFailed || !bHasDiagnostic)
		{
			std::fprintf(stderr, "Assertion child failed=%d, diagnostic:\n%s\n", bFailed ? 1 : 0, diagnostic.c_str());
		}
		ASSERT_TRUE(bFailed && bHasDiagnostic);
	}
}

int main(int argc, char** argv)
{
#if defined(_WIN32)
	if (argc == 2 && std::strcmp(argv[1], "--assert-child") == 0)
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		triggerAssertion();
		return 0;
	}
#endif
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
