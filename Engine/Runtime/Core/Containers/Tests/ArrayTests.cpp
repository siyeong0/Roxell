#include <Array.hpp>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

namespace
{
	struct Tracked final
	{
		inline static int32 Live = 0;
		int32 Value;

		explicit Tracked(const int32 value) noexcept
			: Value(value)
		{
			++Live;
		}
		Tracked(const Tracked& other) noexcept
			: Tracked(other.Value)
		{
		}
		Tracked(Tracked&& other) noexcept
			: Tracked(other.Value)
		{
			other.Value = -1;
		}
		Tracked& operator=(const Tracked&) = delete;
		Tracked& operator=(Tracked&&) = delete;
		~Tracked() noexcept
		{
			--Live;
		}
	};
}

TEST(ArrayTests, FailureAndAliasing)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource resource(tracking, 1);
	{
		Array<Tracked> values(resource);
		ASSERT_TRUE(values.Reserve(2));
		values.EmplaceRequired(10);
		values.EmplaceRequired(20);
		resource.Reset(0);
		ASSERT_TRUE(!values.Reserve(3));
		ASSERT_TRUE(values.Size() == 2 && values[0].Value == 10 && values[1].Value == 20);
		ASSERT_TRUE(Tracked::Live == 2);
		resource.Reset(1);
		values.AppendRequired(values.View());
		ASSERT_TRUE(values.Size() == 4 && values[0].Value == 10 && values[1].Value == 20);
		ASSERT_TRUE(values[2].Value == 10 && values[3].Value == 20 && Tracked::Live == 4);
		const usize removedCount = values.RemoveIf([](const Tracked& value) noexcept
		{
			return value.Value == 10;
		});
		ASSERT_EQ(removedCount, 2u);
		ASSERT_TRUE(values.Size() == 2 && values[0].Value == 20 && values[1].Value == 20);
		ASSERT_TRUE(Tracked::Live == 2);
	}
	ASSERT_TRUE(Tracked::Live == 0 && tracking.GetStatistics().LiveAllocationCount == 0);
}

TEST(ArrayTests, MutationSearchAndCapacity)
{
	Array<int32> values{1, 2, 3, 4, 5};
	EXPECT_EQ(values.Front(), 1);
	EXPECT_EQ(values.Back(), 5);
	int32 calls = 0;
	EXPECT_EQ(values.FindIf([&calls](const int32 value) noexcept
	{
		++calls;
		return value == 3;
	}), &values[2]);
	EXPECT_EQ(calls, 3);
	const Array<int32>& readOnly = values;
	EXPECT_EQ(readOnly.Find(9), nullptr);
	EXPECT_TRUE(readOnly.Contains(4));
	EXPECT_FALSE(readOnly.ContainsIf([](const int32 value) noexcept
	{
		return value > 5;
	}));
	ASSERT_NE(values.Insert(0, values[2]), nullptr);
	EXPECT_EQ(values.Front(), 3);
	values.RemoveAt(1);
	EXPECT_EQ(values[1], 2);
	values.RemoveAtSwap(1);
	EXPECT_EQ(values[1], 5);
	values.PopBack();
	const usize capacity = values.Capacity();
	int32* const data = values.Data();
	values.Clear();
	EXPECT_TRUE(values.IsEmpty());
	EXPECT_EQ(values.Capacity(), capacity);
	EXPECT_EQ(values.Data(), data);
	values.Reset();
	EXPECT_EQ(values.Capacity(), 0u);
	EXPECT_EQ(values.Data(), nullptr);
}

TEST(ArrayTests, RecoverableOperationsAndClonePreserveStateOnFailure)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource resource(tracking, 1);
	{
		Array<int32> values(resource);
		ASSERT_TRUE(values.Reserve(2));
		ASSERT_TRUE(values.Add(10));
		ASSERT_TRUE(values.Add(20));
		resource.Reset(0);
		EXPECT_FALSE(values.Add(values[0]));
		EXPECT_EQ(values.Insert(1, values[0]), nullptr);
		EXPECT_FALSE(values.Append(values.View()));
		EXPECT_FALSE(values.Resize(3));
		EXPECT_FALSE(values.ResizeUninitialized(3));
		EXPECT_EQ(values.Size(), 2u);
		EXPECT_EQ(values[0], 10);
		EXPECT_EQ(values[1], 20);

		Array<int32> output(resource);
		EXPECT_FALSE(values.CloneTo(resource, &output));
		EXPECT_TRUE(output.IsEmpty());
		resource.Reset(1);
		ASSERT_TRUE(values.CloneTo(resource, &output));
		EXPECT_EQ(output.Size(), 2u);
		EXPECT_EQ(output[1], 20);
		values[1] = 30;
		EXPECT_EQ(output[1], 20);
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ArrayTests, AssignmentRetainsDestinationResource)
{
	TrackingMemoryResource first(SystemMemoryResource::Get());
	TrackingMemoryResource second(SystemMemoryResource::Get());
	{
		Array<Tracked> source(first);
		source.EmplaceRequired(7);
		source.EmplaceRequired(8);
		Array<Tracked> destination(second);
		destination.ReserveRequired(4);
		Tracked* const storage = destination.Data();
		destination.AssignDisjoint(source);
		EXPECT_EQ(destination.Data(), storage);
		EXPECT_EQ(destination[1].Value, 8);
		destination = rox::core::Move(source);
		EXPECT_EQ(&destination.GetResource(), &second);
		EXPECT_EQ(destination.Size(), 2u);
		EXPECT_EQ(destination[0].Value, 7);
		EXPECT_TRUE(source.IsEmpty());
		EXPECT_EQ(source.Capacity(), 0u);
	}
	EXPECT_EQ(Tracked::Live, 0);
	EXPECT_EQ(first.GetStatistics().LiveAllocationCount, 0u);
	EXPECT_EQ(second.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ArrayTests, FailedClonePreservesNonEmptyOutputAndFailedGrowthDoesNotMoveArgument)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource resource(tracking, 2);
	{
		Array<Tracked> source(resource);
		ASSERT_TRUE(source.Reserve(2));
		source.EmplaceRequired(10);
		source.EmplaceRequired(20);
		Array<Tracked> output(resource);
		ASSERT_TRUE(output.Reserve(1));
		output.EmplaceRequired(99);
		Tracked* const oldOutput = output.Data();
		Tracked argument(30);
		resource.Reset(0);
		EXPECT_FALSE(source.CloneTo(resource, &output));
		EXPECT_EQ(output.Data(), oldOutput);
		ASSERT_EQ(output.Size(), 1u);
		EXPECT_EQ(output[0].Value, 99);
		EXPECT_FALSE(source.Add(Move(argument)));
		EXPECT_EQ(argument.Value, 30);
		EXPECT_EQ(source.Size(), 2u);
		EXPECT_EQ(source[0].Value, 10);
		EXPECT_EQ(source[1].Value, 20);
		EXPECT_EQ(Tracked::Live, 4);
		resource.Reset(1);
		ASSERT_TRUE(source.CloneTo(resource, &output));
		ASSERT_EQ(output.Size(), 2u);
		EXPECT_EQ(output[0].Value, 10);
		EXPECT_EQ(output[1].Value, 20);
		EXPECT_EQ(Tracked::Live, 5);
	}
	EXPECT_EQ(Tracked::Live, 0);
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
}
