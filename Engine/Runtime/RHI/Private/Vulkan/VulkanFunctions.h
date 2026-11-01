#pragma once

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#else
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <vulkan/vulkan.h>
#if defined(__ANDROID__)
#include <vulkan/vulkan_android.h>
#elif defined(__linux__)
// Vulkan loads the window-system entry points; no Xlib or Wayland linkage is needed.
struct _XDisplay;
using Display = _XDisplay;
using Window = unsigned long;
using VisualID = unsigned long;
#include <vulkan/vulkan_xlib.h>
#include <vulkan/vulkan_wayland.h>
#endif

namespace rox::vulkan
{
	struct Functions final
	{
		PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
		PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;

		PFN_vkCreateInstance CreateInstance = nullptr;
		PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensions = nullptr;
		PFN_vkEnumerateInstanceLayerProperties EnumerateInstanceLayers = nullptr;
		PFN_vkDestroyInstance DestroyInstance = nullptr;

		PFN_vkCreateDebugUtilsMessengerEXT CreateDebugMessenger = nullptr;
		PFN_vkDestroyDebugUtilsMessengerEXT DestroyDebugMessenger = nullptr;

		PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
		PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensions = nullptr;

		PFN_vkDestroySurfaceKHR DestroySurface = nullptr;
		PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetSurfaceSupport = nullptr;
		PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetSurfaceCapabilities = nullptr;
		PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetSurfaceFormats = nullptr;
		PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPresentModes = nullptr;
#ifdef _WIN32

		PFN_vkCreateWin32SurfaceKHR CreateWin32Surface = nullptr;
#elif defined(__ANDROID__)
		PFN_vkCreateAndroidSurfaceKHR CreateAndroidSurface = nullptr;
#elif defined(__linux__)
		PFN_vkCreateXlibSurfaceKHR CreateXlibSurface = nullptr;
		PFN_vkCreateWaylandSurfaceKHR CreateWaylandSurface = nullptr;
#endif

		PFN_vkSetHdrMetadataEXT SetHdrMetadata = nullptr;

		PFN_vkGetPhysicalDeviceQueueFamilyProperties GetQueueFamilies = nullptr;
		PFN_vkGetPhysicalDeviceMemoryProperties GetMemoryProperties = nullptr;
		PFN_vkGetPhysicalDeviceMemoryProperties2 GetMemoryProperties2 = nullptr;

		PFN_vkGetPhysicalDeviceFormatProperties GetFormatProperties = nullptr;
		PFN_vkGetPhysicalDeviceImageFormatProperties GetImageFormatProperties = nullptr;

		PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
		PFN_vkGetPhysicalDeviceProperties2 GetPhysicalDeviceProperties2 = nullptr;

		PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures = nullptr;
		PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2 = nullptr;

		PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR GetFragmentShadingRates = nullptr;

		PFN_vkCreateDevice CreateDevice = nullptr;
		PFN_vkDestroyDevice DestroyDevice = nullptr;
		PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;

		PFN_vkCmdDrawMeshTasksEXT CmdDrawMeshTasks = nullptr;
		PFN_vkCmdDrawMeshTasksIndirectEXT CmdDrawMeshTasksIndirect = nullptr;

		PFN_vkCreateBuffer CreateBuffer = nullptr;
		PFN_vkDestroyBuffer DestroyBuffer = nullptr;

		PFN_vkCreateImage CreateImage = nullptr;
		PFN_vkDestroyImage DestroyImage = nullptr;

		PFN_vkCreateBufferView CreateBufferView = nullptr;
		PFN_vkDestroyBufferView DestroyBufferView = nullptr;

		PFN_vkCreateImageView CreateImageView = nullptr;
		PFN_vkDestroyImageView DestroyImageView = nullptr;

		PFN_vkCreateFramebuffer CreateFramebuffer = nullptr;
		PFN_vkDestroyFramebuffer DestroyFramebuffer = nullptr;

		PFN_vkCreateRenderPass CreateRenderPass = nullptr;
		PFN_vkCreateRenderPass2 CreateRenderPass2 = nullptr;
		PFN_vkDestroyRenderPass DestroyRenderPass = nullptr;

		PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements = nullptr;
		PFN_vkGetImageMemoryRequirements2 GetImageMemoryRequirements2 = nullptr;

