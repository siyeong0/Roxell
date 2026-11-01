#include <WindowsProcessService.h>
#include <Span.hpp>
#include <TaskSource.hpp>
#include <ByteOperations.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Synchronization.hpp>
#include <Utility.hpp>
#include <Types.h>
#include <Cancellation.h>

#include <Assertion.h>
#include <Log.h>
#include <MemoryBlock.h>
#include <SharedPtr.hpp>
#include <UniquePtr.hpp>
#include <Utf8.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <Algorithms.hpp>
#include <TextPrimitives.hpp>
#include <process.h>
#include <Vocabulary.hpp>

namespace rox::os
{
	using namespace core;

	namespace
	{
		class ImageHandle final
		{
		public:
			explicit ImageHandle(HANDLE value) noexcept
				: m_Value(value)
			{
			}

			~ImageHandle()
			{
				if (m_Value != nullptr && m_Value != INVALID_HANDLE_VALUE)
				{
					const BOOL bClosed = CloseHandle(m_Value);
					ASSERT(bClosed != FALSE, "A process image handle must close successfully.");
				}
			}

			ImageHandle(const ImageHandle&) = delete;
			ImageHandle& operator=(const ImageHandle&) = delete;

			[[nodiscard]] HANDLE GetHandle() const noexcept
			{
				return m_Value;
			}

		private:
			HANDLE m_Value;
		};

		Result<Hash256> imageFailure(StringView message)
		{
			return Result<Hash256>::Failure(EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-IMAGE-READ", message,
				"Restore read access to the running executable and retry.");
		}

		constexpr uint32 PROCESS_READER_BUFFER_BYTE_COUNT = 4'096;
		constexpr uint32 PROCESS_WORKER_JOIN_TIMEOUT_MILLISECONDS = 5'000;
		constexpr usize MAXIMUM_WINDOWS_COMMAND_LINE_CODE_UNITS = 32'767;
		constexpr usize MAXIMUM_WINDOWS_PROCESS_TEXT_UTF8_BYTES = (MAXIMUM_WINDOWS_COMMAND_LINE_CODE_UNITS - 1) * 4;

		Atomic<uint64> s_ProcessServiceOwner = 1;

		[[nodiscard]] Error makeProcessError(const EErrorCode code, const StringView diagnosticID,
			const StringView message, const StringView recoveryHint)
		{
			ASSERT(code != EErrorCode::None, "A process failure requires a non-success error code.");
			ASSERT(!diagnosticID.IsEmpty(), "A process failure requires a diagnostic ID.");
			ASSERT(!message.IsEmpty(), "A process failure requires a message.");

			return MakeError(code, diagnosticID, message, recoveryHint);
		}

		[[nodiscard]] Result<ProcessHandle> makeLaunchCancelledResult()
		{
			return Result<ProcessHandle>::Failure(makeProcessError(EErrorCode::Cancelled, "ROX-PLATFORM-PROCESS-CANCELLED",
				"The process launch was cancelled before publication.",
				"Retry the launch with a live cancellation token."));
		}

		[[nodiscard]] Result<ProcessExit> makeWaitCancelledResult()
		{
			return Result<ProcessExit>::Failure(makeProcessError(EErrorCode::Cancelled, "ROX-PLATFORM-PROCESS-WAIT-CANCELLED",
				"The process Wait observation was cancelled.",
				"Wait again or terminate the process explicitly."));
		}

		[[nodiscard]] bool isNativeHandleValid(const HANDLE handle) noexcept
		{
			return handle != nullptr && handle != INVALID_HANDLE_VALUE;
		}

		void closeNativeHandle(HANDLE* const handle) noexcept
		{
			ASSERT(handle != nullptr, "Closing a native process handle requires its storage.");

			if (isNativeHandleValid(*handle))
			{
				const BOOL bClosed = CloseHandle(*handle);
				ASSERT(bClosed != FALSE, "A process-owned Windows handle must close successfully.");
			}

			*handle = nullptr;
		}

		class ScopedNativeHandle final
		{
		public:
			ScopedNativeHandle() noexcept = default;

			explicit ScopedNativeHandle(const HANDLE handle) noexcept
				: m_Handle(handle)
			{
			}

			~ScopedNativeHandle() noexcept
			{
				Reset();
			}

			ScopedNativeHandle(const ScopedNativeHandle&) = delete;
			ScopedNativeHandle& operator=(const ScopedNativeHandle&) = delete;

			ScopedNativeHandle(ScopedNativeHandle&& other) noexcept
				: m_Handle(other.Release())
			{
			}

			ScopedNativeHandle& operator=(ScopedNativeHandle&& other) noexcept
			{
				if (this != &other)
				{
					Reset(other.Release());
				}

				return *this;
			}

			[[nodiscard]] HANDLE Get() const noexcept
			{
				return m_Handle;
			}

			[[nodiscard]] bool IsValid() const noexcept
			{
				return isNativeHandleValid(m_Handle);
			}

			[[nodiscard]] HANDLE Release() noexcept
			{
				const HANDLE handle = m_Handle;
				m_Handle = nullptr;

				return handle;
			}

			void Reset(const HANDLE replacement = nullptr) noexcept
			{
				if (IsValid())
				{
					const BOOL bClosed = CloseHandle(m_Handle);
					ASSERT(bClosed != FALSE, "A scoped Windows handle must close successfully.");
				}

				m_Handle = replacement;
			}

		private:
			HANDLE m_Handle = nullptr;
		};

		enum class ETextBuildResult
		{
			Success,
			Invalid,
			AllocationFailed
		};

		[[nodiscard]] bool containsNull(const StringView text) noexcept
		{
			for (const char value : text)
			{
				if (value == '\0')
				{
					return true;
				}
			}

			return false;
		}

		[[nodiscard]] ETextBuildResult convertUtf8ToWide(MemoryResource& resource, const StringView text,
			const bool bAllowEmpty, Array<wchar_t>* const outText) noexcept
		{
			ASSERT(outText != nullptr, "UTF-8 process text conversion requires an output array.");
			ASSERT(&outText->GetResource() == &resource, "UTF-8 process text conversion must use the requested memory resource.");

			if ((!bAllowEmpty && text.IsEmpty()) || containsNull(text) || !IsValidUtf8(text) ||
				text.Size() > static_cast<usize>(NumericLimits<int>::max()))
			{
				return ETextBuildResult::Invalid;
			}

			if (text.IsEmpty())
			{
				Array<wchar_t> converted(resource);

				if (!converted.Add(L'\0'))
				{
					return ETextBuildResult::AllocationFailed;
				}

				*outText = Move(converted);

				return ETextBuildResult::Success;
			}

			const int sourceSize = static_cast<int>(text.Size());
			const int requiredSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), sourceSize, nullptr, 0);

			if (requiredSize <= 0)
			{
				return ETextBuildResult::Invalid;
			}

			Array<wchar_t> converted(resource);

			if (!converted.Resize(static_cast<usize>(requiredSize) + 1))
			{
				return ETextBuildResult::AllocationFailed;
			}

