#include <FailingMemoryResource.h>
#include <PoolMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#include <cstdlib>
#include <initializer_list>

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

namespace
{
	class RecordingResource final : public MemoryResource
	{
	public:
		DiagnosticText GetName() const noexcept override
		{
			return "RecordingResource";
		}
		EMemoryResourceThreadSafety GetThreadSafety() const noexcept override
		{
			return EMemoryResourceThreadSafety::CallerConfined;
		}

	protected:
		void* allocate(const MemoryAllocationRequest allocation) noexcept override
		{
			LastAllocation = allocation;
			++AllocationCount;
			return SystemMemoryResource::Get().Allocate(allocation);
		}
		void deallocate(void* const memory, const MemoryAllocationRequest allocation) noexcept override
		{
			LastDeallocation = allocation;
			++DeallocationCount;
			SystemMemoryResource::Get().Deallocate(memory, allocation);
		}

	public:
		MemoryAllocationRequest LastAllocation{};
		MemoryAllocationRequest LastDeallocation{};
		usize AllocationCount = 0;
		usize DeallocationCount = 0;
	};
}

TEST(PoolMemoryResourceTests, StorageRequestMatchesSingleUpstreamAllocationAndRelease)
{
	RecordingResource upstream;
	const MemoryAllocationRequest request = PoolMemoryResource::GetStorageRequest(19, 32, 65);
	{
		PoolMemoryResource pool(upstream);
		ASSERT_TRUE(pool.Initialize(19, 32, 65));
		EXPECT_EQ(upstream.AllocationCount, 1);
		EXPECT_EQ(upstream.LastAllocation.ByteCount, request.ByteCount);
		EXPECT_EQ(upstream.LastAllocation.Alignment, request.Alignment);
	}
	EXPECT_EQ(upstream.DeallocationCount, 1);
	EXPECT_EQ(upstream.LastDeallocation.ByteCount, request.ByteCount);
	EXPECT_EQ(upstream.LastDeallocation.Alignment, request.Alignment);
}

TEST(PoolMemoryResourceTests, SlotRecordsPreserveRequestsAcrossCapacityWidthBoundary)
{
	for (const usize capacity : {usize{255}, usize{256}})
	{
		PoolMemoryResource pool(SystemMemoryResource::Get());
		ASSERT_TRUE(pool.Initialize(16, 8, capacity));
		void* blocks[256]{};
		MemoryAllocationRequest requests[256]{};
		bool bAllocatedAll = true;
		for (usize index = 0; index < capacity; ++index)
		{
			requests[index] = index % 2 == 0 ? MemoryAllocationRequest{16, 8} : MemoryAllocationRequest{3, 1};
			blocks[index] = pool.Allocate(requests[index]);
			if (blocks[index] == nullptr)
			{
				bAllocatedAll = false;
				break;
			}
		}
		bool bFull = false;
		bool bReused = false;
		if (bAllocatedAll)
		{
			void* const excess = pool.Allocate({1, 1});
			bFull = excess == nullptr;
			if (excess != nullptr)
			{
				pool.Deallocate(excess, {1, 1});
			}
			pool.Deallocate(blocks[capacity - 1], requests[capacity - 1]);
			requests[capacity - 1] = {7, 4};
			blocks[capacity - 1] = pool.Allocate(requests[capacity - 1]);
			bReused = blocks[capacity - 1] != nullptr;
		}
		for (usize index = 0; index < capacity; ++index)
		{
			if (blocks[index] != nullptr)
			{
				pool.Deallocate(blocks[index], requests[index]);
			}
		}
		EXPECT_TRUE(bAllocatedAll);
		EXPECT_TRUE(bFull);
		EXPECT_TRUE(bReused);
		EXPECT_EQ(pool.GetLiveBlockCount(), 0);
	}
}

