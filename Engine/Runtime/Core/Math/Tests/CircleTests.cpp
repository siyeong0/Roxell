#include <Circle.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(CircleTests, TangencyAndDistanceIncludeBoundary)
{
	const Circle<> circle({0, 0}, 2);
	EXPECT_TRUE(circle.Contains({2, 0}));
	EXPECT_FALSE(circle.Contains({3, 0}));
	EXPECT_TRUE(circle.Overlaps(Circle<>({4, 0}, 2)));
	EXPECT_EQ(circle.ClosestPoint({3, 0}), Vector2f(2, 0));
	EXPECT_FLOAT_EQ(circle.DistanceSquared({3, 0}), 1.0f);
}

TEST(CircleTests, ZeroRadiusAndInteriorQueries)
{
	const Circle<float64> circle({2, 3}, 2);
	EXPECT_TRUE(circle.IsValid());
	EXPECT_EQ(circle.ClosestPoint({2, 3}), (Vector2d{2, 3}));
	EXPECT_DOUBLE_EQ(circle.DistanceSquared({2, 3}), 0.0);
	EXPECT_FALSE(circle.Overlaps(Circle<float64>({7, 3}, 2)));
	const Circle<float64> point({2, 3}, 0);
	EXPECT_TRUE(point.Contains({2, 3}));
	EXPECT_FALSE(point.Contains({2, 4}));
	EXPECT_EQ(point.ClosestPoint({2, 4}), (Vector2d{2, 3}));
	EXPECT_DOUBLE_EQ(point.DistanceSquared({2, 4}), 1.0);
	EXPECT_FALSE(Circle<float64>({0, 0}, -1).IsValid());
}
