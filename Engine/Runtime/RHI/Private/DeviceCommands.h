#pragma once

#include "PreparedBindingCache.h"

#include <MemoryBlock.h>
#include <RhiInterop.h>
#include <NumericLimits.hpp>
#include <RhiBindingTypes.h>
#include <RhiCommandPreparation.h>
#include <RhiCommandTypes.h>
#include <RhiCoopVecTypes.h>
#include <RhiPresentationTypes.h>
#include <RhiRayTracingTypes.h>
#include <SharedPtr.hpp>

namespace rox::rhi
{
	enum class EDepthLoadOp : core::uint8
	{
		Clear,
		Load
	};

	struct DepthAttachmentOptions final
	{
		EDepthLoadOp LoadOp = EDepthLoadOp::Clear;
		float ClearValue = 1.0f;
		core::uint8 ClearStencil = 0;
		bool bReadOnly = false;

		EDepthLoadOp StencilLoadOp = EDepthLoadOp::Load;
		bool bReadOnlyStencil = false;
	};

	struct NativeBindingProgram;

	struct NativeExternalMemoryView final
	{
		ExternalHandleView Handle;
		GpuIdentity Device;

		core::uint64 AllocationBytes = 0;
		core::uint32 MemoryTypeIndex = 0;
		bool bDedicated = true;
	};

	struct NativeExternalMemory final
	{
		ExternalHandle Handle;
		GpuIdentity Device;

		core::uint64 AllocationBytes = 0;
		core::uint32 MemoryTypeIndex = 0;
		bool bDedicated = true;
	};

	constexpr core::uint64 MAXIMUM_SUBMISSION_SERIAL = core::NumericLimits<core::uint64>::max() >> 2;

	enum class EComputeBindingType : core::uint8
	{
		UniformBuffer,
		StorageBuffer,
		ReadOnlyBuffer
	};

	struct ComputeBindingDesc final
	{
		core::uint32 Slot = 0;
		EComputeBindingType Type = EComputeBindingType::StorageBuffer;
		EFormat Format = EFormat::UNKNOWN;
	};

	enum class EComputeTextureBindingType : core::uint8
	{
		SampledTexture,
		StorageTexture
	};

	struct ComputeTextureBindingDesc final
	{
		core::uint32 Slot = 0;
		EComputeTextureBindingType Type = EComputeTextureBindingType::SampledTexture;
		SamplerDesc Sampler;
	};

	struct GraphicsTextureBindingDesc final
	{
		core::uint32 Slot = 0;
		SamplerDesc Sampler;
		core::uint32 SamplerSlot = 0xffffffffu;
	};

	struct GraphicsStorageTextureBindingDesc final
	{
		core::uint32 Slot = 0;
	};

	enum class EGraphicsBufferBindingType : core::uint8
	{
		UniformBuffer,
		StorageBuffer,
		ReadOnlyBuffer
	};

	struct GraphicsBufferBindingDesc final
	{
		core::uint32 Slot = 0;
		EGraphicsBufferBindingType Type = EGraphicsBufferBindingType::UniformBuffer;
		core::uint32 RegisterSlot = 0xffffffffu;
		EFormat Format = EFormat::UNKNOWN;
	};
	struct NativeCopy final
	{
		void* Source = nullptr;
		void* Destination = nullptr;

		core::uint64 SourceOffsetBytes = 0;
		core::uint64 DestinationOffsetBytes = 0;

		core::uint64 ByteCount = 0;
	};

	struct NativeTextureTileMapping final
	{
		TextureSubresource Subresource;

		core::uint32 X = 0;
		core::uint32 Y = 0;
		core::uint32 Z = 0;

		void* Heap = nullptr;
		core::uint32 HeapTileOffset = 0;
	};

	struct NativeTextureCopy final
	{
		void* Source = nullptr;
		void* Destination = nullptr;

		core::uint32 Width = 0;
		core::uint32 Height = 0;

		core::uint32 MipLevels = 1;
		core::uint32 ArrayLayers = 1;

		core::uint32 SourceSubresource = 0;
		core::uint32 DestinationSubresource = 0;

