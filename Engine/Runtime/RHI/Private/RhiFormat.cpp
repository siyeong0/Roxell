#include <RhiFormat.h>

/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <Types.h>
#include <Assertion.h>

#include <Utility.hpp>

namespace rox::rhi
{
	using namespace core;

	// Format mapping table. The rows must be in the exactly same order as Format enum members are defined.
	static const FormatInfo c_FormatInfo[] = {
		// Format                      Name                 BytesPerBlock BlockSize Kind                       Red    Green  Blue   Alpha  Depth  Stencil Signed SRGB
		{  EFormat::UNKNOWN,           "UNKNOWN",           0,            0,        EFormatKind::Integer,      false, false, false, false, false, false,  false, false},
		{  EFormat::R8_UINT,           "R8_UINT",           1,            1,        EFormatKind::Integer,      true,  false, false, false, false, false,  false, false},
		{  EFormat::R8_SINT,           "R8_SINT",           1,            1,        EFormatKind::Integer,      true,  false, false, false, false, false,  true,  false},
		{  EFormat::R8_UNORM,          "R8_UNORM",          1,            1,        EFormatKind::Normalized,   true,  false, false, false, false, false,  false, false},
		{  EFormat::R8_SNORM,          "R8_SNORM",          1,            1,        EFormatKind::Normalized,   true,  false, false, false, false, false,  true,  false},
		{  EFormat::RG8_UINT,          "RG8_UINT",          2,            1,        EFormatKind::Integer,      true,  true,  false, false, false, false,  false, false},
		{  EFormat::RG8_SINT,          "RG8_SINT",          2,            1,        EFormatKind::Integer,      true,  true,  false, false, false, false,  true,  false},
		{  EFormat::RG8_UNORM,         "RG8_UNORM",         2,            1,        EFormatKind::Normalized,   true,  true,  false, false, false, false,  false, false},
		{  EFormat::RG8_SNORM,         "RG8_SNORM",         2,            1,        EFormatKind::Normalized,   true,  true,  false, false, false, false,  true,  false},
		{  EFormat::R16_UINT,          "R16_UINT",          2,            1,        EFormatKind::Integer,      true,  false, false, false, false, false,  false, false},
		{  EFormat::R16_SINT,          "R16_SINT",          2,            1,        EFormatKind::Integer,      true,  false, false, false, false, false,  true,  false},
		{  EFormat::R16_UNORM,         "R16_UNORM",         2,            1,        EFormatKind::Normalized,   true,  false, false, false, false, false,  false, false},
		{  EFormat::R16_SNORM,         "R16_SNORM",         2,            1,        EFormatKind::Normalized,   true,  false, false, false, false, false,  true,  false},
		{  EFormat::R16_FLOAT,         "R16_FLOAT",         2,            1,        EFormatKind::Float,        true,  false, false, false, false, false,  true,  false},
		{  EFormat::BGRA4_UNORM,       "BGRA4_UNORM",       2,            1,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::B5G6R5_UNORM,      "B5G6R5_UNORM",      2,            1,        EFormatKind::Normalized,   true,  true,  true,  false, false, false,  false, false},
		{  EFormat::B5G5R5A1_UNORM,    "B5G5R5A1_UNORM",    2,            1,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::RGBA8_UINT,        "RGBA8_UINT",        4,            1,        EFormatKind::Integer,      true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::RGBA8_SINT,        "RGBA8_SINT",        4,            1,        EFormatKind::Integer,      true,  true,  true,  true,  false, false,  true,  false},
		{  EFormat::RGBA8_UNORM,       "RGBA8_UNORM",       4,            1,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::RGBA8_SNORM,       "RGBA8_SNORM",       4,            1,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  true,  false},
		{  EFormat::BGRA8_UNORM,       "BGRA8_UNORM",       4,            1,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::BGRX8_UNORM,       "BGRX8_UNORM",       4,            1,        EFormatKind::Normalized,   true,  true,  true,  false, false, false,  false, false},
		{  EFormat::SRGBA8_UNORM,      "SRGBA8_UNORM",      4,            1,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, true},
		{  EFormat::SBGRA8_UNORM,      "SBGRA8_UNORM",      4,            1,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, true},
		{  EFormat::SBGRX8_UNORM,      "SBGRX8_UNORM",      4,            1,        EFormatKind::Normalized,   true,  true,  true,  false, false, false,  false, true},
		{  EFormat::R10G10B10A2_UNORM, "R10G10B10A2_UNORM", 4,            1,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::R11G11B10_FLOAT,   "R11G11B10_FLOAT",   4,            1,        EFormatKind::Float,        true,  true,  true,  false, false, false,  false, false},
		{  EFormat::RG16_UINT,         "RG16_UINT",         4,            1,        EFormatKind::Integer,      true,  true,  false, false, false, false,  false, false},
		{  EFormat::RG16_SINT,         "RG16_SINT",         4,            1,        EFormatKind::Integer,      true,  true,  false, false, false, false,  true,  false},
		{  EFormat::RG16_UNORM,        "RG16_UNORM",        4,            1,        EFormatKind::Normalized,   true,  true,  false, false, false, false,  false, false},
		{  EFormat::RG16_SNORM,        "RG16_SNORM",        4,            1,        EFormatKind::Normalized,   true,  true,  false, false, false, false,  true,  false},
		{  EFormat::RG16_FLOAT,        "RG16_FLOAT",        4,            1,        EFormatKind::Float,        true,  true,  false, false, false, false,  true,  false},
		{  EFormat::R32_UINT,          "R32_UINT",          4,            1,        EFormatKind::Integer,      true,  false, false, false, false, false,  false, false},
		{  EFormat::R32_SINT,          "R32_SINT",          4,            1,        EFormatKind::Integer,      true,  false, false, false, false, false,  true,  false},
		{  EFormat::R32_FLOAT,         "R32_FLOAT",         4,            1,        EFormatKind::Float,        true,  false, false, false, false, false,  true,  false},
		{  EFormat::RGBA16_UINT,       "RGBA16_UINT",       8,            1,        EFormatKind::Integer,      true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::RGBA16_SINT,       "RGBA16_SINT",       8,            1,        EFormatKind::Integer,      true,  true,  true,  true,  false, false,  true,  false},
		{  EFormat::RGBA16_FLOAT,      "RGBA16_FLOAT",      8,            1,        EFormatKind::Float,        true,  true,  true,  true,  false, false,  true,  false},
		{  EFormat::RGBA16_UNORM,      "RGBA16_UNORM",      8,            1,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::RGBA16_SNORM,      "RGBA16_SNORM",      8,            1,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  true,  false},
		{  EFormat::RG32_UINT,         "RG32_UINT",         8,            1,        EFormatKind::Integer,      true,  true,  false, false, false, false,  false, false},
		{  EFormat::RG32_SINT,         "RG32_SINT",         8,            1,        EFormatKind::Integer,      true,  true,  false, false, false, false,  true,  false},
		{  EFormat::RG32_FLOAT,        "RG32_FLOAT",        8,            1,        EFormatKind::Float,        true,  true,  false, false, false, false,  true,  false},
		{  EFormat::RGB32_UINT,        "RGB32_UINT",        12,           1,        EFormatKind::Integer,      true,  true,  true,  false, false, false,  false, false},
		{  EFormat::RGB32_SINT,        "RGB32_SINT",        12,           1,        EFormatKind::Integer,      true,  true,  true,  false, false, false,  true,  false},
		{  EFormat::RGB32_FLOAT,       "RGB32_FLOAT",       12,           1,        EFormatKind::Float,        true,  true,  true,  false, false, false,  true,  false},
		{  EFormat::RGBA32_UINT,       "RGBA32_UINT",       16,           1,        EFormatKind::Integer,      true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::RGBA32_SINT,       "RGBA32_SINT",       16,           1,        EFormatKind::Integer,      true,  true,  true,  true,  false, false,  true,  false},
		{  EFormat::RGBA32_FLOAT,      "RGBA32_FLOAT",      16,           1,        EFormatKind::Float,        true,  true,  true,  true,  false, false,  true,  false},
		{  EFormat::D16,               "D16",               2,            1,        EFormatKind::DepthStencil, false, false, false, false, true,  false,  false, false},
		{  EFormat::D24S8,             "D24S8",             4,            1,        EFormatKind::DepthStencil, false, false, false, false, true,  true,   false, false},
		{  EFormat::X24G8_UINT,        "X24G8_UINT",        4,            1,        EFormatKind::Integer,      false, false, false, false, false, true,   false, false},
		{  EFormat::D32,               "D32",               4,            1,        EFormatKind::DepthStencil, false, false, false, false, true,  false,  false, false},
		{  EFormat::D32S8,             "D32S8",             8,            1,        EFormatKind::DepthStencil, false, false, false, false, true,  true,   false, false},
		{  EFormat::X32G8_UINT,        "X32G8_UINT",        8,            1,        EFormatKind::Integer,      false, false, false, false, false, true,   false, false},
		{  EFormat::BC1_UNORM,         "BC1_UNORM",         8,            4,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::BC1_UNORM_SRGB,    "BC1_UNORM_SRGB",    8,            4,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, true},
		{  EFormat::BC2_UNORM,         "BC2_UNORM",         16,           4,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::BC2_UNORM_SRGB,    "BC2_UNORM_SRGB",    16,           4,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, true},
		{  EFormat::BC3_UNORM,         "BC3_UNORM",         16,           4,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::BC3_UNORM_SRGB,    "BC3_UNORM_SRGB",    16,           4,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, true},
		{  EFormat::BC4_UNORM,         "BC4_UNORM",         8,            4,        EFormatKind::Normalized,   true,  false, false, false, false, false,  false, false},
		{  EFormat::BC4_SNORM,         "BC4_SNORM",         8,            4,        EFormatKind::Normalized,   true,  false, false, false, false, false,  true,  false},
		{  EFormat::BC5_UNORM,         "BC5_UNORM",         16,           4,        EFormatKind::Normalized,   true,  true,  false, false, false, false,  false, false},
		{  EFormat::BC5_SNORM,         "BC5_SNORM",         16,           4,        EFormatKind::Normalized,   true,  true,  false, false, false, false,  true,  false},
		{  EFormat::BC6H_UFLOAT,       "BC6H_UFLOAT",       16,           4,        EFormatKind::Float,        true,  true,  true,  false, false, false,  false, false},
		{  EFormat::BC6H_SFLOAT,       "BC6H_SFLOAT",       16,           4,        EFormatKind::Float,        true,  true,  true,  false, false, false,  true,  false},
		{  EFormat::BC7_UNORM,         "BC7_UNORM",         16,           4,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, false},
		{  EFormat::BC7_UNORM_SRGB,    "BC7_UNORM_SRGB",    16,           4,        EFormatKind::Normalized,   true,  true,  true,  true,  false, false,  false, true},
	};

