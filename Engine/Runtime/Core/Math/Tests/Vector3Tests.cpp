#include <Vector3.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace rox::core;
using namespace rox::math;

namespace
{
	bool isNear(const float32 actual, const float32 expected)
	{
		return std::isfinite(actual) && std::abs(actual - expected) <= 1.0e-5f;
	}
}

TEST(Vector3Tests, VectorOperationsAndFailedNormalization)
{
	const Vector3f vector{2.0f, -3.0f, 6.0f};
	ASSERT_TRUE(isNear(vector.Length(), 7.0f));
	const Vector3f normalized = vector.Normalized();
	ASSERT_TRUE(isNear(normalized.X, 2.0f / 7.0f));
	ASSERT_TRUE(isNear(normalized.Y, -3.0f / 7.0f));
	ASSERT_TRUE(isNear(normalized.Z, 6.0f / 7.0f));
	ASSERT_EQ(Vector3f::UnitX().Cross(Vector3f::UnitY()), Vector3f::UnitZ());
	ASSERT_EQ(Vector3f::UnitY().Cross(Vector3f::UnitX()), -Vector3f::UnitZ());
	ASSERT_EQ(vector.Dot({1.0f, 2.0f, 3.0f}), 14.0f);
	Vector3f output{7.0f, 8.0f, 9.0f};
	ASSERT_FALSE(Vector3f::Zero().TryNormalize(&output));
	ASSERT_EQ(output, Vector3f(7.0f, 8.0f, 9.0f));
	ASSERT_TRUE(isNear(Vector3f{3.0e20f, 4.0e20f, 0.0f}.Length() / 1.0e20f, 5.0f));
}

TEST(Vector3Tests, ArithmeticAndComponentOperations)
{
	const Vector3d value{6.0, -8.0, 10.0};
	const Vector3d other{2.0, 4.0, -5.0};
	EXPECT_EQ(+value, (Vector3d{6.0, -8.0, 10.0}));
	EXPECT_EQ(-value, (Vector3d{-6.0, 8.0, -10.0}));
	EXPECT_EQ(value + other, (Vector3d{8.0, -4.0, 5.0}));
	EXPECT_EQ(value - other, (Vector3d{4.0, -12.0, 15.0}));
	EXPECT_EQ(value * other, (Vector3d{12.0, -32.0, -50.0}));
	EXPECT_EQ(value / other, (Vector3d{3.0, -2.0, -2.0}));
	EXPECT_EQ(0.5 * value, (Vector3d{3.0, -4.0, 5.0}));
	EXPECT_EQ(value / 2.0, (Vector3d{3.0, -4.0, 5.0}));
	Vector3d modified = value;
	modified += other;
	modified -= Vector3d{1.0, 2.0, 3.0};
	modified *= 2.0;
	modified /= 2.0;
	EXPECT_EQ(modified, (Vector3d{7.0, -6.0, 2.0}));
	EXPECT_EQ(value.Min(other), (Vector3d{2.0, -8.0, -5.0}));
	EXPECT_EQ(value.Max(other), (Vector3d{6.0, 4.0, 10.0}));
	EXPECT_EQ(value.Abs(), (Vector3d{6.0, 8.0, 10.0}));
	EXPECT_EQ(value.Clamped({0.0, -5.0, 0.0}, {4.0, 5.0, 8.0}), (Vector3d{4.0, -5.0, 8.0}));
}

TEST(Vector3Tests, NormalizationAndToleranceBoundaries)
{
	const Vector3d value{2.0, -3.0, 6.0};
	EXPECT_DOUBLE_EQ(value.LengthSquared(), 49.0);
	EXPECT_DOUBLE_EQ(value.Distance(Vector3d::Zero()), 7.0);
	EXPECT_DOUBLE_EQ(value.DistanceSquared(Vector3d::Zero()), 49.0);
	EXPECT_TRUE((Vector3d{0.25, -0.25, 0.0}.IsNearlyZero(0.25)));
	EXPECT_FALSE((Vector3d{0.25, 0.0, 0.5}.IsNearlyZero(0.25)));
	EXPECT_TRUE(value.NearlyEquals({2.25, -3.25, 6.0}, 0.25));
	EXPECT_FALSE(value.NearlyEquals({2.0, -3.0, 6.5}, 0.25));
	Vector3d output{9.0, 8.0, 7.0};
	EXPECT_FALSE(value.TryNormalize(&output, 7.0));
	EXPECT_EQ(output, (Vector3d{9.0, 8.0, 7.0}));
	EXPECT_EQ(value.NormalizedOrZero(7.0), Vector3d::Zero());
	ASSERT_TRUE(value.TryNormalize(&output, 6.0));
	EXPECT_NEAR(output.X, 2.0 / 7.0, 1.0e-15);
	EXPECT_NEAR(output.Y, -3.0 / 7.0, 1.0e-15);
	EXPECT_NEAR(output.Z, 6.0 / 7.0, 1.0e-15);
	output = value;
	output.Normalize();
	EXPECT_NEAR(output.X, 2.0 / 7.0, 1.0e-15);
	EXPECT_NEAR(output.Y, -3.0 / 7.0, 1.0e-15);
	EXPECT_NEAR(output.Z, 6.0 / 7.0, 1.0e-15);
}

