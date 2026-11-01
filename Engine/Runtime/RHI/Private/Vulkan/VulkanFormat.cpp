#include "VulkanFormat.h"
/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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

#include <Vocabulary.hpp>
#include <Types.h>
#include <Assertion.h>

namespace rox::rhi
{
	using namespace core;

	namespace
	{
		struct FormatMapping final
		{
			EFormat Format;
			VkFormat Native;
		};

		constexpr StaticArray<FormatMapping, static_cast<usize>(EFormat::COUNT)> FORMAT_MAPPINGS{ {
			{EFormat::UNKNOWN, VK_FORMAT_UNDEFINED},
			{EFormat::R8_UINT, VK_FORMAT_R8_UINT},
			{EFormat::R8_SINT, VK_FORMAT_R8_SINT},
			{EFormat::R8_UNORM, VK_FORMAT_R8_UNORM},
			{EFormat::R8_SNORM, VK_FORMAT_R8_SNORM},
			{EFormat::RG8_UINT, VK_FORMAT_R8G8_UINT},
			{EFormat::RG8_SINT, VK_FORMAT_R8G8_SINT},
			{EFormat::RG8_UNORM, VK_FORMAT_R8G8_UNORM},
			{EFormat::RG8_SNORM, VK_FORMAT_R8G8_SNORM},
			{EFormat::R16_UINT, VK_FORMAT_R16_UINT},
			{EFormat::R16_SINT, VK_FORMAT_R16_SINT},
			{EFormat::R16_UNORM, VK_FORMAT_R16_UNORM},
			{EFormat::R16_SNORM, VK_FORMAT_R16_SNORM},
			{EFormat::R16_FLOAT, VK_FORMAT_R16_SFLOAT},
			{EFormat::BGRA4_UNORM, VK_FORMAT_A4R4G4B4_UNORM_PACK16},
			{EFormat::B5G6R5_UNORM, VK_FORMAT_R5G6B5_UNORM_PACK16},
			{EFormat::B5G5R5A1_UNORM, VK_FORMAT_A1R5G5B5_UNORM_PACK16},
			{EFormat::RGBA8_UINT, VK_FORMAT_R8G8B8A8_UINT},
			{EFormat::RGBA8_SINT, VK_FORMAT_R8G8B8A8_SINT},
			{EFormat::RGBA8_UNORM, VK_FORMAT_R8G8B8A8_UNORM},
			{EFormat::RGBA8_SNORM, VK_FORMAT_R8G8B8A8_SNORM},
			{EFormat::BGRA8_UNORM, VK_FORMAT_B8G8R8A8_UNORM},
			{EFormat::BGRX8_UNORM, VK_FORMAT_UNDEFINED},
			{EFormat::SRGBA8_UNORM, VK_FORMAT_R8G8B8A8_SRGB},
			{EFormat::SBGRA8_UNORM, VK_FORMAT_B8G8R8A8_SRGB},
			{EFormat::SBGRX8_UNORM, VK_FORMAT_UNDEFINED},
			{EFormat::R10G10B10A2_UNORM, VK_FORMAT_A2B10G10R10_UNORM_PACK32},
			{EFormat::R11G11B10_FLOAT, VK_FORMAT_B10G11R11_UFLOAT_PACK32},
			{EFormat::RG16_UINT, VK_FORMAT_R16G16_UINT},
			{EFormat::RG16_SINT, VK_FORMAT_R16G16_SINT},
			{EFormat::RG16_UNORM, VK_FORMAT_R16G16_UNORM},
			{EFormat::RG16_SNORM, VK_FORMAT_R16G16_SNORM},
			{EFormat::RG16_FLOAT, VK_FORMAT_R16G16_SFLOAT},
			{EFormat::R32_UINT, VK_FORMAT_R32_UINT},
			{EFormat::R32_SINT, VK_FORMAT_R32_SINT},
			{EFormat::R32_FLOAT, VK_FORMAT_R32_SFLOAT},
			{EFormat::RGBA16_UINT, VK_FORMAT_R16G16B16A16_UINT},
			{EFormat::RGBA16_SINT, VK_FORMAT_R16G16B16A16_SINT},
			{EFormat::RGBA16_FLOAT, VK_FORMAT_R16G16B16A16_SFLOAT},
			{EFormat::RGBA16_UNORM, VK_FORMAT_R16G16B16A16_UNORM},
			{EFormat::RGBA16_SNORM, VK_FORMAT_R16G16B16A16_SNORM},
			{EFormat::RG32_UINT, VK_FORMAT_R32G32_UINT},
			{EFormat::RG32_SINT, VK_FORMAT_R32G32_SINT},
			{EFormat::RG32_FLOAT, VK_FORMAT_R32G32_SFLOAT},
			{EFormat::RGB32_UINT, VK_FORMAT_R32G32B32_UINT},
			{EFormat::RGB32_SINT, VK_FORMAT_R32G32B32_SINT},
			{EFormat::RGB32_FLOAT, VK_FORMAT_R32G32B32_SFLOAT},
			{EFormat::RGBA32_UINT, VK_FORMAT_R32G32B32A32_UINT},
			{EFormat::RGBA32_SINT, VK_FORMAT_R32G32B32A32_SINT},
			{EFormat::RGBA32_FLOAT, VK_FORMAT_R32G32B32A32_SFLOAT},
			{EFormat::D16, VK_FORMAT_D16_UNORM},
			{EFormat::D24S8, VK_FORMAT_D24_UNORM_S8_UINT},
			{EFormat::X24G8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
			{EFormat::D32, VK_FORMAT_D32_SFLOAT},
			{EFormat::D32S8, VK_FORMAT_D32_SFLOAT_S8_UINT},
			{EFormat::X32G8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT},
			{EFormat::BC1_UNORM, VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
			{EFormat::BC1_UNORM_SRGB, VK_FORMAT_BC1_RGBA_SRGB_BLOCK},
			{EFormat::BC2_UNORM, VK_FORMAT_BC2_UNORM_BLOCK},
			{EFormat::BC2_UNORM_SRGB, VK_FORMAT_BC2_SRGB_BLOCK},
			{EFormat::BC3_UNORM, VK_FORMAT_BC3_UNORM_BLOCK},
			{EFormat::BC3_UNORM_SRGB, VK_FORMAT_BC3_SRGB_BLOCK},
			{EFormat::BC4_UNORM, VK_FORMAT_BC4_UNORM_BLOCK},
			{EFormat::BC4_SNORM, VK_FORMAT_BC4_SNORM_BLOCK},
			{EFormat::BC5_UNORM, VK_FORMAT_BC5_UNORM_BLOCK},
			{EFormat::BC5_SNORM, VK_FORMAT_BC5_SNORM_BLOCK},
			{EFormat::BC6H_UFLOAT, VK_FORMAT_BC6H_UFLOAT_BLOCK},
			{EFormat::BC6H_SFLOAT, VK_FORMAT_BC6H_SFLOAT_BLOCK},
			{EFormat::BC7_UNORM, VK_FORMAT_BC7_UNORM_BLOCK},
			{EFormat::BC7_UNORM_SRGB, VK_FORMAT_BC7_SRGB_BLOCK},
		} };
	} // namespace

	VkFormat ConvertVulkanFormat(EFormat format) noexcept
	{
		ASSERT(format < EFormat::COUNT, "The format must be valid.");
		const FormatMapping& mapping = FORMAT_MAPPINGS[static_cast<usize>(format)];
		ASSERT(mapping.Format == format, "The Vulkan format table must match EFormat order.");
		return mapping.Native;
	}
}
