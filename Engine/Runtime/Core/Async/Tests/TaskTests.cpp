#include <Task.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(TaskTests, ReadyFailureSurvivesMoveAndIsConsumedOnce)
{
	Task<int32> original(Result<int32>::Failure(EErrorCode::NotFound, "TEST", "Missing."));
	ASSERT_TRUE(original.IsValid());
	ASSERT_TRUE(original.IsReady());
	EXPECT_TRUE(original.IsRetired());
	EXPECT_FALSE(original.IsConsumed());
	Task<int32> moved(Move(original));
	EXPECT_FALSE(original.IsValid());
	ASSERT_TRUE(moved.IsValid());
	ASSERT_TRUE(moved.IsReady());
	const Result<int32> result = moved.TakeResult();
	ASSERT_FALSE(result);
	EXPECT_EQ(result.GetError().Code, EErrorCode::NotFound);
	EXPECT_TRUE(moved.IsConsumed());
}
