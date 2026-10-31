#include <ThreadPrimitives.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace rox::core;

TEST(ThreadPrimitivesTests, CurrentThreadIdentityIsStableAndDistinctWhileWorkerLives)
{
	const ExecutionThreadId mainID = CurrentExecutionThreadId();
	EXPECT_EQ(mainID, CurrentExecutionThreadId());
	EXPECT_NE(mainID, ExecutionThreadId{});
	ExecutionThreadId workerID;
	std::thread worker([&workerID]()
	{
		workerID = CurrentExecutionThreadId();
		YieldThread();
		SleepFor(std::chrono::milliseconds(0));
	});
	worker.join();
	EXPECT_NE(workerID, ExecutionThreadId{});
	EXPECT_NE(workerID, mainID);
}
