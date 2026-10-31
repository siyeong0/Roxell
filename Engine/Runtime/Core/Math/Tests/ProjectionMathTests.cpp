#include <ProjectionMath.h>

#include <gtest/gtest.h>

#include <limits>

using namespace rox::math;

TEST(ProjectionMathTests, IdentityProjectionMapsViewportAndRoundTrips)
{
	const ProjectionViewport<> viewport{0, 0, 100, 100, 0, 1};
	const Matrix4x4f identity = Matrix4x4f::Identity();
	Vector3f screen{};
	ASSERT_TRUE(ProjectToScreen(Vector3f(0, 0, 0.5f), identity, viewport, &screen));
	EXPECT_EQ(screen, Vector3f(50, 50, 0.5f));
	Vector3f point{};
	ASSERT_TRUE(UnprojectFromScreen(screen, identity, viewport, &point));
	EXPECT_EQ(point, Vector3f(0, 0, 0.5f));
}

TEST(ProjectionMathTests, HorizonFailurePreservesOutputs)
{
	const ProjectionViewport<> viewport{0, 0, 100, 100, 0, 1};
	const Matrix4x4f horizon = Matrix4x4f::Zero();
	const Vector3f sentinel{7, 8, 9};
	Vector3f output = sentinel;
	EXPECT_FALSE(ProjectToScreen(Vector3f{1, 2, 3}, horizon, viewport, &output));
	EXPECT_EQ(output, sentinel);
	EXPECT_FALSE(UnprojectFromScreen(Vector3f{50, 50, 0.5f}, horizon, viewport, &output));
	EXPECT_EQ(output, sentinel);
	Ray<> ray{{1, 2, 3}, {0, 1, 0}};
	EXPECT_FALSE(BuildScreenRay(Vector2f{50, 50}, horizon, viewport, &ray));
	EXPECT_EQ(ray.Origin, (Vector3f{1, 2, 3}));
	EXPECT_EQ(ray.Direction, (Vector3f{0, 1, 0}));
}

TEST(ProjectionMathTests, ScreenRayUsesPhysicalNearPlaneForBothDepthModes)
{
	const ProjectionViewport<> viewport{0, 0, 100, 100, 0, 1};
	Ray<> ray;
	ASSERT_TRUE(BuildScreenRay(Vector2f{50, 50}, Matrix4x4f::Identity(), viewport, &ray));
	EXPECT_EQ(ray.Origin, Vector3f::Zero());
	EXPECT_EQ(ray.Direction, Vector3f::UnitZ());
	ASSERT_TRUE(BuildScreenRay(Vector2f{50, 50}, Matrix4x4f::Identity(), viewport, &ray, EProjectionDepth::Reversed));
	EXPECT_EQ(ray.Origin, Vector3f::UnitZ());
	EXPECT_EQ(ray.Direction, -Vector3f::UnitZ());
}

TEST(ProjectionMathTests, OffCenterDepthAndInfiniteProjectionAgreeWithReversal)
{
	const ProjectionViewport<rox::core::float64> viewport{10, 20, 200, 100, 0, 1};
	const Matrix4x4d forward = PerspectiveOffCenterLH(-1.0, 3.0, -2.0, 2.0, 2.0, 10.0);
	const Matrix4x4d reversed = PerspectiveOffCenterLH(-1.0, 3.0, -2.0, 2.0, 2.0, 10.0, EProjectionDepth::Reversed);
	Vector3d screen;
	ASSERT_TRUE(ProjectToScreen(Vector3d{-1, 2, 2}, forward, viewport, &screen));
	EXPECT_NEAR(screen.X, 10.0, 1.0e-12);
	EXPECT_NEAR(screen.Y, 20.0, 1.0e-12);
	EXPECT_NEAR(screen.Z, 0.0, 1.0e-12);
	ASSERT_TRUE(ProjectToScreen(Vector3d{0, 0, 10}, reversed, viewport, &screen));
	EXPECT_NEAR(screen.Z, 0.0, 1.0e-12);
	const Matrix4x4d reversedForward = ReverseProjectionDepth(forward);
	for (rox::core::uint32 index = 0; index < 16; ++index)
	{
		EXPECT_NEAR(reversedForward.Data()[index], reversed.Data()[index], 1.0e-12);
	}
	const Matrix4x4d infinite = PerspectiveOffCenterInfiniteLH(-1.0, 1.0, -1.0, 1.0, 1.0);
	const Matrix4x4d fov = PerspectiveFovInfiniteLH(1.5707963267948966, 1.0, 1.0);
	for (rox::core::uint32 index = 0; index < 16; ++index)
	{
		EXPECT_NEAR(infinite.Data()[index], fov.Data()[index], 1.0e-12);
	}
}

TEST(ProjectionMathTests, DegenerateScreenRayPreservesCompleteOutput)
{
	const ProjectionViewport<> viewport{0, 0, 100, 100, 0, 1};
	Matrix4x4f collapsed = Matrix4x4f::Identity();
	collapsed[2][2] = 0;
	Ray<> ray{{1, 2, 3}, {0, 1, 0}};
	EXPECT_FALSE(BuildScreenRay(Vector2f{50, 50}, collapsed, viewport, &ray));
	EXPECT_EQ(ray.Origin, (Vector3f{1, 2, 3}));
	EXPECT_EQ(ray.Direction, (Vector3f{0, 1, 0}));
}

TEST(ProjectionMathTests, InfinitePerspectiveUnprojectsAndBuildsRaysInBothDepthModes)
{
	const ProjectionViewport<rox::core::float64> viewport{10, 20, 200, 100, 0, 1};
	const Vector3d point{1, -0.5, 4};
	for (const EProjectionDepth depth : {EProjectionDepth::Forward, EProjectionDepth::Reversed})
	{
		const Matrix4x4d projection = PerspectiveFovInfiniteLH(1.5707963267948966, 2.0, 1.0, depth);
		Matrix4x4d inverse;
		ASSERT_TRUE(projection.TryInverse(&inverse));
		Vector3d screen;
		ASSERT_TRUE(ProjectToScreen(point, projection, viewport, &screen));
		Vector3d restored;
		ASSERT_TRUE(UnprojectFromScreen(screen, inverse, viewport, &restored));
		EXPECT_TRUE(restored.NearlyEquals(point, 1.0e-12));
		Ray<rox::core::float64> ray;
		ASSERT_TRUE(BuildScreenRay(Vector2d{110, 70}, inverse, viewport, &ray, depth));
		EXPECT_TRUE(ray.Origin.NearlyEquals(Vector3d{0, 0, 1}, 1.0e-12));
		EXPECT_TRUE(ray.Direction.NearlyEquals(Vector3d::UnitZ(), 1.0e-12));
	}
}

TEST(ProjectionMathTests, OutOfRangeProjectionPreservesOutput)
{
	const rox::core::float32 maximum = std::numeric_limits<rox::core::float32>::max();
	const ProjectionViewport<> viewport{0, 0, maximum, 100, 0, 1};
	const Vector3f sentinel{7, 8, 9};
	Vector3f output = sentinel;
	EXPECT_FALSE(ProjectToScreen(Vector3f{2, 0, 0}, Matrix4x4f::Identity(), viewport, &output));
	EXPECT_EQ(output, sentinel);
	const ProjectionViewport<> ordinaryViewport{0, 0, 100, 100, 0, 1};
	EXPECT_FALSE(UnprojectFromScreen(Vector3f{maximum, 50, 0.5f}, Matrix4x4f::Identity(), ordinaryViewport, &output));
	EXPECT_EQ(output, sentinel);
}
