#include <AffineTransform.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(AffineTransformTests, TranslationCompositionAndInversePreservePosition)
{
	AffineTransform<> first = AffineTransform<>::Identity();
	first.Translation = {4, 5, 6};
	AffineTransform<> second = AffineTransform<>::Identity();
	second.Translation = {-2, 1, 3};
	const Vector3f position{2, -1, 4};
	const AffineTransform<> combined = first * second;
	const Vector3f transformed = combined.TransformPosition(position);
	EXPECT_EQ(transformed, Vector3f(4, 5, 13));
	EXPECT_EQ(combined.InverseTransformPosition(transformed), position);
	EXPECT_EQ(combined.TransformDirection(position), position);
	const AffineTransform<> restored = AffineTransform<>::FromMatrix(combined.ToMatrix());
	EXPECT_EQ(restored.TransformPosition(position), transformed);
}

TEST(AffineTransformTests, NormalMatrixAndInverseDirectionHandleNonuniformScale)
{
	const AffineTransform<rox::core::float64> transform{
	    Matrix3x3d{2.0, 0.0, 0.0, 0.0, 4.0, 0.0, 0.0, 0.0, 8.0}, {1.0, 2.0, 3.0}};
	const Matrix3x3d normal = transform.NormalMatrix();
	EXPECT_EQ(normal.TransformVector({2.0, 4.0, 8.0}), (Vector3d{1.0, 1.0, 1.0}));
	EXPECT_EQ(transform.TransformNormal({2.0, 4.0, 8.0}), (Vector3d{1.0, 1.0, 1.0}));
	EXPECT_EQ(transform.InverseTransformDirection({2.0, 4.0, 8.0}), Vector3d::One());
}
