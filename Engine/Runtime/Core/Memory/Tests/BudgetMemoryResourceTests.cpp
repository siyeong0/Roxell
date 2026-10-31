#include <BudgetMemoryResource.h>
#include <FailingMemoryResource.h>
#include <MemoryBlock.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace rox::core;

TEST(BudgetMemoryResourceTests, Rollback)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource upstream(tracked, 0);
	BudgetMemoryResource budget(upstream, 32);
	MemoryBlock upstreamFailure = MemoryBlock::Create(budget, {16, 8});
	ASSERT_TRUE(upstreamFailure.IsEmpty());
	MemoryBudgetStatistics stats = budget.GetStatistics();
	EXPECT_EQ(stats.UsedByteCount, 0);
	EXPECT_EQ(stats.ReservedByteCount, 0);
	EXPECT_EQ(stats.RejectedAllocationCount, 0);

	upstream.Reset(2);
	MemoryBlock first = MemoryBlock::Create(budget, {20, 8});
	ASSERT_FALSE(first.IsEmpty());
	MemoryBlock rejected = MemoryBlock::Create(budget, {13, 8});
	ASSERT_TRUE(rejected.IsEmpty());
	MemoryBlock second = MemoryBlock::Create(budget, {12, 8});
	ASSERT_FALSE(second.IsEmpty());
	stats = budget.GetStatistics();
	EXPECT_EQ(stats.UsedByteCount, 32);
	EXPECT_EQ(stats.PeakByteCount, 32);
	EXPECT_EQ(stats.ReservedByteCount, 0);
	EXPECT_EQ(stats.RejectedAllocationCount, 1);

	first.Reset();
	second.Reset();
	EXPECT_EQ(budget.GetStatistics().UsedByteCount, 0);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
}

TEST(BudgetMemoryResourceTests, ConcurrentAllocationsAndReleasesBalanceBothResources)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	BudgetMemoryResource budget(tracked, 4 * 32);
	std::atomic<int32> failures{0};
	std::mutex startMutex;
	std::condition_variable startCondition;
	bool bStart = false;
	bool bThreadsCreated = true;
	bool bWorkersReady = false;
	int32 readyCount = 0;
	std::thread workers[4];
	try
	{
		for (std::thread& worker : workers)
		{
			worker = std::thread([&]()
			{
				{
					std::unique_lock<std::mutex> lock(startMutex);
					++readyCount;
					startCondition.notify_all();
					if (!startCondition.wait_for(lock, std::chrono::seconds(10), [&]()
						{
							return bStart;
						}))
					{
						++failures;
						return;
					}
				}
				for (int32 iteration = 0; iteration < 500; ++iteration)
				{
					void* const memory = budget.Allocate({32, 16});
					if (memory == nullptr)
					{
						++failures;
						continue;
					}
					budget.Deallocate(memory, {32, 16});
				}
			});
		}
	}
	catch (...)
	{
		bThreadsCreated = false;
	}
	{
		std::unique_lock<std::mutex> lock(startMutex);
		if (bThreadsCreated)
		{
			bWorkersReady = startCondition.wait_for(lock, std::chrono::seconds(5), [&]()
			{
				return readyCount == 4;
			});
		}
		bStart = true;
	}
	startCondition.notify_all();
	for (std::thread& worker : workers)
	{
		if (worker.joinable())
		{
			worker.join();
		}
	}
	ASSERT_TRUE(bThreadsCreated);
	ASSERT_TRUE(bWorkersReady);
	EXPECT_EQ(failures.load(), 0);
	const MemoryBudgetStatistics budgetStats = budget.GetStatistics();
	EXPECT_EQ(budgetStats.UsedByteCount, 0);
	EXPECT_EQ(budgetStats.ReservedByteCount, 0);
	EXPECT_EQ(budgetStats.RejectedAllocationCount, 0);
	EXPECT_LE(budgetStats.PeakByteCount, 128);
	const MemoryStatistics trackedStats = tracked.GetStatistics();
	EXPECT_EQ(trackedStats.LiveAllocationCount, 0);
	EXPECT_EQ(trackedStats.LiveByteCount, 0);
	EXPECT_LE(trackedStats.PeakByteCount, 128);
}
