#include <FailingMemoryResource.h>
#include <FlatMap.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

namespace
{
	struct BucketOrder final
	{
		bool operator()(const int32 left, const int32 right) const noexcept
		{
			return left / 10 < right / 10;
		}
	};
}

TEST(FlatMapTests, OrderAndEquivalentKeys)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource resource(tracking, 1);
	{
		FlatMap<int32, int32, BucketOrder> map(resource);
		ASSERT_TRUE(map.Reserve(3));
		ASSERT_TRUE(map.Emplace(31, 3).bInserted);
		ASSERT_TRUE(map.Emplace(11, 1).bInserted);
		ASSERT_TRUE(map.Emplace(21, 2).bInserted);
		resource.Reset(0);
		const FlatMap<int32, int32, BucketOrder>::InsertResult duplicate = map.Emplace(28, 99);
		ASSERT_TRUE(!duplicate.bInserted && duplicate.Value != nullptr && *duplicate.Value == 2);
		ASSERT_TRUE(map.Emplace(41, 4).Value == nullptr);
		ASSERT_TRUE(map.Size() == 3 && map.At(0).GetKey() == 11 && map.At(1).GetKey() == 21);
		ASSERT_TRUE(map.At(2).GetKey() == 31 && map.Get(28) == 2);
		ASSERT_TRUE(map.Erase(21) && map.Find(28) == nullptr && map.Get(31) == 3);
	}
	ASSERT_TRUE(tracking.GetStatistics().LiveAllocationCount == 0);
}
