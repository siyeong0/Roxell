#include "VulkanBackend.h"
#include "VulkanDescriptors.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] const NativeTableRangeBinding* FindNativeTableRange(const NativeFixedBindingSet& set, uint32 rangeIndex) noexcept
	{
		for (const rhi::NativeTableRangeBinding& table : Span{ set.Tables, set.TableCount })
		{
			if (table.RangeIndex == rangeIndex)
			{
				return &table;
			}
		}

		return nullptr;
	}

	[[nodiscard]] VkImageLayout FixedDescriptorImageLayout(
		Pending& pending,
		Texture& texture,
		TextureView* view,
		EDescriptorType type,
		Span<const NativePreparedTextureAccess> intents,
		bool bPrepared) noexcept
	{
		const TextureViewDesc* extent = view != nullptr ? &view->Descriptor : nullptr;
		if (type == EDescriptorType::StorageTexture)
		{
			return VK_IMAGE_LAYOUT_GENERAL;
		}
		if (!bPrepared)
		{
			return SampledTextureLayout(pending, texture, extent);
		}

		// Unused table images retain their immutable GENERAL descriptor and are never tracked here.
		const ETextureAspect aspect = extent != nullptr && extent->Aspect != ETextureAspect::Default ? extent->Aspect : texture.bDepth ? ETextureAspect::Depth : ETextureAspect::Color;
		const uint32 baseMip = extent != nullptr ? extent->BaseMipLevel : 0;
		const uint32 mipCount = extent != nullptr ? extent->MipLevelCount : texture.MipLevels;
		const uint32 baseLayer = extent != nullptr ? extent->BaseArrayLayer : 0;
		const uint32 layerCount = extent != nullptr ? extent->ArrayLayerCount : texture.ArrayLayers;
		for (const rhi::NativePreparedTextureAccess& access : intents)
		{
			const PreparedTextureAccessDesc& intent = access.Intent;
			if (access.Resource == &texture &&
				(intent.Aspect == ETextureAspect::Default || intent.Aspect == aspect) &&
				baseMip >= intent.BaseMipLevel &&
				baseLayer >= intent.BaseArrayLayer &&
				uint64(baseMip) + mipCount <= uint64(intent.BaseMipLevel) + intent.MipLevelCount &&
				uint64(baseLayer) + layerCount <= uint64(intent.BaseArrayLayer) + intent.ArrayLayerCount)
			{
				return PreparedTextureLayout(texture, intents, intent);
			}
		}
		ASSERT(false, "A used prepared image has an exact declared phase layout.");

		return VK_IMAGE_LAYOUT_GENERAL;
	}

	[[nodiscard]] VkShaderStageFlags FixedDescriptorStages(const BindingRangeDesc& range, bool bGraphics, bool bRay) noexcept
	{
		const uint32 visibility = static_cast<uint32>(range.Visibility);
		if (bRay)
		{
			return (visibility & 32u ? VK_SHADER_STAGE_RAYGEN_BIT_KHR : 0) | (visibility & 64u ? VK_SHADER_STAGE_MISS_BIT_KHR : 0) |
				(visibility & 128u ? VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR : 0) |
				(visibility & 256u ? VK_SHADER_STAGE_ANY_HIT_BIT_KHR : 0) |
				(visibility & 512u ? VK_SHADER_STAGE_INTERSECTION_BIT_KHR : 0) |
				(visibility & 1024u ? VK_SHADER_STAGE_CALLABLE_BIT_KHR : 0);
		}
		if (bGraphics)
		{
			return (visibility & 2u ? VK_SHADER_STAGE_VERTEX_BIT : 0) | (visibility & 4u ? VK_SHADER_STAGE_FRAGMENT_BIT : 0) |
				(visibility & 8u ? VK_SHADER_STAGE_MESH_BIT_EXT : 0) | (visibility & 16u ? VK_SHADER_STAGE_TASK_BIT_EXT : 0) |
				(visibility & 2048u ? VK_SHADER_STAGE_GEOMETRY_BIT : 0) |
				(visibility & 4096u ? VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT : 0) |
				(visibility & 8192u ? VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT : 0);
		}

		return VK_SHADER_STAGE_COMPUTE_BIT;
	}

	void DestroyDescriptorSetRealization(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DescriptorSetRealization* realization = static_cast<DescriptorSetRealization*>(native);
		if (realization->Pool != VK_NULL_HANDLE)
		{
			state.F.DestroyDescriptorPool(state.Device, realization->Pool, nullptr);
		}
		if (realization->Layout != VK_NULL_HANDLE)
		{
			state.F.DestroyDescriptorSetLayout(state.Device, realization->Layout, nullptr);
		}
		DestroyObject(state.Resource, realization);
	}

	[[nodiscard]] bool DescriptorSetMatches(
		const DescriptorSetRealization& realization,
		const NativeFixedLayoutState& fixed,
		uint32 layout,
		const NativeFixedBindingSet& set,
		Pending& pending,
		bool bGraphics,
		bool bRay,
		Span<const NativePreparedTextureAccess> intents,
		bool bPrepared) noexcept
	{
		if (realization.RangeCount != fixed.RangeCounts[layout] ||
			realization.Space != fixed.Spaces[layout] ||
			realization.bGraphics != bGraphics ||
			realization.bRay != bRay ||
			realization.FixedCount != set.Descriptors.Size())
		{
			return false;
		}
		for (uint32 index = 0; index < realization.RangeCount; ++index)
		{
			const BindingRangeDesc& left = realization.Ranges[index];
			const BindingRangeDesc& right = fixed.Ranges[layout][index];
			if (left.Type != right.Type ||
				left.Binding != right.Binding ||
				left.DescriptorCount != right.DescriptorCount ||
				left.BufferFormat != right.BufferFormat ||
				left.Visibility != right.Visibility ||
				left.StructureStrideBytes != right.StructureStrideBytes ||
				left.Source != right.Source)
			{
				return false;
			}
			const NativeTableRangeBinding* table = FindNativeTableRange(set, index);
			if (realization.Tables[index] != (table != nullptr ? table->Table : nullptr))
			{
				return false;
			}
		}
		for (uint32 index = 0; index < realization.FixedCount; ++index)
		{
			const NativeFixedDescriptor& left = realization.Fixed[index];
			const rhi::NativeFixedDescriptor& right = set.Descriptors[index];
			if (left.Resource != right.Resource || left.View != right.View || left.ByteOffset != right.ByteOffset || left.ByteCount != right.ByteCount || left.bUsed != right.bUsed)
			{
				return false;
			}
		}
		for (vulkan::DescriptorSetRealization::Image const& image : realization.Images)
		{
			if (image.Layout != FixedDescriptorImageLayout(pending, *image.Resource, image.View, realization.Ranges[image.RangeIndex].Type,
				intents, bPrepared))
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] Result<void> InitializeDescriptorSetStorage(
		State& state,
		DescriptorSetRealization& realization,
		const NativeFixedLayoutState& fixed,
		uint32 layout,
		const NativeFixedBindingSet& set,
		bool bGraphics,
		bool bRay)
	{
		ASSERT(set.TableCount <= MAXIMUM_BINDING_RANGES_PER_LAYOUT, "An immutable set has bounded native table ranges.");
		realization.RangeCount = fixed.RangeCounts[layout];
		realization.Space = fixed.Spaces[layout];
		realization.bGraphics = bGraphics;
		realization.bRay = bRay;
		realization.FixedCount = static_cast<uint32>(set.Descriptors.Size());
		ASSERT(realization.RangeCount <= MAXIMUM_BINDING_RANGES_PER_LAYOUT && realization.FixedCount <= 32, "An immutable set has bounded fixed ranges and tuples.");
		for (uint32 index = 0; index < realization.FixedCount; ++index)
		{
			realization.Fixed[index] = set.Descriptors[index];
		}

		VkDescriptorSetLayoutBinding bindings[MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
		VkDescriptorPoolSize sizes[MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
		uint32 imageCount = 0;
		for (uint32 index = 0; index < realization.RangeCount; ++index)
		{
			const BindingRangeDesc& range = fixed.Ranges[layout][index];
			realization.Ranges[index] = range;
			bindings[index] = { range.Binding, RangeDescriptorType(range), range.DescriptorCount,
							   FixedDescriptorStages(range, bGraphics, bRay), nullptr };
			sizes[index] = { bindings[index].descriptorType, range.DescriptorCount };
			const NativeTableRangeBinding* table = FindNativeTableRange(set, index);
			ASSERT((table != nullptr) == (range.Source == EBindingRangeSource::DescriptorTable), "Each table-backed range retains its immutable native version.");
			realization.Tables[index] = table != nullptr ? table->Table : nullptr;
			if (range.Type == EDescriptorType::SampledTexture || range.Type == EDescriptorType::StorageTexture)
			{
				imageCount += table != nullptr ? static_cast<uint32>(table->UsedEntries.Size()) : range.DescriptorCount;
			}
		}
		if (!realization.Images.Reserve(imageCount))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not capture selected descriptor image layouts."));
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		layoutInfo.bindingCount = realization.RangeCount;
		layoutInfo.pBindings = bindings;
		VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = realization.RangeCount;
		poolInfo.pPoolSizes = sizes;
		if (state.F.CreateDescriptorSetLayout(state.Device, &layoutInfo, nullptr, &realization.Layout) != VK_SUCCESS ||
			state.F.CreateDescriptorPool(state.Device, &poolInfo, nullptr, &realization.Pool) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.BindingRange", "Could not create immutable native descriptor storage."));
		}
		VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocation.descriptorPool = realization.Pool;
		allocation.descriptorSetCount = 1;
		allocation.pSetLayouts = &realization.Layout;
		if (state.F.AllocateDescriptorSets(state.Device, &allocation, &realization.Set) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.BindingRange", "Could not allocate immutable native descriptor set."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> WriteDescriptorSetValues(
		State& state,
		Pending& pending,
		DescriptorSetRealization& realization,
		const NativeFixedBindingSet& set,
		Span<const NativePreparedTextureAccess> intents,
		bool bPrepared)
	{
		VkCopyDescriptorSet copies[MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
		uint32 copyCount = 0;
		for (uint32 index = 0; index < realization.RangeCount; ++index)
		{
			const NativeTableRangeBinding* table = FindNativeTableRange(set, index);
			if (table == nullptr)
			{
				continue;
			}
			VkCopyDescriptorSet& copy = copies[copyCount++];
			copy = { VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET };
			copy.srcSet = static_cast<DescriptorTable*>(table->Table)->Set;
			copy.srcBinding = 0;
			copy.dstSet = realization.Set;
			copy.dstBinding = realization.Ranges[index].Binding;
			copy.descriptorCount = realization.Ranges[index].DescriptorCount;
		}
		if (copyCount != 0)
		{
			WriteCommandDescriptors(state, pending, 0, nullptr, copyCount, copies);
		}

		Array<VkWriteDescriptorSet> imageWrites(state.Resource);
		Array<VkDescriptorImageInfo> selectedImages(state.Resource);
		if (!imageWrites.Reserve(realization.Images.Capacity()) || !selectedImages.Reserve(realization.Images.Capacity()))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not capture selected descriptor writes."));
		}
		VkWriteDescriptorSet writes[MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
		VkWriteDescriptorSetAccelerationStructureKHR accelerationWrites[MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
		VkDescriptorBufferInfo buffers[32]{};
		VkDescriptorImageInfo images[32]{};
		VkBufferView views[32]{};
		VkAccelerationStructureKHR accelerationStructures[32]{};
		uint32 fixedIndex = 0;
		uint32 writeCount = 0;
		for (uint32 rangeIndex = 0; rangeIndex < realization.RangeCount; ++rangeIndex)
		{
			const BindingRangeDesc& range = realization.Ranges[rangeIndex];
			const NativeTableRangeBinding* tableBinding = FindNativeTableRange(set, rangeIndex);
			const bool bImage = range.Type == EDescriptorType::SampledTexture || range.Type == EDescriptorType::StorageTexture;
			if (tableBinding != nullptr)
			{
				if (bImage)
				{
					DescriptorTable& table = *static_cast<DescriptorTable*>(tableBinding->Table);
					for (const uint32 element : tableBinding->UsedEntries)
					{
						vulkan::Texture* texture = table.Textures[element];
						vulkan::TextureView* view = table.TextureViews[element];
						const VkImageLayout imageLayout = FixedDescriptorImageLayout(pending, *texture, view, range.Type, intents, bPrepared);
						if (!SupportsAttachmentSamplingLayout(state, imageLayout))
						{
							return Result<void>::Failure(
								EErrorCode::Unsupported,
								"Rhi.AttachmentSampling",
								"Read-only attachment sampling requires load/store NONE; mixed planes also require " "separate depth/stencil attachment access.");
						}
						if (!SupportsAttachmentSamplingLayout(state, imageLayout))
						{
							return Result<void>::Failure(
								EErrorCode::Unsupported,
								"Rhi.AttachmentSampling",
								"Read-only attachment sampling requires load/store NONE; mixed planes also require " "separate depth/stencil attachment access.");
						}
						realization.Images.AddRequired({ texture, view, rangeIndex, element, imageLayout });
						selectedImages.AddRequired({ VK_NULL_HANDLE,
													view != nullptr ? view->Handle
													: range.Type == EDescriptorType::StorageTexture ? texture->GetStorageView()
																									: texture->View,
													imageLayout });
						VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
						write.dstSet = realization.Set;
						write.dstBinding = range.Binding;
						write.dstArrayElement = element;
						write.descriptorCount = 1;
						write.descriptorType = RangeDescriptorType(range);
						write.pImageInfo = &selectedImages.Back();
						imageWrites.AddRequired(write);
					}
				}
				continue;
			}

			VkWriteDescriptorSet& write = writes[writeCount];
			write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.dstSet = realization.Set;
			write.dstBinding = range.Binding;
			write.descriptorCount = range.DescriptorCount;
			write.descriptorType = RangeDescriptorType(range);
			if (range.Type == EDescriptorType::AccelerationStructure)
			{
				VkWriteDescriptorSetAccelerationStructureKHR& acceleration = accelerationWrites[writeCount];
				acceleration = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
				acceleration.accelerationStructureCount = range.DescriptorCount;
				acceleration.pAccelerationStructures = &accelerationStructures[fixedIndex];
				write.pNext = &acceleration;
			}
			else if (bImage || range.Type == EDescriptorType::Sampler)
			{
				write.pImageInfo = &images[fixedIndex];
			}
			else if (range.BufferFormat != EFormat::UNKNOWN)
			{
				write.pTexelBufferView = &views[fixedIndex];
			}
			else
			{
				write.pBufferInfo = &buffers[fixedIndex];
			}
			++writeCount;
			for (uint32 element = 0; element < range.DescriptorCount; ++element, ++fixedIndex)
			{
				ASSERT(fixedIndex < set.Descriptors.Size(), "Each fixed range element has a captured resource tuple.");
				const rhi::NativeFixedDescriptor& value = set.Descriptors[fixedIndex];
				if (range.Type == EDescriptorType::Sampler)
				{
					images[fixedIndex].sampler = static_cast<Sampler*>(value.Resource)->Handle;
				}
				else if (range.Type == EDescriptorType::AccelerationStructure)
				{
					accelerationStructures[fixedIndex] = static_cast<NativeAccelStruct*>(value.Resource)->Handle;
				}
				else if (bImage)
				{
					Texture* texture = static_cast<Texture*>(value.Resource);
					TextureView* view = static_cast<TextureView*>(value.View);
					const VkImageLayout imageLayout = FixedDescriptorImageLayout(pending, *texture, view, range.Type, intents, bPrepared);
					if (!SupportsAttachmentSamplingLayout(state, imageLayout))
					{
						return Result<void>::Failure(
							EErrorCode::Unsupported,
							"Rhi.AttachmentSampling",
							"Read-only attachment sampling requires load/store NONE; mixed planes also require " "separate depth/stencil attachment access.");
					}
					realization.Images.AddRequired({ texture, view, rangeIndex, element, imageLayout });
					images[fixedIndex] = { VK_NULL_HANDLE,
										  view != nullptr ? view->Handle
										  : range.Type == EDescriptorType::StorageTexture ? texture->GetStorageView()
																						  : texture->View,
										  imageLayout };
				}
				else if (range.BufferFormat != EFormat::UNKNOWN)
				{
					views[fixedIndex] = static_cast<BufferView*>(value.View)->Handle;
				}
				else
				{
					const bool bUniform = range.Type == EDescriptorType::UniformBuffer;
					if (value.ByteCount > (bUniform ? state.DeviceProperties.limits.maxUniformBufferRange : state.DeviceProperties.limits.maxStorageBufferRange) ||
						value.ByteOffset % (bUniform ? state.DeviceProperties.limits.minUniformBufferOffsetAlignment : state.DeviceProperties.limits.minStorageBufferOffsetAlignment) != 0)
					{
						return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BindingRange", "The buffer range exceeds native limits.");
					}
					buffers[fixedIndex] = { static_cast<Buffer*>(value.Resource)->Handle, value.ByteOffset, value.ByteCount };
				}
			}
		}
		ASSERT(fixedIndex == set.Descriptors.Size(), "Fixed descriptors exclude native table ranges.");
		if (writeCount != 0)
		{
			WriteCommandDescriptors(state, pending, writeCount, writes, 0, nullptr);
		}
		if (!imageWrites.IsEmpty())
		{
			WriteCommandDescriptors(state, pending, static_cast<uint32>(imageWrites.Size()), imageWrites.Data(), 0, nullptr);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<SharedPtr<PreparedDescriptorSet>> CreateDescriptorSetRealization(
		State& state,
		Pending& pending,
		const NativeFixedLayoutState& fixed,
		uint32 layout,
		const NativeFixedBindingSet& set,
		bool bGraphics,
		bool bRay,
		Span<const NativePreparedTextureAccess> intents,
		bool bPrepared)
	{
		using R = Result<SharedPtr<PreparedDescriptorSet>>;
		vulkan::DescriptorSetRealization* realization = CreateObject<DescriptorSetRealization>(state.Resource, state.Resource);
		if (realization == nullptr)
		{
			return R::Failure(Failure("Rhi.Allocation", "Could not own immutable native descriptors."));
		}
		SharedPtr<PreparedDescriptorSet> owner;
		if (!MakeSharedInto(state.Resource, &owner, &state, realization))
		{
			DestroyDescriptorSetRealization(&state, realization);

			return R::Failure(Failure("Rhi.Allocation", "Could not retain immutable native descriptors."));
		}

		Result<void> storage = InitializeDescriptorSetStorage(state, *realization, fixed, layout, set, bGraphics, bRay);
		if (!storage)
		{
			return R::Failure(storage.GetError());
		}

		Result<void> values = WriteDescriptorSetValues(state, pending, *realization, set, intents, bPrepared);
		if (!values)
		{
			return R::Failure(values.GetError());
		}

		++pending.Statistics.DescriptorTableMaterializations;

		return R::Success(Move(owner));
	}

	[[nodiscard]] Result<void> WriteCachedFixedBindingSets(
		State& state,
		Pending& pending,
		const NativeFixedLayoutState& fixed,
		VkPipelineLayout pipelineLayout,
		bool bGraphics,
		bool bRay,
		Span<const NativeFixedBindingSet> sets,
		Pending::PreparedBinding* prepared,
		Span<const NativePreparedTextureAccess> intents)
	{
		if (prepared == nullptr && !pending.DescriptorOwners.Reserve(pending.DescriptorOwners.Size() + sets.Size()))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain native descriptors."));
		}
		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			const rhi::NativeFixedBindingSet& set = sets[layout];
			ASSERT(set.Cache, "An immutable binding set owns its native realization cache.");
			SharedPtr<PreparedDescriptorSet> owner;
			for (uint32 entry = 0; entry < PreparedBindingCache::CAPACITY; ++entry)
			{
				const SharedPtr<PreparedDescriptorSet>& candidate = set.Cache->Get(entry);
				if (candidate && DescriptorSetMatches(*static_cast<const DescriptorSetRealization*>(candidate->GetNative()), fixed, layout, set, pending, bGraphics, bRay, intents, prepared != nullptr))
				{
					owner = candidate;
					++pending.Statistics.DescriptorTableCacheHits;
					break;
				}
			}
			if (!owner)
			{
				Result<SharedPtr<PreparedDescriptorSet>> result = CreateDescriptorSetRealization(state, pending, fixed, layout, set, bGraphics, bRay, intents, prepared != nullptr);
				if (!result)
				{
					return Result<void>::Failure(result.GetError());
				}
				owner = Move(result).TakeValue();
				set.Cache->Capture(owner);
			}
			DescriptorSetRealization& realization = *static_cast<DescriptorSetRealization*>(owner->GetNative());
			if (prepared == nullptr)
			{
				for (vulkan::DescriptorSetRealization::Image const& image : realization.Images)
				{
					const TextureViewDesc* extent = image.View != nullptr ? &image.View->Descriptor : nullptr;
					if (TextureHasLayout(pending, *image.Resource, image.Layout, extent))
					{
						continue;
					}
					ASSERT(!bGraphics, "Prepare fixed textures before rendering.");
					if (extent != nullptr)
					{
						RequireTextureLayoutRange(state, pending, *image.Resource, image.Layout, *extent);
					}
					else
					{
						RequireTextureLayout(state, pending, *image.Resource, image.Layout);
					}
				}
				pending.DescriptorOwners.AddRequired(owner);
				state.F.CmdBindDescriptorSets(pending.Commands,
					bGraphics ? VK_PIPELINE_BIND_POINT_GRAPHICS
					: bRay ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
					: VK_PIPELINE_BIND_POINT_COMPUTE,
					pipelineLayout, fixed.Spaces[layout], 1, &realization.Set, 0, nullptr);
			}
			else
			{
				prepared->DescriptorOwners[layout] = owner;
				prepared->Sets[layout] = realization.Set;
			}
		}
		if (prepared != nullptr)
		{
			prepared->Count = static_cast<uint32>(sets.Size());
		}
		else if (bGraphics)
		{
			SynchronizeGraphicsStorage(state, pending);
		}
		pending.LastComputePipeline = nullptr;

		return Result<void>::Success();
	}
}
