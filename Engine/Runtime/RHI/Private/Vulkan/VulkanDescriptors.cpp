#include "VulkanDescriptors.h"
#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] VkDescriptorType TableDescriptorType(EDescriptorType type, EFormat format) noexcept
	{
		switch (type)
		{
		case EDescriptorType::AccelerationStructure:
			return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		case EDescriptorType::Sampler:
			return VK_DESCRIPTOR_TYPE_SAMPLER;
		case EDescriptorType::UniformBuffer:
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case EDescriptorType::StorageBuffer:
			return format != EFormat::UNKNOWN ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		case EDescriptorType::ReadOnlyBuffer:
			return format != EFormat::UNKNOWN ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		case EDescriptorType::StorageTexture:
			return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		default:
			return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		}
	}

	void DestroyDescriptorTable(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DescriptorTable* const table = static_cast<DescriptorTable*>(native);

		if (table->Pool != VK_NULL_HANDLE)
		{
			state.F.DestroyDescriptorPool(state.Device, table->Pool, nullptr);
		}

		if (table->Layout != VK_NULL_HANDLE)
		{
			state.F.DestroyDescriptorSetLayout(state.Device, table->Layout, nullptr);
		}

		DestroyObject(state.Resource, table);
	}

	[[nodiscard]] bool CreateBindlessDescriptorSetLayout(State& state, EDescriptorType type, uint32 capacity, VkDescriptorSetLayout& output, EFormat format)
	{
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = TableDescriptorType(type, format);
		binding.descriptorCount = capacity;
		binding.stageFlags = VK_SHADER_STAGE_ALL;
		VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
		VkDescriptorSetLayoutBindingFlagsCreateInfo flags{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
		flags.bindingCount = 1;
		flags.pBindingFlags = &bindingFlags;
		VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		info.pNext = type == EDescriptorType::AccelerationStructure ? nullptr : &flags;
		info.bindingCount = 1;
		info.pBindings = &binding;

		return state.F.CreateDescriptorSetLayout(state.Device, &info, nullptr, &output) == VK_SUCCESS;
	}

	[[nodiscard]] bool CreateSampledDescriptorSetLayout(State& state, uint32 capacity, VkDescriptorSetLayout& output)
	{
		return CreateBindlessDescriptorSetLayout(state, EDescriptorType::SampledTexture, capacity, output);
	}

	[[nodiscard]] bool CreateSamplerDescriptorSetLayout(State& state, uint32 capacity, VkDescriptorSetLayout& output)
	{
		return CreateBindlessDescriptorSetLayout(state, EDescriptorType::Sampler, capacity, output);
	}

	[[nodiscard]] bool CreateStorageBufferDescriptorSetLayout(State& state, uint32 capacity, VkDescriptorSetLayout& output, EFormat format)
	{
		return CreateBindlessDescriptorSetLayout(state, EDescriptorType::StorageBuffer, capacity, output, format);
	}

	[[nodiscard]] bool CreateStorageTextureDescriptorSetLayout(State& state, uint32 capacity, VkDescriptorSetLayout& output)
	{
		return CreateBindlessDescriptorSetLayout(state, EDescriptorType::StorageTexture, capacity, output);
	}

	[[nodiscard]] bool CreateUniformBufferDescriptorSetLayout(State& state, uint32 capacity, VkDescriptorSetLayout& output)
	{
		return CreateBindlessDescriptorSetLayout(state, EDescriptorType::UniformBuffer, capacity, output);
	}

	[[nodiscard]] Result<void*> CreateDescriptorTable(State* opaque, const DescriptorTableDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		const uint32 limit = desc.BufferFormat != EFormat::UNKNOWN
			? (desc.Type == EDescriptorType::ReadOnlyBuffer ? state.MaximumBindlessSampledTextures
				: state.MaximumBindlessStorageTextures)
			: desc.Type == EDescriptorType::SampledTexture ? state.MaximumBindlessSampledTextures
			: desc.Type == EDescriptorType::Sampler ? state.MaximumBindlessSamplers
			: (desc.Type == EDescriptorType::StorageBuffer || desc.Type == EDescriptorType::ReadOnlyBuffer)
			? state.MaximumBindlessStorageBuffers
			: desc.Type == EDescriptorType::StorageTexture ? state.MaximumBindlessStorageTextures
			: desc.Type == EDescriptorType::UniformBuffer ? state.MaximumBindlessUniformBuffers
			: desc.Type == EDescriptorType::AccelerationStructure && state.bAccelerationStructure
			? Min(state.AccelerationProperties.maxDescriptorSetAccelerationStructures,
				state.AccelerationProperties.maxPerStageDescriptorAccelerationStructures)
			: 0;

		if (desc.Capacity == 0 || desc.Capacity > limit)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanDescriptorTable", "The descriptor table exceeds the device capability.");
		}
		DescriptorTable* const table = CreateObject<DescriptorTable>(state.Resource, state.Resource);

		if (table == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a Vulkan descriptor table."));
		}

		if ((desc.Type == EDescriptorType::SampledTexture || desc.Type == EDescriptorType::StorageTexture) && (!table->Textures.Resize(desc.Capacity) || !table->TextureViews.Resize(desc.Capacity)))
		{
			DestroyObject(state.Resource, table);

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not track descriptor textures."));
		}

		if ((desc.Type == EDescriptorType::StorageBuffer || desc.Type == EDescriptorType::ReadOnlyBuffer || desc.Type == EDescriptorType::UniformBuffer) && !table->Buffers.Resize(desc.Capacity))
		{
			DestroyObject(state.Resource, table);

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not track descriptor buffers."));
		}
		VkDescriptorPoolSize poolSize{ TableDescriptorType(desc.Type, desc.BufferFormat), desc.Capacity };
		VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		bool bCreated = CreateBindlessDescriptorSetLayout(state, desc.Type, desc.Capacity, table->Layout, desc.BufferFormat);

		if (bCreated)
		{
			bCreated = state.F.CreateDescriptorPool(state.Device, &poolInfo, nullptr, &table->Pool) == VK_SUCCESS;
		}

		if (bCreated)
		{
			VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocation.descriptorPool = table->Pool;
			allocation.descriptorSetCount = 1;
			allocation.pSetLayouts = &table->Layout;
			bCreated = state.F.AllocateDescriptorSets(state.Device, &allocation, &table->Set) == VK_SUCCESS;
		}

		if (!bCreated)
		{
			DestroyDescriptorTable(opaque, table);

			return Result<void*>::Failure(Failure("Rhi.VulkanDescriptorTable", "Could not allocate a descriptor set."));
		}
		table->Capacity = desc.Capacity;
		table->Type = desc.Type;
		table->BufferFormat = desc.BufferFormat;
		table->StructureStrideBytes = desc.StructureStrideBytes;

		return Result<void*>::Success(table);
	}

	[[nodiscard]] Result<void> CopyDescriptorTableEntries(
		State* opaque,
		const void* sourceNative,
		uint32 sourceIndex,
		void* destinationNative,
		uint32 destinationIndex,
		uint32 count)
	{
		State& state = *opaque;
		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}
		const DescriptorTable& source = *static_cast<const DescriptorTable*>(sourceNative);
		DescriptorTable& destination = *static_cast<DescriptorTable*>(destinationNative);
		ASSERT(&source != &destination &&
			count &&
			sourceIndex < source.Capacity &&
			count <= source.Capacity - sourceIndex &&
			destinationIndex < destination.Capacity &&
			count <= destination.Capacity - destinationIndex &&
			source.Type == destination.Type &&
			source.BufferFormat == destination.BufferFormat &&
			source.StructureStrideBytes == destination.StructureStrideBytes,
			"Descriptor copy requires compatible nonempty table ranges.");
		VkCopyDescriptorSet copy{ VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET };
		copy.srcSet = source.Set;
		copy.srcBinding = 0;
		copy.srcArrayElement = sourceIndex;
		copy.dstSet = destination.Set;
		copy.dstBinding = 0;
		copy.dstArrayElement = destinationIndex;
		copy.descriptorCount = count;
		state.F.UpdateDescriptorSets(state.Device, 0, nullptr, 1, &copy);
		for (uint32 index = 0; index < count; ++index)
		{
			if (!source.Textures.IsEmpty())
			{
				destination.Textures[destinationIndex + index] = source.Textures[sourceIndex + index];
				destination.TextureViews[destinationIndex + index] = source.TextureViews[sourceIndex + index];
			}
			if (!source.Buffers.IsEmpty())
			{
				destination.Buffers[destinationIndex + index] = source.Buffers[sourceIndex + index];
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> WriteDescriptorTable(
		State* opaque,
		void* native,
		uint32 index,
		void* textureNative,
		void* viewNative,
		void* samplerNative,
		void* bufferNative,
		BufferRange range)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		DescriptorTable& table = *static_cast<DescriptorTable*>(native);

		ASSERT(index < table.Capacity, "The descriptor index exceeds the table capacity.");

		if (textureNative == nullptr && viewNative == nullptr && samplerNative == nullptr && bufferNative == nullptr)
		{
			// Partially bound entries may retain descriptor bytes when shaders do not access them.
			if (table.Type == EDescriptorType::SampledTexture || table.Type == EDescriptorType::StorageTexture)
			{
				table.Textures[index] = nullptr;
				table.TextureViews[index] = nullptr;
			}

			if (table.Type == EDescriptorType::StorageBuffer || table.Type == EDescriptorType::ReadOnlyBuffer || table.Type == EDescriptorType::UniformBuffer)
			{
				table.Buffers[index] = nullptr;
			}

			return Result<void>::Success();
		}
		VkDescriptorImageInfo image{};
		VkDescriptorBufferInfo bufferInfo{};
		if (table.Type == EDescriptorType::AccelerationStructure)
		{
			const VkAccelerationStructureKHR acceleration = GetDescriptorAccelerationStructure(bufferNative);
			VkWriteDescriptorSetAccelerationStructureKHR accelerationWrite{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
			accelerationWrite.accelerationStructureCount = 1;
			accelerationWrite.pAccelerationStructures = &acceleration;
			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.pNext = &accelerationWrite;
			write.dstSet = table.Set;
			write.dstBinding = 0;
			write.dstArrayElement = index;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			state.F.UpdateDescriptorSets(state.Device, 1, &write, 0, nullptr);

			return Result<void>::Success();
		}
		if (table.Type == EDescriptorType::Sampler)
		{
			image.sampler = static_cast<const Sampler*>(samplerNative)->Handle;
		}
		else if (table.Type == EDescriptorType::StorageBuffer || table.Type == EDescriptorType::ReadOnlyBuffer || table.Type == EDescriptorType::UniformBuffer)
		{
			const bool bUniform = table.Type == EDescriptorType::UniformBuffer;

			if (viewNative == nullptr &&
				(range.ByteCount > (bUniform ? state.DeviceProperties.limits.maxUniformBufferRange : state.DeviceProperties.limits.maxStorageBufferRange) ||
						range.ByteOffset % (bUniform ? state.DeviceProperties.limits.minUniformBufferOffsetAlignment : state.DeviceProperties.limits.minStorageBufferOffsetAlignment) != 0))
			{
				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanDescriptorTable", "The buffer range exceeds the Vulkan limit.");
			}
			bufferInfo = { static_cast<const Buffer*>(bufferNative)->Handle, range.ByteOffset, range.ByteCount };
		}
		else
		{
			Texture& texture = *static_cast<Texture*>(textureNative);
			image.imageView = viewNative != nullptr ? static_cast<const TextureView*>(viewNative)->Handle : (table.Type == EDescriptorType::StorageTexture ? texture.GetStorageView() : texture.View);
			image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		}
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = table.Set;
		write.dstBinding = 0;
		write.dstArrayElement = index;
		write.descriptorCount = 1;
		write.descriptorType = TableDescriptorType(table.Type, table.BufferFormat);
		const bool bBufferTable = table.Type == EDescriptorType::StorageBuffer || table.Type == EDescriptorType::ReadOnlyBuffer || table.Type == EDescriptorType::UniformBuffer;
		write.pImageInfo = bBufferTable ? nullptr : &image;
		write.pBufferInfo = bBufferTable ? &bufferInfo : nullptr;

		if (bBufferTable && viewNative != nullptr)
		{
			write.pBufferInfo = nullptr;
			write.pTexelBufferView = &static_cast<const BufferView*>(viewNative)->Handle;
		}
		state.F.UpdateDescriptorSets(state.Device, 1, &write, 0, nullptr);

		if (table.Type == EDescriptorType::SampledTexture || table.Type == EDescriptorType::StorageTexture)
		{
			table.Textures[index] = static_cast<Texture*>(textureNative);
			table.TextureViews[index] = static_cast<TextureView*>(viewNative);
		}

		if (table.Type == EDescriptorType::StorageBuffer || table.Type == EDescriptorType::ReadOnlyBuffer || table.Type == EDescriptorType::UniformBuffer)
		{
			table.Buffers[index] = static_cast<Buffer*>(bufferNative);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] VkSamplerAddressMode VulkanAddress(ESamplerAddressMode mode) noexcept
	{
		switch (mode)
		{
		case ESamplerAddressMode::Repeat:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case ESamplerAddressMode::Mirror:
			return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case ESamplerAddressMode::Border:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		default:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		}
	}

	[[nodiscard]] bool SupportsSampler(const State& state, const SamplerDesc& sampler) noexcept
	{
		return sampler.MipBias >= -state.DeviceProperties.limits.maxSamplerLodBias &&
			sampler.MipBias <= state.DeviceProperties.limits.maxSamplerLodBias &&
			(sampler.Reduction == ESamplerReduction::Standard || state.bSamplerFilterMinmax) &&
			(sampler.MaxAnisotropy <= 1.0f || (state.bSamplerAnisotropy && sampler.MaxAnisotropy <= state.DeviceProperties.limits.maxSamplerAnisotropy));
	}

	[[nodiscard]] VkSamplerCreateInfo MakeSamplerCreateInfo(const SamplerDesc& sampler, VkSamplerReductionModeCreateInfo& reduction) noexcept
	{
		constexpr VkCompareOp COMPARE[]{ VK_COMPARE_OP_NEVER,
										VK_COMPARE_OP_LESS,
										VK_COMPARE_OP_EQUAL,
										VK_COMPARE_OP_LESS_OR_EQUAL,
										VK_COMPARE_OP_GREATER,
										VK_COMPARE_OP_NOT_EQUAL,
										VK_COMPARE_OP_GREATER_OR_EQUAL,
										VK_COMPARE_OP_ALWAYS };
		VkSamplerCreateInfo native{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		native.magFilter = sampler.MagFilter == ESamplerFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		native.minFilter = sampler.MinFilter == ESamplerFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		native.mipmapMode = sampler.MipFilter == ESamplerFilter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
		native.addressModeU = VulkanAddress(sampler.AddressU);
		native.addressModeV = VulkanAddress(sampler.AddressV);
		native.addressModeW = VulkanAddress(sampler.AddressW);
		native.mipLodBias = sampler.MipBias;
		native.anisotropyEnable = sampler.MaxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
		native.maxAnisotropy = sampler.MaxAnisotropy;
		native.compareEnable = sampler.bCompareEnabled ? VK_TRUE : VK_FALSE;
		native.compareOp = COMPARE[static_cast<uint32>(sampler.Compare)];
		native.borderColor = sampler.BorderColor == ESamplerBorderColor::OpaqueWhite ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
			: sampler.BorderColor == ESamplerBorderColor::OpaqueBlack ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK
			: VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		native.minLod = sampler.MinLod;
		native.maxLod = sampler.MaxLod == NumericLimits<float>::max() && sampler.MinLod <= VK_LOD_CLAMP_NONE ? VK_LOD_CLAMP_NONE : sampler.MaxLod;

		if (sampler.Reduction != ESamplerReduction::Standard)
		{
			reduction = { VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO };
			reduction.reductionMode = sampler.Reduction == ESamplerReduction::Minimum ? VK_SAMPLER_REDUCTION_MODE_MIN : VK_SAMPLER_REDUCTION_MODE_MAX;
			native.pNext = &reduction;
		}

		return native;
	}

	[[nodiscard]] Result<void*> CreateSampler(State* opaque, const SamplerDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		if (!SupportsSampler(state, desc))
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanSampler", "The sampler exceeds the device anisotropy or mip bias limit.");
		}
		Sampler* const sampler = CreateObject<Sampler>(state.Resource);

		if (sampler == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a sampler."));
		}
		VkSamplerReductionModeCreateInfo reduction{};
		const VkSamplerCreateInfo native = MakeSamplerCreateInfo(desc, reduction);

		if (state.F.CreateSampler(state.Device, &native, nullptr, &sampler->Handle) != VK_SUCCESS)
		{
			DestroyObject(state.Resource, sampler);

			return Result<void*>::Failure(Failure("Rhi.VulkanSampler", "Could not create a sampler."));
		}

		return Result<void*>::Success(sampler);
	}

	void DestroySampler(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		Sampler* const sampler = static_cast<Sampler*>(native);
		state.F.DestroySampler(state.Device, sampler->Handle, nullptr);
		DestroyObject(state.Resource, sampler);
	}
}
