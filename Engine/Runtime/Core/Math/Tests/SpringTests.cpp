#include <Spring.h>
#include <MathConstants.hpp>

#include <gtest/gtest.h>

#include <cmath>

using namespace rox::math;

TEST(SpringTests, ZeroTimePreservesStateAndZeroFrequencyIsBallistic)
{
	const SpringState<float32> initial{2.0f, 3.0f};
	const SpringState<float32> unchanged = SpringStep(initial, 9.0f, 1.0f, 1.0f, 0.0f);
	EXPECT_FLOAT_EQ(unchanged.Value, 2.0f);
	EXPECT_FLOAT_EQ(unchanged.Velocity, 3.0f);
	const SpringState<float32> ballistic = SpringStep(initial, 9.0f, 0.0f, 1.0f, 2.0f);
	EXPECT_FLOAT_EQ(ballistic.Value, 8.0f);
	EXPECT_FLOAT_EQ(ballistic.Velocity, 3.0f);
}

TEST(SpringTests, UndampedAndCriticalMotionMatchAnalyticSolutions)
{
	const SpringState<rox::core::float64> initial{1.0, 0.0};
	const SpringState<rox::core::float64> quarter = SpringStep(initial, 0.0, 1.0, 0.0, 0.25);
	EXPECT_NEAR(quarter.Value, 0.0, 1.0e-12);
	EXPECT_NEAR(quarter.Velocity, -2.0 * rox::core::PI<rox::core::float64>, 1.0e-12);
	const SpringState<rox::core::float64> critical = SpringStep(initial, 0.0, 1.0 / (2.0 * rox::core::PI<rox::core::float64>), 1.0, 1.0);
	EXPECT_NEAR(critical.Value, 2.0 / std::exp(1.0), 1.0e-12);
	EXPECT_NEAR(critical.Velocity, -1.0 / std::exp(1.0), 1.0e-12);
}

TEST(SpringTests, SplittingTimePreservesDampedMotion)
{
	const SpringState<rox::core::float64> initial{2.0, -0.5};
	for (const rox::core::float64 dampingRatio : {0.3, 1.0, 2.0})
	{
		const SpringState<rox::core::float64> whole = SpringStep(initial, 5.0, 0.8, dampingRatio, 0.75);
		const SpringState<rox::core::float64> first = SpringStep(initial, 5.0, 0.8, dampingRatio, 0.25);
		const SpringState<rox::core::float64> split = SpringStep(first, 5.0, 0.8, dampingRatio, 0.5);
		EXPECT_NEAR(split.Value, whole.Value, 1.0e-12);
		EXPECT_NEAR(split.Velocity, whole.Velocity, 1.0e-12);
	}
}

TEST(SpringTests, VectorOverloadsApplyMotionToEveryComponent)
{
	const SpringState<Vector2d> state2{{1, 2}, {0, 0}};
	const SpringState<Vector3d> state3{{1, 2, 3}, {0, 0, 0}};
	const SpringState<Vector4d> state4{{1, 2, 3, 4}, {0, 0, 0, 0}};
	const SpringState<Vector2d> result2 = SpringStep(state2, Vector2d::Zero(), 1.0, 0.0, 0.25);
	const SpringState<Vector3d> result3 = SpringStep(state3, Vector3d::Zero(), 1.0, 0.0, 0.25);
	const SpringState<Vector4d> result4 = SpringStep(state4, Vector4d::Zero(), 1.0, 0.0, 0.25);
	EXPECT_TRUE(result2.Value.NearlyEquals(Vector2d::Zero(), 1.0e-12));
	EXPECT_TRUE(result3.Value.NearlyEquals(Vector3d::Zero(), 1.0e-12));
	EXPECT_TRUE(result4.Value.NearlyEquals(Vector4d::Zero(), 1.0e-12));
	const rox::core::float64 omega = 2.0 * rox::core::PI<rox::core::float64>;
	EXPECT_TRUE(result2.Velocity.NearlyEquals(Vector2d{-omega, -2 * omega}, 1.0e-12));
	EXPECT_TRUE(result3.Velocity.NearlyEquals(Vector3d{-omega, -2 * omega, -3 * omega}, 1.0e-12));
	EXPECT_TRUE(result4.Velocity.NearlyEquals(Vector4d{-omega, -2 * omega, -3 * omega, -4 * omega}, 1.0e-12));
}

TEST(SpringTests, SmallTimeAndNearlyCriticalDampingRemainContinuous)
{
	const SpringState<rox::core::float64> initial{1.0, 2.0};
	const rox::core::float64 frequency = 1.0 / (2.0 * rox::core::PI<rox::core::float64>);
	const rox::core::float64 delta = 1.0e-4;
	const SpringState<rox::core::float64> small = SpringStep(initial, 0.0, frequency, 0.0, delta);
	EXPECT_NEAR(small.Value, std::cos(delta) + 2.0 * std::sin(delta), 1.0e-14);
	EXPECT_NEAR(small.Velocity, -std::sin(delta) + 2.0 * std::cos(delta), 1.0e-14);
	const SpringState<rox::core::float64> critical = SpringStep(initial, 0.0, frequency, 1.0, 0.5);
	for (const rox::core::float64 ratio : {1.0 - 1.0e-8, 1.0 + 1.0e-8})
	{
		const SpringState<rox::core::float64> nearby = SpringStep(initial, 0.0, frequency, ratio, 0.5);
		EXPECT_NEAR(nearby.Value, critical.Value, 1.0e-7);
		EXPECT_NEAR(nearby.Velocity, critical.Velocity, 1.0e-7);
	}
}
