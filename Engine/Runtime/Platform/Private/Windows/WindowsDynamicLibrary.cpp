#include <WindowsDynamicLibrary.h>
#include <Algorithms.hpp>
#include <Types.h>
#include <ByteOperations.hpp>

#include <Array.hpp>
#include <Log.h>
#include <UniquePtr.hpp>
#include <Utf8.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <TextPrimitives.hpp>

namespace rox::os
{
	namespace
	{
		struct WindowsLibrary final
		{
		public:
			HMODULE Handle = nullptr;
		};

		core::Error error(core::EErrorCode code, core::StringView id, core::StringView message) noexcept
		{
			return core::MakeError(code, id, message);
		}

		core::Result<DynamicLibrarySymbol> find(void* object, core::StringView name) noexcept
		{
			char terminated[257] = {};
			rox::core::CopyBytes(terminated, name.Data(), name.Size());
			const FARPROC symbol = GetProcAddress(static_cast<WindowsLibrary*>(object)->Handle, terminated);

			if (symbol == nullptr)
			{
				return core::Result<DynamicLibrarySymbol>::Failure(error(core::EErrorCode::NotFound, "ROX-PLATFORM-LIBRARY-EXPORT", "The requested named export was not found."));
			}

			return core::Result<DynamicLibrarySymbol>::Success(reinterpret_cast<DynamicLibrarySymbol>(symbol));
		}

		core::Result<void> close(void* object) noexcept
		{
			WindowsLibrary& library = *static_cast<WindowsLibrary*>(object);

			if (!FreeLibrary(library.Handle))
			{
				return core::Result<void>::Failure(error(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-LIBRARY-CLOSE", "FreeLibrary failed; the library owner is retained."));
			}

			library.Handle = nullptr;

			return core::Result<void>::Success();
		}

		bool isSeparator(char value) noexcept
		{
			return value == '/' || value == '\\';
		}

		bool isAbsolute(core::StringView path) noexcept
		{
			if (path.Size() >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' && isSeparator(path[2]))
			{
				return true;
			}
			// UNC requires a server, share, and file. Device namespace paths are outside this profile.
			if (path.Size() < 5 || !isSeparator(path[0]) || !isSeparator(path[1]) || path[2] == '?' || path[2] == '.' ||
				isSeparator(path[2]))
			{
				return false;
			}

			rox::core::usize separator = 2;

			while (separator < path.Size() && !isSeparator(path[separator]))
			{
				++separator;
			}

			if (++separator >= path.Size() || isSeparator(path[separator]))
			{
				return false;
			}
			while (separator < path.Size() && !isSeparator(path[separator]))
			{
				++separator;
			}

			return separator + 1 < path.Size();
		}
	} // namespace

	core::Result<DynamicLibraryOwner> LoadWindowsDynamicLibrary(core::MemoryResource& resource, const core::StringView absolutePathUtf8) noexcept
	{
		if (absolutePathUtf8.IsEmpty() ||
			absolutePathUtf8.Size() > 32767 ||
			!core::IsValidUtf8(absolutePathUtf8) ||
			!isAbsolute(absolutePathUtf8) ||
			rox::core::Find(absolutePathUtf8.begin(), absolutePathUtf8.end(), '\0') != absolutePathUtf8.end())
		{
			return core::Result<DynamicLibraryOwner>::Failure(error(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-LIBRARY-PATH", "Library loading requires an absolute, bounded UTF-8 file path."));
		}

		core::Array<wchar_t> path(resource);

		if (!path.Resize(absolutePathUtf8.Size() + 1))
		{
			return core::Result<DynamicLibraryOwner>::Failure(error(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-LIBRARY-ALLOCATION", "Library path allocation failed."));
		}

		const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, absolutePathUtf8.Data(), static_cast<int>(absolutePathUtf8.Size()), path.Data(), static_cast<int>(path.Size() - 1));

		if (length == 0)
		{
			return core::Result<DynamicLibraryOwner>::Failure(error(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-LIBRARY-UTF8", "Library path conversion failed."));
		}

		path[static_cast<rox::core::usize>(length)] = L'\0';
		rox::core::Replace(path.begin(), path.end(), L'/', L'\\');
		WindowsLibrary* const library = core::CreateObject<WindowsLibrary>(resource);

		if (library == nullptr)
		{
			return core::Result<DynamicLibraryOwner>::Failure(error(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-LIBRARY-ALLOCATION", "Library owner allocation failed."));
		}

		library->Handle = LoadLibraryExW(path.Data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);

		if (library->Handle == nullptr)
		{
			core::DestroyObject(resource, library);

			return core::Result<DynamicLibraryOwner>::Failure(error(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-LIBRARY-LOAD", "The explicit library or one of its dependencies could not load."));
		}

		LOG_INFO("Platform.Library", "Loaded an explicitly selected native library.");

		return core::Result<DynamicLibraryOwner>::Success(
			DynamicLibraryOwner::Adopt(resource, library, &find, &close, [](core::MemoryResource& allocationResource, void* object) noexcept
				{ core::DestroyObject(allocationResource, static_cast<WindowsLibrary*>(object)); }));
	}
} // namespace rox::os