			const int writtenSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), sourceSize, converted.Data(), requiredSize);

			if (writtenSize != requiredSize)
			{
				return ETextBuildResult::Invalid;
			}

			converted[static_cast<usize>(requiredSize)] = L'\0';
			*outText = Move(converted);

			return ETextBuildResult::Success;
		}

		[[nodiscard]] bool isPathSeparator(const wchar_t value) noexcept
		{
			return value == L'\\' || value == L'/';
		}

		[[nodiscard]] bool isAbsoluteWindowsPath(const Array<wchar_t>& path) noexcept
		{
			if (path.Size() < 4 || path.Back() != L'\0')
			{
				return false;
			}

			const bool bDriveRooted = ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && isPathSeparator(path[2]);
			const bool bNetworkRooted = isPathSeparator(path[0]) && isPathSeparator(path[1]) && path[2] != L'\0' && !isPathSeparator(path[2]);
			return bDriveRooted || bNetworkRooted;
		}

		[[nodiscard]] bool tryAddCommandCodeUnit(Array<wchar_t>& commandLine, const wchar_t value) noexcept
		{
			if (commandLine.Size() >= MAXIMUM_WINDOWS_COMMAND_LINE_CODE_UNITS - 1)
			{
				return false;
			}

			return commandLine.Add(value);
		}

		[[nodiscard]] bool tryAddRepeatedCommandCodeUnit(Array<wchar_t>& commandLine, const wchar_t value,
			const usize count) noexcept
		{
			for (usize index = 0; index < count; ++index)
			{
				if (!tryAddCommandCodeUnit(commandLine, value))
				{
					return false;
				}
			}

			return true;
		}

		[[nodiscard]] bool tryAddQuotedArgument(Array<wchar_t>& commandLine, const wchar_t* const text, const usize textSize) noexcept
		{
			ASSERT(text != nullptr || textSize == 0, "A non-empty process argument requires UTF-16 text.");

			if (!tryAddCommandCodeUnit(commandLine, L'\"'))
			{
				return false;
			}

			usize index = 0;

			while (index < textSize)
			{
				usize backslashCount = 0;

				while (index < textSize && text[index] == L'\\')
				{
					++backslashCount;
					++index;
				}

				if (index == textSize)
				{
					if (!tryAddRepeatedCommandCodeUnit(commandLine, L'\\', backslashCount * 2))
					{
						return false;
					}

					break;
				}

				if (text[index] == L'\"')
				{
					if (!tryAddRepeatedCommandCodeUnit(commandLine, L'\\', backslashCount * 2 + 1) || !tryAddCommandCodeUnit(commandLine, L'\"'))
					{
						return false;
					}
				}
				else
				{
					if (!tryAddRepeatedCommandCodeUnit(commandLine, L'\\', backslashCount) || !tryAddCommandCodeUnit(commandLine, text[index]))
					{
						return false;
					}
				}

				++index;
			}

			return tryAddCommandCodeUnit(commandLine, L'\"');
		}

		[[nodiscard]] ETextBuildResult buildCommandLine(MemoryResource& resource, const Array<wchar_t>& executable, const Span<const StringView> arguments, Array<wchar_t>* const outCommandLine) noexcept
		{
			ASSERT(outCommandLine != nullptr, "Process command-line construction requires an output array.");
			ASSERT(executable.Size() >= 2 && executable.Back() == L'\0', "Process command-line construction requires a terminated executable path.");

			if (arguments.Size() > MAXIMUM_PROCESS_ARGUMENT_COUNT)
			{
				return ETextBuildResult::Invalid;
			}

			Array<wchar_t> commandLine(resource);

			if (!tryAddQuotedArgument(commandLine, executable.Data(), executable.Size() - 1))
			{
				return commandLine.Size() >= MAXIMUM_WINDOWS_COMMAND_LINE_CODE_UNITS - 1 ? ETextBuildResult::Invalid : ETextBuildResult::AllocationFailed;
			}

			for (const StringView argument : arguments)
			{
				Array<wchar_t> converted(resource);
				const ETextBuildResult conversion = convertUtf8ToWide(resource, argument, true, &converted);

				if (conversion != ETextBuildResult::Success)
				{
					return conversion;
				}

				if (!tryAddCommandCodeUnit(commandLine, L' ') || !tryAddQuotedArgument(commandLine, converted.Data(), converted.Size() - 1))
				{
					return commandLine.Size() >= MAXIMUM_WINDOWS_COMMAND_LINE_CODE_UNITS - 1 ? ETextBuildResult::Invalid : ETextBuildResult::AllocationFailed;
				}
			}

			if (!commandLine.Add(L'\0'))
			{
				return ETextBuildResult::AllocationFailed;
			}

			ASSERT(commandLine.Size() <= MAXIMUM_WINDOWS_COMMAND_LINE_CODE_UNITS, "A published Windows command line must remain within the native limit.");
			*outCommandLine = Move(commandLine);

			return ETextBuildResult::Success;
		}

		[[nodiscard]] bool createOutputPipe(ScopedNativeHandle* const outParentRead, ScopedNativeHandle* const outChildWrite) noexcept
		{
			ASSERT(outParentRead != nullptr && outChildWrite != nullptr, "Process pipe creation requires both output handles.");
			SECURITY_ATTRIBUTES security{};
			security.nLength = sizeof(security);
			security.bInheritHandle = TRUE;

			HANDLE parentRead = nullptr;
			HANDLE childWrite = nullptr;

			if (CreatePipe(&parentRead, &childWrite, &security, 0) == FALSE)
			{
				return false;
			}

			ScopedNativeHandle parent(parentRead);
			ScopedNativeHandle child(childWrite);

			if (SetHandleInformation(parent.Get(), HANDLE_FLAG_INHERIT, 0) == FALSE)
			{
				return false;
			}

			*outParentRead = Move(parent);
			*outChildWrite = Move(child);

			return true;
		}

		class ProcessAttributeList final
		{
		public:
			~ProcessAttributeList() noexcept
			{
				if (m_List != nullptr)
				{
					DeleteProcThreadAttributeList(m_List);
					m_List = nullptr;
				}
			}

			[[nodiscard]] ETextBuildResult TryInitialize(MemoryResource& resource, HANDLE* const handles,
				const usize handleCount) noexcept
			{
				ASSERT(handles != nullptr && handleCount != 0, "A process attribute list requires inherited handles.");
				SIZE_T byteCount = 0;
				(void)InitializeProcThreadAttributeList(nullptr, 1, 0, &byteCount);

				if (byteCount == 0)
				{
					return ETextBuildResult::Invalid;
				}

				if (!MemoryBlock::CreateInto(resource, &m_Storage, { byteCount, DEFAULT_MEMORY_ALIGNMENT }))
				{
					return ETextBuildResult::AllocationFailed;
				}

				m_List = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_Storage.GetData());

				if (InitializeProcThreadAttributeList(m_List, 1, 0, &byteCount) == FALSE)
				{
					m_List = nullptr;

					return ETextBuildResult::Invalid;
				}

				if (UpdateProcThreadAttribute(m_List, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, handleCount * sizeof(HANDLE), nullptr,
					nullptr) == FALSE)
				{
					return ETextBuildResult::Invalid;
				}

				return ETextBuildResult::Success;
			}

			[[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST Get() const noexcept
			{
				return m_List;
			}

		private:
			MemoryBlock m_Storage;
			LPPROC_THREAD_ATTRIBUTE_LIST m_List = nullptr;
		};

		class PendingProcessWait final
		{
		public:
			PendingProcessWait(TaskSource<ProcessExit> source, MemoryResource& resultResource,
				Result<ProcessExit> cancellationResult) noexcept
				: m_Source(Move(source))
				, m_ResultResource(resultResource)
				, m_CancellationResult(Move(cancellationResult))
			{
			}

			[[nodiscard]] bool TryClaimCompletion() noexcept
			{
				bool bExpected = false;

				return m_bCompletionClaimed.compare_exchange_strong(bExpected, true, MemoryOrderAcqRel,
					MemoryOrderAcquire);
			}

			[[nodiscard]] bool IsCompletionClaimed() const noexcept
			{
				return m_bCompletionClaimed.load(MemoryOrderAcquire);
			}

			[[nodiscard]] bool IsCancellationClaimed() const noexcept
			{
				return m_bCancellationClaimed.load(MemoryOrderAcquire);
			}

			[[nodiscard]] MemoryResource& ResultResource() const noexcept
			{
				return m_ResultResource;
			}

			[[nodiscard]] CancellationRegistration& Registration() noexcept
			{
				return m_Registration;
			}

			void CompleteClaimed(Result<ProcessExit> result) noexcept
			{
				ASSERT(IsCompletionClaimed(), "A process Wait result requires the completion claim.");
				m_Source.SetResult(Move(result));
			}

			static void OnCancellation(void* const context) noexcept
			{
				ASSERT(context != nullptr, "Process Wait cancellation requires its pending operation.");
				PendingProcessWait& pending = *static_cast<PendingProcessWait*>(context);

				if (pending.TryClaimCompletion())
				{
					pending.m_bCancellationClaimed.store(true, MemoryOrderRelease);
					pending.CompleteClaimed(Move(pending.m_CancellationResult));
				}
			}

		private:
			TaskSource<ProcessExit> m_Source;
			MemoryResource& m_ResultResource;
			Result<ProcessExit> m_CancellationResult;
			CancellationRegistration m_Registration;
			Atomic<bool> m_bCompletionClaimed = false;
			Atomic<bool> m_bCancellationClaimed = false;
		};

		enum class ENativeProcessPhase
		{
			Running,
			Exited
		};

		class NativeProcess final
		{
		public:
			NativeProcess(MemoryResource& resource, const usize maximumCapturedOutputBytes) noexcept
				: m_Resource(resource)
				, m_MaximumCapturedOutputBytes(maximumCapturedOutputBytes)
				, m_StandardOutput(resource)
				, m_StandardError(resource)
			{
			}

			~NativeProcess() noexcept
			{
				ASSERT(!isNativeHandleValid(m_MonitorThread), "A native process must join its monitor before destruction.");
				ASSERT(!isNativeHandleValid(m_StandardOutputThread) && !isNativeHandleValid(m_StandardErrorThread),
					"A native process must join its pipe readers before destruction.");
				closeNativeHandle(&m_Process);
				closeNativeHandle(&m_Job);
				closeNativeHandle(&m_StandardOutputRead);
				closeNativeHandle(&m_StandardErrorRead);
			}

			void Adopt(const HANDLE process, const HANDLE job, const HANDLE standardOutputRead, const HANDLE standardErrorRead) noexcept
			{
				ASSERT(isNativeHandleValid(process) && isNativeHandleValid(job), "A native process requires its process and job handles.");
				ASSERT(isNativeHandleValid(standardOutputRead) && isNativeHandleValid(standardErrorRead), "A native process requires both parent pipe handles.");
				ASSERT(!isNativeHandleValid(m_Process) && !isNativeHandleValid(m_Job), "A native process may adopt handles only once.");
				m_Process = process;
				m_Job = job;
				m_StandardOutputRead = standardOutputRead;
				m_StandardErrorRead = standardErrorRead;
			}

			[[nodiscard]] bool StartWorkers() noexcept
			{
				ASSERT(isNativeHandleValid(m_Process) && isNativeHandleValid(m_Job), "Process workers require adopted native ownership.");
				const uintptr_t standardOutputThread = _beginthreadex(nullptr, 0, &standardOutputThreadEntry, this, 0, nullptr);

				if (standardOutputThread == 0)
				{
					return false;
				}

				m_StandardOutputThread = reinterpret_cast<HANDLE>(standardOutputThread);

				const uintptr_t standardErrorThread = _beginthreadex(nullptr, 0, &standardErrorThreadEntry, this, 0, nullptr);

				if (standardErrorThread == 0)
				{
					return false;
				}

				m_StandardErrorThread = reinterpret_cast<HANDLE>(standardErrorThread);

				const uintptr_t monitorThread = _beginthreadex(nullptr, 0, &monitorThreadEntry, this, 0, nullptr);

				if (monitorThread == 0)
				{
					return false;
				}

				m_MonitorThread = reinterpret_cast<HANDLE>(monitorThread);

				return true;
			}

			void AbortUnpublished() noexcept
			{
				if (isNativeHandleValid(m_Job))
				{
					(void)TerminateJobObject(m_Job, ERROR_PROCESS_ABORTED);
				}

				if (isNativeHandleValid(m_MonitorThread))
				{
					joinMonitor();

					return;
				}

				if (isNativeHandleValid(m_Process))
				{
					(void)WaitForSingleObject(m_Process, PROCESS_WORKER_JOIN_TIMEOUT_MILLISECONDS);
				}

				joinReader(&m_StandardOutputThread, &m_StandardOutputRead);
				joinReader(&m_StandardErrorThread, &m_StandardErrorRead);
			}

			[[nodiscard]] Task<ProcessExit> Wait(MemoryResource& resultResource, const CancellationToken cancellation)
			{
				if (cancellation.IsCancellationRequested())
				{
					return MakeReadyTask(makeWaitCancelledResult());
				}

				SharedPtr<PendingProcessWait> detachedPending;
				bool bExited = false;
				{
					LockGuard lock(m_Mutex);

					if (m_PendingWait)
					{
						ASSERT(m_PendingWait->IsCompletionClaimed(), "Only one process Wait operation may be pending.");
						detachedPending = m_PendingWait;
						m_PendingWait.Reset();
					}

					bExited = m_Phase == ENativeProcessPhase::Exited;
				}

				if (detachedPending)
				{
					detachedPending->Registration().Reset();
				}

				if (bExited)
				{
					return MakeReadyTask(buildExitResult(resultResource));
				}

				TaskSource<ProcessExit> completion(m_Resource);
				Task<ProcessExit> task = completion.GetTask();
				SharedPtr<PendingProcessWait> pending;

				if (!MakeSharedInto<PendingProcessWait>(m_Resource, &pending, Move(completion), resultResource,
					makeWaitCancelledResult()))
				{
					completion.SetResult(Result<ProcessExit>::Failure(
						makeProcessError(EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-MEMORY",
							"Memory allocation for the pending process Wait failed.",
							"Release bounded process-service memory and retry Wait.")));
					return task;
				}

				(void)pending->Registration().Register(cancellation, &PendingProcessWait::OnCancellation, pending.Get());
				bool bCompleteExited = false;
				{
					LockGuard lock(m_Mutex);

					if (m_PendingWait)
					{
						ASSERT(m_PendingWait->IsCompletionClaimed(), "Only one process Wait operation may be pending.");
					}

					if (!pending->IsCompletionClaimed() && m_Phase == ENativeProcessPhase::Running)
					{
						m_PendingWait = pending;
					}
					else if (!pending->IsCompletionClaimed())
					{
						// Cancellation can win after the preceding load. Only the CAS winner completes.
						bCompleteExited = pending->TryClaimCompletion();
					}
				}

				if (pending->IsCancellationClaimed())
				{
					return task;
				}

				if (bCompleteExited)
				{
					pending->Registration().Reset();
					pending->CompleteClaimed(buildExitResult(resultResource));
				}

				return task;
			}

			[[nodiscard]] Result<void> RequestTermination() noexcept
			{
				{
					LockGuard lock(m_Mutex);

					if (m_Phase == ENativeProcessPhase::Exited || m_bTerminationIssued)
					{
						return Result<void>::Success();
					}

					m_bTerminationRequested = true;
				}

				if (TerminateJobObject(m_Job, ERROR_PROCESS_ABORTED) != FALSE)
				{
					LockGuard lock(m_Mutex);
					m_bTerminationIssued = true;
					LOG_INFO("Platform.Process", "A Windows process tree termination was requested.");

					return Result<void>::Success();
				}

				{
					LockGuard lock(m_Mutex);

					if (m_Phase == ENativeProcessPhase::Exited || WaitForSingleObject(m_Process, 0) == WAIT_OBJECT_0)
					{
						return Result<void>::Success();
					}
				}

				LOG_ERROR("Platform.Process", "Windows could not terminate an owned process tree.");

				return Result<void>::Failure(makeProcessError(EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-TERMINATE",
					"Windows could not terminate the owned process tree.",
					"Inspect process security and Job Object state before retrying."));
			}

			[[nodiscard]] bool CanRelease(SharedPtr<PendingProcessWait>* const outDetached) noexcept
			{
				ASSERT(outDetached != nullptr, "Process release requires detached Wait storage.");
				LockGuard lock(m_Mutex);

				if (m_Phase != ENativeProcessPhase::Exited)
				{
					return false;
				}

				if (m_PendingWait && !m_PendingWait->IsCompletionClaimed())
				{
					return false;
				}

				*outDetached = m_PendingWait;
				m_PendingWait.Reset();

				return true;
			}

			void JoinForRelease() noexcept
			{
				joinMonitor();
			}

			void CancelPendingForShutdown() noexcept
			{
				SharedPtr<PendingProcessWait> pending;
				bool bClaimed = false;
				{
					LockGuard lock(m_Mutex);
					pending = m_PendingWait;
					m_PendingWait.Reset();

					if (pending && !pending->IsCompletionClaimed())
					{
						bClaimed = pending->TryClaimCompletion();
					}
				}

				if (pending)
				{
					pending->Registration().Reset();

					if (bClaimed)
					{
						pending->CompleteClaimed(makeWaitCancelledResult());
					}
				}
			}

		private:
			void appendCaptured(Array<Byte>& output, bool* const outTruncated, bool* const outAllocationFailed, const Byte* const data, const usize byteCount) noexcept
			{
				ASSERT(outTruncated != nullptr && outAllocationFailed != nullptr, "Process output capture requires status storage.");
				ASSERT(data != nullptr || byteCount == 0, "Non-empty process output capture requires bytes.");

				if (byteCount == 0)
				{
					return;
				}

				if (*outAllocationFailed || output.Size() >= m_MaximumCapturedOutputBytes)
				{
					*outTruncated = true;

					return;
				}

				const usize remaining = m_MaximumCapturedOutputBytes - output.Size();
				const usize copySize = (Min)(remaining, byteCount);
				const usize originalSize = output.Size();

				if (!output.Resize(originalSize + copySize))
				{
					*outAllocationFailed = true;
					*outTruncated = true;

					return;
				}

				CopyBytes(output.Data() + originalSize, data, copySize);

				if (copySize != byteCount)
				{
					*outTruncated = true;
				}
			}

			void runReader(const HANDLE pipe, Array<Byte>& output, bool* const outTruncated, bool* const outReadFailed, bool* const outAllocationFailed) noexcept
			{
				ASSERT(isNativeHandleValid(pipe), "A process output reader requires a pipe handle.");
				Byte buffer[PROCESS_READER_BUFFER_BYTE_COUNT]{};

				for (;;)
				{
					DWORD readSize = 0;
					const BOOL bRead = ReadFile(pipe, buffer, PROCESS_READER_BUFFER_BYTE_COUNT, &readSize, nullptr);

					if (bRead == FALSE)
					{
						const DWORD error = GetLastError();

						if (error != ERROR_BROKEN_PIPE && error != ERROR_OPERATION_ABORTED)
						{
							*outReadFailed = true;
						}

						break;
					}

					if (readSize == 0)
					{
						break;
					}

					appendCaptured(output, outTruncated, outAllocationFailed, buffer, readSize);
				}
			}

			[[nodiscard]] bool tryCloneOutput(const Array<Byte>& source, const bool bTruncated, const bool bReadFailed, const bool bAllocationFailed, ProcessOutput* const outOutput) const noexcept
			{
				ASSERT(outOutput != nullptr, "Process exit publication requires output storage.");

				if (!outOutput->Bytes.Resize(source.Size()))
				{
					return false;
				}

				if (!source.IsEmpty())
				{
					CopyBytes(outOutput->Bytes.Data(), source.Data(), source.Size());
				}

				outOutput->bTruncated = bTruncated;
				outOutput->bReadFailed = bReadFailed;
				outOutput->bAllocationFailed = bAllocationFailed;

				return true;
			}

			[[nodiscard]] Result<ProcessExit> buildExitResult(MemoryResource& resultResource) const
			{
				if (m_bWaitFailed)
				{
					return Result<ProcessExit>::Failure(makeProcessError(
						EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-WAIT", "Windows could not observe the owned process exit.",
						"Inspect native handle lifetime and retry the process operation."));
				}

				ProcessExit result(resultResource);
				result.ExitCode = m_ExitCode;
				result.bTerminationRequested = m_bTerminationRequested;

				if (!tryCloneOutput(m_StandardOutput, m_bStandardOutputTruncated, m_bStandardOutputReadFailed,
					m_bStandardOutputAllocationFailed, &result.StandardOutput) ||
					!tryCloneOutput(m_StandardError, m_bStandardErrorTruncated, m_bStandardErrorReadFailed, m_bStandardErrorAllocationFailed, &result.StandardError))
				{
					return Result<ProcessExit>::Failure(
						makeProcessError(EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-RESULT-MEMORY",
							"Process output could not be cloned into the result memory resource.",
							"Release result memory or retry Wait with a larger memory budget."));
				}

				return Result<ProcessExit>::Success(Move(result));
			}

			void joinReader(HANDLE* const thread, HANDLE* const pipe) noexcept
			{
				ASSERT(thread != nullptr && pipe != nullptr, "Process reader shutdown requires thread and pipe storage.");

				if (isNativeHandleValid(*thread))
				{
					DWORD waitResult = WaitForSingleObject(*thread, PROCESS_WORKER_JOIN_TIMEOUT_MILLISECONDS);

					if (waitResult == WAIT_TIMEOUT)
					{
						(void)CancelSynchronousIo(*thread);
						waitResult = WaitForSingleObject(*thread, INFINITE);
					}

					ASSERT(waitResult == WAIT_OBJECT_0, "A process output reader must terminate after cancellation.");
					closeNativeHandle(thread);
				}

				closeNativeHandle(pipe);
			}

			void joinMonitor() noexcept
			{
				if (!isNativeHandleValid(m_MonitorThread))
				{
					return;
				}

				DWORD waitResult = WaitForSingleObject(m_MonitorThread, PROCESS_WORKER_JOIN_TIMEOUT_MILLISECONDS);

				if (waitResult == WAIT_TIMEOUT)
				{
					(void)TerminateJobObject(m_Job, ERROR_PROCESS_ABORTED);
					waitResult = WaitForSingleObject(m_MonitorThread, INFINITE);
				}

				ASSERT(waitResult == WAIT_OBJECT_0, "A process monitor must terminate after its job exits.");
				closeNativeHandle(&m_MonitorThread);
			}

			void runMonitor() noexcept
			{
				const DWORD waitResult = WaitForSingleObject(m_Process, INFINITE);
				DWORD exitCode = ERROR_PROCESS_ABORTED;
				bool bWaitFailed = waitResult != WAIT_OBJECT_0;

				if (!bWaitFailed && GetExitCodeProcess(m_Process, &exitCode) == FALSE)
				{
					bWaitFailed = true;
					exitCode = ERROR_PROCESS_ABORTED;
				}

				(void)TerminateJobObject(m_Job, exitCode);
				joinReader(&m_StandardOutputThread, &m_StandardOutputRead);
				joinReader(&m_StandardErrorThread, &m_StandardErrorRead);

				SharedPtr<PendingProcessWait> pending;
				bool bClaimed = false;
				{
					LockGuard lock(m_Mutex);
					m_ExitCode = exitCode;
					m_bWaitFailed = bWaitFailed;
					m_Phase = ENativeProcessPhase::Exited;
					pending = m_PendingWait;
					m_PendingWait.Reset();

					if (pending && !pending->IsCompletionClaimed())
					{
						bClaimed = pending->TryClaimCompletion();
					}
				}

				if (pending)
				{
					pending->Registration().Reset();

					if (bClaimed)
					{
						if (bWaitFailed)
						{
							pending->CompleteClaimed(Result<ProcessExit>::Failure(
								makeProcessError(EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-WAIT",
									"Windows could not observe the owned process exit.",
									"Inspect native handle lifetime and retry the process operation.")));
						}
						else
						{
							pending->CompleteClaimed(buildExitResult(pending->ResultResource()));
						}
					}
				}

				if (m_bStandardOutputAllocationFailed || m_bStandardErrorAllocationFailed || m_bStandardOutputReadFailed || m_bStandardErrorReadFailed)
				{
					LOG_WARNING("Platform.Process", "Windows process output capture completed with degradation.");
				}

				LOG_INFO("Platform.Process", "A Windows process tree exited.");
			}

			static unsigned __stdcall standardOutputThreadEntry(void* const context) noexcept
			{
				ASSERT(context != nullptr, "The process stdout reader requires its process state.");
				NativeProcess& process = *static_cast<NativeProcess*>(context);
				process.runReader(process.m_StandardOutputRead, process.m_StandardOutput, &process.m_bStandardOutputTruncated, &process.m_bStandardOutputReadFailed, &process.m_bStandardOutputAllocationFailed);
				return 0;
			}

			static unsigned __stdcall standardErrorThreadEntry(void* const context) noexcept
			{
				ASSERT(context != nullptr, "The process stderr reader requires its process state.");
				NativeProcess& process = *static_cast<NativeProcess*>(context);
				process.runReader(process.m_StandardErrorRead, process.m_StandardError, &process.m_bStandardErrorTruncated, &process.m_bStandardErrorReadFailed, &process.m_bStandardErrorAllocationFailed);
				return 0;
			}

			static unsigned __stdcall monitorThreadEntry(void* const context) noexcept
			{
				ASSERT(context != nullptr, "The process monitor requires its process state.");
				static_cast<NativeProcess*>(context)->runMonitor();

				return 0;
			}

		private:
			MemoryResource& m_Resource;
			usize m_MaximumCapturedOutputBytes = 0;
			HANDLE m_Process = nullptr;
			HANDLE m_Job = nullptr;
			HANDLE m_StandardOutputRead = nullptr;
			HANDLE m_StandardErrorRead = nullptr;
			HANDLE m_StandardOutputThread = nullptr;
			HANDLE m_StandardErrorThread = nullptr;
			HANDLE m_MonitorThread = nullptr;
			Array<Byte> m_StandardOutput;
			Array<Byte> m_StandardError;
			Mutex m_Mutex;
			SharedPtr<PendingProcessWait> m_PendingWait;
			ENativeProcessPhase m_Phase = ENativeProcessPhase::Running;
			uint32 m_ExitCode = 0;
			bool m_bTerminationRequested = false;
			bool m_bTerminationIssued = false;
			bool m_bWaitFailed = false;
			bool m_bStandardOutputTruncated = false;
			bool m_bStandardOutputReadFailed = false;
			bool m_bStandardOutputAllocationFailed = false;
			bool m_bStandardErrorTruncated = false;
			bool m_bStandardErrorReadFailed = false;
			bool m_bStandardErrorAllocationFailed = false;
		};

		struct ProcessSlot final
		{
		public:
			NativeProcess* Process = nullptr;
			uint32 Generation = 1;
			bool bReserved = false;
			bool bGenerationExhausted = false;
		};
	} // namespace

	class WindowsProcessService::State final
	{
	public:
		State(MemoryResource& resource, const WindowsProcessServiceOptions options) noexcept
			: m_Resource(resource)
			, m_Options(options)
			, m_Slots(resource)
		{
			m_Owner = s_ProcessServiceOwner.fetch_add(1, MemoryOrderRelaxed);
			ASSERT(m_Owner != 0 && m_Owner != NumericLimits<uint64>::max(),
				"Process service owner identity space must not be exhausted.");
		}

		~State() noexcept
		{
			Shutdown();
		}

		[[nodiscard]] bool Initialize() noexcept
		{
			return m_Slots.Resize(m_Options.MaximumProcesses);
		}

		[[nodiscard]] Task<ProcessHandle> Launch(const ProcessLaunchDesc& desc, const CancellationToken cancellation)
		{
			if (cancellation.IsCancellationRequested())
			{
				return MakeReadyTask(makeLaunchCancelledResult());
			}

			if (desc.MaximumCapturedOutputBytesPerStream == 0 ||
				desc.MaximumCapturedOutputBytesPerStream > MAXIMUM_CAPTURED_PROCESS_OUTPUT_BYTES ||
				desc.Arguments.Size() > MAXIMUM_PROCESS_ARGUMENT_COUNT ||
				desc.ExecutablePath.Size() > MAXIMUM_WINDOWS_PROCESS_TEXT_UTF8_BYTES ||
				desc.WorkingDirectoryPath.Size() > MAXIMUM_WINDOWS_PROCESS_TEXT_UTF8_BYTES)
			{
				return MakeReadyTask(Result<ProcessHandle>::Failure(
					makeProcessError(EErrorCode::InvalidArgument, "ROX-PLATFORM-PROCESS-DESC",
						"The process descriptor exceeds its argument or output-capture bound.",
						"Use the documented nonzero bounded process descriptor values.")));
			}

			for (const StringView argument : desc.Arguments)
			{
				if (argument.Size() > MAXIMUM_WINDOWS_PROCESS_TEXT_UTF8_BYTES)
				{
					return MakeReadyTask(Result<ProcessHandle>::Failure(
						makeProcessError(EErrorCode::InvalidArgument, "ROX-PLATFORM-PROCESS-DESC",
							"A process argument exceeds the bounded UTF-8 input size.",
							"Pass individual arguments within the Windows command-line bound.")));
				}
			}

			Array<wchar_t> executable(m_Resource);
			const ETextBuildResult executableResult = convertUtf8ToWide(m_Resource, desc.ExecutablePath, false, &executable);

			if (executableResult != ETextBuildResult::Success || !isAbsoluteWindowsPath(executable))
			{
				return MakeReadyTask(makeTextFailure(executableResult, "The executable path is not a valid absolute UTF-8 Windows path."));
			}

			Array<wchar_t> workingDirectory(m_Resource);

			if (!desc.WorkingDirectoryPath.IsEmpty())
			{
				const ETextBuildResult workingDirectoryResult = convertUtf8ToWide(m_Resource, desc.WorkingDirectoryPath, false, &workingDirectory);

				if (workingDirectoryResult != ETextBuildResult::Success || !isAbsoluteWindowsPath(workingDirectory))
				{
					return MakeReadyTask(makeTextFailure(workingDirectoryResult, "The working directory is not a valid absolute UTF-8 Windows path."));
				}
			}

			Array<wchar_t> commandLine(m_Resource);
			const ETextBuildResult commandLineResult = buildCommandLine(m_Resource, executable, desc.Arguments, &commandLine);

			if (commandLineResult != ETextBuildResult::Success)
			{
				return MakeReadyTask(makeTextFailure(commandLineResult, "The arguments are invalid or exceed the Windows command-line limit."));
			}

			LockGuard lock(m_Mutex);

			if (m_bStopping)
			{
				return MakeReadyTask(makeLaunchCancelledResult());
			}

			ProcessSlot* const slot = reserveSlot();

			if (slot == nullptr)
			{
				return MakeReadyTask(Result<ProcessHandle>::Failure(makeProcessError(
					EErrorCode::Conflict, "ROX-PLATFORM-PROCESS-CAPACITY", "Every configured process-service slot is occupied.",
					"Release an exited process or increase the reviewed service bound.")));
			}

			const uint32 slotIndex = static_cast<uint32>(slot - m_Slots.Data());

			Result<ProcessHandle> launchResult = launchReserved(*slot, slotIndex, desc, executable, workingDirectory, commandLine, cancellation);

			if (!launchResult)
			{
				slot->bReserved = false;
			}

			return MakeReadyTask(Move(launchResult));
		}

		[[nodiscard]] Task<ProcessExit> Wait(const ProcessHandle process, MemoryResource& resultResource, const CancellationToken cancellation)
		{
			LockGuard lock(m_Mutex);
			Result<NativeProcess*> resolved = resolve(process);

			if (!resolved)
			{
				return MakeReadyTask(Result<ProcessExit>::Failure(resolved.GetError()));
			}

			return resolved.Value()->Wait(resultResource, cancellation);
		}

		[[nodiscard]] Result<void> RequestTermination(const ProcessHandle process)
		{
			LockGuard lock(m_Mutex);
			Result<NativeProcess*> resolved = resolve(process);

			if (!resolved)
			{
				return Result<void>::Failure(resolved.GetError());
			}

			return resolved.Value()->RequestTermination();
		}

		[[nodiscard]] Result<void> Release(const ProcessHandle process)
		{
			LockGuard lock(m_Mutex);
			Result<NativeProcess*> resolved = resolve(process);

			if (!resolved)
			{
				return Result<void>::Failure(resolved.GetError());
			}

			ProcessSlot& slot = m_Slots[process.Slot];
			SharedPtr<PendingProcessWait> detachedPending;

			if (!resolved.Value()->CanRelease(&detachedPending))
			{
				return Result<void>::Failure(
					makeProcessError(EErrorCode::Conflict, "ROX-PLATFORM-PROCESS-RELEASE-ACTIVE",
						"The process is still running or has a pending Wait operation.",
						"Wait for process exit and consume or cancel the pending observation before release."));
			}

			if (detachedPending)
			{
				detachedPending->Registration().Reset();
			}

			resolved.Value()->JoinForRelease();
			DestroyObject(m_Resource, slot.Process);
			slot.Process = nullptr;

			if (slot.Generation == NumericLimits<uint32>::max())
			{
				slot.bGenerationExhausted = true;
				LOG_WARNING("Platform.Process", "A process slot exhausted its generation space.");
			}
			else
			{
				++slot.Generation;
			}

			return Result<void>::Success();
		}

		void Shutdown() noexcept
		{
			LockGuard lock(m_Mutex);

			if (m_bStopping)
			{
				return;
			}

			m_bStopping = true;

			for (ProcessSlot& slot : m_Slots)
			{
				if (slot.Process == nullptr)
				{
					slot.bReserved = false;
					continue;
				}

				slot.Process->CancelPendingForShutdown();
				(void)slot.Process->RequestTermination();
				slot.Process->JoinForRelease();
				DestroyObject(m_Resource, slot.Process);
				slot.Process = nullptr;
				slot.bReserved = false;
			}

			LOG_INFO("Platform.Process", "The Windows process service shut down.");
		}

	private:
		[[nodiscard]] Result<ProcessHandle> makeTextFailure(const ETextBuildResult result, const StringView message) const
		{
			if (result == ETextBuildResult::AllocationFailed)
			{
				return Result<ProcessHandle>::Failure(makeProcessError(
					EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-MEMORY", "Memory allocation for process launch text failed.",
					"Release bounded process-service memory and retry launch."));
			}

			return Result<ProcessHandle>::Failure(
				makeProcessError(EErrorCode::InvalidArgument, "ROX-PLATFORM-PROCESS-DESC", message,
					"Provide valid absolute UTF-8 paths and individual bounded arguments."));
		}

		[[nodiscard]] ProcessSlot* reserveSlot() noexcept
		{
			for (ProcessSlot& slot : m_Slots)
			{
				if (slot.Process == nullptr && !slot.bReserved && !slot.bGenerationExhausted)
				{
					slot.bReserved = true;

					return &slot;
				}
			}

			return nullptr;
		}

		[[nodiscard]] Result<ProcessHandle> launchReserved(
			ProcessSlot& slot,
			const uint32 slotIndex,
			const ProcessLaunchDesc& desc,
			const Array<wchar_t>& executable,
			const Array<wchar_t>& workingDirectory,
			Array<wchar_t>& commandLine,
			const CancellationToken cancellation)
		{
			ASSERT(slot.bReserved && slot.Process == nullptr, "A native process launch requires one reserved empty slot.");

			ScopedNativeHandle standardOutputRead;
			ScopedNativeHandle standardOutputWrite;
			ScopedNativeHandle standardErrorRead;
			ScopedNativeHandle standardErrorWrite;

			if (!createOutputPipe(&standardOutputRead, &standardOutputWrite) || !createOutputPipe(&standardErrorRead, &standardErrorWrite))
			{
				return Result<ProcessHandle>::Failure(makeProcessError(
					EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-CREATE",
					"Windows could not create redirected process output pipes.", "Release native handle pressure and retry launch."));
			}

			SECURITY_ATTRIBUTES inputSecurity{};
			inputSecurity.nLength = sizeof(inputSecurity);
			inputSecurity.bInheritHandle = TRUE;
			ScopedNativeHandle standardInput(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inputSecurity, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));

			if (!standardInput.IsValid())
			{
				return Result<ProcessHandle>::Failure(makeProcessError(
					EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-CREATE",
					"Windows could not open the child process input sink.", "Inspect native handle pressure and retry launch."));
			}

			HANDLE inheritedHandles[] = { standardInput.Get(), standardOutputWrite.Get(), standardErrorWrite.Get() };
			ProcessAttributeList attributeList;
			const ETextBuildResult attributeResult = attributeList.TryInitialize(m_Resource, inheritedHandles, 3);

			if (attributeResult != ETextBuildResult::Success)
			{
				return Result<ProcessHandle>::Failure(makeProcessError(
					attributeResult == ETextBuildResult::AllocationFailed ? EErrorCode::ExternalFailure
					: EErrorCode::ExternalFailure,
					attributeResult == ETextBuildResult::AllocationFailed ? "ROX-PLATFORM-PROCESS-MEMORY" : "ROX-PLATFORM-PROCESS-CREATE",
					"Windows process handle-inheritance setup failed.", "Release native or memory pressure and retry launch."));
			}

			ScopedNativeHandle job(CreateJobObjectW(nullptr, nullptr));
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInformation{};
			jobInformation.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

			if (!job.IsValid() || SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &jobInformation, sizeof(jobInformation)) == FALSE)
			{
				return Result<ProcessHandle>::Failure(makeProcessError(
					EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-JOB", "Windows could not create the owned process Job Object.",
					"Inspect Job Object support and native handle pressure before retrying."));
			}

			STARTUPINFOEXW startup{};
			startup.StartupInfo.cb = sizeof(startup);
			startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
			startup.StartupInfo.hStdInput = standardInput.Get();
			startup.StartupInfo.hStdOutput = standardOutputWrite.Get();
			startup.StartupInfo.hStdError = standardErrorWrite.Get();
			startup.lpAttributeList = attributeList.Get();

			DWORD creationFlags = CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;

			if (desc.bHidden)
			{
				creationFlags |= CREATE_NO_WINDOW;
			}

			PROCESS_INFORMATION information{};
			const BOOL bCreated = CreateProcessW(executable.Data(), commandLine.Data(), nullptr, nullptr, TRUE, creationFlags, nullptr, workingDirectory.IsEmpty() ? nullptr : workingDirectory.Data(), &startup.StartupInfo, &information);

			if (bCreated == FALSE)
			{
				return makeCreateProcessFailure(GetLastError());
			}

			ScopedNativeHandle process(information.hProcess);
			ScopedNativeHandle primaryThread(information.hThread);
			standardInput.Reset();
			standardOutputWrite.Reset();
			standardErrorWrite.Reset();

			if (AssignProcessToJobObject(job.Get(), process.Get()) == FALSE)
			{
				(void)TerminateProcess(process.Get(), ERROR_PROCESS_ABORTED);
				(void)WaitForSingleObject(process.Get(), PROCESS_WORKER_JOIN_TIMEOUT_MILLISECONDS);

				return Result<ProcessHandle>::Failure(
					makeProcessError(EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-JOB",
						"Windows could not assign the suspended process to its Job Object.",
						"Inspect parent Job Object restrictions before retrying."));
			}

			NativeProcess* const nativeProcess = CreateObject<NativeProcess>(m_Resource, m_Resource, desc.MaximumCapturedOutputBytesPerStream);

			if (nativeProcess == nullptr)
			{
				(void)TerminateJobObject(job.Get(), ERROR_PROCESS_ABORTED);
				(void)WaitForSingleObject(process.Get(), PROCESS_WORKER_JOIN_TIMEOUT_MILLISECONDS);

				return Result<ProcessHandle>::Failure(makeProcessError(
					EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-MEMORY", "Memory allocation for process ownership failed.",
					"Release bounded process-service memory and retry launch."));
			}

			nativeProcess->Adopt(process.Release(), job.Release(), standardOutputRead.Release(), standardErrorRead.Release());

			if (!nativeProcess->StartWorkers())
			{
				nativeProcess->AbortUnpublished();
				DestroyObject(m_Resource, nativeProcess);

				return Result<ProcessHandle>::Failure(makeProcessError(
					EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-THREAD",
					"Windows could not start the bounded process worker threads.", "Release native thread pressure and retry launch."));
			}

			if (cancellation.IsCancellationRequested())
			{
				nativeProcess->AbortUnpublished();
				DestroyObject(m_Resource, nativeProcess);

				return makeLaunchCancelledResult();
			}

			slot.Process = nativeProcess;
			slot.bReserved = false;

			if (ResumeThread(primaryThread.Get()) == NumericLimits<DWORD>::max())
			{
				slot.Process = nullptr;
				nativeProcess->AbortUnpublished();
				DestroyObject(m_Resource, nativeProcess);

				return Result<ProcessHandle>::Failure(
					makeProcessError(EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-RESUME",
						"Windows could not resume the suspended child process.",
						"Inspect process security and native thread state before retrying."));
			}

			LOG_INFO("Platform.Process", "A Windows process tree was launched.");

			return Result<ProcessHandle>::Success({ m_Owner, slotIndex, slot.Generation });
		}

		[[nodiscard]] Result<ProcessHandle> makeCreateProcessFailure(const DWORD error) const
		{
			if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_BAD_PATHNAME)
			{
				return Result<ProcessHandle>::Failure(
					makeProcessError(EErrorCode::NotFound, "ROX-PLATFORM-PROCESS-NOT-FOUND",
						"Windows could not locate the explicit executable or working directory.",
						"Provide existing absolute executable and working-directory paths."));
			}

			if (error == ERROR_ACCESS_DENIED || error == ERROR_ELEVATION_REQUIRED)
			{
				return Result<ProcessHandle>::Failure(
					makeProcessError(EErrorCode::PermissionDenied, "ROX-PLATFORM-PROCESS-ACCESS",
						"Windows denied creation of the requested executable.",
						"Check executable permissions, policy, and elevation requirements."));
			}

			return Result<ProcessHandle>::Failure(
				makeProcessError(EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-CREATE",
					"Windows process creation failed.",
					"Inspect the executable, working directory, policy, and native process limits."));
		}

		[[nodiscard]] Result<NativeProcess*> resolve(const ProcessHandle handle) const
		{
			if (!handle.IsValid())
			{
				return Result<NativeProcess*>::Failure(
					makeProcessError(EErrorCode::InvalidArgument, "ROX-PLATFORM-PROCESS-HANDLE",
						"The process handle is structurally invalid.", "Use a handle returned by this process service."));
			}

			if (handle.Owner != m_Owner)
			{
				return Result<NativeProcess*>::Failure(makeProcessError(
					EErrorCode::PermissionDenied, "ROX-PLATFORM-PROCESS-HANDLE",
					"The process handle belongs to another service instance.", "Use the process service that created the handle."));
			}

			if (handle.Slot >= m_Slots.Size())
			{
				return Result<NativeProcess*>::Failure(makeProcessError(EErrorCode::NotFound, "ROX-PLATFORM-PROCESS-HANDLE",
					"The process handle slot is outside this service.",
					"Discard the invalid process handle."));
			}

			const ProcessSlot& slot = m_Slots[handle.Slot];

			if (handle.Generation != slot.Generation)
			{
				return Result<NativeProcess*>::Failure(makeProcessError(EErrorCode::Conflict, "ROX-PLATFORM-PROCESS-HANDLE",
					"The process handle generation is stale.",
					"Use the current handle returned by a later launch."));
			}

			if (slot.Process == nullptr)
			{
				return Result<NativeProcess*>::Failure(
					makeProcessError(EErrorCode::NotFound, "ROX-PLATFORM-PROCESS-HANDLE",
						"The process handle no longer identifies a live service record.",
						"Launch another process and retain its returned handle."));
			}

			return Result<NativeProcess*>::Success(slot.Process);
		}

	private:
		MemoryResource& m_Resource;
		WindowsProcessServiceOptions m_Options;
		Array<ProcessSlot> m_Slots;
		uint64 m_Owner = 0;
		Mutex m_Mutex;
		bool m_bStopping = false;
	};

	WindowsProcessService::WindowsProcessService(MemoryResource& resource, State* const state) noexcept
		: m_Resource(&resource)
		, m_State(state)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "A Windows process service requires initialized state.");
	}

	WindowsProcessService::~WindowsProcessService() noexcept
	{
		if (m_State != nullptr)
		{
			m_State->Shutdown();
			DestroyObject(*m_Resource, m_State);
			m_State = nullptr;
			m_Resource = nullptr;
		}
	}

	WindowsProcessService::WindowsProcessService(WindowsProcessService&& other) noexcept
		: m_Resource(other.m_Resource)
		, m_State(other.m_State)
	{
		other.m_Resource = nullptr;
		other.m_State = nullptr;
		assertInvariant();
		other.assertInvariant();
	}

	WindowsProcessService& WindowsProcessService::operator=(WindowsProcessService&& other) noexcept
	{
		if (this != &other)
		{
			if (m_State != nullptr)
			{
				m_State->Shutdown();
				DestroyObject(*m_Resource, m_State);
			}

			m_Resource = other.m_Resource;
			m_State = other.m_State;
			other.m_Resource = nullptr;
			other.m_State = nullptr;
		}

		assertInvariant();
		other.assertInvariant();

		return *this;
	}

	Result<void> WindowsProcessService::TryCreate(MemoryResource& resource, const WindowsProcessServiceOptions options,
		WindowsProcessService* const outService)
	{
		ASSERT(outService != nullptr, "Windows process service creation requires an output object.");
		ASSERT(!outService->IsValid(), "Windows process service creation requires an empty output object.");
		ASSERT(resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Windows process services require a thread-safe memory resource.");

		if (options.MaximumProcesses == 0 || options.MaximumProcesses > MAXIMUM_PROCESS_COUNT)
		{
			return Result<void>::Failure(makeProcessError(EErrorCode::InvalidArgument, "ROX-PLATFORM-PROCESS-DESC",
				"The Windows process-service capacity is outside the supported bound.",
				"Select a process count between one and sixty-four."));
		}

		State* const state = CreateObject<State>(resource, resource, options);

		if (state == nullptr || !state->Initialize())
		{
			DestroyObject(resource, state);

			return Result<void>::Failure(makeProcessError(EErrorCode::ExternalFailure, "ROX-PLATFORM-PROCESS-MEMORY",
				"Memory allocation for the Windows process service failed.",
				"Release bounded service memory and retry creation."));
		}

		WindowsProcessService replacement(resource, state);
		*outService = Move(replacement);
		LOG_INFO("Platform.Process", "The Windows process service started.");

		return Result<void>::Success();
	}

	Task<ProcessHandle> WindowsProcessService::Launch(const ProcessLaunchDesc& desc, const CancellationToken cancellation)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Process launch requires a valid Windows process service.");

		return m_State->Launch(desc, cancellation);
	}

	Task<ProcessExit> WindowsProcessService::Wait(const ProcessHandle process, MemoryResource& resultResource,
		const CancellationToken cancellation)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Process Wait requires a valid Windows process service.");
		ASSERT(resultResource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe, "Process Wait requires a thread-safe result memory resource.");
		return m_State->Wait(process, resultResource, cancellation);
	}

	Result<void> WindowsProcessService::RequestTermination(const ProcessHandle process)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Process termination requires a valid Windows process service.");

		return m_State->RequestTermination(process);
	}

	Result<void> WindowsProcessService::Release(const ProcessHandle process)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Process release requires a valid Windows process service.");

		return m_State->Release(process);
	}

	bool WindowsProcessService::IsValid() const noexcept
	{
		assertInvariant();

		return m_State != nullptr;
	}

	void WindowsProcessService::assertInvariant() const noexcept
	{
		ASSERT((m_Resource == nullptr) == (m_State == nullptr),
			"A Windows process service must retain its memory resource and state together.");
	}

	Result<Hash256> ReadCurrentWindowsProcessImageHash(uint64 maximumBytes)
	{
		if (maximumBytes == 0 || maximumBytes > MAXIMUM_PROCESS_IMAGE_HASH_BYTES)
		{
			return Result<Hash256>::Failure(EErrorCode::InvalidArgument, "ROX-PLATFORM-PROCESS-IMAGE-LIMIT",
				"The process image hash limit is outside its supported range.",
				"Supply a positive limit no larger than MAXIMUM_PROCESS_IMAGE_HASH_BYTES.");
		}

		wchar_t path[32768];
		const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(Size(path)));

		if (length == 0 || length >= Size(path))
		{
			return imageFailure("The current process executable path could not be read without truncation.");
		}

		const ImageHandle file(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));

		if (file.GetHandle() == INVALID_HANDLE_VALUE)
		{
			return imageFailure("The current process executable could not be opened for hashing.");
		}

		LARGE_INTEGER size;

		if (!GetFileSizeEx(file.GetHandle(), &size))
		{
			return imageFailure("The current process executable size could not be read.");
		}

		if (size.QuadPart <= 0 || static_cast<uint64>(size.QuadPart) > maximumBytes)
		{
			return Result<Hash256>::Failure(EErrorCode::Unsupported, "ROX-PLATFORM-PROCESS-IMAGE-LIMIT",
				"The current executable is empty or exceeds the image hash byte limit.",
				"Use an executable within the configured process image hash bound.");
		}

		const ImageHandle mapping(CreateFileMappingW(file.GetHandle(), nullptr, PAGE_READONLY, 0, 0, nullptr));

		if (mapping.GetHandle() == nullptr)
		{
			return imageFailure("The current process executable could not be mapped read-only.");
		}

		const void* const view = MapViewOfFile(mapping.GetHandle(), FILE_MAP_READ, 0, 0, static_cast<SIZE_T>(size.QuadPart));

		if (view == nullptr)
		{
			return imageFailure("The current process executable mapping could not be viewed.");
		}

		const Hash256 hash = ComputeSha256({ static_cast<const Byte*>(view), static_cast<usize>(size.QuadPart) });
		const BOOL unmapped = UnmapViewOfFile(view);
		ASSERT(unmapped != FALSE, "A process image view must unmap successfully.");

		return Result<Hash256>::Success(hash);
	}

} // namespace rox::os
