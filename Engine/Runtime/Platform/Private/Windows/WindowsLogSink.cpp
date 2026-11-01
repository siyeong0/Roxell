#include <WindowsLogSink.h>
#include <Synchronization.hpp>
#include <Vocabulary.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include "WindowsPath.h"

#include <Assertion.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace rox::os
{
	namespace
	{
		constexpr rox::core::usize MAXIMUM_LOG_PATH_CHARACTERS = 1024;
		constexpr rox::core::usize MAXIMUM_DEBUG_LINE_BYTES = 8192;
	} // namespace

	WindowsLogSink::~WindowsLogSink()
	{
		Close();
	}

	bool WindowsLogSink::TryOpen(const core::StringView utf8Path) noexcept
	{
		if (IsOpen() || utf8Path.IsEmpty() || utf8Path.Size() > static_cast<rox::core::usize>(rox::core::NumericLimits<int>::max()))
		{
			return false;
		}

		rox::core::StaticArray<wchar_t, MAXIMUM_LOG_PATH_CHARACTERS> widePath = {};
		const int characterCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path.Data(), static_cast<int>(utf8Path.Size()),
			widePath.data(), static_cast<int>(widePath.size() - 1));

		if (characterCount <= 0)
		{
			return false;
		}

		widePath[static_cast<rox::core::usize>(characterCount)] = L'\0';

		const HANDLE file = CreateFileW(widePath.data(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);

		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		m_FileHandle = file;
		m_bWriteFailed.store(false, rox::core::MemoryOrderRelease);

		return true;
	}

	bool WindowsLogSink::TryOpenCreatingDirectories(const core::StringView utf8Path) noexcept
	{
		if (IsOpen() || utf8Path.IsEmpty() || utf8Path.Size() >= MAXIMUM_LOG_PATH_CHARACTERS ||
			utf8Path.Find('\0') != core::StringView::NPOS)
		{
			return false;
		}

		rox::core::StaticArray<wchar_t, MAXIMUM_LOG_PATH_CHARACTERS> supplied{}, full{};
		const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path.Data(), static_cast<int>(utf8Path.Size()),
			supplied.data(), static_cast<int>(supplied.size() - 1));

		if (size <= 0)
		{
			return false;
		}

		const DWORD count = GetFullPathNameW(supplied.data(), static_cast<DWORD>(full.size()), full.data(), nullptr);

		if (count == 0 || count >= full.size())
		{
			return false;
		}

		const core::usize root = RootPathSize({ full.data(), count });

		if (root == 0 || root >= count)
		{
			return false;
		}

		rox::core::usize begin = root;

		for (rox::core::usize index = root; index <= count; ++index)
		{
			if (index != count && full[index] == L':')
			{
				return false;
			}

			if (index != count && full[index] != L'\\' && full[index] != L'/')
			{
				continue;
			}

			if (index == begin || full[index - 1] == L'.' || full[index - 1] == L' ')
			{
				return false;
			}

			const wchar_t separator = full[index];
			full[index] = 0;
			DWORD attributes = GetFileAttributesW(full.data());

			if (attributes == INVALID_FILE_ATTRIBUTES && index != count)
			{
				const DWORD missing = GetLastError();

				if (missing != ERROR_FILE_NOT_FOUND && missing != ERROR_PATH_NOT_FOUND)
				{
					return false;
				}

				if (!CreateDirectoryW(full.data(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
				{
					return false;
				}

				attributes = GetFileAttributesW(full.data());
			}

			if (attributes != INVALID_FILE_ATTRIBUTES &&
				((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 || ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != (index != count)))
			{
				return false;
			}

			if (index != count && attributes == INVALID_FILE_ATTRIBUTES)
			{
				return false;
			}

			full[index] = separator;
			begin = index + 1;
		}

		const HANDLE file = CreateFileW(full.data(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		BY_HANDLE_FILE_INFORMATION info{};

		if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
		{
			CloseHandle(file);

			return false;
		}

		m_FileHandle = file;
		m_bWriteFailed.store(false, rox::core::MemoryOrderRelease);

		return true;
	}

	bool WindowsLogSink::TryOpenRelativeToExecutable(const core::StringView utf8RelativePath) noexcept
	{
		if (IsOpen() || utf8RelativePath.IsEmpty() ||
			utf8RelativePath.Size() > static_cast<rox::core::usize>(rox::core::NumericLimits<int>::max()))
		{
			return false;
		}

		rox::core::StaticArray<wchar_t, MAXIMUM_LOG_PATH_CHARACTERS> modulePath = {};
		const DWORD moduleLength = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));

		if (moduleLength == 0 || moduleLength >= static_cast<DWORD>(modulePath.size()))
		{
			return false;
		}

		rox::core::usize directoryLength = static_cast<rox::core::usize>(moduleLength);

		while (directoryLength > 0 && modulePath[directoryLength - 1] != L'\\' && modulePath[directoryLength - 1] != L'/')
		{
			--directoryLength;
		}

		if (directoryLength == 0)
		{
			return false;
		}

		rox::core::StaticArray<wchar_t, MAXIMUM_LOG_PATH_CHARACTERS> relativePath = {};
		const int relativeCount =
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8RelativePath.Data(), static_cast<int>(utf8RelativePath.Size()),
				relativePath.data(), static_cast<int>(relativePath.size() - 1));

		if (relativeCount <= 0)
		{
			return false;
		}

		const rox::core::usize relativeLength = static_cast<rox::core::usize>(relativeCount);

		if (directoryLength + relativeLength + 1 >= modulePath.size())
		{
			return false;
		}

		rox::core::StaticArray<wchar_t, MAXIMUM_LOG_PATH_CHARACTERS> fullPath = {};
		rox::core::CopyN(modulePath.data(), directoryLength, fullPath.data());
		rox::core::CopyN(relativePath.data(), relativeLength, fullPath.data() + directoryLength);
		const rox::core::usize fullLength = directoryLength + relativeLength;
		fullPath[fullLength] = L'\0';

		// Every intermediate directory is created in turn, because CreateFileW with
		// OPEN_ALWAYS creates a file and never a directory. An already-present
		// directory is a success, which is what makes a second run of a relocated
		// executable behave like the first.
		for (rox::core::usize index = directoryLength; index < fullLength; ++index)
		{
			if (fullPath[index] != L'\\' && fullPath[index] != L'/')
			{
				continue;
			}

			const wchar_t separator = fullPath[index];
			fullPath[index] = L'\0';
			const BOOL bCreated = CreateDirectoryW(fullPath.data(), nullptr);
			const DWORD createError = bCreated == FALSE ? GetLastError() : ERROR_SUCCESS;
			fullPath[index] = separator;

			if (bCreated == FALSE && createError != ERROR_ALREADY_EXISTS)
			{
				return false;
			}
		}

		const HANDLE file = CreateFileW(fullPath.data(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);

		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		m_FileHandle = file;
		m_bWriteFailed.store(false, rox::core::MemoryOrderRelease);

		return true;
	}

	void WindowsLogSink::Close() noexcept
	{
		lock();

		if (m_FileHandle != nullptr)
		{
			const HANDLE file = static_cast<HANDLE>(m_FileHandle);

			if (FlushFileBuffers(file) == FALSE)
			{
				m_bWriteFailed.store(true, rox::core::MemoryOrderRelease);
			}

			if (CloseHandle(file) == FALSE)
			{
				m_bWriteFailed.store(true, rox::core::MemoryOrderRelease);
			}

			m_FileHandle = nullptr;
		}

		unlock();
	}

	bool WindowsLogSink::IsOpen() const noexcept
	{
		return m_FileHandle != nullptr;
	}

	bool WindowsLogSink::HasWriteFailure() const noexcept
	{
		return m_bWriteFailed.load(rox::core::MemoryOrderAcquire);
	}

	void WindowsLogSink::Write(const core::LogEvent& event, const rox::core::DiagnosticText jsonLine) noexcept
	{
		ASSERT(!jsonLine.empty(), "Windows log sink requires a non-empty JSON record.");
		ASSERT(jsonLine.size() <= MAXIMUM_DEBUG_LINE_BYTES, "Core diagnostics must provide a bounded JSON record to the Windows sink.");
		ASSERT(jsonLine.back() == '\n', "Each structured log record must end with one newline.");
		rox::core::StaticArray<char, MAXIMUM_DEBUG_LINE_BYTES + 1> debugLine = {};
		const rox::core::usize debugByteCount = (rox::core::Min)(jsonLine.size(), MAXIMUM_DEBUG_LINE_BYTES);
		rox::core::CopyN(jsonLine.data(), debugByteCount, debugLine.data());
		debugLine[debugByteCount] = '\0';

		if (IsDebuggerPresent() != FALSE)
		{
			OutputDebugStringA(debugLine.data());
		}

		lock();

		if (m_FileHandle != nullptr)
		{
			DWORD bytesWritten = 0;
			const DWORD requestedBytes = static_cast<DWORD>(debugByteCount);

			if (WriteFile(static_cast<HANDLE>(m_FileHandle), debugLine.data(), requestedBytes, &bytesWritten, nullptr) == FALSE ||
				bytesWritten != requestedBytes)
			{
				m_bWriteFailed.store(true, rox::core::MemoryOrderRelease);
			}

			if (event.Record.Level >= core::ELogLevel::Error && FlushFileBuffers(static_cast<HANDLE>(m_FileHandle)) == FALSE)
			{
				m_bWriteFailed.store(true, rox::core::MemoryOrderRelease);
			}
		}

		unlock();
	}

	void WindowsLogSink::Flush() noexcept
	{
		lock();

		if (m_FileHandle != nullptr && FlushFileBuffers(static_cast<HANDLE>(m_FileHandle)) == FALSE)
		{
			m_bWriteFailed.store(true, rox::core::MemoryOrderRelease);
		}

		unlock();
	}

	void WindowsLogSink::lock() noexcept
	{
		while (m_OutputLock.test_and_set(rox::core::MemoryOrderAcquire))
		{
			YieldProcessor();
		}
	}

	void WindowsLogSink::unlock() noexcept
	{
		m_OutputLock.clear(rox::core::MemoryOrderRelease);
	}
} // namespace rox::os
