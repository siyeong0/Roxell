#include <CurveMath.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(CurveMathVectorTests, StraightCubicCurveHasExpectedPositionTangentAndLength)
{
	const Vector2f start{0, 0};
	const Vector2f control1{1, 0};
	const Vector2f control2{2, 0};
	const Vector2f end{3, 0};
	EXPECT_EQ(CubicBezier(start, control1, control2, end, 0.5f), Vector2f(1.5f, 0));
	EXPECT_EQ(CubicBezierTangent(start, control1, control2, end, 0.5f), Vector2f(3, 0));
	EXPECT_FLOAT_EQ(ApproximateCubicBezierLength(start, control1, control2, end, 8), 3.0f);
}

TEST(CurveMathVectorTests, VectorCurvesHonorEndpointDerivativesAndLengths)
{
	const Vector3d start{1, 2, 3};
	const Vector3d end{5, 6, 7};
	const Vector3d tangent{4, 4, 4};
	EXPECT_EQ(Hermite(start, tangent, end, tangent, 0.0), start);
	EXPECT_EQ(Hermite(start, tangent, end, tangent, 1.0), end);
	EXPECT_EQ(HermiteTangent(start, tangent, end, tangent, 0.0), tangent);
	EXPECT_EQ(HermiteTangent(start, tangent, end, tangent, 1.0), tangent);
	EXPECT_NEAR(ApproximateHermiteLength(start, tangent, end, tangent, 8), tangent.Length(), 1.0e-12);
	const Vector4d p0{0, 0, 0, 0};
	const Vector4d p1{1, 2, 3, 4};
	const Vector4d p2{2, 4, 6, 8};
	const Vector4d p3{3, 6, 9, 12};
	EXPECT_EQ(CatmullRom(p0, p1, p2, p3, 0.5), (p1 + p2) * 0.5);
	EXPECT_EQ(CatmullRomTangent(p0, p1, p2, p3, 0.5), p1);
	EXPECT_NEAR(ApproximateCatmullRomLength(p0, p1, p2, p3, 8), p1.Length(), 1.0e-12);
	EXPECT_EQ(QuadraticBezierTangent(p0, p1, p2, 0.5), p2);
	EXPECT_NEAR(ApproximateQuadraticBezierLength(p0, p1, p2, 8), p2.Length(), 1.0e-12);
}