		PFN_vkGetImageSparseMemoryRequirements GetImageSparseMemoryRequirements = nullptr;
		PFN_vkQueueBindSparse QueueBindSparse = nullptr;
		PFN_vkBindImageMemory BindImageMemory = nullptr;

		PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = nullptr;
		PFN_vkGetBufferMemoryRequirements2 GetBufferMemoryRequirements2 = nullptr;
		PFN_vkGetBufferDeviceAddress GetBufferDeviceAddress = nullptr;

		PFN_vkGetAccelerationStructureBuildSizesKHR GetAccelerationStructureBuildSizes = nullptr;
		PFN_vkCreateAccelerationStructureKHR CreateAccelerationStructure = nullptr;
		PFN_vkDestroyAccelerationStructureKHR DestroyAccelerationStructure = nullptr;
		PFN_vkGetAccelerationStructureDeviceAddressKHR GetAccelerationStructureDeviceAddress = nullptr;
		PFN_vkCmdBuildAccelerationStructuresKHR CmdBuildAccelerationStructures = nullptr;

		PFN_vkGetClusterAccelerationStructureBuildSizesNV GetClusterBuildSizes = nullptr;
		PFN_vkCmdBuildClusterAccelerationStructureIndirectNV CmdBuildClusters = nullptr;

		PFN_vkGetMicromapBuildSizesEXT GetMicromapBuildSizes = nullptr;
		PFN_vkCreateMicromapEXT CreateMicromap = nullptr;
		PFN_vkDestroyMicromapEXT DestroyMicromap = nullptr;
		PFN_vkCmdBuildMicromapsEXT CmdBuildMicromaps = nullptr;

		PFN_vkCreateRayTracingPipelinesKHR CreateRayTracingPipelines = nullptr;
		PFN_vkGetRayTracingShaderGroupHandlesKHR GetRayTracingShaderGroupHandles = nullptr;
		PFN_vkCmdTraceRaysKHR CmdTraceRays = nullptr;

		PFN_vkGetPhysicalDeviceCooperativeVectorPropertiesNV GetCoopVecProperties = nullptr;
		PFN_vkConvertCooperativeVectorMatrixNV ConvertCoopVecMatrix = nullptr;
		PFN_vkCmdConvertCooperativeVectorMatrixNV CmdConvertCoopVecMatrix = nullptr;

		PFN_vkAllocateMemory AllocateMemory = nullptr;
		PFN_vkFreeMemory FreeMemory = nullptr;
		PFN_vkBindBufferMemory BindBufferMemory = nullptr;

		PFN_vkMapMemory MapMemory = nullptr;
		PFN_vkUnmapMemory UnmapMemory = nullptr;
		PFN_vkFlushMappedMemoryRanges FlushMappedMemoryRanges = nullptr;
		PFN_vkInvalidateMappedMemoryRanges InvalidateMappedMemoryRanges = nullptr;

		PFN_vkCreateCommandPool CreateCommandPool = nullptr;

		PFN_vkCreateQueryPool CreateQueryPool = nullptr;
		PFN_vkDestroyQueryPool DestroyQueryPool = nullptr;
		PFN_vkGetQueryPoolResults GetQueryPoolResults = nullptr;
		PFN_vkResetQueryPool ResetQueryPool = nullptr;
		PFN_vkCmdResetQueryPool CmdResetQueryPool = nullptr;
		PFN_vkCmdWriteTimestamp CmdWriteTimestamp = nullptr;

		PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
		PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
		PFN_vkFreeCommandBuffers FreeCommandBuffers = nullptr;
		PFN_vkResetCommandBuffer ResetCommandBuffer = nullptr;
		PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
		PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;

		PFN_vkCmdCopyBuffer CmdCopyBuffer = nullptr;
		PFN_vkCmdFillBuffer CmdFillBuffer = nullptr;
		PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage = nullptr;
		PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer = nullptr;
		PFN_vkCmdCopyImage CmdCopyImage = nullptr;
		PFN_vkCmdResolveImage CmdResolveImage = nullptr;

		PFN_vkCmdClearColorImage CmdClearColorImage = nullptr;
		PFN_vkCmdClearAttachments CmdClearAttachments = nullptr;
		PFN_vkCmdClearDepthStencilImage CmdClearDepthStencilImage = nullptr;

		PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;

		PFN_vkCmdBeginDebugUtilsLabelEXT CmdBeginDebugUtilsLabel = nullptr;
		PFN_vkCmdEndDebugUtilsLabelEXT CmdEndDebugUtilsLabel = nullptr;
		PFN_vkCmdSetCheckpointNV CmdSetCheckpoint = nullptr;

		PFN_vkCreateShaderModule CreateShaderModule = nullptr;
		PFN_vkDestroyShaderModule DestroyShaderModule = nullptr;

		PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
		PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout = nullptr;

		PFN_vkCreatePipelineLayout CreatePipelineLayout = nullptr;
		PFN_vkDestroyPipelineLayout DestroyPipelineLayout = nullptr;

		PFN_vkCreateComputePipelines CreateComputePipelines = nullptr;
		PFN_vkDestroyPipeline DestroyPipeline = nullptr;

		PFN_vkCreateDescriptorPool CreateDescriptorPool = nullptr;
		PFN_vkDestroyDescriptorPool DestroyDescriptorPool = nullptr;
		PFN_vkAllocateDescriptorSets AllocateDescriptorSets = nullptr;
		PFN_vkUpdateDescriptorSets UpdateDescriptorSets = nullptr;

		PFN_vkCmdBindPipeline CmdBindPipeline = nullptr;
		PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = nullptr;

		PFN_vkCmdDispatch CmdDispatch = nullptr;
		PFN_vkCmdDispatchIndirect CmdDispatchIndirect = nullptr;

		PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines = nullptr;

		PFN_vkCreateSampler CreateSampler = nullptr;
		PFN_vkDestroySampler DestroySampler = nullptr;

		PFN_vkCmdBeginRenderPass CmdBeginRenderPass = nullptr;
		PFN_vkCmdBeginRendering CmdBeginRendering = nullptr;
		PFN_vkCmdEndRendering CmdEndRendering = nullptr;
		PFN_vkCmdEndRenderPass CmdEndRenderPass = nullptr;

		PFN_vkCmdSetViewport CmdSetViewport = nullptr;
		PFN_vkCmdSetScissor CmdSetScissor = nullptr;
		PFN_vkCmdSetStencilReference CmdSetStencilReference = nullptr;
		PFN_vkCmdSetBlendConstants CmdSetBlendConstants = nullptr;
		PFN_vkCmdSetFragmentShadingRateKHR CmdSetFragmentShadingRate = nullptr;

		PFN_vkCmdDraw CmdDraw = nullptr;
		PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers = nullptr;
		PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer = nullptr;
		PFN_vkCmdDrawIndexed CmdDrawIndexed = nullptr;
		PFN_vkCmdDrawIndirect CmdDrawIndirect = nullptr;
		PFN_vkCmdDrawIndexedIndirect CmdDrawIndexedIndirect = nullptr;
		PFN_vkCmdDrawIndexedIndirectCount CmdDrawIndexedIndirectCount = nullptr;
		PFN_vkCmdDrawIndirectCount CmdDrawIndirectCount = nullptr;

		PFN_vkCmdPushConstants CmdPushConstants = nullptr;

		PFN_vkCreateSemaphore CreateSemaphore = nullptr;
		PFN_vkCreateFence CreateFence = nullptr;
		PFN_vkDestroyFence DestroyFence = nullptr;
		PFN_vkResetFences ResetFences = nullptr;
		PFN_vkWaitForFences WaitForFences = nullptr;
		PFN_vkDestroySemaphore DestroySemaphore = nullptr;

		PFN_vkCreateSwapchainKHR CreateSwapchain = nullptr;
		PFN_vkDestroySwapchainKHR DestroySwapchain = nullptr;
		PFN_vkGetSwapchainImagesKHR GetSwapchainImages = nullptr;
		PFN_vkAcquireNextImageKHR AcquireNextImage = nullptr;
		PFN_vkQueuePresentKHR QueuePresent = nullptr;

		PFN_vkGetSemaphoreCounterValue GetSemaphoreCounterValue = nullptr;
		PFN_vkWaitSemaphores WaitSemaphores = nullptr;

		PFN_vkQueueSubmit QueueSubmit = nullptr;
		PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
	};
}
