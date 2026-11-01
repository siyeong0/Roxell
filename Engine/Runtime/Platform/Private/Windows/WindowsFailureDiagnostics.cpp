#include <WindowsFailureDiagnostics.h>
#include <Assertion.h>
#include <TextPrimitives.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <Psapi.h>

namespace rox::os
{
	using namespace core;

	namespace
	{
		SRWLOCK s_CaptureLock = SRWLOCK_INIT;

		void emit(const LogEvent& event, const char* message) noexcept
		{
			WriteLog({ event.Record.Level, "FailureDiagnostics", message, {}, event.Record.Source });
		}
	} // namespace

	WindowsFailureDiagnostics::~WindowsFailureDiagnostics()
	{
		Stop();
	}

	void WindowsFailureDiagnostics::Start(StringView logPath, bool bRelativeToExecutable) noexcept
	{
		ASSERT(!m_bInstalled, "Failure diagnostics must be installed only once.");
		m_DumpPrefix[0] = 0;
		m_DumpCount = 0;
		m_bFatalDumpAttempted = false;
		char setting[16] = {};
		const DWORD settingSize = GetEnvironmentVariableA("ROXELL_FAILURE_DIAGNOSTICS", setting, sizeof(setting));
		bool bEnabled = IsFailureDiagnosticsEnabled();

		if (settingSize == 1 && (setting[0] == '0' || setting[0] == '1'))
		{
			bEnabled = setting[0] == '1';
		}

		m_bPreviousEnabled = SetFailureDiagnosticsEnabled(bEnabled);
		m_bFullMemory = GetEnvironmentVariableA("ROXELL_FULL_MEMORY_DUMPS", setting, sizeof(setting)) == 1 && setting[0] == '1';

		wchar_t path[1024] = {};

		if (!logPath.IsEmpty() && logPath.Size() < 1024)
		{
			const int count =
				MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, logPath.Data(), static_cast<int>(logPath.Size()), path, 1023);

			if (count > 0)
			{
				if (bRelativeToExecutable)
				{
					wchar_t executable[1024] = {};
					const DWORD length = GetModuleFileNameW(nullptr, executable, 1024);

					if (length > 0 && length < 1024)
					{
						for (DWORD index = length; index > 0; --index)
						{
							if (executable[index - 1] == L'\\')
							{
								executable[index] = 0;

								if (index + count < 1024)
								{
									lstrcpyW(m_DumpPrefix, executable);
									lstrcatW(m_DumpPrefix, path);
								}

								break;
							}
						}
					}
				}
				else
				{
					const DWORD length = GetFullPathNameW(path, 1024, m_DumpPrefix, nullptr);

					if (length == 0 || length >= 1024)
					{
						m_DumpPrefix[0] = 0;
					}
				}
			}
		}

