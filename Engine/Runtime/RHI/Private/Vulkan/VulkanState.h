#pragma once

#include "../SubmissionCompletion.h"
#include "VulkanSubmission.h"

#include <RhiDeviceTypes.h>

namespace rox::rhi
{
	class IGpuCrashDiagnostics;
}

namespace rox::vulkan
{
	using namespace rox::rhi;

	struct DirectRenderPass final
	{
		EFormat ColorFormat = EFormat::UNKNOWN;
		EFormat DepthFormat = EFormat::UNKNOWN;
		core::uint32 SampleCount = 1;

		bool bLoadDepth = false;
		bool bReadOnlyDepth = false;
		bool bReadOnlyStencil = false;

		VkRenderPass Handle = VK_NULL_HANDLE;
	};

	struct State final
	{
		bool bBindlessTypedBuffers = false;

		explicit State(core::MemoryResource& resource) noexcept;

		core::MemoryResource& Resource;
#ifdef _WIN32
		HMODULE Library = nullptr;
#else
		void* Library = nullptr;
#endif
		Functions F;

		VkInstance Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;

		VkPhysicalDevice Physical = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;

		VkQueue Queue = VK_NULL_HANDLE;
		VkQueue ComputeQueue = VK_NULL_HANDLE;
		VkQueue CopyQueue = VK_NULL_HANDLE;

		bool bSeparateComputeQueue = false;
		bool bSeparateCopyQueue = false;

		bool bExternalDevice = false;
		bool bExternalMemoryEnabled = false;
		bool bExternalSemaphoreEnabled = false;

		bool bMemoryBudget = false;
		core::uint32 ApiVersion = VK_API_VERSION_1_2;
		bool bHostQueryReset = false;
		bool bSparseResidency = false;

		VkCommandPool Pool = VK_NULL_HANDLE;
		VkCommandPool ComputePool = VK_NULL_HANDLE;
		VkCommandPool CopyPool = VK_NULL_HANDLE;

		VkSemaphore Timeline = VK_NULL_HANDLE;
		VkSemaphore SparseReady[3]{};
		VkSemaphore ComputeTimeline = VK_NULL_HANDLE;
		VkSemaphore CopyTimeline = VK_NULL_HANDLE;

		VkRenderPass RenderPass = VK_NULL_HANDLE;
		VkRenderPass DepthRenderPass = VK_NULL_HANDLE;
		VkRenderPass DepthLoadRenderPass = VK_NULL_HANDLE;

		core::uint32 QueueFamily = UINT32_MAX;
		core::uint32 ComputeQueueFamily = UINT32_MAX;
		core::uint32 CopyQueueFamily = UINT32_MAX;
		core::uint32 ComputeQueueIndex = 0;
		core::uint32 CopyQueueIndex = 0;

		core::uint32 ResourceQueueFamilies[3]{};
		core::uint32 ResourceQueueFamilyCount = 0;

		VkQueueFlags GraphicsQueueFlags = 0;
		VkQueueFlags ComputeQueueFlags = 0;
		VkQueueFlags CopyQueueFlags = 0;

		core::uint32 TimestampValidBits = 0;
		core::uint32 ComputeTimestampValidBits = 0;
		core::uint32 CopyTimestampValidBits = 0;

		bool bSurfaceEnabled = false;
		bool bXlibSurface = false;
		bool bWaylandSurface = false;
		bool bExtendedColorSpace = false;
		bool bHdrMetadata = false;

		bool bSwapchainEnabled = false;
		bool bSwapchainMaintenance = false;
		bool bSurfaceMaintenance = false;
		bool bSurfaceCapabilities2 = false;

		SwapChain* SwapChains = nullptr;

		bool bDebugUtilsEnabled = false;
		IGpuCrashDiagnostics* Diagnostics = nullptr;
		bool bAftermathAttached = false;
		bool bAftermathCheckpoints = false;

		VkPhysicalDeviceMemoryProperties MemoryProperties{};
		VkPhysicalDeviceProperties DeviceProperties{};

		core::uint32 WaveLaneCount = 0;
		ESubgroupOperationFlags ComputeSubgroupOperations = ESubgroupOperationFlags::None;
		ESubgroupOperationFlags FragmentSubgroupOperations = ESubgroupOperationFlags::None;

		bool bGraphicsStorageWrites = false;

		bool bDrawIndexedIndirectCount = false;
		bool bDrawIndirectCount = false;

		bool bSamplerAnisotropy = false;
		bool bSamplerFilterMinmax = false;

		bool bCubeArray = false;

		bool bGeometryShader = false;
		bool bTessellationShader = false;

		bool bWireframe = false;
		bool bDepthClamp = false;
		bool bDepthBiasClamp = false;
		bool bDepthBounds = false;
		bool bConservativeRasterization = false;

