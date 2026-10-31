#include <Color.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace rox::core;
using namespace rox::math;

TEST(ColorTests, ColorEncodingAndClamping)
{
	constexpr Color32 ENCODED{0x12, 0x34, 0x56, 0x78};
	ASSERT_EQ(ENCODED.PackRGBA(), 0x78563412u);
	ASSERT_EQ(ENCODED.PackBGRA(), 0x78123456u);
	ASSERT_EQ(Color32::FromRGBA(0x78563412u), ENCODED);
	ASSERT_EQ(Color32::FromBGRA(0x78123456u), ENCODED);
	const LinearColor<> hdr{-2.0f, 3.0f, 0.5f, 0.5f};
	ASSERT_EQ(hdr.ToLinearUNorm(), (Color32{0, 255, 128, 128}));
	ASSERT_EQ(hdr.ToSRGB(), (Color32{0, 255, 188, 128}));
	ASSERT_TRUE(std::abs(SRGBToLinear<float32>(0.5f) - 0.21404114f) < 1.0e-6f);
	ASSERT_TRUE(std::abs(LinearToSRGB<float32>(0.5f) - 0.73535698f) < 1.0e-6f);
	ASSERT_EQ(LinearColor<>::FromHSV({-0.5f, 1, 1}, 0.25f), (LinearColor<>{0, 1, 1, 0.25f}));
	ASSERT_EQ((LinearColor<>{4, 4, 4, 1}.ToHSV()), (Vector3f{0, 0, 4}));
	ASSERT_FALSE((LinearColor<>{0, 0, 0, std::numeric_limits<float32>::infinity()}.IsFinite()));
}

TEST(ColorTests, EncodedColorsRoundTripAndAlphaRemainsLinear)
{
	for (uint32 value = 0; value <= 255; ++value)
	{
		const uint8 channel = static_cast<uint8>(value);
		const Color32 color{channel, channel, channel, channel};
		EXPECT_EQ(LinearColor<float64>::FromSRGB(color).ToSRGB(), color);
		EXPECT_EQ(LinearColor<float64>::FromLinearUNorm(color).ToLinearUNorm(), color);
	}
	EXPECT_NEAR(SRGBToLinear(0.04045), 0.04045 / 12.92, 1.0e-14);
	EXPECT_NEAR(LinearToSRGB(0.0031308), 0.0031308 * 12.92, 1.0e-14);
}

TEST(ColorTests, HsvAndLinearOperations)
{
	const LinearColor<float64> colors[] = {{1.0, 0.0, 0.0, 0.25}, {0.0, 1.0, 0.0, 0.5},
	                                       {0.0, 0.0, 1.0, 0.75}, {0.2, 0.6, 0.9, 1.0}};
	for (const auto& color : colors)
	{
		const auto restored = LinearColor<float64>::FromHSV(color.ToHSV(), color.A);
		EXPECT_NEAR(restored.R, color.R, 1.0e-12);
		EXPECT_NEAR(restored.G, color.G, 1.0e-12);
		EXPECT_NEAR(restored.B, color.B, 1.0e-12);
		EXPECT_DOUBLE_EQ(restored.A, color.A);
	}
	const LinearColor<float64> color{0.2, 0.4, 0.6, 0.5};
	EXPECT_EQ(color.Premultiplied(), (LinearColor<float64>{0.1, 0.2, 0.3, 0.5}));
	EXPECT_NEAR(color.Luminance(), 0.37192, 1.0e-12);
	const auto middle = LinearColor<float64>::Lerp({0.0, 0.0, 0.0, 0.0}, {1.0, 0.5, 0.25, 1.0}, 0.5);
	EXPECT_EQ(middle, (LinearColor<float64>{0.5, 0.25, 0.125, 0.5}));
}
