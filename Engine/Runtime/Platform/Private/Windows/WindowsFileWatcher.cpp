#include <WindowsFileWatcher.h>
#include <Algorithms.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <NumericLimits.hpp>
#include <Utility.hpp>

#include <Array.hpp>
#include <Assertion.h>
#include <Log.h>
#include <UniquePtr.hpp>
#include <Utf8.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <TextPrimitives.hpp>
#include <Vocabulary.hpp>

namespace rox::os
{
	using namespace core;

	namespace
	{
		constexpr usize FILE_NOTIFY_HEADER_BYTES = offsetof(FILE_NOTIFY_INFORMATION, FileName);
		constexpr DWORD WATCH_NOTIFY_FILTER =
			FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;

		Error makeWatchError(const EErrorCode code, const StringView diagnosticID, const StringView message,
			const StringView recoveryHint)
		{
			ASSERT(code != EErrorCode::None, "A file-watch failure requires a non-success error code.");
			ASSERT(!diagnosticID.IsEmpty(), "A file-watch failure requires a diagnostic ID.");
			ASSERT(!message.IsEmpty(), "A file-watch failure requires a message.");

			return MakeError(code, diagnosticID, message, recoveryHint);
		}

		enum class ERootOpenResult : uint8
		{
			Success,
			NotFound,
			AccessDenied,
			NativeFailure
		};

		Error makeRootGoneError()
		{
			return makeWatchError(EErrorCode::NotFound, "ROX-PLATFORM-WATCH-ROOT-GONE",
				"The watched directory root is gone or its native watch handle is no longer usable.",
				"Create a new watcher once the root exists again; this watcher is terminal.");
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
	} // namespace

	class WindowsFileWatcher::State final
	{
	public:
		// Store arena offsets; pointers may be invalidated while parsing.
		struct Record final
		{
		public:
			uint32 PathOffset = 0;
			uint32 PathLength = 0;
			uint32 OldPathOffset = 0;
			uint32 OldPathLength = 0;
			EFileChangeKind Kind = EFileChangeKind::Created;
		};

		State(MemoryResource& resource, const WindowsFileWatcherOptions options) noexcept
			: m_MaximumChanges(options.MaximumChangesPerPoll)
			, m_NativeBufferWords(options.NativeBufferBytes / sizeof(uint32))
			, m_NativeBuffer(resource)
			, m_Records(resource)
			, m_PendingPaths(resource)
			, m_PublishedPaths(resource)
			, m_PublishedChanges(resource)
		{
		}

		~State() noexcept
		{
			ASSERT(IsOwnerThread(), "A file watcher must be destroyed on the thread that created it.");
			// Cancel and drain the native read before members release its buffer, then close.
			if (m_Directory != INVALID_HANDLE_VALUE)
			{
				if (m_bArmed)
				{
					(void)CancelIoEx(m_Directory, &m_Overlapped);
					DWORD ignored = 0;
					(void)GetOverlappedResult(m_Directory, &m_Overlapped, &ignored, TRUE);
					m_bArmed = false;
				}

				const BOOL bClosed = CloseHandle(m_Directory);
				ASSERT(bClosed != FALSE, "A Windows directory watch handle must close successfully.");
				m_Directory = INVALID_HANDLE_VALUE;
			}

			if (m_Event != nullptr)
			{
				const BOOL bClosed = CloseHandle(m_Event);
				ASSERT(bClosed != FALSE, "A Windows watch completion event must close successfully.");
				m_Event = nullptr;
			}

			ASSERT(!m_bArmed, "A destroyed file watcher must leave no pending native read.");
		}

		[[nodiscard]] bool IsOwnerThread() const noexcept
		{
			return GetCurrentThreadId() == m_OwnerThreadId;
		}

		State(const State&) = delete;
		State& operator=(const State&) = delete;
		State(State&&) = delete;
		State& operator=(State&&) = delete;

