#include "D3D12Backend.h"
#include "D3D12Descriptors.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<void*> CreateGraphicsPipeline(State* opaque, const NativeGraphicsPipelineDesc& desc, Span<const NativeGraphicsBindingLayoutDesc> layouts)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		ASSERT(!layouts.IsEmpty() && layouts.Size() <= MAXIMUM_FIXED_BINDING_LAYOUTS, "The graphics layout count is invalid.");
		GraphicsTextureBindingDesc sampledBindings[32]{};
		GraphicsStorageTextureBindingDesc storageBindings[32]{};
		GraphicsBufferBindingDesc bufferBindings[32]{};
		uint32 sampledSpaces[32]{};
		uint32 storageSpaces[32]{};
		uint32 bufferSpaces[32]{};
		uint32 sampledCount = 0;
		uint32 storageCount = 0;
		uint32 bufferCount = 0;

		for (uint32 layoutIndex = 0; layoutIndex < layouts.Size(); ++layoutIndex)
		{
			const rhi::NativeGraphicsBindingLayoutDesc& layout = layouts[layoutIndex];

			if (layout.SampledCount() > 32 - sampledCount || layout.StorageCount() > 32 - storageCount || layout.BufferCount() > 32 - bufferCount)
			{
				return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Graphics", "The graphics layout exceeds native binding capacity.");
			}

			for (uint32 bindingIndex = 0; bindingIndex < layout.SampledCount(); ++bindingIndex)
			{
				const GraphicsTextureBindingDesc binding = layout.Sampled(bindingIndex);
				sampledBindings[sampledCount] = binding;
				sampledSpaces[sampledCount++] = layoutIndex;
			}

			for (uint32 bindingIndex = 0; bindingIndex < layout.StorageCount(); ++bindingIndex)
			{
				const GraphicsStorageTextureBindingDesc binding = layout.Storage(bindingIndex);
				storageBindings[storageCount] = binding;
				storageSpaces[storageCount++] = layoutIndex;
			}

			for (uint32 bindingIndex = 0; bindingIndex < layout.BufferCount(); ++bindingIndex)
			{
				const GraphicsBufferBindingDesc binding = layout.Buffer(bindingIndex);
				bufferBindings[bufferCount] = binding;
				bufferSpaces[bufferCount++] = layoutIndex;
			}
		}

		if (desc.bDeviceHeapIndexed && (!state.bDeviceHeapIndexingEnabled || !state.GlobalResourceHeap || !state.GlobalSamplerHeap))
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Graphics", "The device descriptor heaps are unavailable.");
		}

		const uint32 descriptorTables =
			(desc.BindlessSampledTextureCapacity != 0 ? 1u : 0u) + (desc.BindlessSamplerCapacity != 0 ? 1u : 0u) +
			(desc.BindlessUniformBufferCapacity != 0 ? 1u : 0u) + (desc.BindlessStorageBufferCapacity != 0 ? 1u : 0u) +
			(desc.BindlessStorageTextureCapacity != 0 ? 1u : 0u) + (desc.BindlessReadOnlyBufferCapacity != 0 ? 1u : 0u);
		const uint32 rootCost = desc.PushConstantBytes / 4 +
			(layouts[0].bRangeLayout ? RangeRootDwordCost(layouts)
				: sampledCount * 2 + bufferCount * 2 + storageCount + descriptorTables +
				(desc.AccelStructSlot != 0xFFFFFFFFu ? 2u : 0u));

		if (rootCost > 64)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Graphics", "The graphics root signature exceeds the D3D12 limit.");
		}
		GraphicsPipeline* pipeline = CreateObject<GraphicsPipeline>(state.Resource);

		if (pipeline == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a graphics pipeline."));
		}
		D3D12_ROOT_SIGNATURE_DESC rootDesc{};
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		D3D12_ROOT_PARAMETER parameters[136]{};
		D3D12_DESCRIPTOR_RANGE ranges[32]{};
		D3D12_DESCRIPTOR_RANGE bufferRanges[32]{};
		D3D12_DESCRIPTOR_RANGE samplerRanges[32]{};
		D3D12_DESCRIPTOR_RANGE storageRanges[32]{};
		D3D12_DESCRIPTOR_RANGE bindlessRange{};
		D3D12_DESCRIPTOR_RANGE bindlessSamplerRange{};
		D3D12_DESCRIPTOR_RANGE bindlessStorageBufferRange{};
		D3D12_DESCRIPTOR_RANGE bindlessStorageTextureRange{};
		D3D12_DESCRIPTOR_RANGE bindlessUniformBufferRange{};
		D3D12_DESCRIPTOR_RANGE bindlessReadOnlyBufferRange{};
		const uint32 textureRoot = desc.PushConstantBytes != 0 ? 1 : 0;
		const uint32 samplerRoot = textureRoot + sampledCount;

		if (desc.PushConstantBytes != 0)
		{
			parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			parameters[0].Constants.ShaderRegister = 0;
			parameters[0].Constants.RegisterSpace = layouts.Size() > 1 ? static_cast<UINT>(layouts.Size()) : 0;
			parameters[0].Constants.Num32BitValues = desc.PushConstantBytes / 4;
			parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		for (uint32 index = 0; index < sampledCount; ++index)
		{
			const GraphicsTextureBindingDesc& binding = sampledBindings[index];
			ranges[index] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, binding.Slot, sampledSpaces[index], D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			parameters[textureRoot + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameters[textureRoot + index].DescriptorTable = { 1, &ranges[index] };
			parameters[textureRoot + index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			samplerRanges[index] = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1,
									binding.SamplerSlot == 0xffffffffu ? binding.Slot : binding.SamplerSlot, sampledSpaces[index],
									D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			parameters[samplerRoot + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameters[samplerRoot + index].DescriptorTable = { 1, &samplerRanges[index] };
			parameters[samplerRoot + index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		const uint32 bufferRoot = samplerRoot + sampledCount;

		for (uint32 index = 0; index < bufferCount; ++index)
		{
			const GraphicsBufferBindingDesc& binding = bufferBindings[index];
			parameters[bufferRoot + index].ParameterType =
				binding.Type == EGraphicsBufferBindingType::UniformBuffer ? D3D12_ROOT_PARAMETER_TYPE_CBV
				: binding.Type == EGraphicsBufferBindingType::ReadOnlyBuffer ? D3D12_ROOT_PARAMETER_TYPE_SRV
				: D3D12_ROOT_PARAMETER_TYPE_UAV;
			parameters[bufferRoot + index].Descriptor.ShaderRegister = binding.RegisterSlot == 0xffffffffu ? binding.Slot : binding.RegisterSlot;
			parameters[bufferRoot + index].Descriptor.RegisterSpace = bufferSpaces[index];
			parameters[bufferRoot + index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			if (binding.Format != EFormat::UNKNOWN)
			{
				bufferRanges[index] = { binding.Type == EGraphicsBufferBindingType::ReadOnlyBuffer ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
																								  : D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
									   1, binding.RegisterSlot == 0xffffffffu ? binding.Slot : binding.RegisterSlot, bufferSpaces[index],
									   D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
				parameters[bufferRoot + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				parameters[bufferRoot + index].DescriptorTable = { 1, &bufferRanges[index] };
			}
		}
		pipeline->StorageTextureRootIndex = bufferRoot + bufferCount;

		for (uint32 index = 0; index < storageCount; ++index)
		{
			storageRanges[index] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, storageBindings[index].Slot, storageSpaces[index],
									D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->StorageTextureRootIndex + index];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &storageRanges[index] };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		rootDesc.NumParameters = pipeline->StorageTextureRootIndex + storageCount;
		const UINT bindlessSpace = layouts.Size() > 1 ? static_cast<UINT>(layouts.Size() + 1) : 2;
		const UINT bindlessStorageTextureSpace = bindlessSpace + 1;

		if (desc.BindlessSampledTextureCapacity != 0)
		{
			pipeline->BindlessRootIndex = rootDesc.NumParameters++;
			bindlessRange = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, desc.BindlessSampledTextureCapacity, 0, bindlessSpace,
							 D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.BindlessSamplerCapacity != 0)
		{
			pipeline->BindlessSamplerRootIndex = rootDesc.NumParameters++;
			bindlessSamplerRange = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, desc.BindlessSamplerCapacity, 0, bindlessSpace,
									D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessSamplerRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessSamplerRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.BindlessUniformBufferCapacity != 0)
		{
			pipeline->BindlessUniformBufferRootIndex = rootDesc.NumParameters++;
			bindlessUniformBufferRange = { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, desc.BindlessUniformBufferCapacity, 0, bindlessSpace,
										  D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessUniformBufferRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessUniformBufferRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.BindlessReadOnlyBufferCapacity != 0)
		{
			pipeline->BindlessReadOnlyBufferRootIndex = rootDesc.NumParameters++;
			bindlessReadOnlyBufferRange = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, desc.BindlessReadOnlyBufferCapacity, 0, bindlessSpace + 2,
										   D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessReadOnlyBufferRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessReadOnlyBufferRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.BindlessStorageBufferCapacity != 0)
		{
			pipeline->BindlessStorageBufferRootIndex = rootDesc.NumParameters++;
			bindlessStorageBufferRange = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, desc.BindlessStorageBufferCapacity, 0, bindlessSpace,
										  D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessStorageBufferRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessStorageBufferRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.BindlessStorageTextureCapacity != 0)
		{
			pipeline->BindlessStorageTextureRootIndex = rootDesc.NumParameters++;
			bindlessStorageTextureRange = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, desc.BindlessStorageTextureCapacity, 0,
										   bindlessStorageTextureSpace, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->BindlessStorageTextureRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { 1, &bindlessStorageTextureRange };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		if (desc.AccelStructSlot != 0xFFFFFFFFu)
		{
			pipeline->AccelRootIndex = rootDesc.NumParameters++;
			D3D12_ROOT_PARAMETER& parameter = parameters[pipeline->AccelRootIndex];
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			parameter.Descriptor.ShaderRegister = desc.AccelStructSlot;
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		D3D12_DESCRIPTOR_RANGE fixedRanges[MAXIMUM_FIXED_BINDING_LAYOUTS * MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
		if (layouts[0].bRangeLayout)
		{
			FillRangeRootParameters(pipeline->Fixed, layouts, parameters, fixedRanges, textureRoot, true);
			rootDesc.NumParameters = textureRoot + RangeRootCost(layouts);
		}
		rootDesc.pParameters = parameters;
		ComPtr<ID3DBlob> serialized;
		ComPtr<ID3DBlob> errors;
		const HRESULT serializedResult =
			desc.bHeapDirectlyIndexed
			? SerializeDirectRootSignature(rootDesc, serialized.GetAddressOf(), errors.GetAddressOf())
			: D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), errors.GetAddressOf());

		if (FAILED(serializedResult) || FAILED(state.Device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(pipeline->RootSignature.GetAddressOf()))))
		{
			DestroyObject(state.Resource, pipeline);

			return Result<void*>::Failure(Failure("Rhi.D3D12Graphics", "Could not create the graphics root signature."));
		}

		if (sampledCount != 0 && pipeline->Fixed.LayoutCount == 0)
		{
			D3D12_DESCRIPTOR_HEAP_DESC samplerHeap{};
			samplerHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			samplerHeap.NumDescriptors = sampledCount;

			if (FAILED(state.Device->CreateDescriptorHeap(&samplerHeap, IID_PPV_ARGS(pipeline->DefaultSamplers.GetAddressOf()))))
			{
				DestroyObject(state.Resource, pipeline);

				return Result<void*>::Failure(Failure("Rhi.D3D12Sampler", "Could not create pipeline samplers."));
			}
			D3D12_CPU_DESCRIPTOR_HANDLE cpu = pipeline->DefaultSamplers->GetCPUDescriptorHandleForHeapStart();
			const UINT stride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

			for (uint32 index = 0; index < sampledCount; ++index)
			{
				const D3D12_SAMPLER_DESC sampler = MakeSamplerDesc(sampledBindings[index].Sampler);
				state.Device->CreateSampler(&sampler, cpu);
				cpu.ptr += stride;
			}
		}
		D3D12_GRAPHICS_PIPELINE_STATE_DESC nativeDesc{};
		D3D12_INPUT_ELEMENT_DESC elements[16]{};
		for (usize index = 0; index < desc.VertexAttributes.Size(); ++index)
		{
			const VertexAttributeDesc& attribute = desc.VertexAttributes[index];
			const bool bPerInstance = attribute.BufferSlot != 0 && desc.ExtraVertexBufferSlots[attribute.BufferSlot - 1].bPerInstance;

			if (FindByte(attribute.SemanticName, 0, sizeof(attribute.SemanticName)) == nullptr)
			{
				DestroyObject(state.Resource, pipeline);

				return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.VertexSemantic", "The vertex semantic is not terminated.");
			}
			elements[index] = { attribute.SemanticName[0] == 0 ? "ATTRIBUTE" : attribute.SemanticName,
							   attribute.SemanticName[0] == 0 ? attribute.Location : attribute.SemanticIndex,
							   GetDxgiFormatMapping(attribute.Format).SrvFormat,
							   attribute.BufferSlot,
							   attribute.OffsetBytes,
							   bPerInstance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
							   bPerInstance ? 1u : 0u };
		}
		nativeDesc.InputLayout = { elements, static_cast<UINT>(desc.VertexAttributes.Size()) };
		nativeDesc.pRootSignature = pipeline->RootSignature.Get();
		nativeDesc.VS = { desc.VertexShaderBytecode.Data(), desc.VertexShaderBytecode.Size() };
		nativeDesc.HS = { desc.HullShaderBytecode.Data(), desc.HullShaderBytecode.Size() };
		nativeDesc.DS = { desc.DomainShaderBytecode.Data(), desc.DomainShaderBytecode.Size() };
		nativeDesc.GS = { desc.GeometryShaderBytecode.Data(), desc.GeometryShaderBytecode.Size() };
		nativeDesc.PS = { desc.FragmentShaderBytecode.Data(), desc.FragmentShaderBytecode.Size() };
		const uint32 colorCount = desc.ColorFormat == EFormat::UNKNOWN ? 0 : desc.ColorFormats.IsEmpty() ? 1 : static_cast<uint32>(desc.ColorFormats.Size());

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
			D3D12_RENDER_TARGET_BLEND_DESC& target = nativeDesc.BlendState.RenderTarget[color];
			target.BlendEnable = colorBlend.bEnable;
			target.SrcBlend = D3D_BLEND_FACTORS[static_cast<usize>(colorBlend.SourceColor)];
			target.DestBlend = D3D_BLEND_FACTORS[static_cast<usize>(colorBlend.DestinationColor)];
			target.BlendOp = D3D_BLEND_OPERATIONS[static_cast<usize>(colorBlend.ColorOperation)];
			target.SrcBlendAlpha = D3D_BLEND_FACTORS[static_cast<usize>(colorBlend.SourceAlpha)];
			target.DestBlendAlpha = D3D_BLEND_FACTORS[static_cast<usize>(colorBlend.DestinationAlpha)];
			target.BlendOpAlpha = D3D_BLEND_OPERATIONS[static_cast<usize>(colorBlend.AlphaOperation)];
			target.RenderTargetWriteMask = colorBlend.WriteMask;
			const EFormat format = desc.ColorFormats.IsEmpty() ? desc.ColorFormat : desc.ColorFormats[color];
			nativeDesc.RTVFormats[color] = GetDxgiFormatMapping(format).RtvFormat;
		}
		nativeDesc.BlendState.IndependentBlendEnable = TRUE;
		nativeDesc.SampleMask = UINT_MAX;
		nativeDesc.RasterizerState.FillMode = desc.Raster.FillMode == EFillMode::Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
		nativeDesc.RasterizerState.CullMode = desc.Raster.CullMode == ECullMode::Front ? D3D12_CULL_MODE_FRONT : desc.Raster.CullMode == ECullMode::Back ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
		nativeDesc.RasterizerState.FrontCounterClockwise = desc.Raster.FrontFace == EFrontFace::CounterClockwise;
		nativeDesc.RasterizerState.DepthClipEnable = desc.Raster.bDepthClipEnabled;
		nativeDesc.RasterizerState.DepthBias = desc.Raster.DepthBias;
		nativeDesc.RasterizerState.DepthBiasClamp = desc.Raster.DepthBiasClamp;
		nativeDesc.RasterizerState.SlopeScaledDepthBias = desc.Raster.SlopeScaledDepthBias;
		nativeDesc.RasterizerState.ConservativeRaster = desc.Raster.bConservativeRasterEnabled ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		nativeDesc.DepthStencilState.DepthEnable = desc.bDepthTest || desc.bDepthWrite;
		nativeDesc.DepthStencilState.DepthWriteMask = desc.bDepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		nativeDesc.DepthStencilState.DepthFunc = desc.bDepthTest ? D3D_COMPARE[static_cast<usize>(desc.DepthCompare)] : D3D12_COMPARISON_FUNC_ALWAYS;
		nativeDesc.DepthStencilState.StencilEnable = desc.Stencil.bEnable;
		nativeDesc.DepthStencilState.StencilReadMask = desc.Stencil.ReadMask;
		nativeDesc.DepthStencilState.StencilWriteMask = desc.Stencil.WriteMask;
		const auto stencilFace = [](const StencilFaceDesc& face) noexcept
			{
				D3D12_DEPTH_STENCILOP_DESC result{};
				result.StencilFailOp = D3D_STENCIL_OPERATIONS[static_cast<usize>(face.FailOp)];
				result.StencilDepthFailOp = D3D_STENCIL_OPERATIONS[static_cast<usize>(face.DepthFailOp)];
				result.StencilPassOp = D3D_STENCIL_OPERATIONS[static_cast<usize>(face.PassOp)];
				result.StencilFunc = D3D_COMPARE[static_cast<usize>(face.Compare)];

				return result;
			};
		nativeDesc.DepthStencilState.FrontFace = stencilFace(desc.Stencil.FrontFace);
		nativeDesc.DepthStencilState.BackFace = stencilFace(desc.Stencil.BackFace);
		nativeDesc.DSVFormat = desc.DepthFormat == EFormat::UNKNOWN ? DXGI_FORMAT_UNKNOWN : GetDxgiFormatMapping(desc.DepthFormat).RtvFormat;
		nativeDesc.PrimitiveTopologyType = D3D_TOPOLOGY_TYPES[static_cast<usize>(desc.Topology)];
		nativeDesc.NumRenderTargets = colorCount;
		bool bSupportsSamples = true;

		for (uint32 color = 0; color < colorCount; ++color)
		{
			bSupportsSamples &= SupportsSamples(state, nativeDesc.RTVFormats[color], desc.SampleCount);
		}

		if (!bSupportsSamples || (desc.DepthFormat != EFormat::UNKNOWN && !SupportsSamples(state, nativeDesc.DSVFormat, desc.SampleCount)))
		{
			DestroyObject(state.Resource, pipeline);

			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.Multisample", "The D3D12 device does not support this pipeline sample count.");
		}
		nativeDesc.SampleDesc.Count = desc.SampleCount;
		const D3D12_DEPTH_STENCIL_DESC1 depthState{ nativeDesc.DepthStencilState.DepthEnable,
												   nativeDesc.DepthStencilState.DepthWriteMask,
												   nativeDesc.DepthStencilState.DepthFunc,
												   nativeDesc.DepthStencilState.StencilEnable,
												   nativeDesc.DepthStencilState.StencilReadMask,
												   nativeDesc.DepthStencilState.StencilWriteMask,
												   nativeDesc.DepthStencilState.FrontFace,
												   nativeDesc.DepthStencilState.BackFace,
												   desc.DepthBounds.bEnable };
		HRESULT created = E_FAIL;

		if (!desc.MeshShaderBytecode.IsEmpty())
		{
#pragma warning(push)
#pragma warning(disable : 4324)
			struct MeshPipelineStream final
			{
				using Subobject = __declspec(align(sizeof(void*))) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE;
				Subobject RootType;
				ID3D12RootSignature* Root;

				Subobject TopologyType;
				D3D12_PRIMITIVE_TOPOLOGY_TYPE Topology;

				Subobject TaskType;
				D3D12_SHADER_BYTECODE Task;

				Subobject MeshType;
				D3D12_SHADER_BYTECODE Mesh;

				Subobject FragmentType;
				D3D12_SHADER_BYTECODE Fragment;

				Subobject RasterType;
				D3D12_RASTERIZER_DESC Raster;

				Subobject DepthType;
				D3D12_DEPTH_STENCIL_DESC1 Depth;

				Subobject BlendType;
				D3D12_BLEND_DESC Blend;

				Subobject SamplesType;
				DXGI_SAMPLE_DESC Samples;

				Subobject SampleMaskType;
				UINT SampleMask;

				Subobject TargetsType;
				D3D12_RT_FORMAT_ARRAY Targets;

				Subobject DepthFormatType;
				DXGI_FORMAT DepthFormat;
			} stream{};
#pragma warning(pop)
			stream.RootType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
			stream.Root = nativeDesc.pRootSignature;
			stream.TopologyType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY;
			stream.Topology = nativeDesc.PrimitiveTopologyType;
			stream.TaskType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS;
			stream.Task = { desc.TaskShaderBytecode.Data(), desc.TaskShaderBytecode.Size() };
			stream.MeshType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
			stream.Mesh = { desc.MeshShaderBytecode.Data(), desc.MeshShaderBytecode.Size() };
			stream.FragmentType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
			stream.Fragment = nativeDesc.PS;
			stream.RasterType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER;
			stream.Raster = nativeDesc.RasterizerState;
			stream.DepthType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1;
			stream.Depth = depthState;
			stream.BlendType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND;
			stream.Blend = nativeDesc.BlendState;
			stream.SamplesType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
			stream.Samples = nativeDesc.SampleDesc;
			stream.SampleMaskType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK;
			stream.SampleMask = nativeDesc.SampleMask;
			stream.TargetsType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
			stream.Targets.NumRenderTargets = nativeDesc.NumRenderTargets;

			for (uint32 index = 0; index < nativeDesc.NumRenderTargets; ++index)
			{
				stream.Targets.RTFormats[index] = nativeDesc.RTVFormats[index];
			}
			stream.DepthFormatType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT;
			stream.DepthFormat = nativeDesc.DSVFormat;
			ComPtr<ID3D12Device2> device2;

			if (SUCCEEDED(state.Device.As(&device2)))
			{
				D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{ sizeof(stream), &stream };
				created = device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(pipeline->State.GetAddressOf()));
			}
		}
		else if (desc.DepthBounds.bEnable)
		{
#pragma warning(push)
#pragma warning(disable : 4324)
			struct BoundsPipelineStream final
			{
				using Subobject = __declspec(align(sizeof(void*))) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE;
				Subobject RootType;
				ID3D12RootSignature* Root;

				Subobject InputType;
				D3D12_INPUT_LAYOUT_DESC Input;

				Subobject TopologyType;
				D3D12_PRIMITIVE_TOPOLOGY_TYPE Topology;

				Subobject VertexType;
				D3D12_SHADER_BYTECODE Vertex;

				Subobject FragmentType;
				D3D12_SHADER_BYTECODE Fragment;

				Subobject GeometryType;
				D3D12_SHADER_BYTECODE Geometry;

				Subobject HullType;
				D3D12_SHADER_BYTECODE Hull;

				Subobject DomainType;
				D3D12_SHADER_BYTECODE Domain;

				Subobject RasterType;
				D3D12_RASTERIZER_DESC Raster;

				Subobject DepthType;
				D3D12_DEPTH_STENCIL_DESC1 Depth;

				Subobject BlendType;
				D3D12_BLEND_DESC Blend;

				Subobject SamplesType;
				DXGI_SAMPLE_DESC Samples;

				Subobject SampleMaskType;
				UINT SampleMask;

				Subobject TargetsType;
				D3D12_RT_FORMAT_ARRAY Targets;

				Subobject DepthFormatType;
				DXGI_FORMAT DepthFormat;
			} stream{};
#pragma warning(pop)
			stream.RootType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
			stream.Root = nativeDesc.pRootSignature;
			stream.InputType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT;
			stream.Input = nativeDesc.InputLayout;
			stream.TopologyType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY;
			stream.Topology = nativeDesc.PrimitiveTopologyType;
			stream.VertexType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS;
			stream.Vertex = nativeDesc.VS;
			stream.FragmentType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
			stream.Fragment = nativeDesc.PS;
			stream.GeometryType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS;
			stream.Geometry = nativeDesc.GS;
			stream.HullType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS;
			stream.Hull = nativeDesc.HS;
			stream.DomainType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS;
			stream.Domain = nativeDesc.DS;
			stream.RasterType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER;
			stream.Raster = nativeDesc.RasterizerState;
			stream.DepthType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1;
			stream.Depth = depthState;
			stream.BlendType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND;
			stream.Blend = nativeDesc.BlendState;
			stream.SamplesType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
			stream.Samples = nativeDesc.SampleDesc;
			stream.SampleMaskType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK;
			stream.SampleMask = nativeDesc.SampleMask;
			stream.TargetsType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
			stream.Targets.NumRenderTargets = nativeDesc.NumRenderTargets;
			for (uint32 index = 0; index < nativeDesc.NumRenderTargets; ++index)
			{
				stream.Targets.RTFormats[index] = nativeDesc.RTVFormats[index];
			}
			stream.DepthFormatType = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT;
			stream.DepthFormat = nativeDesc.DSVFormat;
			ComPtr<ID3D12Device2> device2;
			if (SUCCEEDED(state.Device.As(&device2)))
			{
				D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{ sizeof(stream), &stream };
				created = device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(pipeline->State.GetAddressOf()));
			}
		}
		else
		{
			created = state.Device->CreateGraphicsPipelineState(&nativeDesc, IID_PPV_ARGS(pipeline->State.GetAddressOf()));
		}

		if (FAILED(created))
		{
			DestroyObject(state.Resource, pipeline);
			char message[] = "Could not create the graphics pipeline state (HRESULT 0x00000000).";
			constexpr usize prefixBytes = sizeof("Could not create the graphics pipeline state (HRESULT 0x") - 1;
			constexpr char hexDigits[] = "0123456789abcdef";
			const uint32 nativeError = static_cast<uint32>(created);
			for (uint32 digit = 0; digit < 8; ++digit)
			{
				message[prefixBytes + digit] = hexDigits[(nativeError >> ((7 - digit) * 4)) & 15u];
			}

			return Result<void*>::Failure(Failure("Rhi.D3D12Graphics", message));
		}
		pipeline->PushConstantBytes = desc.PushConstantBytes;
		pipeline->bHeapDirectlyIndexed = desc.bHeapDirectlyIndexed;
		pipeline->bDeviceHeapIndexed = desc.bDeviceHeapIndexed;
		pipeline->bMeshShader = !desc.MeshShaderBytecode.IsEmpty();
		pipeline->bStencil = desc.Stencil.bEnable;
		pipeline->DepthBounds = desc.DepthBounds.bEnable ? desc.DepthBounds : DepthBoundsDesc{};
		pipeline->SampledTextureCount = sampledCount;
		pipeline->StorageTextureCount = storageCount;
		pipeline->GraphicsBufferCount = bufferCount;
		pipeline->Topology =
			desc.Topology == EPrimitiveTopology::PatchList
			? static_cast<D3D_PRIMITIVE_TOPOLOGY>(D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + desc.PatchControlPoints - 1)
			: D3D_TOPOLOGIES[static_cast<usize>(desc.Topology)];

		return Result<void*>::Success(pipeline);
	}

	void DestroyGraphicsPipelineNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<GraphicsPipeline*>(native));
	}

	[[nodiscard]] Result<void> DestroyGraphicsPipeline(State* opaque, void* native)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}

		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(native);

		if (pipeline.LastUse > CompletedSerial(state))
		{
			if (!state.RetiredGraphicsPipelines.Add({ &pipeline, pipeline.LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retire the graphics pipeline."));
			}
		}
		else
		{
			DestroyGraphicsPipelineNow(opaque, native);
		}

		return Result<void>::Success();
	}
}
#endif
