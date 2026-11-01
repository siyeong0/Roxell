#include <WindowsClock.h>
#include <Utility.hpp>
#include <WindowsWaitTimer.h>

#include <gtest/gtest.h>


using namespace rox::core;
using namespace rox::os;

TEST(WindowsWaitTimerTests, ZeroWaitAndRepeatedArmingSucceed)
{
	Result<WindowsWaitTimer> created = WindowsWaitTimer::Create();
	ASSERT_TRUE(created);
	WindowsWaitTimer timer = rox::core::Move(created).TakeValue();
	ASSERT_TRUE(timer.Wait(0));

	Result<WindowsClock> clockCreated = WindowsClock::Create();
	ASSERT_TRUE(clockCreated);
	WindowsClock clock = rox::core::Move(clockCreated).TakeValue();
	const MonotonicTime before = clock.NowMonotonic();
	for (int32 iteration = 0; iteration < 4; ++iteration)
	{
		ASSERT_TRUE(timer.Wait(2));
	}
	const MonotonicTime after = clock.NowMonotonic();
	EXPECT_GE(after.Nanoseconds - before.Nanoseconds, 8000000u);
}

TEST(WindowsWaitTimerTests, MovedTimerSurvivesOriginalOwnerDestruction)
{
	Result<WindowsWaitTimer> created = WindowsWaitTimer::Create();
	ASSERT_TRUE(created);
	WindowsWaitTimer timer = [&created]()
	{
		WindowsWaitTimer original = rox::core::Move(created).TakeValue();
		return WindowsWaitTimer(rox::core::Move(original));
	}();

	EXPECT_TRUE(timer.Wait(0));
	EXPECT_TRUE(timer.Wait(1));
}
