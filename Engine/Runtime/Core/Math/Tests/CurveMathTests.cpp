#include <CurveMath.h>

#include <gtest/gtest.h>

using namespace rox::core;
using namespace rox::math;

TEST(CurveMathTests, CurveEndpointsAndTangentsFloat32)
{
	using T = float32;
	constexpr T ZERO = T{0};
	constexpr T HALF = T{0.5};
	constexpr T ONE = T{1};
	ASSERT_EQ(QuadraticBezier<T>(ZERO, T{2}, T{4}, ZERO), ZERO);
	ASSERT_EQ(QuadraticBezier<T>(ZERO, T{2}, T{4}, ONE), T{4});
	ASSERT_EQ(QuadraticBezier<T>(ZERO, T{2}, T{4}, HALF), T{2});
	ASSERT_EQ(QuadraticBezierTangent<T>(ZERO, T{2}, T{4}, HALF), T{4});
	ASSERT_EQ(CubicBezier<T>(ZERO, ZERO, T{4}, T{4}, HALF), T{2});
	ASSERT_EQ(CubicBezierTangent<T>(ZERO, ZERO, T{4}, T{4}, HALF), T{6});
	ASSERT_EQ(Hermite<T>(T{1}, T{2}, T{5}, T{3}, ZERO), T{1});
	ASSERT_EQ(Hermite<T>(T{1}, T{2}, T{5}, T{3}, ONE), T{5});
	ASSERT_EQ(HermiteTangent<T>(T{1}, T{2}, T{5}, T{3}, ZERO), T{2});
	ASSERT_EQ(HermiteTangent<T>(T{1}, T{2}, T{5}, T{3}, ONE), T{3});
	ASSERT_EQ(CatmullRom<T>(ZERO, T{2}, T{4}, T{6}, HALF), T{3});
	ASSERT_EQ(ApproximateQuadraticBezierLength<T>(ZERO, T{2}, T{4}, 4), T{4});
}

TEST(CurveMathTests, CurveEndpointsAndTangentsFloat64)
{
	using T = float64;
	constexpr T ZERO = T{0};
	constexpr T HALF = T{0.5};
	constexpr T ONE = T{1};
	ASSERT_EQ(QuadraticBezier<T>(ZERO, T{2}, T{4}, ZERO), ZERO);
	ASSERT_EQ(QuadraticBezier<T>(ZERO, T{2}, T{4}, ONE), T{4});
	ASSERT_EQ(QuadraticBezier<T>(ZERO, T{2}, T{4}, HALF), T{2});
	ASSERT_EQ(QuadraticBezierTangent<T>(ZERO, T{2}, T{4}, HALF), T{4});
	ASSERT_EQ(CubicBezier<T>(ZERO, ZERO, T{4}, T{4}, HALF), T{2});
	ASSERT_EQ(CubicBezierTangent<T>(ZERO, ZERO, T{4}, T{4}, HALF), T{6});
	ASSERT_EQ(Hermite<T>(T{1}, T{2}, T{5}, T{3}, ZERO), T{1});
	ASSERT_EQ(Hermite<T>(T{1}, T{2}, T{5}, T{3}, ONE), T{5});
	ASSERT_EQ(HermiteTangent<T>(T{1}, T{2}, T{5}, T{3}, ZERO), T{2});
	ASSERT_EQ(HermiteTangent<T>(T{1}, T{2}, T{5}, T{3}, ONE), T{3});
	ASSERT_EQ(CatmullRom<T>(ZERO, T{2}, T{4}, T{6}, HALF), T{3});
	ASSERT_EQ(ApproximateQuadraticBezierLength<T>(ZERO, T{2}, T{4}, 4), T{4});
}

TEST(CurveMathTests, LengthEstimatesAndCatmullRomTangentsForStraightSegments)
{
	EXPECT_DOUBLE_EQ(CatmullRomTangent(0.0, 2.0, 4.0, 6.0, 0.0), 2.0);
	EXPECT_DOUBLE_EQ(CatmullRomTangent(0.0, 2.0, 4.0, 6.0, 1.0), 2.0);
	EXPECT_DOUBLE_EQ(ApproximateCubicBezierLength(0.0, 1.0, 2.0, 3.0, 8), 3.0);
	EXPECT_DOUBLE_EQ(ApproximateHermiteLength(1.0, 4.0, 5.0, 4.0, 8), 4.0);
	EXPECT_DOUBLE_EQ(ApproximateCatmullRomLength(0.0, 2.0, 4.0, 6.0, 8), 2.0);
	EXPECT_DOUBLE_EQ(ApproximateCubicBezierLength(4.0, 4.0, 4.0, 4.0, 1), 0.0);
}
