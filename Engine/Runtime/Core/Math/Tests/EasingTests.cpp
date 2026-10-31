#include <Easing.h>

#include <gtest/gtest.h>

using namespace rox::core;
using namespace rox::math;

TEST(EasingTests, EveryCurveAndModePreservesEndpoints)
{
	constexpr EEasingCurve CURVES[] =
	{
		EEasingCurve::Linear, EEasingCurve::Quadratic, EEasingCurve::Cubic, EEasingCurve::Quartic,
		EEasingCurve::Quintic, EEasingCurve::Sine, EEasingCurve::Exponential, EEasingCurve::Circular,
		EEasingCurve::Back, EEasingCurve::Elastic, EEasingCurve::Bounce
	};
	constexpr EEasingMode MODES[] = {EEasingMode::In, EEasingMode::Out, EEasingMode::InOut};
	for (const EEasingCurve curve : CURVES)
	{
		for (const EEasingMode mode : MODES)
		{
			EXPECT_FLOAT_EQ(Ease<float32>(0.0f, curve, mode), 0.0f);
			EXPECT_FLOAT_EQ(Ease<float32>(1.0f, curve, mode), 1.0f);
		}
	}
	EXPECT_FLOAT_EQ(Ease<float32>(0.5f, EEasingCurve::Quadratic, EEasingMode::In), 0.25f);
	EXPECT_FLOAT_EQ(Ease<float32>(0.5f, EEasingCurve::Quadratic, EEasingMode::Out), 0.75f);
}

TEST(EasingTests, ModesReflectEveryCurveAndPreserveOvershoot)
{
	constexpr EEasingCurve CURVES[] =
	{
		EEasingCurve::Linear, EEasingCurve::Quadratic, EEasingCurve::Cubic, EEasingCurve::Quartic,
		EEasingCurve::Quintic, EEasingCurve::Sine, EEasingCurve::Exponential, EEasingCurve::Circular,
		EEasingCurve::Back, EEasingCurve::Elastic, EEasingCurve::Bounce
	};
	for (const EEasingCurve curve : CURVES)
	{
		const float64 in = Ease(0.4, curve, EEasingMode::In);
		EXPECT_NEAR(Ease(0.6, curve, EEasingMode::Out), 1.0 - in, 1.0e-12);
		EXPECT_NEAR(Ease(0.2, curve, EEasingMode::InOut), 0.5 * in, 1.0e-12);
		EXPECT_NEAR(Ease(0.8, curve, EEasingMode::InOut), 1.0 - 0.5 * in, 1.0e-12);
		EXPECT_NEAR(Ease(0.5, curve, EEasingMode::InOut), 0.5, 1.0e-12);
	}
	EXPECT_LT(Ease(0.25, EEasingCurve::Back, EEasingMode::In), 0.0);
	EXPECT_GT(Ease(0.75, EEasingCurve::Back, EEasingMode::Out), 1.0);
	EXPECT_LT(Ease(0.2, EEasingCurve::Elastic, EEasingMode::In), 0.0);
}

TEST(EasingTests, RepresentativeCurvesMatchIndependentInteriorValues)
{
	EXPECT_DOUBLE_EQ(Ease(0.5, EEasingCurve::Cubic, EEasingMode::In), 0.125);
	EXPECT_DOUBLE_EQ(Ease(0.5, EEasingCurve::Quartic, EEasingMode::In), 0.0625);
	EXPECT_NEAR(Ease(0.5, EEasingCurve::Exponential, EEasingMode::In), 31.0 / 1023.0, 1.0e-12);
	EXPECT_NEAR(Ease(0.6, EEasingCurve::Circular, EEasingMode::In), 0.2, 1.0e-12);
	EXPECT_NEAR(Ease(0.5, EEasingCurve::Bounce, EEasingMode::Out), 0.765625, 1.0e-12);
}
