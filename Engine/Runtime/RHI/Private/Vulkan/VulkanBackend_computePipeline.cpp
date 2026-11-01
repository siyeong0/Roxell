#include "VulkanBackend.h"
#include "VulkanDescriptors.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<void> CreateComputeFixedLayout(State& state, ComputePipeline& pipeline, uint32 layoutIndex, const NativeBindingLayoutDesc& layout, uint32 accelSlot)
	{
		const uint32 bufferCount = layout.BufferCount();
		const uint32 textureCount = layout.TextureCount();
		VkDescriptorSetLayoutBinding bindings[25]{};
		uint32 samplerCount = 0;

		for (usize index = 0; index < bufferCount; ++index)
		{
			bindings[index].binding = layout.Buffer(static_cast<uint32>(index)).Slot;
			bindings[index].descriptorType = BufferDescriptorType(layout.Buffer(static_cast<uint32>(index)).Type, layout.Buffer(static_cast<uint32>(index)).Format);
			bindings[index].descriptorCount = 1;
			bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		}

		for (usize index = 0; index < textureCount; ++index)
		{
			pipeline.TextureSlots[layoutIndex][index] = layout.Texture(static_cast<uint32>(index)).Slot;
			VkDescriptorSetLayoutBinding& binding = bindings[bufferCount + index];
			binding.binding = (layout.Texture(static_cast<uint32>(index)).Type == EComputeTextureBindingType::StorageTexture ? 64u : 32u) + layout.Texture(static_cast<uint32>(index)).Slot;
			binding.descriptorType = layout.Texture(static_cast<uint32>(index)).Type == EComputeTextureBindingType::SampledTexture
				? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
				: VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			binding.descriptorCount = 1;
			binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			if (layout.Texture(static_cast<uint32>(index)).Type != EComputeTextureBindingType::SampledTexture)
			{
				continue;
			}
			VkSamplerReductionModeCreateInfo reduction{};
			const VkSamplerCreateInfo samplerInfo = MakeSamplerCreateInfo(layout.Texture(static_cast<uint32>(index)).Sampler, reduction);

			if (state.F.CreateSampler(state.Device, &samplerInfo, nullptr, &pipeline.Samplers[layoutIndex][index]) != VK_SUCCESS)
			{
				return Result<void>::Failure(Failure("Rhi.VulkanSampler", "Could not create a compute sampler."));
			}

			VkDescriptorSetLayoutBinding& samplerBinding = bindings[bufferCount + textureCount + samplerCount++];
			samplerBinding.binding = 48 + layout.Texture(static_cast<uint32>(index)).Slot;
			samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
			samplerBinding.descriptorCount = 1;
			samplerBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		}
		VkDescriptorSetLayoutCreateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setInfo.bindingCount = static_cast<uint32>(bufferCount + textureCount) + samplerCount;

		if (layoutIndex == 0 && accelSlot != 0xFFFFFFFFu)
		{
			VkDescriptorSetLayoutBinding& binding = bindings[setInfo.bindingCount++];
			binding.binding = 80 + accelSlot;
			binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			binding.descriptorCount = 1;
			binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		}
		setInfo.pBindings = bindings;

		if (state.F.CreateDescriptorSetLayout(state.Device, &setInfo, nullptr, &pipeline.SetLayouts[layoutIndex]) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanCompute", "Could not create a compute descriptor set layout."));
		}

		return Result<void>::Success();
	}

	void FreePipeline(State& state, ComputePipeline* pipeline) noexcept
	{
		if (pipeline == nullptr)
		{
			return;
		}

		if (pipeline->Handle)
		{
			state.F.DestroyPipeline(state.Device, pipeline->Handle, nullptr);
		}

		if (pipeline->Layout)
		{
			state.F.DestroyPipelineLayout(state.Device, pipeline->Layout, nullptr);
		}

		for (VkDescriptorSetLayout layout : pipeline->SetLayouts)
		{
			if (layout)
			{
				state.F.DestroyDescriptorSetLayout(state.Device, layout, nullptr);
			}
		}

		if (pipeline->BindlessSetLayout)
		{
			state.F.DestroyDescriptorSetLayout(state.Device, pipeline->BindlessSetLayout, nullptr);
		}

		if (pipeline->BindlessSamplerSetLayout)
		{
			state.F.DestroyDescriptorSetLayout(state.Device, pipeline->BindlessSamplerSetLayout, nullptr);
		}

		if (pipeline->BindlessStorageBufferSetLayout)
		{
			state.F.DestroyDescriptorSetLayout(state.Device, pipeline->BindlessStorageBufferSetLayout, nullptr);
		}

		if (pipeline->BindlessStorageTextureSetLayout)
		{
			state.F.DestroyDescriptorSetLayout(state.Device, pipeline->BindlessStorageTextureSetLayout, nullptr);
		}

		if (pipeline->BindlessUniformBufferSetLayout)
		{
			state.F.DestroyDescriptorSetLayout(state.Device, pipeline->BindlessUniformBufferSetLayout, nullptr);
		}

		if (pipeline->BindlessReadOnlyBufferSetLayout)
		{
			state.F.DestroyDescriptorSetLayout(state.Device, pipeline->BindlessReadOnlyBufferSetLayout, nullptr);
		}

		for (const VkSampler(&layoutSamplers)[8] : pipeline->Samplers)
		{
			for (VkSampler sampler : layoutSamplers)
			{
				if (sampler)
				{
					state.F.DestroySampler(state.Device, sampler, nullptr);
				}
			}
		}

		DestroyObject(state.Resource, pipeline);
	}

	[[nodiscard]] Result<void*> CreateComputePipeline(State* opaque, const NativeComputePipelineDesc& desc, Span<const NativeBindingLayoutDesc> layouts)
	{
		ASSERT(!layouts.IsEmpty() && layouts.Size() <= MAXIMUM_FIXED_BINDING_LAYOUTS, "A compute pipeline has one to eight native layouts.");
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		if (desc.PushConstantBytes > state.DeviceProperties.limits.maxPushConstantsSize)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCompute", "The push constant range exceeds the device limit.");
		}

		if (desc.BindlessSampledTextureCapacity > state.MaximumBindlessSampledTextures)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCompute", "The sampled descriptor table exceeds the device limit.");
		}

		if (desc.BindlessStorageBufferCapacity > state.MaximumBindlessStorageBuffers)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCompute", "The storage buffer table exceeds the device limit.");
		}

		if (desc.BindlessStorageTextureCapacity > state.MaximumBindlessStorageTextures)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCompute", "The storage texture table exceeds the device limit.");
		}

		if (desc.BindlessUniformBufferCapacity > state.MaximumBindlessUniformBuffers)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCompute", "The uniform buffer table exceeds the device limit.");
		}

		for (const NativeBindingLayoutDesc& layout : layouts)
		{
			for (uint32 index = 0; index < layout.TextureCount(); ++index)
			{
				const ComputeTextureBindingDesc binding = layout.Texture(index);
				if (binding.Type == EComputeTextureBindingType::SampledTexture && !SupportsSampler(state, binding.Sampler))
				{
					return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanSampler", "The sampler exceeds the device anisotropy or mip bias limit.");
				}
			}
		}

		Result<void> typedTables = ValidateTypedTableLimits(
			state, desc.BindlessStorageBufferCapacity, desc.BindlessStorageBufferFormat, desc.BindlessReadOnlyBufferCapacity,
			desc.BindlessReadOnlyBufferFormat, desc.BindlessSampledTextureCapacity, desc.BindlessStorageTextureCapacity);

		if (!typedTables)
		{
			return Result<void*>::Failure(typedTables.GetError());
		}

		uint32 requiredSets = static_cast<uint32>(layouts.Size()) + (desc.BindlessReadOnlyBufferCapacity != 0 ? 6u
			: desc.BindlessUniformBufferCapacity != 0 ? 5u
			: desc.BindlessStorageTextureCapacity != 0 ? 4u
			: desc.BindlessStorageBufferCapacity != 0 ? 3u
			: desc.BindlessSamplerCapacity != 0 ? 2u
			: desc.BindlessSampledTextureCapacity != 0 ? 1u
			: 0u);

		if (layouts[0].bRangeLayout)
		{
			requiredSets = 0;
			for (const rhi::NativeBindingLayoutDesc& source : layouts)
			{
				requiredSets = Max(requiredSets, source.Space + 1);
			}
		}
		if (requiredSets > state.DeviceProperties.limits.maxBoundDescriptorSets)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCompute", "The compute descriptor sets exceed the device limit.");
		}

		if (desc.ShaderBytecode.Size() % sizeof(uint32) != 0)
		{
			return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.VulkanShader", "SPIR-V bytecode must contain whole words.");
		}

		Array<uint32> words(state.Resource);

		if (!words.ResizeUninitialized(desc.ShaderBytecode.Size() / sizeof(uint32)))
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not align shader bytecode."));
		}

		CopyBytes(words.Data(), desc.ShaderBytecode.Data(), desc.ShaderBytecode.Size());
		VkShaderModuleCreateInfo shaderInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		shaderInfo.codeSize = desc.ShaderBytecode.Size();
		shaderInfo.pCode = words.Data();
		VkShaderModule shader = VK_NULL_HANDLE;

		if (state.F.CreateShaderModule(state.Device, &shaderInfo, nullptr, &shader) != VK_SUCCESS)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanShader", "Could not create a SPIR-V shader module."));
		}
		ComputePipeline* pipeline = CreateObject<ComputePipeline>(state.Resource);

		if (pipeline == nullptr)
		{
			state.F.DestroyShaderModule(state.Device, shader, nullptr);

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a Vulkan compute pipeline."));
		}
		pipeline->PushConstantBytes = desc.PushConstantBytes;
		pipeline->AccelStructSlot = desc.AccelStructSlot;
		VkDescriptorSetLayout setLayouts[10]{};
		if (layouts[0].bRangeLayout)
		{
			Result<void> created = CreateRangeLayouts(state, *pipeline, layouts, false);
			if (!created)
			{
				state.F.DestroyShaderModule(state.Device, shader, nullptr);
				FreePipeline(state, pipeline);

				return Result<void*>::Failure(created.GetError());
			}
			for (uint32 space = 0; space < pipeline->SetLayoutCount; ++space)
			{
				setLayouts[space] = pipeline->SetLayouts[space];
			}
		}
		else
		{
			for (uint32 index = 0; index < layouts.Size(); ++index)
			{
				Result<void> created = CreateComputeFixedLayout(state, *pipeline, index, layouts[index], desc.AccelStructSlot);

				if (!created)
				{
					state.F.DestroyShaderModule(state.Device, shader, nullptr);
					FreePipeline(state, pipeline);

					return Result<void*>::Failure(created.GetError());
				}
				setLayouts[index] = pipeline->SetLayouts[index];
			}
			pipeline->SetLayoutCount = static_cast<uint32>(layouts.Size());
		}

		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.setLayoutCount = requiredSets;
		layoutInfo.pSetLayouts = setLayouts;
		VkPushConstantRange pushRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, desc.PushConstantBytes };
		if (desc.PushConstantBytes != 0)
		{
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &pushRange;
		}
		VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stage.module = shader;
		stage.pName = desc.EntryPoint;
		VkSpecializationMapEntry specializationEntries[64]{};
		uint32 specializationData[64]{};
		VkSpecializationInfo specializationInfo{};
		if (!desc.Specializations.IsEmpty())
		{
			FillSpecializations(desc.Specializations, specializationEntries, specializationData, specializationInfo);
			stage.pSpecializationInfo = &specializationInfo;
		}
		VkComputePipelineCreateInfo nativeDesc{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		nativeDesc.stage = stage;
		nativeDesc.layout = pipeline->Layout;
		bool bCreated = true;

		if (bCreated &&
			(desc.BindlessSampledTextureCapacity != 0 ||
			 desc.BindlessSamplerCapacity != 0 ||
			 desc.BindlessStorageBufferCapacity != 0 ||
			 desc.BindlessStorageTextureCapacity != 0 ||
			 desc.BindlessUniformBufferCapacity != 0 ||
			 desc.BindlessReadOnlyBufferCapacity != 0))
		{
			if (desc.BindlessSampledTextureCapacity != 0)
			{
				bCreated = CreateSampledDescriptorSetLayout(state, desc.BindlessSampledTextureCapacity, pipeline->BindlessSetLayout);
			}
			else
			{
				VkDescriptorSetLayoutCreateInfo empty{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				bCreated = state.F.CreateDescriptorSetLayout(state.Device, &empty, nullptr, &pipeline->BindlessSetLayout) == VK_SUCCESS;
			}
			setLayouts[layouts.Size()] = pipeline->BindlessSetLayout;
		}

		if (bCreated &&
			(desc.BindlessSamplerCapacity != 0 ||
			 desc.BindlessStorageBufferCapacity != 0 ||
			 desc.BindlessStorageTextureCapacity != 0 ||
			 desc.BindlessUniformBufferCapacity != 0 ||
			 desc.BindlessReadOnlyBufferCapacity != 0))
		{
			if (desc.BindlessSamplerCapacity != 0)
			{
				bCreated = CreateSamplerDescriptorSetLayout(state, desc.BindlessSamplerCapacity, pipeline->BindlessSamplerSetLayout);
			}
			else
			{
				VkDescriptorSetLayoutCreateInfo empty{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				bCreated = state.F.CreateDescriptorSetLayout(state.Device, &empty, nullptr, &pipeline->BindlessSamplerSetLayout) == VK_SUCCESS;
			}
			setLayouts[layouts.Size() + 1] = pipeline->BindlessSamplerSetLayout;
		}

		if (bCreated && (desc.BindlessStorageBufferCapacity != 0 || desc.BindlessStorageTextureCapacity != 0 || desc.BindlessUniformBufferCapacity != 0 || desc.BindlessReadOnlyBufferCapacity != 0))
		{
			if (desc.BindlessStorageBufferCapacity != 0)
			{
				bCreated = CreateStorageBufferDescriptorSetLayout( state, desc.BindlessStorageBufferCapacity, pipeline->BindlessStorageBufferSetLayout, desc.BindlessStorageBufferFormat);
			}
			else
			{
				VkDescriptorSetLayoutCreateInfo empty{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				bCreated = state.F.CreateDescriptorSetLayout(state.Device, &empty, nullptr, &pipeline->BindlessStorageBufferSetLayout) == VK_SUCCESS;
			}
			setLayouts[layouts.Size() + 2] = pipeline->BindlessStorageBufferSetLayout;
		}

		if (bCreated && (desc.BindlessStorageTextureCapacity != 0 || desc.BindlessUniformBufferCapacity != 0 || desc.BindlessReadOnlyBufferCapacity != 0))
		{
			if (desc.BindlessStorageTextureCapacity != 0)
			{
				bCreated = CreateStorageTextureDescriptorSetLayout(state, desc.BindlessStorageTextureCapacity, pipeline->BindlessStorageTextureSetLayout);
			}
			else
			{
				VkDescriptorSetLayoutCreateInfo empty{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				bCreated = state.F.CreateDescriptorSetLayout(state.Device, &empty, nullptr, &pipeline->BindlessStorageTextureSetLayout) == VK_SUCCESS;
			}
			setLayouts[layouts.Size() + 3] = pipeline->BindlessStorageTextureSetLayout;
		}

		if (bCreated && (desc.BindlessUniformBufferCapacity != 0 || desc.BindlessReadOnlyBufferCapacity != 0))
		{
			if (desc.BindlessUniformBufferCapacity != 0)
			{
				bCreated = CreateUniformBufferDescriptorSetLayout(state, desc.BindlessUniformBufferCapacity, pipeline->BindlessUniformBufferSetLayout);
			}
			else
			{
				VkDescriptorSetLayoutCreateInfo empty{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				bCreated = state.F.CreateDescriptorSetLayout(state.Device, &empty, nullptr, &pipeline->BindlessUniformBufferSetLayout) == VK_SUCCESS;
			}
			setLayouts[layouts.Size() + 4] = pipeline->BindlessUniformBufferSetLayout;
		}

		if (bCreated && desc.BindlessReadOnlyBufferCapacity != 0)
		{
			bCreated = CreateBindlessDescriptorSetLayout(state, EDescriptorType::ReadOnlyBuffer, desc.BindlessReadOnlyBufferCapacity,
				pipeline->BindlessReadOnlyBufferSetLayout, desc.BindlessReadOnlyBufferFormat);
			setLayouts[layouts.Size() + 5] = pipeline->BindlessReadOnlyBufferSetLayout;
		}

		if (bCreated)
		{
			bCreated = state.F.CreatePipelineLayout(state.Device, &layoutInfo, nullptr, &pipeline->Layout) == VK_SUCCESS;
		}

		if (bCreated)
		{
			nativeDesc.layout = pipeline->Layout;
			bCreated = state.F.CreateComputePipelines(state.Device, VK_NULL_HANDLE, 1, &nativeDesc, nullptr, &pipeline->Handle) == VK_SUCCESS;
		}
		state.F.DestroyShaderModule(state.Device, shader, nullptr);

		if (!bCreated)
		{
			FreePipeline(state, pipeline);

			return Result<void*>::Failure(Failure("Rhi.VulkanCompute", "Could not create the compute pipeline."));
		}

		return Result<void*>::Success(pipeline);
	}

	void DestroyComputePipelineNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		FreePipeline(state, static_cast<ComputePipeline*>(native));
	}

	[[nodiscard]] Result<void> DestroyComputePipeline(State* opaque, void* native)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		ComputePipeline& pipeline = *static_cast<ComputePipeline*>(native);
		uint64 complete = 0;

		if (state.F.GetSemaphoreCounterValue(state.Device, state.Timeline, &complete) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanTimeline", "Could not inspect Vulkan completion."));
		}

		if (pipeline.LastUse > complete)
		{
			if (!state.RetiredPipelines.Add({ &pipeline, pipeline.LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retire the compute pipeline."));
			}
		}
		else
		{
			FreePipeline(state, &pipeline);
		}

		return Result<void>::Success();
	}
}
