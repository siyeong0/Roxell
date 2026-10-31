#include <RandomNoise.h>

#include <gtest/gtest.h>

using namespace rox::core;
using namespace rox::math;

TEST(RandomNoiseTests, CounterAndNoiseAreDeterministicAndBounded)
{
	const uint64 expected = RandomCounterU64(42, 7, 3);
	for (uint64 counter = 0; counter < 128; ++counter)
	{
		const float32 sample = RandomCounterFloat01<float32>(42, counter, 3);
		EXPECT_GE(sample, 0.0f);
		EXPECT_LT(sample, 1.0f);
	}
	EXPECT_EQ(RandomCounterU64(42, 7, 3), expected);
	const Vector2f point2{-1.25f, 4.5f};
	const Vector3f point3{-1.25f, 4.5f, 0.75f};
	const float32 noise2 = ValueNoise2D(point2, 42);
	const float32 noise3 = ValueNoise3D(point3, 42);
	EXPECT_GE(noise2, 0.0f);
	EXPECT_LE(noise2, 1.0f);
	EXPECT_GE(noise3, 0.0f);
	EXPECT_LE(noise3, 1.0f);
	EXPECT_FLOAT_EQ(ValueNoise2D(point2, 42), noise2);
	EXPECT_FLOAT_EQ(ValueNoise3D(point3, 42), noise3);
}
