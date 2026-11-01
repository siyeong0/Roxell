#include <WindowsThreadBackend.h>
#include <Types.h>
#include <Assertion.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <process.h>

#include <NativeRuntime.hpp>

namespace rox::os
{
	using namespace core;

	namespace
	{
		unsigned __stdcall runThread(void* context) noexcept
		{
			const NativeThreadEntry& entry = *static_cast<const NativeThreadEntry*>(context);
			entry.Function(entry.Context);

			return 0;
		}
	}

	NativeThreadStartResult WindowsThreadBackend::Start(NativeThreadEntry& entry) noexcept
	{
		ASSERT(entry.Function != nullptr, "Native thread entry requires a function.");
		unsigned identifier = 0;
		// The entry remains at its borrowed address until Join completes.
		const uintptr_t handle = _beginthreadex(nullptr, 0, runThread, &entry, 0, &identifier);

		if (handle == 0)
		{
			const uint32 error = static_cast<uint32>(NativeErrorNumber());

			return { {}, {EThreadStartStatus::NativeFailure, error} };
		}

		return { {reinterpret_cast<void*>(handle), {identifier}}, {EThreadStartStatus::Started, 0} };
	}

	void WindowsThreadBackend::Join(const NativeThread thread) noexcept
	{
		ASSERT(thread.Handle != nullptr && thread.Id.Value != 0, "Native thread join requires an owned handle and identifier.");

		if (thread.Id.Value == GetCurrentThreadId())
		{
			// A terminated thread's identifier may already belong to the caller.
			const DWORD completed = WaitForSingleObject(thread.Handle, 0);
			ASSERT(completed == WAIT_OBJECT_0, "A native thread cannot join itself.");
		}

		const DWORD waited = WaitForSingleObject(thread.Handle, INFINITE);
		ASSERT(waited == WAIT_OBJECT_0, "Native thread join must observe complete termination.");
		const BOOL closed = CloseHandle(thread.Handle);
		ASSERT(closed != FALSE, "Native thread join must close its owned handle.");
	}

	ThreadId WindowsThreadBackend::CurrentThreadId() const noexcept
	{
		return { GetCurrentThreadId() };
	}
}
