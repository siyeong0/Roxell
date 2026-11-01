#include "WindowsClipboardMemory.h"

#include <Assertion.h>
#include <TextPrimitives.hpp>
#include <Utility.hpp>
#include <Vocabulary.hpp>

namespace rox::os
{
	WindowsClipboardMemory::~WindowsClipboardMemory() noexcept
	{
		if (m_bLocked)
		{
			Unlock();
		}

		if (m_Handle != nullptr)
		{
			ASSERT(GlobalFree(m_Handle) == nullptr, "Owned clipboard allocation must be released.");
		}
	}

	WindowsClipboardMemory::WindowsClipboardMemory(WindowsClipboardMemory&& other) noexcept
		: m_Handle(rox::core::Exchange(other.m_Handle, nullptr))
		, m_bLocked(rox::core::Exchange(other.m_bLocked, false))
	{
	}

	core::Result<WindowsClipboardMemory> WindowsClipboardMemory::Allocate(core::uint32 bytes)
	{
		if (bytes == 0 || bytes > 2 * (1'048'576 + 1))
		{
			return core::Result<WindowsClipboardMemory>::Failure(core::EErrorCode::InvalidArgument, "platform.window.clipboard", "Clipboard movable allocation exceeds its bound.");
		}

		WindowsClipboardMemory result;
		result.m_Handle = GlobalAlloc(GMEM_MOVEABLE, bytes);

		if (result.m_Handle == nullptr)
		{
			return core::Result<WindowsClipboardMemory>::Failure(makeMemoryError("GlobalAlloc", GetLastError()));
		}

		return core::Result<WindowsClipboardMemory>::Success(rox::core::Move(result));
	}

	core::Result<void*> WindowsClipboardMemory::Lock()
	{
		ASSERT(m_Handle != nullptr && !m_bLocked, "Lock requires one unlocked owned clipboard allocation.");
		void* const view = GlobalLock(m_Handle);

		if (view == nullptr)
		{
			return core::Result<void*>::Failure(makeMemoryError("GlobalLock", GetLastError()));
		}

		m_bLocked = true;

		return core::Result<void*>::Success(view);
	}

	void WindowsClipboardMemory::Unlock() noexcept
	{
		ASSERT(m_Handle != nullptr && m_bLocked, "Unlock requires a locked owned clipboard allocation.");
		SetLastError(ERROR_SUCCESS);
		const BOOL bRemainsLocked = GlobalUnlock(m_Handle);
		ASSERT(bRemainsLocked == FALSE && GetLastError() == ERROR_SUCCESS, "Owned clipboard allocation must unlock exactly once.");
		m_bLocked = false;
	}

	void WindowsClipboardMemory::ReleaseToWindows() noexcept
	{
		ASSERT(m_Handle != nullptr && !m_bLocked, "Clipboard publication transfers unlocked owned storage.");
		m_Handle = nullptr;
	}

	core::Error WindowsClipboardMemory::makeMemoryError(const char* operation, DWORD error)
	{
		char message[128];
		rox::core::FormatText(message, sizeof(message), "%s failed (Windows error %lu).", operation, error);

		return core::MakeError(core::EErrorCode::ExternalFailure, "platform.window.clipboard", core::StringView(message, rox::core::CStringLength(message)));
	}
}
