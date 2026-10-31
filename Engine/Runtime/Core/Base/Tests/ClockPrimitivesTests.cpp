#include <ClockPrimitives.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

static_assert(MonotonicClock::is_steady);

TEST(ClockPrimitivesTests, CastTruncatesAndCeilRoundsUp)
{
	const Microseconds duration(1501);
	EXPECT_EQ(DurationCast<Milliseconds>(duration).count(), 1);
	EXPECT_EQ(DurationCeil<Milliseconds>(duration).count(), 2);
	EXPECT_EQ(DurationCast<Milliseconds>(Microseconds(-1501)).count(), -1);
	EXPECT_EQ(DurationCeil<Milliseconds>(Microseconds(-1501)).count(), -1);
}
