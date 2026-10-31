#include <GeometryQueries.h>

#include <gtest/gtest.h>

using namespace rox::core;
using namespace rox::math;

TEST(GeometryQueriesTests, RayIntersectionsAndMissPreservation)
{
	const Box<> box{{-1, -1, -1}, {1, 1, 1}};
	const Sphere<> sphere{{}, 1};
	const Ray<> through{{0, 0, -5}, {0, 0, 2}};
	RayInterval<> interval{-7, -8};
	ASSERT_TRUE(Intersect<float32>(through, box, &interval));
	ASSERT_EQ(interval.Enter, 2);
	ASSERT_EQ(interval.Exit, 3);
	ASSERT_TRUE(Intersect<float32>(through, sphere, &interval));
	ASSERT_EQ(interval.Enter, 2);
	ASSERT_EQ(interval.Exit, 3);
	const Ray<> tangent{{1, 0, -4}, {0, 0, 2}};
	ASSERT_TRUE(Intersect<float32>(tangent, sphere, &interval));
	ASSERT_EQ(interval.Enter, 2);
	ASSERT_EQ(interval.Exit, 2);
	interval = {-7, -8};
	const Ray<> miss{{2, 0, -4}, {0, 0, 2}};
	ASSERT_FALSE(Intersect<float32>(miss, sphere, &interval));
	ASSERT_EQ(interval.Enter, -7);
	ASSERT_EQ(interval.Exit, -8);
	ASSERT_FALSE(Intersect<float32>(through, box, &interval, 1));
	ASSERT_EQ(interval.Enter, -7);
	ASSERT_EQ(interval.Exit, -8);
	ASSERT_TRUE(Intersect<float32>(through, box, &interval, 2));
	ASSERT_EQ(interval.Enter, 2);
	ASSERT_EQ(interval.Exit, 2);
}

TEST(GeometryQueriesTests, RayIntersectionsPreserveOutputOnMiss)
{
	const Box<> box{{-1, -1, -1}, {1, 1, 1}};
	RayInterval<> interval{-7, -8};
	ASSERT_TRUE(Intersect<float32>(Ray<>{{0, 0, -5}, {0, 0, 2}}, box, &interval));
	ASSERT_EQ(interval.Enter, 2.0f);
	ASSERT_EQ(interval.Exit, 3.0f);
	interval = {-7, -8};
	ASSERT_FALSE(Intersect<float32>(Ray<>{{2, 0, -5}, {0, 0, 2}}, box, &interval));
	ASSERT_EQ(interval.Enter, -7);
	ASSERT_EQ(interval.Exit, -8);
	const Sphere<> sphere{{}, 1};
	ASSERT_TRUE(Intersect<float32>(Ray<>{{1, 0, -4}, {0, 0, 2}}, sphere, &interval));
	ASSERT_EQ(interval.Enter, 2.0f);
	ASSERT_EQ(interval.Exit, 2.0f);
}

TEST(GeometryQueriesTests, TriangleAndPlaneIntersectionsHandleLimitsAndMisses)
{
	const Triangle<float64> triangle{{0, 0, 0}, {4, 0, 0}, {0, 4, 0}};
	const Ray<float64> ray{{1, 1, 4}, {0, 0, -2}};
	float64 parameter = -7.0;
	EXPECT_TRUE(Intersect(ray, triangle, &parameter, 2.0));
	EXPECT_DOUBLE_EQ(parameter, 2.0);
	parameter = -7.0;
	EXPECT_FALSE(Intersect(ray, triangle, &parameter, 1.0));
	EXPECT_DOUBLE_EQ(parameter, -7.0);
	EXPECT_FALSE(Intersect(Ray<float64>{{5, 5, 4}, {0, 0, -2}}, triangle, &parameter));
	EXPECT_FALSE(Intersect(Ray<float64>{{1, 1, 4}, {1, 0, 0}}, triangle, &parameter));
	EXPECT_FALSE(Intersect(ray, Triangle<float64>{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}, &parameter));
	EXPECT_DOUBLE_EQ(parameter, -7.0);

	const Plane<float64> plane{{0, 0, 2}, 0};
	EXPECT_TRUE(Intersect(ray, plane, &parameter));
	EXPECT_DOUBLE_EQ(parameter, 2.0);
	parameter = -7.0;
	EXPECT_FALSE(Intersect(ray, plane, &parameter, 1.0));
	EXPECT_FALSE(Intersect(Ray<float64>{{0, 0, 1}, {1, 0, 0}}, plane, &parameter));
	EXPECT_DOUBLE_EQ(parameter, -7.0);
	EXPECT_TRUE(Intersect(Ray<float64>{{0, 0, 0}, {1, 0, 0}}, plane, &parameter));
	EXPECT_DOUBLE_EQ(parameter, 0.0);
}

