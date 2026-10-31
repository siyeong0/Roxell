#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TaskSource.hpp>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#include <latch>
#include <thread>
#include <utility>

using namespace rox::core;

TEST(TaskSourceTests, CompletionAndOwnership)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracking, 0);
	ASSERT_FALSE(TaskSource<int32>::Create(failing).IsValid());
	{
		TaskSource<int32> source(tracking);
		Task<int32> task = source.GetTask();
		TaskHandle handle = task.GetHandle();
		const bool bPending = !task.IsReady() && !handle.IsComplete();
		const bool bWon = source.TrySetResult(Result<int32>::Success(42));
		const bool bLost = !source.TrySetResult(Result<int32>::Success(99));
		ASSERT_TRUE(bPending && bWon && bLost && task.IsReady() && handle.IsComplete());
		Task<int32> moved = std::move(task);
		ASSERT_TRUE(!task.IsValid() && moved.IsValid());
		ASSERT_TRUE(moved.TakeResult().Value() == 42 && moved.IsConsumed());
	}
	ASSERT_TRUE(tracking.GetStatistics().LiveAllocationCount == 0);
}

TEST(TaskSourceTests, CompetingCompletion)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		TaskSource<int32> source(tracking);
		Task<int32> task = source.GetTask();
		std::latch start(1);
		bool bFirstWon = false;
		bool bSecondWon = false;
		std::thread first([&]
		{
			start.wait();
			bFirstWon = source.TrySetResult(Result<int32>::Success(17));
		});
		std::thread second([&]
		{
			start.wait();
			bSecondWon = source.TrySetResult(Result<int32>::Success(29));
		});
		start.count_down();
		first.join();
		second.join();
		ASSERT_TRUE(bFirstWon != bSecondWon && task.IsReady());
		ASSERT_TRUE(task.TakeResult().Value() == (bFirstWon ? 17 : 29));
	}
	ASSERT_TRUE(tracking.GetStatistics().LiveAllocationCount == 0);
}

TEST(TaskSourceTests, PartialAllocationIsReleased)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource resource(tracking, 1);
	TaskSource<int32> source = TaskSource<int32>::Create(resource);
	ASSERT_TRUE(!source.IsValid() && resource.GetAllocationAttemptCount() == 2);
	ASSERT_TRUE(tracking.GetStatistics().LiveAllocationCount == 0);
}

TEST(TaskSourceTests, CancelledResultSurvivesSourceMoveAndRelease)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		TaskSource<int32> source(tracking);
		Task<int32> task = source.GetTask();
		TaskHandle handle = task.GetHandle();
		{
			TaskSource<int32> moved(std::move(source));
			EXPECT_FALSE(source.IsValid());
			moved.SetResult(Result<int32>::Failure(EErrorCode::Cancelled, "TEST_CANCELLED", "Cancelled."));
		}
		EXPECT_TRUE(handle.IsComplete());
		ASSERT_TRUE(task.IsReady());
		const Result<int32> result = task.TakeResult();
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().Code, EErrorCode::Cancelled);
		EXPECT_TRUE(task.IsConsumed());
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
}
