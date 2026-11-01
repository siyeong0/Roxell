#include <WindowsTerminalSession.h>

#include <Algorithms.hpp>
#include <ClockPrimitives.hpp>
#include <Comparison.hpp>
#include <CoreString.hpp>
#include <Synchronization.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utf8.h>
#include <Utility.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <icu.h>
#include <process.h>

namespace rox::os
{
	using namespace core;

	namespace
	{
		Error failure(EErrorCode code, const char* operation, DWORD native = 0)
		{
			char message[192];
			FormatText(message, sizeof(message), "%s (Windows error %lu).", operation, native);

			return MakeError(code, "ROX-PLATFORM-TERMINAL", StringView(message, CStringLength(message)), "Close the session or retry after draining queued output.");
		}

		bool validSize(TerminalSize size) noexcept
		{
			return size.Columns >= 1 && size.Columns <= 512 && size.Rows >= 1 && size.Rows <= 256;
		}

		void closeHandle(HANDLE& handle) noexcept
		{
			if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
			{
				ASSERT(CloseHandle(handle) != FALSE, "Terminal handle must close.");
			}

			handle = nullptr;
		}

		Result<Array<wchar_t>> wide(StringView text, MemoryResource& memory)
		{
			if (text.Size() > 131'064 || !IsValidUtf8(text) || Find(text.begin(), text.end(), '\0') != text.end())
			{
				return Result<Array<wchar_t>>::Failure(failure(EErrorCode::InvalidArgument, "Terminal path or argument is invalid"));
			}

			const int count = text.IsEmpty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Size()), nullptr, 0);
			Array<wchar_t> result(memory);

			if ((!text.IsEmpty() && count == 0) || !result.Resize(static_cast<usize>(count) + 1))
			{
				return Result<Array<wchar_t>>::Failure(failure(EErrorCode::ExternalFailure, "Terminal UTF-16 conversion allocation failed", GetLastError()));
			}

