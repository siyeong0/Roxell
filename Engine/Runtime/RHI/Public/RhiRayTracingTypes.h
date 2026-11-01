#pragma once
#include <RhiFormat.h>
#include <RhiTypes.h>
#include <Span.hpp>
#include <Types.h>
namespace rox::rhi
{
	enum class EAccelStructState : core::uint8
	{
		BuildWrite,
		ShaderRead,
		BuildRead,
		CopySource,
		CopyDestination
	};

	enum class EOpacityMicromapFormat : core::uint32
	{
		TwoState = 1,
		FourState = 2
	};

	enum class EOpacityMicromapBuildFlags : core::uint8
	{
		None = 0,
		FastTrace = 1,
		FastBuild = 2,
		AllowCompaction = 4
	};

	[[nodiscard]] constexpr EOpacityMicromapBuildFlags operator|(EOpacityMicromapBuildFlags left, EOpacityMicromapBuildFlags right) noexcept
	{
		return static_cast<EOpacityMicromapBuildFlags>(static_cast<core::uint8>(left) | static_cast<core::uint8>(right));
	}

	struct OpacityMicromapUsageCount final
	{
		core::uint32 Count = 0;
		core::uint32 SubdivisionLevel = 0;
		EOpacityMicromapFormat Format = EOpacityMicromapFormat::TwoState;
	};

	struct OpacityMicromapBuildSizes final
	{
		core::uint64 ResultBytes = 0;
		core::uint64 BuildScratchBytes = 0;

		core::uint64 ResultAlignmentBytes = 256;
		core::uint64 ScratchAlignmentBytes = 0;
	};

	struct OpacityMicromapDesc final
	{
		BufferHandle Storage;
		core::uint64 ByteOffset = 0;
		core::uint64 ByteCount = 0;
	};

	struct OpacityMicromapBuildDesc final
	{
		core::Span<const OpacityMicromapUsageCount> Counts;
		EOpacityMicromapBuildFlags Flags = EOpacityMicromapBuildFlags::None;

		BufferHandle InputData;
		core::uint64 InputDataOffsetBytes = 0;

		BufferHandle TriangleArray;
		core::uint64 TriangleArrayOffsetBytes = 0;
	};

	enum class EAccelStructType : core::uint8
	{
		BottomLevel,
		TopLevel,
		ClusterBottomLevel
	};

	struct AccelTriangleGeometryDesc final
	{
		BufferHandle VertexBuffer;
		core::uint64 VertexOffsetBytes = 0;
		core::uint32 VertexStrideBytes = 12;
		core::uint32 VertexCount = 0;

		BufferHandle IndexBuffer;
		core::uint64 IndexOffsetBytes = 0;
		core::uint32 IndexCount = 0;
		EFormat IndexFormat = EFormat::R32_UINT;

		bool bOpaque = true;

		OpacityMicromapHandle OpacityMicromap;

		BufferHandle OpacityMicromapIndexBuffer;
		core::uint64 OpacityMicromapIndexOffsetBytes = 0;
		core::uint32 OpacityMicromapIndexStrideBytes = 0;
		EFormat OpacityMicromapIndexFormat = EFormat::R32_UINT;

		core::uint32 OpacityMicromapBaseTriangle = 0;
		core::Span<const OpacityMicromapUsageCount> OpacityMicromapUsageCounts;
	};

	struct AccelAabbGeometryDesc final
	{
		BufferHandle Buffer;
		core::uint64 OffsetBytes = 0;
		core::uint32 StrideBytes = 24;
		core::uint32 Count = 0;

		bool bOpaque = true;
	};

	struct AccelStructBuildDesc final
	{
		EAccelStructType Type = EAccelStructType::BottomLevel;
		core::Span<const AccelTriangleGeometryDesc> Triangles;
		core::uint32 InstanceCount = 0;

		bool bAllowUpdate = false;

		core::Span<const AccelAabbGeometryDesc> Aabbs;

		bool bPerformUpdate = false;
		bool bAllowCompaction = false;
	};

	struct AccelStructBuildSizes final
	{
		core::uint64 ResultBytes = 0;
		core::uint64 BuildScratchBytes = 0;
		core::uint64 UpdateScratchBytes = 0;