		IFailureDiagnostics* previous = SetFailureDiagnostics(this);
		ASSERT(previous == nullptr, "Only one host may own failure diagnostics.");
		m_bInstalled = true;
	}

	void WindowsFailureDiagnostics::initializeProcess() noexcept
	{
		HANDLE process = nullptr;

		if (DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &process, 0, FALSE, DUPLICATE_SAME_ACCESS))
		{
			m_Process = process;
			m_bSymbols = SymInitialize(process, nullptr, TRUE) != FALSE;
		}
	}

	void WindowsFailureDiagnostics::Stop() noexcept
	{
		if (!m_bInstalled)
		{
			return;
		}

		IFailureDiagnostics* previous = SetFailureDiagnostics(nullptr);
		ASSERT(previous == this, "The host must remove its own failure diagnostics provider.");
		SetFailureDiagnosticsEnabled(m_bPreviousEnabled);
		AcquireSRWLockExclusive(&s_CaptureLock);

		if (m_bSymbols)
		{
			SymCleanup(m_Process);
		}

		if (m_Process != nullptr)
		{
			CloseHandle(m_Process);
		}

		m_Process = nullptr;
		m_bSymbols = false;
		m_bInstalled = false;
		ReleaseSRWLockExclusive(&s_CaptureLock);
	}

	void WindowsFailureDiagnostics::Capture(const LogEvent& event) noexcept
	{
		char message[512] = {};

		if (!TryAcquireSRWLockExclusive(&s_CaptureLock))
		{
			FormatText(message, sizeof(message), "event=%llu capture skipped: another capture is active.", event.Sequence);
			emit(event, message);
			void* rawFrames[32] = {};
			const USHORT count = CaptureStackBackTrace(1, 32, rawFrames, nullptr);

			for (USHORT index = 0; index < count; ++index)
			{
				FormatText(message, sizeof(message), "event=%llu frame=%u address=%p symbol=busy", event.Sequence, index,
					rawFrames[index]);
				emit(event, message);
			}

			return;
		}

		if (m_Process == nullptr)
		{
			initializeProcess();
		}

		PROCESS_MEMORY_COUNTERS_EX memory = {};
		memory.cb = sizeof(memory);

		if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
		{
			FormatText(message, sizeof(message), "event=%llu privateBytes=%llu workingSetBytes=%llu nativeThread=%lu", event.Sequence,
				static_cast<uint64>(memory.PrivateUsage), static_cast<uint64>(memory.WorkingSetSize), GetCurrentThreadId());
			emit(event, message);
		}

		void* frames[32] = {};
		const USHORT count = CaptureStackBackTrace(1, 32, frames, nullptr);

		for (USHORT index = 0; index < count; ++index)
		{
			alignas(SYMBOL_INFO) char symbolStorage[sizeof(SYMBOL_INFO) + 256] = {};
			SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 255;
			DWORD64 displacement = 0;
			const bool bNamed = m_bSymbols && SymFromAddr(m_Process, reinterpret_cast<DWORD64>(frames[index]), &displacement, symbol);
			FormatText(message, sizeof(message), "event=%llu frame=%u address=%p symbol=%s offset=%llu", event.Sequence, index, frames[index], bNamed ? symbol->Name : "unavailable", displacement);
			emit(event, message);
		}

		const bool bFatal = event.Record.Level == ELogLevel::Critical;

		if (bFatal ? !m_bFatalDumpAttempted : m_DumpCount < 2)
		{
			if (bFatal)
			{
				m_bFatalDumpAttempted = true;
			}
			else
			{
				++m_DumpCount;
			}

			wchar_t path[1152] = {};
			FormatWideText(path, 1152, L"%ls.%lu.%llu.%llu.dmp", m_DumpPrefix, GetCurrentProcessId(), GetTickCount64(), event.Sequence);
			HANDLE file = m_DumpPrefix[0] == 0 ? INVALID_HANDLE_VALUE : CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			bool bWritten = false;
			DWORD error = m_DumpPrefix[0] == 0 ? ERROR_INVALID_NAME : file == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;

			if (file != INVALID_HANDLE_VALUE)
			{
				const MINIDUMP_TYPE type =
					static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
						(m_bFullMemory ? MiniDumpWithFullMemory : MiniDumpWithIndirectlyReferencedMemory));
				bWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, nullptr, nullptr, nullptr) != FALSE;

				if (!bWritten)
				{
					error = GetLastError();
				}

				CloseHandle(file);

				if (!bWritten)
				{
					DeleteFileW(path);
				}
			}

			char utf8[512] = {};

			if (WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr) == 0)
			{
				FormatText(utf8, sizeof(utf8), "path exceeds log field; see the configured log directory");
			}

			FormatText(message, sizeof(message), "event=%llu dump=%s win32Error=%lu path=%.350s", event.Sequence,
				bWritten ? "written" : "failed", error, utf8);
			emit(event, message);
		}
		else
		{
			FormatText(message, sizeof(message), "event=%llu dump skipped: per-session limit reached.", event.Sequence);
			emit(event, message);
		}

		ReleaseSRWLockExclusive(&s_CaptureLock);
		FlushLog();
	}
} // namespace rox::os
