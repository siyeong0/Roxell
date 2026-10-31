#include <OrientedBox.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(OrientedBoxTests, IdentityRotationUsesLocalExtents)
{
	const OrientedBox<> box({1, 2, 3}, {2, 1, 1});
	EXPECT_TRUE(box.IsValid());
	EXPECT_EQ(box.GetCorner(0), Vector3f(-1, 1, 2));
	EXPECT_EQ(box.GetCorner(7), Vector3f(3, 3, 4));
	EXPECT_TRUE(box.Contains({3, 2, 3}));
	EXPECT_FALSE(box.Contains({4, 2, 3}));
	EXPECT_EQ(box.ClosestPoint({4, 2, 3}), Vector3f(3, 2, 3));
	EXPECT_FLOAT_EQ(box.DistanceSquared({4, 2, 3}), 1.0f);
}

TEST(OrientedBoxTests, RotatedBoundsAndDistanceUseLocalAxes)
{
	const Quaternion<float64> rotation = Quaternion<float64>::FromAxisAngle(Vector3d::UnitZ(), 1.5707963267948966);
	const OrientedBox<float64> box({0, 0, 0}, {2, 1, 1}, rotation);
	ASSERT_TRUE(box.IsValid());
	const Box<float64> bounds = box.GetBounds();
	EXPECT_NEAR(bounds.Min.X, -1.0, 1.0e-12);
	EXPECT_NEAR(bounds.Min.Y, -2.0, 1.0e-12);
	EXPECT_NEAR(bounds.Max.X, 1.0, 1.0e-12);
	EXPECT_NEAR(bounds.Max.Y, 2.0, 1.0e-12);
	EXPECT_TRUE(box.Contains({0, 1.5, 0}));
	EXPECT_FALSE(box.Contains({1.5, 0, 0}));
	const Vector3d closest = box.ClosestPoint({2, 0, 0});
	EXPECT_NEAR(closest.X, 1.0, 1.0e-12);
	EXPECT_NEAR(closest.Y, 0.0, 1.0e-12);
	EXPECT_NEAR(box.DistanceSquared({2, 0, 0}), 1.0, 1.0e-12);
	EXPECT_FALSE(OrientedBox<float64>({0, 0, 0}, {-1, 1, 1}).IsValid());
}
