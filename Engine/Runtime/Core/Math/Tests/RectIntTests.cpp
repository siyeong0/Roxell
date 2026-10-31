#include <RectInt.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(RectIntTests, HalfOpenBoundsAndCovering)
{
	const RectInt rectangle = RectInt::FromPositionSize(Int2(1, 2), 4, 3);
	EXPECT_TRUE(rectangle.Contains(Int2(1, 2)));
	EXPECT_FALSE(rectangle.Contains(Int2(5, 2)));
	EXPECT_EQ(rectangle.Area(), 12);
	const Rect<> source(Vector2f(-1.5f, 2.25f), Vector2f(2.2f, 4.0f));
	const RectInt covering = RectInt::Covering(source);
	EXPECT_EQ(covering.Min, Int2(-2, 2));
	EXPECT_EQ(covering.Max, Int2(3, 4));
}

TEST(RectIntTests, SetOperationsAndConversionsUseHalfOpenBounds)
{
	const RectInt first{{1, 2}, {5, 5}};
	const RectInt second{{3, 4}, {7, 8}};
	EXPECT_TRUE(first.Overlaps(second));
	EXPECT_EQ(first.Intersection(second), (RectInt{{3, 4}, {5, 5}}));
	EXPECT_EQ(first.Union(second), (RectInt{{1, 2}, {7, 8}}));
	EXPECT_EQ(first.Union(RectInt::Empty()), first);
	EXPECT_EQ(first.Translated({-1, 2}), (RectInt{{0, 4}, {4, 7}}));
	EXPECT_EQ(first.Width(), 4u);
	EXPECT_EQ(first.Height(), 3u);
	EXPECT_EQ(first.Center<float64>(), (Vector2d{3, 3.5}));
	EXPECT_EQ(first.ToRect<float64>(), (Rect<float64>{{1, 2}, {5, 5}}));
	EXPECT_EQ(RectInt::Covering(Rect<float64>{{2, 2}, {2, 4}}), RectInt::Empty());
	const RectInt touching{{5, 2}, {7, 5}};
	EXPECT_FALSE(first.Overlaps(touching));
	EXPECT_EQ(first.Intersection(touching), RectInt::Empty());
	EXPECT_FALSE(RectInt({5, 2}, {1, 5}).IsValid());
}
