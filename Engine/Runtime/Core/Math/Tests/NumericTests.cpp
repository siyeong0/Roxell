#include <Numeric.hpp>
#include <Types.h>

#include <gtest/gtest.h>

#include <limits>

using namespace rox::core;

TEST(NumericTests, RoundingAndSpecialValuesPreserveContracts)
{
	EXPECT_FLOAT_EQ(Floor(-1.5f), -2.0f);
	EXPECT_FLOAT_EQ(Ceil(-1.5f), -1.0f);
	EXPECT_FLOAT_EQ(Round(-1.5f), -2.0f);
	EXPECT_FLOAT_EQ(Trunc(-1.5f), -1.0f);
	EXPECT_FALSE(IsFinite(std::numeric_limits<float32>::infinity()));
	EXPECT_TRUE(IsNaN(std::numeric_limits<float32>::quiet_NaN()));
	EXPECT_TRUE(SignBit(CopySign(0.0f, -1.0f)));
	EXPECT_FLOAT_EQ(Hypot(3.0f, 4.0f), 5.0f);
}

TEST(NumericTests, MathematicalFunctionsHandleRepresentativeInputs)
{
	EXPECT_EQ(Abs(-7), 7);
	EXPECT_DOUBLE_EQ(Fabs(-2.5), 2.5);
	EXPECT_EQ(RoundToLong(-1.5), -2);
	EXPECT_EQ(RoundToLongLong(1.5), 2);
	EXPECT_DOUBLE_EQ(Sqrt(9.0), 3.0);
	EXPECT_DOUBLE_EQ(Cbrt(-8.0), -2.0);
	EXPECT_DOUBLE_EQ(Hypot(2.0, 3.0, 6.0), 7.0);
	EXPECT_DOUBLE_EQ(Sin(0.0), 0.0);
	EXPECT_DOUBLE_EQ(Cos(0.0), 1.0);
	EXPECT_DOUBLE_EQ(Tan(0.0), 0.0);
	EXPECT_DOUBLE_EQ(Asin(0.0), 0.0);
	EXPECT_DOUBLE_EQ(Acos(1.0), 0.0);
	EXPECT_DOUBLE_EQ(Atan2(0.0, 1.0), 0.0);
	EXPECT_DOUBLE_EQ(Pow(2.0, 3.0), 8.0);
	EXPECT_DOUBLE_EQ(Fma(2.0, 3.0, 4.0), 10.0);
	EXPECT_DOUBLE_EQ(Exp(0.0), 1.0);
	EXPECT_DOUBLE_EQ(Exp2(3.0), 8.0);
	EXPECT_DOUBLE_EQ(Expm1(0.0), 0.0);
	EXPECT_DOUBLE_EQ(Log1p(0.0), 0.0);
	EXPECT_DOUBLE_EQ(Log2(8.0), 3.0);
	EXPECT_DOUBLE_EQ(Fmod(-5.0, 3.0), -2.0);
	EXPECT_DOUBLE_EQ(Remainder(5.0, 3.0), -1.0);
	EXPECT_DOUBLE_EQ(LerpValue(2.0, 6.0, 0.25), 3.0);
	EXPECT_GT(NextAfter(1.0, 2.0), 1.0);
	EXPECT_TRUE(IsNaN(Sqrt(-1.0)));
}
