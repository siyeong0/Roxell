#include <WindowsStandardStreams.h>
#include <Synchronization.hpp>
#include <TextPrimitives.hpp>
#include <Vocabulary.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <Comparison.hpp>
#include <Utility.hpp>
#include <Cancellation.h>

#include <Assertion.h>
#include <UniquePtr.hpp>
#include <WindowsThreadBackend.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <Algorithms.hpp>

namespace rox::os
{
	namespace
	{
		core::Error streamError(
			core::MemoryResource& resource,
			const core::EErrorCode code,
			const core::StringView id,
			const core::StringView message,
			const DWORD nativeError = 0,
			const rox::core::usize completedBytes = 0)
		{
			char detail[160]{};
			const int count = rox::core::FormatText(detail, sizeof(detail), "native=%lu; completed-prefix=%zu. Keep owners until close completes; retire failed streams.", static_cast<unsigned long>(nativeError), completedBytes);
			const core::StringView hint(detail, count > 0 ? rox::core::Min(static_cast<rox::core::usize>(count), sizeof(detail) - 1) : 0);

			return { code, core::MakeLossyString(id, resource), core::MakeLossyString(message, resource), core::MakeLossyString(hint, resource) };
		}

		core::Error allocationError(core::MemoryResource& resource)
		{
			return streamError(resource, core::EErrorCode::ExternalFailure, "ROX-PLATFORM-STREAM-ALLOCATION", "Standard stream operation storage could not be allocated.");
		}

		struct StreamOperation final
		{
		public:
			// Array/Token ownership and metadata are owner-only. The worker receives the stable raw
			// pointer/size below and never calls a Core allocator or completes a TaskSource.
			core::Array<core::Byte> Buffer;
			core::CancellationToken Cancellation;
			core::NativeThreadEntry Entry;
			core::NativeThread Thread;
			HANDLE Pipe = nullptr;
			OVERLAPPED Overlapped{};
			core::Byte* Data = nullptr;
			rox::core::usize Size = 0;
			bool bWrite = false;
			bool bJoined = false;
			rox::core::Atomic<bool> bCancel = false;
			rox::core::Atomic<bool> bFinished = false;
			DWORD NativeError = ERROR_SUCCESS;
			rox::core::usize CompletedBytes = 0;
			bool bEof = false;

		public:
			StreamOperation(core::MemoryResource& bufferResource, const bool bWrite) noexcept
				: Buffer(bufferResource)
				, bWrite(bWrite)
			{
			}

			~StreamOperation() noexcept
			{
				ASSERT(Thread.Handle == nullptr, "A stream operation must join its worker before destruction.");

				if (Overlapped.hEvent != nullptr)
				{
					const BOOL closed = CloseHandle(Overlapped.hEvent);
					ASSERT(closed != FALSE, "A completed stream event must close.");
				}
			}

		};

