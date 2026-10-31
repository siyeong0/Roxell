#include <FailingMemoryResource.h>
#include <LinearMemoryScope.hpp>
#include <MemoryBlock.h>
#include <ScratchMemory.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#include <utility>

using namespace rox::core;

TEST(ScratchMemoryTests, Scopes)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	ScratchMemory scratch;
	ASSERT_FALSE(scratch.Initialize(failing, 128));
	ASSERT_FALSE(scratch.IsInitialized());
	failing.Reset(1);
	ASSERT_TRUE(scratch.Initialize(failing, 128));
	LinearMemoryResource& arena = scratch.GetResource();
	void* const permanent = arena.Allocate({16, 1});
	ASSERT_NE(permanent, nullptr);
	const usize usedBeforeScope = arena.GetUsedByteCount();
	void* temporary = nullptr;
	{
		LinearMemoryScope scope(arena);
		temporary = arena.Allocate({32, 1});
		ASSERT_NE(temporary, nullptr);
		ASSERT_TRUE(arena.Allocate({128, 1}) == nullptr);
		ASSERT_TRUE(arena.GetUsedByteCount() == usedBeforeScope + 32);
	}
	ASSERT_TRUE(arena.GetUsedByteCount() == usedBeforeScope);
	ASSERT_TRUE(arena.GetPeakByteCount() == usedBeforeScope + 32);
	ASSERT_TRUE(arena.Allocate({32, 1}) == temporary);
}

TEST(ScratchMemoryTests, AdoptsMemoryBlock)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	{
		MemoryBlock block = MemoryBlock::CreateRequired(tracked, {64, 16});
		void* const backing = block.GetData();
		ScratchMemory scratch(std::move(block));
		ASSERT_TRUE(block.IsEmpty() && scratch.IsInitialized());
		ASSERT_TRUE(scratch.GetResource().GetCapacityByteCount() == 64);
		ASSERT_TRUE(scratch.GetResource().Allocate({64, 1}) == backing);
	}
	ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 0);
}

TEST(ScratchMemoryTests, ZeroCapacityIsInitializedWithoutAllocation)
{
	FailingMemoryResource failing(SystemMemoryResource::Get(), 0);
	ScratchMemory initialized;
	ASSERT_TRUE(initialized.Initialize(failing, 0));
	EXPECT_TRUE(initialized.IsInitialized());
	EXPECT_EQ(initialized.GetResource().GetCapacityByteCount(), 0);
	EXPECT_EQ(initialized.GetResource().Allocate({1, 1}), nullptr);
	EXPECT_EQ(failing.GetAllocationAttemptCount(), 0);

	MemoryBlock empty;
	ScratchMemory adopted(std::move(empty));
	EXPECT_TRUE(adopted.IsInitialized());
	EXPECT_EQ(adopted.GetResource().GetCapacityByteCount(), 0);
	EXPECT_EQ(adopted.GetResource().Allocate({1, 1}), nullptr);
}
