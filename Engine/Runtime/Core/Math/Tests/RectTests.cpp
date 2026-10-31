#include <Rect.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(RectTests, HalfOpenBoundsAndIntersection)
{
	const Rect<> rectangle = Rect<>::FromPositionSize(Vector2f(1.0f, 2.0f), Vector2f(4.0f, 3.0f));
	EXPECT_TRUE(rectangle.Contains(Vector2f(1.0f, 2.0f)));
	EXPECT_FALSE(rectangle.Contains(Vector2f(5.0f, 2.0f)));
	const Rect<> touching(Vector2f(5.0f, 2.0f), Vector2f(7.0f, 5.0f));
	EXPECT_FALSE(rectangle.Overlaps(touching));
	EXPECT_TRUE(rectangle.Intersection(touching).IsEmpty());
	EXPECT_FLOAT_EQ(rectangle.Area(), 12.0f);
}

TEST(RectTests, IntersectionUnionAndTranslationHandleEmptyBounds)
{
	const Rect<float64> first{{1, 2}, {5, 5}};
	const Rect<float64> second{{3, 4}, {7, 8}};
	EXPECT_EQ(first.Intersection(second), (Rect<float64>{{3, 4}, {5, 5}}));
	EXPECT_EQ(first.Union(second), (Rect<float64>{{1, 2}, {7, 8}}));
	EXPECT_EQ(first.Union(Rect<float64>::Empty()), first);
	EXPECT_EQ(Rect<float64>::Empty().Union(first), first);
	EXPECT_EQ(first.Translated({-1, 2}), (Rect<float64>{{0, 4}, {4, 7}}));
	EXPECT_EQ(first.Size(), (Vector2d{4, 3}));
	EXPECT_EQ(first.Center(), (Vector2d{3, 3.5}));
	EXPECT_TRUE(Rect<float64>({1, 2}, {1, 5}).IsEmpty());
	EXPECT_FALSE(Rect<float64>({5, 2}, {1, 5}).IsValid());
}
