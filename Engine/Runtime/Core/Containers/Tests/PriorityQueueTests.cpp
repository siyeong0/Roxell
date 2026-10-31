#include <FailingMemoryResource.h>
#include <PriorityQueue.hpp>
#include <SystemMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(PriorityQueueTests, ExtractsInPriorityOrderAndPreservesHeapOnFailure)
{
	FailingMemoryResource resource(SystemMemoryResource::Get(), 1);
	PriorityQueue<int32> queue(resource);
	ASSERT_TRUE(queue.Reserve(3));
	ASSERT_TRUE(queue.Push(4));
	ASSERT_TRUE(queue.Push(9));
	ASSERT_TRUE(queue.Push(2));
	resource.Reset(0);
	EXPECT_FALSE(queue.Push(12));
	ASSERT_EQ(queue.Size(), 3u);
	EXPECT_EQ(queue.ExtractTop(), 9);
	EXPECT_EQ(queue.ExtractTop(), 4);
	EXPECT_EQ(queue.ExtractTop(), 2);
	EXPECT_TRUE(queue.IsEmpty());
}