		void runStreamOperation(void* context) noexcept
		{
			StreamOperation& operation = *static_cast<StreamOperation*>(context);
			do
			{
				if (operation.bCancel.load(rox::core::MemoryOrderAcquire))
				{
					operation.NativeError = ERROR_OPERATION_ABORTED;
					break;
				}

				const HANDLE event = operation.Overlapped.hEvent;
				operation.Overlapped = {};
				operation.Overlapped.hEvent = event;

				if (ResetEvent(event) == FALSE)
				{
					operation.NativeError = GetLastError();
					break;
				}

				const DWORD requested = static_cast<DWORD>(rox::core::Min<rox::core::usize>(operation.Size - operation.CompletedBytes, 65'536));
				// The cancellation window here cannot be closed by another flag check. The owner
				// latches cancellation and repeats both targeted requests until this worker exits.
				DWORD transferred = 0;
				BOOL succeeded = operation.bWrite ? WriteFile(operation.Pipe, operation.Data + operation.CompletedBytes, requested, nullptr, &operation.Overlapped) : ReadFile(operation.Pipe, operation.Data, requested, nullptr, &operation.Overlapped);
				DWORD error = succeeded ? ERROR_SUCCESS : GetLastError();

				if (succeeded || error == ERROR_IO_PENDING)
				{
					// Also supports an inherited byte pipe opened with FILE_FLAG_OVERLAPPED. The
					// event/OVERLAPPED/buffer remain alive; owner CancelIoEx targets this operation.
					succeeded = GetOverlappedResult(operation.Pipe, &operation.Overlapped, &transferred, TRUE);
					error = succeeded ? ERROR_SUCCESS : GetLastError();
				}

				if (!succeeded)
				{
					if (!operation.bWrite && (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED))
					{
						operation.bEof = true;
					}
					else
					{
						operation.NativeError = error;
					}

					break;
				}

				if (transferred > requested)
				{
					operation.NativeError = ERROR_INVALID_DATA;
					break;
				}

				operation.CompletedBytes += transferred;

				if (!operation.bWrite)
				{
					// A successful zero-byte pipe read can be a peer's zero-byte write, not EOF.
					// Keep waiting for data, broken pipe, or cancellation without publishing false EOF.
					if (transferred == 0)
					{
						continue;
					}

					break;
				}

				if (transferred == 0)
				{
					operation.NativeError = ERROR_WRITE_FAULT;
					break;
				}
			} while (operation.CompletedBytes < operation.Size);
			operation.bFinished.store(true, rox::core::MemoryOrderRelease);
		}

		core::Result<HANDLE> duplicatePipe(core::MemoryResource& resource, const DWORD selector)
		{
			const HANDLE original = GetStdHandle(selector);

			if (original == nullptr || original == INVALID_HANDLE_VALUE)
			{
				return core::Result<HANDLE>::Failure(streamError(resource, core::EErrorCode::InvalidArgument, "ROX-PLATFORM-STREAM-HANDLE", "The current standard stream handle is missing or invalid."));
			}

			HANDLE duplicate = nullptr;

			if (DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS) == FALSE)
			{
				return core::Result<HANDLE>::Failure(streamError(resource, core::EErrorCode::ExternalFailure,
					"ROX-PLATFORM-STREAM-DUPLICATE",
					"A standard stream handle could not be duplicated.", GetLastError()));
			}

			DWORD flags = 0;
			DWORD mode = 0;
			DWORD error = ERROR_SUCCESS;
			bool bSupported = GetFileType(duplicate) == FILE_TYPE_PIPE;

			if (bSupported && (GetNamedPipeInfo(duplicate, &flags, nullptr, nullptr, nullptr) == FALSE || GetNamedPipeHandleStateW(duplicate, &mode, nullptr, nullptr, nullptr, nullptr, 0) == FALSE))
			{
				error = GetLastError();
				bSupported = false;
			}

			bSupported = bSupported && (flags & PIPE_TYPE_MESSAGE) == 0 && (mode & (PIPE_READMODE_MESSAGE | PIPE_NOWAIT)) == 0;

			if (!bSupported)
			{
				const BOOL closed = CloseHandle(duplicate);
				ASSERT(closed != FALSE, "An unpublished duplicate must close after unsupported stream detection.");

				return core::Result<HANDLE>::Failure(streamError(resource, core::EErrorCode::Unsupported, "ROX-PLATFORM-STREAM-SUPPORT", "Standard streams require queryable byte-mode blocking pipes.", error));
			}

			return core::Result<HANDLE>::Success(duplicate);
		}
	} // namespace

	class WindowsStandardStreams::State final
	{
	public:
		core::MemoryResource& Resource;
		StandardStreamsOptions Options;
		HANDLE Input = nullptr;
		HANDLE Output = nullptr;
		DWORD OwnerThread = GetCurrentThreadId();
		WindowsThreadBackend Threads;
		core::UniquePtr<StreamOperation> ReadOperation;
		core::UniquePtr<StreamOperation> WriteOperation;
		bool bClosing = false;
		bool bClosed = false;
		bool bReadRetired = false;
		bool bWriteRetired = false;

	public:
		State(core::MemoryResource& resource, const StandardStreamsOptions options, const HANDLE input, const HANDLE output) noexcept
			: Resource(resource)
			, Options(options)
			, Input(input)
			, Output(output)
		{
		}

		~State() noexcept
		{
			AssertOwner();
			ASSERT(bClosed && Input == nullptr && Output == nullptr, "Standard streams must finish explicit close before state destruction.");
		}

		void AssertOwner() const noexcept
		{
			ASSERT(OwnerThread == GetCurrentThreadId(), "Standard streams must be operated on their creating thread.");
		}

