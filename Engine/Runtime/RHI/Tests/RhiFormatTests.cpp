#include <RhiFormat.h>
#include <RhiResourceTypes.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	using namespace core;

	TEST(RhiFormat, EveryFormatHasMatchingMetadata)
	{
		for (uint32 index = 1; index < static_cast<uint32>(EFormat::COUNT); ++index)
		{
			const EFormat format = static_cast<EFormat>(index);
			const FormatInfo& info = GetFormatInfo(format);
			SCOPED_TRACE(index);
			EXPECT_EQ(info.Format, format);
			ASSERT_NE(info.Name, nullptr);
			EXPECT_NE(info.Name[0], '\0');
			EXPECT_GT(info.BytesPerBlock, 0);
			EXPECT_GT(info.BlockSize, 0);
		}
	}

	TEST(RhiFormat, InvalidMetadataQueryReturnsUnknown)
	{
		EXPECT_EQ(GetFormatInfo(EFormat::COUNT).Format, EFormat::UNKNOWN);
		EXPECT_EQ(GetFormatInfo(static_cast<EFormat>(0xffffffffu)).Format, EFormat::UNKNOWN);
	}

	TEST(RhiFormat, CompressedBlocksKeepTheirStorageSize)
	{
		EXPECT_EQ(GetFormatInfo(EFormat::BC1_UNORM).BytesPerBlock, 8);
		EXPECT_EQ(GetFormatInfo(EFormat::BC1_UNORM).BlockSize, 4);
		EXPECT_EQ(GetFormatInfo(EFormat::BC7_UNORM).BytesPerBlock, 16);
		EXPECT_EQ(GetFormatInfo(EFormat::BC7_UNORM).BlockSize, 4);
	}

	TEST(RhiFormat, CopiesPermitSrgbPairsButDoNotSwizzleChannels)
	{
		EXPECT_TRUE(AreTextureCopyFormatsCompatible(EFormat::RGBA8_UNORM, EFormat::SRGBA8_UNORM));
		EXPECT_TRUE(AreTextureCopyFormatsCompatible(EFormat::SRGBA8_UNORM, EFormat::RGBA8_UNORM));
		EXPECT_FALSE(AreTextureCopyFormatsCompatible(EFormat::RGBA8_UNORM, EFormat::BGRA8_UNORM));
		EXPECT_FALSE(AreTextureCopyFormatsCompatible(EFormat::UNKNOWN, EFormat::UNKNOWN));
		EXPECT_FALSE(AreTextureCopyFormatsCompatible(EFormat::COUNT, EFormat::COUNT));
	}

	TEST(RhiFormat, ViewsRequireTheSameStorageFamily)
	{
		EXPECT_TRUE(AreTextureViewFormatsCompatible(EFormat::BC7_UNORM, EFormat::BC7_UNORM_SRGB));
		EXPECT_FALSE(AreTextureViewFormatsCompatible(EFormat::BC1_UNORM, EFormat::BC7_UNORM));
		EXPECT_FALSE(AreTextureViewFormatsCompatible(EFormat::UNKNOWN, EFormat::RGBA8_UNORM));
	}

	TEST(RhiFormat, TransfersSelectTheRequestedDepthStencilPlane)
	{
		EXPECT_EQ(GetTextureTransferFormat(EFormat::D16, ETextureAspect::Depth), EFormat::R16_UNORM);
		EXPECT_EQ(GetTextureTransferFormat(EFormat::D24S8, ETextureAspect::Depth), EFormat::R32_UINT);
		EXPECT_EQ(GetTextureTransferFormat(EFormat::D32S8, ETextureAspect::Stencil), EFormat::R8_UINT);
		EXPECT_EQ(GetTextureTransferFormat(EFormat::D32, ETextureAspect::Default), EFormat::R32_FLOAT);
		EXPECT_EQ(GetTextureTransferFormat(EFormat::D32, ETextureAspect::Stencil), EFormat::UNKNOWN);
		EXPECT_EQ(GetTextureTransferFormat(EFormat::RGBA8_UNORM, ETextureAspect::Depth), EFormat::UNKNOWN);
		EXPECT_EQ(GetTextureTransferFormat(EFormat::D24S8, ETextureAspect::Color), EFormat::UNKNOWN);
	}
}
