#include <Cancellation.h>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#include <atomic>
#include <latch>
#include <thread>

using namespace rox::core;

namespace
{
	void countCancellation(void* const context) noexcept
	{
		++*static_cast<int32*>(context);
	}

	void countConcurrentCancellation(void* const context) noexcept
	{
		++*static_cast<std::atomic<int32>*>(context);
	}
}

TEST(CancellationTests, RegistrationRacingRequestInvokesExactlyOnce)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	for (int32 iteration = 0; iteration < 32; ++iteration)
	{
		CancellationSource source(tracking);
		CancellationToken token = source.GetToken();
		CancellationRegistration registration;
		std::atomic<int32> called = 0;
		ECancellationRegistrationResult result = ECancellationRegistrationResult::NotCancellable;
		std::latch start(1);
		std::thread registering([&]
		{
			start.wait();
			result = registration.Register(token, countConcurrentCancellation, &called);
		});
		start.count_down();
		// A test-runner timeout is needed to bound dispatch and join if the implementation deadlocks.
		source.RequestCancellation();
		registering.join();
		EXPECT_TRUE(result == ECancellationRegistrationResult::Registered ||
		            result == ECancellationRegistrationResult::InvokedSynchronously);
		EXPECT_EQ(called.load(), 1);
		registration.Reset();
	}
	EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
}

TEST(CancellationTests, Lifecycle)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracking, 0);
	ASSERT_FALSE(CancellationSource::Create(failing).IsValid());
	{
		CancellationSource source(tracking);
		CancellationToken token = source.GetToken();
		CancellationToken copy = token;
		int32 called = 0;
		int32 removed = 0;
		CancellationRegistration active;
		CancellationRegistration cancelled;
		CancellationRegistration inactive;
		ASSERT_TRUE(inactive.Register(CancellationToken{}, countCancellation, &called) ==
		        ECancellationRegistrationResult::NotCancellable);
		ASSERT_TRUE(active.Register(token, countCancellation, &called) == ECancellationRegistrationResult::Registered);
		ASSERT_TRUE(cancelled.Register(token, countCancellation, &removed) == ECancellationRegistrationResult::Registered);
		cancelled.Reset();
		ASSERT_TRUE(token.CanBeCancelled() && !token.IsCancellationRequested());
		source.RequestCancellation();
		source.RequestCancellation();
		ASSERT_TRUE(copy.IsCancellationRequested() && called == 1 && removed == 0);
		CancellationRegistration late;
		ASSERT_TRUE(late.Register(copy, countCancellation, &called) ==
		        ECancellationRegistrationResult::InvokedSynchronously);
		ASSERT_TRUE(called == 2);
	}
	ASSERT_TRUE(tracking.GetStatistics().LiveAllocationCount == 0);
}
