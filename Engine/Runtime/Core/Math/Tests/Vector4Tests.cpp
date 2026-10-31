#include <Vector4.hpp>

#include <gtest/gtest.h>

#include <limits>

using namespace rox::math;

TEST(Vector4Tests, LengthAndClamping)
{
	const Vector4f value(0.0f, 3.0f, 0.0f, 4.0f);
	EXPECT_FLOAT_EQ(value.Length(), 5.0f);
	EXPECT_TRUE(value.Normalized().NearlyEquals(Vector4f(0.0f, 0.6f, 0.0f, 0.8f)));
	EXPECT_TRUE(value.ClampedLength(2.5f).NearlyEquals(Vector4f(0.0f, 1.5f, 0.0f, 2.0f)));
	EXPECT_TRUE(Vector4f::Zero().NormalizedOrZero().IsNearlyZero());
}

TEST(Vector4Tests, ArithmeticAndComponentOperations)
{
	const Vector4d value{6.0, -8.0, 10.0, -12.0};
	const Vector4d other{2.0, 4.0, -5.0, 3.0};
	EXPECT_EQ(+value, (Vector4d{6.0, -8.0, 10.0, -12.0}));
	EXPECT_EQ(-value, (Vector4d{-6.0, 8.0, -10.0, 12.0}));
	EXPECT_EQ(value + other, (Vector4d{8.0, -4.0, 5.0, -9.0}));
	EXPECT_EQ(value - other, (Vector4d{4.0, -12.0, 15.0, -15.0}));
	EXPECT_EQ(value * other, (Vector4d{12.0, -32.0, -50.0, -36.0}));
	EXPECT_EQ(value / other, (Vector4d{3.0, -2.0, -2.0, -4.0}));
	EXPECT_EQ(0.5 * value, (Vector4d{3.0, -4.0, 5.0, -6.0}));
	EXPECT_EQ(value / 2.0, (Vector4d{3.0, -4.0, 5.0, -6.0}));
	Vector4d modified = value;
	modified += other;
	modified -= Vector4d{1.0, 2.0, 3.0, 4.0};
	modified *= 2.0;
	modified /= 2.0;
	EXPECT_EQ(modified, (Vector4d{7.0, -6.0, 2.0, -13.0}));
	EXPECT_EQ(value.Min(other), (Vector4d{2.0, -8.0, -5.0, -12.0}));
	EXPECT_EQ(value.Max(other), (Vector4d{6.0, 4.0, 10.0, 3.0}));
	EXPECT_EQ(value.Abs(), (Vector4d{6.0, 8.0, 10.0, 12.0}));
	EXPECT_EQ(value.Clamped({0.0, -5.0, 0.0, -10.0}, {4.0, 5.0, 8.0, 0.0}), (Vector4d{4.0, -5.0, 8.0, -10.0}));
}

TEST(Vector4Tests, NormalizationAndToleranceBoundaries)
{
	const Vector4d value{0.0, 3.0, 0.0, 4.0};
	EXPECT_DOUBLE_EQ(value.Dot({1.0, -2.0, 3.0, 5.0}), 14.0);
	EXPECT_DOUBLE_EQ(value.LengthSquared(), 25.0);
	EXPECT_DOUBLE_EQ(value.Distance(Vector4d::Zero()), 5.0);
	EXPECT_DOUBLE_EQ(value.DistanceSquared(Vector4d::Zero()), 25.0);
	EXPECT_TRUE((Vector4d{0.25, -0.25, 0.0, 0.0}.IsNearlyZero(0.25)));
	EXPECT_FALSE((Vector4d{0.0, 0.0, 0.0, 0.5}.IsNearlyZero(0.25)));
	EXPECT_TRUE(value.NearlyEquals({0.25, 2.75, 0.0, 4.0}, 0.25));
	EXPECT_FALSE(value.NearlyEquals({0.0, 3.0, 0.0, 4.5}, 0.25));
	Vector4d output{9.0, 8.0, 7.0, 6.0};
	EXPECT_FALSE(value.TryNormalize(&output, 5.0));
	EXPECT_EQ(output, (Vector4d{9.0, 8.0, 7.0, 6.0}));
	EXPECT_EQ(value.NormalizedOrZero(5.0), Vector4d::Zero());
	ASSERT_TRUE(value.TryNormalize(&output, 4.0));
	EXPECT_DOUBLE_EQ(output.Y, 0.6);
	EXPECT_DOUBLE_EQ(output.W, 0.8);
	output = value;
	output.Normalize();
	EXPECT_DOUBLE_EQ(output.X, 0.0);
	EXPECT_DOUBLE_EQ(output.Y, 0.6);
	EXPECT_DOUBLE_EQ(output.Z, 0.0);
	EXPECT_DOUBLE_EQ(output.W, 0.8);
}