		core::Result<void> Admit(const bool bWrite, const rox::core::usize size, core::MemoryResource& bufferResource, const core::Span<const core::Byte> input, core::CancellationToken cancellation)
		{
			AssertOwner();
			core::UniquePtr<StreamOperation>& slot = bWrite ? WriteOperation : ReadOperation;

			if (bClosing || bClosed || slot || (bWrite ? bWriteRetired : bReadRetired))
			{
				return core::Result<void>::Failure(streamError(Resource, core::EErrorCode::Conflict, "ROX-PLATFORM-STREAM-BUSY", "The stream is closing, retired, or retains an unconsumed operation."));
			}

			if (size == 0 || size > (bWrite ? Options.MaximumWriteBytes : Options.MaximumReadBytes) || (bWrite && input.Data() == nullptr))
			{
				return core::Result<void>::Failure(streamError(Resource, core::EErrorCode::InvalidArgument, "ROX-PLATFORM-STREAM-BOUNDS", "The standard stream byte count is empty or exceeds its explicit bound."));
			}

			if (cancellation.IsCancellationRequested())
			{
				return core::Result<void>::Failure(streamError(Resource, core::EErrorCode::Cancelled, "ROX-PLATFORM-STREAM-CANCELLED", "Standard stream admission was already cancelled."));
			}

			core::UniquePtr<StreamOperation> operation = core::MakeUnique<StreamOperation>(Resource, bufferResource, bWrite);

			if (!operation || !operation->Buffer.ResizeUninitialized(size))
			{
				return core::Result<void>::Failure(allocationError(Resource));
			}

			if (bWrite)
			{
				rox::core::CopyBytes(operation->Buffer.Data(), input.Data(), size);
			}

			operation->Cancellation = rox::core::Move(cancellation);
			operation->Pipe = bWrite ? Output : Input;
			operation->Data = operation->Buffer.Data();
			operation->Size = size;
			operation->Overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

			if (operation->Overlapped.hEvent == nullptr)
			{
				return core::Result<void>::Failure(streamError(Resource, core::EErrorCode::ExternalFailure, "ROX-PLATFORM-STREAM-EVENT", "A stream completion event could not be created.", GetLastError()));
			}

			operation->Entry = { &runStreamOperation, operation.Get() };
			const core::NativeThreadStartResult started = Threads.Start(operation->Entry);

			if (started.Result.Status != core::EThreadStartStatus::Started)
			{
				return core::Result<void>::Failure(streamError(Resource, core::EErrorCode::ExternalFailure, "ROX-PLATFORM-STREAM-THREAD", "A standard stream worker could not start.", started.Result.NativeError));
			}

			operation->Thread = started.Thread;
			slot = rox::core::Move(operation);

			return core::Result<void>::Success();
		}

		core::Result<void> PollOperation(StreamOperation* operation)
		{
			if (operation == nullptr || operation->bJoined)
			{
				return core::Result<void>::Success();
			}

			DWORD waited = WaitForSingleObject(operation->Thread.Handle, 0);

			if (waited == WAIT_FAILED)
			{
				return core::Result<void>::Failure(streamError(Resource, core::EErrorCode::ExternalFailure, "ROX-PLATFORM-STREAM-POLL", "A stream worker termination could not be observed.", GetLastError()));
			}

			if (waited == WAIT_OBJECT_0)
			{
				ASSERT(operation->bFinished.load(rox::core::MemoryOrderAcquire), "A terminated I/O worker must publish its native result.");
				// Join is entered only after the native thread object is signaled. No blocking I/O
				// or unbounded destructor wait is hidden in the public polling contract.
				Threads.Join(operation->Thread);
				operation->Thread = {};
				operation->bJoined = true;

				if (operation->NativeError != ERROR_SUCCESS)
				{
					(operation->bWrite ? bWriteRetired : bReadRetired) = true;
				}

				return core::Result<void>::Success();
			}

			if (bClosing || operation->Cancellation.IsCancellationRequested())
			{
				operation->bCancel.store(true, rox::core::MemoryOrderRelease);
			}

			if (!operation->bCancel.load(rox::core::MemoryOrderAcquire))
			{
				return core::Result<void>::Success();
			}

			DWORD cancelError = ERROR_SUCCESS;

			if (CancelSynchronousIo(operation->Thread.Handle) == FALSE)
			{
				cancelError = GetLastError();

				if (cancelError == ERROR_NOT_FOUND)
				{
					cancelError = ERROR_SUCCESS;
				}
			}
			// Target only our OVERLAPPED. Never cancel unrelated operations on the original or
			// duplicated handle. A cancellation miss does not disarm subsequent polling.
			if (CancelIoEx(operation->Pipe, &operation->Overlapped) == FALSE)
			{
				const DWORD error = GetLastError();

				if (error != ERROR_NOT_FOUND && cancelError == ERROR_SUCCESS)
				{
					cancelError = error;
				}
			}

			if (cancelError != ERROR_SUCCESS)
			{
				return core::Result<void>::Failure(
					streamError(Resource, core::EErrorCode::ExternalFailure, "ROX-PLATFORM-STREAM-CANCEL-POLL",
						"A native stream cancellation request failed; the operation remains owned.", cancelError));
			}

			return core::Result<void>::Success();
		}

