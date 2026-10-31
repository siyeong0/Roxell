#include <Matrix3x3.hpp>

#include <gtest/gtest.h>

#include <limits>

using namespace rox::math;

TEST(Matrix3x3Tests, InverseAndSingularOutput)
{
	const Matrix3x3f matrix(2.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, 4.0f);
	Matrix3x3f inverse;
	ASSERT_TRUE(matrix.TryInverse(&inverse));
	EXPECT_FLOAT_EQ(matrix.Determinant(), 24.0f);
	const Vector3f transformed = inverse.TransformVector(matrix.TransformVector(Vector3f(1.0f, 2.0f, 3.0f)));
	EXPECT_TRUE(transformed.NearlyEquals(Vector3f(1.0f, 2.0f, 3.0f)));
	const Matrix3x3f singular = Matrix3x3f::Zero();
	EXPECT_FALSE(singular.TryInverse(&inverse));
	EXPECT_FLOAT_EQ(inverse[0][0], 0.5f);
}

TEST(Matrix3x3Tests, RowsTransposeAndMultiplication)
{
	const Matrix3x3d matrix = Matrix3x3d::FromRows({1.0, 2.0, 3.0}, {0.0, 1.0, 4.0}, {5.0, 6.0, 0.0});
	EXPECT_EQ(matrix.GetRow(1), (Vector3d{0.0, 1.0, 4.0}));
	EXPECT_EQ(matrix.GetColumn(2), (Vector3d{3.0, 4.0, 0.0}));
	EXPECT_DOUBLE_EQ(matrix.Data()[5], 4.0);
	EXPECT_EQ(matrix.TransformVector({1.0, 2.0, 3.0}), (Vector3d{16.0, 22.0, 11.0}));
	const Matrix3x3d transpose = matrix.Transposed();
	EXPECT_EQ(transpose.GetRow(0), (Vector3d{1.0, 0.0, 5.0}));
	EXPECT_EQ(transpose.GetRow(1), (Vector3d{2.0, 1.0, 6.0}));
	EXPECT_EQ(transpose.GetRow(2), (Vector3d{3.0, 4.0, 0.0}));
	const Matrix3x3d product = matrix * transpose;
	EXPECT_EQ(product.GetRow(0), (Vector3d{14.0, 14.0, 17.0}));
	EXPECT_EQ(product.GetRow(1), (Vector3d{14.0, 17.0, 6.0}));
	EXPECT_EQ(product.GetRow(2), (Vector3d{17.0, 6.0, 61.0}));
}

TEST(Matrix3x3Tests, GeneralInverseAndScaledThreshold)
{
	const Matrix3x3d matrix{1.0, 2.0, 3.0, 0.0, 1.0, 4.0, 5.0, 6.0, 0.0};
	EXPECT_DOUBLE_EQ(matrix.Determinant(), 1.0);
	const Matrix3x3d inverse = matrix.Inversed();
	const Matrix3x3d expected{-24.0, 18.0, 5.0, 20.0, -15.0, -4.0, -5.0, 4.0, 1.0};
	for (rox::core::uint32 index = 0; index < 9; ++index)
	{
		EXPECT_NEAR(inverse.Data()[index], expected.Data()[index], 1.0e-12);
	}
	const Matrix3x3d threshold{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.25};
	Matrix3x3d output = matrix;
	EXPECT_FALSE(threshold.TryInverse(&output, 0.25));
	for (rox::core::uint32 index = 0; index < 9; ++index)
	{
		EXPECT_DOUBLE_EQ(output.Data()[index], matrix.Data()[index]);
	}
	ASSERT_TRUE(threshold.TryInverse(&output, 0.125));
	EXPECT_DOUBLE_EQ(output[2][2], 4.0);
}

TEST(Matrix3x3Tests, RotationAxesFollowRowVectorConvention)
{
	constexpr rox::core::float64 HALF_PI = 1.5707963267948966;
	const Vector3d aroundX = Matrix3x3d::RotationX(HALF_PI).TransformVector(Vector3d::UnitY());
	const Vector3d aroundY = Matrix3x3d::RotationY(HALF_PI).TransformVector(Vector3d::UnitZ());
	const Vector3d aroundZ = Matrix3x3d::RotationZ(HALF_PI).TransformVector(Vector3d::UnitX());
	const Vector3d aroundAxis = Matrix3x3d::RotationAxis({0.0, 0.0, 2.0}, HALF_PI).TransformVector(Vector3d::UnitX());
	EXPECT_NEAR(aroundX.Z, 1.0, 1.0e-12);
	EXPECT_NEAR(aroundY.X, 1.0, 1.0e-12);
	EXPECT_NEAR(aroundZ.Y, 1.0, 1.0e-12);
	EXPECT_NEAR(aroundAxis.X, 0.0, 1.0e-12);
	EXPECT_NEAR(aroundAxis.Y, 1.0, 1.0e-12);
	EXPECT_NEAR(aroundAxis.Z, 0.0, 1.0e-12);
}

TEST(Matrix3x3Tests, InverseRejectsNonfiniteAndUnrepresentableResultsWithoutChangingOutput)
{
	const Matrix3x3d sentinel{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
	Matrix3x3d output = sentinel;
	Matrix3x3d invalid = Matrix3x3d::Identity();
	invalid[1][2] = std::numeric_limits<rox::core::float64>::infinity();
	EXPECT_FALSE(invalid.IsFinite());
	EXPECT_FALSE(invalid.TryInverse(&output));
	invalid[1][2] = std::numeric_limits<rox::core::float64>::quiet_NaN();
	EXPECT_FALSE(invalid.TryInverse(&output));
	const Matrix3x3d tiny{1.0e-320, 0.0, 0.0, 0.0, 1.0e-320, 0.0, 0.0, 0.0, 1.0e-320};
	EXPECT_FALSE(tiny.TryInverse(&output, 0.0));
	for (rox::core::uint32 index = 0; index < 9; ++index)
	{
		EXPECT_DOUBLE_EQ(output.Data()[index], sentinel.Data()[index]);
	}
}