TEST(GeometryQueriesTests, ClosestPointsAndDistancesRespectVolumeInteriors)
{
	const Plane<float64> plane{{0, 2, 0}, -4};
	const Box<float64> box{{-1, -2, -3}, {1, 2, 3}};
	const Sphere<float64> sphere{{0, 0, 0}, 2};
	const Vector3d point{4, 4, 0};
	EXPECT_EQ(ClosestPoint(point, plane), (Vector3d{4, 2, 0}));
	EXPECT_DOUBLE_EQ(DistanceSquared(point, plane), 4.0);
	EXPECT_EQ(ClosestPoint(point, box), (Vector3d{1, 2, 0}));
	EXPECT_DOUBLE_EQ(DistanceSquared(point, box), 13.0);
	EXPECT_EQ(ClosestPoint(Vector3d{3, 0, 0}, sphere), (Vector3d{2, 0, 0}));
	EXPECT_DOUBLE_EQ(DistanceSquared(Vector3d{3, 0, 0}, sphere), 1.0);
	EXPECT_EQ(ClosestPoint(Vector3d{1, 0, 0}, sphere), (Vector3d{1, 0, 0}));
	EXPECT_DOUBLE_EQ(DistanceSquared(Vector3d{1, 0, 0}, sphere), 0.0);
}

TEST(GeometryQueriesTests, RayIntervalsStartAtZeroInsideAndPreserveOutputBehindRay)
{
	const Box<float64> box{{-1, -1, -1}, {1, 1, 1}};
	const Sphere<float64> sphere{{0, 0, 0}, 1};
	RayInterval<float64> interval{-7, -8};
	const Ray<float64> inside{{0, 0, 0}, {2, 0, 0}};
	ASSERT_TRUE(Intersect(inside, box, &interval));
	EXPECT_DOUBLE_EQ(interval.Enter, 0.0);
	EXPECT_DOUBLE_EQ(interval.Exit, 0.5);
	ASSERT_TRUE(Intersect(inside, sphere, &interval));
	EXPECT_DOUBLE_EQ(interval.Enter, 0.0);
	EXPECT_DOUBLE_EQ(interval.Exit, 0.5);
	interval = {-7, -8};
	const Ray<float64> away{{2, 0, 0}, {1, 0, 0}};
	EXPECT_FALSE(Intersect(away, box, &interval));
	EXPECT_FALSE(Intersect(away, sphere, &interval));
	EXPECT_DOUBLE_EQ(interval.Enter, -7.0);
	EXPECT_DOUBLE_EQ(interval.Exit, -8.0);
}

TEST(GeometryQueriesTests, TransformedBoundsHandlesTranslationAndEmptyInput)
{
	const Box<> box{{-1, -2, -3}, {1, 2, 3}};
	AffineTransform<> transform;
	transform.Translation = {4, 5, 6};
	const auto bounds = TransformedBounds(box, transform);
	ASSERT_TRUE(bounds);
	EXPECT_EQ(bounds->Min, (Vector3f{3, 3, 3}));
	EXPECT_EQ(bounds->Max, (Vector3f{5, 7, 9}));
	EXPECT_FALSE(TransformedBounds(Box<>::Empty(), transform));
}