		[[nodiscard]] bool TryReserveStorage() noexcept
		{
			// Reserve fixed buffers from the supplied resource to keep borrowed views stable.
			// Allow three UTF-8 bytes per UTF-16 code unit.
			const usize bufferBytes = m_NativeBufferWords * sizeof(uint32);
			const usize arenaBytes = bufferBytes * 3 / 2 + 8;

			return m_NativeBuffer.Resize(m_NativeBufferWords) &&
				   m_Records.Reserve(m_MaximumChanges) &&
				   m_PendingPaths.Reserve(arenaBytes) &&
				   m_PublishedPaths.Reserve(arenaBytes) &&
				   m_PublishedChanges.Reserve(m_MaximumChanges);
		}

		// Report root-open and event-allocation failures separately.
		[[nodiscard]] ERootOpenResult TryOpenRoot(const Array<wchar_t>& rootPathUtf16) noexcept
		{
			ASSERT(m_Directory == INVALID_HANDLE_VALUE, "A watcher opens its directory root exactly once.");
			// Allow root rename and removal; the watcher reports root loss.
			m_Directory = CreateFileW(rootPathUtf16.Data(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

			if (m_Directory == INVALID_HANDLE_VALUE)
			{
				switch (GetLastError())
				{
				case ERROR_FILE_NOT_FOUND:
				case ERROR_PATH_NOT_FOUND:
				case ERROR_INVALID_NAME:
				case ERROR_BAD_NETPATH:
				case ERROR_BAD_PATHNAME:
					return ERootOpenResult::NotFound;
				case ERROR_ACCESS_DENIED:
				case ERROR_SHARING_VIOLATION:
					return ERootOpenResult::AccessDenied;
				default:
					return ERootOpenResult::NativeFailure;
				}
			}

			m_Event = CreateEventW(nullptr, TRUE, FALSE, nullptr);

			return m_Event != nullptr ? ERootOpenResult::Success : ERootOpenResult::NativeFailure;
		}

		[[nodiscard]] bool TryArm() noexcept
		{
			ASSERT(m_Directory != INVALID_HANDLE_VALUE, "Arming a watch requires an open directory handle.");
			ASSERT(m_Event != nullptr, "Arming a watch requires a manual-reset completion event.");
			ASSERT(!m_bArmed, "Arming a watch requires that no read is already pending.");
			const BOOL bReset = ResetEvent(m_Event);
			ASSERT(bReset != FALSE, "A manual-reset watch event must reset successfully.");
			m_Overlapped = OVERLAPPED{};
			m_Overlapped.hEvent = m_Event;
			DWORD ignoredImmediateBytes = 0;
			const BOOL bStarted = ReadDirectoryChangesW(m_Directory, m_NativeBuffer.Data(), static_cast<DWORD>(m_NativeBufferWords * sizeof(uint32)), TRUE, WATCH_NOTIFY_FILTER, &ignoredImmediateBytes, &m_Overlapped, nullptr);
			m_bArmed = bStarted != FALSE;

			return m_bArmed;
		}

		[[nodiscard]] Result<FileChangeBatch> Poll() noexcept
		{
			ASSERT(IsOwnerThread(), "A file watcher must be polled on the thread that created it.");

			if (m_bTerminal)
			{
				return Result<FileChangeBatch>::Failure(makeRootGoneError());
			}

			ASSERT(m_bArmed, "A live file watcher always holds a pending native read between polls.");

			bool bRescanRequired = false;
			DWORD transferred = 0;

			if (GetOverlappedResult(m_Directory, &m_Overlapped, &transferred, FALSE) == FALSE)
			{
				const DWORD error = GetLastError();

				if (error == ERROR_IO_INCOMPLETE)
				{
					// Nothing has completed. The read stays armed and the caller receives an empty batch.
					return Result<FileChangeBatch>::Success(FileChangeBatch{ {}, false });
				}

				m_bArmed = false;

				if (error != ERROR_NOTIFY_ENUM_DIR)
				{
					m_bTerminal = true;

					return Result<FileChangeBatch>::Failure(makeRootGoneError());
				}
				// Dropped records and zero-length results request a rescan without discarding this batch.
				reportOverflow();
				bRescanRequired = true;
			}
			else
			{
				m_bArmed = false;

				if (transferred == 0)
				{
					reportOverflow();
					bRescanRequired = true;
				}
				else
				{
					bRescanRequired = parse(transferred);
				}
			}

			publish();
			// Re-arm before returning to minimize gaps. On failure, return this batch and fail the next Poll.
			if (!TryArm())
			{
				m_bTerminal = true;
			}

			return Result<FileChangeBatch>::Success(FileChangeBatch{ m_PublishedChanges.View(), bRescanRequired });
		}

	private:
		void reportOverflow() noexcept
		{
			if (!m_bOverflowReported)
			{
				m_bOverflowReported = true;
				LOG_WARNING("Platform.FileWatcher", "ROX-PLATFORM-WATCH-OVERFLOW: the bounded directory-change buffer overflowed, so this " "batch is incomplete and the watched tree requires a rescan.");
			}
		}

		// Returns true when the batch is incomplete and the caller must rescan.
		[[nodiscard]] bool parse(const DWORD transferred) noexcept
		{
			ASSERT(transferred <= m_NativeBufferWords * sizeof(uint32), "A completed directory read cannot report more bytes than its buffer holds.");
			m_Records.Clear();
			m_PendingPaths.Clear();

			bool bRescanRequired = false;
			bool bHasPendingOldName = false;
			uint32 pendingOldOffset = 0;
			uint32 pendingOldLength = 0;
			const unsigned char* const base = reinterpret_cast<const unsigned char*>(m_NativeBuffer.Data());
			usize offset = 0;

			for (;;)
			{
				if (offset + FILE_NOTIFY_HEADER_BYTES > transferred)
				{
					bRescanRequired = true;
					break;
				}

				FILE_NOTIFY_INFORMATION entry = {};
				CopyBytes(&entry, base + offset, FILE_NOTIFY_HEADER_BYTES);
				const usize nameBytes = entry.FileNameLength;

				if (nameBytes == 0 || (nameBytes % sizeof(wchar_t)) != 0 || offset + FILE_NOTIFY_HEADER_BYTES + nameBytes > transferred)
				{
					bRescanRequired = true;
					break;
				}

				const wchar_t* const nameData = reinterpret_cast<const wchar_t*>(base + offset + FILE_NOTIFY_HEADER_BYTES);
				const int nameLength = static_cast<int>(nameBytes / sizeof(wchar_t));

				uint32 pathOffset = 0;
				uint32 pathLength = 0;

				if (!tryAppendPath(nameData, nameLength, &pathOffset, &pathLength))
				{
					// Stop at the gap; do not pair an earlier rename source with a later destination.
					bRescanRequired = true;
					break;
				}
				else if (entry.Action == FILE_ACTION_RENAMED_NEW_NAME && bHasPendingOldName)
				{
					// Pair adjacent rename halves within one buffer; sort by destination path.
					bHasPendingOldName = false;

					if (!tryAppendRecord(Record{ pathOffset, pathLength, pendingOldOffset, pendingOldLength, EFileChangeKind::Renamed }))
					{
						bRescanRequired = true;
					}
				}
				else
				{
					if (bHasPendingOldName)
					{
						// An unmatched rename source becomes a deletion.
						bHasPendingOldName = false;

						if (!tryAppendRecord(Record{ pendingOldOffset, pendingOldLength, 0, 0, EFileChangeKind::Deleted }))
						{
							bRescanRequired = true;
						}
					}

					if (entry.Action == FILE_ACTION_RENAMED_OLD_NAME)
					{
						bHasPendingOldName = true;
						pendingOldOffset = pathOffset;
						pendingOldLength = pathLength;
					}
					else if (!tryAppendRecord(Record{ pathOffset, pathLength, 0, 0, classify(entry.Action) }))
					{
						bRescanRequired = true;
					}
				}

				if (entry.NextEntryOffset == 0)
				{
					break;
				}

				offset += entry.NextEntryOffset;
			}

			if (bHasPendingOldName && !tryAppendRecord(Record{ pendingOldOffset, pendingOldLength, 0, 0, EFileChangeKind::Deleted }))
			{
				bRescanRequired = true;
			}

			sortAndDeduplicate();

			return bRescanRequired;
		}

		[[nodiscard]] static EFileChangeKind classify(const DWORD action) noexcept
		{
			switch (action)
			{
			case FILE_ACTION_ADDED:
				return EFileChangeKind::Created;
			case FILE_ACTION_REMOVED:
				return EFileChangeKind::Deleted;
			case FILE_ACTION_RENAMED_NEW_NAME:
				// A destination whose source never arrived is an arrival, not half of a rename.
				return EFileChangeKind::Created;
			default:
				return EFileChangeKind::Modified;
			}
		}

		[[nodiscard]] bool tryAppendRecord(const Record& record) noexcept
		{
			if (m_Records.Size() >= m_MaximumChanges)
			{
				reportOverflow();

				return false;
			}

			return m_Records.Add(record);
		}

		[[nodiscard]] bool tryAppendPath(const wchar_t* const nameData, const int nameLength, uint32* const outOffset, uint32* const outLength) noexcept
		{
			const int measured = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, nameData, nameLength, nullptr, 0, nullptr, nullptr);

			if (measured <= 0)
			{
				return false;
			}

			const usize start = m_PendingPaths.Size();
			const usize required = start + static_cast<usize>(measured);

			if (required > m_PendingPaths.Capacity() || required > static_cast<usize>(NumericLimits<uint32>::max()))
			{
				return false;
			}

			if (!m_PendingPaths.Resize(required))
			{
				return false;
			}

			const int converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, nameData, nameLength, m_PendingPaths.Data() + start, measured, nullptr, nullptr);

			if (converted != measured)
			{
				const bool bRestored = m_PendingPaths.Resize(start);
				ASSERT(bRestored, "Shrinking a preallocated path arena cannot fail.");

				return false;
			}
			// Normalize root-relative paths to forward slashes.
			for (usize index = start; index < required; ++index)
			{
				if (m_PendingPaths[index] == static_cast<char>(0x5C))
				{
					m_PendingPaths[index] = static_cast<char>(0x2F);
				}
			}

			*outOffset = static_cast<uint32>(start);
			*outLength = static_cast<uint32>(measured);

			return true;
		}

