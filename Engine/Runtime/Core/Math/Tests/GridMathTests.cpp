#include <GridMath.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace rox::core;
using namespace rox::math;

TEST(GridMathTests, GridDivisionAndCoordinatesAtNegativeBoundary)
{
	ASSERT_EQ(FloorDiv(-17, 16), -2);
	ASSERT_EQ(FloorMod(-17, 16), 15);
	ASSERT_EQ(FloorDiv(std::numeric_limits<int32>::min(), 1), std::numeric_limits<int32>::min());
	ASSERT_EQ(DivideRoundUp(std::numeric_limits<uint32>::max(), 2), 2147483648u);
	const Vector2f origin{-10.0f, 5.0f};
	const Vector2f size{2.0f, 4.0f};
	ASSERT_EQ(PointToCell<float32>({-12.0f, 5.0f}, origin, size), Int2(-1, 0));
	ASSERT_EQ(PointToCell<float32>({std::nextafter(-12.0f, -13.0f), 5.0f}, origin, size), Int2(-2, 0));
	ASSERT_EQ(CellMinimum<float32>(Int2(-1, -1), origin, size), Vector2f(-12.0f, 1.0f));
}