		core::uint64 ResultAlignmentBytes = 0;
		core::uint64 ScratchAlignmentBytes = 0;
	};

	enum class EAccelInstanceFlags : core::uint8
	{
		None = 0,
		DisableTriangleCull = 1,
		FrontCounterClockwise = 2,
		ForceOpaque = 4,
		ForceNonOpaque = 8
	};

	[[nodiscard]] constexpr EAccelInstanceFlags operator|(EAccelInstanceFlags left, EAccelInstanceFlags right) noexcept
	{
		return static_cast<EAccelInstanceFlags>(static_cast<core::uint8>(left) | static_cast<core::uint8>(right));
	}

	struct AccelInstanceDesc final
	{
		core::float32 Transform[12]{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };

		AccelStructHandle BottomLevel;

		core::uint32 InstanceId = 0;
		core::uint32 HitGroupIndex = 0;

		core::uint8 Mask = 0xFF;
		EAccelInstanceFlags Flags = EAccelInstanceFlags::None;
	};

	struct alignas(16) AccelGpuInstanceDesc final
	{
		core::float32 Transform[12]{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
		core::uint32 InstanceIdAndMask = 0xFF000000u;
		core::uint32 HitGroupIndexAndFlags = 0;
		core::uint64 BottomLevelAddress = 0;
	};

	static_assert(sizeof(AccelGpuInstanceDesc) == 64);

	enum class EClusterOperationType : core::uint8
	{
		Move,
		ClasBuild,
		ClasBuildTemplates,
		ClasInstantiateTemplates,
		BlasBuild
	};

	enum class EClusterMoveType : core::uint8
	{
		BottomLevel,
		ClusterLevel,
		Template
	};

	enum class EClusterOperationMode : core::uint8
	{
		ImplicitDestinations,
		ExplicitDestinations,
		GetSizes
	};

	enum class EClusterOperationFlags : core::uint8
	{
		None = 0,
		FastTrace = 1,
		FastBuild = 2,
		NoOverlap = 4,
		AllowOpacityMicromaps = 8
	};

	[[nodiscard]] constexpr EClusterOperationFlags operator|(EClusterOperationFlags left, EClusterOperationFlags right) noexcept
	{
		return static_cast<EClusterOperationFlags>(static_cast<core::uint8>(left) | static_cast<core::uint8>(right));
	}

	struct ClusterOperationParams final
	{
		core::uint32 MaxArgCount = 0;
		EClusterOperationType Type = EClusterOperationType::ClasBuild;
		EClusterOperationMode Mode = EClusterOperationMode::ImplicitDestinations;
		EClusterOperationFlags Flags = EClusterOperationFlags::None;

		EClusterMoveType MoveType = EClusterMoveType::ClusterLevel;
		core::uint64 MaxMovedBytes = 0;

		EFormat VertexFormat = EFormat::RGB32_FLOAT;
		core::uint32 MaxGeometryIndex = 0;

		core::uint32 MaxUniqueGeometryCount = 1;
		core::uint32 MaxTriangleCount = 0;
		core::uint32 MaxVertexCount = 0;

		core::uint32 MaxTotalTriangleCount = 0;
		core::uint32 MaxTotalVertexCount = 0;

		core::uint32 MinPositionTruncateBitCount = 0;

		core::uint32 MaxClasPerBlasCount = 0;
		core::uint32 MaxTotalClasCount = 0;
	};

	struct ClusterOperationSizeInfo final
	{
		core::uint64 ResultMaxSizeInBytes = 0;
		core::uint64 ScratchSizeInBytes = 0;

		core::uint64 ResultAlignmentBytes = 0;
		core::uint64 ScratchAlignmentBytes = 0;
	};

	// GPU indirect arguments. BLAS reference stride differs between native backends.
	struct ClusterMoveInfo final
	{
		core::uint64 SourceAddress = 0;
	};

	struct ClusterBlasBuildInfo final
	{
		core::uint32 ClusterReferenceCount = 0;
		core::uint32 ClusterReferenceStrideBytes = 8;
		core::uint64 ClusterReferencesAddress = 0;

