#pragma once

#include <RhiInterop.h>
#include <RhiTypes.h>
#include <Types.h>

namespace rox::rhi
{
	enum class ESubgroupOperationFlags : core::uint32
	{
		None = 0,
		Basic = 1,
		Vote = 2,
		Arithmetic = 4,
		Ballot = 8,
		Shuffle = 16,
		ShuffleRelative = 32,
		Clustered = 64,
		Quad = 128
	};

	struct DeviceFeatures final
	{
		bool bGraphics = false;
		bool bCompute = false;
		bool bCopy = false;

		bool bGeometryShader = false;
		bool bTessellationShader = false;
		bool bMeshShader = false;
		bool bTaskShader = false;

		bool bShaderInt64 = false;
		// Native shader arithmetic only; sixteen-bit buffer/storage features are separate.
		bool bShaderFloat16 = false;
		bool bShaderSpecializations = false;
		// Vulkan LocalSizeId execution mode support enabled through maintenance4.
		bool bShaderLocalSizeId = false;
		bool bShaderDrawParameters = false;
		// SPIR-V DemoteToHelperInvocation support enabled on the Vulkan device.
		bool bShaderDemoteToHelperInvocation = false;
		bool bCooperativeVectorMatrixConversion = false;

		// Physical subgroup operation families available to compute shaders; no stage inference.
		ESubgroupOperationFlags ComputeSubgroupOperations = ESubgroupOperationFlags::None;
		// Physical fragment-stage subgroup families, independent of compute-stage support.
		ESubgroupOperationFlags PixelSubgroupOperations = ESubgroupOperationFlags::None;

		bool bWireframe = false;
		bool bDepthClamp = false;
		bool bDepthBiasClamp = false;
		bool bDepthBounds = false;
		bool bConservativeRasterization = false;
		bool bIndependentBlend = false;
		bool bSampleRateShading = false;
		bool bSeparateDepthStencilAttachmentAccess = false;

		bool bVariableRateShading = false;
		bool bShadingRateImage = false;
		bool bPrimitiveShadingRate = false;
		bool bShadingRateNonTrivialCombiners = false;

		bool bMultiDrawIndirect = false;
		bool bIndirectFirstInstance = false;
		bool bDrawIndirectCount = false;
		bool bDrawIndexedIndirectCount = false;
		bool bMeshIndirect = false;

		bool bBufferDeviceAddress = false;
		bool bConstantBufferRanges = false;
		bool bBindlessTypedBuffers = false;
		bool bHeapDirectlyIndexed = false;
		// True only when direct device heap indexing was enabled at device creation.
		bool bDeviceHeapIndexing = false;
		bool bUniformBufferArrayDynamicIndexing = false;
		bool bSampledImageArrayDynamicIndexing = false;
		bool bStorageBufferArrayDynamicIndexing = false;
		bool bStorageImageArrayDynamicIndexing = false;

		bool bSparseTextures = false;
		bool bSamplerReductionMinMax = false;
		bool bSamplerFeedback = false;
		// Supports nonzero-base-mip stencil sampled views and combined depth/stencil attachments.
		bool bNonzeroMipStencilViews = false;
		bool bSliced3DStorageViews = false;
		// D3D12_OPTIONS.TypedUAVLoadAdditionalFormats; individual formats still require ShaderUavLoad.
		bool bTypedUavLoadAdditionalFormats = false;
		// True only when Vulkan shaderImageGatherExtended was enabled on the logical device.
		bool bShaderImageGatherExtended = false;
		// Vulkan extended storage-image formats admitted by the selected logical-device contract.
		bool bShaderStorageImageExtendedFormats = false;
		bool bShaderStorageImageReadWithoutFormat = false;
		bool bShaderStorageImageWriteWithoutFormat = false;

		bool bAccelerationStructure = false;
		bool bRayQuery = false;
		bool bRayTracingPipeline = false;
		bool bOpacityMicromap = false;
		bool bRayTracingClusters = false;

		bool bDebugMarkers = false;
		// Actual queue timestamp/query-reset support; allocation failures remain recoverable.
		bool bGraphicsTimestamps = false;
		bool bComputeTimestamps = false;
		bool bCopyTimestamps = false;

