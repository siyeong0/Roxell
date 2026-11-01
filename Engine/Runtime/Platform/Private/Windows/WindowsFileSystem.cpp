#include <WindowsFileSystem.h>
#include <Algorithms.hpp>
#include <Synchronization.hpp>
#include <TextPrimitives.hpp>
#include <Vocabulary.hpp>
#include <SystemError.hpp>
#include <StringView.hpp>
#include <Span.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Utility.hpp>
#include "WindowsPath.h"

#include <Assertion.h>
#include <Log.h>
#include <Utf8.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <ScopeExit.hpp>

namespace rox::os
{
	using namespace core;

	namespace
	{
		Error failure(EErrorCode code, StringView message)
		{
			return MakeError(code, "ROX-PLATFORM-DIRECTORY-INVENTORY", message);
		}

		Error pathAllocationError() noexcept
		{
			return MakeError(EErrorCode::ExternalFailure, "ROX-PLATFORM-PATH-ALLOCATION",
				"The filesystem operation could not allocate temporary storage.",
				"Release memory and retry the filesystem operation.");
		}

		class DirectorySearch final
		{
		public:
			explicit DirectorySearch(HANDLE handle) noexcept
				: m_Handle(handle)
			{
			}

			DirectorySearch(const DirectorySearch&) = delete;
			DirectorySearch& operator=(const DirectorySearch&) = delete;

			~DirectorySearch()
			{
				if (m_Handle != INVALID_HANDLE_VALUE)
				{
					const BOOL bClosed = FindClose(m_Handle);
					ASSERT(bClosed, "Directory search must close.");
				}
			}

			[[nodiscard]] HANDLE GetHandle() const noexcept
			{
				return m_Handle;
			}

		private:
			HANDLE m_Handle;
		};

		Result<void> scanRecursive(const WindowsPath& directory, StringView prefix, Array<String>& result, uint32& entries, uint32 maximum, uint32 depth, CancellationToken cancellation)
		{
			MemoryResource& memory = result.GetResource();

			if (cancellation.IsCancellationRequested())
			{
				return Result<void>::Failure(failure(EErrorCode::Cancelled, "Directory inventory was cancelled."));
			}

			WindowsPath pattern(memory);

			if (!pattern.Assign(directory.View()))
			{
				return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
			}
			if (!pattern.AppendComponent(L"*"))
			{
				return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
			}
			WIN32_FIND_DATAW found{};
			DirectorySearch search(FindFirstFileW(pattern.CStr(), &found));

			if (search.GetHandle() == INVALID_HANDLE_VALUE)
			{
				if (GetLastError() == ERROR_FILE_NOT_FOUND)
				{
					return Result<void>::Success();
				}

				return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Directory inventory could not be opened."));
			}

			do
			{
				if (cancellation.IsCancellationRequested())
				{
					return Result<void>::Failure(failure(EErrorCode::Cancelled, "Directory inventory was cancelled."));
				}

				const WideStringView name(found.cFileName);

				if (name == L"." || name == L"..")
				{
					continue;
				}

				if (++entries > maximum || (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
				{
					return Result<void>::Failure(failure(EErrorCode::Conflict, "Directory inventory exceeds its bound or traverses a reparse point."));
				}

				if (name.empty() || name.back() == L'.' || name.back() == L' ')
				{
					return Result<void>::Failure(failure(EErrorCode::Conflict, "Directory inventory contains a path alias."));
				}

				const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);

				if (length <= 0)
				{
					return Result<void>::Failure(failure(EErrorCode::InvalidArgument, "Directory entry is not valid Unicode."));
				}

				Array<char> utf8(memory);

				if (!utf8.Resize(static_cast<usize>(length)))
				{
					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Directory entry allocation failed."));
				}

				if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), utf8.Data(), length, nullptr, nullptr) != length)
				{
					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Directory entry conversion failed."));
				}

				String relative(memory);

				if (!relative.Assign(prefix) || !relative.Append({ utf8.Data(), utf8.Size() }))
				{
					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Directory path allocation failed."));
				}

				if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					if (depth == 0)
					{
						return Result<void>::Failure(failure(EErrorCode::Conflict, "Directory inventory exceeds its depth bound."));
					}

					WindowsPath child(memory);
					if (!child.Assign(directory.View()))
					{
						return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
					}
					if (!child.AppendComponent(name))
					{
						return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
					}

					if (!relative.Append("/"))
					{
						return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Directory path allocation failed."));
					}

					Result<void> nested = scanRecursive(child, relative.View(), result, entries, maximum, depth - 1, cancellation);

