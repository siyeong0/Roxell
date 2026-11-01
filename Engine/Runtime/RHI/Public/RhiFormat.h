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
#pragma once
#include <Types.h>
namespace rox::rhi
{
	enum class ETextureAspect : core::uint8
	{
		Default,
		Color,
		Depth,
		Stencil
	};

	enum class EFormat : core::uint8
	{
		UNKNOWN,

		R8_UINT,
		R8_SINT,
		R8_UNORM,
		R8_SNORM,
		RG8_UINT,
		RG8_SINT,
		RG8_UNORM,
		RG8_SNORM,
		R16_UINT,
		R16_SINT,
		R16_UNORM,
		R16_SNORM,
		R16_FLOAT,
		BGRA4_UNORM,
		B5G6R5_UNORM,
		B5G5R5A1_UNORM,
		RGBA8_UINT,
		RGBA8_SINT,
		RGBA8_UNORM,
		RGBA8_SNORM,
		BGRA8_UNORM,
		BGRX8_UNORM,
		SRGBA8_UNORM,
		SBGRA8_UNORM,
		SBGRX8_UNORM,
		R10G10B10A2_UNORM,
		R11G11B10_FLOAT,
		RG16_UINT,
		RG16_SINT,
		RG16_UNORM,
		RG16_SNORM,
		RG16_FLOAT,
		R32_UINT,
		R32_SINT,
		R32_FLOAT,
		RGBA16_UINT,
		RGBA16_SINT,
		RGBA16_FLOAT,
		RGBA16_UNORM,
		RGBA16_SNORM,
		RG32_UINT,
		RG32_SINT,
		RG32_FLOAT,
		RGB32_UINT,
		RGB32_SINT,
		RGB32_FLOAT,
		RGBA32_UINT,
		RGBA32_SINT,
		RGBA32_FLOAT,

		D16,
		D24S8,
		X24G8_UINT,
		D32,
		D32S8,
		X32G8_UINT,

		BC1_UNORM,
		BC1_UNORM_SRGB,
		BC2_UNORM,
		BC2_UNORM_SRGB,
		BC3_UNORM,
		BC3_UNORM_SRGB,
		BC4_UNORM,
		BC4_SNORM,
		BC5_UNORM,
		BC5_SNORM,
		BC6H_UFLOAT,
		BC6H_SFLOAT,
		BC7_UNORM,
		BC7_UNORM_SRGB,

		COUNT,
	};

	enum class EFormatKind : core::uint8
	{
		Integer,
		Normalized,
		Float,
		DepthStencil
	};

	enum class EFormatSupport : core::uint32
	{
		None = 0,
		Buffer = 0x00000001,
		IndexBuffer = 0x00000002,
		VertexBuffer = 0x00000004,
		Texture = 0x00000008,
		DepthStencil = 0x00000010,
		RenderTarget = 0x00000020,
		Blendable = 0x00000040,
		ShaderLoad = 0x00000080,
		ShaderSample = 0x00000100,
		ShaderUavLoad = 0x00000200,
		ShaderUavStore = 0x00000400,
		ShaderAtomic = 0x00000800,
		SamplerMinMax = 0x00001000,
		BufferShaderRead = 0x00002000,
		BufferShaderLoad = 0x00004000,
		BufferShaderStore = 0x00008000
	};

	[[nodiscard]] constexpr EFormatSupport operator|(EFormatSupport left, EFormatSupport right) noexcept
	{
		return static_cast<EFormatSupport>(static_cast<core::uint32>(left) | static_cast<core::uint32>(right));
	}

	[[nodiscard]] constexpr bool HasFormatSupport(EFormatSupport support, EFormatSupport required) noexcept
	{
		return (static_cast<core::uint32>(support) & static_cast<core::uint32>(required)) == static_cast<core::uint32>(required);
	}

	struct FormatInfo
	{
		EFormat Format;
		const char* Name;

		core::uint8 BytesPerBlock;
		core::uint8 BlockSize;

		EFormatKind Kind;
		bool bHasRed : 1;
		bool bHasGreen : 1;
		bool bHasBlue : 1;
		bool bHasAlpha : 1;
		bool bHasDepth : 1;
		bool bHasStencil : 1;
		bool bIsSigned : 1;
		bool bIsSRGB : 1;
	};

	const FormatInfo& GetFormatInfo(EFormat format);
	[[nodiscard]] EFormat GetTextureTransferFormat(EFormat format, ETextureAspect aspect) noexcept;

	// Copies preserve bytes. RGBA8 and BGRA8 permit their UNORM/SRGB pair, without color conversion.
	[[nodiscard]] bool AreTextureCopyFormatsCompatible(EFormat source, EFormat destination) noexcept;
	[[nodiscard]] bool AreTextureViewFormatsCompatible(EFormat source, EFormat view) noexcept;
}
