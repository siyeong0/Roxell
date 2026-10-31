#include <SystemMemoryResource.h>
#include <TaskCombinators.hpp>
#include <TaskSource.hpp>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(TaskCombinatorsTests, PreservesOrderAndErrors)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		Array<Task<int32>> tasks(tracking);
		tasks.AddRequired(MakeReadyTask(Result<int32>::Success(7)));
		tasks.AddRequired(MakeReadyTask(Result<int32>::Failure(EErrorCode::NotFound, "TEST", "Missing.")));
		tasks.AddRequired(MakeReadyTask(Result<int32>::Success(13)));
		Task<Array<Result<int32>>> all = WhenAll(tracking, Move(tasks));
		ASSERT_TRUE(all.IsReady());
		Result<Array<Result<int32>>> results = all.TakeResult();
		ASSERT_TRUE(results && results.Value().Size() == 3);
		ASSERT_TRUE(results.Value()[0]);
		ASSERT_TRUE(results.Value()[2]);
		ASSERT_TRUE(results.Value()[0].Value() == 7 && !results.Value()[1]);
		ASSERT_TRUE(results.Value()[1].GetError().Code == EErrorCode::NotFound);
		ASSERT_TRUE(results.Value()[2].Value() == 13);
		all.WaitForRetirement();
	}
	ASSERT_TRUE(tracking.GetStatistics().LiveAllocationCount == 0);
}
