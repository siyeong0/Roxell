#include <BinaryHeap.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(BinaryHeapTests, BuildPushAndPopMaintainHeapPrefix)
{
	int32 values[] = {3, 1, 4, 2, 8};
	Span<int32> prefix(values, 4);
	MakeHeap(prefix);
	ASSERT_TRUE(IsHeap(prefix));
	EXPECT_EQ(values[0], 4);
	Span<int32> all(values);
	PushHeap(all);
	ASSERT_TRUE(IsHeap(all));
	EXPECT_EQ(values[0], 8);
	PopHeap(all);
	EXPECT_EQ(values[4], 8);
	EXPECT_TRUE(IsHeap(prefix));
	EXPECT_EQ(values[0], 4);
}
