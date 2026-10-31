#include <Matrix4x4.hpp>
#include <Quaternion.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

using namespace rox::core;
using namespace rox::math;

namespace
{
	bool isNear(const float32 actual, const float32 expected)
	{
		return std::isfinite(actual) && std::abs(actual - expected) <= 0.0001f;
	}
}

TEST(Matrix4x4Tests, MatrixCompositionAndInverse)
{
	const Matrix4x4f matrix = Matrix4x4f::FromTRS(
		{10, 20, 30}, Quaternion<>::FromAxisAngle(Vector3f::UnitZ(), std::numbers::pi_v<float32> / 2), {2, 3, 4});
	const Vector3f point = matrix.TransformPosition({1, 2, 3});
	ASSERT_TRUE(isNear(point.X, 4));
	ASSERT_TRUE(isNear(point.Y, 22));
	ASSERT_TRUE(isNear(point.Z, 42));
	Matrix4x4f inverse;
	ASSERT_TRUE(matrix.TryInverse(&inverse));
	const Vector3f original = inverse.TransformPosition({4, 22, 42});
	ASSERT_TRUE(isNear(original.X, 1));
	ASSERT_TRUE(isNear(original.Y, 2));
	ASSERT_TRUE(isNear(original.Z, 3));
	Matrix4x4f output = Matrix4x4f::Translation({7, 8, 9});
	const Matrix4x4f before = output;
	ASSERT_FALSE(Matrix4x4f::Scale({1, 0, 1}).TryInverse(&output));
	for (uint32 index = 0; index < 16; ++index)
	{
		ASSERT_EQ(output.Data()[index], before.Data()[index]);
	}
	Matrix4x4f nonfinite = Matrix4x4f::Identity();
	nonfinite.M[0][0] = std::numeric_limits<float32>::quiet_NaN();
	ASSERT_FALSE(nonfinite.TryInverse(&output));
	for (uint32 index = 0; index < 16; ++index)
	{
		ASSERT_EQ(output.Data()[index], before.Data()[index]);
	}
}

TEST(Matrix4x4Tests, ProjectionUsesExpectedDepthRange)
{
	const Matrix4x4f projection = Matrix4x4f::PerspectiveFovLH(std::numbers::pi_v<float32> / 2, 1, 1, 11);
	ASSERT_TRUE(isNear(projection.TransformPosition({0, 0, 1}).Z, 0));
	ASSERT_TRUE(isNear(projection.TransformPosition({0, 0, 11}).Z, 1));
	const Matrix4x4f ortho = Matrix4x4f::OrthographicOffCenterLH(-2, 6, -3, 1, 2, 10);
	const Vector3f low = ortho.TransformPosition({-2, -3, 2});
	const Vector3f high = ortho.TransformPosition({6, 1, 10});
	ASSERT_TRUE(isNear(low.X, -1));
	ASSERT_TRUE(isNear(low.Y, -1));
	ASSERT_TRUE(isNear(low.Z, 0));
	ASSERT_TRUE(isNear(high.X, 1));
	ASSERT_TRUE(isNear(high.Y, 1));
	ASSERT_TRUE(isNear(high.Z, 1));
}

TEST(Matrix4x4Tests, AffineCompositionTransformsPointsAndDirections)
{
	const Matrix4x4d matrix = Matrix4x4d::Scale({2.0, 3.0, 4.0}) * Matrix4x4d::Translation({5.0, 6.0, 7.0});
	EXPECT_TRUE(matrix.IsFinite());
	EXPECT_TRUE(matrix.IsAffine());
	EXPECT_EQ(matrix.ExtractTranslation(), (Vector3d{5.0, 6.0, 7.0}));
	EXPECT_EQ(matrix.TransformPosition({1.0, 2.0, 3.0}), (Vector3d{7.0, 12.0, 19.0}));
	EXPECT_EQ(matrix.TransformDirection({1.0, 2.0, 3.0}), (Vector3d{2.0, 6.0, 12.0}));
	EXPECT_EQ(matrix.TransformVector4({1.0, 2.0, 3.0, 2.0}), (Vector4d{12.0, 18.0, 26.0, 2.0}));
	EXPECT_DOUBLE_EQ(matrix.Determinant(), 24.0);
	const Matrix3x3d linear = matrix.ExtractLinearMatrix();
	EXPECT_EQ(linear.GetRow(0), (Vector3d{2.0, 0.0, 0.0}));
	EXPECT_EQ(linear.GetRow(1), (Vector3d{0.0, 3.0, 0.0}));
	EXPECT_EQ(linear.GetRow(2), (Vector3d{0.0, 0.0, 4.0}));
	const Matrix4x4d transpose = matrix.Transposed();
	EXPECT_EQ(transpose.GetColumn(3), (Vector4d{5.0, 6.0, 7.0, 1.0}));
	EXPECT_FALSE(transpose.IsAffine());
}

