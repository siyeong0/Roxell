#include <GeometryVolumeQueries.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(GeometryVolumeQueriesTests, PlaneClassifiesSphereTangency)
{
	const Plane<> plane(Vector3f(0.0f, 2.0f, 0.0f), 0.0f);
	const Sphere<> positive(Vector3f(0.0f, 3.0f, 0.0f), 1.0f);
	const Sphere<> tangent(Vector3f(0.0f, 1.0f, 0.0f), 1.0f);
	const Sphere<> negative(Vector3f(0.0f, -3.0f, 0.0f), 1.0f);
	EXPECT_EQ(Classify(plane, positive), EPlaneSide::Positive);
	EXPECT_EQ(Classify(plane, tangent), EPlaneSide::Intersecting);
	EXPECT_EQ(Classify(plane, negative), EPlaneSide::Negative);
}

TEST(GeometryVolumeQueriesTests, BoxIntersectionHandlesTangencyAndPreservesOutputOnMiss)
{
	const Box<> first{{0, 0, 0}, {2, 2, 2}};
	const Box<> overlap{{1, -1, 1}, {3, 1, 3}};
	Box<> output{{-7, -7, -7}, {-6, -6, -6}};
	ASSERT_TRUE(Intersection(first, overlap, &output));
	EXPECT_EQ(output.Min, (Vector3f{1, 0, 1}));
	EXPECT_EQ(output.Max, (Vector3f{2, 1, 2}));
	const Box<> combined = Union(first, overlap);
	EXPECT_EQ(combined.Min, (Vector3f{0, -1, 0}));
	EXPECT_EQ(combined.Max, (Vector3f{3, 2, 3}));
	const Box<> expanded = Expanded(first, Vector3f{1, 2, 3});
	EXPECT_EQ(expanded.Min, (Vector3f{-1, -2, -3}));
	EXPECT_EQ(expanded.Max, (Vector3f{3, 4, 5}));
	const Box<> touching{{2, 1, 1}, {4, 2, 2}};
	ASSERT_TRUE(Intersection(first, touching, &output));
	EXPECT_EQ(output.Min, (Vector3f{2, 1, 1}));
	EXPECT_EQ(output.Max, (Vector3f{2, 2, 2}));
	const Box<> previous = output;
	EXPECT_FALSE(Intersection(first, Box<>({3, 3, 3}, {4, 4, 4}), &output));
	EXPECT_EQ(output.Min, previous.Min);
	EXPECT_EQ(output.Max, previous.Max);
}

TEST(GeometryVolumeQueriesTests, ClosestPointsCoverCrossingParallelAndDegenerateSegments)
{
	const LineSegment<> horizontal{{-1, 0, 0}, {1, 0, 0}};
	const LineSegment<> crossing{{0, -1, 0}, {0, 1, 0}};
	const auto intersection = ClosestPoints(horizontal, crossing);
	EXPECT_EQ(intersection.First, Vector3f::Zero());
	EXPECT_EQ(intersection.Second, Vector3f::Zero());
	EXPECT_FLOAT_EQ(intersection.FirstParameter, 0.5f);
	EXPECT_FLOAT_EQ(intersection.SecondParameter, 0.5f);
	EXPECT_FLOAT_EQ(DistanceSquared(horizontal, crossing), 0.0f);
	const LineSegment<> parallel{{-1, 2, 0}, {1, 2, 0}};
	EXPECT_FLOAT_EQ(DistanceSquared(horizontal, parallel), 4.0f);
	const LineSegment<> point{{3, 0, 0}, {3, 0, 0}};
	const auto closest = ClosestPoints(horizontal, point);
	EXPECT_EQ(closest.First, (Vector3f{1, 0, 0}));
	EXPECT_EQ(closest.Second, (Vector3f{3, 0, 0}));
	EXPECT_FLOAT_EQ(DistanceSquared(horizontal, point), 4.0f);
}

TEST(GeometryVolumeQueriesTests, SphereAndCapsuleOverlapIncludesTangency)
{
	const Box<> box{{-1, -1, -1}, {1, 1, 1}};
	EXPECT_TRUE(Overlaps(Sphere<>({2, 0, 0}, 1), box));
	EXPECT_FALSE(Overlaps(Sphere<>({3, 0, 0}, 1), box));
	const Capsule<> capsule(LineSegment<>({0, -1, 0}, {0, 1, 0}), 1);
	EXPECT_TRUE(Overlaps(capsule, Sphere<>({2, 0, 0}, 1)));
	EXPECT_FALSE(Overlaps(capsule, Sphere<>({3, 0, 0}, 1)));
	const Capsule<> touching(LineSegment<>({2, -1, 0}, {2, 1, 0}), 1);
	const Capsule<> separated(LineSegment<>({4, -1, 0}, {4, 1, 0}), 1);
	EXPECT_TRUE(Overlaps(capsule, touching));
	EXPECT_FALSE(Overlaps(capsule, separated));
	EXPECT_FLOAT_EQ(DistanceSquared(capsule, separated), 4.0f);
}

