#include <Plane.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(PlaneTests, SignedDistanceAndNormalization)
{
	const Plane<> plane(Vector3f(0.0f, 2.0f, 0.0f), -4.0f);
	const Plane<> fromPoint = Plane<>::FromPointNormal(Vector3f(0.0f, 2.0f, 0.0f), Vector3f(0.0f, 2.0f, 0.0f));
	EXPECT_FLOAT_EQ(fromPoint.SignedDistance(Vector3f(0.0f, 3.0f, 0.0f)), 1.0f);
	EXPECT_FLOAT_EQ(plane.SignedDistance(Vector3f(0.0f, 3.0f, 0.0f)), 2.0f);
	Plane<> normalized;
	ASSERT_TRUE(plane.TryNormalize(&normalized));
	EXPECT_FLOAT_EQ(normalized.SignedDistance(Vector3f(0.0f, 3.0f, 0.0f)), 1.0f);
	const Plane<> zero;
	EXPECT_FALSE(zero.TryNormalize(&normalized));
	EXPECT_FLOAT_EQ(normalized.Distance, -2.0f);
}

TEST(PlaneTests, NormalizationThresholdFailurePreservesOutput)
{
	const Plane<float64> small({0, 0.25, 0}, -0.5);
	Plane<float64> output({1, 0, 0}, 7);
	EXPECT_FALSE(small.TryNormalize(&output, 0.25));
	EXPECT_EQ(output.Normal, (Vector3d{1, 0, 0}));
	EXPECT_DOUBLE_EQ(output.Distance, 7.0);
	ASSERT_TRUE(small.TryNormalize(&output, 0.0));
	EXPECT_EQ(output.Normal, (Vector3d{0, 1, 0}));
	EXPECT_DOUBLE_EQ(output.Distance, -2.0);
	Plane<float64> inPlace = small;
	inPlace.Normalize(0.0);
	EXPECT_EQ(inPlace.Normal, output.Normal);
	EXPECT_DOUBLE_EQ(inPlace.Distance, output.Distance);
	const Plane<float64> normalized = small.Normalized(0.0);
	EXPECT_EQ(normalized.Normal, output.Normal);
	EXPECT_DOUBLE_EQ(normalized.Distance, output.Distance);
}
