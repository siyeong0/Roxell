#include "VulkanDescriptors.h"

namespace rox::vulkan
{
	using namespace core;

	VKAPI_ATTR VkBool32 VKAPI_CALL ValidationMessage(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT,
		const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
		void*) noexcept
	{
		const StringView message = callbackData != nullptr && callbackData->pMessage != nullptr
			? StringView(callbackData->pMessage, CStringLength(callbackData->pMessage))
			: StringView("Vulkan validation emitted an empty message.");

		if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
		{
			LOG_ERROR("Rhi.Vulkan.Validation", message);
		}
		else
		{
			LOG_WARNING("Rhi.Vulkan.Validation", message);
		}

		return VK_FALSE;
	}

	[[nodiscard]] uint32 AdapterRank(VkPhysicalDeviceType type, EAdapterPreference preference) noexcept
	{
		if (preference == EAdapterPreference::Default)
		{
			return 0;
		}

		if (preference == EAdapterPreference::Software)
		{
			return type == VK_PHYSICAL_DEVICE_TYPE_CPU ? 0u : UINT32_MAX;
		}

		if (type == VK_PHYSICAL_DEVICE_TYPE_CPU)
		{
			return UINT32_MAX;
		}

		if (preference == EAdapterPreference::HighPerformance)
		{
			if (type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				return 0;
			}

			if (type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
			{
				return 1;
			}
		}
		else
		{
			if (type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
			{
				return 0;
			}

			if (type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				return 1;
			}
		}

		return type == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2u : 3u;
	}

	Result<BackendData> CreateVulkanBackend(
		MemoryResource& resource,
		bool bEnableValidation,
		EAdapterPreference preference,
		NativeDeviceHandles externalDevice,
		NativeQueueHandle externalComputeQueue,
		NativeQueueHandle externalCopyQueue,
		ExternalVulkanFeatures enabledFeatures,
		IGpuCrashDiagnostics* diagnostics)
	{
		if (diagnostics != nullptr && externalDevice.Device != nullptr)
		{
			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.AftermathVulkan", "GPU diagnostics require an engine-created Vulkan device.");
		}

		if (externalDevice.Device != nullptr)
		{
			return CreateExternalVulkanBackend(resource, externalDevice, externalComputeQueue, externalCopyQueue, enabledFeatures);
		}
		State* state = CreateObject<State>(resource, resource);

		if (!state)
		{
			return Result<BackendData>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan state."));
		}

		const AftermathVulkanOptions aftermath = GetAftermathVulkanOptions(diagnostics);
		const bool bNeedsDiagnosticsConfig = aftermath.bResourceTracking || aftermath.bShaderDebugInfo || aftermath.bShaderErrorReporting;

		if (!LoadFunctions(*state))
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.VulkanLoader", "Could not load the Vulkan runtime."));
		}

		if (bEnableValidation)
		{
			uint32 layerCount = 0;

			if (state->F.EnumerateInstanceLayers(&layerCount, nullptr) != VK_SUCCESS)
			{
				Destroy(state);

				return Result<BackendData>::Failure(Failure("Rhi.VulkanValidation", "Could not enumerate Vulkan instance layers."));
			}

			Array<VkLayerProperties> layers(resource);

			if (!layers.ResizeUninitialized(layerCount) || state->F.EnumerateInstanceLayers(&layerCount, layers.Data()) != VK_SUCCESS)
			{
				Destroy(state);

				return Result<BackendData>::Failure(Failure("Rhi.VulkanValidation", "Could not read Vulkan instance layers."));
			}
			bool bAvailable = false;

			for (const VkLayerProperties& layer : layers)
			{
				bAvailable |= CompareCString(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
			}

			if (!bAvailable)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanValidation", "The requested Vulkan validation layer is unavailable.");
			}
		}
		VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.pApplicationName = "Runtime-RHI";
		app.apiVersion = VK_API_VERSION_1_2;
		const PFN_vkEnumerateInstanceVersion enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(state->F.GetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
		uint32 loaderVersion = VK_API_VERSION_1_0;

		if (enumerateVersion && enumerateVersion(&loaderVersion) == VK_SUCCESS && loaderVersion >= VK_API_VERSION_1_3)
		{
			app.apiVersion = VK_API_VERSION_1_3;
		}
		state->ApiVersion = app.apiVersion;
		VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instanceInfo.pApplicationInfo = &app;
		uint32 instanceExtensionCount = 0;

		if (state->F.EnumerateInstanceExtensions(nullptr, &instanceExtensionCount, nullptr) == VK_SUCCESS)
		{
			Array<VkExtensionProperties> available(resource);

			if (available.ResizeUninitialized(instanceExtensionCount) && state->F.EnumerateInstanceExtensions(nullptr, &instanceExtensionCount, available.Data()) == VK_SUCCESS)
			{
				bool bSurface = false;
				bool bNativeSurface = false;

				for (const VkExtensionProperties& extension : available)
				{
					state->bDebugUtilsEnabled |= CompareCString(extension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
					bSurface |= CompareCString(extension.extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0;
					state->bExtendedColorSpace |= CompareCString(extension.extensionName, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME) == 0;
					state->bSurfaceMaintenance |= CompareCString(extension.extensionName, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME) == 0;
					state->bSurfaceCapabilities2 |= CompareCString(extension.extensionName, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) == 0;
#ifdef _WIN32
					bNativeSurface |= CompareCString(extension.extensionName, VK_KHR_WIN32_SURFACE_EXTENSION_NAME) == 0;
#elif defined(__ANDROID__)
					nativeSurface |= CompareCString(extension.extensionName, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME) == 0;
#elif defined(__linux__)
					state->bXlibSurface |= CompareCString(extension.extensionName, VK_KHR_XLIB_SURFACE_EXTENSION_NAME) == 0;
					state->bWaylandSurface |= CompareCString(extension.extensionName, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME) == 0;
					nativeSurface = state->bXlibSurface || state->bWaylandSurface;
#endif
				}
				state->bSurfaceEnabled = bSurface && bNativeSurface;
			}
		}
		state->bSurfaceMaintenance = state->bSurfaceMaintenance && state->bSurfaceCapabilities2 && state->bSurfaceEnabled;
		const char* instanceExtensions[7]{};
		if (state->bSurfaceEnabled)
		{
			instanceExtensions[instanceInfo.enabledExtensionCount++] = VK_KHR_SURFACE_EXTENSION_NAME;
#ifdef _WIN32
			instanceExtensions[instanceInfo.enabledExtensionCount++] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#elif defined(__ANDROID__)
			instanceExtensions[instanceInfo.enabledExtensionCount++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
#elif defined(__linux__)
			if (state->bXlibSurface)
			{
				instanceExtensions[instanceInfo.enabledExtensionCount++] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
			}

			if (state->bWaylandSurface)
			{
				instanceExtensions[instanceInfo.enabledExtensionCount++] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
			}
#endif
			if (state->bSurfaceMaintenance)
			{
				instanceExtensions[instanceInfo.enabledExtensionCount++] = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
				instanceExtensions[instanceInfo.enabledExtensionCount++] = VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
			}

			if (state->bExtendedColorSpace)
			{
				instanceExtensions[instanceInfo.enabledExtensionCount++] = VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME;
			}
		}

		if (state->bDebugUtilsEnabled)
		{
			instanceExtensions[instanceInfo.enabledExtensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		}

		if (bEnableValidation && !state->bDebugUtilsEnabled)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanValidation", "The Vulkan debug utility extension is unavailable.");
		}

		const char* validationLayer = "VK_LAYER_KHRONOS_validation";
		VkDebugUtilsMessengerCreateInfoEXT debugInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		if (bEnableValidation)
		{
			debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			debugInfo.pfnUserCallback = ValidationMessage;
			instanceInfo.pNext = &debugInfo;
			instanceInfo.enabledLayerCount = 1;
			instanceInfo.ppEnabledLayerNames = &validationLayer;
		}
		instanceInfo.ppEnabledExtensionNames = instanceExtensions;
		VkResult instanceResult = state->F.CreateInstance(&instanceInfo, nullptr, &state->Instance);

		if (instanceResult != VK_SUCCESS && state->bDebugUtilsEnabled && !bEnableValidation)
		{
			state->bDebugUtilsEnabled = false;
			--instanceInfo.enabledExtensionCount;
			instanceResult = state->F.CreateInstance(&instanceInfo, nullptr, &state->Instance);
		}

		if (instanceResult != VK_SUCCESS && state->bSurfaceEnabled && !bEnableValidation)
		{
			state->bSurfaceEnabled = false;
			instanceInfo.enabledExtensionCount = 0;
			instanceInfo.ppEnabledExtensionNames = nullptr;
			instanceResult = state->F.CreateInstance(&instanceInfo, nullptr, &state->Instance);
		}

		if (instanceResult != VK_SUCCESS || !LoadInstanceFunctions(*state))
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.VulkanInstance", "Could not create a Vulkan 1.2 instance."));
		}

		if (bEnableValidation)
		{
			state->F.CreateDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>( state->F.GetInstanceProcAddr(state->Instance, "vkCreateDebugUtilsMessengerEXT"));
			state->F.DestroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>( state->F.GetInstanceProcAddr(state->Instance, "vkDestroyDebugUtilsMessengerEXT"));

			if (state->F.CreateDebugMessenger == nullptr ||
				state->F.DestroyDebugMessenger == nullptr ||
				state->F.CreateDebugMessenger(state->Instance, &debugInfo, nullptr, &state->DebugMessenger) != VK_SUCCESS)
			{
				Destroy(state);

				return Result<BackendData>::Failure(Failure("Rhi.VulkanValidation", "Could not create the Vulkan validation messenger."));
			}
		}

		uint32 count = 0;

		if (state->F.EnumeratePhysicalDevices(state->Instance, &count, nullptr) != VK_SUCCESS || count == 0)
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.VulkanDevice", "No Vulkan physical device exists."));
		}

		Array<VkPhysicalDevice> physical(resource);

		if (!physical.ResizeUninitialized(count) || state->F.EnumeratePhysicalDevices(state->Instance, &count, physical.Data()) != VK_SUCCESS)
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.VulkanDevice", "Could not enumerate Vulkan devices."));
		}

		if (preference != EAdapterPreference::Default)
		{
			const auto rank = [&](VkPhysicalDevice device) noexcept
				{
					VkPhysicalDeviceProperties properties{};
					state->F.GetPhysicalDeviceProperties(device, &properties);

					return AdapterRank(properties.deviceType, preference);
				};
			for (usize index = 1; index < physical.Size(); ++index)
			{
				const VkPhysicalDevice device = physical[index];
				const uint32 deviceRank = rank(device);
				usize position = index;

				while (position != 0 && rank(physical[position - 1]) > deviceRank)
				{
					physical[position] = physical[position - 1];
					--position;
				}
				physical[position] = device;
			}
		}

		uint32 family = UINT32_MAX;

		for (VkPhysicalDevice candidate : physical)
		{
			VkPhysicalDeviceProperties candidateProperties{};
			state->F.GetPhysicalDeviceProperties(candidate, &candidateProperties);

			if (AdapterRank(candidateProperties.deviceType, preference) == UINT32_MAX)
			{
				continue;
			}

			uint32 familyCount = 0;
			state->F.GetQueueFamilies(candidate, &familyCount, nullptr);
			Array<VkQueueFamilyProperties> families(resource);

			if (!families.ResizeUninitialized(familyCount))
			{
				continue;
			}
			state->F.GetQueueFamilies(candidate, &familyCount, families.Data());

			for (uint32 index = 0; index < familyCount; ++index)
			{
				if (!families[index].queueCount || (families[index].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
				{
					continue;
				}
				VkPhysicalDeviceVulkan11Features supported11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
				VkPhysicalDeviceVulkan12Features supported12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
				VkPhysicalDeviceFeatures2 supported2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
				supported2.pNext = &supported11;
				supported11.pNext = &supported12;
				state->F.GetPhysicalDeviceFeatures2(candidate, &supported2);

				if (!supported12.timelineSemaphore)
				{
					continue;
				}
				VkPhysicalDeviceVulkan11Features enabled11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
				VkPhysicalDeviceVulkan12Features enabled12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
				enabled11.shaderDrawParameters = supported11.shaderDrawParameters;
				enabled11.pNext = &enabled12;
				enabled12.timelineSemaphore = VK_TRUE;
				enabled12.hostQueryReset = supported12.hostQueryReset;
				enabled12.drawIndirectCount = supported12.drawIndirectCount;
				enabled12.bufferDeviceAddress = supported12.bufferDeviceAddress;
				enabled12.shaderFloat16 = supported12.shaderFloat16;
				enabled12.shaderInt8 = supported12.shaderInt8;
				// Ordinary shader atomics use Device scope. Enabling the Vulkan memory
				// model without that companion feature makes those shaders invalid.
				enabled12.vulkanMemoryModel = supported12.vulkanMemoryModel && supported12.vulkanMemoryModelDeviceScope;
				enabled12.vulkanMemoryModelDeviceScope = enabled12.vulkanMemoryModel;
				enabled12.samplerFilterMinmax = supported12.samplerFilterMinmax;
				const bool bBindlessSampledTextures = supported12.runtimeDescriptorArray && supported12.descriptorBindingPartiallyBound && supported12.shaderSampledImageArrayNonUniformIndexing;
				const bool bBindlessSamplers = bBindlessSampledTextures;
				const bool bBindlessTypedBuffers = supported12.runtimeDescriptorArray &&
					supported12.descriptorBindingPartiallyBound &&
					supported12.shaderUniformTexelBufferArrayNonUniformIndexing &&
					supported12.shaderStorageTexelBufferArrayNonUniformIndexing;
				const bool bBindlessStorageBuffers = supported12.runtimeDescriptorArray && supported12.descriptorBindingPartiallyBound && supported12.shaderStorageBufferArrayNonUniformIndexing;
				const bool bBindlessStorageTextures = supported12.runtimeDescriptorArray && supported12.descriptorBindingPartiallyBound && supported12.shaderStorageImageArrayNonUniformIndexing;
				const bool bBindlessUniformBuffers = supported12.runtimeDescriptorArray && supported12.descriptorBindingPartiallyBound && supported12.shaderUniformBufferArrayNonUniformIndexing;

				if (bBindlessTypedBuffers || bBindlessSampledTextures || bBindlessStorageBuffers || bBindlessStorageTextures || bBindlessUniformBuffers)
				{
					enabled12.shaderUniformTexelBufferArrayNonUniformIndexing = bBindlessTypedBuffers ? VK_TRUE : VK_FALSE;
					enabled12.shaderStorageTexelBufferArrayNonUniformIndexing = bBindlessTypedBuffers ? VK_TRUE : VK_FALSE;
					enabled12.runtimeDescriptorArray = VK_TRUE;
					enabled12.descriptorBindingPartiallyBound = VK_TRUE;
					enabled12.shaderSampledImageArrayNonUniformIndexing = bBindlessSampledTextures ? VK_TRUE : VK_FALSE;
					enabled12.shaderStorageBufferArrayNonUniformIndexing = bBindlessStorageBuffers ? VK_TRUE : VK_FALSE;
					enabled12.shaderStorageImageArrayNonUniformIndexing = bBindlessStorageTextures ? VK_TRUE : VK_FALSE;
					enabled12.shaderUniformBufferArrayNonUniformIndexing = bBindlessUniformBuffers ? VK_TRUE : VK_FALSE;
				}
				VkPhysicalDeviceFeatures supportedFeatures{};
				supportedFeatures = supported2.features;
				VkPhysicalDeviceFeatures deviceFeatures{};
				deviceFeatures.imageCubeArray = supportedFeatures.imageCubeArray;
				deviceFeatures.shaderStorageImageExtendedFormats = supportedFeatures.shaderStorageImageExtendedFormats;
				deviceFeatures.shaderStorageImageReadWithoutFormat = supportedFeatures.shaderStorageImageReadWithoutFormat;
				deviceFeatures.shaderStorageImageWriteWithoutFormat = supportedFeatures.shaderStorageImageWriteWithoutFormat;
				deviceFeatures.geometryShader = supportedFeatures.geometryShader;
				deviceFeatures.sampleRateShading = supportedFeatures.sampleRateShading;
				deviceFeatures.shaderImageGatherExtended = supportedFeatures.shaderImageGatherExtended;
				deviceFeatures.tessellationShader = supportedFeatures.tessellationShader;
				deviceFeatures.fillModeNonSolid = supportedFeatures.fillModeNonSolid;
				deviceFeatures.depthClamp = supportedFeatures.depthClamp;
				deviceFeatures.depthBounds = supportedFeatures.depthBounds;
				deviceFeatures.depthBiasClamp = supportedFeatures.depthBiasClamp;
				deviceFeatures.multiViewport = supportedFeatures.multiViewport;
				deviceFeatures.dualSrcBlend = supportedFeatures.dualSrcBlend;
				deviceFeatures.independentBlend = supportedFeatures.independentBlend;
				deviceFeatures.multiDrawIndirect = supportedFeatures.multiDrawIndirect;
				deviceFeatures.drawIndirectFirstInstance = supportedFeatures.drawIndirectFirstInstance;
				deviceFeatures.shaderUniformBufferArrayDynamicIndexing = supportedFeatures.shaderUniformBufferArrayDynamicIndexing;
				deviceFeatures.shaderSampledImageArrayDynamicIndexing = supportedFeatures.shaderSampledImageArrayDynamicIndexing;
				deviceFeatures.shaderStorageBufferArrayDynamicIndexing = supportedFeatures.shaderStorageBufferArrayDynamicIndexing;
				deviceFeatures.shaderStorageImageArrayDynamicIndexing = supportedFeatures.shaderStorageImageArrayDynamicIndexing;
				deviceFeatures.shaderInt64 = supportedFeatures.shaderInt64;
				const bool bSparseResidency = (families[index].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) != 0 &&
					supportedFeatures.sparseBinding &&
					supportedFeatures.sparseResidencyImage2D &&
					supportedFeatures.sparseResidencyAliased;

				if (bSparseResidency)
				{
					deviceFeatures.sparseBinding = VK_TRUE;
					deviceFeatures.sparseResidencyImage2D = VK_TRUE;
					deviceFeatures.sparseResidencyAliased = VK_TRUE;
				}

				if (supportedFeatures.samplerAnisotropy)
				{
					deviceFeatures.samplerAnisotropy = VK_TRUE;
				}

				const bool bGraphicsStorageWrites = supportedFeatures.vertexPipelineStoresAndAtomics && supportedFeatures.fragmentStoresAndAtomics;

				if (bGraphicsStorageWrites)
				{
					deviceFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
					deviceFeatures.fragmentStoresAndAtomics = VK_TRUE;
				}

				uint32 computeFamily = index;
				uint32 copyFamily = index;

				for (uint32 other = 0; other < familyCount; ++other)
				{
					VkQueueFamilyProperties const& candidateFamily = families[other];

					if (candidateFamily.queueCount == 0 || (bSparseResidency && (candidateFamily.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) == 0))
					{
						continue;
					}

					if (computeFamily == index && (candidateFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 && (candidateFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
					{
						computeFamily = other;
					}

					if (copyFamily == index && (candidateFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0 && (candidateFamily.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0)
					{
						copyFamily = other;
					}
				}
				float priorities[3]{ 1.0f, 1.0f, 1.0f };
				uint32 graphicsQueueCount = 1;
				uint32 computeQueueIndex = 0;
				uint32 copyQueueIndex = 0;

				if (computeFamily == index && families[index].queueCount > graphicsQueueCount)
				{
					computeQueueIndex = graphicsQueueCount++;
				}

				if (copyFamily == index && families[index].queueCount > graphicsQueueCount)
				{
					copyQueueIndex = graphicsQueueCount++;
				}
				VkDeviceQueueCreateInfo queueInfos[3]{};
				queueInfos[0] = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
				queueInfos[0].queueFamilyIndex = index;
				queueInfos[0].queueCount = graphicsQueueCount;
				queueInfos[0].pQueuePriorities = priorities;
				uint32 queueInfoCount = 1;

				if (computeFamily != index)
				{
					queueInfos[queueInfoCount] = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
					queueInfos[queueInfoCount].queueFamilyIndex = computeFamily;
					queueInfos[queueInfoCount].queueCount = 1;
					queueInfos[queueInfoCount++].pQueuePriorities = priorities;
				}

				if (copyFamily != index && copyFamily != computeFamily)
				{
					queueInfos[queueInfoCount] = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
					queueInfos[queueInfoCount].queueFamilyIndex = copyFamily;
					queueInfos[queueInfoCount].queueCount = 1;
					queueInfos[queueInfoCount++].pQueuePriorities = priorities;
				}
				VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
				deviceInfo.pNext = &enabled11;
				deviceInfo.pEnabledFeatures = &deviceFeatures;
				deviceInfo.queueCreateInfoCount = queueInfoCount;
				deviceInfo.pQueueCreateInfos = queueInfos;
				bool bSupportsSwapchain = false;
				bool bSupportsSwapchainMaintenance = false;
				bool bSupportsExternalMemory = false;
				bool bSupportsExternalSemaphore = false;
				bool bSupportsMemoryBudget = false;
				bool bSupportsShaderDemote = false;
				bool bSupportsLoadStoreOpNone = false;
				bool bSupportsMaintenance7 = false;
				bool bSupportsDynamicRendering = false;
				bool bSupportsMaintenance4 = false;
				bool bSupportsHdrMetadata = false;
				bool bSupportsConservativeRaster = false;
				bool bSupportsShadingRate = false;
				bool bSupportsMeshShader = false;
				bool bSupportsDeferredHostOperations = false;
				bool bSupportsAccelerationStructure = false;
				bool bSupportsRayQuery = false;
				bool bSupportsRayTracingPipeline = false;
				bool bSupportsOpacityMicromap = false;
				bool bSupportsSynchronization2 = false;
				bool bSupportsClusters = false;
				bool bSupportsSliced3DViews = false;
				bool bSupportsCoopVec = false;
				bool bSupportsDiagnosticsConfig = false;
				bool bSupportsDiagnosticCheckpoints = false;
				uint32 extensionCount = 0;

				if (state->F.EnumerateDeviceExtensions(candidate, nullptr, &extensionCount, nullptr) == VK_SUCCESS)
				{
					Array<VkExtensionProperties> available(resource);

					if (available.ResizeUninitialized(extensionCount) && state->F.EnumerateDeviceExtensions(candidate, nullptr, &extensionCount, available.Data()) == VK_SUCCESS)
					{
						for (const VkExtensionProperties& extension : available)
						{
							bSupportsMaintenance4 |= CompareCString(extension.extensionName, VK_KHR_MAINTENANCE_4_EXTENSION_NAME) == 0;
							bSupportsDynamicRendering |= CompareCString(extension.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0;
							bSupportsMaintenance7 |= CompareCString(extension.extensionName, VK_KHR_MAINTENANCE_7_EXTENSION_NAME) == 0;
							bSupportsLoadStoreOpNone |= CompareCString(extension.extensionName, VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME) == 0;
							bSupportsMemoryBudget |= CompareCString(extension.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0;
							bSupportsShaderDemote |= CompareCString(extension.extensionName, VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME) == 0;
#ifdef _WIN32
							bSupportsExternalMemory |= CompareCString(extension.extensionName, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) == 0;
							bSupportsExternalSemaphore |= CompareCString(extension.extensionName, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME) == 0;
#else
							supportsExternalMemory |= CompareCString(extension.extensionName, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) == 0;
							supportsExternalSemaphore |= CompareCString(extension.extensionName, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME) == 0;
#endif
							bSupportsSwapchain |= state->bSurfaceEnabled && CompareCString(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
							bSupportsHdrMetadata |= CompareCString(extension.extensionName, VK_EXT_HDR_METADATA_EXTENSION_NAME) == 0;
							bSupportsSwapchainMaintenance |= state->bSurfaceMaintenance && CompareCString(extension.extensionName, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) == 0;
							bSupportsConservativeRaster |= CompareCString(extension.extensionName, VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME) == 0;
							bSupportsShadingRate |= CompareCString(extension.extensionName, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME) == 0;
							bSupportsMeshShader |= CompareCString(extension.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0;
							bSupportsDeferredHostOperations |= CompareCString(extension.extensionName, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0;
							bSupportsAccelerationStructure |= CompareCString(extension.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0;
							bSupportsRayQuery |= CompareCString(extension.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0;
							bSupportsRayTracingPipeline |= CompareCString(extension.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0;
							bSupportsOpacityMicromap |= CompareCString(extension.extensionName, VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME) == 0;
							bSupportsSynchronization2 |= CompareCString(extension.extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0;
							bSupportsClusters |= CompareCString(extension.extensionName, VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0;
							bSupportsSliced3DViews |= CompareCString(extension.extensionName, VK_EXT_IMAGE_SLICED_VIEW_OF_3D_EXTENSION_NAME) == 0;
							bSupportsCoopVec |= CompareCString(extension.extensionName, VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME) == 0;
							bSupportsDiagnosticsConfig |= CompareCString(extension.extensionName, VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME) == 0;
							bSupportsDiagnosticCheckpoints |= CompareCString(extension.extensionName, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME) == 0;
						}
					}
				}

				const bool bCoreShaderDemote = state->ApiVersion >= VK_API_VERSION_1_3 && candidateProperties.apiVersion >= VK_API_VERSION_1_3;
				VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures supportedDemote{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES };
				if (bCoreShaderDemote || bSupportsShaderDemote)
				{
					VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					features.pNext = &supportedDemote;
					state->F.GetPhysicalDeviceFeatures2(candidate, &features);
				}
				const bool bAvailableShaderDemote = supportedDemote.shaderDemoteToHelperInvocation == VK_TRUE;
				VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures enabledDemote{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES };
				enabledDemote.shaderDemoteToHelperInvocation = bAvailableShaderDemote ? VK_TRUE : VK_FALSE;
				enabledDemote.pNext = &enabled12;
				enabled11.pNext = bAvailableShaderDemote ? static_cast<void*>(&enabledDemote) : static_cast<void*>(&enabled12);
				VkPhysicalDeviceMaintenance7FeaturesKHR maintenance7{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_FEATURES_KHR };
				VkPhysicalDeviceMaintenance7PropertiesKHR maintenance7Properties{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_PROPERTIES_KHR };
				if (bSupportsMaintenance7)
				{
					VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					features.pNext = &maintenance7;
					state->F.GetPhysicalDeviceFeatures2(candidate, &features);
					VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
					properties.pNext = &maintenance7Properties;
					state->F.GetPhysicalDeviceProperties2(candidate, &properties);
				}
				const bool bAvailableMaintenance7 = bSupportsMaintenance7 && maintenance7.maintenance7;
				if (bAvailableMaintenance7)
				{
					maintenance7.pNext = const_cast<void*>(enabled11.pNext);
					enabled11.pNext = &maintenance7;
				}
				const bool bCoreMaintenance4 = state->ApiVersion >= VK_API_VERSION_1_3 && candidateProperties.apiVersion >= VK_API_VERSION_1_3;
				VkPhysicalDeviceMaintenance4Features maintenance4{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES };
				if (bCoreMaintenance4 || bSupportsMaintenance4)
				{
					VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					features.pNext = &maintenance4;
					state->F.GetPhysicalDeviceFeatures2(candidate, &features);
				}
				const bool bAvailableMaintenance4 = maintenance4.maintenance4;
				if (bAvailableMaintenance4)
				{
					maintenance4.pNext = const_cast<void*>(enabled11.pNext);
					enabled11.pNext = &maintenance4;
				}
				const bool bCoreDynamicRendering = state->ApiVersion >= VK_API_VERSION_1_3 && candidateProperties.apiVersion >= VK_API_VERSION_1_3;
				VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
				if (bCoreDynamicRendering || bSupportsDynamicRendering)
				{
					VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					features.pNext = &dynamicRendering;
					state->F.GetPhysicalDeviceFeatures2(candidate, &features);
				}
				const bool bAvailableDynamicRendering = dynamicRendering.dynamicRendering;
				if (bAvailableDynamicRendering)
				{
					dynamicRendering.pNext = const_cast<void*>(enabled11.pNext);
					enabled11.pNext = &dynamicRendering;
				}
				const bool bAvailableSwapchain = bSupportsSwapchain;
				VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT supportedMaintenance{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT };
				if (bSupportsSwapchainMaintenance)
				{
					VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					features.pNext = &supportedMaintenance;
					state->F.GetPhysicalDeviceFeatures2(candidate, &features);
				}

				const bool bAvailableMaintenance = bSupportsSwapchainMaintenance && supportedMaintenance.swapchainMaintenance1;

				if ((bNeedsDiagnosticsConfig && !bSupportsDiagnosticsConfig) || (aftermath.bCheckpoints && !bSupportsDiagnosticCheckpoints))
				{
					continue;
				}

				if (bNeedsDiagnosticsConfig)
				{
					VkPhysicalDeviceDiagnosticsConfigFeaturesNV supportedDiagnostics{
						VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV };
					VkPhysicalDeviceFeatures2 diagnosticFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					diagnosticFeatures.pNext = &supportedDiagnostics;
					state->F.GetPhysicalDeviceFeatures2(candidate, &diagnosticFeatures);

					if (!supportedDiagnostics.diagnosticsConfig)
					{
						continue;
					}
				}

				const bool bAvailableConservativeRaster = bSupportsConservativeRaster;
				VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT supportedSliced3DViews{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_SLICED_VIEW_OF_3D_FEATURES_EXT };
				if (bSupportsSliced3DViews)
				{
					VkPhysicalDeviceFeatures2 slicedFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					slicedFeatures.pNext = &supportedSliced3DViews;
					state->F.GetPhysicalDeviceFeatures2(candidate, &slicedFeatures);
				}

				const bool bAvailableSliced3DViews = bSupportsSliced3DViews && supportedSliced3DViews.imageSlicedViewOf3D == VK_TRUE;
				VkPhysicalDeviceCooperativeVectorFeaturesNV supportedCoopVec{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV };
				if (bSupportsCoopVec && supported12.bufferDeviceAddress == VK_TRUE && state->F.GetCoopVecProperties != nullptr)
				{
					VkPhysicalDeviceFeatures2 coopFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					coopFeatures.pNext = &supportedCoopVec;
					state->F.GetPhysicalDeviceFeatures2(candidate, &coopFeatures);
				}

				const bool bAvailableCoopVec = bSupportsCoopVec &&
					supportedCoopVec.cooperativeVector == VK_TRUE &&
					supported12.bufferDeviceAddress == VK_TRUE &&
					state->F.GetCoopVecProperties != nullptr;
				VkPhysicalDeviceCooperativeVectorFeaturesNV enabledCoopVec{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV };
				enabledCoopVec.cooperativeVector = VK_TRUE;
				enabledCoopVec.cooperativeVectorTraining = supportedCoopVec.cooperativeVectorTraining;
				VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT enabledSliced3DViews{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_SLICED_VIEW_OF_3D_FEATURES_EXT };
				enabledSliced3DViews.imageSlicedViewOf3D = VK_TRUE;
				VkPhysicalDeviceMeshShaderFeaturesEXT supportedMeshShader{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
				if (bSupportsMeshShader)
				{
					VkPhysicalDeviceFeatures2 meshFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					meshFeatures.pNext = &supportedMeshShader;
					state->F.GetPhysicalDeviceFeatures2(candidate, &meshFeatures);
				}

				const bool bAvailableMeshShader = bSupportsMeshShader && supportedMeshShader.meshShader == VK_TRUE;
				VkPhysicalDeviceMeshShaderFeaturesEXT enabledMeshShader{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
				enabledMeshShader.meshShader = VK_TRUE;
				enabledMeshShader.taskShader = supportedMeshShader.taskShader;
				VkPhysicalDeviceFragmentShadingRateFeaturesKHR supportedShadingRate{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR };
				if (bSupportsShadingRate && state->F.GetFragmentShadingRates != nullptr)
				{
					VkPhysicalDeviceFeatures2 shadingRateFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					shadingRateFeatures.pNext = &supportedShadingRate;
					state->F.GetPhysicalDeviceFeatures2(candidate, &shadingRateFeatures);
				}

				const bool bAvailableShadingRate = bSupportsShadingRate && supportedShadingRate.pipelineFragmentShadingRate == VK_TRUE;
				VkPhysicalDeviceFragmentShadingRateFeaturesKHR enabledShadingRate{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR };
				enabledShadingRate.pipelineFragmentShadingRate = VK_TRUE;
				enabledShadingRate.attachmentFragmentShadingRate = supportedShadingRate.attachmentFragmentShadingRate;
				enabledShadingRate.primitiveFragmentShadingRate = supportedShadingRate.primitiveFragmentShadingRate;
				VkPhysicalDeviceAccelerationStructureFeaturesKHR supportedAcceleration{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
				VkPhysicalDeviceRayQueryFeaturesKHR supportedRayQuery{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
				VkPhysicalDeviceRayTracingPipelineFeaturesKHR supportedRayTracingPipeline{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
				const bool bAccelerationExtensions = bSupportsDeferredHostOperations && bSupportsAccelerationStructure && supported12.bufferDeviceAddress == VK_TRUE;

				if (bAccelerationExtensions)
				{
					VkPhysicalDeviceFeatures2 rayFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					rayFeatures.pNext = &supportedAcceleration;
					supportedAcceleration.pNext = bSupportsRayQuery ? static_cast<void*>(&supportedRayQuery) : bSupportsRayTracingPipeline ? static_cast<void*>(&supportedRayTracingPipeline) : nullptr;
					supportedRayQuery.pNext = bSupportsRayTracingPipeline ? &supportedRayTracingPipeline : nullptr;
					state->F.GetPhysicalDeviceFeatures2(candidate, &rayFeatures);
				}

				const bool bAvailableAcceleration = bAccelerationExtensions && supportedAcceleration.accelerationStructure == VK_TRUE;
				const bool bAvailableRayQuery = bAvailableAcceleration && bSupportsRayQuery && supportedRayQuery.rayQuery == VK_TRUE;
				const bool bAvailableRayTracingPipeline = bAvailableAcceleration && bSupportsRayTracingPipeline && supportedRayTracingPipeline.rayTracingPipeline == VK_TRUE;
				VkPhysicalDeviceOpacityMicromapFeaturesEXT supportedMicromap{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT };
				VkPhysicalDeviceSynchronization2FeaturesKHR supportedSynchronization2{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
				if (bAvailableAcceleration && bSupportsOpacityMicromap && bSupportsSynchronization2)
				{
					VkPhysicalDeviceFeatures2 micromapFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					micromapFeatures.pNext = &supportedMicromap;
					supportedMicromap.pNext = &supportedSynchronization2;
					state->F.GetPhysicalDeviceFeatures2(candidate, &micromapFeatures);
				}

				const bool bAvailableMicromap = bAvailableAcceleration &&
					bSupportsOpacityMicromap &&
					bSupportsSynchronization2 &&
					supportedMicromap.micromap == VK_TRUE &&
					supportedSynchronization2.synchronization2 == VK_TRUE;
				VkPhysicalDeviceClusterAccelerationStructureFeaturesNV supportedClusters{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV };
				if (bAvailableAcceleration && bSupportsClusters)
				{
					VkPhysicalDeviceFeatures2 clusterFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
					clusterFeatures.pNext = &supportedClusters;
					state->F.GetPhysicalDeviceFeatures2(candidate, &clusterFeatures);
				}

				const bool bAvailableClusters = bAvailableAcceleration && bSupportsClusters && supportedClusters.clusterAccelerationStructure == VK_TRUE;
				VkPhysicalDeviceClusterAccelerationStructureFeaturesNV enabledClusters{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV };
				enabledClusters.clusterAccelerationStructure = VK_TRUE;
				VkPhysicalDeviceOpacityMicromapFeaturesEXT enabledMicromap{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT };
				enabledMicromap.micromap = VK_TRUE;
				VkPhysicalDeviceSynchronization2FeaturesKHR enabledSynchronization2{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
				enabledSynchronization2.synchronization2 = VK_TRUE;
				VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAcceleration{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
				enabledAcceleration.accelerationStructure = VK_TRUE;
				VkPhysicalDeviceRayQueryFeaturesKHR enabledRayQuery{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
				enabledRayQuery.rayQuery = VK_TRUE;
				VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipeline{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
				enabledRayTracingPipeline.rayTracingPipeline = VK_TRUE;
				VkDevice created = VK_NULL_HANDLE;
				VkResult createdResult = VK_ERROR_INITIALIZATION_FAILED;
				const uint32 rayPipelineAttempts = bAvailableRayTracingPipeline ? 16u : 0u;
				const uint32 rayPipelineOnlyAttempts = bAvailableRayTracingPipeline && bAvailableRayQuery ? 16u : 0u;
				const uint32 rayQueryAttempts = bAvailableRayQuery ? 16u : 0u;
				const uint32 accelerationAttempts = bAvailableAcceleration ? 16u : 0u;

				for (uint32 attempt = 0;
					attempt < rayPipelineAttempts + rayPipelineOnlyAttempts + rayQueryAttempts + accelerationAttempts + 16u; ++attempt)
				{
					const uint32 baseAttempt = attempt % 16;
					const uint32 option = baseAttempt % 4;
					const bool bUseSwapchain = bAvailableSwapchain && (option == 0 || option == 2);
					const bool bUseConservativeRaster = bAvailableConservativeRaster && (option == 0 || option == 1);
					const bool bUseMeshShader = bAvailableMeshShader && (baseAttempt < 4 || (baseAttempt >= 8 && baseAttempt < 12));
					const bool bUseShadingRate = bAvailableShadingRate && baseAttempt < 8;
					const bool bUseAcceleration = attempt < rayPipelineAttempts + rayPipelineOnlyAttempts + rayQueryAttempts + accelerationAttempts;
					const bool bUseRayQuery =
						bAvailableRayQuery &&
						(attempt < rayPipelineAttempts || (attempt >= rayPipelineAttempts + rayPipelineOnlyAttempts && attempt < rayPipelineAttempts + rayPipelineOnlyAttempts + rayQueryAttempts));
					const bool bUseRayTracingPipeline = attempt < rayPipelineAttempts + rayPipelineOnlyAttempts;
					const bool bUseMicromap = bUseAcceleration && bAvailableMicromap;
					const bool bUseClusters = bUseAcceleration && bAvailableClusters;
					void* const clusterTail = bUseClusters ? static_cast<void*>(&enabledClusters) : nullptr;
					void* const optionalChain = bUseMeshShader ? static_cast<void*>(&enabledMeshShader)
						: bUseShadingRate ? static_cast<void*>(&enabledShadingRate)
						: bUseAcceleration ? static_cast<void*>(&enabledAcceleration)
						: nullptr;
					enabled12.pNext = bAvailableCoopVec ? static_cast<void*>(&enabledCoopVec) : bAvailableSliced3DViews ? static_cast<void*>(&enabledSliced3DViews) : optionalChain;
					enabledCoopVec.pNext = bAvailableSliced3DViews ? static_cast<void*>(&enabledSliced3DViews) : optionalChain;
					enabledSliced3DViews.pNext = optionalChain;
					enabledMeshShader.pNext = bUseShadingRate ? static_cast<void*>(&enabledShadingRate) : bUseAcceleration ? static_cast<void*>(&enabledAcceleration) : nullptr;
					enabledShadingRate.pNext = bUseAcceleration ? &enabledAcceleration : nullptr;
					enabledAcceleration.pNext = bUseRayQuery ? static_cast<void*>(&enabledRayQuery)
						: bUseRayTracingPipeline ? static_cast<void*>(&enabledRayTracingPipeline)
						: bUseMicromap ? static_cast<void*>(&enabledMicromap)
						: clusterTail;
					enabledRayQuery.pNext = bUseRayTracingPipeline ? static_cast<void*>(&enabledRayTracingPipeline) : bUseMicromap ? static_cast<void*>(&enabledMicromap) : clusterTail;
					enabledRayTracingPipeline.pNext = bUseMicromap ? static_cast<void*>(&enabledMicromap) : clusterTail;
					enabledMicromap.pNext = bUseMicromap ? static_cast<void*>(&enabledSynchronization2) : clusterTail;
					enabledSynchronization2.pNext = clusterTail;
					const char* extensions[30]{};
					deviceInfo.pNext = &enabled11;
					deviceInfo.enabledExtensionCount = 0;
					if (bAvailableShaderDemote && !bCoreShaderDemote)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME;
					}

					if (bAvailableMaintenance4 && !bCoreMaintenance4)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_MAINTENANCE_4_EXTENSION_NAME;
					}
					if (bAvailableDynamicRendering && !bCoreDynamicRendering)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
					}
					if (bAvailableMaintenance7)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_MAINTENANCE_7_EXTENSION_NAME;
					}
					if (bSupportsLoadStoreOpNone)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME;
					}

					if (bSupportsMemoryBudget)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
					}
#ifdef _WIN32
					if (bSupportsExternalMemory)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME;
					}

					if (bSupportsExternalSemaphore)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME;
					}
#else
					if (supportsExternalMemory)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
					}

					if (supportsExternalSemaphore)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
					}
#endif
					VkDeviceDiagnosticsConfigCreateInfoNV diagnosticConfig{ VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV };
					VkPhysicalDeviceDiagnosticsConfigFeaturesNV enabledDiagnostics{
						VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV };
					if (bNeedsDiagnosticsConfig)
					{
						if (aftermath.bResourceTracking)
						{
							diagnosticConfig.flags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV;
						}

						if (aftermath.bShaderDebugInfo)
						{
							diagnosticConfig.flags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV;
						}

						if (aftermath.bShaderErrorReporting)
						{
							diagnosticConfig.flags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV;
						}
						enabledDiagnostics.diagnosticsConfig = VK_TRUE;
						enabledDiagnostics.pNext = &enabled11;
						diagnosticConfig.pNext = &enabledDiagnostics;
						deviceInfo.pNext = &diagnosticConfig;
						extensions[deviceInfo.enabledExtensionCount++] = VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME;
					}

					if (aftermath.bCheckpoints)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME;
					}

					if (bUseSwapchain)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

						if (bSupportsHdrMetadata)
						{
							extensions[deviceInfo.enabledExtensionCount++] = VK_EXT_HDR_METADATA_EXTENSION_NAME;
						}

						if (bAvailableMaintenance)
						{
							extensions[deviceInfo.enabledExtensionCount++] = VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
						}
					}

					if (bUseConservativeRaster)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME;
					}

					if (bUseShadingRate)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME;
					}

					if (bUseMeshShader)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_EXT_MESH_SHADER_EXTENSION_NAME;
					}

					if (bUseAcceleration)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
					}

					if (bUseRayQuery)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
					}

					if (bUseRayTracingPipeline)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME;
					}

					if (bUseMicromap)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
					}

					if (bUseMicromap)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME;
					}

					if (bAvailableSliced3DViews)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_EXT_IMAGE_SLICED_VIEW_OF_3D_EXTENSION_NAME;
					}

					if (bAvailableCoopVec)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME;
					}

					if (bUseClusters)
					{
						extensions[deviceInfo.enabledExtensionCount++] = VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME;
					}
					VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT enabledMaintenance{
						VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT };
					if (bUseSwapchain && bAvailableMaintenance)
					{
						enabledMaintenance.swapchainMaintenance1 = VK_TRUE;
						enabledMaintenance.pNext = bNeedsDiagnosticsConfig ? static_cast<void*>(&diagnosticConfig) : static_cast<void*>(&enabled11);
						deviceInfo.pNext = &enabledMaintenance;
					}
					deviceInfo.ppEnabledExtensionNames = extensions;
					created = VK_NULL_HANDLE;
					createdResult = state->F.CreateDevice(candidate, &deviceInfo, nullptr, &created);
					bool bCoopEnabled = bAvailableCoopVec && createdResult == VK_SUCCESS;
					bool bSlicedEnabled = bAvailableSliced3DViews && createdResult == VK_SUCCESS;
					bool bMicromapEnabled = bUseMicromap && createdResult == VK_SUCCESS;
					bool bClustersEnabled = bUseClusters && createdResult == VK_SUCCESS;

					if (createdResult != VK_SUCCESS && bUseClusters)
					{
						enabledAcceleration.pNext = bUseRayQuery ? static_cast<void*>(&enabledRayQuery)
							: bUseRayTracingPipeline ? static_cast<void*>(&enabledRayTracingPipeline)
							: bUseMicromap ? static_cast<void*>(&enabledMicromap)
							: nullptr;
						enabledRayQuery.pNext = bUseRayTracingPipeline ? static_cast<void*>(&enabledRayTracingPipeline) : bUseMicromap ? static_cast<void*>(&enabledMicromap) : nullptr;
						enabledRayTracingPipeline.pNext = bUseMicromap ? &enabledMicromap : nullptr;
						enabledMicromap.pNext = bUseMicromap ? static_cast<void*>(&enabledSynchronization2) : nullptr;
						enabledSynchronization2.pNext = nullptr;
						--deviceInfo.enabledExtensionCount;
						createdResult = state->F.CreateDevice(candidate, &deviceInfo, nullptr, &created);
						bClustersEnabled = false;
					}

					if (createdResult != VK_SUCCESS && bAvailableCoopVec)
					{
						enabled12.pNext = bAvailableSliced3DViews ? static_cast<void*>(&enabledSliced3DViews) : optionalChain;
						--deviceInfo.enabledExtensionCount;
						createdResult = state->F.CreateDevice(candidate, &deviceInfo, nullptr, &created);
						bCoopEnabled = false;
						bSlicedEnabled = bAvailableSliced3DViews && createdResult == VK_SUCCESS;
					}

					if (createdResult != VK_SUCCESS && bAvailableSliced3DViews)
					{
						enabled12.pNext = optionalChain;
						--deviceInfo.enabledExtensionCount;
						createdResult = state->F.CreateDevice(candidate, &deviceInfo, nullptr, &created);
						bSlicedEnabled = false;
					}

					if (createdResult != VK_SUCCESS && bUseMicromap)
					{
						enabledAcceleration.pNext = bUseRayQuery ? static_cast<void*>(&enabledRayQuery) : bUseRayTracingPipeline ? static_cast<void*>(&enabledRayTracingPipeline) : nullptr;
						enabledRayQuery.pNext = bUseRayTracingPipeline ? static_cast<void*>(&enabledRayTracingPipeline) : nullptr;
						enabledRayTracingPipeline.pNext = nullptr;
						--deviceInfo.enabledExtensionCount;
						createdResult = state->F.CreateDevice(candidate, &deviceInfo, nullptr, &created);
						bMicromapEnabled = false;
					}

					if (createdResult == VK_SUCCESS)
					{
						bSupportsSliced3DViews = bSlicedEnabled;
						bSupportsCoopVec = bCoopEnabled;
						bSupportsSwapchain = bUseSwapchain;
						bSupportsConservativeRaster = bUseConservativeRaster;
						bSupportsShadingRate = bUseShadingRate;
						bSupportsMeshShader = bUseMeshShader;
						bSupportsAccelerationStructure = bUseAcceleration;
						bSupportsRayQuery = bUseRayQuery;
						bSupportsRayTracingPipeline = bUseRayTracingPipeline;
						bSupportsOpacityMicromap = bMicromapEnabled;
						bSupportsClusters = bClustersEnabled;
						break;
					}
				}

				if (createdResult == VK_SUCCESS)
				{
					state->Physical = candidate;
					state->Device = created;
					state->Diagnostics = diagnostics;
					state->bAftermathCheckpoints = aftermath.bCheckpoints;

					if (diagnostics != nullptr)
					{
						Result<void> attached = AttachAftermathVulkan(diagnostics, bNeedsDiagnosticsConfig, aftermath.bCheckpoints);

						if (!attached)
						{
							Destroy(state);

							return Result<BackendData>::Failure(attached.GetError());
						}
						state->bAftermathAttached = true;
					}
					state->bGraphicsStorageWrites = bGraphicsStorageWrites;
					state->bDrawIndexedIndirectCount = supported12.drawIndirectCount == VK_TRUE;
					state->bHostQueryReset = enabled12.hostQueryReset == VK_TRUE;
					state->bDrawIndirectCount = supported12.drawIndirectCount == VK_TRUE;
					state->bSamplerAnisotropy = supportedFeatures.samplerAnisotropy == VK_TRUE;
					state->bSamplerFilterMinmax = enabled12.samplerFilterMinmax == VK_TRUE;
					state->bCubeArray = supportedFeatures.imageCubeArray == VK_TRUE;
					state->bGeometryShader = supportedFeatures.geometryShader == VK_TRUE;
					state->bTessellationShader = supportedFeatures.tessellationShader == VK_TRUE;
					state->bWireframe = supportedFeatures.fillModeNonSolid == VK_TRUE;
					state->bDepthClamp = supportedFeatures.depthClamp == VK_TRUE;
					state->bDepthBounds = deviceFeatures.depthBounds == VK_TRUE;
					state->bDepthBiasClamp = supportedFeatures.depthBiasClamp == VK_TRUE;
					state->bConservativeRasterization = bSupportsConservativeRaster;

					if (bSupportsMeshShader)
					{
						state->F.CmdDrawMeshTasks = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(state->F.GetDeviceProcAddr(created, "vkCmdDrawMeshTasksEXT"));
					}
					state->bMeshShader = bSupportsMeshShader && state->F.CmdDrawMeshTasks != nullptr;

					if (state->bMeshShader)
					{
						state->F.CmdDrawMeshTasksIndirect = reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectEXT>( state->F.GetDeviceProcAddr(created, "vkCmdDrawMeshTasksIndirectEXT"));
					}
					state->bMeshIndirect = state->F.CmdDrawMeshTasksIndirect != nullptr;
					state->bTaskShader = state->bMeshShader && enabledMeshShader.taskShader == VK_TRUE;
					state->bAttachmentShadingRate = bSupportsShadingRate && enabledShadingRate.attachmentFragmentShadingRate == VK_TRUE;
					state->bPrimitiveShadingRate = bSupportsShadingRate && enabledShadingRate.primitiveFragmentShadingRate == VK_TRUE;

					if (bSupportsShadingRate)
					{
						state->F.CmdSetFragmentShadingRate = reinterpret_cast<PFN_vkCmdSetFragmentShadingRateKHR>( state->F.GetDeviceProcAddr(created, "vkCmdSetFragmentShadingRateKHR"));
					}
					state->bMultiViewport = deviceFeatures.multiViewport == VK_TRUE;
					state->bDualSourceBlend = deviceFeatures.dualSrcBlend == VK_TRUE;
					state->bIndependentBlend = deviceFeatures.independentBlend == VK_TRUE;
					state->bMultiDrawIndirect = deviceFeatures.multiDrawIndirect == VK_TRUE;
					state->bIndirectFirstInstance = deviceFeatures.drawIndirectFirstInstance == VK_TRUE;
					state->bUniformBufferArrayDynamicIndexing = deviceFeatures.shaderUniformBufferArrayDynamicIndexing == VK_TRUE;
					state->bSampledImageArrayDynamicIndexing = deviceFeatures.shaderSampledImageArrayDynamicIndexing == VK_TRUE;
					state->bStorageBufferArrayDynamicIndexing = deviceFeatures.shaderStorageBufferArrayDynamicIndexing == VK_TRUE;
					state->bStorageImageArrayDynamicIndexing = deviceFeatures.shaderStorageImageArrayDynamicIndexing == VK_TRUE;
					state->bBufferDeviceAddress = enabled12.bufferDeviceAddress == VK_TRUE;
					state->bCooperativeVector = bSupportsCoopVec && state->bBufferDeviceAddress;
					state->bCooperativeVectorTraining = state->bCooperativeVector && enabledCoopVec.cooperativeVectorTraining == VK_TRUE;
					state->bImageSlicedViewOf3D = bSupportsSliced3DViews;
					state->bAccelerationStructure = bSupportsAccelerationStructure;
					state->bRayQuery = bSupportsRayQuery;
					state->bRayTracingPipeline = bSupportsRayTracingPipeline;
					state->bOpacityMicromap = bSupportsOpacityMicromap;
					state->bRayTracingClusters = bSupportsClusters;
					state->bShaderInt64 = deviceFeatures.shaderInt64 == VK_TRUE;
					state->bShaderDrawParameters = enabled11.shaderDrawParameters == VK_TRUE;
					state->bSampleRateShading = deviceFeatures.sampleRateShading == VK_TRUE;
					state->bShaderImageGatherExtended = deviceFeatures.shaderImageGatherExtended == VK_TRUE;
					state->bShaderStorageImageExtendedFormats = deviceFeatures.shaderStorageImageExtendedFormats == VK_TRUE;
					state->bShaderStorageImageReadWithoutFormat = deviceFeatures.shaderStorageImageReadWithoutFormat == VK_TRUE;
					state->bShaderStorageImageWriteWithoutFormat = deviceFeatures.shaderStorageImageWriteWithoutFormat == VK_TRUE;
					state->bShaderFloat16 = enabled12.shaderFloat16 == VK_TRUE;
					state->bLoadStoreOpNone = bSupportsLoadStoreOpNone;
					state->bDynamicRendering = bAvailableDynamicRendering;
					state->bMaintenance4 = bAvailableMaintenance4;
					state->bSeparateDepthStencilAttachmentAccess = bAvailableMaintenance7 && maintenance7Properties.separateDepthStencilAttachmentAccess;
					state->bShaderDemoteToHelperInvocation = bAvailableShaderDemote;
					state->bSparseResidency = bSparseResidency;
					state->MaximumBindlessSampledTextures = bBindlessSampledTextures ? SampledDescriptorCapacity(candidateProperties.limits) : 0u;
					state->MaximumBindlessSamplers = bBindlessSamplers ? 1024u : 0u;
					state->MaximumBindlessStorageBuffers = bBindlessStorageBuffers ? 1024u : 0u;
					state->bBindlessTypedBuffers = bBindlessTypedBuffers;
					state->MaximumBindlessStorageTextures = bBindlessStorageTextures ? 1024u : 0u;
					state->MaximumBindlessUniformBuffers = bBindlessUniformBuffers ? 1024u : 0u;
					state->bExternalMemoryEnabled = bSupportsExternalMemory;
					state->bExternalSemaphoreEnabled = bSupportsExternalSemaphore;
					state->bMemoryBudget = bSupportsMemoryBudget;
					state->bSwapchainEnabled = bSupportsSwapchain;
					state->bSwapchainMaintenance = bSupportsSwapchain && bAvailableMaintenance;
					state->bHdrMetadata = bSupportsSwapchain && bSupportsHdrMetadata;
					state->QueueFamily = index;
					state->ComputeQueueFamily = computeFamily;
					state->CopyQueueFamily = copyFamily;
					state->ComputeQueueIndex = computeQueueIndex;
					state->CopyQueueIndex = copyQueueIndex;
					state->bSeparateComputeQueue = computeFamily != index || computeQueueIndex != 0;
					state->bSeparateCopyQueue = copyFamily != index || copyQueueIndex != 0;
					state->ResourceQueueFamilies[0] = index;
					state->ResourceQueueFamilyCount = 1;

					if (computeFamily != index)
					{
						state->ResourceQueueFamilies[state->ResourceQueueFamilyCount++] = computeFamily;
					}

					if (copyFamily != index && copyFamily != computeFamily)
					{
						state->ResourceQueueFamilies[state->ResourceQueueFamilyCount++] = copyFamily;
					}
					state->GraphicsQueueFlags = families[index].queueFlags;
					state->ComputeQueueFlags = families[computeFamily].queueFlags;
					state->CopyQueueFlags = families[copyFamily].queueFlags;
					state->TimestampValidBits = families[index].timestampValidBits;
					state->ComputeTimestampValidBits = families[computeFamily].timestampValidBits;
					state->CopyTimestampValidBits = families[copyFamily].timestampValidBits;
					family = index;
					break;
				}
			}

			if (family != UINT32_MAX)
			{
				break;
			}
		}

		if (family == UINT32_MAX)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanAdapter", "No Vulkan adapter matches the preference and required features.");
		}

		return FinishVulkanBackend(state, resource);
	}
}

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<BackendData> FinishVulkanBackend(State* state, MemoryResource& resource)
	{
		const bool bRequestedExternalAddress = state->bExternalDevice && state->bBufferDeviceAddress;
		const bool bRequestedExternalAcceleration = state->bExternalDevice && state->bAccelerationStructure;
		const bool bRequestedExternalRayPipeline = state->bExternalDevice && state->bRayTracingPipeline;
		const bool bRequestedExternalMicromap = state->bExternalDevice && state->bOpacityMicromap;
		const bool bRequestedExternalClusters = state->bExternalDevice && state->bRayTracingClusters;
		const bool bRequestedExternalCoopVec = state->bExternalDevice && state->bCooperativeVector;
		const bool bRequestedExternalShadingRate = state->bExternalDevice && state->F.CmdSetFragmentShadingRate != nullptr;
		const bool bRequestedExternalAttachmentShadingRate = state->bExternalDevice && state->bAttachmentShadingRate;
		state->F.GetMemoryProperties(state->Physical, &state->MemoryProperties);
		state->F.GetPhysicalDeviceProperties(state->Physical, &state->DeviceProperties);

		if (state->ApiVersion > state->DeviceProperties.apiVersion)
		{
			state->ApiVersion = state->DeviceProperties.apiVersion;
		}
		VkPhysicalDeviceSubgroupProperties subgroup{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
		VkPhysicalDeviceFragmentShadingRatePropertiesKHR shadingRateProperties{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR };
		VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT };
		if (state->bMeshShader)
		{
			subgroup.pNext = &meshProperties;
			meshProperties.pNext = state->F.CmdSetFragmentShadingRate != nullptr ? static_cast<void*>(&shadingRateProperties) : nullptr;
		}
		else if (state->F.CmdSetFragmentShadingRate != nullptr)
		{
			subgroup.pNext = &shadingRateProperties;
		}
		VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		properties.pNext = &subgroup;
		state->F.GetPhysicalDeviceProperties2(state->Physical, &properties);

		if (state->bAccelerationStructure)
		{
			VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration{
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
			VkPhysicalDeviceProperties2 accelerationProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			accelerationProperties.pNext = &acceleration;
			state->F.GetPhysicalDeviceProperties2(state->Physical, &accelerationProperties);
			state->AccelScratchAlignment = acceleration.minAccelerationStructureScratchOffsetAlignment;
			state->AccelerationProperties = acceleration;
		}

		if (state->bRayTracingPipeline)
		{
			VkPhysicalDeviceProperties2 rayProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			rayProperties.pNext = &state->RayTracingProperties;
			state->F.GetPhysicalDeviceProperties2(state->Physical, &rayProperties);
			state->bRayTracingPipeline =
				state->RayTracingProperties.shaderGroupHandleSize != 0 && state->RayTracingProperties.shaderGroupHandleAlignment != 0 &&
				state->RayTracingProperties.shaderGroupBaseAlignment != 0 && state->RayTracingProperties.maxRayRecursionDepth != 0;
		}

		if (state->bMeshShader)
		{
			for (uint32 index = 0; index < 3; ++index)
			{
				state->MaximumMeshGroups[index] = meshProperties.maxMeshWorkGroupCount[index];
			}
			state->MaximumMeshGroupTotalCount = meshProperties.maxMeshWorkGroupTotalCount;
		}
		state->bNonTrivialShadingRateCombiners = shadingRateProperties.fragmentShadingRateNonTrivialCombinerOps == VK_TRUE;

		if (state->bAttachmentShadingRate)
		{
			VkFormatProperties rateFormat{};
			state->F.GetFormatProperties(state->Physical, VK_FORMAT_R8_UINT, &rateFormat);
			state->bAttachmentShadingRate = (rateFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR) != 0;

			if (state->bAttachmentShadingRate)
			{
				state->ShadingRateTile = shadingRateProperties.minFragmentShadingRateAttachmentTexelSize;
			}
		}

		if (state->bOpacityMicromap)
		{
			VkPhysicalDeviceProperties2 micromapProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			micromapProperties.pNext = &state->MicromapProperties;
			state->F.GetPhysicalDeviceProperties2(state->Physical, &micromapProperties);
		}

		if (state->bRayTracingClusters)
		{
			uint32 extensionCount = 0;
			Array<VkExtensionProperties> extensions(resource);

			if (state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &extensionCount, nullptr) != VK_SUCCESS ||
				!extensions.ResizeUninitialized(extensionCount) ||
				state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &extensionCount, extensions.Data()) != VK_SUCCESS)
			{
				Destroy(state);

				return Result<BackendData>::Failure(Failure("Rhi.VulkanCluster", "Could not query the cluster extension revision."));
			}

			for (VkExtensionProperties const& extension : extensions)
			{
				if (CompareCString(extension.extensionName, VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
				{
					state->ClusterExtensionRevision = extension.specVersion;
				}
			}
			VkPhysicalDeviceProperties2 clusterProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			clusterProperties.pNext = &state->ClusterProperties;
			state->F.GetPhysicalDeviceProperties2(state->Physical, &clusterProperties);
		}

		if (bRequestedExternalAttachmentShadingRate && !state->bAttachmentShadingRate)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalShadingRate", "The external device cannot use R8_UINT shading rate attachments.");
		}

		if (subgroup.subgroupSize != 0 && (subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0 && (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT) != 0)
		{
			state->WaveLaneCount = subgroup.subgroupSize;
			static_assert(static_cast<uint32>(ESubgroupOperationFlags::Basic) == VK_SUBGROUP_FEATURE_BASIC_BIT);
			static_assert(static_cast<uint32>(ESubgroupOperationFlags::Vote) == VK_SUBGROUP_FEATURE_VOTE_BIT);
			static_assert(static_cast<uint32>(ESubgroupOperationFlags::Arithmetic) == VK_SUBGROUP_FEATURE_ARITHMETIC_BIT);
			static_assert(static_cast<uint32>(ESubgroupOperationFlags::Ballot) == VK_SUBGROUP_FEATURE_BALLOT_BIT);
			static_assert(static_cast<uint32>(ESubgroupOperationFlags::Shuffle) == VK_SUBGROUP_FEATURE_SHUFFLE_BIT);
			static_assert(static_cast<uint32>(ESubgroupOperationFlags::ShuffleRelative) == VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT);
			static_assert(static_cast<uint32>(ESubgroupOperationFlags::Clustered) == VK_SUBGROUP_FEATURE_CLUSTERED_BIT);
			static_assert(static_cast<uint32>(ESubgroupOperationFlags::Quad) == VK_SUBGROUP_FEATURE_QUAD_BIT);
			state->ComputeSubgroupOperations = static_cast<ESubgroupOperationFlags>(subgroup.supportedOperations & 255u);
		}

		if (subgroup.subgroupSize != 0 && (subgroup.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) != 0 && (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT) != 0)
		{
			state->WaveLaneCount = subgroup.subgroupSize;
			state->FragmentSubgroupOperations = static_cast<ESubgroupOperationFlags>(subgroup.supportedOperations & 255u);
		}

		if (state->MaximumBindlessSampledTextures != 0)
		{
			const VkPhysicalDeviceLimits& limits = state->DeviceProperties.limits;

			if (limits.maxBoundDescriptorSets < 2)
			{
				state->MaximumBindlessSampledTextures = 0;
			}

			if (limits.maxPerStageDescriptorSampledImages < state->MaximumBindlessSampledTextures)
			{
				state->MaximumBindlessSampledTextures = limits.maxPerStageDescriptorSampledImages;
			}

			if (limits.maxDescriptorSetSampledImages < state->MaximumBindlessSampledTextures)
			{
				state->MaximumBindlessSampledTextures = limits.maxDescriptorSetSampledImages;
			}
		}

		if (state->MaximumBindlessSamplers != 0)
		{
			const VkPhysicalDeviceLimits& limits = state->DeviceProperties.limits;

			if (limits.maxBoundDescriptorSets < 3)
			{
				state->MaximumBindlessSamplers = 0;
			}

			if (limits.maxPerStageDescriptorSamplers < state->MaximumBindlessSamplers)
			{
				state->MaximumBindlessSamplers = limits.maxPerStageDescriptorSamplers;
			}

			if (limits.maxDescriptorSetSamplers < state->MaximumBindlessSamplers)
			{
				state->MaximumBindlessSamplers = limits.maxDescriptorSetSamplers;
			}
		}

		if (state->MaximumBindlessStorageBuffers != 0)
		{
			const VkPhysicalDeviceLimits& limits = state->DeviceProperties.limits;

			if (limits.maxBoundDescriptorSets < 4)
			{
				state->MaximumBindlessStorageBuffers = 0;
			}

			const uint32 reserved = 8;
			const uint32 stage = limits.maxPerStageDescriptorStorageBuffers > reserved ? limits.maxPerStageDescriptorStorageBuffers - reserved : 0;
			const uint32 set = limits.maxDescriptorSetStorageBuffers > reserved ? limits.maxDescriptorSetStorageBuffers - reserved : 0;

			if (stage < state->MaximumBindlessStorageBuffers)
			{
				state->MaximumBindlessStorageBuffers = stage;
			}

			if (set < state->MaximumBindlessStorageBuffers)
			{
				state->MaximumBindlessStorageBuffers = set;
			}
		}

		if (state->MaximumBindlessStorageTextures != 0)
		{
			const VkPhysicalDeviceLimits& limits = state->DeviceProperties.limits;

			if (limits.maxBoundDescriptorSets < 5)
			{
				state->MaximumBindlessStorageTextures = 0;
			}

			const uint32 reserved = 8;
			const uint32 stage = limits.maxPerStageDescriptorStorageImages > reserved ? limits.maxPerStageDescriptorStorageImages - reserved : 0;
			const uint32 set = limits.maxDescriptorSetStorageImages > reserved ? limits.maxDescriptorSetStorageImages - reserved : 0;

			if (stage < state->MaximumBindlessStorageTextures)
			{
				state->MaximumBindlessStorageTextures = stage;
			}

			if (set < state->MaximumBindlessStorageTextures)
			{
				state->MaximumBindlessStorageTextures = set;
			}
		}

		if (state->MaximumBindlessUniformBuffers != 0)
		{
			const VkPhysicalDeviceLimits& limits = state->DeviceProperties.limits;

			if (limits.maxBoundDescriptorSets < 6)
			{
				state->MaximumBindlessUniformBuffers = 0;
			}

			const uint32 reserved = 8;
			const uint32 stage = limits.maxPerStageDescriptorUniformBuffers > reserved ? limits.maxPerStageDescriptorUniformBuffers - reserved : 0;
			const uint32 set = limits.maxDescriptorSetUniformBuffers > reserved ? limits.maxDescriptorSetUniformBuffers - reserved : 0;

			if (stage < state->MaximumBindlessUniformBuffers)
			{
				state->MaximumBindlessUniformBuffers = stage;
			}

			if (set < state->MaximumBindlessUniformBuffers)
			{
				state->MaximumBindlessUniformBuffers = set;
			}
		}

		if (!LoadDeviceFunctions(*state))
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.VulkanDevice", "Could not create a Vulkan timeline device."));
		}

		if (state->bHostQueryReset)
		{
			state->F.ResetQueryPool = reinterpret_cast<PFN_vkResetQueryPool>(state->F.GetDeviceProcAddr(state->Device, "vkResetQueryPool"));
			state->bHostQueryReset = state->F.ResetQueryPool != nullptr;
		}

		if (state->bCooperativeVector)
		{
			VkPhysicalDeviceProperties2 coopProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			coopProperties.pNext = &state->CoopVecProperties;
			state->F.GetPhysicalDeviceProperties2(state->Physical, &coopProperties);
		}

		if (bRequestedExternalCoopVec && !state->bCooperativeVector)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalCoopVec", "The external device does not expose cooperative vector conversion commands.");
		}

		if (bRequestedExternalAddress && !state->bBufferDeviceAddress)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalAddress", "The external device does not expose buffer device address commands.");
		}

		if (bRequestedExternalAcceleration && !state->bAccelerationStructure)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalAcceleration", "The external device does not expose acceleration structure commands.");
		}

		if (bRequestedExternalRayPipeline && !state->bRayTracingPipeline)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalRayPipeline", "The external device does not expose ray tracing pipeline commands.");
		}

		if (bRequestedExternalMicromap && !state->bOpacityMicromap)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalMicromap", "The external device does not expose opacity micromap commands.");
		}

		if (bRequestedExternalClusters && !state->bRayTracingClusters)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalClusters", "The external device does not expose cluster acceleration structure commands.");
		}