		core::Error OperationError(StreamOperation& operation)
		{
			const bool bCancelled = operation.NativeError == ERROR_OPERATION_ABORTED;
			const core::StringView diagnostic = bCancelled ? core::StringView("ROX-PLATFORM-STREAM-CANCELLED")
				: (operation.bWrite ? core::StringView("ROX-PLATFORM-STREAM-WRITE")
					: core::StringView("ROX-PLATFORM-STREAM-READ"));
			const core::StringView message =
				operation.bWrite ? core::StringView("Standard output did not complete; retire the output without retransmission.")
				: core::StringView("Standard input did not complete; retire the input stream.");
			return streamError(operation.Buffer.GetResource(), bCancelled ? core::EErrorCode::Cancelled : core::EErrorCode::ExternalFailure,
				diagnostic, message, operation.NativeError, operation.CompletedBytes);
		}

	};

	WindowsStandardStreams::~WindowsStandardStreams() noexcept
	{
		if (m_State != nullptr)
		{
			m_State->AssertOwner();
			ASSERT(m_State->bClosed, "Windows standard streams require completed explicit close.");
			core::DestroyObject(*m_Resource, m_State);
		}
	}

	core::Result<StandardStreamsOwner> WindowsStandardStreams::Create(core::MemoryResource& resource, const StandardStreamsOptions options)
	{
		if (options.MaximumReadBytes == 0 || options.MaximumWriteBytes == 0 || options.MaximumReadBytes > MAXIMUM_STANDARD_STREAM_BYTES || options.MaximumWriteBytes > MAXIMUM_STANDARD_STREAM_BYTES)
		{
			return core::Result<StandardStreamsOwner>::Failure(
				streamError(resource, core::EErrorCode::InvalidArgument, "ROX-PLATFORM-STREAM-BOUNDS",
					"Standard stream creation requires positive bounded read/write capacities."));
		}

		core::Result<HANDLE> input = duplicatePipe(resource, STD_INPUT_HANDLE);

		if (!input)
		{
			return core::Result<StandardStreamsOwner>::Failure(input.GetError());
		}

		core::Result<HANDLE> output = duplicatePipe(resource, STD_OUTPUT_HANDLE);

		if (!output)
		{
			const BOOL closed = CloseHandle(input.Value());
			ASSERT(closed != FALSE, "A first standard stream duplicate must close when the second is refused.");

			return core::Result<StandardStreamsOwner>::Failure(output.GetError());
		}

		StandardStreamsOwner owner = StandardStreamsOwner::TryCreate<WindowsStandardStreams>(resource);
		State* state = owner ? core::CreateObject<State>(resource, resource, options, input.Value(), output.Value()) : nullptr;

		if (state == nullptr)
		{
			const BOOL inputClosed = CloseHandle(input.Value());
			const BOOL outputClosed = CloseHandle(output.Value());
			ASSERT(inputClosed != FALSE && outputClosed != FALSE, "Unpublished stream duplicates must close after allocation failure.");

			return core::Result<StandardStreamsOwner>::Failure(allocationError(resource));
		}

		WindowsStandardStreams* streams = static_cast<WindowsStandardStreams*>(owner.Get());
		streams->m_Resource = &resource;
		streams->m_State = state;

		return core::Result<StandardStreamsOwner>::Success(rox::core::Move(owner));
	}

	core::Result<void> WindowsStandardStreams::ReadSome(const rox::core::usize maximumBytes, core::MemoryResource& resultResource, core::CancellationToken cancellation)
	{
		ASSERT(m_State != nullptr, "ReadSome requires a created Windows standard stream service.");

		return m_State->Admit(false, maximumBytes, resultResource, {}, rox::core::Move(cancellation));
	}

	core::Result<void> WindowsStandardStreams::Write(const core::Span<const core::Byte> bytes, core::CancellationToken cancellation)
	{
		ASSERT(m_State != nullptr, "Write requires a created Windows standard stream service.");

		return m_State->Admit(true, bytes.Size(), *m_Resource, bytes, rox::core::Move(cancellation));
	}

	core::Result<void> WindowsStandardStreams::Poll()
	{
		ASSERT(m_State != nullptr, "Poll requires a created Windows standard stream service.");
		m_State->AssertOwner();
		core::Result<void> read = m_State->PollOperation(m_State->ReadOperation.Get());
		core::Result<void> write = m_State->PollOperation(m_State->WriteOperation.Get());

		if (!read)
		{
			return read;
		}

		return write;
	}

