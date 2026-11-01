#include "VulkanBackend.h"
#include "VulkanDescriptors.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] VkDescriptorType RangeDescriptorType(const BindingRangeDesc& range) noexcept
	{
		if (range.Type == EDescriptorType::AccelerationStructure)
		{
			return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		}
		if (range.Type == EDescriptorType::Sampler)
		{
			return VK_DESCRIPTOR_TYPE_SAMPLER;
		}
		if (range.Type == EDescriptorType::SampledTexture)
		{
			return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		}
		if (range.Type == EDescriptorType::StorageTexture)
		{
			return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		}

		return BufferDescriptorType(range.Type == EDescriptorType::UniformBuffer ? EComputeBindingType::UniformBuffer
			: range.Type == EDescriptorType::ReadOnlyBuffer ? EComputeBindingType::ReadOnlyBuffer
			: EComputeBindingType::StorageBuffer,
			range.BufferFormat);
	}

	[[nodiscard]] Result<void> WriteFixedBindingSets(
		State* opaque,
		void* native,
		void* pipelineNative,
		bool bGraphics,
		Span<const NativeFixedBindingSet> sets,
		Pending::PreparedBinding* prepared,
		Span<const NativePreparedTextureAccess> intents,
		RayTracingPipeline* rayPipeline)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		const NativeFixedLayoutState& fixed = rayPipeline != nullptr ? rayPipeline->Fixed
			: bGraphics ? static_cast<GraphicsPipeline*>(pipelineNative)->Fixed
			: static_cast<ComputePipeline*>(pipelineNative)->Fixed;
		const VkDescriptorSetLayout* layouts = rayPipeline != nullptr ? rayPipeline->SetLayouts
			: bGraphics ? static_cast<GraphicsPipeline*>(pipelineNative)->SetLayouts
			: static_cast<ComputePipeline*>(pipelineNative)->SetLayouts;
		const VkPipelineLayout pipelineLayout = rayPipeline != nullptr ? rayPipeline->Layout
			: bGraphics ? static_cast<GraphicsPipeline*>(pipelineNative)->Layout
			: static_cast<ComputePipeline*>(pipelineNative)->Layout;
		ASSERT(sets.Size() == fixed.LayoutCount && fixed.LayoutCount <= MAXIMUM_FIXED_BINDING_LAYOUTS, "Fixed sets match the immutable native pipeline.");
		if (!sets.IsEmpty() && sets[0].Cache)
		{
			return WriteCachedFixedBindingSets(state, pending, fixed, pipelineLayout, bGraphics, rayPipeline != nullptr, sets, prepared,
				intents);
		}
		constexpr uint32 SETS_PER_POOL = 32;
		uint32 descriptorCount = 0;
		uint32 typeCounts[8]{};
		bool bExpanded = false;
		for (uint32 layout = 0; layout < fixed.LayoutCount; ++layout)
		{
			for (uint32 index = 0; index < fixed.RangeCounts[layout]; ++index)
			{
				const BindingRangeDesc& range = fixed.Ranges[layout][index];
				const VkDescriptorType type = RangeDescriptorType(range);
				const uint32 at = type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ? 0u
					: type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ? 1u
					: type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ? 2u
					: type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ? 3u
					: type == VK_DESCRIPTOR_TYPE_SAMPLER ? 4u
					: type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ? 5u
					: type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ? 6u
					: 7u;
				typeCounts[at] += range.DescriptorCount;
				descriptorCount += range.DescriptorCount;
				bExpanded |= range.Source == EBindingRangeSource::DescriptorTable;
			}
		}
		if (bExpanded ||
			rayPipeline != nullptr ||
			pending.DescriptorPools.IsEmpty() ||
			pending.DescriptorPools.Back().bDedicated ||
			pending.DescriptorPools.Back().UsedSets + sets.Size() > SETS_PER_POOL)
		{
			const bool bAccelerationStructures = typeCounts[7] != 0;
			VkDescriptorPoolSize sizes[8]{};
			const VkDescriptorType types[8]{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
											VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
											VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
											VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
											VK_DESCRIPTOR_TYPE_SAMPLER,
											VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
											VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
											VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR };
			uint32 sizeCount = 0;
			for (uint32 index = 0; index < 8; ++index)
			{
				if (bExpanded && typeCounts[index] == 0)
				{
					continue;
				}
				if (!bExpanded && index == 7 && !bAccelerationStructures)
				{
					continue;
				}
				sizes[sizeCount++] = { types[index], bExpanded ? typeCounts[index] : 8 * SETS_PER_POOL };
			}
			VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			info.maxSets = bExpanded ? static_cast<uint32>(sets.Size()) : SETS_PER_POOL;
			info.poolSizeCount = sizeCount;
			info.pPoolSizes = sizes;
			VkDescriptorPool pool = VK_NULL_HANDLE;
			if (state.F.CreateDescriptorPool(state.Device, &info, nullptr, &pool) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.BindingRange", "Could not allocate native range descriptors."));
			}
			if (!pending.DescriptorPools.Add({ pool, 0, bAccelerationStructures, bExpanded }))
			{
				state.F.DestroyDescriptorPool(state.Device, pool, nullptr);

				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain native range descriptors."));
			}
		}
		VkDescriptorSetLayout requested[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		for (uint32 index = 0; index < sets.Size(); ++index)
		{
			requested[index] = layouts[fixed.Spaces[index]];
		}
		VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocation.descriptorPool = pending.DescriptorPools.Back().Handle;
		allocation.descriptorSetCount = static_cast<uint32>(sets.Size());
		allocation.pSetLayouts = requested;
		VkDescriptorSet nativeSets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		if (state.F.AllocateDescriptorSets(state.Device, &allocation, nativeSets) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.BindingRange", "Could not allocate native range sets."));
		}
		pending.DescriptorPools.Back().UsedSets += static_cast<uint32>(sets.Size());
		VkWriteDescriptorSet writes[MAXIMUM_FIXED_BINDING_LAYOUTS * 8]{};
		VkDescriptorBufferInfo fixedBuffers[128]{};
		VkDescriptorImageInfo fixedImages[128]{};
		VkBufferView fixedViews[128]{};
		VkAccelerationStructureKHR fixedAccels[128]{};
		Array<VkDescriptorBufferInfo> ownedBuffers(state.Resource);
		Array<VkDescriptorImageInfo> ownedImages(state.Resource);
		Array<VkBufferView> ownedViews(state.Resource);
		Array<VkAccelerationStructureKHR> ownedAccels(state.Resource);
		if (descriptorCount > 128 && (!ownedBuffers.Resize(descriptorCount) || !ownedImages.Resize(descriptorCount) || !ownedViews.Resize(descriptorCount) || !ownedAccels.Resize(descriptorCount)))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not capture native table descriptors."));
		}
		VkDescriptorBufferInfo* buffers = descriptorCount > 128 ? ownedBuffers.Data() : fixedBuffers;
		VkDescriptorImageInfo* images = descriptorCount > 128 ? ownedImages.Data() : fixedImages;
		VkBufferView_T** views = descriptorCount > 128 ? ownedViews.Data() : fixedViews;
		VkAccelerationStructureKHR_T** accels = descriptorCount > 128 ? ownedAccels.Data() : fixedAccels;
		VkWriteDescriptorSetAccelerationStructureKHR accelWrites[MAXIMUM_FIXED_BINDING_LAYOUTS * 8]{};
		uint32 writeCount = 0;
		uint32 infoCount = 0;
		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			uint32 descriptor = 0;
			for (uint32 index = 0; index < fixed.RangeCounts[layout]; ++index)
			{
				const BindingRangeDesc& range = fixed.Ranges[layout][index];
				VkWriteDescriptorSet& write = writes[writeCount++];
				write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.dstSet = nativeSets[layout];
				write.dstBinding = range.Binding;
				write.descriptorCount = range.DescriptorCount;
				write.descriptorType = RangeDescriptorType(range);
				if (range.Type == EDescriptorType::AccelerationStructure)
				{
					VkWriteDescriptorSetAccelerationStructureKHR& acceleration = accelWrites[writeCount - 1];
					acceleration = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
					acceleration.accelerationStructureCount = range.DescriptorCount;
					acceleration.pAccelerationStructures = &accels[infoCount];
					write.pNext = &acceleration;
				}
				else if (range.Type == EDescriptorType::Sampler || range.Type == EDescriptorType::SampledTexture || range.Type == EDescriptorType::StorageTexture)
				{
					write.pImageInfo = &images[infoCount];
				}
				else if (range.BufferFormat != EFormat::UNKNOWN)
				{
					write.pTexelBufferView = &views[infoCount];
				}
				else
				{
					write.pBufferInfo = &buffers[infoCount];
				}
				for (uint32 element = 0; element < range.DescriptorCount; ++element, ++infoCount)
				{
					ASSERT(descriptor < sets[layout].Descriptors.Size(), "Every range element has a captured native resource.");
					const rhi::NativeFixedDescriptor& value = sets[layout].Descriptors[descriptor++];
					if (range.Type == EDescriptorType::Sampler)
					{
						images[infoCount].sampler = static_cast<const Sampler*>(value.Resource)->Handle;
					}
					else if (range.Type == EDescriptorType::AccelerationStructure)
					{
						accels[infoCount] = static_cast<const NativeAccelStruct*>(value.Resource)->Handle;
					}
					else if (range.Type == EDescriptorType::SampledTexture || range.Type == EDescriptorType::StorageTexture)
					{
						Texture& texture = *static_cast<Texture*>(value.Resource);
						const TextureView* view = static_cast<const TextureView*>(value.View);
						const TextureViewDesc* subrange = view != nullptr ? &view->Descriptor : nullptr;
						const ETextureAspect descriptorAspect = subrange != nullptr && subrange->Aspect != ETextureAspect::Default
							? subrange->Aspect
							: texture.bDepth ? ETextureAspect::Depth
							: ETextureAspect::Color;
						VkImageLayout imageLayout = range.Type == EDescriptorType::StorageTexture ? VK_IMAGE_LAYOUT_GENERAL : SampledTextureLayout(pending, texture, subrange);
						if (prepared != nullptr)
						{
							for (const rhi::NativePreparedTextureAccess& intent : intents)
							{
								if (intent.Resource == &texture &&
									(intent.Intent.Aspect == ETextureAspect::Default || intent.Intent.Aspect == descriptorAspect) &&
									(subrange == nullptr ||
									 (subrange->BaseMipLevel >= intent.Intent.BaseMipLevel &&
									  subrange->BaseArrayLayer >= intent.Intent.BaseArrayLayer &&
									  uint64(subrange->BaseMipLevel) + subrange->MipLevelCount <= uint64(intent.Intent.BaseMipLevel) + intent.Intent.MipLevelCount &&
									  uint64(subrange->BaseArrayLayer) + subrange->ArrayLayerCount <= uint64(intent.Intent.BaseArrayLayer) + intent.Intent.ArrayLayerCount)))
								{
									imageLayout = PreparedTextureLayout(texture, intents, intent.Intent);
									break;
								}
							}
						}
						if (value.bUsed && !SupportsAttachmentSamplingLayout(state, imageLayout))
						{
							return Result<void>::Failure(
								EErrorCode::Unsupported,
								"Rhi.AttachmentSampling",
								"Read-only attachment sampling requires load/store NONE; mixed planes also require " "separate depth/stencil attachment access.");
						}
						if (prepared == nullptr && value.bUsed && bGraphics)
						{
							ASSERT(TextureHasLayout(pending, texture, imageLayout, subrange), "Prepare fixed textures before rendering.");
						}
						else
							if (prepared == nullptr && value.bUsed && !TextureHasLayout(pending, texture, imageLayout, subrange))
							{
								if (subrange != nullptr)
								{
									RequireTextureLayoutRange(state, pending, texture, imageLayout, *subrange);
								}
								else
								{
									RequireTextureLayout(state, pending, texture, imageLayout);
								}
							}
						images[infoCount] = { VK_NULL_HANDLE,
											 view != nullptr ? view->Handle
											 : range.Type == EDescriptorType::StorageTexture ? texture.GetStorageView()
																							 : texture.View,
											 imageLayout };
					}
					else if (range.BufferFormat != EFormat::UNKNOWN)
					{
						views[infoCount] = static_cast<const BufferView*>(value.View)->Handle;
					}
					else
					{
						const bool bUniform = range.Type == EDescriptorType::UniformBuffer;
						if (value.ByteCount > (bUniform ? state.DeviceProperties.limits.maxUniformBufferRange : state.DeviceProperties.limits.maxStorageBufferRange) ||
							value.ByteOffset % (bUniform ? state.DeviceProperties.limits.minUniformBufferOffsetAlignment : state.DeviceProperties.limits.minStorageBufferOffsetAlignment) != 0)
						{
							return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BindingRange", "The buffer range exceeds native limits.");
						}
						buffers[infoCount] = { static_cast<const Buffer*>(value.Resource)->Handle, value.ByteOffset, value.ByteCount };
					}
				}
			}
			ASSERT(descriptor == sets[layout].Descriptors.Size(), "The captured descriptor count matches native ranges.");
		}
		if (prepared == nullptr && bGraphics)
		{
			SynchronizeGraphicsStorage(state, pending);
		}
		WriteCommandDescriptors(state, pending, writeCount, writes, 0, nullptr);
		if (prepared != nullptr)
		{
			prepared->Count = static_cast<uint32>(sets.Size());
			for (uint32 index = 0; index < sets.Size(); ++index)
			{
				prepared->Sets[index] = nativeSets[index];
			}
		}
		else
		{
			for (uint32 index = 0; index < sets.Size(); ++index)
			{
				state.F.CmdBindDescriptorSets(pending.Commands,
					bGraphics ? VK_PIPELINE_BIND_POINT_GRAPHICS :
					(rayPipeline != nullptr ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR : VK_PIPELINE_BIND_POINT_COMPUTE), pipelineLayout,
					fixed.Spaces[index], 1, &nativeSets[index], 0, nullptr);
			}
		}
		// Scalar descriptor caches may describe earlier bindings on this command list.
		pending.LastComputePipeline = nullptr;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordFixedBindingSets(State* opaque, void* native, void* pipelineNative, bool bGraphics, Span<const NativeFixedBindingSet> sets)
	{
		Result<void> recorded = WriteFixedBindingSets(opaque, native, pipelineNative, bGraphics, sets);
		if (!recorded || !bGraphics)
		{
			return recorded;
		}
		Pending& pending = *static_cast<Pending*>(native);
		pending.RangeStorageTableCount = 0;
		for (const NativeFixedBindingSet& set : sets)
		{
			for (uint32 index = 0; index < set.TableCount; ++index)
			{
				const DescriptorTable* table = static_cast<const DescriptorTable*>(set.Tables[index].Table);
				if (table->Type == EDescriptorType::StorageBuffer || table->Type == EDescriptorType::StorageTexture)
				{
					ASSERT(pending.RangeStorageTableCount < MAXIMUM_FIXED_BINDING_LAYOUTS * MAXIMUM_BINDING_RANGES_PER_LAYOUT, "Graphics table ranges fit the native binding limit.");
					pending.RangeStorageTables[pending.RangeStorageTableCount++] = set.Tables[index];
				}
			}
		}
		if (pending.RangeStorageTableCount != 0 && pending.bAutomaticBarriers && pending.bRangeStorageBarrierPending)
		{
			State& state = *opaque;
			state.F.CmdEndRenderPass(pending.Commands);
			Result<void> barrier = RecordShaderMemoryBarrier(opaque, native);
			state.F.CmdBeginRenderPass(pending.Commands, &pending.ResumeRendering, VK_SUBPASS_CONTENTS_INLINE);
			if (!barrier)
			{
				return barrier;
			}
			pending.bRangeStorageBarrierPending = false;
		}
		return recorded;
	}
}
