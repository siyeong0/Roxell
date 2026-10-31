#include <Sphere.h>
#include <NumericLimits.hpp>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(SphereTests, ContainsAndTangency)
{
	const Sphere<> sphere(Vector3f::Zero(), 2.0f);
	EXPECT_TRUE(sphere.Contains(Vector3f(2.0f, 0.0f, 0.0f)));
	EXPECT_FALSE(sphere.Contains(Vector3f(3.0f, 0.0f, 0.0f)));
	const Sphere<> tangent(Vector3f(3.0f, 0.0f, 0.0f), 1.0f);
	EXPECT_TRUE(sphere.Overlaps(tangent));
}

TEST(SphereTests, EncapsulationPreservesStateOnInvalidInput)
{
	Sphere<float64> sphere({0, 0, 0}, 1);
	ASSERT_TRUE(sphere.TryEncapsulate({0.5, 0, 0}));
	EXPECT_EQ(sphere.Center, Vector3d::Zero());
	EXPECT_DOUBLE_EQ(sphere.Radius, 1.0);
	ASSERT_TRUE(sphere.TryEncapsulate({3, 0, 0}));
	EXPECT_EQ(sphere.Center, (Vector3d{1, 0, 0}));
	EXPECT_DOUBLE_EQ(sphere.Radius, 2.0);
	EXPECT_TRUE(sphere.Contains({-1, 0, 0}));
	EXPECT_TRUE(sphere.Contains({3, 0, 0}));
	const Sphere<float64> previous = sphere;
	EXPECT_FALSE(sphere.TryEncapsulate({rox::core::NumericLimits<float64>::infinity(), 0, 0}));
	EXPECT_EQ(sphere.Center, previous.Center);
	EXPECT_DOUBLE_EQ(sphere.Radius, previous.Radius);
	EXPECT_FALSE(sphere.Overlaps(Sphere<float64>({6, 0, 0}, 1)));
	sphere.Encapsulate({5, 0, 0});
	EXPECT_TRUE(sphere.Contains({5, 0, 0}));
}
