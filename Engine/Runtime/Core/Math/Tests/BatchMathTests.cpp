#include <BatchMath.h>

#include <gtest/gtest.h>

using namespace rox::core;
using namespace rox::math;

TEST(BatchMathTests, PositionsSupportInPlaceAndDirectionsIgnoreTranslation)
{
	AffineTransform<> transform = AffineTransform<>::Identity();
	transform.Translation = {3, 4, 5};
	Vector3f values[] = {{1, 2, 3}, {-1, -2, -3}};
	TransformPositions(transform, values, values, 2);
	EXPECT_EQ(values[0], Vector3f(4, 6, 8));
	EXPECT_EQ(values[1], Vector3f(2, 2, 2));
	Vector3f directions[2]{};
	TransformDirections(transform, values, directions, 2);
	EXPECT_EQ(directions[0], values[0]);
	EXPECT_EQ(directions[1], values[1]);
	float32 products[2]{};
	DotProducts(values, directions, products, 2);
	EXPECT_FLOAT_EQ(products[0], 116.0f);
	EXPECT_FLOAT_EQ(products[1], 12.0f);
}

TEST(BatchMathTests, NormalsUseInverseTransposeWithOddCountAndInPlaceOutput)
{
	const AffineTransform<float64> transform{{2.0, 0.0, 0.0, 0.0, 4.0, 0.0, 0.0, 0.0, 8.0}, {9.0, 8.0, 7.0}};
	Vector3d normals[] = {{2.0, 4.0, 8.0}, {4.0, 0.0, -8.0}, {0.0, 8.0, 0.0}};
	TransformNormals(transform, normals, normals, 3);
	EXPECT_EQ(normals[0], (Vector3d{1.0, 1.0, 1.0}));
	EXPECT_EQ(normals[1], (Vector3d{2.0, 0.0, -1.0}));
	EXPECT_EQ(normals[2], (Vector3d{0.0, 2.0, 0.0}));
}

TEST(BatchMathTests, HomogeneousVectorsPreserveWAndEmptyRangesAcceptNull)
{
	const Matrix4x4d matrix = Matrix4x4d::Translation({2.0, 3.0, 4.0});
	Vector4d input[] = {{1.0, 2.0, 3.0, 1.0}, {1.0, 2.0, 3.0, 0.0}, {1.0, 2.0, 3.0, 2.0}};
	TransformVectors(matrix, input, input, 3);
	EXPECT_EQ(input[0], (Vector4d{3.0, 5.0, 7.0, 1.0}));
	EXPECT_EQ(input[1], (Vector4d{1.0, 2.0, 3.0, 0.0}));
	EXPECT_EQ(input[2], (Vector4d{5.0, 8.0, 11.0, 2.0}));
	const AffineTransform<float64> identity = AffineTransform<float64>::Identity();
	TransformPositions<float64>(identity, nullptr, nullptr, 0);
	TransformDirections<float64>(identity, nullptr, nullptr, 0);
	TransformNormals<float64>(identity, nullptr, nullptr, 0);
	TransformVectors<float64>(matrix, nullptr, nullptr, 0);
	DotProducts<float64>(nullptr, nullptr, nullptr, 0);
}
