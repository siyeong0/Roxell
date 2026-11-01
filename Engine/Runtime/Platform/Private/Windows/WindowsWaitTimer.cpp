#include <WindowsWaitTimer.h>
#include <Utility.hpp>
#include <Assertion.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace rox::os
{
	using namespace core;

	Result<WindowsWaitTimer> WindowsWaitTimer::Create()
	{
		HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);

		if (!handle)
		{
			return Result<WindowsWaitTimer>::Failure(EErrorCode::ExternalFailure, "ROX-PLATFORM-WAIT-TIMER", "Windows could not create a high-resolution wait timer.");
		}

		return Result<WindowsWaitTimer>::Success(WindowsWaitTimer(handle));
	}

	WindowsWaitTimer::WindowsWaitTimer(WindowsWaitTimer&& other) noexcept
		: m_Handle(Exchange(other.m_Handle, nullptr))
	{
	}

	WindowsWaitTimer::~WindowsWaitTimer()
	{
		if (m_Handle)
		{
			CloseHandle(m_Handle);
		}
	}

	Result<void> WindowsWaitTimer::Wait(uint32 milliseconds)
	{
		ASSERT(m_Handle, "A wait timer must retain its native handle.");

		if (milliseconds == 0)
		{
			return Result<void>::Success();
		}

		LARGE_INTEGER due{};
		due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10'000;

		if (!SetWaitableTimerEx(m_Handle, &due, 0, nullptr, nullptr, nullptr, 0) ||
			WaitForSingleObject(m_Handle, INFINITE) != WAIT_OBJECT_0)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "ROX-PLATFORM-WAIT-TIMER", "Windows could not arm or wait on the pacing timer.");
		}

		return Result<void>::Success();
	}
}
