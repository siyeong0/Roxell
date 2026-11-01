#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	void DestroyPreparedShaderTable(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<PreparedRayTableOwner*>(native));
	}

	[[nodiscard]] Result<SharedPtr<PreparedShaderTable>> CreatePreparedShaderTable(State* opaque, void* pipelineNative, const NativeShaderTableDesc& table)
	{
		State& state = *opaque;
		const RayTracingPipeline& pipeline = *static_cast<const RayTracingPipeline*>(pipelineNative);
		using R = Result<SharedPtr<PreparedShaderTable>>;
		vulkan::PreparedRayTableOwner* nativeTable = CreateObject<PreparedRayTableOwner>(state.Resource);
		if (nativeTable == nullptr)
		{
			return R::Failure(Failure("Rhi.Allocation", "Could not allocate a native shader table owner."));
		}

		SharedPtr<PreparedShaderTable> owner;
		if (!MakeSharedInto(state.Resource, &owner, opaque, nativeTable))
		{
			DestroyPreparedShaderTable(opaque, nativeTable);

			return R::Failure(Failure("Rhi.Allocation", "Could not retain a native shader table owner."));
		}

		const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& properties = state.RayTracingProperties;
		const uint64 handleBytes = properties.shaderGroupHandleSize;
		const uint64 handleAlignment = Max(properties.shaderGroupHandleAlignment, pipeline.LocalDataAlignmentBytes);
		const uint64 baseAlignment = Max(properties.shaderGroupBaseAlignment, pipeline.LocalDataAlignmentBytes);
		uint64 localBytes = table.RayGenerationLocal.LocalData.Size();
		for (const rhi::NativeBindingSet& local : table.MissLocals)
		{
			localBytes = Max(localBytes, uint64(local.LocalData.Size()));
		}
		for (const rhi::NativeBindingSet& local : table.HitLocals)
		{
			localBytes = Max(localBytes, uint64(local.LocalData.Size()));
		}
		for (const rhi::NativeBindingSet& local : table.CallableLocals)
		{
			localBytes = Max(localBytes, uint64(local.LocalData.Size()));
		}
		const auto align = [](uint64 value, uint64 alignment) noexcept { return (value + alignment - 1) & ~(alignment - 1); };
		const uint64 stride = align(handleBytes + localBytes, handleAlignment);
		const uint64 missOffset = align(stride, baseAlignment);
		const uint64 hitOffset = align(missOffset + table.MissShaderIndices.Size() * stride, baseAlignment);
		const uint64 callableOffset = align(hitOffset + table.HitGroupIndices.Size() * stride, baseAlignment);
		const uint64 bytes = align(callableOffset + table.CallableShaderIndices.Size() * stride, baseAlignment);
		if (stride > properties.maxShaderGroupStride)
		{
			return R::Failure(EErrorCode::Unsupported, "Rhi.PreparedRay", "Shader table stride exceeds the device limit.");
		}
		nativeTable->Device = &state;
		Result<Buffer*> created = NewBuffer(state, bytes + baseAlignment, true, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
		if (!created)
		{
			return R::Failure(created.GetError());
		}
		vulkan::Buffer* buffer = created.Value();
		nativeTable->Resource = buffer;
		const VkDeviceAddress address = PreparedBufferAddress(state, *buffer);
		if (address == 0)
		{
			return R::Failure(Failure("Rhi.PreparedRay", "Shader table device address is unavailable."));
		}
		const uint64_t start = align(address, baseAlignment) - address;
		void* mapped = nullptr;
		if (state.F.MapMemory(state.Device, buffer->Memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
		{
			return R::Failure(Failure("Rhi.PreparedRay", "Could not map a prepared shader table."));
		}
		const auto copy = [&](uint64 offset, uint32 group, const NativeBindingSet& local) noexcept
			{
				Byte* record = static_cast<Byte*>(mapped) + start + offset;
				CopyBytes(record, pipeline.GroupHandles.Data() + uint64(group) * handleBytes,
					static_cast<usize>(handleBytes));
				if (!local.LocalData.IsEmpty())
				{
					CopyBytes(record + handleBytes, local.LocalData.Data(), local.LocalData.Size());
				}
			};
		SetBytes(static_cast<Byte*>(mapped) + start, 0, static_cast<usize>(bytes));
		copy(0, table.RayGenerationShaderIndex, table.RayGenerationLocal);
		for (uint32 index = 0; index < table.MissShaderIndices.Size(); ++index)
		{
			copy(missOffset + index * stride, table.MissShaderIndices[index], table.MissLocals[index]);
		}
		for (uint32 index = 0; index < table.HitGroupIndices.Size(); ++index)
		{
			copy(hitOffset + index * stride, table.GeneralShaderCount + table.HitGroupIndices[index], table.HitLocals[index]);
		}
		for (uint32 index = 0; index < table.CallableShaderIndices.Size(); ++index)
		{
			copy(callableOffset + index * stride, table.CallableShaderIndices[index], table.CallableLocals[index]);
		}
		VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.memory = buffer->Memory;
		range.size = VK_WHOLE_SIZE;
		const bool bFlushed = buffer->Coherent || state.F.FlushMappedMemoryRanges(state.Device, 1, &range) == VK_SUCCESS;
		state.F.UnmapMemory(state.Device, buffer->Memory);
		if (!bFlushed)
		{
			return R::Failure(Failure("Rhi.PreparedRay", "Could not flush a prepared shader table."));
		}
		const uint64_t base = address + start;
		nativeTable->Rays[0] = { base, stride, stride };
		if (!table.MissShaderIndices.IsEmpty())
		{
			nativeTable->Rays[1] = { base + missOffset, stride, table.MissShaderIndices.Size() * stride };
		}
		if (!table.HitGroupIndices.IsEmpty())
		{
			nativeTable->Rays[2] = { base + hitOffset, stride, table.HitGroupIndices.Size() * stride };
		}
		if (!table.CallableShaderIndices.IsEmpty())
		{
			nativeTable->Rays[3] = { base + callableOffset, stride, table.CallableShaderIndices.Size() * stride };
		}

		return R::Success(Move(owner));
	}

	[[nodiscard]] Result<uint32> PrepareRayBinding(
		State* opaque,
		void* native,
		void* pipelineNative,
		const SharedPtr<PreparedShaderTable>& table,
		Span<const NativeFixedBindingSet> sets,
		Span<const NativePreparedTextureAccess> intents)
	{
		Pending& pending = *static_cast<Pending*>(native);
		RayTracingPipeline& pipeline = *static_cast<RayTracingPipeline*>(pipelineNative);
		using R = Result<uint32>;
		MemoryResource& memory = opaque->Resource;
		SharedPtr<NativeBindingProgram> owner;
		if (!MakeSharedInto(memory, &owner))
		{
			return Result<uint32>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingProgram", "Could not retain a prepared binding program.");
		}
		owner->Resource = &memory;
		owner->Data = CreateObject<Pending::PreparedBinding>(memory, memory);
		if (!owner->Data)
		{
			return Result<uint32>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingProgram", "Could not retain a prepared binding program.");
		}
		Pending::PreparedBinding& binding = *static_cast<Pending::PreparedBinding*>(owner->Data);
		if (!sets.IsEmpty())
		{
			Result<void> descriptors = WriteFixedBindingSets(opaque, native, pipelineNative, false, sets, &binding, intents, &pipeline);
			if (!descriptors)
			{
				return R::Failure(descriptors.GetError());
			}
		}

		Result<void> storage = CapturePreparedStorage(binding, pipeline.Fixed, sets);
		if (!storage)
		{
			return R::Failure(storage.GetError());
		}

		ASSERT(table, "Prepared bindings capture a fully realized immutable shader table.");
		const PreparedRayTableOwner& nativeTable = *static_cast<const PreparedRayTableOwner*>(table->GetNative());
		binding.RayTable = table;
		CopyBytes(binding.Rays, nativeTable.Rays, sizeof(binding.Rays));

		const uint32 index = static_cast<uint32>(pending.PreparedBindings.Size());
		const bool bAdded = pending.PreparedBindings.Add(owner);
		ASSERT(bAdded, "Prepared ray bindings have reserved storage.");

		return R::Success(index);
	}

	[[nodiscard]] Result<void> RecordPreparedRayDispatch(State* opaque, void* native, void* pipelineNative, uint32 index, const RayDispatch& dispatch)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		const RayTracingPipeline& pipeline = *static_cast<const RayTracingPipeline*>(pipelineNative);
		const Pending::PreparedBinding& binding = pending.BindingAt(index);
		for (uint32 buffer = 0; buffer < binding.BufferCount; ++buffer)
		{
			(void)RecordShaderBufferBarrier(opaque, native, binding.StorageBuffers[buffer]);
		}
		for (uint32 image = 0; image < binding.TextureCount; ++image)
		{
			rhi::TextureViewDesc const& extent = binding.StorageRanges[image];
			EmitTextureBarrier(state, pending, *binding.StorageTextures[image], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
				extent.BaseMipLevel, extent.MipLevelCount, extent.BaseArrayLayer, extent.ArrayLayerCount);
		}
		for (uint32 set = 0; set < binding.Count; ++set)
		{
			state.F.CmdBindDescriptorSets(pending.Commands, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.Layout,
				pipeline.Fixed.Spaces[set], 1, &binding.Sets[set], 0, nullptr);
		}
		state.F.CmdBindPipeline(pending.Commands, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.Handle);
		if (pipeline.PushConstantBytes != 0)
		{
			constexpr VkShaderStageFlags stages = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
				VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
				VK_SHADER_STAGE_INTERSECTION_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR;
			state.F.CmdPushConstants(pending.Commands, pipeline.Layout, stages, 0, pipeline.PushConstantBytes,
				dispatch.PushConstants.Data());
		}
		state.F.CmdTraceRays(pending.Commands, &binding.Rays[0], &binding.Rays[1], &binding.Rays[2], &binding.Rays[3], dispatch.Width,
			dispatch.Height, dispatch.Depth);
		pending.LastComputePipeline = nullptr;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordImmutableRayDispatch(
		State* opaque,
		void* native,
		void* pipelineNative,
		const SharedPtr<PreparedShaderTable>& table,
		Span<const NativeFixedBindingSet> globals,
		const RayDispatch& dispatch)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		RayTracingPipeline& pipeline = *static_cast<RayTracingPipeline*>(pipelineNative);
		if (!pending.PreparedBindings.Reserve(pending.PreparedBindings.Size() + 1))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain an immutable ray binding."));
		}
		Result<uint32> binding = PrepareRayBinding(opaque, native, pipelineNative, table, {}, {});
		if (!binding)
		{
			return Result<void>::Failure(binding.GetError());
		}
		if (!globals.IsEmpty())
		{
			Result<void> descriptors = WriteFixedBindingSets(opaque, native, pipelineNative, false, globals, nullptr, {}, &pipeline);
			if (!descriptors)
			{
				return descriptors;
			}
		}
		if (pending.bAutomaticBarriers)
		{
			VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_MEMORY_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
		}
		return RecordPreparedRayDispatch(opaque, native, pipelineNative, binding.Value(), dispatch);
	}
}
