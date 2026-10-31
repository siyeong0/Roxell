#include <RingBuffer.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>
#include <utility>

using namespace rox::core;

namespace
{
	struct Tracked final
	{
		inline static int32 Live = 0;
		int32 Value;

		explicit Tracked(const int32 value) noexcept
			: Value(value)
		{
			++Live;
		}
		Tracked(const Tracked& other) noexcept
			: Tracked(other.Value)
		{
		}
		Tracked(Tracked&& other) noexcept
			: Tracked(other.Value)
		{
			other.Value = -1;
		}
		Tracked& operator=(const Tracked&) = delete;
		Tracked& operator=(Tracked&&) = delete;
		~Tracked() noexcept
		{
			--Live;
		}
	};
}

TEST(RingBufferTests, WrapAndRefusal)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	{
		RingBuffer<Tracked> queue(tracking, 2);
		queue.EmplaceRequired(11);
		queue.EmplaceRequired(22);
		Tracked pending(33);
		ASSERT_TRUE(!queue.Push(std::move(pending)) && pending.Value == 33);
		ASSERT_TRUE(queue.Size() == 2 && queue.Front().Value == 11 && queue.Back().Value == 22);
		queue.Pop();
		ASSERT_TRUE(queue.Push(std::move(pending)));
		ASSERT_TRUE(queue[0].Value == 22 && queue[1].Value == 33);
		queue.Clear();
		ASSERT_TRUE(queue.IsEmpty() && Tracked::Live == 1);
	}
	ASSERT_TRUE(Tracked::Live == 0 && tracking.GetStatistics().LiveAllocationCount == 0);
}
