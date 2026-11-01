#include <WindowsGamepadSource.h>
#include <Utility.hpp>

#include <gtest/gtest.h>


using namespace rox::core;
using namespace rox::os;

TEST(WindowsGamepadSourceTests, RepeatedPollingPublishesEverySlot)
{
	WindowsGamepadSource source;
	for (int32 attempt = 0; attempt < 2; ++attempt)
	{
		Result<GamepadStateBatch> result = source.Poll();
		ASSERT_TRUE(result);
		ASSERT_EQ(result.Value().States.Size(), MAXIMUM_GAMEPAD_SLOTS);
		ASSERT_NE(result.Value().States.Data(), nullptr);
	}
}

TEST(WindowsGamepadSourceTests, MovedSourcesRemainPollable)
{
	WindowsGamepadSource original;
	ASSERT_TRUE(original.Poll());
	WindowsGamepadSource moved(rox::core::Move(original));
	ASSERT_TRUE(moved.Poll());
	WindowsGamepadSource assigned;
	assigned = rox::core::Move(moved);
	const Result<GamepadStateBatch> result = assigned.Poll();
	ASSERT_TRUE(result);
	EXPECT_EQ(result.Value().States.Size(), MAXIMUM_GAMEPAD_SLOTS);
}
