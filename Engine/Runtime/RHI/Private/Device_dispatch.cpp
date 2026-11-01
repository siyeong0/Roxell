#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::DispatchCompute(CommandListHandle handle, PreparedBindingRef ref, const ComputeDispatch& dispatch)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(phase != nullptr && ref.List == handle && ref.Phase == preparedList(command)->ActivePhase && ref.Slot < phase->Bindings.Size() && !command->bRendering,
			"Prepared dispatch uses a binding from the active phase outside rendering.");

		rhi::PreparedBinding const& binding = phase->Bindings[ref.Slot];
		ASSERT(binding.ComputePipeline.IsValid() && dispatch.PushConstants.Size() == binding.PushConstantBytes, "Prepared dispatch matches the captured compute pipeline and push extent.");
		ASSERT(dispatch.GroupsX != 0 &&
			dispatch.GroupsY != 0 &&
			dispatch.GroupsZ != 0 &&
			dispatch.GroupsX <= m_State->m_Backend.Capabilities.Limits.MaximumComputeWorkGroupCount[0] &&
			dispatch.GroupsY <= m_State->m_Backend.Capabilities.Limits.MaximumComputeWorkGroupCount[1] &&
			dispatch.GroupsZ <= m_State->m_Backend.Capabilities.Limits.MaximumComputeWorkGroupCount[2],
			"Prepared dispatch group counts fit the device limits.");

		ASSERT(phase->Used.Dispatches < phase->Capacity.Dispatches, "Prepared dispatch stays within its admitted capacity.");

		Result<void> result = recordNative(
			[&]
			{
				return backend::RecordPreparedDispatch(m_State->m_Backend.State, command->Native, binding.NativePipeline, binding.NativeIndex,
					dispatch);
			});
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}

		++phase->Used.Dispatches;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::DispatchMesh(CommandListHandle handle, PreparedBindingRef ref, const MeshDispatch& dispatch)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(phase != nullptr && ref.List == handle && ref.Phase == preparedList(command)->ActivePhase && ref.Slot < phase->Bindings.Size() && command->bRendering,
			"Prepared mesh dispatch uses a binding from the active render phase.");

		rhi::PreparedBinding const& binding = phase->Bindings[ref.Slot];
		ASSERT(binding.GraphicsPipeline.IsValid() &&
			binding.bMeshShader &&
			dispatch.PushConstants.Size() == binding.PushConstantBytes &&
			matchesRendering(*command, m_State->m_GraphicsPipelines[binding.GraphicsPipeline.Slot]) &&
			command->ViewportCount == 1 &&
			command->ScissorCount == 1,
			"Prepared mesh dispatch matches its captured graphics pipeline and rendering state.");
		if (!m_State->m_Backend.Capabilities.Features.bMeshShader || backend::HAS_RECORD_PREPARED_MESH_DISPATCH == false)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		const GpuCapabilities& capabilities = m_State->m_Backend.Capabilities;
		const uint32* maximum = binding.bTaskShader ? capabilities.Limits.MaximumTaskGroups : capabilities.Limits.MaximumMeshGroups;
		const uint32 total = binding.bTaskShader ? capabilities.Limits.MaximumTaskGroupTotalCount : capabilities.Limits.MaximumMeshGroupTotalCount;
		ASSERT(meshGroupsFit(dispatch.GroupsX, dispatch.GroupsY, dispatch.GroupsZ, maximum, total), "Prepared mesh dispatch group counts fit the device limits.");
		ASSERT(phase->Used.Draws < phase->Capacity.Draws, "Prepared mesh dispatch stays within its admitted capacity.");

		Result<void> result = recordNative(
			[&]
			{
				return backend::RecordPreparedMeshDispatch(m_State->m_Backend.State, command->Native, binding.NativePipeline,
					binding.NativeIndex, dispatch);
			});
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}

		++phase->Used.Draws;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::dispatchComputeMultiple(
		CommandListHandle list,
		PipelineSlot& pipeline,
		ComputePipelineHandle pipelineHandle,
		Span<const BindingSetHandle> setHandles,
		Span<const Byte> pushConstants,
		uint32 groupsX,
		uint32 groupsY,
		uint32 groupsZ,
		BufferHandle argumentHandle,
		uint64 argumentOffset,
		DescriptorTableHandle sampledTableHandle,
		DescriptorTableHandle samplerTableHandle,
		DescriptorTableHandle storageBufferTableHandle,
		DescriptorTableHandle storageTextureTableHandle,
		DescriptorTableHandle uniformBufferTableHandle,
		DescriptorTableHandle readOnlyBufferTableHandle,
		AccelStructHandle accelHandle,
		Span<const DescriptorHeapBinding> deviceHeaps)
	{
		CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		const auto fail = [&](const Error& error)
			{
				ASSERT(error.Code != EErrorCode::InvalidArgument, "Compute dispatch bindings, arguments, and resource aliases must be valid.");
				poisonCommandList(list);

				return Result<void>::Failure(error);
			};

		const bool bIndirect = argumentHandle.IsValid();
		BufferSlot* const arguments = bIndirect ? findBuffer(argumentHandle) : nullptr;

		ASSERT(!(bIndirect && arguments == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!(command->Queue == EQueue::Copy ||
				command->bRendering ||
				setHandles.Size() != pipeline.LayoutCount ||
				pushConstants.Size() != pipeline.PushConstantBytes ||
				(bIndirect ? (!HasUsage(arguments->Usage, EBufferUsage::Indirect) || argumentOffset % 4 != 0 ||
				!Fits(arguments->ByteCount, argumentOffset, sizeof(IndirectDispatchArguments)))
				: (groupsX == 0 || groupsY == 0 || groupsZ == 0 || groupsX > m_State->m_Backend.Capabilities.Limits.MaximumComputeWorkGroupCount[0] ||
					groupsY > m_State->m_Backend.Capabilities.Limits.MaximumComputeWorkGroupCount[1] || groupsZ > m_State->m_Backend.Capabilities.Limits.MaximumComputeWorkGroupCount[2]))), "Compute dispatch bindings, arguments, and resource aliases must be valid.");

		if constexpr (backend::HAS_RECORD_COMPUTE_DISPATCH_MULTIPLE == false)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ComputeDispatch", "Multi-layout compute dispatch is unavailable.");
		}

		Result<SharedPtr<BoundDescriptorTables>> heapBindings = captureDescriptorHeapBindings(deviceHeaps, pipeline.bDeviceHeapIndexed);
		if (!heapBindings)
		{
			return fail(heapBindings.GetError());
		}
		const DescriptorTableHandle namedHeaps[6]{ sampledTableHandle,		 samplerTableHandle,	   storageBufferTableHandle,
												  storageTextureTableHandle, uniformBufferTableHandle, readOnlyBufferTableHandle };
		Result<void> heapValid = validateOrdinaryDeviceHeapUses(*command, heapBindings.Value().Get(), setHandles, namedHeaps);
		if (!heapValid)
		{
			return fail(heapValid.GetError());
		}

		Result<AccelStructSlot*> resolvedAccel = resolveAccelStruct(pipeline.AccelStructSlot, accelHandle);

		if (!resolvedAccel)
		{
			return fail(resolvedAccel.GetError());
		}
		AccelStructSlot* const accel = resolvedAccel.Value();
		Result<DescriptorTableSlot*> sampledTable = resolveComputeDescriptorTable(pipeline, sampledTableHandle, *command, list.Slot);

		if (!sampledTable)
		{
			return fail(sampledTable.GetError());
		}

		Result<DescriptorTableSlot*> samplerTable = resolveComputeSamplerTable(pipeline, samplerTableHandle, *command, list.Slot);

		if (!samplerTable)
		{
			return fail(samplerTable.GetError());
		}

		Result<DescriptorTableSlot*> storageBufferTable = resolveComputeStorageBufferTable(pipeline, storageBufferTableHandle, *command, list.Slot);

		if (!storageBufferTable)
		{
			return fail(storageBufferTable.GetError());
		}

		Result<DescriptorTableSlot*> storageTextureTable = resolveComputeStorageTextureTable(pipeline, storageTextureTableHandle, *command, list.Slot);

		if (!storageTextureTable)
		{
			return fail(storageTextureTable.GetError());
		}

		Result<DescriptorTableSlot*> uniformBufferTable = resolveComputeUniformBufferTable(pipeline, uniformBufferTableHandle, *command, list.Slot);

		if (!uniformBufferTable)
		{
			return fail(uniformBufferTable.GetError());
		}

		Result<DescriptorTableSlot*> readOnlyBufferTable = resolveComputeReadOnlyBufferTable(pipeline, readOnlyBufferTableHandle, *command, list.Slot);

		if (!readOnlyBufferTable)
		{
			return fail(readOnlyBufferTable.GetError());
		}
		BindingSetSlot* sets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		NativeComputeBuffer buffers[MAXIMUM_FIXED_BINDING_LAYOUTS][8]{};
		NativeComputeTexture textures[MAXIMUM_FIXED_BINDING_LAYOUTS][8]{};
		NativeBindingSet nativeSets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		BufferHandle fixedBuffers[32]{};
		TextureHandle fixedTextures[32]{};
		uint32 bufferCount = 0;
		uint32 textureCount = 0;
		uint32 samplerCount = 0;
		uint32 viewCount = 0;
		uint32 accelCount = 0;

		for (uint32 layoutIndex = 0; layoutIndex < pipeline.LayoutCount; ++layoutIndex)
		{
			const PipelineSlot::FixedLayout& layout = pipeline.Layouts[layoutIndex];
			BindingSetSlot* const set = findBindingSet(setHandles[layoutIndex]);

			ASSERT(set != nullptr, "RHI operations require valid handles for this device.");

			ASSERT(!(set->Layout != layout.Handle || set->BufferCount != layout.BufferCount || set->TextureCount != layout.SampledTextureCount || set->StorageTextureCount != layout.StorageTextureCount), "Compute dispatch bindings, arguments, and resource aliases must be valid.");
			sets[layoutIndex] = set;
			for (uint32 index = 0; index < set->AccelStructCount; ++index)
			{
				const AccelStructSlot* boundAccel = findAccelStruct(set->AccelStructs[index]);
				ASSERT(boundAccel != nullptr, "A binding set retains its acceleration structures.");
				ASSERT(!(!boundAccel->bBuilt || boundAccel->bUnavailable || boundAccel->Type != EAccelStructType::TopLevel), "Compute dispatch bindings, arguments, and resource aliases must be valid.");
			}
			accelCount += set->AccelStructCount;
			const Span<const TextureHandle> sampled = resolveComputeTextures(layout.Handle, setHandles[layoutIndex], {});

			const Span<const TextureHandle> storage = resolveComputeStorageTextures(layout.Handle, setHandles[layoutIndex], {});

			Result<void> validTextures = validateComputeTextures(layout, sampled, storage, *command, list.Slot, setHandles[layoutIndex], textures[layoutIndex]);

			if (!validTextures)
			{
				return fail(validTextures.GetError());
			}

			for (uint32 index = 0; index < layout.TextureCount; ++index)
			{
				for (uint32 previousLayout = 0; previousLayout < layoutIndex; ++previousLayout)
				{
					for (uint32 previous = 0; previous < pipeline.Layouts[previousLayout].TextureCount; ++previous)
					{
						ASSERT(!(textures[layoutIndex][index].Texture == textures[previousLayout][previous].Texture &&
								(textures[layoutIndex][index].Type == EComputeTextureBindingType::StorageTexture ||
								 textures[previousLayout][previous].Type == EComputeTextureBindingType::StorageTexture) &&
								!textureViewsDisjoint(computeTextureView(layout, set, index), computeTextureView(pipeline.Layouts[previousLayout], sets[previousLayout], previous))), "Compute dispatch bindings, arguments, and resource aliases must be valid.");
					}
				}
			}
			textureCount += layout.TextureCount;

			for (uint32 index = 0; index < layout.TextureCount; ++index)
			{
				const bool bStorageBinding = layout.Textures[index].Type == EComputeTextureBindingType::StorageTexture;
				uint32 sampledIndex = 0;
				uint32 storageIndex = 0;

				for (uint32 previous = 0; previous < index; ++previous)
				{
					if (layout.Textures[previous].Type == EComputeTextureBindingType::StorageTexture)
					{
						++storageIndex;
					}
					else
					{
						++sampledIndex;
					}
				}
				fixedTextures[textureCount - layout.TextureCount + index] = bStorageBinding ? set->StorageTextures[storageIndex] : set->SampledTextures[sampledIndex];
			}
			samplerCount += set->SamplerCount;
			viewCount += set->TextureCount + set->StorageTextureCount;

			for (uint32 index = 0; index < layout.BufferCount; ++index)
			{
				BufferSlot* const buffer = findBuffer(set->Buffers[index]);

				ASSERT(buffer != nullptr, "RHI operations require valid handles for this device.");

				const ComputeBindingDesc binding = layout.Buffers[index];
				const BufferRange range = resolveBufferRange(setHandles[layoutIndex], index, buffer->ByteCount);
				const EBufferUsage required = binding.Type == EComputeBindingType::UniformBuffer ? EBufferUsage::Uniform : EBufferUsage::Storage;

				ASSERT(!(!HasUsage(buffer->Usage, required) || (binding.Type == EComputeBindingType::UniformBuffer && range.ByteCount > 65536) || (bIndirect && set->Buffers[index] == argumentHandle)), "Compute dispatch bindings, arguments, and resource aliases must be valid.");

				for (uint32 previousLayout = 0; previousLayout <= layoutIndex; ++previousLayout)
				{
					for (uint32 previous = 0;
						previous < (previousLayout == layoutIndex ? index : pipeline.Layouts[previousLayout].BufferCount); ++previous)
					{
						if (sets[previousLayout]->Buffers[previous] != set->Buffers[index])
						{
							continue;
						}

						const ComputeBindingDesc oldBinding = pipeline.Layouts[previousLayout].Buffers[previous];

						ASSERT(!(binding.Type != oldBinding.Type ||
								!canShareBuffer(binding.Type != EComputeBindingType::StorageBuffer, oldBinding.Type != EComputeBindingType::StorageBuffer, range, resolveBufferRange(setHandles[previousLayout], previous, buffer->ByteCount))), "Compute dispatch bindings, arguments, and resource aliases must be valid.");
					}
				}

				Result<void*> bufferView = resolveBoundBufferView(setHandles[layoutIndex], index, set->Buffers[index], binding.Format, binding.Type == EComputeBindingType::ReadOnlyBuffer);

				if (!bufferView)
				{
					return fail(bufferView.GetError());
				}
				buffers[layoutIndex][index] = { buffer->Native,	binding.Type,		binding.Slot,  range.ByteOffset,
											   range.ByteCount, bufferView.Value(), binding.Format };
				fixedBuffers[bufferCount + index] = set->Buffers[index];
			}
			nativeSets[layoutIndex] = { {buffers[layoutIndex], layout.BufferCount}, {textures[layoutIndex], layout.TextureCount} };
			bufferCount += layout.BufferCount;
		}

		if (bIndirect)
		{
			for (uint32 layoutIndex = 0; layoutIndex < pipeline.LayoutCount; ++layoutIndex)
			{
				const BindingSetSlot& set = *sets[layoutIndex];
				if (!set.Tables)
				{
					continue;
				}
				for (const BoundDescriptorTableRange& row : set.Tables->Ranges)
				{
					const DescriptorTableSlot& table = m_State->m_DescriptorTables[row.Table.Slot];
					for (

						uint32 used = 0; used < row.UsedCount; ++used)
					{
						const DescriptorTableEntry& entry = table.Entries[set.Tables->UsedEntries[row.UsedOffset + used]];
						ASSERT(entry.Buffer != argumentHandle, "Compute dispatch bindings, arguments, and resource aliases must be valid.");
					}
				}
			}
		}

		Result<void> readOnlyAliases =
			checkBufferTableAliases(readOnlyBufferTable.Value(), { fixedBuffers, bufferCount }, argumentHandle);
		if (!readOnlyAliases)
		{
			return fail(readOnlyAliases.GetError());
		}
		readOnlyAliases = checkBufferTableOverlap(storageBufferTable.Value(), readOnlyBufferTable.Value());

		if (!readOnlyAliases)
		{
			return fail(readOnlyAliases.GetError());
		}
		readOnlyAliases = checkBufferTableOverlap(uniformBufferTable.Value(), readOnlyBufferTable.Value());

		if (!readOnlyAliases)
		{
			return fail(readOnlyAliases.GetError());
		}

		Result<void> aliases = checkBufferTableAliases(storageBufferTable.Value(), { fixedBuffers, bufferCount }, argumentHandle);
		if (!aliases)
		{
			return fail(aliases.GetError());
		}
		aliases = checkBufferTableAliases(uniformBufferTable.Value(), { fixedBuffers, bufferCount }, argumentHandle);
		if (!aliases)
		{
			return fail(aliases.GetError());
		}
		aliases = checkBufferTableOverlap(storageBufferTable.Value(), uniformBufferTable.Value());

		if (!aliases)
		{
			return fail(aliases.GetError());
		}

		if (accel != nullptr)
		{
			const BufferHandle backing = accel->Storage;

			ASSERT(!(backing == argumentHandle), "Compute dispatch bindings, arguments, and resource aliases must be valid.");

			for (uint32 index = 0; index < bufferCount; ++index)
			{
				ASSERT(!(fixedBuffers[index] == backing), "Compute dispatch bindings, arguments, and resource aliases must be valid.");
			}

			const DescriptorTableSlot* bufferTables[2]{ storageBufferTable.Value(), uniformBufferTable.Value() };
			for (const DescriptorTableSlot* table : bufferTables)
			{
				if (table == nullptr)
				{
					continue;
				}

				for (const DescriptorTableEntry& entry : table->Entries)
				{
					ASSERT(!(entry.Buffer.IsValid() && entry.Buffer == backing), "Compute dispatch bindings, arguments, and resource aliases must be valid.");
				}
			}
		}
		aliases = checkStorageTableTextureAliases(storageTextureTable.Value(), sampledTable.Value());

		if (!aliases)
		{
			return fail(aliases.GetError());
		}

		const DescriptorTableSlot* textureTables[2]{ sampledTable.Value(), storageTextureTable.Value() };
		for (const DescriptorTableSlot* table : textureTables)
		{
			if (table == nullptr)
			{
				continue;
			}

			for (const DescriptorTableEntry& entry : table->Entries)
			{
				for (uint32 index = 0; index < textureCount; ++index)
				{
					ASSERT(!(entry.Texture.IsValid() && entry.Texture == fixedTextures[index]), "Compute dispatch bindings, arguments, and resource aliases must be valid.");
				}
			}
		}

		const auto tableSize = [](const DescriptorTableSlot* table) -> usize
			{ return table != nullptr ? table->Entries.Size() : 0; };
		const usize tableCount =
			(sampledTable.Value() != nullptr ? 1 : 0) + (samplerTable.Value() != nullptr ? 1 : 0) +
			(storageBufferTable.Value() != nullptr ? 1 : 0) + (storageTextureTable.Value() != nullptr ? 1 : 0) +
			(uniformBufferTable.Value() != nullptr ? 1 : 0) + (readOnlyBufferTable.Value() != nullptr ? 1 : 0);

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + bufferCount + (bIndirect ? 1u : 0u) + (accel != nullptr ? 1u : 0u) + accelCount + tableSize(storageBufferTable.Value()) + tableSize(uniformBufferTable.Value()) + tableSize(readOnlyBufferTable.Value())) ||
			!command->TextureUses.Reserve(command->TextureUses.Size() + textureCount + tableSize(sampledTable.Value()) + tableSize(storageTextureTable.Value())) ||
			!command->SamplerUses.Reserve(command->SamplerUses.Size() + samplerCount + tableSize(samplerTable.Value())) ||
			!command->TextureViewUses.Reserve(command->TextureViewUses.Size() + viewCount) ||
			!command->DescriptorTableUses.Reserve(command->DescriptorTableUses.Size() + tableCount) ||
			!command->AccelUses.Reserve(command->AccelUses.Size() + (accel != nullptr ? 1u : 0u) + accelCount) ||
			!command->PipelineUses.Add({ pipelineHandle, pipeline.Native }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track multi-layout compute resources.");
		}

		for (uint32 layoutIndex = 0; layoutIndex < pipeline.LayoutCount; ++layoutIndex)
		{
			for (uint32 index = 0; index < pipeline.Layouts[layoutIndex].BufferCount; ++index)
			{
				const BufferHandle handle = sets[layoutIndex]->Buffers[index];
				(void)command->AddBufferUse(
					{ handle, m_State->m_Buffers[handle.Slot].Native, BufferUseState(pipeline.Layouts[layoutIndex].Buffers[index].Type) });
				++m_State->m_Buffers[handle.Slot].RecordedUses;
			}
			trackGraphicsTextures(*command, list.Slot, { sets[layoutIndex]->SampledTextures, sets[layoutIndex]->TextureCount });
			trackGraphicsTextures(*command, list.Slot, { sets[layoutIndex]->StorageTextures, sets[layoutIndex]->StorageTextureCount },
				ETrackedResourceState::Storage);
			trackBoundSamplers(*command, setHandles[layoutIndex]);
			trackBoundViews(*command, setHandles[layoutIndex]);
			for (uint32 index = 0; index < sets[layoutIndex]->AccelStructCount; ++index)
			{
				const AccelStructHandle handle = sets[layoutIndex]->AccelStructs[index];
				trackAccelStruct(*command, handle, findAccelStruct(handle));
			}
		}
		trackDescriptorTableUse(*command, list.Slot, sampledTableHandle, sampledTable.Value());
		trackDescriptorTableUse(*command, list.Slot, samplerTableHandle, samplerTable.Value());
		trackDescriptorTableUse(*command, list.Slot, storageBufferTableHandle, storageBufferTable.Value());
		trackDescriptorTableUse(*command, list.Slot, storageTextureTableHandle, storageTextureTable.Value());
		trackDescriptorTableUse(*command, list.Slot, uniformBufferTableHandle, uniformBufferTable.Value());
		trackDescriptorTableUse(*command, list.Slot, readOnlyBufferTableHandle, readOnlyBufferTable.Value());
		trackAccelStruct(*command, accelHandle, accel);

		if (bIndirect)
		{
			(void)command->AddBufferUse({ argumentHandle, arguments->Native, ETrackedResourceState::Indirect });
			++arguments->RecordedUses;
		}
		trackDeviceHeapUses(*command, heapBindings.Value().Get());
		++pipeline.RecordedUses;
		Result<void> recorded = recordNative(
			[&]
			{
				Result<void> heapsRecorded = recordDescriptorHeapBindings(*command, false, heapBindings.Value().Get());
				if (!heapsRecorded)
				{
					return heapsRecorded;
				}

				Result<void> rangesRecorded = recordRangeBindingSets(*command, pipeline.Native, false, setHandles);
				if (!rangesRecorded)
				{
					return rangesRecorded;
				}
				if (pipeline.LayoutCount == 0)
				{
					return backend::RecordComputeDispatch(
						m_State->m_Backend.State, command->Native, pipeline.Native, {}, {}, groupsX, groupsY, groupsZ,
						bIndirect ? arguments->Native : nullptr, argumentOffset, pushConstants, nullptr, nullptr, nullptr,
						nullptr, nullptr, nullptr, nullptr);
				}

				return backend::RecordComputeDispatchMultiple(
					m_State->m_Backend.State, command->Native, pipeline.Native, { nativeSets, pipeline.LayoutCount }, groupsX, groupsY, groupsZ,
					bIndirect ? arguments->Native : nullptr, argumentOffset, pushConstants,
					sampledTable.Value() != nullptr ? sampledTable.Value()->Native : nullptr,
					samplerTable.Value() != nullptr ? samplerTable.Value()->Native : nullptr,
					storageBufferTable.Value() != nullptr ? storageBufferTable.Value()->Native : nullptr,
					storageTextureTable.Value() != nullptr ? storageTextureTable.Value()->Native : nullptr,
					uniformBufferTable.Value() != nullptr ? uniformBufferTable.Value()->Native : nullptr,
					readOnlyBufferTable.Value() != nullptr ? readOnlyBufferTable.Value()->Native : nullptr,
					accel != nullptr ? accel->Native : nullptr);
			});
		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::DispatchCompute(CommandListHandle list, const ComputeBindings& dispatchBindings, const ComputeDispatch& dispatch)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		PipelineSlot* const pipeline = findPipeline(dispatchBindings.Pipeline);
		ASSERT(command != nullptr && pipeline != nullptr, "RHI operations require valid handles for this device.");

		return dispatchComputeMultiple(list, *pipeline, dispatchBindings.Pipeline, dispatchBindings.Resources.Sets,
			dispatch.PushConstants, dispatch.GroupsX, dispatch.GroupsY,
			dispatch.GroupsZ,
			{}, 0, {}, {}, {}, {}, {}, {}, {},
			dispatchBindings.Resources.DeviceHeaps);
	}

	[[nodiscard]] Result<void> Device::DispatchMesh(CommandListHandle list, const GraphicsState& dispatchState, const MeshDispatch& dispatch)
	{
		CallScope call(*this, true);
		ASSERT(findCommand(list) != nullptr, "RHI operations require valid handles for this device.");

		if (!m_State->m_Backend.Capabilities.Features.bMeshShader)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.MeshShader", "Mesh shaders are unavailable on this device.");
		}

		const GraphicsPipelineSlot* pipeline = findGraphicsPipeline(dispatchState.Pipeline);
		ASSERT(pipeline != nullptr, "RHI operations require valid handles for this device.");
		const GpuCapabilities& capabilities = m_State->m_Backend.Capabilities;
		const uint32* maximum = pipeline->bTaskShader ? capabilities.Limits.MaximumTaskGroups : capabilities.Limits.MaximumMeshGroups;
		const uint32 total = pipeline->bTaskShader ? capabilities.Limits.MaximumTaskGroupTotalCount : capabilities.Limits.MaximumMeshGroupTotalCount;
		ASSERT(meshGroupsFit(dispatch.GroupsX, dispatch.GroupsY, dispatch.GroupsZ, maximum, total), "Mesh dispatch group counts must fit the device limit.");
		ASSERT(!dispatchState.VertexBuffer.IsValid() &&
			!dispatchState.IndexBuffer.IsValid() &&
			dispatchState.VertexBufferOffsetBytes == 0 &&
			dispatchState.IndexBufferOffsetBytes == 0 &&
			dispatchState.ExtraVertexBuffers.IsEmpty(), "Mesh dispatch does not use vertex or index streams.");
		DrawArguments draw;
		draw.VertexCount = dispatch.GroupsX;
		draw.InstanceCount = dispatch.GroupsY;
		draw.FirstVertex = dispatch.GroupsZ;
		draw.PushConstants = dispatch.PushConstants;
		draw.StencilReference = dispatch.StencilReference;

		return drawInternal(list, dispatchState, draw, true);
	}
}
