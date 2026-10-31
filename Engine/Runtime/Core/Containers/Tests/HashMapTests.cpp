#include <FailingMemoryResource.h>
#include <HashMap.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

namespace
{
	struct CollisionHash final
	{
		usize operator()(int32) const noexcept
		{
			return 1;
		}
	};
}

TEST(HashMapTests, CollisionLookupAndFailure)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource resource(tracking, 0);
	{
		HashMap<int32, int32, CollisionHash> values(resource);
		ASSERT_TRUE(values.Emplace(1, 10).Value == nullptr && values.IsEmpty());
		resource.Reset(1);
		ASSERT_TRUE(values.Reserve(6));
		for (int32 key = 0; key < 6; ++key)
		{
			ASSERT_TRUE(values.Emplace(key, key * 10).bInserted);
		}
		resource.Reset(0);
		ASSERT_TRUE(values.Emplace(9, 90).Value == nullptr);
		ASSERT_TRUE(values.Size() == 6 && values.Get(5) == 50);
		ASSERT_TRUE(!values.Emplace(5, 999).bInserted && values.Get(5) == 50);
		ASSERT_TRUE(values.Erase(0) && values.Get(5) == 50);
		ASSERT_TRUE(values.Emplace(9, 90).bInserted && values.Get(9) == 90);
	}
	ASSERT_TRUE(tracking.GetStatistics().LiveAllocationCount == 0);
}

TEST(HashMapTests, IterationEraseAndStorageLifecycle)
{
	TrackingMemoryResource resource(SystemMemoryResource::Get());
	{
		HashMap<int32, int32, CollisionHash> values(resource);
		EXPECT_EQ(values.begin(), values.end());
		for (int32 key = 0; key < 8; ++key)
		{
			ASSERT_TRUE(values.Insert(key, key * 10).bInserted);
		}
		EXPECT_FALSE(values.Erase(99));
		const auto* entry = values.FindEntry(3);
		ASSERT_NE(entry, nullptr);
		EXPECT_EQ(entry->GetKey(), 3);
		EXPECT_EQ(entry->Value, 30);
		EXPECT_EQ(values.FindEntry(99), nullptr);
		const auto& readOnly = values;
		int32 sum = 0;
		for (const auto& item : readOnly)
		{
			sum += item.Value;
		}
		EXPECT_EQ(sum, 280);
		EXPECT_EQ(values.EraseIf([](const auto& item) noexcept
		{
			return item.GetKey() % 2 == 0;
		}), 4u);
		for (int32 key = 0; key < 8; ++key)
		{
			EXPECT_EQ(values.Contains(key), key % 2 != 0);
		}
		const usize capacity = values.Capacity();
		values.Clear();
		EXPECT_TRUE(values.IsEmpty());
		EXPECT_EQ(values.Capacity(), capacity);
		ASSERT_TRUE(values.Insert(10, 100).bInserted);
		values.Reset();
		EXPECT_TRUE(values.IsEmpty());
		EXPECT_EQ(values.Capacity(), 0u);
	}
	EXPECT_EQ(resource.GetStatistics().LiveAllocationCount, 0u);
}

TEST(HashMapTests, AssignmentRetainsDestinationResource)
{
	TrackingMemoryResource first(SystemMemoryResource::Get());
	TrackingMemoryResource second(SystemMemoryResource::Get());
	{
		HashMap<int32, int32> source(first);
		ASSERT_TRUE(source.Insert(1, 10).bInserted);
		HashMap<int32, int32> destination(second);
		destination = source;
		EXPECT_EQ(&destination.GetResource(), &second);
		EXPECT_EQ(destination.Get(1), 10);
		source.Get(1) = 20;
		EXPECT_EQ(destination.Get(1), 10);
		destination = rox::core::Move(source);
		EXPECT_EQ(&destination.GetResource(), &second);
		EXPECT_EQ(destination.Get(1), 20);
		EXPECT_TRUE(source.IsEmpty());
	}
	EXPECT_EQ(first.GetStatistics().LiveAllocationCount, 0u);
	EXPECT_EQ(second.GetStatistics().LiveAllocationCount, 0u);
}

TEST(HashMapTests, FailedRehashPreservesCollidingEntriesAndTombstone)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource resource(tracking, 1);
	{
		HashMap<int32, int32, CollisionHash> values(resource);
		ASSERT_TRUE(values.Reserve(6));
		ASSERT_TRUE(values.Emplace(1, 10).bInserted);
		ASSERT_TRUE(values.Emplace(2, 20).bInserted);
		ASSERT_TRUE(values.Emplace(3, 30).bInserted);
		ASSERT_TRUE(values.Erase(2));
		const usize capacity = values.Capacity();
		resource.Reset(0);
		EXPECT_FALSE(values.Reserve(capacity));
		EXPECT_EQ(values.Capacity(), capacity);
		EXPECT_EQ(values.Size(), 2u);
		EXPECT_EQ(values.Get(1), 10);
		EXPECT_FALSE(values.Contains(2));
		EXPECT_EQ(values.Get(3), 30);
		const auto duplicate = values.Emplace(3, 300);
		EXPECT_FALSE(duplicate.bInserted);
		ASSERT_NE(duplicate.Value, nullptr);
		EXPECT_EQ(*duplicate.Value, 30);
		ASSERT_TRUE(values.Emplace(4, 40).bInserted);
		EXPECT_EQ(values.Get(4), 40);
		EXPECT_EQ(values.Get(3), 30);
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
}
