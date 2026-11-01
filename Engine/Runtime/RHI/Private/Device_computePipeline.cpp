#include "DeviceState.h"
#include "Vulkan/SpirvDescriptorRequirements.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<ComputePipelineHandle> Device::createComputePipelineMultiple(const ComputePipelineDesc& input, const NativeComputePipelineDesc& desc)
	{
		Result<void> typedTables = validateTypedBufferTables(desc.BindlessStorageBufferCapacity, desc.BindlessStorageBufferFormat,
			desc.BindlessReadOnlyBufferCapacity, desc.BindlessReadOnlyBufferFormat);

		if (!typedTables)
		{
			return Result<ComputePipelineHandle>::Failure(typedTables.GetError());
		}

		if (!m_State->m_Backend.Capabilities.Features.bCompute)
		{
			return Result<ComputePipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.Compute", "Compute execution is unavailable.");
		}

		if (desc.bHeapDirectlyIndexed && !m_State->m_Backend.bHeapDirectlyIndexed)
		{
			return Result<ComputePipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.ComputePipeline", "Direct descriptor heap indexing is unavailable.");
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

		ASSERT(!
			(input.Layouts.Size() > MAXIMUM_FIXED_BINDING_LAYOUTS ||
				!desc.Bindings.IsEmpty() ||
				desc.ShaderBytecode.IsEmpty() ||
				desc.ShaderBytecode.Size() > 16 * 1024 * 1024 ||
				desc.EntryPoint == nullptr ||
				CStringLength(desc.EntryPoint) == 0 ||
				CStringLength(desc.EntryPoint) > 63 ||
				desc.PushConstantBytes > 128 ||
				desc.PushConstantBytes % 4 != 0 ||
				!IsValidSpecializations(desc.Specializations)), "The multi-layout compute pipeline is invalid.");
		if (input.Layouts.Size() > m_State->m_Backend.Capabilities.Limits.MaximumBindingLayouts)
		{
			return Result<ComputePipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.ComputePipeline", "The device supports fewer binding layouts.");
		}

		if (!desc.Specializations.IsEmpty() && m_State->m_Kind != EBackend::Vulkan)
		{
			return Result<ComputePipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.ShaderSpecialization", "Shader specializations are unavailable on this backend.");
		}

		if (desc.BindlessSampledTextureCapacity > m_State->m_Backend.MaximumBindlessSampledTextures ||
			desc.BindlessSamplerCapacity > m_State->m_Backend.MaximumBindlessSamplers ||
			desc.BindlessStorageBufferCapacity > m_State->m_Backend.MaximumBindlessStorageBuffers ||
			desc.BindlessStorageTextureCapacity > m_State->m_Backend.MaximumBindlessStorageTextures ||
			desc.BindlessUniformBufferCapacity > m_State->m_Backend.MaximumBindlessUniformBuffers ||
			(desc.AccelStructSlot != 0xFFFFFFFFu && !m_State->m_Backend.bRayQuery))
		{
			return Result<ComputePipelineHandle>::Failure(
				EErrorCode::Unsupported,
				"Rhi.ComputePipeline",
				"A multi-layout descriptor table exceeds the device limit or ray query is unavailable.");
		}

		ASSERT(!(desc.AccelStructSlot != 0xFFFFFFFFu && desc.AccelStructSlot > 31), "The acceleration structure slot is invalid.");
		Result<void> rangeValidation = validateRangePipelineLayouts( input.Layouts, EBindingLayoutKind::Compute, false);
		if (!rangeValidation)
		{
			return Result<ComputePipelineHandle>::Failure(rangeValidation.GetError());
		}
		NativeBindingLayoutDesc nativeLayouts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		BindingLayoutScratch layoutScratch[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		for (uint32 index = 0; index < input.Layouts.Size(); ++index)
		{
			BindingLayoutSlot* const layout = findBindingLayout(input.Layouts[index]);

			ASSERT(layout != nullptr, "RHI operations require valid handles for this device.");

			uint32 repeated = 1;

			for (uint32 previous = 0; previous < index; ++previous)
			{
				if (input.Layouts[previous] == input.Layouts[index])
				{
					++repeated;
				}
			}

			if (layout->Kind != EBindingLayoutKind::Compute ||
				layout->GraphicsCount != 0 ||
				layout->GraphicsStorageCount != 0 ||
				layout->GraphicsBufferCount != 0 ||
				layout->References > NumericLimits<uint32>::max() - repeated)
			{
				return Result<ComputePipelineHandle>::Failure(EErrorCode::Unsupported, "Rhi.ComputePipeline", "A multi-layout compute set requires compute bindings only.");
			}

			if (index == 0 && desc.AccelStructSlot != 0xFFFFFFFFu)
			{
				for (uint32 binding = 0; binding < layout->ComputeCount; ++binding)
				{
					ASSERT(!(layout->ComputeBuffer(binding).Type == EComputeBindingType::ReadOnlyBuffer && layout->ComputeBuffer(binding).Slot == desc.AccelStructSlot),
						"The acceleration structure shares a shader register.");
				}

				for (uint32 binding = 0; binding < layout->ComputeTextureCount; ++binding)
				{
					ASSERT(!(layout->ComputeTexture(binding).Type == EComputeTextureBindingType::SampledTexture && layout->ComputeTexture(binding).Slot == desc.AccelStructSlot),
						"The acceleration structure shares a shader register.");
				}
			}
			if (layout->RangeCount != 0)
			{
				layoutScratch[index].Capture(*layout);
			}
			nativeLayouts[index].Buffers = { layoutScratch[index].ComputeBuffers, layout->ComputeCount };
			nativeLayouts[index].Textures = { layoutScratch[index].ComputeTextures, layout->ComputeTextureCount };
			nativeLayouts[index].Ranges = { layout->Entries, layout->RangeCount };
			nativeLayouts[index].Space = layout->Space;
			nativeLayouts[index].bRangeLayout = true;
			if (layout->RangeCount == 0)
			{
				nativeLayouts[index].ScalarEntries = { layout->Entries, layout->ComputeCount + layout->ComputeTextureCount };
				nativeLayouts[index].ScalarBufferCount = layout->ComputeCount;
			}
		}

		const

			usize nativeLayoutCount = input.Layouts.IsEmpty() ? 1 : input.Layouts.Size();
		Result<void*> created = backend::CreateComputePipeline(m_State->m_Backend.State, desc, { nativeLayouts, nativeLayoutCount });
		if (!created)
		{
			return Result<ComputePipelineHandle>::Failure(created.GetError());
		}

		const bool bNewSlot = m_State->m_FreePipeline == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Pipelines.Size()) : m_State->m_FreePipeline;

		if (bNewSlot && (m_State->m_Pipelines.Size() >= NumericLimits<uint32>::max() || !m_State->m_Pipelines.Add(PipelineSlot(m_State->m_Resource))))
		{
			backend::DestroyComputePipelineNow(m_State->m_Backend.State, created.Value());

			return Result<ComputePipelineHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a pipeline slot.");
		}
		PipelineSlot& slot = m_State->m_Pipelines[index];

		if (!bNewSlot)
		{
			m_State->m_FreePipeline = slot.NextFree;
		}

		slot.NextFree = NumericLimits<uint32>::max();
		slot.Bindings.Clear();
		slot.Layout = {};
		slot.LayoutCount = static_cast<uint32>(input.Layouts.Size());

		for (uint32 layoutIndex = 0; layoutIndex < slot.LayoutCount; ++layoutIndex)
		{
			BindingLayoutSlot& layout = m_State->m_BindingLayouts[input.Layouts[layoutIndex].Slot];
			PipelineSlot::FixedLayout& target = slot.Layouts[layoutIndex];
			target.Handle = input.Layouts[layoutIndex];
			target.BufferCount = layout.ComputeCount;
			target.TextureCount = layout.ComputeTextureCount;
			target.SampledTextureCount = layout.ComputeSampledCount;
			target.StorageTextureCount = layout.ComputeStorageCount;

			for (uint32 binding = 0; binding < target.BufferCount; ++binding)
			{
				target.Buffers[binding] = layout.ComputeBuffer(binding);
			}

			for (uint32 binding = 0; binding < target.TextureCount; ++binding)
			{
				target.Textures[binding] = layout.ComputeTexture(binding);
			}
			++layout.References;
		}
		slot.Native = created.Value();
		slot.PushConstantBytes = desc.PushConstantBytes;
		slot.TextureCount = 0;
		slot.SampledTextureCount = 0;
		slot.StorageTextureCount = 0;
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
		slot.AccelStructSlot = desc.AccelStructSlot;
		slot.RecordedUses = 0;
		slot.bLive = true;

		return Result<ComputePipelineHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<ComputePipelineHandle> Device::CreateComputePipeline(const ComputePipelineDesc& input)
	{
		CallScope call(*this, false);
		NativeComputePipelineDesc resolved(input);
		ASSERT(input.PushConstantBytes <= 128 && input.PushConstantBytes % 4 == 0 && IsValidSpecializations(input.Compute.Specializations), "The multi-layout compute pipeline is invalid.");
		ASSERT(input.Compute.Source.GetKind() != ShaderInput::EKind::None, "A compute pipeline requires a compute shader source.");
		if (input.Compute.Source.GetKind() == ShaderInput::EKind::Code)
		{
			ASSERT(!input.Compute.Source.GetCode().IsEmpty() && input.Compute.Source.GetEntryPoint() != nullptr, "A compute shader source requires bytecode and an entry point."); resolved.ShaderBytecode = input.Compute.Source.GetCode();
			resolved.EntryPoint = input.Compute.Source.GetEntryPoint();
			resolved.Shader = {};
			resolved.Specializations = input.Compute.Specializations;
		}
		else if (input.Compute.Source.GetKind() == ShaderInput::EKind::Shader)
		{
			ASSERT(input.Compute.Source.GetShader().IsValid(), "A compute shader handle must be valid.");
			resolved.ShaderBytecode = {};
			resolved.EntryPoint = "main";
			resolved.Shader = input.Compute.Source.GetShader();
			resolved.Specializations = input.Compute.Specializations;
		}

		Result<void> shader = resolveShader(resolved.Shader, EShaderStage::Compute, resolved.ShaderBytecode, resolved.EntryPoint);

		if (!shader)
		{
			return Result<ComputePipelineHandle>::Failure(shader.GetError());
		}

		if (m_State->m_Kind == EBackend::Vulkan)
		{
			Result<void> supported = ValidateSpirvDescriptorIndexing(resolved.ShaderBytecode, m_State->m_Backend.Capabilities);
			if (!supported)
			{
				return Result<ComputePipelineHandle>::Failure(Move(supported.GetError()));
			}
		}

		Result<void> directTables = deriveDirectTableCapacities(resolved);

		if (!directTables)
		{
			return Result<ComputePipelineHandle>::Failure(directTables.GetError());
		}

		return createComputePipelineMultiple(input, resolved);
	}

	[[nodiscard]] Result<uint32> Device::GetComputeDescriptorHeapIndex(ComputePipelineHandle handle, EDescriptorType type, uint32 entry)
	{
		CallScope call(*this, true);
		PipelineSlot* const slot = findPipeline(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(type <= EDescriptorType::ReadOnlyBuffer, "A packed descriptor heap index requires a supported descriptor type.");

		if (!slot->bHeapDirectlyIndexed)
		{
			return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorHeap", "The pipeline does not use direct descriptor heap indexing.");
		}

		if (slot->bDeviceHeapIndexed)
		{
			return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorHeap", "Use the descriptor table handle to query a device heap index.");
		}

		uint32 fixedTextureCount = 0;
		uint32 fixedSamplerCount = 0;

		for (uint32 index = 0; index < slot->LayoutCount; ++index)
		{
			const BindingLayoutSlot* layout = findBindingLayout(slot->Layouts[index].Handle);
			ASSERT(layout != nullptr, "A pipeline retains its binding layouts.");
			fixedTextureCount += layout->ComputeTextureCount;
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

	[[nodiscard]] Result<void> Device::DestroyComputePipeline(ComputePipelineHandle handle)
	{
		CallScope call(*this, false);
		PipelineSlot* const slot = findPipeline(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->RecordedUses != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "A recording command list still references the pipeline.");
		}

		Result<void> destroyed = backend::DestroyComputePipeline(m_State->m_Backend.State, slot->Native);

		if (!destroyed)
		{
			return destroyed;
		}
		slot->Native = nullptr;

		if (slot->Layout.IsValid())
		{
			--m_State->m_BindingLayouts[slot->Layout.Slot].References;
			slot->Layout = {};
		}

		for (uint32 index = 0; index < slot->LayoutCount; ++index)
		{
			--m_State->m_BindingLayouts[slot->Layouts[index].Handle.Slot].References;
			slot->Layouts[index] = {};
		}
		slot->LayoutCount = 0;
		slot->Bindings.Clear();
		slot->TextureCount = 0;
		slot->SampledTextureCount = 0;
		slot->StorageTextureCount = 0;
		slot->PushConstantBytes = 0;
		slot->BindlessSampledTextureCapacity = 0;
		slot->BindlessSamplerCapacity = 0;
		slot->BindlessStorageBufferCapacity = 0;
		slot->BindlessReadOnlyBufferCapacity = 0;
		slot->BindlessStorageTextureCapacity = 0;
		slot->BindlessUniformBufferCapacity = 0;
		slot->bHeapDirectlyIndexed = false;
		slot->bDeviceHeapIndexed = false;
		slot->AccelStructSlot = 0xFFFFFFFFu;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreePipeline;
			m_State->m_FreePipeline = handle.Slot;
		}

		return Result<void>::Success();
	}
}