TEST(Vector4Tests, LengthClampingConversionAndInterpolation)
{
	const Vector4d value{0.0, 3.0, 0.0, 4.0};
	EXPECT_EQ(value.ClampedLength(0.0), Vector4d::Zero());
	EXPECT_EQ(value.ClampedLength(5.0), value);
	EXPECT_EQ(value.ClampedLength(10.0), value);
	EXPECT_EQ(value.ClampedLength(2.5), (Vector4d{0.0, 1.5, 0.0, 2.0}));
	const Vector3d xyz{1.0, 2.0, 3.0};
	EXPECT_EQ((Vector4d{xyz, 4.0}.XYZ()), xyz);
	EXPECT_EQ(Vector4d::Lerp({2.0, 4.0, 6.0, 8.0}, {6.0, -4.0, 2.0, 0.0}, 1.5), (Vector4d{8.0, -8.0, 0.0, -4.0}));
}

TEST(Vector4Tests, NonfiniteNormalizationPreservesOutput)
{
	const Vector4f value{1.0f, 2.0f, 3.0f, std::numeric_limits<rox::core::float32>::infinity()};
	Vector4f output{2.0f, 3.0f, 4.0f, 5.0f};
	EXPECT_FALSE(value.IsFinite());
	EXPECT_FALSE(value.IsNearlyZero());
	EXPECT_FALSE(value.NearlyEquals(value));
	EXPECT_FALSE(value.TryNormalize(&output));
	EXPECT_EQ(output, (Vector4f{2.0f, 3.0f, 4.0f, 5.0f}));
}

TEST(Vector4Tests, ConstantsAndPrecisionConversion)
{
	EXPECT_EQ(Vector4f::Zero(), (Vector4f{0.0f, 0.0f, 0.0f, 0.0f}));
	EXPECT_EQ(Vector4f::One(), (Vector4f{1.0f, 1.0f, 1.0f, 1.0f}));
	EXPECT_EQ(Vector4d(Vector4f{1.5f, -2.25f, 3.5f, -4.0f}), (Vector4d{1.5, -2.25, 3.5, -4.0}));
}

TEST(Vector4Tests, ExtremeNormalizationAndInterpolation)
{
	const Vector4d large{1.0e200, -2.0e200, 2.0e200, -4.0e200};
	Vector4d normalized;
	ASSERT_TRUE(large.TryNormalize(&normalized));
	EXPECT_NEAR(normalized.X, 0.2, 1.0e-15);
	EXPECT_NEAR(normalized.Y, -0.4, 1.0e-15);
	EXPECT_NEAR(normalized.Z, 0.4, 1.0e-15);
	EXPECT_NEAR(normalized.W, -0.8, 1.0e-15);
	const double maximum = std::numeric_limits<double>::max();
	const Vector4d midpoint = Vector4d::Lerp({maximum, -maximum, maximum, -maximum},
	                                         {-maximum, maximum, -maximum, maximum}, 0.5);
	EXPECT_EQ(midpoint, Vector4d::Zero());
}
