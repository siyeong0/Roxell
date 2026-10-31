#include <SpscQueue.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <UniquePtr.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <latch>
#include <thread>
#include <utility>

using namespace rox::core;

namespace
{
	struct Packet final
	{
		usize Producer = 0;
		usize Sequence = 0;
	};
}

TEST(SpscQueueTests, BoundedOwnership)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		SpscQueue<UniquePtr<int32>> queue(tracking, 2);
		UniquePtr<int32> output;
		ASSERT_TRUE(!queue.Pop(&output) && !output);
		UniquePtr<int32> first = MakeUniqueRequired<int32>(tracking, 11);
		UniquePtr<int32> second = MakeUniqueRequired<int32>(tracking, 22);
		UniquePtr<int32> third = MakeUniqueRequired<int32>(tracking, 33);
		ASSERT_TRUE(queue.Push(std::move(first)) && queue.Push(std::move(second)));
		ASSERT_TRUE(!queue.Push(std::move(third)) && third && *third == 33);
		ASSERT_TRUE(queue.Pop(&output) && *output == 11);
		ASSERT_TRUE(queue.Push(std::move(third)) && !third);
		ASSERT_TRUE(queue.Pop(&output) && *output == 22);
		ASSERT_TRUE(queue.Pop(&output) && *output == 33);
		ASSERT_TRUE(!queue.Pop(&output) && *output == 33);
	}
	ASSERT_TRUE(tracking.GetStatistics().LiveAllocationCount == 0);
}

TEST(SpscQueueTests, ConcurrentDelivery)
{
	constexpr usize PRODUCER_COUNT = 1;
	constexpr usize PACKETS_PER_PRODUCER = 256;
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	bool bValid = true;
	std::atomic<bool> bStop = false;
	{
		SpscQueue<Packet> queue(tracking, 3);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
		std::latch start(1);
		std::array<std::thread, PRODUCER_COUNT> producers;
		for (usize producer = 0; producer < PRODUCER_COUNT; ++producer)
		{
			producers[producer] = std::thread([&, producer]
			{
				start.wait();
				for (usize sequence = 0; sequence < PACKETS_PER_PRODUCER; ++sequence)
				{
					while (!queue.Emplace(Packet{producer, sequence}))
					{
						if (bStop.load() || std::chrono::steady_clock::now() >= deadline)
						{
							bStop.store(true);
							return;
						}
						std::this_thread::yield();
					}
				}
			});
		}
		start.count_down();
		std::array<usize, PRODUCER_COUNT> expected{};
		for (usize received = 0; received < PRODUCER_COUNT * PACKETS_PER_PRODUCER; ++received)
		{
			Packet packet;
			while (!queue.Pop(&packet))
			{
				if (bStop.load() || std::chrono::steady_clock::now() >= deadline)
				{
					bStop.store(true);
					break;
				}
				std::this_thread::yield();
			}
			if (bStop.load())
			{
				break;
			}
			if (packet.Producer >= PRODUCER_COUNT)
			{
				bValid = false;
				continue;
			}
			bValid = bValid && packet.Sequence == expected[packet.Producer];
			++expected[packet.Producer];
		}
		for (std::thread& producer : producers)
		{
			producer.join();
		}
		EXPECT_FALSE(bStop.load()) << "Queue delivery did not finish before the deadline.";
		for (const usize count : expected)
		{
			bValid = bValid && count == PACKETS_PER_PRODUCER;
		}
		Packet extra;
		ASSERT_FALSE(queue.Pop(&extra));
	}
	ASSERT_TRUE(bValid && tracking.GetStatistics().LiveAllocationCount == 0);
}
