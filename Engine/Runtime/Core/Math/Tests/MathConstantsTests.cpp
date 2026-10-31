#include <MathConstants.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(MathConstantsTests, ConstantsPreserveExpectedRelationships)
{
	EXPECT_DOUBLE_EQ(TWO_PI<float64>, 2.0 * PI<float64>);
	EXPECT_DOUBLE_EQ(HALF_PI<float64>, PI<float64> / 2.0);
	EXPECT_NEAR(PI<float64>, 3.141592653589793, 1.0e-15);
	EXPECT_NEAR(EULER<float64>, 2.718281828459045, 1.0e-15);
	EXPECT_NEAR(LN_TWO<float64>, 0.6931471805599453, 1.0e-15);
}
