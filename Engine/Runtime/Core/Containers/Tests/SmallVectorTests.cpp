#include <FailingMemoryResource.h>
#include <SmallVector.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

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

TEST(SmallVectorTests, InlineSpillAndFailure)
{
	TrackingMemoryResource tracking(SystemMemoryResource::Get());
	FailingMemoryResource resource(tracking, 0);
	{
		SmallVector<Tracked, 2> values(resource);
		values.EmplaceRequired(4);
		values.EmplaceRequired(8);
		ASSERT_TRUE(!values.Reserve(3));
		ASSERT_TRUE(values.Size() == 2 && values[0].Value == 4 && values[1].Value == 8);
		resource.Reset(1);
		values.EmplaceRequired(values[0].Value);
		ASSERT_TRUE(values.Size() == 3 && values[0].Value == 4 && values[2].Value == 4);
		values.Reset();
		ASSERT_TRUE(values.IsEmpty() && Tracked::Live == 0);
		ASSERT_TRUE(tracking.GetStatistics().LiveAllocationCount == 0);
	}
}
