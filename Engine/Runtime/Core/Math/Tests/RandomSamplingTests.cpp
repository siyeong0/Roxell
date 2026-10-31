#include <RandomSampling.h>

#include <gtest/gtest.h>
#include <cmath>
#include <limits>

using namespace rox::core;
using namespace rox::math;

TEST(RandomSamplingTests, SamplesRespectGeometricAndWeightBounds)
{
	RandomStream stream(42, 54);
	constexpr float32 WEIGHTS[] = {0.0f, 2.0f, 0.0f};
	for (uint32 index = 0; index < 128; ++index)
	{
		EXPECT_EQ(RandomWeightedIndex(stream, WEIGHTS, 3), 1);
		EXPECT_NEAR(RandomUnitCircle<float32>(stream).Length(), 1.0f, 1.0e-5f);
		EXPECT_LE(RandomInUnitDisk<float32>(stream).Length(), 1.00001f);
		EXPECT_NEAR(RandomUnitSphere<float32>(stream).Length(), 1.0f, 1.0e-5f);
		EXPECT_LE(RandomInUnitSphere<float32>(stream).Length(), 1.00001f);
		EXPECT_GE(RandomExponential<float32>(stream), 0.0f);
	}
}

TEST(RandomSamplingTests, ConeSamplesStayWithinAngleAndRepeatForSameSeed)
{
	RandomStream first(123, 456);
	RandomStream second(123, 456);
	for (uint32 index = 0; index < 128; ++index)
	{
		const Vector3d sample = RandomCone<float64>(first, Vector3d::UnitZ(), 0.5);
		EXPECT_EQ(sample, RandomCone<float64>(second, Vector3d::UnitZ(), 0.5));
		EXPECT_NEAR(sample.Length(), 1.0, 1.0e-12);
		EXPECT_GE(sample.Z, std::cos(0.5) - 1.0e-12);
	}
	EXPECT_TRUE(RandomCone<float64>(first, Vector3d::UnitZ(), 0.0).NearlyEquals(Vector3d::UnitZ()));
}

TEST(RandomSamplingTests, NormalAndExponentialSamplesAreFiniteAndRepeatable)
{
	RandomStream first(9, 10);
	RandomStream second(9, 10);
	for (uint32 index = 0; index < 128; ++index)
	{
		const float64 normal = RandomNormal<float64>(first);
		EXPECT_TRUE(std::isfinite(normal));
		EXPECT_DOUBLE_EQ(normal, RandomNormal<float64>(second));
		const float64 exponential = RandomExponential<float64>(first);
		EXPECT_TRUE(std::isfinite(exponential));
		EXPECT_GE(exponential, 0.0);
		EXPECT_DOUBLE_EQ(exponential, RandomExponential<float64>(second));
	}
}

TEST(RandomSamplingTests, WeightedSelectionSkipsZeroWeightsAndHandlesLargeWeights)
{
	RandomStream stream(42, 54);
	const float64 maximum = std::numeric_limits<float64>::max();
	const float64 weights[] = {0.0, maximum / 3.0, maximum, 0.0};
	uint32 firstCount = 0;
	uint32 secondCount = 0;
	for (uint32 index = 0; index < 4096; ++index)
	{
		const usize selected = RandomWeightedIndex(stream, weights, 4);
		ASSERT_TRUE(selected == 1 || selected == 2);
		firstCount += selected == 1 ? 1 : 0;
		secondCount += selected == 2 ? 1 : 0;
	}
	EXPECT_GT(firstCount, 800u);
	EXPECT_LT(firstCount, 1250u);
	EXPECT_EQ(firstCount + secondCount, 4096u);
}
