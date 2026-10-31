#include <JobContext.h>
#include <SystemMemoryResource.h>

#include <gtest/gtest.h>
#include <limits>

using namespace rox::core;

TEST(JobContextTests, DefaultsAndCopiedCancellationReferToExecutionContext)
{
	SystemMemoryResource& memory = SystemMemoryResource::Get();
	CancellationSource source(memory);
	JobContext context{memory, JobContext::INLINE_WORKER_INDEX, source.GetToken()};
	JobContext copy = context;
	EXPECT_EQ(&copy.Scratch, &memory);
	EXPECT_EQ(copy.WorkerIndex, std::numeric_limits<usize>::max());
	EXPECT_EQ(copy.ExecutionContext, EJobExecutionContext::AnyThread);
	EXPECT_FALSE(copy.Cancellation.IsCancellationRequested());
	source.RequestCancellation();
	EXPECT_TRUE(context.Cancellation.IsCancellationRequested());
	EXPECT_TRUE(copy.Cancellation.IsCancellationRequested());
}
