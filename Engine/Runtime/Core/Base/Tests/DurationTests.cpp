#include <Duration.hpp>

#include <gtest/gtest.h>

#include <limits>

using namespace rox::core;

TEST(DurationTests, DurationConversionsAndSignedArithmetic)
{
	ASSERT_EQ(Duration{}.ToNanoseconds(), 0);
	ASSERT_EQ(Duration::FromMilliseconds(-3).ToNanoseconds(), -3000000);
	ASSERT_EQ(Duration::FromSeconds(1.25).ToNanoseconds(), 1250000000);
	ASSERT_EQ(Duration::FromSeconds(-0.0000000019).ToNanoseconds(), -1);
	ASSERT_EQ((Duration::FromNanoseconds(-7) / 2).ToNanoseconds(), -3);
	ASSERT_TRUE((Duration::FromNanoseconds(std::numeric_limits<int64>::min()) +
	              Duration::FromNanoseconds(std::numeric_limits<int64>::max())).ToNanoseconds() == -1);
}
