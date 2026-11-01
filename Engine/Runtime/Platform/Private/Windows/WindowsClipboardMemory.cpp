#include "WindowsClipboardMemory.h"

#include <Assertion.h>
#include <TextPrimitives.hpp>
#include <Utility.hpp>
#include <Vocabulary.hpp>

namespace rox::os
{
	using namespace core;

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
		: m_Handle(Exchange(other.m_Handle, nullptr))
		, m_bLocked(Exchange(other.m_bLocked, false))
	{
	}

	Result<WindowsClipboardMemory> WindowsClipboardMemory::Allocate(uint32 bytes)
	{
		if (bytes == 0 || bytes > 2 * (1'048'576 + 1))
		{
			return Result<WindowsClipboardMemory>::Failure(EErrorCode::InvalidArgument, "platform.window.clipboard", "Clipboard movable allocation exceeds its bound.");
		}

		WindowsClipboardMemory result;
		result.m_Handle = GlobalAlloc(GMEM_MOVEABLE, bytes);

		if (result.m_Handle == nullptr)
		{
			return Result<WindowsClipboardMemory>::Failure(makeMemoryError("GlobalAlloc", GetLastError()));
		}

		return Result<WindowsClipboardMemory>::Success(Move(result));
	}

	Result<void*> WindowsClipboardMemory::Lock()
	{
		ASSERT(m_Handle != nullptr && !m_bLocked, "Lock requires one unlocked owned clipboard allocation.");
		void* const view = GlobalLock(m_Handle);

		if (view == nullptr)
		{
			return Result<void*>::Failure(makeMemoryError("GlobalLock", GetLastError()));
		}

		m_bLocked = true;

		return Result<void*>::Success(view);
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

	Error WindowsClipboardMemory::makeMemoryError(const char* operation, DWORD error)
	{
		char message[128];
		FormatText(message, sizeof(message), "%s failed (Windows error %lu).", operation, error);

		return MakeError(EErrorCode::ExternalFailure, "platform.window.clipboard", StringView(message, CStringLength(message)));
	}
}
