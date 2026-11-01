#include "WindowsPath.h"
#include <TextPrimitives.hpp>
#include <SystemError.hpp>
#include <StringView.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include <Utf8.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <Algorithms.hpp>

namespace rox::os
{
	namespace
	{
		core::Error pathAllocationError() noexcept
		{
			return core::MakeError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-PATH-ALLOCATION",
				"The filesystem operation could not allocate temporary storage.",
				"Release memory and retry the filesystem operation.");
		}

		void setError(rox::core::SystemErrorCode& error, const DWORD code) noexcept
		{
			error = rox::core::SystemErrorCode(static_cast<int>(code), rox::core::SystemCategory());
		}

		bool isMissing(const DWORD error) noexcept
		{
			return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME ||
				error == ERROR_BAD_NETPATH || error == ERROR_DIRECTORY || error == ERROR_NETNAME_DELETED;
		}

		bool isDrive(const rox::core::WideStringView path) noexcept
		{
			return path.size() >= 2 && path[1] == L':' && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'));
		}

		bool queryAttributes(const WindowsPath& path, WIN32_FILE_ATTRIBUTE_DATA& data, rox::core::SystemErrorCode& error) noexcept
		{
			if (GetFileAttributesExW(path.CStr(), GetFileExInfoStandard, &data))
			{
				return true;
			}

			const DWORD code = GetLastError();

			if (code != ERROR_SHARING_VIOLATION)
			{
				setError(error, code);

				return false;
			}
			// Match the existing filesystem query fallback when metadata access conflicts with an open handle.
			WIN32_FIND_DATAW found{};
			const HANDLE handle = FindFirstFileW(path.CStr(), &found);

			if (handle == INVALID_HANDLE_VALUE)
			{
				setError(error, GetLastError());

				return false;
			}

			const BOOL closed = FindClose(handle);
			ASSERT(closed != FALSE, "A filesystem metadata search handle must close successfully.");
			data.dwFileAttributes = found.dwFileAttributes;
			data.nFileSizeHigh = found.nFileSizeHigh;
			data.nFileSizeLow = found.nFileSizeLow;

			return true;
		}

		class PathHandle final
		{
		public:
			HANDLE Value;

		public:
			explicit PathHandle(const wchar_t* path) noexcept
				: Value(CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
					OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr))
			{
			}

			~PathHandle() noexcept
			{
				if (Value != INVALID_HANDLE_VALUE)
				{
					const BOOL closed = CloseHandle(Value);
					ASSERT(closed != FALSE, "A filesystem query handle must close successfully.");
				}
			}

