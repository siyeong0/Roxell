#pragma once
#include <NumericLimits.hpp>
#include <RhiFormat.h>
#include <RhiTypes.h>
#include <Span.hpp>
#include <Types.h>
namespace rox::rhi
{
	enum class EBufferUsage : core::uint32
	{
		None = 0,
		CopySource = 1 << 0,
		CopyDestination = 1 << 1,
		Vertex = 1 << 2,
		Index = 1 << 3,
		Uniform = 1 << 4,
		Storage = 1 << 5,
		Indirect = 1 << 6,
		DeviceAddress = 1 << 7,
		AccelBuildInput = 1 << 8,
		AccelStorage = 1 << 9,
		MicromapBuildInput = 1 << 10,
		MicromapStorage = 1 << 11,
		TypedReadOnly = 1 << 12,
		TypedStorage = 1 << 13
	};

	[[nodiscard]] constexpr EBufferUsage operator|(EBufferUsage left, EBufferUsage right) noexcept
	{
		return static_cast<EBufferUsage>(static_cast<core::uint32>(left) | static_cast<core::uint32>(right));
	}

	[[nodiscard]] constexpr bool HasUsage(EBufferUsage usages, EBufferUsage usage) noexcept
	{
		return (static_cast<core::uint32>(usages) & static_cast<core::uint32>(usage)) != 0;
	}

	enum class EBufferMemory : core::uint8
	{
		Device,
		CpuWrite,
		CpuRead
	};

	enum class EBufferState : core::uint8
	{
		CopySource,
		CopyDestination,
		Uniform,
		Storage,
		ReadOnlyStorage,
		Vertex,
		Index,
		VertexAndIndex,
		Indirect,
		AccelBuildInput,
		AccelStorage,
		AccelScratch,
		VertexAndReadOnlyStorage
	};

	struct BufferDesc final
	{
		core::uint64 ByteCount = 0;
		EBufferUsage Usage = EBufferUsage::None;
		EBufferMemory Memory = EBufferMemory::Device;
	};

	struct MemoryRequirements final
	{
		core::uint64 ByteCount = 0;
		core::uint64 AlignmentBytes = 0;

		bool bDedicatedRequired = false;
		bool bDedicatedPreferred = false;

		// Match MemoryBudget::HeapIndex for standalone allocation and first placement in a new heap.
		// An existing heap keeps its selected memory type. UINT32_MAX means no budget group is known.
		core::uint32 BudgetHeapIndex = UINT32_MAX;
		core::uint32 PlacedBudgetHeapIndex = UINT32_MAX;
	};

	enum class EMemoryBudgetAccuracy : core::uint8
	{
		DriverEstimate
	};

	// Native heap indices are backend-specific. Estimates can change between calls.
	struct MemoryBudget final
	{
		core::uint32 HeapIndex = 0;
		bool bDeviceLocal = false;
		EMemoryBudgetAccuracy Accuracy = EMemoryBudgetAccuracy::DriverEstimate;

		core::uint64 Budget = 0;
		core::uint64 Usage = 0;
	};

	enum class EHeapUsage : core::uint8
	{
		Buffers,
		Textures,
		Attachments
	};

	struct HeapDesc final
	{
		core::uint64 CapacityBytes = 0;
		EBufferMemory Memory = EBufferMemory::Device;
		EHeapUsage Usage = EHeapUsage::Buffers;
	};

	enum class ETextureUsage : core::uint32
	{
		None = 0,
		CopySource = 1 << 0,
		CopyDestination = 1 << 1,
		ShaderRead = 1 << 2,
		RenderTarget = 1 << 3,
		DepthStencil = 1 << 4,
		Storage = 1 << 5,
		ShadingRateSource = 1 << 6
	};

	[[nodiscard]] constexpr ETextureUsage operator|(ETextureUsage left, ETextureUsage right) noexcept
	{
		return static_cast<ETextureUsage>(static_cast<core::uint32>(left) | static_cast<core::uint32>(right));
	}

