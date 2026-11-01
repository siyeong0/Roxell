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
	namespace
	{
		core::Error failure(core::EErrorCode code, const char* operation, DWORD native = 0)
		{
			char message[192];
			rox::core::FormatText(message, sizeof(message), "%s (Windows error %lu).", operation, native);

			return core::MakeError(code, "ROX-PLATFORM-TERMINAL", core::StringView(message, rox::core::CStringLength(message)), "Close the session or retry after draining queued output.");
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

		core::Result<core::Array<wchar_t>> wide(core::StringView text, core::MemoryResource& memory)
		{
			if (text.Size() > 131'064 || !core::IsValidUtf8(text) || rox::core::Find(text.begin(), text.end(), '\0') != text.end())
			{
				return core::Result<core::Array<wchar_t>>::Failure(failure(core::EErrorCode::InvalidArgument, "Terminal path or argument is invalid"));
			}

			const int count = text.IsEmpty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Size()), nullptr, 0);
			core::Array<wchar_t> result(memory);

			if ((!text.IsEmpty() && count == 0) || !result.Resize(static_cast<rox::core::usize>(count) + 1))
			{
				return core::Result<core::Array<wchar_t>>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal UTF-16 conversion allocation failed", GetLastError()));
			}

			if (count != 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Size()), result.Data(), count) != count)
			{
				return core::Result<core::Array<wchar_t>>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal UTF-16 conversion failed", GetLastError()));
			}

			result[static_cast<rox::core::usize>(count)] = 0;

			return core::Result<core::Array<wchar_t>>::Success(rox::core::Move(result));
		}

		struct Command final
		{
		public:
			bool bResize = false;
			TerminalSize Size;
			core::uint32 Bytes = 0;
		};

		class Session final : public ITerminalSession
		{
		public:
			explicit Session(core::MemoryResource& memory) noexcept
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

			core::Result<void> Initialize(const TerminalDesc& desc, core::MemoryResource& memory)
			{
				m_PollMaximum = desc.MaximumPollBytes;

				if (!m_Input.Resize(desc.MaximumInputBytes) || !m_Output.Resize(desc.MaximumOutputBytes) || !m_Commands.Resize(desc.MaximumQueuedCommands))
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal queue allocation failed"));
				}

				core::Result<core::Array<wchar_t>> executable = wide(desc.ExecutablePath, memory);

				if (!executable)
				{
					return core::Result<void>::Failure(executable.GetError());
				}

				core::Result<core::Array<wchar_t>> directory = wide(desc.WorkingDirectoryPath, memory);

				if (!directory)
				{
					return core::Result<void>::Failure(directory.GetError());
				}

				core::Array<wchar_t> command(memory);

				if (!command.Reserve(32'767))
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal command allocation failed"));
				}

				auto appendArgument = [&](const core::Array<wchar_t>& argument) -> bool
					{
						const core::usize length = argument.Size() - 1;

						if (command.Size() + length * 2 + 4 > 32'766)
						{
							return false;
						}

						if (command.Size() != 0)
						{
							command.AddRequired(L' ');
						}

						command.AddRequired(L'"');
						rox::core::usize slashes = 0;

						for (rox::core::usize index = 0; index < length; ++index)
						{
							const wchar_t unit = argument[index];

							if (unit == L'\\')
							{
								++slashes;
								continue;
							}

							const core::usize count = unit == L'"' ? slashes * 2 + 1 : slashes;

							for (rox::core::usize slash = 0; slash < count; ++slash)
							{
								command.AddRequired(L'\\');
							}

							slashes = 0;
							command.AddRequired(unit);
						}

						for (rox::core::usize slash = 0; slash < slashes * 2; ++slash)
						{
							command.AddRequired(L'\\');
						}

						command.AddRequired(L'"');

						return true;
					};

				if (!appendArgument(executable.Value()))
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::InvalidArgument, "Terminal command line exceeds Windows bound"));
				}

				for (const core::StringView argument : desc.Arguments)
				{
					core::Result<core::Array<wchar_t>> converted = wide(argument, memory);

					if (!converted)
					{
						return core::Result<void>::Failure(converted.GetError());
					}

					if (!appendArgument(converted.Value()))
					{
						return core::Result<void>::Failure(failure(core::EErrorCode::InvalidArgument, "Terminal command line exceeds Windows bound"));
					}
				}

				command.AddRequired(L'\0');

				if (!CreatePipe(&m_ConsoleInput, &m_InputWrite, nullptr, 0) || !CreatePipe(&m_OutputRead, &m_ConsoleOutput, nullptr, 0))
				{
					const DWORD error = GetLastError();
					closeHandle(m_ConsoleInput);
					closeHandle(m_ConsoleOutput);

					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal CreatePipe failed", error));
				}

				const HRESULT created = CreatePseudoConsole({ static_cast<SHORT>(desc.Size.Columns), static_cast<SHORT>(desc.Size.Rows) }, m_ConsoleInput, m_ConsoleOutput, 0, &m_Console);

				if (FAILED(created))
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "CreatePseudoConsole failed", static_cast<DWORD>(created)));
				}

				SIZE_T attributeBytes = 0;
				InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
				core::Array<rox::core::MaxAlign> attributes(memory);

				if (!attributes.Resize((attributeBytes + sizeof(rox::core::MaxAlign) - 1) / sizeof(rox::core::MaxAlign)))
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal attribute allocation failed"));
				}

				_PROC_THREAD_ATTRIBUTE_LIST* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.Data());

				if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeBytes))
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal attribute initialization failed", GetLastError()));
				}

				if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_Console, sizeof(m_Console), nullptr, nullptr))
				{
					const DWORD error = GetLastError();
					DeleteProcThreadAttributeList(list);

					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal pseudoconsole binding failed", error));
				}

				m_Job = CreateJobObjectW(nullptr, nullptr);
				JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
				limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

				if (m_Job == nullptr || !SetInformationJobObject(m_Job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
				{
					const DWORD error = GetLastError();
					DeleteProcThreadAttributeList(list);

					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal job creation failed", error));
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
					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal CreateProcess failed", launchError));
				}

				m_Process = process.hProcess;

				if (!AssignProcessToJobObject(m_Job, m_Process))
				{
					const DWORD error = GetLastError();
					TerminateProcess(m_Process, 1);
					closeHandle(process.hThread);

					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal process job assignment failed", error));
				}

				m_ReadThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &readEntry, this, 0, nullptr));

				if (m_ReadThread != nullptr)
				{
					m_ControlThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &controlEntry, this, 0, nullptr));
				}

				if (m_ReadThread == nullptr || m_ControlThread == nullptr)
				{
					closeHandle(process.hThread);

					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal worker creation failed"));
				}

				const DWORD resumed = ResumeThread(process.hThread);
				const DWORD resumeError = GetLastError();
				closeHandle(process.hThread);

				if (resumed == static_cast<DWORD>(-1))
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal process resume failed", resumeError));
				}

				return core::Result<void>::Success();
			}

			core::Result<void> Write(core::Span<const core::Byte> bytes) override
			{
				rox::core::LockGuard lock(m_Mutex);

				if (m_bClosing || m_bExited)
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::Cancelled, "Terminal is closing or exited"));
				}

				if (bytes.Size() > m_Input.Size())
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::InvalidArgument, "Terminal input exceeds whole-batch bound"));
				}

				if (bytes.IsEmpty())
				{
					return core::Result<void>::Success();
				}

				if (m_CommandCount == m_Commands.Size() || bytes.Size() > m_Input.Size() - m_InputCount)
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::Conflict, "Terminal input queue is full"));
				}

				for (rox::core::usize index = 0; index < bytes.Size(); ++index)
				{
					m_Input[(m_InputStart + m_InputCount + index) % m_Input.Size()] = bytes[index];
				}

				m_InputCount += bytes.Size();
				m_Commands[(m_CommandStart + m_CommandCount++) % m_Commands.Size()] = { .Bytes = static_cast<core::uint32>(bytes.Size()) };
				m_Changed.notify_all();

				return core::Result<void>::Success();
			}

			core::Result<void> Resize(TerminalSize size) override
			{
				if (!validSize(size))
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::InvalidArgument, "Terminal size is outside its bound"));
				}

				rox::core::LockGuard lock(m_Mutex);

				if (m_bClosing || m_bExited)
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::Cancelled, "Terminal is closing or exited"));
				}

				if (m_CommandCount == m_Commands.Size())
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::Conflict, "Terminal command queue is full"));
				}

				m_Commands[(m_CommandStart + m_CommandCount++) % m_Commands.Size()] = { .bResize = true, .Size = size };
				m_Changed.notify_all();

				return core::Result<void>::Success();
			}

			core::Result<TerminalOutput> Poll(core::MemoryResource& memory) override
			{
				TerminalOutput result(memory);
				rox::core::LockGuard lock(m_Mutex);
				const core::usize count = rox::core::Min<rox::core::usize>(m_OutputCount, m_PollMaximum);

				if (!result.Bytes.Resize(count))
				{
					return core::Result<TerminalOutput>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal poll allocation failed"));
				}

				for (rox::core::usize index = 0; index < count; ++index)
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

				return core::Result<TerminalOutput>::Success(rox::core::Move(result));
			}

			void BeginClose() noexcept override
			{
				{
					rox::core::LockGuard lock(m_Mutex);
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
				rox::core::LockGuard lock(m_Mutex);

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
				rox::core::LockGuard lock(m_Mutex);

				if (m_Failure.Code == core::EErrorCode::None)
				{
					m_Failure = failure(core::EErrorCode::ExternalFailure, operation, native);
				}
			}

			void readLoop()
			{
				core::Byte bytes[4096];

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

					rox::core::usize offset = 0;
					rox::core::UniqueLock lock(m_Mutex);

					while (offset < count)
					{
						m_Changed.wait(lock, [&] { return m_bClosing || m_OutputCount < m_Output.Size(); });

						if (m_bClosing)
						{
							break;
						}

						// Preserve unread bytes under backpressure unless closing explicitly permits discard.
						const core::usize copied = rox::core::Min<rox::core::usize>(count - offset, m_Output.Size() - m_OutputCount);

						for (rox::core::usize index = 0; index < copied; ++index)
						{
							m_Output[(m_OutputStart + m_OutputCount + index) % m_Output.Size()] = bytes[offset + index];
						}

						m_OutputCount += copied;
						offset += copied;
					}
				}

				rox::core::LockGuard lock(m_Mutex);
				m_bReadDone = true;
				m_Changed.notify_all();
			}

			void controlLoop()
			{
				for (;;)
				{
					rox::core::UniqueLock lock(m_Mutex);

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
						m_Changed.wait_for(lock, rox::core::Milliseconds(10));
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
						const core::usize count = rox::core::Min<rox::core::usize>(command.Bytes, m_Input.Size() - m_InputStart);
						const core::Byte* data = m_Input.Data() + m_InputStart;
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
				rox::core::LockGuard lock(m_Mutex);
				m_bExited = true;
				m_ExitCode = exit;
				m_bControlDone = true;
				m_Changed.notify_all();
			}

		private:
			core::Array<core::Byte> m_Input;
			core::Array<core::Byte> m_Output;
			core::Array<Command> m_Commands;

			mutable rox::core::Mutex m_Mutex;
			rox::core::ConditionVariable m_Changed;

			rox::core::usize m_InputStart = 0;
			rox::core::usize m_InputCount = 0;
			rox::core::usize m_OutputStart = 0;
			rox::core::usize m_OutputCount = 0;
			rox::core::usize m_CommandStart = 0;
			rox::core::usize m_CommandCount = 0;
			core::uint32 m_PollMaximum = 0;

			bool m_bClosing = false;
			bool m_bExited = false;
			bool m_bReadDone = false;
			bool m_bControlDone = false;

			DWORD m_ExitCode = 0;
			core::Error m_Failure;

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

	core::Result<core::InterfaceOwner<ITerminalSession>> CreateWindowsTerminalSession(const TerminalDesc& desc, core::MemoryResource& memory)
	{
		using Owner = core::InterfaceOwner<ITerminalSession>;

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
			return core::Result<Owner>::Failure(failure(core::EErrorCode::InvalidArgument, "Terminal description exceeds its bounds"));
		}

		core::InterfaceOwner<Session> owner = core::InterfaceOwner<Session>::Create(memory, memory);

		if (!owner)
		{
			return core::Result<Owner>::Failure(failure(core::EErrorCode::ExternalFailure, "Terminal owner allocation failed"));
		}

		core::Result<void> initialized = owner->Initialize(desc, memory);

		if (!initialized)
		{
			return core::Result<Owner>::Failure(initialized.GetError());
		}

		return core::Result<Owner>::Success(Owner(rox::core::Move(owner)));
	}

	WindowsTerminalUnicodeVersion GetWindowsTerminalUnicodeVersion() noexcept
	{
		UVersionInfo version{};
		u_getUnicodeVersion(version);

		return { version[0], version[1], version[2], version[3] };
	}

	core::uint32 WindowsTerminalCellWidth(core::uint32 scalar) noexcept
	{
		if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
		{
			return 1;
		}

		const UChar32 codepoint = static_cast<UChar32>(scalar);
		const core::int32 category = u_getIntPropertyValue(codepoint, UCHAR_GENERAL_CATEGORY);

		if (category == U_NON_SPACING_MARK || category == U_ENCLOSING_MARK || u_hasBinaryProperty(codepoint, UCHAR_DEFAULT_IGNORABLE_CODE_POINT))
		{
			return 0;
		}

		const core::int32 width = u_getIntPropertyValue(codepoint, UCHAR_EAST_ASIAN_WIDTH);

		return width == U_EA_WIDE || width == U_EA_FULLWIDTH ? 2u : 1u;
	}
}