		bool bBoundedPresentationWait = false;
	};

	struct DeviceLimits final
	{
		core::uint64 MaximumBufferByteCount = 1ull << 40;

		core::uint64 MaximumUniformBufferRangeBytes = 65536;
		core::uint64 MaximumStorageBufferRangeBytes = 1ull << 32;

		core::uint64 UniformBufferOffsetAlignmentBytes = 256;
		core::uint64 StorageBufferOffsetAlignmentBytes = 16;

		core::uint32 MaximumTexture1DDimension = 16384;
		core::uint32 MaximumTexture2DDimension = 16384;
		core::uint32 MaximumTexture3DDimension = 2048;
		core::uint32 MaximumTextureArrayLayers = 2048;
		core::uint32 MaximumTextureMipLevels = 16;

		core::uint64 MaximumTextureByteCount = 1ull << 40;
		core::uint64 MaximumSparseTextureByteCount = 1ull << 40;

		core::uint32 MaximumBindingLayouts = MAXIMUM_FIXED_BINDING_LAYOUTS;
		core::uint32 MaximumBindingsPerLayout = 16;
		core::uint32 MaximumPushConstantByteCount = 128;

		core::uint32 TextureBufferOffsetAlignmentBytes = 512;
		core::uint32 TextureBufferRowPitchAlignmentBytes = 256;

		core::uint32 MaximumComputeWorkGroupSize[3] = {};
		core::uint32 MaximumComputeWorkGroupInvocations = 0;
		core::uint32 MaximumComputeWorkGroupCount[3] = {};

		core::uint32 MaximumColorAttachments = 0;

		core::uint32 MaximumBindlessSampledTextures = 0;
		core::uint32 MaximumBindlessSamplers = 0;
		core::uint32 MaximumBindlessStorageBuffers = 0;
		core::uint32 MaximumBindlessStorageTextures = 0;
		core::uint32 MaximumBindlessUniformBuffers = 0;

		core::uint32 MaximumGraphicsBindlessStorageBuffers = 0;
		core::uint32 MaximumGraphicsBindlessStorageTextures = 0;

		core::uint32 MaximumViewports = 0;

		core::uint32 MaximumCountedIndirectDraws = 0;

		core::uint32 MinimumWaveLaneCount = 0;
		core::uint32 MaximumWaveLaneCount = 0;

		core::uint32 MaximumDualSourceColorAttachments = 0;

		core::uint32 SupportedShadingRates[4]{};
		core::uint32 ShadingRateImageTileWidth = 0;
		core::uint32 ShadingRateImageTileHeight = 0;

		core::uint32 MaximumMeshGroups[3]{};
		core::uint32 MaximumMeshGroupTotalCount = 0;

		core::uint32 MaximumBindlessReadOnlyBuffers = 0;

		core::uint32 MaximumTaskGroups[3]{};
		core::uint32 MaximumTaskGroupTotalCount = 0;

		core::uint32 MaximumMeshWorkGroupSize[3]{};
		core::uint32 MaximumMeshWorkGroupInvocations = 0;

		core::uint32 MaximumMeshOutputVertices = 0;
		core::uint32 MaximumMeshOutputPrimitives = 0;
		core::uint32 MaximumMeshOutputComponents = 0;
		core::uint32 MaximumMeshOutputByteCount = 0;

		core::uint32 MeshOutputPerVertexGranularity = 0;
		core::uint32 MeshOutputPerPrimitiveGranularity = 0;

		core::uint32 MaximumTaskWorkGroupSize[3]{};
		core::uint32 MaximumTaskWorkGroupInvocations = 0;

		core::uint32 MaximumTaskPayloadByteCount = 0;
		core::uint32 MaximumTaskSharedByteCount = 0;
		core::uint32 MaximumTaskPayloadAndSharedByteCount = 0;

		core::uint32 MaximumMeshSharedByteCount = 0;
		core::uint32 MaximumMeshPayloadAndSharedByteCount = 0;
		core::uint32 MaximumMeshPayloadAndOutputByteCount = 0;

