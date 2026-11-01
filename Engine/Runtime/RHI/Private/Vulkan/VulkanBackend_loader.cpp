#include "VulkanBackend.h"

namespace rox::vulkan
{
	[[nodiscard]] bool LoadFunctions(State& state)
	{
#ifdef _WIN32
		state.Library = LoadLibraryW(L"vulkan-1.dll");

		if (!state.Library)
		{
			return false;
		}
		state.F.GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(state.Library, "vkGetInstanceProcAddr"));
#else
		state.Library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);

		if (!state.Library)
		{
			return false;
		}
		state.F.GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(state.Library, "vkGetInstanceProcAddr"));
#endif
		if (!state.F.GetInstanceProcAddr)
		{
			return false;
		}
#define LOAD_GLOBAL(field, name)                                                                                                           \
	do                                                                                                                                     \
	{                                                                                                                                      \
		state.F.field = reinterpret_cast<PFN_vk##name>(state.F.GetInstanceProcAddr(VK_NULL_HANDLE, "vk" #name));                           \
		if (!state.F.field)                                                                                                                \
		{                                                                                                                                  \
			return false;                                                                                                                  \
		}                                                                                                                                  \
	} while (false)
		LOAD_GLOBAL(CreateInstance, CreateInstance);
		LOAD_GLOBAL(EnumerateInstanceExtensions, EnumerateInstanceExtensionProperties);
		LOAD_GLOBAL(EnumerateInstanceLayers, EnumerateInstanceLayerProperties);
