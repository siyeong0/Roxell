#include <Frustum.h>
#include <Matrix4x4.hpp>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(FrustumTests, IdentityViewProjectionUsesExpectedDepthRange)
{
	const Frustum<> frustum = Frustum<>::FromViewProjection(Matrix4x4f::Identity());
	ASSERT_TRUE(frustum.IsValid());
	ASSERT_TRUE(frustum.ContainsPoint({0, 0, 0.5f}));
	ASSERT_FALSE(frustum.ContainsPoint({0, 0, -0.1f}));
	ASSERT_FALSE(frustum.ContainsPoint({1.1f, 0, 0.5f}));
}

TEST(FrustumTests, BoxAndSphereClassificationIncludesBoundaries)
{
	Frustum<float64> frustum = Frustum<float64>::FromViewProjection(Matrix4x4d::Identity());
	ASSERT_TRUE(frustum.IsValid());
	const Box<float64> inside{{-0.5, -0.5, 0.25}, {0.5, 0.5, 0.75}};
	const Box<float64> touching{{1, -0.5, 0.25}, {2, 0.5, 0.75}};
	const Box<float64> outside{{2, -0.5, 0.25}, {3, 0.5, 0.75}};
	EXPECT_EQ(frustum.Classify(inside), EFrustumContainment::Inside);
	EXPECT_EQ(frustum.Classify(touching), EFrustumContainment::Intersecting);
	EXPECT_EQ(frustum.Classify(outside), EFrustumContainment::Outside);
	EXPECT_TRUE(frustum.Intersects(touching));
	EXPECT_FALSE(frustum.Intersects(outside));
	EXPECT_TRUE(frustum.Intersects(Sphere<float64>({2, 0, 0.5}, 1)));
	EXPECT_FALSE(frustum.Intersects(Sphere<float64>({3, 0, 0.5}, 1)));
	EXPECT_TRUE(frustum.ContainsPoint({1, 0, 0.5}));
	frustum.GetPlane(EFrustumPlane::Far) = Plane<float64>({0, 0, 0}, 1);
	EXPECT_TRUE(frustum.IsValid());
	EXPECT_TRUE(frustum.ContainsPoint({0, 0, 10}));
	frustum.GetPlane(EFrustumPlane::Left) = Plane<float64>();
	EXPECT_FALSE(frustum.IsValid());
}