		void sortAndDeduplicate() noexcept
		{
			const char* const arena = m_PendingPaths.Data();
			const auto isOrderedBefore = [arena](const Record& left, const Record& right) noexcept
				{
					const usize shared = left.PathLength < right.PathLength ? left.PathLength : right.PathLength;
					const int order = shared == 0 ? 0 : CompareBytes(arena + left.PathOffset, arena + right.PathOffset, shared);

					if (order != 0)
					{
						return order < 0;
					}

					if (left.PathLength != right.PathLength)
					{
						return left.PathLength < right.PathLength;
					}

					return static_cast<uint8>(left.Kind) < static_cast<uint8>(right.Kind);
				};
			Sort(m_Records.begin(), m_Records.end(), isOrderedBefore);

			usize uniqueCount = 0;

			for (usize index = 0; index < m_Records.Size(); ++index)
			{
				const Record candidate = m_Records[index];

				if (uniqueCount != 0)
				{
					const Record& previous = m_Records[uniqueCount - 1];
					const bool bSamePath =
						previous.PathLength == candidate.PathLength &&
						(candidate.PathLength == 0 || CompareBytes(arena + previous.PathOffset, arena + candidate.PathOffset, candidate.PathLength) == 0);

					if (bSamePath && previous.Kind == candidate.Kind)
					{
						continue;
					}
				}

				m_Records[uniqueCount] = candidate;
				++uniqueCount;
			}

			const bool bTrimmed = m_Records.Resize(uniqueCount);
			ASSERT(bTrimmed, "Shrinking the parsed record array cannot fail.");
		}

