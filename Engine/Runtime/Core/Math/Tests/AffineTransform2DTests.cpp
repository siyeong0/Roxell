#include <AffineTransform2D.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(AffineTransform2DTests, CompositionAndInversePreservePosition)
{
	const AffineTransform2D<> first{{2, 0}, {0, 3}, {4, 5}};
	const AffineTransform2D<> second{{1, 0}, {0, 1}, {-2, 1}};
	const Vector2f position{2, -1};
	const Vector2f transformed = (first * second).TransformPosition(position);
	EXPECT_EQ(transformed, Vector2f(6, 3));
	EXPECT_EQ((first * second).InverseTransformPosition(transformed), position);
	EXPECT_EQ(first.TransformDirection(position), Vector2f(4, -3));
	EXPECT_EQ(first.TransformNormal({2, 3}), Vector2f(1, 1));
	const AffineTransform2D<> restored = AffineTransform2D<>::FromMatrix(first.ToMatrix());
	EXPECT_EQ(restored.AxisX, first.AxisX);
	EXPECT_EQ(restored.AxisY, first.AxisY);
	EXPECT_EQ(restored.Translation, first.Translation);
}

TEST(AffineTransform2DTests, ShearCompositionRelativeAndNormal)
{
	const AffineTransform2D<rox::core::float64> local{{2, 1}, {0, 3}, {4, 5}};
	const AffineTransform2D<rox::core::float64> parent{{1, 0}, {0, 2}, {-2, 1}};
	const AffineTransform2D<rox::core::float64> world = local * parent;
	const AffineTransform2D<rox::core::float64> recovered = world.RelativeTo(parent);
	const Vector2d point{3, -2};
	EXPECT_EQ(world.TransformPosition(point), parent.TransformPosition(local.TransformPosition(point)));
	EXPECT_EQ(recovered.TransformPosition(point), local.TransformPosition(point));
	EXPECT_EQ(local.InverseTransformDirection(local.TransformDirection(point)), point);
	const Vector2d tangent{1, 0};
	const Vector2d normal{0, 1};
	EXPECT_DOUBLE_EQ(local.TransformDirection(tangent).Dot(local.TransformNormal(normal)), 0.0);
	EXPECT_EQ(local.TransformNormal(Vector2d::Zero()), Vector2d::Zero());
}
