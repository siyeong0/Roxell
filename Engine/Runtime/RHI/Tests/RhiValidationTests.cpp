#include "DeviceValidation.h"
#include <gtest/gtest.h>

namespace rox::rhi
{
	using namespace core;

	TEST(RhiValidation, RangesDoNotWrapAtTheIntegerLimit)
	{
		constexpr uint64 MAXIMUM = NumericLimits<uint64>::max();
		EXPECT_TRUE(Fits(16, 12, 4));
		EXPECT_FALSE(Fits(16, 13, 4));
		EXPECT_FALSE(Fits(16, 16, 0));
		EXPECT_FALSE(Fits(16, MAXIMUM, 4));
		EXPECT_FALSE(Fits(MAXIMUM, MAXIMUM - 1, 4));
		EXPECT_TRUE(Fits(MAXIMUM, MAXIMUM - 1, 1));
	}

	TEST(RhiValidation, PartialCompressedBlocksAndMipTailsHaveNonzeroStorage)
	{
		const TextureDesc compressed{ 5, 7, EFormat::BC1_UNORM, ETextureUsage::CopySource, 3 };
		EXPECT_EQ(SubresourceBytes(compressed, 0), 32u);
		EXPECT_EQ(SubresourceBytes(compressed, 1), 8u);
		EXPECT_EQ(SubresourceBytes(compressed, 2), 8u);
		EXPECT_EQ(MipExtent(7, 2), 1u);
		EXPECT_EQ(MipExtent(1, 8), 1u);
		TextureDesc volume{ 4, 4, EFormat::RGBA8_UNORM, ETextureUsage::CopySource, 3 };
		volume.Dimension = ETextureDimension::Texture3D;
		volume.Depth = 4;
		EXPECT_EQ(SubresourceBytes(volume, 0), 256u);
		EXPECT_EQ(SubresourceBytes(volume, 1), 32u);
		EXPECT_EQ(SubresourceBytes(volume, 2), 4u);
	}

	TEST(RhiValidation, DepthAndStencilTransfersUseIndependentPlaneWidths)
	{
		const TextureDesc desc{ 2, 2, EFormat::D32S8, ETextureUsage::CopySource };
		EXPECT_EQ(SubresourceBytes(desc, 0, ETextureAspect::Depth), 16u);
		EXPECT_EQ(SubresourceBytes(desc, 0, ETextureAspect::Stencil), 4u);
		EXPECT_EQ(SubresourceBytes(desc, 0, ETextureAspect::Color), 0u);
	}

	TEST(RhiValidation, ClearValuesRespectNumericRangeAndRepresentation)
	{
		EXPECT_TRUE(ValidAttachmentClear(EFormat::R8_UINT, ClearColor::Uint(255, 0, 0, 0)));
		EXPECT_FALSE(ValidAttachmentClear(EFormat::R8_UINT, ClearColor::Uint(256, 0, 0, 0)));
		EXPECT_FALSE(ValidAttachmentClear(EFormat::R8_UINT, ClearColor::Float(1, 0, 0, 0)));
		EXPECT_TRUE(ValidAttachmentClear(EFormat::R8_SINT, ClearColor::Sint(-128, 0, 0, 0)));
		EXPECT_FALSE(ValidAttachmentClear(EFormat::R8_SINT, ClearColor::Sint(-129, 0, 0, 0)));
		EXPECT_TRUE(ValidAttachmentClear(EFormat::RGBA16_FLOAT, ClearColor::Float(65504, 0, 0, 0)));
		EXPECT_FALSE(ValidAttachmentClear(EFormat::RGBA16_FLOAT, ClearColor::Float(65505, 0, 0, 0)));
		EXPECT_FALSE(ValidAttachmentClear(EFormat::RGBA8_UNORM, ClearColor::Float(-0.1f, 0, 0, 0)));
		EXPECT_FALSE(ValidAttachmentClear(EFormat::RGBA32_FLOAT, ClearColor::Float(NumericLimits<float32>::infinity(), 0, 0, 0)));
		EXPECT_FALSE(ValidAttachmentClear(EFormat::RGBA32_FLOAT, ClearColor::Float(NumericLimits<float32>::quiet_NaN(), 0, 0, 0)));
	}

	TEST(RhiValidation, DepthUploadsRejectNanOutOfRangeAndPackedStencilBits)
	{
		float32 depth = 0.5f;
		const Span<const Byte> bytes{ reinterpret_cast<const Byte*>(&depth), sizeof(depth) };
		EXPECT_TRUE(ValidTextureUploadData(EFormat::D32, ETextureAspect::Depth, bytes));
		depth = -0.1f;
		EXPECT_FALSE(ValidTextureUploadData(EFormat::D32, ETextureAspect::Depth, bytes));
		depth = 1.1f;
		EXPECT_FALSE(ValidTextureUploadData(EFormat::D32, ETextureAspect::Depth, bytes));
		depth = NumericLimits<float32>::quiet_NaN();
		EXPECT_FALSE(ValidTextureUploadData(EFormat::D32, ETextureAspect::Depth, bytes));
		uint32 packed = 0x00ffffffu;
		const Span<const Byte> packedBytes{ reinterpret_cast<const Byte*>(&packed), sizeof(packed) };
		EXPECT_TRUE(ValidTextureUploadData(EFormat::D24S8, ETextureAspect::Depth, packedBytes));
		packed = 0xffffffffu;
		EXPECT_FALSE(ValidTextureUploadData(EFormat::D24S8, ETextureAspect::Depth, packedBytes));
	}

	class SamplerValidationTest : public testing::TestWithParam<uint32> {};

	TEST_P(SamplerValidationTest, InvalidSamplingAndLodCombinationsAreRejected)
	{
		SamplerDesc desc;
		ASSERT_TRUE(IsValidSampler(desc));
		switch (GetParam())
		{
		case 0: desc.MinLod = -1; break;
		case 1: desc.MinLod = 2; desc.MaxLod = 1; break;
		case 2: desc.MaxLod = NumericLimits<float32>::infinity(); break;
		case 3: desc.MinLod = NumericLimits<float32>::quiet_NaN(); break;
		case 4: desc.MaxAnisotropy = 0; break;
		case 5: desc.MaxAnisotropy = 17; break;
		case 6: desc.MipBias = NumericLimits<float32>::infinity(); break;
		case 7: desc.MinFilter = static_cast<ESamplerFilter>(255); break;
		case 8: desc.AddressU = static_cast<ESamplerAddressMode>(255); break;
		case 9: desc.bCompareEnabled = true; desc.Reduction = ESamplerReduction::Minimum; break;
		}
		EXPECT_FALSE(IsValidSampler(desc));
	}

	INSTANTIATE_TEST_SUITE_P(InvalidDescriptors, SamplerValidationTest, testing::Range(0u, 10u));
}
