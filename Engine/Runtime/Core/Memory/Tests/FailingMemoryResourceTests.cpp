#include <FailingMemoryResource.h>
#include <MemoryBlock.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(FailingMemoryResourceTests, ResourceBoundaries)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 1);
	EXPECT_EQ(failing.GetAllocationAttemptsBeforeFailure(), 1);
	EXPECT_EQ(failing.Allocate({0, 64}), nullptr);
	failing.Deallocate(nullptr, {0, 64});
	EXPECT_EQ(failing.GetAllocationAttemptCount(), 0);

	MemoryBlock memory = MemoryBlock::Create(failing, {33, 128});
	ASSERT_FALSE(memory.IsEmpty());
	EXPECT_EQ(reinterpret_cast<uintptr>(memory.GetData()) % 128, 0);
	MemoryBlock rejected = MemoryBlock::Create(failing, {1, 1});
	ASSERT_TRUE(rejected.IsEmpty());
	EXPECT_EQ(failing.GetAllocationAttemptCount(), 2);
	EXPECT_EQ(tracked.GetStatistics().LiveByteCount, 33);
	memory.Reset();
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);

	failing.Reset(1);
	EXPECT_EQ(failing.GetAllocationAttemptsBeforeFailure(), 1);
	MemoryBlock retried = MemoryBlock::Create(failing, {1, 1});
	ASSERT_FALSE(retried.IsEmpty());
	retried.Reset();
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}
