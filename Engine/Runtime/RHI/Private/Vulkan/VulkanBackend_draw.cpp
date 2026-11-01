#include "VulkanBackend.h"
#include "VulkanDescriptors.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<void> RecordSetViewport(State* opaque, void* native, Span<const ViewportDesc> viewports)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		VkViewport regions[16]{};
		for (usize index = 0; index < viewports.Size(); ++index)
		{
			const ViewportDesc& viewport = viewports[index];
			regions[index] = { viewport.X,	   viewport.Y,		  viewport.Width,
							  viewport.Height, viewport.MinDepth, viewport.MaxDepth };
		}state.F.CmdSetViewport(pending.Commands, 0, static_cast<uint32>(viewports.Size()), regions);
		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordSetScissor(State* opaque, void* native, Span<const ScissorDesc> scissors)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		VkRect2D regions[16]{};
		for (usize index = 0; index < scissors.Size(); ++index)
		{
			const ScissorDesc& scissor = scissors[index];
			regions[index] = { {static_cast<int32>(scissor.X), static_cast<int32>(scissor.Y)}, {scissor.Width, scissor.Height} };
		}
		state.F.CmdSetScissor(pending.Commands, 0, static_cast<uint32>(scissors.Size()), regions);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> BindGraphicsResourcesMultiple(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		Span<const NativeSampledTexture> textures,
		Span<const NativeStorageTexture> storageTextures,
		Span<const NativeGraphicsBuffer> buffers,
		void* accelNative)
	{
		uint32 sampledCount = 0;
		uint32 storageCount = 0;
		uint32 bufferCount = 0;

		for (uint32 index = 0; index < pipeline.SetLayoutCount; ++index)
		{
			sampledCount += pipeline.SampledTextureCounts[index];
			storageCount += pipeline.StorageTextureCounts[index];
			bufferCount += pipeline.GraphicsBufferCounts[index];
		}

		ASSERT(sampledCount == textures.Size() && storageCount == storageTextures.Size() && bufferCount == buffers.Size(), "The graphics binding counts differ from the pipeline.");

		for (const NativeSampledTexture& binding : textures)
		{
			const Texture& texture = *static_cast<const Texture*>(binding.Texture);
			const TextureViewDesc* const range = binding.View != nullptr ? &static_cast<const TextureView*>(binding.View)->Descriptor : nullptr;
			const VkImageLayout layout = SampledTextureLayout(pending, texture, range);
			if (!SupportsAttachmentSamplingLayout(state, layout))
			{
				return Result<void>::Failure(
					EErrorCode::Unsupported,
					"Rhi.AttachmentSampling",
					"Read-only attachment sampling requires load/store NONE; mixed " "planes also require separate depth/stencil attachment access.");
			}

			ASSERT(TextureHasLayout(pending, texture, layout, range), "Prepare a fixed sampled texture before rendering.");
		}

		for (const NativeStorageTexture& binding : storageTextures)
		{
			const Texture& texture = *static_cast<const Texture*>(binding.Texture);
			const TextureViewDesc* const range = binding.View != nullptr ? &static_cast<const TextureView*>(binding.View)->Descriptor : nullptr;

			ASSERT(TextureHasLayout(pending, texture, VK_IMAGE_LAYOUT_GENERAL, range), "Prepare a fixed storage texture before rendering.");
		}

		for (const NativeGraphicsBuffer& binding : buffers)
		{
			const bool bUniform = binding.Type == EGraphicsBufferBindingType::UniformBuffer;
			if (binding.View == nullptr &&
				(binding.ByteCount > (bUniform ? state.DeviceProperties.limits.maxUniformBufferRange : state.DeviceProperties.limits.maxStorageBufferRange) ||
						binding.ByteOffset % (bUniform ? state.DeviceProperties.limits.minUniformBufferOffsetAlignment : state.DeviceProperties.limits.minStorageBufferOffsetAlignment) != 0))
			{
				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsBinding", "The buffer exceeds the Vulkan descriptor range.");
			}
		}
		SynchronizeGraphicsStorage(state, pending);
		bool bReuse = pending.LastTexturePipeline == &pipeline &&
			pending.LastGraphicsAccelStruct == accelNative &&
			pending.LastTextureSetCount == pipeline.SetLayoutCount &&
			pending.LastTextureCount == textures.Size() &&
			pending.LastStorageTextureCount == storageTextures.Size() &&
			pending.LastGraphicsBufferCount == buffers.Size();

		for (usize index = 0; bReuse && index < textures.Size(); ++index)
		{
			bReuse = pending.LastTextures[index] == textures[index].Texture && pending.LastSamplers[index] == textures[index].Sampler && pending.LastViews[index] == textures[index].View;
		}

		for (usize index = 0; bReuse && index < storageTextures.Size(); ++index)
		{
			bReuse = pending.LastStorageTextures[index] == storageTextures[index].Texture && pending.LastStorageViews[index] == storageTextures[index].View;
		}

		for (usize index = 0; bReuse && index < buffers.Size(); ++index)
		{
			bReuse = pending.LastGraphicsBuffers[index] == buffers[index].Buffer &&
				pending.LastGraphicsBufferRanges[index].ByteOffset == buffers[index].ByteOffset &&
				pending.LastGraphicsBufferRanges[index].ByteCount == buffers[index].ByteCount;
		}

		if (bReuse)
		{
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, 0, pipeline.SetLayoutCount,
				pending.LastTextureSets, 0, nullptr);

			return Result<void>::Success();
		}
		constexpr uint32 SETS_PER_POOL = 32;

		if (pending.DescriptorPools.IsEmpty() ||
			pending.DescriptorPools.Back().UsedSets + pipeline.SetLayoutCount > SETS_PER_POOL ||
			(pipeline.AccelStructSlot != 0xFFFFFFFFu && !pending.DescriptorPools.Back().bAccelerationStructures))
		{
			VkDescriptorPoolSize poolSizes[8]{ {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_SAMPLER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, SETS_PER_POOL} };
			VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			poolInfo.maxSets = SETS_PER_POOL;
			poolInfo.poolSizeCount = pipeline.AccelStructSlot != 0xFFFFFFFFu ? 8 : 7;
			poolInfo.pPoolSizes = poolSizes;
			VkDescriptorPool pool = VK_NULL_HANDLE;

			if (state.F.CreateDescriptorPool(state.Device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanGraphics", "Could not allocate graphics descriptors."));
			}

			if (!pending.DescriptorPools.Add({ pool, 0, pipeline.AccelStructSlot != 0xFFFFFFFFu }))
			{
				state.F.DestroyDescriptorPool(state.Device, pool, nullptr);

				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track graphics descriptors."));
			}
		}
		VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocation.descriptorPool = pending.DescriptorPools.Back().Handle;
		allocation.descriptorSetCount = pipeline.SetLayoutCount;
		allocation.pSetLayouts = pipeline.SetLayouts;
		VkDescriptorSet sets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		if (state.F.AllocateDescriptorSets(state.Device, &allocation, sets) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanGraphics", "Could not allocate graphics descriptor sets."));
		}
		pending.DescriptorPools.Back().UsedSets += pipeline.SetLayoutCount;
		VkDescriptorImageInfo images[32]{};
		VkDescriptorImageInfo samplers[32]{};
		VkDescriptorImageInfo storageImages[32]{};
		VkDescriptorBufferInfo bufferInfos[32]{};
		VkWriteDescriptorSet writes[132]{};
		uint32 writeCount = 0;
		uint32 accelWriteIndex = 0;
		uint32 sampledOffset = 0;
		uint32 storageOffset = 0;
		uint32 bufferOffset = 0;

		for (uint32 layoutIndex = 0; layoutIndex < pipeline.SetLayoutCount; ++layoutIndex)
		{
			for (uint32 index = 0; index < pipeline.SampledTextureCounts[layoutIndex]; ++index)
			{
				const uint32 flat = sampledOffset + index;
				const Texture& texture = *static_cast<const Texture*>(textures[flat].Texture);
				const TextureViewDesc* const range = textures[flat].View != nullptr ? &static_cast<const TextureView*>(textures[flat].View)->Descriptor : nullptr;
				images[flat] = { VK_NULL_HANDLE,
								textures[flat].View != nullptr ? static_cast<const TextureView*>(textures[flat].View)->Handle
															   : texture.View,
								SampledTextureLayout(pending, texture, range) };
				samplers[flat] = { textures[flat].Sampler != nullptr ? static_cast<const Sampler*>(textures[flat].Sampler)->Handle
																	: pipeline.Samplers[layoutIndex][index],
								  VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
				VkWriteDescriptorSet& imageWrite = writes[writeCount++];
				imageWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				imageWrite.dstSet = sets[layoutIndex];
				imageWrite.dstBinding = pipeline.SampledTextureSlots[layoutIndex][index];
				imageWrite.descriptorCount = 1;
				imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				imageWrite.pImageInfo = &images[flat];
				VkWriteDescriptorSet& samplerWrite = writes[writeCount++];
				samplerWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				samplerWrite.dstSet = sets[layoutIndex];
				samplerWrite.dstBinding = 16 + pipeline.SampledSamplerSlots[layoutIndex][index];
				samplerWrite.descriptorCount = 1;
				samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				samplerWrite.pImageInfo = &samplers[flat];
			}

			for (uint32 index = 0; index < pipeline.StorageTextureCounts[layoutIndex]; ++index)
			{
				const uint32 flat = storageOffset + index;
				const Texture& texture = *static_cast<const Texture*>(storageTextures[flat].Texture);
				storageImages[flat] = { VK_NULL_HANDLE,
									   storageTextures[flat].View != nullptr
										   ? static_cast<const TextureView*>(storageTextures[flat].View)->Handle
										   : texture.GetStorageView(),
									   VK_IMAGE_LAYOUT_GENERAL };
				VkWriteDescriptorSet& write = writes[writeCount++];
				write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.dstSet = sets[layoutIndex];
				write.dstBinding = 64 + pipeline.StorageTextureSlots[layoutIndex][index];
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				write.pImageInfo = &storageImages[flat];
			}

			for (uint32 index = 0; index < pipeline.GraphicsBufferCounts[layoutIndex]; ++index)
			{
				const uint32 flat = bufferOffset + index;
				const rhi::NativeGraphicsBuffer& binding = buffers[flat];
				const Buffer& buffer = *static_cast<const Buffer*>(binding.Buffer);

				bufferInfos[flat] = { buffer.Handle, binding.ByteOffset, binding.ByteCount };
				VkWriteDescriptorSet& write = writes[writeCount++];
				write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.dstSet = sets[layoutIndex];
				write.dstBinding = 32 + binding.Slot;
				write.descriptorCount = 1;
				write.descriptorType = BufferDescriptorType(binding.Type, binding.Format);
				write.pBufferInfo = &bufferInfos[flat];

				if (binding.View != nullptr)
				{
					write.pBufferInfo = nullptr;
					write.pTexelBufferView = &static_cast<const BufferView*>(binding.View)->Handle;
				}
			}

			if (layoutIndex == 0 && accelNative != nullptr)
			{
				accelWriteIndex = writeCount;
				VkWriteDescriptorSet& write = writes[writeCount++];
				write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.dstSet = sets[0];
				write.dstBinding = 80 + pipeline.AccelStructSlot;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			}
			sampledOffset += pipeline.SampledTextureCounts[layoutIndex];
			storageOffset += pipeline.StorageTextureCounts[layoutIndex];
			bufferOffset += pipeline.GraphicsBufferCounts[layoutIndex];
		}
		VkAccelerationStructureKHR accelHandle = accelNative != nullptr ? static_cast<NativeAccelStruct*>(accelNative)->Handle : VK_NULL_HANDLE;
		VkWriteDescriptorSetAccelerationStructureKHR accelInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
		if (accelNative != nullptr)
		{
			accelInfo.accelerationStructureCount = 1;
			accelInfo.pAccelerationStructures = &accelHandle;
			writes[accelWriteIndex].pNext = &accelInfo;
		}
		WriteCommandDescriptors(state, pending, writeCount, writes, 0, nullptr);
		state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, 0, pipeline.SetLayoutCount, sets,
			0, nullptr);
		pending.LastTexturePipeline = &pipeline;
		pending.LastGraphicsAccelStruct = accelNative;
		pending.LastTextureCount = sampledCount;
		pending.LastStorageTextureCount = storageCount;
		pending.LastGraphicsBufferCount = bufferCount;
		pending.LastTextureSetCount = pipeline.SetLayoutCount;

		for (uint32 index = 0; index < pipeline.SetLayoutCount; ++index)
		{
			pending.LastTextureSets[index] = sets[index];
		}

		for (usize index = 0; index < textures.Size(); ++index)
		{
			pending.LastTextures[index] = textures[index].Texture;
			pending.LastSamplers[index] = textures[index].Sampler;
			pending.LastViews[index] = textures[index].View;
		}

		for (usize index = 0; index < storageTextures.Size(); ++index)
		{
			pending.LastStorageTextures[index] = storageTextures[index].Texture;
			pending.LastStorageViews[index] = storageTextures[index].View;
		}

		for (usize index = 0; index < buffers.Size(); ++index)
		{
			pending.LastGraphicsBuffers[index] = buffers[index].Buffer;
			pending.LastGraphicsBufferRanges[index] = { buffers[index].ByteOffset, buffers[index].ByteCount };
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> BindGraphicsResources(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		Span<const NativeSampledTexture> textures,
		Span<const NativeStorageTexture> storageTextures,
		Span<const NativeGraphicsBuffer> buffers,
		void* accelNative)
	{
		if (pipeline.Fixed.LayoutCount != 0)
		{
			return Result<void>::Success();
		}
		if (pipeline.SetLayoutCount > 1)
		{
			return BindGraphicsResourcesMultiple(state, pending, pipeline, textures, storageTextures, buffers, accelNative);
		}

		for (const NativeSampledTexture& binding : textures)
		{
			const Texture& texture = *static_cast<const Texture*>(binding.Texture);
			const TextureViewDesc* const range = binding.View != nullptr ? &static_cast<const TextureView*>(binding.View)->Descriptor : nullptr;
			const VkImageLayout layout = SampledTextureLayout(pending, texture, range);
			if (!SupportsAttachmentSamplingLayout(state, layout))
			{
				return Result<void>::Failure(
					EErrorCode::Unsupported,
					"Rhi.AttachmentSampling",
					"Read-only attachment sampling requires load/store NONE; mixed " "planes also require separate depth/stencil attachment access.");
			}

			ASSERT(TextureHasLayout(pending, texture, layout, range), "Prepare a fixed sampled texture before rendering.");
		}

		for (const NativeStorageTexture& binding : storageTextures)
		{
			const Texture& texture = *static_cast<const Texture*>(binding.Texture);
			const TextureViewDesc* const range = binding.View != nullptr ? &static_cast<const TextureView*>(binding.View)->Descriptor : nullptr;

			ASSERT(TextureHasLayout(pending, texture, VK_IMAGE_LAYOUT_GENERAL, range), "Prepare a fixed storage texture before rendering.");
		}
		SynchronizeGraphicsStorage(state, pending);

		if (textures.IsEmpty() && storageTextures.IsEmpty() && buffers.IsEmpty() && accelNative == nullptr)
		{
			return Result<void>::Success();
		}

		for (const NativeGraphicsBuffer& binding : buffers)
		{
			const bool bUniform = binding.Type == EGraphicsBufferBindingType::UniformBuffer;
			if (binding.View == nullptr &&
				(binding.ByteCount > (bUniform ? state.DeviceProperties.limits.maxUniformBufferRange : state.DeviceProperties.limits.maxStorageBufferRange) ||
						binding.ByteOffset % (bUniform ? state.DeviceProperties.limits.minUniformBufferOffsetAlignment : state.DeviceProperties.limits.minStorageBufferOffsetAlignment) != 0))
			{
				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsBinding", "The buffer exceeds the Vulkan descriptor range.");
			}
		}
		bool bReuse = pending.LastTexturePipeline == &pipeline &&
			pending.LastGraphicsAccelStruct == accelNative &&
			pending.LastTextureCount == textures.Size() &&
			pending.LastStorageTextureCount == storageTextures.Size() &&
			pending.LastGraphicsBufferCount == buffers.Size();

		for (usize index = 0; bReuse && index < textures.Size(); ++index)
		{
			bReuse = pending.LastTextures[index] == textures[index].Texture && pending.LastSamplers[index] == textures[index].Sampler && pending.LastViews[index] == textures[index].View;
		}

		for (usize index = 0; bReuse && index < storageTextures.Size(); ++index)
		{
			bReuse = pending.LastStorageTextures[index] == storageTextures[index].Texture && pending.LastStorageViews[index] == storageTextures[index].View;
		}

		for (usize index = 0; bReuse && index < buffers.Size(); ++index)
		{
			bReuse = pending.LastGraphicsBuffers[index] == buffers[index].Buffer &&
				pending.LastGraphicsBufferRanges[index].ByteOffset == buffers[index].ByteOffset &&
				pending.LastGraphicsBufferRanges[index].ByteCount == buffers[index].ByteCount;
		}

		if (bReuse)
		{
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, 0, 1, &pending.LastTextureSet,
				0, nullptr);

			return Result<void>::Success();
		}
		constexpr uint32 SETS_PER_POOL = 32;

		if (pending.DescriptorPools.IsEmpty() ||
			pending.DescriptorPools.Back().UsedSets == SETS_PER_POOL ||
			(pipeline.AccelStructSlot != 0xFFFFFFFFu && !pending.DescriptorPools.Back().bAccelerationStructures))
		{
			VkDescriptorPoolSize poolSizes[8]{ {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_SAMPLER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, SETS_PER_POOL} };
			VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			poolInfo.maxSets = SETS_PER_POOL;
			poolInfo.poolSizeCount = pipeline.AccelStructSlot != 0xFFFFFFFFu ? 8 : 7;
			poolInfo.pPoolSizes = poolSizes;
			VkDescriptorPool pool = VK_NULL_HANDLE;

			if (state.F.CreateDescriptorPool(state.Device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanGraphics", "Could not allocate texture descriptors."));
			}

			if (!pending.DescriptorPools.Add({ pool, 0, pipeline.AccelStructSlot != 0xFFFFFFFFu }))
			{
				state.F.DestroyDescriptorPool(state.Device, pool, nullptr);

				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track texture descriptors."));
			}
		}
		VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocation.descriptorPool = pending.DescriptorPools.Back().Handle;
		allocation.descriptorSetCount = 1;
		allocation.pSetLayouts = &pipeline.SetLayouts[0];
		VkDescriptorSet set = VK_NULL_HANDLE;

		if (state.F.AllocateDescriptorSets(state.Device, &allocation, &set) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanGraphics", "Could not allocate a texture descriptor set."));
		}
		++pending.DescriptorPools.Back().UsedSets;
		VkDescriptorImageInfo images[8]{};
		VkDescriptorImageInfo samplers[8]{};
		VkDescriptorImageInfo storageImages[8]{};
		VkDescriptorBufferInfo bufferInfos[8]{};
		VkWriteDescriptorSet writes[33]{};
		for (usize index = 0; index < textures.Size(); ++index)
		{
			const Texture& texture = *static_cast<const Texture*>(textures[index].Texture);
			const TextureViewDesc* const range = textures[index].View != nullptr ? &static_cast<const TextureView*>(textures[index].View)->Descriptor : nullptr;
			images[index] = { VK_NULL_HANDLE,
							 textures[index].View != nullptr ? static_cast<const TextureView*>(textures[index].View)->Handle : texture.View,
							 SampledTextureLayout(pending, texture, range) };
			samplers[index] = { textures[index].Sampler != nullptr ? static_cast<const Sampler*>(textures[index].Sampler)->Handle
																  : pipeline.Samplers[0][index],
							   VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
			writes[2 * index] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			writes[2 * index].dstSet = set;
			writes[2 * index].dstBinding = pipeline.SampledTextureSlots[0][index];
			writes[2 * index].descriptorCount = 1;
			writes[2 * index].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			writes[2 * index].pImageInfo = &images[index];
			writes[2 * index + 1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			writes[2 * index + 1].dstSet = set;
			writes[2 * index + 1].dstBinding = 16 + pipeline.SampledSamplerSlots[0][index];
			writes[2 * index + 1].descriptorCount = 1;
			writes[2 * index + 1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
			writes[2 * index + 1].pImageInfo = &samplers[index];
		}

		for (usize index = 0; index < storageTextures.Size(); ++index)
		{
			const Texture& texture = *static_cast<const Texture*>(storageTextures[index].Texture);
			storageImages[index] = { VK_NULL_HANDLE,
									storageTextures[index].View != nullptr
										? static_cast<const TextureView*>(storageTextures[index].View)->Handle
										: texture.GetStorageView(),
									VK_IMAGE_LAYOUT_GENERAL };
			VkWriteDescriptorSet& write = writes[textures.Size() * 2 + index];
			write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.dstSet = set;
			write.dstBinding = 64 + pipeline.StorageTextureSlots[0][index];
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			write.pImageInfo = &storageImages[index];
		}

		for (usize index = 0; index < buffers.Size(); ++index)
		{
			const Buffer& buffer = *static_cast<const Buffer*>(buffers[index].Buffer);

			bufferInfos[index] = { buffer.Handle, buffers[index].ByteOffset, buffers[index].ByteCount };
			VkWriteDescriptorSet& write = writes[textures.Size() * 2 + storageTextures.Size() + index];
			write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.dstSet = set;
			write.dstBinding = 32 + buffers[index].Slot;
			write.descriptorCount = 1;
			write.descriptorType = BufferDescriptorType(buffers[index].Type, buffers[index].Format);
			write.pBufferInfo = &bufferInfos[index];

			if (buffers[index].View != nullptr)
			{
				write.pBufferInfo = nullptr;
				write.pTexelBufferView = &static_cast<const BufferView*>(buffers[index].View)->Handle;
			}
		}
		VkAccelerationStructureKHR accelHandle = accelNative != nullptr ? static_cast<NativeAccelStruct*>(accelNative)->Handle : VK_NULL_HANDLE;
		VkWriteDescriptorSetAccelerationStructureKHR accelInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
		if (accelNative != nullptr)
		{
			accelInfo.accelerationStructureCount = 1;
			accelInfo.pAccelerationStructures = &accelHandle;
			VkWriteDescriptorSet& write = writes[textures.Size() * 2 + storageTextures.Size() + buffers.Size()];
			write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.pNext = &accelInfo;
			write.dstSet = set;
			write.dstBinding = 80 + pipeline.AccelStructSlot;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		}
		WriteCommandDescriptors(
			state, pending,
			static_cast<uint32>(textures.Size() * 2 + storageTextures.Size() + buffers.Size() + (accelNative != nullptr ? 1 : 0)),
			writes, 0, nullptr);
		state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, 0, 1, &set, 0, nullptr);
		pending.LastTexturePipeline = &pipeline;
		pending.LastGraphicsAccelStruct = accelNative;
		pending.LastTextureCount = static_cast<uint32>(textures.Size());
		pending.LastStorageTextureCount = static_cast<uint32>(storageTextures.Size());
		pending.LastGraphicsBufferCount = static_cast<uint32>(buffers.Size());
		pending.LastTextureSet = set;

		for (usize index = 0; index < textures.Size(); ++index)
		{
			pending.LastTextures[index] = textures[index].Texture;
			pending.LastSamplers[index] = textures[index].Sampler;
			pending.LastViews[index] = textures[index].View;
		}

		for (usize index = 0; index < storageTextures.Size(); ++index)
		{
			pending.LastStorageTextures[index] = storageTextures[index].Texture;
			pending.LastStorageViews[index] = storageTextures[index].View;
		}

		for (usize index = 0; index < buffers.Size(); ++index)
		{
			pending.LastGraphicsBuffers[index] = buffers[index].Buffer;
			pending.LastGraphicsBufferRanges[index] = { buffers[index].ByteOffset, buffers[index].ByteCount };
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<VkDescriptorSet> CaptureGraphicsSampledTable(State& state, Pending& pending, const DescriptorTable& table)
	{
		using R = Result<VkDescriptorSet>;
		bool bNeedsSnapshot = false;
		for (uint32 index = 0; index < table.Capacity; ++index)
		{
			const vulkan::Texture* texture = table.Textures[index];
			if (texture == nullptr)
			{
				continue;
			}
			const vulkan::TextureView* view = table.TextureViews[index];
			const TextureViewDesc* range = view != nullptr ? &view->Descriptor : nullptr;
			const VkImageLayout layout = SampledTextureLayout(pending, *texture, range);
			const bool bStencil = range != nullptr && range->Aspect == ETextureAspect::Stencil;
			ASSERT(TextureHasLayout(pending, *texture, layout, range) &&
				(layout != VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL || !bStencil) &&
				(layout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL || bStencil),
				"Prepare a bindless sampled texture for shader access before rendering.");
			if (!SupportsAttachmentSamplingLayout(state, layout))
			{
				return R::Failure(EErrorCode::Unsupported, "Rhi.GraphicsSampledTexture", "The device cannot sample this active read-only attachment layout.");
			}
			bNeedsSnapshot |= layout != VK_IMAGE_LAYOUT_GENERAL;
		}
		if (!bNeedsSnapshot)
		{
			return R::Success(table.Set);
		}

		Result<void*> created = CreateDescriptorTable(&state, { EDescriptorType::SampledTexture, table.Capacity });
		if (!created)
		{
			return R::Failure(created.GetError());
		}
		DescriptorTable* snapshot = static_cast<DescriptorTable*>(created.Value());
		SharedPtr<PreparedDescriptorSet> owner;
		if (!MakeSharedInto(state.Resource, &owner, &state, snapshot, EPreparedDescriptorStorage::TableSnapshot))
		{
			DestroyDescriptorTable(&state, snapshot);

			return R::Failure(Failure("Rhi.Allocation", "Could not retain sampled table layouts."));
		}
		for (uint32 index = 0; index < table.Capacity; ++index)
		{
			const vulkan::Texture* texture = table.Textures[index];
			if (texture == nullptr)
			{
				continue;
			}
			const vulkan::TextureView* view = table.TextureViews[index];
			VkDescriptorImageInfo image{};
			image.imageView = view != nullptr ? view->Handle : texture->View;
			image.imageLayout = SampledTextureLayout(pending, *texture, view != nullptr ? &view->Descriptor : nullptr);
			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.dstSet = snapshot->Set;
			write.dstBinding = 0;
			write.dstArrayElement = index;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			write.pImageInfo = &image;
			WriteCommandDescriptors(state, pending, 1, &write, 0, nullptr);
		}
		// RHI pins the source table and its resources; Pending retains this layout-specific set through retirement.
		if (!pending.DescriptorOwners.Add(Move(owner)))
		{
			return R::Failure(Failure("Rhi.Allocation", "Could not track sampled table layouts."));
		}

		return R::Success(snapshot->Set);
	}

	[[nodiscard]] Result<void> BindGraphicsDescriptorTable(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		void* native,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative)
	{
		if (storageTextureNative != nullptr)
		{
			const DescriptorTable& table = *static_cast<const DescriptorTable*>(storageTextureNative);

			ASSERT(DescriptorTableTexturesHaveLayout(pending, table, VK_IMAGE_LAYOUT_GENERAL), "Prepare a new storage texture for shader access before rendering.");
		}

		if (native != nullptr)
		{
			Result<VkDescriptorSet> captured = CaptureGraphicsSampledTable(state, pending, *static_cast<const DescriptorTable*>(native));
			if (!captured)
			{
				return Result<void>::Failure(captured.GetError());
			}
			const VkDescriptorSet set = captured.Value();
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, pipeline.SetLayoutCount, 1,
				&set, 0, nullptr);
		}

		if (samplerNative != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(samplerNative)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, pipeline.SetLayoutCount + 1,
				1, &set, 0, nullptr);
		}

		if (storageNative != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(storageNative)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, pipeline.SetLayoutCount + 2,
				1, &set, 0, nullptr);
		}

		if (storageTextureNative != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(storageTextureNative)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, pipeline.SetLayoutCount + 3,
				1, &set, 0, nullptr);
		}

		if (uniformNative != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(uniformNative)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, pipeline.SetLayoutCount + 4,
				1, &set, 0, nullptr);
		}

		if (readOnlyBufferNative != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(readOnlyBufferNative)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout, pipeline.SetLayoutCount + 5,
				1, &set, 0, nullptr);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> TrackGraphicsStorageWrites(
		Pending& pending,
		Span<const NativeStorageTexture> storageTextures,
		Span<const NativeGraphicsBuffer> buffers,
		const DescriptorTable* storageBufferTable,
		const DescriptorTable* storageTextureTable)
	{
		if (!pending.bAutomaticBarriers)
		{
			pending.RangeStorageTableCount = 0;
			return Result<void>::Success();
		}

		const usize bufferCount = pending.GraphicsStorageBuffers.Size();
		const usize imageCount = pending.GraphicsStorageImages.Size();
		const auto addBuffer = [&pending](const Buffer& buffer)
			{
				for (const VkBufferMemoryBarrier& existing : pending.GraphicsStorageBuffers)
				{
					if (existing.buffer == buffer.Handle)
					{
						return true;
					}
				}
				VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
				barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
					VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT |
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.buffer = buffer.Handle;
				barrier.offset = 0;
				barrier.size = VK_WHOLE_SIZE;

				return pending.GraphicsStorageBuffers.Add(barrier);
			};
		const auto addImage = [&pending](const Texture& texture, const TextureView* view)
			{
				const TextureViewDesc* const range = view != nullptr ? &view->Descriptor : nullptr;
				const uint32 baseMip = range != nullptr ? range->BaseMipLevel : 0;
				const uint32 mipCount = range != nullptr ? range->MipLevelCount : texture.MipLevels;
				const uint32 baseLayer = range != nullptr ? range->BaseArrayLayer : 0;
				const uint32 layerCount = range != nullptr ? range->ArrayLayerCount : texture.ArrayLayers;
				const VkImageAspectFlags aspect =
					texture.bDepth ? VK_IMAGE_ASPECT_DEPTH_BIT | (GetFormatInfo(texture.Format).bHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)
					: VK_IMAGE_ASPECT_COLOR_BIT;

				for (uint32 layer = baseLayer; layer < baseLayer + layerCount; ++layer)
				{
					for (uint32 mip = baseMip; mip < baseMip + mipCount; ++mip)
					{
						bool bDuplicate = false;

						for (const VkImageMemoryBarrier& existing : pending.GraphicsStorageImages)
						{
							bDuplicate |= existing.image == texture.Handle && existing.subresourceRange.baseMipLevel == mip && existing.subresourceRange.baseArrayLayer == layer;
						}

						if (bDuplicate)
						{
							continue;
						}
						VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
						barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
						barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
						barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
						barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
						barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						barrier.image = texture.Handle;
						barrier.subresourceRange = { aspect, mip, 1, layer, 1 };
						if (!pending.GraphicsStorageImages.Add(barrier))
						{
							return false;
						}
					}
				}

				return true;
			};
		bool bTracked = true;

		for (const NativeGraphicsBuffer& binding : buffers)
		{
			if (binding.Type == EGraphicsBufferBindingType::StorageBuffer)
			{
				bTracked = addBuffer(*static_cast<const Buffer*>(binding.Buffer));

				if (!bTracked)
				{
					break;
				}
			}
		}

		for (const NativeStorageTexture& binding : storageTextures)
		{
			if (bTracked)
			{
				bTracked = addImage(*static_cast<const Texture*>(binding.Texture), static_cast<const TextureView*>(binding.View));
			}
		}

		if (bTracked && storageBufferTable != nullptr)
		{
			for (Buffer* buffer : storageBufferTable->Buffers)
			{
				if (buffer != nullptr && !addBuffer(*buffer))
				{
					bTracked = false;
					break;
				}
			}
		}

		if (bTracked && storageTextureTable != nullptr)
		{
			for (usize index = 0; index < storageTextureTable->Textures.Size(); ++index)
			{
				Texture* const texture = storageTextureTable->Textures[index];

				if (texture != nullptr && !addImage(*texture, storageTextureTable->TextureViews[index]))
				{
					bTracked = false;
					break;
				}
			}
		}
		for (uint32 tableIndex = 0; bTracked && tableIndex < pending.RangeStorageTableCount; ++tableIndex)
		{
			const NativeTableRangeBinding& range = pending.RangeStorageTables[tableIndex];
			const DescriptorTable& table = *static_cast<const DescriptorTable*>(range.Table);
			if (table.Type == EDescriptorType::StorageBuffer)
			{
				for (const uint32 index : range.UsedEntries)
				{
					Buffer* const buffer = table.Buffers[index];
					if (buffer != nullptr && !addBuffer(*buffer))
					{
						bTracked = false;
						break;
					}
				}
			}
			else
			{
				for (const uint32 index : range.UsedEntries)
				{
					Texture* const texture = table.Textures[index];

					if (texture != nullptr && !addImage(*texture, table.TextureViews[index]))
					{
						bTracked = false;
						break;
					}
				}
			}
		}
		pending.RangeStorageTableCount = 0;

		if (!bTracked)
		{
			while (pending.GraphicsStorageBuffers.Size() > bufferCount)
			{
				pending.GraphicsStorageBuffers.PopBack();
			}

			while (pending.GraphicsStorageImages.Size() > imageCount)
			{
				pending.GraphicsStorageImages.PopBack();
			}

			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track graphics storage writes."));
		}
		pending.bGraphicsStorageWritten |= pending.GraphicsStorageBuffers.Size() != bufferCount ||
			pending.GraphicsStorageImages.Size() != imageCount ||
			!pending.GraphicsStorageBuffers.IsEmpty() || !pending.GraphicsStorageImages.IsEmpty();

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBindExtraVertexBuffers(State* opaque, void* native, Span<const NativeVertexBuffer> buffers)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		VkBuffer handles[3]{};
		VkDeviceSize offsets[3]{};
		for (usize index = 0; index < buffers.Size(); ++index)
		{
			handles[index] = static_cast<Buffer*>(buffers[index].Buffer)->Handle;
			offsets[index] = buffers[index].Offset;
		}

		if (!buffers.IsEmpty())
		{
			state.F.CmdBindVertexBuffers(pending.Commands, 1, static_cast<uint32>(buffers.Size()), handles, offsets);
		}

		return Result<void>::Success();
	}

	void ApplyBlendConstant(State& state, Pending& pending, const GraphicsPipeline& pipeline, const BlendColor& color) noexcept
	{
		if (!pipeline.bBlendConstant)
		{
			return;
		}

		const float value[4]{ color.R, color.G, color.B, color.A };
		state.F.CmdSetBlendConstants(pending.Commands, value);
	}

	void ApplyShadingRate(State& state, Pending& pending, EShadingRate rate, EShadingRateCombiner primitive, EShadingRateCombiner image) noexcept
	{
		if (state.SupportedShadingRates[0] == 0 ||
			(pending.bShadingRateSet && pending.CurrentShadingRate == rate && pending.CurrentPrimitiveCombiner == primitive && pending.CurrentImageCombiner == image))
		{
			return;
		}
		constexpr VkExtent2D RATES[7]{ {1, 1}, {1, 2}, {2, 1}, {2, 2}, {2, 4}, {4, 2}, {4, 4} };
		constexpr VkFragmentShadingRateCombinerOpKHR COMBINERS[5]{
			VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR,
			VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MIN_KHR, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MAX_KHR,
			VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MUL_KHR };
		const VkFragmentShadingRateCombinerOpKHR selected[2]{ COMBINERS[static_cast<uint32>(primitive)],
															 COMBINERS[static_cast<uint32>(image)] };
		state.F.CmdSetFragmentShadingRate(pending.Commands, &RATES[static_cast<uint32>(rate)], selected);
		pending.CurrentShadingRate = rate;
		pending.CurrentPrimitiveCombiner = primitive;
		pending.CurrentImageCombiner = image;
		pending.bShadingRateSet = true;
	}

	[[nodiscard]] Result<void> RecordDraw(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* vertexNative,
		uint64 vertexOffset,
		uint64,
		uint32,
		Span<const NativeSampledTexture> textures,
		Span<const NativeStorageTexture> storageTextures,
		Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsState& drawState,
		const DrawArguments& draw)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(pipelineNative);
		state.F.CmdBindPipeline(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pending.bShadingRateImageActive ? pipeline.ShadingRateHandle : pipeline.Handle);

		if (pipeline.bStencil)
		{
			state.F.CmdSetStencilReference(pending.Commands, VK_STENCIL_FACE_FRONT_AND_BACK, draw.StencilReference);
		}
		ApplyBlendConstant(state, pending, pipeline, drawState.BlendConstant);
		ApplyShadingRate(state, pending, drawState.ShadingRate, drawState.PrimitiveShadingRateCombiner,
			drawState.ImageShadingRateCombiner);
		Result<void> bound = BindGraphicsResources(state, pending, pipeline, textures, storageTextures, buffers, accelNative);

		if (!bound)
		{
			return bound;
		}
		bound = BindGraphicsDescriptorTable(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		if (!bound)
		{
			return bound;
		}
		bound = TrackGraphicsStorageWrites(pending, storageTextures, buffers, static_cast<const DescriptorTable*>(storageNative), static_cast<const DescriptorTable*>(storageTextureNative));

		if (!bound)
		{
			return bound;
		}

		if (pipeline.PushConstantBytes != 0)
		{
			state.F.CmdPushConstants(pending.Commands, pipeline.Layout, pipeline.ShaderStages, 0, pipeline.PushConstantBytes,
				draw.PushConstants.Data());
		}

		if (pipeline.bMeshShader)
		{
			state.F.CmdDrawMeshTasks(pending.Commands, draw.VertexCount, draw.InstanceCount, draw.FirstVertex);
		}
		else
		{
			if (vertexNative != nullptr)
			{
				const VkBuffer buffer = static_cast<Buffer*>(vertexNative)->Handle;
				const VkDeviceSize offset = vertexOffset;
				state.F.CmdBindVertexBuffers(pending.Commands, 0, 1, &buffer, &offset);
			}
			state.F.CmdDraw(pending.Commands, draw.VertexCount, draw.InstanceCount, draw.FirstVertex, draw.FirstInstance);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordDrawIndexed(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* vertexNative,
		void* indexNative,
		uint64 vertexOffset,
		uint64,
		uint64 indexOffset,
		uint32,
		Span<const NativeSampledTexture> textures,
		Span<const NativeStorageTexture> storageTextures,
		Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsState& drawState,
		const DrawIndexedArguments& draw)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(pipelineNative);
		state.F.CmdBindPipeline(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pending.bShadingRateImageActive ? pipeline.ShadingRateHandle : pipeline.Handle);

		if (pipeline.bStencil)
		{
			state.F.CmdSetStencilReference(pending.Commands, VK_STENCIL_FACE_FRONT_AND_BACK, draw.StencilReference);
		}
		ApplyBlendConstant(state, pending, pipeline, drawState.BlendConstant);
		ApplyShadingRate(state, pending, drawState.ShadingRate, drawState.PrimitiveShadingRateCombiner,
			drawState.ImageShadingRateCombiner);
		Result<void> bound = BindGraphicsResources(state, pending, pipeline, textures, storageTextures, buffers, accelNative);

		if (!bound)
		{
			return bound;
		}
		bound = BindGraphicsDescriptorTable(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		if (!bound)
		{
			return bound;
		}
		bound = TrackGraphicsStorageWrites(pending, storageTextures, buffers, static_cast<const DescriptorTable*>(storageNative), static_cast<const DescriptorTable*>(storageTextureNative));

		if (!bound)
		{
			return bound;
		}

		if (pipeline.PushConstantBytes != 0)
		{
			state.F.CmdPushConstants(pending.Commands, pipeline.Layout, pipeline.ShaderStages, 0, pipeline.PushConstantBytes,
				draw.PushConstants.Data());
		}

		if (vertexNative != nullptr)
		{
			const VkBuffer vertex = static_cast<Buffer*>(vertexNative)->Handle;
			const VkDeviceSize offset = vertexOffset;
			state.F.CmdBindVertexBuffers(pending.Commands, 0, 1, &vertex, &offset);
		}
		state.F.CmdBindIndexBuffer(pending.Commands, static_cast<Buffer*>(indexNative)->Handle, indexOffset,
			drawState.IndexFormat == EFormat::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
		state.F.CmdDrawIndexed(pending.Commands, draw.IndexCount, draw.InstanceCount, draw.FirstIndex, draw.BaseVertex, draw.FirstInstance);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordDrawIndirect(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* argumentNative,
		void* vertexNative,
		void* countNative,
		uint64 vertexOffset,
		uint64,
		uint32,
		Span<const NativeSampledTexture> textures,
		Span<const NativeStorageTexture> storageTextures,
		Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsDrawIndirect& draw)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(pipelineNative);
		state.F.CmdBindPipeline(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pending.bShadingRateImageActive ? pipeline.ShadingRateHandle : pipeline.Handle);

		if (pipeline.bStencil)
		{
			state.F.CmdSetStencilReference(pending.Commands, VK_STENCIL_FACE_FRONT_AND_BACK, draw.StencilReference);
		}
		ApplyBlendConstant(state, pending, pipeline, draw.BlendConstant);
		ApplyShadingRate(state, pending, draw.ShadingRate, draw.PrimitiveShadingRateCombiner, draw.ImageShadingRateCombiner);
		Result<void> bound = BindGraphicsResources(state, pending, pipeline, textures, storageTextures, buffers, accelNative);

		if (!bound)
		{
			return bound;
		}
		bound = BindGraphicsDescriptorTable(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		if (!bound)
		{
			return bound;
		}
		bound = TrackGraphicsStorageWrites(pending, storageTextures, buffers, static_cast<const DescriptorTable*>(storageNative), static_cast<const DescriptorTable*>(storageTextureNative));

		if (!bound)
		{
			return bound;
		}

		if (pipeline.PushConstantBytes != 0)
		{
			state.F.CmdPushConstants(pending.Commands, pipeline.Layout, pipeline.ShaderStages, 0, pipeline.PushConstantBytes,
				draw.PushConstants.Data());
		}

		if (!pipeline.bMeshShader && vertexNative != nullptr)
		{
			const VkBuffer vertex = static_cast<Buffer*>(vertexNative)->Handle;
			const VkDeviceSize offset = vertexOffset;
			state.F.CmdBindVertexBuffers(pending.Commands, 0, 1, &vertex, &offset);
		}

		const VkBuffer arguments = static_cast<Buffer*>(argumentNative)->Handle;

		if (pipeline.bMeshShader)
		{
			state.F.CmdDrawMeshTasksIndirect(pending.Commands, arguments, draw.ArgumentsOffsetBytes, 1, sizeof(IndirectMeshArguments));

			return Result<void>::Success();
		}

		if (countNative != nullptr)
		{
			state.F.CmdDrawIndirectCount(pending.Commands, arguments, draw.ArgumentsOffsetBytes, static_cast<Buffer*>(countNative)->Handle,
				draw.CountBufferOffsetBytes, draw.DrawCount, sizeof(IndirectDrawArguments));

			return Result<void>::Success();
		}

		for (uint32 index = 0; index < draw.DrawCount; ++index)
		{
			state.F.CmdDrawIndirect(pending.Commands, arguments,
				draw.ArgumentsOffsetBytes + static_cast<uint64>(index) * sizeof(IndirectDrawArguments), 1,
				sizeof(IndirectDrawArguments));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordDrawIndexedIndirect(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* argumentNative,
		void* vertexNative,
		void* indexNative,
		void* countNative,
		uint64 vertexOffset,
		uint64,
		uint64 indexOffset,
		uint32,
		Span<const NativeSampledTexture> textures,
		Span<const NativeStorageTexture> storageTextures,
		Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsDrawIndexedIndirect& draw)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(pipelineNative);
		state.F.CmdBindPipeline(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pending.bShadingRateImageActive ? pipeline.ShadingRateHandle : pipeline.Handle);

		if (pipeline.bStencil)
		{
			state.F.CmdSetStencilReference(pending.Commands, VK_STENCIL_FACE_FRONT_AND_BACK, draw.StencilReference);
		}
		ApplyBlendConstant(state, pending, pipeline, draw.BlendConstant);
		ApplyShadingRate(state, pending, draw.ShadingRate, draw.PrimitiveShadingRateCombiner, draw.ImageShadingRateCombiner);
		Result<void> bound = BindGraphicsResources(state, pending, pipeline, textures, storageTextures, buffers, accelNative);

		if (!bound)
		{
			return bound;
		}
		bound = BindGraphicsDescriptorTable(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		if (!bound)
		{
			return bound;
		}
		bound = TrackGraphicsStorageWrites(pending, storageTextures, buffers, static_cast<const DescriptorTable*>(storageNative), static_cast<const DescriptorTable*>(storageTextureNative));

		if (!bound)
		{
			return bound;
		}

		if (pipeline.PushConstantBytes != 0)
		{
			state.F.CmdPushConstants(pending.Commands, pipeline.Layout, pipeline.ShaderStages, 0, pipeline.PushConstantBytes,
				draw.PushConstants.Data());
		}

		if (vertexNative != nullptr)
		{
			const VkBuffer vertex = static_cast<Buffer*>(vertexNative)->Handle;
			const VkDeviceSize offset = vertexOffset;
			state.F.CmdBindVertexBuffers(pending.Commands, 0, 1, &vertex, &offset);
		}
		state.F.CmdBindIndexBuffer(pending.Commands, static_cast<Buffer*>(indexNative)->Handle, indexOffset,
			draw.IndexFormat == EFormat::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
		const VkBuffer arguments = static_cast<Buffer*>(argumentNative)->Handle;

		if (countNative != nullptr)
		{
			state.F.CmdDrawIndexedIndirectCount(pending.Commands, arguments, draw.ArgumentsOffsetBytes,
				static_cast<Buffer*>(countNative)->Handle, draw.CountBufferOffsetBytes, draw.DrawCount,
				sizeof(IndirectDrawIndexedArguments));

			return Result<void>::Success();
		}

		for (uint32 index = 0; index < draw.DrawCount; ++index)
		{
			state.F.CmdDrawIndexedIndirect(pending.Commands, arguments,
				draw.ArgumentsOffsetBytes + static_cast<uint64>(index) * sizeof(IndirectDrawIndexedArguments),
				1, sizeof(IndirectDrawIndexedArguments));
		}

		return Result<void>::Success();
	}
}
