#include <Capsule.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(CapsuleTests, BoundaryAndClosestPointUseAxisSegment)
{
	const Capsule<> capsule(LineSegment<>({0, 0, 0}, {0, 2, 0}), 1);
	EXPECT_TRUE(capsule.IsValid());
	EXPECT_TRUE(capsule.Contains({1, 1, 0}));
	EXPECT_FALSE(capsule.Contains({2, 1, 0}));
	EXPECT_EQ(capsule.ClosestPoint({2, 1, 0}), Vector3f(1, 1, 0));
	EXPECT_FLOAT_EQ(capsule.DistanceSquared({2, 1, 0}), 1.0f);
}

TEST(CapsuleTests, DegenerateAxisAndZeroRadiusHaveValidBounds)
{
	const Capsule<float64> sphere(LineSegment<float64>({1, 2, 3}, {1, 2, 3}), 2);
	EXPECT_TRUE(sphere.IsValid());
	EXPECT_TRUE(sphere.Contains({3, 2, 3}));
	EXPECT_FALSE(sphere.Contains({4, 2, 3}));
	EXPECT_EQ(sphere.ClosestPoint({1, 2, 3}), (Vector3d{1, 2, 3}));
	EXPECT_EQ(sphere.ClosestPoint({4, 2, 3}), (Vector3d{3, 2, 3}));
	EXPECT_DOUBLE_EQ(sphere.DistanceSquared({4, 2, 3}), 1.0);
	const Box<float64> bounds = sphere.GetBounds();
	EXPECT_EQ(bounds.Min, (Vector3d{-1, 0, 1}));
	EXPECT_EQ(bounds.Max, (Vector3d{3, 4, 5}));
	const Capsule<float64> segment(LineSegment<float64>({0, 0, 0}, {0, 2, 0}), 0);
	EXPECT_TRUE(segment.Contains({0, 1, 0}));
	EXPECT_FALSE(segment.Contains({1, 1, 0}));
	EXPECT_EQ(segment.ClosestPoint({1, 1, 0}), (Vector3d{0, 1, 0}));
	EXPECT_FALSE(Capsule<float64>(segment.Axis, -1).IsValid());
}
