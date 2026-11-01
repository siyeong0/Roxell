#include <WindowsConsoleCancellation.h>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>
#include <Cancellation.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>


int main()
{
	if (GetConsoleWindow() == nullptr)
	{
		return 10;
	}

	rox::core::CancellationSource source;
	if (!source.IsValid())
	{
		return 11;
	}

	int result = 0;
	{
		rox::os::WindowsConsoleCancellation subscription;
		if (!subscription.Start(source))
		{
			return 12;
		}
		if (source.GetToken().IsCancellationRequested())
		{
			return 13;
		}
		if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0))
		{
			return 14;
		}
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!source.GetToken().IsCancellationRequested() && rox::core::MonotonicClock::now() < deadline)
		{
			rox::core::YieldThread();
		}
		if (!source.GetToken().IsCancellationRequested())
		{
			result = 15;
		}
	}

	if (!FreeConsole())
	{
		return 16;
	}
	return result;
}
