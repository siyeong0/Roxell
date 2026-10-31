#include <Vector2.hpp>

#include <gtest/gtest.h>

#include <limits>

using namespace rox::math;

TEST(Vector2Tests, NormalizationAndProjection)
{
	const Vector2f value(3.0f, 4.0f);
	EXPECT_FLOAT_EQ(value.Length(), 5.0f);
	EXPECT_TRUE(value.Normalized().NearlyEquals(Vector2f(0.6f, 0.8f)));
	EXPECT_TRUE(value.ProjectedOnto(Vector2f(2.0f, 0.0f)).NearlyEquals(Vector2f(3.0f, 0.0f)));
	EXPECT_TRUE(value.RejectedFrom(Vector2f(2.0f, 0.0f)).NearlyEquals(Vector2f(0.0f, 4.0f)));
	EXPECT_TRUE(Vector2f::Zero().NormalizedOrZero().IsNearlyZero());
}

TEST(Vector2Tests, ArithmeticAndComponentOperations)
{
	const Vector2f value{6.0f, -8.0f};
	const Vector2f other{2.0f, 4.0f};
	EXPECT_EQ(+value, (Vector2f{6.0f, -8.0f}));
	EXPECT_EQ(-value, (Vector2f{-6.0f, 8.0f}));
	EXPECT_EQ(value + other, (Vector2f{8.0f, -4.0f}));
	EXPECT_EQ(value - other, (Vector2f{4.0f, -12.0f}));
	EXPECT_EQ(value * other, (Vector2f{12.0f, -32.0f}));
	EXPECT_EQ(value / other, (Vector2f{3.0f, -2.0f}));
	EXPECT_EQ(0.5f * value, (Vector2f{3.0f, -4.0f}));
	EXPECT_EQ(value / 2.0f, (Vector2f{3.0f, -4.0f}));
	Vector2f modified = value;
	modified += other;
	modified -= Vector2f{1.0f, 2.0f};
	modified *= 2.0f;
	modified /= 2.0f;
	EXPECT_EQ(modified, (Vector2f{7.0f, -6.0f}));
	EXPECT_EQ(value.Min(other), (Vector2f{2.0f, -8.0f}));
	EXPECT_EQ(value.Max(other), (Vector2f{6.0f, 4.0f}));
	EXPECT_EQ(value.Abs(), (Vector2f{6.0f, 8.0f}));
	EXPECT_EQ(value.Clamped({0.0f, -5.0f}, {4.0f, 5.0f}), (Vector2f{4.0f, -5.0f}));
}

TEST(Vector2Tests, DistanceToleranceAndNormalizationBoundaries)
{
	const Vector2d value{3.0, 4.0};
	EXPECT_DOUBLE_EQ(value.Dot({-2.0, 5.0}), 14.0);
	EXPECT_DOUBLE_EQ(value.LengthSquared(), 25.0);
	EXPECT_DOUBLE_EQ(value.Distance({0.0, 0.0}), 5.0);
	EXPECT_DOUBLE_EQ(value.DistanceSquared({0.0, 0.0}), 25.0);
	EXPECT_TRUE((Vector2d{0.25, -0.25}.IsNearlyZero(0.25)));
	EXPECT_FALSE((Vector2d{0.25, -0.5}.IsNearlyZero(0.25)));
	EXPECT_TRUE(value.NearlyEquals({3.25, 3.75}, 0.25));
	EXPECT_FALSE(value.NearlyEquals({3.5, 4.0}, 0.25));
	Vector2d output{9.0, 8.0};
	EXPECT_FALSE(value.TryNormalize(&output, 5.0));
	EXPECT_EQ(output, (Vector2d{9.0, 8.0}));
	EXPECT_EQ(value.NormalizedOrZero(5.0), Vector2d::Zero());
	ASSERT_TRUE(value.TryNormalize(&output, 4.0));
	EXPECT_DOUBLE_EQ(output.X, 0.6);
	EXPECT_DOUBLE_EQ(output.Y, 0.8);
	output = value;
	output.Normalize();
	EXPECT_DOUBLE_EQ(output.X, 0.6);
	EXPECT_DOUBLE_EQ(output.Y, 0.8);
}

TEST(Vector2Tests, GeometryAndInterpolation)
{
	const Vector2d value{3.0, -4.0};
	EXPECT_EQ(value.ClampedLength(0.0), Vector2d::Zero());
	EXPECT_EQ(value.ClampedLength(5.0), value);
	EXPECT_EQ(value.ClampedLength(10.0), value);
	EXPECT_EQ(value.Reflected({0.0, 1.0}), (Vector2d{3.0, 4.0}));
	EXPECT_EQ(value.Slide({0.0, 1.0}), (Vector2d{3.0, 0.0}));
	EXPECT_EQ(value.MoveTowards({3.0, 1.0}, 2.0), (Vector2d{3.0, -2.0}));
	EXPECT_EQ(value.MoveTowards({3.0, 1.0}, 6.0), (Vector2d{3.0, 1.0}));
	EXPECT_NEAR(Vector2d::UnitX().Angle(Vector2d::UnitY()), 1.5707963267948966, 1.0e-12);
	EXPECT_NEAR(Vector2d::UnitY().SignedAngle(Vector2d::UnitX()), -1.5707963267948966, 1.0e-12);
	EXPECT_EQ(Vector2d::Lerp({2.0, 4.0}, {6.0, -4.0}, 1.5), (Vector2d{8.0, -8.0}));
}

TEST(Vector2Tests, NonfiniteNormalizationPreservesOutput)
{
	const Vector2f value{std::numeric_limits<rox::core::float32>::infinity(), 1.0f};
	Vector2f output{2.0f, 3.0f};
	EXPECT_FALSE(value.IsFinite());
	EXPECT_FALSE(value.IsNearlyZero());
	EXPECT_FALSE(value.NearlyEquals(value));
	EXPECT_FALSE(value.TryNormalize(&output));
	EXPECT_EQ(output, (Vector2f{2.0f, 3.0f}));
}

TEST(Vector2Tests, ConstantsAndPrecisionConversion)
{
	EXPECT_EQ(Vector2f::Zero(), (Vector2f{0.0f, 0.0f}));
	EXPECT_EQ(Vector2f::One(), (Vector2f{1.0f, 1.0f}));
	EXPECT_EQ(Vector2f::UnitX(), (Vector2f{1.0f, 0.0f}));
	EXPECT_EQ(Vector2f::UnitY(), (Vector2f{0.0f, 1.0f}));
	EXPECT_EQ(Vector2d(Vector2f{1.5f, -2.25f}), (Vector2d{1.5, -2.25}));
}

TEST(Vector2Tests, ExtremeNormalizationAndObliqueProjection)
{
	const Vector2d large{3.0e200, -4.0e200};
	Vector2d normalized;
	ASSERT_TRUE(large.TryNormalize(&normalized));
	EXPECT_NEAR(normalized.X, 0.6, 1.0e-15);
	EXPECT_NEAR(normalized.Y, -0.8, 1.0e-15);
	const Vector2d value{5.0, 1.0};
	const Vector2d projected = value.ProjectedOnto({1.0, 2.0});
	EXPECT_NEAR(projected.X, 1.4, 1.0e-15);
	EXPECT_NEAR(projected.Y, 2.8, 1.0e-15);
	const Vector2d rejected = value.RejectedFrom({1.0, 2.0});
	EXPECT_NEAR(rejected.X, 3.6, 1.0e-15);
	EXPECT_NEAR(rejected.Y, -1.8, 1.0e-15);
}