#undef LOAD_GLOBAL
		return true;
	}

	[[nodiscard]] bool LoadInstanceFunctions(State& state)
	{
#define LOAD_INSTANCE(field, name)                                                                                                         \
	do                                                                                                                                     \
	{                                                                                                                                      \
		state.F.field = reinterpret_cast<PFN_vk##name>(state.F.GetInstanceProcAddr(state.Instance, "vk" #name));                           \
		if (!state.F.field)                                                                                                                \
		{                                                                                                                                  \
			return false;                                                                                                                  \
		}                                                                                                                                  \
	} while (false)
		LOAD_INSTANCE(DestroyInstance, DestroyInstance);
		LOAD_INSTANCE(EnumeratePhysicalDevices, EnumeratePhysicalDevices);
		LOAD_INSTANCE(EnumerateDeviceExtensions, EnumerateDeviceExtensionProperties);
		LOAD_INSTANCE(GetQueueFamilies, GetPhysicalDeviceQueueFamilyProperties);
		LOAD_INSTANCE(GetMemoryProperties, GetPhysicalDeviceMemoryProperties);
		LOAD_INSTANCE(GetMemoryProperties2, GetPhysicalDeviceMemoryProperties2);
		LOAD_INSTANCE(GetFormatProperties, GetPhysicalDeviceFormatProperties);
		LOAD_INSTANCE(GetImageFormatProperties, GetPhysicalDeviceImageFormatProperties);
		LOAD_INSTANCE(GetPhysicalDeviceProperties, GetPhysicalDeviceProperties);
		LOAD_INSTANCE(GetPhysicalDeviceProperties2, GetPhysicalDeviceProperties2);
		LOAD_INSTANCE(GetPhysicalDeviceFeatures, GetPhysicalDeviceFeatures);
		LOAD_INSTANCE(GetPhysicalDeviceFeatures2, GetPhysicalDeviceFeatures2);
		LOAD_INSTANCE(CreateDevice, CreateDevice);
		LOAD_INSTANCE(GetDeviceProcAddr, GetDeviceProcAddr);
#undef LOAD_INSTANCE
		state.F.GetFragmentShadingRates = reinterpret_cast<PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR>( state.F.GetInstanceProcAddr(state.Instance, "vkGetPhysicalDeviceFragmentShadingRatesKHR"));
		state.F.GetCoopVecProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceCooperativeVectorPropertiesNV>(
			state.F.GetInstanceProcAddr(state.Instance, "vkGetPhysicalDeviceCooperativeVectorPropertiesNV"));

		if (state.bSurfaceEnabled)
		{
#define LOAD_SURFACE(field, name)                                                                                                          \
	do                                                                                                                                     \
	{                                                                                                                                      \
		state.F.field = reinterpret_cast<PFN_vk##name>(state.F.GetInstanceProcAddr(state.Instance, "vk" #name));                           \
		if (!state.F.field)                                                                                                                \
		{                                                                                                                                  \
			return false;                                                                                                                  \
		}                                                                                                                                  \
	} while (false)
			LOAD_SURFACE(DestroySurface, DestroySurfaceKHR);
			LOAD_SURFACE(GetSurfaceSupport, GetPhysicalDeviceSurfaceSupportKHR);
			LOAD_SURFACE(GetSurfaceCapabilities, GetPhysicalDeviceSurfaceCapabilitiesKHR);
			LOAD_SURFACE(GetSurfaceFormats, GetPhysicalDeviceSurfaceFormatsKHR);
			LOAD_SURFACE(GetPresentModes, GetPhysicalDeviceSurfacePresentModesKHR);
#ifdef _WIN32
			LOAD_SURFACE(CreateWin32Surface, CreateWin32SurfaceKHR);
#elif defined(__ANDROID__)
			LOAD_SURFACE(CreateAndroidSurface, CreateAndroidSurfaceKHR);
#elif defined(__linux__)
			if (state.bXlibSurface)
			{
				LOAD_SURFACE(CreateXlibSurface, CreateXlibSurfaceKHR);
			}

			if (state.bWaylandSurface)
			{
				LOAD_SURFACE(CreateWaylandSurface, CreateWaylandSurfaceKHR);
			}
#endif
#undef LOAD_SURFACE
		}

		return true;
	}

	[[nodiscard]] EFormatSupport QueryFormatSupport(const State* opaque, EFormat format) noexcept
	{
		const State& state = *opaque;
		const VkFormat native = ConvertVulkanFormat(format);

		if (native == VK_FORMAT_UNDEFINED)
		{
			return EFormatSupport::None;
		}
		VkFormatProperties properties{};
		state.F.GetFormatProperties(state.Physical, native, &properties);
		const VkFormatFeatureFlags buffer = properties.bufferFeatures;
		const VkFormatFeatureFlags image = properties.optimalTilingFeatures;
		EFormatSupport result = EFormatSupport::None;

		if (buffer)
		{
			result = result | EFormatSupport::Buffer;
		}

		if (buffer & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)
		{
			result = result | EFormatSupport::BufferShaderRead;
		}

		if (buffer & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT)
		{
			result = result | EFormatSupport::BufferShaderLoad | EFormatSupport::BufferShaderStore;
		}

		if (format == EFormat::R16_UINT || format == EFormat::R32_UINT)
		{
			result = result | EFormatSupport::IndexBuffer;
		}

		if (buffer & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
		{
			result = result | EFormatSupport::VertexBuffer;
		}

		if (image)
		{
			result = result | EFormatSupport::Texture;
		}

		if (image & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			result = result | EFormatSupport::DepthStencil;
		}

		if (image & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
		{
			result = result | EFormatSupport::RenderTarget;
		}

		if (image & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
		{
			result = result | EFormatSupport::Blendable;
		}

		if ((image & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) || (buffer & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT))
		{
			result = result | EFormatSupport::ShaderLoad;
		}

		if (image & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
		{
			result = result | EFormatSupport::ShaderSample;
		}

		if (state.bSamplerFilterMinmax && (image & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT))
		{
			result = result | EFormatSupport::SamplerMinMax;
		}

		if ((image & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) || (buffer & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT))
		{
			result = result | EFormatSupport::ShaderUavLoad | EFormatSupport::ShaderUavStore;
		}

		if ((image & VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT) || (buffer & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT))
		{
			result = result | EFormatSupport::ShaderAtomic;
		}

		return result;
	}

	[[nodiscard]] bool LoadDeviceFunctions(State& state)
	{
#define LOAD_DEVICE(field, name)                                                                                                           \
	do                                                                                                                                     \
	{                                                                                                                                      \
		state.F.field = reinterpret_cast<PFN_vk##name>(state.F.GetDeviceProcAddr(state.Device, "vk" #name));                               \
		if (!state.F.field)                                                                                                                \
		{                                                                                                                                  \
			return false;                                                                                                                  \
		}                                                                                                                                  \
	} while (false)
		LOAD_DEVICE(DestroyDevice, DestroyDevice);
		LOAD_DEVICE(GetDeviceQueue, GetDeviceQueue);
		LOAD_DEVICE(CreateBuffer, CreateBuffer);
		LOAD_DEVICE(DestroyBuffer, DestroyBuffer);
		LOAD_DEVICE(CreateImage, CreateImage);
		LOAD_DEVICE(DestroyImage, DestroyImage);
		LOAD_DEVICE(CreateBufferView, CreateBufferView);
		LOAD_DEVICE(DestroyBufferView, DestroyBufferView);
		LOAD_DEVICE(CreateImageView, CreateImageView);
		LOAD_DEVICE(DestroyImageView, DestroyImageView);
		LOAD_DEVICE(CreateFramebuffer, CreateFramebuffer);
		LOAD_DEVICE(DestroyFramebuffer, DestroyFramebuffer);
		LOAD_DEVICE(CreateRenderPass, CreateRenderPass);
		LOAD_DEVICE(CreateRenderPass2, CreateRenderPass2);
		LOAD_DEVICE(DestroyRenderPass, DestroyRenderPass);
		LOAD_DEVICE(GetImageMemoryRequirements, GetImageMemoryRequirements);
		LOAD_DEVICE(GetImageMemoryRequirements2, GetImageMemoryRequirements2);
		LOAD_DEVICE(GetImageSparseMemoryRequirements, GetImageSparseMemoryRequirements);
		LOAD_DEVICE(QueueBindSparse, QueueBindSparse);
		LOAD_DEVICE(BindImageMemory, BindImageMemory);
		LOAD_DEVICE(GetBufferMemoryRequirements, GetBufferMemoryRequirements);
		LOAD_DEVICE(GetBufferMemoryRequirements2, GetBufferMemoryRequirements2);
		state.F.GetBufferDeviceAddress = reinterpret_cast<PFN_vkGetBufferDeviceAddress>(state.F.GetDeviceProcAddr(state.Device, "vkGetBufferDeviceAddress"));

		if (!state.F.GetBufferDeviceAddress)
		{
			state.bBufferDeviceAddress = false;
		}

		if (state.bAftermathCheckpoints)
		{
			state.F.CmdSetCheckpoint = reinterpret_cast<PFN_vkCmdSetCheckpointNV>(state.F.GetDeviceProcAddr(state.Device, "vkCmdSetCheckpointNV"));

			if (!state.F.CmdSetCheckpoint)
			{
				return false;
			}
		}

		if (state.bCooperativeVector)
		{
			state.F.ConvertCoopVecMatrix = reinterpret_cast<PFN_vkConvertCooperativeVectorMatrixNV>( state.F.GetDeviceProcAddr(state.Device, "vkConvertCooperativeVectorMatrixNV"));
			state.F.CmdConvertCoopVecMatrix = reinterpret_cast<PFN_vkCmdConvertCooperativeVectorMatrixNV>( state.F.GetDeviceProcAddr(state.Device, "vkCmdConvertCooperativeVectorMatrixNV"));
			state.bCooperativeVector = state.bBufferDeviceAddress && state.F.GetCoopVecProperties != nullptr && state.F.ConvertCoopVecMatrix != nullptr && state.F.CmdConvertCoopVecMatrix != nullptr;
			state.bCooperativeVectorTraining &= state.bCooperativeVector;
		}

		if (state.bAccelerationStructure)
		{
			state.F.GetAccelerationStructureBuildSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
				state.F.GetDeviceProcAddr(state.Device, "vkGetAccelerationStructureBuildSizesKHR"));
			state.F.CreateAccelerationStructure = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>( state.F.GetDeviceProcAddr(state.Device, "vkCreateAccelerationStructureKHR"));
			state.F.DestroyAccelerationStructure = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>( state.F.GetDeviceProcAddr(state.Device, "vkDestroyAccelerationStructureKHR"));
			state.F.GetAccelerationStructureDeviceAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
				state.F.GetDeviceProcAddr(state.Device, "vkGetAccelerationStructureDeviceAddressKHR"));
			state.F.CmdBuildAccelerationStructures = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>( state.F.GetDeviceProcAddr(state.Device, "vkCmdBuildAccelerationStructuresKHR"));

			if (!state.F.GetAccelerationStructureBuildSizes ||
				!state.F.CreateAccelerationStructure ||
				!state.F.DestroyAccelerationStructure ||
				!state.F.GetAccelerationStructureDeviceAddress ||
				!state.F.CmdBuildAccelerationStructures)
			{
				state.bAccelerationStructure = false;
				state.bRayQuery = false;
				state.bRayTracingPipeline = false;
			}
		}

		if (state.bOpacityMicromap)
		{
			state.F.GetMicromapBuildSizes = reinterpret_cast<PFN_vkGetMicromapBuildSizesEXT>(state.F.GetDeviceProcAddr(state.Device, "vkGetMicromapBuildSizesEXT"));
			state.F.CreateMicromap = reinterpret_cast<PFN_vkCreateMicromapEXT>(state.F.GetDeviceProcAddr(state.Device, "vkCreateMicromapEXT"));
			state.F.DestroyMicromap = reinterpret_cast<PFN_vkDestroyMicromapEXT>(state.F.GetDeviceProcAddr(state.Device, "vkDestroyMicromapEXT"));
			state.F.CmdBuildMicromaps = reinterpret_cast<PFN_vkCmdBuildMicromapsEXT>(state.F.GetDeviceProcAddr(state.Device, "vkCmdBuildMicromapsEXT"));
			state.bOpacityMicromap = state.bAccelerationStructure && state.F.GetMicromapBuildSizes != nullptr &&
				state.F.CreateMicromap != nullptr && state.F.DestroyMicromap != nullptr &&
				state.F.CmdBuildMicromaps != nullptr;
		}

		if (state.bRayTracingClusters)
		{
			state.F.GetClusterBuildSizes = reinterpret_cast<PFN_vkGetClusterAccelerationStructureBuildSizesNV>(
				state.F.GetDeviceProcAddr(state.Device, "vkGetClusterAccelerationStructureBuildSizesNV"));
			state.F.CmdBuildClusters = reinterpret_cast<PFN_vkCmdBuildClusterAccelerationStructureIndirectNV>(
				state.F.GetDeviceProcAddr(state.Device, "vkCmdBuildClusterAccelerationStructureIndirectNV"));
			state.bRayTracingClusters = state.bAccelerationStructure && state.bBufferDeviceAddress && state.F.GetClusterBuildSizes != nullptr && state.F.CmdBuildClusters != nullptr;
		}

		if (state.bRayTracingPipeline)
		{
			state.F.CreateRayTracingPipelines = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>( state.F.GetDeviceProcAddr(state.Device, "vkCreateRayTracingPipelinesKHR"));
			state.F.GetRayTracingShaderGroupHandles = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>( state.F.GetDeviceProcAddr(state.Device, "vkGetRayTracingShaderGroupHandlesKHR"));
			state.F.CmdTraceRays = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(state.F.GetDeviceProcAddr(state.Device, "vkCmdTraceRaysKHR"));
			state.bRayTracingPipeline = state.F.CreateRayTracingPipelines != nullptr &&
				state.F.GetRayTracingShaderGroupHandles != nullptr && state.F.CmdTraceRays != nullptr &&
				state.bAccelerationStructure && state.bBufferDeviceAddress;
		}
		LOAD_DEVICE(AllocateMemory, AllocateMemory);
		LOAD_DEVICE(FreeMemory, FreeMemory);
		LOAD_DEVICE(BindBufferMemory, BindBufferMemory);
		LOAD_DEVICE(MapMemory, MapMemory);
		LOAD_DEVICE(UnmapMemory, UnmapMemory);
		LOAD_DEVICE(FlushMappedMemoryRanges, FlushMappedMemoryRanges);
		LOAD_DEVICE(InvalidateMappedMemoryRanges, InvalidateMappedMemoryRanges);
		LOAD_DEVICE(CreateCommandPool, CreateCommandPool);
		LOAD_DEVICE(CreateQueryPool, CreateQueryPool);
		LOAD_DEVICE(DestroyQueryPool, DestroyQueryPool);
		LOAD_DEVICE(GetQueryPoolResults, GetQueryPoolResults);
		LOAD_DEVICE(CmdResetQueryPool, CmdResetQueryPool);
		LOAD_DEVICE(CmdWriteTimestamp, CmdWriteTimestamp);
		LOAD_DEVICE(DestroyCommandPool, DestroyCommandPool);
		LOAD_DEVICE(AllocateCommandBuffers, AllocateCommandBuffers);
		LOAD_DEVICE(FreeCommandBuffers, FreeCommandBuffers);
		LOAD_DEVICE(ResetCommandBuffer, ResetCommandBuffer);
		LOAD_DEVICE(BeginCommandBuffer, BeginCommandBuffer);
		LOAD_DEVICE(EndCommandBuffer, EndCommandBuffer);
		LOAD_DEVICE(CmdCopyBuffer, CmdCopyBuffer);
		LOAD_DEVICE(CmdFillBuffer, CmdFillBuffer);
		LOAD_DEVICE(CmdCopyBufferToImage, CmdCopyBufferToImage);
		LOAD_DEVICE(CmdCopyImageToBuffer, CmdCopyImageToBuffer);
		LOAD_DEVICE(CmdCopyImage, CmdCopyImage);

		LOAD_DEVICE(CmdResolveImage, CmdResolveImage);
		LOAD_DEVICE(CmdClearColorImage, CmdClearColorImage);
		LOAD_DEVICE(CmdClearAttachments, CmdClearAttachments);
		LOAD_DEVICE(CmdClearDepthStencilImage, CmdClearDepthStencilImage);
		LOAD_DEVICE(CmdPipelineBarrier, CmdPipelineBarrier);

		if (state.bDebugUtilsEnabled)
		{
			state.F.CmdBeginDebugUtilsLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(state.F.GetDeviceProcAddr(state.Device, "vkCmdBeginDebugUtilsLabelEXT"));
			state.F.CmdEndDebugUtilsLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(state.F.GetDeviceProcAddr(state.Device, "vkCmdEndDebugUtilsLabelEXT"));
			state.bDebugUtilsEnabled = state.F.CmdBeginDebugUtilsLabel != nullptr && state.F.CmdEndDebugUtilsLabel != nullptr;
		}
		LOAD_DEVICE(CreateShaderModule, CreateShaderModule);
		LOAD_DEVICE(DestroyShaderModule, DestroyShaderModule);
		LOAD_DEVICE(CreateDescriptorSetLayout, CreateDescriptorSetLayout);
		LOAD_DEVICE(DestroyDescriptorSetLayout, DestroyDescriptorSetLayout);
		LOAD_DEVICE(CreatePipelineLayout, CreatePipelineLayout);
		LOAD_DEVICE(DestroyPipelineLayout, DestroyPipelineLayout);
		LOAD_DEVICE(CreateComputePipelines, CreateComputePipelines);
		LOAD_DEVICE(DestroyPipeline, DestroyPipeline);
		LOAD_DEVICE(CreateDescriptorPool, CreateDescriptorPool);
		LOAD_DEVICE(DestroyDescriptorPool, DestroyDescriptorPool);
		LOAD_DEVICE(AllocateDescriptorSets, AllocateDescriptorSets);
		LOAD_DEVICE(UpdateDescriptorSets, UpdateDescriptorSets);
		LOAD_DEVICE(CmdBindPipeline, CmdBindPipeline);
		LOAD_DEVICE(CmdBindDescriptorSets, CmdBindDescriptorSets);
		LOAD_DEVICE(CmdDispatch, CmdDispatch);
		LOAD_DEVICE(CmdDispatchIndirect, CmdDispatchIndirect);
		LOAD_DEVICE(CreateGraphicsPipelines, CreateGraphicsPipelines);
		LOAD_DEVICE(CreateSampler, CreateSampler);
		LOAD_DEVICE(DestroySampler, DestroySampler);
		LOAD_DEVICE(CmdBeginRenderPass, CmdBeginRenderPass);
		state.F.CmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(state.F.GetDeviceProcAddr(state.Device, "vkCmdBeginRendering"));
		state.F.CmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(state.F.GetDeviceProcAddr(state.Device, "vkCmdEndRendering"));
		if (!state.F.CmdBeginRendering)
		{
			state.F.CmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(state.F.GetDeviceProcAddr(state.Device, "vkCmdBeginRenderingKHR"));
		}
		if (!state.F.CmdEndRendering)
		{
			state.F.CmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(state.F.GetDeviceProcAddr(state.Device, "vkCmdEndRenderingKHR"));
		}
		LOAD_DEVICE(CmdEndRenderPass, CmdEndRenderPass);
		LOAD_DEVICE(CmdSetViewport, CmdSetViewport);
		LOAD_DEVICE(CmdSetScissor, CmdSetScissor);
		LOAD_DEVICE(CmdSetStencilReference, CmdSetStencilReference);
		LOAD_DEVICE(CmdSetBlendConstants, CmdSetBlendConstants);
		LOAD_DEVICE(CmdDraw, CmdDraw);
		LOAD_DEVICE(CmdBindVertexBuffers, CmdBindVertexBuffers);
		LOAD_DEVICE(CmdBindIndexBuffer, CmdBindIndexBuffer);
		LOAD_DEVICE(CmdDrawIndexed, CmdDrawIndexed);
		LOAD_DEVICE(CmdDrawIndirect, CmdDrawIndirect);
		LOAD_DEVICE(CmdDrawIndexedIndirect, CmdDrawIndexedIndirect);

		if (state.bDrawIndexedIndirectCount)
		{
			state.F.CmdDrawIndexedIndirectCount = reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCount>( state.F.GetDeviceProcAddr(state.Device, "vkCmdDrawIndexedIndirectCount"));
			state.bDrawIndexedIndirectCount = state.F.CmdDrawIndexedIndirectCount != nullptr;
		}

		if (state.bDrawIndirectCount)
		{
			state.F.CmdDrawIndirectCount = reinterpret_cast<PFN_vkCmdDrawIndirectCount>(state.F.GetDeviceProcAddr(state.Device, "vkCmdDrawIndirectCount"));
			state.bDrawIndirectCount = state.F.CmdDrawIndirectCount != nullptr;
		}
		LOAD_DEVICE(CmdPushConstants, CmdPushConstants);
		LOAD_DEVICE(CreateFence, CreateFence);
		LOAD_DEVICE(DestroyFence, DestroyFence);
		LOAD_DEVICE(ResetFences, ResetFences);
		LOAD_DEVICE(WaitForFences, WaitForFences);
		LOAD_DEVICE(CreateSemaphore, CreateSemaphore);
		LOAD_DEVICE(DestroySemaphore, DestroySemaphore);
		LOAD_DEVICE(GetSemaphoreCounterValue, GetSemaphoreCounterValue);
		LOAD_DEVICE(WaitSemaphores, WaitSemaphores);
		LOAD_DEVICE(QueueSubmit, QueueSubmit);
		LOAD_DEVICE(DeviceWaitIdle, DeviceWaitIdle);
#undef LOAD_DEVICE
		if (state.bHdrMetadata)
		{
			state.F.SetHdrMetadata = reinterpret_cast<PFN_vkSetHdrMetadataEXT>(state.F.GetDeviceProcAddr(state.Device, "vkSetHdrMetadataEXT"));
		}

		if (state.bSwapchainEnabled)
		{
#define LOAD_SWAPCHAIN(field, name)                                                                                                        \
	do                                                                                                                                     \
	{                                                                                                                                      \
		state.F.field = reinterpret_cast<PFN_vk##name>(state.F.GetDeviceProcAddr(state.Device, "vk" #name));                               \
		if (!state.F.field)                                                                                                                \
		{                                                                                                                                  \
			return false;                                                                                                                  \
		}                                                                                                                                  \
	} while (false)
			LOAD_SWAPCHAIN(CreateSwapchain, CreateSwapchainKHR);
			LOAD_SWAPCHAIN(DestroySwapchain, DestroySwapchainKHR);
			LOAD_SWAPCHAIN(GetSwapchainImages, GetSwapchainImagesKHR);
			LOAD_SWAPCHAIN(AcquireNextImage, AcquireNextImageKHR);
			LOAD_SWAPCHAIN(QueuePresent, QueuePresentKHR);
#undef LOAD_SWAPCHAIN
		}

		return true;
	}
}
