#include <IntrusiveList.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

namespace
{
	struct Node final
	{
		int32 Value = 0;
		IntrusiveListHook<Node> Hook;
	};
}

TEST(IntrusiveListTests, InsertRemoveAndClearUnlinkNodes)
{
	Node first;
	Node middle;
	Node last;
	first.Value = 1;
	middle.Value = 2;
	last.Value = 3;
	IntrusiveList<Node, &Node::Hook> values;
	values.PushBack(first);
	values.PushBack(last);
	values.InsertBefore(last, middle);
	ASSERT_EQ(values.Size(), 3u);
	EXPECT_EQ(values.Front(), &first);
	EXPECT_EQ(values.Next(first), &middle);
	EXPECT_EQ(values.Previous(last), &middle);
	values.Remove(middle);
	EXPECT_FALSE(middle.Hook.IsLinked());
	EXPECT_EQ(values.Next(first), &last);
	EXPECT_EQ(values.PopFront(), &first);
	EXPECT_FALSE(first.Hook.IsLinked());
	values.Clear();
	EXPECT_TRUE(values.IsEmpty());
	EXPECT_FALSE(last.Hook.IsLinked());
}