TEST(Matrix4x4Tests, InversePivotsAndThresholdPreservesOutput)
{
	const Matrix4x4d matrix{0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
		0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 0.0, 4.0};
	EXPECT_DOUBLE_EQ(matrix.Determinant(), -8.0);
	const Matrix4x4d inverse = matrix.Inversed();
	const Matrix4x4d expected{0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
		0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.25};
	for (uint32 index = 0; index < 16; ++index)
	{
		EXPECT_DOUBLE_EQ(inverse.Data()[index], expected.Data()[index]);
	}
	const Matrix4x4d threshold = Matrix4x4d::Scale({1.0, 1.0, 0.25});
	Matrix4x4d output = matrix;
	EXPECT_FALSE(threshold.TryInverse(&output, 0.25));
	for (uint32 index = 0; index < 16; ++index)
	{
		EXPECT_DOUBLE_EQ(output.Data()[index], matrix.Data()[index]);
	}
	ASSERT_TRUE(threshold.TryInverse(&output, 0.125));
	EXPECT_DOUBLE_EQ(output[2][2], 4.0);
}

TEST(Matrix4x4Tests, CameraAndHomogeneousProjection)
{
	const Matrix4x4d view = Matrix4x4d::LookAtLH({2.0, 3.0, 4.0}, {2.0, 3.0, 5.0}, Vector3d::UnitY());
	EXPECT_EQ(view.TransformPosition({2.0, 3.0, 4.0}), Vector3d::Zero());
	EXPECT_EQ(view.TransformPosition({3.0, 5.0, 7.0}), (Vector3d{1.0, 2.0, 3.0}));
	const Matrix4x4d projection = Matrix4x4d::PerspectiveFovLH(1.5707963267948966, 2.0, 1.0, 11.0);
	EXPECT_FALSE(projection.IsAffine());
	const Vector3d projected = projection.TransformPosition({2.0, 2.0, 2.0});
	EXPECT_NEAR(projected.X, 0.5, 1.0e-12);
	EXPECT_NEAR(projected.Y, 1.0, 1.0e-12);
	EXPECT_NEAR(projected.Z, 0.55, 1.0e-12);
}

TEST(Matrix4x4Tests, GeneralProjectiveInverseMatchesIndependentCoefficients)
{
	const Matrix4x4d matrix{1.0, 2.0, 0.0, 0.0, 0.0, 1.0, 3.0, 0.0,
	                       0.0, 0.0, 1.0, 4.0, 0.0, 0.0, 0.0, 1.0};
	const Matrix4x4d expected{1.0, -2.0, 6.0, -24.0, 0.0, 1.0, -3.0, 12.0,
	                         0.0, 0.0, 1.0, -4.0, 0.0, 0.0, 0.0, 1.0};
	Matrix4x4d inverse;
	ASSERT_TRUE(matrix.TryInverse(&inverse));
	for (uint32 index = 0; index < 16; ++index)
	{
		EXPECT_NEAR(inverse.Data()[index], expected.Data()[index], 1.0e-12);
	}
}

TEST(Matrix4x4Tests, UnrepresentableInversePreservesOutput)
{
	const Matrix4x4d tiny = Matrix4x4d::Scale({std::numeric_limits<float64>::min() / 8.0, 1.0, 1.0});
	const Matrix4x4d sentinel = Matrix4x4d::Translation({7, 8, 9});
	Matrix4x4d output = sentinel;
	EXPECT_FALSE(tiny.TryInverse(&output, 0.0));
	for (uint32 index = 0; index < 16; ++index)
	{
		EXPECT_DOUBLE_EQ(output.Data()[index], sentinel.Data()[index]);
	}
}
