#include "VulkanBackend.h"
#include "VulkanDescriptors.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<void*> CreateRayTracingPipeline(State* opaque, const NativeRayPipelineDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		if (!state.bRayTracingPipeline)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanRayPipeline", "The Vulkan ray pipeline feature is unavailable.");
		}

		if (desc.MaxRecursionDepth > state.RayTracingProperties.maxRayRecursionDepth ||
			desc.MaxAttributeBytes > state.RayTracingProperties.maxRayHitAttributeSize ||
			desc.PushConstantBytes > state.DeviceProperties.limits.maxPushConstantsSize)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanRayPipeline", "The ray pipeline exceeds the device limits.");
		}

		for (const NativeBindingLayoutDesc& layout : desc.GlobalLayouts)
		{
			for (uint32 item = 0; item < layout.TextureCount(); ++item)
			{
				const ComputeTextureBindingDesc texture = layout.Texture(item);
				if (texture.Type == EComputeTextureBindingType::SampledTexture && !SupportsSampler(state, texture.Sampler))
				{
					return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanSampler", "The ray sampler exceeds device limits.");
				}
			}
		}

		const bool bRanges = !desc.GlobalLayouts.IsEmpty() && desc.GlobalLayouts[0].bRangeLayout;
		uint32 localDataAlignment = 1;
		for (const rhi::NativeRayShaderDesc& shader : desc.Shaders)
		{
			if (shader.LocalDataBytes != 0)
			{
				localDataAlignment = Max(localDataAlignment, shader.LocalDataAlignmentBytes);
			}
		}
		for (const rhi::NativeRayHitGroupDesc& group : desc.HitGroups)
		{
			if (group.LocalDataBytes != 0)
			{
				localDataAlignment = Max(localDataAlignment, group.LocalDataAlignmentBytes);
			}
		}
		if (state.RayTracingProperties.shaderGroupHandleSize % localDataAlignment != 0)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanRayPipeline", "The native shader handle cannot align the requested record storage.");
		}
		uint32 setCount = static_cast<uint32>(desc.GlobalLayouts.Size() != 0 ? desc.GlobalLayouts.Size() : (desc.AccelStructSlot != 0xFFFFFFFFu ? 1u : 0u));

		if (setCount > state.DeviceProperties.limits.maxBoundDescriptorSets)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanRayPipeline", "The ray layouts exceed the device set limit.");
		}
		RayTracingPipeline* pipeline = CreateObject<RayTracingPipeline>(state.Resource, state.Resource);

		if (pipeline == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a Vulkan ray pipeline."));
		}
		VkShaderModule modules[128]{};
		pipeline->LocalDataAlignmentBytes = localDataAlignment;
		VkPipelineShaderStageCreateInfo stages[128]{};
		VkRayTracingShaderGroupCreateInfoKHR groups[64]{};
		uint32 moduleCount = 0;
		auto release = [&]() noexcept
			{
				for (uint32 index = 0; index < moduleCount; ++index)
				{
					state.F.DestroyShaderModule(state.Device, modules[index], nullptr);
				}
				FreeRayPipeline(state, pipeline);
			};
		for (const NativeRayShaderDesc& shader : desc.Shaders)
		{
			if (shader.Bytecode.IsEmpty() || shader.Bytecode.Size() % sizeof(uint32) != 0)
			{
				release();

				return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.VulkanRayPipeline", "SPIR-V shader bytecode must contain whole words.");
			}

			Array<uint32> words(state.Resource);

			if (!words.ResizeUninitialized(shader.Bytecode.Size() / sizeof(uint32)))
			{
				release();

				return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not align ray shader bytecode."));
			}

			CopyBytes(words.Data(), shader.Bytecode.Data(), shader.Bytecode.Size());
			VkShaderModuleCreateInfo shaderInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			shaderInfo.codeSize = shader.Bytecode.Size();
			shaderInfo.pCode = words.Data();
			VkShaderModule module = VK_NULL_HANDLE;

			if (state.F.CreateShaderModule(state.Device, &shaderInfo, nullptr, &module) != VK_SUCCESS)
			{
				release();

				return Result<void*>::Failure(Failure("Rhi.VulkanRayPipeline", "Could not create a ray shader module."));
			}
			modules[moduleCount] = module;
			VkPipelineShaderStageCreateInfo& stage = stages[moduleCount];
			stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			stage.module = module;
			stage.pName = shader.EntryPoint;
			++moduleCount;

			switch (shader.Stage)
			{
			case EShaderStage::RayGeneration:
				stage.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
				break;
			case EShaderStage::Miss:
				stage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
				break;
			case EShaderStage::ClosestHit:
				stage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
				break;
			case EShaderStage::AnyHit:
				stage.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
				break;
			case EShaderStage::Intersection:
				stage.stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
				break;
			case EShaderStage::Callable:
				stage.stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;
				break;
			default:
				release();

				return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.VulkanRayPipeline", "The ray shader stage is invalid.");
			}
		}

		const uint32 groupCount = desc.GeneralShaderCount + static_cast<uint32>(desc.HitGroups.Size());

		for (uint32 index = 0; index < desc.GeneralShaderCount; ++index)
		{
			VkRayTracingShaderGroupCreateInfoKHR& group = groups[index];
			group = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
			group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
			group.generalShader = index;
			group.closestHitShader = VK_SHADER_UNUSED_KHR;
			group.anyHitShader = VK_SHADER_UNUSED_KHR;
			group.intersectionShader = VK_SHADER_UNUSED_KHR;
		}

		for (usize index = 0; index < desc.HitGroups.Size(); ++index)
		{
			const NativeRayHitGroupDesc& source = desc.HitGroups[index];
			VkRayTracingShaderGroupCreateInfoKHR& group = groups[desc.GeneralShaderCount + index];
			group = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
			group.type = source.bProcedural ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
			group.generalShader = VK_SHADER_UNUSED_KHR;
			group.closestHitShader = source.ClosestHitIndex;
			group.anyHitShader = source.AnyHitIndex;
			group.intersectionShader = source.IntersectionIndex;
		}
		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipeline->AccelStructSlot = desc.AccelStructSlot;
		pipeline->PushConstantBytes = desc.PushConstantBytes;
		pipeline->SetLayoutCount = setCount;

		if (bRanges)
		{
			Result<void> created = CreateRangeLayouts(state, *pipeline, desc.GlobalLayouts, false, true);
			if (!created)
			{
				release();

				return Result<void*>::Failure(created.GetError());
			}
			setCount = pipeline->SetLayoutCount;
		}
		for (uint32 layoutIndex = 0; !bRanges && layoutIndex < setCount; ++layoutIndex)
		{
			VkDescriptorSetLayoutBinding globalBindings[25]{};
			constexpr VkShaderStageFlags rayStages = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
				VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
				VK_SHADER_STAGE_INTERSECTION_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR;
			uint32 bindingCount = 0;
			const NativeBindingLayoutDesc* const layout = layoutIndex < desc.GlobalLayouts.Size() ? &desc.GlobalLayouts[layoutIndex] : nullptr;
			const uint32 bufferCount = layout != nullptr ? layout->BufferCount() : 0;
			const uint32 textureCount = layout != nullptr ? layout->TextureCount() : 0;
			for (uint32 index = 0; index < bufferCount; ++index)
			{
				const ComputeBindingDesc binding = layout->Buffer(index);
				VkDescriptorSetLayoutBinding& nativeBinding = globalBindings[bindingCount++];
				nativeBinding.binding = binding.Slot;
				nativeBinding.descriptorType = BufferDescriptorType(binding.Type, binding.Format);
				nativeBinding.descriptorCount = 1;
				nativeBinding.stageFlags = rayStages;
			}

			for (uint32 index = 0; index < textureCount; ++index)
			{
				const ComputeTextureBindingDesc texture = layout->Texture(index);
				pipeline->TextureSlots[layoutIndex][index] = texture.Slot;
				VkDescriptorSetLayoutBinding& nativeBinding = globalBindings[bindingCount++];
				nativeBinding.binding = (texture.Type == EComputeTextureBindingType::SampledTexture ? 32u : 64u) + texture.Slot;
				nativeBinding.descriptorType = texture.Type == EComputeTextureBindingType::SampledTexture ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				nativeBinding.descriptorCount = 1;
				nativeBinding.stageFlags = rayStages;

				if (texture.Type != EComputeTextureBindingType::SampledTexture)
				{
					continue;
				}
				VkSamplerReductionModeCreateInfo reduction{};
				const VkSamplerCreateInfo samplerInfo = MakeSamplerCreateInfo(texture.Sampler, reduction);

				if (state.F.CreateSampler(state.Device, &samplerInfo, nullptr, &pipeline->Samplers[layoutIndex][index]) != VK_SUCCESS)
				{
					release();

					return Result<void*>::Failure(Failure("Rhi.VulkanRayPipeline", "Could not create a ray sampler."));
				}

				VkDescriptorSetLayoutBinding& samplerBinding = globalBindings[bindingCount++];
				samplerBinding.binding = 48 + texture.Slot;
				samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				samplerBinding.descriptorCount = 1;
				samplerBinding.stageFlags = rayStages;
			}

			if (layoutIndex == 0 && desc.AccelStructSlot != 0xFFFFFFFFu)
			{
				VkDescriptorSetLayoutBinding& binding = globalBindings[bindingCount++];
				binding.binding = 80 + desc.AccelStructSlot;
				binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
				binding.descriptorCount = 1;
				binding.stageFlags = rayStages;
			}
			VkDescriptorSetLayoutCreateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			setInfo.bindingCount = bindingCount;
			setInfo.pBindings = globalBindings;

			if (state.F.CreateDescriptorSetLayout(state.Device, &setInfo, nullptr, &pipeline->SetLayouts[layoutIndex]) != VK_SUCCESS)
			{
				release();

				return Result<void*>::Failure(Failure("Rhi.VulkanRayPipeline", "Could not create the global ray layout."));
			}
		}
		bool localKinds[5]{};
		bool bLocalAddresses = false;

		for (uint32 group = 0; group < groupCount; ++group)
		{
			const NativeBindingLayoutDesc& local = group < desc.GeneralShaderCount ? desc.Shaders[group].LocalLayout : desc.HitGroups[group - desc.GeneralShaderCount].LocalLayout;

			for (uint32 item = 0; item < local.BufferCount(); ++item)
			{
				const ComputeBindingDesc binding = local.Buffer(item);
				if (binding.Format == EFormat::UNKNOWN)
				{
					bLocalAddresses = true;
				}
				else
				{
					localKinds[binding.Type == EComputeBindingType::ReadOnlyBuffer ? 3 : 4] = true;
				}
			}

			for (usize index = 0; index < local.TextureCount(); ++index)
			{
				const ComputeTextureBindingDesc& texture = local.Texture(static_cast<uint32>(index));
				const bool bSampled = texture.Type == EComputeTextureBindingType::SampledTexture;
				localKinds[bSampled ? 0 : 1] = true;

				if (!bSampled)
				{
					continue;
				}
				localKinds[2] = true;

				if (!SupportsSampler(state, texture.Sampler))
				{
					release();

					return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanRayPipeline", "A local sampler exceeds device limits.");
				}
				VkSamplerReductionModeCreateInfo reduction{};
				const VkSamplerCreateInfo samplerInfo = MakeSamplerCreateInfo(texture.Sampler, reduction);

				if (state.F.CreateSampler(state.Device, &samplerInfo, nullptr, &pipeline->LocalSamplers[group][index]) != VK_SUCCESS)
				{
					release();

					return Result<void*>::Failure(Failure("Rhi.VulkanRayPipeline", "Could not create a local sampler."));
				}
			}
		}

		const bool bLocalDescriptors = localKinds[0] || localKinds[1] || localKinds[2] || localKinds[3] || localKinds[4];

		if ((bLocalAddresses && !state.bShaderInt64) ||
			(localKinds[0] && state.MaximumBindlessSampledTextures == 0) ||
			(localKinds[1] && state.MaximumBindlessStorageTextures == 0) ||
			(localKinds[2] && state.MaximumBindlessSamplers == 0) ||
			((localKinds[3] || localKinds[4]) && !state.bBindlessTypedBuffers))
		{
			release();

			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanRayPipeline", "The local ray resources require unavailable address or descriptor indexing features.");
		}

		if (bLocalDescriptors)
		{
			const uint32 capacity = desc.LocalDescriptorCapacity != 0 ? desc.LocalDescriptorCapacity : 32;
			const VkPhysicalDeviceLimits& limits = state.DeviceProperties.limits;

			if (capacity > 97 * 8 || limits.maxBoundDescriptorSets < 5)
			{
				release();

				return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanRayPipeline", "The local ray descriptor capacity or set exceeds supported limits.");
			}

			uint64 sampledCount = 0;
			uint64 storageCount = 0;
			uint64 samplerCount = 0;
			uint64 uniformCount = 0;
			uint64 bufferCount = 0;

			for (const rhi::NativeBindingLayoutDesc& global : desc.GlobalLayouts)
			{
				for (uint32 item = 0; item < global.BufferCount(); ++item)
				{
					const ComputeBindingDesc binding = global.Buffer(item);
					if (binding.Format != EFormat::UNKNOWN)
					{
						if (binding.Type == EComputeBindingType::ReadOnlyBuffer)
						{
							++sampledCount;
						}
						else
						{
							++storageCount;
						}
					}
					else if (binding.Type == EComputeBindingType::UniformBuffer)
					{
						++uniformCount;
					}
					else
					{
						++bufferCount;
					}
				}

				for (uint32 item = 0; item < global.TextureCount(); ++item)
				{
					const ComputeTextureBindingDesc texture = global.Texture(item);
					if (texture.Type == EComputeTextureBindingType::SampledTexture)
					{
						++sampledCount;
						++samplerCount;
					}
					else
					{
						++storageCount;
					}
				}
			}
			sampledCount += capacity * (static_cast<uint32>(localKinds[0]) + static_cast<uint32>(localKinds[3]));
			storageCount += capacity * (static_cast<uint32>(localKinds[1]) + static_cast<uint32>(localKinds[4]));
			samplerCount += localKinds[2] ? capacity : 0;

			if (sampledCount > Min(limits.maxDescriptorSetSampledImages, limits.maxPerStageDescriptorSampledImages) ||
				storageCount > Min(limits.maxDescriptorSetStorageImages, limits.maxPerStageDescriptorStorageImages) ||
				samplerCount > Min(limits.maxDescriptorSetSamplers, limits.maxPerStageDescriptorSamplers) ||
				uniformCount > Min(limits.maxDescriptorSetUniformBuffers, limits.maxPerStageDescriptorUniformBuffers) ||
				bufferCount > Min(limits.maxDescriptorSetStorageBuffers, limits.maxPerStageDescriptorStorageBuffers) ||
				sampledCount + storageCount + uniformCount + bufferCount > limits.maxPerStageResources)
			{
				release();

				return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanRayPipeline", "Global and local ray descriptors exceed device limits.");
			}

			for (uint32 index = setCount; index < 4; ++index)
			{
				VkDescriptorSetLayoutCreateInfo empty{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				if (state.F.CreateDescriptorSetLayout(state.Device, &empty, nullptr, &pipeline->SetLayouts[index]) != VK_SUCCESS)
				{
					release();

					return Result<void*>::Failure(Failure("Rhi.VulkanRayPipeline", "Could not create a ray layout gap."));
				}
			}
			constexpr VkDescriptorType types[5]{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
												VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
												VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER };
			VkDescriptorSetLayoutBinding bindings[5]{};
			VkDescriptorBindingFlags bindingFlags[5]{};
			uint32 count = 0;

			for (uint32 index = 0; index < 5; ++index)
			{
				if (!localKinds[index])
				{
					continue;
				}
				pipeline->LocalDescriptorCounts[index] = capacity;
				bindings[count] = { index, types[index], capacity,
								   VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
									   VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
									   VK_SHADER_STAGE_CALLABLE_BIT_KHR,
								   nullptr };
				bindingFlags[count++] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
			}
			VkDescriptorSetLayoutBindingFlagsCreateInfo flags{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
			flags.bindingCount = count;
			flags.pBindingFlags = bindingFlags;
			VkDescriptorSetLayoutCreateInfo localInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			localInfo.pNext = &flags;
			localInfo.bindingCount = count;
			localInfo.pBindings = bindings;

			if (state.F.CreateDescriptorSetLayout(state.Device, &localInfo, nullptr, &pipeline->SetLayouts[4]) != VK_SUCCESS)
			{
				release();

				return Result<void*>::Failure(Failure("Rhi.VulkanRayPipeline", "Could not create the local ray layout."));
			}
		}
		layoutInfo.setLayoutCount = bLocalDescriptors ? 5 : setCount;
		layoutInfo.pSetLayouts = pipeline->SetLayouts;
		VkPushConstantRange pushRange{};
		if (desc.PushConstantBytes != 0)
		{
			pushRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
				VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
				VK_SHADER_STAGE_CALLABLE_BIT_KHR;
			pushRange.size = desc.PushConstantBytes;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &pushRange;
		}

		if (state.F.CreatePipelineLayout(state.Device, &layoutInfo, nullptr, &pipeline->Layout) != VK_SUCCESS)
		{
			release();

			return Result<void*>::Failure(Failure("Rhi.VulkanRayPipeline", "Could not create the ray pipeline layout."));
		}
		VkRayTracingPipelineCreateInfoKHR info{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
		info.stageCount = static_cast<uint32>(desc.Shaders.Size());
		info.pStages = stages;
		info.groupCount = groupCount;
		info.pGroups = groups;
		info.maxPipelineRayRecursionDepth = desc.MaxRecursionDepth;
		info.layout = pipeline->Layout;

		if (desc.bAllowOpacityMicromaps)
		{
			info.flags |= VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;
		}
		VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV clusterInfo{
			VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CLUSTER_ACCELERATION_STRUCTURE_CREATE_INFO_NV };
		if (desc.bAllowClusterAccelerationStructures)
		{
			clusterInfo.allowClusterAccelerationStructure = VK_TRUE;
			info.pNext = &clusterInfo;
		}

		if (state.F.CreateRayTracingPipelines(state.Device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline->Handle) !=
			VK_SUCCESS)
		{
			release();

			return Result<void*>::Failure(Failure("Rhi.VulkanRayPipeline", "Could not create the ray pipeline."));
		}

		const usize handleBytes = static_cast<usize>(state.RayTracingProperties.shaderGroupHandleSize) * groupCount;

		if (!pipeline->GroupHandles.ResizeUninitialized(handleBytes))
		{
			release();

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not store ray shader group handles."));
		}

		if (state.F.GetRayTracingShaderGroupHandles(state.Device, pipeline->Handle, 0, groupCount, handleBytes,
			pipeline->GroupHandles.Data()) != VK_SUCCESS)
		{
			release();

			return Result<void*>::Failure(Failure("Rhi.VulkanRayPipeline", "Could not read ray shader group handles."));
		}

		for (uint32 index = 0; index < moduleCount; ++index)
		{
			state.F.DestroyShaderModule(state.Device, modules[index], nullptr);
		}

		return Result<void*>::Success(pipeline);
	}

	void DestroyRayTracingPipelineNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		FreeRayPipeline(state, static_cast<RayTracingPipeline*>(native));
	}

	[[nodiscard]] Result<void> DestroyRayTracingPipeline(State* opaque, void* native)
	{
		State& state = *opaque;
		RayTracingPipeline* pipeline = static_cast<RayTracingPipeline*>(native);
		uint64 complete = 0;

		if (!ReadCompletedSerial(state, complete))
		{
			return Result<void>::Failure(Failure("Rhi.VulkanTimeline", "Could not read GPU completion."));
		}

		if (pipeline->LastUse > complete)
		{
			if (!state.RetiredRayPipelines.Add({ pipeline, pipeline->LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retire a ray pipeline."));
			}
		}
		else
		{
			FreeRayPipeline(state, pipeline);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordRayResourceBarriers(State& state, Pending& pending, Span<const NativeBindingSet> sets, bool bBefore)
	{
		Array<VkBufferMemoryBarrier> buffers(state.Resource);
		Array<VkImageMemoryBarrier> images(state.Resource);
		const bool bGraphicsQueue = (QueueFlags(state, pending.QueueKind) & VK_QUEUE_GRAPHICS_BIT) != 0;

		for (const NativeBindingSet& set : sets)
		{
			for (const NativeComputeBuffer& binding : set.Buffers)
			{
				if (!bBefore && binding.Type != EComputeBindingType::StorageBuffer)
				{
					continue;
				}

				const VkBuffer handle = static_cast<const Buffer*>(binding.Buffer)->Handle;

				if (binding.Type == EComputeBindingType::StorageBuffer && !ShouldRecordBufferUavBarrier(pending, handle))
				{
					continue;
				}
				bool bFound = false;

				for (const VkBufferMemoryBarrier& existing : buffers)
				{
					if (existing.buffer == handle && existing.offset == binding.ByteOffset && existing.size == binding.ByteCount)
					{
						bFound = true;
						break;
					}
				}

				if (bFound)
				{
					continue;
				}
				VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
				barrier.srcAccessMask = bBefore ? VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_WRITE_BIT;
				barrier.dstAccessMask = bBefore ? VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
					: VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT |
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
					VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

				if (!bBefore && bGraphicsQueue)
				{
					barrier.dstAccessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
				}
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.buffer = handle;
				barrier.offset = binding.ByteOffset;
				barrier.size = binding.ByteCount;

				if (!buffers.Add(barrier))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not record ray buffer barriers."));
				}
			}

			for (const NativeComputeTexture& binding : set.Textures)
			{
				const bool bStorage = binding.Type == EComputeTextureBindingType::StorageTexture;

				if (!bBefore && !bStorage)
				{
					continue;
				}

				const Texture& texture = *static_cast<const Texture*>(binding.Texture);

				if (bStorage && !ShouldRecordTextureUavBarrier(pending, texture.Handle))
				{
					continue;
				}

				const TextureViewDesc* const view = binding.View != nullptr ? &static_cast<const TextureView*>(binding.View)->Descriptor : nullptr;
				const VkImageLayout layout = bStorage ? VK_IMAGE_LAYOUT_GENERAL : SampledTextureLayout(pending, texture, view);
				const VkImageSubresourceRange range{
					static_cast<VkImageAspectFlags>(texture.bDepth
														? VK_IMAGE_ASPECT_DEPTH_BIT |
															  (GetFormatInfo(texture.Format).bHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)
														: VK_IMAGE_ASPECT_COLOR_BIT),
					view != nullptr ? view->BaseMipLevel : 0, view != nullptr ? view->MipLevelCount : texture.MipLevels,
					view != nullptr ? view->BaseArrayLayer : 0, view != nullptr ? view->ArrayLayerCount : texture.ArrayLayers };
				bool bFound = false;

				for (const VkImageMemoryBarrier& existing : images)
				{
					if (existing.image == texture.Handle &&
						existing.oldLayout == layout &&
						existing.subresourceRange.aspectMask == range.aspectMask &&
						existing.subresourceRange.baseMipLevel == range.baseMipLevel &&
						existing.subresourceRange.levelCount == range.levelCount &&
						existing.subresourceRange.baseArrayLayer == range.baseArrayLayer &&
						existing.subresourceRange.layerCount == range.layerCount)
					{
						bFound = true;
						break;
					}
				}

				if (bFound)
				{
					continue;
				}
				VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
				barrier.srcAccessMask = bBefore ? VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_WRITE_BIT;
				barrier.dstAccessMask = bBefore ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
					: VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
					VK_ACCESS_SHADER_WRITE_BIT;
				barrier.oldLayout = layout;
				barrier.newLayout = layout;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = texture.Handle;
				barrier.subresourceRange = range;

				if (!images.Add(barrier))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not record ray texture barriers."));
				}
			}
		}

		if (!buffers.IsEmpty() || !images.IsEmpty())
		{
			RecordBarriers(state, pending, bBefore ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
				bBefore ? VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
				static_cast<uint32>(buffers.Size()), buffers.Data(), static_cast<uint32>(images.Size()),
				images.Data());

			if (!bBefore)
			{
				for (const VkBufferMemoryBarrier& barrier : buffers)
				{
					MarkBufferUavBarrierPlaced(pending, barrier.buffer);
				}

				for (const VkImageMemoryBarrier& barrier : images)
				{
					MarkTextureUavBarrierPlaced(pending, barrier.image);
				}
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordRayDispatch(
		State* opaque,
		void* command,
		void* native,
		const NativeShaderTableDesc& table,
		Span<const NativeBindingSet> sets,
		Span<const NativeFixedBindingSet> fixedSets,
		Span<const Byte> pushConstants,
		void* accelNative,
		uint32 width,
		uint32 height,
		uint32 depth)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(command);
		RayTracingPipeline& pipeline = *static_cast<RayTracingPipeline*>(native);

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		for (const NativeBindingSet& set : sets)
		{
			for (const NativeComputeBuffer& binding : set.Buffers)
			{
				const bool bUniform = binding.Type == EComputeBindingType::UniformBuffer;

				if (binding.View == nullptr &&
					(binding.ByteCount > (bUniform ? state.DeviceProperties.limits.maxUniformBufferRange : state.DeviceProperties.limits.maxStorageBufferRange) ||
							binding.ByteOffset % (bUniform ? state.DeviceProperties.limits.minUniformBufferOffsetAlignment : state.DeviceProperties.limits.minStorageBufferOffsetAlignment) != 0))
				{
					return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanRayDispatch", "A global buffer exceeds the descriptor limits.");
				}
			}
		}

		const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& properties = state.RayTracingProperties;
		const uint64 handleSize = properties.shaderGroupHandleSize;
		const uint64 handleAlignment = Max(properties.shaderGroupHandleAlignment, pipeline.LocalDataAlignmentBytes);
		const uint64 baseAlignment = Max(properties.shaderGroupBaseAlignment, pipeline.LocalDataAlignmentBytes);
		const uint64 maxRays = properties.maxRayDispatchInvocationCount;

		if (handleSize == 0 || handleAlignment == 0 || baseAlignment == 0 || width > maxRays / height || static_cast<uint64>(width) * height > maxRays / depth)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanRayDispatch", "The ray dispatch exceeds device limits.");
		}

		auto align = [](uint64 value, uint64 alignment) noexcept { return (value + alignment - 1) / alignment * alignment; };
		uint32 requiredLocal[5]{};
		uint64 maximumLocalBytes = 0;
		NativeBindingSet allSets[101]{};
		uint32 allSetCount = 0;

		for (const rhi::NativeBindingSet& set : sets)
		{
			allSets[allSetCount++] = set;
		}

		auto countLocal = [&](const NativeBindingSet& local)
			{
				allSets[allSetCount++] = local;
				uint32 sampled = 0;

				for (const rhi::NativeComputeBuffer& binding : local.Buffers)
				{
					if (binding.Format != EFormat::UNKNOWN)
					{
						++requiredLocal[binding.Type == EComputeBindingType::ReadOnlyBuffer ? 3 : 4];
					}
				}

				for (const rhi::NativeComputeTexture& texture : local.Textures)
				{
					if (texture.Type == EComputeTextureBindingType::SampledTexture)
					{
						++requiredLocal[0];
						++requiredLocal[2];
						++sampled;
					}
					else
					{
						++requiredLocal[1];
					}
				}
				maximumLocalBytes =
					Max(maximumLocalBytes,
						static_cast<uint64>((local.Buffers.Size() + local.Textures.Size() + sampled) * sizeof(uint64) +
							local.LocalData.Size()));
			};
		countLocal(table.RayGenerationLocal);

		for (const rhi::NativeBindingSet& local : table.MissLocals)
		{
			countLocal(local);
		}

		for (const rhi::NativeBindingSet& local : table.HitLocals)
		{
			countLocal(local);
		}

		for (const rhi::NativeBindingSet& local : table.CallableLocals)
		{
			countLocal(local);
		}

		for (uint32 index = 0; index < 5; ++index)
		{
			ASSERT(requiredLocal[index] <= pipeline.LocalDescriptorCounts[index], "Local shader records exceed LocalDescriptorCapacity.");
		}
		VkDescriptorSet localSet = VK_NULL_HANDLE;

		if (pipeline.SetLayouts[4] != VK_NULL_HANDLE)
		{
			constexpr VkDescriptorType types[5]{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
												VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
												VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER };
			VkDescriptorPoolSize sizes[5]{};
			uint32 sizeCount = 0;

			for (uint32 index = 0; index < 5; ++index)
			{
				if (pipeline.LocalDescriptorCounts[index] != 0)
				{
					sizes[sizeCount++] = { types[index], pipeline.LocalDescriptorCounts[index] };
				}
			}
			VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			poolInfo.maxSets = 1;
			poolInfo.poolSizeCount = sizeCount;
			poolInfo.pPoolSizes = sizes;
			VkDescriptorPool pool = VK_NULL_HANDLE;

			if (state.F.CreateDescriptorPool(state.Device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanRayDispatch", "Could not create local ray descriptor pool."));
			}
			// A full pool is retained for this dispatch and never reused as a global pool.
			if (!pending.DescriptorPools.Add({ pool, 32 }))
			{
				state.F.DestroyDescriptorPool(state.Device, pool, nullptr);

				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain local ray descriptor pool."));
			}
			VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocation.descriptorPool = pool;
			allocation.descriptorSetCount = 1;
			allocation.pSetLayouts = &pipeline.SetLayouts[4];

			if (state.F.AllocateDescriptorSets(state.Device, &allocation, &localSet) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanRayDispatch", "Could not allocate local ray descriptors."));
			}
		}

		const uint64 stride = align(handleSize + maximumLocalBytes, handleAlignment);

		if (stride > properties.maxShaderGroupStride)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanRayDispatch", "The shader record exceeds the device stride limit.");
		}

		const uint64 missOffset = align(stride, baseAlignment);
		const uint64 hitOffset = align(missOffset + table.MissShaderIndices.Size() * stride, baseAlignment);
		const uint64 callableOffset = align(hitOffset + table.HitGroupIndices.Size() * stride, baseAlignment);
		const uint64 bytes = callableOffset + table.CallableShaderIndices.Size() * stride;
		Result<Buffer*> created = NewBuffer(state, bytes + baseAlignment, true, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

		if (!created)
		{
			return Result<void>::Failure(created.GetError());
		}
		Buffer* buffer = created.Value();
		VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		addressInfo.buffer = buffer->Handle;
		const VkDeviceAddress deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &addressInfo);

		if (deviceAddress == 0)
		{
			FreeBuffer(state, buffer);

			return Result<void>::Failure(Failure("Rhi.VulkanRayDispatch", "The shader table has no device address."));
		}

		const uint64 start = align(deviceAddress, baseAlignment) - deviceAddress;
		void* mapped = nullptr;

		if (state.F.MapMemory(state.Device, buffer->Memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
		{
			FreeBuffer(state, buffer);

			return Result<void>::Failure(Failure("Rhi.VulkanRayDispatch", "Could not map the shader table."));
		}

		SetBytes(static_cast<Byte*>(mapped) + start, 0, static_cast<usize>(bytes));
		uint32 usedLocal[5]{};
		auto copyGroup = [&](uint64 offset, uint32 group, const NativeBindingSet& local) -> bool
			{
				Byte* record = static_cast<Byte*>(mapped) + start + offset;
				CopyBytes(record, pipeline.GroupHandles.Data() + static_cast<usize>(group) * handleSize,
					static_cast<usize>(handleSize));
				auto writeArgument = [&](usize index, uint64 value)
					{ CopyBytes(record + handleSize + index * sizeof(value), &value, sizeof(value)); };
				for (usize index = 0; index < local.Buffers.Size(); ++index)
				{
					const rhi::NativeComputeBuffer& binding = local.Buffers[index];

					if (binding.Format == EFormat::UNKNOWN)
					{
						VkBufferDeviceAddressInfo address{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
						address.buffer = static_cast<const Buffer*>(binding.Buffer)->Handle;
						const VkDeviceAddress value = state.F.GetBufferDeviceAddress(state.Device, &address);

						if (value == 0)
						{
							return false;
						}
						writeArgument(index, value + binding.ByteOffset);
					}
					else
					{
						const uint32 kind = binding.Type == EComputeBindingType::ReadOnlyBuffer ? 3 : 4;
						const uint32 descriptor = usedLocal[kind]++;
						VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
						write.dstSet = localSet;
						write.dstBinding = kind;
						write.dstArrayElement = descriptor;
						write.descriptorCount = 1;
						write.descriptorType = kind == 3 ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
						write.pTexelBufferView = &static_cast<const BufferView*>(binding.View)->Handle;
						WriteCommandDescriptors(state, pending, 1, &write, 0, nullptr);
						writeArgument(index, descriptor);
					}
				}

				uint32 sampled = 0;

				for (usize index = 0; index < local.Textures.Size(); ++index)
				{
					const rhi::NativeComputeTexture& binding = local.Textures[index];
					Texture& texture = *static_cast<Texture*>(binding.Texture);
					const bool bStorage = binding.Type == EComputeTextureBindingType::StorageTexture;
					const uint32 kind = bStorage ? 1 : 0;
					const uint32 descriptor = usedLocal[kind]++;
					const TextureViewDesc* const viewRange = binding.View != nullptr ? &static_cast<const TextureView*>(binding.View)->Descriptor : nullptr;
					const VkImageLayout layout = bStorage ? VK_IMAGE_LAYOUT_GENERAL : SampledTextureLayout(pending, texture, viewRange);

					if (!TextureHasLayout(pending, texture, layout, viewRange))
					{
						if (viewRange != nullptr)
						{
							RequireTextureLayoutRange(state, pending, texture, layout, *viewRange);
						}
						else
						{
							RequireTextureLayout(state, pending, texture, layout);
						}
					}
					VkDescriptorImageInfo imageInfo{ VK_NULL_HANDLE,
													binding.View != nullptr ? static_cast<const TextureView*>(binding.View)->Handle
													: bStorage ? texture.GetStorageView()
																			: texture.View,
													layout };
					VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					write.dstSet = localSet;
					write.dstBinding = kind;
					write.dstArrayElement = descriptor;
					write.descriptorCount = 1;
					write.descriptorType = bStorage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					write.pImageInfo = &imageInfo;
					WriteCommandDescriptors(state, pending, 1, &write, 0, nullptr);
					writeArgument(local.Buffers.Size() + index, descriptor);

					if (bStorage)
					{
						continue;
					}

					const uint32 samplerIndex = usedLocal[2]++;
					imageInfo = { binding.Sampler != nullptr ? static_cast<const Sampler*>(binding.Sampler)->Handle
															: pipeline.LocalSamplers[group][index],
								 VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
					write.dstBinding = 2;
					write.dstArrayElement = samplerIndex;
					write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
					WriteCommandDescriptors(state, pending, 1, &write, 0, nullptr);
					writeArgument(local.Buffers.Size() + local.Textures.Size() + sampled++, samplerIndex);
				}

				if (!local.LocalData.IsEmpty())
				{
					CopyBytes(record + handleSize + (local.Buffers.Size() + local.Textures.Size() + sampled) * sizeof(uint64),
						local.LocalData.Data(), local.LocalData.Size());
				}

				return true;
			};
		bool bCopied = copyGroup(0, table.RayGenerationShaderIndex, table.RayGenerationLocal);

		for (usize index = 0; bCopied && index < table.MissShaderIndices.Size(); ++index)
		{
			bCopied = copyGroup(missOffset + index * stride, table.MissShaderIndices[index], table.MissLocals[index]);
		}

		for (usize index = 0; bCopied && index < table.HitGroupIndices.Size(); ++index)
		{
			bCopied = copyGroup(hitOffset + index * stride, table.GeneralShaderCount + table.HitGroupIndices[index], table.HitLocals[index]);
		}

		for (usize index = 0; bCopied && index < table.CallableShaderIndices.Size(); ++index)
		{
			bCopied = copyGroup(callableOffset + index * stride, table.CallableShaderIndices[index], table.CallableLocals[index]);
		}

		if (!bCopied)
		{
			state.F.UnmapMemory(state.Device, buffer->Memory);
			FreeBuffer(state, buffer);

			return Result<void>::Failure(Failure("Rhi.VulkanRayDispatch", "A local buffer has no device address."));
		}
		VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.memory = buffer->Memory;
		range.size = VK_WHOLE_SIZE;
		const bool bFlushed = buffer->Coherent || state.F.FlushMappedMemoryRanges(state.Device, 1, &range) == VK_SUCCESS;
		state.F.UnmapMemory(state.Device, buffer->Memory);

		if (!bFlushed)
		{
			FreeBuffer(state, buffer);

			return Result<void>::Failure(Failure("Rhi.VulkanRayDispatch", "Could not flush the shader table."));
		}

		if (!pending.Temporary.Add(buffer))
		{
			FreeBuffer(state, buffer);

			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain the shader table."));
		}

		const VkDeviceAddress base = deviceAddress + start;
		const VkStridedDeviceAddressRegionKHR raygen{ base, stride, stride };
		const VkStridedDeviceAddressRegionKHR miss =
			table.MissShaderIndices.IsEmpty()
			? VkStridedDeviceAddressRegionKHR{}
		: VkStridedDeviceAddressRegionKHR{ base + missOffset, stride, table.MissShaderIndices.Size() * stride };
		const VkStridedDeviceAddressRegionKHR hit =
			table.HitGroupIndices.IsEmpty()
			? VkStridedDeviceAddressRegionKHR{}
		: VkStridedDeviceAddressRegionKHR{ base + hitOffset, stride, table.HitGroupIndices.Size() * stride };
		const VkStridedDeviceAddressRegionKHR callable =
			table.CallableShaderIndices.IsEmpty()
			? VkStridedDeviceAddressRegionKHR{}
		: VkStridedDeviceAddressRegionKHR{ base + callableOffset, stride, table.CallableShaderIndices.Size() * stride };
		for (uint32 layoutIndex = 0; fixedSets.IsEmpty() && layoutIndex < pipeline.SetLayoutCount; ++layoutIndex)
		{
			const NativeBindingSet* const bindings = layoutIndex < sets.Size() ? &sets[layoutIndex] : nullptr;
			const Span<const NativeComputeBuffer> buffers =
				bindings != nullptr ? bindings->Buffers : Span<const NativeComputeBuffer>{};
			const Span<const NativeComputeTexture> textures =
				bindings != nullptr ? bindings->Textures : Span<const NativeComputeTexture>{};
			const bool bBindAccel = layoutIndex == 0 && accelNative != nullptr;
			constexpr uint32 SETS_PER_POOL = 32;

			if (pending.DescriptorPools.IsEmpty() || pending.DescriptorPools.Back().UsedSets == SETS_PER_POOL || !pending.DescriptorPools.Back().bAccelerationStructures)
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
				poolInfo.poolSizeCount = 8;
				poolInfo.pPoolSizes = poolSizes;
				VkDescriptorPool pool = VK_NULL_HANDLE;

				if (state.F.CreateDescriptorPool(state.Device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
				{
					return Result<void>::Failure(Failure("Rhi.VulkanRayDispatch", "Could not allocate global ray descriptors."));
				}

				if (!pending.DescriptorPools.Add({ pool, 0, true }))
				{
					state.F.DestroyDescriptorPool(state.Device, pool, nullptr);

					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain global ray descriptors."));
				}
			}
			VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocation.descriptorPool = pending.DescriptorPools.Back().Handle;
			allocation.descriptorSetCount = 1;
			allocation.pSetLayouts = &pipeline.SetLayouts[layoutIndex];
			VkDescriptorSet set = VK_NULL_HANDLE;

			if (state.F.AllocateDescriptorSets(state.Device, &allocation, &set) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanRayDispatch", "Could not allocate a global ray set."));
			}
			++pending.DescriptorPools.Back().UsedSets;
			VkDescriptorBufferInfo infos[8]{};
			VkDescriptorImageInfo imageInfos[8]{};
			VkDescriptorImageInfo samplerInfos[8]{};
			VkWriteDescriptorSet writes[25]{};
			for (usize index = 0; index < buffers.Size(); ++index)
			{
				infos[index] = { static_cast<const Buffer*>(buffers[index].Buffer)->Handle, buffers[index].ByteOffset,
								buffers[index].ByteCount };
				writes[index] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				writes[index].dstSet = set;
				writes[index].dstBinding = buffers[index].Slot;
				writes[index].descriptorCount = 1;
				writes[index].descriptorType = BufferDescriptorType(buffers[index].Type, buffers[index].Format);
				writes[index].pBufferInfo = &infos[index];

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
				const TextureViewDesc* const viewRange = textures[index].View != nullptr ? &static_cast<const TextureView*>(textures[index].View)->Descriptor : nullptr;
				imageInfos[index] = { VK_NULL_HANDLE,
									 textures[index].View != nullptr ? static_cast<const TextureView*>(textures[index].View)->Handle
																	 : (bStorage ? texture.GetStorageView() : texture.View),
									 bStorage ? VK_IMAGE_LAYOUT_GENERAL : SampledTextureLayout(pending, texture, viewRange) };
				VkWriteDescriptorSet& write = writes[buffers.Size() + index];
				write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.dstSet = set;
				write.dstBinding = (bStorage ? 64u : 32u) + pipeline.TextureSlots[layoutIndex][index];
				write.descriptorCount = 1;
				write.descriptorType = bStorage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				write.pImageInfo = &imageInfos[index];

				if (!bStorage)
				{
					samplerInfos[samplerWrites] = { textures[index].Sampler != nullptr
													   ? static_cast<const Sampler*>(textures[index].Sampler)->Handle
													   : pipeline.Samplers[layoutIndex][index],
												   VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
					VkWriteDescriptorSet& samplerWrite = writes[buffers.Size() + textures.Size() + samplerWrites];
					samplerWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					samplerWrite.dstSet = set;
					samplerWrite.dstBinding = 48 + pipeline.TextureSlots[layoutIndex][index];
					samplerWrite.descriptorCount = 1;
					samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
					samplerWrite.pImageInfo = &samplerInfos[samplerWrites];
					++samplerWrites;
				}
			}

			const uint32 writeCount = static_cast<uint32>(buffers.Size() + textures.Size()) + samplerWrites;
			VkAccelerationStructureKHR accelHandle = accelNative != nullptr ? static_cast<const NativeAccelStruct*>(accelNative)->Handle : VK_NULL_HANDLE;
			VkWriteDescriptorSetAccelerationStructureKHR accelInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
			if (bBindAccel)
			{
				accelInfo.accelerationStructureCount = 1;
				accelInfo.pAccelerationStructures = &accelHandle;
				VkWriteDescriptorSet& write = writes[writeCount];
				write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				write.pNext = &accelInfo;
				write.dstSet = set;
				write.dstBinding = 80 + pipeline.AccelStructSlot;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			}
			WriteCommandDescriptors(state, pending, writeCount + (bBindAccel ? 1u : 0u), writes, 0, nullptr);
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.Layout, layoutIndex, 1, &set,
				0, nullptr);

			for (const NativeComputeTexture& binding : textures)
			{
				Texture& texture = *static_cast<Texture*>(binding.Texture);
				const TextureViewDesc* const viewRange = binding.View != nullptr ? &static_cast<const TextureView*>(binding.View)->Descriptor : nullptr;
				const VkImageLayout layout = binding.Type == EComputeTextureBindingType::StorageTexture ? VK_IMAGE_LAYOUT_GENERAL : SampledTextureLayout(pending, texture, viewRange);

				if (!TextureHasLayout(pending, texture, layout, viewRange))
				{
					if (viewRange != nullptr)
					{
						RequireTextureLayoutRange(state, pending, texture, layout, *viewRange);
					}
					else
					{
						RequireTextureLayout(state, pending, texture, layout);
					}
				}
			}
		}

		if (pending.bAutomaticBarriers)
		{
			Result<void> barriers = RecordRayResourceBarriers(state, pending, { allSets, allSetCount }, true);
			if (!barriers)
			{
				return barriers;
			}

			if (accelNative != nullptr || !fixedSets.IsEmpty())
			{
				VkMemoryBarrier accel{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
				accel.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_MEMORY_WRITE_BIT;
				accel.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
				RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1,
					&accel, 0, nullptr, 0, nullptr);
			}
		}

		if (!fixedSets.IsEmpty())
		{
			Result<void> bound = WriteFixedBindingSets(opaque, command, native, false, fixedSets, nullptr, {}, &pipeline);
			if (!bound)
			{
				return bound;
			}
		}

		if (localSet != VK_NULL_HANDLE)
		{
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.Layout, 4, 1, &localSet, 0,
				nullptr);
		}
		state.F.CmdBindPipeline(pending.Commands, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.Handle);

		if (pipeline.PushConstantBytes != 0)
		{
			state.F.CmdPushConstants(pending.Commands, pipeline.Layout,
				VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
				VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
				VK_SHADER_STAGE_CALLABLE_BIT_KHR,
				0, pipeline.PushConstantBytes, pushConstants.Data());
		}
		state.F.CmdTraceRays(pending.Commands, &raygen, &miss, &hit, &callable, width, height, depth);

		if (pending.bAutomaticBarriers && !fixedSets.IsEmpty())
		{
			VkMemoryBarrier fixedWrites{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			fixedWrites.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			fixedWrites.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			RecordBarriers(state, pending, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &fixedWrites, 0, nullptr, 0, nullptr);
		}

		if (pending.bAutomaticBarriers)
		{
			Result<void> barriers = RecordRayResourceBarriers(state, pending, { allSets, allSetCount }, false);
			if (!barriers)
			{
				return barriers;
			}
		}

		return Result<void>::Success();
	}
}