		core::uint32 SourceMip = 0;
		core::uint32 SourceLayer = 0;
		core::uint32 DestinationMip = 0;
		core::uint32 DestinationLayer = 0;

		core::uint32 SourceX = 0;
		core::uint32 SourceY = 0;
		core::uint32 DestinationX = 0;
		core::uint32 DestinationY = 0;

		bool bRegion = false;

		core::uint32 Depth = 1;
		core::uint32 SourceZ = 0;
		core::uint32 DestinationZ = 0;

		ETextureAspect Aspect = ETextureAspect::Default;
	};

	struct NativeTextureBufferCopy final
	{
		void* Buffer = nullptr;
		void* Texture = nullptr;

		TextureSubresource Subresource;

		TextureBufferFootprint Footprint;

		core::uint64 BufferOffsetBytes = 0;

		core::uint32 NativeFormat = 0;
		core::uint32 NativeSubresource = 0;

		bool bPrepared = false;
	};

	struct NativeTextureResolve final
	{
		void* Source = nullptr;
		void* Destination = nullptr;

		core::uint32 Width = 0;
		core::uint32 Height = 0;

		EFormat Format = EFormat::UNKNOWN;

		ETextureResolveMode Mode = ETextureResolveMode::Average;
		ETextureResolveMode StencilMode = ETextureResolveMode::None;

		TextureSubresource SourceSubresource;
		TextureSubresource DestinationSubresource;

		void* Prepared = nullptr;
	};

	struct NativeComputeBuffer final
	{
		void* Buffer = nullptr;
		EComputeBindingType Type = EComputeBindingType::StorageBuffer;
		core::uint32 Slot = 0;

		core::uint64 ByteOffset = 0;
		core::uint64 ByteCount = 0;

		void* View = nullptr;
		EFormat Format = EFormat::UNKNOWN;
	};

	struct NativeSampledTexture final
	{
		void* Texture = nullptr;
		void* Sampler = nullptr;
		void* View = nullptr;
	};

	struct NativeStorageTexture final
	{
		void* Texture = nullptr;
		void* View = nullptr;
	};

	struct NativeComputeTexture final
	{
		void* Texture = nullptr;
		void* Sampler = nullptr;
		EComputeTextureBindingType Type = EComputeTextureBindingType::SampledTexture;
		void* View = nullptr;
	};

	struct NativeGraphicsBuffer final
	{
		void* Buffer = nullptr;
		EGraphicsBufferBindingType Type = EGraphicsBufferBindingType::UniformBuffer;
		core::uint32 Slot = 0;

		core::uint64 ByteOffset = 0;
		core::uint64 ByteCount = 0;

		void* View = nullptr;
		EFormat Format = EFormat::UNKNOWN;
	};

	struct NativeVertexBuffer final
	{
		void* Buffer = nullptr;

		core::uint64 Offset = 0;
		core::uint64 ByteCount = 0;
		core::uint32 Stride = 0;
	};

	struct NativeAccelTriangleGeometry final
	{
		void* VertexBuffer = nullptr;
		core::uint64 VertexOffsetBytes = 0;
		core::uint32 VertexStrideBytes = 0;
		core::uint32 VertexCount = 0;

		void* IndexBuffer = nullptr;
		core::uint64 IndexOffsetBytes = 0;
		core::uint32 IndexCount = 0;
		EFormat IndexFormat = EFormat::R32_UINT;

		bool bOpaque = true;

		void* OpacityMicromap = nullptr;

		void* OpacityMicromapIndexBuffer = nullptr;
		core::uint64 OpacityMicromapIndexOffsetBytes = 0;
		core::uint32 OpacityMicromapIndexStrideBytes = 0;
		EFormat OpacityMicromapIndexFormat = EFormat::R32_UINT;

		core::uint32 OpacityMicromapBaseTriangle = 0;
		core::Span<const OpacityMicromapUsageCount> OpacityMicromapUsageCounts;
	};

	struct NativeAccelAabbGeometry final
	{
		void* Buffer = nullptr;
		core::uint64 OffsetBytes = 0;
		core::uint32 StrideBytes = 24;
		core::uint32 Count = 0;

		bool bOpaque = true;
	};

