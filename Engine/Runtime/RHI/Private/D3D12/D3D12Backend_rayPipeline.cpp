#include "D3D12Backend.h"
#include "D3D12Descriptors.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<void*> CreateRayTracingPipeline(State* opaque, const NativeRayPipelineDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}

		if (!state.RayDevice)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12RayPipeline", "The device has no ray tracing pipeline support.");
		}

		if (desc.MaxRecursionDepth > D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12RayPipeline", "The ray recursion depth exceeds the D3D12 limit.");
		}

		const bool bRanges = !desc.GlobalLayouts.IsEmpty() && desc.GlobalLayouts[0].bRangeLayout;
		uint32 bufferCount = 0;
		uint32 textureCount = 0;
		uint32 samplerCount = 0;

		for (const NativeBindingLayoutDesc& layout : desc.GlobalLayouts)
		{
			bufferCount += static_cast<uint32>(layout.BufferCount());
			textureCount += static_cast<uint32>(layout.TextureCount());

			for (uint32 item = 0; item < layout.TextureCount(); ++item)
			{
				const ComputeTextureBindingDesc texture = layout.Texture(item);
				samplerCount += texture.Type == EComputeTextureBindingType::SampledTexture ? 1u : 0u;
			}
		}

		uint32 localSamplerCount = 0;

		for (uint32 index = 0; index < desc.GeneralShaderCount; ++index)
		{
			for (uint32 item = 0; item < desc.Shaders[index].LocalLayout.TextureCount(); ++item)
			{
				const ComputeTextureBindingDesc texture = desc.Shaders[index].LocalLayout.Texture(item);
				localSamplerCount += texture.Type == EComputeTextureBindingType::SampledTexture ? 1u : 0u;
			}
		}

		for (const NativeRayHitGroupDesc& group : desc.HitGroups)
		{
			for (uint32 item = 0; item < group.LocalLayout.TextureCount(); ++item)
			{
				const ComputeTextureBindingDesc texture = group.LocalLayout.Texture(item);
				localSamplerCount += texture.Type == EComputeTextureBindingType::SampledTexture ? 1u : 0u;
			}
		}

		if (desc.PushConstantBytes / 4 + (bRanges ? RangeRootDwordCost(desc.GlobalLayouts) : bufferCount * 2 + textureCount + samplerCount) +
			(desc.AccelStructSlot != 0xFFFFFFFFu ? 2u : 0u) >
			64)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12RayPipeline", "The ray root signature exceeds 64 DWORDs.");
		}
		RayTracingPipeline* pipeline = CreateObject<RayTracingPipeline>(state.Resource, state.Resource);

		if (pipeline == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a D3D12 ray pipeline."));
		}
		uint32 localArgumentCount = 0;
		for (uint32 index = 0; index < desc.GeneralShaderCount; ++index)
		{
			localArgumentCount += static_cast<uint32>(desc.Shaders[index].LocalRootArguments.Size());
		}
		for (const rhi::NativeRayHitGroupDesc& group : desc.HitGroups)
		{
			localArgumentCount += static_cast<uint32>(group.LocalRootArguments.Size());
		}
		if (!pipeline->LocalRootArguments.Reserve(localArgumentCount) || (localArgumentCount != 0 && !EnsureGlobalDescriptorHeaps(state)))
		{
			DestroyObject(state.Resource, pipeline);

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not realize typed local root storage."));
		}
		pipeline->bTypedLocalRoots = localArgumentCount != 0;
		D3D12_ROOT_SIGNATURE_DESC rootDesc{};
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		D3D12_ROOT_PARAMETER globalParameters[98]{};
		D3D12_DESCRIPTOR_RANGE textureRanges[64]{};
		D3D12_DESCRIPTOR_RANGE bufferRanges[32]{};
		D3D12_DESCRIPTOR_RANGE samplerRanges[32]{};
		pipeline->PushConstantBytes = desc.PushConstantBytes;
		pipeline->BufferCount = bufferCount;
		pipeline->TextureCount = textureCount;
		pipeline->SampledTextureCount = samplerCount;
		uint32 bufferIndex = 0;

		for (usize layoutIndex = 0; layoutIndex < desc.GlobalLayouts.Size(); ++layoutIndex)
		{
			for (uint32 item = 0; item < desc.GlobalLayouts[layoutIndex].BufferCount(); ++item)
			{
				const ComputeBindingDesc binding = desc.GlobalLayouts[layoutIndex].Buffer(item);
				globalParameters[bufferIndex].ParameterType =
					binding.Type == EComputeBindingType::UniformBuffer ? D3D12_ROOT_PARAMETER_TYPE_CBV
					: binding.Type == EComputeBindingType::ReadOnlyBuffer ? D3D12_ROOT_PARAMETER_TYPE_SRV
					: D3D12_ROOT_PARAMETER_TYPE_UAV;
				globalParameters[bufferIndex].Descriptor.ShaderRegister = binding.Slot;
				globalParameters[bufferIndex].Descriptor.RegisterSpace = static_cast<UINT>(layoutIndex);
				globalParameters[bufferIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

				if (binding.Format != EFormat::UNKNOWN)
				{
					bufferRanges[bufferIndex] = { binding.Type == EComputeBindingType::ReadOnlyBuffer ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
																									 : D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
												 1, binding.Slot, static_cast<UINT>(layoutIndex), D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
					globalParameters[bufferIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
					globalParameters[bufferIndex].DescriptorTable = { 1, &bufferRanges[bufferIndex] };
				}
				++bufferIndex;
			}
		}

		uint32 textureIndex = 0;
		uint32 samplerIndex = 0;

		for (usize layoutIndex = 0; layoutIndex < desc.GlobalLayouts.Size(); ++layoutIndex)
		{
			for (uint32 item = 0; item < desc.GlobalLayouts[layoutIndex].TextureCount(); ++item)
			{
				const ComputeTextureBindingDesc texture = desc.GlobalLayouts[layoutIndex].Texture(item);
				textureRanges[textureIndex] = { texture.Type == EComputeTextureBindingType::SampledTexture ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
																										  : D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
											   1, texture.Slot, static_cast<UINT>(layoutIndex), D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
				D3D12_ROOT_PARAMETER& parameter = globalParameters[bufferCount + textureIndex];
				parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				parameter.DescriptorTable = { 1, &textureRanges[textureIndex] };
				parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

				if (texture.Type == EComputeTextureBindingType::SampledTexture)
				{
					samplerRanges[samplerIndex] = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, texture.Slot, static_cast<UINT>(layoutIndex),
												   D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
					D3D12_ROOT_PARAMETER& samplerParameter = globalParameters[bufferCount + textureCount + samplerIndex];
					samplerParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
					samplerParameter.DescriptorTable = { 1, &samplerRanges[samplerIndex] };
					samplerParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
					++samplerIndex;
				}
				++textureIndex;
			}
		}
		rootDesc.NumParameters = pipeline->BufferCount + pipeline->TextureCount + samplerCount;

		if (desc.AccelStructSlot != 0xFFFFFFFFu)
		{
			pipeline->AccelRootIndex = rootDesc.NumParameters++;
			D3D12_ROOT_PARAMETER& parameter = globalParameters[pipeline->AccelRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			parameter.Descriptor.ShaderRegister = desc.AccelStructSlot;
			parameter.Descriptor.RegisterSpace = 0;
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.PushConstantBytes != 0)
		{
			pipeline->PushRootIndex = rootDesc.NumParameters++;
			D3D12_ROOT_PARAMETER& parameter = globalParameters[pipeline->PushRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			parameter.Constants.ShaderRegister = 0;
			parameter.Constants.RegisterSpace = 5;
			parameter.Constants.Num32BitValues = desc.PushConstantBytes / 4;
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		if (bRanges)
		{
			const uint32 offset = desc.PushConstantBytes != 0 ? 1u : 0u;
			if (offset != 0)
			{
				globalParameters[0] = globalParameters[pipeline->PushRootIndex];
				pipeline->PushRootIndex = 0;
			}
			FillRangeRootParameters(pipeline->Fixed, desc.GlobalLayouts, globalParameters, textureRanges, offset, false);
			rootDesc.NumParameters = offset + RangeRootCost(desc.GlobalLayouts);
		}
		rootDesc.pParameters = globalParameters;
		ComPtr<ID3DBlob> serialized;
		ComPtr<ID3DBlob> errors;

		if (FAILED( D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), errors.GetAddressOf())) ||
			FAILED(state.Device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(pipeline->RootSignature.GetAddressOf()))))
		{
			DestroyObject(state.Resource, pipeline);

			return Result<void*>::Failure(Failure("Rhi.D3D12RayPipeline", "Could not create the ray root signature."));
		}

		if (samplerCount + localSamplerCount != 0)
		{
			D3D12_DESCRIPTOR_HEAP_DESC heap{};
			heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			heap.NumDescriptors = samplerCount + localSamplerCount;

			if (FAILED(state.Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(pipeline->DefaultSamplers.GetAddressOf()))))
			{
				DestroyObject(state.Resource, pipeline);

				return Result<void*>::Failure(Failure("Rhi.D3D12RayPipeline", "Could not create default ray samplers."));
			}
			D3D12_CPU_DESCRIPTOR_HANDLE cpu = pipeline->DefaultSamplers->GetCPUDescriptorHandleForHeapStart();
			const UINT stride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

			for (const NativeBindingLayoutDesc& layout : desc.GlobalLayouts)
			{
				for (uint32 item = 0; item < layout.TextureCount(); ++item)
				{
					const ComputeTextureBindingDesc texture = layout.Texture(item);
					if (texture.Type != EComputeTextureBindingType::SampledTexture)
					{
						continue;
					}

					const D3D12_SAMPLER_DESC sampler = MakeSamplerDesc(texture.Sampler);
					state.Device->CreateSampler(&sampler, cpu);
					cpu.ptr += stride;
				}
			}

			uint32 offset = samplerCount;

			for (uint32 group = 0; group < desc.GeneralShaderCount + desc.HitGroups.Size(); ++group)
			{
				pipeline->LocalSamplerOffset[group] = offset;
				const NativeBindingLayoutDesc& local = group < desc.GeneralShaderCount ? desc.Shaders[group].LocalLayout : desc.HitGroups[group - desc.GeneralShaderCount].LocalLayout;

				for (uint32 item = 0; item < local.TextureCount(); ++item)
				{
					const ComputeTextureBindingDesc texture = local.Texture(item);
					if (texture.Type != EComputeTextureBindingType::SampledTexture)
					{
						continue;
					}

					const D3D12_SAMPLER_DESC sampler = MakeSamplerDesc(texture.Sampler);
					state.Device->CreateSampler(&sampler, cpu);
					cpu.ptr += stride;
					++offset;
				}
			}
		}
		D3D12_EXPORT_DESC exports[128]{};
		D3D12_DXIL_LIBRARY_DESC libraries[128]{};
		D3D12_HIT_GROUP_DESC hits[32]{};
		D3D12_STATE_SUBOBJECT subobjects[291]{};
		D3D12_LOCAL_ROOT_SIGNATURE localRoots[64]{};
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION localAssociations[64]{};
		const wchar_t* localExports[64]{};
		wchar_t importNames[128][64]{};
		wchar_t exportNames[128][32]{};
		wchar_t hitNames[32][32]{};
		uint32 objectCount = 0;

		for (usize index = 0; index < desc.Shaders.Size(); ++index)
		{
			const NativeRayShaderDesc& shader = desc.Shaders[index];
			const usize nameLength = CStringLength(shader.EntryPoint);

			if (nameLength == 0 || nameLength >= 64)
			{
				DestroyObject(state.Resource, pipeline);

				return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.D3D12RayPipeline", "The ray shader export name is invalid.");
			}

			for (usize character = 0; character < nameLength; ++character)
			{
				const unsigned char value = static_cast<unsigned char>(shader.EntryPoint[character]);

				if (value > 127)
				{
					DestroyObject(state.Resource, pipeline);

					return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.D3D12RayPipeline", "Ray shader exports must use ASCII names.");
				}
				importNames[index][character] = static_cast<wchar_t>(value);
			}
			(void)swprintf_s(exportNames[index], 32, L"RoxellRay%u", static_cast<unsigned>(index));
			exports[index].Name = exportNames[index];
			exports[index].ExportToRename = importNames[index];
			libraries[index].DXILLibrary.pShaderBytecode = shader.Bytecode.Data();
			libraries[index].DXILLibrary.BytecodeLength = shader.Bytecode.Size();
			libraries[index].NumExports = 1;
			libraries[index].pExports = &exports[index];
			subobjects[objectCount++] = { D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &libraries[index] };
		}

		for (usize index = 0; index < desc.HitGroups.Size(); ++index)
		{
			const NativeRayHitGroupDesc& group = desc.HitGroups[index];
			(void)swprintf_s(hitNames[index], 32, L"RoxellHit%u", static_cast<unsigned>(index));
			D3D12_HIT_GROUP_DESC& hit = hits[index];
			hit.Type = group.bProcedural ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE : D3D12_HIT_GROUP_TYPE_TRIANGLES;
			hit.HitGroupExport = hitNames[index];
			hit.ClosestHitShaderImport = group.ClosestHitIndex == 0xFFFFFFFFu ? nullptr : exportNames[group.ClosestHitIndex];
			hit.AnyHitShaderImport = group.AnyHitIndex == 0xFFFFFFFFu ? nullptr : exportNames[group.AnyHitIndex];
			hit.IntersectionShaderImport = group.IntersectionIndex == 0xFFFFFFFFu ? nullptr : exportNames[group.IntersectionIndex];
			subobjects[objectCount++] = { D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit };
		}
		D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
		shaderConfig.MaxPayloadSizeInBytes = desc.MaxPayloadBytes;
		shaderConfig.MaxAttributeSizeInBytes = desc.MaxAttributeBytes;
		subobjects[objectCount++] = { D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderConfig };
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
		pipelineConfig.MaxTraceRecursionDepth = desc.MaxRecursionDepth;
		subobjects[objectCount++] = { D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineConfig };
		D3D12_GLOBAL_ROOT_SIGNATURE globalRoot{ pipeline->RootSignature.Get() };
		subobjects[objectCount++] = { D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRoot };
		uint32 localCount = 0;
		auto addLocalRoot = [&](const NativeBindingLayoutDesc& layout, uint32 group, const wchar_t* exportName,
			uint32 localDataBytes, Span<const NativeRayLocalRootArgument> arguments) -> bool
			{
				pipeline->LocalRootArgumentOffsets[group] = static_cast<uint32>(pipeline->LocalRootArguments.Size());
				for (const rhi::NativeRayLocalRootArgument& argument : arguments)
				{
					pipeline->LocalRootArguments.AddRequired(argument);
				}
				pipeline->LocalRootArgumentOffsets[group + 1] = static_cast<uint32>(pipeline->LocalRootArguments.Size());
				if (!arguments.IsEmpty())
				{
					constexpr uint32 MAXIMUM_ARGUMENTS = (4096 - D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) / 4;
					if (arguments.Size() > MAXIMUM_ARGUMENTS)
					{
						return false;
					}
					D3D12_ROOT_PARAMETER parameters[MAXIMUM_ARGUMENTS]{};
					D3D12_DESCRIPTOR_RANGE ranges[MAXIMUM_ARGUMENTS]{};
					for (uint32 index = 0; index < arguments.Size(); ++index)
					{
						const rhi::NativeRayLocalRootArgument& argument = arguments[index];
						D3D12_ROOT_PARAMETER& parameter = parameters[index];
						parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
						switch (argument.Kind)
						{
						case ERayLocalRootArgumentKind::Constants32:
							parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
							parameter.Constants = { argument.Range.Binding, argument.Space, argument.ValueCount };
							break;
						case ERayLocalRootArgumentKind::ConstantBufferAddress:
						case ERayLocalRootArgumentKind::ShaderResourceAddress:
						case ERayLocalRootArgumentKind::UnorderedAccessAddress:
							parameter.ParameterType =
								argument.Kind == ERayLocalRootArgumentKind::ConstantBufferAddress ? D3D12_ROOT_PARAMETER_TYPE_CBV
								: argument.Kind == ERayLocalRootArgumentKind::ShaderResourceAddress ? D3D12_ROOT_PARAMETER_TYPE_SRV
								: D3D12_ROOT_PARAMETER_TYPE_UAV;
							parameter.Descriptor = { argument.Range.Binding, argument.Space };
							break;
						case ERayLocalRootArgumentKind::DescriptorTable:
							ranges[index] = { argument.Range.Type == EDescriptorType::Sampler ? D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
											 : argument.Range.Type == EDescriptorType::UniformBuffer ? D3D12_DESCRIPTOR_RANGE_TYPE_CBV
											 : argument.Range.Type == EDescriptorType::StorageBuffer ||
													 argument.Range.Type == EDescriptorType::StorageTexture
												 ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
												 : D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
											 argument.Range.DescriptorCount, argument.Range.Binding, argument.Space,
											 D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
							parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
							parameter.DescriptorTable = { 1, &ranges[index] };
							break;

						default:
						{
							break;
						}
						}
					}
					D3D12_ROOT_SIGNATURE_DESC localDesc{ static_cast<UINT>(arguments.Size()), parameters, 0, nullptr,
														D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE };
					ComPtr<ID3DBlob> serializedLocal;
					ComPtr<ID3DBlob> localErrors;
					if (FAILED(D3D12SerializeRootSignature(&localDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedLocal.GetAddressOf(), localErrors.GetAddressOf())) ||
						FAILED(state.Device->CreateRootSignature(0, serializedLocal->GetBufferPointer(), serializedLocal->GetBufferSize(), IID_PPV_ARGS(pipeline->LocalRootSignatures[group].GetAddressOf()))))
					{
						return false;
					}
					localRoots[localCount].pLocalRootSignature = pipeline->LocalRootSignatures[group].Get();
					const uint32 localObject = objectCount;
					subobjects[objectCount++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &localRoots[localCount] };
					localExports[localCount] = exportName;
					localAssociations[localCount] = { &subobjects[localObject], 1, &localExports[localCount] };
					subobjects[objectCount++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &localAssociations[localCount] };
					++localCount;

					return true;
				}
				if (layout.BufferCount() == 0 && layout.TextureCount() == 0 && localDataBytes == 0)
				{
					return true;
				}
				D3D12_ROOT_PARAMETER parameters[25]{};
				D3D12_DESCRIPTOR_RANGE textureRanges[8]{};
				D3D12_DESCRIPTOR_RANGE bufferRanges[8]{};
				D3D12_DESCRIPTOR_RANGE samplerRanges[8]{};
				for (usize index = 0; index < layout.BufferCount(); ++index)
				{
					parameters[index].ParameterType =
						layout.Buffer(static_cast<uint32>(index)).Type == EComputeBindingType::UniformBuffer
						? D3D12_ROOT_PARAMETER_TYPE_CBV
						: layout.Buffer(static_cast<uint32>(index)).Type == EComputeBindingType::ReadOnlyBuffer
						? D3D12_ROOT_PARAMETER_TYPE_SRV
						: D3D12_ROOT_PARAMETER_TYPE_UAV;
					parameters[index].Descriptor.ShaderRegister = layout.Buffer(static_cast<uint32>(index)).Slot;
					parameters[index].Descriptor.RegisterSpace = 4;
					parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

					if (layout.Buffer(static_cast<uint32>(index)).Format != EFormat::UNKNOWN)
					{
						bufferRanges[index] = { layout.Buffer(static_cast<uint32>(index)).Type == EComputeBindingType::ReadOnlyBuffer
												   ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
												   : D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
											   1, layout.Buffer(static_cast<uint32>(index)).Slot, 4,
											   D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
						parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
						parameters[index].DescriptorTable = { 1, &bufferRanges[index] };
					}
				}

				uint32 sampled = 0;

				for (usize index = 0; index < layout.TextureCount(); ++index)
				{
					const ComputeTextureBindingDesc& texture = layout.Texture(static_cast<uint32>(index));
					textureRanges[index] = { texture.Type == EComputeTextureBindingType::SampledTexture ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
																									   : D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
											1, texture.Slot, 4, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
					D3D12_ROOT_PARAMETER& parameter = parameters[layout.BufferCount() + index];
					parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
					parameter.DescriptorTable = { 1, &textureRanges[index] };
					parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

					if (texture.Type == EComputeTextureBindingType::SampledTexture)
					{
						samplerRanges[sampled] = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, texture.Slot, 4,
												  D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
						D3D12_ROOT_PARAMETER& sampler = parameters[layout.BufferCount() + layout.TextureCount() + sampled];
						sampler.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
						sampler.DescriptorTable = { 1, &samplerRanges[sampled] };
						sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
						++sampled;
					}
				}
				D3D12_ROOT_SIGNATURE_DESC localDesc{};
				localDesc.NumParameters = static_cast<UINT>(layout.BufferCount() + layout.TextureCount() + sampled);

				if (localDataBytes != 0)
				{
					D3D12_ROOT_PARAMETER& constants = parameters[localDesc.NumParameters++];
					constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
					constants.Constants = { 0, 6, localDataBytes / 4 };
					constants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
				}
				localDesc.pParameters = parameters;
				localDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
				ComPtr<ID3DBlob> serializedLocal;
				ComPtr<ID3DBlob> localErrors;

				if (FAILED(D3D12SerializeRootSignature(&localDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedLocal.GetAddressOf(), localErrors.GetAddressOf())) ||
					FAILED(state.Device->CreateRootSignature(0, serializedLocal->GetBufferPointer(), serializedLocal->GetBufferSize(), IID_PPV_ARGS(pipeline->LocalRootSignatures[group].GetAddressOf()))))
				{
					return false;
				}

				const uint32 associationIndex = objectCount + 1;
				localRoots[localCount].pLocalRootSignature = pipeline->LocalRootSignatures[group].Get();
				subobjects[objectCount++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &localRoots[localCount] };
				localExports[localCount] = exportName;
				localAssociations[localCount].pSubobjectToAssociate = &subobjects[associationIndex - 1];
				localAssociations[localCount].NumExports = 1;
				localAssociations[localCount].pExports = &localExports[localCount];
				subobjects[objectCount++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &localAssociations[localCount] };
				++localCount;

				return true;
			};
		for (uint32 index = 0; index < desc.GeneralShaderCount; ++index)
		{
			if (!addLocalRoot(desc.Shaders[index].LocalLayout, index, exportNames[index], desc.Shaders[index].LocalDataBytes,
				desc.Shaders[index].LocalRootArguments))
			{
				DestroyObject(state.Resource, pipeline);

				return Result<void*>::Failure(Failure("Rhi.D3D12RayPipeline", "Could not create a local ray root signature."));
			}
		}

		for (uint32 index = 0; index < desc.HitGroups.Size(); ++index)
		{
			if (!addLocalRoot(desc.HitGroups[index].LocalLayout, desc.GeneralShaderCount + index, hitNames[index],
				desc.HitGroups[index].LocalDataBytes, desc.HitGroups[index].LocalRootArguments))
			{
				DestroyObject(state.Resource, pipeline);

				return Result<void*>::Failure(Failure("Rhi.D3D12RayPipeline", "Could not create a local hit root signature."));
			}
		}
		D3D12_STATE_OBJECT_DESC stateDesc{};
		stateDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
		stateDesc.NumSubobjects = objectCount;
		stateDesc.pSubobjects = subobjects;

		if (FAILED(state.RayDevice->CreateStateObject(&stateDesc, IID_PPV_ARGS(pipeline->State.GetAddressOf()))))
		{
			DestroyObject(state.Resource, pipeline);

			return Result<void*>::Failure(Failure("Rhi.D3D12RayPipeline", "Could not create the ray tracing state object."));
		}
		ComPtr<ID3D12StateObjectProperties> properties;

		if (FAILED(pipeline->State.As(&properties)))
		{
			DestroyObject(state.Resource, pipeline);

			return Result<void*>::Failure(Failure("Rhi.D3D12RayPipeline", "Could not query ray shader identifiers."));
		}

		const usize groupCount = desc.GeneralShaderCount + desc.HitGroups.Size();

		if (!pipeline->GroupHandles.ResizeUninitialized(groupCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES))
		{
			DestroyObject(state.Resource, pipeline);

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not store ray shader identifiers."));
		}

		for (usize index = 0; index < groupCount; ++index)
		{
			const void* identifier = properties->GetShaderIdentifier( index < desc.GeneralShaderCount ? exportNames[index] : hitNames[index - desc.GeneralShaderCount]);

			if (identifier == nullptr)
			{
				DestroyObject(state.Resource, pipeline);

				return Result<void*>::Failure(Failure("Rhi.D3D12RayPipeline", "A ray shader identifier is missing."));
			}

			CopyBytes(pipeline->GroupHandles.Data() + index * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, identifier,
				D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		}

		return Result<void*>::Success(pipeline);
	}

	void DestroyRayTracingPipelineNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<RayTracingPipeline*>(native));
	}

	[[nodiscard]] Result<void> DestroyRayTracingPipeline(State* opaque, void* native)
	{
		State& state = *opaque;
		RayTracingPipeline* pipeline = static_cast<RayTracingPipeline*>(native);

		if (pipeline->LastUse > CompletedSerial(state))
		{
			if (!state.RetiredRayPipelines.Add({ pipeline, pipeline->LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retire a ray pipeline."));
			}
		}
		else
		{
			DestroyRayTracingPipelineNow(opaque, native);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> BindRayTextures(State& state, Pending& pending, const RayTracingPipeline& pipeline, Span<const NativeComputeTexture> textures)
	{
		if (textures.IsEmpty())
		{
			return Result<void>::Success();
		}
		constexpr UINT DESCRIPTORS_PER_HEAP = 256;

		if (pending.TextureHeaps.IsEmpty() || pending.TextureHeapUsed + textures.Size() > pending.TextureHeaps.Back()->GetDesc().NumDescriptors)
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc{};
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			desc.NumDescriptors = DESCRIPTORS_PER_HEAP;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			ComPtr<ID3D12DescriptorHeap> heap;

			if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap.GetAddressOf()))))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12RayDispatch", "Could not allocate ray texture descriptors."));
			}

			if (!pending.TextureHeaps.Add(Move(heap)))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain ray texture descriptors."));
			}
			pending.TextureHeapUsed = 0;
		}

		if (pipeline.SampledTextureCount != 0 && (pending.SamplerHeaps.IsEmpty() || pending.SamplerHeapUsed + pipeline.SampledTextureCount > pending.SamplerHeaps.Back()->GetDesc().NumDescriptors))
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc{};
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			desc.NumDescriptors = DESCRIPTORS_PER_HEAP;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			ComPtr<ID3D12DescriptorHeap> heap;

			if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap.GetAddressOf()))))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12RayDispatch", "Could not allocate ray samplers."));
			}

			if (!pending.SamplerHeaps.Add(Move(heap)))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain ray samplers."));
			}
			pending.SamplerHeapUsed = 0;
		}
		ID3D12DescriptorHeap* const heap = pending.TextureHeaps.Back().Get();
		ID3D12DescriptorHeap* const samplerHeap = pending.SamplerHeaps.IsEmpty() ? nullptr : pending.SamplerHeaps.Back().Get();

		if (pending.BoundTextureHeap != heap || pending.BoundSamplerHeap != samplerHeap)
		{
			ID3D12DescriptorHeap* heaps[2]{ heap, samplerHeap };
			pending.List->SetDescriptorHeaps(samplerHeap != nullptr ? 2 : 1, heaps);
			pending.BoundTextureHeap = heap;
			pending.BoundSamplerHeap = samplerHeap;
		}

		const UINT stride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
		cpu.ptr += static_cast<SIZE_T>(pending.TextureHeapUsed) * stride;
		gpu.ptr += static_cast<UINT64>(pending.TextureHeapUsed) * stride;
		D3D12_CPU_DESCRIPTOR_HANDLE samplerCpu =
			samplerHeap != nullptr ? samplerHeap->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};
		D3D12_GPU_DESCRIPTOR_HANDLE samplerGpu =
			samplerHeap != nullptr ? samplerHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
		samplerCpu.ptr += static_cast<SIZE_T>(pending.SamplerHeapUsed) * samplerStride;
		samplerGpu.ptr += static_cast<UINT64>(pending.SamplerHeapUsed) * samplerStride;
		uint32 samplerIndex = 0;

		for (usize index = 0; index < textures.Size(); ++index)
		{
			Texture& texture = *static_cast<Texture*>(textures[index].Texture);
			const bool bStorage = textures[index].Type == EComputeTextureBindingType::StorageTexture;
			TransitionTextureView(pending, texture, static_cast<const TextureView*>(textures[index].View),
				bStorage ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			CopyCommandDescriptors(state, pending, 1, cpu, TextureDescriptorCpu(texture, textures[index].View, bStorage),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			pending.RayList->SetComputeRootDescriptorTable(pipeline.BufferCount + static_cast<UINT>(index), gpu);

			if (!bStorage)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE source = pipeline.DefaultSamplers->GetCPUDescriptorHandleForHeapStart();
				source.ptr += static_cast<SIZE_T>(samplerIndex) * samplerStride;

				if (textures[index].Sampler != nullptr)
				{
					source = static_cast<const Sampler*>(textures[index].Sampler)->Descriptor->GetCPUDescriptorHandleForHeapStart();
				}
				CopyCommandDescriptors(state, pending, 1, samplerCpu, source, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
				pending.RayList->SetComputeRootDescriptorTable(pipeline.BufferCount + pipeline.TextureCount + samplerIndex, samplerGpu);
				samplerCpu.ptr += samplerStride;
				samplerGpu.ptr += samplerStride;
				++samplerIndex;
			}
			cpu.ptr += stride;
			gpu.ptr += stride;
		}
		pending.TextureHeapUsed += static_cast<uint32>(textures.Size());
		pending.SamplerHeapUsed += samplerIndex;

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
		NativeComputeBuffer flattenedBuffers[32]{};
		NativeComputeTexture flattenedTextures[32]{};
		uint32 bufferCount = 0;
		uint32 textureCount = 0;

		for (const NativeBindingSet& set : sets)
		{
			for (const NativeComputeBuffer& buffer : set.Buffers)
			{
				flattenedBuffers[bufferCount++] = buffer;
			}

			for (const NativeComputeTexture& texture : set.Textures)
			{
				flattenedTextures[textureCount++] = texture;
			}
		}

		const Span<const NativeComputeBuffer> buffers{ flattenedBuffers, bufferCount };
		const Span<const NativeComputeTexture> textures{ flattenedTextures, textureCount };
		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}
		constexpr uint64 maxRays = D3D12_RAYTRACING_MAX_RAY_GENERATION_SHADER_THREADS;

		ASSERT(width <= maxRays / height && !(static_cast<uint64>(width) * height > maxRays / depth), "The ray dispatch exceeds the device limit.");

		if (!pending.RayList && FAILED(pending.List.As(&pending.RayList)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12RayDispatch", "Could not query the ray command list."));
		}
		constexpr uint64 identifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		constexpr uint64 tableAlignment = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
		auto align = [](uint64 value) noexcept { return (value + tableAlignment - 1) & ~(tableAlignment - 1); };
		usize maximumLocalBytes = 0;
		uint32 localTextureCount = 0;
		uint32 localSamplerCount = 0;
		auto countLocal = [&](const NativeBindingSet& local)
			{
				uint32 sampled = 0;

				for (const NativeComputeTexture& texture : local.Textures)
				{
					sampled += texture.Type == EComputeTextureBindingType::SampledTexture ? 1u : 0u;
				}
				maximumLocalBytes = Max(maximumLocalBytes, (local.Buffers.Size() + local.Textures.Size() + sampled) * sizeof(uint64) + local.LocalData.Size());
				localTextureCount += static_cast<uint32>(local.Textures.Size()) + CountTypedBuffers(local.Buffers);
				localSamplerCount += sampled;
			};
		countLocal(table.RayGenerationLocal);

		for (const NativeBindingSet& local : table.MissLocals)
		{
			countLocal(local);
		}

		for (const NativeBindingSet& local : table.HitLocals)
		{
			countLocal(local);
		}

		for (const NativeBindingSet& local : table.CallableLocals)
		{
			countLocal(local);
		}

		const uint64 stride = (identifierSize + maximumLocalBytes + D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT - 1) & ~(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT - 1);
		const uint32 globalTypedCount = fixedSets.IsEmpty() ? CountTypedBuffers(buffers) : 0;
		uint32 fixedResourceCount = 0;
		uint32 fixedSamplerCount = 0;
		for (uint32 layout = 0; layout < pipeline.Fixed.LayoutCount; ++layout)
		{
			for (uint32 index = 0; index < pipeline.Fixed.RangeCounts[layout]; ++index)
			{
				const BindingRangeDesc& range = pipeline.Fixed.Ranges[layout][index];
				if (range.BufferMode == EBufferBindingMode::RootDescriptor && range.Source == EBindingRangeSource::Fixed)
				{
					continue;
				}
				(range.Type == EDescriptorType::Sampler ? fixedSamplerCount : fixedResourceCount) += range.DescriptorCount;
			}
		}

		if (localTextureCount + globalTypedCount + fixedResourceCount + fixedSamplerCount != 0)
		{
			constexpr UINT DESCRIPTORS_PER_HEAP = 256;
			const UINT requiredTextures = Max(1u, localTextureCount + pipeline.TextureCount + globalTypedCount + fixedResourceCount);
			const UINT requiredSamplers = localSamplerCount + pipeline.SampledTextureCount + fixedSamplerCount;

			if (pending.TextureHeaps.IsEmpty() || pending.TextureHeapUsed + requiredTextures > pending.TextureHeaps.Back()->GetDesc().NumDescriptors)
			{
				D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
				heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				heapDesc.NumDescriptors = Max(DESCRIPTORS_PER_HEAP, requiredTextures);
				heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				ComPtr<ID3D12DescriptorHeap> heap;

				if (FAILED(state.Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(heap.GetAddressOf()))))
				{
					return Result<void>::Failure(Failure("Rhi.D3D12RayDispatch", "Could not allocate local ray texture descriptors."));
				}

				if (!pending.TextureHeaps.Add(Move(heap)))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain local ray texture descriptors."));
				}
				pending.TextureHeapUsed = 0;
			}

			if (requiredSamplers != 0 && (pending.SamplerHeaps.IsEmpty() || pending.SamplerHeapUsed + requiredSamplers > pending.SamplerHeaps.Back()->GetDesc().NumDescriptors))
			{
				D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
				heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
				heapDesc.NumDescriptors = Max(DESCRIPTORS_PER_HEAP, requiredSamplers);
				heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				ComPtr<ID3D12DescriptorHeap> heap;

				if (FAILED(state.Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(heap.GetAddressOf()))))
				{
					return Result<void>::Failure(Failure("Rhi.D3D12RayDispatch", "Could not allocate local ray samplers."));
				}

				if (!pending.SamplerHeaps.Add(Move(heap)))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain local ray samplers."));
				}
				pending.SamplerHeapUsed = 0;
			}
			ID3D12DescriptorHeap* heaps[2]{ pending.TextureHeaps.Back().Get(),
										   requiredSamplers != 0 ? pending.SamplerHeaps.Back().Get() : nullptr };
			if (pending.BoundTextureHeap != heaps[0] || pending.BoundSamplerHeap != heaps[1])
			{
				pending.List->SetDescriptorHeaps(heaps[1] != nullptr ? 2 : 1, heaps);
				pending.BoundTextureHeap = heaps[0];
				pending.BoundSamplerHeap = heaps[1];
			}
		}

		const uint64 missOffset = align(stride);
		const uint64 hitOffset = align(missOffset + table.MissShaderIndices.Size() * stride);
		const uint64 callableOffset = align(hitOffset + table.HitGroupIndices.Size() * stride);
		const uint64 total = align(callableOffset + table.CallableShaderIndices.Size() * stride);
		ComPtr<ID3D12Resource> upload;

		if (!CreateResource(state, total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, upload))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12RayDispatch", "Could not allocate the shader table."));
		}
		void* mapped = nullptr;

		if (FAILED(upload->Map(0, nullptr, &mapped)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12RayDispatch", "Could not map the shader table."));
		}

		SetBytes(mapped, 0, static_cast<usize>(total));
		auto copyGroup = [&](uint64 offset, uint32 group, const NativeBindingSet& local)
			{
				CopyBytes(static_cast<Byte*>(mapped) + offset,
					pipeline.GroupHandles.Data() + static_cast<usize>(group) * identifierSize, identifierSize);

				for (usize index = 0; index < local.Buffers.Size(); ++index)
				{
					const NativeComputeBuffer& binding = local.Buffers[index];
					Buffer& buffer = *static_cast<Buffer*>(binding.Buffer);
					const D3D12_RESOURCE_STATES required =
						binding.Type == EComputeBindingType::UniformBuffer ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
						: binding.Type == EComputeBindingType::ReadOnlyBuffer ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
						: D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
					Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, required);
					D3D12_GPU_VIRTUAL_ADDRESS address = buffer.Resource->GetGPUVirtualAddress() + binding.ByteOffset;

					if (binding.View != nullptr)
					{
						const UINT descriptorStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						ID3D12DescriptorHeap* const heap = pending.TextureHeaps.Back().Get();
						D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
						cpu.ptr += static_cast<SIZE_T>(pending.TextureHeapUsed) * descriptorStride;
						address = heap->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<UINT64>(pending.TextureHeapUsed) * descriptorStride;
						CopyCommandDescriptors(state, pending, 1, cpu,
							static_cast<const BufferView*>(binding.View)->Descriptor->GetCPUDescriptorHandleForHeapStart(),
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						++pending.TextureHeapUsed;
					}

					CopyBytes(static_cast<Byte*>(mapped) + offset + identifierSize + index * sizeof(address), &address,
						sizeof(address));
				}

				const UINT textureStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
				uint32 sampled = 0;

				for (usize index = 0; index < local.Textures.Size(); ++index)
				{
					const NativeComputeTexture& binding = local.Textures[index];
					Texture& texture = *static_cast<Texture*>(binding.Texture);
					const bool bStorage = binding.Type == EComputeTextureBindingType::StorageTexture;
					TransitionTextureView(pending, texture, static_cast<const TextureView*>(binding.View),
						bStorage ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					ID3D12DescriptorHeap* const heap = pending.TextureHeaps.Back().Get();
					D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
					D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
					cpu.ptr += static_cast<SIZE_T>(pending.TextureHeapUsed) * textureStride;
					gpu.ptr += static_cast<UINT64>(pending.TextureHeapUsed) * textureStride;
					CopyCommandDescriptors(state, pending, 1, cpu, TextureDescriptorCpu(texture, binding.View, bStorage),
						D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					CopyBytes(static_cast<Byte*>(mapped) + offset + identifierSize +
						(local.Buffers.Size() + index) * sizeof(gpu.ptr),
						&gpu.ptr, sizeof(gpu.ptr));
					++pending.TextureHeapUsed;

					if (bStorage)
					{
						continue;
					}
					ID3D12DescriptorHeap* const samplerHeap = pending.SamplerHeaps.Back().Get();
					D3D12_CPU_DESCRIPTOR_HANDLE samplerCpu = samplerHeap->GetCPUDescriptorHandleForHeapStart();
					D3D12_GPU_DESCRIPTOR_HANDLE samplerGpu = samplerHeap->GetGPUDescriptorHandleForHeapStart();
					samplerCpu.ptr += static_cast<SIZE_T>(pending.SamplerHeapUsed) * samplerStride;
					samplerGpu.ptr += static_cast<UINT64>(pending.SamplerHeapUsed) * samplerStride;
					D3D12_CPU_DESCRIPTOR_HANDLE source = pipeline.DefaultSamplers->GetCPUDescriptorHandleForHeapStart();
					source.ptr += static_cast<SIZE_T>(pipeline.LocalSamplerOffset[group] + sampled) * samplerStride;

					if (binding.Sampler != nullptr)
					{
						source = static_cast<const Sampler*>(binding.Sampler)->Descriptor->GetCPUDescriptorHandleForHeapStart();
					}
					CopyCommandDescriptors(state, pending, 1, samplerCpu, source, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
					CopyBytes(static_cast<Byte*>(mapped) + offset + identifierSize +
						(local.Buffers.Size() + local.Textures.Size() + sampled) * sizeof(samplerGpu.ptr),
						&samplerGpu.ptr, sizeof(samplerGpu.ptr));
					++pending.SamplerHeapUsed;
					++sampled;
				}

				if (!local.LocalData.IsEmpty())
				{
					CopyBytes(static_cast<Byte*>(mapped) + offset + identifierSize +
						(local.Buffers.Size() + local.Textures.Size() + sampled) * sizeof(uint64),
						local.LocalData.Data(), local.LocalData.Size());
				}
			};
		copyGroup(0, table.RayGenerationShaderIndex, table.RayGenerationLocal);

		for (usize index = 0; index < table.MissShaderIndices.Size(); ++index)
		{
			copyGroup(missOffset + index * stride, table.MissShaderIndices[index], table.MissLocals[index]);
		}

		for (usize index = 0; index < table.HitGroupIndices.Size(); ++index)
		{
			copyGroup(hitOffset + index * stride, table.GeneralShaderCount + table.HitGroupIndices[index], table.HitLocals[index]);
		}

		for (usize index = 0; index < table.CallableShaderIndices.Size(); ++index)
		{
			copyGroup(callableOffset + index * stride, table.CallableShaderIndices[index], table.CallableLocals[index]);
		}
		upload->Unmap(0, nullptr);
		const D3D12_GPU_VIRTUAL_ADDRESS base = upload->GetGPUVirtualAddress();

		if (!pending.Temporary.Add(Move(upload)))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain the shader table."));
		}
		D3D12_DISPATCH_RAYS_DESC dispatch{};
		dispatch.RayGenerationShaderRecord = { base, stride };
		if (!table.MissShaderIndices.IsEmpty())
		{
			dispatch.MissShaderTable = { base + missOffset, static_cast<uint64>(table.MissShaderIndices.Size()) * stride, stride };
		}

		if (!table.HitGroupIndices.IsEmpty())
		{
			dispatch.HitGroupTable = { base + hitOffset, static_cast<uint64>(table.HitGroupIndices.Size()) * stride, stride };
		}

		if (!table.CallableShaderIndices.IsEmpty())
		{
			dispatch.CallableShaderTable = { base + callableOffset, static_cast<uint64>(table.CallableShaderIndices.Size()) * stride,
											stride };
		}
		dispatch.Width = width;
		dispatch.Height = height;
		dispatch.Depth = depth;
		pending.RayList->SetComputeRootSignature(pipeline.RootSignature.Get());
		pending.RayList->SetPipelineState1(pipeline.State.Get());

		if (pipeline.PushRootIndex != UINT32_MAX)
		{
			pending.RayList->SetComputeRoot32BitConstants(pipeline.PushRootIndex, static_cast<UINT>(pushConstants.Size() / 4),
				pushConstants.Data(), 0);
		}

		if (!fixedSets.IsEmpty())
		{
			Result<void> bound = WriteFixedBindingSets(opaque, command, native, false, fixedSets, nullptr, &pipeline, true);
			if (!bound)
			{
				return bound;
			}
			if (pipeline.PushRootIndex != UINT32_MAX)
			{
				pending.RayList->SetComputeRoot32BitConstants(pipeline.PushRootIndex,
					static_cast<UINT>(pushConstants.Size() / 4), pushConstants.Data(), 0);
			}
		}
		else
		{
			for (usize index = 0; index < buffers.Size(); ++index)
			{
				Buffer& buffer = *static_cast<Buffer*>(buffers[index].Buffer);
				const bool bUniform = buffers[index].Type == EComputeBindingType::UniformBuffer;
				const bool bReadOnly = buffers[index].Type == EComputeBindingType::ReadOnlyBuffer;
				bool bFirst = true;

				for (usize previous = 0; previous < index; ++previous)
				{
					if (buffers[previous].Buffer == buffers[index].Buffer)
					{
						bFirst = false;
						break;
					}
				}

				if (bFirst)
				{
					Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
						bUniform ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
						: bReadOnly ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
						: D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				}

				const D3D12_GPU_VIRTUAL_ADDRESS address = buffer.Resource->GetGPUVirtualAddress() + buffers[index].ByteOffset;

				if (buffers[index].View != nullptr)
				{
					continue;
				}

				if (bUniform)
				{
					pending.RayList->SetComputeRootConstantBufferView(static_cast<UINT>(index), address);
				}
				else if (bReadOnly)
				{
					pending.RayList->SetComputeRootShaderResourceView(static_cast<UINT>(index), address);
				}
				else
				{
					pending.RayList->SetComputeRootUnorderedAccessView(static_cast<UINT>(index), address);
				}
			}

			if (accelNative != nullptr)
			{
				pending.RayList->SetComputeRootShaderResourceView(pipeline.AccelRootIndex,
					static_cast<const NativeAccelStruct*>(accelNative)->Address);
			}

			Result<void> boundTextures = BindRayTextures(state, pending, pipeline, textures);

			if (!boundTextures)
			{
				return boundTextures;
			}

			if (globalTypedCount != 0)
			{
				const UINT descriptorStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				ID3D12DescriptorHeap* const heap = pending.TextureHeaps.Back().Get();
				D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
				cpu.ptr += static_cast<SIZE_T>(pending.TextureHeapUsed) * descriptorStride;
				pending.TypedBufferGpu = heap->GetGPUDescriptorHandleForHeapStart();
				pending.TypedBufferGpu.ptr += static_cast<UINT64>(pending.TextureHeapUsed) * descriptorStride;
				CopyTypedBufferDescriptors(state, pending, buffers, cpu);
				BindTypedBufferRoots(pending, buffers, 0, false);
				pending.TextureHeapUsed += globalTypedCount;
			}
		}
		pending.RayList->DispatchRays(&dispatch);

		for (const NativeComputeTexture& binding : textures)
		{
			Texture& texture = *static_cast<Texture*>(binding.Texture);
			Transition(pending, *texture.Resource.Get(),
				binding.Type == EComputeTextureBindingType::StorageTexture ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_COMMON);
		}

		auto restoreLocal = [&](const NativeBindingSet& local)
			{
				for (const NativeComputeTexture& binding : local.Textures)
				{
					Texture& texture = *static_cast<Texture*>(binding.Texture);
					Transition(pending, *texture.Resource.Get(),
						binding.Type == EComputeTextureBindingType::StorageTexture ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_COMMON);
				}
			};
		restoreLocal(table.RayGenerationLocal);

		for (const NativeBindingSet& local : table.MissLocals)
		{
			restoreLocal(local);
		}

		for (const NativeBindingSet& local : table.HitLocals)
		{
			restoreLocal(local);
		}

		for (const NativeBindingSet& local : table.CallableLocals)
		{
			restoreLocal(local);
		}

		return Result<void>::Success();
	}
}
#endif
