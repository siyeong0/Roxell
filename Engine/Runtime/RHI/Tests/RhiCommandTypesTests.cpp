#include <RhiCommandTypes.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	TEST(RhiCommandTypes, FloatingClearPreservesAllChannels)
	{
		const ClearColor color = ClearColor::Float(-1.0f, 0.25f, 2.0f, 0.5f);
		EXPECT_EQ(color.GetType(), EClearColorType::Float);
		EXPECT_FLOAT_EQ(color.GetFloat()[0], -1.0f);
		EXPECT_FLOAT_EQ(color.GetFloat()[1], 0.25f);
		EXPECT_FLOAT_EQ(color.GetFloat()[2], 2.0f);
		EXPECT_FLOAT_EQ(color.GetFloat()[3], 0.5f);
	}

	TEST(RhiCommandTypes, IntegerClearDoesNotRoundThroughFloat)
	{
		const ClearColor unsignedColor = ClearColor::Uint(0xffffffffu, 16777217u, 0, 1);
		EXPECT_EQ(unsignedColor.GetType(), EClearColorType::Uint);
		EXPECT_EQ(unsignedColor.GetUint()[0], 0xffffffffu);
		EXPECT_EQ(unsignedColor.GetUint()[1], 16777217u);
		const ClearColor signedColor = ClearColor::Sint(-16777217, -1, 0, 1);
		EXPECT_EQ(signedColor.GetType(), EClearColorType::Sint);
		EXPECT_EQ(signedColor.GetSint()[0], -16777217);
		EXPECT_EQ(signedColor.GetSint()[1], -1);
	}

	TEST(RhiCommandTypesDeathTest, ClearColorRejectsTheWrongRepresentation)
	{
		EXPECT_DEATH((void)ClearColor::Uint(0, 0, 0, 0).GetFloat(), "floating-point");
		EXPECT_DEATH((void)ClearColor::Float(0, 0, 0, 0).GetSint(), "signed integer");
	}
}
