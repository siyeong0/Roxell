#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::validatePreparedDependencies(const PreparedRecordingGroup& group)
	{
		bool bSingleQueue = true;
		for (rhi::PreparedRecordingList const& list : group.Lists)
		{
			bSingleQueue &= list.Queue == group.Lists[0].Queue;
		}
		if (bSingleQueue)
		{
			// Native queue order already supplies ancestry between every pair of lists.
			return Result<void>::Success();
		}
		// Sort by actual resource identity so unrelated resources do not participate in pair checks.
		struct Access final
		{
			uint32 Kind = 0;
			uint32 Resource = 0;
			uint32 List = 0;
			uint32 State = 0;

			uint32 BaseMip = 0;
			uint32 MipCount = 0;

			uint32 BaseLayer = 0;
			uint32 LayerCount = 0;

			bool bWrite = false;
		};

		usize count = 0;
		for (rhi::PreparedRecordingList const& list : group.Lists)
		{
			for (rhi::PreparedRecordingPhase const& phase : list.Phases)
			{
				if (!AddMemorySize(count, phase.Buffers.Size(), &count) ||
					!AddMemorySize(count, phase.Textures.Size(), &count) ||
					!AddMemorySize(count, phase.AccelStructs.Size(), &count))
				{
					return preparedFailure(EErrorCode::ExternalFailure);
				}
			}
		}

		Array<Access> accesses(m_State->m_Resource);
		if (!accesses.Reserve(count))
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}

		for (uint32 listIndex = 0; listIndex < group.Lists.Size(); ++listIndex)
		{
			for (rhi::PreparedRecordingPhase const& phase : group.Lists[listIndex].Phases)
			{
				for (rhi::PreparedBufferAccessDesc const& source : phase.Buffers)
				{
					const bool bWrite = source.State == EBufferState::CopyDestination ||
						source.State == EBufferState::Storage ||
						source.State == EBufferState::AccelScratch ||
						(source.State == EBufferState::AccelStorage && preparedAccelStorageWrites(phase, source.Buffer));
					(void)accesses.Add({ 0, source.Buffer.Slot, listIndex, static_cast<uint32>(source.State), 0, 0, 0, 0, bWrite });
				}
				for (rhi::PreparedTextureAccessDesc const& source : phase.Textures)
				{
					const bool bWrite = source.State != ETextureState::CopySource && source.State != ETextureState::ShaderRead && source.State != ETextureState::ResolveSource;
					(void)accesses.Add({ 1, source.Texture.Slot, listIndex, static_cast<uint32>(source.State), source.BaseMipLevel,
										source.MipLevelCount, source.BaseArrayLayer, source.ArrayLayerCount, bWrite });
				}
				for (rhi::PreparedAccelStructAccessDesc const& source : phase.AccelStructs)
				{
					const bool bWrite = source.State == EAccelStructState::BuildWrite || source.State == EAccelStructState::CopyDestination;
					(void)accesses.Add(
						{ 2, source.AccelStruct.Slot, listIndex, static_cast<uint32>(source.State), 0, 0, 0, 0, bWrite });
				}
			}
		}

		if (!accesses.IsEmpty())
		{
			Sort(accesses.begin(), accesses.end(), [](const Access& left, const Access& right) noexcept
				{
					return left.Kind < right.Kind || (left.Kind == right.Kind && left.Resource < right.Resource);
				});
		}

		for (usize begin = 0; begin < accesses.Size();)
		{
			usize end = begin + 1;
			while (end < accesses.Size() && accesses[end].Kind == accesses[begin].Kind && accesses[end].Resource == accesses[begin].Resource)
			{
				++end;
			}
			for (usize index = begin; index < end; ++index)
			{
				Access const& current = accesses[index];
				for (usize previous = begin; previous < index; ++previous)
				{
					Access const& prior = accesses[previous];
					if (current.List == prior.List || group.HappensBefore(prior.List, current.List) || group.HappensBefore(current.List, prior.List))
					{
						continue;
					}
					const bool bTexture = current.Kind == 1;
					if (bTexture &&
						(uint64(current.BaseMip) + current.MipCount <= prior.BaseMip ||
						 uint64(prior.BaseMip) + prior.MipCount <= current.BaseMip ||
						 uint64(current.BaseLayer) + current.LayerCount <= prior.BaseLayer ||
						 uint64(prior.BaseLayer) + prior.LayerCount <= current.BaseLayer))
					{
						continue;
					}
					ASSERT(!current.bWrite && !prior.bWrite && current.State == prior.State, "Resource access, aliases, and dependencies must match the prepared command declaration.");
					if (bTexture && m_State->m_Kind == EBackend::D3D12)
					{
						// This prepared profile restores non-simultaneous-access images to COMMON.
						// Unordered texture readers would each emit transitions; require an edge.
						return preparedFailure(EErrorCode::Conflict);
					}
					// Vulkan checks exact raw layouts after its ancestor-only native seeds are built.
				}
			}
			begin = end;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] bool Device::preparedBufferContains(
		const PreparedRecordingPhase& phase,
		BufferHandle buffer,
		EBufferState state,
		uint64 offset,
		uint64 bytes) const noexcept
	{
		if (phase.bBufferLookupReady)
		{
			return phase.ContainsBuffer(buffer, state, offset, bytes);
		}
		for (rhi::PreparedBufferAccessDesc const& access : phase.Buffers)
		{
			if (access.Buffer == buffer &&
				(access.State == state ||
				 (access.State == EBufferState::VertexAndIndex && (state == EBufferState::Vertex || state == EBufferState::Index)) ||
				 (access.State == EBufferState::VertexAndReadOnlyStorage && (state == EBufferState::Vertex || state == EBufferState::ReadOnlyStorage))) &&
				offset >= access.Range.ByteOffset &&
				offset - access.Range.ByteOffset <= access.Range.ByteCount &&
				bytes <= access.Range.ByteCount - (offset - access.Range.ByteOffset))
			{
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] ETrackedResourceState Device::expectedVulkanSharedReadState(const TextureDesc& texture, ETextureState state) noexcept
	{
		const FormatInfo& info = GetFormatInfo(texture.Format);
		if (info.bHasStencil || state == ETextureState::ShaderRead || (state == ETextureState::ResolveSource && (info.bHasDepth || info.Kind == EFormatKind::Integer)))
		{
			return (info.bHasDepth ? ETrackedResourceState::DepthRead : ETrackedResourceState::Unknown) |
				(HasUsage(texture.Usage, ETextureUsage::ShaderRead) ? ETrackedResourceState::ShaderRead
					: ETrackedResourceState::Unknown) |
				(HasUsage(texture.Usage, ETextureUsage::Storage) ? ETrackedResourceState::Storage : ETrackedResourceState::Unknown);
		}

		return TextureUseState(state == ETextureState::ResolveSource ? ETextureState::CopySource : state);
	}

	[[nodiscard]] bool Device::preparedTextureAspectSupported(const TextureDesc& desc, ETextureAspect aspect) noexcept
	{
		const FormatInfo& info = GetFormatInfo(desc.Format);

		return aspect == ETextureAspect::Default ||
			(aspect == ETextureAspect::Depth && info.bHasDepth) ||
			(aspect == ETextureAspect::Stencil && info.bHasStencil) ||
			(aspect == ETextureAspect::Color && !info.bHasDepth && !info.bHasStencil);
	}

	[[nodiscard]] bool Device::preparedTextureContains(const PreparedRecordingPhase& phase, TextureHandle texture, ETextureState state, const TextureViewDesc* view) const noexcept
	{
		const TextureDesc& desc = m_State->m_Textures[texture.Slot].Descriptor;
		const uint32 mip = view != nullptr ? view->BaseMipLevel : 0;
		const uint32 mips = view != nullptr ? view->MipLevelCount : desc.MipLevels;
		const uint32 layer = view != nullptr ? view->BaseArrayLayer : 0;
		const uint32 layers = view != nullptr ? view->ArrayLayerCount : desc.ArrayLayers;
		const FormatInfo& format = GetFormatInfo(desc.Format);
		const ETextureAspect requiredAspect = view != nullptr && view->Aspect != ETextureAspect::Default ? view->Aspect : format.bHasDepth ? ETextureAspect::Depth : ETextureAspect::Color;
		for (const uint32 index : phase.FindTextureAccesses(texture))
		{
			rhi::PreparedTextureAccessDesc const& access = phase.Textures[index];
			const bool bAspectContains = access.Aspect == ETextureAspect::Default || access.Aspect == requiredAspect;
			if (access.Texture == texture &&
				access.State == state &&
				bAspectContains &&
				mip >= access.BaseMipLevel &&
				mip - access.BaseMipLevel <= access.MipLevelCount &&
				mips <= access.MipLevelCount - (mip - access.BaseMipLevel) &&
				layer >= access.BaseArrayLayer &&
				layer - access.BaseArrayLayer <= access.ArrayLayerCount &&
				layers <= access.ArrayLayerCount - (layer - access.BaseArrayLayer))
			{
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] uint32 Device::preparedVertexAlignment(EFormat format) noexcept
	{
		switch (format)
		{
		case EFormat::BGRA4_UNORM:
		case EFormat::B5G6R5_UNORM:
		case EFormat::B5G5R5A1_UNORM:
			return 2;
		case EFormat::R10G10B10A2_UNORM:
		case EFormat::R11G11B10_FLOAT:
			return 4;
		default:
			const FormatInfo& info = GetFormatInfo(format);
			const uint32 components = static_cast<uint32>(info.bHasRed) + static_cast<uint32>(info.bHasGreen) +
				static_cast<uint32>(info.bHasBlue) + static_cast<uint32>(info.bHasAlpha);

			return components != 0 ? Max(uint32{ 1 }, uint32(info.BytesPerBlock) / components) : 1;
		}
	}

	[[nodiscard]] Result<void> Device::validatePreparedVertexInput(PreparedRecordingPhase& phase, PreparedBinding& binding, const GraphicsPipelineSlot& pipeline)
	{
		const uint32 expectedCount = pipeline.VertexStrideBytes != 0 ? pipeline.ExtraVertexBufferCount + 1 : 0;
		ASSERT(binding.VertexBufferCount == expectedCount && (binding.IndexBuffer.IsValid() || (binding.IndexRange.ByteOffset == 0 && binding.IndexRange.ByteCount == 0)),
			"Prepared command arguments must match their declared resources, ranges, and execution order.");

		for (uint32 slot = 0; slot < binding.VertexBufferCount; ++slot)
		{
			PreparedVertexBufferBinding& input = binding.VertexBuffers[slot];
			BufferSlot* buffer = findBuffer(input.Buffer);
			const uint32 stride = slot == 0 ? pipeline.VertexStrideBytes : pipeline.ExtraVertexBufferSlots[slot - 1].StrideBytes;
			ASSERT(buffer != nullptr &&
				stride != 0 &&
				HasUsage(buffer->Usage, EBufferUsage::Vertex) &&
				input.Range.ByteOffset <= buffer->ByteCount,
				"Prepared command arguments must match their declared resources, ranges, and execution order.");
			if (input.Range.ByteCount == 0)
			{
				input.Range.ByteCount = buffer->ByteCount - input.Range.ByteOffset;
			}
			ASSERT(Fits(buffer->ByteCount, input.Range.ByteOffset, input.Range.ByteCount) &&
				input.Range.ByteCount >= stride &&
				(m_State->m_Kind != EBackend::D3D12 || input.Range.ByteCount <= UINT32_MAX),
				"Prepared command arguments must match their declared resources, ranges, and execution order.");
			ASSERT(preparedBufferContains(phase, input.Buffer, EBufferState::Vertex, input.Range.ByteOffset, input.Range.ByteCount),
				"Resource access, aliases, and dependencies must match the prepared command declaration.");

			if (m_State->m_Kind == EBackend::Vulkan)
			{
				for (uint32 attribute = 0; attribute < pipeline.VertexAttributeCount; ++attribute)
				{
					const VertexAttributeDesc& description = pipeline.VertexAttributes[attribute];
					if (description.BufferSlot != slot)
					{
						continue;
					}

					const uint32 alignment = preparedVertexAlignment(description.Format);
					ASSERT((input.Range.ByteOffset + description.OffsetBytes) % alignment == 0 &&
						(input.Range.ByteCount / stride <= 1 || stride % alignment == 0),
						"Prepared command arguments must match their declared resources, ranges, and execution order.");
				}
			}

			binding.VertexElementCounts[slot] = input.Range.ByteCount / stride;
			binding.bPerInstance[slot] = slot != 0 && pipeline.ExtraVertexBufferSlots[slot - 1].bPerInstance;
			binding.NativeVertexInput.Vertices[slot] = { buffer->Native, input.Range.ByteOffset, input.Range.ByteCount, stride };
		}
		binding.NativeVertexInput.VertexCount = binding.VertexBufferCount;

		if (binding.IndexBuffer.IsValid())
		{
			BufferSlot* buffer = findBuffer(binding.IndexBuffer);
			ASSERT(buffer != nullptr &&
				HasUsage(buffer->Usage, EBufferUsage::Index) &&
				(binding.IndexFormat == EFormat::R16_UINT || binding.IndexFormat == EFormat::R32_UINT) &&
				binding.IndexRange.ByteOffset <= buffer->ByteCount,
				"Prepared command arguments must match their declared resources, ranges, and execution order.");
			const uint32 elementBytes = binding.IndexFormat == EFormat::R16_UINT ? 2 : 4;
			if (binding.IndexRange.ByteCount == 0)
			{
				binding.IndexRange.ByteCount = buffer->ByteCount - binding.IndexRange.ByteOffset;
			}
			ASSERT(binding.IndexRange.ByteOffset % elementBytes == 0 &&
				binding.IndexRange.ByteCount % elementBytes == 0 &&
				binding.IndexRange.ByteCount >= elementBytes &&
				Fits(buffer->ByteCount, binding.IndexRange.ByteOffset, binding.IndexRange.ByteCount) &&
				(m_State->m_Kind != EBackend::D3D12 || binding.IndexRange.ByteCount <= UINT32_MAX),
				"Prepared command arguments must match their declared resources, ranges, and execution order.");
			ASSERT(preparedBufferContains(phase, binding.IndexBuffer, EBufferState::Index, binding.IndexRange.ByteOffset, binding.IndexRange.ByteCount),
				"Resource access, aliases, and dependencies must match the prepared command declaration.");

			binding.NativeVertexInput.IndexBuffer = buffer->Native;
			binding.NativeVertexInput.IndexRange = binding.IndexRange;
			binding.NativeVertexInput.IndexFormat = binding.IndexFormat;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] bool Device::preparedVertexBounds(const PreparedBinding& binding, uint64 vertexEnd, uint64 instanceEnd) noexcept
	{
		for (uint32 slot = 0; slot < binding.VertexBufferCount; ++slot)
		{
			if ((binding.bPerInstance[slot] ? instanceEnd : vertexEnd) > binding.VertexElementCounts[slot])
			{
				return false;
			}
		}

		return true;
	}

	[[nodiscard]] Result<void> Device::validatePreparedBinding(CommandSlot& command, PreparedRecordingPhase& phase, PreparedBinding& binding, EQueue queue)
	{
		ROX_PROFILE_DEEP_SCOPE("RHI", "ValidatePreparedBinding");
		const bool bGraphics = binding.GraphicsPipeline.IsValid();
		const bool bRay = binding.ShaderTable.IsValid();
		ASSERT(uint32(binding.ComputePipeline.IsValid()) + uint32(bGraphics) + uint32(bRay) == 1, "A prepared binding requires exactly one compute, graphics, or ray pipeline.");
		BindingLayoutHandle layouts[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		uint32 count = 0;
		bool bDeviceHeapIndexed = false;
		if (binding.Execution)
		{
			// Compilation proved immutable layout, stride and format rules. Only live generations
			// and this execution's access coverage can authorize their use now.
			ASSERT(!(bGraphics ? (queue != EQueue::Graphics || !findGraphicsPipeline(binding.GraphicsPipeline)) : (queue == EQueue::Copy || !findPipeline(binding.ComputePipeline))),
				"Prepared command arguments must match their declared resources, ranges, and execution order.");
			const PreparedBinding& shape = binding.Execution->Shape;
			binding.NativePipeline = shape.NativePipeline;
			binding.PushConstantBytes = shape.PushConstantBytes;
			binding.bMeshShader = shape.bMeshShader;
			binding.bTaskShader = shape.bTaskShader;
			binding.NativeVertexInput = shape.NativeVertexInput;
			for (uint32 index = 0; index < binding.VertexBufferCount; ++index)
			{
				const PreparedVertexBufferBinding& input = binding.VertexBuffers[index];
				ASSERT(findBuffer(input.Buffer), "Prepared command arguments must match their declared resources, ranges, and execution order.");
				ASSERT(preparedBufferContains(phase, input.Buffer, EBufferState::Vertex, input.Range.ByteOffset, input.Range.ByteCount),
					"Resource access, aliases, and dependencies must match the prepared command declaration.");
				binding.VertexElementCounts[index] = shape.VertexElementCounts[index];
				binding.bPerInstance[index] = shape.bPerInstance[index];
			}
			if (binding.IndexBuffer.IsValid())
			{
				ASSERT(findBuffer(binding.IndexBuffer), "Prepared command arguments must match their declared resources, ranges, and execution order.");
				ASSERT(preparedBufferContains(phase, binding.IndexBuffer, EBufferState::Index, binding.IndexRange.ByteOffset, binding.IndexRange.ByteCount),
					"Resource access, aliases, and dependencies must match the prepared command declaration.");
			}
			count = binding.SetCount;
			for (uint32 index = 0; index < count; ++index)
			{
				const BindingSetSlot* set = findBindingSet(binding.Sets[index]);
				ASSERT(set, "Prepared command arguments must match their declared resources, ranges, and execution order.");
				layouts[index] = set->Layout;
			}
		}
		else if (bRay)
		{
			ShaderTableSlot* table = findShaderTable(binding.ShaderTable);
			RayTracingPipelineSlot* pipeline = table != nullptr ? findRayTracingPipeline(table->Pipeline) : nullptr;
			ASSERT(pipeline != nullptr &&
				queue != EQueue::Copy &&
				binding.VertexBufferCount == 0 &&
				!binding.IndexBuffer.IsValid() &&
				binding.IndexRange.ByteOffset == 0 &&
				binding.IndexRange.ByteCount == 0 &&
				pipeline->AccelStructSlot == UINT32_MAX,
				"Prepared ray bindings require a ray pipeline, a compatible queue, and no graphics geometry.");
			if (!m_State->m_Backend.Capabilities.Features.bRayTracingPipeline)
			{
				return preparedFailure(EErrorCode::Unsupported);
			}
			if (backend::HAS_CREATE_PREPARED_SHADER_TABLE == false ||
				backend::HAS_PREPARE_RAY_BINDING == false ||
				backend::HAS_RECORD_PREPARED_RAY_DISPATCH == false ||
				table->RayGenerationLocalSet.IsValid())
			{
				return preparedFailure(EErrorCode::Unsupported);
			}
			for (uint32 index = 0; index < table->MissShaderCount; ++index)
			{
				if (table->MissLocalSets[index].IsValid())
				{
					return preparedFailure(EErrorCode::Unsupported);
				}
			}
			for (uint32 index = 0; index < table->HitGroupCount; ++index)
			{
				if (table->HitGroupLocalSets[index].IsValid())
				{
					return preparedFailure(EErrorCode::Unsupported);
				}
			}
			for (uint32 index = 0; index < table->CallableShaderCount; ++index)
			{
				if (table->CallableLocalSets[index].IsValid())
				{
					return preparedFailure(EErrorCode::Unsupported);
				}
			}

			Result<void> local = validatePreparedRayLocalBindings(*table, phase);
			if (!local)
			{
				return local;
			}
			binding.NativePipeline = pipeline->Native;
			binding.PushConstantBytes = pipeline->PushConstantBytes;
			count = pipeline->LayoutCount;
			for (uint32 index = 0; index < count; ++index)
			{
				layouts[index] = pipeline->Layouts[index].Handle;
			}
		}
		else if (bGraphics)
		{
			GraphicsPipelineSlot* pipeline = findGraphicsPipeline(binding.GraphicsPipeline);
			ASSERT(pipeline != nullptr && queue == EQueue::Graphics, "Prepared command arguments must match their declared resources, ranges, and execution order.");
			if ((pipeline->bHeapDirectlyIndexed && !pipeline->bDeviceHeapIndexed) ||
				pipeline->AccelStructSlot != UINT32_MAX ||
				pipeline->BindlessSampledTextureCapacity != 0 ||
				pipeline->BindlessSamplerCapacity != 0 ||
				pipeline->BindlessStorageBufferCapacity != 0 ||
				pipeline->BindlessStorageTextureCapacity != 0 ||
				pipeline->BindlessUniformBufferCapacity != 0 ||
				pipeline->BindlessReadOnlyBufferCapacity != 0)
			{
				return preparedFailure(EErrorCode::Unsupported);
			}
			if (pipeline->LayoutCount == 0 && !pipeline->Layout.IsValid() && (pipeline->BufferBindingCount != 0 || pipeline->SampledTextureCount != 0 || pipeline->StorageTextureCount != 0))
			{
				return preparedFailure(EErrorCode::Unsupported);
			}
			ASSERT(!pipeline->bMeshShader || (binding.VertexBufferCount == 0 && !binding.IndexBuffer.IsValid() && binding.IndexRange.ByteOffset == 0 && binding.IndexRange.ByteCount == 0),
				"Prepared command arguments must match their declared resources, ranges, and execution order.");

			Result<void> inputValid = validatePreparedVertexInput(phase, binding, *pipeline);
			if (!inputValid)
			{
				return inputValid;
			}

			binding.NativePipeline = pipeline->Native;
			binding.PushConstantBytes = pipeline->PushConstantBytes;
			binding.bMeshShader = pipeline->bMeshShader;
			binding.bTaskShader = pipeline->bTaskShader;
			bDeviceHeapIndexed = pipeline->bDeviceHeapIndexed;
			count = pipeline->LayoutCount != 0 ? pipeline->LayoutCount : (pipeline->Layout.IsValid() ? 1u : 0u);
			for (uint32 index = 0; index < count; ++index)
			{
				layouts[index] = pipeline->LayoutCount != 0 ? pipeline->Layouts[index] : pipeline->Layout;
			}
		}
		else
		{
			ASSERT(binding.VertexBufferCount == 0 && !binding.IndexBuffer.IsValid() && binding.IndexRange.ByteOffset == 0 && binding.IndexRange.ByteCount == 0,
				"Prepared command arguments must match their declared resources, ranges, and execution order.");

			PipelineSlot* pipeline = findPipeline(binding.ComputePipeline);
			ASSERT(pipeline != nullptr && queue != EQueue::Copy, "Prepared command arguments must match their declared resources, ranges, and execution order.");
			if ((pipeline->bHeapDirectlyIndexed && !pipeline->bDeviceHeapIndexed) ||
				pipeline->AccelStructSlot != UINT32_MAX ||
				pipeline->BindlessSampledTextureCapacity != 0 ||
				pipeline->BindlessSamplerCapacity != 0 ||
				pipeline->BindlessStorageBufferCapacity != 0 ||
				pipeline->BindlessStorageTextureCapacity != 0 ||
				pipeline->BindlessUniformBufferCapacity != 0 ||
				pipeline->BindlessReadOnlyBufferCapacity != 0)
			{
				return preparedFailure(EErrorCode::Unsupported);
			}
			if (pipeline->LayoutCount == 0 && !pipeline->Layout.IsValid() && (!pipeline->Bindings.IsEmpty() || pipeline->TextureCount != 0))
			{
				return preparedFailure(EErrorCode::Unsupported);
			}
			binding.NativePipeline = pipeline->Native;
			binding.PushConstantBytes = pipeline->PushConstantBytes;
			bDeviceHeapIndexed = pipeline->bDeviceHeapIndexed;
			count = pipeline->LayoutCount != 0 ? pipeline->LayoutCount : (pipeline->Layout.IsValid() ? 1u : 0u);
			for (uint32 index = 0; index < count; ++index)
			{
				layouts[index] = pipeline->LayoutCount != 0 ? pipeline->Layouts[index].Handle : pipeline->Layout;
			}
		}
		if (binding.DeviceHeaps)
		{
			DescriptorHeapBinding inputs[32]{};
			for (usize index = 0; index < binding.DeviceHeaps->Ranges.Size(); ++index)
			{
				rhi::BoundDescriptorTableRange const& row = binding.DeviceHeaps->Ranges[index];
				inputs[index] = {
					row.Table, row.UsedCount != 0
								   ? Span<const uint32>{binding.DeviceHeaps->UsedEntries.Data() + row.UsedOffset, row.UsedCount}
								   : Span<const uint32>{} };
			}
			Result<SharedPtr<BoundDescriptorTables>> captured =
				captureDescriptorHeapBindings({ inputs, binding.DeviceHeaps->Ranges.Size() }, bDeviceHeapIndexed);
			if (!captured)
			{
				return preparedFailure(captured.GetError().Code);
			}

			binding.DeviceHeaps = Move(captured).TakeValue();
			Result<void> valid = validateDeviceHeapUses(command, binding.DeviceHeaps.Get(), &phase);
			if (!valid)
			{
				return valid;
			}
		}
		ASSERT(binding.SetCount == count, "Prepared command arguments must match their declared resources, ranges, and execution order.");
		for (uint32 setIndex = 0; setIndex < count; ++setIndex)
		{
			BindingSetSlot* set = findBindingSet(binding.Sets[setIndex]);
			ASSERT(set != nullptr && set->Layout == layouts[setIndex], "Prepared command arguments must match their declared resources, ranges, and execution order.");

			rhi::BindingLayoutSlot const& layout = m_State->m_BindingLayouts[set->Layout.Slot];
			if (layout.RangeCount == 0)
			{
				return preparedFailure(EErrorCode::Unsupported);
			}
			const bool bCacheable = !bRay && !set->Tables && set->AccelStructCount == 0;
			const uint64 cacheKey = uint64(binding.Sets[setIndex].Slot) * 2 + (bGraphics ? 1u : 0u);
			if (bCacheable)
			{
				rhi::BindingSetHandle const* cached = phase.ValidatedFixedSets.Find(cacheKey);
				if (cached && *cached == binding.Sets[setIndex])
				{
					continue;
				}
			}
			if (set->Tables)
			{
				for (rhi::BoundDescriptorTableRange const& row : set->Tables->Ranges)
				{
					rhi::DescriptorTableSlot const& table = m_State->m_DescriptorTables[row.Table.Slot];
					if (table.RecordedUses == NumericLimits<usize>::max())
					{
						return preparedFailure(EErrorCode::ExternalFailure);
					}
					for (uint32 used = 0; used < row.UsedCount; ++used)
					{
						const uint32 entryIndex = set->Tables->UsedEntries[row.UsedOffset + used];
						rhi::DescriptorTableEntry const& entry = table.Entries[entryIndex];
						const EDescriptorType type = table.Descriptor.Type;
						if (entry.Buffer.IsValid())
						{
							const uint64 bytes = entry.Range.ByteCount != 0 ? entry.Range.ByteCount : m_State->m_Buffers[entry.Buffer.Slot].ByteCount - entry.Range.ByteOffset;
							const EBufferState state = type == EDescriptorType::UniformBuffer ? EBufferState::Uniform
								: type == EDescriptorType::ReadOnlyBuffer ? EBufferState::ReadOnlyStorage
								: EBufferState::Storage;
							ASSERT(preparedBufferContains(phase, entry.Buffer, state, entry.Range.ByteOffset, bytes),
								"Resource access, aliases, and dependencies must match the prepared command declaration.");
						}
						else if (entry.Texture.IsValid())
						{
							const bool bStorage = type == EDescriptorType::StorageTexture;
							const TextureViewDesc* view = entry.View.IsValid() ? &m_State->m_TextureViews[entry.View.Slot].Descriptor : nullptr;
							ASSERT(preparedTextureContains(phase, entry.Texture, bStorage ? ETextureState::Storage : ETextureState::ShaderRead, view) ||
								(!bStorage && preparedTextureContains(phase, entry.Texture, ETextureState::DepthRead, view)),
								"Resource access, aliases, and dependencies must match the prepared command declaration.");
						}
						else if (entry.AccelerationStructure.IsValid())
						{
							ASSERT(bRay && preparedAccelContains(phase, entry.AccelerationStructure, EAccelStructState::ShaderRead),
								"Resource access, aliases, and dependencies must match the prepared command declaration.");
							if (entry.BottomReferences)
							{
								for (rhi::AccelStructHandle const child : *entry.BottomReferences)
								{
									ASSERT(preparedAccelContains(phase, child, EAccelStructState::ShaderRead),
										"Resource access, aliases, and dependencies must match the prepared command declaration.");
								}
							}
						}
					}
				}
			}
			for (uint32 index = 0; index < set->BufferCount; ++index)
			{
				rhi::BufferSlot const& buffer = m_State->m_Buffers[set->Buffers[index].Slot];
				const BufferRange range = resolveBufferRange(binding.Sets[setIndex], index, buffer.ByteCount);
				const uint32 type = bGraphics || bRay ? static_cast<uint32>(layout.GraphicsBuffer(index).Type) : static_cast<uint32>(layout.ComputeBuffer(index).Type);
				const EBufferState state = type == 0 ? EBufferState::Uniform : type == 2 ? EBufferState::ReadOnlyStorage : EBufferState::Storage;
				ASSERT(preparedBufferContains(phase, set->Buffers[index], state, range.ByteOffset, range.ByteCount),
					"Resource access, aliases, and dependencies must match the prepared command declaration.");
			}
			for (uint32 index = 0; index < set->AccelStructCount; ++index)
			{
				ASSERT(bRay &&
					preparedAccelContains(phase, set->AccelStructs[index], EAccelStructState::ShaderRead),
					"Resource access, aliases, and dependencies must match the prepared command declaration.");
			}
			for (uint32 index = 0; index < set->TextureCount + set->StorageTextureCount; ++index)
			{
				const bool bStorage = index >= set->TextureCount;
				const uint32 at = bStorage ? index - set->TextureCount : index;
				const TextureHandle texture = bStorage ? set->StorageTextures[at] : set->SampledTextures[at];
				const TextureViewHandle viewHandle = bStorage ? set->StorageViews[at] : set->SampledViews[at];
				const TextureViewDesc* view = viewHandle.IsValid() ? &m_State->m_TextureViews[viewHandle.Slot].Descriptor : nullptr;
				ASSERT(preparedTextureContains(phase, texture, bStorage ? ETextureState::Storage : ETextureState::ShaderRead, view) ||
					(!bStorage && preparedTextureContains(phase, texture, ETextureState::DepthRead, view)),
					"Resource access, aliases, and dependencies must match the prepared command declaration.");
			}
			if (bCacheable && phase.ValidatedFixedSets.Size() < phase.AccessProgram->FixedSetCoverageLimit)
			{
				HashMap<uint64, BindingSetHandle>::InsertResult retained = phase.ValidatedFixedSets.Emplace(cacheKey, binding.Sets[setIndex]);
				if (!retained.Value)
				{
					return preparedFailure(EErrorCode::ExternalFailure);
				}
				*retained.Value = binding.Sets[setIndex];
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::validatePreparedIndirect(PreparedRecordingPhase& phase, PreparedRecordingIndirect& indirect, EQueue queue)
	{
		PreparedIndirectDesc& tuple = indirect.Description;
		ASSERT(tuple.Kind <= EPreparedIndirectKind::Mesh &&
			tuple.BindingSlot < phase.Bindings.Size() &&
			tuple.MaximumCount != 0 &&
			tuple.MaximumCount <= 65535 &&
			tuple.ArgumentsRange.ByteOffset % 4 == 0 &&
			(tuple.CountBuffer.IsValid() || tuple.CountBufferOffsetBytes == 0),
			"Prepared command arguments must match their declared resources, ranges, and execution order.");

		rhi::PreparedBinding const& binding = phase.Bindings[tuple.BindingSlot];
		const bool bCompute = tuple.Kind == EPreparedIndirectKind::Compute;
		const bool bMesh = tuple.Kind == EPreparedIndirectKind::Mesh;
		ASSERT(!(bCompute ? !binding.ComputePipeline.IsValid() : !binding.GraphicsPipeline.IsValid()) &&
			(bCompute || binding.bMeshShader == bMesh) &&
			queue != EQueue::Copy &&
			(bCompute || queue == EQueue::Graphics) &&
			!(tuple.Kind == EPreparedIndirectKind::DrawIndexed ? !binding.IndexBuffer.IsValid() : binding.IndexBuffer.IsValid()),
			"Prepared command arguments must match their declared resources, ranges, and execution order.");
		if ((bCompute || bMesh) && (tuple.MaximumCount != 1 || tuple.CountBuffer.IsValid()))
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		if (bMesh && !m_State->m_Backend.Capabilities.Features.bMeshIndirect)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		if constexpr (backend::HAS_PREPARE_INDIRECT == false || backend::HAS_RECORD_PREPARED_INDIRECT == false)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}

		const uint32 recordBytes = bCompute ? static_cast<uint32>(sizeof(IndirectDispatchArguments))
			: bMesh ? static_cast<uint32>(sizeof(IndirectMeshArguments))
			: tuple.Kind == EPreparedIndirectKind::DrawIndexed
			? static_cast<uint32>(sizeof(IndirectDrawIndexedArguments))
			: static_cast<uint32>(sizeof(IndirectDrawArguments));
		if (tuple.ArgumentStrideBytes == 0)
		{
			tuple.ArgumentStrideBytes = recordBytes;
		}
		ASSERT(tuple.ArgumentStrideBytes >= recordBytes && tuple.ArgumentStrideBytes % 4 == 0, "Prepared command arguments must match their declared resources, ranges, and execution order.");

		const BufferSlot* arguments = findBuffer(tuple.Arguments);
		ASSERT(arguments != nullptr &&
			!arguments->bMapped &&
			HasUsage(arguments->Usage, EBufferUsage::Indirect) &&
			tuple.ArgumentsRange.ByteOffset <= arguments->ByteCount,
			"Prepared command arguments must match their declared resources, ranges, and execution order.");
		if (tuple.ArgumentsRange.ByteCount == 0)
		{
			tuple.ArgumentsRange.ByteCount = arguments->ByteCount - tuple.ArgumentsRange.ByteOffset;
		}
		const uint64 requiredBytes = m_State->m_Kind == EBackend::D3D12
			? uint64(tuple.MaximumCount) * tuple.ArgumentStrideBytes
			: uint64(tuple.MaximumCount - 1) * tuple.ArgumentStrideBytes + recordBytes;
		ASSERT(requiredBytes <= tuple.ArgumentsRange.ByteCount && Fits(arguments->ByteCount, tuple.ArgumentsRange.ByteOffset, tuple.ArgumentsRange.ByteCount),
			"Prepared command arguments must match their declared resources, ranges, and execution order.");
		ASSERT(preparedBufferContains(phase, tuple.Arguments, EBufferState::Indirect, tuple.ArgumentsRange.ByteOffset, tuple.ArgumentsRange.ByteCount),
			"Resource access, aliases, and dependencies must match the prepared command declaration.");

		void* countNative = nullptr;
		if (tuple.CountBuffer.IsValid())
		{
			const bool bCountSupported = tuple.Kind == EPreparedIndirectKind::DrawIndexed ? m_State->m_Backend.Capabilities.Features.bDrawIndexedIndirectCount
				: m_State->m_Backend.Capabilities.Features.bDrawIndirectCount;
			if (!bCountSupported ||
				tuple.MaximumCount > m_State->m_Backend.Capabilities.Limits.MaximumCountedIndirectDraws ||
				(m_State->m_Kind == EBackend::Vulkan && tuple.MaximumCount > 1 && !m_State->m_Backend.Capabilities.Features.bMultiDrawIndirect))
			{
				return preparedFailure(EErrorCode::Unsupported);
			}
			const BufferSlot* count = findBuffer(tuple.CountBuffer);
			ASSERT(count != nullptr &&
				!count->bMapped &&
				HasUsage(count->Usage, EBufferUsage::Indirect) &&
				tuple.CountBufferOffsetBytes % 4 == 0 &&
				Fits(count->ByteCount, tuple.CountBufferOffsetBytes, sizeof(uint32)),
				"Prepared command arguments must match their declared resources, ranges, and execution order.");

			ASSERT(preparedBufferContains(phase, tuple.CountBuffer, EBufferState::Indirect, tuple.CountBufferOffsetBytes, sizeof(uint32)),
				"Resource access, aliases, and dependencies must match the prepared command declaration.");
			countNative = count->Native;
		}

		indirect.Native = { tuple.Kind,		   arguments->Native, tuple.ArgumentsRange.ByteOffset, tuple.ArgumentStrideBytes,
						   tuple.MaximumCount, countNative,		  tuple.CountBufferOffsetBytes };

		return Result<void>::Success();
	}
}
