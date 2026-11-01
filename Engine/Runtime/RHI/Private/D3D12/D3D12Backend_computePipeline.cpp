#include "D3D12Backend.h"
#include "D3D12Descriptors.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<void*> CreateComputePipeline(State* opaque, const NativeComputePipelineDesc& desc, Span<const NativeBindingLayoutDesc> layouts)
	{
		ASSERT(!layouts.IsEmpty() && layouts.Size() <= MAXIMUM_FIXED_BINDING_LAYOUTS, "A compute pipeline has one to eight native layouts.");
		ComputeBindingDesc flattenedBuffers[32]{};
		ComputeTextureBindingDesc flattenedTextures[32]{};
		uint32 bufferSpaces[32]{};
		uint32 textureSpaces[32]{};
		uint32 bufferCount = 0;
		uint32 textureCount = 0;
		uint32 sampledCount = 0;

		for (uint32 layoutIndex = 0; layoutIndex < layouts.Size(); ++layoutIndex)
		{
			for (uint32 bindingIndex = 0; bindingIndex < layouts[layoutIndex].BufferCount(); ++bindingIndex)
			{
				const ComputeBindingDesc binding = layouts[layoutIndex].Buffer(bindingIndex);
				ASSERT(bufferCount < 32, "Too many compute buffer bindings.");
				flattenedBuffers[bufferCount] = binding;
				bufferSpaces[bufferCount++] = layoutIndex;
			}

			for (uint32 textureIndex = 0; textureIndex < layouts[layoutIndex].TextureCount(); ++textureIndex)
			{
				const ComputeTextureBindingDesc texture = layouts[layoutIndex].Texture(textureIndex);
				ASSERT(textureCount < 32, "Too many compute texture bindings.");
				flattenedTextures[textureCount] = texture;
				textureSpaces[textureCount++] = layoutIndex;
				sampledCount += texture.Type == EComputeTextureBindingType::SampledTexture ? 1u : 0u;
			}
		}

		const Span<const ComputeBindingDesc> buffers{ flattenedBuffers, bufferCount };
		const Span<const ComputeTextureBindingDesc> textures{ flattenedTextures, textureCount };
		const uint32 bindlessCount =
			(desc.BindlessSampledTextureCapacity != 0 ? 1u : 0u) + (desc.BindlessSamplerCapacity != 0 ? 1u : 0u) +
			(desc.BindlessStorageBufferCapacity != 0 ? 1u : 0u) + (desc.BindlessStorageTextureCapacity != 0 ? 1u : 0u) +
			(desc.BindlessUniformBufferCapacity != 0 ? 1u : 0u) + (desc.BindlessReadOnlyBufferCapacity != 0 ? 1u : 0u);

		if (desc.PushConstantBytes / 4 +
			(layouts[0].bRangeLayout ? RangeRootDwordCost(layouts) : bufferCount * 2 + textureCount + sampledCount + bindlessCount) +
			(desc.AccelStructSlot != 0xFFFFFFFFu ? 2u : 0u) >
			64)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Compute", "The compute root signature exceeds 64 DWORDs.");
		}

		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		if (desc.bDeviceHeapIndexed && (!state.bDeviceHeapIndexingEnabled || !state.GlobalResourceHeap || !state.GlobalSamplerHeap))
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Compute", "The device descriptor heaps are unavailable.");
		}
		ComputePipeline* pipeline = CreateObject<ComputePipeline>(state.Resource);

		if (pipeline == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a compute pipeline."));
		}
		pipeline->bHeapDirectlyIndexed = desc.bHeapDirectlyIndexed;
		pipeline->bDeviceHeapIndexed = desc.bDeviceHeapIndexed;
		D3D12_ROOT_PARAMETER parameters[102]{};
		D3D12_DESCRIPTOR_RANGE ranges[32]{};
		D3D12_DESCRIPTOR_RANGE bufferRanges[32]{};
		D3D12_DESCRIPTOR_RANGE samplerRanges[32]{};
		D3D12_DESCRIPTOR_RANGE bindlessRange{};
		D3D12_DESCRIPTOR_RANGE bindlessSamplerRange{};
		D3D12_DESCRIPTOR_RANGE bindlessStorageBufferRange{};
		D3D12_DESCRIPTOR_RANGE bindlessStorageTextureRange{};
		D3D12_DESCRIPTOR_RANGE bindlessUniformBufferRange{};
		D3D12_DESCRIPTOR_RANGE bindlessReadOnlyBufferRange{};
		uint32 samplerCount = 0;
		pipeline->BufferCount = static_cast<uint32>(buffers.Size());
		pipeline->PushConstantBytes = desc.PushConstantBytes;
		const uint32 rootOffset = desc.PushConstantBytes != 0 ? 1 : 0;

		if (rootOffset != 0)
		{
			parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			parameters[0].Constants.ShaderRegister = 0;
			parameters[0].Constants.RegisterSpace = static_cast<UINT>(layouts.Size());
			parameters[0].Constants.Num32BitValues = desc.PushConstantBytes / 4;
			parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		for (usize index = 0; index < buffers.Size(); ++index)
		{
			const ComputeBindingDesc& binding = buffers[index];
			parameters[rootOffset + index].ParameterType =
				binding.Type == EComputeBindingType::UniformBuffer ? D3D12_ROOT_PARAMETER_TYPE_CBV
				: binding.Type == EComputeBindingType::ReadOnlyBuffer ? D3D12_ROOT_PARAMETER_TYPE_SRV
				: D3D12_ROOT_PARAMETER_TYPE_UAV;
			parameters[rootOffset + index].Descriptor.ShaderRegister = binding.Slot;
			parameters[rootOffset + index].Descriptor.RegisterSpace = bufferSpaces[index];
			parameters[rootOffset + index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			if (binding.Format != EFormat::UNKNOWN)
			{
				bufferRanges[index] = { binding.Type == EComputeBindingType::ReadOnlyBuffer ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
																						   : D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
									   1, binding.Slot, bufferSpaces[index], D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
				parameters[rootOffset + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				parameters[rootOffset + index].DescriptorTable = { 1, &bufferRanges[index] };
			}
		}

		for (usize index = 0; index < textures.Size(); ++index)
		{
			ranges[index] = { textures[index].Type == EComputeTextureBindingType::SampledTexture ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
																								: D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
							 1, textures[index].Slot, textureSpaces[index], D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[rootOffset + buffers.Size() + index];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &ranges[index] };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			if (textures[index].Type == EComputeTextureBindingType::SampledTexture)
			{
				samplerRanges[samplerCount] = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, textures[index].Slot, textureSpaces[index],
											   D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
				D3D12_ROOT_PARAMETER& samplerParameter = parameters[rootOffset + buffers.Size() + textures.Size() + samplerCount];
				samplerParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				samplerParameter.DescriptorTable = { 1, &samplerRanges[samplerCount] };
				samplerParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
				++samplerCount;
			}
		}

		if (desc.BindlessSampledTextureCapacity != 0)
		{
			pipeline->BindlessRootIndex = rootOffset + static_cast<uint32>(buffers.Size() + textures.Size()) + samplerCount;
			bindlessRange = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, desc.BindlessSampledTextureCapacity, 0, static_cast<UINT>(layouts.Size() + 1),
							 D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.BindlessSamplerCapacity != 0)
		{
			pipeline->BindlessSamplerRootIndex = rootOffset + static_cast<uint32>(buffers.Size() + textures.Size()) + samplerCount + (desc.BindlessSampledTextureCapacity != 0 ? 1u : 0u);
			bindlessSamplerRange = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, desc.BindlessSamplerCapacity, 0,
									static_cast<UINT>(layouts.Size() + 1), D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessSamplerRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessSamplerRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.BindlessStorageBufferCapacity != 0)
		{
			pipeline->BindlessStorageBufferRootIndex = rootOffset + static_cast<uint32>(buffers.Size() + textures.Size()) +
				samplerCount + (desc.BindlessSampledTextureCapacity != 0 ? 1u : 0u) +
				(desc.BindlessSamplerCapacity != 0 ? 1u : 0u);
			bindlessStorageBufferRange = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, desc.BindlessStorageBufferCapacity, 0,
										  static_cast<UINT>(layouts.Size() + 1), D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessStorageBufferRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessStorageBufferRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.BindlessStorageTextureCapacity != 0)
		{
			pipeline->BindlessStorageTextureRootIndex = rootOffset + static_cast<uint32>(buffers.Size() + textures.Size()) +
				samplerCount + (desc.BindlessSampledTextureCapacity != 0 ? 1u : 0u) +
				(desc.BindlessSamplerCapacity != 0 ? 1u : 0u) +
				(desc.BindlessStorageBufferCapacity != 0 ? 1u : 0u);
			bindlessStorageTextureRange = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, desc.BindlessStorageTextureCapacity, 0,
										   static_cast<UINT>(layouts.Size() + 2), D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessStorageTextureRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessStorageTextureRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.BindlessUniformBufferCapacity != 0)
		{
			pipeline->BindlessUniformBufferRootIndex =
				rootOffset + static_cast<uint32>(buffers.Size() + textures.Size()) + samplerCount +
				(desc.BindlessSampledTextureCapacity != 0 ? 1u : 0u) + (desc.BindlessSamplerCapacity != 0 ? 1u : 0u) +
				(desc.BindlessStorageBufferCapacity != 0 ? 1u : 0u) + (desc.BindlessStorageTextureCapacity != 0 ? 1u : 0u);
			bindlessUniformBufferRange = { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, desc.BindlessUniformBufferCapacity, 0,
										  static_cast<UINT>(layouts.Size() + 1), D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessUniformBufferRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessUniformBufferRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.BindlessReadOnlyBufferCapacity != 0)
		{
			pipeline->BindlessReadOnlyBufferRootIndex =
				rootOffset + static_cast<uint32>(buffers.Size() + textures.Size()) + samplerCount +
				(desc.BindlessSampledTextureCapacity != 0 ? 1u : 0u) + (desc.BindlessSamplerCapacity != 0 ? 1u : 0u) +
				(desc.BindlessStorageBufferCapacity != 0 ? 1u : 0u) + (desc.BindlessStorageTextureCapacity != 0 ? 1u : 0u) +
				(desc.BindlessUniformBufferCapacity != 0 ? 1u : 0u);
			bindlessReadOnlyBufferRange = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, desc.BindlessReadOnlyBufferCapacity, 0,
										   static_cast<UINT>(layouts.Size() + 3), D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessReadOnlyBufferRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessReadOnlyBufferRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		D3D12_ROOT_SIGNATURE_DESC rootDesc{};
		rootDesc.NumParameters = rootOffset + static_cast<UINT>(buffers.Size() + textures.Size()) + samplerCount +
			(desc.BindlessSampledTextureCapacity != 0 ? 1u : 0u) + (desc.BindlessSamplerCapacity != 0 ? 1u : 0u) +
			(desc.BindlessStorageBufferCapacity != 0 ? 1u : 0u) +
			(desc.BindlessStorageTextureCapacity != 0 ? 1u : 0u) +
			(desc.BindlessUniformBufferCapacity != 0 ? 1u : 0u) + (desc.BindlessReadOnlyBufferCapacity != 0 ? 1u : 0u);

		if (desc.AccelStructSlot != 0xFFFFFFFFu)
		{
			pipeline->AccelRootIndex = rootDesc.NumParameters++;
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->AccelRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			parameter.Descriptor.ShaderRegister = desc.AccelStructSlot;
			parameter.Descriptor.RegisterSpace = 0;
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		D3D12_DESCRIPTOR_RANGE fixedRanges[MAXIMUM_FIXED_BINDING_LAYOUTS * MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
		if (layouts[0].bRangeLayout)
		{
			FillRangeRootParameters(pipeline->Fixed, layouts, parameters, fixedRanges, rootOffset, false);
			rootDesc.NumParameters = rootOffset + RangeRootCost(layouts);
			samplerCount = 0;
		}
		rootDesc.pParameters = parameters;
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		ComPtr<ID3DBlob> serialized;
		ComPtr<ID3DBlob> errors;
		HRESULT serializeResult = E_FAIL;

		if (desc.bHeapDirectlyIndexed)
		{
			serializeResult = SerializeDirectRootSignature(rootDesc, serialized.GetAddressOf(), errors.GetAddressOf());
		}
		else
		{
			serializeResult = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), errors.GetAddressOf());
		}

		if (FAILED(serializeResult) || FAILED(state.Device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(pipeline->RootSignature.GetAddressOf()))))
		{
			DestroyObject(state.Resource, pipeline);

			return Result<void*>::Failure(Failure("Rhi.D3D12Compute", "Could not create the compute root signature."));
		}
		pipeline->SampledTextureCount = samplerCount;

		if (samplerCount != 0)
		{
			D3D12_DESCRIPTOR_HEAP_DESC samplerHeap{};
			samplerHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			samplerHeap.NumDescriptors = samplerCount;

			if (FAILED(state.Device->CreateDescriptorHeap(&samplerHeap, IID_PPV_ARGS(pipeline->DefaultSamplers.GetAddressOf()))))
			{
				DestroyObject(state.Resource, pipeline);

				return Result<void*>::Failure(Failure("Rhi.D3D12Sampler", "Could not create compute samplers."));
			}
			D3D12_CPU_DESCRIPTOR_HANDLE cpu = pipeline->DefaultSamplers->GetCPUDescriptorHandleForHeapStart();
			const UINT stride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

			for (const ComputeTextureBindingDesc& texture : textures)
			{
				if (texture.Type != EComputeTextureBindingType::SampledTexture)
				{
					continue;
				}

				const D3D12_SAMPLER_DESC sampler = MakeSamplerDesc(texture.Sampler);
				state.Device->CreateSampler(&sampler, cpu);
				cpu.ptr += stride;
			}
		}
		D3D12_COMPUTE_PIPELINE_STATE_DESC nativeDesc{};
		nativeDesc.pRootSignature = pipeline->RootSignature.Get();
		nativeDesc.CS = { desc.ShaderBytecode.Data(), desc.ShaderBytecode.Size() };
		if (FAILED(state.Device->CreateComputePipelineState(&nativeDesc, IID_PPV_ARGS(pipeline->State.GetAddressOf()))))
		{
			DestroyObject(state.Resource, pipeline);

			return Result<void*>::Failure(Failure("Rhi.D3D12Compute", "Could not create the compute pipeline state."));
		}

		return Result<void*>::Success(pipeline);
	}

	void DestroyComputePipelineNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<ComputePipeline*>(native));
	}

	[[nodiscard]] Result<void> DestroyComputePipeline(State* opaque, void* native)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}

		ComputePipeline& pipeline = *static_cast<ComputePipeline*>(native);

		if (pipeline.LastUse > CompletedSerial(state))
		{
			if (!state.RetiredPipelines.Add({ &pipeline, pipeline.LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retire the compute pipeline."));
			}
		}
		else
		{
			DestroyComputePipelineNow(opaque, native);
		}

		return Result<void>::Success();
	}
}
#endif