	namespace
	{
		[[nodiscard]] EFormat TextureViewFamily(EFormat format) noexcept
		{
			switch (format)
			{
			case EFormat::R8_UINT:
			case EFormat::R8_SINT:
			case EFormat::R8_UNORM:
			case EFormat::R8_SNORM:
				return EFormat::R8_UINT;
			case EFormat::RG8_UINT:
			case EFormat::RG8_SINT:
			case EFormat::RG8_UNORM:
			case EFormat::RG8_SNORM:
				return EFormat::RG8_UINT;
			case EFormat::R16_UINT:
			case EFormat::R16_SINT:
			case EFormat::R16_UNORM:
			case EFormat::R16_SNORM:
			case EFormat::R16_FLOAT:
				return EFormat::R16_UINT;
			case EFormat::RGBA8_UINT:
			case EFormat::RGBA8_SINT:
			case EFormat::RGBA8_UNORM:
			case EFormat::RGBA8_SNORM:
			case EFormat::SRGBA8_UNORM:
				return EFormat::RGBA8_UINT;
			case EFormat::BGRA8_UNORM:
			case EFormat::SBGRA8_UNORM:
				return EFormat::BGRA8_UNORM;
			case EFormat::BGRX8_UNORM:
			case EFormat::SBGRX8_UNORM:
				return EFormat::BGRX8_UNORM;
			case EFormat::RG16_UINT:
			case EFormat::RG16_SINT:
			case EFormat::RG16_UNORM:
			case EFormat::RG16_SNORM:
			case EFormat::RG16_FLOAT:
				return EFormat::RG16_UINT;
			case EFormat::R32_UINT:
			case EFormat::R32_SINT:
			case EFormat::R32_FLOAT:
				return EFormat::R32_UINT;
			case EFormat::RGBA16_UINT:
			case EFormat::RGBA16_SINT:
			case EFormat::RGBA16_UNORM:
			case EFormat::RGBA16_SNORM:
			case EFormat::RGBA16_FLOAT:
				return EFormat::RGBA16_UINT;
			case EFormat::RG32_UINT:
			case EFormat::RG32_SINT:
			case EFormat::RG32_FLOAT:
				return EFormat::RG32_UINT;
			case EFormat::RGB32_UINT:
			case EFormat::RGB32_SINT:
			case EFormat::RGB32_FLOAT:
				return EFormat::RGB32_UINT;
			case EFormat::RGBA32_UINT:
			case EFormat::RGBA32_SINT:
			case EFormat::RGBA32_FLOAT:
				return EFormat::RGBA32_UINT;
			case EFormat::BC1_UNORM:
			case EFormat::BC1_UNORM_SRGB:
				return EFormat::BC1_UNORM;
			case EFormat::BC2_UNORM:
			case EFormat::BC2_UNORM_SRGB:
				return EFormat::BC2_UNORM;
			case EFormat::BC3_UNORM:
			case EFormat::BC3_UNORM_SRGB:
				return EFormat::BC3_UNORM;
			case EFormat::BC4_UNORM:
			case EFormat::BC4_SNORM:
				return EFormat::BC4_UNORM;
			case EFormat::BC5_UNORM:
			case EFormat::BC5_SNORM:
				return EFormat::BC5_UNORM;
			case EFormat::BC6H_UFLOAT:
			case EFormat::BC6H_SFLOAT:
				return EFormat::BC6H_UFLOAT;
			case EFormat::BC7_UNORM:
			case EFormat::BC7_UNORM_SRGB:
				return EFormat::BC7_UNORM;
			default:
				return format;
			}
		}
	}

