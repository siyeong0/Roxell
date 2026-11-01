#include "DeviceState.h"

#include <Assertion.h>

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::validatePreparedRayLocalBindings(const ShaderTableSlot& table, const PreparedRecordingPhase& phase)
	{
		for (rhi::BindingSetHandle const handle : table.TypedLocalSets)
		{
			const BindingSetSlot* set = findBindingSet(handle);
			ASSERT(set != nullptr, "Prepared command arguments must match their declared resources, ranges, and execution order.");
			rhi::BindingLayoutSlot const& layout = m_State->m_BindingLayouts[set->Layout.Slot];
			for (uint32 index = 0; index < set->BufferCount; ++index)
			{
				rhi::BufferSlot const& buffer = m_State->m_Buffers[set->Buffers[index].Slot];
				const BufferRange range = resolveBufferRange(handle, index, buffer.ByteCount);
				const EGraphicsBufferBindingType type = layout.GraphicsBuffer(index).Type;
				const EBufferState state = type == EGraphicsBufferBindingType::UniformBuffer ? EBufferState::Uniform
					: type == EGraphicsBufferBindingType::ReadOnlyBuffer ? EBufferState::ReadOnlyStorage
					: EBufferState::Storage;
				ASSERT(preparedBufferContains(phase, set->Buffers[index], state, range.ByteOffset, range.ByteCount),
					"Resource access, aliases, and dependencies must match the prepared command declaration.");
			}
			for (uint32 index = 0; index < set->AccelStructCount; ++index)
			{
				ASSERT(preparedAccelContains(phase, set->AccelStructs[index], EAccelStructState::ShaderRead),
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
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::realizeRayShaderTable(ShaderTableSlot& table, const RayTracingPipelineSlot& pipeline)
	{
		using R = Result<void>;
		if (table.PreparedTable)
		{
			return R::Success();
		}

		struct RecordScratch final
		{
			RecordScratch() noexcept {}

			NativeFixedDescriptor Descriptors[MAXIMUM_FIXED_BINDING_LAYOUTS][32]{};
			NativeFixedBindingSet Sets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		};

		Array<RecordScratch> scratch(m_State->m_Resource);
		const uint32 count = 1 + table.MissShaderCount + table.HitGroupCount + table.CallableShaderCount;
		if (!table.TypedLocalSets.IsEmpty() && !scratch.Resize(count))
		{
			return R::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not capture typed native shader records.");
		}
		NativeBindingSet locals[97]{};
		for (uint32 index = 0; index < count; ++index)
		{
			const uint32 bytes = table.LocalDataOffsets[index + 1] - table.LocalDataOffsets[index];
			if (bytes != 0)
			{
				locals[index].LocalData = { table.LocalData.Data() + table.LocalDataOffsets[index], bytes };
			}
			const uint32 firstSet = table.TypedLocalSetOffsets[index];
			const uint32 setCount = table.TypedLocalSetOffsets[index + 1] - firstSet;
			if (setCount != 0)
			{
				PreparedBinding binding;
				binding.SetCount = setCount;
				for (uint32 set = 0; set < setCount; ++set)
				{
					binding.Sets[set] = table.TypedLocalSets[firstSet + set];
				}
				fillPreparedNativeSets(binding, scratch[index].Descriptors, scratch[index].Sets);
				locals[index].FixedSets = { scratch[index].Sets, setCount };
			}
		}
		const NativeShaderTableDesc native{ table.RayGenerationShaderIndex,
										   {table.MissShaderIndices, table.MissShaderCount},
										   {table.HitGroupIndices, table.HitGroupCount},
										   {table.CallableShaderIndices, table.CallableShaderCount},
										   pipeline.GeneralShaderCount,
										   locals[0],
										   {locals + 1, table.MissShaderCount},
										   {locals + 1 + table.MissShaderCount, table.HitGroupCount},
										   {locals + 1 + table.MissShaderCount + table.HitGroupCount, table.CallableShaderCount} };
		Result<SharedPtr<PreparedShaderTable>> created = backend::CreatePreparedShaderTable(m_State->m_Backend.State, pipeline.Native, native);
		if (!created)
		{
			return R::Failure(created.GetError());
		}

		table.PreparedTable = Move(created).TakeValue();

		return R::Success();
	}

	[[nodiscard]] Result<uint32> Device::capturePreparedRayBinding(
		CommandSlot& command,
		const PreparedRecordingPhase& phase,
		const PreparedBinding& binding,
		Span<const NativeFixedBindingSet> sets)
	{
		rhi::ShaderTableSlot& table = m_State->m_ShaderTables[binding.ShaderTable.Slot];
		rhi::RayTracingPipelineSlot const& pipeline = m_State->m_RayTracingPipelines[table.Pipeline.Slot];
		const bool bCached = static_cast<bool>(table.PreparedTable);
		if (!bCached)
		{
			Result<void> created = realizeRayShaderTable(table, pipeline);
			if (!created)
			{
				return Result<uint32>::Failure(created.GetError());
			}
			++command.PreparedShaderTableCreations;
		}

		Result<uint32> captured = backend::PrepareRayBinding(m_State->m_Backend.State, command.Native, binding.NativePipeline, table.PreparedTable, sets, phase.NativeTextures.View());
		if (captured && bCached)
		{
			++command.PreparedShaderTableReuses;
		}

		return captured;
	}

	[[nodiscard]] Result<void> Device::DispatchRays(CommandListHandle handle, PreparedBindingRef ref, const RayDispatch& dispatch)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(!(phase == nullptr || ref.List != handle || ref.Phase != preparedList(command)->ActivePhase || ref.Slot >= phase->Bindings.Size() || command->bRendering || command->Queue == EQueue::Copy),
			"Prepared recording requires valid admitted input and capacity.");
		rhi::PreparedBinding const& binding = phase->Bindings[ref.Slot];
		const GpuCapabilities& caps = m_State->m_Backend.Capabilities;
		ASSERT(!(!binding.ShaderTable.IsValid() ||
				dispatch.PushConstants.Size() != binding.PushConstantBytes ||
				dispatch.Width == 0 ||
				dispatch.Height == 0 ||
				dispatch.Depth == 0 ||
				dispatch.Width > caps.Limits.MaximumRayDispatchDimensions[0] ||
				dispatch.Height > caps.Limits.MaximumRayDispatchDimensions[1] ||
				dispatch.Depth > caps.Limits.MaximumRayDispatchDimensions[2]),
			"Prepared recording requires valid admitted input and capacity.");
		const uint64 plane = uint64(dispatch.Width) * dispatch.Height;
		ASSERT(!(plane > caps.Limits.MaximumRayDispatchInvocations || dispatch.Depth > caps.Limits.MaximumRayDispatchInvocations / plane),
			"Prepared recording requires valid admitted input and capacity.");
		ASSERT(!(phase->Used.RayDispatches >= phase->Capacity.RayDispatches), "Prepared recording requires valid admitted input and capacity.");
		Result<void> result = recordNative(
			[&]
			{
				return backend::RecordPreparedRayDispatch(m_State->m_Backend.State, command->Native, binding.NativePipeline,
					binding.NativeIndex, dispatch);
			});
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}
		++phase->Used.RayDispatches;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::dispatchTypedRays(
		CommandListHandle list,
		CommandSlot& command,
		ShaderTableSlot& table,
		RayTracingPipelineSlot& pipeline,
		const RayBindings& dispatchBindings,
		const RayDispatch& dispatch)
	{
		using R = Result<void>;
		const GpuCapabilities& caps = m_State->m_Backend.Capabilities;
		ASSERT(!(command.Queue == EQueue::Copy ||
				command.bRendering ||
				dispatch.Width == 0 ||
				dispatch.Height == 0 ||
				dispatch.Depth == 0 ||
				dispatch.Width > caps.Limits.MaximumRayDispatchDimensions[0] ||
				dispatch.Height > caps.Limits.MaximumRayDispatchDimensions[1] ||
				dispatch.Depth > caps.Limits.MaximumRayDispatchDimensions[2] ||
				dispatch.PushConstants.Size() != pipeline.PushConstantBytes ||
				dispatchBindings.Resources.Sets.Size() != pipeline.LayoutCount), "The typed ray dispatch or its resource access is invalid.");
		const uint64 plane = uint64(dispatch.Width) * dispatch.Height;
		ASSERT(!(plane > caps.Limits.MaximumRayDispatchInvocations || dispatch.Depth > caps.Limits.MaximumRayDispatchInvocations / plane), "The typed ray dispatch or its resource access is invalid.");
		if constexpr (backend::HAS_RECORD_IMMUTABLE_RAY_DISPATCH == false)
		{
			poisonCommandList(list);

			return R::Failure(EErrorCode::Unsupported, "Rhi.RayDispatch", "Immutable typed ray dispatch is unavailable.");
		}
		BindingSetHandle handles[392]{};
		uint32 setCount = pipeline.LayoutCount;
		for (uint32 index = 0; index < pipeline.LayoutCount; ++index)
		{
			handles[index] = dispatchBindings.Resources.Sets[index];
			const BindingSetSlot* set = findBindingSet(handles[index]);
			ASSERT(set != nullptr, "RHI operations require valid handles for this device.");
			ASSERT(set->Layout == pipeline.Layouts[index].Handle, "The typed ray dispatch or its resource access is invalid.");
		}
		for (rhi::BindingSetHandle const handle : table.TypedLocalSets)
		{
			ASSERT(setCount < 392, "The bounded shader table and globals fit the access capture.");
			handles[setCount++] = handle;
		}
		uint32 bufferCount = 0;
		uint32 textureCount = 0;
		uint32 samplerCount = 0;
		uint32 viewCount = 0;
		uint32 accelCount = 0;
		for (uint32 setIndex = 0; setIndex < setCount; ++setIndex)
		{
			const BindingSetSlot* set = findBindingSet(handles[setIndex]);
			ASSERT(set != nullptr, "Immutable local records retain their binding sets.");
			rhi::BindingLayoutSlot const& layout = m_State->m_BindingLayouts[set->Layout.Slot];
			for (uint32 index = 0; index < set->BufferCount; ++index)
			{
				const BufferHandle handle = set->Buffers[index];
				rhi::BufferSlot const& buffer = m_State->m_Buffers[handle.Slot];
				const GraphicsBufferBindingDesc& declared = layout.GraphicsBuffer(index);
				const BufferRange range = resolveBufferRange(handles[setIndex], index, buffer.ByteCount);
				ASSERT(!(buffer.bMapped ||
						!HasUsage(buffer.Usage, declared.Type == EGraphicsBufferBindingType::UniformBuffer ? EBufferUsage::Uniform : EBufferUsage::Storage) ||
						(declared.Type == EGraphicsBufferBindingType::UniformBuffer && range.ByteCount > 65536)), "The typed ray dispatch or its resource access is invalid.");
				for (uint32 previousSet = 0; previousSet <= setIndex; ++previousSet)
				{
					const BindingSetSlot* oldSet = findBindingSet(handles[previousSet]);
					rhi::BindingLayoutSlot const& oldLayout = m_State->m_BindingLayouts[oldSet->Layout.Slot];
					for (uint32 previous = 0; previous < (previousSet == setIndex ? index : oldSet->BufferCount); ++previous)
					{
						const EGraphicsBufferBindingType oldType = oldLayout.GraphicsBuffer(previous).Type;
						ASSERT(!(oldSet->Buffers[previous] == handle &&
								(oldType != declared.Type ||
								 !canShareBuffer(declared.Type != EGraphicsBufferBindingType::StorageBuffer, oldType != EGraphicsBufferBindingType::StorageBuffer, range, resolveBufferRange(handles[previousSet], previous, buffer.ByteCount)))), "The typed ray dispatch or its resource access is invalid.");
					}
				}
			}
			RayTracingPipelineSlot::GlobalLayout textureLayout{};
			textureLayout.SampledTextureCount = set->TextureCount;
			textureLayout.StorageTextureCount = set->StorageTextureCount;
			textureLayout.TextureCount = set->TextureCount + set->StorageTextureCount;
			for (uint32 index = 0; index < textureLayout.TextureCount; ++index)
			{
				const bool bStorage = index >= set->TextureCount;
				textureLayout.Textures[index].Type = bStorage ? EComputeTextureBindingType::StorageTexture : EComputeTextureBindingType::SampledTexture;
				const uint32 at = bStorage ? index - set->TextureCount : index;
				const TextureHandle texture = bStorage ? set->StorageTextures[at] : set->SampledTextures[at];
				const TextureViewHandle view = bStorage ? set->StorageViews[at] : set->SampledViews[at];
				for (uint32 previousSet = 0; previousSet < setIndex; ++previousSet)
				{
					const BindingSetSlot* oldSet = findBindingSet(handles[previousSet]);
					for (uint32 previous = 0; previous < oldSet->TextureCount + oldSet->StorageTextureCount; ++previous)
					{
						const bool bOldStorage = previous >= oldSet->TextureCount;
						const uint32 oldAt = bOldStorage ? previous - oldSet->TextureCount : previous;
						const TextureHandle oldTexture = bOldStorage ? oldSet->StorageTextures[oldAt] : oldSet->SampledTextures[oldAt];
						const TextureViewHandle oldView = bOldStorage ? oldSet->StorageViews[oldAt] : oldSet->SampledViews[oldAt];
						ASSERT(!(texture == oldTexture && (bStorage || bOldStorage) && !textureViewsDisjoint(view, oldView)), "The typed ray dispatch or its resource access is invalid.");
					}
				}
			}
			NativeComputeTexture validatedTextures[8]{};
			Result<void> validTextures = validateComputeTextures(
				textureLayout,
				{ set->SampledTextures, set->TextureCount },
				{ set->StorageTextures, set->StorageTextureCount },
				command,
				list.Slot,
				handles[setIndex],
				validatedTextures);
			if (!validTextures)
			{
				ASSERT(validTextures.GetError().Code != EErrorCode::InvalidArgument, "The typed ray dispatch or its resource access is invalid.");
				poisonCommandList(list);

				return validTextures;
			}
			for (uint32 index = 0; index < set->AccelStructCount; ++index)
			{
				const AccelStructSlot* accel = findAccelStruct(set->AccelStructs[index]);
				ASSERT(accel != nullptr && accel->bBuilt && accel->Type == EAccelStructType::TopLevel, "The typed ray dispatch or its resource access is invalid.");
			}
			bufferCount += set->BufferCount;
			textureCount += textureLayout.TextureCount;
			samplerCount += set->SamplerCount;
			viewCount += textureLayout.TextureCount;
			accelCount += set->AccelStructCount;
		}
		if (!command.RayPipelineUses.Reserve(command.RayPipelineUses.Size() + 1) ||
			!command.BufferUses.Reserve(command.BufferUses.Size() + bufferCount + accelCount) ||
			!command.TextureUses.Reserve(command.TextureUses.Size() + textureCount) ||
			!command.SamplerUses.Reserve(command.SamplerUses.Size() + samplerCount) ||
			!command.TextureViewUses.Reserve(command.TextureViewUses.Size() + viewCount) ||
			!command.AccelUses.Reserve(command.AccelUses.Size() + accelCount))
		{
			poisonCommandList(list);

			return R::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain typed ray dispatch dependencies.");
		}
		PreparedBinding globalBinding;
		globalBinding.SetCount = pipeline.LayoutCount;
		for (uint32 index = 0; index < pipeline.LayoutCount; ++index)
		{
			globalBinding.Sets[index] = handles[index];
		}
		NativeFixedDescriptor fixedDescriptors[MAXIMUM_FIXED_BINDING_LAYOUTS][32]{};
		Result<void> globalUses = prepareOrdinaryBoundTableUses(command, { handles, setCount });
		if (!globalUses || !command.DescriptorTableUses.Reserve(command.DescriptorTableUses.Size() + pipeline.LayoutCount * 8))
		{
			ASSERT(globalUses || globalUses.GetError().Code != EErrorCode::InvalidArgument, "The typed ray dispatch or its resource access is invalid.");
			poisonCommandList(list);

			return globalUses ? R::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain global ray tables.") : globalUses;
		}
		NativeFixedBindingSet nativeSets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		fillPreparedNativeSets(globalBinding, fixedDescriptors, nativeSets);
		const bool bCached = static_cast<bool>(table.PreparedTable);
		Result<void> realized = realizeRayShaderTable(table, pipeline);
		if (!realized)
		{
			poisonCommandList(list);

			return realized;
		}
		if (!bCached)
		{
			++command.PreparedShaderTableCreations;
		}
		for (uint32 index = 0; index < pipeline.LayoutCount; ++index)
		{
			rhi::BindingSetSlot const& set = m_State->m_BindingSets[handles[index].Slot];
			if (set.Tables)
			{
				for (rhi::BoundDescriptorTableRange const& row : set.Tables->Ranges)
				{
					trackBoundTableUses(command, row, *set.Tables);
				}
			}
		}
		if (bCached)
		{
			++command.PreparedShaderTableReuses;
		}
		(void)command.RayPipelineUses.Add({ table.Pipeline, pipeline.Native });
		++pipeline.RecordedUses;
		for (uint32 setIndex = 0; setIndex < setCount; ++setIndex)
		{
			const BindingSetSlot* set = findBindingSet(handles[setIndex]);
			rhi::BindingLayoutSlot const& layout = m_State->m_BindingLayouts[set->Layout.Slot];
			for (uint32 index = 0; index < set->BufferCount; ++index)
			{
				const BufferHandle handle = set->Buffers[index];
				(void)command.AddBufferUse({ handle, m_State->m_Buffers[handle.Slot].Native, BufferUseState(layout.GraphicsBuffer(index).Type) });
				++m_State->m_Buffers[handle.Slot].RecordedUses;
			}
			trackGraphicsTextures(command, list.Slot, { set->SampledTextures, set->TextureCount });
			trackGraphicsTextures(command, list.Slot, { set->StorageTextures, set->StorageTextureCount }, ETrackedResourceState::Storage);
			trackBoundSamplers(command, handles[setIndex]);
			trackBoundViews(command, handles[setIndex]);
			for (uint32 index = 0; index < set->AccelStructCount; ++index)
			{
				trackAccelStruct(command, set->AccelStructs[index], findAccelStruct(set->AccelStructs[index]));
			}
		}
		const RayDispatch nativeDispatch{ dispatch.Width, dispatch.Height, dispatch.Depth,
												 dispatch.PushConstants };
		Result<void> recorded = backend::RecordImmutableRayDispatch(m_State->m_Backend.State, command.Native, pipeline.Native,
			table.PreparedTable,
			{ nativeSets, pipeline.LayoutCount }, nativeDispatch);
		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return R::Success();
	}
}