TEST(GeometryVolumeQueriesTests, FrustumClassifiesVolumesAndReturnsCorners)
{
	const Frustum<> frustum = Frustum<>::FromViewProjection(Matrix4x4f::Identity());
	EXPECT_EQ(Classify(frustum, Sphere<>({0, 0, 0.5f}, 0.25f)), EFrustumContainment::Inside);
	EXPECT_EQ(Classify(frustum, Sphere<>({1, 0, 0.5f}, 0.25f)), EFrustumContainment::Intersecting);
	EXPECT_EQ(Classify(frustum, Sphere<>({2, 0, 0.5f}, 0.25f)), EFrustumContainment::Outside);
	EXPECT_EQ(Classify(frustum, OrientedBox<>({0, 0, 0.5f}, {0.25f, 0.25f, 0.25f})), EFrustumContainment::Inside);
	EXPECT_EQ(Classify(frustum, OrientedBox<>({1, 0, 0.5f}, {0.25f, 0.25f, 0.25f})), EFrustumContainment::Intersecting);
	EXPECT_EQ(Classify(frustum, OrientedBox<>({2, 0, 0.5f}, {0.25f, 0.25f, 0.25f})), EFrustumContainment::Outside);
	EXPECT_EQ(Classify(frustum, Capsule<>(LineSegment<>({0, 0, 0.4f}, {0, 0, 0.6f}), 0.1f)), EFrustumContainment::Inside);
	EXPECT_EQ(Classify(frustum, Capsule<>(LineSegment<>({1, 0, 0.4f}, {1, 0, 0.6f}), 0.1f)), EFrustumContainment::Intersecting);
	EXPECT_EQ(Classify(frustum, Capsule<>(LineSegment<>({2, 0, 0.4f}, {2, 0, 0.6f}), 0.1f)), EFrustumContainment::Outside);
	FrustumCorners<> corners;
	ASSERT_TRUE(GetCorners(frustum, &corners));
	EXPECT_EQ(corners.Points[0], (Vector3f{-1, -1, 0}));
	EXPECT_EQ(corners.Points[7], (Vector3f{1, 1, 1}));

	Frustum<> infinite = frustum;
	infinite.Far = Plane<>({0, 0, 0}, 1);
	corners.Points[0] = {7, 8, 9};
	EXPECT_FALSE(GetCorners(infinite, &corners));
	EXPECT_EQ(corners.Points[0], (Vector3f{7, 8, 9}));
}

TEST(GeometryVolumeQueriesTests, PlaneClassifiesBoxesAndCapsulesOnEachSide)
{
	const Plane<> plane({0, 2, 0}, 0);
	EXPECT_EQ(Classify(plane, Box<>({-1, 1, -1}, {1, 2, 1})), EPlaneSide::Positive);
	EXPECT_EQ(Classify(plane, Box<>({-1, -2, -1}, {1, -1, 1})), EPlaneSide::Negative);
	EXPECT_EQ(Classify(plane, Box<>({-1, 0, -1}, {1, 1, 1})), EPlaneSide::Intersecting);
	EXPECT_EQ(Classify(plane, OrientedBox<>({0, 2, 0}, {1, 1, 1})), EPlaneSide::Positive);
	EXPECT_EQ(Classify(plane, OrientedBox<>({0, -2, 0}, {1, 1, 1})), EPlaneSide::Negative);
	EXPECT_EQ(Classify(plane, OrientedBox<>({0, 1, 0}, {1, 1, 1})), EPlaneSide::Intersecting);
	EXPECT_EQ(Classify(plane, Capsule<>(LineSegment<>({0, 2, 0}, {1, 2, 0}), 1)), EPlaneSide::Positive);
	EXPECT_EQ(Classify(plane, Capsule<>(LineSegment<>({0, -2, 0}, {1, -2, 0}), 1)), EPlaneSide::Negative);
	EXPECT_EQ(Classify(plane, Capsule<>(LineSegment<>({0, 1, 0}, {1, 1, 0}), 1)), EPlaneSide::Intersecting);
}

TEST(GeometryVolumeQueriesTests, OrientedBoxAndCapsuleRayQueriesRespectLimits)
{
	const OrientedBox<> box({0, 0, 0}, {1, 1, 1});
	const Capsule<> capsule(LineSegment<>({0, -1, 0}, {0, 1, 0}), 1);
	EXPECT_TRUE(Overlaps(box, Sphere<>({2, 0, 0}, 1)));
	EXPECT_FALSE(Overlaps(box, Sphere<>({3, 0, 0}, 1)));
	EXPECT_TRUE(Overlaps(box, OrientedBox<>({2, 0, 0}, {1, 1, 1})));
	EXPECT_FALSE(Overlaps(box, OrientedBox<>({3, 0, 0}, {1, 1, 1})));
	const Ray<> ray{{-3, 0, 0}, {1, 0, 0}};
	RayInterval<> interval{-7, -8};
	ASSERT_TRUE(Intersect(ray, box, &interval));
	EXPECT_FLOAT_EQ(interval.Enter, 2.0f);
	EXPECT_FLOAT_EQ(interval.Exit, 4.0f);
	ASSERT_TRUE(Intersect(ray, capsule, &interval));
	EXPECT_FLOAT_EQ(interval.Enter, 2.0f);
	EXPECT_FLOAT_EQ(interval.Exit, 4.0f);
	interval = {-7, -8};
	EXPECT_FALSE(Intersect(ray, box, &interval, 1.0f));
	EXPECT_FALSE(Intersect(ray, capsule, &interval, 1.0f));
	EXPECT_FLOAT_EQ(interval.Enter, -7.0f);
	EXPECT_FLOAT_EQ(interval.Exit, -8.0f);
}