	const FormatInfo& GetFormatInfo(EFormat format)
	{
		static_assert(sizeof(c_FormatInfo) / sizeof(FormatInfo) == usize(EFormat::COUNT), "The format info table doesn't have the right number of elements");

		if (uint32(format) >= uint32(EFormat::COUNT))
		{
			return c_FormatInfo[0]; // UNKNOWN
		}

		const FormatInfo& info = c_FormatInfo[uint32(format)];
		ASSERT(info.Format == format, "FormatInfo table is not in the same order as Format enum");
		return info;
	}

	EFormat GetTextureTransferFormat(EFormat format, ETextureAspect aspect) noexcept
	{
		if (format == EFormat::UNKNOWN || format >= EFormat::COUNT || aspect > ETextureAspect::Stencil)
		{
			return EFormat::UNKNOWN;
		}

		const FormatInfo& info = GetFormatInfo(format);

		if (aspect == ETextureAspect::Default)
		{
			aspect = info.bHasDepth ? ETextureAspect::Depth : ETextureAspect::Color;
		}

		if (aspect == ETextureAspect::Color)
		{
			return info.bHasDepth || info.bHasStencil ? EFormat::UNKNOWN : format;
		}

		if (aspect == ETextureAspect::Stencil)
		{
			return info.bHasStencil ? EFormat::R8_UINT : EFormat::UNKNOWN;
		}

		if (!info.bHasDepth)
		{
			return EFormat::UNKNOWN;
		}

		return format == EFormat::D16 ? EFormat::R16_UNORM
			: format == EFormat::D24S8 ? EFormat::R32_UINT : EFormat::R32_FLOAT;
	}

	bool AreTextureCopyFormatsCompatible(EFormat source, EFormat destination) noexcept
	{
		if (source == destination)
		{
			return source > EFormat::UNKNOWN && source < EFormat::COUNT;
		}
		return ((source == EFormat::RGBA8_UNORM || source == EFormat::SRGBA8_UNORM) && (destination == EFormat::RGBA8_UNORM || destination == EFormat::SRGBA8_UNORM)) ||
			((source == EFormat::BGRA8_UNORM || source == EFormat::SBGRA8_UNORM) && (destination == EFormat::BGRA8_UNORM || destination == EFormat::SBGRA8_UNORM));
	}

	bool AreTextureViewFormatsCompatible(EFormat source, EFormat view) noexcept
	{
		return source > EFormat::UNKNOWN && source < EFormat::COUNT && view > EFormat::UNKNOWN && view < EFormat::COUNT && TextureViewFamily(source) == TextureViewFamily(view);
	}
} // namespace rox::rhi
