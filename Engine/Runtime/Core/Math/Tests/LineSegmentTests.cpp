#include <LineSegment.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(LineSegmentTests, ClosestPointClampsToEndpoints)
{
	const LineSegment<> segment({0, 0, 0}, {4, 0, 0});
	EXPECT_FLOAT_EQ(segment.Length(), 4.0f);
	EXPECT_FLOAT_EQ(segment.ClosestParameter({-2, 1, 0}), 0.0f);
	EXPECT_FLOAT_EQ(segment.ClosestParameter({2, 1, 0}), 0.5f);
	EXPECT_EQ(segment.ClosestPoint({6, 1, 0}), Vector3f(4, 0, 0));
	EXPECT_FLOAT_EQ(segment.DistanceSquared({2, 1, 0}), 1.0f);
	const LineSegment<> point({1, 2, 3}, {1, 2, 3});
	EXPECT_EQ(point.ClosestPoint({9, 2, 3}), Vector3f(1, 2, 3));
}

TEST(LineSegmentTests, EndpointsBoundsAndPointSegment)
{
	const LineSegment<float64> segment({3, -1, 2}, {-1, 2, 2});
	EXPECT_TRUE(segment.IsValid());
	EXPECT_EQ(segment.At(0.0), segment.Start);
	EXPECT_EQ(segment.At(1.0), segment.End);
	EXPECT_EQ(segment.At(0.5), (Vector3d{1, 0.5, 2}));
	EXPECT_DOUBLE_EQ(segment.Length(), 5.0);
	EXPECT_DOUBLE_EQ(segment.LengthSquared(), 25.0);
	const Box<float64> bounds = segment.GetBounds();
	EXPECT_EQ(bounds.Min, (Vector3d{-1, -1, 2}));
	EXPECT_EQ(bounds.Max, (Vector3d{3, 2, 2}));
	const LineSegment<float64> point({1, 2, 3}, {1, 2, 3});
	EXPECT_DOUBLE_EQ(point.ClosestParameter({5, 2, 3}), 0.0);
	EXPECT_DOUBLE_EQ(point.LengthSquared(), 0.0);
	EXPECT_DOUBLE_EQ(point.DistanceSquared({5, 2, 3}), 16.0);
}
