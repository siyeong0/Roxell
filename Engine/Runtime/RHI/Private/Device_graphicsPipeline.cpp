#include "DeviceState.h"
#include "Vulkan/SpirvDescriptorRequirements.h"

namespace rox::rhi
{
	using namespace core;

	void Device::validateGraphicsState(const NativeGraphicsPipelineDesc& desc) noexcept
	{
		const usize colorCount = desc.ColorFormat == EFormat::UNKNOWN && desc.ColorFormats.IsEmpty() ? 0 : desc.ColorFormats.IsEmpty() ? 1 : desc.ColorFormats.Size();
		const bool bHull = !desc.HullShaderBytecode.IsEmpty();
		const bool bDomain = !desc.DomainShaderBytecode.IsEmpty();

		ASSERT(desc.Topology <= EPrimitiveTopology::PatchList &&
			bHull == bDomain &&
			(desc.Topology == EPrimitiveTopology::PatchList) == bHull &&
			!(bHull ? (desc.PatchControlPoints < 1 || desc.PatchControlPoints > 32) : desc.PatchControlPoints != 0) &&
			desc.Raster.CullMode <= ECullMode::Back &&
			desc.Raster.FrontFace <= EFrontFace::Clockwise &&
			desc.Raster.FillMode <= EFillMode::Wireframe &&
			IsFinite(desc.Raster.DepthBiasClamp) &&
			IsFinite(desc.Raster.SlopeScaledDepthBias) &&
			desc.DepthCompare <= ECompareOp::Always &&
			(!desc.DepthBounds.bEnable || (IsFinite(desc.DepthBounds.Minimum) && IsFinite(desc.DepthBounds.Maximum) && desc.DepthBounds.Minimum >= 0.0f && desc.DepthBounds.Maximum <= 1.0f && desc.DepthBounds.Minimum <= desc.DepthBounds.Maximum)) &&
			desc.Stencil.FrontFace.FailOp <= EStencilOp::DecrementWrap &&
			desc.Stencil.FrontFace.DepthFailOp <= EStencilOp::DecrementWrap &&
			desc.Stencil.FrontFace.PassOp <= EStencilOp::DecrementWrap &&
			desc.Stencil.FrontFace.Compare <= ECompareOp::Always &&
			desc.Stencil.BackFace.FailOp <= EStencilOp::DecrementWrap &&
			desc.Stencil.BackFace.DepthFailOp <= EStencilOp::DecrementWrap &&
			desc.Stencil.BackFace.PassOp <= EStencilOp::DecrementWrap &&
			desc.Stencil.BackFace.Compare <= ECompareOp::Always &&
			(desc.ColorBlends.IsEmpty() || desc.ColorBlends.Size() == colorCount),
			"The topology, raster, depth, or blend state is invalid.");

		for (const ColorBlendDesc& blend : desc.ColorBlends)
		{
			const auto alphaFactor = [](EBlendFactor value) noexcept
				{
					return value == EBlendFactor::Zero ||
						value == EBlendFactor::One ||
						(value >= EBlendFactor::SourceAlpha && value <= EBlendFactor::OneMinusDestinationAlpha) ||
						value == EBlendFactor::ConstantColor ||
						value == EBlendFactor::OneMinusConstantColor ||
						value == EBlendFactor::Source1Alpha ||
						value == EBlendFactor::OneMinusSource1Alpha;
				};
			ASSERT(blend.SourceColor <= EBlendFactor::OneMinusSource1Alpha &&
				blend.DestinationColor <= EBlendFactor::OneMinusSource1Alpha &&
				blend.DestinationColor != EBlendFactor::SourceAlphaSaturate &&
				alphaFactor(blend.SourceAlpha) &&
				alphaFactor(blend.DestinationAlpha) &&
				blend.ColorOperation <= EBlendOp::Max &&
				blend.AlphaOperation <= EBlendOp::Max &&
				(blend.WriteMask & ~0xFu) == 0,
				"A color blend attachment is invalid.");
		}
	}

	[[nodiscard]] bool Device::isValidBlendConstant(const BlendColor& color) noexcept
	{
		return IsFinite(color.R) && IsFinite(color.G) && IsFinite(color.B) && IsFinite(color.A);
	}

	[[nodiscard]] Result<InputLayoutHandle> Device::CreateInputLayout(const InputLayoutDesc& desc)
	{
		CallScope call(*this, false);

		ASSERT(!(desc.VertexStrideBytes == 0 || desc.VertexStrideBytes > 2048 || desc.Attributes.IsEmpty() || desc.Attributes.Size() > 16 || desc.ExtraVertexBufferSlots.Size() > 3),
			"The input layout count or primary stride is invalid.");

		for (const VertexBufferSlotDesc& extra : desc.ExtraVertexBufferSlots)
		{
			ASSERT(!(extra.StrideBytes == 0 || extra.StrideBytes > 2048), "An extra vertex buffer stride is invalid.");
		}

		for (usize index = 0; index < desc.Attributes.Size(); ++index)
		{
			const VertexAttributeDesc& attribute = desc.Attributes[index];

			ASSERT(!(attribute.Location >= 16 || attribute.Format == EFormat::UNKNOWN || attribute.Format >= EFormat::COUNT || attribute.BufferSlot > desc.ExtraVertexBufferSlots.Size()),
				"A vertex attribute is invalid.");

			const FormatInfo& format = GetFormatInfo(attribute.Format);
			const uint32 stride = attribute.BufferSlot == 0 ? desc.VertexStrideBytes : desc.ExtraVertexBufferSlots[attribute.BufferSlot - 1].StrideBytes;

			ASSERT(!(format.BlockSize != 1 || format.BytesPerBlock == 0 || attribute.OffsetBytes > stride || format.BytesPerBlock > stride - attribute.OffsetBytes),
				"A vertex format or offset exceeds its buffer stride.");

			for (usize previous = 0; previous < index; ++previous)
			{
				ASSERT(!(desc.Attributes[previous].Location == attribute.Location), "Vertex locations must be unique.");
			}
		}

		const bool bNewSlot = m_State->m_FreeInputLayout == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_InputLayouts.Size()) : m_State->m_FreeInputLayout;