			PathHandle(const PathHandle&) = delete;
			PathHandle& operator=(const PathHandle&) = delete;
		};

		core::Result<WindowsPath> canonicalPath(const WindowsPath& input, rox::core::SystemErrorCode& error)
		{
			WindowsPath result(input.Resource());
			PathHandle handle(input.CStr());

			if (handle.Value == INVALID_HANDLE_VALUE)
			{
				setError(error, GetLastError());

				return core::Result<WindowsPath>::Success(rox::core::Move(result));
			}

			DWORD kind = VOLUME_NAME_DOS;
			if (!result.Resize(MAX_PATH))
			{
				return core::Result<WindowsPath>::Failure(pathAllocationError());
			}

			for (;;)
			{
				const DWORD capacity = static_cast<DWORD>(result.Size() + 1);
				const DWORD size = GetFinalPathNameByHandleW(handle.Value, result.Data(), capacity, kind);

				if (size == 0)
				{
					const DWORD code = GetLastError();

					if (code == ERROR_PATH_NOT_FOUND && kind == VOLUME_NAME_DOS)
					{
						kind = VOLUME_NAME_NT;
						continue;
					}

					setError(error, code);
					if (!result.Resize(0))
					{
						return core::Result<WindowsPath>::Failure(pathAllocationError());
					}

					return core::Result<WindowsPath>::Success(rox::core::Move(result));
				}

				if (!result.Resize(size))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}

				if (size < capacity)
				{
					break;
				}
			}

			WindowsPath normalized(input.Resource());
			const core::WideStringView text = result.View();

			if (kind == VOLUME_NAME_NT)
			{
				if (!normalized.Assign(L"\\\\?\\GLOBALROOT"))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}
				if (!normalized.Append(text))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}
			}
			else if (text.starts_with(L"\\\\?\\") && isDrive(text.substr(4)))
			{
				if (!normalized.Assign(text.substr(4)))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}
			}
			else if (text.starts_with(L"\\\\?\\UNC\\"))
			{
				if (!normalized.Assign(L"\\\\"))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}
				if (!normalized.Append(text.substr(8)))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}
			}
			else
			{
				normalized = rox::core::Move(result);
			}

			error.clear();

			return core::Result<WindowsPath>::Success(rox::core::Move(normalized));
		}

		core::Result<WindowsPath> lexicalNormal(const WindowsPath& input)
		{
			const core::WideStringView text = input.View();
			WindowsPath result(input.Resource());
			const rox::core::usize rootName = RootNameSize(text);
			if (!result.Assign(text.substr(0, rootName)))
			{
				return core::Result<WindowsPath>::Failure(pathAllocationError());
			}

			for (rox::core::usize index = 0; index < result.Size(); ++index)
			{
				if (IsPathSeparator(result.Data()[index]))
				{
					result.Data()[index] = L'\\';
				}
			}

			const rox::core::usize relative = RootPathSize(text);
			const bool bRooted = relative > rootName;

			if (bRooted)
			{
				if (!result.Append(L"\\"))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}
			}

			rox::core::usize cursor = relative;

			while (cursor < text.size())
			{
				const rox::core::usize start = cursor;

				while (cursor < text.size() && !IsPathSeparator(text[cursor]))
				{
					++cursor;
				}

				const core::WideStringView component = text.substr(start, cursor - start);
				const bool bTrailing = cursor < text.size();

				while (cursor < text.size() && IsPathSeparator(text[cursor]))
				{
					++cursor;
				}

				if (component == L".")
				{
					if (cursor == text.size() && !result.IsEmpty() && !IsPathSeparator(result.View().back()))
					{
						if (!result.Append(L"\\"))
						{
							return core::Result<WindowsPath>::Failure(pathAllocationError());
						}
					}

					continue;
				}

				if (component == L"..")
				{
					const core::WideStringView current = result.View();
					core::Result<WindowsPath> parentResult = result.Parent();

					if (!parentResult)
					{
						return core::Result<WindowsPath>::Failure(parentResult.GetError());
					}

					const WindowsPath& parent = parentResult.Value();
					const core::WideStringView last =
						current.substr(parent.Size() + (parent.Size() < current.size() && IsPathSeparator(current[parent.Size()]) ? 1 : 0));

					if (current.size() > RootPathSize(current) && last != L"..")
					{
						if (!result.Resize(parent.Size()))
						{
							return core::Result<WindowsPath>::Failure(pathAllocationError());
						}

						if (cursor == text.size() && !result.IsEmpty() && !IsPathSeparator(result.View().back()))
						{
							if (!result.Append(L"\\"))
							{
								return core::Result<WindowsPath>::Failure(pathAllocationError());
							}
						}
					}
					else if (!bRooted)
					{
						if (!result.AppendComponent(component))
						{
							return core::Result<WindowsPath>::Failure(pathAllocationError());
						}
					}
				}
				else
				{
					if (!result.AppendComponent(component))
					{
						return core::Result<WindowsPath>::Failure(pathAllocationError());
					}
				}

				if (bTrailing && cursor == text.size() && component != L".." && !result.IsEmpty() && !IsPathSeparator(result.View().back()))
				{
					if (!result.Append(L"\\"))
					{
						return core::Result<WindowsPath>::Failure(pathAllocationError());
					}
				}
			}

			if (result.IsEmpty())
			{
				if (!result.Assign(L"."))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}
			}

			return core::Result<WindowsPath>::Success(rox::core::Move(result));
		}
	}

	WindowsPath& WindowsPath::operator=(WindowsPath&& other) noexcept
	{
		ASSERT(&Resource() == &other.Resource(), "Path move assignment must retain the same scratch resource.");
		m_Text = rox::core::Move(other.m_Text);

		return *this;
	}

	bool WindowsPath::Resize(const rox::core::usize size)
	{
		if (size >= (rox::core::NumericLimits<DWORD>::max)() || !m_Text.Resize(size + 1))
		{
			return false;
		}

		m_Text[size] = L'\0';

		return true;
	}

	bool WindowsPath::Assign(const rox::core::WideStringView text)
	{
		if (!Resize(text.size()))
		{
			return false;
		}

		if (!text.empty())
		{
			rox::core::CopyBytes(Data(), text.data(), text.size() * sizeof(wchar_t));
		}

		return true;
	}

	bool WindowsPath::Append(const rox::core::WideStringView text)
	{
		const rox::core::usize size = Size();

		if (text.size() > (rox::core::NumericLimits<DWORD>::max)() - size - 1)
		{
			return false;
		}

		if (!Resize(size + text.size()))
		{
			return false;
		}

		if (!text.empty())
		{
			rox::core::CopyBytes(Data() + size, text.data(), text.size() * sizeof(wchar_t));
		}

		return true;
	}

	bool WindowsPath::AppendComponent(const rox::core::WideStringView text)
	{
		if (!IsEmpty() && !IsPathSeparator(View().back()) && Size() > RootNameSize(View()))
		{
			if (!Append(L"\\"))
			{
				return false;
			}
		}

		return Append(text);
	}

	core::Result<WindowsPath> WindowsPath::Parent() const
	{
		rox::core::usize end = Size();
		const core::WideStringView text = View();
		const core::usize root = RootPathSize(text);

		while (end > root && !IsPathSeparator(text[end - 1]))
		{
			--end;
		}
		while (end > root && IsPathSeparator(text[end - 1]))
		{
			--end;
		}

		WindowsPath result(Resource());
		if (!result.Assign(text.substr(0, end)))
		{
			return core::Result<WindowsPath>::Failure(pathAllocationError());
		}

		return core::Result<WindowsPath>::Success(rox::core::Move(result));
	}

	bool IsPathSeparator(const wchar_t character) noexcept
	{
		return character == L'\\' || character == L'/';
	}

	bool IsMissingPathError(const rox::core::SystemErrorCode& error) noexcept
	{
		return error.category() == rox::core::SystemCategory() && isMissing(static_cast<DWORD>(error.value()));
	}

	rox::core::usize RootNameSize(const rox::core::WideStringView path) noexcept
	{
		if (isDrive(path))
		{
			return 2;
		}

		if (path.size() < 2 || !IsPathSeparator(path[0]))
		{
			return 0;
		}

		if (path.size() >= 4 && IsPathSeparator(path[3]) && (path.size() == 4 || !IsPathSeparator(path[4])) &&
			((IsPathSeparator(path[1]) && (path[2] == L'?' || path[2] == L'.')) || (path[1] == L'?' && path[2] == L'?')))
		{
			return 3;
		}

		if (path.size() >= 3 && IsPathSeparator(path[1]) && !IsPathSeparator(path[2]))
		{
			rox::core::usize end = 3;

			while (end < path.size() && !IsPathSeparator(path[end]))
			{
				++end;
			}

			return end;
		}

		return 0;
	}

	rox::core::usize RootPathSize(const rox::core::WideStringView path) noexcept
	{
		rox::core::usize end = RootNameSize(path);

		while (end < path.size() && IsPathSeparator(path[end]))
		{
			++end;
		}

		return end;
	}

	bool IsAbsolutePath(const rox::core::WideStringView path) noexcept
	{
		return isDrive(path) ? path.size() >= 3 && IsPathSeparator(path[2]) : RootNameSize(path) != 0;
	}

	bool IsWithinRoot(const WindowsPath& root, const WindowsPath& candidate, const bool bWindowsCase) noexcept
	{
		ASSERT(IsAbsolutePath(root.View()) && IsAbsolutePath(candidate.View()), "Containment requires absolute paths.");
		const core::WideStringView a = root.View();
		const core::WideStringView b = candidate.View();
		rox::core::usize i = 0;
		rox::core::usize j = 0;

		while (i < a.size())
		{
			rox::core::usize ai = i;
			rox::core::usize bj = j;

			if (i == 0 && RootNameSize(a) != 0)
			{
				i = RootNameSize(a);
				j = RootNameSize(b);
			}
			else
			{
				while (i < a.size() && IsPathSeparator(a[i]))
				{
					++i;
				}
				while (j < b.size() && IsPathSeparator(b[j]))
				{
					++j;
				}

				if (i == a.size())
				{
					return i == RootPathSize(a) || bWindowsCase || j == b.size();
				}

				ai = i;
				bj = j;

				while (i < a.size() && !IsPathSeparator(a[i]))
				{
					++i;
				}
				while (j < b.size() && !IsPathSeparator(b[j]))
				{
					++j;
				}
			}

			const core::WideStringView left = a.substr(ai, i - ai);
			const core::WideStringView right = b.substr(bj, j - bj);

			if (bWindowsCase ? CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()),
				TRUE) != CSTR_EQUAL
				: left != right)
			{
				return false;
			}
		}

		return true;
	}

	core::Result<WindowsPath> PathFromUtf8(const rox::core::StringView text, core::MemoryResource& resource)
	{
		ASSERT(!text.empty() && core::IsValidUtf8(core::StringView(text)), "Path conversion requires nonempty valid UTF-8.");

		if (text.size() > static_cast<rox::core::usize>((rox::core::NumericLimits<int>::max)()))
		{
			return core::Result<WindowsPath>::Failure(pathAllocationError());
		}

		const int length = static_cast<int>(text.size());
		const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
		ASSERT(count > 0, "Validated UTF-8 must have a positive UTF-16 length.");
		WindowsPath path(resource);
		if (!path.Resize(static_cast<rox::core::usize>(count)))
		{
			return core::Result<WindowsPath>::Failure(pathAllocationError());
		}
		const int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, path.Data(), count);
		ASSERT(converted == count, "Validated UTF-8 conversion must fill the measured UTF-16 range.");

		return core::Result<WindowsPath>::Success(rox::core::Move(path));
	}

	core::Result<WindowsPath> AbsolutePath(const WindowsPath& input, rox::core::SystemErrorCode& error)
	{
		WindowsPath result(input.Resource());
		if (!result.Resize(MAX_PATH))
		{
			return core::Result<WindowsPath>::Failure(pathAllocationError());
		}

		for (;;)
		{
			const DWORD capacity = static_cast<DWORD>(result.Size() + 1);
			const DWORD size = GetFullPathNameW(input.CStr(), capacity, result.Data(), nullptr);

			if (size == 0)
			{
				setError(error, GetLastError());
				if (!result.Resize(0))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}

				return core::Result<WindowsPath>::Success(rox::core::Move(result));
			}

			if (!result.Resize(size))
			{
				return core::Result<WindowsPath>::Failure(pathAllocationError());
			}

			if (size < capacity)
			{
				error.clear();

				return core::Result<WindowsPath>::Success(rox::core::Move(result));
			}
		}
	}

	core::Result<WindowsPath> WeaklyCanonicalPath(const WindowsPath& input, rox::core::SystemErrorCode& error)
	{
		core::Result<WindowsPath> initial = canonicalPath(input, error);

		if (!initial)
		{
			return core::Result<WindowsPath>::Failure(initial.GetError());
		}

		WindowsPath result = rox::core::Move(initial).TakeValue();

		if (!error || !isMissing(static_cast<DWORD>(error.value())))
		{
			return core::Result<WindowsPath>::Success(rox::core::Move(result));
		}

		core::Result<WindowsPath> normal = lexicalNormal(input);

		if (!normal)
		{
			return core::Result<WindowsPath>::Failure(normal.GetError());
		}

		const WindowsPath& normalized = normal.Value();
		const core::WideStringView text = normalized.View();
		rox::core::usize cursor = RootPathSize(text);
		if (!result.Assign(text.substr(0, cursor)))
		{
			return core::Result<WindowsPath>::Failure(pathAllocationError());
		}
		bool bCanonicalize = true;

		while (cursor < text.size())
		{
			const rox::core::usize start = cursor;

			while (cursor < text.size() && !IsPathSeparator(text[cursor]))
			{
				++cursor;
			}

			if (!result.AppendComponent(text.substr(start, cursor - start)))
			{
				return core::Result<WindowsPath>::Failure(pathAllocationError());
			}

			if (bCanonicalize)
			{
				core::Result<WindowsPath> resolvedResult = canonicalPath(result, error);

				if (!resolvedResult)
				{
					return core::Result<WindowsPath>::Failure(resolvedResult.GetError());
				}

				WindowsPath resolved = rox::core::Move(resolvedResult).TakeValue();

				if (!error)
				{
					result = rox::core::Move(resolved);
				}
				else if (isMissing(static_cast<DWORD>(error.value())))
				{
					bCanonicalize = false;
					error.clear();
				}
				else
				{
					return core::Result<WindowsPath>::Success(rox::core::Move(result));
				}
			}
			while (cursor < text.size() && IsPathSeparator(text[cursor]))
			{
				++cursor;
			}

			if (cursor == text.size() && !text.empty() && IsPathSeparator(text.back()))
			{
				if (!result.AppendComponent({}))
				{
					return core::Result<WindowsPath>::Failure(pathAllocationError());
				}

				if (bCanonicalize)
				{
					core::Result<WindowsPath> resolvedResult = canonicalPath(result, error);

					if (!resolvedResult)
					{
						return core::Result<WindowsPath>::Failure(resolvedResult.GetError());
					}

					WindowsPath resolved = rox::core::Move(resolvedResult).TakeValue();

					if (!error)
					{
						result = rox::core::Move(resolved);
					}
					else if (isMissing(static_cast<DWORD>(error.value())))
					{
						error.clear();
					}
					else
					{
						return core::Result<WindowsPath>::Success(rox::core::Move(result));
					}
				}
			}
		}

		error.clear();

		return core::Result<WindowsPath>::Success(rox::core::Move(result));
	}

	core::Result<void> CreatePathDirectories(const WindowsPath& path, rox::core::SystemErrorCode& error)
	{
		WindowsPath prefix(path.Resource());
		const core::WideStringView text = path.View();
		rox::core::usize cursor = RootPathSize(text);

		if (cursor != 0 && isDrive(text.substr(cursor)) && text.size() >= cursor + 3 && IsPathSeparator(text[cursor + 2]))
		{
			cursor += 2;
		}

		if (!prefix.Assign(text.substr(0, cursor)))
		{
			return core::Result<void>::Failure(pathAllocationError());
		}
		DWORD last = ERROR_SUCCESS, meaningful = ERROR_SUCCESS;

		while (cursor < text.size())
		{
			const rox::core::usize start = cursor;

			while (cursor < text.size() && IsPathSeparator(text[cursor]))
			{
				++cursor;
			}
			while (cursor < text.size() && !IsPathSeparator(text[cursor]))
			{
				++cursor;
			}

			if (!prefix.Append(text.substr(start, cursor - start)))
			{
				return core::Result<void>::Failure(pathAllocationError());
			}
			last = CreateDirectoryW(prefix.CStr(), nullptr) ? ERROR_SUCCESS : GetLastError();

			if (last == ERROR_ALREADY_EXISTS)
			{
				rox::core::SystemErrorCode statusError;
				const DWORD attributes = PathAttributes(prefix, statusError);

				if (statusError)
				{
					last = static_cast<DWORD>(statusError.value());
				}
				else if (attributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					last = ERROR_SUCCESS;
				}
			}

			if (last != ERROR_SUCCESS && !isMissing(last))
			{
				meaningful = last;
			}
		}

		setError(error, last != ERROR_SUCCESS && meaningful != ERROR_SUCCESS ? meaningful : last);

		return core::Result<void>::Success();
	}

	unsigned long PathAttributes(const WindowsPath& path, rox::core::SystemErrorCode& error) noexcept
	{
		WIN32_FILE_ATTRIBUTE_DATA data{};

		if (!queryAttributes(path, data, error))
		{
			return INVALID_FILE_ATTRIBUTES;
		}

		DWORD attributes = data.dwFileAttributes;

		if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
		{
			PathHandle handle(path.CStr());

			if (handle.Value == INVALID_HANDLE_VALUE)
			{
				setError(error, GetLastError());

				return INVALID_FILE_ATTRIBUTES;
			}

			FILE_BASIC_INFO info{};

			if (!GetFileInformationByHandleEx(handle.Value, FileBasicInfo, &info, sizeof(info)))
			{
				setError(error, GetLastError());

				return INVALID_FILE_ATTRIBUTES;
			}

			attributes = info.FileAttributes;
		}

		error.clear();

		return attributes;
	}

	core::uint64 PathFileSize(const WindowsPath& path, rox::core::SystemErrorCode& error) noexcept
	{
		WIN32_FILE_ATTRIBUTE_DATA data{};

		if (!queryAttributes(path, data, error))
		{
			return 0;
		}

		if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		{
			PathHandle handle(path.CStr());
			FILE_STANDARD_INFO info{};

			if (handle.Value == INVALID_HANDLE_VALUE || !GetFileInformationByHandleEx(handle.Value, FileStandardInfo, &info, sizeof(info)))
			{
				setError(error, GetLastError());

				return 0;
			}

			error.clear();

			return static_cast<core::uint64>(info.EndOfFile.QuadPart);
		}

		error.clear();

		return (static_cast<core::uint64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
	}
}