	[[nodiscard]] constexpr bool HasUsage(ETextureUsage usages, ETextureUsage usage) noexcept
	{
		return (static_cast<core::uint32>(usages) & static_cast<core::uint32>(usage)) != 0;
	}

	enum class ETextureDimension : core::uint8
	{
		Texture2D,
		Cube,
		Texture3D,
		Texture1D
	};

	struct TextureDesc final
	{
		core::uint32 Width = 0;
		core::uint32 Height = 0;

		EFormat Format = EFormat::UNKNOWN;
		ETextureUsage Usage = ETextureUsage::None;

		core::uint32 MipLevels = 1;
		core::uint32 ArrayLayers = 1;

		core::uint32 SampleCount = 1;

		ETextureDimension Dimension = ETextureDimension::Texture2D;
		core::uint32 Depth = 1;

		bool bSparse = false;
		bool bMutableFormat = false;
	};

	struct TextureTileShape final
	{
		core::uint32 WidthTexels = 0;
		core::uint32 HeightTexels = 0;
		core::uint32 DepthTexels = 0;
	};

	struct TextureSubresourceTiling final
	{
		core::uint32 WidthTiles = 0;
		core::uint32 HeightTiles = 0;
		core::uint32 DepthTiles = 0;

		core::uint32 FirstTile = 0;
	};

	struct TextureTiling final
	{
		core::uint32 TileBytes = 0;
		core::uint32 TileCount = 0;
		core::uint32 SubresourceCount = 0;

		core::uint32 StandardMipCount = 0;

		core::uint32 PackedMipCount = 0;
		core::uint32 PackedTileCount = 0;
		core::uint32 FirstPackedTile = 0;
		bool bSingleMipTail = false;

		TextureTileShape Shape;
	};

	enum class ESamplerFeedbackFormat : core::uint8
	{
		MinMip,
		MipRegionUsed
	};

	enum class ESamplerFeedbackState : core::uint8
	{
		Storage,
		ResolveSource,
		ResolveDestination,
		CopySource,
		CopyDestination
	};

	struct SamplerFeedbackDesc final
	{
		ESamplerFeedbackFormat Format = ESamplerFeedbackFormat::MinMip;
		core::uint32 RegionWidth = 8;
		core::uint32 RegionHeight = 8;
	};

	struct TextureSubresource final
	{
		core::uint32 MipLevel = 0;
		core::uint32 ArrayLayer = 0;
		ETextureAspect Aspect = ETextureAspect::Default;
	};

	enum class ETextureState : core::uint8
	{
		CopySource,
		CopyDestination,
		ShaderRead,
		Storage,
		RenderTarget,
		DepthWrite,
		ShadingRateSource,
		DepthRead,
		ResolveSource,
		ResolveDestination,
		Present
	};

	enum class ETrackedResourceState : core::uint32
	{
		Unknown = 0,
		Common = 1u << 0,
		CopySource = 1u << 1,
		CopyDestination = 1u << 2,
		ShaderRead = 1u << 3,
		Storage = 1u << 4,
		RenderTarget = 1u << 5,
		DepthWrite = 1u << 6,
		DepthRead = 1u << 7,
		ResolveSource = 1u << 8,
		ResolveDestination = 1u << 9,
		Present = 1u << 10,
		Uniform = 1u << 11,
		Vertex = 1u << 12,
		Index = 1u << 13,
		Indirect = 1u << 14,
		ShadingRateSource = 1u << 15,
		AccelBuildInput = 1u << 16,
		AccelStorage = 1u << 17
	};

	[[nodiscard]] constexpr ETrackedResourceState operator|(ETrackedResourceState left, ETrackedResourceState right) noexcept
	{
		return static_cast<ETrackedResourceState>(static_cast<core::uint32>(left) | static_cast<core::uint32>(right));
	}

