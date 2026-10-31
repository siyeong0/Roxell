#include <MemoryResource.h>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>

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

TEST(MemoryResourceTests, SizeArithmeticRejectsOverflowWithoutChangingOutput)
{
	constexpr usize MAXIMUM = NumericLimits<usize>::max();
	usize result = 7;
	EXPECT_TRUE(AddMemorySize(MAXIMUM - 1, 1, &result));
	EXPECT_EQ(result, MAXIMUM);
	EXPECT_FALSE(AddMemorySize(MAXIMUM, 1, &result));
	EXPECT_EQ(result, MAXIMUM);
	EXPECT_TRUE(MultiplyMemorySize(MAXIMUM, 0, &result));
	EXPECT_EQ(result, 0);
	EXPECT_FALSE(MultiplyMemorySize(MAXIMUM, 2, &result));
	EXPECT_EQ(result, 0);
	EXPECT_TRUE(MultiplyMemorySize(6, 7, &result));
	EXPECT_EQ(result, 42);
}

TEST(MemoryResourceTests, AlignmentRequiresNonzeroPowerOfTwo)
{
	EXPECT_FALSE(IsValidMemoryAlignment(0));
	EXPECT_TRUE(IsValidMemoryAlignment(1));
	EXPECT_TRUE(IsValidMemoryAlignment(128));
	EXPECT_FALSE(IsValidMemoryAlignment(3));
}

TEST(MemoryResourceTests, RequiredAllocationReportsFailureDetails)
{
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			FailingMemoryResource failing(SystemMemoryResource::Get(), 0);
			static const MemoryResource* s_ExpectedResource = nullptr;
			s_ExpectedResource = &failing;
			SetMemoryFailureHandler([](const MemoryFailure& failure) noexcept
			{
				const bool bExpected = failure.Reason == EMemoryFailureReason::AllocationFailed &&
				                       failure.Resource == s_ExpectedResource && failure.ResourceName == "FailingMemoryResource" &&
				                       failure.Allocation.ByteCount == 32 && failure.Allocation.Alignment == 16;
				std::fputs(bExpected ? "memory-failure-details:ok\n" : "memory-failure-details:incorrect\n", stderr);
				std::fflush(stderr);
			});
			(void)failing.AllocateRequired({32, 16});
		},
		"memory-failure-details:ok");
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			FailingMemoryResource failing(SystemMemoryResource::Get(), 0);
			(void)failing.AllocateRequired({32, 16});
		},
		"mandatory memory allocation failed");
}

TEST(MemoryResourceTests, InvalidRequestsAreRejectedBeforeDelegation)
{
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			(void)SystemMemoryResource::Get().Allocate({0, 3});
		},
		"Memory allocation alignment must be a non-zero power of two");
	EXPECT_DEATH(
		{
			suppressAssertionDialog();
			int32 value = 0;
			SystemMemoryResource::Get().Deallocate(&value, {0, 4});
		},
		"A non-null allocation must have a non-zero byte count");
}