	core::Result<rox::core::Optional<StandardReadResult>> WindowsStandardStreams::TakeRead()
	{
		ASSERT(m_State != nullptr, "TakeRead requires a created Windows standard stream service.");
		m_State->AssertOwner();
		using Result = core::Result<rox::core::Optional<StandardReadResult>>;
		core::UniquePtr<StreamOperation>& operation = m_State->ReadOperation;

		if (!operation)
		{
			return Result::Failure(streamError(*m_Resource, core::EErrorCode::Conflict, "ROX-PLATFORM-STREAM-NO-READ",
				"No standard input operation remains to consume."));
		}

		if (!operation->bJoined)
		{
			return Result::Success(rox::core::NullOptional);
		}

		if (operation->NativeError != ERROR_SUCCESS)
		{
			core::Error error = m_State->OperationError(*operation);
			operation.Reset();

			return Result::Failure(rox::core::Move(error));
		}

		StandardReadResult result(operation->Buffer.GetResource());
		result.Status = operation->bEof ? EStandardReadStatus::EndOfStream : EStandardReadStatus::Data;
		operation->Buffer.ResizeRequired(operation->CompletedBytes);
		result.Bytes = rox::core::Move(operation->Buffer);
		operation.Reset();

		return Result::Success(rox::core::Optional<StandardReadResult>(rox::core::Move(result)));
	}

	core::Result<rox::core::Optional<StandardWriteResult>> WindowsStandardStreams::TakeWrite()
	{
		ASSERT(m_State != nullptr, "TakeWrite requires a created Windows standard stream service.");
		m_State->AssertOwner();
		using Result = core::Result<rox::core::Optional<StandardWriteResult>>;
		core::UniquePtr<StreamOperation>& operation = m_State->WriteOperation;

		if (!operation)
		{
			return Result::Failure(streamError(*m_Resource, core::EErrorCode::Conflict, "ROX-PLATFORM-STREAM-NO-WRITE", "No standard output operation remains to consume."));
		}

		if (!operation->bJoined)
		{
			return Result::Success(rox::core::NullOptional);
		}

		if (operation->NativeError != ERROR_SUCCESS)
		{
			core::Error error = m_State->OperationError(*operation);
			operation.Reset();

			return Result::Failure(rox::core::Move(error));
		}

		ASSERT(operation->CompletedBytes == operation->Size, "A successful stream write must complete all bytes.");
		const StandardWriteResult result{ operation->CompletedBytes };
		operation.Reset();

		return Result::Success(rox::core::Optional<StandardWriteResult>(result));
	}

	void WindowsStandardStreams::RequestClose() noexcept
	{
		ASSERT(m_State != nullptr, "RequestClose requires a created Windows standard stream service.");
		m_State->AssertOwner();
		m_State->bClosing = true;

		if (m_State->ReadOperation)
		{
			m_State->ReadOperation->bCancel.store(true, rox::core::MemoryOrderRelease);
		}

		if (m_State->WriteOperation)
		{
			m_State->WriteOperation->bCancel.store(true, rox::core::MemoryOrderRelease);
		}
	}

	core::Result<bool> WindowsStandardStreams::PollClose()
	{
		RequestClose();
		core::Result<void> polled = Poll();

		if (!polled)
		{
			return core::Result<bool>::Failure(polled.GetError());
		}

		if ((m_State->ReadOperation && !m_State->ReadOperation->bJoined) || (m_State->WriteOperation && !m_State->WriteOperation->bJoined))
		{
			return core::Result<bool>::Success(false);
		}

		for (HANDLE* handle : { &m_State->Input, &m_State->Output })
		{
			if (*handle != nullptr)
			{
				if (CloseHandle(*handle) == FALSE)
				{
					return core::Result<bool>::Failure(
						streamError(*m_Resource, core::EErrorCode::ExternalFailure, "ROX-PLATFORM-STREAM-CLOSE", "A completed standard stream duplicate could not close.", GetLastError()));
				}

				*handle = nullptr;
			}
		}

		m_State->bClosed = true;

		return core::Result<bool>::Success(true);
	}

	bool WindowsStandardStreams::IsClosed() const noexcept
	{
		if (m_State == nullptr)
		{
			return true;
		}

		m_State->AssertOwner();

		return m_State->bClosed;
	}

} // namespace rox::os