	struct NativeAccelInstance final
	{
		float Transform[12]{};

		core::uint64 BottomLevelAddress = 0;

		core::uint32 InstanceId = 0;
		core::uint32 HitGroupIndex = 0;

		core::uint8 Mask = 0;
		EAccelInstanceFlags Flags = EAccelInstanceFlags::None;
	};

	struct NativeRayLocalRootArgument final
	{
		ERayLocalRootArgumentKind Kind = ERayLocalRootArgumentKind::Constants32;
		BindingRangeDesc Range;
		core::uint32 Space = 0;

		core::uint32 LocalLayoutIndex = UINT32_MAX;
		core::uint32 DescriptorIndex = 0;

		core::uint32 ByteOffset = 0;
		core::uint32 ValueCount = 0;
	};

	// Scalar graphics uses the same stage bits as public range visibility.
	constexpr core::uint32 BINDING_STAGE_GEOMETRY = 1u << 11;
	constexpr core::uint32 BINDING_STAGE_HULL = 1u << 12;
	constexpr core::uint32 BINDING_STAGE_DOMAIN = 1u << 13;
	constexpr core::uint32 SCALAR_COMPUTE_BINDING_STAGES = static_cast<core::uint32>(EShaderVisibilityFlags::Compute) | static_cast<core::uint32>(EShaderVisibilityFlags::RayTracing);
	constexpr core::uint32 SCALAR_GRAPHICS_BINDING_STAGES = static_cast<core::uint32>(EShaderVisibilityFlags::GraphicsRangeStages) | BINDING_STAGE_GEOMETRY | BINDING_STAGE_HULL | BINDING_STAGE_DOMAIN;

	struct BindingLayoutEntry final
	{
		EDescriptorType Type = EDescriptorType::StorageBuffer;
		core::uint32 DescriptorCount = 1;

		EFormat BufferFormat = EFormat::UNKNOWN;
		core::uint32 StructureStrideBytes = 0;

		EBindingRangeSource Source = EBindingRangeSource::Fixed;
		EBufferBindingMode BufferMode = EBufferBindingMode::DescriptorTable;

		SamplerDesc Sampler;
		bool bImmutableSampler = false;
		SamplerHandle ImmutableSampler;

		core::uint32 Slot = 0;
		core::uint32 ResourceOrdinal = 0;

		core::uint32 StageMask = 0;
		bool bImplicitSampler = false;

		core::uint32 RegisterSlot = 0xffffffffu;
		core::uint32 SamplerSlot = 0xffffffffu;
	};

	struct NativeBindingLayoutDesc final
	{
		core::Span<const ComputeBindingDesc> Buffers;
		core::Span<const ComputeTextureBindingDesc> Textures;

		core::Span<const BindingLayoutEntry> Ranges;
		core::uint32 Space = 0;
		[[nodiscard]] BindingRangeDesc Range(core::uint32 index) const noexcept;
		[[nodiscard]] core::uint32 BufferCount() const noexcept;
		[[nodiscard]] core::uint32 TextureCount() const noexcept;
		[[nodiscard]] ComputeBindingDesc Buffer(core::uint32 index) const noexcept;
		[[nodiscard]] ComputeTextureBindingDesc Texture(core::uint32 index) const noexcept;

		core::Span<const BindingLayoutEntry> ScalarEntries;
		core::uint32 ScalarBufferCount = 0;
		bool bRangeLayout = false;
	};

	struct NativeGraphicsBindingLayoutDesc final
	{
		core::Span<const GraphicsTextureBindingDesc> SampledTextures;
		core::Span<const GraphicsStorageTextureBindingDesc> StorageTextures;
		core::Span<const GraphicsBufferBindingDesc> Buffers;

		core::Span<const BindingLayoutEntry> Ranges;
		core::uint32 Space = 0;
		[[nodiscard]] BindingRangeDesc Range(core::uint32 index) const noexcept;
		[[nodiscard]] core::uint32 SampledCount() const noexcept;
		[[nodiscard]] core::uint32 StorageCount() const noexcept;
		[[nodiscard]] core::uint32 BufferCount() const noexcept;
		[[nodiscard]] GraphicsTextureBindingDesc Sampled(core::uint32 index) const noexcept;
		[[nodiscard]] GraphicsStorageTextureBindingDesc Storage(core::uint32 index) const noexcept;
		[[nodiscard]] GraphicsBufferBindingDesc Buffer(core::uint32 index) const noexcept;

