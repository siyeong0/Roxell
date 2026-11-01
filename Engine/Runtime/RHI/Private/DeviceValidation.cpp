#include "DeviceValidation.h"
#include "DeviceResources.h"
#include "DevicePipelines.h"

#include <Assertion.h>
#include <Numeric.hpp>

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] SubmissionToken MakeSubmissionToken(uint64 owner, EQueue queue, uint64 serial) noexcept
	{
		ASSERT(serial != 0 && serial <= MAXIMUM_SUBMISSION_SERIAL, "Backend returned an invalid submission serial.");
		return { owner, queue, (serial << 2) | static_cast<uint64>(queue) };
	}

	[[nodiscard]] bool IsStorageColorFormat(EFormat format) noexcept
	{
		const FormatInfo& info = GetFormatInfo(format);
		return info.BlockSize == 1 && info.BytesPerBlock != 0 && !info.bIsSRGB && (info.Kind == EFormatKind::Normalized || info.Kind == EFormatKind::Float);
	}

	[[nodiscard]] bool IsFloatColorTargetFormat(EFormat format) noexcept
	{
		const FormatInfo& info = GetFormatInfo(format);
		return info.BlockSize == 1 && info.BytesPerBlock != 0 && (info.Kind == EFormatKind::Normalized || info.Kind == EFormatKind::Float);
	}

	[[nodiscard]] bool ValidAttachmentClear(EFormat format, const ClearColor& color) noexcept
	{
		const FormatInfo& info = GetFormatInfo(format);
		if (info.Kind != EFormatKind::Integer)
		{
			if (color.GetType() != EClearColorType::Float)
			{
				return false;
			}
			const float values[]{ color.GetFloat()[0], color.GetFloat()[1], color.GetFloat()[2], color.GetFloat()[3] };
			const bool bHalf = format == EFormat::R16_FLOAT || format == EFormat::RG16_FLOAT || format == EFormat::RGBA16_FLOAT;
			for (const float value : values)
			{
				if (!IsFinite(value) ||
					(bHalf && (value < -65504.0f || value> 65504.0f)) ||
					(info.Kind == EFormatKind::Normalized && (value < (info.bIsSigned ? -1.0f : 0.0f) || value> 1.0f)))
				{
					return false;
				}
			}

			return true;
		}
		if (color.GetType() != (info.bIsSigned ? EClearColorType::Sint : EClearColorType::Uint))
		{
			return false;
		}
		const uint32 channels = uint32(info.bHasRed) + info.bHasGreen + info.bHasBlue + info.bHasAlpha;
		const uint32 bits = channels != 0 ? info.BytesPerBlock * 8u / channels : 0;
		if (bits == 0)
		{
			return false;
		}
		if (bits >= 32)
		{
			return true;
		}
		for (uint32 channel = 0; channel < channels; ++channel)
		{
			if (info.bIsSigned
				? (color.GetSint()[channel] < -(int32{ 1 } << (bits - 1)) || color.GetSint()[channel] >= (int32{ 1 } << (bits - 1)))
				: color.GetUint()[channel] >= (uint32{ 1 } << bits))
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] bool IsColorTargetFormat(EFormat format) noexcept
	{
		const FormatInfo& info = GetFormatInfo(format);
		return info.BlockSize == 1 && info.BytesPerBlock != 0 && !info.bHasDepth && !info.bHasStencil;
	}

	[[nodiscard]] bool IsIntegerStorageFormat(EFormat format) noexcept
	{
		const FormatInfo& info = GetFormatInfo(format);
		return info.Kind == EFormatKind::Integer && IsColorTargetFormat(format);
	}

	[[nodiscard]] bool Fits(uint64 size, uint64 offset, uint64 count) noexcept
	{
		return count != 0 && offset <= size && count <= size - offset;
	}

	[[nodiscard]] uint32 MipExtent(uint32 extent, uint32 level) noexcept
	{
		const uint32 reduced = extent >> level;
		return reduced == 0 ? 1 : reduced;
	}

	[[nodiscard]] uint64 SubresourceBytes(const TextureDesc& desc, uint32 level, ETextureAspect aspect) noexcept
	{
		const EFormat transferFormat = GetTextureTransferFormat(desc.Format, aspect);

		if (transferFormat == EFormat::UNKNOWN)
		{
			return 0;
		}

		const FormatInfo& format = GetFormatInfo(transferFormat);
		const uint64 blocksWide = (static_cast<uint64>(MipExtent(desc.Width, level)) + format.BlockSize - 1) / format.BlockSize;
		const uint64 blocksHigh = (static_cast<uint64>(MipExtent(desc.Height, level)) + format.BlockSize - 1) / format.BlockSize;

		return blocksWide * blocksHigh * format.BytesPerBlock * MipExtent(desc.Depth, level);
	}

	[[nodiscard]] bool ValidTextureUploadData(EFormat format, ETextureAspect aspect, Span<const Byte> data) noexcept
	{
		if (data.Data() == nullptr)
		{
			return false;
		}

		if (!IsDepthFormat(format) || aspect == ETextureAspect::Stencil)
		{
			return true;
		}

		if (format == EFormat::D16)
		{
			return true;
		}

		for (usize offset = 0; offset < data.Size(); offset += 4)
		{
			if (format == EFormat::D24S8)
			{
				uint32 value = 0;
				CopyBytes(&value, data.Data() + offset, sizeof(value));

				if ((value & 0xff000000u) != 0)
				{
					return false;
				}
			}
			else
			{
				float value = 0;
				CopyBytes(&value, data.Data() + offset, sizeof(value));

				if (!(value >= 0.0f && value <= 1.0f))
				{
					return false;
				}
			}
		}

		return true;
	}

	[[nodiscard]] bool IsValidSampler(const SamplerDesc& sampler) noexcept
	{
		return sampler.MinFilter <= ESamplerFilter::Linear &&
			sampler.MagFilter <= ESamplerFilter::Linear &&
			sampler.MipFilter <= ESamplerFilter::Linear &&
			IsFinite(sampler.MaxAnisotropy) &&
			sampler.MaxAnisotropy >= 1.0f &&
			sampler.MaxAnisotropy <= 16.0f &&
			IsFinite(sampler.MinLod) &&
			IsFinite(sampler.MaxLod) &&
			sampler.MinLod >= 0.0f &&
			sampler.MaxLod >= sampler.MinLod &&
			IsFinite(sampler.MipBias) &&
			sampler.AddressU <= ESamplerAddressMode::Border &&
			sampler.AddressV <= ESamplerAddressMode::Border &&
			sampler.AddressW <= ESamplerAddressMode::Border &&
			sampler.BorderColor <= ESamplerBorderColor::OpaqueWhite &&
			sampler.Compare <= ECompareOp::Always &&
			sampler.Reduction <= ESamplerReduction::Maximum &&
			(!sampler.bCompareEnabled || sampler.Reduction == ESamplerReduction::Standard);
	}

	[[nodiscard]] bool IsValidSpecializations(Span<const ShaderSpecialization> specializations) noexcept
	{
		if (specializations.Size() > 64)
		{
			return false;
		}

		for (usize index = 0; index < specializations.Size(); ++index)
		{
			for (usize previous = 0; previous < index; ++previous)
			{
				if (specializations[previous].ConstantID == specializations[index].ConstantID)
				{
					return false;
				}
			}
		}

		return true;
	}
}
