#include <RandomStream.h>

#include <gtest/gtest.h>

#include <limits>

using namespace rox::core;
using namespace rox::math;

TEST(RandomStreamTests, RandomStreamPublishedSequenceAndRestore)
{
	RandomStream stream(42, 54);
	const RandomStreamState initial = stream.GetState();
	constexpr uint32 EXPECTED[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u};
	for (const uint32 value : EXPECTED)
	{
		ASSERT_EQ(stream.NextU32(), value);
	}
	stream.RestoreState(initial);
	ASSERT_EQ(stream.NextU64(), 0xa15c02b77b47f409ULL);
	for (uint32 index = 0; index < 1000; ++index)
	{
		const int32 value = stream.NextI32(-7, 11);
		ASSERT_TRUE(value >= -7);
		ASSERT_TRUE(value < 11);
	}
}

TEST(RandomStreamTests, ResetAndBooleanEndpointsPreserveDocumentedDrawSequence)
{
	RandomStream stream(42, 54);
	RandomStream reference(42, 54);
	EXPECT_FALSE(stream.Bool(0.0f));
	(void)reference.Float01();
	EXPECT_EQ(stream.GetState(), reference.GetState());
	EXPECT_TRUE(stream.Bool(1.0f));
	(void)reference.Float01();
	EXPECT_EQ(stream.GetState(), reference.GetState());
	stream.Reset(42, 54);
	EXPECT_EQ(stream.NextU32(), 0xa15c02b7u);
}

TEST(RandomStreamTests, NumericRangesStayHalfOpenAtExtremeBounds)
{
	RandomStream stream(123, 456);
	for (uint32 index = 0; index < 256; ++index)
	{
		EXPECT_LT(stream.NextU32(0, std::numeric_limits<uint32>::max()), std::numeric_limits<uint32>::max());
		EXPECT_LT(stream.NextU64(0, std::numeric_limits<uint64>::max()), std::numeric_limits<uint64>::max());
		const int64 signedValue = stream.NextI64(std::numeric_limits<int64>::min(), std::numeric_limits<int64>::max());
		EXPECT_LT(signedValue, std::numeric_limits<int64>::max());
		const float32 value = stream.Float(-4.0f, 7.0f);
		EXPECT_GE(value, -4.0f);
		EXPECT_LT(value, 7.0f);
		const float64 real = stream.Real<float64>(-3.0, 5.0);
		EXPECT_GE(real, -3.0);
		EXPECT_LT(real, 5.0);
	}
}
