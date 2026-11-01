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
	namespace
	{
		core::Error failure(core::EErrorCode code, core::StringView message)
		{
			return core::MakeError(code, "ROX-PLATFORM-DIRECTORY-INVENTORY", message);
		}

		core::Error pathAllocationError() noexcept
		{
			return core::MakeError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-PATH-ALLOCATION",
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

		core::Result<void> scanRecursive(const WindowsPath& directory, core::StringView prefix, core::Array<core::String>& result, core::uint32& entries, core::uint32 maximum, core::uint32 depth, core::CancellationToken cancellation)
		{
			core::MemoryResource& memory = result.GetResource();

			if (cancellation.IsCancellationRequested())
			{
				return core::Result<void>::Failure(failure(core::EErrorCode::Cancelled, "Directory inventory was cancelled."));
			}

			WindowsPath pattern(memory);

			if (!pattern.Assign(directory.View()))
			{
				return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
			}
			if (!pattern.AppendComponent(L"*"))
			{
				return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
			}
			WIN32_FIND_DATAW found{};
			DirectorySearch search(FindFirstFileW(pattern.CStr(), &found));

			if (search.GetHandle() == INVALID_HANDLE_VALUE)
			{
				if (GetLastError() == ERROR_FILE_NOT_FOUND)
				{
					return core::Result<void>::Success();
				}

				return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Directory inventory could not be opened."));
			}

			do
			{
				if (cancellation.IsCancellationRequested())
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::Cancelled, "Directory inventory was cancelled."));
				}

				const rox::core::WideStringView name(found.cFileName);

				if (name == L"." || name == L"..")
				{
					continue;
				}

				if (++entries > maximum || (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::Conflict, "Directory inventory exceeds its bound or traverses a reparse point."));
				}

				if (name.empty() || name.back() == L'.' || name.back() == L' ')
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::Conflict, "Directory inventory contains a path alias."));
				}

				const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);

				if (length <= 0)
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::InvalidArgument, "Directory entry is not valid Unicode."));
				}

				core::Array<char> utf8(memory);

				if (!utf8.Resize(static_cast<rox::core::usize>(length)))
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Directory entry allocation failed."));
				}

				if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), utf8.Data(), length, nullptr, nullptr) != length)
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Directory entry conversion failed."));
				}

				core::String relative(memory);

				if (!relative.Assign(prefix) || !relative.Append({ utf8.Data(), utf8.Size() }))
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Directory path allocation failed."));
				}

				if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					if (depth == 0)
					{
						return core::Result<void>::Failure(failure(core::EErrorCode::Conflict, "Directory inventory exceeds its depth bound."));
					}

					WindowsPath child(memory);
					if (!child.Assign(directory.View()))
					{
						return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
					}
					if (!child.AppendComponent(name))
					{
						return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
					}

					if (!relative.Append("/"))
					{
						return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Directory path allocation failed."));
					}

					core::Result<void> nested = scanRecursive(child, relative.View(), result, entries, maximum, depth - 1, cancellation);

					if (!nested)
					{
						return nested;
					}
				}
				else if (!result.Add(rox::core::Move(relative)))
				{
					return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
				}
			} while (FindNextFileW(search.GetHandle(), &found));

			if (GetLastError() != ERROR_NO_MORE_FILES)
			{
				return core::Result<void>::Failure(failure(core::EErrorCode::ExternalFailure, "Directory inventory ended unexpectedly."));
			}

			return core::Result<void>::Success();
		}

		rox::core::Atomic<core::uint64> s_temporaryFileCounter = 0;

		core::Error makeError(core::EErrorCode code, core::StringView diagnosticId, core::StringView message, core::StringView recoveryHint)
		{
			ASSERT(code != core::EErrorCode::None, "A filesystem failure must use a non-success error code.");
			ASSERT(!diagnosticId.IsEmpty(), "A filesystem failure must provide a diagnostic ID.");
			ASSERT(!message.IsEmpty(), "A filesystem failure must provide a message.");

			return core::MakeError(code, diagnosticId, message, recoveryHint);
		}

		core::Result<void> validateRootPath(const rox::core::StringView rootPathUtf8)
		{
			if (rootPathUtf8.empty())
			{
				return core::Result<void>::Failure(makeError(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-ROOT-EMPTY",
					"The filesystem capability root is empty.",
					"Provide an absolute or relative UTF-8 root directory."));
			}

			if (rootPathUtf8.find('\0') != rox::core::StringView::npos)
			{
				return core::Result<void>::Failure(makeError(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-ROOT-INVALID",
					"The filesystem capability root contains a null character.",
					"Remove embedded null characters from the UTF-8 root path."));
			}

			const core::StringView rootPath(rootPathUtf8.data(), rootPathUtf8.size());

			if (!core::IsValidUtf8(rootPath))
			{
				return core::Result<void>::Failure(makeError(
					core::EErrorCode::InvalidArgument, "ROX-PLATFORM-ROOT-UTF8", "The filesystem capability root is not well-formed UTF-8.",
					"Encode the root path as valid UTF-8 before creating the filesystem capability."));
			}

			return core::Result<void>::Success();
		}

		// Win32 resolves device names inside any directory, ignoring extensions and trailing spaces.
		// Reject them explicitly; root containment alone does not exclude device access.
		[[nodiscard]] bool isReservedWindowsDeviceName(const rox::core::WideStringView segment) noexcept
		{
			static constexpr rox::core::WideStringView RESERVED_NAMES[] = {
				L"CON",  L"PRN",  L"AUX",  L"NUL",  L"CONIN$",    L"CONOUT$",   L"COM1",      L"COM2",      L"COM3",      L"COM4",
				L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",      L"LPT1",      L"LPT2",      L"LPT3",      L"LPT4",      L"LPT5",
				L"LPT6", L"LPT7", L"LPT8", L"LPT9", L"COM\u00b9", L"COM\u00b2", L"COM\u00b3", L"LPT\u00b9", L"LPT\u00b2", L"LPT\u00b3" };

			rox::core::usize end = segment.find(L'.');

			if (end == rox::core::WideStringView::npos)
			{
				end = segment.size();
			}
			while (end != 0 && segment[end - 1] == L' ')
			{
				--end;
			}

			const rox::core::WideStringView base = segment.substr(0, end);

			for (const rox::core::WideStringView reserved : RESERVED_NAMES)
			{
				if (base.size() != reserved.size())
				{
					continue;
				}

				bool bMatches = true;

				for (rox::core::usize index = 0; index < base.size(); ++index)
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

		core::Error makeWindowsFileSystemError(rox::core::StringView operation, const rox::core::SystemErrorCode& error) noexcept
		{
			constexpr rox::core::usize MAX_OPERATION_BYTES = 34;
			ASSERT(!operation.empty() && operation.size() <= MAX_OPERATION_BYTES, "Filesystem operation labels must contain between 1 and 34 bytes.");
			ASSERT(core::IsValidUtf8(core::StringView(operation)), "Filesystem operation labels must be valid UTF-8.");
			ASSERT(error, "Filesystem error conversion requires a failing error code.");
			core::EErrorCode code = core::EErrorCode::ExternalFailure;

			if (error == rox::core::EErrorCondition::no_such_file_or_directory)
			{
				code = core::EErrorCode::NotFound;
			}
			else if (error == rox::core::EErrorCondition::permission_denied)
			{
				code = core::EErrorCode::PermissionDenied;
			}
			else if (error == rox::core::EErrorCondition::file_exists)
			{
				code = core::EErrorCode::Conflict;
			}
			else if (error == rox::core::EErrorCondition::invalid_argument)
			{
				code = core::EErrorCode::InvalidArgument;
			}

			char codeText[16] = {};
			const rox::core::ToCharsResult converted = rox::core::ToChars(codeText, codeText + sizeof(codeText), error.value());
			ASSERT(converted.ec == rox::core::EErrorCondition(), "An operating-system error code must render into 16 bytes.");

			constexpr char FAILURE_TEXT[] = " failed with operating-system error code ";
			char message[MAX_OPERATION_BYTES + sizeof(FAILURE_TEXT) + sizeof(codeText)] = {};
			const rox::core::usize prefixSize = operation.size() + sizeof(FAILURE_TEXT) - 1;
			const rox::core::usize codeSize = static_cast<rox::core::usize>(converted.ptr - codeText);
			rox::core::CopyBytes(message, operation.data(), operation.size());
			rox::core::CopyBytes(message + operation.size(), FAILURE_TEXT, sizeof(FAILURE_TEXT) - 1);
			rox::core::CopyBytes(message + prefixSize, codeText, codeSize);
			message[prefixSize + codeSize] = '.';

			return makeError(code, "ROX-PLATFORM-FILESYSTEM", core::StringView(message, prefixSize + codeSize + 1), "Check the root-relative path and filesystem permissions.");
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
				: m_Root(rox::core::Move(root))
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

			core::Result<void> Initialize(const bool bCreateRoot)
			{
				ASSERT(!m_bMounted, "A Windows filesystem capability can be initialized only once.");
				rox::core::SystemErrorCode error;

				if (bCreateRoot)
				{
					core::Result<void> createdDirectories = CreatePathDirectories(m_Root, error);

					if (!createdDirectories)
					{
						return core::Result<void>::Failure(createdDirectories.GetError());
					}
				}
				else
				{
					const unsigned long status = PathAttributes(m_Root, error);

					if (error)
					{
						return core::Result<void>::Failure(makeWindowsFileSystemError("Opening the filesystem root", error));
					}

					if (status == INVALID_FILE_ATTRIBUTES)
					{
						return core::Result<void>::Failure(makeError(core::EErrorCode::NotFound, "ROX-PLATFORM-ROOT-MISSING",
							"The input filesystem root does not exist.",
							"Provide an existing directory."));
					}

					if ((status & FILE_ATTRIBUTE_DIRECTORY) == 0)
					{
						return core::Result<void>::Failure(makeError(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-ROOT-NOT-DIRECTORY",
							"The input filesystem root is not a directory.",
							"Provide an existing directory."));
					}
				}

				if (error)
				{
					return core::Result<void>::Failure(makeWindowsFileSystemError("Creating the filesystem root", error));
				}

				core::Result<WindowsPath> canonicalRootResult = WeaklyCanonicalPath(m_Root, error);

				if (!canonicalRootResult)
				{
					return core::Result<void>::Failure(canonicalRootResult.GetError());
				}

				WindowsPath canonicalRoot = rox::core::Move(canonicalRootResult).TakeValue();

				if (error)
				{
					return core::Result<void>::Failure(makeWindowsFileSystemError("Canonicalizing the filesystem root", error));
				}

				m_Root = rox::core::Move(canonicalRoot);
				m_bMounted = true;
				ASSERT(!m_Root.IsEmpty() && IsAbsolutePath(m_Root.View()), "An initialized Windows filesystem capability must retain a canonical absolute root.");
				return core::Result<void>::Success();
			}

			core::Result<bool> Exists(rox::core::StringView relativePath) const override
			{
				core::Result<WindowsPath> path = resolve(relativePath);

				if (!path)
				{
					return core::Result<bool>::Failure(path.GetError());
				}

				rox::core::SystemErrorCode error;
				const unsigned long attributes = PathAttributes(path.Value(), error);
				const bool bExists = attributes != INVALID_FILE_ATTRIBUTES;

				if (IsMissingPathError(error))
				{
					error.clear();
				}

				if (error)
				{
					return core::Result<bool>::Failure(makeWindowsFileSystemError("Checking file existence", error));
				}

				return core::Result<bool>::Success(bExists);
			}

			core::Result<core::Array<core::Byte>> ReadFile(rox::core::StringView relativePath, core::uint64 maximumBytes, core::MemoryResource& resource) const override
			{
				if (maximumBytes == 0)
				{
					return core::Result<core::Array<core::Byte>>::Failure(
						makeError(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-FILE-LIMIT-ZERO",
							"The maximum file size must be greater than zero.", "Provide a bounded non-zero maximum byte count."));
				}

				core::Result<WindowsPath> path = resolve(relativePath);

				if (!path)
				{
					return core::Result<core::Array<core::Byte>>::Failure(path.GetError());
				}

				rox::core::SystemErrorCode error;
				const rox::core::uintmax size = PathFileSize(path.Value(), error);

				if (error)
				{
					return core::Result<core::Array<core::Byte>>::Failure(makeWindowsFileSystemError("Reading file size", error));
				}

				if (size > maximumBytes || size > static_cast<rox::core::uintmax>(rox::core::NumericLimits<rox::core::usize>::max()) ||
					size > static_cast<rox::core::uintmax>(rox::core::NumericLimits<core::int64>::max()))
				{
					return core::Result<core::Array<core::Byte>>::Failure(
						makeError(core::EErrorCode::Unsupported, "ROX-PLATFORM-FILE-LIMIT",
							"The file exceeds the caller's maximum byte count or readable buffer range.",
							"Raise the explicit limit only after validating the input source."));
				}

				const HANDLE file = CreateFileW(path.Value().CStr(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
					OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

				if (file == INVALID_HANDLE_VALUE)
				{
					return core::Result<core::Array<core::Byte>>::Failure(
						makeError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-FILE-OPEN", "The file could not be opened for reading.",
							"Check whether another process or filesystem policy denied access."));
				}

				core::ScopeExit closeFile([file]() noexcept
				{
					CloseHandle(file);
				});
				const rox::core::usize byteCount = static_cast<rox::core::usize>(size);
				core::Array<core::Byte> bytes(resource);

				if (!bytes.ResizeUninitialized(byteCount))
				{
					return core::Result<core::Array<core::Byte>>::Failure(makeError(
						core::EErrorCode::ExternalFailure, "ROX-PLATFORM-FILE-ALLOCATION", "The file read buffer could not be allocated.",
						"Free memory, lower the bounded input size, or retry with a larger memory budget."));
				}

				ASSERT(bytes.Size() == byteCount, "A successful resource-aware file read allocation must match the file size.");

				core::usize offset = 0;

				while (offset < bytes.Size())
				{
					const DWORD request =
						static_cast<DWORD>(core::Min<core::usize>(bytes.Size() - offset, core::NumericLimits<DWORD>::max()));
					DWORD received = 0;

					if (!::ReadFile(file, bytes.Data() + offset, request, &received, nullptr) || received != request)
					{
						return core::Result<core::Array<core::Byte>>::Failure(
							makeError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-FILE-SHORT-READ",
								"The file changed or became unreadable during the bounded read.",
								"Retry the operation from a stable input snapshot."));
					}

					offset += received;
				}

				return core::Result<core::Array<core::Byte>>::Success(rox::core::Move(bytes));
			}

			core::Result<EAtomicWriteOutcome> WriteFileAtomically(rox::core::StringView relativePath, rox::core::Span<const core::Byte> data, EAtomicWritePolicy policy) override
			{
				core::Result<WindowsPath> destination = resolve(relativePath);

				if (!destination)
				{
					return core::Result<EAtomicWriteOutcome>::Failure(destination.GetError());
				}

				ASSERT(policy == EAtomicWritePolicy::CreateNew || policy == EAtomicWritePolicy::ReplaceExisting, "Atomic write policy value is out of range.");

				rox::core::SystemErrorCode directoryError;
				core::Result<WindowsPath> destinationParent = destination.Value().Parent();

				if (!destinationParent)
				{
					return core::Result<EAtomicWriteOutcome>::Failure(destinationParent.GetError());
				}

				core::Result<void> createdDirectories = CreatePathDirectories(destinationParent.Value(), directoryError);

				if (!createdDirectories)
				{
					return core::Result<EAtomicWriteOutcome>::Failure(createdDirectories.GetError());
				}

				if (directoryError)
				{
					return core::Result<EAtomicWriteOutcome>::Failure(makeWindowsFileSystemError("Creating the destination directory", directoryError));
				}

				const core::uint64 temporaryId = s_temporaryFileCounter.fetch_add(1, rox::core::MemoryOrderRelaxed);
				WindowsPath staging(m_Root.Resource());
				if (!staging.Assign(destination.Value().View()))
				{
					return core::Result<EAtomicWriteOutcome>::Failure(pathAllocationError());
				}
				wchar_t suffix[64]{};
				const int suffixSize = swprintf_s(suffix, L".roxell-tmp-%lu-%llu", GetCurrentProcessId(), temporaryId);
				ASSERT(suffixSize > 0, "The bounded staging suffix must contain both numeric identifiers.");
				if (!staging.Append({ suffix, static_cast<rox::core::usize>(suffixSize) }))
				{
					return core::Result<EAtomicWriteOutcome>::Failure(pathAllocationError());
				}

				HANDLE file = CreateFileW(staging.CStr(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);

				if (file == INVALID_HANDLE_VALUE)
				{
					return core::Result<EAtomicWriteOutcome>::Failure(
						makeWindowsFileSystemError("Creating the staging file", rox::core::SystemErrorCode(static_cast<int>(GetLastError()),
							rox::core::SystemCategory())));
				}

				bool bWriteSucceeded = true;
				DWORD writeError = ERROR_SUCCESS;
				rox::core::usize offset = 0;

				while (offset < data.size())
				{
					const rox::core::usize remaining = data.size() - offset;
					const DWORD chunkSize = static_cast<DWORD>((rox::core::Min)(remaining, static_cast<rox::core::usize>(rox::core::NumericLimits<DWORD>::max())));
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

					return core::Result<EAtomicWriteOutcome>::Failure(makeWindowsFileSystemError(
						"Writing the staging file", rox::core::SystemErrorCode(static_cast<int>(writeError), rox::core::SystemCategory())));
				}

				DWORD moveFlags = MOVEFILE_WRITE_THROUGH;

				if (policy == EAtomicWritePolicy::ReplaceExisting)
				{
					moveFlags |= MOVEFILE_REPLACE_EXISTING;
				}

				DWORD moveError = ERROR_SUCCESS;
				constexpr core::uint32 MAXIMUM_PUBLISH_ATTEMPTS = 5;

				for (core::uint32 attempt = 0; attempt < MAXIMUM_PUBLISH_ATTEMPTS; ++attempt)
				{
					if (MoveFileExW(staging.CStr(), destination.Value().CStr(), moveFlags) != FALSE)
					{
						return core::Result<EAtomicWriteOutcome>::Success(EAtomicWriteOutcome::Published);
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
					return core::Result<EAtomicWriteOutcome>::Success(EAtomicWriteOutcome::AlreadyExists);
				}

				return core::Result<EAtomicWriteOutcome>::Failure(makeWindowsFileSystemError("Publishing the staging file", rox::core::SystemErrorCode(static_cast<int>(moveError), rox::core::SystemCategory())));
			}

			core::Result<void> RemoveFile(rox::core::StringView relativePath) override
			{
				core::Result<WindowsPath> path = resolve(relativePath);

				if (!path)
				{
					return core::Result<void>::Failure(path.GetError());
				}

				if (DeleteFileW(path.Value().CStr()) == FALSE)
				{
					const DWORD removeError = GetLastError();

					if (removeError != ERROR_FILE_NOT_FOUND && removeError != ERROR_PATH_NOT_FOUND)
					{
						return core::Result<void>::Failure(makeWindowsFileSystemError(
							"Removing the file", rox::core::SystemErrorCode(static_cast<int>(removeError), rox::core::SystemCategory())));
					}
				}

				return core::Result<void>::Success();
			}

		protected:
			core::Result<core::uintptr> tryAcquireExclusiveFileLock(rox::core::StringView relativePath) override
			{
				core::Result<WindowsPath> path = resolve(relativePath);

				if (!path)
				{
					return core::Result<core::uintptr>::Failure(path.GetError());
				}

				rox::core::SystemErrorCode directoryError;
				core::Result<WindowsPath> lockParent = path.Value().Parent();

				if (!lockParent)
				{
					return core::Result<core::uintptr>::Failure(lockParent.GetError());
				}

				core::Result<void> createdDirectories = CreatePathDirectories(lockParent.Value(), directoryError);

				if (!createdDirectories)
				{
					return core::Result<core::uintptr>::Failure(createdDirectories.GetError());
				}

				if (directoryError)
				{
					return core::Result<core::uintptr>::Failure(makeWindowsFileSystemError("Creating the file-lock directory", directoryError));
				}

				HANDLE handle = CreateFileW(path.Value().CStr(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);

				if (handle == INVALID_HANDLE_VALUE)
				{
					const DWORD error = GetLastError();

					if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION)
					{
						return core::Result<core::uintptr>::Failure(
							makeError(core::EErrorCode::Conflict, "ROX-PLATFORM-FILE-LOCK-CONFLICT",
								"The file is already locked by another owner.",
								"Retry after the current owner releases the lock."));
					}

					return core::Result<core::uintptr>::Failure(
						makeWindowsFileSystemError("Acquiring the exclusive file lock",
							rox::core::SystemErrorCode(static_cast<int>(error), rox::core::SystemCategory())));
				}

				const core::uintptr token = reinterpret_cast<core::uintptr>(handle);
				ASSERT(token != 0, "A successful Windows file lock must produce a valid opaque token.");

				return core::Result<core::uintptr>::Success(token);
			}

			void releaseExclusiveFileLock(const core::uintptr token) noexcept override
			{
				ASSERT(token != 0, "Releasing an exclusive file lock requires a valid opaque token.");
				HANDLE handle = reinterpret_cast<HANDLE>(token);
				ASSERT(handle != INVALID_HANDLE_VALUE, "Releasing an exclusive file lock requires a valid Windows handle.");
				const BOOL bClosed = CloseHandle(handle);
				ASSERT(bClosed != FALSE, "A Windows exclusive file lock handle must close successfully.");
			}

		private:
			core::Result<WindowsPath> resolve(rox::core::StringView relativePath) const
			{
				if (relativePath.empty() || relativePath.find('\0') != rox::core::StringView::npos)
				{
					return core::Result<WindowsPath>::Failure(makeError(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-PATH-INVALID",
						"The root-relative path is empty or contains a null character.",
						"Provide a non-empty UTF-8 path relative to the filesystem root."));
				}

				const core::StringView relativePathUtf8(relativePath.data(), relativePath.size());

				if (!core::IsValidUtf8(relativePathUtf8))
				{
					return core::Result<WindowsPath>::Failure(makeError(
						core::EErrorCode::InvalidArgument, "ROX-PLATFORM-PATH-UTF8", "The root-relative path is not well-formed UTF-8.",
						"Encode the path as valid UTF-8 before using the filesystem capability."));
				}

				core::Result<WindowsPath> relativeResult = PathFromUtf8(relativePath, m_Root.Resource());

				if (!relativeResult)
				{
					return core::Result<WindowsPath>::Failure(relativeResult.GetError());
				}

				const WindowsPath relative = rox::core::Move(relativeResult).TakeValue();

				if (RootNameSize(relative.View()) != 0 || RootPathSize(relative.View()) != 0)
				{
					return core::Result<WindowsPath>::Failure(makeError(core::EErrorCode::PermissionDenied, "ROX-PLATFORM-PATH-ABSOLUTE",
						"Absolute paths are outside this filesystem capability.",
						"Use a normalized path relative to the configured root."));
				}

				const core::WideStringView relativeText = relative.View();
				rox::core::usize segmentStart = 0;

				while (segmentStart < relativeText.size())
				{
					rox::core::usize segmentEnd = segmentStart;

					while (segmentEnd < relativeText.size() && !IsPathSeparator(relativeText[segmentEnd]))
					{
						++segmentEnd;
					}

					const core::WideStringView segment = relativeText.substr(segmentStart, segmentEnd - segmentStart);
					segmentStart = segmentEnd;

					while (segmentStart < relativeText.size() && IsPathSeparator(relativeText[segmentStart]))
					{
						++segmentStart;
					}

					if (segment == L"..")
					{
						return core::Result<WindowsPath>::Failure(makeError(core::EErrorCode::PermissionDenied, "ROX-PLATFORM-PATH-ESCAPE",
							"The path attempts to escape the configured filesystem root.",
							"Remove parent traversal segments from the path."));
					}

					if (isReservedWindowsDeviceName(segment))
					{
						return core::Result<WindowsPath>::Failure(
							makeError(core::EErrorCode::PermissionDenied, "ROX-PLATFORM-PATH-DEVICE",
								"The path names a reserved Windows device rather than a file under the root.",
								"Rename the path segment so it is not a reserved device name."));
					}
				}

				rox::core::SystemErrorCode error;
				WindowsPath joined(m_Root.Resource());
				if (!joined.Assign(m_Root.View()))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}
				if (!joined.AppendComponent(relative.View()))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}
				core::Result<WindowsPath> candidateResult = WeaklyCanonicalPath(joined, error);

				if (!candidateResult)
				{
					return core::Result<WindowsPath>::Failure(candidateResult.GetError());
				}

				WindowsPath candidate = rox::core::Move(candidateResult).TakeValue();

				if (error)
				{
					return core::Result<WindowsPath>::Failure(makeWindowsFileSystemError("Resolving the root-relative path", error));
				}

				if (!IsWithinRoot(m_Root, candidate))
				{
					return core::Result<WindowsPath>::Failure(makeError(
						core::EErrorCode::PermissionDenied, "ROX-PLATFORM-PATH-SYMLINK-ESCAPE",
						"The resolved path leaves the configured filesystem root.", "Remove or replace the escaping symbolic link."));
				}

				return core::Result<WindowsPath>::Success(rox::core::Move(candidate));
			}

		private:
			WindowsPath m_Root;
			bool m_bMounted = false;
		};
	} // namespace

	static core::Result<bool> queryWindowsPathWithinRoot(const rox::core::StringView rootPathUtf8, const rox::core::StringView candidatePathUtf8, core::MemoryResource& pathResource)
	{
		for (const rox::core::StringView input : {rootPathUtf8, candidatePathUtf8})
		{
			const core::Result<void> validated = validateRootPath(input);

			if (!validated)
			{
				return core::Result<bool>::Failure(validated.GetError());
			}
		}

		rox::core::SystemErrorCode error;
		const auto canonicalize = [&error, &pathResource](const rox::core::StringView input) -> core::Result<WindowsPath>
		{
			core::Result<WindowsPath> path = PathFromUtf8(input, pathResource);

			if (!path)
			{
				return core::Result<WindowsPath>::Failure(path.GetError());
			}

			core::Result<WindowsPath> absolute = AbsolutePath(path.Value(), error);

			if (!absolute || error)
			{
				return absolute;
			}

			return WeaklyCanonicalPath(absolute.Value(), error);
		};
		core::Result<WindowsPath> rootResult = canonicalize(rootPathUtf8);

		if (!rootResult)
		{
			return core::Result<bool>::Failure(makeError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-PATH-ALLOCATION",
				"Path containment allocation failed.", "Release memory and retry the path query."));
		}

		const WindowsPath& root = rootResult.Value();
		if (error)
		{
			return core::Result<bool>::Failure(makeWindowsFileSystemError("Resolving the containment root", error));
		}

		core::Result<WindowsPath> candidateResult = canonicalize(candidatePathUtf8);

		if (!candidateResult)
		{
			return core::Result<bool>::Failure(makeError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-PATH-ALLOCATION",
				"Path containment allocation failed.", "Release memory and retry the path query."));
		}

		const WindowsPath candidate = rox::core::Move(candidateResult).TakeValue();

		if (error)
		{
			return core::Result<bool>::Failure(makeWindowsFileSystemError("Resolving the containment candidate", error));
		}

		return core::Result<bool>::Success(rox::os::IsWithinRoot(root, candidate, true));
	}

	namespace
	{
		core::Result<FileSystemOwner> createWindowsFileSystem(core::MemoryResource& resource, const rox::core::StringView rootPathUtf8,
			const bool bCreateRoot, core::MemoryResource& pathResource)
		{
			const core::Result<void> validation = validateRootPath(rootPathUtf8);

			if (!validation)
			{
				return core::Result<FileSystemOwner>::Failure(validation.GetError());
			}

			rox::core::SystemErrorCode error;
			core::Result<WindowsPath> input = PathFromUtf8(rootPathUtf8, pathResource);

			if (!input)
			{
				return core::Result<FileSystemOwner>::Failure(makeError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-FILESYSTEM-ALLOCATION",
					"Opening the filesystem root could not allocate memory.", "Release memory and retry."));
			}

			core::Result<WindowsPath> absolute = AbsolutePath(input.Value(), error);

			if (!absolute)
			{
				return core::Result<FileSystemOwner>::Failure(makeError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-FILESYSTEM-ALLOCATION",
					"Opening the filesystem root could not allocate memory.", "Release memory and retry."));
			}

			WindowsPath root = rox::core::Move(absolute).TakeValue();

			if (error)
			{
				return core::Result<FileSystemOwner>::Failure(makeWindowsFileSystemError("Resolving the filesystem root", error));
			}

			FileSystemOwner fileSystem = FileSystemOwner::TryCreate<WindowsFileSystem>(resource, rox::core::Move(root));

			if (!fileSystem)
			{
				return core::Result<FileSystemOwner>::Failure(
					makeError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-FILESYSTEM-ALLOCATION",
						"The Windows filesystem capability allocation failed.",
						"Release memory or provide a memory resource with sufficient capacity, then retry."));
			}

			WindowsFileSystem* const windowsFileSystem = static_cast<WindowsFileSystem*>(fileSystem.Get());
			core::Result<void> initialization = windowsFileSystem->Initialize(bCreateRoot);

			if (!initialization)
			{
				if (initialization.GetError().DiagnosticID == "ROX-PLATFORM-PATH-ALLOCATION")
				{
					return core::Result<FileSystemOwner>::Failure(makeError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-FILESYSTEM-ALLOCATION",
						"Opening the filesystem root could not allocate memory.", "Release memory and retry."));
				}

				return core::Result<FileSystemOwner>::Failure(initialization.GetError());
			}

			LOG_INFO("Platform.FileSystem", "A Windows filesystem capability root was mounted.");

			return core::Result<FileSystemOwner>::Success(rox::core::Move(fileSystem));
		}

	} // namespace

	static core::Result<FileSystemOwner> openWindowsFileSystem(core::MemoryResource& resource, const rox::core::StringView rootPathUtf8, const bool bCreateRoot, core::MemoryResource& pathResource)
	{
		return createWindowsFileSystem(resource, rootPathUtf8, bCreateRoot, pathResource);
	}

	core::Result<bool> IsWindowsPathWithinRoot(const rox::core::StringView rootPathUtf8, const rox::core::StringView candidatePathUtf8)
	{
		return queryWindowsPathWithinRoot(rootPathUtf8, candidatePathUtf8, core::SystemMemoryResource::Get());
	}

	core::Result<FileSystemOwner> CreateWindowsFileSystem(core::MemoryResource& resource, const rox::core::StringView rootPathUtf8)
	{
		return openWindowsFileSystem(resource, rootPathUtf8, true, core::SystemMemoryResource::Get());
	}

	core::Result<FileSystemOwner> OpenWindowsFileSystem(core::MemoryResource& resource, const rox::core::StringView rootPathUtf8)
	{
		return openWindowsFileSystem(resource, rootPathUtf8, false, core::SystemMemoryResource::Get());
	}

	core::Result<core::Array<core::Byte>> ReadWindowsFile(core::StringView path, core::uint64 maximumBytes, core::MemoryResource& resource)
	{
		const core::usize separator = path.find_last_of("/\\");
		const core::StringView directory = separator == core::StringView::NPOS ? core::StringView(".") : path.Substr(0, separator + 1);
		const core::StringView name = separator == core::StringView::NPOS ? path : path.Substr(separator + 1);
		core::Result<FileSystemOwner> files = OpenWindowsFileSystem(resource, directory);

		if (!files)
		{
			return core::Result<core::Array<core::Byte>>::Failure(files.GetError());
		}

		return files.Value()->ReadFile(name, maximumBytes, resource);
	}

	core::Result<void> WriteWindowsFileAtomically(core::StringView path, core::Span<const core::Byte> bytes, core::MemoryResource& resource)
	{
		const core::usize separator = path.find_last_of("/\\");
		const core::StringView directory = separator == core::StringView::NPOS ? core::StringView(".") : path.Substr(0, separator + 1);
		const core::StringView name = separator == core::StringView::NPOS ? path : path.Substr(separator + 1);
		core::Result<FileSystemOwner> files = OpenWindowsFileSystem(resource, directory);

		if (!files)
		{
			return core::Result<void>::Failure(files.GetError());
		}

		core::Result<EAtomicWriteOutcome> written = files.Value()->WriteFileAtomically(name, bytes, EAtomicWritePolicy::ReplaceExisting);

		return written ? core::Result<void>::Success() : core::Result<void>::Failure(written.GetError());
	}

	core::Result<core::uint64> GetWindowsFileWriteStamp(core::StringView path, core::MemoryResource& resource)
	{
		core::Result<void> valid = validateRootPath(path);

		if (!valid)
		{
			return core::Result<core::uint64>::Failure(valid.GetError());
		}

		core::Result<WindowsPath> nativeResult = PathFromUtf8(path, resource);

		if (!nativeResult)
		{
			return core::Result<core::uint64>::Failure(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-PATH-ALLOCATION",
				"The timestamp query could not allocate its path.");
		}

		WindowsPath native = rox::core::Move(nativeResult).TakeValue();
		WIN32_FILE_ATTRIBUTE_DATA attributes{};

		if (!GetFileAttributesExW(native.CStr(), GetFileExInfoStandard, &attributes))
		{
			return core::Result<core::uint64>::Failure(makeWindowsFileSystemError(
				"Reading file timestamp", core::SystemErrorCode(static_cast<int>(GetLastError()), core::SystemCategory())));
		}

		return core::Result<core::uint64>::Success((static_cast<core::uint64>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
			attributes.ftLastWriteTime.dwLowDateTime);
	}

	core::Result<core::Array<core::String>> ReadWindowsDirectoryFiles(core::StringView root, core::MemoryResource& memory, core::uint32 maximum, core::uint32 depth, core::CancellationToken cancellation)
	{
		using Result = core::Result<core::Array<core::String>>;

		if (root.IsEmpty() || root.Size() > 32767 || root.Find('\0') != core::StringView::NPOS || !core::IsValidUtf8(root) ||
			maximum == 0 || maximum > 65536 || depth == 0 || depth > 64)
		{
			return Result::Failure(
				failure(core::EErrorCode::InvalidArgument, "Directory inventory input is invalid or exceeds its bounds."));
		}

		rox::core::SystemErrorCode error;
		core::Result<WindowsPath> inputResult = PathFromUtf8({ root.Data(), root.Size() }, memory);

		if (!inputResult)
		{
			return Result::Failure(failure(core::EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
		}

		const WindowsPath input = rox::core::Move(inputResult).TakeValue();
		core::Result<WindowsPath> pathResult = AbsolutePath(input, error);

		if (!pathResult)
		{
			return Result::Failure(failure(core::EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
		}

		const WindowsPath path = rox::core::Move(pathResult).TakeValue();

		if (error)
		{
			return Result::Failure(failure(core::EErrorCode::ExternalFailure, "Directory root cannot be resolved."));
		}

		const core::WideStringView text = path.View();
		const core::usize rootLength = RootPathSize(text);

		for (rox::core::usize offset = rootLength; offset <= text.size(); ++offset)
		{
			if (offset != text.size() && !IsPathSeparator(text[offset]))
			{
				continue;
			}

			WindowsPath prefix(memory);
			if (!prefix.Assign(text.substr(0, offset)))
			{
				return Result::Failure(failure(core::EErrorCode::ExternalFailure, "Directory inventory allocation failed."));
			}
			const DWORD attributes = GetFileAttributesW(prefix.CStr());

			if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
				(attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			{
				return Result::Failure(
					failure(core::EErrorCode::Conflict, "Directory root is unavailable or crosses a reparse point."));
			}
		}

		core::Array<core::String> output(memory);
		core::uint32 entries = 0;
		core::Result<void> read = scanRecursive(path, {}, output, entries, maximum, depth, cancellation);

		if (!read)
		{
			return Result::Failure(read.GetError());
		}

		rox::core::Sort(output.begin(), output.end(), [](const core::String& a, const core::String& b)
		{
			return a.View() < b.View();
		});

		return Result::Success(rox::core::Move(output));
	}

} // namespace rox::os
