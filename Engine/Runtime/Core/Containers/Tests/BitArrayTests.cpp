#include <BitArray.h>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(BitArrayTests, BoundariesAndFailure)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource resource(tracking, 1);
	{
		BitArray bits(resource);
		ASSERT_TRUE(bits.Resize(65, true));
		bits.Set(63, false);
		ASSERT_TRUE(bits.Count() == 64 && bits.FindFirstClear() == 63);
		resource.Reset(0);
		ASSERT_TRUE(!bits.Resize(bits.Capacity() + 1));
		ASSERT_TRUE(bits.Size() == 65 && bits.Count() == 64 && !bits.Test(63) && bits.Test(64));
		bits.ResizeRequired(63);
		bits.ResizeRequired(65);
		ASSERT_TRUE(bits.Count() == 63 && !bits.Test(63) && !bits.Test(64));
	}
	ASSERT_TRUE(tracking.GetStatistics().LiveAllocationCount == 0);
}
