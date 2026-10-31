#include <RandomAlgorithms.hpp>

#include <gtest/gtest.h>

using namespace rox::core;
using namespace rox::math;

TEST(RandomAlgorithmsTests, EmptyAndSingletonShuffleDoNotConsumeDraws)
{
	RandomStream stream(42, 54);
	const RandomStreamState before = stream.GetState();
	int32 value = 7;
	RandomShuffle<int32>(stream, nullptr, 0);
	RandomShuffle(stream, &value, 1);
	EXPECT_EQ(stream.GetState(), before);
	EXPECT_EQ(value, 7);
	EXPECT_EQ(&RandomChoice(stream, &value, 1), &value);
}

TEST(RandomAlgorithmsTests, ShuffleRetainsEveryElement)
{
	RandomStream stream(42, 54);
	int32 values[] = {0, 1, 2, 3};
	RandomShuffle(stream, values, 4);
	bool bSeen[] = {false, false, false, false};
	for (const int32 value : values)
	{
		ASSERT_GE(value, 0);
		ASSERT_LT(value, 4);
		EXPECT_FALSE(bSeen[value]);
		bSeen[value] = true;
	}
}