		core::Span<const BindingLayoutEntry> ScalarEntries;
		core::uint32 ScalarSampledCount = 0;
		core::uint32 ScalarStorageCount = 0;
		bool bRangeLayout = false;
	};

	struct NativeRayShaderDesc final
	{
		core::Span<const core::Byte> Bytecode;
		const char* EntryPoint = nullptr;
		EShaderStage Stage = EShaderStage::RayGeneration;

		NativeBindingLayoutDesc LocalLayout;
		core::uint32 LocalDataBytes = 0;

		core::Span<const NativeRayLocalRootArgument> LocalRootArguments;
		core::uint32 LocalDataAlignmentBytes = 4;
	};

	struct NativeRayHitGroupDesc final
	{
		core::uint32 ClosestHitIndex = 0xFFFFFFFFu;
		core::uint32 AnyHitIndex = 0xFFFFFFFFu;
		core::uint32 IntersectionIndex = 0xFFFFFFFFu;

		bool bProcedural = false;

		NativeBindingLayoutDesc LocalLayout;
		core::uint32 LocalDataBytes = 0;

		core::Span<const NativeRayLocalRootArgument> LocalRootArguments;
		core::uint32 LocalDataAlignmentBytes = 4;
	};

	struct NativeFixedLayoutState final
	{
		BindingRangeDesc Ranges[MAXIMUM_FIXED_BINDING_LAYOUTS][MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
		core::uint32 RangeCounts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		core::uint32 Spaces[MAXIMUM_FIXED_BINDING_LAYOUTS]{};

		// Compiled D3D12 preparation policy; current descriptor owners and phase coverage remain dynamic.
		core::uint32 FixedDescriptorCounts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};

		bool bPreparedFixedCacheEligible = true;
		bool bHasStorage = false;

		core::uint32 LayoutCount = 0;
	};

	struct NativeDescriptorHeapBinding final
	{
		EDescriptorType Type = EDescriptorType::ReadOnlyBuffer;
		void* Table = nullptr;
		core::Span<const core::uint32> UsedEntries;
	};

	struct NativeTableRangeBinding final
	{
		core::uint32 RangeIndex = 0;
		void* Table = nullptr;
		core::Span<const core::uint32> UsedEntries;
	};

	struct NativeFixedBindingSet final
	{
		core::Span<const NativeFixedDescriptor> Descriptors;

		NativeTableRangeBinding Tables[MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
		core::uint32 TableCount = 0;

		core::SharedPtr<PreparedBindingCache> Cache;
	};

	struct NativePreparedBufferAccess final
	{
		void* Resource = nullptr;
		EBufferState State = EBufferState::CopySource;
	};

	struct NativePreparedVertexInput final
	{
		// ByteCount is the narrowed length, not the whole resource size.
		NativeVertexBuffer Vertices[4]{};
		core::uint32 VertexCount = 0;

		void* IndexBuffer = nullptr;
		BufferRange IndexRange;
		EFormat IndexFormat = EFormat::R16_UINT;
	};

	struct NativePreparedTextureAccess final
	{
		void* Resource = nullptr;
		PreparedTextureAccessDesc Intent;
	};

	struct NativePreparedIndirect final
	{
		EPreparedIndirectKind Kind = EPreparedIndirectKind::Compute;

		void* Arguments = nullptr;
		core::uint64 ArgumentsOffsetBytes = 0;
		core::uint32 Stride = 0;
		core::uint32 MaximumCount = 1;

		void* CountBuffer = nullptr;
		core::uint64 CountBufferOffsetBytes = 0;

		core::uint32 NativeIndex = 0;
	};

	struct NativePreparedPhase final
	{
		core::Span<const NativePreparedBufferAccess> Buffers;
		core::Span<const NativePreparedTextureAccess> Textures;
	};

	struct NativePreparedAccelBuild final
	{
		void* Target = nullptr;
		AccelStructBuildDesc Build;

