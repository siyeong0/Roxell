#include "VulkanBackend.h"
#include "VulkanDescriptors.h"

namespace rox::vulkan
{
	using namespace core;

	void FreeGraphicsPipeline(State& state, GraphicsPipeline* pipeline) noexcept
	{
		if (pipeline == nullptr)
		{
			return;
		}

		if (pipeline->Handle)
		{
			state.F.DestroyPipeline(state.Device, pipeline->Handle, nullptr);
		}

		if (pipeline->ShadingRateHandle)
		{
			state.F.DestroyPipeline(state.Device, pipeline->ShadingRateHandle, nullptr);
		}

		if (pipeline->RenderPass)
		{
			state.F.DestroyRenderPass(state.Device, pipeline->RenderPass, nullptr);
		}

		if (pipeline->ShadingRateRenderPass)
		{
			state.F.DestroyRenderPass(state.Device, pipeline->ShadingRateRenderPass, nullptr);
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

		if (pipeline->EmptyStorageBufferSetLayout)
		{
			state.F.DestroyDescriptorSetLayout(state.Device, pipeline->EmptyStorageBufferSetLayout, nullptr);
		}

		if (pipeline->BindlessStorageBufferSetLayout)
		{
			state.F.DestroyDescriptorSetLayout(state.Device, pipeline->BindlessStorageBufferSetLayout, nullptr);
		}

		if (pipeline->EmptyStorageTextureSetLayout)
		{
			state.F.DestroyDescriptorSetLayout(state.Device, pipeline->EmptyStorageTextureSetLayout, nullptr);
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

	[[nodiscard]] Result<void*> CreateGraphicsPipeline(State* opaque, const NativeGraphicsPipelineDesc& desc, Span<const NativeGraphicsBindingLayoutDesc> layouts)
	{
		ASSERT(!layouts.IsEmpty() && layouts.Size() <= MAXIMUM_FIXED_BINDING_LAYOUTS, "The graphics layout count is invalid.");

		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Result<void> typedTables = ValidateTypedTableLimits(
			state, desc.BindlessStorageBufferCapacity, desc.BindlessStorageBufferFormat, desc.BindlessReadOnlyBufferCapacity,
			desc.BindlessReadOnlyBufferFormat, desc.BindlessSampledTextureCapacity, desc.BindlessStorageTextureCapacity);

		if (!typedTables)
		{
			return Result<void*>::Failure(typedTables.GetError());
		}

		const uint32 requiredSets = static_cast<uint32>(layouts.Size()) + (desc.BindlessReadOnlyBufferCapacity != 0 ? 6u
			: desc.BindlessUniformBufferCapacity != 0 ? 5u
			: desc.BindlessStorageTextureCapacity != 0 ? 4u
			: desc.BindlessStorageBufferCapacity != 0 ? 3u
			: desc.BindlessSamplerCapacity != 0 ? 2u
			: desc.BindlessSampledTextureCapacity != 0 ? 1u
			: 0u);

		if (requiredSets > state.DeviceProperties.limits.maxBoundDescriptorSets)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanGraphics", "The graphics descriptor sets exceed the device limit.");
		}

		if (desc.Topology == EPrimitiveTopology::PatchList && desc.PatchControlPoints > state.DeviceProperties.limits.maxTessellationPatchSize)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.Tessellation", "The patch exceeds the Vulkan device limit.");
		}

		const VkSampleCountFlags samples = desc.SampleCount;

		if ((desc.ColorFormat != EFormat::UNKNOWN && (state.DeviceProperties.limits.framebufferColorSampleCounts & samples) == 0) ||
			(desc.DepthFormat != EFormat::UNKNOWN && (state.DeviceProperties.limits.framebufferDepthSampleCounts & samples) == 0))
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.Multisample", "The Vulkan framebuffer sample count is unsupported.");
		}

		for (const rhi::NativeGraphicsBindingLayoutDesc& layout : layouts)
		{
			for (uint32 index = 0; index < layout.BufferCount(); ++index)
			{
				const GraphicsBufferBindingDesc binding = layout.Buffer(index);
				if (binding.Type == EGraphicsBufferBindingType::StorageBuffer && !state.bGraphicsStorageWrites)
				{
					return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsBinding", "The device cannot write storage buffers in graphics shaders.");
				}
			}
		}

		if (desc.BindlessStorageBufferCapacity != 0 && !state.bGraphicsStorageWrites)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsBinding", "The device cannot write storage buffers in graphics shaders.");
		}

