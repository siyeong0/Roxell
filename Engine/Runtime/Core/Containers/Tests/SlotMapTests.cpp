#include <FailingMemoryResource.h>
#include <SlotMap.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(SlotMapTests, HandlesAndFailure)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource resource(tracking, 2);
	{
		SlotMap<int32, 2> values(resource);
		const SlotMapHandle<int32> first = values.Emplace(10);
		const SlotMapHandle<int32> second = values.Emplace(20);
		ASSERT_TRUE(first.IsValid() && second.IsValid());
		resource.Reset(0);
		ASSERT_TRUE(!values.Emplace(30).IsValid());
		ASSERT_TRUE(values.Size() == 2 && values.Get(first) == 10 && values.Get(second) == 20);
		ASSERT_TRUE(values.Erase(first) && !values.Erase(first));
		const SlotMapHandle<int32> replacement = values.Emplace(30);
		ASSERT_TRUE(replacement.IsValid() && !values.Contains(first));
		ASSERT_TRUE(values.Get(replacement) == 30 && values.Get(second) == 20);
		SlotMap<int32, 2> other(tracking);
		const SlotMapHandle<int32> foreign = other.EmplaceRequired(99);
		ASSERT_TRUE(!values.Contains(foreign) && !other.Contains(replacement));
	}
	ASSERT_TRUE(tracking.GetStatistics().LiveAllocationCount == 0);
}

TEST(SlotMapTests, ClearAndMoveInvalidateOnlyTheAppropriateHandles)
{
	TrackingMemoryResource resource(SystemMemoryResource::Get());
	{
		SlotMap<int32, 2> source(resource);
		const SlotMapHandle<int32> first = source.EmplaceRequired(10);
		const SlotMapHandle<int32> second = source.EmplaceRequired(20);
		SlotMap<int32, 2> destination(Move(source));
		EXPECT_TRUE(source.IsEmpty());
		EXPECT_FALSE(source.Contains(first));
		ASSERT_TRUE(destination.Contains(first));
		EXPECT_EQ(destination.Get(second), 20);
		const usize capacity = destination.Capacity();
		destination.Clear();
		EXPECT_TRUE(destination.IsEmpty());
		EXPECT_EQ(destination.Capacity(), capacity);
		EXPECT_FALSE(destination.Contains(first));
		EXPECT_FALSE(destination.Contains(second));
		const SlotMapHandle<int32> replacement = destination.EmplaceRequired(30);
		EXPECT_TRUE(destination.Contains(replacement));
		EXPECT_FALSE(destination.Contains(first));
		EXPECT_FALSE(destination.Contains(second));
		EXPECT_EQ(destination.Get(replacement), 30);
		destination.Reset();
		EXPECT_EQ(destination.Capacity(), 0u);
		EXPECT_FALSE(destination.Contains(replacement));
	}
	EXPECT_EQ(resource.GetStatistics().LiveAllocationCount, 0u);
}
