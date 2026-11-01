#include <WindowsClock.h>
#include <ClockPrimitives.hpp>
#include <Utility.hpp>
#include <Synchronization.hpp>
#include <ThreadPrimitives.hpp>

#include <gtest/gtest.h>

#include <thread>

using namespace rox::core;
using namespace rox::os;

TEST(WindowsClockTests, MonotonicTimeNeverDecreases)
{
	Result<WindowsClock> created = WindowsClock::Create();
	ASSERT_TRUE(created);
	WindowsClock clock = rox::core::Move(created).TakeValue();

	MonotonicTime previous = clock.NowMonotonic();
	for (int32 sample = 0; sample < 10000; ++sample)
	{
		const MonotonicTime current = clock.NowMonotonic();
		EXPECT_GE(current.Nanoseconds, previous.Nanoseconds);
		previous = current;
	}
}

TEST(WindowsClockTests, UtcTimeUsesUnixMicroseconds)
{
	Result<WindowsClock> created = WindowsClock::Create();
	ASSERT_TRUE(created);
	WindowsClock clock = rox::core::Move(created).TakeValue();

	const int64 before = rox::core::DurationCast<rox::core::Microseconds>(rox::core::WallClock::now().time_since_epoch()).count();
	const UtcTime utc = clock.NowUtc();
	const int64 after = rox::core::DurationCast<rox::core::Microseconds>(rox::core::WallClock::now().time_since_epoch()).count();
	constexpr int64 TOLERANCE = 60000000;
	EXPECT_GE(utc.UnixMicroseconds, before - TOLERANCE);
	EXPECT_LE(utc.UnixMicroseconds, after + TOLERANCE);
}

TEST(WindowsClockTests, ConcurrentMonotonicReadsNeverDecrease)
{
	Result<WindowsClock> created = WindowsClock::Create();
	ASSERT_TRUE(created);
	WindowsClock clock = rox::core::Move(created).TakeValue();
	rox::core::Atomic<bool> bOrdered = true;
	std::thread threads[4];

	for (int32 index = 0; index < 4; ++index)
	{
		threads[index] = std::thread([&clock, &bOrdered]()
		{
			MonotonicTime previous = clock.NowMonotonic();
			for (int32 sample = 0; sample < 10000; ++sample)
			{
				const MonotonicTime current = clock.NowMonotonic();
				if (current < previous)
				{
					bOrdered.store(false, rox::core::MemoryOrderRelaxed);
					return;
				}
				previous = current;
			}
		});
	}

	for (std::thread& thread : threads)
	{
		thread.join();
	}
	EXPECT_TRUE(bOrdered.load(rox::core::MemoryOrderRelaxed));
}

TEST(WindowsClockTests, MonotonicNanosecondsAdvanceAcrossWait)
{
	Result<WindowsClock> created = WindowsClock::Create();
	ASSERT_TRUE(created);
	WindowsClock clock = rox::core::Move(created).TakeValue();
	const MonotonicTime before = clock.NowMonotonic();
	rox::core::SleepFor(rox::core::Milliseconds(10));
	const MonotonicTime after = clock.NowMonotonic();
	ASSERT_GT(after.Nanoseconds, before.Nanoseconds);
	EXPECT_GE(after.Nanoseconds - before.Nanoseconds, 1000000u);
	EXPECT_LT(after.Nanoseconds - before.Nanoseconds, 60000000000u);
}
