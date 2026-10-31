#include <Task.hpp>
#include <TaskHandle.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(TaskHandleTests, CopiesObserveCompletionWithoutConsumingResult)
{
	Task<int32> task(Result<int32>::Success(17));
	TaskHandle handle = task.GetHandle();
	TaskHandle copy(handle);
	ASSERT_TRUE(handle.IsValid());
	ASSERT_TRUE(copy.IsValid());
	EXPECT_TRUE(copy.IsComplete());
	EXPECT_TRUE(copy.IsRetired());
	EXPECT_FALSE(copy.WasSkipped());
	EXPECT_TRUE(copy.GetName().IsEmpty());
	handle.Reset();
	EXPECT_FALSE(handle.IsValid());
	EXPECT_TRUE(copy.IsComplete());
	EXPECT_FALSE(task.IsConsumed());
	const Result<int32> result = task.TakeResult();
	ASSERT_TRUE(result);
	EXPECT_EQ(result.Value(), 17);
	EXPECT_TRUE(copy.IsComplete());
}
