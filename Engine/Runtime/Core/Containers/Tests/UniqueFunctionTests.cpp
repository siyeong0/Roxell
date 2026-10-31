#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <UniqueFunction.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

namespace
{
	struct LargeTarget final
	{
		char Padding[UniqueFunction<int32() noexcept>::INLINE_CAPACITY + 1]{};
		int32 Value = 0;

		explicit LargeTarget(const int32 value) noexcept
			: Value(value)
		{
		}

		LargeTarget(LargeTarget&& other) noexcept
			: Value(other.Value)
		{
			other.Value = -1;
		}

		int32 operator()() noexcept
		{
			return Value;
		}
	};
}

TEST(UniqueFunctionTests, FailedAssignmentPreservesOwnerAndTarget)
{
	MemoryResource& system = SystemMemoryResource::Get();
	UniqueFunction<int32() noexcept> function(system, []() noexcept
	{
		return 7;
	});
	FailingMemoryResource failing(system, 0);
	LargeTarget target(42);
	EXPECT_FALSE(function.Assign(failing, Move(target)));
	EXPECT_EQ(target.Value, 42);
	EXPECT_EQ(function(), 7);
	EXPECT_EQ(function.GetResource(), &system);
	failing.Reset(1);
	ASSERT_TRUE(function.Assign(failing, Move(target)));
	EXPECT_EQ(target.Value, -1);
	EXPECT_EQ(function(), 42);
	EXPECT_EQ(function.GetResource(), &failing);
	function.Reset();
	EXPECT_FALSE(static_cast<bool>(function));
}

namespace
{
	struct TrackedTarget final
	{
		int32* Live;
		int32 Value;
		char Padding[UniqueFunction<int32() noexcept>::INLINE_CAPACITY + 1]{};

		TrackedTarget(int32* live, const int32 value) noexcept
			: Live(live)
			, Value(value)
		{
			++*Live;
		}

		TrackedTarget(TrackedTarget&& other) noexcept
			: Live(Exchange(other.Live, nullptr))
			, Value(other.Value)
		{
		}

		~TrackedTarget() noexcept
		{
			if (Live != nullptr)
			{
				--*Live;
			}
		}

		int32 operator()() noexcept
		{
			return Value;
		}
	};
}

TEST(UniqueFunctionTests, MoveAssignmentDestroysPreviousTargetAndTransfersOwnership)
{
	int32 live = 0;
	MemoryResource& resource = SystemMemoryResource::Get();
	{
		UniqueFunction<int32() noexcept> source(resource, TrackedTarget(&live, 42));
		UniqueFunction<int32() noexcept> destination(resource, TrackedTarget(&live, 7));
		ASSERT_EQ(live, 2);
		destination = Move(source);
		EXPECT_FALSE(static_cast<bool>(source));
		EXPECT_EQ(live, 1);
		EXPECT_EQ(destination(), 42);
		destination.Reset();
		EXPECT_EQ(live, 0);
		destination.Reset();
		EXPECT_EQ(live, 0);
	}
	EXPECT_EQ(live, 0);
}