		void publish() noexcept
		{
			// The recycled arena becomes the live one only here, so the batch published by the previous poll
			// stays readable through the entire parse that replaces it.
			Swap(m_PendingPaths, m_PublishedPaths);
			m_PublishedChanges.Clear();
			const char* const arena = m_PublishedPaths.Data();

			for (const Record& record : m_Records)
			{
				FileChange change;
				change.RelativePath = StringView(arena + record.PathOffset, record.PathLength);
				change.OldRelativePath = record.OldPathLength == 0 ? StringView() : StringView(arena + record.OldPathOffset, record.OldPathLength);
				change.Kind = record.Kind;
				const bool bAdded = m_PublishedChanges.Add(change);
				ASSERT(bAdded, "A preallocated published-change buffer must hold every parsed record.");
			}

			m_Records.Clear();
		}

	private:
		// Captured where the watch is created, which is the thread the whole contract is written for. A watch
		// is a handle plus one pending overlapped read and carries no synchronization at all, so a second
		// thread reaching Poll is a programmer error and not a recoverable failure.
		const DWORD m_OwnerThreadId = GetCurrentThreadId();
		uint32 m_MaximumChanges = 0;
		usize m_NativeBufferWords = 0;
		HANDLE m_Directory = INVALID_HANDLE_VALUE;
		HANDLE m_Event = nullptr;
		OVERLAPPED m_Overlapped = {};
		bool m_bArmed = false;
		bool m_bTerminal = false;
		bool m_bOverflowReported = false;
		Array<uint32> m_NativeBuffer;
		Array<Record> m_Records;
		Array<char> m_PendingPaths;
		Array<char> m_PublishedPaths;
		Array<FileChange> m_PublishedChanges;
	};