		if (desc.BindlessStorageTextureCapacity != 0 && !state.bGraphicsStorageWrites)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsBinding", "The device cannot write storage textures in graphics shaders.");
		}
		bool bHasStorageTexture = false;

		for (const rhi::NativeGraphicsBindingLayoutDesc& layout : layouts)
		{
			bHasStorageTexture |= layout.StorageCount() != 0;
		}

		if (bHasStorageTexture && !state.bGraphicsStorageWrites)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsBinding", "The device cannot write fixed storage textures in graphics shaders.");
		}

		for (const rhi::NativeGraphicsBindingLayoutDesc& layout : layouts)
		{
			for (uint32 index = 0; index < layout.SampledCount(); ++index)
			{
				const GraphicsTextureBindingDesc binding = layout.Sampled(index);
				if (!SupportsSampler(state, binding.Sampler))
				{
					return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanSampler", "The sampler exceeds the device anisotropy or mip bias limit.");
				}
			}
		}

		if (desc.VertexShaderBytecode.Size() % 4 != 0 ||
			desc.MeshShaderBytecode.Size() % 4 != 0 ||
			desc.TaskShaderBytecode.Size() % 4 != 0 ||
			desc.FragmentShaderBytecode.Size() % 4 != 0 ||
			desc.GeometryShaderBytecode.Size() % 4 != 0 ||
			desc.HullShaderBytecode.Size() % 4 != 0 ||
			desc.DomainShaderBytecode.Size() % 4 != 0)
		{
			return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.VulkanShader", "SPIR-V bytecode must contain whole words.");
		}

		Array<uint32> vertexWords(state.Resource);
		Array<uint32> fragmentWords(state.Resource);
		Array<uint32> geometryWords(state.Resource);
		Array<uint32> hullWords(state.Resource);
		Array<uint32> domainWords(state.Resource);
		Array<uint32> meshWords(state.Resource);
		Array<uint32> taskWords(state.Resource);

		if (!vertexWords.ResizeUninitialized(desc.VertexShaderBytecode.Size() / 4) ||
			!fragmentWords.ResizeUninitialized(desc.FragmentShaderBytecode.Size() / 4) ||
			!geometryWords.ResizeUninitialized(desc.GeometryShaderBytecode.Size() / 4) ||
			!hullWords.ResizeUninitialized(desc.HullShaderBytecode.Size() / 4) ||
			!domainWords.ResizeUninitialized(desc.DomainShaderBytecode.Size() / 4) ||
			!meshWords.ResizeUninitialized(desc.MeshShaderBytecode.Size() / 4) ||
			!taskWords.ResizeUninitialized(desc.TaskShaderBytecode.Size() / 4))
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not align graphics shader bytecode."));
		}

		if (!desc.VertexShaderBytecode.IsEmpty())
		{
			CopyBytes(vertexWords.Data(), desc.VertexShaderBytecode.Data(), desc.VertexShaderBytecode.Size());
		}

		if (!desc.MeshShaderBytecode.IsEmpty())
		{
			CopyBytes(meshWords.Data(), desc.MeshShaderBytecode.Data(), desc.MeshShaderBytecode.Size());
		}

		if (!desc.TaskShaderBytecode.IsEmpty())
		{
			CopyBytes(taskWords.Data(), desc.TaskShaderBytecode.Data(), desc.TaskShaderBytecode.Size());
		}

		if (!desc.FragmentShaderBytecode.IsEmpty())
		{
			CopyBytes(fragmentWords.Data(), desc.FragmentShaderBytecode.Data(), desc.FragmentShaderBytecode.Size());
		}

		if (!desc.GeometryShaderBytecode.IsEmpty())
		{
			CopyBytes(geometryWords.Data(), desc.GeometryShaderBytecode.Data(), desc.GeometryShaderBytecode.Size());
		}

		if (!desc.HullShaderBytecode.IsEmpty())
		{
			CopyBytes(hullWords.Data(), desc.HullShaderBytecode.Data(), desc.HullShaderBytecode.Size());
		}

		if (!desc.DomainShaderBytecode.IsEmpty())
		{
			CopyBytes(domainWords.Data(), desc.DomainShaderBytecode.Data(), desc.DomainShaderBytecode.Size());
		}
		VkShaderModuleCreateInfo vertexInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		vertexInfo.codeSize = desc.VertexShaderBytecode.Size();
		vertexInfo.pCode = vertexWords.Data();
		VkShaderModuleCreateInfo fragmentInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		fragmentInfo.codeSize = desc.FragmentShaderBytecode.Size();
		fragmentInfo.pCode = fragmentWords.Data();
		VkShaderModuleCreateInfo geometryInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		geometryInfo.codeSize = desc.GeometryShaderBytecode.Size();
		geometryInfo.pCode = geometryWords.Data();
		VkShaderModuleCreateInfo hullInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		hullInfo.codeSize = desc.HullShaderBytecode.Size();
		hullInfo.pCode = hullWords.Data();
		VkShaderModuleCreateInfo domainInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		domainInfo.codeSize = desc.DomainShaderBytecode.Size();
		domainInfo.pCode = domainWords.Data();
		VkShaderModuleCreateInfo meshInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		meshInfo.codeSize = desc.MeshShaderBytecode.Size();
		meshInfo.pCode = meshWords.Data();
		VkShaderModuleCreateInfo taskInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		taskInfo.codeSize = desc.TaskShaderBytecode.Size();
		taskInfo.pCode = taskWords.Data();
		VkShaderModule vertex = VK_NULL_HANDLE;
		VkShaderModule fragment = VK_NULL_HANDLE;
		VkShaderModule geometry = VK_NULL_HANDLE;
		VkShaderModule hull = VK_NULL_HANDLE;
		VkShaderModule domain = VK_NULL_HANDLE;
		VkShaderModule mesh = VK_NULL_HANDLE;
		VkShaderModule task = VK_NULL_HANDLE;
		const auto destroyModules = [&]() noexcept
			{
				if (vertex)
				{
					state.F.DestroyShaderModule(state.Device, vertex, nullptr);
				}

				if (fragment)
				{
					state.F.DestroyShaderModule(state.Device, fragment, nullptr);
				}

				if (geometry)
				{
					state.F.DestroyShaderModule(state.Device, geometry, nullptr);
				}

				if (hull)
				{
					state.F.DestroyShaderModule(state.Device, hull, nullptr);
				}

				if (domain)
				{
					state.F.DestroyShaderModule(state.Device, domain, nullptr);
				}

				if (mesh)
				{
					state.F.DestroyShaderModule(state.Device, mesh, nullptr);
				}

				if (task)
				{
					state.F.DestroyShaderModule(state.Device, task, nullptr);
				}
			};
		if ((!desc.VertexShaderBytecode.IsEmpty() && state.F.CreateShaderModule(state.Device, &vertexInfo, nullptr, &vertex) != VK_SUCCESS) ||
			(!desc.FragmentShaderBytecode.IsEmpty() && state.F.CreateShaderModule(state.Device, &fragmentInfo, nullptr, &fragment) != VK_SUCCESS) ||
			(!desc.GeometryShaderBytecode.IsEmpty() && state.F.CreateShaderModule(state.Device, &geometryInfo, nullptr, &geometry) != VK_SUCCESS) ||
			(!desc.HullShaderBytecode.IsEmpty() && state.F.CreateShaderModule(state.Device, &hullInfo, nullptr, &hull) != VK_SUCCESS) ||
			(!desc.DomainShaderBytecode.IsEmpty() && state.F.CreateShaderModule(state.Device, &domainInfo, nullptr, &domain) != VK_SUCCESS) ||
			(!desc.MeshShaderBytecode.IsEmpty() && state.F.CreateShaderModule(state.Device, &meshInfo, nullptr, &mesh) != VK_SUCCESS) ||
			(!desc.TaskShaderBytecode.IsEmpty() && state.F.CreateShaderModule(state.Device, &taskInfo, nullptr, &task) != VK_SUCCESS))
		{
			destroyModules();

			return Result<void*>::Failure(Failure("Rhi.VulkanShader", "Could not create graphics shader modules."));
		}
		GraphicsPipeline* pipeline = CreateObject<GraphicsPipeline>(state.Resource);

		if (pipeline == nullptr)
		{
			destroyModules();

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a graphics pipeline."));
		}
		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipeline->AccelStructSlot = desc.AccelStructSlot;
		pipeline->ShaderStages = (mesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT) |
			(task ? VK_SHADER_STAGE_TASK_BIT_EXT : 0) | (fragment ? VK_SHADER_STAGE_FRAGMENT_BIT : 0) |
			(geometry ? VK_SHADER_STAGE_GEOMETRY_BIT : 0) | (hull ? VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT : 0) |
			(domain ? VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT : 0);
		VkDescriptorSetLayout setLayouts[10]{};
		if (layouts[0].bRangeLayout)
		{
			Result<void> created = CreateRangeLayouts(state, *pipeline, layouts, true);
			if (!created)
			{
				destroyModules();
				FreeGraphicsPipeline(state, pipeline);

				return Result<void*>::Failure(created.GetError());
			}
			for (uint32 space = 0; space < pipeline->SetLayoutCount; ++space)
			{
				setLayouts[space] = pipeline->SetLayouts[space];
			}
		}
		else
		{
			for (uint32 layoutIndex = 0; layoutIndex < layouts.Size(); ++layoutIndex)
			{
				const rhi::NativeGraphicsBindingLayoutDesc& source = layouts[layoutIndex];

				if (source.SampledCount() > 8 || source.StorageCount() > 8 || source.BufferCount() > 8)
				{
					destroyModules();
					FreeGraphicsPipeline(state, pipeline);

					return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanGraphics", "A graphics layout exceeds native binding capacity.");
				}
				VkDescriptorSetLayoutBinding bindings[33]{};
				VkDescriptorSetLayoutCreateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				pipeline->SampledTextureCounts[layoutIndex] = static_cast<uint32>(source.SampledCount());
				pipeline->StorageTextureCounts[layoutIndex] = static_cast<uint32>(source.StorageCount());
				pipeline->GraphicsBufferCounts[layoutIndex] = static_cast<uint32>(source.BufferCount());

				for (usize index = 0; index < source.SampledCount(); ++index)
				{
					const GraphicsTextureBindingDesc& binding = source.Sampled(static_cast<uint32>(index));
					pipeline->SampledTextureSlots[layoutIndex][index] = binding.Slot;
					pipeline->SampledSamplerSlots[layoutIndex][index] = binding.SamplerSlot == 0xffffffffu ? binding.Slot : binding.SamplerSlot;
					bindings[2 * index] = { binding.Slot, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, pipeline->ShaderStages, nullptr };
					bindings[2 * index + 1] = { 16 + pipeline->SampledSamplerSlots[layoutIndex][index], VK_DESCRIPTOR_TYPE_SAMPLER, 1,
											   pipeline->ShaderStages, nullptr };
					VkSamplerReductionModeCreateInfo reduction{};
					const VkSamplerCreateInfo samplerInfo = MakeSamplerCreateInfo(binding.Sampler, reduction);

					if (state.F.CreateSampler(state.Device, &samplerInfo, nullptr, &pipeline->Samplers[layoutIndex][index]) != VK_SUCCESS)
					{
						destroyModules();
						FreeGraphicsPipeline(state, pipeline);

						return Result<void*>::Failure(Failure("Rhi.VulkanSampler", "Could not create a graphics sampler."));
					}
				}

				for (usize index = 0; index < source.StorageCount(); ++index)
				{
					const uint32 slot = source.Storage(static_cast<uint32>(index)).Slot;
					pipeline->StorageTextureSlots[layoutIndex][index] = slot;
					bindings[source.SampledCount() * 2 + index] = { 64 + slot, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, pipeline->ShaderStages,
																   nullptr };
				}

				for (usize index = 0; index < source.BufferCount(); ++index)
				{
					const GraphicsBufferBindingDesc& binding = source.Buffer(static_cast<uint32>(index));
					bindings[source.SampledCount() * 2 + source.StorageCount() + index] = {
						32 + binding.Slot, BufferDescriptorType(binding.Type, binding.Format), 1, pipeline->ShaderStages, nullptr };
				}

				if (layoutIndex == 0 && desc.AccelStructSlot != 0xFFFFFFFFu)
				{
					bindings[source.SampledCount() * 2 + source.StorageCount() + source.BufferCount()] = {
						80 + desc.AccelStructSlot, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, pipeline->ShaderStages, nullptr };
				}
				setInfo.bindingCount = static_cast<uint32>(source.SampledCount() * 2 + source.StorageCount() + source.BufferCount() +
					(layoutIndex == 0 && desc.AccelStructSlot != 0xFFFFFFFFu ? 1 : 0));
				setInfo.pBindings = bindings;
				const bool bNeedsSet = layouts.Size() > 1 ||
					setInfo.bindingCount != 0 ||
					desc.BindlessSampledTextureCapacity != 0 ||
					desc.BindlessSamplerCapacity != 0 ||
					desc.BindlessStorageBufferCapacity != 0 ||
					desc.BindlessStorageTextureCapacity != 0 ||
					desc.BindlessUniformBufferCapacity != 0 ||
					desc.BindlessReadOnlyBufferCapacity != 0;

				if (bNeedsSet && state.F.CreateDescriptorSetLayout(state.Device, &setInfo, nullptr, &pipeline->SetLayouts[layoutIndex]) != VK_SUCCESS)
				{
					destroyModules();
					FreeGraphicsPipeline(state, pipeline);

					return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create a graphics descriptor set layout."));
				}
				setLayouts[layoutIndex] = pipeline->SetLayouts[layoutIndex];
			}
			pipeline->SetLayoutCount = static_cast<uint32>(layouts.Size());
		}

		if (setLayouts[0] != VK_NULL_HANDLE)
		{
			layoutInfo.setLayoutCount = pipeline->SetLayoutCount;
			layoutInfo.pSetLayouts = setLayouts;
		}

		if (desc.BindlessSampledTextureCapacity != 0)
		{
			if (!CreateSampledDescriptorSetLayout(state, desc.BindlessSampledTextureCapacity, pipeline->BindlessSetLayout))
			{
				destroyModules();
				FreeGraphicsPipeline(state, pipeline);

				return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create the graphics descriptor table layout."));
			}
			setLayouts[layouts.Size()] = pipeline->BindlessSetLayout;
			layoutInfo.setLayoutCount = static_cast<uint32>(layouts.Size() + 1);
		}

		if (desc.BindlessSamplerCapacity != 0 ||
			desc.BindlessStorageBufferCapacity != 0 ||
			desc.BindlessStorageTextureCapacity != 0 ||
			(desc.BindlessUniformBufferCapacity != 0 || desc.BindlessReadOnlyBufferCapacity != 0))
		{
			if (pipeline->BindlessSetLayout == VK_NULL_HANDLE)
			{
				VkDescriptorSetLayoutCreateInfo emptyInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				if (state.F.CreateDescriptorSetLayout(state.Device, &emptyInfo, nullptr, &pipeline->BindlessSetLayout) != VK_SUCCESS)
				{
					destroyModules();
					FreeGraphicsPipeline(state, pipeline);

					return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create the empty descriptor table layout."));
				}
				setLayouts[layouts.Size()] = pipeline->BindlessSetLayout;
			}

			if (desc.BindlessSamplerCapacity != 0 && !CreateSamplerDescriptorSetLayout(state, desc.BindlessSamplerCapacity, pipeline->BindlessSamplerSetLayout))
			{
				destroyModules();
				FreeGraphicsPipeline(state, pipeline);

				return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create the graphics sampler table layout."));
			}

			if (desc.BindlessSamplerCapacity == 0)
			{
				VkDescriptorSetLayoutCreateInfo emptyInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				if (state.F.CreateDescriptorSetLayout(state.Device, &emptyInfo, nullptr, &pipeline->BindlessSamplerSetLayout) != VK_SUCCESS)
				{
					destroyModules();
					FreeGraphicsPipeline(state, pipeline);

					return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create the empty sampler table layout."));
				}
			}
			setLayouts[layouts.Size() + 1] = pipeline->BindlessSamplerSetLayout;
			layoutInfo.setLayoutCount = static_cast<uint32>(layouts.Size() + 2);
		}

		if (desc.BindlessStorageBufferCapacity != 0 || desc.BindlessStorageTextureCapacity != 0 || (desc.BindlessUniformBufferCapacity != 0 || desc.BindlessReadOnlyBufferCapacity != 0))
		{
			if (desc.BindlessStorageBufferCapacity != 0)
			{
				if (!CreateStorageBufferDescriptorSetLayout(state, desc.BindlessStorageBufferCapacity,
					pipeline->BindlessStorageBufferSetLayout, desc.BindlessStorageBufferFormat))
				{
					destroyModules();
					FreeGraphicsPipeline(state, pipeline);

					return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create the graphics storage buffer table layout."));
				}
				setLayouts[layouts.Size() + 2] = pipeline->BindlessStorageBufferSetLayout;
			}
			else
			{
				VkDescriptorSetLayoutCreateInfo emptyInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				if (state.F.CreateDescriptorSetLayout(state.Device, &emptyInfo, nullptr, &pipeline->EmptyStorageBufferSetLayout) !=
					VK_SUCCESS)
				{
					destroyModules();
					FreeGraphicsPipeline(state, pipeline);

					return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create the empty storage buffer layout."));
				}
				setLayouts[layouts.Size() + 2] = pipeline->EmptyStorageBufferSetLayout;
			}
			layoutInfo.setLayoutCount = static_cast<uint32>(layouts.Size() + 3);
		}

		if (desc.BindlessStorageTextureCapacity != 0 || (desc.BindlessUniformBufferCapacity != 0 || desc.BindlessReadOnlyBufferCapacity != 0))
		{
			if (desc.BindlessStorageTextureCapacity != 0)
			{
				if (!CreateStorageTextureDescriptorSetLayout(state, desc.BindlessStorageTextureCapacity,
					pipeline->BindlessStorageTextureSetLayout))
				{
					destroyModules();
					FreeGraphicsPipeline(state, pipeline);

					return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create the graphics storage texture table layout."));
				}
				setLayouts[layouts.Size() + 3] = pipeline->BindlessStorageTextureSetLayout;
			}
			else
			{
				VkDescriptorSetLayoutCreateInfo emptyInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				if (state.F.CreateDescriptorSetLayout(state.Device, &emptyInfo, nullptr, &pipeline->EmptyStorageTextureSetLayout) !=
					VK_SUCCESS)
				{
					destroyModules();
					FreeGraphicsPipeline(state, pipeline);

					return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create the empty storage texture table layout."));
				}
				setLayouts[layouts.Size() + 3] = pipeline->EmptyStorageTextureSetLayout;
			}
			layoutInfo.setLayoutCount = static_cast<uint32>(layouts.Size() + 4);
		}

		if ((desc.BindlessUniformBufferCapacity != 0 || desc.BindlessReadOnlyBufferCapacity != 0))
		{
			VkDescriptorSetLayoutCreateInfo empty{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			const bool bCreated = desc.BindlessUniformBufferCapacity != 0
				? CreateUniformBufferDescriptorSetLayout(state, desc.BindlessUniformBufferCapacity,
					pipeline->BindlessUniformBufferSetLayout)
				: state.F.CreateDescriptorSetLayout(state.Device, &empty, nullptr,
					&pipeline->BindlessUniformBufferSetLayout) == VK_SUCCESS;

			if (!bCreated)
			{
				destroyModules();
				FreeGraphicsPipeline(state, pipeline);

				return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create the graphics uniform buffer table layout."));
			}
			setLayouts[layouts.Size() + 4] = pipeline->BindlessUniformBufferSetLayout;
			layoutInfo.setLayoutCount = static_cast<uint32>(layouts.Size() + 5);
		}

		if (desc.BindlessReadOnlyBufferCapacity != 0)
		{
			if (!CreateBindlessDescriptorSetLayout(state, EDescriptorType::ReadOnlyBuffer, desc.BindlessReadOnlyBufferCapacity,
				pipeline->BindlessReadOnlyBufferSetLayout, desc.BindlessReadOnlyBufferFormat))
			{
				destroyModules();
				FreeGraphicsPipeline(state, pipeline);

				return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create the read-only buffer layout."));
			}
			setLayouts[layouts.Size() + 5] = pipeline->BindlessReadOnlyBufferSetLayout;
			layoutInfo.setLayoutCount = static_cast<uint32>(layouts.Size() + 6);
		}

		VkPushConstantRange pushRange{ pipeline->ShaderStages, 0, desc.PushConstantBytes };
		if (desc.PushConstantBytes != 0)
		{
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &pushRange;
		}
		bool bCreated = state.F.CreatePipelineLayout(state.Device, &layoutInfo, nullptr, &pipeline->Layout) == VK_SUCCESS;
		VkPipelineShaderStageCreateInfo stages[6]{};
		stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stages[0].stage = task ? VK_SHADER_STAGE_TASK_BIT_EXT : mesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = task ? task : mesh ? mesh : vertex;
		stages[0].pName = task ? desc.TaskEntryPoint : mesh ? desc.MeshEntryPoint : desc.VertexEntryPoint;
		uint32 stageCount = 1;

		if (task)
		{
			VkPipelineShaderStageCreateInfo& stage = stages[stageCount++];
			stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			stage.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
			stage.module = mesh;
			stage.pName = desc.MeshEntryPoint;
		}

		if (hull)
		{
			VkPipelineShaderStageCreateInfo& stage = stages[stageCount++];
			stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			stage.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
			stage.module = hull;
			stage.pName = desc.HullEntryPoint;
		}

		if (domain)
		{
			VkPipelineShaderStageCreateInfo& stage = stages[stageCount++];
			stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			stage.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
			stage.module = domain;
			stage.pName = desc.DomainEntryPoint;
		}

		if (geometry)
		{
			VkPipelineShaderStageCreateInfo& stage = stages[stageCount++];
			stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			stage.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
			stage.module = geometry;
			stage.pName = desc.GeometryEntryPoint;
		}

		if (fragment)
		{
			VkPipelineShaderStageCreateInfo& stage = stages[stageCount++];
			stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stage.module = fragment;
			stage.pName = desc.FragmentEntryPoint;
		}
		VkSpecializationMapEntry specializationEntries[6][64]{};
		uint32 specializationData[6][64]{};
		VkSpecializationInfo specializationInfos[6]{};
		for (uint32 index = 0; index < stageCount; ++index)
		{
			const Span<const ShaderSpecialization> values =
				stages[index].stage == VK_SHADER_STAGE_VERTEX_BIT ? desc.VertexSpecializations
				: stages[index].stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ? desc.HullSpecializations
				: stages[index].stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT ? desc.DomainSpecializations
				: stages[index].stage == VK_SHADER_STAGE_GEOMETRY_BIT ? desc.GeometrySpecializations
				: stages[index].stage == VK_SHADER_STAGE_MESH_BIT_EXT ? desc.MeshSpecializations
				: stages[index].stage == VK_SHADER_STAGE_TASK_BIT_EXT ? desc.TaskSpecializations
				: desc.FragmentSpecializations;

			if (values.IsEmpty())
			{
				continue;
			}
			FillSpecializations(values, specializationEntries[index], specializationData[index], specializationInfos[index]);
			stages[index].pSpecializationInfo = &specializationInfos[index];
		}
		VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		VkVertexInputBindingDescription vertexBindings[4]{ {0, desc.VertexStrideBytes, VK_VERTEX_INPUT_RATE_VERTEX} };
		VkVertexInputAttributeDescription vertexAttributes[16]{};
		if (!desc.VertexAttributes.IsEmpty())
		{
			vertexInput.vertexBindingDescriptionCount = 1 + static_cast<uint32>(desc.ExtraVertexBufferSlots.Size());
			vertexInput.pVertexBindingDescriptions = vertexBindings;

			for (uint32 binding = 0; binding < desc.ExtraVertexBufferSlots.Size(); ++binding)
			{
				vertexBindings[binding + 1] = { binding + 1, desc.ExtraVertexBufferSlots[binding].StrideBytes,
											   desc.ExtraVertexBufferSlots[binding].bPerInstance ? VK_VERTEX_INPUT_RATE_INSTANCE
																								 : VK_VERTEX_INPUT_RATE_VERTEX };
			}
			vertexInput.vertexAttributeDescriptionCount = static_cast<uint32>(desc.VertexAttributes.Size());
			vertexInput.pVertexAttributeDescriptions = vertexAttributes;

			for (usize index = 0; index < desc.VertexAttributes.Size(); ++index)
			{
				const VertexAttributeDesc& attribute = desc.VertexAttributes[index];
				vertexAttributes[index] = { attribute.Location, attribute.BufferSlot, ConvertVulkanFormat(attribute.Format),
										   attribute.OffsetBytes };
			}
		}
		VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		assembly.topology = VULKAN_TOPOLOGIES[static_cast<usize>(desc.Topology)];
		VkPipelineTessellationStateCreateInfo tessellation{ VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO };
		tessellation.patchControlPoints = desc.PatchControlPoints;
		VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewport.viewportCount = desc.ViewportCount;
		viewport.scissorCount = desc.ViewportCount;
		VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		raster.polygonMode = desc.Raster.FillMode == EFillMode::Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
		raster.depthClampEnable = !desc.Raster.bDepthClipEnabled;
		raster.depthBiasEnable = desc.Raster.DepthBias != 0 || desc.Raster.DepthBiasClamp != 0.0f || desc.Raster.SlopeScaledDepthBias != 0.0f;
		raster.depthBiasConstantFactor = static_cast<float>(desc.Raster.DepthBias);
		raster.depthBiasClamp = desc.Raster.DepthBiasClamp;
		raster.depthBiasSlopeFactor = desc.Raster.SlopeScaledDepthBias;
		VkPipelineRasterizationConservativeStateCreateInfoEXT conservative{
			VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT };
		conservative.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;

		if (desc.Raster.bConservativeRasterEnabled)
		{
			raster.pNext = &conservative;
		}
		raster.cullMode = desc.Raster.CullMode == ECullMode::Front ? VK_CULL_MODE_FRONT_BIT : desc.Raster.CullMode == ECullMode::Back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
		// The positive-height Vulkan viewport reverses clip-space winding relative to D3D12.
		raster.frontFace = desc.Raster.FrontFace == EFrontFace::CounterClockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
		raster.lineWidth = 1.0f;
		VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(desc.SampleCount);
		VkPipelineDepthStencilStateCreateInfo depthState{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depthState.depthTestEnable = desc.bDepthTest || desc.bDepthWrite;
		depthState.depthWriteEnable = desc.bDepthWrite;
		depthState.depthBoundsTestEnable = desc.DepthBounds.bEnable;
		depthState.minDepthBounds = desc.DepthBounds.bEnable ? desc.DepthBounds.Minimum : 0.0f;
		depthState.maxDepthBounds = desc.DepthBounds.bEnable ? desc.DepthBounds.Maximum : 1.0f;
		depthState.depthCompareOp = desc.bDepthTest ? VULKAN_COMPARE[static_cast<usize>(desc.DepthCompare)] : VK_COMPARE_OP_ALWAYS;
		depthState.stencilTestEnable = desc.Stencil.bEnable;
		const auto stencilFace = [&desc](const StencilFaceDesc& face) noexcept
			{
				VkStencilOpState result{};
				result.failOp = VULKAN_STENCIL_OPERATIONS[static_cast<usize>(face.FailOp)];
				result.depthFailOp = VULKAN_STENCIL_OPERATIONS[static_cast<usize>(face.DepthFailOp)];
				result.passOp = VULKAN_STENCIL_OPERATIONS[static_cast<usize>(face.PassOp)];
				result.compareOp = VULKAN_COMPARE[static_cast<usize>(face.Compare)];
				result.compareMask = desc.Stencil.ReadMask;
				result.writeMask = desc.Stencil.WriteMask;

				return result;
			};
		depthState.front = stencilFace(desc.Stencil.FrontFace);
		depthState.back = stencilFace(desc.Stencil.BackFace);
		const uint32 colorCount = desc.ColorFormat == EFormat::UNKNOWN ? 0 : desc.ColorFormats.IsEmpty() ? 1 : static_cast<uint32>(desc.ColorFormats.Size());
		VkPipelineColorBlendAttachmentState attachments[8]{};
		for (uint32 color = 0; color < colorCount; ++color)
		{
			const ColorBlendDesc colorBlend = desc.ColorBlends.IsEmpty() ? ColorBlendDesc{} : desc.ColorBlends[color];
			pipeline->bBlendConstant =
				pipeline->bBlendConstant || (colorBlend.bEnable && ((colorBlend.SourceColor >= EBlendFactor::ConstantColor &&
					colorBlend.SourceColor <= EBlendFactor::OneMinusConstantColor) ||
					(colorBlend.DestinationColor >= EBlendFactor::ConstantColor &&
						colorBlend.DestinationColor <= EBlendFactor::OneMinusConstantColor) ||
					(colorBlend.SourceAlpha >= EBlendFactor::ConstantColor &&
						colorBlend.SourceAlpha <= EBlendFactor::OneMinusConstantColor) ||
					(colorBlend.DestinationAlpha >= EBlendFactor::ConstantColor &&
						colorBlend.DestinationAlpha <= EBlendFactor::OneMinusConstantColor)));
			VkPipelineColorBlendAttachmentState& target = attachments[color];
			target.blendEnable = colorBlend.bEnable;
			target.srcColorBlendFactor = VULKAN_BLEND_FACTORS[static_cast<usize>(colorBlend.SourceColor)];
			target.dstColorBlendFactor = VULKAN_BLEND_FACTORS[static_cast<usize>(colorBlend.DestinationColor)];
			target.colorBlendOp = VULKAN_BLEND_OPERATIONS[static_cast<usize>(colorBlend.ColorOperation)];
			target.srcAlphaBlendFactor = VULKAN_BLEND_FACTORS[static_cast<usize>(colorBlend.SourceAlpha)];
			target.dstAlphaBlendFactor = VULKAN_BLEND_FACTORS[static_cast<usize>(colorBlend.DestinationAlpha)];
			target.alphaBlendOp = VULKAN_BLEND_OPERATIONS[static_cast<usize>(colorBlend.AlphaOperation)];
			target.colorWriteMask = colorBlend.WriteMask;
		}
		VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		blend.attachmentCount = colorCount;
		blend.pAttachments = colorCount != 0 ? attachments : nullptr;
		VkDynamicState dynamicStates[5]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		uint32 dynamicCount = 2;

		if (desc.Stencil.bEnable)
		{
			dynamicStates[dynamicCount++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
		}

		if (pipeline->bBlendConstant)
		{
			dynamicStates[dynamicCount++] = VK_DYNAMIC_STATE_BLEND_CONSTANTS;
		}

		if (state.SupportedShadingRates[0] != 0)
		{
			dynamicStates[dynamicCount++] = VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR;
		}
		VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamic.dynamicStateCount = dynamicCount;
		dynamic.pDynamicStates = dynamicStates;
		VkGraphicsPipelineCreateInfo nativeDesc{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		nativeDesc.stageCount = stageCount;
		nativeDesc.pStages = stages;
		nativeDesc.pVertexInputState = mesh ? nullptr : &vertexInput;
		nativeDesc.pInputAssemblyState = mesh ? nullptr : &assembly;
		nativeDesc.pTessellationState = hull ? &tessellation : nullptr;
		nativeDesc.pViewportState = &viewport;
		nativeDesc.pRasterizationState = &raster;
		nativeDesc.pMultisampleState = &multisample;
		nativeDesc.pDepthStencilState = desc.DepthFormat != EFormat::UNKNOWN ? &depthState : nullptr;
		nativeDesc.pColorBlendState = &blend;
		nativeDesc.pDynamicState = &dynamic;
		nativeDesc.layout = pipeline->Layout;

		if (colorCount == 1 &&
			desc.SampleCount == 1 &&
			(desc.ColorFormats.IsEmpty() ? desc.ColorFormat : desc.ColorFormats[0]) == EFormat::RGBA8_UNORM &&
			(desc.DepthFormat == EFormat::UNKNOWN || desc.DepthFormat == EFormat::D32))
		{
			nativeDesc.renderPass = desc.DepthFormat == EFormat::D32 ? state.DepthRenderPass : state.RenderPass;
		}
		else
		{
			const EFormat colorFormat = desc.ColorFormat;
			const Span<const EFormat> formats = colorCount == 0 ? Span<const EFormat>{}
			: desc.ColorFormats.IsEmpty() ? Span<const EFormat>(&colorFormat, 1)
				: desc.ColorFormats;
			Result<VkRenderPass> renderPass = CreateAttachmentRenderPass(state, formats, desc.DepthFormat, desc.SampleCount);

			if (renderPass)
			{
				pipeline->RenderPass = renderPass.Value();
			}
			else
			{
				bCreated = false;
			}
			nativeDesc.renderPass = pipeline->RenderPass;
		}

		if (bCreated)
		{
			bCreated = state.F.CreateGraphicsPipelines(state.Device, VK_NULL_HANDLE, 1, &nativeDesc, nullptr, &pipeline->Handle) == VK_SUCCESS;
		}

		if (bCreated && desc.bSupportsShadingRateImage)
		{
			const EFormat singleColor = desc.ColorFormat;
			const Span<const EFormat> formats = colorCount == 0 ? Span<const EFormat>{}
			: desc.ColorFormats.IsEmpty() ? Span<const EFormat>(&singleColor, 1)
				: desc.ColorFormats;
			Result<VkRenderPass> ratePass = CreateAttachmentRenderPass(state, formats, desc.DepthFormat, desc.SampleCount, false, true);

			if (ratePass)
			{
				pipeline->ShadingRateRenderPass = ratePass.Value();
				nativeDesc.renderPass = ratePass.Value();
				bCreated = state.F.CreateGraphicsPipelines(state.Device, VK_NULL_HANDLE, 1, &nativeDesc, nullptr, &pipeline->ShadingRateHandle) == VK_SUCCESS;
			}
			else
			{
				bCreated = false;
			}
		}
		destroyModules();

		if (!bCreated)
		{
			FreeGraphicsPipeline(state, pipeline);

			return Result<void*>::Failure(Failure("Rhi.VulkanGraphics", "Could not create the graphics pipeline."));
		}
		pipeline->PushConstantBytes = desc.PushConstantBytes;
		pipeline->bMeshShader = mesh != VK_NULL_HANDLE;
		pipeline->bStencil = desc.Stencil.bEnable;

		return Result<void*>::Success(pipeline);
	}

	void DestroyGraphicsPipelineNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		FreeGraphicsPipeline(state, static_cast<GraphicsPipeline*>(native));
	}

	[[nodiscard]] Result<void> DestroyGraphicsPipeline(State* opaque, void* native)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(native);
		uint64 complete = 0;

		if (state.F.GetSemaphoreCounterValue(state.Device, state.Timeline, &complete) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanTimeline", "Could not inspect Vulkan completion."));
		}

		if (pipeline.LastUse > complete)
		{
			if (!state.RetiredGraphicsPipelines.Add({ &pipeline, pipeline.LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retire the graphics pipeline."));
			}
		}
		else
		{
			FreeGraphicsPipeline(state, &pipeline);
		}

		return Result<void>::Success();
	}
}