		[[nodiscard]] static constexpr ClusterBlasBuildInfo ForBackend(EBackend backend, core::uint32 count, core::uint64 address) noexcept
		{
			return { count, backend == EBackend::D3D12 ? 0u : 8u, address };
		}
	};

	struct ClusterTriangleBuildInfo final
	{
		core::uint32 ClusterId = 0;
		core::uint32 ClusterFlags = 0;
		core::uint32 PackedCounts = 0;
		core::uint32 BaseGeometryIndexAndFlags = 0;

		core::uint16 IndexBufferStride = 0;
		core::uint16 VertexBufferStride = 0;
		core::uint16 GeometryIndexAndFlagsBufferStride = 0;
		core::uint16 OpacityMicromapIndexBufferStride = 0;

		core::uint64 IndexBufferAddress = 0;
		core::uint64 VertexBufferAddress = 0;
		core::uint64 GeometryIndexAndFlagsBufferAddress = 0;
		core::uint64 OpacityMicromapArrayAddress = 0;
		core::uint64 OpacityMicromapIndexBufferAddress = 0;

		[[nodiscard]] static constexpr core::uint32 PackCounts(
			core::uint32 triangles,
			core::uint32 vertices,
			core::uint32 truncateBits,
			core::uint32 indexFormat,
			core::uint32 micromapIndexFormat = 1) noexcept
		{
			return triangles | (vertices << 9) | (truncateBits << 18) | (indexFormat << 24) | (micromapIndexFormat << 28);
		}
	};

	struct ClusterTriangleTemplateBuildInfo final
	{
		ClusterTriangleBuildInfo Triangle;
		core::uint64 InstantiationBoundingBoxLimitAddress = 0;
	};

	struct ClusterInstantiateInfo final
	{
		core::uint32 ClusterIdOffset = 0;
		core::uint32 GeometryIndexOffset = 0;

		core::uint64 ClusterTemplateAddress = 0;

		core::uint64 VertexBufferAddress = 0;
		core::uint64 VertexBufferStride = 0;
	};

	static_assert(sizeof(ClusterMoveInfo) == 8);
	static_assert(sizeof(ClusterBlasBuildInfo) == 16);
	static_assert(sizeof(ClusterTriangleBuildInfo) == 64);
	static_assert(sizeof(ClusterTriangleTemplateBuildInfo) == 72);
	static_assert(sizeof(ClusterInstantiateInfo) == 32);

	struct ClusterOperationDesc final
	{
		ClusterOperationParams Params;

		BufferHandle Scratch;
		core::uint64 ScratchOffsetBytes = 0;

		BufferHandle IndirectArgCount;
		core::uint64 IndirectArgCountOffsetBytes = 0;

		BufferHandle IndirectArgs;
		core::uint64 IndirectArgsOffsetBytes = 0;
		core::uint64 IndirectArgsStrideBytes = 0;

		BufferHandle Addresses;
		core::uint64 AddressesOffsetBytes = 0;
		core::uint64 AddressesStrideBytes = 8;

		BufferHandle Sizes;
		core::uint64 SizesOffsetBytes = 0;
		core::uint64 SizesStrideBytes = 4;

		BufferHandle Result;
		core::uint64 ResultOffsetBytes = 0;
		// Explicit build extent derived from GetSizes. Zero keeps the conservative size check.
		core::uint64 ResultByteCount = 0;

		AccelStructHandle ResultBlas;

		core::Span<const BufferHandle> ReferencedBuffers;
		core::Span<const OpacityMicromapHandle> ReferencedMicromaps;
	};

	struct AccelStructDesc final
	{
		EAccelStructType Type = EAccelStructType::BottomLevel;

		BufferHandle Storage;
		core::uint64 ByteOffset = 0;
		core::uint64 ByteCount = 0;
	};

	enum class ERayLocalRootArgumentKind : core::uint8
	{
		Constants32,
		ConstantBufferAddress,
		ShaderResourceAddress,
		UnorderedAccessAddress,
		DescriptorTable
	};

