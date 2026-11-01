#pragma once
#include "DeviceCommands.h"
#include <NumericLimits.hpp>
#include <Result.hpp>
#include <RhiCommandTypes.h>
namespace rox::rhi
{
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	extern thread_local bool g_bFailNextAcceptedSubmit;
	extern thread_local bool g_bFailNextSubmissionReservation;
	extern thread_local core::uint32 g_PreparedFailureCountdown;
	extern thread_local bool g_bRejectPreparedFailureBeforeAcceptance;
#endif
	extern thread_local bool g_bPreparedErrors;
	inline constexpr core::uint32 KNOWN_USAGE_BITS = (1u << 14) - 1u;
	inline constexpr core::uint32 TEXTURE_COPY_BITS = (1u << 2) - 1u;
	inline constexpr core::uint32 TEXTURE_CLEAR_BITS =
		TEXTURE_COPY_BITS | static_cast<core::uint32>(ETextureUsage::RenderTarget) | static_cast<core::uint32>(ETextureUsage::ShaderRead) |
		static_cast<core::uint32>(ETextureUsage::DepthStencil) | static_cast<core::uint32>(ETextureUsage::Storage) |
		static_cast<core::uint32>(ETextureUsage::ShadingRateSource);
	inline constexpr core::uint32 KNOWN_TEXTURE_USAGE_BITS = (1u << 7) - 1u;

	[[nodiscard]] SubmissionToken MakeSubmissionToken(core::uint64 owner, EQueue queue, core::uint64 serial) noexcept;

	[[nodiscard]] bool IsStorageColorFormat(EFormat format) noexcept;
	[[nodiscard]] bool IsFloatColorTargetFormat(EFormat format) noexcept;
	[[nodiscard]] bool ValidAttachmentClear(EFormat format, const ClearColor& color) noexcept;
	[[nodiscard]] bool IsColorTargetFormat(EFormat format) noexcept;
	[[nodiscard]] bool IsIntegerStorageFormat(EFormat format) noexcept;
	[[nodiscard]] constexpr bool IsDepthFormat(EFormat format) noexcept
	{
		return format == EFormat::D16 || format == EFormat::D24S8 || format == EFormat::D32 || format == EFormat::D32S8;
	}

	[[nodiscard]] bool Fits(core::uint64 size, core::uint64 offset, core::uint64 count) noexcept;
	[[nodiscard]] core::uint32 MipExtent(core::uint32 extent, core::uint32 level) noexcept;

	[[nodiscard]] core::uint64 SubresourceBytes(const TextureDesc& desc, core::uint32 level, ETextureAspect aspect = ETextureAspect::Default) noexcept;

	[[nodiscard]] bool ValidTextureUploadData(EFormat format, ETextureAspect aspect, core::Span<const core::Byte> data) noexcept;


	[[nodiscard]] constexpr ETrackedResourceState BufferUseState(EComputeBindingType type) noexcept
	{
		return type == EComputeBindingType::UniformBuffer ? ETrackedResourceState::Uniform
			: type == EComputeBindingType::ReadOnlyBuffer ? ETrackedResourceState::ShaderRead
			: ETrackedResourceState::Storage;
	}

	[[nodiscard]] constexpr ETrackedResourceState TextureUseState(ETextureState state) noexcept
	{
		switch (state)
		{
		case ETextureState::CopySource:
			return ETrackedResourceState::CopySource;
		case ETextureState::CopyDestination:
			return ETrackedResourceState::CopyDestination;
		case ETextureState::ResolveSource:
			return ETrackedResourceState::ResolveSource;
		case ETextureState::ResolveDestination:
			return ETrackedResourceState::ResolveDestination;
		case ETextureState::ShaderRead:
			return ETrackedResourceState::ShaderRead;
		case ETextureState::Storage:
			return ETrackedResourceState::Storage;
		case ETextureState::RenderTarget:
			return ETrackedResourceState::RenderTarget;
		case ETextureState::Present:
			return ETrackedResourceState::Present;
		case ETextureState::DepthWrite:
			return ETrackedResourceState::DepthWrite;
		case ETextureState::DepthRead:
			return ETrackedResourceState::DepthRead;
		case ETextureState::ShadingRateSource:
			return ETrackedResourceState::ShadingRateSource;

		default:
		{
			break;
		}
		}

		return ETrackedResourceState::Unknown;
	}

	[[nodiscard]] constexpr ETrackedResourceState BufferUseState(EGraphicsBufferBindingType type) noexcept
	{
		return type == EGraphicsBufferBindingType::UniformBuffer ? ETrackedResourceState::Uniform
			: type == EGraphicsBufferBindingType::ReadOnlyBuffer ? ETrackedResourceState::ShaderRead
			: ETrackedResourceState::Storage;
	}

	[[nodiscard]] constexpr ETrackedResourceState BufferUseState(EDescriptorType type) noexcept
	{
		return type == EDescriptorType::UniformBuffer ? ETrackedResourceState::Uniform
			: type == EDescriptorType::ReadOnlyBuffer ? ETrackedResourceState::ShaderRead
			: ETrackedResourceState::Storage;
	}

	[[nodiscard]] bool IsValidSampler(const SamplerDesc& sampler) noexcept;
	[[nodiscard]] bool IsValidSpecializations(core::Span<const ShaderSpecialization> specializations) noexcept;

	template <typename TSlot> void Advance(TSlot& slot) noexcept
	{
		if (slot.Generation == core::NumericLimits<core::uint32>::max())
		{
			slot.bExhausted = true;
		}
		else
		{
			++slot.Generation;
		}
	}
}