		if (bRequestedExternalShadingRate && state->F.GetFragmentShadingRates == nullptr)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalShadingRate", "The external instance does not expose fragment shading rates.");
		}

		if (state->F.CmdSetFragmentShadingRate != nullptr && state->F.GetFragmentShadingRates != nullptr)
		{
			uint32 rateCount = 0;

			if (state->F.GetFragmentShadingRates(state->Physical, &rateCount, nullptr) == VK_SUCCESS && rateCount != 0)
			{
				Array<VkPhysicalDeviceFragmentShadingRateKHR> rates(resource);

				if (rates.ResizeUninitialized(rateCount))
				{
					for (VkPhysicalDeviceFragmentShadingRateKHR& rate : rates)
					{
						rate = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR };
					}

					if (state->F.GetFragmentShadingRates(state->Physical, &rateCount, rates.Data()) == VK_SUCCESS)
					{
						constexpr VkSampleCountFlags SAMPLES[4]{ VK_SAMPLE_COUNT_1_BIT, VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT,
																VK_SAMPLE_COUNT_8_BIT };
						constexpr VkExtent2D EXTENTS[7]{ {1, 1}, {1, 2}, {2, 1}, {2, 2}, {2, 4}, {4, 2}, {4, 4} };
						for (VkPhysicalDeviceFragmentShadingRateKHR const& rate : rates)
						{
							for (uint32 index = 0; index < 7; ++index)
							{
								if (rate.fragmentSize.width != EXTENTS[index].width || rate.fragmentSize.height != EXTENTS[index].height)
								{
									continue;
								}

								for (uint32 sample = 0; sample < 4; ++sample)
								{
									if ((rate.sampleCounts & SAMPLES[sample]) != 0)
									{
										state->SupportedShadingRates[sample] |= 1u << index;
									}
								}
							}
						}
					}
				}
			}
		}

		if (!state->Queue)
		{
			state->F.GetDeviceQueue(state->Device, state->QueueFamily, 0, &state->Queue);
		}

		if (state->bSeparateComputeQueue)
		{
			if (!state->ComputeQueue)
			{
				state->F.GetDeviceQueue(state->Device, state->ComputeQueueFamily, state->ComputeQueueIndex, &state->ComputeQueue);
			}
		}
		else
		{
			state->ComputeQueue = state->Queue;
		}

		if (state->bSeparateCopyQueue)
		{
			if (!state->CopyQueue)
			{
				state->F.GetDeviceQueue(state->Device, state->CopyQueueFamily, state->CopyQueueIndex, &state->CopyQueue);
			}
		}
		else
		{
			state->CopyQueue = state->Queue;
		}

		if (!state->Queue || !state->ComputeQueue || !state->CopyQueue)
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.VulkanQueue", "Could not acquire the Vulkan submission queues."));
		}
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		VkAttachmentReference colorReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		VkSubpassDependency dependencies[1]{ dependency };
		VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = dependencies;
		VkAttachmentDescription depthAttachments[2]{ colorAttachment, {} };
		depthAttachments[1].format = VK_FORMAT_D32_SFLOAT;
		depthAttachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAttachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		VkAttachmentReference depthReference{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
		VkSubpassDescription depthSubpass = subpass;
		depthSubpass.pDepthStencilAttachment = &depthReference;
		VkSubpassDependency depthDependency = dependency;
		depthDependency.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		depthDependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		depthDependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		VkRenderPassCreateInfo depthPassInfo = renderPassInfo;
		depthPassInfo.attachmentCount = 2;
		depthPassInfo.pAttachments = depthAttachments;
		depthPassInfo.pSubpasses = &depthSubpass;
		VkSubpassDependency depthDependencies[1]{ depthDependency };
		depthPassInfo.pDependencies = depthDependencies;
		VkAttachmentDescription depthLoadAttachments[2]{ depthAttachments[0], depthAttachments[1] };
		depthLoadAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		VkRenderPassCreateInfo depthLoadPassInfo = depthPassInfo;
		depthLoadPassInfo.pAttachments = depthLoadAttachments;
		VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.queueFamilyIndex = state->QueueFamily;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VkSemaphoreTypeCreateInfo typeInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
		typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		semaphoreInfo.pNext = &typeInfo;
		const bool bSupportsD32 = HasFormatSupport(QueryFormatSupport(state, EFormat::D32), EFormatSupport::DepthStencil);

		if (state->F.CreateRenderPass(state->Device, &renderPassInfo, nullptr, &state->RenderPass) != VK_SUCCESS ||
			(bSupportsD32 && state->F.CreateRenderPass(state->Device, &depthPassInfo, nullptr, &state->DepthRenderPass) != VK_SUCCESS) ||
			(bSupportsD32 && state->F.CreateRenderPass(state->Device, &depthLoadPassInfo, nullptr, &state->DepthLoadRenderPass) != VK_SUCCESS) ||
			state->F.CreateCommandPool(state->Device, &poolInfo, nullptr, &state->Pool) != VK_SUCCESS ||
			state->F.CreateSemaphore(state->Device, &semaphoreInfo, nullptr, &state->Timeline) != VK_SUCCESS)
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.VulkanSetup", "Could not create Vulkan submission state."));
		}
		poolInfo.queueFamilyIndex = state->ComputeQueueFamily;

		if (state->F.CreateCommandPool(state->Device, &poolInfo, nullptr, &state->ComputePool) != VK_SUCCESS)
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.VulkanSetup", "Could not create the Vulkan compute command pool."));
		}
		poolInfo.queueFamilyIndex = state->CopyQueueFamily;

		if (state->F.CreateCommandPool(state->Device, &poolInfo, nullptr, &state->CopyPool) != VK_SUCCESS)
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.VulkanSetup", "Could not create the Vulkan copy command pool."));
		}

		if (state->bSeparateComputeQueue)
		{
			if (state->F.CreateSemaphore(state->Device, &semaphoreInfo, nullptr, &state->ComputeTimeline) != VK_SUCCESS)
			{
				Destroy(state);

				return Result<BackendData>::Failure(Failure("Rhi.VulkanSetup", "Could not create the Vulkan compute timeline."));
			}
		}
		else
		{
			state->ComputeTimeline = state->Timeline;
		}

		if (state->bSeparateCopyQueue)
		{
			if (state->F.CreateSemaphore(state->Device, &semaphoreInfo, nullptr, &state->CopyTimeline) != VK_SUCCESS)
			{
				Destroy(state);

				return Result<BackendData>::Failure(Failure("Rhi.VulkanSetup", "Could not create the Vulkan copy timeline."));
			}
		}
		else
		{
			state->CopyTimeline = state->Timeline;
		}

		const uint32 colorLimit =
			state->DeviceProperties.limits.maxColorAttachments < state->DeviceProperties.limits.maxFragmentOutputAttachments
			? state->DeviceProperties.limits.maxColorAttachments
			: state->DeviceProperties.limits.maxFragmentOutputAttachments;
		GpuCapabilities capabilities
		{};
		capabilities.Features.bGraphics = true;
		capabilities.Features.bCompute = true;
		capabilities.Features.bCopy = true;
		capabilities.Limits.MaximumBufferByteCount = MAXIMUM_BUFFER_BYTES;
		capabilities.Limits.MaximumColorAttachments = colorLimit < 8 ? colorLimit : 8;
		capabilities.Features.bDrawIndexedIndirectCount = state->bDrawIndexedIndirectCount;
		capabilities.Features.bDebugMarkers = (state->bDebugUtilsEnabled || state->bAftermathCheckpoints);
		capabilities.Limits.StorageBufferOffsetAlignmentBytes = state->DeviceProperties.limits.minStorageBufferOffsetAlignment;
		capabilities.Features.bGeometryShader = state->bGeometryShader;
		capabilities.Features.bTessellationShader = state->bTessellationShader;
		capabilities.Features.bWireframe = state->bWireframe;
		capabilities.Features.bDepthClamp = state->bDepthClamp;
		capabilities.Features.bDepthBiasClamp = state->bDepthBiasClamp;
		capabilities.Features.bConservativeRasterization = state->bConservativeRasterization;
		capabilities.Limits.MaximumBindlessSampledTextures = 0;
		capabilities.Limits.MaximumBindlessSamplers = 0;
		capabilities.Limits.MaximumBindlessStorageBuffers = 0;
		capabilities.Limits.MaximumBindlessStorageTextures = 0;
		capabilities.Limits.MaximumBindlessUniformBuffers = 0;
		capabilities.Limits.UniformBufferOffsetAlignmentBytes = state->DeviceProperties.limits.minUniformBufferOffsetAlignment;
		capabilities.Limits.MaximumGraphicsBindlessStorageBuffers = 0;
		capabilities.Limits.MaximumGraphicsBindlessStorageTextures = 0;
		capabilities.Limits.MaximumViewports = state->bMultiViewport ? (state->DeviceProperties.limits.maxViewports < 16 ? state->DeviceProperties.limits.maxViewports : 16u) : 1u;
		capabilities.Features.bDrawIndirectCount = state->bDrawIndirectCount;
		capabilities.Limits.MaximumCountedIndirectDraws =
			(state->bDrawIndirectCount || state->bDrawIndexedIndirectCount)
			? (state->DeviceProperties.limits.maxDrawIndirectCount < 65535 ? state->DeviceProperties.limits.maxDrawIndirectCount
				: 65535u)
			: 0u;
		capabilities.Limits.MinimumWaveLaneCount = state->WaveLaneCount;
		capabilities.Limits.MaximumWaveLaneCount = state->WaveLaneCount;
		capabilities.Limits.MaximumDualSourceColorAttachments = state->bDualSourceBlend ? state->DeviceProperties.limits.maxFragmentDualSrcAttachments : 0u;
		capabilities.Features.bBufferDeviceAddress = state->bBufferDeviceAddress;
		capabilities.Features.bShaderInt64 = state->bShaderInt64;
		capabilities.Adapter.VendorID = state->DeviceProperties.vendorID;
		capabilities.Adapter.DeviceID = state->DeviceProperties.deviceID;
		capabilities.Adapter.bSoftwareAdapter = state->DeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
		capabilities.Features.bSamplerReductionMinMax = state->bSamplerFilterMinmax;
		capabilities.Limits.SupportedShadingRates[0] = state->SupportedShadingRates[0];
		capabilities.Limits.SupportedShadingRates[1] = state->SupportedShadingRates[1];
		capabilities.Limits.SupportedShadingRates[2] = state->SupportedShadingRates[2];
		capabilities.Limits.SupportedShadingRates[3] = state->SupportedShadingRates[3];
		capabilities.Limits.ShadingRateImageTileWidth = state->ShadingRateTile.width;
		capabilities.Limits.ShadingRateImageTileHeight = state->ShadingRateTile.height;
		capabilities.Features.bShadingRateImage = state->bAttachmentShadingRate;
		capabilities.Features.bPrimitiveShadingRate = state->bPrimitiveShadingRate;
		capabilities.Features.bShadingRateNonTrivialCombiners = state->bNonTrivialShadingRateCombiners;
		capabilities.Features.bMeshShader = state->bMeshShader;
		capabilities.Features.bTaskShader = state->bTaskShader;
		capabilities.Limits.MaximumMeshGroups[0] = state->MaximumMeshGroups[0];
		capabilities.Limits.MaximumMeshGroups[1] = state->MaximumMeshGroups[1];
		capabilities.Limits.MaximumMeshGroups[2] = state->MaximumMeshGroups[2];
		capabilities.Limits.MaximumMeshGroupTotalCount = state->MaximumMeshGroupTotalCount;
		capabilities.Features.bMeshIndirect = state->bMeshIndirect;
		capabilities.Features.bSparseTextures = state->bSparseResidency;
		capabilities.Features.bBoundedPresentationWait = state->bSwapchainEnabled && state->bSwapchainMaintenance;
		capabilities.Limits.MaximumBindlessReadOnlyBuffers = 0;
		capabilities.Features.bBindlessTypedBuffers = state->bBindlessTypedBuffers;
		capabilities.Features.bShaderDrawParameters = state->bShaderDrawParameters;
		capabilities.Features.bIndependentBlend = state->bIndependentBlend;
		capabilities.Limits.MaximumTaskGroups[0] = state->bTaskShader ? meshProperties.maxTaskWorkGroupCount[0] : 0u;
		capabilities.Limits.MaximumTaskGroups[1] = state->bTaskShader ? meshProperties.maxTaskWorkGroupCount[1] : 0u;
		capabilities.Limits.MaximumTaskGroups[2] = state->bTaskShader ? meshProperties.maxTaskWorkGroupCount[2] : 0u;
		capabilities.Limits.MaximumTaskGroupTotalCount = state->bTaskShader ? meshProperties.maxTaskWorkGroupTotalCount : 0u;
		capabilities.Limits.MaximumMeshWorkGroupSize[0] = meshProperties.maxMeshWorkGroupSize[0];
		capabilities.Limits.MaximumMeshWorkGroupSize[1] = meshProperties.maxMeshWorkGroupSize[1];
		capabilities.Limits.MaximumMeshWorkGroupSize[2] = meshProperties.maxMeshWorkGroupSize[2];
		capabilities.Limits.MaximumMeshWorkGroupInvocations = meshProperties.maxMeshWorkGroupInvocations;
		capabilities.Limits.MaximumMeshOutputVertices = meshProperties.maxMeshOutputVertices;
		capabilities.Limits.MaximumMeshOutputPrimitives = meshProperties.maxMeshOutputPrimitives;
		capabilities.Limits.MaximumMeshOutputComponents = meshProperties.maxMeshOutputComponents;
		capabilities.Limits.MaximumMeshOutputByteCount =
			meshProperties.maxMeshOutputMemorySize < meshProperties.maxMeshPayloadAndOutputMemorySize
			? meshProperties.maxMeshOutputMemorySize
			: meshProperties.maxMeshPayloadAndOutputMemorySize;
		capabilities.Limits.MeshOutputPerVertexGranularity = meshProperties.meshOutputPerVertexGranularity;
		capabilities.Limits.MeshOutputPerPrimitiveGranularity = meshProperties.meshOutputPerPrimitiveGranularity;
		capabilities.Features.bMultiDrawIndirect = state->bMultiDrawIndirect;
		capabilities.Features.bIndirectFirstInstance = state->bIndirectFirstInstance;
		capabilities.Limits.MaximumTaskWorkGroupSize[0] = state->bTaskShader ? meshProperties.maxTaskWorkGroupSize[0] : 0u;
		capabilities.Limits.MaximumTaskWorkGroupSize[1] = state->bTaskShader ? meshProperties.maxTaskWorkGroupSize[1] : 0u;
		capabilities.Limits.MaximumTaskWorkGroupSize[2] = state->bTaskShader ? meshProperties.maxTaskWorkGroupSize[2] : 0u;
		capabilities.Limits.MaximumTaskWorkGroupInvocations = state->bTaskShader ? meshProperties.maxTaskWorkGroupInvocations : 0u;
		capabilities.Limits.MaximumTaskPayloadByteCount = state->bTaskShader ? meshProperties.maxTaskPayloadSize : 0u;
		capabilities.Limits.MaximumTaskSharedByteCount = state->bTaskShader ? meshProperties.maxTaskSharedMemorySize : 0u;
		capabilities.Limits.MaximumTaskPayloadAndSharedByteCount = state->bTaskShader ? meshProperties.maxTaskPayloadAndSharedMemorySize : 0u;
		capabilities.Limits.MaximumMeshSharedByteCount = state->bMeshShader ? meshProperties.maxMeshSharedMemorySize : 0u;
		capabilities.Limits.MaximumMeshPayloadAndSharedByteCount = state->bMeshShader ? meshProperties.maxMeshPayloadAndSharedMemorySize : 0u;
		capabilities.Limits.MaximumMeshPayloadAndOutputByteCount = state->bMeshShader ? meshProperties.maxMeshPayloadAndOutputMemorySize : 0u;
		capabilities.Features.bAccelerationStructure = state->bAccelerationStructure;
		capabilities.Features.bRayTracingPipeline = state->bRayTracingPipeline;
		capabilities.Limits.MaximumAccelGeometryCount = state->bAccelerationStructure ? state->AccelerationProperties.maxGeometryCount : 0;
		capabilities.Limits.MaximumAccelPrimitiveCount = state->bAccelerationStructure ? state->AccelerationProperties.maxPrimitiveCount : 0;
		capabilities.Limits.MaximumAccelInstanceCount = state->bAccelerationStructure ? state->AccelerationProperties.maxInstanceCount : 0;
		capabilities.Limits.MaximumRayRecursionDepth = state->bRayTracingPipeline ? state->RayTracingProperties.maxRayRecursionDepth : 0u;
		capabilities.Limits.MaximumRayAttributeByteCount = state->bRayTracingPipeline ? state->RayTracingProperties.maxRayHitAttributeSize : 0u;
		capabilities.Limits.MaximumRayDispatchInvocations = state->bRayTracingPipeline ? state->RayTracingProperties.maxRayDispatchInvocationCount : 0u;
		capabilities.Limits.MaximumRayDispatchDimensions[0] = state->bRayTracingPipeline ? uint64(state->DeviceProperties.limits.maxComputeWorkGroupCount[0]) *
			state->DeviceProperties.limits.maxComputeWorkGroupSize[0]
			: 0;
		capabilities.Limits.MaximumRayDispatchDimensions[1] =
			state->bRayTracingPipeline ? uint64(state->DeviceProperties.limits.maxComputeWorkGroupCount[1]) *
			state->DeviceProperties.limits.maxComputeWorkGroupSize[1]
			: 0;
		capabilities.Limits.MaximumRayDispatchDimensions[2] =
			state->bRayTracingPipeline ? uint64(state->DeviceProperties.limits.maxComputeWorkGroupCount[2]) *
			state->DeviceProperties.limits.maxComputeWorkGroupSize[2]
			: 0;
		capabilities.Features.bUniformBufferArrayDynamicIndexing = state->bUniformBufferArrayDynamicIndexing;
		capabilities.Features.bSampledImageArrayDynamicIndexing = state->bSampledImageArrayDynamicIndexing;
		capabilities.Features.bStorageBufferArrayDynamicIndexing = state->bStorageBufferArrayDynamicIndexing;
		capabilities.Features.bStorageImageArrayDynamicIndexing = state->bStorageImageArrayDynamicIndexing;
		capabilities.Limits.MaximumDescriptorTableAccelerationStructures =
			state->bAccelerationStructure ? Min(state->AccelerationProperties.maxDescriptorSetAccelerationStructures,
				state->AccelerationProperties.maxPerStageDescriptorAccelerationStructures)
			: 0u;
		capabilities.Features.bDeviceHeapIndexing = false;
		capabilities.Features.bShaderDemoteToHelperInvocation = state->bShaderDemoteToHelperInvocation;
		capabilities.Features.bSampleRateShading = state->bSampleRateShading;
		capabilities.Features.bSeparateDepthStencilAttachmentAccess = state->bSeparateDepthStencilAttachmentAccess;
		capabilities.Features.bShaderLocalSizeId = state->bMaintenance4;
		capabilities.Features.bNonzeroMipStencilViews = SupportsNonzeroMipStencilViews(*state);
		capabilities.Features.bGraphicsTimestamps = state->TimestampValidBits != 0 && state->DeviceProperties.limits.timestampPeriod > 0.0f;
		capabilities.Features.bComputeTimestamps = state->TimestampValidBits != 0 && state->DeviceProperties.limits.timestampPeriod > 0.0f && state->ComputeTimestampValidBits != 0;
		capabilities.Features.bCopyTimestamps =
			state->TimestampValidBits != 0 && state->DeviceProperties.limits.timestampPeriod > 0.0f &&
			state->CopyTimestampValidBits != 0 &&
			((state->CopyQueueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0 || state->bHostQueryReset);
		capabilities.Features.bDepthBounds = state->bDepthBounds;
		capabilities.Features.bTypedUavLoadAdditionalFormats = false;
		capabilities.Features.bShaderImageGatherExtended = state->bShaderImageGatherExtended;
		capabilities.Features.bShaderStorageImageReadWithoutFormat = state->bShaderStorageImageReadWithoutFormat;
		capabilities.Features.bShaderStorageImageWriteWithoutFormat = state->bShaderStorageImageWriteWithoutFormat;
		capabilities.Features.bShaderFloat16 = state->bShaderFloat16;
		capabilities.Features.ComputeSubgroupOperations = state->ComputeSubgroupOperations;
		capabilities.Features.bShaderStorageImageExtendedFormats = state->bShaderStorageImageExtendedFormats;
		capabilities.Features.PixelSubgroupOperations = state->FragmentSubgroupOperations;

		return Result<BackendData>::Success(
			{ state,
			 capabilities,
			 state->MaximumBindlessSampledTextures,
			 state->MaximumBindlessSamplers,
			 state->MaximumBindlessStorageBuffers,
			 state->MaximumBindlessStorageTextures,
			 state->MaximumBindlessUniformBuffers,
			 state->bGraphicsStorageWrites ? state->MaximumBindlessStorageBuffers : 0,
			 state->bGraphicsStorageWrites ? state->MaximumBindlessStorageTextures : 0,
			 {EBackend::Vulkan, state->Device, state->Queue, state->Instance, state->Physical, state->QueueFamily, state->ApiVersion},
			 reinterpret_cast<void*>(state->ComputeQueue),
			 reinterpret_cast<void*>(state->CopyQueue),
			 state->bAccelerationStructure,
			 state->bRayQuery,
			 state->bRayTracingPipeline,
			 false,
			 state->ComputeQueueFamily,
			 state->CopyQueueFamily,
			 false,
			 state->bImageSlicedViewOf3D,
			 state->bCooperativeVector,
			 state->bCooperativeVectorTraining,
			 state->bOpacityMicromap,
			 state->bRayTracingClusters,
			 state->Diagnostics,
			 state->bAftermathAttached });
	}

	[[nodiscard]] Result<BackendData> CreateExternalVulkanBackend(
		MemoryResource& resource,
		NativeDeviceHandles external,
		NativeQueueHandle externalComputeQueue,
		NativeQueueHandle externalCopyQueue,
		ExternalVulkanFeatures enabledFeatures)
	{
		ASSERT(external.Instance != nullptr && external.PhysicalDevice != nullptr && external.QueueFamilyIndex != UINT32_MAX,
			"An external Vulkan device needs an instance, physical device, and queue family.");
		State* state = CreateObject<State>(resource, resource);

		if (state == nullptr)
		{
			return Result<BackendData>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan state."));
		}
		state->bExternalDevice = true;
		state->bNativeQueuesExposed = true;
		state->bIdleConfirmed = false;
		state->bExternalMemoryEnabled = enabledFeatures.bExternalMemory;
		state->bExternalSemaphoreEnabled = enabledFeatures.bExternalTimelineSemaphore;
		state->bMemoryBudget = enabledFeatures.bMemoryBudget;
		state->ApiVersion = external.ApiVersion != 0 ? external.ApiVersion : VK_API_VERSION_1_2;
		state->Instance = reinterpret_cast<VkInstance>(external.Instance);
		state->Physical = reinterpret_cast<VkPhysicalDevice>(external.PhysicalDevice);
		state->Device = reinterpret_cast<VkDevice>(external.Device);
		state->Queue = reinterpret_cast<VkQueue>(external.Queue);

		if (externalComputeQueue.Handle != nullptr)
		{
			state->ComputeQueue = reinterpret_cast<VkQueue>(externalComputeQueue.Handle);
			state->bSeparateComputeQueue = state->ComputeQueue != state->Queue;
		}

		if (externalCopyQueue.Handle != nullptr)
		{
			state->CopyQueue = reinterpret_cast<VkQueue>(externalCopyQueue.Handle);
			state->bSeparateCopyQueue = state->CopyQueue != state->Queue;
		}
		state->QueueFamily = external.QueueFamilyIndex;
		state->ComputeQueueFamily = externalComputeQueue.Handle != nullptr ? externalComputeQueue.QueueFamilyIndex : state->QueueFamily;
		state->CopyQueueFamily = externalCopyQueue.Handle != nullptr ? externalCopyQueue.QueueFamilyIndex : state->QueueFamily;
		state->ResourceQueueFamilies[0] = state->QueueFamily;
		state->ResourceQueueFamilyCount = 1;

		if (!LoadFunctions(*state) || !LoadInstanceFunctions(*state))
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.VulkanExternalDevice", "Could not load functions for the external Vulkan instance."));
		}

		uint32 physicalCount = 0;

		if (state->F.EnumeratePhysicalDevices(state->Instance, &physicalCount, nullptr) != VK_SUCCESS || physicalCount == 0)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::InvalidArgument, "Rhi.VulkanExternalDevice", "The external instance has no physical devices.");
		}

		Array<VkPhysicalDevice> physical(resource);

		if (!physical.ResizeUninitialized(physicalCount) || state->F.EnumeratePhysicalDevices(state->Instance, &physicalCount, physical.Data()) != VK_SUCCESS)
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.VulkanExternalDevice", "Could not enumerate external physical devices."));
		}
		bool bFound = false;

		for (VkPhysicalDevice candidate : physical)
		{
			bFound |= candidate == state->Physical;
		}

		if (!bFound)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::InvalidArgument, "Rhi.VulkanExternalDevice", "The physical device does not belong to the external instance.");
		}
		VkPhysicalDeviceProperties properties{};
		state->F.GetPhysicalDeviceProperties(state->Physical, &properties);
		uint32 familyCount = 0;
		state->F.GetQueueFamilies(state->Physical, &familyCount, nullptr);
		Array<VkQueueFamilyProperties> families(resource);

		if (properties.apiVersion < VK_API_VERSION_1_2 || state->QueueFamily >= familyCount || !families.ResizeUninitialized(familyCount))
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalDevice", "The external device needs Vulkan 1.2 and a valid graphics-compute queue family.");
		}
		state->F.GetQueueFamilies(state->Physical, &familyCount, families.Data());
		VkQueueFamilyProperties const& queueFamily = families[state->QueueFamily];

		if (queueFamily.queueCount == 0 || (queueFamily.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalDevice", "The external queue family must support graphics and compute.");
		}

		if (state->ComputeQueueFamily >= familyCount ||
			state->CopyQueueFamily >= familyCount ||
			families[state->ComputeQueueFamily].queueCount == 0 ||
			families[state->CopyQueueFamily].queueCount == 0 ||
			(families[state->ComputeQueueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0 ||
			(families[state->CopyQueueFamily].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) == 0)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalQueue", "The external compute or copy queue family lacks the required capability.");
		}

		if (state->ComputeQueueFamily != state->QueueFamily)
		{
			state->ResourceQueueFamilies[state->ResourceQueueFamilyCount++] = state->ComputeQueueFamily;
		}

		if (state->CopyQueueFamily != state->QueueFamily && state->CopyQueueFamily != state->ComputeQueueFamily)
		{
			state->ResourceQueueFamilies[state->ResourceQueueFamilyCount++] = state->CopyQueueFamily;
		}
		state->TimestampValidBits = queueFamily.timestampValidBits;
		state->ComputeTimestampValidBits = families[state->ComputeQueueFamily].timestampValidBits;
		state->CopyTimestampValidBits = families[state->CopyQueueFamily].timestampValidBits;
		state->GraphicsQueueFlags = queueFamily.queueFlags;
		state->ComputeQueueFlags = families[state->ComputeQueueFamily].queueFlags;
		state->CopyQueueFlags = families[state->CopyQueueFamily].queueFlags;

		if (enabledFeatures.bMaintenance4)
		{
			bool bAvailable = state->ApiVersion >= VK_API_VERSION_1_3 && properties.apiVersion >= VK_API_VERSION_1_3;
			if (!bAvailable)
			{
				uint32 count = 0;
				Array<VkExtensionProperties> extensions(resource);
				if (state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &count, nullptr) != VK_SUCCESS ||
					!extensions.ResizeUninitialized(count) ||
					state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &count, extensions.Data()) != VK_SUCCESS)
				{
					Destroy(state);

					return Result<BackendData>::Failure(Failure("Rhi.VulkanMaintenance4", "Could not verify declared maintenance4 support."));
				}
				for (VkExtensionProperties const& extension : extensions)
				{
					bAvailable |= CompareCString(extension.extensionName, VK_KHR_MAINTENANCE_4_EXTENSION_NAME) == 0;
				}
			}
			VkPhysicalDeviceMaintenance4Features maintenance4{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES };
			VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features.pNext = &maintenance4;
			if (bAvailable)
			{
				state->F.GetPhysicalDeviceFeatures2(state->Physical, &features);
			}
			if (!maintenance4.maintenance4)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanMaintenance4", "The declared maintenance4 feature is unavailable.");
			}
			state->bMaintenance4 = true;
		}

		if (enabledFeatures.bDynamicRendering)
		{
			bool bAvailable = state->ApiVersion >= VK_API_VERSION_1_3 && properties.apiVersion >= VK_API_VERSION_1_3;
			if (!bAvailable)
			{
				uint32 count = 0;
				Array<VkExtensionProperties> extensions(resource);
				if (state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &count, nullptr) != VK_SUCCESS ||
					!extensions.ResizeUninitialized(count) ||
					state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &count, extensions.Data()) != VK_SUCCESS)
				{
					Destroy(state);

					return Result<BackendData>::Failure(Failure("Rhi.VulkanDynamicRendering", "Could not verify declared dynamic rendering support."));
				}
				for (VkExtensionProperties const& extension : extensions)
				{
					bAvailable |= CompareCString(extension.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0;
				}
			}
			VkPhysicalDeviceDynamicRenderingFeatures dynamic{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
			VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features.pNext = &dynamic;
			if (bAvailable)
			{
				state->F.GetPhysicalDeviceFeatures2(state->Physical, &features);
			}
			if (!dynamic.dynamicRendering || !state->F.CmdBeginRendering || !state->F.CmdEndRendering)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanDynamicRendering", "The declared dynamic rendering feature is unavailable.");
			}
			state->bDynamicRendering = true;
		}

		if (enabledFeatures.bMaintenance7)
		{
			uint32 count = 0;
			Array<VkExtensionProperties> extensions(resource);
			bool bAvailable = false;
			if (state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &count, nullptr) != VK_SUCCESS ||
				!extensions.ResizeUninitialized(count) ||
				state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &count, extensions.Data()) != VK_SUCCESS)
			{
				Destroy(state);

				return Result<BackendData>::Failure(Failure("Rhi.VulkanMaintenance7", "Could not query declared maintenance7 support."));
			}
			for (VkExtensionProperties const& extension : extensions)
			{
				bAvailable |= CompareCString(extension.extensionName, VK_KHR_MAINTENANCE_7_EXTENSION_NAME) == 0;
			}
			VkPhysicalDeviceMaintenance7FeaturesKHR features7{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_FEATURES_KHR };
			VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features.pNext = &features7;
			VkPhysicalDeviceMaintenance7PropertiesKHR properties7{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_PROPERTIES_KHR };
			VkPhysicalDeviceProperties2 properties2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			properties2.pNext = &properties7;
			if (bAvailable)
			{
				state->F.GetPhysicalDeviceFeatures2(state->Physical, &features);
				state->F.GetPhysicalDeviceProperties2(state->Physical, &properties2);
			}
			if (!features7.maintenance7)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanMaintenance7", "The declared maintenance7 feature is unavailable.");
			}
			state->bSeparateDepthStencilAttachmentAccess = properties7.separateDepthStencilAttachmentAccess;
		}

		if (enabledFeatures.bLoadStoreOpNone)
		{
			uint32 count = 0;
			Array<VkExtensionProperties> extensions(resource);
			if (state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &count, nullptr) != VK_SUCCESS ||
				!extensions.ResizeUninitialized(count) ||
				state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &count, extensions.Data()) != VK_SUCCESS)
			{
				Destroy(state);

				return Result<BackendData>::Failure(Failure("Rhi.VulkanExternalResolve", "Could not verify load/store NONE support."));
			}
			for (VkExtensionProperties const& extension : extensions)
			{
				state->bLoadStoreOpNone |= CompareCString(extension.extensionName, VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME) == 0;
			}
			if (!state->bLoadStoreOpNone)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalResolve", "The declared load/store NONE extension is unavailable.");
			}
		}

		if (enabledFeatures.bShaderDemoteToHelperInvocation)
		{
			bool bAvailable = state->ApiVersion >= VK_API_VERSION_1_3 && properties.apiVersion >= VK_API_VERSION_1_3;
			if (!bAvailable)
			{
				uint32 extensionCount = 0;
				Array<VkExtensionProperties> extensions(resource);
				if (state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &extensionCount, nullptr) != VK_SUCCESS ||
					!extensions.ResizeUninitialized(extensionCount) ||
					state->F.EnumerateDeviceExtensions(state->Physical, nullptr, &extensionCount, extensions.Data()) != VK_SUCCESS)
				{
					Destroy(state);

					return Result<BackendData>::Failure(Failure("Rhi.VulkanExternalShaderDemote", "Could not query shader demote extension support."));
				}
				for (VkExtensionProperties const& extension : extensions)
				{
					bAvailable |= CompareCString(extension.extensionName, VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME) == 0;
				}
			}
			VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures supportedDemote{
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES };
			if (bAvailable)
			{
				VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
				features.pNext = &supportedDemote;
				state->F.GetPhysicalDeviceFeatures2(state->Physical, &features);
			}
			if (!supportedDemote.shaderDemoteToHelperInvocation)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalShaderDemote", "The physical device does not support the declared shader demote feature.");
			}
			state->bShaderDemoteToHelperInvocation = true;
		}

		if (enabledFeatures.bSparseResidency ||
			enabledFeatures.bHostQueryReset ||
			enabledFeatures.bBufferDeviceAddress ||
			enabledFeatures.bShaderInt64 ||
			enabledFeatures.bShaderDrawParameters ||
			enabledFeatures.bSampleRateShading ||
			enabledFeatures.bShaderImageGatherExtended ||
			enabledFeatures.bShaderFloat16 ||
			enabledFeatures.bShaderStorageImageExtendedFormats ||
			enabledFeatures.bShaderStorageImageReadWithoutFormat ||
			enabledFeatures.bShaderStorageImageWriteWithoutFormat ||
			enabledFeatures.bIndependentBlend ||
			enabledFeatures.bMultiDrawIndirect ||
			enabledFeatures.bIndirectFirstInstance ||
			enabledFeatures.bUniformBufferArrayDynamicIndexing ||
			enabledFeatures.bSampledImageArrayDynamicIndexing ||
			enabledFeatures.bStorageBufferArrayDynamicIndexing ||
			enabledFeatures.bStorageImageArrayDynamicIndexing ||
			enabledFeatures.bMeshShader ||
			enabledFeatures.bTaskShader ||
			enabledFeatures.bAccelerationStructure ||
			enabledFeatures.bRayQuery ||
			enabledFeatures.bRayTracingPipeline ||
			enabledFeatures.bRayTracingClusters ||
			enabledFeatures.bOpacityMicromap ||
			enabledFeatures.bFragmentShadingRate ||
			enabledFeatures.bAttachmentShadingRate ||
			enabledFeatures.bPrimitiveShadingRate ||
			enabledFeatures.bImageSlicedViewOf3D ||
			enabledFeatures.bCooperativeVector ||
			enabledFeatures.bCooperativeVectorTraining)
		{
			VkPhysicalDeviceVulkan11Features supported11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
			VkPhysicalDeviceVulkan12Features supported12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			VkPhysicalDeviceAccelerationStructureFeaturesKHR supportedAcceleration{
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
			VkPhysicalDeviceRayQueryFeaturesKHR supportedRayQuery{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
			VkPhysicalDeviceRayTracingPipelineFeaturesKHR supportedRayPipeline{
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
			VkPhysicalDeviceMeshShaderFeaturesEXT supportedMesh{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
			VkPhysicalDeviceFragmentShadingRateFeaturesKHR supportedShadingRate{
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR };
			VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT supportedSliced3DViews{
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_SLICED_VIEW_OF_3D_FEATURES_EXT };
			VkPhysicalDeviceCooperativeVectorFeaturesNV supportedCoopVec{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV };
			VkPhysicalDeviceClusterAccelerationStructureFeaturesNV supportedClusters{
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV };
			VkPhysicalDeviceFeatures2 supported{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			supported.pNext = &supported11;
			supported11.pNext = &supported12;
			void* const optionalChain = enabledFeatures.bAccelerationStructure ? static_cast<void*>(&supportedAcceleration)
				: enabledFeatures.bMeshShader ? static_cast<void*>(&supportedMesh)
				: enabledFeatures.bFragmentShadingRate ? static_cast<void*>(&supportedShadingRate)
				: enabledFeatures.bCooperativeVector ? static_cast<void*>(&supportedCoopVec)
				: nullptr;
			supported12.pNext = enabledFeatures.bImageSlicedViewOf3D ? static_cast<void*>(&supportedSliced3DViews) : optionalChain;
			supportedSliced3DViews.pNext = optionalChain;
			supportedAcceleration.pNext = enabledFeatures.bRayQuery ? static_cast<void*>(&supportedRayQuery)
				: enabledFeatures.bRayTracingPipeline ? static_cast<void*>(&supportedRayPipeline)
				: enabledFeatures.bMeshShader ? static_cast<void*>(&supportedMesh)
				: enabledFeatures.bFragmentShadingRate ? static_cast<void*>(&supportedShadingRate)
				: enabledFeatures.bCooperativeVector ? static_cast<void*>(&supportedCoopVec)
				: nullptr;
			supportedRayQuery.pNext = enabledFeatures.bRayTracingPipeline ? static_cast<void*>(&supportedRayPipeline)
				: enabledFeatures.bMeshShader ? static_cast<void*>(&supportedMesh)
				: enabledFeatures.bFragmentShadingRate ? static_cast<void*>(&supportedShadingRate)
				: enabledFeatures.bCooperativeVector ? static_cast<void*>(&supportedCoopVec)
				: nullptr;
			supportedRayPipeline.pNext = enabledFeatures.bMeshShader ? static_cast<void*>(&supportedMesh)
				: enabledFeatures.bFragmentShadingRate ? static_cast<void*>(&supportedShadingRate)
				: enabledFeatures.bCooperativeVector ? static_cast<void*>(&supportedCoopVec)
				: nullptr;
			supportedMesh.pNext = enabledFeatures.bFragmentShadingRate ? static_cast<void*>(&supportedShadingRate)
				: enabledFeatures.bCooperativeVector ? static_cast<void*>(&supportedCoopVec)
				: nullptr;
			supportedShadingRate.pNext = enabledFeatures.bCooperativeVector ? static_cast<void*>(&supportedCoopVec)
				: enabledFeatures.bRayTracingClusters ? static_cast<void*>(&supportedClusters)
				: nullptr;
			supportedCoopVec.pNext = enabledFeatures.bRayTracingClusters ? static_cast<void*>(&supportedClusters) : nullptr;
			state->F.GetPhysicalDeviceFeatures2(state->Physical, &supported);

			if (enabledFeatures.bHostQueryReset && !supported12.hostQueryReset)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalTimer", "The physical device does not support host query reset.");
			}

			if (enabledFeatures.bSparseResidency &&
				(!supported.features.sparseBinding ||
				 !supported.features.sparseResidencyImage2D ||
				 !supported.features.sparseResidencyAliased ||
				 (queueFamily.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) == 0))
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalSparse", "Sparse residency needs physical support and a sparse binding graphics queue.");
			}

			if (enabledFeatures.bBufferDeviceAddress && !supported12.bufferDeviceAddress)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalAddress", "The physical device does not support buffer device address.");
			}

			if ((enabledFeatures.bMultiDrawIndirect && !supported.features.multiDrawIndirect) || (enabledFeatures.bIndirectFirstInstance && !supported.features.drawIndirectFirstInstance))
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalIndirect", "The physical device does not support the declared indirect draw features.");
			}

			if ((enabledFeatures.bUniformBufferArrayDynamicIndexing && !supported.features.shaderUniformBufferArrayDynamicIndexing) ||
				(enabledFeatures.bSampledImageArrayDynamicIndexing && !supported.features.shaderSampledImageArrayDynamicIndexing) ||
				(enabledFeatures.bStorageBufferArrayDynamicIndexing && !supported.features.shaderStorageBufferArrayDynamicIndexing) ||
				(enabledFeatures.bStorageImageArrayDynamicIndexing && !supported.features.shaderStorageImageArrayDynamicIndexing))
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalFeatures", "An external dynamic descriptor indexing feature is not physically supported.");
			}

			if (enabledFeatures.bShaderImageGatherExtended && !supported.features.shaderImageGatherExtended)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalGather", "The physical device does not support extended image gathers.");
			}

			if (enabledFeatures.bShaderStorageImageExtendedFormats && !supported.features.shaderStorageImageExtendedFormats)
			{
				Destroy(state);

				return Result<BackendData>::Failure(
					EErrorCode::Unsupported,
					"Rhi.VulkanExternalStorageExtendedFormats",
					"The physical device does not support the declared extended storage-image formats.");
			}

			if ((enabledFeatures.bShaderStorageImageReadWithoutFormat && !supported.features.shaderStorageImageReadWithoutFormat) ||
				(enabledFeatures.bShaderStorageImageWriteWithoutFormat && !supported.features.shaderStorageImageWriteWithoutFormat))
			{
				Destroy(state);

				return Result<BackendData>::Failure(
					EErrorCode::Unsupported,
					"Rhi.VulkanExternalStorageFormat",
					"The physical device does not support the declared storage-image format features.");
			}

			if (enabledFeatures.bSampleRateShading && !supported.features.sampleRateShading)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalSampleShading", "The physical device does not support sample-rate shading.");
			}
			if (enabledFeatures.bDepthBounds && !supported.features.depthBounds)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalDepthBounds", "The physical device does not support the declared depth bounds feature.");
			}

			if (enabledFeatures.bIndependentBlend && !supported.features.independentBlend)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalIndependentBlend", "The physical device does not support independent color blending.");
			}

			if (enabledFeatures.bShaderDrawParameters && !supported11.shaderDrawParameters)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalShaderDrawParameters", "The physical device does not support shader draw parameters.");
			}

			if (enabledFeatures.bShaderInt64 && !supported.features.shaderInt64)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalShaderInt64", "The physical device does not support 64-bit shader integers.");
			}
			if (enabledFeatures.bShaderFloat16 && (state->ApiVersion < VK_API_VERSION_1_2 || !supported12.shaderFloat16))
			{
				Destroy(state);

				return Result<BackendData>::Failure(
					EErrorCode::Unsupported,
					"Rhi.VulkanExternalShaderFloat16",
					"The declared Vulkan 1.2 native sixteen-bit shader arithmetic is unavailable.");
			}

			if (enabledFeatures.bMeshShader && (!supportedMesh.meshShader || (enabledFeatures.bTaskShader && !supportedMesh.taskShader)))
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalMesh", "The physical device does not support the declared mesh or task shader feature.");
			}

			if (enabledFeatures.bAccelerationStructure && !supportedAcceleration.accelerationStructure)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalAcceleration", "The physical device does not support acceleration structures.");
			}

			if (enabledFeatures.bRayQuery && !supportedRayQuery.rayQuery)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalRayQuery", "The physical device does not support ray queries.");
			}

			if (enabledFeatures.bRayTracingPipeline && !supportedRayPipeline.rayTracingPipeline)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalRayPipeline", "The physical device does not support ray tracing pipelines.");
			}

			if (enabledFeatures.bOpacityMicromap)
			{
				VkPhysicalDeviceOpacityMicromapFeaturesEXT supportedMicromap{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT };
				VkPhysicalDeviceFeatures2 micromapFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
				micromapFeatures.pNext = &supportedMicromap;
				state->F.GetPhysicalDeviceFeatures2(state->Physical, &micromapFeatures);

				if (!supportedMicromap.micromap)
				{
					Destroy(state);

					return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalMicromap", "The physical device does not support opacity micromaps.");
				}
			}

			if (enabledFeatures.bRayTracingClusters)
			{
				VkPhysicalDeviceFeatures2 clusterFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
				clusterFeatures.pNext = &supportedClusters;
				state->F.GetPhysicalDeviceFeatures2(state->Physical, &clusterFeatures);

				if (!enabledFeatures.bAccelerationStructure || !enabledFeatures.bBufferDeviceAddress || !supportedClusters.clusterAccelerationStructure)
				{
					Destroy(state);

					return Result<BackendData>::Failure(
						EErrorCode::Unsupported,
						"Rhi.VulkanExternalClusters",
						"The external device lacks the declared cluster acceleration structure feature.");
				}
			}

			if (enabledFeatures.bFragmentShadingRate &&
				(!supportedShadingRate.pipelineFragmentShadingRate ||
				 (enabledFeatures.bAttachmentShadingRate && !supportedShadingRate.attachmentFragmentShadingRate) ||
				 (enabledFeatures.bPrimitiveShadingRate && !supportedShadingRate.primitiveFragmentShadingRate)))
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalShadingRate", "The physical device does not support the declared shading rate features.");
			}

			if (enabledFeatures.bImageSlicedViewOf3D && !supportedSliced3DViews.imageSlicedViewOf3D)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalSliced3DView", "The physical device does not support sliced 3D image views.");
			}

			if (enabledFeatures.bCooperativeVector && (!supportedCoopVec.cooperativeVector || (enabledFeatures.bCooperativeVectorTraining && !supportedCoopVec.cooperativeVectorTraining)))
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalCoopVec", "The physical device lacks the declared cooperative vector features.");
			}
			state->bSparseResidency = enabledFeatures.bSparseResidency;
			state->bHostQueryReset = enabledFeatures.bHostQueryReset;
			state->bBufferDeviceAddress = enabledFeatures.bBufferDeviceAddress;
			state->bShaderInt64 = enabledFeatures.bShaderInt64;
			state->bShaderDrawParameters = enabledFeatures.bShaderDrawParameters;
			state->bSampleRateShading = enabledFeatures.bSampleRateShading;
			state->bShaderImageGatherExtended = enabledFeatures.bShaderImageGatherExtended;
			state->bShaderStorageImageExtendedFormats = enabledFeatures.bShaderStorageImageExtendedFormats;
			state->bShaderStorageImageReadWithoutFormat = enabledFeatures.bShaderStorageImageReadWithoutFormat;
			state->bShaderStorageImageWriteWithoutFormat = enabledFeatures.bShaderStorageImageWriteWithoutFormat;
			state->bShaderFloat16 = enabledFeatures.bShaderFloat16;
			state->bDepthBounds = enabledFeatures.bDepthBounds;
			state->bIndependentBlend = enabledFeatures.bIndependentBlend;
			state->bMultiDrawIndirect = enabledFeatures.bMultiDrawIndirect;
			state->bIndirectFirstInstance = enabledFeatures.bIndirectFirstInstance;
			state->bUniformBufferArrayDynamicIndexing = enabledFeatures.bUniformBufferArrayDynamicIndexing;
			state->bSampledImageArrayDynamicIndexing = enabledFeatures.bSampledImageArrayDynamicIndexing;
			state->bStorageBufferArrayDynamicIndexing = enabledFeatures.bStorageBufferArrayDynamicIndexing;
			state->bStorageImageArrayDynamicIndexing = enabledFeatures.bStorageImageArrayDynamicIndexing;
			state->bMeshShader = enabledFeatures.bMeshShader;
			state->bTaskShader = enabledFeatures.bTaskShader;
			state->bAccelerationStructure = enabledFeatures.bAccelerationStructure;
			state->bRayQuery = enabledFeatures.bRayQuery;
			state->bRayTracingPipeline = enabledFeatures.bRayTracingPipeline;
			state->bOpacityMicromap = enabledFeatures.bOpacityMicromap;
			state->bRayTracingClusters = enabledFeatures.bRayTracingClusters;
			state->bAttachmentShadingRate = enabledFeatures.bAttachmentShadingRate;
			state->bPrimitiveShadingRate = enabledFeatures.bPrimitiveShadingRate;
			state->bImageSlicedViewOf3D = enabledFeatures.bImageSlicedViewOf3D;
			state->bCooperativeVector = enabledFeatures.bCooperativeVector;
			state->bCooperativeVectorTraining = enabledFeatures.bCooperativeVectorTraining;
		}

		if (enabledFeatures.bFragmentShadingRate)
		{
			state->F.CmdSetFragmentShadingRate = reinterpret_cast<PFN_vkCmdSetFragmentShadingRateKHR>( state->F.GetDeviceProcAddr(state->Device, "vkCmdSetFragmentShadingRateKHR"));

			if (state->F.CmdSetFragmentShadingRate == nullptr || state->F.GetFragmentShadingRates == nullptr)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalShadingRate", "The external device does not expose fragment shading rate commands.");
			}
		}

		if (state->bMeshShader)
		{
			state->F.CmdDrawMeshTasks = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(state->F.GetDeviceProcAddr(state->Device, "vkCmdDrawMeshTasksEXT"));
			state->F.CmdDrawMeshTasksIndirect = reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectEXT>( state->F.GetDeviceProcAddr(state->Device, "vkCmdDrawMeshTasksIndirectEXT"));

			if (state->F.CmdDrawMeshTasks == nullptr)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.VulkanExternalMesh", "The external device does not expose mesh shader commands.");
			}
			state->bMeshIndirect = state->F.CmdDrawMeshTasksIndirect != nullptr;
		}

		return FinishVulkanBackend(state, resource);
	}
}
