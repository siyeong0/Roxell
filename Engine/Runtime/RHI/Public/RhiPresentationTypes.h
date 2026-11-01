#pragma once
#include <RhiFormat.h>
#include <RhiResourceTypes.h>
#include <RhiTypes.h>
#include <Span.hpp>
#include <Types.h>
namespace rox::rhi
{
	enum class ENativeWindowKind : core::uint8
	{
		Win32,
		X11,
		Wayland,
		Android
	};

	enum class EColorSpace : core::uint8
	{
		SrgbNonlinear,
		LinearSrgb,
		Hdr10PqRec2020
	};

	struct HdrMetadata final
	{
		core::float32 RedPrimary[2]{ 0.708f, 0.292f };
		core::float32 GreenPrimary[2]{ 0.170f, 0.797f };
		core::float32 BluePrimary[2]{ 0.131f, 0.046f };
		core::float32 WhitePoint[2]{ 0.3127f, 0.3290f };

		core::float32 MaximumLuminance = 1000.0f;
		core::float32 MinimumLuminance = 0.001f;

		core::float32 MaximumContentLightLevel = 1000.0f;
		core::float32 MaximumFrameAverageLightLevel = 400.0f;
	};

	struct AcquiredSwapChainImage final
	{
		SwapChainHandle SwapChain;
		TextureHandle Image;

		core::uint64 OwnerGeneration = 0;
		core::uint64 AcquireGeneration = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return SwapChain.IsValid() && Image.IsValid() && OwnerGeneration != 0 && AcquireGeneration != 0;
		}
	};

	enum class EAcquireStatus : core::uint8
	{
		Acquired,
		NotReady,
		RecreateRequired,
		Failed
	};

	struct AcquireOutcome final
	{
		EAcquireStatus Status = EAcquireStatus::Failed;
		AcquiredSwapChainImage Image;
	};

	enum class EPresentStatus : core::uint8
	{
		Presented,
		Busy,
		RecreateRequired,
		Failed
	};

	struct PresentOutcome final
	{
		EPresentStatus Status = EPresentStatus::Failed;
		SubmissionToken Token;
	};

	struct SwapChainDesc final
	{
		ENativeWindowKind WindowKind = ENativeWindowKind::Win32;
		core::uintptr Display = 0;
		core::uintptr Window = 0;

		core::uint32 Width = 0;
		core::uint32 Height = 0;

		core::uint32 BufferCount = 2;
		bool bVSync = true;

		EFormat Format = EFormat::RGBA8_UNORM;
		EColorSpace ColorSpace = EColorSpace::SrgbNonlinear;

		bool bEnableHdrMetadata = false;
		HdrMetadata Metadata;
	};

	struct FramebufferDesc final
	{
		core::Span<const TextureHandle> Colors;
		TextureHandle Depth;

		core::Span<const TextureSubresource> ColorSubresources;
		TextureSubresource DepthSubresource;

		TextureHandle ShadingRateImage;
	};
}
