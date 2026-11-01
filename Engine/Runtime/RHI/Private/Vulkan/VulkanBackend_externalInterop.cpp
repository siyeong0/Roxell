#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] bool ExternalMemoryFeatures(VkExternalMemoryFeatureFlags flags) noexcept
	{
		constexpr const int required = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;

		return (flags & required) == required;
	}

	[[nodiscard]] bool ExternalBufferSupported(const State& state, VkBufferUsageFlags usage)
	{
		if (!state.bExternalMemoryEnabled)
		{
			return false;
		}

		const PFN_vkGetPhysicalDeviceExternalBufferProperties query = reinterpret_cast<PFN_vkGetPhysicalDeviceExternalBufferProperties>(
			state.F.GetInstanceProcAddr(state.Instance, "vkGetPhysicalDeviceExternalBufferProperties"));

		if (query == nullptr)
		{
			return false;
		}
		VkPhysicalDeviceExternalBufferInfo info{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO };
		info.usage = usage;
		info.handleType = VulkanExternalMemoryType();
		VkExternalBufferProperties properties{ VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES };
		query(state.Physical, &info, &properties);

		return ExternalMemoryFeatures(properties.externalMemoryProperties.externalMemoryFeatures);
	}

	[[nodiscard]] bool ExternalSemaphoreSupported(const State& state)
	{
		if (!state.bExternalSemaphoreEnabled)
		{
			return false;
		}

		const PFN_vkGetPhysicalDeviceExternalSemaphoreProperties query =
			reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(
				state.F.GetInstanceProcAddr(state.Instance, "vkGetPhysicalDeviceExternalSemaphoreProperties"));

		if (query == nullptr)
		{
			return false;
		}
		VkSemaphoreTypeCreateInfo timeline{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
		timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		VkPhysicalDeviceExternalSemaphoreInfo info{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO };
		info.pNext = &timeline;
		info.handleType = VulkanExternalSemaphoreType();
		VkExternalSemaphoreProperties properties{ VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES };
		query(state.Physical, &info, &properties);
		constexpr const int required = VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;

		return (properties.externalSemaphoreFeatures & required) == required;
	}

	[[nodiscard]] ExternalInteropCapabilities GetExternalInteropCapabilities(const State* opaque)
	{
		const State& state = *opaque;
		ExternalInteropCapabilities result;
		VkPhysicalDeviceIDProperties identity{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
		VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		properties.pNext = &identity;
		state.F.GetPhysicalDeviceProperties2(state.Physical, &properties);
		CopyBytes(result.Device.DeviceUuid, identity.deviceUUID, sizeof(result.Device.DeviceUuid));
		CopyBytes(result.Device.DeviceLuid, identity.deviceLUID, sizeof(result.Device.DeviceLuid));
		result.Device.bUuidValid = true;
		result.Device.bLuidValid = identity.deviceLUIDValid == VK_TRUE;
		result.Device.NodeMask = identity.deviceNodeMask;
		CopyBytes(result.DriverBuildUuid, identity.driverUUID, sizeof(result.DriverBuildUuid));
		result.AdapterDriverVersion = properties.properties.driverVersion;
		result.bDriverBuildUuidValid = true;
		result.bAdapterDriverVersionValid = true;
		result.bBuffers = ExternalBufferSupported(state, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		result.bTextures = state.bExternalMemoryEnabled;
		result.bTimelineFences = ExternalSemaphoreSupported(state);

		if (result.bBuffers || result.bTextures)
		{
			result.MemoryHandleType = VulkanExternalHandleType();
		}

		if (result.bTimelineFences)
		{
			result.FenceHandleType = VulkanExternalHandleType();
		}

		return result;
	}

	[[nodiscard]] Result<ExternalHandle> DuplicateExternalHandle(ExternalHandleView source)
	{
		uint64 value = 0;
#ifdef _WIN32
		HANDLE duplicated = nullptr;

		if (!DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(static_cast<uintptr_t>(source.Value)), GetCurrentProcess(),
			&duplicated, 0, FALSE, DUPLICATE_SAME_ACCESS))
		{
			return Result<ExternalHandle>::Failure(Failure("Rhi.ExternalHandle", "Could not duplicate the shared NT handle."));
		}
		value = static_cast<uint64>(reinterpret_cast<uintptr_t>(duplicated));
#else
		const int duplicated = fcntl(static_cast<int>(source.Value), F_DUPFD_CLOEXEC, 0);

		if (duplicated < 0)
		{
			return Result<ExternalHandle>::Failure(Failure("Rhi.ExternalHandle", "Could not duplicate the shared file descriptor."));
		}
		value = static_cast<uint64>(duplicated);
#endif
		return Result<ExternalHandle>::Success(ExternalHandle::Adopt(VulkanExternalHandleType(), value));
	}

	[[nodiscard]] Result<VkDeviceMemory> AllocateExternalMemory(
		State& state,
		const VkMemoryRequirements& requirements,
		VkBuffer buffer,
		VkImage image,
		bool bDeviceAddress,
		const NativeExternalMemoryView* imported,
		uint32& memoryType)
	{
		memoryType = UINT32_MAX;

		if (imported != nullptr)
		{
			if (imported->Handle.Type != VulkanExternalHandleType() ||
				imported->AllocationBytes != requirements.size ||
				imported->MemoryTypeIndex >= state.MemoryProperties.memoryTypeCount ||
				(requirements.memoryTypeBits & (1u << imported->MemoryTypeIndex)) == 0)
			{
				return Result<VkDeviceMemory>::Failure(EErrorCode::InvalidArgument, "Rhi.ExternalMemory", "The opaque allocation type, size or memory type does not match this resource.");
			}
			memoryType = imported->MemoryTypeIndex;
		}
		else
		{
			for (uint32 index = 0; index < state.MemoryProperties.memoryTypeCount; ++index)
			{
				if ((requirements.memoryTypeBits & (1u << index)) == 0)
				{
					continue;
				}

				if (memoryType == UINT32_MAX)
				{
					memoryType = index;
				}

				if (state.MemoryProperties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
				{
					memoryType = index;
					break;
				}
			}
		}

		if (memoryType == UINT32_MAX)
		{
			return Result<VkDeviceMemory>::Failure(Failure("Rhi.ExternalMemory", "No compatible memory type exists."));
		}
		VkMemoryDedicatedAllocateInfo dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
		dedicated.buffer = buffer;
		dedicated.image = image;
		VkMemoryAllocateFlagsInfo flags{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
		flags.flags = bDeviceAddress ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;
		flags.pNext = &dedicated;
		VkExportMemoryAllocateInfo exportInfo{ VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
		exportInfo.handleTypes = VulkanExternalMemoryType();
		exportInfo.pNext = &flags;
		VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		allocation.pNext = &exportInfo;
		allocation.allocationSize = requirements.size;
		allocation.memoryTypeIndex = memoryType;
#ifdef _WIN32
		VkImportMemoryWin32HandleInfoKHR importInfo{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
		importInfo.handleType = VulkanExternalMemoryType();

		if (imported != nullptr)
		{
			importInfo.handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(imported->Handle.Value));
			// Keep import separate from a new export allocation request. Re-export
			// duplicates the retained original OS handle without changing the payload.
			importInfo.pNext = &flags;
			allocation.pNext = &importInfo;
		}
#else
		VkImportMemoryFdInfoKHR importInfo{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
		importInfo.handleType = VulkanExternalMemoryType();
		importInfo.fd = -1;

		if (imported != nullptr)
		{
			if (imported->Handle.Value > INT32_MAX || (importInfo.fd = fcntl(static_cast<int>(imported->Handle.Value), F_DUPFD_CLOEXEC, 0)) < 0)
			{
				return Result<VkDeviceMemory>::Failure(Failure("Rhi.ExternalMemory", "Could not duplicate the borrowed memory FD."));
			}
			// Keep import separate from a new export allocation request. Re-export
			// duplicates the retained original OS handle without changing the payload.
			importInfo.pNext = &flags;
			allocation.pNext = &importInfo;
		}
#endif
		VkDeviceMemory memory = VK_NULL_HANDLE;

		if (state.F.AllocateMemory(state.Device, &allocation, nullptr, &memory) != VK_SUCCESS)
		{
#ifndef _WIN32
			if (importInfo.fd >= 0)
			{
				(void)close(importInfo.fd);
			}
#endif
			return Result<VkDeviceMemory>::Failure(Failure("Rhi.ExternalMemory", "Could not allocate or import dedicated external memory."));
		}

		return Result<VkDeviceMemory>::Success(memory);
	}

	[[nodiscard]] Result<void*> CreateExternalBuffer(State* opaque, const BufferDesc& desc, const NativeExternalMemoryView* imported)
	{
		State& state = *opaque;
		const VkBufferUsageFlags usage = BufferUsageFlags(desc);

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan device is unavailable."));
		}

		if (!ExternalBufferSupported(state, usage))
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.ExternalMemory", "This buffer usage does not support external import and export.");
		}

		vulkan::Buffer* buffer = CreateObject<Buffer>(state.Resource);

		if (buffer == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate the shared buffer record."));
		}
		VkExternalMemoryBufferCreateInfo external{ VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO };
		external.handleTypes = VulkanExternalMemoryType();
		VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		info.pNext = &external;
		info.size = desc.ByteCount;
		info.usage = usage;
		SetQueueFamilySharing(state, info);

		if (state.F.CreateBuffer(state.Device, &info, nullptr, &buffer->Handle) != VK_SUCCESS)
		{
			FreeBuffer(state, buffer);

			return Result<void*>::Failure(Failure("Rhi.ExternalMemory", "Could not create the shared Vulkan buffer."));
		}
		VkMemoryRequirements requirements{};
		state.F.GetBufferMemoryRequirements(state.Device, buffer->Handle, &requirements);
		Result<VkDeviceMemory> memory =
			AllocateExternalMemory(state, requirements, buffer->Handle, VK_NULL_HANDLE, HasUsage(desc.Usage, EBufferUsage::DeviceAddress),
				imported, buffer->ExternalMemoryType);

		if (!memory)
		{
			FreeBuffer(state, buffer);

			return Result<void*>::Failure(memory.GetError());
		}
		buffer->Memory = memory.Value();
		buffer->ByteCount = desc.ByteCount;
		buffer->AllocationSize = requirements.size;

		if (state.F.BindBufferMemory(state.Device, buffer->Handle, buffer->Memory, 0) != VK_SUCCESS)
		{
			FreeBuffer(state, buffer);

			return Result<void*>::Failure(Failure("Rhi.ExternalMemory", "Could not bind shared buffer memory."));
		}

		if (imported != nullptr)
		{
			Result<ExternalHandle> retained = DuplicateExternalHandle(imported->Handle);

			if (!retained)
			{
				FreeBuffer(state, buffer);

				return Result<void*>::Failure(retained.GetError());
			}
			buffer->ExportableExternalMemory = Move(retained.Value());
		}

		return Result<void*>::Success(buffer);
	}

	[[nodiscard]] Result<void*> CreateExternalTexture(State* opaque, const TextureDesc& desc, const NativeExternalMemoryView* imported)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan device is unavailable."));
		}

		const PFN_vkGetPhysicalDeviceImageFormatProperties2 query = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
			state.F.GetInstanceProcAddr(state.Instance, "vkGetPhysicalDeviceImageFormatProperties2"));

		if (!state.bExternalMemoryEnabled || query == nullptr)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.ExternalMemory", "External image memory is unavailable.");
		}
		VkImageCreateInfo info = MakeImageInfo(state, desc, ConvertVulkanFormat(desc.Format));
		VkPhysicalDeviceExternalImageFormatInfo externalQuery{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO };
		externalQuery.handleType = VulkanExternalMemoryType();
		VkPhysicalDeviceImageFormatInfo2 formatInfo{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2 };
		formatInfo.pNext = &externalQuery;
		formatInfo.format = info.format;
		formatInfo.type = info.imageType;
		formatInfo.tiling = info.tiling;
		formatInfo.usage = info.usage;
		formatInfo.flags = info.flags;
		VkExternalImageFormatProperties externalProperties{ VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES };
		VkImageFormatProperties2 properties{ VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
		properties.pNext = &externalProperties;

		if (query(state.Physical, &formatInfo, &properties) != VK_SUCCESS ||
			!ExternalMemoryFeatures(externalProperties.externalMemoryProperties.externalMemoryFeatures) ||
			(properties.imageFormatProperties.sampleCounts & info.samples) == 0)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.ExternalMemory", "This image descriptor does not support external import and export.");
		}
		VkExternalMemoryImageCreateInfo external{ VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
		external.handleTypes = VulkanExternalMemoryType();
		info.pNext = &external;
		VkImage image = VK_NULL_HANDLE;

		if (state.F.CreateImage(state.Device, &info, nullptr, &image) != VK_SUCCESS)
		{
			return Result<void*>::Failure(Failure("Rhi.ExternalMemory", "Could not create the shared Vulkan image."));
		}
		VkMemoryRequirements requirements{};
		state.F.GetImageMemoryRequirements(state.Device, image, &requirements);
		uint32 memoryType = UINT32_MAX;
		Result<VkDeviceMemory> memory = AllocateExternalMemory(state, requirements, VK_NULL_HANDLE, image, false, imported, memoryType);

		if (!memory)
		{
			state.F.DestroyImage(state.Device, image, nullptr);

			return Result<void*>::Failure(memory.GetError());
		}

		if (state.F.BindImageMemory(state.Device, image, memory.Value(), 0) != VK_SUCCESS)
		{
			state.F.DestroyImage(state.Device, image, nullptr);
			state.F.FreeMemory(state.Device, memory.Value(), nullptr);

			return Result<void*>::Failure(Failure("Rhi.ExternalMemory", "Could not bind shared image memory."));
		}

		Result<void*> created = CreateTextureInternal( opaque, desc, nullptr, 0, image, imported != nullptr ? ENativeTextureContents::Preserve : ENativeTextureContents::Discard);

		if (!created)
		{
			state.F.DestroyImage(state.Device, image, nullptr);
			state.F.FreeMemory(state.Device, memory.Value(), nullptr);

			return created;
		}

		Texture& texture = *static_cast<Texture*>(created.Value());
		texture.bExternalImage = false;
		texture.bOwnsMemory = true;
		texture.Memory = memory.Value();
		texture.ExternalMemoryType = memoryType;
		texture.ExternalAllocationSize = requirements.size;

		if (imported != nullptr)
		{
			Result<ExternalHandle> retained = DuplicateExternalHandle(imported->Handle);

			if (!retained)
			{
				FreeTexture(state, &texture);

				return Result<void*>::Failure(retained.GetError());
			}
			texture.ExportableExternalMemory = Move(retained.Value());
		}

		return created;
	}

	[[nodiscard]] Result<NativeExternalMemory> ExportExternalMemory(State* opaque, void* native, bool bTexture)
	{
		State& state = *opaque;
		NativeExternalMemory result;
		result.Device = GetExternalInteropCapabilities(opaque).Device;
		const VkDeviceMemory memory = bTexture ? static_cast<Texture*>(native)->Memory : static_cast<Buffer*>(native)->Memory;
		result.MemoryTypeIndex = bTexture ? static_cast<Texture*>(native)->ExternalMemoryType : static_cast<Buffer*>(native)->ExternalMemoryType;
		result.AllocationBytes = bTexture ? static_cast<Texture*>(native)->ExternalAllocationSize : static_cast<Buffer*>(native)->AllocationSize;
		ExternalHandle& retained = bTexture ? static_cast<Texture*>(native)->ExportableExternalMemory : static_cast<Buffer*>(native)->ExportableExternalMemory;

		if (retained.IsValid())
		{
			Result<ExternalHandle> duplicated = DuplicateExternalHandle(retained.Borrow());

			if (!duplicated)
			{
				return Result<NativeExternalMemory>::Failure(duplicated.GetError());
			}
			result.Handle = Move(duplicated.Value());

			return Result<NativeExternalMemory>::Success(Move(result));
		}
#ifdef _WIN32
		const PFN_vkGetMemoryWin32HandleKHR get = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(state.F.GetDeviceProcAddr(state.Device, "vkGetMemoryWin32HandleKHR"));
		VkMemoryGetWin32HandleInfoKHR info{ VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
		info.memory = memory;
		info.handleType = VulkanExternalMemoryType();
		HANDLE handle = nullptr;

		if (get == nullptr || get(state.Device, &info, &handle) != VK_SUCCESS)
		{
			return Result<NativeExternalMemory>::Failure(Failure("Rhi.ExternalMemory", "Could not export the shared Vulkan memory NT handle."));
		}
		retained = ExternalHandle::Adopt(VulkanExternalHandleType(), static_cast<uint64>(reinterpret_cast<uintptr_t>(handle)));
		Result<ExternalHandle> duplicated = DuplicateExternalHandle(retained.Borrow());
		if (!duplicated)
		{
			return Result<NativeExternalMemory>::Failure(duplicated.GetError());
		}
		result.Handle = Move(duplicated.Value());
#else
		PFN_vkGetMemoryFdKHR const get = reinterpret_cast<PFN_vkGetMemoryFdKHR>(state.F.GetDeviceProcAddr(state.Device, "vkGetMemoryFdKHR"));
		VkMemoryGetFdInfoKHR info{ VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
		info.memory = memory;
		info.handleType = VulkanExternalMemoryType();
		int fd = -1;

		if (get == nullptr || get(state.Device, &info, &fd) != VK_SUCCESS)
		{
			return Result<NativeExternalMemory>::Failure(Failure("Rhi.ExternalMemory", "Could not export the shared Vulkan memory FD."));
		}
		result.Handle = ExternalHandle::Adopt(VulkanExternalHandleType(), static_cast<uint64>(fd));
#endif
		return Result<NativeExternalMemory>::Success(Move(result));
	}

	[[nodiscard]] Result<void*> CreateExternalFence(State* opaque, uint64 initial, const ExternalHandleView* imported)
	{
		State& state = *opaque;

		if (!ExternalSemaphoreSupported(state))
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.ExternalFence", "External timeline semaphore import and export are unavailable.");
		}

		if (imported != nullptr && imported->Type != VulkanExternalHandleType())
		{
			return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.ExternalFence", "The semaphore OS handle type does not match this backend.");
		}

		vulkan::ExternalFenceRecord* record = CreateObject<ExternalFenceRecord>(state.Resource);

		if (record == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate an external semaphore record."));
		}
		record->Type = VulkanExternalHandleType();
		VkExportSemaphoreCreateInfo exportInfo{ VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
		exportInfo.handleTypes = VulkanExternalSemaphoreType();
		VkSemaphoreTypeCreateInfo timeline{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
		timeline.pNext = &exportInfo;
		timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		timeline.initialValue = initial;
		VkSemaphoreCreateInfo info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		info.pNext = &timeline;

		if (state.F.CreateSemaphore(state.Device, &info, nullptr, &record->Semaphore) != VK_SUCCESS)
		{
			DestroyObject(state.Resource, record);

			return Result<void*>::Failure(Failure("Rhi.ExternalFence", "Could not create a shared timeline semaphore."));
		}
		VkResult status = VK_SUCCESS;

		if (imported != nullptr)
		{
#ifdef _WIN32
			const PFN_vkImportSemaphoreWin32HandleKHR import = reinterpret_cast<PFN_vkImportSemaphoreWin32HandleKHR>( state.F.GetDeviceProcAddr(state.Device, "vkImportSemaphoreWin32HandleKHR"));
			VkImportSemaphoreWin32HandleInfoKHR importInfo{ VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR };
			importInfo.semaphore = record->Semaphore;
			importInfo.handleType = VulkanExternalSemaphoreType();
			importInfo.handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(imported->Value));
			status = import != nullptr ? import(state.Device, &importInfo) : VK_ERROR_EXTENSION_NOT_PRESENT;
#else
			const PFN_vkImportSemaphoreFdKHR import = reinterpret_cast<PFN_vkImportSemaphoreFdKHR>(state.F.GetDeviceProcAddr(state.Device, "vkImportSemaphoreFdKHR"));
			VkImportSemaphoreFdInfoKHR importInfo{ VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR };
			importInfo.semaphore = record->Semaphore;
			importInfo.handleType = VulkanExternalSemaphoreType();
			importInfo.fd = imported->Value <= INT32_MAX ? fcntl(static_cast<int>(imported->Value), F_DUPFD_CLOEXEC, 0) : -1;
			status = import != nullptr && importInfo.fd >= 0 ? import(state.Device, &importInfo) : VK_ERROR_INVALID_EXTERNAL_HANDLE;

			if (status != VK_SUCCESS && importInfo.fd >= 0)
			{
				(void)close(importInfo.fd);
			}
#endif
		}

		if (status != VK_SUCCESS)
		{
			state.F.DestroySemaphore(state.Device, record->Semaphore, nullptr);
			DestroyObject(state.Resource, record);

			return Result<void*>::Failure(Failure("Rhi.ExternalFence", "Could not import the external timeline semaphore."));
		}
#ifdef _WIN32
		if (imported != nullptr)
		{
			Result<ExternalHandle> retained = DuplicateExternalHandle(*imported);
			if (!retained)
			{
				state.F.DestroySemaphore(state.Device, record->Semaphore, nullptr);
				DestroyObject(state.Resource, record);

				return Result<void*>::Failure(retained.GetError());
			}
			record->ExportableHandle = Move(retained.Value());
		}
#endif
		return Result<void*>::Success(record);
	}

	void DestroyExternalFence(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		ExternalFenceRecord* record = static_cast<ExternalFenceRecord*>(native);
		state.F.DestroySemaphore(state.Device, record->Semaphore, nullptr);
		DestroyObject(state.Resource, record);
	}

	[[nodiscard]] Result<ExternalFence> ExportExternalFence(State* opaque, void* native)
	{
		State& state = *opaque;
		ExternalFenceRecord& record = *static_cast<ExternalFenceRecord*>(native);
		const VkSemaphore semaphore = record.Semaphore;
		ExternalFence result;
		result.Device = GetExternalInteropCapabilities(opaque).Device;
#ifdef _WIN32
		if (record.ExportableHandle.IsValid())
		{
			Result<ExternalHandle> duplicated = DuplicateExternalHandle(record.ExportableHandle.Borrow());
			if (!duplicated)
			{
				return Result<ExternalFence>::Failure(duplicated.GetError());
			}
			result.Handle = Move(duplicated.Value());

			return Result<ExternalFence>::Success(Move(result));
		}
		const PFN_vkGetSemaphoreWin32HandleKHR get = reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(state.F.GetDeviceProcAddr(state.Device, "vkGetSemaphoreWin32HandleKHR"));
		VkSemaphoreGetWin32HandleInfoKHR info{ VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR };
		info.semaphore = semaphore;
		info.handleType = VulkanExternalSemaphoreType();
		HANDLE handle = nullptr;

		if (get == nullptr || get(state.Device, &info, &handle) != VK_SUCCESS)
		{
			return Result<ExternalFence>::Failure(Failure("Rhi.ExternalFence", "Could not export the semaphore NT handle."));
		}
		record.ExportableHandle = ExternalHandle::Adopt(VulkanExternalHandleType(), static_cast<uint64>(reinterpret_cast<uintptr_t>(handle)));
		Result<ExternalHandle> duplicated = DuplicateExternalHandle(record.ExportableHandle.Borrow());
		if (!duplicated)
		{
			return Result<ExternalFence>::Failure(duplicated.GetError());
		}
		result.Handle = Move(duplicated.Value());
#else
		PFN_vkGetSemaphoreFdKHR const get = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(state.F.GetDeviceProcAddr(state.Device, "vkGetSemaphoreFdKHR"));
		VkSemaphoreGetFdInfoKHR info{ VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR };
		info.semaphore = semaphore;
		info.handleType = VulkanExternalSemaphoreType();
		int fd = -1;

		if (get == nullptr || get(state.Device, &info, &fd) != VK_SUCCESS)
		{
			return Result<ExternalFence>::Failure(Failure("Rhi.ExternalFence", "Could not export the semaphore FD."));
		}
		result.Handle = ExternalHandle::Adopt(VulkanExternalHandleType(), static_cast<uint64>(fd));
#endif
		return Result<ExternalFence>::Success(Move(result));
	}

	[[nodiscard]] Result<uint64> GetExternalFenceValue(State* opaque, void* native)
	{
		State& state = *opaque;
		uint64 value = 0;

		if (state.F.GetSemaphoreCounterValue(state.Device, static_cast<ExternalFenceRecord*>(native)->Semaphore, &value) != VK_SUCCESS)
		{
			return Result<uint64>::Failure(Failure("Rhi.ExternalFence", "Could not read the external timeline semaphore."));
		}

		return Result<uint64>::Success(value);
	}

	[[nodiscard]] Result<void> RecordExternalTransfer(State* opaque, void* commands, void* resource, bool bTexture, void* fence, uint64 value, bool bAcquire)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(commands);
		const uint32 family = state.ResourceQueueFamilyCount > 1 ? VK_QUEUE_FAMILY_IGNORED
			: pending.QueueKind == EQueue::Compute ? state.ComputeQueueFamily
			: pending.QueueKind == EQueue::Copy ? state.CopyQueueFamily
			: state.QueueFamily;

		if (resource != nullptr && bTexture)
		{
			Texture& texture = *static_cast<Texture*>(resource);

			if (!bAcquire)
			{
				RequireTextureLayout(state, pending, texture, VK_IMAGE_LAYOUT_GENERAL);
			}
			else
			{
				Pending::TextureLayoutState* tracked = TrackTextureLayout(pending, texture);

				if (tracked != nullptr)
				{
					tracked->WholeLayout = VK_IMAGE_LAYOUT_GENERAL;
					tracked->SubresourceLayouts.Clear();
				}
			}

			if (pending.bInvalidTextureLayouts)
			{
				return Result<void>::Failure(Failure("Rhi.ExternalMemory", "Could not track the external image layout."));
			}
			VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.srcAccessMask = bAcquire ? 0 : VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
			barrier.dstAccessMask = bAcquire ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : 0;
			barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier.srcQueueFamilyIndex = bAcquire ? VK_QUEUE_FAMILY_EXTERNAL : family;
			barrier.dstQueueFamilyIndex = bAcquire ? family : VK_QUEUE_FAMILY_EXTERNAL;
			barrier.image = texture.Handle;
			barrier.subresourceRange.aspectMask =
				texture.bDepth ? VK_IMAGE_ASPECT_DEPTH_BIT | (GetFormatInfo(texture.Format).bHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)
				: VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.levelCount = texture.MipLevels;
			barrier.subresourceRange.layerCount = texture.ArrayLayers;
			RecordBarriers(state, pending, bAcquire ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				bAcquire ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
				1, &barrier);
		}
		else if (resource != nullptr)
		{
			VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
			barrier.srcAccessMask = bAcquire ? 0 : VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
			barrier.dstAccessMask = bAcquire ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : 0;
			barrier.srcQueueFamilyIndex = bAcquire ? VK_QUEUE_FAMILY_EXTERNAL : family;
			barrier.dstQueueFamilyIndex = bAcquire ? family : VK_QUEUE_FAMILY_EXTERNAL;
			barrier.buffer = static_cast<Buffer*>(resource)->Handle;
			barrier.size = VK_WHOLE_SIZE;
			RecordBarriers(state, pending, bAcquire ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				bAcquire ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 1, &barrier,
				0, nullptr);
		}
		pending.ExternalSemaphore = static_cast<ExternalFenceRecord*>(fence)->Semaphore;
		pending.ExternalSemaphoreValue = value;
		pending.bExternalAcquire = bAcquire;

		return Result<void>::Success();
	}
}