			if (count != 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Size()), result.Data(), count) != count)
			{
				return Result<Array<wchar_t>>::Failure(failure(EErrorCode::ExternalFailure, "Terminal UTF-16 conversion failed", GetLastError()));
			}

			result[static_cast<usize>(count)] = 0;

			return Result<Array<wchar_t>>::Success(Move(result));
		}

		struct Command final
		{
		public:
			bool bResize = false;
			TerminalSize Size;
			uint32 Bytes = 0;
		};

		class Session final : public ITerminalSession
		{
		public:
			explicit Session(MemoryResource& memory) noexcept
				: m_Input(memory)
				, m_Output(memory)
				, m_Commands(memory)
			{
			}

			~Session() noexcept override
			{
				BeginClose();
				closeHandle(m_ConsoleInput);
				closeHandle(m_ConsoleOutput);

				if (m_ControlThread == nullptr && m_Console != nullptr)
				{
					if (m_ReadThread == nullptr)
					{
						closeHandle(m_OutputRead);
					}

					ClosePseudoConsole(m_Console);
					m_Console = nullptr;
				}

				if (m_ControlThread != nullptr)
				{
					ASSERT(WaitForSingleObject(m_ControlThread, INFINITE) == WAIT_OBJECT_0, "Terminal control thread must join.");
				}

				if (m_ReadThread != nullptr)
				{
					ASSERT(WaitForSingleObject(m_ReadThread, INFINITE) == WAIT_OBJECT_0, "Terminal reader thread must join.");
				}

				if (m_Console != nullptr)
				{
					ClosePseudoConsole(m_Console);
					m_Console = nullptr;
				}

				closeHandle(m_ControlThread);
				closeHandle(m_ReadThread);
				closeHandle(m_InputWrite);
				closeHandle(m_OutputRead);
				closeHandle(m_Process);
				closeHandle(m_Job);
			}

			Result<void> Initialize(const TerminalDesc& desc, MemoryResource& memory)
			{
				m_PollMaximum = desc.MaximumPollBytes;

				if (!m_Input.Resize(desc.MaximumInputBytes) || !m_Output.Resize(desc.MaximumOutputBytes) || !m_Commands.Resize(desc.MaximumQueuedCommands))
				{
					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Terminal queue allocation failed"));
				}

				Result<Array<wchar_t>> executable = wide(desc.ExecutablePath, memory);

				if (!executable)
				{
					return Result<void>::Failure(executable.GetError());
				}

				Result<Array<wchar_t>> directory = wide(desc.WorkingDirectoryPath, memory);

				if (!directory)
				{
					return Result<void>::Failure(directory.GetError());
				}

				Array<wchar_t> command(memory);

				if (!command.Reserve(32'767))
				{
					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Terminal command allocation failed"));
				}

				auto appendArgument = [&](const Array<wchar_t>& argument) -> bool
					{
						const usize length = argument.Size() - 1;

						if (command.Size() + length * 2 + 4 > 32'766)
						{
							return false;
						}

						if (command.Size() != 0)
						{
							command.AddRequired(L' ');
						}

						command.AddRequired(L'"');
						usize slashes = 0;

						for (usize index = 0; index < length; ++index)
						{
							const wchar_t unit = argument[index];

							if (unit == L'\\')
							{
								++slashes;
								continue;
							}

							const usize count = unit == L'"' ? slashes * 2 + 1 : slashes;

							for (usize slash = 0; slash < count; ++slash)
							{
								command.AddRequired(L'\\');
							}

							slashes = 0;
							command.AddRequired(unit);
						}

						for (usize slash = 0; slash < slashes * 2; ++slash)
						{
							command.AddRequired(L'\\');
						}

						command.AddRequired(L'"');

						return true;
					};

				if (!appendArgument(executable.Value()))
				{
					return Result<void>::Failure(failure(EErrorCode::InvalidArgument, "Terminal command line exceeds Windows bound"));
				}

				for (const StringView argument : desc.Arguments)
				{
					Result<Array<wchar_t>> converted = wide(argument, memory);

					if (!converted)
					{
						return Result<void>::Failure(converted.GetError());
					}

					if (!appendArgument(converted.Value()))
					{
						return Result<void>::Failure(failure(EErrorCode::InvalidArgument, "Terminal command line exceeds Windows bound"));
					}
				}

				command.AddRequired(L'\0');

				if (!CreatePipe(&m_ConsoleInput, &m_InputWrite, nullptr, 0) || !CreatePipe(&m_OutputRead, &m_ConsoleOutput, nullptr, 0))
				{
					const DWORD error = GetLastError();
					closeHandle(m_ConsoleInput);
					closeHandle(m_ConsoleOutput);

					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Terminal CreatePipe failed", error));
				}

				const HRESULT created = CreatePseudoConsole({ static_cast<SHORT>(desc.Size.Columns), static_cast<SHORT>(desc.Size.Rows) }, m_ConsoleInput, m_ConsoleOutput, 0, &m_Console);

				if (FAILED(created))
				{
					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "CreatePseudoConsole failed", static_cast<DWORD>(created)));
				}

				SIZE_T attributeBytes = 0;
				InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
				Array<MaxAlign> attributes(memory);

				if (!attributes.Resize((attributeBytes + sizeof(MaxAlign) - 1) / sizeof(MaxAlign)))
				{
					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Terminal attribute allocation failed"));
				}

				_PROC_THREAD_ATTRIBUTE_LIST* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.Data());

				if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeBytes))
				{
					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Terminal attribute initialization failed", GetLastError()));
				}

				if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_Console, sizeof(m_Console), nullptr, nullptr))
				{
					const DWORD error = GetLastError();
					DeleteProcThreadAttributeList(list);

					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Terminal pseudoconsole binding failed", error));
				}

				m_Job = CreateJobObjectW(nullptr, nullptr);
				JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
				limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

				if (m_Job == nullptr || !SetInformationJobObject(m_Job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
				{
					const DWORD error = GetLastError();
					DeleteProcThreadAttributeList(list);

					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Terminal job creation failed", error));
				}

				STARTUPINFOEXW startup{};
				startup.StartupInfo.cb = sizeof(startup);
				startup.lpAttributeList = list;
				// Null explicit stdio prevents redirected host handles from being duplicated into the PTY child.
				// Console attachment replaces these null values with the new pseudoconsole handles.
				startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
				PROCESS_INFORMATION process{};
				const BOOL launched = CreateProcessW(
					executable.Value().Data(), command.Data(), nullptr, nullptr, FALSE,
					EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, nullptr,
					desc.WorkingDirectoryPath.IsEmpty() ? nullptr : directory.Value().Data(),
					&startup.StartupInfo, &process);
				const DWORD launchError = GetLastError();
				DeleteProcThreadAttributeList(list);
				closeHandle(m_ConsoleInput);
				closeHandle(m_ConsoleOutput);

				if (!launched)
				{
					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Terminal CreateProcess failed", launchError));
				}

				m_Process = process.hProcess;

				if (!AssignProcessToJobObject(m_Job, m_Process))
				{
					const DWORD error = GetLastError();
					TerminateProcess(m_Process, 1);
					closeHandle(process.hThread);

					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Terminal process job assignment failed", error));
				}

				m_ReadThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &readEntry, this, 0, nullptr));

				if (m_ReadThread != nullptr)
				{
					m_ControlThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &controlEntry, this, 0, nullptr));
				}

				if (m_ReadThread == nullptr || m_ControlThread == nullptr)
				{
					closeHandle(process.hThread);

					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Terminal worker creation failed"));
				}

				const DWORD resumed = ResumeThread(process.hThread);
				const DWORD resumeError = GetLastError();
				closeHandle(process.hThread);

				if (resumed == static_cast<DWORD>(-1))
				{
					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Terminal process resume failed", resumeError));
				}

				return Result<void>::Success();
			}

			Result<void> Write(Span<const Byte> bytes) override
			{
				LockGuard lock(m_Mutex);

				if (m_bClosing || m_bExited)
				{
					return Result<void>::Failure(failure(EErrorCode::Cancelled, "Terminal is closing or exited"));
				}

				if (bytes.Size() > m_Input.Size())
				{
					return Result<void>::Failure(failure(EErrorCode::InvalidArgument, "Terminal input exceeds whole-batch bound"));
				}

				if (bytes.IsEmpty())
				{
					return Result<void>::Success();
				}

				if (m_CommandCount == m_Commands.Size() || bytes.Size() > m_Input.Size() - m_InputCount)
				{
					return Result<void>::Failure(failure(EErrorCode::Conflict, "Terminal input queue is full"));
				}

				for (usize index = 0; index < bytes.Size(); ++index)
				{
					m_Input[(m_InputStart + m_InputCount + index) % m_Input.Size()] = bytes[index];
				}

				m_InputCount += bytes.Size();
				m_Commands[(m_CommandStart + m_CommandCount++) % m_Commands.Size()] = { .Bytes = static_cast<uint32>(bytes.Size()) };
				m_Changed.notify_all();

				return Result<void>::Success();
			}

			Result<void> Resize(TerminalSize size) override
			{
				if (!validSize(size))
				{
					return Result<void>::Failure(failure(EErrorCode::InvalidArgument, "Terminal size is outside its bound"));
				}

				LockGuard lock(m_Mutex);

				if (m_bClosing || m_bExited)
				{
					return Result<void>::Failure(failure(EErrorCode::Cancelled, "Terminal is closing or exited"));
				}

				if (m_CommandCount == m_Commands.Size())
				{
					return Result<void>::Failure(failure(EErrorCode::Conflict, "Terminal command queue is full"));
				}

				m_Commands[(m_CommandStart + m_CommandCount++) % m_Commands.Size()] = { .bResize = true, .Size = size };
				m_Changed.notify_all();

				return Result<void>::Success();
			}

			Result<TerminalOutput> Poll(MemoryResource& memory) override
			{
				TerminalOutput result(memory);
				LockGuard lock(m_Mutex);
				const usize count = Min<usize>(m_OutputCount, m_PollMaximum);

				if (!result.Bytes.Resize(count))
				{
					return Result<TerminalOutput>::Failure(failure(EErrorCode::ExternalFailure, "Terminal poll allocation failed"));
				}

				for (usize index = 0; index < count; ++index)
				{
					result.Bytes[index] = m_Output[(m_OutputStart + index) % m_Output.Size()];
				}

				m_OutputStart = (m_OutputStart + count) % m_Output.Size();
				m_OutputCount -= count;
				result.bExited = m_bExited;
				result.bClosing = m_bClosing;
				result.ExitCode = m_ExitCode;
				result.StreamFailure = m_Failure;
				result.bClosed = closedLocked() && m_OutputCount == 0;
				m_Changed.notify_all();

				return Result<TerminalOutput>::Success(Move(result));
			}

			void BeginClose() noexcept override
			{
				{
					LockGuard lock(m_Mutex);
					m_bClosing = true;
					m_Changed.notify_all();
				}

				if (m_Job != nullptr)
				{
					TerminateJobObject(m_Job, 1);
				}

				if (m_ControlThread != nullptr)
				{
					CancelSynchronousIo(m_ControlThread);
				}
			}

			bool IsClosed() const noexcept override
			{
				LockGuard lock(m_Mutex);

				return closedLocked();
			}

		private:
			bool closedLocked() const noexcept
			{
				return m_bControlDone &&
					   m_bReadDone &&
					   m_ControlThread != nullptr &&
					   m_ReadThread != nullptr &&
					   WaitForSingleObject(m_ControlThread, 0) == WAIT_OBJECT_0 &&
					   WaitForSingleObject(m_ReadThread, 0) == WAIT_OBJECT_0;
			}

			static unsigned __stdcall readEntry(void* value)
			{
				static_cast<Session*>(value)->readLoop();

				return 0;
			}

			static unsigned __stdcall controlEntry(void* value)
			{
				static_cast<Session*>(value)->controlLoop();

				return 0;
			}

			void recordFailure(const char* operation, DWORD native)
			{
				LockGuard lock(m_Mutex);

				if (m_Failure.Code == EErrorCode::None)
				{
					m_Failure = failure(EErrorCode::ExternalFailure, operation, native);
				}
			}

			void readLoop()
			{
				Byte bytes[4096];

				for (;;)
				{
					DWORD count = 0;

					if (!ReadFile(m_OutputRead, bytes, sizeof(bytes), &count, nullptr))
					{
						const DWORD error = GetLastError();

						if (error != ERROR_BROKEN_PIPE && error != ERROR_OPERATION_ABORTED)
						{
							recordFailure("Terminal output read failed", error);
							BeginClose();
						}

						break;
					}

					if (count == 0)
					{
						break;
					}

					usize offset = 0;
					UniqueLock lock(m_Mutex);

					while (offset < count)
					{
						m_Changed.wait(lock, [&] { return m_bClosing || m_OutputCount < m_Output.Size(); });

						if (m_bClosing)
						{
							break;
						}

						// Preserve unread bytes under backpressure unless closing explicitly permits discard.
						const usize copied = Min<usize>(count - offset, m_Output.Size() - m_OutputCount);

						for (usize index = 0; index < copied; ++index)
						{
							m_Output[(m_OutputStart + m_OutputCount + index) % m_Output.Size()] = bytes[offset + index];
						}

						m_OutputCount += copied;
						offset += copied;
					}
				}

				LockGuard lock(m_Mutex);
				m_bReadDone = true;
				m_Changed.notify_all();
			}

			void controlLoop()
			{
				for (;;)
				{
					UniqueLock lock(m_Mutex);

					if (WaitForSingleObject(m_Process, 0) == WAIT_OBJECT_0)
					{
						DWORD exit = 0;
						GetExitCodeProcess(m_Process, &exit);
						m_ExitCode = exit;
						m_bExited = true;
						break;
					}

					if (m_bClosing)
					{
						break;
					}

					if (m_CommandCount == 0)
					{
						m_Changed.wait_for(lock, Milliseconds(10));
						continue;
					}

					Command& command = m_Commands[m_CommandStart];

					if (command.bResize)
					{
						const TerminalSize size = command.Size;
						lock.unlock();
						const HRESULT resized = ResizePseudoConsole(m_Console, { static_cast<SHORT>(size.Columns), static_cast<SHORT>(size.Rows) });

						if (FAILED(resized))
						{
							recordFailure("ResizePseudoConsole failed", static_cast<DWORD>(resized));
							BeginClose();
						}

						lock.lock();
						command.Bytes = 0;
					}
					else
					{
						const usize count = Min<usize>(command.Bytes, m_Input.Size() - m_InputStart);
						const Byte* data = m_Input.Data() + m_InputStart;
						lock.unlock();
						DWORD written = 0;
						const BOOL success = WriteFile(m_InputWrite, data, static_cast<DWORD>(count), &written, nullptr);
						const DWORD error = success ? ERROR_SUCCESS : GetLastError();
						lock.lock();

						if (!success || written == 0)
						{
							const bool bClosing = m_bClosing;
							lock.unlock();

							if (!bClosing)
							{
								recordFailure("Terminal input write failed", error);
								BeginClose();
							}

							continue;
						}

						m_InputStart = (m_InputStart + written) % m_Input.Size();
						m_InputCount -= written;
						command.Bytes -= written;
					}

					if (command.Bytes == 0)
					{
						m_CommandStart = (m_CommandStart + 1) % m_Commands.Size();
						--m_CommandCount;
					}
				}
				// Reader remains alive to drain ConPTY while ClosePseudoConsole completes.
				closeHandle(m_InputWrite);
				ClosePseudoConsole(m_Console);
				m_Console = nullptr;
				WaitForSingleObject(m_Process, INFINITE);
				DWORD exit = 0;
				GetExitCodeProcess(m_Process, &exit);
				LockGuard lock(m_Mutex);
				m_bExited = true;
				m_ExitCode = exit;
				m_bControlDone = true;
				m_Changed.notify_all();
			}

		private:
			Array<Byte> m_Input;
			Array<Byte> m_Output;
			Array<Command> m_Commands;

			mutable Mutex m_Mutex;
			ConditionVariable m_Changed;

			usize m_InputStart = 0;
			usize m_InputCount = 0;
			usize m_OutputStart = 0;
			usize m_OutputCount = 0;
			usize m_CommandStart = 0;
			usize m_CommandCount = 0;
			uint32 m_PollMaximum = 0;

			bool m_bClosing = false;
			bool m_bExited = false;
			bool m_bReadDone = false;
			bool m_bControlDone = false;

			DWORD m_ExitCode = 0;
			Error m_Failure;

			HANDLE m_InputWrite = nullptr;
			HANDLE m_OutputRead = nullptr;
			HANDLE m_ConsoleInput = nullptr;
			HANDLE m_ConsoleOutput = nullptr;
			HANDLE m_Process = nullptr;
			HANDLE m_Job = nullptr;

			HANDLE m_ReadThread = nullptr;
			HANDLE m_ControlThread = nullptr;
			HPCON m_Console = nullptr;
		};
	}

	Result<InterfaceOwner<ITerminalSession>> CreateWindowsTerminalSession(const TerminalDesc& desc, MemoryResource& memory)
	{
		using Owner = InterfaceOwner<ITerminalSession>;

		if (desc.ExecutablePath.IsEmpty() ||
			!validSize(desc.Size) ||
			desc.Arguments.Size() > 256 ||
			desc.MaximumInputBytes == 0 ||
			desc.MaximumInputBytes > 1'048'576 ||
			desc.MaximumOutputBytes == 0 ||
			desc.MaximumOutputBytes > 16'777'216 ||
			desc.MaximumPollBytes == 0 ||
			desc.MaximumPollBytes > desc.MaximumOutputBytes ||
			desc.MaximumQueuedCommands == 0 ||
			desc.MaximumQueuedCommands > 1024)
		{
			return Result<Owner>::Failure(failure(EErrorCode::InvalidArgument, "Terminal description exceeds its bounds"));
		}

		InterfaceOwner<Session> owner = InterfaceOwner<Session>::Create(memory, memory);

		if (!owner)
		{
			return Result<Owner>::Failure(failure(EErrorCode::ExternalFailure, "Terminal owner allocation failed"));
		}

		Result<void> initialized = owner->Initialize(desc, memory);

		if (!initialized)
		{
			return Result<Owner>::Failure(initialized.GetError());
		}

		return Result<Owner>::Success(Owner(Move(owner)));
	}

	WindowsTerminalUnicodeVersion GetWindowsTerminalUnicodeVersion() noexcept
	{
		UVersionInfo version{};
		u_getUnicodeVersion(version);

		return { version[0], version[1], version[2], version[3] };
	}

	uint32 WindowsTerminalCellWidth(uint32 scalar) noexcept
	{
		if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
		{
			return 1;
		}

		const UChar32 codepoint = static_cast<UChar32>(scalar);
		const int32 category = u_getIntPropertyValue(codepoint, UCHAR_GENERAL_CATEGORY);

		if (category == U_NON_SPACING_MARK || category == U_ENCLOSING_MARK || u_hasBinaryProperty(codepoint, UCHAR_DEFAULT_IGNORABLE_CODE_POINT))
		{
			return 0;
		}

		const int32 width = u_getIntPropertyValue(codepoint, UCHAR_EAST_ASIAN_WIDTH);

		return width == U_EA_WIDE || width == U_EA_FULLWIDTH ? 2u : 1u;
	}
}