					if (!nested)
					{
						return nested;
					}
				}
				else if (!result.Add(Move(relative)))
				{
					return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
				}
			} while (FindNextFileW(search.GetHandle(), &found));

			if (GetLastError() != ERROR_NO_MORE_FILES)
			{
				return Result<void>::Failure(failure(EErrorCode::ExternalFailure, "Directory inventory ended unexpectedly."));
			}

			return Result<void>::Success();
		}

		Atomic<uint64> s_temporaryFileCounter = 0;

		Error makeError(EErrorCode code, StringView diagnosticId, StringView message, StringView recoveryHint)
		{
			ASSERT(code != EErrorCode::None, "A filesystem failure must use a non-success error code.");
			ASSERT(!diagnosticId.IsEmpty(), "A filesystem failure must provide a diagnostic ID.");
			ASSERT(!message.IsEmpty(), "A filesystem failure must provide a message.");

			return MakeError(code, diagnosticId, message, recoveryHint);
		}

		Result<void> validateRootPath(const StringView rootPathUtf8)
		{
			if (rootPathUtf8.empty())
			{
				return Result<void>::Failure(makeError(EErrorCode::InvalidArgument, "ROX-PLATFORM-ROOT-EMPTY",
					"The filesystem capability root is empty.",
					"Provide an absolute or relative UTF-8 root directory."));
			}

			if (rootPathUtf8.find('\0') != StringView::npos)
			{
				return Result<void>::Failure(makeError(EErrorCode::InvalidArgument, "ROX-PLATFORM-ROOT-INVALID",
					"The filesystem capability root contains a null character.",
					"Remove embedded null characters from the UTF-8 root path."));
			}

			const StringView rootPath(rootPathUtf8.data(), rootPathUtf8.size());

			if (!IsValidUtf8(rootPath))
			{
				return Result<void>::Failure(makeError(
					EErrorCode::InvalidArgument, "ROX-PLATFORM-ROOT-UTF8", "The filesystem capability root is not well-formed UTF-8.",
					"Encode the root path as valid UTF-8 before creating the filesystem capability."));
			}

			return Result<void>::Success();
		}

		// Win32 resolves device names inside any directory, ignoring extensions and trailing spaces.
		// Reject them explicitly; root containment alone does not exclude device access.
		[[nodiscard]] bool isReservedWindowsDeviceName(const WideStringView segment) noexcept
		{
			static constexpr WideStringView RESERVED_NAMES[] = {
				L"CON",  L"PRN",  L"AUX",  L"NUL",  L"CONIN$",    L"CONOUT$",   L"COM1",      L"COM2",      L"COM3",      L"COM4",
				L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",      L"LPT1",      L"LPT2",      L"LPT3",      L"LPT4",      L"LPT5",
				L"LPT6", L"LPT7", L"LPT8", L"LPT9", L"COM\u00b9", L"COM\u00b2", L"COM\u00b3", L"LPT\u00b9", L"LPT\u00b2", L"LPT\u00b3" };

			usize end = segment.find(L'.');

			if (end == WideStringView::npos)
			{
				end = segment.size();
			}
			while (end != 0 && segment[end - 1] == L' ')
			{
				--end;
			}

			const WideStringView base = segment.substr(0, end);

			for (const WideStringView reserved : RESERVED_NAMES)
			{
				if (base.size() != reserved.size())
				{
					continue;
				}

				bool bMatches = true;

				for (usize index = 0; index < base.size(); ++index)
				{
					wchar_t character = base[index];

					if (character >= L'a' && character <= L'z')
					{
						character = static_cast<wchar_t>(character - L'a' + L'A');
					}

					if (character != reserved[index])
					{
						bMatches = false;
						break;
					}
				}

				if (bMatches)
				{
					return true;
				}
			}

			return false;
		}

		Error makeWindowsFileSystemError(StringView operation, const SystemErrorCode& error) noexcept
		{
			constexpr usize MAX_OPERATION_BYTES = 34;
			ASSERT(!operation.empty() && operation.size() <= MAX_OPERATION_BYTES, "Filesystem operation labels must contain between 1 and 34 bytes.");
			ASSERT(IsValidUtf8(StringView(operation)), "Filesystem operation labels must be valid UTF-8.");
			ASSERT(error, "Filesystem error conversion requires a failing error code.");
			EErrorCode code = EErrorCode::ExternalFailure;

			if (error == EErrorCondition::no_such_file_or_directory)
			{
				code = EErrorCode::NotFound;
			}
			else if (error == EErrorCondition::permission_denied)
			{
				code = EErrorCode::PermissionDenied;
			}
			else if (error == EErrorCondition::file_exists)
			{
				code = EErrorCode::Conflict;
			}
			else if (error == EErrorCondition::invalid_argument)
			{
				code = EErrorCode::InvalidArgument;
			}

			char codeText[16] = {};
			const ToCharsResult converted = ToChars(codeText, codeText + sizeof(codeText), error.value());
			ASSERT(converted.ec == EErrorCondition(), "An operating-system error code must render into 16 bytes.");

			constexpr char FAILURE_TEXT[] = " failed with operating-system error code ";
			char message[MAX_OPERATION_BYTES + sizeof(FAILURE_TEXT) + sizeof(codeText)] = {};
			const usize prefixSize = operation.size() + sizeof(FAILURE_TEXT) - 1;
			const usize codeSize = static_cast<usize>(converted.ptr - codeText);
			CopyBytes(message, operation.data(), operation.size());
			CopyBytes(message + operation.size(), FAILURE_TEXT, sizeof(FAILURE_TEXT) - 1);
			CopyBytes(message + prefixSize, codeText, codeSize);
			message[prefixSize + codeSize] = '.';

			return makeError(code, "ROX-PLATFORM-FILESYSTEM", StringView(message, prefixSize + codeSize + 1), "Check the root-relative path and filesystem permissions.");
		}

		void removeStagingFileBestEffort(const WindowsPath& staging) noexcept
		{
			if (DeleteFileW(staging.CStr()) == FALSE && GetLastError() != ERROR_FILE_NOT_FOUND)
			{
				LOG_WARNING("Platform.FileSystem", "An atomic-write staging file could not be removed.");
			}
		}

		class WindowsFileSystem final : public IFileSystem
		{
		public:
			explicit WindowsFileSystem(WindowsPath root) noexcept
				: m_Root(Move(root))
			{
				ASSERT(!m_Root.IsEmpty() && IsAbsolutePath(m_Root.View()), "WindowsFileSystem requires a non-empty absolute root path.");
			}

			~WindowsFileSystem() noexcept override
			{
				if (m_bMounted)
				{
					LOG_INFO("Platform.FileSystem", "A Windows filesystem capability root was unmounted.");
				}
			}

			Result<void> Initialize(const bool bCreateRoot)
			{
				ASSERT(!m_bMounted, "A Windows filesystem capability can be initialized only once.");
				SystemErrorCode error;

				if (bCreateRoot)
				{
					Result<void> createdDirectories = CreatePathDirectories(m_Root, error);

					if (!createdDirectories)
					{
						return Result<void>::Failure(createdDirectories.GetError());
					}
				}
				else
				{
					const unsigned long status = PathAttributes(m_Root, error);

					if (error)
					{
						return Result<void>::Failure(makeWindowsFileSystemError("Opening the filesystem root", error));
					}

					if (status == INVALID_FILE_ATTRIBUTES)
					{
						return Result<void>::Failure(makeError(EErrorCode::NotFound, "ROX-PLATFORM-ROOT-MISSING",
							"The input filesystem root does not exist.",
							"Provide an existing directory."));
					}

					if ((status & FILE_ATTRIBUTE_DIRECTORY) == 0)
					{
						return Result<void>::Failure(makeError(EErrorCode::InvalidArgument, "ROX-PLATFORM-ROOT-NOT-DIRECTORY",
							"The input filesystem root is not a directory.",
							"Provide an existing directory."));
					}
				}

				if (error)
				{
					return Result<void>::Failure(makeWindowsFileSystemError("Creating the filesystem root", error));
				}

				Result<WindowsPath> canonicalRootResult = WeaklyCanonicalPath(m_Root, error);

				if (!canonicalRootResult)
				{
					return Result<void>::Failure(canonicalRootResult.GetError());
				}

				WindowsPath canonicalRoot = Move(canonicalRootResult).TakeValue();

				if (error)
				{
					return Result<void>::Failure(makeWindowsFileSystemError("Canonicalizing the filesystem root", error));
				}

				m_Root = Move(canonicalRoot);
				m_bMounted = true;
				ASSERT(!m_Root.IsEmpty() && IsAbsolutePath(m_Root.View()), "An initialized Windows filesystem capability must retain a canonical absolute root.");
				return Result<void>::Success();
			}

			Result<bool> Exists(StringView relativePath) const override
			{
				Result<WindowsPath> path = resolve(relativePath);

				if (!path)
				{
					return Result<bool>::Failure(path.GetError());
				}

				SystemErrorCode error;
				const unsigned long attributes = PathAttributes(path.Value(), error);
				const bool bExists = attributes != INVALID_FILE_ATTRIBUTES;

				if (IsMissingPathError(error))
				{
					error.clear();
				}

				if (error)
				{
					return Result<bool>::Failure(makeWindowsFileSystemError("Checking file existence", error));
				}

				return Result<bool>::Success(bExists);
			}

			Result<Array<Byte>> ReadFile(StringView relativePath, uint64 maximumBytes, MemoryResource& resource) const override
			{
				if (maximumBytes == 0)
				{
					return Result<Array<Byte>>::Failure(
						makeError(EErrorCode::InvalidArgument, "ROX-PLATFORM-FILE-LIMIT-ZERO",
							"The maximum file size must be greater than zero.", "Provide a bounded non-zero maximum byte count."));
				}

				Result<WindowsPath> path = resolve(relativePath);

				if (!path)
				{
					return Result<Array<Byte>>::Failure(path.GetError());
				}

				SystemErrorCode error;
				const uintmax size = PathFileSize(path.Value(), error);

				if (error)
				{
					return Result<Array<Byte>>::Failure(makeWindowsFileSystemError("Reading file size", error));
				}

				if (size > maximumBytes || size > static_cast<uintmax>(NumericLimits<usize>::max()) ||
					size > static_cast<uintmax>(NumericLimits<int64>::max()))
				{
					return Result<Array<Byte>>::Failure(
						makeError(EErrorCode::Unsupported, "ROX-PLATFORM-FILE-LIMIT",
							"The file exceeds the caller's maximum byte count or readable buffer range.",
							"Raise the explicit limit only after validating the input source."));
				}

				const HANDLE file = CreateFileW(path.Value().CStr(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
					OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

				if (file == INVALID_HANDLE_VALUE)
				{
					return Result<Array<Byte>>::Failure(
						makeError(EErrorCode::ExternalFailure, "ROX-PLATFORM-FILE-OPEN", "The file could not be opened for reading.",
							"Check whether another process or filesystem policy denied access."));
				}

				ScopeExit closeFile([file]() noexcept
				{
					CloseHandle(file);
				});
				const usize byteCount = static_cast<usize>(size);
				Array<Byte> bytes(resource);

				if (!bytes.ResizeUninitialized(byteCount))
				{
					return Result<Array<Byte>>::Failure(makeError(
						EErrorCode::ExternalFailure, "ROX-PLATFORM-FILE-ALLOCATION", "The file read buffer could not be allocated.",
						"Free memory, lower the bounded input size, or retry with a larger memory budget."));
				}

				ASSERT(bytes.Size() == byteCount, "A successful resource-aware file read allocation must match the file size.");

				usize offset = 0;

				while (offset < bytes.Size())
				{
					const DWORD request =
						static_cast<DWORD>(Min<usize>(bytes.Size() - offset, NumericLimits<DWORD>::max()));
					DWORD received = 0;

					if (!::ReadFile(file, bytes.Data() + offset, request, &received, nullptr) || received != request)
					{
						return Result<Array<Byte>>::Failure(
							makeError(EErrorCode::ExternalFailure, "ROX-PLATFORM-FILE-SHORT-READ",
								"The file changed or became unreadable during the bounded read.",
								"Retry the operation from a stable input snapshot."));
					}

					offset += received;
				}

				return Result<Array<Byte>>::Success(Move(bytes));
			}

			Result<EAtomicWriteOutcome> WriteFileAtomically(StringView relativePath, Span<const Byte> data, EAtomicWritePolicy policy) override
			{
				Result<WindowsPath> destination = resolve(relativePath);

				if (!destination)
				{
					return Result<EAtomicWriteOutcome>::Failure(destination.GetError());
				}

				ASSERT(policy == EAtomicWritePolicy::CreateNew || policy == EAtomicWritePolicy::ReplaceExisting, "Atomic write policy value is out of range.");

				SystemErrorCode directoryError;
				Result<WindowsPath> destinationParent = destination.Value().Parent();

				if (!destinationParent)
				{
					return Result<EAtomicWriteOutcome>::Failure(destinationParent.GetError());
				}

				Result<void> createdDirectories = CreatePathDirectories(destinationParent.Value(), directoryError);

				if (!createdDirectories)
				{
					return Result<EAtomicWriteOutcome>::Failure(createdDirectories.GetError());
				}

				if (directoryError)
				{
					return Result<EAtomicWriteOutcome>::Failure(makeWindowsFileSystemError("Creating the destination directory", directoryError));
				}

				const uint64 temporaryId = s_temporaryFileCounter.fetch_add(1, MemoryOrderRelaxed);
				WindowsPath staging(m_Root.Resource());
				if (!staging.Assign(destination.Value().View()))
				{
					return Result<EAtomicWriteOutcome>::Failure(pathAllocationError());
				}
				wchar_t suffix[64]{};
				const int suffixSize = swprintf_s(suffix, L".roxell-tmp-%lu-%llu", GetCurrentProcessId(), temporaryId);
				ASSERT(suffixSize > 0, "The bounded staging suffix must contain both numeric identifiers.");
				if (!staging.Append({ suffix, static_cast<usize>(suffixSize) }))
				{
					return Result<EAtomicWriteOutcome>::Failure(pathAllocationError());
				}

				HANDLE file = CreateFileW(staging.CStr(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);

				if (file == INVALID_HANDLE_VALUE)
				{
					return Result<EAtomicWriteOutcome>::Failure(
						makeWindowsFileSystemError("Creating the staging file", SystemErrorCode(static_cast<int>(GetLastError()),
							SystemCategory())));
				}

				bool bWriteSucceeded = true;
				DWORD writeError = ERROR_SUCCESS;
				usize offset = 0;

				while (offset < data.size())
				{
					const usize remaining = data.size() - offset;
					const DWORD chunkSize = static_cast<DWORD>((Min)(remaining, static_cast<usize>(NumericLimits<DWORD>::max())));
					ASSERT(chunkSize != 0, "Atomic file writes must make forward progress.");
					DWORD bytesWritten = 0;

					if (WriteFile(file, data.data() + offset, chunkSize, &bytesWritten, nullptr) == FALSE)
					{
						writeError = GetLastError();

						if (writeError == ERROR_SUCCESS)
						{
							writeError = ERROR_WRITE_FAULT;
						}

						bWriteSucceeded = false;
						break;
					}

					if (bytesWritten != chunkSize)
					{
						writeError = ERROR_WRITE_FAULT;
						bWriteSucceeded = false;
						break;
					}

					offset += bytesWritten;
				}

				if (bWriteSucceeded && FlushFileBuffers(file) == FALSE)
				{
					writeError = GetLastError();

					if (writeError == ERROR_SUCCESS)
					{
						writeError = ERROR_WRITE_FAULT;
					}

					bWriteSucceeded = false;
				}

				if (CloseHandle(file) == FALSE && bWriteSucceeded)
				{
					writeError = GetLastError();

					if (writeError == ERROR_SUCCESS)
					{
						writeError = ERROR_INVALID_HANDLE;
					}

					bWriteSucceeded = false;
				}

				if (!bWriteSucceeded)
				{
					removeStagingFileBestEffort(staging);

					return Result<EAtomicWriteOutcome>::Failure(makeWindowsFileSystemError(
						"Writing the staging file", SystemErrorCode(static_cast<int>(writeError), SystemCategory())));
				}

				DWORD moveFlags = MOVEFILE_WRITE_THROUGH;

				if (policy == EAtomicWritePolicy::ReplaceExisting)
				{
					moveFlags |= MOVEFILE_REPLACE_EXISTING;
				}

				DWORD moveError = ERROR_SUCCESS;
				constexpr uint32 MAXIMUM_PUBLISH_ATTEMPTS = 5;

				for (uint32 attempt = 0; attempt < MAXIMUM_PUBLISH_ATTEMPTS; ++attempt)
				{
					if (MoveFileExW(staging.CStr(), destination.Value().CStr(), moveFlags) != FALSE)
					{
						return Result<EAtomicWriteOutcome>::Success(EAtomicWriteOutcome::Published);
					}

					moveError = GetLastError();

					if ((moveError != ERROR_ACCESS_DENIED && moveError != ERROR_SHARING_VIOLATION) ||
						attempt + 1 == MAXIMUM_PUBLISH_ATTEMPTS)
					{
						break;
					}
					// A short external handle can outlive CloseHandle; retain the same flushed staging file.
					Sleep(5);
				}

				removeStagingFileBestEffort(staging);

				if (policy == EAtomicWritePolicy::CreateNew && (moveError == ERROR_FILE_EXISTS || moveError == ERROR_ALREADY_EXISTS))
				{
					return Result<EAtomicWriteOutcome>::Success(EAtomicWriteOutcome::AlreadyExists);
				}

				return Result<EAtomicWriteOutcome>::Failure(makeWindowsFileSystemError("Publishing the staging file", SystemErrorCode(static_cast<int>(moveError), SystemCategory())));
			}

			Result<void> RemoveFile(StringView relativePath) override
			{
				Result<WindowsPath> path = resolve(relativePath);

				if (!path)
				{
					return Result<void>::Failure(path.GetError());
				}

				if (DeleteFileW(path.Value().CStr()) == FALSE)
				{
					const DWORD removeError = GetLastError();

					if (removeError != ERROR_FILE_NOT_FOUND && removeError != ERROR_PATH_NOT_FOUND)
					{
						return Result<void>::Failure(makeWindowsFileSystemError(
							"Removing the file", SystemErrorCode(static_cast<int>(removeError), SystemCategory())));
					}
				}

				return Result<void>::Success();
			}

		protected:
			Result<uintptr> tryAcquireExclusiveFileLock(StringView relativePath) override
			{
				Result<WindowsPath> path = resolve(relativePath);

				if (!path)
				{
					return Result<uintptr>::Failure(path.GetError());
				}

				SystemErrorCode directoryError;
				Result<WindowsPath> lockParent = path.Value().Parent();

				if (!lockParent)
				{
					return Result<uintptr>::Failure(lockParent.GetError());
				}

				Result<void> createdDirectories = CreatePathDirectories(lockParent.Value(), directoryError);

				if (!createdDirectories)
				{
					return Result<uintptr>::Failure(createdDirectories.GetError());
				}

				if (directoryError)
				{
					return Result<uintptr>::Failure(makeWindowsFileSystemError("Creating the file-lock directory", directoryError));
				}

				HANDLE handle = CreateFileW(path.Value().CStr(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);

				if (handle == INVALID_HANDLE_VALUE)
				{
					const DWORD error = GetLastError();

					if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION)
					{
						return Result<uintptr>::Failure(
							makeError(EErrorCode::Conflict, "ROX-PLATFORM-FILE-LOCK-CONFLICT",
								"The file is already locked by another owner.",
								"Retry after the current owner releases the lock."));
					}

					return Result<uintptr>::Failure(
						makeWindowsFileSystemError("Acquiring the exclusive file lock",
							SystemErrorCode(static_cast<int>(error), SystemCategory())));
				}

				const uintptr token = reinterpret_cast<uintptr>(handle);
				ASSERT(token != 0, "A successful Windows file lock must produce a valid opaque token.");

				return Result<uintptr>::Success(token);
			}

			void releaseExclusiveFileLock(const uintptr token) noexcept override
			{
				ASSERT(token != 0, "Releasing an exclusive file lock requires a valid opaque token.");
				HANDLE handle = reinterpret_cast<HANDLE>(token);
				ASSERT(handle != INVALID_HANDLE_VALUE, "Releasing an exclusive file lock requires a valid Windows handle.");
				const BOOL bClosed = CloseHandle(handle);
				ASSERT(bClosed != FALSE, "A Windows exclusive file lock handle must close successfully.");
			}

		private:
			Result<WindowsPath> resolve(StringView relativePath) const
			{
				if (relativePath.empty() || relativePath.find('\0') != StringView::npos)
				{
					return Result<WindowsPath>::Failure(makeError(EErrorCode::InvalidArgument, "ROX-PLATFORM-PATH-INVALID",
						"The root-relative path is empty or contains a null character.",
						"Provide a non-empty UTF-8 path relative to the filesystem root."));
				}

				const StringView relativePathUtf8(relativePath.data(), relativePath.size());

				if (!IsValidUtf8(relativePathUtf8))
				{
					return Result<WindowsPath>::Failure(makeError(
						EErrorCode::InvalidArgument, "ROX-PLATFORM-PATH-UTF8", "The root-relative path is not well-formed UTF-8.",
						"Encode the path as valid UTF-8 before using the filesystem capability."));
				}

				Result<WindowsPath> relativeResult = PathFromUtf8(relativePath, m_Root.Resource());

				if (!relativeResult)
				{
					return Result<WindowsPath>::Failure(relativeResult.GetError());
				}

				const WindowsPath relative = Move(relativeResult).TakeValue();

				if (RootNameSize(relative.View()) != 0 || RootPathSize(relative.View()) != 0)
				{
					return Result<WindowsPath>::Failure(makeError(EErrorCode::PermissionDenied, "ROX-PLATFORM-PATH-ABSOLUTE",
						"Absolute paths are outside this filesystem capability.",
						"Use a normalized path relative to the configured root."));
				}

				const WideStringView relativeText = relative.View();
				usize segmentStart = 0;

				while (segmentStart < relativeText.size())
				{
					usize segmentEnd = segmentStart;

					while (segmentEnd < relativeText.size() && !IsPathSeparator(relativeText[segmentEnd]))
					{
						++segmentEnd;
					}

					const WideStringView segment = relativeText.substr(segmentStart, segmentEnd - segmentStart);
					segmentStart = segmentEnd;

					while (segmentStart < relativeText.size() && IsPathSeparator(relativeText[segmentStart]))
					{
						++segmentStart;
					}

					if (segment == L"..")
					{
						return Result<WindowsPath>::Failure(makeError(EErrorCode::PermissionDenied, "ROX-PLATFORM-PATH-ESCAPE",
							"The path attempts to escape the configured filesystem root.",
							"Remove parent traversal segments from the path."));
					}

					if (isReservedWindowsDeviceName(segment))
					{
						return Result<WindowsPath>::Failure(
							makeError(EErrorCode::PermissionDenied, "ROX-PLATFORM-PATH-DEVICE",
								"The path names a reserved Windows device rather than a file under the root.",
								"Rename the path segment so it is not a reserved device name."));
					}
				}

				SystemErrorCode error;
				WindowsPath joined(m_Root.Resource());
				if (!joined.Assign(m_Root.View()))
				{
					return Result<WindowsPath>::Failure(pathAllocationError());
				}
				if (!joined.AppendComponent(relative.View()))
				{
					return Result<WindowsPath>::Failure(pathAllocationError());
				}
				Result<WindowsPath> candidateResult = WeaklyCanonicalPath(joined, error);

				if (!candidateResult)
				{
					return Result<WindowsPath>::Failure(candidateResult.GetError());
				}

				WindowsPath candidate = Move(candidateResult).TakeValue();

				if (error)
				{
					return Result<WindowsPath>::Failure(makeWindowsFileSystemError("Resolving the root-relative path", error));
				}

				if (!IsWithinRoot(m_Root, candidate))
				{
					return Result<WindowsPath>::Failure(makeError(
						EErrorCode::PermissionDenied, "ROX-PLATFORM-PATH-SYMLINK-ESCAPE",
						"The resolved path leaves the configured filesystem root.", "Remove or replace the escaping symbolic link."));
				}

				return Result<WindowsPath>::Success(Move(candidate));
			}

		private:
			WindowsPath m_Root;
			bool m_bMounted = false;
		};
	} // namespace

	static Result<bool> queryWindowsPathWithinRoot(const StringView rootPathUtf8, const StringView candidatePathUtf8, MemoryResource& pathResource)
	{
		for (const StringView input : {rootPathUtf8, candidatePathUtf8})
		{
			const Result<void> validated = validateRootPath(input);

			if (!validated)
			{
				return Result<bool>::Failure(validated.GetError());
			}
		}

		SystemErrorCode error;
		const auto canonicalize = [&error, &pathResource](const StringView input) -> Result<WindowsPath>
		{
			Result<WindowsPath> path = PathFromUtf8(input, pathResource);

			if (!path)
			{
				return Result<WindowsPath>::Failure(path.GetError());
			}

			Result<WindowsPath> absolute = AbsolutePath(path.Value(), error);

			if (!absolute || error)
			{
				return absolute;
			}

			return WeaklyCanonicalPath(absolute.Value(), error);
		};
		Result<WindowsPath> rootResult = canonicalize(rootPathUtf8);

		if (!rootResult)
		{
			return Result<bool>::Failure(makeError(EErrorCode::ExternalFailure, "ROX-PLATFORM-PATH-ALLOCATION",
				"Path containment allocation failed.", "Release memory and retry the path query."));
		}

		const WindowsPath& root = rootResult.Value();
		if (error)
		{
			return Result<bool>::Failure(makeWindowsFileSystemError("Resolving the containment root", error));
		}

		Result<WindowsPath> candidateResult = canonicalize(candidatePathUtf8);

		if (!candidateResult)
		{
			return Result<bool>::Failure(makeError(EErrorCode::ExternalFailure, "ROX-PLATFORM-PATH-ALLOCATION",
				"Path containment allocation failed.", "Release memory and retry the path query."));
		}

		const WindowsPath candidate = Move(candidateResult).TakeValue();

		if (error)
		{
			return Result<bool>::Failure(makeWindowsFileSystemError("Resolving the containment candidate", error));
		}

		return Result<bool>::Success(rox::os::IsWithinRoot(root, candidate, true));
	}

	namespace
	{
		Result<FileSystemOwner> createWindowsFileSystem(MemoryResource& resource, const StringView rootPathUtf8,
			const bool bCreateRoot, MemoryResource& pathResource)
		{
			const Result<void> validation = validateRootPath(rootPathUtf8);

			if (!validation)
			{
				return Result<FileSystemOwner>::Failure(validation.GetError());
			}

			SystemErrorCode error;
			Result<WindowsPath> input = PathFromUtf8(rootPathUtf8, pathResource);

			if (!input)
			{
				return Result<FileSystemOwner>::Failure(makeError(EErrorCode::ExternalFailure, "ROX-PLATFORM-FILESYSTEM-ALLOCATION",
					"Opening the filesystem root could not allocate memory.", "Release memory and retry."));
			}

			Result<WindowsPath> absolute = AbsolutePath(input.Value(), error);

			if (!absolute)
			{
				return Result<FileSystemOwner>::Failure(makeError(EErrorCode::ExternalFailure, "ROX-PLATFORM-FILESYSTEM-ALLOCATION",
					"Opening the filesystem root could not allocate memory.", "Release memory and retry."));
			}

			WindowsPath root = Move(absolute).TakeValue();

			if (error)
			{
				return Result<FileSystemOwner>::Failure(makeWindowsFileSystemError("Resolving the filesystem root", error));
			}

			FileSystemOwner fileSystem = FileSystemOwner::TryCreate<WindowsFileSystem>(resource, Move(root));

			if (!fileSystem)
			{
				return Result<FileSystemOwner>::Failure(
					makeError(EErrorCode::ExternalFailure, "ROX-PLATFORM-FILESYSTEM-ALLOCATION",
						"The Windows filesystem capability allocation failed.",
						"Release memory or provide a memory resource with sufficient capacity, then retry."));
			}

			WindowsFileSystem* const windowsFileSystem = static_cast<WindowsFileSystem*>(fileSystem.Get());
			Result<void> initialization = windowsFileSystem->Initialize(bCreateRoot);

			if (!initialization)
			{
				if (initialization.GetError().DiagnosticID == "ROX-PLATFORM-PATH-ALLOCATION")
				{
					return Result<FileSystemOwner>::Failure(makeError(EErrorCode::ExternalFailure, "ROX-PLATFORM-FILESYSTEM-ALLOCATION",
						"Opening the filesystem root could not allocate memory.", "Release memory and retry."));
				}

				return Result<FileSystemOwner>::Failure(initialization.GetError());
			}

			LOG_INFO("Platform.FileSystem", "A Windows filesystem capability root was mounted.");

			return Result<FileSystemOwner>::Success(Move(fileSystem));
		}

	} // namespace

	static Result<FileSystemOwner> openWindowsFileSystem(MemoryResource& resource, const StringView rootPathUtf8, const bool bCreateRoot, MemoryResource& pathResource)
	{
		return createWindowsFileSystem(resource, rootPathUtf8, bCreateRoot, pathResource);
	}

	Result<bool> IsWindowsPathWithinRoot(const StringView rootPathUtf8, const StringView candidatePathUtf8)
	{
		return queryWindowsPathWithinRoot(rootPathUtf8, candidatePathUtf8, SystemMemoryResource::Get());
	}

	Result<FileSystemOwner> CreateWindowsFileSystem(MemoryResource& resource, const StringView rootPathUtf8)
	{
		return openWindowsFileSystem(resource, rootPathUtf8, true, SystemMemoryResource::Get());
	}

	Result<FileSystemOwner> OpenWindowsFileSystem(MemoryResource& resource, const StringView rootPathUtf8)
	{
		return openWindowsFileSystem(resource, rootPathUtf8, false, SystemMemoryResource::Get());
	}

	Result<Array<Byte>> ReadWindowsFile(StringView path, uint64 maximumBytes, MemoryResource& resource)
	{
		const usize separator = path.find_last_of("/\\");
		const StringView directory = separator == StringView::NPOS ? StringView(".") : path.Substr(0, separator + 1);
		const StringView name = separator == StringView::NPOS ? path : path.Substr(separator + 1);
		Result<FileSystemOwner> files = OpenWindowsFileSystem(resource, directory);

		if (!files)
		{
			return Result<Array<Byte>>::Failure(files.GetError());
		}

		return files.Value()->ReadFile(name, maximumBytes, resource);
	}

	Result<void> WriteWindowsFileAtomically(StringView path, Span<const Byte> bytes, MemoryResource& resource)
	{
		const usize separator = path.find_last_of("/\\");
		const StringView directory = separator == StringView::NPOS ? StringView(".") : path.Substr(0, separator + 1);
		const StringView name = separator == StringView::NPOS ? path : path.Substr(separator + 1);
		Result<FileSystemOwner> files = OpenWindowsFileSystem(resource, directory);

		if (!files)
		{
			return Result<void>::Failure(files.GetError());
		}

		Result<EAtomicWriteOutcome> written = files.Value()->WriteFileAtomically(name, bytes, EAtomicWritePolicy::ReplaceExisting);

		return written ? Result<void>::Success() : Result<void>::Failure(written.GetError());
	}

	Result<uint64> GetWindowsFileWriteStamp(StringView path, MemoryResource& resource)
	{
		Result<void> valid = validateRootPath(path);

		if (!valid)
		{
			return Result<uint64>::Failure(valid.GetError());
		}

		Result<WindowsPath> nativeResult = PathFromUtf8(path, resource);

		if (!nativeResult)
		{
			return Result<uint64>::Failure(EErrorCode::ExternalFailure, "ROX-PLATFORM-PATH-ALLOCATION",
				"The timestamp query could not allocate its path.");
		}

		WindowsPath native = Move(nativeResult).TakeValue();
		WIN32_FILE_ATTRIBUTE_DATA attributes{};

		if (!GetFileAttributesExW(native.CStr(), GetFileExInfoStandard, &attributes))
		{
			return Result<uint64>::Failure(makeWindowsFileSystemError(
				"Reading file timestamp", SystemErrorCode(static_cast<int>(GetLastError()), SystemCategory())));
		}

		return Result<uint64>::Success((static_cast<uint64>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
			attributes.ftLastWriteTime.dwLowDateTime);
	}

	Result<Array<String>> ReadWindowsDirectoryFiles(StringView root, MemoryResource& memory, uint32 maximum, uint32 depth, CancellationToken cancellation)
	{
		using DirectoryResult = Result<Array<String>>;

		if (root.IsEmpty() || root.Size() > 32767 || root.Find('\0') != StringView::NPOS || !IsValidUtf8(root) ||
			maximum == 0 || maximum > 65536 || depth == 0 || depth > 64)
		{
			return DirectoryResult::Failure(
				failure(EErrorCode::InvalidArgument, "Directory inventory input is invalid or exceeds its bounds."));
		}

		SystemErrorCode error;
		Result<WindowsPath> inputResult = PathFromUtf8({ root.Data(), root.Size() }, memory);

		if (!inputResult)
		{
			return DirectoryResult::Failure(failure(EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
		}

		const WindowsPath input = Move(inputResult).TakeValue();
		Result<WindowsPath> pathResult = AbsolutePath(input, error);

		if (!pathResult)
		{
			return DirectoryResult::Failure(failure(EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
		}

		const WindowsPath path = Move(pathResult).TakeValue();

		if (error)
		{
			return DirectoryResult::Failure(failure(EErrorCode::ExternalFailure, "Directory root cannot be resolved."));
		}

		const WideStringView text = path.View();
		const usize rootLength = RootPathSize(text);

		for (usize offset = rootLength; offset <= text.size(); ++offset)
		{
			if (offset != text.size() && !IsPathSeparator(text[offset]))
			{
				continue;
			}

			WindowsPath prefix(memory);
			if (!prefix.Assign(text.substr(0, offset)))
			{
				return DirectoryResult::Failure(failure(EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
			}
			const DWORD attributes = GetFileAttributesW(prefix.CStr());

			if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
				(attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			{
				return DirectoryResult::Failure(
					failure(EErrorCode::Conflict, "Directory root is unavailable or crosses a reparse point."));
			}
		}

		Array<String> output(memory);
		uint32 entries = 0;
		Result<void> read = scanRecursive(path, {}, output, entries, maximum, depth, cancellation);

		if (!read)
		{
			return DirectoryResult::Failure(read.GetError());
		}

		Sort(output.begin(), output.end(), [](const String& a, const String& b)
		{
			return a.View() < b.View();
		});

		return DirectoryResult::Success(Move(output));
	}

} // namespace rox::os