	struct RayLocalRootArgumentDesc final
	{
		ERayLocalRootArgumentKind Kind = ERayLocalRootArgumentKind::Constants32;
		core::uint32 LocalLayoutIndex = 0xffffffffu;
		core::uint32 RangeIndex = 0xffffffffu;

		// Constants use these explicit registers; resources use their actual fixed range.
		core::uint32 Binding = 0;
		core::uint32 Space = 0;

		core::uint32 ValueCount = 0;
	};

	struct RayTracingShaderDesc final
	{
		ShaderHandle Shader;

		BindingLayoutHandle LocalLayout;
		// Four-byte aligned user constants copied into each shader record; at most 128 bytes.
		core::uint32 LocalDataBytes = 0;
		// Ordered typed roots are exclusive with LocalLayout/LocalDataBytes.
		core::Span<const BindingLayoutHandle> LocalLayouts;

		core::Span<const RayLocalRootArgumentDesc> LocalRootArguments;
		// Numeric shader-record bytes begin immediately after the native shader identifier.
		core::uint32 LocalDataAlignmentBytes = 4;
	};

	struct RayTracingHitGroupDesc final
	{
		ShaderHandle ClosestHitShader;
		ShaderHandle AnyHitShader;
		ShaderHandle IntersectionShader;

		BindingLayoutHandle LocalLayout;

		bool bProcedural = false;

		core::uint32 LocalDataBytes = 0;
		core::Span<const BindingLayoutHandle> LocalLayouts;

		core::Span<const RayLocalRootArgumentDesc> LocalRootArguments;
		core::uint32 LocalDataAlignmentBytes = 4;
	};

	struct RayTracingPipelineDesc final
	{
		core::Span<const RayTracingShaderDesc> Shaders;
		core::Span<const RayTracingHitGroupDesc> HitGroups;

		core::Span<const BindingLayoutHandle> GlobalLayouts;

		core::uint32 MaxPayloadBytes = 0;
		core::uint32 MaxAttributeBytes = 8;
		core::uint32 MaxRecursionDepth = 1;

		bool bAllowOpacityMicromaps = false;
		bool bAllowClusterAccelerationStructures = false;

		core::uint32 PushConstantBytes = 0;

		// Capacity per used Vulkan local descriptor array; 0 selects 32. Checked at dispatch.
		core::uint32 LocalDescriptorCapacity = 0;
	};

	struct ShaderTableRecordDesc final
	{
		// Each set matches the corresponding ordered typed local pipeline layout.
		core::Span<const BindingSetHandle> LocalSets;
		// Exact root constant byte spans, in Constants32 argument order. Copied at creation.
		core::Span<const core::Span<const core::Byte>> Constants;
	};

	struct ShaderTableDesc final
	{
		RayTracingPipelineHandle Pipeline;

		core::uint32 RayGenerationShaderIndex = 0;
		core::Span<const core::uint32> MissShaderIndices;
		core::Span<const core::uint32> HitGroupIndices;
		core::Span<const core::uint32> CallableShaderIndices;

		BindingSetHandle RayGenerationLocalSet;
		core::Span<const BindingSetHandle> MissLocalSets;
		core::Span<const BindingSetHandle> HitGroupLocalSets;
		core::Span<const BindingSetHandle> CallableLocalSets;

		// Data is copied at creation. Optional arrays match their corresponding record counts.
		core::Span<const core::Byte> RayGenerationLocalData;
		core::Span<const core::Span<const core::Byte>> MissLocalData;
		core::Span<const core::Span<const core::Byte>> HitGroupLocalData;
		core::Span<const core::Span<const core::Byte>> CallableLocalData;

		// Typed record descriptions are exclusive with LocalSets/LocalData.
		ShaderTableRecordDesc RayGenerationRecord;
		core::Span<const ShaderTableRecordDesc> MissRecords;
		core::Span<const ShaderTableRecordDesc> HitGroupRecords;
		core::Span<const ShaderTableRecordDesc> CallableRecords;
	};

	enum class EAccelCompactionQueryState : core::uint8
	{
		Pending,
		Ready,
		Unavailable
	};

	struct AccelCompactionResult final
	{
		EAccelCompactionQueryState State = EAccelCompactionQueryState::Pending;
		core::uint64 ByteCount = 0;
	};
}
