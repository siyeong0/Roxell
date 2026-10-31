#include <IntegerAlgorithms.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(IntegerAlgorithmsTests, CommonDivisorsHandleSignsAndZero)
{
	EXPECT_EQ(Gcd(-12, 18), 6);
	EXPECT_EQ(Gcd(0, 0), 0);
	EXPECT_EQ(Gcd(0, 7), 7);
	EXPECT_EQ(Lcm(-12, 18), 36);
	EXPECT_EQ(Lcm(0, 7), 0);
}
