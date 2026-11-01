#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<uint32> QueryMemoryBudget(const State* opaque, Span<MemoryBudget> budgets)
	{
		const State& state = *opaque;

		if (state.bLost)
		{
			return Result<uint32>::Failure(Failure("Rhi.DeviceLost", "The Vulkan device is unavailable."));
		}

		if (!state.bMemoryBudget)
		{
			return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.MemoryBudget", "The Vulkan memory budget extension is not enabled.");
		}
		VkPhysicalDeviceMemoryBudgetPropertiesEXT nativeBudget{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
		VkPhysicalDeviceMemoryProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
		properties.pNext = &nativeBudget;
		state.F.GetMemoryProperties2(state.Physical, &properties);
		const uint32 count = properties.memoryProperties.memoryHeapCount;

		if (count == 0 || count > VK_MAX_MEMORY_HEAPS)
		{
			return Result<uint32>::Failure(Failure("Rhi.MemoryBudget", "The driver returned an invalid memory heap count."));
		}

		if (budgets.Size() != 0 && budgets.Size() < count)
		{
			return Result<uint32>::Failure(EErrorCode::InvalidArgument, "Rhi.MemoryBudget", "The output span cannot hold every native memory heap.");
		}

		if (budgets.Size() != 0)
		{
			for (uint32 index = 0; index < count; ++index)
			{
				budgets[index] = { index, (properties.memoryProperties.memoryHeaps[index].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0,
								  EMemoryBudgetAccuracy::DriverEstimate, nativeBudget.heapBudget[index], nativeBudget.heapUsage[index] };
			}
		}

		return Result<uint32>::Success(count);
	}

	[[nodiscard]] VkImageCreateInfo MakeImageInfo(const State& state, const TextureDesc& desc, VkFormat format) noexcept
	{
		VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		info.imageType = desc.Dimension == ETextureDimension::Texture3D ? VK_IMAGE_TYPE_3D : desc.Dimension == ETextureDimension::Texture1D ? VK_IMAGE_TYPE_1D : VK_IMAGE_TYPE_2D;
		info.format = format;
		info.extent = { desc.Width, desc.Height, desc.Depth };
		info.mipLevels = desc.MipLevels;
		info.arrayLayers = desc.ArrayLayers;

		if (desc.Dimension == ETextureDimension::Cube)
		{
			info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		}

		if (desc.bSparse)
		{
			info.flags |= VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT | VK_IMAGE_CREATE_SPARSE_ALIASED_BIT;
		}

		if (desc.Dimension == ETextureDimension::Texture3D && HasUsage(desc.Usage, ETextureUsage::RenderTarget))
		{
			info.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
		}

		if (desc.bMutableFormat)
		{
			info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
		}
		info.samples = static_cast<VkSampleCountFlagBits>(desc.SampleCount);
		info.tiling = VK_IMAGE_TILING_OPTIMAL;

		if (HasUsage(desc.Usage, ETextureUsage::CopySource))
		{
			info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}

		if (HasUsage(desc.Usage, ETextureUsage::CopyDestination) ||
			HasUsage(desc.Usage, ETextureUsage::RenderTarget) ||
			HasUsage(desc.Usage, ETextureUsage::DepthStencil) ||
			HasUsage(desc.Usage, ETextureUsage::Storage))
		{
			info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}

		if (HasUsage(desc.Usage, ETextureUsage::ShaderRead))
		{
			info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		}

		if (HasUsage(desc.Usage, ETextureUsage::Storage))
		{
			info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		}

		if (HasUsage(desc.Usage, ETextureUsage::RenderTarget))
		{
			info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}

		if (HasUsage(desc.Usage, ETextureUsage::DepthStencil))
		{
			info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		}

		if (HasUsage(desc.Usage, ETextureUsage::ShadingRateSource))
		{
			info.usage |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
		}
		SetQueueFamilySharing(state, info);
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		return info;
	}

	[[nodiscard]] Result<MemoryRequirements> GetTextureMemoryRequirements(const State* opaque, const TextureDesc& desc)
	{
		const State& state = *opaque;

		if (state.bLost)
		{
			return Result<MemoryRequirements>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		if (desc.Dimension == ETextureDimension::Cube && desc.ArrayLayers > 6 && !state.bCubeArray)
		{
			return Result<MemoryRequirements>::Failure(EErrorCode::Unsupported, "Rhi.TextureCube", "This Vulkan device does not support cube arrays.");
		}

		const VkFormat format = ConvertVulkanFormat(desc.Format);

		if (format == VK_FORMAT_UNDEFINED)
		{
			return Result<MemoryRequirements>::Failure(Failure("Rhi.VulkanFormat", "No Vulkan texture format mapping exists."));
		}
		VkImageCreateInfo info = MakeImageInfo(state, desc, format);
		VkImage image = VK_NULL_HANDLE;

		if (state.F.CreateImage(state.Device, &info, nullptr, &image) != VK_SUCCESS)
		{
			return Result<MemoryRequirements>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTexture", "The Vulkan image cannot be created with these parameters.");
		}
		VkMemoryDedicatedRequirements dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
		VkMemoryRequirements2 queried{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
		queried.pNext = &dedicated;
		VkImageMemoryRequirementsInfo2 requirementsInfo{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
		requirementsInfo.image = image;
		state.F.GetImageMemoryRequirements2(state.Device, &requirementsInfo, &queried);
		const VkMemoryRequirements& requirements = queried.memoryRequirements;
		state.F.DestroyImage(state.Device, image, nullptr);

		if (requirements.size == 0 || requirements.alignment == 0)
		{
			return Result<MemoryRequirements>::Failure(Failure("Rhi.VulkanMemory", "The image has invalid memory requirements."));
		}

		const uint32 memoryType = SelectMemoryType(state, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, true);
		const uint32 budgetIndex = MemoryBudgetIndex(state, memoryType);

		return Result<MemoryRequirements>::Success({ requirements.size, requirements.alignment,
														  dedicated.requiresDedicatedAllocation != VK_FALSE,
														  dedicated.prefersDedicatedAllocation != VK_FALSE, budgetIndex, budgetIndex });
	}

	[[nodiscard]] VkBufferUsageFlags BufferUsageFlags(const BufferDesc& desc) noexcept
	{
		VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		if (HasUsage(desc.Usage, EBufferUsage::Vertex))
		{
			usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		}

		if (HasUsage(desc.Usage, EBufferUsage::Index))
		{
			usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		}

		if (HasUsage(desc.Usage, EBufferUsage::Uniform))
		{
			usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		}

		if (HasUsage(desc.Usage, EBufferUsage::Storage))
		{
			usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		}

		if (HasUsage(desc.Usage, EBufferUsage::TypedReadOnly))
		{
			usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
		}

		if (HasUsage(desc.Usage, EBufferUsage::TypedStorage))
		{
			usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
		}

		if (HasUsage(desc.Usage, EBufferUsage::Indirect))
		{
			usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		}

		if (HasUsage(desc.Usage, EBufferUsage::DeviceAddress))
		{
			usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		}

		if (HasUsage(desc.Usage, EBufferUsage::AccelBuildInput))
		{
			usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
		}

		if (HasUsage(desc.Usage, EBufferUsage::AccelStorage))
		{
			usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
		}

		if (HasUsage(desc.Usage, EBufferUsage::MicromapBuildInput))
		{
			usage |= VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT;
		}

		if (HasUsage(desc.Usage, EBufferUsage::MicromapStorage))
		{
			usage |= VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT;
		}

		return usage;
	}

	[[nodiscard]] Result<MemoryRequirements> GetBufferMemoryRequirements(const State* opaque, const BufferDesc& desc)
	{
		const State& state = *opaque;

		if (state.bLost)
		{
			return Result<MemoryRequirements>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}
		VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		info.size = desc.ByteCount;
		info.usage = BufferUsageFlags(desc);
		SetQueueFamilySharing(state, info);
		VkBuffer buffer = VK_NULL_HANDLE;

		if (state.F.CreateBuffer(state.Device, &info, nullptr, &buffer) != VK_SUCCESS)
		{
			return Result<MemoryRequirements>::Failure(Failure("Rhi.VulkanBuffer", "Could not query buffer memory requirements."));
		}
		VkMemoryDedicatedRequirements dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
		VkMemoryRequirements2 queried{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
		queried.pNext = &dedicated;
		VkBufferMemoryRequirementsInfo2 requirementsInfo{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
		requirementsInfo.buffer = buffer;
		state.F.GetBufferMemoryRequirements2(state.Device, &requirementsInfo, &queried);
		VkMemoryRequirements requirements = queried.memoryRequirements;
		state.F.DestroyBuffer(state.Device, buffer, nullptr);

		if (requirements.size == 0 || requirements.alignment == 0)
		{
			return Result<MemoryRequirements>::Failure(Failure("Rhi.VulkanMemory", "The buffer has invalid memory requirements."));
		}

		if (desc.Memory != EBufferMemory::Device)
		{
			const VkDeviceSize atom = state.DeviceProperties.limits.nonCoherentAtomSize;
			requirements.alignment = Max(requirements.alignment, atom);
			requirements.size = ((requirements.size + atom - 1) / atom) * atom;
		}

		const bool bHostVisible = desc.Memory != EBufferMemory::Device;
		const VkMemoryPropertyFlags required = bHostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		const VkMemoryPropertyFlags placedPreferred = desc.Memory == EBufferMemory::CpuRead ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT
			: desc.Memory == EBufferMemory::CpuWrite ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			: 0;
		const uint32 standaloneType = SelectMemoryType(state, requirements.memoryTypeBits, required, bHostVisible ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0, !bHostVisible);
		const uint32 placedType = SelectMemoryType(state, requirements.memoryTypeBits, required, placedPreferred, false);

		return Result<MemoryRequirements>::Success({ requirements.size, requirements.alignment,
														  dedicated.requiresDedicatedAllocation != VK_FALSE,
														  dedicated.prefersDedicatedAllocation != VK_FALSE,
														  MemoryBudgetIndex(state, standaloneType), MemoryBudgetIndex(state, placedType) });
	}

	[[nodiscard]] Result<void*> CreateHeap(State* opaque, const HeapDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}
		Heap* heap = CreateObject<Heap>(state.Resource);

		if (heap == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan heap state."));
		}
		heap->Descriptor = desc;

		return Result<void*>::Success(heap);
	}

	[[nodiscard]] uint64 GetHeapCommittedBytes(const void* native) noexcept
	{
		const Heap& heap = *static_cast<const Heap*>(native);

		return heap.Memory != VK_NULL_HANDLE ? heap.Descriptor.CapacityBytes : 0;
	}

	void DestroyHeapNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		Heap* heap = static_cast<Heap*>(native);

		if (heap->Memory)
		{
			state.F.FreeMemory(state.Device, heap->Memory, nullptr);
		}

		DestroyObject(state.Resource, heap);
	}

	[[nodiscard]] Result<void> DestroyHeap(State* opaque, void* native)
	{
		DestroyHeapNow(opaque, native);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> CreatePlacedBuffer(State* opaque, const BufferDesc& desc, void* nativeHeap, uint64 offset)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Heap& heap = *static_cast<Heap*>(nativeHeap);
		Buffer* buffer = CreateObject<Buffer>(state.Resource);

		if (buffer == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate placed buffer state."));
		}
		VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		info.size = desc.ByteCount;
		info.usage = BufferUsageFlags(desc);
		SetQueueFamilySharing(state, info);

		if (state.F.CreateBuffer(state.Device, &info, nullptr, &buffer->Handle) != VK_SUCCESS)
		{
			FreeBuffer(state, buffer);

			return Result<void*>::Failure(Failure("Rhi.VulkanBuffer", "Could not create a placed buffer."));
		}
		VkMemoryDedicatedRequirements dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
		VkMemoryRequirements2 queried{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
		queried.pNext = &dedicated;
		VkBufferMemoryRequirementsInfo2 requirementsInfo{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
		requirementsInfo.buffer = buffer->Handle;
		state.F.GetBufferMemoryRequirements2(state.Device, &requirementsInfo, &queried);
		const VkMemoryRequirements& requirements = queried.memoryRequirements;

		if (dedicated.requiresDedicatedAllocation)
		{
			FreeBuffer(state, buffer);

			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.DedicatedAllocation", "The buffer requires a dedicated allocation.");
		}

		uint32 memoryType = heap.MemoryTypeIndex;

		if (heap.Memory == VK_NULL_HANDLE)
		{
			const VkMemoryPropertyFlags required = desc.Memory == EBufferMemory::Device ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			const VkMemoryPropertyFlags preferred = desc.Memory == EBufferMemory::CpuRead ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT
				: desc.Memory == EBufferMemory::CpuWrite ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				: 0;

			memoryType = SelectMemoryType(state, requirements.memoryTypeBits, required, preferred, false);
		}

		if (memoryType == UINT32_MAX || (requirements.memoryTypeBits & (1u << memoryType)) == 0)
		{
			FreeBuffer(state, buffer);

			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanHeap", "The heap memory type is incompatible with this buffer.");
		}
		VkDeviceMemory memory = heap.Memory;

		if (memory == VK_NULL_HANDLE)
		{
			VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocation.allocationSize = heap.Descriptor.CapacityBytes;
			allocation.memoryTypeIndex = memoryType;
			VkMemoryAllocateFlagsInfo addressFlags{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
			if (state.bBufferDeviceAddress)
			{
				addressFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
				allocation.pNext = &addressFlags;
			}

			if (state.F.AllocateMemory(state.Device, &allocation, nullptr, &memory) != VK_SUCCESS)
			{
				FreeBuffer(state, buffer);

				return Result<void*>::Failure(Failure("Rhi.VulkanHeap", "Could not allocate Vulkan heap memory."));
			}
		}

		if (state.F.BindBufferMemory(state.Device, buffer->Handle, memory, offset) != VK_SUCCESS)
		{
			if (heap.Memory == VK_NULL_HANDLE)
			{
				state.F.FreeMemory(state.Device, memory, nullptr);
			}
			FreeBuffer(state, buffer);

			return Result<void*>::Failure(Failure("Rhi.VulkanHeap", "Could not bind a placed buffer."));
		}
		heap.Memory = memory;
		heap.MemoryTypeIndex = memoryType;
		heap.Coherent = (state.MemoryProperties.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
		buffer->Memory = memory;
		buffer->AllocationSize = heap.Descriptor.CapacityBytes;
		buffer->ByteCount = desc.ByteCount;
		buffer->MemoryOffset = offset;
		buffer->Coherent = heap.Coherent;
		buffer->bOwnsMemory = false;
		buffer->MemoryAccess = desc.Memory;

		return Result<void*>::Success(buffer);
	}
}
