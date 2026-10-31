#include <Transform.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(TransformTests, PositionDirectionAndInverse)
{
	const Transform<> transform(Vector3f(5.0f, -2.0f, 1.0f), Quaternion<float32>::Identity(), Vector3f(2.0f, 3.0f, 4.0f));
	const Vector3f input(1.0f, 2.0f, 3.0f);
	const Vector3f position = transform.TransformPosition(input);
	EXPECT_TRUE(position.NearlyEquals(Vector3f(7.0f, 4.0f, 13.0f)));
	EXPECT_TRUE(transform.TransformDirection(input).NearlyEquals(Vector3f(2.0f, 6.0f, 12.0f)));
	EXPECT_TRUE(transform.InverseTransformPosition(position).NearlyEquals(input));
}

TEST(TransformTests, DecompositionRejectsShearAndProjectiveMatricesWithoutChangingOutput)
{
	const Transform<> sentinel{{7, 8, 9}, Quaternion<>::Identity(), {2, 3, 4}};
	Transform<> output = sentinel;
	Matrix4x4f shear = Matrix4x4f::Identity();
	shear[0][1] = 0.5f;
	EXPECT_FALSE(Transform<>::DecomposeTRS(shear, &output));
	EXPECT_EQ(output.Translation, sentinel.Translation);
	EXPECT_EQ(output.Rotation, sentinel.Rotation);
	EXPECT_EQ(output.Scale, sentinel.Scale);
	Matrix4x4f projective = Matrix4x4f::Identity();
	projective[0][3] = 1.0f;
	EXPECT_FALSE(Transform<>::DecomposeTRS(projective, &output));
	EXPECT_EQ(output.Translation, sentinel.Translation);
	EXPECT_EQ(output.Rotation, sentinel.Rotation);
	EXPECT_EQ(output.Scale, sentinel.Scale);
}

TEST(TransformTests, DecompositionSupportsZeroScaleAndPreservesMatrix)
{
	const Matrix4x4f matrix = Matrix4x4f::Scale({0, 3, 4}) * Matrix4x4f::Translation({5, 6, 7});
	Transform<> output;
	ASSERT_TRUE(Transform<>::DecomposeTRS(matrix, &output));
	const Matrix4x4f reconstructed = output.ToMatrix();
	for (rox::core::uint32 index = 0; index < 16; ++index)
	{
		EXPECT_NEAR(reconstructed.Data()[index], matrix.Data()[index], 1.0e-5f);
	}
}

TEST(TransformTests, AffineCompositionAndRelativePreservePosition)
{
	const Transform<rox::core::float64> local{{2, 3, 4}, Quaternion<rox::core::float64>::Identity(), {2, 3, 4}};
	const Transform<rox::core::float64> parent{{-1, 5, 2}, Quaternion<rox::core::float64>::Identity(), {3, 2, 1}};
	const Vector3d position{1, -2, 3};
	const AffineTransform<rox::core::float64> world = local.ComposeAffine(parent);
	EXPECT_EQ(world.TransformPosition(position), parent.TransformPosition(local.TransformPosition(position)));
	EXPECT_TRUE(parent.TransformPosition(local.RelativeAffine(parent).TransformPosition(position)).NearlyEquals(local.TransformPosition(position), 1.0e-12));
	EXPECT_EQ(local.InverseTransformDirection(local.TransformDirection(position)), position);
	EXPECT_DOUBLE_EQ(local.TransformDirection(Vector3d::UnitX()).Dot(local.TransformNormal(Vector3d::UnitY())), 0.0);
}
