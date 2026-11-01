#include <WindowsFileTailReader.h>
#include <NumericLimits.hpp>
#include <Vocabulary.hpp>
#include <Types.h>
#include <Utility.hpp>
#include <Array.hpp>
#include <Assertion.h>
#include <UniquePtr.hpp>
#include <Utf8.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace rox::os
{
	using namespace core;

	namespace
	{
		// The same path bound WindowsLogSink applies to the file it opens for append, so the reader can
		// hold the converted path for the lifetime of the tail without a heap allocation of its own.
		constexpr usize MAXIMUM_FILE_TAIL_PATH_CHARACTERS = 1'024;

		// Refusal diagnostics allocate through the caller resource so its lifetime and allocation policy apply.
		Error makeTailError(MemoryResource& resource, const EErrorCode code, const StringView diagnosticID, const StringView message, const StringView recoveryHint)
		{
			ASSERT(code != EErrorCode::None, "A file-tail failure requires a non-success error code.");
			ASSERT(!diagnosticID.IsEmpty(), "A file-tail failure requires a diagnostic ID.");
			ASSERT(!message.IsEmpty(), "A file-tail failure requires a message.");

			return Error{ code, MakeLossyString(diagnosticID, resource), MakeLossyString(message, resource), MakeLossyString(recoveryHint, resource) };
		}

		Error makeTailError(const EErrorCode code, const StringView diagnosticID, const StringView message, const StringView recoveryHint)
		{
			ASSERT(code != EErrorCode::None, "A file-tail failure requires a non-success error code.");
			ASSERT(!diagnosticID.IsEmpty(), "A file-tail failure requires a diagnostic ID.");
			ASSERT(!message.IsEmpty(), "A file-tail failure requires a message.");

			return MakeError(code, diagnosticID, message, recoveryHint);
		}

		Error makeTailGoneError()
		{
			return makeTailError(EErrorCode::ExternalFailure, "ROX-PLATFORM-TAIL-GONE",
				"The tailed file could not be read or shrank behind the reader, so this tail is over.",
				"Create a new reader against the file; this reader is terminal.");
		}

		Error makeTailReplacedError()
		{
			return makeTailError(EErrorCode::NotFound, "ROX-PLATFORM-TAIL-REPLACED",
				"The tailed path no longer names the file this reader holds open, so this tail is over.",
				"Create a new reader against the path to follow the file that replaced it.");
		}

		[[nodiscard]] bool containsNull(const StringView text) noexcept
		{
			for (const char character : text)
			{
				if (character == 0)
				{
					return true;
				}
			}

			return false;
		}

		enum class EOpenResult : uint8
		{
			Success,
			NotFound,
			AccessDenied,
			NativeFailure
		};

		[[nodiscard]] EOpenResult classifyOpenFailure() noexcept
		{
			switch (GetLastError())
			{
			case ERROR_FILE_NOT_FOUND:
			case ERROR_PATH_NOT_FOUND:
			case ERROR_INVALID_NAME:
			case ERROR_BAD_NETPATH:
			case ERROR_BAD_PATHNAME:
				return EOpenResult::NotFound;
			case ERROR_ACCESS_DENIED:
			case ERROR_SHARING_VIOLATION:
				return EOpenResult::AccessDenied;
			default:
				return EOpenResult::NativeFailure;
			}
		}
	} // namespace

	class WindowsFileTailReader::State final
	{
	public:
		State(MemoryResource& resource, const WindowsFileTailReaderOptions options, const HANDLE file) noexcept
			: m_MaximumBytesPerRead(options.MaximumBytesPerRead)
			, m_BackfillBytes(options.BackfillBytes)
			, m_File(file)
			, m_Pending(resource)
			, m_Published(resource)
		{
			ASSERT(m_File != INVALID_HANDLE_VALUE, "A tail reader state adopts an already-open file.");
		}

		~State() noexcept
		{
			ASSERT(IsOwnerThread(), "A file tail reader must be destroyed on the thread that created it.");
			// Cancel and drain I/O before members release the pending buffer, then close the handle.
			closeNative();
			ASSERT(!m_bArmed, "A destroyed file tail reader must leave no pending native read.");
		}

		State(const State&) = delete;
		State& operator=(const State&) = delete;
		State(State&&) = delete;
		State& operator=(State&&) = delete;

		[[nodiscard]] bool IsOwnerThread() const noexcept
		{
			return GetCurrentThreadId() == m_OwnerThreadId;
		}

		[[nodiscard]] bool TryReserveStorage() noexcept
		{
			// Reserve pending and published buffers separately; native I/O must not overwrite the borrowed view.
			return m_Pending.Reserve(m_MaximumBytesPerRead) && m_Published.Reserve(m_MaximumBytesPerRead);
		}

		// Captures the start offset, the completion event, and the file identity. Called once, after the
		// handle is already open, so a missing file has cost no allocation at all.
		[[nodiscard]] bool TryBind(const wchar_t* const pathUtf16, const usize pathLength) noexcept
		{
			ASSERT(pathLength < MAXIMUM_FILE_TAIL_PATH_CHARACTERS, "A bound tail path must fit the reader path buffer.");

			for (usize index = 0; index < pathLength; ++index)
			{
				m_Path[index] = pathUtf16[index];
			}

			m_Path[pathLength] = L'\0';

			BY_HANDLE_FILE_INFORMATION information = {};

			if (GetFileInformationByHandle(m_File, &information) == FALSE)
			{
				return false;
			}

			m_VolumeSerial = information.dwVolumeSerialNumber;
			m_FileIndexHigh = information.nFileIndexHigh;
			m_FileIndexLow = information.nFileIndexLow;

			// Capture the resolved handle name at attachment; later probes compare it without reopening the path.
			const DWORD resolved = GetFinalPathNameByHandleW(m_File, m_ResolvedPath, static_cast<DWORD>(MAXIMUM_FILE_TAIL_PATH_CHARACTERS), FILE_NAME_NORMALIZED);
			m_bResolvedPathValid = resolved != 0 && resolved < MAXIMUM_FILE_TAIL_PATH_CHARACTERS;

			if (!m_bResolvedPathValid)
			{
				// If the resolved name is unavailable, attachment still succeeds; probes use deletion status only.
				m_ResolvedPath[0] = L'\0';
			}

			LARGE_INTEGER size = {};

			if (GetFileSizeEx(m_File, &size) == FALSE || size.QuadPart < 0)
			{
				return false;
			}

			const uint64 endOffset = static_cast<uint64>(size.QuadPart);
			// Start within the backfill window to avoid replaying all earlier log data.
			m_Offset = endOffset > m_BackfillBytes ? endOffset - m_BackfillBytes : 0;
			m_bBeganMidFile = m_Offset != 0;
			// The initial end offset distinguishes backfill from live appends across multiple read chunks.
			m_BackfillEndOffset = endOffset;

			m_Event = CreateEventW(nullptr, TRUE, FALSE, nullptr);

			return m_Event != nullptr;
		}

		[[nodiscard]] bool BeganMidFile() const noexcept
		{
			return m_bBeganMidFile;
		}

		[[nodiscard]] bool IsBacklogged() const noexcept
		{
			return m_bBacklogged;
		}

		[[nodiscard]] Result<Span<const Byte>> Read() noexcept
		{
			ASSERT(IsOwnerThread(), "A file tail reader must be read on the thread that created it.");
			// Apply an already published replacement immediately without further I/O.
			ApplyPublishedIdentity();

			if (m_bTerminal)
			{
				return Result<Span<const Byte>>::Failure(m_bReplaced ? makeTailReplacedError() : makeTailGoneError());
			}

			ASSERT(m_File != INVALID_HANDLE_VALUE, "A live file tail reader always holds an open handle.");

			if (!m_bArmed)
			{
				const EArmResult arm = tryArm();

				if (arm == EArmResult::Terminal)
				{
					return terminalFailure();
				}

				if (arm == EArmResult::Idle)
				{
					return Result<Span<const Byte>>::Success(Span<const Byte>());
				}
			}

			return harvest();
		}

	private:
		enum class EArmResult : uint8
		{
			Idle,
			Armed,
			Terminal
		};

		[[nodiscard]] Result<Span<const Byte>> terminalFailure() noexcept
		{
			// Release terminal native state immediately so a deleted file is not kept alive.
			closeNative();

			return Result<Span<const Byte>>::Failure(m_bReplaced ? makeTailReplacedError() : makeTailGoneError());
		}

		void closeNative() noexcept
		{
			if (m_File != INVALID_HANDLE_VALUE)
			{
				if (m_bArmed)
				{
					(void)CancelIoEx(m_File, &m_Overlapped);
					DWORD ignored = 0;
					(void)GetOverlappedResult(m_File, &m_Overlapped, &ignored, TRUE);
					m_bArmed = false;
				}

				const BOOL bClosed = CloseHandle(m_File);
				ASSERT(bClosed != FALSE, "A Windows file tail handle must close successfully.");
				m_File = INVALID_HANDLE_VALUE;
			}

			if (m_Event != nullptr)
			{
				const BOOL bClosed = CloseHandle(m_Event);
				ASSERT(bClosed != FALSE, "A Windows file tail completion event must close successfully.");
				m_Event = nullptr;
			}
		}

	public:
		// DeletePending detects unlinking; the resolved handle name detects renaming.
		// Failed metadata queries are inconclusive and must not retire a live source.
		void RequestIdentityProbe() noexcept
		{
			ASSERT(IsOwnerThread(), "A file tail probe is requested on the thread that created it.");
			ASSERT(!m_bArmed, "A file tail identity probe must not run while a read is still draining.");

			if (m_bTerminal)
			{
				return;
			}

			m_Identity = queryIdentity();
		}

		[[nodiscard]] EFileTailIdentity ReadIdentity() const noexcept
		{
			return m_Identity;
		}

		// Probing retains the handle. ApplyPublishedIdentity or the next Read applies replacement and releases it.
		void ApplyPublishedIdentity() noexcept
		{
			ASSERT(IsOwnerThread(), "A published identity is applied on the thread that created it.");

			if (m_bTerminal || m_Identity != EFileTailIdentity::Replaced)
			{
				return;
			}

			m_bTerminal = true;
			m_bReplaced = true;
			closeNative();
		}

		[[nodiscard]] bool HasPendingBackfill() const noexcept
		{
			return m_Offset < m_BackfillEndOffset;
		}

		[[nodiscard]] bool IsReadPending() const noexcept
		{
			return m_bArmed;
		}

	private:
		[[nodiscard]] EFileTailIdentity queryIdentity() const noexcept
		{
			if (m_File == INVALID_HANDLE_VALUE)
			{
				return EFileTailIdentity::Inconclusive;
			}

			FILE_STANDARD_INFO standard = {};

			if (GetFileInformationByHandleEx(m_File, FileStandardInfo, &standard, static_cast<DWORD>(sizeof(standard))) == FALSE)
			{
				return EFileTailIdentity::Inconclusive;
			}

			if (standard.DeletePending != FALSE)
			{
				return EFileTailIdentity::Replaced;
			}

			if (!m_bResolvedPathValid)
			{
				// Without a resolved name, deletion status alone cannot establish Same.
				return EFileTailIdentity::Inconclusive;
			}

			wchar_t current[MAXIMUM_FILE_TAIL_PATH_CHARACTERS] = {};
			const DWORD resolved =
				GetFinalPathNameByHandleW(m_File, current, static_cast<DWORD>(MAXIMUM_FILE_TAIL_PATH_CHARACTERS), FILE_NAME_NORMALIZED);

			if (resolved == 0)
			{
				const DWORD error = GetLastError();
				// A handle that resolves to no name at all is a file whose last name is gone.
				return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? EFileTailIdentity::Replaced
					: EFileTailIdentity::Inconclusive;
			}

			if (resolved >= MAXIMUM_FILE_TAIL_PATH_CHARACTERS)
			{
				return EFileTailIdentity::Inconclusive;
			}

			for (DWORD index = 0; index <= resolved; ++index)
			{
				if (current[index] != m_ResolvedPath[index])
				{
					return EFileTailIdentity::Replaced;
				}
			}

			return EFileTailIdentity::Same;
		}

		[[nodiscard]] EArmResult tryArm() noexcept
		{
			ASSERT(!m_bArmed, "Arming a tail read requires that no read is already pending.");
			LARGE_INTEGER size = {};

			if (GetFileSizeEx(m_File, &size) == FALSE || size.QuadPart < 0)
			{
				m_bTerminal = true;

				return EArmResult::Terminal;
			}

			const uint64 endOffset = static_cast<uint64>(size.QuadPart);

			if (endOffset < m_Offset)
			{
				// Truncation is terminal; rewinding would reuse offsets for different content.
				m_bTerminal = true;

				return EArmResult::Terminal;
			}

			const uint64 available = endOffset - m_Offset;

			if (available == 0)
			{
				// Idle; identity checks belong to RequestIdentityProbe.
				m_bBacklogged = false;

				return EArmResult::Idle;
			}

			const usize requested = available > m_MaximumBytesPerRead ? m_MaximumBytesPerRead : static_cast<usize>(available);
			m_bBacklogged = requested < available;
			const bool bSized = m_Pending.Resize(requested);
			ASSERT(bSized, "A preallocated file-tail buffer must accept a bounded read length.");

			const BOOL bReset = ResetEvent(m_Event);
			ASSERT(bReset != FALSE, "A manual-reset file-tail event must reset successfully.");
			m_Overlapped = OVERLAPPED{};
			m_Overlapped.Offset = static_cast<DWORD>(m_Offset & 0xFFFF'FFFFull);
			m_Overlapped.OffsetHigh = static_cast<DWORD>(m_Offset >> 32);
			m_Overlapped.hEvent = m_Event;
			m_RequestedBytes = requested;
			DWORD immediate = 0;

			if (ReadFile(m_File, m_Pending.Data(), static_cast<DWORD>(requested), &immediate, &m_Overlapped) == FALSE && GetLastError() != ERROR_IO_PENDING)
			{
				m_bTerminal = true;

				return EArmResult::Terminal;
			}

			m_bArmed = true;

			return EArmResult::Armed;
		}

		// Pending reads return an empty view without waiting or disarming the read.
		[[nodiscard]] Result<Span<const Byte>> harvest() noexcept
		{
			ASSERT(m_bArmed, "Harvesting a tail read requires a pending native read.");
			DWORD transferred = 0;

			if (GetOverlappedResult(m_File, &m_Overlapped, &transferred, FALSE) == FALSE)
			{
				if (GetLastError() == ERROR_IO_INCOMPLETE)
				{
					return Result<Span<const Byte>>::Success(Span<const Byte>());
				}

				m_bArmed = false;
				m_bTerminal = true;

				return terminalFailure();
			}

			m_bArmed = false;

			if (transferred != m_RequestedBytes)
			{
				// A short read contradicts the queried size; reject the inconsistent byte range.
				m_bTerminal = true;

				return terminalFailure();
			}

			// Publish only after completion to preserve the previous borrowed view during I/O.
			Swap(m_Pending, m_Published);
			const bool bSized = m_Published.Resize(transferred);
			ASSERT(bSized, "A preallocated file-tail buffer must hold its own completed read.");
			m_Offset += transferred;
			// HasPendingBackfill compares the read offset with the end captured at attachment.
			return Result<Span<const Byte>>::Success(m_Published.View());
		}

	private:
		// Captured where the reader is created, which is the thread the whole contract is written for.
		const DWORD m_OwnerThreadId = GetCurrentThreadId();
		usize m_MaximumBytesPerRead = 0;
		usize m_BackfillBytes = 0;
		usize m_RequestedBytes = 0;

		HANDLE m_File = INVALID_HANDLE_VALUE;
		HANDLE m_Event = nullptr;
		OVERLAPPED m_Overlapped = {};

		uint64 m_Offset = 0;
		uint64 m_BackfillEndOffset = 0;

		DWORD m_VolumeSerial = 0;
		DWORD m_FileIndexHigh = 0;
		DWORD m_FileIndexLow = 0;

		bool m_bBeganMidFile = false;
		bool m_bBacklogged = false;
		bool m_bArmed = false;
		bool m_bTerminal = false;
		bool m_bReplaced = false;
		bool m_bResolvedPathValid = false;

		EFileTailIdentity m_Identity = EFileTailIdentity::Unknown;
		wchar_t m_Path[MAXIMUM_FILE_TAIL_PATH_CHARACTERS] = {};
		// The name the handle resolved to at attach. Compared against the name it resolves to now, which
		// is how a rename is told from a live file without opening the path.
		wchar_t m_ResolvedPath[MAXIMUM_FILE_TAIL_PATH_CHARACTERS] = {};

		Array<Byte> m_Pending;
		Array<Byte> m_Published;
	};

	WindowsFileTailReader::WindowsFileTailReader(MemoryResource& resource, State* const state) noexcept
		: m_Resource(&resource)
		, m_State(state)
	{
		ASSERT(state != nullptr, "A constructed Windows file tail reader requires state.");
	}

	WindowsFileTailReader::~WindowsFileTailReader() noexcept
	{
		if (m_State != nullptr)
		{
			ASSERT(m_Resource != nullptr, "A Windows file tail reader holding state must retain its resource.");
			ASSERT(m_State->IsOwnerThread(), "A file tail reader must be destroyed on the thread that created it.");
			DestroyObject(*m_Resource, m_State);
			m_State = nullptr;
		}
	}

	WindowsFileTailReader::WindowsFileTailReader(WindowsFileTailReader&& other) noexcept
		: m_Resource(other.m_Resource)
		, m_State(other.m_State)
	{
		ASSERT(m_State == nullptr || m_State->IsOwnerThread(), "A live file tail reader must be moved on the thread that created it.");
		other.m_Resource = nullptr;
		other.m_State = nullptr;
	}

	WindowsFileTailReader& WindowsFileTailReader::operator=(WindowsFileTailReader&& other) noexcept
	{
		if (this != &other)
		{
			ASSERT(other.m_State == nullptr || other.m_State->IsOwnerThread(),
				"A live file tail reader must be moved on the thread that created it.");

			if (m_State != nullptr)
			{
				ASSERT(m_Resource != nullptr, "A Windows file tail reader holding state must retain its resource.");
				ASSERT(m_State->IsOwnerThread(), "A file tail reader must be destroyed on the thread that created it.");
				DestroyObject(*m_Resource, m_State);
			}

			m_Resource = other.m_Resource;
			m_State = other.m_State;
			other.m_Resource = nullptr;
			other.m_State = nullptr;
		}

		return *this;
	}

	bool WindowsFileTailReader::IsValid() const noexcept
	{
		return m_State != nullptr;
	}

	bool WindowsFileTailReader::BeganMidFile() const noexcept
	{
		ASSERT(m_State != nullptr, "Reading the start mode requires a valid Windows file tail reader.");

		return m_State->BeganMidFile();
	}

	bool WindowsFileTailReader::HasPendingBackfill() const noexcept
	{
		ASSERT(m_State != nullptr, "Reading the backfill state requires a valid Windows file tail reader.");

		return m_State->HasPendingBackfill();
	}

	bool WindowsFileTailReader::IsReadPending() const noexcept
	{
		ASSERT(m_State != nullptr, "Reading the pending state requires a valid Windows file tail reader.");

		return m_State->IsReadPending();
	}

	void WindowsFileTailReader::RequestIdentityProbe()
	{
		ASSERT(m_State != nullptr, "Probing requires a valid Windows file tail reader.");
		m_State->RequestIdentityProbe();
	}

	EFileTailIdentity WindowsFileTailReader::ReadIdentity() const noexcept
	{
		ASSERT(m_State != nullptr, "Reading an identity requires a valid Windows file tail reader.");

		return m_State->ReadIdentity();
	}

	void WindowsFileTailReader::ApplyPublishedIdentity()
	{
		ASSERT(m_State != nullptr, "Applying an identity requires a valid Windows file tail reader.");
		m_State->ApplyPublishedIdentity();
	}

	bool WindowsFileTailReader::IsBacklogged() const noexcept
	{
		ASSERT(m_State != nullptr, "Reading the backlog flag requires a valid Windows file tail reader.");

		return m_State->IsBacklogged();
	}

	EFileTailCreateResult WindowsFileTailReader::TryCreate(MemoryResource& resource, const StringView pathUtf8,
		const WindowsFileTailReaderOptions options,
		WindowsFileTailReader* const outReader) noexcept
	{
		ASSERT(outReader != nullptr, "Creating a Windows file tail reader requires a destination.");
		// Every refusal below happens before the first allocation AND without building a core::Error, whose
		// three owned strings would otherwise be allocated on every polling round by a caller waiting for a
		// writer that has not started yet.
		if (pathUtf8.IsEmpty() || containsNull(pathUtf8) || !IsValidUtf8(pathUtf8) || pathUtf8.Size() > static_cast<usize>(NumericLimits<int>::max()))
		{
			return EFileTailCreateResult::InvalidPath;
		}

		if (options.MaximumBytesPerRead == 0 || options.MaximumBytesPerRead > MAXIMUM_FILE_TAIL_READ_BYTES || options.BackfillBytes > MAXIMUM_FILE_TAIL_READ_BYTES)
		{
			return EFileTailCreateResult::InvalidOptions;
		}

		StaticArray<wchar_t, MAXIMUM_FILE_TAIL_PATH_CHARACTERS> pathUtf16 = {};
		const int measured = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pathUtf8.Data(), static_cast<int>(pathUtf8.Size()), pathUtf16.data(), static_cast<int>(pathUtf16.size() - 1));

		if (measured <= 0)
		{
			return EFileTailCreateResult::InvalidPath;
		}

		pathUtf16[static_cast<usize>(measured)] = L'\0';

		// FILE_SHARE_WRITE is the whole point: the file is open for append in another process and this
		// reader must not stop it writing. FILE_SHARE_DELETE is deliberate for the same reason the
		// directory watcher takes it -- losing the file is a case this reader reports, not one it prevents.
		// FILE_FLAG_OVERLAPPED is what makes Read a nonblocking call rather than a promise.
		const HANDLE file = CreateFileW(pathUtf16.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);

		if (file == INVALID_HANDLE_VALUE)
		{
			switch (classifyOpenFailure())
			{
			case EOpenResult::NotFound:
				return EFileTailCreateResult::Missing;
			case EOpenResult::AccessDenied:
				return EFileTailCreateResult::AccessDenied;
			case EOpenResult::Success:
			case EOpenResult::NativeFailure:
				break;

			default:
			{
				break;
			}
			}

			return EFileTailCreateResult::NativeFailure;
		}

		State* const state = CreateObject<State>(resource, resource, options, file);

		if (state == nullptr)
		{
			const BOOL bClosed = CloseHandle(file);
			ASSERT(bClosed != FALSE, "An unadopted Windows file tail handle must close successfully.");

			return EFileTailCreateResult::Allocation;
		}

		if (!state->TryReserveStorage())
		{
			DestroyObject(resource, state);

			return EFileTailCreateResult::Allocation;
		}

		if (!state->TryBind(pathUtf16.data(), static_cast<usize>(measured)))
		{
			DestroyObject(resource, state);

			return EFileTailCreateResult::NativeFailure;
		}

		*outReader = WindowsFileTailReader(resource, state);

		return EFileTailCreateResult::Created;
	}

	StringView GetFileTailCreateDiagnosticID(const EFileTailCreateResult result) noexcept
	{
		switch (result)
		{
		case EFileTailCreateResult::Created:
			return "";
		case EFileTailCreateResult::Missing:
			return "ROX-PLATFORM-TAIL-MISSING";
		case EFileTailCreateResult::AccessDenied:
			return "ROX-PLATFORM-TAIL-ACCESS";
		case EFileTailCreateResult::InvalidPath:
			return "ROX-PLATFORM-TAIL-PATH";
		case EFileTailCreateResult::InvalidOptions:
			return "ROX-PLATFORM-TAIL-OPTIONS";
		case EFileTailCreateResult::NativeFailure:
			return "ROX-PLATFORM-TAIL-OPEN";
		case EFileTailCreateResult::Allocation:
			return "ROX-PLATFORM-TAIL-ALLOCATION";

		default:
		{
			break;
		}
		}

		ASSERT(false, "Every file-tail creation result must carry a stable diagnostic identity.");

		return "ROX-PLATFORM-TAIL-OPEN";
	}

	Result<WindowsFileTailReader> WindowsFileTailReader::Create(MemoryResource& resource, const StringView pathUtf8,
		const WindowsFileTailReaderOptions options)
	{
		WindowsFileTailReader reader;
		const EFileTailCreateResult created = TryCreate(resource, pathUtf8, options, &reader);

		switch (created)
		{
		case EFileTailCreateResult::Created:
			return Result<WindowsFileTailReader>::Success(Move(reader));
		case EFileTailCreateResult::Missing:
			// Absent is not a failed tail: a caller that expects the writer to start later retries.
			return Result<WindowsFileTailReader>::Failure(
				makeTailError(resource, EErrorCode::NotFound, "ROX-PLATFORM-TAIL-MISSING", "The file to tail does not exist yet.",
					"Retry creation once the writing process has created the file."));
		case EFileTailCreateResult::AccessDenied:
			return Result<WindowsFileTailReader>::Failure(
				makeTailError(resource, EErrorCode::PermissionDenied, "ROX-PLATFORM-TAIL-ACCESS",
					"The file exists but could not be opened for shared reading.",
					"Grant read access, or close the owner that holds it without read sharing."));
		case EFileTailCreateResult::InvalidPath:
			return Result<WindowsFileTailReader>::Failure(
				makeTailError(resource, EErrorCode::InvalidArgument, "ROX-PLATFORM-TAIL-PATH",
					"The file-tail path is empty, over-long, contains a null character, or is not UTF-8.",
					"Provide a well-formed UTF-8 file path under one thousand twenty-three wide characters."));
		case EFileTailCreateResult::InvalidOptions:
			return Result<WindowsFileTailReader>::Failure(
				makeTailError(resource, EErrorCode::InvalidArgument, "ROX-PLATFORM-TAIL-OPTIONS",
					"The file-tail capacities are outside their supported bounds.",
					"Use one through one mebibyte per read and a backfill window no larger than that."));
		case EFileTailCreateResult::Allocation:
			return Result<WindowsFileTailReader>::Failure(
				makeTailError(resource, EErrorCode::ExternalFailure, "ROX-PLATFORM-TAIL-ALLOCATION",
					"Memory allocation for the file-tail reader failed.", "Release bounded service memory and retry creation."));
		case EFileTailCreateResult::NativeFailure:
			break;

		default:
		{
			break;
		}
		}

		return Result<WindowsFileTailReader>::Failure(
			makeTailError(resource, EErrorCode::ExternalFailure, "ROX-PLATFORM-TAIL-OPEN",
				"The native file handle, identity, or completion event could not be established.",
				"Release bounded operating-system handles and retry creation."));
	}

	Result<Span<const Byte>> WindowsFileTailReader::Read()
	{
		ASSERT(m_State != nullptr, "Reading requires a valid Windows file tail reader.");

		return m_State->Read();
	}
} // namespace rox::os