		core::uint64 MaximumAccelGeometryCount = 0;
		core::uint64 MaximumAccelPrimitiveCount = 0;
		core::uint64 MaximumAccelInstanceCount = 0;

		core::uint32 MaximumRayRecursionDepth = 0;
		core::uint32 MaximumRayAttributeByteCount = 0;
		core::uint64 MaximumRayDispatchInvocations = 0;
		core::uint64 MaximumRayDispatchDimensions[3]{};

		core::uint32 MaximumDescriptorTableAccelerationStructures = 0;
	};

	struct AdapterInfo final
	{
		core::uint32 VendorID = 0;
		core::uint32 DeviceID = 0;
		bool bSoftwareAdapter = false;
	};

	struct GpuCapabilities final
	{
		DeviceFeatures Features{};
		DeviceLimits Limits{};
		AdapterInfo Adapter{};
	};

	class IGpuCrashDiagnostics;

	enum class EAdapterPreference : core::uint8
	{
		Default,
		HighPerformance,
		MinimumPower,
		Software
	};

	struct ExternalVulkanFeatures final
	{
		bool bMeshShader = false;
		bool bTaskShader = false;
		bool bShaderInt64 = false;
		// Declares Vulkan 1.2 shaderFloat16 enabled on the borrowed logical device.
		bool bShaderFloat16 = false;
		bool bShaderDrawParameters = false;
		// Declares both the enabled feature and its extension when the device API is below Vulkan 1.3.
		bool bShaderDemoteToHelperInvocation = false;
		// Declares maintenance4 enabled, plus VK_KHR_maintenance4 below API 1.3.
		bool bMaintenance4 = false;

		bool bCooperativeVector = false;
		bool bCooperativeVectorTraining = false;

		bool bIndependentBlend = false;
		bool bSampleRateShading = false;
		// Declares the core depthBounds feature enabled on the borrowed Vulkan device.
		bool bDepthBounds = false;
		// Declares dynamicRendering enabled, plus VK_KHR_dynamic_rendering below API 1.3.
		bool bDynamicRendering = false;
		// Declares VK_EXT_load_store_op_none enabled on the borrowed device.
		bool bLoadStoreOpNone = false;
		// Declares VK_KHR_maintenance7 and its maintenance7 feature enabled.
		bool bMaintenance7 = false;

		bool bFragmentShadingRate = false;
		bool bAttachmentShadingRate = false;
		bool bPrimitiveShadingRate = false;

		bool bMultiDrawIndirect = false;
		bool bIndirectFirstInstance = false;

		bool bBufferDeviceAddress = false;
		bool bUniformBufferArrayDynamicIndexing = false;
		bool bSampledImageArrayDynamicIndexing = false;
		bool bStorageBufferArrayDynamicIndexing = false;
		bool bStorageImageArrayDynamicIndexing = false;

		bool bSparseResidency = false;
		bool bImageSlicedViewOf3D = false;
		// Declares the core shaderImageGatherExtended feature enabled on the borrowed Vulkan device.
		bool bShaderImageGatherExtended = false;
		// Declares the extended storage-image format contract for the borrowed Vulkan device.
		bool bShaderStorageImageExtendedFormats = false;
		bool bShaderStorageImageReadWithoutFormat = false;
		bool bShaderStorageImageWriteWithoutFormat = false;

		bool bAccelerationStructure = false;
		bool bRayQuery = false;
		bool bRayTracingPipeline = false;
		bool bRayTracingClusters = false;
		bool bOpacityMicromap = false;

		bool bExternalMemory = false;
		bool bExternalTimelineSemaphore = false;
		bool bMemoryBudget = false;

		bool bHostQueryReset = false;
	};

	struct DeviceCreateDesc final
	{
		EBackend Backend = EBackend::Null;
		bool bEnableValidation = false;
		EAdapterPreference AdapterPreference = EAdapterPreference::Default;

		NativeDeviceHandles ExternalDevice{};
		NativeQueueHandle ExternalComputeQueue{};
		NativeQueueHandle ExternalCopyQueue{};

		ExternalVulkanFeatures VulkanFeatures{};

		bool bEnableDeviceHeapIndexing = false;

		IGpuCrashDiagnostics* Diagnostics = nullptr;
	};
}