TEST(Vector3Tests, GeometryAndInterpolation)
{
	const Vector3d value{3.0, -4.0, 0.0};
	EXPECT_EQ(value.ClampedLength(0.0), Vector3d::Zero());
	EXPECT_EQ(value.ClampedLength(5.0), value);
	EXPECT_EQ(value.ProjectedOnto({2.0, 0.0, 0.0}), (Vector3d{3.0, 0.0, 0.0}));
	EXPECT_EQ(value.RejectedFrom({2.0, 0.0, 0.0}), (Vector3d{0.0, -4.0, 0.0}));
	EXPECT_EQ(value.Reflected({0.0, 1.0, 0.0}), (Vector3d{3.0, 4.0, 0.0}));
	EXPECT_EQ(value.Slide({0.0, 1.0, 0.0}), (Vector3d{3.0, 0.0, 0.0}));
	EXPECT_EQ(value.MoveTowards({3.0, 1.0, 0.0}, 2.0), (Vector3d{3.0, -2.0, 0.0}));
	EXPECT_EQ(value.MoveTowards({3.0, 1.0, 0.0}, 6.0), (Vector3d{3.0, 1.0, 0.0}));
	EXPECT_NEAR(Vector3d::UnitX().Angle(Vector3d::UnitY()), 1.5707963267948966, 1.0e-12);
	EXPECT_NEAR(Vector3d::UnitY().SignedAngle(Vector3d::UnitX(), Vector3d::UnitZ()), -1.5707963267948966, 1.0e-12);
	const Vector3d perpendicular = value.Perpendicular();
	EXPECT_NEAR(perpendicular.X, -0.8, 1.0e-12);
	EXPECT_NEAR(perpendicular.Y, -0.6, 1.0e-12);
	EXPECT_NEAR(perpendicular.Z, 0.0, 1.0e-12);
	EXPECT_EQ(Vector3d::Lerp({2.0, 4.0, 6.0}, {6.0, -4.0, 2.0}, 1.5), (Vector3d{8.0, -8.0, 0.0}));
}

TEST(Vector3Tests, RefractionWritesResultAndHandlesTotalInternalReflection)
{
	Vector3d direction{0.0, -1.0, 0.0};
	ASSERT_TRUE(direction.Refracted(Vector3d::UnitY(), 0.5, &direction));
	EXPECT_EQ(direction, (Vector3d{0.0, -1.0, 0.0}));
	const Vector3d incident{0.8, -0.6, 0.0};
	Vector3d output{7.0, 8.0, 9.0};
	EXPECT_FALSE(incident.Refracted(Vector3d::UnitY(), 2.0, &output));
	EXPECT_EQ(output, Vector3d::Zero());
}

TEST(Vector3Tests, NonfiniteNormalizationPreservesOutput)
{
	const Vector3f value{1.0f, std::numeric_limits<float32>::infinity(), 2.0f};
	Vector3f output{2.0f, 3.0f, 4.0f};
	EXPECT_FALSE(value.IsFinite());
	EXPECT_FALSE(value.IsNearlyZero());
	EXPECT_FALSE(value.NearlyEquals(value));
	EXPECT_FALSE(value.TryNormalize(&output));
	EXPECT_EQ(output, (Vector3f{2.0f, 3.0f, 4.0f}));
}

TEST(Vector3Tests, ConstantsAndPrecisionConversion)
{
	EXPECT_EQ(Vector3f::Zero(), (Vector3f{0.0f, 0.0f, 0.0f}));
	EXPECT_EQ(Vector3f::One(), (Vector3f{1.0f, 1.0f, 1.0f}));
	EXPECT_EQ(Vector3f::Right(), (Vector3f{1.0f, 0.0f, 0.0f}));
	EXPECT_EQ(Vector3f::Up(), (Vector3f{0.0f, 1.0f, 0.0f}));
	EXPECT_EQ(Vector3f::Forward(), (Vector3f{0.0f, 0.0f, 1.0f}));
	EXPECT_EQ(Vector3d(Vector3f{1.5f, -2.25f, 3.5f}), (Vector3d{1.5, -2.25, 3.5}));
}

TEST(Vector3Tests, ExtremeNormalizationAndObliqueProjection)
{
	const Vector3d large{2.0e200, -3.0e200, 6.0e200};
	Vector3d normalized;
	ASSERT_TRUE(large.TryNormalize(&normalized));
	EXPECT_NEAR(normalized.X, 2.0 / 7.0, 1.0e-15);
	EXPECT_NEAR(normalized.Y, -3.0 / 7.0, 1.0e-15);
	EXPECT_NEAR(normalized.Z, 6.0 / 7.0, 1.0e-15);
	const Vector3d value{3.0, 1.0, 2.0};
	const Vector3d projected = value.ProjectedOnto({1.0, 2.0, 2.0});
	EXPECT_NEAR(projected.X, 1.0, 1.0e-15);
	EXPECT_NEAR(projected.Y, 2.0, 1.0e-15);
	EXPECT_NEAR(projected.Z, 2.0, 1.0e-15);
	const Vector3d rejected = value.RejectedFrom({1.0, 2.0, 2.0});
	EXPECT_NEAR(rejected.X, 2.0, 1.0e-15);
	EXPECT_NEAR(rejected.Y, -1.0, 1.0e-15);
	EXPECT_NEAR(rejected.Z, 0.0, 1.0e-15);
}