	WindowsFileWatcher::WindowsFileWatcher(MemoryResource& resource, State* const state) noexcept
		: m_Resource(&resource)
		, m_State(state)
	{
		ASSERT(state != nullptr, "A constructed Windows file watcher requires state.");
	}

	WindowsFileWatcher::~WindowsFileWatcher() noexcept
	{
		if (m_State != nullptr)
		{
			ASSERT(m_Resource != nullptr, "A Windows file watcher holding state must retain its resource.");
			ASSERT(m_State->IsOwnerThread(), "A file watcher must be destroyed on the thread that created it.");
			DestroyObject(*m_Resource, m_State);
			m_State = nullptr;
		}
	}

	WindowsFileWatcher::WindowsFileWatcher(WindowsFileWatcher&& other) noexcept
		: m_Resource(other.m_Resource)
		, m_State(other.m_State)
	{
		ASSERT(m_State == nullptr || m_State->IsOwnerThread(), "A live file watcher must be moved on the thread that created it.");
		other.m_Resource = nullptr;
		other.m_State = nullptr;
	}

	WindowsFileWatcher& WindowsFileWatcher::operator=(WindowsFileWatcher&& other) noexcept
	{
		if (this != &other)
		{
			ASSERT(other.m_State == nullptr || other.m_State->IsOwnerThread(),
				"A live file watcher must be moved on the thread that created it.");

			if (m_State != nullptr)
			{
				ASSERT(m_Resource != nullptr, "A Windows file watcher holding state must retain its resource.");
				ASSERT(m_State->IsOwnerThread(), "A file watcher must be destroyed on the thread that created it.");
				DestroyObject(*m_Resource, m_State);
			}

			m_Resource = other.m_Resource;
			m_State = other.m_State;
			other.m_Resource = nullptr;
			other.m_State = nullptr;
		}

		return *this;
	}

	bool WindowsFileWatcher::IsValid() const noexcept
	{
		return m_State != nullptr;
	}

	Result<WindowsFileWatcher> WindowsFileWatcher::Create(MemoryResource& resource, const StringView rootPathUtf8,
		const WindowsFileWatcherOptions options)
	{
		if (rootPathUtf8.IsEmpty() || containsNull(rootPathUtf8) || !IsValidUtf8(rootPathUtf8) ||
			rootPathUtf8.Size() > static_cast<usize>(NumericLimits<int>::max()))
		{
			return Result<WindowsFileWatcher>::Failure(
				makeWatchError(EErrorCode::InvalidArgument, "ROX-PLATFORM-WATCH-ROOT",
					"The file-watch root is empty, contains a null character, or is not well-formed UTF-8.",
					"Provide a non-empty well-formed UTF-8 directory path."));
		}

		if (options.MaximumChangesPerPoll == 0 || options.MaximumChangesPerPoll > MAXIMUM_FILE_CHANGE_COUNT ||
			options.NativeBufferBytes < sizeof(uint32) || options.NativeBufferBytes > MAXIMUM_FILE_WATCH_BUFFER_BYTES ||
			(options.NativeBufferBytes % sizeof(uint32)) != 0)
		{
			return Result<WindowsFileWatcher>::Failure(
				makeWatchError(EErrorCode::InvalidArgument, "ROX-PLATFORM-WATCH-OPTIONS",
					"The file-watch capacities are outside their supported bounds.",
					"Use one through 65,536 changes and a four-byte-aligned buffer up to one mebibyte."));
		}

		// The root is opened exactly as written once converted to UTF-16, with no long-path prefix added.
		// Prefixing would demand an already absolute and canonical root, which this contract does not require,
		// and it would change nothing about the names ReadDirectoryChangesW reports, which are root relative
		// either way. A root at or beyond MAX_PATH therefore fails to open and is reported as a missing root.
		const int rootLength = static_cast<int>(rootPathUtf8.Size());
		const int measured = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rootPathUtf8.Data(), rootLength, nullptr, 0);