TEST(PoolMemoryResourceTests, AllocationAndReuse)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	{
		PoolMemoryResource pool(failing);
		ASSERT_FALSE(pool.Initialize(16, 16, 2));
		ASSERT_FALSE(pool.IsInitialized());
		failing.Reset(1);
		ASSERT_TRUE(pool.Initialize(16, 16, 2));
		void* const oversized = pool.Allocate({17, 1});
		if (oversized != nullptr)
		{
			pool.Deallocate(oversized, {17, 1});
		}
		ASSERT_EQ(oversized, nullptr);
		void* const overAligned = pool.Allocate({1, 32});
		if (overAligned != nullptr)
		{
			pool.Deallocate(overAligned, {1, 32});
		}
		ASSERT_EQ(overAligned, nullptr);
		void* const first = pool.Allocate({16, 16});
		void* const second = pool.Allocate({1, 1});
		if (first == nullptr || second == nullptr || first == second)
		{
			if (first != nullptr)
			{
				pool.Deallocate(first, {16, 16});
			}
			if (second != nullptr && second != first)
			{
				pool.Deallocate(second, {1, 1});
			}
			FAIL() << "Pool returned invalid live blocks.";
		}
		void* const excess = pool.Allocate({1, 1});
		const bool bFull = excess == nullptr && pool.GetLiveBlockCount() == 2;
		if (excess != nullptr)
		{
			pool.Deallocate(excess, {1, 1});
		}
		pool.Deallocate(first, {16, 16});
		void* const reused = pool.Allocate({8, 8});
		const bool bReused = reused != nullptr && pool.GetLiveBlockCount() == 2;
		pool.Deallocate(second, {1, 1});
		if (reused != nullptr)
		{
			pool.Deallocate(reused, {8, 8});
		}
		ASSERT_TRUE(bFull);
		ASSERT_TRUE(bReused);
	}
	ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 0);
}

TEST(PoolMemoryResourceTests, EmptyPoolIsInitializedWithoutUpstreamAllocation)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	PoolMemoryResource pool(failing);
	ASSERT_TRUE(pool.Initialize(16, 8, 0));
	EXPECT_TRUE(pool.IsInitialized());
	EXPECT_EQ(pool.GetBlockCapacity(), 0);
	EXPECT_EQ(pool.GetLiveBlockCount(), 0);
	EXPECT_EQ(pool.GetBlockByteCount(), 16);
	EXPECT_EQ(pool.GetBlockAlignment(), 8);
	EXPECT_EQ(&pool.GetUpstreamResource(), &failing);
	EXPECT_EQ(pool.GetThreadSafety(), EMemoryResourceThreadSafety::CallerConfined);
	void* const unexpected = pool.Allocate({1, 1});
	EXPECT_EQ(unexpected, nullptr);
	if (unexpected != nullptr)
	{
		pool.Deallocate(unexpected, {1, 1});
	}
	EXPECT_EQ(failing.GetAllocationAttemptCount(), 0);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(PoolMemoryResourceTests, MismatchedDeallocationIsRejected)
{
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			PoolMemoryResource pool(SystemMemoryResource::Get());
			if (pool.Initialize(16, 8, 1))
			{
				void* const block = pool.Allocate({8, 8});
				if (block != nullptr)
				{
					pool.Deallocate(block, {4, 8});
				}
			}
		},
		"must match the original byte count and alignment");
}

TEST(PoolMemoryResourceTests, ReleasingSameBlockTwiceIsRejected)
{
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			PoolMemoryResource pool(SystemMemoryResource::Get());
			if (pool.Initialize(16, 8, 1))
			{
				void* const block = pool.Allocate({8, 8});
				if (block != nullptr)
				{
					pool.Deallocate(block, {8, 8});
					pool.Deallocate(block, {8, 8});
				}
			}
		},
		"must identify a live allocation");
}

TEST(PoolMemoryResourceTests, ForeignAndInteriorPointersAreRejected)
{
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			PoolMemoryResource pool(SystemMemoryResource::Get());
			if (pool.Initialize(16, 8, 1))
			{
				Byte foreign[16]{};
				pool.Deallocate(foreign, {16, 8});
			}
		},
		"A memory pool release must belong to this pool's storage");
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			PoolMemoryResource pool(SystemMemoryResource::Get());
			if (pool.Initialize(16, 8, 1))
			{
				Byte* const memory = static_cast<Byte*>(pool.Allocate({16, 8}));
				if (memory != nullptr)
				{
					pool.Deallocate(memory + 1, {16, 8});
				}
			}
		},
		"A memory pool release must point to the start of a slot");
}