		core::Span<const NativeAccelTriangleGeometry> Triangles;
		core::Span<const NativeAccelAabbGeometry> Aabbs;

		void* Scratch = nullptr;
		core::uint64 ScratchOffsetBytes = 0;

		void* Instances = nullptr;
		core::uint64 InstanceOffsetBytes = 0;
	};

	struct NativePreparedList final
	{
		void* Commands = nullptr;
		core::Span<const NativePreparedPhase> Phases;

		core::uint32 BindingCount = 0;

		core::uint64 FixedResourceDescriptorCount = 0;
		core::uint64 FixedSamplerDescriptorCount = 0;

		core::uint32 CommandCount = 0;
		core::uint32 IndirectCount = 0;

		core::uint32 AccelBuildCount = 0;
		core::uint32 AccelQueryCount = 0;
		core::uint32 AccelCompactCount = 0;

		EQueue Queue = EQueue::Graphics;
		core::uint32 DependencyFrontier[3]{};
	};

	struct NativePreparedSubmissionWait final
	{
		EQueue Queue = EQueue::Graphics;
		core::uint64 Serial = 0;
	};

	struct NativePreparedSubmissionDesc final
	{
		core::Span<const NativePreparedSubmissionWait> Waits;
		bool bExplicitDependencies = false;

		bool bAcquiredFirstUse = false;
		bool bAcquiredRelease = false;
	};

	struct NativePreparedSubmissionResult final
	{
		core::uint64 Serial = 0;
		core::uint32 QueueWaitCount = 0;

		core::EErrorCode ErrorCode = core::EErrorCode::None;
		core::int32 NativeCode = 0;
		core::uint32 FailureStage = 0;

		bool bAccepted = false;
	};

	struct NativeRayPipelineDesc final
	{
		core::Span<const NativeRayShaderDesc> Shaders;
		core::Span<const NativeRayHitGroupDesc> HitGroups;

		core::Span<const NativeBindingLayoutDesc> GlobalLayouts;

		core::uint32 GeneralShaderCount = 0;

		core::uint32 MaxPayloadBytes = 0;
		core::uint32 MaxAttributeBytes = 8;
		core::uint32 MaxRecursionDepth = 1;

		core::uint32 PushConstantBytes = 0;
		core::uint32 AccelStructSlot = 0xFFFFFFFFu;

		bool bAllowOpacityMicromaps = false;
		bool bAllowClusterAccelerationStructures = false;

		core::uint32 LocalDescriptorCapacity = 0;
	};

	struct NativeBindingSet final
	{
		core::Span<const NativeComputeBuffer> Buffers;
		core::Span<const NativeComputeTexture> Textures;

		core::Span<const core::Byte> LocalData;

		core::Span<const NativeFixedBindingSet> FixedSets;
	};

	struct NativeShaderTableDesc final
	{
		core::uint32 RayGenerationShaderIndex = 0;
		core::Span<const core::uint32> MissShaderIndices;
		core::Span<const core::uint32> HitGroupIndices;
		core::Span<const core::uint32> CallableShaderIndices;

		core::uint32 GeneralShaderCount = 0;

		NativeBindingSet RayGenerationLocal;
		core::Span<const NativeBindingSet> MissLocals;
		core::Span<const NativeBindingSet> HitLocals;
		core::Span<const NativeBindingSet> CallableLocals;
	};

	struct NativeFramebufferDesc final
	{
		void* Colors[8]{};
		EFormat ColorFormats[8]{};
		TextureSubresource ColorSubresources[8]{};
		core::uint32 ColorCount = 0;

		void* Depth = nullptr;
		EFormat DepthFormat = EFormat::UNKNOWN;
		TextureSubresource DepthSubresource;

		void* ShadingRateImage = nullptr;

		core::uint32 Width = 0;
		core::uint32 Height = 0;

		core::uint32 SampleCount = 1;
	};

	// Resolved matrix conversion resources for backend recording.
	struct NativeCoopVecMatrixConversion final
	{
		void* Source = nullptr;
		void* Destination = nullptr;
		CoopVecMatrixConversion Descriptor{};
	};
}