		bool bMeshShader = false;
		bool bTaskShader = false;
		bool bMeshIndirect = false;
		core::uint32 MaximumMeshGroups[3]{};
		core::uint32 MaximumMeshGroupTotalCount = 0;

		bool bAttachmentShadingRate = false;
		bool bPrimitiveShadingRate = false;
		bool bNonTrivialShadingRateCombiners = false;
		VkExtent2D ShadingRateTile{};

		bool bMultiViewport = false;
		bool bDualSourceBlend = false;
		bool bIndependentBlend = false;

		bool bMultiDrawIndirect = false;
		bool bIndirectFirstInstance = false;

		bool bUniformBufferArrayDynamicIndexing = false;
		bool bSampledImageArrayDynamicIndexing = false;
		bool bStorageBufferArrayDynamicIndexing = false;
		bool bStorageImageArrayDynamicIndexing = false;

		bool bBufferDeviceAddress = false;
		bool bImageSlicedViewOf3D = false;

		bool bAccelerationStructure = false;

		bool bOpacityMicromap = false;
		bool bRayTracingClusters = false;
		core::uint32 ClusterExtensionRevision = 0;
		VkPhysicalDeviceClusterAccelerationStructurePropertiesNV ClusterProperties{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV };
		VkPhysicalDeviceOpacityMicromapPropertiesEXT MicromapProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT };

		bool bRayQuery = false;
		bool bRayTracingPipeline = false;

		bool bCooperativeVector = false;
		bool bCooperativeVectorTraining = false;
		VkPhysicalDeviceCooperativeVectorPropertiesNV CoopVecProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_PROPERTIES_NV };

		core::uint64 AccelScratchAlignment = 0;
		VkPhysicalDeviceAccelerationStructurePropertiesKHR AccelerationProperties{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR RayTracingProperties{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };

		bool bShaderInt64 = false;
		bool bShaderDrawParameters = false;

		bool bSampleRateShading = false;

		bool bShaderImageGatherExtended = false;
		bool bShaderStorageImageExtendedFormats = false;
		bool bShaderStorageImageReadWithoutFormat = false;
		bool bShaderStorageImageWriteWithoutFormat = false;

		bool bShaderFloat16 = false;

		bool bLoadStoreOpNone = false;
		bool bDynamicRendering = false;
		bool bMaintenance4 = false;
		bool bSeparateDepthStencilAttachmentAccess = false;
		bool bShaderDemoteToHelperInvocation = false;

		core::uint32 SupportedShadingRates[4]{};

		core::uint32 MaximumBindlessSampledTextures = 0;
		core::uint32 MaximumBindlessSamplers = 0;
		core::uint32 MaximumBindlessStorageBuffers = 0;
		core::uint32 MaximumBindlessStorageTextures = 0;
		core::uint32 MaximumBindlessUniformBuffers = 0;

		SubmissionCompletion Submissions;
		core::Array<Pending> InFlight;
		core::usize PreparedCredits = 0;

		struct RecycledCommandBuffer final
		{
			VkCommandBuffer Commands = VK_NULL_HANDLE;
			VkCommandPool Pool = VK_NULL_HANDLE;
			EQueue Queue = EQueue::Graphics;
		};

		core::Array<RecycledCommandBuffer> Recycled;

		core::Array<Retired> RetiredBuffers;
		core::Array<RetiredAccelStruct> RetiredAccelStructs;
		core::Array<RetiredMicromap> RetiredMicromaps;
		core::Array<RetiredTexture> RetiredTextures;
		core::Array<RetiredPipeline> RetiredPipelines;
		core::Array<RetiredGraphicsPipeline> RetiredGraphicsPipelines;
		core::Array<RetiredRayPipeline> RetiredRayPipelines;
		core::Array<RetiredFramebuffer> RetiredFramebuffers;

		core::Array<DirectRenderPass> DirectRenderPasses;

		core::uint64 LastSerial = 0;
		core::uint64 LastGraphicsSerial = 0;
		core::uint64 LastComputeSerial = 0;
		core::uint64 LastCopySerial = 0;

		bool bLost = false;
		bool bDeviceLossConfirmed = false;

		VkFence IdleFences[3]{};
		bool bIdleFencePending[3]{};
		bool bIdleIssued = false;
		bool bIdleConfirmed = true;

		bool bNativeQueuesExposed = false;

		bool bSubmissionMayBePending = false;
		core::uint64 PresentCopySerial = 0;

		core::int32 LastSubmissionNativeCode = 0;
		core::uint32 LastSubmissionFailureStage = 0;

		bool bHasExplicitPreparedWork = false;

		VkFence SparseFences[3]{};
		bool bSparseFencePending[3]{};
	};
}
