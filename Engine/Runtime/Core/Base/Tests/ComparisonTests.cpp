#include <Comparison.hpp>
#include <Types.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(ComparisonTests, SelectsValuesAndClampsAtBoundaries)
{
	const int32 low = -4;
	const int32 high = 8;
	const int32 middle = 3;
	EXPECT_EQ(&Min(low, high), &low);
	EXPECT_EQ(&Max(low, high), &high);
	EXPECT_EQ(Min<int32>({3, -4, 8}), -4);
	EXPECT_EQ(Max<int32>({3, -4, 8}), 8);
	EXPECT_EQ(&ClampValue(low, middle, high), &middle);
	EXPECT_EQ(&ClampValue(high, low, middle), &middle);
	EXPECT_EQ(&ClampValue(middle, low, high), &middle);
}
