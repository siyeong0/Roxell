#include <Triangle.h>

#include <gtest/gtest.h>

using namespace rox::core;
using namespace rox::math;

TEST(TriangleTests, TriangleBarycentricAndDegeneracy)
{
	const Triangle<> triangle{{}, {4, 0, 0}, {0, 4, 0}};
	Vector3f weights{7, 8, 9};
	ASSERT_TRUE(triangle.Barycentric({1, 1, 7}, &weights));
	ASSERT_EQ(weights, (Vector3f{0.5f, 0.25f, 0.25f}));
	ASSERT_EQ(triangle.DistanceSquared({1, 1, 7}), 49);
	ASSERT_EQ(triangle.ClosestPoint({-1, -2, 0}), Vector3f{});
	const Triangle<> degenerate{{}, {1, 0, 0}, {2, 0, 0}};
	weights = {7, 8, 9};
	ASSERT_FALSE(degenerate.Barycentric({}, &weights));
	ASSERT_EQ(weights, (Vector3f{7, 8, 9}));
}

TEST(TriangleTests, ShapePropertiesAndDegenerateClosestPoint)
{
	const Triangle<float64> triangle{{0, 0, 0}, {6, 0, 0}, {0, 3, 0}};
	EXPECT_TRUE(triangle.IsValid());
	EXPECT_FALSE(triangle.IsDegenerate());
	EXPECT_DOUBLE_EQ(triangle.Area(), 9.0);
	EXPECT_EQ(triangle.Centroid(), (Vector3d{2, 1, 0}));
	EXPECT_EQ(triangle.Normal(), (Vector3d{0, 0, 1}));
	const Box<float64> bounds = triangle.GetBounds();
	EXPECT_EQ(bounds.Min, (Vector3d{0, 0, 0}));
	EXPECT_EQ(bounds.Max, (Vector3d{6, 3, 0}));
	const Triangle<float64> line{{0, 0, 0}, {2, 0, 0}, {4, 0, 0}};
	EXPECT_TRUE(line.IsDegenerate());
	EXPECT_DOUBLE_EQ(line.Area(), 0.0);
	EXPECT_EQ(line.ClosestPoint({3, 2, 0}), (Vector3d{3, 0, 0}));
	EXPECT_DOUBLE_EQ(line.DistanceSquared({3, 2, 0}), 4.0);
}
