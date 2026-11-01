#include "SubmissionCompletion.h"
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	using namespace core;

	TEST(SubmissionCompletion, LaterQueuesCannotCompleteAnEarlierGap)
	{
		SubmissionCompletion completion(SystemMemoryResource::Get());
		ASSERT_TRUE(completion.TryReserveCredits(3));
		completion.ReleaseCredit();
		completion.RecordSubmission(1, SubmissionCompletion::ELane::Direct);
		completion.ReleaseCredit();
		completion.RecordSubmission(2, SubmissionCompletion::ELane::Compute);
		completion.ReleaseCredit();
		completion.RecordSubmission(3, SubmissionCompletion::ELane::Copy);
		EXPECT_EQ(completion.CompletedPrefix(0, 2, 3), 0u);
		EXPECT_EQ(completion.CompletedPrefix(1, 0, 3), 1u);
		EXPECT_EQ(completion.CompletedPrefix(1, 2, 3), 3u);
		completion.Advance(1, 2, 3);
		EXPECT_EQ(completion.CompletedPrefix(1, 2, 3), 3u);
		ASSERT_TRUE(completion.TryReserveOne());
		completion.RecordSubmission(4, SubmissionCompletion::ELane::Direct);
		EXPECT_EQ(completion.CompletedPrefix(1, 2, 3), 3u);
		EXPECT_EQ(completion.CompletedPrefix(4, 2, 3), 4u);
	}

	TEST(SubmissionCompletion, FailedReservationCanBeRetried)
	{
		FailingMemoryResource memory(SystemMemoryResource::Get(), 0);
		SubmissionCompletion completion(memory);
		EXPECT_FALSE(completion.TryReserveCredits(1));
		EXPECT_EQ(completion.CompletedPrefix(0, 0, 0), 0u);
		memory.Reset(1);
		ASSERT_TRUE(completion.TryReserveCredits(1));
		completion.ReleaseCredit();
		completion.RecordSubmission(1, SubmissionCompletion::ELane::Copy);
		EXPECT_EQ(completion.CompletedPrefix(0, 0, 1), 1u);
	}

	TEST(SubmissionCompletionDeathTest, CreditsCannotBeReleasedTwice)
	{
		SubmissionCompletion completion(SystemMemoryResource::Get());
		ASSERT_TRUE(completion.TryReserveCredits(1));
		completion.ReleaseCredit();
		EXPECT_DEATH(completion.ReleaseCredit(), "released once");
	}
}