		if (bNewSlot && (m_State->m_InputLayouts.Size() >= NumericLimits<uint32>::max() || !m_State->m_InputLayouts.Add(InputLayoutSlot{})))
		{
			return Result<InputLayoutHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate an input layout slot.");
		}
		InputLayoutSlot& slot = m_State->m_InputLayouts[index];

		if (!bNewSlot)
		{
			m_State->m_FreeInputLayout = slot.NextFree;
		}

		slot.NextFree = NumericLimits<uint32>::max();
		slot.VertexStrideBytes = desc.VertexStrideBytes;
		slot.AttributeCount = static_cast<uint32>(desc.Attributes.Size());
		slot.ExtraVertexBufferCount = static_cast<uint32>(desc.ExtraVertexBufferSlots.Size());

		for (uint32 attribute = 0; attribute < slot.AttributeCount; ++attribute)
		{
			slot.Attributes[attribute] = desc.Attributes[attribute];
		}

		for (uint32 extra = 0; extra < slot.ExtraVertexBufferCount; ++extra)
		{
			slot.ExtraVertexBufferSlots[extra] = desc.ExtraVertexBufferSlots[extra];
		}
		slot.References = 0;
		slot.bLive = true;

		return Result<InputLayoutHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyInputLayout(InputLayoutHandle handle)
	{
		CallScope call(*this, false);
		InputLayoutSlot* const slot = findInputLayout(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->References != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.InputLayout", "A graphics pipeline still references the input layout.");
		}
		slot->VertexStrideBytes = 0;
		slot->AttributeCount = 0;
		slot->ExtraVertexBufferCount = 0;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeInputLayout;
			m_State->m_FreeInputLayout = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<GraphicsPipelineHandle> Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& input)
	{
		CallScope call(*this, false);
		NativeGraphicsPipelineDesc resolved(input);
		InputLayoutSlot* inputLayout = nullptr;

		if (input.InputLayout.Owner != 0 || input.InputLayout.Slot != 0 || input.InputLayout.Generation != 0)
		{
			inputLayout = findInputLayout(input.InputLayout);

			ASSERT(inputLayout != nullptr, "RHI operations require valid handles for this device.");

			ASSERT(input.VertexStrideBytes == 0 && input.VertexAttributes.IsEmpty() && input.ExtraVertexBufferSlots.IsEmpty(), "An input layout cannot be combined with inline vertex inputs.");
			if (
				inputLayout->References == NumericLimits<uint32>::max())
			{
				return Result<GraphicsPipelineHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.InputLayout", "The input layout has too many references.");
			}
			resolved.VertexStrideBytes = inputLayout->VertexStrideBytes;
			resolved.VertexAttributes = { inputLayout->Attributes, inputLayout->AttributeCount };
			resolved.ExtraVertexBufferSlots = { inputLayout->ExtraVertexBufferSlots, inputLayout->ExtraVertexBufferCount };
		}

		Result<void> shaders = resolveGraphicsShaders(resolved);

		if (!shaders)
		{
			return Result<GraphicsPipelineHandle>::Failure(shaders.GetError());
		}

		Result<void> directTables = deriveDirectTableCapacities(resolved);

		if (!directTables)
		{
			return Result<GraphicsPipelineHandle>::Failure(directTables.GetError());
		}
		NativeGraphicsBindingLayoutDesc nativeLayouts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		BindingLayoutScratch layoutScratch[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		const bool bMultiple = input.Layouts.Size() > 1;
		const BindingLayoutHandle singleLayout = input.Layouts.Size() == 1 ? input.Layouts[0] : BindingLayoutHandle{};
		const Span<const BindingLayoutHandle> layoutHandles = input.Layouts;
		Result<void> rangeValidation = validateRangePipelineLayouts( layoutHandles, EBindingLayoutKind::Graphics, false);
		if (!rangeValidation)
		{
			return Result<GraphicsPipelineHandle>::Failure(rangeValidation.GetError());
		}

		if (bMultiple)
		{
			ASSERT(input.Layouts.Size() <= MAXIMUM_FIXED_BINDING_LAYOUTS && resolved.SampledTextures.IsEmpty() && resolved.StorageTextures.IsEmpty(), "Multiple graphics layouts cannot mix with a single layout or inline textures.");
			if (input.Layouts.Size() > m_State->m_Backend.Capabilities.Limits.MaximumBindingLayouts)
			{
				return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsPipeline", "The device supports fewer binding layouts.");
			}

			for (uint32 layoutIndex = 0; layoutIndex < input.Layouts.Size(); ++layoutIndex)
			{
				BindingLayoutSlot* const candidate = findBindingLayout(input.Layouts[layoutIndex]);

				ASSERT(candidate != nullptr, "RHI operations require valid handles for this device.");

				uint32 references = 1;

				for (uint32 previous = 0; previous < layoutIndex; ++previous)
				{
					if (input.Layouts[previous] == input.Layouts[layoutIndex])
					{
						++references;
					}
				}

				ASSERT(candidate->Kind == EBindingLayoutKind::Graphics, "A graphics layout must match the pipeline.");
				if (
					candidate->References > NumericLimits<uint32>::max() - references)
				{
					return Result<GraphicsPipelineHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.GraphicsPipeline", "A graphics layout has too many references.");
				}

				if (layoutIndex == 0 && resolved.AccelStructSlot != 0xFFFFFFFFu)
				{
					for (uint32 binding = 0; binding < candidate->GraphicsCount; ++binding)
					{
						ASSERT(!(candidate->GraphicsTexture(binding).Slot == resolved.AccelStructSlot), "The acceleration structure shares a sampled texture register.");
					}

					for (uint32 binding = 0; binding < candidate->GraphicsBufferCount; ++binding)
					{
						ASSERT(!(candidate->GraphicsBuffer(binding).Type == EGraphicsBufferBindingType::ReadOnlyBuffer &&
								(candidate->GraphicsBuffer(binding).RegisterSlot == 0xffffffffu ? candidate->GraphicsBuffer(binding).Slot : candidate->GraphicsBuffer(binding).RegisterSlot) == resolved.AccelStructSlot),
							"The acceleration structure shares a read-only buffer register.");
					}
				}
				if (candidate->RangeCount != 0)
				{
					layoutScratch[layoutIndex].Capture(*candidate);
				}
				nativeLayouts[layoutIndex] = { {layoutScratch[layoutIndex].GraphicsTextures, candidate->GraphicsCount},
											  {layoutScratch[layoutIndex].GraphicsStorageTextures, candidate->GraphicsStorageCount},
											  {layoutScratch[layoutIndex].GraphicsBuffers, candidate->GraphicsBufferCount},
											  {candidate->Entries, candidate->RangeCount},
											  candidate->Space };
				nativeLayouts[layoutIndex].bRangeLayout = true;
				if (candidate->RangeCount == 0)
				{
					nativeLayouts[layoutIndex].ScalarEntries = {
						candidate->Entries, candidate->GraphicsCount + candidate->GraphicsStorageCount + candidate->GraphicsBufferCount };
					nativeLayouts[layoutIndex].ScalarSampledCount = candidate->GraphicsCount;
					nativeLayouts[layoutIndex].ScalarStorageCount = candidate->GraphicsStorageCount;
				}
			}
		}
		BindingLayoutSlot* layout = nullptr;

		if (singleLayout.Owner != 0 || singleLayout.Slot != 0 || singleLayout.Generation != 0)
		{
			layout = findBindingLayout(singleLayout);

			ASSERT(layout != nullptr, "RHI operations require valid handles for this device.");

			ASSERT(layout->Kind == EBindingLayoutKind::Graphics && resolved.SampledTextures.IsEmpty() && resolved.StorageTextures.IsEmpty(), "The graphics pipeline layout is incompatible.");
			if (
				layout->References == NumericLimits<uint32>::max())
			{
				return Result<GraphicsPipelineHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingLayout", "The graphics pipeline layout has too many references.");
			}
			layoutScratch[0].Capture(*layout);
			resolved.SampledTextures = { layoutScratch[0].GraphicsTextures, layout->GraphicsCount };
			resolved.StorageTextures = { layoutScratch[0].GraphicsStorageTextures, layout->GraphicsStorageCount };
		}

		const NativeGraphicsPipelineDesc& desc = resolved;
		const bool bRanges = layout != nullptr && layout->RangeCount != 0;
		const bool bMesh = !desc.MeshShaderBytecode.IsEmpty();
		for (const rhi::BindingLayoutHandle handle : layoutHandles)
		{
			const BindingLayoutSlot* selected = findBindingLayout(handle);
			if (selected == nullptr || selected->RangeCount == 0)
			{
				continue;
			}
			const uint32 allowed =
				bMesh ? 8u | (!desc.FragmentShaderBytecode.IsEmpty() ? 4u : 0u) | (!desc.TaskShaderBytecode.IsEmpty() ? 16u : 0u) : 7u | (!desc.GeometryShaderBytecode.IsEmpty() ? 2048u : 0u) |
				(!desc.HullShaderBytecode.IsEmpty() ? 4096u : 0u) |
				(!desc.DomainShaderBytecode.IsEmpty() ? 8192u : 0u);
			for (uint32 index = 0; index < selected->RangeCount; ++index)
			{
				ASSERT(!((static_cast<uint32>(selected->Range(index).Visibility) & ~allowed) != 0), "Descriptor visibility names a stage absent from this pipeline.");
			}
		}

		const bool bDepthOnly = desc.ColorFormat == EFormat::UNKNOWN && desc.ColorFormats.IsEmpty() && IsDepthFormat(desc.DepthFormat);
		ASSERT(bMesh != !desc.VertexShaderBytecode.IsEmpty(), "A graphics pipeline requires exactly one vertex or mesh shader.");
		ASSERT(!bMesh || (desc.GeometryShaderBytecode.IsEmpty() && desc.HullShaderBytecode.IsEmpty() && desc.DomainShaderBytecode.IsEmpty()),
			"Mesh and traditional graphics stages are mutually exclusive.");
		ASSERT(bMesh || desc.TaskShaderBytecode.IsEmpty(), "A task shader requires a mesh shader.");
		ASSERT(bDepthOnly || !desc.FragmentShaderBytecode.IsEmpty(), "A color graphics pipeline requires a pixel shader.");
		ASSERT(desc.HullShaderBytecode.IsEmpty() == desc.DomainShaderBytecode.IsEmpty(), "Tessellation requires both hull and domain shaders.");
		Result<void> typedTables = validateTypedBufferTables(desc.BindlessStorageBufferCapacity, desc.BindlessStorageBufferFormat,
			desc.BindlessReadOnlyBufferCapacity, desc.BindlessReadOnlyBufferFormat);

		if (!typedTables)
		{
			return Result<GraphicsPipelineHandle>::Failure(typedTables.GetError());
		}

		if (!m_State->m_Backend.Capabilities.Features.bGraphics)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.Graphics", "Graphics execution is unavailable.");
		}

		if (desc.bHeapDirectlyIndexed && !m_State->m_Backend.bHeapDirectlyIndexed)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsPipeline", "Direct descriptor heap indexing is unavailable.");
		}

