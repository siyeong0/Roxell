#include "VulkanBackend.h"
#include "VulkanDescriptors.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<void> RecordComputeStorageBufferBarriers(State& state, Pending& pending, Span<const NativeBindingSet> sets, const DescriptorTable* table)
	{
		Array<Buffer*> resources(state.Resource);

		for (const NativeBindingSet& set : sets)
		{
			for (const NativeComputeBuffer& binding : set.Buffers)
			{
				if (binding.Type == EComputeBindingType::StorageBuffer && !resources.Add(static_cast<Buffer*>(binding.Buffer)))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track compute storage buffers."));
				}
			}
		}

		if (table != nullptr)
		{
			for (Buffer* buffer : table->Buffers)
			{
				if (buffer != nullptr && !resources.Add(buffer))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track compute storage buffers."));
				}
			}
		}

		if (resources.IsEmpty())
		{
			return Result<void>::Success();
		}

		if (resources.Size() > 1)
		{
			Sort(resources.Data(), resources.Data() + resources.Size(), Less<Buffer*>{});
		}

		Array<VkBufferMemoryBarrier> barriers(state.Resource);

		if (!barriers.Reserve(resources.Size()))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not record compute storage barriers."));
		}

		const bool bGraphicsQueue = (QueueFlags(state, pending.QueueKind) & VK_QUEUE_GRAPHICS_BIT) != 0;
		Buffer* previous = nullptr;

		for (Buffer* buffer : resources)
		{
			if (buffer == previous)
			{
				continue;
			}
			previous = buffer;

			if (!ShouldRecordBufferUavBarrier(pending, buffer->Handle))
			{
				continue;
			}
			VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
				VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

			if (bGraphicsQueue)
			{
				barrier.dstAccessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
			}
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = buffer->Handle;
			barrier.offset = 0;
			barrier.size = VK_WHOLE_SIZE;

			if (!barriers.Add(barrier))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not record compute storage barriers."));
			}
		}

		if (!barriers.IsEmpty())
		{
			RecordBarriers(state, pending, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
				static_cast<uint32>(barriers.Size()), barriers.Data(), 0, nullptr);

			for (const VkBufferMemoryBarrier& barrier : barriers)
			{
				MarkBufferUavBarrierPlaced(pending, barrier.buffer);
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordComputeStorageTextureBarriers(State& state, Pending& pending, Span<const NativeBindingSet> sets, const DescriptorTable* table)
	{
		struct ImageSubresource final
		{
			Texture* Item = nullptr;
			uint32 Mip = 0;
			uint32 Layer = 0;
		};

		Array<ImageSubresource> subresources(state.Resource);
		const auto add = [&](Texture* texture, const TextureView* view)
			{
				const uint32 baseMip = view != nullptr ? view->Descriptor.BaseMipLevel : 0;
				const uint32 mipCount = view != nullptr ? view->Descriptor.MipLevelCount : texture->MipLevels;
				const uint32 baseLayer = view != nullptr ? view->Descriptor.BaseArrayLayer : 0;
				const uint32 layerCount = view != nullptr ? view->Descriptor.ArrayLayerCount : texture->ArrayLayers;

				for (uint32 layer = baseLayer; layer < baseLayer + layerCount; ++layer)
				{
					for (uint32 mip = baseMip; mip < baseMip + mipCount; ++mip)
					{
						if (!subresources.Add({ texture, mip, layer }))
						{
							return false;
						}
					}
				}

				return true;
			};
		for (const NativeBindingSet& set : sets)
		{
			for (const NativeComputeTexture& binding : set.Textures)
			{
				if (binding.Type == EComputeTextureBindingType::StorageTexture && !add(static_cast<Texture*>(binding.Texture), static_cast<const TextureView*>(binding.View)))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track compute storage textures."));
				}
			}
		}

		if (table != nullptr)
		{
			for (usize index = 0; index < table->Textures.Size(); ++index)
			{
				if (table->Textures[index] != nullptr && !add(table->Textures[index], table->TextureViews[index]))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track compute storage textures."));
				}
			}
		}

		if (subresources.IsEmpty())
		{
			return Result<void>::Success();
		}

		if (subresources.Size() > 1)
		{
			Sort(subresources.Data(), subresources.Data() + subresources.Size(),
				[](const ImageSubresource& a, const ImageSubresource& b)
				{
					if (a.Item != b.Item)
					{
						return Less<Texture*>{}(a.Item, b.Item);
					}
					if (a.Layer != b.Layer)
					{
						return a.Layer < b.Layer;
					}

					return a.Mip < b.Mip;
				});
		}

		usize uniqueCount = 0;

		for (const ImageSubresource& subresource : subresources)
		{
			if (uniqueCount == 0 ||
				subresources[uniqueCount - 1].Item != subresource.Item ||
				subresources[uniqueCount - 1].Mip != subresource.Mip ||
				subresources[uniqueCount - 1].Layer != subresource.Layer)
			{
				subresources[uniqueCount++] = subresource;
			}
		}

		Array<VkImageMemoryBarrier> barriers(state.Resource);

		if (!barriers.Reserve(uniqueCount))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not record compute texture barriers."));
		}

		for (usize index = 0; index < uniqueCount; ++index)
		{
			const ImageSubresource& subresource = subresources[index];

			if (!ShouldRecordTextureUavBarrier(pending, subresource.Item->Handle))
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
			barrier.image = subresource.Item->Handle;
			barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, subresource.Mip, 1, subresource.Layer, 1 };
			if (!barriers.Add(barrier))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not record compute texture barriers."));
			}
		}

		if (!barriers.IsEmpty())
		{
			RecordBarriers(state, pending, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
				nullptr, static_cast<uint32>(barriers.Size()), barriers.Data());

			for (const VkImageMemoryBarrier& barrier : barriers)
			{
				MarkTextureUavBarrierPlaced(pending, barrier.image);
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordComputeInputBarriers(
		State& state,
		Pending& pending,
		Span<const NativeBindingSet> sets,
		const DescriptorTable* sampledTable,
		const DescriptorTable* storageBufferTable,
		const DescriptorTable* storageTextureTable,
		const DescriptorTable* uniformBufferTable,
		const DescriptorTable* readOnlyBufferTable,
		Buffer* indirect,
		bool bAccelStruct)
	{
		struct BufferUse final
		{
			Buffer* Item = nullptr;
			bool bIndirect = false;
			bool bStorage = false;
		};

		struct ImageUse final
		{
			Texture* Item = nullptr;
			uint32 Mip = 0;
			uint32 Layer = 0;
			VkImageLayout Layout = VK_IMAGE_LAYOUT_GENERAL;
			bool bStorage = false;
		};

		Array<BufferUse> buffers(state.Resource);
		Array<ImageUse> images(state.Resource);
		const auto addImage = [&](Texture* texture, const TextureView* view, bool bStorage)
			{
				const TextureViewDesc* const range = view != nullptr ? &view->Descriptor : nullptr;
				const uint32 baseMip = range != nullptr ? range->BaseMipLevel : 0;
				const uint32 mipCount = range != nullptr ? range->MipLevelCount : texture->MipLevels;
				const uint32 baseLayer = range != nullptr ? range->BaseArrayLayer : 0;
				const uint32 layerCount = range != nullptr ? range->ArrayLayerCount : texture->ArrayLayers;
				const VkImageLayout layout = bStorage ? VK_IMAGE_LAYOUT_GENERAL : SampledTextureLayout(pending, *texture, range);

				for (uint32 layer = baseLayer; layer < baseLayer + layerCount; ++layer)
				{
					for (uint32 mip = baseMip; mip < baseMip + mipCount; ++mip)
					{
						if (!images.Add({ texture, mip, layer, layout, bStorage }))
						{
							return false;
						}
					}
				}

				return true;
			};
		for (const NativeBindingSet& set : sets)
		{
			for (const NativeComputeBuffer& binding : set.Buffers)
			{
				if (!buffers.Add({ static_cast<Buffer*>(binding.Buffer), false, binding.Type == EComputeBindingType::StorageBuffer }))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track compute input buffers."));
				}
			}

			for (const NativeComputeTexture& binding : set.Textures)
			{
				if (!addImage(static_cast<Texture*>(binding.Texture), static_cast<const TextureView*>(binding.View),
					binding.Type == EComputeTextureBindingType::StorageTexture))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track compute input textures."));
				}
			}
		}

		const DescriptorTable* const bufferTables[3]{ storageBufferTable, uniformBufferTable, readOnlyBufferTable };
		for (const DescriptorTable* table : bufferTables)
		{
			if (table != nullptr)
			{
				for (Buffer* buffer : table->Buffers)
				{
					if (buffer != nullptr && !buffers.Add({ buffer, false, table == storageBufferTable }))
					{
						return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track compute input buffers."));
					}
				}
			}
		}

		const DescriptorTable* const imageTables[2]{ sampledTable, storageTextureTable };
		for (const DescriptorTable* table : imageTables)
		{
			if (table != nullptr)
			{
				for (usize index = 0; index < table->Textures.Size(); ++index)
				{
					if (table->Textures[index] != nullptr && !addImage(table->Textures[index], table->TextureViews[index], table == storageTextureTable))
					{
						return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track compute input textures."));
					}
				}
			}
		}

		if (indirect != nullptr && !buffers.Add({ indirect, true, false }))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track the compute indirect buffer."));
		}

		if (buffers.Size() > 1)
		{
			Sort(buffers.Data(), buffers.Data() + buffers.Size(),
				[](const BufferUse& a, const BufferUse& b) { return Less<Buffer*>{}(a.Item, b.Item); });
		}

		if (images.Size() > 1)
		{
			Sort(images.Data(), images.Data() + images.Size(),
				[](const ImageUse& a, const ImageUse& b)
				{
					if (a.Item != b.Item)
					{
						return Less<Texture*>{}(a.Item, b.Item);
					}
					if (a.Layer != b.Layer)
					{
						return a.Layer < b.Layer;
					}

					return a.Mip < b.Mip;
				});
		}

		Array<VkBufferMemoryBarrier> bufferBarriers(state.Resource);
		Array<VkImageMemoryBarrier> imageBarriers(state.Resource);

		if (!bufferBarriers.Reserve(buffers.Size()) || !imageBarriers.Reserve(images.Size()))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not record compute input barriers."));
		}

		for (usize index = 0; index < buffers.Size();)
		{
			Buffer* const buffer = buffers[index].Item;
			bool bIndirect = false;
			bool bOnlyStorage = true;
			do
			{
				bIndirect |= buffers[index].bIndirect;
				bOnlyStorage &= buffers[index++].bStorage;
			} while (index < buffers.Size() && buffers[index].Item == buffer);
			if (bOnlyStorage && !ShouldRecordBufferUavBarrier(pending, buffer->Handle))
			{
				continue;
			}
			VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | (bIndirect ? VK_ACCESS_INDIRECT_COMMAND_READ_BIT : 0);
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = buffer->Handle;
			barrier.offset = 0;
			barrier.size = VK_WHOLE_SIZE;

			if (!bufferBarriers.Add(barrier))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not record compute input buffer barriers."));
			}
		}

		for (usize index = 0; index < images.Size();)
		{
			const ImageUse& use = images[index];
			bool bOnlyStorage = true;
			usize next = index;
			do
			{
				ASSERT(images[next].Layout == use.Layout, "A texture subresource has incompatible descriptor layouts.");
				bOnlyStorage &= images[next++].bStorage;
			} while (next < images.Size() && images[next].Item == use.Item && images[next].Mip == use.Mip && images[next].Layer == use.Layer);
			index = next;

			if (bOnlyStorage && !ShouldRecordTextureUavBarrier(pending, use.Item->Handle))
			{
				continue;
			}
			VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			barrier.oldLayout = use.Layout;
			barrier.newLayout = use.Layout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = use.Item->Handle;
			barrier.subresourceRange = {
				static_cast<VkImageAspectFlags>(use.Item->bDepth
													? VK_IMAGE_ASPECT_DEPTH_BIT |
														  (GetFormatInfo(use.Item->Format).bHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)
													: VK_IMAGE_ASPECT_COLOR_BIT),
				use.Mip, 1, use.Layer, 1 };
			if (!imageBarriers.Add(barrier))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not record compute input image barriers."));
			}
		}
		// Device-address accesses have no enumerable resource binding; callers provide their dependency.
		VkMemoryBarrier accelBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		accelBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		accelBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

		if (!bufferBarriers.IsEmpty() || !imageBarriers.IsEmpty() || bAccelStruct)
		{
			RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | (indirect != nullptr ? VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT : 0), 0,
				bAccelStruct ? 1u : 0u, bAccelStruct ? &accelBarrier : nullptr, static_cast<uint32>(bufferBarriers.Size()),
				bufferBarriers.Data(), static_cast<uint32>(imageBarriers.Size()), imageBarriers.Data());
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordRangeComputeDispatch(
		State& state,
		Pending& pending,
		const ComputePipeline& pipeline,
		Span<const NativeBindingSet> sets,
		uint32 groupsX,
		uint32 groupsY,
		uint32 groupsZ,
		void* indirectNative,
		uint64 indirectOffset,
		Span<const Byte> pushConstants)
	{
		const uint32_t* limits = state.DeviceProperties.limits.maxComputeWorkGroupCount;
		ASSERT(indirectNative != nullptr || (groupsX <= limits[0] && groupsY <= limits[1] && groupsZ <= limits[2]), "The group count exceeds native limits.");
		if (pending.bAutomaticBarriers)
		{
			Result<void> inputs = RecordComputeInputBarriers(state, pending, sets, nullptr, nullptr, nullptr, nullptr, nullptr, static_cast<Buffer*>(indirectNative), false);
			if (!inputs)
			{
				return inputs;
			}
		}
		state.F.CmdBindPipeline(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Handle);
		if (pipeline.PushConstantBytes != 0)
		{
			state.F.CmdPushConstants(pending.Commands, pipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pipeline.PushConstantBytes,
				pushConstants.Data());
		}
		if (indirectNative != nullptr)
		{
			state.F.CmdDispatchIndirect(pending.Commands, static_cast<Buffer*>(indirectNative)->Handle, indirectOffset);
		}
		else
		{
			state.F.CmdDispatch(pending.Commands, groupsX, groupsY, groupsZ);
		}
		if (pending.bAutomaticBarriers)
		{
			Result<void> buffers = RecordComputeStorageBufferBarriers(state, pending, sets, nullptr);
			if (!buffers)
			{
				return buffers;
			}
			Result<void> textures = RecordComputeStorageTextureBarriers(state, pending, sets, nullptr);
			if (!textures)
			{
				return textures;
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordComputeDispatch(
		State* opaque,
		void* native,
		void* pipelineNative,
		Span<const NativeComputeBuffer> buffers,
		Span<const NativeComputeTexture> textures,
		uint32 groupsX,
		uint32 groupsY,
		uint32 groupsZ,
		void* indirectNative,
		uint64 indirectOffset,
		Span<const Byte> pushConstants,
		void* bindlessNative,
		void* samplerNative,
		void* storageBufferNative,
		void* storageTextureNative,
		void* uniformBufferNative,
		void* readOnlyBufferNative,
		void* accelNative)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		ComputePipeline& pipeline = *static_cast<ComputePipeline*>(pipelineNative);
		if (pipeline.Fixed.LayoutCount != 0)
		{
			const NativeBindingSet set{ buffers, textures };

			return RecordRangeComputeDispatch(state, pending, pipeline, { &set, 1 }, groupsX, groupsY, groupsZ, indirectNative,
				indirectOffset, pushConstants);
		}
		const uint32* limits = state.DeviceProperties.limits.maxComputeWorkGroupCount;

		ASSERT(indirectNative != nullptr || (groupsX <= limits[0] && groupsY <= limits[1] && groupsZ <= limits[2]), "The group count exceeds Vulkan device limits.");
		bool bWritesStorageTexture = storageTextureNative != nullptr;
		bool bWritesStorage = storageBufferNative != nullptr || bWritesStorageTexture;

		for (const NativeComputeBuffer& binding : buffers)
		{
			bWritesStorage |= binding.Type == EComputeBindingType::StorageBuffer;
		}

		for (const NativeComputeTexture& binding : textures)
		{
			bWritesStorageTexture |= binding.Type == EComputeTextureBindingType::StorageTexture;
		}
		bWritesStorage |= bWritesStorageTexture;

		for (const NativeComputeBuffer& binding : buffers)
		{
			const bool bUniform = binding.Type == EComputeBindingType::UniformBuffer;
			if (binding.View == nullptr &&
				(binding.ByteCount > (bUniform ? state.DeviceProperties.limits.maxUniformBufferRange : state.DeviceProperties.limits.maxStorageBufferRange) ||
						binding.ByteOffset % (bUniform ? state.DeviceProperties.limits.minUniformBufferOffsetAlignment : state.DeviceProperties.limits.minStorageBufferOffsetAlignment) != 0))
			{
				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ComputeBinding", "The buffer exceeds the Vulkan descriptor range.");
			}
		}

		const DescriptorTable* const bindless = static_cast<const DescriptorTable*>(bindlessNative);
		const DescriptorTable* const storageTextures = static_cast<const DescriptorTable*>(storageTextureNative);
		bool bReuse = pending.LastComputePipeline == &pipeline &&
			pending.LastComputeAccelStruct == accelNative &&
			pending.LastComputeBufferCount == buffers.Size() &&
			pending.LastComputeTextureCount == textures.Size();

		for (usize index = 0; bReuse && index < buffers.Size(); ++index)
		{
			bReuse = pending.LastComputeBuffers[index] == buffers[index].Buffer &&
				pending.LastComputeBufferRanges[index].ByteOffset == buffers[index].ByteOffset &&
				pending.LastComputeBufferRanges[index].ByteCount == buffers[index].ByteCount;
		}

		for (usize index = 0; bReuse && index < textures.Size(); ++index)
		{
			bReuse = pending.LastComputeTextures[index] == textures[index].Texture &&
				pending.LastComputeSamplers[index] == textures[index].Sampler &&
				pending.LastComputeViews[index] == textures[index].View;
		}
		VkDescriptorSet descriptorSet = pending.LastComputeSet;

		if (!bReuse)
		{
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
					return Result<void>::Failure(Failure("Rhi.VulkanCompute", "Could not allocate compute descriptors."));
				}

				if (!pending.DescriptorPools.Add({ pool, 0, pipeline.AccelStructSlot != 0xFFFFFFFFu }))
				{
					state.F.DestroyDescriptorPool(state.Device, pool, nullptr);

					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track compute descriptors."));
				}
			}
			VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocation.descriptorPool = pending.DescriptorPools.Back().Handle;
			allocation.descriptorSetCount = 1;
			allocation.pSetLayouts = &pipeline.SetLayouts[0];
			descriptorSet = VK_NULL_HANDLE;

			if (state.F.AllocateDescriptorSets(state.Device, &allocation, &descriptorSet) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanCompute", "Could not allocate a compute descriptor set."));
			}
			++pending.DescriptorPools.Back().UsedSets;
			VkDescriptorBufferInfo bufferInfos[8]{};
			VkDescriptorImageInfo imageInfos[8]{};
			VkDescriptorImageInfo samplerInfos[8]{};
			VkWriteDescriptorSet writes[25]{};
			for (usize index = 0; index < buffers.Size(); ++index)
			{
				Buffer& buffer = *static_cast<Buffer*>(buffers[index].Buffer);

				bufferInfos[index] = { buffer.Handle, buffers[index].ByteOffset, buffers[index].ByteCount };
				writes[index] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				writes[index].dstSet = descriptorSet;
				writes[index].dstBinding = buffers[index].Slot;
				writes[index].descriptorCount = 1;
				writes[index].descriptorType = BufferDescriptorType(buffers[index].Type, buffers[index].Format);
				writes[index].pBufferInfo = &bufferInfos[index];

				if (buffers[index].View != nullptr)
				{
					writes[index].pBufferInfo = nullptr;
					writes[index].pTexelBufferView = &static_cast<const BufferView*>(buffers[index].View)->Handle;
				}
			}

			uint32 samplerWrites = 0;

			for (usize index = 0; index < textures.Size(); ++index)
			{
				const Texture& texture = *static_cast<const Texture*>(textures[index].Texture);
				const bool bStorage = textures[index].Type == EComputeTextureBindingType::StorageTexture;
				const TextureViewDesc* const range = textures[index].View != nullptr ? &static_cast<const TextureView*>(textures[index].View)->Descriptor : nullptr;
				imageInfos[index] = { VK_NULL_HANDLE,
									 textures[index].View != nullptr ? static_cast<const TextureView*>(textures[index].View)->Handle
																	 : (bStorage ? texture.GetStorageView() : texture.View),
									 bStorage ? VK_IMAGE_LAYOUT_GENERAL : SampledTextureLayout(pending, texture, range) };
				VkWriteDescriptorSet& write = writes[buffers.Size() + index];
				write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.dstSet = descriptorSet;
				write.dstBinding = (bStorage ? 64u : 32u) + pipeline.TextureSlots[0][index];
				write.descriptorCount = 1;
				write.descriptorType = bStorage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				write.pImageInfo = &imageInfos[index];

				if (!bStorage)
				{
					samplerInfos[samplerWrites] = { textures[index].Sampler != nullptr
													   ? static_cast<const Sampler*>(textures[index].Sampler)->Handle
													   : pipeline.Samplers[0][index],
												   VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
					VkWriteDescriptorSet& samplerWrite = writes[buffers.Size() + textures.Size() + samplerWrites];
					samplerWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					samplerWrite.dstSet = descriptorSet;
					samplerWrite.dstBinding = 48 + pipeline.TextureSlots[0][index];
					samplerWrite.descriptorCount = 1;
					samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
					samplerWrite.pImageInfo = &samplerInfos[samplerWrites];
					++samplerWrites;
				}
			}

			const uint32 writeCount = static_cast<uint32>(buffers.Size() + textures.Size()) + samplerWrites;
			VkAccelerationStructureKHR accelHandle = accelNative != nullptr ? static_cast<NativeAccelStruct*>(accelNative)->Handle : VK_NULL_HANDLE;
			VkWriteDescriptorSetAccelerationStructureKHR accelInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
			if (accelNative != nullptr)
			{
				accelInfo.accelerationStructureCount = 1;
				accelInfo.pAccelerationStructures = &accelHandle;
				VkWriteDescriptorSet& write = writes[writeCount];
				write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.pNext = &accelInfo;
				write.dstSet = descriptorSet;
				write.dstBinding = 80 + pipeline.AccelStructSlot;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			}
			WriteCommandDescriptors(state, pending, writeCount + (accelNative != nullptr ? 1u : 0u), writes, 0, nullptr);
			pending.LastComputePipeline = &pipeline;
			pending.LastComputeAccelStruct = accelNative;
			pending.LastComputeBufferCount = static_cast<uint32>(buffers.Size());
			pending.LastComputeTextureCount = static_cast<uint32>(textures.Size());
			pending.LastComputeSet = descriptorSet;

			for (usize index = 0; index < buffers.Size(); ++index)
			{
				pending.LastComputeBuffers[index] = buffers[index].Buffer;
				pending.LastComputeBufferRanges[index] = { buffers[index].ByteOffset, buffers[index].ByteCount };
			}

			for (usize index = 0; index < textures.Size(); ++index)
			{
				pending.LastComputeTextures[index] = textures[index].Texture;
				pending.LastComputeSamplers[index] = textures[index].Sampler;
				pending.LastComputeViews[index] = textures[index].View;
			}
		}

		for (const NativeComputeTexture& binding : textures)
		{
			Texture& texture = *static_cast<Texture*>(binding.Texture);
			const TextureViewDesc* const range = binding.View != nullptr ? &static_cast<const TextureView*>(binding.View)->Descriptor : nullptr;
			const VkImageLayout layout = binding.Type == EComputeTextureBindingType::StorageTexture ? VK_IMAGE_LAYOUT_GENERAL : SampledTextureLayout(pending, texture, range);

			if (!TextureHasLayout(pending, texture, layout, range))
			{
				if (range != nullptr)
				{
					RequireTextureLayoutRange(state, pending, texture, layout, *range);
				}
				else
				{
					RequireTextureLayout(state, pending, texture, layout);
				}
			}
		}

		if (bindless != nullptr)
		{
			RequireDescriptorTableTextureLayouts(state, pending, *bindless, VK_IMAGE_LAYOUT_GENERAL);
		}

		if (storageTextures != nullptr)
		{
			RequireDescriptorTableTextureLayouts(state, pending, *storageTextures, VK_IMAGE_LAYOUT_GENERAL);
		}

		if (pending.bAutomaticBarriers)
		{
			const NativeBindingSet set{ buffers, textures };
			Result<void> recorded = RecordComputeInputBarriers(
				state, pending, { &set, 1 }, bindless, static_cast<const DescriptorTable*>(storageBufferNative), storageTextures,
				static_cast<const DescriptorTable*>(uniformBufferNative), static_cast<const DescriptorTable*>(readOnlyBufferNative),
				static_cast<Buffer*>(indirectNative), accelNative != nullptr);

			if (!recorded)
			{
				return recorded;
			}
		}
		state.F.CmdBindPipeline(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Handle);
		state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, 0, 1, &descriptorSet, 0, nullptr);

		if (bindless != nullptr)
		{
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, 1, 1, &bindless->Set, 0,
				nullptr);
		}

		if (samplerNative != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(samplerNative)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, 2, 1, &set, 0, nullptr);
		}

		if (storageBufferNative != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(storageBufferNative)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, 3, 1, &set, 0, nullptr);
		}

		if (storageTextureNative != nullptr)
		{
			const VkDescriptorSet set = storageTextures->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, 4, 1, &set, 0, nullptr);
		}

		if (uniformBufferNative != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(uniformBufferNative)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, 5, 1, &set, 0, nullptr);
		}

		if (readOnlyBufferNative != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(readOnlyBufferNative)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, 6, 1, &set, 0, nullptr);
		}

		if (pipeline.PushConstantBytes != 0)
		{
			state.F.CmdPushConstants(pending.Commands, pipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pipeline.PushConstantBytes,
				pushConstants.Data());
		}

		if (indirectNative != nullptr)
		{
			state.F.CmdDispatchIndirect(pending.Commands, static_cast<Buffer*>(indirectNative)->Handle, indirectOffset);
		}
		else
		{
			state.F.CmdDispatch(pending.Commands, groupsX, groupsY, groupsZ);
		}

		if (bWritesStorage && pending.bAutomaticBarriers)
		{
			const NativeBindingSet set{ buffers, textures };
			Result<void> buffersRecorded =
				RecordComputeStorageBufferBarriers(state, pending, { &set, 1 }, static_cast<const DescriptorTable*>(storageBufferNative));
			if (!buffersRecorded)
			{
				return buffersRecorded;
			}

			Result<void> texturesRecorded = RecordComputeStorageTextureBarriers(state, pending, { &set, 1 }, storageTextures);
			if (!texturesRecorded)
			{
				return texturesRecorded;
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordComputeDispatchMultiple(
		State* opaque,
		void* native,
		void* pipelineNative,
		Span<const NativeBindingSet> sets,
		uint32 groupsX,
		uint32 groupsY,
		uint32 groupsZ,
		void* indirectNative,
		uint64 indirectOffset,
		Span<const Byte> pushConstants,
		void* sampledTable,
		void* samplerTable,
		void* storageBufferTable,
		void* storageTextureTable,
		void* uniformBufferTable,
		void* readOnlyBufferNative,
		void* accelNative)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		ComputePipeline& pipeline = *static_cast<ComputePipeline*>(pipelineNative);
		if (pipeline.Fixed.LayoutCount != 0)
		{
			return RecordRangeComputeDispatch(state, pending, pipeline, sets, groupsX, groupsY, groupsZ, indirectNative, indirectOffset,
				pushConstants);
		}
		ASSERT(sets.Size() == pipeline.SetLayoutCount && sets.Size() <= MAXIMUM_FIXED_BINDING_LAYOUTS, "The compute binding sets must match the pipeline layouts.");
		const uint32* limits = state.DeviceProperties.limits.maxComputeWorkGroupCount;

		ASSERT(indirectNative != nullptr || (groupsX <= limits[0] && groupsY <= limits[1] && groupsZ <= limits[2]), "The group count exceeds Vulkan device limits.");
		bool bWritesStorageTexture = storageTextureTable != nullptr;
		bool bWritesStorage = storageBufferTable != nullptr || bWritesStorageTexture;

		for (const NativeBindingSet& set : sets)
		{
			for (const NativeComputeBuffer& binding : set.Buffers)
			{
				bWritesStorage |= binding.Type == EComputeBindingType::StorageBuffer;
			}

			for (const NativeComputeTexture& binding : set.Textures)
			{
				bWritesStorageTexture |= binding.Type == EComputeTextureBindingType::StorageTexture;
			}
		}
		bWritesStorage |= bWritesStorageTexture;
		uint32 bufferCount = 0;
		uint32 textureCount = 0;

		for (const NativeBindingSet& set : sets)
		{
			textureCount += static_cast<uint32>(set.Textures.Size());

			for (const NativeComputeBuffer& binding : set.Buffers)
			{
				const bool bUniform = binding.Type == EComputeBindingType::UniformBuffer;
				if (binding.View == nullptr &&
					(binding.ByteCount > (bUniform ? state.DeviceProperties.limits.maxUniformBufferRange : state.DeviceProperties.limits.maxStorageBufferRange) ||
							binding.ByteOffset % (bUniform ? state.DeviceProperties.limits.minUniformBufferOffsetAlignment : state.DeviceProperties.limits.minStorageBufferOffsetAlignment) != 0))
				{
					return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ComputeBinding", "The buffer exceeds the Vulkan descriptor range.");
				}
				++bufferCount;
			}
		}
		bool bReuse = pending.LastComputePipeline == &pipeline &&
			pending.LastComputeBufferCount == bufferCount &&
			pending.LastComputeTextureCount == textureCount &&
			pending.LastComputeAccelStruct == accelNative;
		uint32 flatIndex = 0;

		for (const NativeBindingSet& set : sets)
		{
			for (const NativeComputeBuffer& binding : set.Buffers)
			{
				if (bReuse &&
					(pending.LastComputeBuffers[flatIndex] != binding.Buffer ||
					 pending.LastComputeBufferRanges[flatIndex].ByteOffset != binding.ByteOffset ||
					 pending.LastComputeBufferRanges[flatIndex].ByteCount != binding.ByteCount))
				{
					bReuse = false;
				}
				++flatIndex;
			}
		}
		flatIndex = 0;

		for (const NativeBindingSet& set : sets)
		{
			for (const NativeComputeTexture& binding : set.Textures)
			{
				if (bReuse &&
					(pending.LastComputeTextures[flatIndex] != binding.Texture || pending.LastComputeSamplers[flatIndex] != binding.Sampler || pending.LastComputeViews[flatIndex] != binding.View))
				{
					bReuse = false;
				}
				++flatIndex;
			}
		}
		VkDescriptorSet nativeSets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		if (bReuse)
		{
			for (uint32 index = 0; index < sets.Size(); ++index)
			{
				nativeSets[index] = pending.LastComputeSets[index];
			}
		}
		else
		{
			constexpr uint32 SETS_PER_POOL = 32;

			if (pending.DescriptorPools.IsEmpty() ||
				pending.DescriptorPools.Back().UsedSets + sets.Size() > SETS_PER_POOL ||
				(pipeline.AccelStructSlot != 0xFFFFFFFFu && !pending.DescriptorPools.Back().bAccelerationStructures))
			{
				VkDescriptorPoolSize sizes[8]{ {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_SAMPLER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 8 * SETS_PER_POOL},
											  {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, SETS_PER_POOL} };
				VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
				info.maxSets = SETS_PER_POOL;
				info.poolSizeCount = pipeline.AccelStructSlot != 0xFFFFFFFFu ? 8 : 7;
				info.pPoolSizes = sizes;
				VkDescriptorPool pool = VK_NULL_HANDLE;

				if (state.F.CreateDescriptorPool(state.Device, &info, nullptr, &pool) != VK_SUCCESS)
				{
					return Result<void>::Failure(Failure("Rhi.VulkanCompute", "Could not allocate multi-layout compute descriptors."));
				}

				if (!pending.DescriptorPools.Add({ pool, 0, pipeline.AccelStructSlot != 0xFFFFFFFFu }))
				{
					state.F.DestroyDescriptorPool(state.Device, pool, nullptr);

					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track multi-layout compute descriptors."));
				}
			}
			VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocation.descriptorPool = pending.DescriptorPools.Back().Handle;
			allocation.descriptorSetCount = static_cast<uint32>(sets.Size());
			allocation.pSetLayouts = pipeline.SetLayouts;

			if (state.F.AllocateDescriptorSets(state.Device, &allocation, nativeSets) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanCompute", "Could not allocate multi-layout compute descriptor sets."));
			}
			pending.DescriptorPools.Back().UsedSets += static_cast<uint32>(sets.Size());
			VkDescriptorBufferInfo infos[32]{};
			VkDescriptorImageInfo imageInfos[32]{};
			VkDescriptorImageInfo samplerInfos[32]{};
			VkWriteDescriptorSet writes[97]{};
			flatIndex = 0;
			uint32 writeCount = 0;

			for (uint32 layoutIndex = 0; layoutIndex < sets.Size(); ++layoutIndex)
			{
				for (const NativeComputeBuffer& binding : sets[layoutIndex].Buffers)
				{
					infos[flatIndex] = { static_cast<Buffer*>(binding.Buffer)->Handle, binding.ByteOffset, binding.ByteCount };
					VkWriteDescriptorSet& write = writes[writeCount++];
					write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					write.dstSet = nativeSets[layoutIndex];
					write.dstBinding = binding.Slot;
					write.descriptorCount = 1;
					write.descriptorType = BufferDescriptorType(binding.Type, binding.Format);
					write.pBufferInfo = &infos[flatIndex];

					if (binding.View != nullptr)
					{
						write.pBufferInfo = nullptr;
						write.pTexelBufferView = &static_cast<const BufferView*>(binding.View)->Handle;
					}
					pending.LastComputeBuffers[flatIndex] = binding.Buffer;
					pending.LastComputeBufferRanges[flatIndex] = { binding.ByteOffset, binding.ByteCount };
					++flatIndex;
				}
			}
			flatIndex = 0;
			uint32 samplerIndex = 0;

			for (uint32 layoutIndex = 0; layoutIndex < sets.Size(); ++layoutIndex)
			{
				uint32 textureIndex = 0;

				for (const NativeComputeTexture& binding : sets[layoutIndex].Textures)
				{
					const Texture& texture = *static_cast<const Texture*>(binding.Texture);
					const bool bStorage = binding.Type == EComputeTextureBindingType::StorageTexture;
					const TextureViewDesc* const range = binding.View != nullptr ? &static_cast<const TextureView*>(binding.View)->Descriptor : nullptr;
					imageInfos[flatIndex] = { VK_NULL_HANDLE,
											 binding.View != nullptr ? static_cast<const TextureView*>(binding.View)->Handle
																	 : (bStorage ? texture.GetStorageView() : texture.View),
											 bStorage ? VK_IMAGE_LAYOUT_GENERAL : SampledTextureLayout(pending, texture, range) };
					VkWriteDescriptorSet& write = writes[writeCount++];
					write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					write.dstSet = nativeSets[layoutIndex];
					write.dstBinding = (bStorage ? 64u : 32u) + pipeline.TextureSlots[layoutIndex][textureIndex];
					write.descriptorCount = 1;
					write.descriptorType = bStorage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					write.pImageInfo = &imageInfos[flatIndex];

					if (!bStorage)
					{
						samplerInfos[samplerIndex] = { binding.Sampler != nullptr ? static_cast<const Sampler*>(binding.Sampler)->Handle
																				 : pipeline.Samplers[layoutIndex][textureIndex],
													  VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
						VkWriteDescriptorSet& samplerWrite = writes[writeCount++];
						samplerWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
						samplerWrite.dstSet = nativeSets[layoutIndex];
						samplerWrite.dstBinding = 48 + pipeline.TextureSlots[layoutIndex][textureIndex];
						samplerWrite.descriptorCount = 1;
						samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
						samplerWrite.pImageInfo = &samplerInfos[samplerIndex++];
					}
					pending.LastComputeTextures[flatIndex] = binding.Texture;
					pending.LastComputeSamplers[flatIndex] = binding.Sampler;
					pending.LastComputeViews[flatIndex] = binding.View;
					++flatIndex;
					++textureIndex;
				}
			}
			VkAccelerationStructureKHR accelHandle = accelNative != nullptr ? static_cast<NativeAccelStruct*>(accelNative)->Handle : VK_NULL_HANDLE;
			VkWriteDescriptorSetAccelerationStructureKHR accelInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
			if (accelNative != nullptr)
			{
				accelInfo.accelerationStructureCount = 1;
				accelInfo.pAccelerationStructures = &accelHandle;
				VkWriteDescriptorSet& write = writes[writeCount++];
				write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.pNext = &accelInfo;
				write.dstSet = nativeSets[0];
				write.dstBinding = 80 + pipeline.AccelStructSlot;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			}
			WriteCommandDescriptors(state, pending, writeCount, writes, 0, nullptr);
			pending.LastComputePipeline = &pipeline;
			pending.LastComputeBufferCount = bufferCount;
			pending.LastComputeTextureCount = textureCount;
			pending.LastComputeAccelStruct = accelNative;

			for (uint32 index = 0; index < sets.Size(); ++index)
			{
				pending.LastComputeSets[index] = nativeSets[index];
			}
		}

		for (const NativeBindingSet& set : sets)
		{
			for (const NativeComputeTexture& binding : set.Textures)
			{
				Texture& texture = *static_cast<Texture*>(binding.Texture);
				const TextureViewDesc* const range = binding.View != nullptr ? &static_cast<const TextureView*>(binding.View)->Descriptor : nullptr;
				const VkImageLayout layout = binding.Type == EComputeTextureBindingType::StorageTexture ? VK_IMAGE_LAYOUT_GENERAL : SampledTextureLayout(pending, texture, range);

				if (!TextureHasLayout(pending, texture, layout, range))
				{
					if (range != nullptr)
					{
						RequireTextureLayoutRange(state, pending, texture, layout, *range);
					}
					else
					{
						RequireTextureLayout(state, pending, texture, layout);
					}
				}
			}
		}

		const DescriptorTable* const sampled = static_cast<const DescriptorTable*>(sampledTable);
		const DescriptorTable* const storageTextures = static_cast<const DescriptorTable*>(storageTextureTable);
		const DescriptorTable* const imageTables[2]{ sampled, storageTextures };
		for (const DescriptorTable* table : imageTables)
		{
			if (table == nullptr)
			{
				continue;
			}
			RequireDescriptorTableTextureLayouts(state, pending, *table, VK_IMAGE_LAYOUT_GENERAL);
		}

		if (pending.bAutomaticBarriers)
		{
			Result<void> recorded = RecordComputeInputBarriers(
				state, pending, sets, sampled, static_cast<const DescriptorTable*>(storageBufferTable), storageTextures,
				static_cast<const DescriptorTable*>(uniformBufferTable), static_cast<const DescriptorTable*>(readOnlyBufferNative),
				static_cast<Buffer*>(indirectNative), accelNative != nullptr);

			if (!recorded)
			{
				return recorded;
			}
		}
		state.F.CmdBindPipeline(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Handle);
		state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, 0,
			static_cast<uint32>(sets.Size()), nativeSets, 0, nullptr);
		const uint32 tableBase = static_cast<uint32>(sets.Size());

		if (sampled != nullptr)
		{
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, tableBase, 1, &sampled->Set, 0,
				nullptr);
		}

		if (samplerTable != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(samplerTable)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, tableBase + 1, 1, &set, 0,
				nullptr);
		}

		if (storageBufferTable != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(storageBufferTable)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, tableBase + 2, 1, &set, 0,
				nullptr);
		}

		if (storageTextures != nullptr)
		{
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, tableBase + 3, 1,
				&storageTextures->Set, 0, nullptr);
		}

		if (uniformBufferTable != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(uniformBufferTable)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, tableBase + 4, 1, &set, 0,
				nullptr);
		}

		if (readOnlyBufferNative != nullptr)
		{
			const VkDescriptorSet set = static_cast<const DescriptorTable*>(readOnlyBufferNative)->Set;
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout, tableBase + 5, 1, &set, 0,
				nullptr);
		}

		if (pipeline.PushConstantBytes != 0)
		{
			state.F.CmdPushConstants(pending.Commands, pipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pipeline.PushConstantBytes,
				pushConstants.Data());
		}

		if (indirectNative != nullptr)
		{
			state.F.CmdDispatchIndirect(pending.Commands, static_cast<Buffer*>(indirectNative)->Handle, indirectOffset);
		}
		else
		{
			state.F.CmdDispatch(pending.Commands, groupsX, groupsY, groupsZ);
		}

		if (bWritesStorage && pending.bAutomaticBarriers)
		{
			Result<void> buffersRecorded = RecordComputeStorageBufferBarriers(state, pending, sets, static_cast<const DescriptorTable*>(storageBufferTable));

			if (!buffersRecorded)
			{
				return buffersRecorded;
			}

			Result<void> texturesRecorded = RecordComputeStorageTextureBarriers(state, pending, sets, storageTextures);

			if (!texturesRecorded)
			{
				return texturesRecorded;
			}
		}

		return Result<void>::Success();
	}
}
