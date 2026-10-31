#include <FailingMemoryResource.h>
#include <PagedArray.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

namespace
{
	struct Item final
	{
		inline static int32 Live = 0;
		int32 Value;
		explicit Item(const int32 value) noexcept
			: Value(value)
		{
			++Live;
		}
		Item(const Item&) = delete;
		Item(Item&&) = delete;
		~Item() noexcept
		{
			--Live;
		}
	};
}

TEST(PagedArrayTests, AddressesAndFailure)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource resource(tracking, 2);
	{
		PagedArray<Item, 2> values(resource);
		Item* first = values.Emplace(10);
		Item* second = values.Emplace(20);
		ASSERT_TRUE(first != nullptr && second != nullptr && Item::Live == 2);
		resource.Reset(0);
		ASSERT_TRUE(values.Emplace(30) == nullptr);
		ASSERT_TRUE(values.Size() == 2 && &values[0] == first && &values[1] == second);
		ASSERT_TRUE(first->Value == 10 && second->Value == 20 && Item::Live == 2);
		resource.Reset(4);
		ASSERT_TRUE(values.Emplace(30) != nullptr);
		ASSERT_TRUE(&values[0] == first && &values[1] == second && values[2].Value == 30);
		values.Clear();
		ASSERT_TRUE(values.IsEmpty() && Item::Live == 0);
	}
	ASSERT_TRUE(Item::Live == 0 && tracking.GetStatistics().LiveAllocationCount == 0);
}

TEST(PagedArrayTests, PartialReserveFailureRollsBackPagesAndPreservesValues)
{
	for (usize successfulAllocations = 0; successfulAllocations < 5; ++successfulAllocations)
	{
		TrackingMemoryResource tracking(SystemMemoryResource::Get());
		FailingMemoryResource resource(tracking, 2);
		{
			PagedArray<Item, 2> values(resource);
			Item* const first = values.Emplace(11);
			ASSERT_NE(first, nullptr);
			const usize capacity = values.Capacity();
			resource.Reset(successfulAllocations);
			EXPECT_FALSE(values.Reserve(10));
			EXPECT_EQ(values.Size(), 1u);
			EXPECT_EQ(values.Capacity(), capacity);
			EXPECT_EQ(&values.Front(), first);
			EXPECT_EQ(first->Value, 11);
			EXPECT_EQ(Item::Live, 1);
		}
		EXPECT_EQ(Item::Live, 0);
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
	}
}

TEST(PagedArrayTests, PopClearAndMovePreserveLifetimeAndAddresses)
{
	TrackingMemoryResource resource(SystemMemoryResource::Get());
	{
		PagedArray<Item, 2> source(resource);
		Item* const first = source.Emplace(1);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(source.Emplace(2), nullptr);
		ASSERT_NE(source.Emplace(3), nullptr);
		PagedArray<Item, 2> destination(Move(source));
		EXPECT_TRUE(source.IsEmpty());
		EXPECT_EQ(&destination.Front(), first);
		EXPECT_EQ(destination.Back().Value, 3);
		destination.PopBack();
		EXPECT_EQ(Item::Live, 2);
		EXPECT_EQ(destination.Back().Value, 2);
		const usize capacity = destination.Capacity();
		destination.Clear();
		EXPECT_EQ(Item::Live, 0);
		EXPECT_EQ(destination.Capacity(), capacity);
		destination.Reset();
		EXPECT_EQ(destination.Capacity(), 0u);
	}
	EXPECT_EQ(resource.GetStatistics().LiveAllocationCount, 0u);
}
