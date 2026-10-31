#include <JobScheduler.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

using namespace rox::core;

namespace
{
	class InlineBackend final : public IThreadBackend
	{
	public:
		NativeThreadStartResult Start(NativeThreadEntry&) noexcept override
		{
			++StartCount;
			return {};
		}

		void Join(NativeThread) noexcept override
		{
			ADD_FAILURE() << "A zero-worker scheduler must not join a thread.";
		}

		ThreadId CurrentThreadId() const noexcept override
		{
			return {1};
		}

	public:
		uint32 StartCount = 0;
	};
}

TEST(JobSchedulerTests, ZeroWorkersRunReadyWorkInlineWithInlineContext)
{
	TrackingMemoryResource memory(SystemMemoryResource::Get());
	InlineBackend backend;
	{
		JobScheduler scheduler(memory, backend);
		const ThreadStartResult started = scheduler.Start(JobSchedulerConfig{});
		ASSERT_EQ(started.Status, EThreadStartStatus::Started);
		EXPECT_TRUE(scheduler.IsRunning());
		bool bCalled = false;
		usize workerIndex = 0;
		EJobExecutionContext execution = EJobExecutionContext::MainThread;
		Task<int32> task = scheduler.Submit<int32>("inline", [&bCalled, &workerIndex, &execution](JobContext& context) noexcept
		{
			bCalled = true;
			workerIndex = context.WorkerIndex;
			execution = context.ExecutionContext;
			return Result<int32>::Success(42);
		});
		ASSERT_TRUE(task.IsReady());
		EXPECT_TRUE(bCalled);
		EXPECT_EQ(workerIndex, JobContext::INLINE_WORKER_INDEX);
		EXPECT_EQ(execution, EJobExecutionContext::AnyThread);
		const Result<int32> result = task.TakeResult();
		ASSERT_TRUE(result);
		EXPECT_EQ(result.Value(), 42);
		scheduler.Shutdown();
		EXPECT_FALSE(scheduler.IsRunning());
		EXPECT_EQ(backend.StartCount, 0u);
	}
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}
