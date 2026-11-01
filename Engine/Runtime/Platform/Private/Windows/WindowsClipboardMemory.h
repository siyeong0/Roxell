#pragma once

#include <Result.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace rox::os
{
	// CF_UNICODETEXT storage; ownership transfers to Windows after publication.
	class WindowsClipboardMemory final
	{
	public:
		WindowsClipboardMemory() noexcept = default;
		~WindowsClipboardMemory() noexcept;

		WindowsClipboardMemory(const WindowsClipboardMemory&) = delete;
		WindowsClipboardMemory& operator=(const WindowsClipboardMemory&) = delete;
		WindowsClipboardMemory(WindowsClipboardMemory&& other) noexcept;
		WindowsClipboardMemory& operator=(WindowsClipboardMemory&&) = delete;

		[[nodiscard]] static core::Result<WindowsClipboardMemory> Allocate(core::uint32 bytes);

		[[nodiscard]] core::Result<void*> Lock();
		void Unlock() noexcept;

		[[nodiscard]] HGLOBAL Get() const noexcept { return m_Handle; }
		void ReleaseToWindows() noexcept;

	private:
		static core::Error makeMemoryError(const char* operation, DWORD error);

	private:
		HGLOBAL m_Handle = nullptr;
		bool m_bLocked = false;
	};
}
