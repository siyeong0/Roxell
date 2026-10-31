#include <FailingMemoryResource.h>
#include <MemoryBlock.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#include <cstdlib>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

using namespace rox::core;

namespace
{
	void suppressAssertionDialog() noexcept
	{
#if defined(_WIN32)
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
	}
}

TEST(TrackingMemoryResourceTests, AccountsForSuccessfulAllocationsAndRetainsPeak)
{
	FailingMemoryResource upstream(SystemMemoryResource::Get(), 1);
	TrackingMemoryResource tracked(upstream);
	EXPECT_EQ(tracked.GetThreadSafety(), upstream.GetThreadSafety());

	MemoryBlock block = MemoryBlock::Create(tracked, {37, 128});
	ASSERT_FALSE(block.IsEmpty());
	EXPECT_EQ(reinterpret_cast<uintptr>(block.GetData()) % 128, 0);
	MemoryStatistics statistics = tracked.GetStatistics();
	EXPECT_EQ(statistics.LiveAllocationCount, 1);
	EXPECT_EQ(statistics.LiveByteCount, 37);
	EXPECT_EQ(statistics.PeakByteCount, 37);

	MemoryBlock failed = MemoryBlock::Create(tracked, {8, 8});
	EXPECT_TRUE(failed.IsEmpty());
	statistics = tracked.GetStatistics();
	EXPECT_EQ(statistics.LiveAllocationCount, 1);
	EXPECT_EQ(statistics.LiveByteCount, 37);
	EXPECT_EQ(statistics.PeakByteCount, 37);

	block.Reset();
	statistics = tracked.GetStatistics();
	EXPECT_EQ(statistics.LiveAllocationCount, 0);
	EXPECT_EQ(statistics.LiveByteCount, 0);
	EXPECT_EQ(statistics.PeakByteCount, 37);
}

TEST(TrackingMemoryResourceTests, RejectsForeignOwnerAndMismatchedRequest)
{
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			TrackingMemoryResource first(SystemMemoryResource::Get());
			TrackingMemoryResource second(SystemMemoryResource::Get());
			void* const memory = first.Allocate({16, 8});
			if (memory != nullptr)
			{
				second.Deallocate(memory, {16, 8});
			}
		},
		"Storage must be deallocated by the TrackingMemoryResource that allocated it");
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			TrackingMemoryResource resource(SystemMemoryResource::Get());
			void* const memory = resource.Allocate({16, 8});
			if (memory != nullptr)
			{
				resource.Deallocate(memory, {16, 4});
			}
		},
		"Deallocation metadata must match the original tracked allocation request");
}

TEST(TrackingMemoryResourceTests, RejectsDestructionWithLiveAllocation)
{
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			{
				TrackingMemoryResource resource(SystemMemoryResource::Get());
				(void)resource.Allocate({16, 8});
			}
		},
		"A tracking memory resource must outlive every tracked allocation");
}
