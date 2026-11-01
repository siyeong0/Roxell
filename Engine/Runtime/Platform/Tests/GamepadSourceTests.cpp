#include <IGamepadSource.h>

#include <gtest/gtest.h>

using namespace rox::core;
using namespace rox::os;

TEST(GamepadSourceTests, ButtonValidationRejectsReservedAndOutOfRangeValues)
{
	for (uint32 value = 0; value < 256; ++value)
	{
		const bool bExpected = value < 16 && value != 10 && value != 11;
		EXPECT_EQ(IsDefinedGamepadButton(static_cast<EGamepadButton>(value)), bExpected) << value;
	}
}

TEST(GamepadSourceTests, AxisValidationAcceptsOnlyDefinedAxes)
{
	for (uint32 value = 0; value < 256; ++value)
	{
		EXPECT_EQ(IsDefinedGamepadAxis(static_cast<EGamepadAxis>(value)), value < 6) << value;
	}
}
