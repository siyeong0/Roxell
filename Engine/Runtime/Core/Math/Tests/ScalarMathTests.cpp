#include <ScalarMath.h>

#include <gtest/gtest.h>

#include <limits>

using namespace rox::core;
using namespace rox::math;

TEST(ScalarMathTests, ScalarEndpointsAndNonfiniteComparisons)
{
	ASSERT_EQ(Clamp<float32>(-4.0f, -2.0f, 3.0f), -2.0f);
	ASSERT_EQ(Clamp<float32>(4.0f, -2.0f, 3.0f), 3.0f);
	ASSERT_EQ(Lerp<float32>(2.0f, 6.0f, 0.25f), 3.0f);
	ASSERT_EQ(InverseLerp<float32>(10.0f, 2.0f, 4.0f), 0.75f);
	ASSERT_EQ(Wrap<float32>(3.0f, -2.0f, 3.0f), -2.0f);
	ASSERT_EQ(Wrap<float32>(-3.0f, -2.0f, 3.0f), 2.0f);
	ASSERT_EQ(PingPong<float32>(2.0f, 2.0f), 2.0f);
	ASSERT_EQ(PingPong<float32>(4.0f, 2.0f), 0.0f);
	const float32 infinity = std::numeric_limits<float32>::infinity();
	ASSERT_TRUE(NearlyEqual<float32>(infinity, infinity));
	ASSERT_FALSE(NearlyEqual<float32>(infinity, -infinity));
	ASSERT_FALSE(NearlyEqual<float32>(std::numeric_limits<float32>::quiet_NaN(), 0.0f));
}

TEST(ScalarMathTests, InterpolationAndMovement)
{
	EXPECT_DOUBLE_EQ(DegreesToRadians(180.0), PI<float64>);
	EXPECT_DOUBLE_EQ(RadiansToDegrees(PI<float64>), 180.0);
	EXPECT_DOUBLE_EQ(Saturate(-0.5), 0.0);
	EXPECT_DOUBLE_EQ(Saturate(1.5), 1.0);
	EXPECT_DOUBLE_EQ(Remap(5.0, 0.0, 10.0, 100.0, 200.0), 150.0);
	EXPECT_DOUBLE_EQ(InverseLerp(-std::numeric_limits<float64>::max(),
	                             std::numeric_limits<float64>::max(), 0.0), 0.5);
	EXPECT_TRUE(IsNearlyZero(-0.001, 0.001));
	EXPECT_FALSE(IsNearlyZero(0.002, 0.001));
	EXPECT_DOUBLE_EQ(MoveTowards(1.0, 4.0, 1.0), 2.0);
	EXPECT_DOUBLE_EQ(MoveTowards(4.0, 1.0, 1.0), 3.0);
	EXPECT_DOUBLE_EQ(MoveTowards(1.0, 2.0, 5.0), 2.0);
	EXPECT_DOUBLE_EQ(SmoothStep(0.0, 1.0, -1.0), 0.0);
	EXPECT_DOUBLE_EQ(SmoothStep(0.0, 1.0, 0.5), 0.5);
	EXPECT_DOUBLE_EQ(SmootherStep(1.0, 0.0, -1.0), 1.0);
	EXPECT_DOUBLE_EQ(SmootherStep(0.0, 1.0, 0.5), 0.5);
}

TEST(ScalarMathTests, PeriodicAnglesAndSnapping)
{
	EXPECT_DOUBLE_EQ(Repeat(-1.0, 4.0), 3.0);
	EXPECT_DOUBLE_EQ(PingPong(-5.0, 4.0), 3.0);
	EXPECT_NEAR(DeltaAngle(0.0, PI<float64>), -PI<float64>, 1.0e-14);
	EXPECT_NEAR(DeltaAngle(3.0 * PI<float64> / 2.0, 0.0), PI<float64> / 2.0, 1.0e-14);
	EXPECT_NEAR(LerpAngle(3.0 * PI<float64> / 2.0, 0.0, 0.5), 7.0 * PI<float64> / 4.0, 1.0e-14);
	EXPECT_DOUBLE_EQ(Snap(2.5, 1.0), 3.0);
	EXPECT_DOUBLE_EQ(Snap(-2.5, 1.0), -3.0);
}
