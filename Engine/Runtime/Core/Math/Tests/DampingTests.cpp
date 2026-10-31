#include <Damping.h>

#include <gtest/gtest.h>

using namespace rox::core;
using namespace rox::math;

TEST(DampingTests, HalfLifeAndZeroTimeRespectBoundaries)
{
	EXPECT_FLOAT_EQ(ExponentialDamp<float32>(2.0f, 10.0f, 1.0f, 0.0f), 2.0f);
	EXPECT_FLOAT_EQ(ExponentialDamp<float32>(2.0f, 10.0f, 0.0f, 1.0f), 10.0f);
	EXPECT_NEAR(ExponentialDamp<float32>(2.0f, 10.0f, 1.0f, 1.0f), 6.0f, 1.0e-5f);
	EXPECT_NEAR(ExponentialDamp<float32>(2.0f, 10.0f, 1.0f, 2.0f), 8.0f, 1.0e-5f);
}

TEST(DampingTests, ZeroTimeTakesPrecedenceOverZeroHalfLife)
{
	EXPECT_DOUBLE_EQ(ExponentialDamp(2.0, 10.0, 0.0, 0.0), 2.0);
	EXPECT_EQ(ExponentialDamp(Vector2d{1, 2}, Vector2d{3, 4}, 0.0, 0.0), (Vector2d{1, 2}));
}

TEST(DampingTests, VectorOverloadsAndSplitTimeFollowHalfLife)
{
	const Vector2d start2{0, 4};
	const Vector2d target2{4, 0};
	EXPECT_TRUE(ExponentialDamp(start2, target2, 1.0, 1.0).NearlyEquals(Vector2d{2, 2}, 1.0e-12));
	const Vector3d start3{0, 4, -2};
	const Vector3d target3{4, 0, 2};
	EXPECT_TRUE(ExponentialDamp(start3, target3, 1.0, 1.0).NearlyEquals(Vector3d{2, 2, 0}, 1.0e-12));
	const Vector4d start4{0, 4, -2, 8};
	const Vector4d target4{4, 0, 2, -4};
	const Vector4d whole = ExponentialDamp(start4, target4, 1.0, 1.0);
	EXPECT_TRUE(whole.NearlyEquals(Vector4d{2, 2, 0, 2}, 1.0e-12));
	const Vector4d first = ExponentialDamp(start4, target4, 1.0, 0.25);
	const Vector4d split = ExponentialDamp(first, target4, 1.0, 0.75);
	EXPECT_TRUE(split.NearlyEquals(whole, 1.0e-12));
}

TEST(DampingTests, SmallElapsedTimeAndInstantVectorDamping)
{
	const float64 result = ExponentialDamp(0.0, 1.0, 1.0, 1.0e-12);
	EXPECT_NEAR(result, 0.6931471805599453e-12, 1.0e-24);
	const Vector3d target{4, -2, 6};
	EXPECT_EQ(ExponentialDamp(Vector3d::Zero(), target, 0.0, 1.0), target);
}