		ASSERT(!(desc.bDeviceHeapIndexed && !desc.bHeapDirectlyIndexed), "Device heap indexing requires direct descriptor heap indexing.");

		ASSERT(!(desc.bHeapDirectlyIndexed &&
				!desc.bDeviceHeapIndexed &&
				desc.BindlessSampledTextureCapacity == 0 &&
				desc.BindlessSamplerCapacity == 0 &&
				desc.BindlessStorageBufferCapacity == 0 &&
				desc.BindlessReadOnlyBufferCapacity == 0 &&
				desc.BindlessStorageTextureCapacity == 0 &&
				desc.BindlessUniformBufferCapacity == 0),
			"Direct indexing requires a descriptor table.");

		if (bMesh && !m_State->m_Backend.Capabilities.Features.bMeshShader)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.MeshShader", "Mesh shaders are unavailable on this device.");
		}

		if (desc.AccelStructSlot != 0xFFFFFFFFu && !m_State->m_Backend.bRayQuery)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.RayQuery", "Ray query is unavailable on this device.");
		}

		ASSERT(!(desc.AccelStructSlot != 0xFFFFFFFFu && desc.AccelStructSlot > 31), "The acceleration structure slot is invalid.");

		if (!desc.TaskShaderBytecode.IsEmpty() && !m_State->m_Backend.Capabilities.Features.bTaskShader)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.TaskShader", "Task shaders are unavailable on this device.");
		}

		if (desc.bSupportsShadingRateImage && !m_State->m_Backend.Capabilities.Features.bShadingRateImage)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.ShadingRate", "Shading rate image pipelines are unavailable.");
		}

		if (desc.BindlessSampledTextureCapacity != 0 && desc.BindlessSampledTextureCapacity > m_State->m_Backend.MaximumBindlessSampledTextures)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorTable", "The graphics bindless capacity is unavailable.");
		}

		if (desc.BindlessSamplerCapacity != 0 && desc.BindlessSamplerCapacity > m_State->m_Backend.MaximumBindlessSamplers)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorTable", "The graphics sampler table capacity is unavailable.");
		}

		if (desc.BindlessUniformBufferCapacity != 0 && desc.BindlessUniformBufferCapacity > m_State->m_Backend.MaximumBindlessUniformBuffers)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorTable", "The graphics uniform buffer table is unavailable.");
		}

		if (desc.BindlessStorageBufferCapacity != 0 && desc.BindlessStorageBufferCapacity > m_State->m_Backend.MaximumGraphicsBindlessStorageBuffers)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorTable", "The graphics storage buffer table capacity is unavailable.");
		}

		if (desc.BindlessStorageTextureCapacity != 0 && desc.BindlessStorageTextureCapacity > m_State->m_Backend.MaximumGraphicsBindlessStorageTextures)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorTable", "The graphics storage texture table capacity is unavailable.");
		}

		const Span<const ShaderSpecialization> specializations[] = {
			desc.VertexSpecializations, desc.FragmentSpecializations, desc.GeometrySpecializations, desc.HullSpecializations,
			desc.DomainSpecializations, desc.MeshSpecializations,	  desc.TaskSpecializations };
		for (const Span<const ShaderSpecialization> values : specializations)
		{
			ASSERT(!(!IsValidSpecializations(values)), "A specialization ID is duplicated or the limit is exceeded.");

			if (!values.IsEmpty() && m_State->m_Kind != EBackend::Vulkan)
			{
				return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.ShaderSpecialization", "Shader specializations are unavailable on this backend.");
			}
		}

		ASSERT(!((bMesh && !desc.VertexSpecializations.IsEmpty()) ||
				(!desc.FragmentSpecializations.IsEmpty() && desc.FragmentShaderBytecode.IsEmpty()) ||
				(!desc.GeometrySpecializations.IsEmpty() && desc.GeometryShaderBytecode.IsEmpty()) ||
				(!desc.HullSpecializations.IsEmpty() && desc.HullShaderBytecode.IsEmpty()) ||
				(!desc.DomainSpecializations.IsEmpty() && desc.DomainShaderBytecode.IsEmpty()) ||
				(!desc.MeshSpecializations.IsEmpty() && desc.MeshShaderBytecode.IsEmpty()) ||
				(!desc.TaskSpecializations.IsEmpty() && desc.TaskShaderBytecode.IsEmpty())),
			"A specialization requires the corresponding shader stage.");

		if (!desc.GeometryShaderBytecode.IsEmpty() && !m_State->m_Backend.Capabilities.Features.bGeometryShader)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.GeometryShader", "The device does not support geometry shaders.");
		}

		if ((!desc.HullShaderBytecode.IsEmpty() || !desc.DomainShaderBytecode.IsEmpty()) && !m_State->m_Backend.Capabilities.Features.bTessellationShader)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.TessellationShader", "The device does not support tessellation shaders.");
		}

		ASSERT(!((bMesh && (!desc.VertexAttributes.IsEmpty() || desc.VertexStrideBytes != 0 || !desc.ExtraVertexBufferSlots.IsEmpty() || desc.Topology == EPrimitiveTopology::PatchList)) ||
				(desc.SampleCount != 1 && desc.SampleCount != 2 && desc.SampleCount != 4 && desc.SampleCount != 8) ||
				desc.VertexShaderBytecode.Size() > 16 * 1024 * 1024 ||
				desc.FragmentShaderBytecode.Size() > 16 * 1024 * 1024 ||
				desc.GeometryShaderBytecode.Size() > 16 * 1024 * 1024 ||
				desc.HullShaderBytecode.Size() > 16 * 1024 * 1024 ||
				desc.DomainShaderBytecode.Size() > 16 * 1024 * 1024 ||
				desc.MeshShaderBytecode.Size() > 16 * 1024 * 1024 ||
				desc.TaskShaderBytecode.Size() > 16 * 1024 * 1024 ||
				desc.VertexEntryPoint == nullptr ||
				(!desc.FragmentShaderBytecode.IsEmpty() && desc.FragmentEntryPoint == nullptr) ||
				(!desc.GeometryShaderBytecode.IsEmpty() && desc.GeometryEntryPoint == nullptr) ||
				(!desc.HullShaderBytecode.IsEmpty() && desc.HullEntryPoint == nullptr) ||
				(!desc.DomainShaderBytecode.IsEmpty() && desc.DomainEntryPoint == nullptr) ||
				(bMesh && desc.MeshEntryPoint == nullptr) ||
				(!desc.TaskShaderBytecode.IsEmpty() && desc.TaskEntryPoint == nullptr) ||
				CStringLength(desc.VertexEntryPoint) == 0 ||
				CStringLength(desc.VertexEntryPoint) > 63 ||
				(!desc.FragmentShaderBytecode.IsEmpty() && (CStringLength(desc.FragmentEntryPoint) == 0 || CStringLength(desc.FragmentEntryPoint) > 63)) ||
				(!desc.GeometryShaderBytecode.IsEmpty() && (CStringLength(desc.GeometryEntryPoint) == 0 || CStringLength(desc.GeometryEntryPoint) > 63)) ||
				(!desc.HullShaderBytecode.IsEmpty() && (CStringLength(desc.HullEntryPoint) == 0 || CStringLength(desc.HullEntryPoint) > 63)) ||
				(!desc.DomainShaderBytecode.IsEmpty() && (CStringLength(desc.DomainEntryPoint) == 0 || CStringLength(desc.DomainEntryPoint) > 63)) ||
				(bMesh && (CStringLength(desc.MeshEntryPoint) == 0 || CStringLength(desc.MeshEntryPoint) > 63)) ||
				(!desc.TaskShaderBytecode.IsEmpty() && (CStringLength(desc.TaskEntryPoint) == 0 || CStringLength(desc.TaskEntryPoint) > 63)) ||
				(!bDepthOnly && (desc.ColorFormat == EFormat::UNKNOWN || desc.ColorFormat >= EFormat::COUNT))),
			"The shaders or color format are invalid.");

		if ((!bDepthOnly && !IsColorTargetFormat(desc.ColorFormat)) || (desc.DepthFormat != EFormat::UNKNOWN && !IsDepthFormat(desc.DepthFormat)))
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsFormat", "This color format is not implemented.");
		}

		ASSERT(!(desc.ColorFormats.Size() > 8), "Too many color attachments.");

		if (desc.ColorFormats.Size() > m_State->m_Backend.Capabilities.Limits.MaximumColorAttachments)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsFormat", "The device has fewer color attachments.");
		}

		validateGraphicsState(desc);

		if (!m_State->m_Backend.Capabilities.Features.bIndependentBlend && desc.ColorBlends.Size() > 1)
		{
			const rhi::ColorBlendDesc& first = desc.ColorBlends[0];
			for (usize index = 1; index < desc.ColorBlends.Size(); ++index)
			{
				const rhi::ColorBlendDesc& blend = desc.ColorBlends[index];
				if (first.bEnable != blend.bEnable ||
					first.SourceColor != blend.SourceColor ||
					first.DestinationColor != blend.DestinationColor ||
					first.ColorOperation != blend.ColorOperation ||
					first.SourceAlpha != blend.SourceAlpha ||
					first.DestinationAlpha != blend.DestinationAlpha ||
					first.AlphaOperation != blend.AlphaOperation ||
					first.WriteMask != blend.WriteMask)
				{
					return Result<GraphicsPipelineHandle>::Failure(
						EErrorCode::Unsupported,
						"Rhi.IndependentBlend",
						"Distinct color attachment blend states require enabled independent blending.");
				}
			}
		}

		for (const ColorBlendDesc& blend : desc.ColorBlends)
		{
			const auto dualSource = [](EBlendFactor value) noexcept
				{ return value >= EBlendFactor::Source1Color && value <= EBlendFactor::OneMinusSource1Alpha; };
			if ((dualSource(blend.SourceColor) || dualSource(blend.DestinationColor) || dualSource(blend.SourceAlpha) || dualSource(blend.DestinationAlpha)) &&
				(desc.ColorFormats.IsEmpty() ? 1u : desc.ColorFormats.Size()) > m_State->m_Backend.Capabilities.Limits.MaximumDualSourceColorAttachments)
			{
				return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.DualSourceBlend", "The device cannot blend this many color attachments with a second source.");
			}
		}

		if ((desc.Raster.FillMode == EFillMode::Wireframe && !m_State->m_Backend.Capabilities.Features.bWireframe) ||
			(!desc.Raster.bDepthClipEnabled && !m_State->m_Backend.Capabilities.Features.bDepthClamp) ||
			(desc.Raster.DepthBiasClamp != 0.0f && !m_State->m_Backend.Capabilities.Features.bDepthBiasClamp) ||
			(desc.Raster.bConservativeRasterEnabled && !m_State->m_Backend.Capabilities.Features.bConservativeRasterization))
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.RasterState", "The device does not support the requested raster state.");
		}

		for (const EFormat format : desc.ColorFormats)
		{
			if (!IsColorTargetFormat(format))
			{
				return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsFormat", "This color format is not implemented.");
			}
		}

		const EFormat defaultColor = desc.ColorFormat;
		const Span<const EFormat> colorFormats = bDepthOnly ? Span<const EFormat>{}
		: desc.ColorFormats.IsEmpty() ? Span<const EFormat>(&defaultColor, 1)
			: desc.ColorFormats;

		for (usize index = 0; index < colorFormats.Size(); ++index)
		{
			const EFormat format = colorFormats[index];
			const EFormatSupport required = EFormatSupport::RenderTarget | (!desc.ColorBlends.IsEmpty() && desc.ColorBlends[index].bEnable ? EFormatSupport::Blendable : EFormatSupport::None);

			if (!HasFormatSupport(queryNativeFormatSupport(format), required))
			{
				return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsFormat", "The color format is not a render target.");
			}
		}

		ASSERT(!((desc.bDepthTest || desc.bDepthWrite || desc.Stencil.bEnable || desc.DepthBounds.bEnable) && desc.DepthFormat == EFormat::UNKNOWN), "Depth operations require a depth format.");
		if (desc.DepthBounds.bEnable && !m_State->m_Backend.Capabilities.Features.bDepthBounds)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.DepthBounds", "Depth bounds testing is unavailable on this device.");
		}

		ASSERT(!(desc.Stencil.bEnable && !GetFormatInfo(desc.DepthFormat).bHasStencil), "Stencil operations require a stencil attachment format.");

		if (desc.DepthFormat != EFormat::UNKNOWN && !HasFormatSupport(queryNativeFormatSupport(desc.DepthFormat), EFormatSupport::DepthStencil))
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.GraphicsDepth", "The depth format is unavailable.");
		}

		ASSERT(!(desc.VertexAttributes.Size() > 16 ||
				(desc.VertexAttributes.IsEmpty() != (desc.VertexStrideBytes == 0)) ||
				desc.ViewportCount == 0 ||
				desc.ExtraVertexBufferSlots.Size() > 3 ||
				(!desc.ExtraVertexBufferSlots.IsEmpty() && desc.VertexStrideBytes == 0) ||
				desc.VertexStrideBytes > 2048 ||
				desc.PushConstantBytes > 128 ||
				desc.PushConstantBytes % 4 != 0 ||
				desc.SampledTextures.Size() + desc.StorageTextures.Size() > 8), "The vertex layout is invalid.");
		if (desc.ViewportCount > m_State->m_Backend.Capabilities.Limits.MaximumViewports)
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.Viewports", "The device supports fewer viewports.");
		}

		for (const VertexBufferSlotDesc& slot : desc.ExtraVertexBufferSlots)
		{
			ASSERT(!(slot.StrideBytes == 0 || slot.StrideBytes > 2048), "An extra vertex buffer slot has an invalid stride.");
		}

		for (usize index = 0; !bRanges && index < desc.SampledTextures.Size(); ++index)
		{
			const GraphicsTextureBindingDesc& binding = desc.SampledTextures[index];

			ASSERT(!(binding.Slot == desc.AccelStructSlot), "The acceleration structure shares a shader register.");

			if (layout != nullptr)
			{
				for (uint32 bufferIndex = 0; bufferIndex < layout->GraphicsBufferCount; ++bufferIndex)
				{
					const GraphicsBufferBindingDesc& buffer = layout->GraphicsBuffer(bufferIndex);

					ASSERT(!(buffer.Type == EGraphicsBufferBindingType::ReadOnlyBuffer && (buffer.RegisterSlot == 0xffffffffu ? buffer.Slot : buffer.RegisterSlot) == binding.Slot),
						"A read-only buffer and sampled texture share a shader register.");
				}
			}

			ASSERT(binding.Slot < 16 && (binding.SamplerSlot == 0xffffffffu || binding.SamplerSlot < 16) && IsValidSampler(binding.Sampler), "A sampled texture binding is invalid.");

			for (usize previous = 0; previous < index; ++previous)
			{
				ASSERT(!(desc.SampledTextures[previous].Slot == binding.Slot ||
						(desc.SampledTextures[previous].SamplerSlot == 0xffffffffu ? desc.SampledTextures[previous].Slot : desc.SampledTextures[previous].SamplerSlot) == (binding.SamplerSlot == 0xffffffffu ? binding.Slot : binding.SamplerSlot)),
					"Sampled texture slots must be unique.");
			}
		}

		for (usize index = 0; !bRanges && index < desc.StorageTextures.Size(); ++index)
		{
			const uint32 storageSlot = desc.StorageTextures[index].Slot;

			ASSERT(!(storageSlot >= 16), "A graphics storage texture slot is invalid.");

			for (usize previous = 0; previous < index; ++previous)
			{
				ASSERT(!(desc.StorageTextures[previous].Slot == storageSlot), "A graphics storage texture slot is repeated.");
			}

			if (layout != nullptr)
			{
				for (uint32 binding = 0; binding < layout->GraphicsBufferCount; ++binding)
				{
					const GraphicsBufferBindingDesc& buffer = layout->GraphicsBuffer(binding);

					ASSERT(!(buffer.Type == EGraphicsBufferBindingType::StorageBuffer && (buffer.RegisterSlot == 0xffffffffu ? buffer.Slot : buffer.RegisterSlot) == storageSlot),
						"A storage buffer and texture share a UAV register.");
				}
			}
		}

		if (layout != nullptr && !bRanges && desc.PushConstantBytes != 0)
		{
			for (uint32 binding = 0; binding < layout->GraphicsBufferCount; ++binding)
			{
				ASSERT(!((layout->GraphicsBuffer(binding).RegisterSlot == 0xffffffffu ? layout->GraphicsBuffer(binding).Slot : layout->GraphicsBuffer(binding).RegisterSlot) == 0 &&
						layout->GraphicsBuffer(binding).Type == EGraphicsBufferBindingType::UniformBuffer),
					"Uniform b0 conflicts with push constants.");
			}
		}

		for (usize index = 0; index < desc.VertexAttributes.Size(); ++index)
		{
			const VertexAttributeDesc& attribute = desc.VertexAttributes[index];

			ASSERT(!(attribute.Location >= 16 || attribute.Format == EFormat::UNKNOWN || attribute.Format >= EFormat::COUNT), "A vertex attribute is invalid.");

			const FormatInfo& format = GetFormatInfo(attribute.Format);
			const uint32 stride = attribute.BufferSlot == 0 ? desc.VertexStrideBytes
				: attribute.BufferSlot <= desc.ExtraVertexBufferSlots.Size()
				? desc.ExtraVertexBufferSlots[attribute.BufferSlot - 1].StrideBytes
				: 0;

			ASSERT(!(format.BlockSize != 1 || format.BytesPerBlock == 0 || stride == 0 || attribute.OffsetBytes > stride || format.BytesPerBlock > stride - attribute.OffsetBytes),
				"A vertex format or offset is unsupported.");
			if (
				!HasFormatSupport(queryNativeFormatSupport(attribute.Format), EFormatSupport::VertexBuffer))
			{
				return Result<GraphicsPipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.VertexLayout", "The vertex format is unsupported by the device.");
			}

			for (usize previous = 0; previous < index; ++previous)
			{
				ASSERT(!(desc.VertexAttributes[previous].Location == attribute.Location), "Vertex locations must be unique.");
			}
		}

		if (m_State->m_Kind == EBackend::Vulkan)
		{
			const Span<const Byte> modules[]{
				desc.VertexShaderBytecode, desc.FragmentShaderBytecode, desc.GeometryShaderBytecode, desc.HullShaderBytecode,
				desc.DomainShaderBytecode, desc.MeshShaderBytecode,		desc.TaskShaderBytecode };
			for (const Span<const Byte> module : modules)
			{
				if (module.IsEmpty())
				{
					continue;
				}

				Result<void> supported = ValidateSpirvDescriptorIndexing(module, m_State->m_Backend.Capabilities);
				if (!supported)
				{
					return Result<GraphicsPipelineHandle>::Failure(Move(supported.GetError()));
				}
			}
		}

		const bool bNewSlot = m_State->m_FreeGraphicsPipeline == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_GraphicsPipelines.Size()) : m_State->m_FreeGraphicsPipeline;

		if (bNewSlot && (m_State->m_GraphicsPipelines.Size() >= NumericLimits<uint32>::max() || !m_State->m_GraphicsPipelines.Add(GraphicsPipelineSlot{})))
		{
			return Result<GraphicsPipelineHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a graphics pipeline slot.");
		}
		GraphicsPipelineSlot& slot = m_State->m_GraphicsPipelines[index];
		const Span<const GraphicsBufferBindingDesc> graphicsBuffers =
			layout == nullptr ? Span<const GraphicsBufferBindingDesc>{}
		: Span<const GraphicsBufferBindingDesc>{ layoutScratch[0].GraphicsBuffers, layout->GraphicsBufferCount };
		for (const GraphicsBufferBindingDesc& binding : graphicsBuffers)
		{
			ASSERT(!(binding.Type == EGraphicsBufferBindingType::ReadOnlyBuffer && binding.Slot == desc.AccelStructSlot), "The acceleration structure shares a shader register.");
		}

		NativeGraphicsBindingLayoutDesc nativeLayout{ desc.SampledTextures, desc.StorageTextures, graphicsBuffers,
													 layout != nullptr
														 ? Span<const BindingLayoutEntry>{layout->Entries, layout->RangeCount}
														 : Span<const BindingLayoutEntry>{},
													 layout != nullptr ? layout->Space : 0 };

		nativeLayout.bRangeLayout = layout != nullptr;

		if (layout != nullptr && layout->RangeCount == 0)
		{
			nativeLayout.ScalarEntries = { layout->Entries,
										  layout->GraphicsCount + layout->GraphicsStorageCount + layout->GraphicsBufferCount };
			nativeLayout.ScalarSampledCount = layout->GraphicsCount;
			nativeLayout.ScalarStorageCount = layout->GraphicsStorageCount;
		}

		Result<void*> created = backend::CreateGraphicsPipeline(
			m_State->m_Backend.State, desc,
			bMultiple ? Span<const NativeGraphicsBindingLayoutDesc>{nativeLayouts, input.Layouts.Size()}
		: Span<const NativeGraphicsBindingLayoutDesc>{ &nativeLayout, 1 });
		if (!created)
		{
			if (bNewSlot)
			{
				slot.NextFree = m_State->m_FreeGraphicsPipeline;
				m_State->m_FreeGraphicsPipeline = index;
			}

			return Result<GraphicsPipelineHandle>::Failure(created.GetError());
		}

		if (!bNewSlot)
		{
			m_State->m_FreeGraphicsPipeline = slot.NextFree;
		}

		slot.NextFree = NumericLimits<uint32>::max();
		slot.Native = created.Value();
		slot.InputLayout = input.InputLayout;

		if (inputLayout != nullptr)
		{
			++inputLayout->References;
		}
		slot.Layout = singleLayout;
		slot.LayoutCount = bMultiple ? static_cast<uint32>(input.Layouts.Size()) : 0;

		if (layout != nullptr)
		{
			++layout->References;
		}

		for (uint32 layoutIndex = 0; layoutIndex < slot.LayoutCount; ++layoutIndex)
		{
			slot.Layouts[layoutIndex] = input.Layouts[layoutIndex];
			++m_State->m_BindingLayouts[input.Layouts[layoutIndex].Slot].References;
		}
		slot.ColorFormat = desc.ColorFormat;
		slot.ColorCount = bDepthOnly ? 0 : desc.ColorFormats.IsEmpty() ? 1 : static_cast<uint32>(desc.ColorFormats.Size());

		for (uint32 color = 0; color < slot.ColorCount; ++color)
		{
			slot.ColorFormats[color] = desc.ColorFormats.IsEmpty() ? desc.ColorFormat : desc.ColorFormats[color];
		}
		slot.DepthFormat = desc.DepthFormat;
		slot.bWritesDepth = desc.bDepthTest && desc.bDepthWrite;
		slot.bWritesStencil =
			(desc.Stencil.bEnable && desc.Stencil.WriteMask != 0 &&
				(desc.Stencil.FrontFace.FailOp != EStencilOp::Keep || desc.Stencil.FrontFace.DepthFailOp != EStencilOp::Keep ||
					desc.Stencil.FrontFace.PassOp != EStencilOp::Keep || desc.Stencil.BackFace.FailOp != EStencilOp::Keep ||
					desc.Stencil.BackFace.DepthFailOp != EStencilOp::Keep || desc.Stencil.BackFace.PassOp != EStencilOp::Keep));
		slot.bWritesDepthStencil = slot.bWritesDepth || slot.bWritesStencil;
		slot.VertexStrideBytes = desc.VertexStrideBytes;
		slot.VertexAttributeCount = static_cast<uint32>(desc.VertexAttributes.Size());
		for (uint32 attribute = 0; attribute < slot.VertexAttributeCount; ++attribute)
		{
			slot.VertexAttributes[attribute] = desc.VertexAttributes[attribute];
		}
		slot.ViewportCount = desc.ViewportCount;
		slot.ExtraVertexBufferCount = static_cast<uint32>(desc.ExtraVertexBufferSlots.Size());

		for (uint32 binding = 0; binding < slot.ExtraVertexBufferCount; ++binding)
		{
			slot.ExtraVertexBufferSlots[binding] = desc.ExtraVertexBufferSlots[binding];
		}
		slot.PushConstantBytes = desc.PushConstantBytes;
		slot.AccelStructSlot = desc.AccelStructSlot;
		slot.SampledTextureCount = static_cast<uint32>(desc.SampledTextures.Size());
		slot.StorageTextureCount = static_cast<uint32>(desc.StorageTextures.Size());
		slot.BindlessSampledTextureCapacity = desc.BindlessSampledTextureCapacity;
		slot.BindlessSamplerCapacity = desc.BindlessSamplerCapacity;
		slot.BindlessStorageBufferCapacity = desc.BindlessStorageBufferCapacity;
		slot.BindlessStorageBufferFormat = desc.BindlessStorageBufferFormat;
		slot.BindlessReadOnlyBufferCapacity = desc.BindlessReadOnlyBufferCapacity;
		slot.BindlessReadOnlyBufferFormat = desc.BindlessReadOnlyBufferFormat;
		slot.BindlessStorageTextureCapacity = desc.BindlessStorageTextureCapacity;
		slot.BindlessUniformBufferCapacity = desc.BindlessUniformBufferCapacity;
		slot.bHeapDirectlyIndexed = desc.bHeapDirectlyIndexed;
		slot.bDeviceHeapIndexed = desc.bDeviceHeapIndexed;
		slot.SampleCount = desc.SampleCount;
		slot.bMeshShader = bMesh;
		slot.bTaskShader = !desc.TaskShaderBytecode.IsEmpty();
		slot.bSupportsShadingRateImage = desc.bSupportsShadingRateImage;
		slot.BufferBindingCount = 0;

		if (bMultiple)
		{
			for (uint32 layoutIndex = 0; layoutIndex < slot.LayoutCount; ++layoutIndex)
			{
				for (const GraphicsBufferBindingDesc& binding : nativeLayouts[layoutIndex].Buffers)
				{
					slot.BufferBindings[slot.BufferBindingCount++] = binding;
				}
			}
		}
		else
		{
			slot.BufferBindingCount = static_cast<uint32>(graphicsBuffers.Size());

			for (uint32 binding = 0; binding < slot.BufferBindingCount; ++binding)
			{
				slot.BufferBindings[binding] = graphicsBuffers[binding];
			}
		}

		for (usize binding = 0; binding < desc.SampledTextures.Size(); ++binding)
		{
			slot.Samplers[binding] = desc.SampledTextures[binding].Sampler;
		}
		slot.RecordedUses = 0;
		slot.bLive = true;

		return Result<GraphicsPipelineHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<uint32> Device::GetGraphicsDescriptorHeapIndex(GraphicsPipelineHandle handle, EDescriptorType type, uint32 entry)
	{
		CallScope call(*this, true);
		GraphicsPipelineSlot* const slot = findGraphicsPipeline(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(type <= EDescriptorType::ReadOnlyBuffer, "A packed descriptor heap index requires a supported descriptor type.");

		if (!slot->bHeapDirectlyIndexed || slot->bDeviceHeapIndexed)
		{
			return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorHeap", "The pipeline does not use direct descriptor heap indexing.");
		}

		uint32 fixedTextureCount = 0;
		uint32 fixedSamplerCount = 0;
		const uint32 count = slot->LayoutCount != 0 ? slot->LayoutCount : (slot->Layout.IsValid() ? 1u : 0u);
		for (uint32 index = 0; index < count; ++index)
		{
			const BindingLayoutHandle layoutHandle = slot->LayoutCount != 0 ? slot->Layouts[index] : slot->Layout;
			const BindingLayoutSlot* layout = findBindingLayout(layoutHandle);
			ASSERT(layout != nullptr, "A pipeline retains its binding layouts.");
			fixedTextureCount += layout->GraphicsCount + layout->GraphicsStorageCount;
			fixedSamplerCount += layout->RangeSamplerCount;
		}
		uint32 offset = fixedTextureCount;
		uint32 capacity = 0;

		switch (type)
		{
		case EDescriptorType::SampledTexture:
			capacity = slot->BindlessSampledTextureCapacity;
			break;
		case EDescriptorType::StorageBuffer:
			offset += slot->BindlessSampledTextureCapacity;
			capacity = slot->BindlessStorageBufferCapacity;
			break;
		case EDescriptorType::StorageTexture:
			offset += slot->BindlessSampledTextureCapacity + slot->BindlessStorageBufferCapacity;
			capacity = slot->BindlessStorageTextureCapacity;
			break;
		case EDescriptorType::UniformBuffer:
			offset += slot->BindlessSampledTextureCapacity + slot->BindlessStorageBufferCapacity + slot->BindlessStorageTextureCapacity;
			capacity = slot->BindlessUniformBufferCapacity;
			break;
		case EDescriptorType::ReadOnlyBuffer:
			offset += slot->BindlessSampledTextureCapacity + slot->BindlessStorageBufferCapacity + slot->BindlessStorageTextureCapacity + slot->BindlessUniformBufferCapacity;
			capacity = slot->BindlessReadOnlyBufferCapacity;
			break;
		case EDescriptorType::Sampler:
			offset = fixedSamplerCount;
			capacity = slot->BindlessSamplerCapacity;
			break;
		default:
			ASSERT(false, "A packed descriptor heap index requires a supported descriptor type.");
			break;
		}

		ASSERT(entry < capacity, "The packed descriptor index must be within its table.");

		return Result<uint32>::Success(offset + entry);
	}

	[[nodiscard]] Result<void> Device::DestroyGraphicsPipeline(GraphicsPipelineHandle handle)
	{
		CallScope call(*this, false);
		GraphicsPipelineSlot* const slot = findGraphicsPipeline(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->RecordedUses != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "A recording command list still references the pipeline.");
		}

		Result<void> destroyed = backend::DestroyGraphicsPipeline(m_State->m_Backend.State, slot->Native);

		if (!destroyed)
		{
			return destroyed;
		}
		slot->Native = nullptr;

		if (slot->InputLayout.IsValid())
		{
			--m_State->m_InputLayouts[slot->InputLayout.Slot].References;
			slot->InputLayout = {};
		}

		if (slot->Layout.IsValid())
		{
			--m_State->m_BindingLayouts[slot->Layout.Slot].References;
			slot->Layout = {};
		}

		for (uint32 layoutIndex = 0; layoutIndex < slot->LayoutCount; ++layoutIndex)
		{
			--m_State->m_BindingLayouts[slot->Layouts[layoutIndex].Slot].References;
			slot->Layouts[layoutIndex] = {};
		}
		slot->LayoutCount = 0;
		slot->ColorFormat = EFormat::UNKNOWN;
		slot->ColorCount = 0;
		slot->DepthFormat = EFormat::UNKNOWN;
		slot->VertexStrideBytes = 0;
		slot->ViewportCount = 1;
		slot->ExtraVertexBufferCount = 0;
		slot->PushConstantBytes = 0;
		slot->SampledTextureCount = 0;
		slot->StorageTextureCount = 0;
		slot->BindlessSampledTextureCapacity = 0;
		slot->BindlessSamplerCapacity = 0;
		slot->BindlessStorageBufferCapacity = 0;
		slot->BindlessReadOnlyBufferCapacity = 0;
		slot->BindlessStorageTextureCapacity = 0;
		slot->BindlessUniformBufferCapacity = 0;
		slot->bMeshShader = false;
		slot->bTaskShader = false;
		slot->bHeapDirectlyIndexed = false;
		slot->bDeviceHeapIndexed = false;
		slot->BufferBindingCount = 0;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeGraphicsPipeline;
			m_State->m_FreeGraphicsPipeline = handle.Slot;
		}

		return Result<void>::Success();
	}
}
