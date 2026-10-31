#include <ComparisonTypes.hpp>
#include <Types.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(ComparisonTypesTests, OrderingAndLessDistinguishEqualValues)
{
	const Less<int32> less{};
	EXPECT_TRUE(less(1, 2));
	EXPECT_FALSE(less(2, 1));
	EXPECT_FALSE(less(2, 2));
	EXPECT_TRUE(StrongOrdering::less < 0);
	EXPECT_TRUE(StrongOrdering::equal == 0);
	EXPECT_TRUE(StrongOrdering::greater > 0);
}
