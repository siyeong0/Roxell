#include <Ray.h>
#include <NumericLimits.hpp>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(RayTests, ClosestPointClampsToOrigin)
{
	const Ray<> ray({1, 0, 0}, {2, 0, 0});
	EXPECT_TRUE(ray.IsValid());
	EXPECT_EQ(ray.At(2), Vector3f(5, 0, 0));
	EXPECT_FLOAT_EQ(ray.ClosestParameter({-1, 1, 0}), 0.0f);
	EXPECT_FLOAT_EQ(ray.ClosestParameter({5, 1, 0}), 2.0f);
	EXPECT_EQ(ray.ClosestPoint({-1, 1, 0}), Vector3f(1, 0, 0));
	EXPECT_FLOAT_EQ(ray.DistanceSquared({5, 1, 0}), 1.0f);
}

TEST(RayTests, SaturatedParameterKeepsRepresentableProjection)
{
	const Ray<> ray({0, 0, 0}, {1.0e-38f, 0, 0});
	EXPECT_TRUE(ray.IsValid());
	EXPECT_FLOAT_EQ(ray.ClosestParameter({10, 2, 0}), rox::core::NumericLimits<float32>::max());
	EXPECT_EQ(ray.ClosestPoint({10, 2, 0}), (Vector3f{10, 0, 0}));
	EXPECT_FLOAT_EQ(ray.DistanceSquared({10, 2, 0}), 4.0f);
	EXPECT_FALSE(Ray<>({0, 0, 0}, {0, 0, 0}).IsValid());
}
