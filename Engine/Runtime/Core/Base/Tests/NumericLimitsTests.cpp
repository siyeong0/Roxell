#include <NumericLimits.hpp>
#include <Types.h>

#include <gtest/gtest.h>

using namespace rox::core;

static_assert(NumericLimits<uint8>::digits == 8);
static_assert(NumericLimits<int32>::is_signed);
static_assert(NumericLimits<uint32>::max() == 0xffffffffu);
static_assert(NumericLimits<float32>::has_infinity);

TEST(NumericLimitsTests, FloatingBoundsAndInfinityAreOrdered)
{
	EXPECT_LT(NumericLimits<float32>::lowest(), 0.0f);
	EXPECT_GT(NumericLimits<float32>::min(), 0.0f);
	EXPECT_GT(NumericLimits<float32>::infinity(), NumericLimits<float32>::max());
}