		if (measured <= 0)
		{
			return Result<WindowsFileWatcher>::Failure(
				makeWatchError(EErrorCode::InvalidArgument, "ROX-PLATFORM-WATCH-ROOT",
					"The file-watch root could not be converted to a native wide path.",
					"Provide a non-empty well-formed UTF-8 directory path."));
		}

		Array<wchar_t> rootPathUtf16(resource);

		if (!rootPathUtf16.Resize(static_cast<usize>(measured) + 1))
		{
			return Result<WindowsFileWatcher>::Failure(makeWatchError(
				EErrorCode::ExternalFailure, "ROX-PLATFORM-WATCH-ALLOCATION",
				"Memory allocation for the file-watch root path failed.", "Release bounded service memory and retry creation."));
		}

		const int converted =
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rootPathUtf8.Data(), rootLength, rootPathUtf16.Data(), measured);

		if (converted != measured)
		{
			return Result<WindowsFileWatcher>::Failure(
				makeWatchError(EErrorCode::InvalidArgument, "ROX-PLATFORM-WATCH-ROOT",
					"The file-watch root converted to a different length than it measured.",
					"Provide a non-empty well-formed UTF-8 directory path."));
		}

		rootPathUtf16[static_cast<usize>(measured)] = L'\0';

		State* const state = CreateObject<State>(resource, resource, options);

		if (state == nullptr || !state->TryReserveStorage())
		{
			DestroyObject(resource, state);

			return Result<WindowsFileWatcher>::Failure(makeWatchError(
				EErrorCode::ExternalFailure, "ROX-PLATFORM-WATCH-ALLOCATION", "Memory allocation for the file-watch buffers failed.",
				"Release bounded service memory and retry creation."));
		}

		const ERootOpenResult openResult = state->TryOpenRoot(rootPathUtf16);

		if (openResult != ERootOpenResult::Success)
		{
			DestroyObject(resource, state);

			if (openResult == ERootOpenResult::NotFound)
			{
				return Result<WindowsFileWatcher>::Failure(makeRootGoneError());
			}

			if (openResult == ERootOpenResult::AccessDenied)
			{
				return Result<WindowsFileWatcher>::Failure(
					makeWatchError(EErrorCode::PermissionDenied, "ROX-PLATFORM-WATCH-ROOT-ACCESS",
						"The watched directory root exists but could not be opened for change notification.",
						"Grant list-directory access to the root, or close the owner that holds it exclusively."));
			}

			return Result<WindowsFileWatcher>::Failure(
				makeWatchError(EErrorCode::ExternalFailure, "ROX-PLATFORM-WATCH-OPEN",
					"The native watch handle or its completion event could not be created.",
					"Release bounded operating-system handles and retry creation."));
		}

		if (!state->TryArm())
		{
			DestroyObject(resource, state);

			return Result<WindowsFileWatcher>::Failure(makeWatchError(
				EErrorCode::ExternalFailure, "ROX-PLATFORM-WATCH-ARM", "The initial directory-change read could not be started.",
				"Verify the root is a local directory that supports change notification and retry."));
		}

		LOG_INFO("Platform.FileWatcher", "A recursive Windows directory watch started.");

		return Result<WindowsFileWatcher>::Success(WindowsFileWatcher(resource, state));
	}

	Result<FileChangeBatch> WindowsFileWatcher::Poll()
	{
		ASSERT(m_State != nullptr, "Polling requires a valid Windows file watcher.");

		return m_State->Poll();
	}
} // namespace rox::os
