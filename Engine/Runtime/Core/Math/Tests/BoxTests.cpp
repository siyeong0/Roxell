#include <Box.h>

#include <gtest/gtest.h>

using namespace rox::math;

TEST(BoxTests, EncapsulationAndBoundary)
{
	Box<> box = Box<>::Empty();
	box.Encapsulate(Vector3f(-1.0f, -2.0f, -3.0f));
	box.Encapsulate(Vector3f(1.0f, 2.0f, 3.0f));
	EXPECT_TRUE(box.IsValid());
	EXPECT_TRUE(box.Contains(Vector3f(1.0f, 2.0f, 3.0f)));
	EXPECT_FALSE(box.Contains(Vector3f(2.0f, 0.0f, 0.0f)));
	EXPECT_FLOAT_EQ(box.Volume(), 48.0f);
}

TEST(BoxTests, EmptyAndDegenerateBoundsHaveExplicitBehavior)
{
	Box<> box = Box<>::Empty();
	EXPECT_FALSE(box.IsValid());
	EXPECT_FALSE(box.Contains(Vector3f::Zero()));
	EXPECT_FALSE(box.Overlaps(Box<>({0, 0, 0}, {1, 1, 1})));
	box.Encapsulate(Box<>::Empty());
	EXPECT_FALSE(box.IsValid());
	box.Encapsulate(Box<>({1, 2, 3}, {1, 4, 5}));
	EXPECT_TRUE(box.IsValid());
	EXPECT_EQ(box.Center(), (Vector3f{1, 3, 4}));
	EXPECT_EQ(box.Size(), (Vector3f{0, 2, 2}));
	EXPECT_EQ(box.Extents(), (Vector3f{0, 1, 1}));
	EXPECT_FLOAT_EQ(box.Volume(), 0.0f);
	EXPECT_TRUE(box.Overlaps(Box<>({1, 4, 5}, {2, 5, 6})));
}

TEST(BoxTests, DoubleBoundsPreserveInclusiveContact)
{
	Box<float64> box{{-2, -1, 0}, {2, 3, 4}};
	const Box<float64> touching{{2, 3, 4}, {5, 6, 7}};
	EXPECT_TRUE(box.Contains({2, 3, 4}));
	EXPECT_TRUE(box.Overlaps(touching));
	box.Encapsulate(touching);
	EXPECT_EQ(box.Min, (Vector3d{-2, -1, 0}));
	EXPECT_EQ(box.Max, (Vector3d{5, 6, 7}));
	EXPECT_DOUBLE_EQ(box.Volume(), 343.0);
}