	[[nodiscard]] constexpr bool HasState(ETrackedResourceState states, ETrackedResourceState state) noexcept
	{
		return state != ETrackedResourceState::Unknown && (static_cast<core::uint32>(states) & static_cast<core::uint32>(state)) == static_cast<core::uint32>(state);
	}

	struct TextureTileMapping final
	{
		TextureSubresource Subresource;

		core::uint32 X = 0;
		core::uint32 Y = 0;
		core::uint32 Z = 0;

		HeapHandle Heap;
		core::uint64 HeapByteOffset = 0;
	};

	enum class ETextureViewType : core::uint8
	{
		Sampled,
		Storage
	};

	enum class ETextureViewDimension : core::uint8
	{
		Texture2D,
		Texture2DArray,
		Cube,
		CubeArray,
		Texture3D,
		Texture1D,
		Texture1DArray
	};

	struct TextureViewDesc final
	{
		TextureHandle Texture;

		ETextureViewType Type = ETextureViewType::Sampled;
		ETextureViewDimension Dimension = ETextureViewDimension::Texture2D;

		core::uint32 BaseMipLevel = 0;
		core::uint32 MipLevelCount = 1;

		core::uint32 BaseArrayLayer = 0;
		core::uint32 ArrayLayerCount = 1;

		core::uint32 BaseDepthSlice = 0;
		core::uint32 DepthSliceCount = 0;

		EFormat Format = EFormat::UNKNOWN;
		ETextureAspect Aspect = ETextureAspect::Default;
	};

	struct TextureBufferFootprint final
	{
		core::uint64 ByteCount = 0;
		core::uint32 RowPitchBytes = 0;
		core::uint32 RowCount = 0;

		core::uint32 Width = 0;
		core::uint32 Height = 0;
		core::uint32 Depth = 1;
	};

	struct StagingTextureMapping final
	{
		core::Span<core::Byte> Bytes;
		TextureBufferFootprint Footprint;
	};

	enum class ESamplerFilter : core::uint8
	{
		Nearest,
		Linear
	};

	enum class ESamplerReduction : core::uint8
	{
		Standard,
		Minimum,
		Maximum
	};

	enum class ESamplerAddressMode : core::uint8
	{
		Clamp,
		Repeat,
		Mirror,
		Border
	};

	enum class ESamplerBorderColor : core::uint8
	{
		TransparentBlack,
		OpaqueBlack,
		OpaqueWhite
	};

	enum class ECompareOp : core::uint8
	{
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always
	};

	struct SamplerDesc final
	{
		ESamplerFilter MinFilter = ESamplerFilter::Linear;
		ESamplerFilter MagFilter = ESamplerFilter::Linear;
		ESamplerFilter MipFilter = ESamplerFilter::Linear;

		core::float32 MaxAnisotropy = 1.0f;
		core::float32 MipBias = 0.0f;

		ESamplerAddressMode AddressU = ESamplerAddressMode::Clamp;
		ESamplerAddressMode AddressV = ESamplerAddressMode::Clamp;
		ESamplerAddressMode AddressW = ESamplerAddressMode::Clamp;

		bool bCompareEnabled = false;
		ECompareOp Compare = ECompareOp::Less;

		ESamplerReduction Reduction = ESamplerReduction::Standard;

		ESamplerBorderColor BorderColor = ESamplerBorderColor::TransparentBlack;

		// Finite, nonnegative mip bounds; the default maximum leaves the mip chain unrestricted.
		core::float32 MinLod = 0.0f;
		core::float32 MaxLod = core::NumericLimits<core::float32>::max();
	};

	struct BufferRange final
	{
		core::uint64 ByteOffset = 0;
		core::uint64 ByteCount = 0;
	};

	enum class EBufferViewType : core::uint8
	{
		ReadOnly,
		Storage
	};

	struct BufferViewDesc final
	{
		BufferHandle Buffer;
		EBufferViewType Type = EBufferViewType::ReadOnly;
		EFormat Format = EFormat::UNKNOWN;

		BufferRange Range;
	};
}
