#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<AccelStructBuildSizes> GetAccelStructBuildSizes(
		const State* opaque,
		const AccelStructBuildDesc& desc,
		Span<const NativeAccelTriangleGeometry> triangles,
		Span<const NativeAccelAabbGeometry> aabbs)
	{
		const State& state = *opaque;

		if (!state.bAccelerationStructure || state.F.GetAccelerationStructureBuildSizes == nullptr || state.F.GetBufferDeviceAddress == nullptr || state.AccelScratchAlignment == 0)
		{
			return Result<AccelStructBuildSizes>::Failure(EErrorCode::Unsupported, "Rhi.VulkanAccelStruct", "The device has no acceleration structure build size query.");
		}

		Array<VkAccelerationStructureGeometryKHR> geometries(state.Resource);
		Array<uint32> primitiveCounts(state.Resource);
		Array<VkAccelerationStructureTrianglesOpacityMicromapEXT> micromaps(state.Resource);
		Array<VkMicromapUsageEXT> micromapCounts(state.Resource);
		usize usageCount = 0;

		for (const NativeAccelTriangleGeometry& triangle : triangles)
		{
			usageCount += triangle.OpacityMicromapUsageCounts.Size();
		}

		if (!micromaps.ResizeUninitialized(triangles.Size()) || !micromapCounts.Reserve(usageCount))
		{
			return Result<AccelStructBuildSizes>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan micromap build geometry."));
		}

		usize triangleIndex = 0;

		for (const NativeAccelTriangleGeometry& triangle : triangles)
		{
			VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			geometry.flags = triangle.bOpaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
			VkAccelerationStructureGeometryTrianglesDataKHR& vertices = geometry.geometry.triangles;
			vertices.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			vertices.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			addressInfo.buffer = static_cast<const Buffer*>(triangle.VertexBuffer)->Handle;
			vertices.vertexData.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &addressInfo) + triangle.VertexOffsetBytes;
			vertices.vertexStride = triangle.VertexStrideBytes;
			vertices.maxVertex = triangle.VertexCount - 1;
			vertices.indexType = VK_INDEX_TYPE_NONE_KHR;

			if (triangle.IndexBuffer != nullptr)
			{
				vertices.indexType = triangle.IndexFormat == EFormat::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
				addressInfo.buffer = static_cast<const Buffer*>(triangle.IndexBuffer)->Handle;
				vertices.indexData.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &addressInfo) + triangle.IndexOffsetBytes;
			}

			if (triangle.OpacityMicromap != nullptr)
			{
				const usize firstCount = micromapCounts.Size();

				for (const OpacityMicromapUsageCount& count : triangle.OpacityMicromapUsageCounts)
				{
					(void)micromapCounts.Add({ count.Count, count.SubdivisionLevel, static_cast<uint32>(count.Format) });
				}

				VkAccelerationStructureTrianglesOpacityMicromapEXT& omm = micromaps[triangleIndex];
				omm = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT };
				omm.indexType = VK_INDEX_TYPE_NONE_KHR;

				if (triangle.OpacityMicromapIndexBuffer != nullptr)
				{
					omm.indexType = triangle.OpacityMicromapIndexFormat == EFormat::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
					addressInfo.buffer = static_cast<const Buffer*>(triangle.OpacityMicromapIndexBuffer)->Handle;
					omm.indexBuffer.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &addressInfo) + triangle.OpacityMicromapIndexOffsetBytes;
					omm.indexStride = triangle.OpacityMicromapIndexStrideBytes;
				}
				omm.baseTriangle = triangle.OpacityMicromapBaseTriangle;
				omm.usageCountsCount = static_cast<uint32>(triangle.OpacityMicromapUsageCounts.Size());
				omm.pUsageCounts = micromapCounts.Data() + firstCount;
				omm.micromap = static_cast<const NativeMicromap*>(triangle.OpacityMicromap)->Handle;
				vertices.pNext = &omm;
			}
			++triangleIndex;
			const uint32 primitives = triangle.IndexBuffer != nullptr ? triangle.IndexCount / 3 : triangle.VertexCount / 3;

			if (!geometries.Add(geometry) || !primitiveCounts.Add(primitives))
			{
				return Result<AccelStructBuildSizes>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan acceleration structure geometries."));
			}
		}

		for (const NativeAccelAabbGeometry& aabb : aabbs)
		{
			VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
			geometry.flags = aabb.bOpaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
			geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
			VkBufferDeviceAddressInfo address{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			address.buffer = static_cast<const Buffer*>(aabb.Buffer)->Handle;
			geometry.geometry.aabbs.data.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &address) + aabb.OffsetBytes;
			geometry.geometry.aabbs.stride = aabb.StrideBytes;

			if (!geometries.Add(geometry) || !primitiveCounts.Add(aabb.Count))
			{
				return Result<AccelStructBuildSizes>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan acceleration structure geometries."));
			}
		}

		if (desc.Type == EAccelStructType::TopLevel)
		{
			VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
			geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;

			if (!geometries.Add(geometry) || !primitiveCounts.Add(desc.InstanceCount))
			{
				return Result<AccelStructBuildSizes>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan instance geometry."));
			}
		}
		VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		build.type = desc.Type == EAccelStructType::BottomLevel ? VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		build.flags = desc.bAllowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0;

		if (desc.bAllowCompaction)
		{
			build.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
		}
		build.geometryCount = static_cast<uint32>(geometries.Size());
		build.pGeometries = geometries.Data();
		VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		state.F.GetAccelerationStructureBuildSizes(state.Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
			primitiveCounts.Data(), &sizes);

		if (sizes.accelerationStructureSize == 0 || sizes.buildScratchSize == 0)
		{
			return Result<AccelStructBuildSizes>::Failure(Failure("Rhi.VulkanAccelStruct", "The driver returned invalid acceleration structure build sizes."));
		}

		return Result<AccelStructBuildSizes>::Success({ sizes.accelerationStructureSize, sizes.buildScratchSize,
															 desc.bAllowUpdate ? sizes.updateScratchSize : 0, 256,
															 state.AccelScratchAlignment });
	}

	[[nodiscard]] Result<void> RecordSetAccelStructState(State* opaque, void* native, void*, EAccelStructState requested)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

		if (requested == EAccelStructState::BuildWrite)
		{
			barrier.dstAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		}
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr,
			0, nullptr);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> CreateAccelStruct(State* opaque, const AccelStructDesc& desc, void* nativeBuffer)
	{
		State& state = *opaque;

		if (!state.bAccelerationStructure || state.F.CreateAccelerationStructure == nullptr)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanAccelStruct", "The device has no acceleration structures.");
		}

		vulkan::NativeAccelStruct* const native = CreateObject<NativeAccelStruct>(state.Resource);

		if (native == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a Vulkan acceleration structure record."));
		}

		if (desc.Type == EAccelStructType::ClusterBottomLevel)
		{
			VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			addressInfo.buffer = static_cast<Buffer*>(nativeBuffer)->Handle;
			native->ClusterAddress = state.F.GetBufferDeviceAddress(state.Device, &addressInfo) + desc.ByteOffset;

			if (native->ClusterAddress == 0)
			{
				DestroyObject(state.Resource, native);

				return Result<void*>::Failure(Failure("Rhi.VulkanCluster", "Could not resolve cluster BLAS storage address."));
			}

			return Result<void*>::Success(native);
		}
		VkAccelerationStructureCreateInfoKHR create{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		create.buffer = static_cast<Buffer*>(nativeBuffer)->Handle;
		create.offset = desc.ByteOffset;
		create.size = desc.ByteCount;
		create.type = desc.Type == EAccelStructType::BottomLevel ? VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

		if (state.F.CreateAccelerationStructure(state.Device, &create, nullptr, &native->Handle) != VK_SUCCESS)
		{
			DestroyObject(state.Resource, native);

			return Result<void*>::Failure(Failure("Rhi.VulkanAccelStruct", "Could not create a Vulkan acceleration structure."));
		}

		return Result<void*>::Success(native);
	}

	void DestroyAccelStructNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		NativeAccelStruct* const accel = static_cast<NativeAccelStruct*>(native);

		if (accel->CompactionQuery != VK_NULL_HANDLE)
		{
			state.F.DestroyQueryPool(state.Device, accel->CompactionQuery, nullptr);
		}

		if (accel->Handle != VK_NULL_HANDLE)
		{
			state.F.DestroyAccelerationStructure(state.Device, accel->Handle, nullptr);
		}

		DestroyObject(state.Resource, accel);
	}

	[[nodiscard]] Result<void> DestroyAccelStruct(State* opaque, void* native)
	{
		State& state = *opaque;
		uint64 complete = 0;

		if (!ReadCompletedSerial(state, complete))
		{
			return Result<void>::Failure(Failure("Rhi.VulkanTimeline", "Could not inspect Vulkan completion."));
		}

		NativeAccelStruct* const accel = static_cast<NativeAccelStruct*>(native);

		if (accel->LastUse > complete)
		{
			if (!state.RetiredAccelStructs.Add({ accel, accel->LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track a retired Vulkan acceleration structure."));
			}
		}
		else
		{
			DestroyAccelStructNow(&state, accel);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBuildTopLevelAccelStructFromBuffer(
		State* opaque,
		void* nativeCommand,
		void* nativeAccel,
		const AccelStructBuildDesc& desc,
		void* nativeInstances,
		uint64 instanceOffset,
		void* nativeScratch,
		uint64 scratchOffset)
	{
		static_assert(sizeof(AccelGpuInstanceDesc) == sizeof(VkAccelerationStructureInstanceKHR));
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(nativeCommand);

		if (!state.bAccelerationStructure || !state.F.CmdBuildAccelerationStructures || pending.QueueKind == EQueue::Copy)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanAccelStruct", "This command list cannot build acceleration structures.");
		}
		VkBufferDeviceAddressInfo instanceInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		instanceInfo.buffer = static_cast<Buffer*>(nativeInstances)->Handle;
		const VkDeviceAddress instanceAddress = state.F.GetBufferDeviceAddress(state.Device, &instanceInfo) + instanceOffset;

		if (instanceAddress == 0 || instanceAddress % 16 != 0)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanAccelStruct", "The instance buffer address is invalid."));
		}
		VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		geometry.geometry.instances.arrayOfPointers = VK_FALSE;
		geometry.geometry.instances.data.deviceAddress = instanceAddress;
		VkBufferDeviceAddressInfo scratchInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		scratchInfo.buffer = static_cast<Buffer*>(nativeScratch)->Handle;
		VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		build.flags = desc.bAllowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0;
		build.mode = desc.bPerformUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build.dstAccelerationStructure = static_cast<NativeAccelStruct*>(nativeAccel)->Handle;

		if (desc.bPerformUpdate)
		{
			build.srcAccelerationStructure = build.dstAccelerationStructure;
		}
		build.geometryCount = 1;
		build.pGeometries = &geometry;
		build.scratchData.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &scratchInfo) + scratchOffset;
		VkAccelerationStructureBuildRangeInfoKHR range{};
		range.primitiveCount = desc.InstanceCount;
		const VkAccelerationStructureBuildRangeInfoKHR* rangePtr = &range;
		VkMemoryBarrier before{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1,
			&before, 0, nullptr, 0, nullptr);
		state.F.CmdBuildAccelerationStructures(pending.Commands, 1, &build, &rangePtr);
		VkMemoryBarrier after{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		after.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		after.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
			&after, 0, nullptr, 0, nullptr);

		return Result<void>::Success();
	}

	[[nodiscard]] uint64 GetAccelStructDeviceAddress(const State* opaque, const void* native) noexcept
	{
		const State& state = *opaque;
		const NativeAccelStruct* const accel = static_cast<const NativeAccelStruct*>(native);

		if (accel->ClusterAddress != 0)
		{
			return accel->ClusterAddress;
		}
		VkAccelerationStructureDeviceAddressInfoKHR address{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
		address.accelerationStructure = static_cast<const NativeAccelStruct*>(native)->Handle;

		return state.F.GetAccelerationStructureDeviceAddress(state.Device, &address);
	}

	void PropagateAccelStructLastUse(const void*, const void* parent, void* child) noexcept
	{
		const NativeAccelStruct* const source = static_cast<const NativeAccelStruct*>(parent);
		NativeAccelStruct* const target = static_cast<NativeAccelStruct*>(child);

		if (target->LastUse < source->LastUse)
		{
			target->LastUse = source->LastUse;
			target->LastQueue = source->LastQueue;
		}
	}

	void PropagateAccelStructLastUseToBuffer(const void*, const void* source, void* nativeBuffer) noexcept
	{
		const NativeAccelStruct* const accel = static_cast<const NativeAccelStruct*>(source);
		Buffer* const target = static_cast<Buffer*>(nativeBuffer);

		if (target->LastUse < accel->LastUse)
		{
			target->LastUse = accel->LastUse;
			target->LastQueue = accel->LastQueue;
		}
	}

	[[nodiscard]] Result<uint64> GetBottomLevelAccelStructCompactedSize(State* opaque, void* native)
	{
		State& state = *opaque;
		NativeAccelStruct& accel = *static_cast<NativeAccelStruct*>(native);
		const PFN_vkCmdWriteAccelerationStructuresPropertiesKHR write = reinterpret_cast<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(
			state.F.GetDeviceProcAddr(state.Device, "vkCmdWriteAccelerationStructuresPropertiesKHR"));

		if (!state.bAccelerationStructure || !write || state.bLost)
		{
			return Result<uint64>::Failure(Failure("Rhi.VulkanAccelStruct", "The device cannot query compacted acceleration structure size."));
		}

		if (accel.CompactionQuery == VK_NULL_HANDLE)
		{
			VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
			info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
			info.queryCount = 1;

			if (state.F.CreateQueryPool(state.Device, &info, nullptr, &accel.CompactionQuery) != VK_SUCCESS)
			{
				return Result<uint64>::Failure(Failure("Rhi.VulkanAccelStruct", "Could not create a compaction query pool."));
			}
		}

		if (accel.CompactionSerial == 0)
		{
			Result<Pending> started = Begin(state, EQueue::Graphics);

			if (!started)
			{
				return Result<uint64>::Failure(started.GetError());
			}
			Pending pending = Move(started).TakeValue();
			pending.QueueKind = EQueue::Graphics;
			state.F.CmdResetQueryPool(pending.Commands, accel.CompactionQuery, 0, 1);
			VkMemoryBarrier before{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
			before.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
			RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1,
				&before, 0, nullptr, 0, nullptr);
			write(pending.Commands, 1, &accel.Handle, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, accel.CompactionQuery, 0);
			Result<uint64> submitted = Execute(state, Move(pending), VK_NULL_HANDLE, VK_NULL_HANDLE, VK_PIPELINE_STAGE_TRANSFER_BIT, false, nullptr, nullptr);

			if (!submitted)
			{
				return Result<uint64>::Failure(submitted.GetError());
			}
			accel.CompactionSerial = submitted.Value();

			if (accel.LastUse < submitted.Value())
			{
				accel.LastUse = submitted.Value();
				accel.LastQueue = EQueue::Graphics;
			}
		}

		Result<void> waited = Wait(&state, EQueue::Graphics, accel.CompactionSerial, NumericLimits<uint32>::max());

		if (!waited)
		{
			return Result<uint64>::Failure(waited.GetError());
		}

		uint64 bytes = 0;

		if (state.F.GetQueryPoolResults(state.Device, accel.CompactionQuery, 0, 1, sizeof(bytes), &bytes, sizeof(bytes),
			VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS)
		{
			return Result<uint64>::Failure(Failure("Rhi.VulkanAccelStruct", "Could not read the compacted acceleration structure size."));
		}
		state.F.DestroyQueryPool(state.Device, accel.CompactionQuery, nullptr);
		accel.CompactionQuery = VK_NULL_HANDLE;
		accel.CompactionSerial = 0;

		return Result<uint64>::Success(bytes);
	}

	[[nodiscard]] Result<void> RecordCompactBottomLevelAccelStruct(State* opaque, void* nativeCommand, void* nativeSource, void* nativeTarget)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(nativeCommand);
		const PFN_vkCmdCopyAccelerationStructureKHR copy = reinterpret_cast<PFN_vkCmdCopyAccelerationStructureKHR>( state.F.GetDeviceProcAddr(state.Device, "vkCmdCopyAccelerationStructureKHR"));

		if (!state.bAccelerationStructure || !copy || pending.QueueKind == EQueue::Copy)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanAccelStruct", "This command list cannot compact acceleration structures."));
		}
		VkMemoryBarrier before{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		before.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1,
			&before, 0, nullptr, 0, nullptr);
		VkCopyAccelerationStructureInfoKHR info{ VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
		info.src = static_cast<NativeAccelStruct*>(nativeSource)->Handle;
		info.dst = static_cast<NativeAccelStruct*>(nativeTarget)->Handle;
		info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
		copy(pending.Commands, &info);
		VkMemoryBarrier after{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		after.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		after.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
			&after, 0, nullptr, 0, nullptr);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBuildBottomLevelAccelStruct(
		State* opaque,
		void* nativeCommand,
		void* nativeAccel,
		const AccelStructBuildDesc& desc,
		Span<const NativeAccelTriangleGeometry> triangles,
		Span<const NativeAccelAabbGeometry> aabbs,
		void* nativeScratch,
		uint64 scratchOffset)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(nativeCommand);

		if (!state.bAccelerationStructure || !state.F.CmdBuildAccelerationStructures || pending.QueueKind == EQueue::Copy)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanAccelStruct", "This command list cannot build acceleration structures.");
		}

		Array<VkAccelerationStructureGeometryKHR> geometries(state.Resource);
		Array<VkAccelerationStructureBuildRangeInfoKHR> ranges(state.Resource);
		Array<VkAccelerationStructureTrianglesOpacityMicromapEXT> micromaps(state.Resource);
		Array<VkMicromapUsageEXT> micromapCounts(state.Resource);
		usize usageCount = 0;

		for (const NativeAccelTriangleGeometry& triangle : triangles)
		{
			usageCount += triangle.OpacityMicromapUsageCounts.Size();
		}

		if (!micromaps.ResizeUninitialized(triangles.Size()) || !micromapCounts.Reserve(usageCount))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan micromap build geometry."));
		}

		usize triangleIndex = 0;

		for (const NativeAccelTriangleGeometry& triangle : triangles)
		{
			VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			geometry.flags = triangle.bOpaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
			VkAccelerationStructureGeometryTrianglesDataKHR& vertices = geometry.geometry.triangles;
			vertices.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			vertices.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			VkBufferDeviceAddressInfo address{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			address.buffer = static_cast<const Buffer*>(triangle.VertexBuffer)->Handle;
			vertices.vertexData.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &address) + triangle.VertexOffsetBytes;
			vertices.vertexStride = triangle.VertexStrideBytes;
			vertices.maxVertex = triangle.VertexCount - 1;
			vertices.indexType = VK_INDEX_TYPE_NONE_KHR;

			if (triangle.IndexBuffer != nullptr)
			{
				vertices.indexType = triangle.IndexFormat == EFormat::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
				address.buffer = static_cast<const Buffer*>(triangle.IndexBuffer)->Handle;
				vertices.indexData.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &address) + triangle.IndexOffsetBytes;
			}

			if (triangle.OpacityMicromap != nullptr)
			{
				const usize firstCount = micromapCounts.Size();

				for (const OpacityMicromapUsageCount& count : triangle.OpacityMicromapUsageCounts)
				{
					(void)micromapCounts.Add({ count.Count, count.SubdivisionLevel, static_cast<uint32>(count.Format) });
				}

				VkAccelerationStructureTrianglesOpacityMicromapEXT& omm = micromaps[triangleIndex];
				omm = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT };
				omm.indexType = VK_INDEX_TYPE_NONE_KHR;

				if (triangle.OpacityMicromapIndexBuffer != nullptr)
				{
					omm.indexType = triangle.OpacityMicromapIndexFormat == EFormat::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
					address.buffer = static_cast<const Buffer*>(triangle.OpacityMicromapIndexBuffer)->Handle;
					omm.indexBuffer.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &address) + triangle.OpacityMicromapIndexOffsetBytes;
					omm.indexStride = triangle.OpacityMicromapIndexStrideBytes;
				}
				omm.baseTriangle = triangle.OpacityMicromapBaseTriangle;
				omm.usageCountsCount = static_cast<uint32>(triangle.OpacityMicromapUsageCounts.Size());
				omm.pUsageCounts = micromapCounts.Data() + firstCount;
				omm.micromap = static_cast<const NativeMicromap*>(triangle.OpacityMicromap)->Handle;
				vertices.pNext = &omm;
			}
			++triangleIndex;
			VkAccelerationStructureBuildRangeInfoKHR range{};
			range.primitiveCount = triangle.IndexBuffer != nullptr ? triangle.IndexCount / 3 : triangle.VertexCount / 3;

			if (!geometries.Add(geometry) || !ranges.Add(range))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan build geometry."));
			}
		}

		for (const NativeAccelAabbGeometry& aabb : aabbs)
		{
			VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
			geometry.flags = aabb.bOpaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
			geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
			VkBufferDeviceAddressInfo address{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			address.buffer = static_cast<const Buffer*>(aabb.Buffer)->Handle;
			geometry.geometry.aabbs.data.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &address) + aabb.OffsetBytes;
			geometry.geometry.aabbs.stride = aabb.StrideBytes;
			VkAccelerationStructureBuildRangeInfoKHR range{};
			range.primitiveCount = aabb.Count;

			if (!geometries.Add(geometry) || !ranges.Add(range))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan build geometry."));
			}
		}
		VkBufferDeviceAddressInfo scratchInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		scratchInfo.buffer = static_cast<Buffer*>(nativeScratch)->Handle;
		VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		build.flags = desc.bAllowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0;

		if (desc.bAllowCompaction)
		{
			build.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
		}
		build.mode = desc.bPerformUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build.dstAccelerationStructure = static_cast<NativeAccelStruct*>(nativeAccel)->Handle;

		if (desc.bPerformUpdate)
		{
			build.srcAccelerationStructure = build.dstAccelerationStructure;
		}
		build.geometryCount = static_cast<uint32>(geometries.Size());
		build.pGeometries = geometries.Data();
		build.scratchData.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &scratchInfo) + scratchOffset;
		VkMemoryBarrier before{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1,
			&before, 0, nullptr, 0, nullptr);
		const VkAccelerationStructureBuildRangeInfoKHR* rangePtr = ranges.Data();
		state.F.CmdBuildAccelerationStructures(pending.Commands, 1, &build, &rangePtr);
		VkMemoryBarrier after{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		after.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		after.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
			&after, 0, nullptr, 0, nullptr);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBuildTopLevelAccelStruct(
		State* opaque,
		void* nativeCommand,
		void* nativeAccel,
		const AccelStructBuildDesc& desc,
		Span<const NativeAccelInstance> instances,
		void* nativeScratch,
		uint64 scratchOffset)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(nativeCommand);

		if (!state.bAccelerationStructure || !state.F.CmdBuildAccelerationStructures || pending.QueueKind == EQueue::Copy)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanAccelStruct", "This command list cannot build acceleration structures.");
		}

		const uint64 instanceBytes = instances.Size() * sizeof(VkAccelerationStructureInstanceKHR);
		Result<Buffer*> created = NewBuffer(state, instanceBytes, true, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

		if (!created)
		{
			return Result<void>::Failure(created.GetError());
		}
		Buffer* const upload = created.Value();
		void* mapping = nullptr;

		if (state.F.MapMemory(state.Device, upload->Memory, 0, VK_WHOLE_SIZE, 0, &mapping) != VK_SUCCESS)
		{
			FreeBuffer(state, upload);

			return Result<void>::Failure(Failure("Rhi.VulkanAccelStruct", "Could not map top level instances."));
		}

		VkAccelerationStructureInstanceKHR* const output = static_cast<VkAccelerationStructureInstanceKHR*>(mapping);

		for (usize index = 0; index < instances.Size(); ++index)
		{
			const NativeAccelInstance& source = instances[index];
			VkAccelerationStructureInstanceKHR& destination = output[index];
			CopyBytes(destination.transform.matrix, source.Transform, sizeof(destination.transform.matrix));
			destination.instanceCustomIndex = source.InstanceId;
			destination.mask = source.Mask;
			destination.instanceShaderBindingTableRecordOffset = source.HitGroupIndex;
			destination.flags = static_cast<VkGeometryInstanceFlagsKHR>(source.Flags);
			destination.accelerationStructureReference = source.BottomLevelAddress;
		}
		VkMappedMemoryRange flush{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		flush.memory = upload->Memory;
		flush.size = VK_WHOLE_SIZE;
		const bool bFlushed = upload->Coherent || state.F.FlushMappedMemoryRanges(state.Device, 1, &flush) == VK_SUCCESS;
		state.F.UnmapMemory(state.Device, upload->Memory);

		if (!bFlushed)
		{
			FreeBuffer(state, upload);

			return Result<void>::Failure(Failure("Rhi.VulkanAccelStruct", "Could not flush top level instances."));
		}
		VkBufferDeviceAddressInfo uploadInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		uploadInfo.buffer = upload->Handle;
		const VkDeviceAddress instanceAddress = state.F.GetBufferDeviceAddress(state.Device, &uploadInfo);

		if (instanceAddress == 0 || instanceAddress % 16 != 0)
		{
			FreeBuffer(state, upload);

			return Result<void>::Failure(Failure("Rhi.VulkanAccelStruct", "The instance buffer address is invalid."));
		}

		if (!pending.Temporary.Add(upload))
		{
			FreeBuffer(state, upload);

			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain top level instance upload storage."));
		}
		VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		geometry.geometry.instances.arrayOfPointers = VK_FALSE;
		geometry.geometry.instances.data.deviceAddress = instanceAddress;
		VkBufferDeviceAddressInfo scratchInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		scratchInfo.buffer = static_cast<Buffer*>(nativeScratch)->Handle;
		VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		build.flags = desc.bAllowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0;
		build.mode = desc.bPerformUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build.dstAccelerationStructure = static_cast<NativeAccelStruct*>(nativeAccel)->Handle;

		if (desc.bPerformUpdate)
		{
			build.srcAccelerationStructure = build.dstAccelerationStructure;
		}
		build.geometryCount = 1;
		build.pGeometries = &geometry;
		build.scratchData.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &scratchInfo) + scratchOffset;
		VkAccelerationStructureBuildRangeInfoKHR range{};
		range.primitiveCount = static_cast<uint32>(instances.Size());
		const VkAccelerationStructureBuildRangeInfoKHR* rangePtr = &range;
		VkMemoryBarrier before{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1,
			&before, 0, nullptr, 0, nullptr);
		state.F.CmdBuildAccelerationStructures(pending.Commands, 1, &build, &rangePtr);
		VkMemoryBarrier after{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		after.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		after.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
			&after, 0, nullptr, 0, nullptr);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<OpacityMicromapBuildSizes> GetOpacityMicromapBuildSizes(const State* opaque, Span<const OpacityMicromapUsageCount> counts, EOpacityMicromapBuildFlags flags)
	{
		const State& state = *opaque;

		if (!state.bOpacityMicromap || state.AccelScratchAlignment == 0)
		{
			return Result<OpacityMicromapBuildSizes>::Failure(EErrorCode::Unsupported, "Rhi.VulkanOpacityMicromap", "The device has no opacity micromap size query.");
		}

		Array<VkMicromapUsageEXT> native(state.Resource);

		if (!native.Reserve(counts.Size()))
		{
			return Result<OpacityMicromapBuildSizes>::Failure(Failure("Rhi.Allocation", "Could not allocate micromap usage counts."));
		}

		for (const OpacityMicromapUsageCount& count : counts)
		{
			const uint32 maximum = count.Format == EOpacityMicromapFormat::TwoState
				? state.MicromapProperties.maxOpacity2StateSubdivisionLevel
				: state.MicromapProperties.maxOpacity4StateSubdivisionLevel;

			if (count.SubdivisionLevel > maximum)
			{
				return Result<OpacityMicromapBuildSizes>::Failure(EErrorCode::Unsupported, "Rhi.VulkanOpacityMicromap", "The micromap subdivision level exceeds the device limit.");
			}
			(void)native.Add({ count.Count, count.SubdivisionLevel, static_cast<uint32>(count.Format) });
		}
		VkMicromapBuildInfoEXT build{ VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT };
		build.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
		build.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
		build.flags = static_cast<VkBuildMicromapFlagsEXT>(flags);
		build.usageCountsCount = static_cast<uint32>(native.Size());
		build.pUsageCounts = native.Data();
		VkMicromapBuildSizesInfoEXT size{ VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT };
		state.F.GetMicromapBuildSizes(state.Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &size);

		if (size.micromapSize == 0 || size.buildScratchSize == 0)
		{
			return Result<OpacityMicromapBuildSizes>::Failure(Failure("Rhi.VulkanOpacityMicromap", "The driver returned invalid micromap build sizes."));
		}

		return Result<OpacityMicromapBuildSizes>::Success(
			{ size.micromapSize, size.buildScratchSize, 256, state.AccelScratchAlignment });
	}

	[[nodiscard]] Result<void*> CreateOpacityMicromap(State* opaque, const OpacityMicromapDesc& desc, void* nativeStorage)
	{
		State& state = *opaque;

		if (!state.bOpacityMicromap)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanOpacityMicromap", "The device has no opacity micromaps.");
		}

		vulkan::NativeMicromap* const native = CreateObject<NativeMicromap>(state.Resource);

		if (native == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a Vulkan micromap record."));
		}
		VkMicromapCreateInfoEXT create{ VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT };
		create.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
		create.buffer = static_cast<Buffer*>(nativeStorage)->Handle;
		create.offset = desc.ByteOffset;
		create.size = desc.ByteCount;

		if (state.F.CreateMicromap(state.Device, &create, nullptr, &native->Handle) != VK_SUCCESS)
		{
			DestroyObject(state.Resource, native);

			return Result<void*>::Failure(Failure("Rhi.VulkanOpacityMicromap", "Could not create a Vulkan micromap."));
		}

		return Result<void*>::Success(native);
	}

	void DestroyOpacityMicromapNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		NativeMicromap* const micromap = static_cast<NativeMicromap*>(native);
		state.F.DestroyMicromap(state.Device, micromap->Handle, nullptr);
		DestroyObject(state.Resource, micromap);
	}

	[[nodiscard]] Result<void> DestroyOpacityMicromap(State* opaque, void* native, void* nativeStorage)
	{
		State& state = *opaque;
		uint64 complete = 0;

		if (!ReadCompletedSerial(state, complete))
		{
			return Result<void>::Failure(Failure("Rhi.VulkanTimeline", "Could not inspect Vulkan completion."));
		}

		const uint64 lastUse = static_cast<const Buffer*>(nativeStorage)->LastUse;

		if (lastUse > complete)
		{
			if (!state.RetiredMicromaps.Add({ static_cast<NativeMicromap*>(native), lastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track a retired Vulkan micromap."));
			}
		}
		else
		{
			DestroyOpacityMicromapNow(&state, native);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBuildOpacityMicromap(
		State* opaque,
		void* nativeCommand,
		void* nativeMicromap,
		const OpacityMicromapBuildDesc& desc,
		void* nativeInput,
		uint64 inputOffset,
		void* nativeTriangles,
		uint64 triangleOffset,
		void* nativeScratch,
		uint64 scratchOffset)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(nativeCommand);

		if (!state.bOpacityMicromap || pending.QueueKind == EQueue::Copy)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanOpacityMicromap", "This command list cannot build micromaps.");
		}

		Array<VkMicromapUsageEXT> counts(state.Resource);

		if (!counts.Reserve(desc.Counts.Size()))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate micromap usage counts."));
		}

		for (const OpacityMicromapUsageCount& count : desc.Counts)
		{
			(void)counts.Add({ count.Count, count.SubdivisionLevel, static_cast<uint32>(count.Format) });
		}
		VkBufferDeviceAddressInfo address{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		VkMicromapBuildInfoEXT build{ VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT };
		build.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
		build.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
		build.flags = static_cast<VkBuildMicromapFlagsEXT>(desc.Flags);
		build.dstMicromap = static_cast<NativeMicromap*>(nativeMicromap)->Handle;
		build.usageCountsCount = static_cast<uint32>(counts.Size());
		build.pUsageCounts = counts.Data();
		address.buffer = static_cast<Buffer*>(nativeInput)->Handle;
		build.data.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &address) + inputOffset;
		address.buffer = static_cast<Buffer*>(nativeTriangles)->Handle;
		build.triangleArray.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &address) + triangleOffset;
		build.triangleArrayStride = sizeof(VkMicromapTriangleEXT);
		address.buffer = static_cast<Buffer*>(nativeScratch)->Handle;
		build.scratchData.deviceAddress = state.F.GetBufferDeviceAddress(state.Device, &address) + scratchOffset;
		VkMemoryBarrier before{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		before.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &before, 0, nullptr, 0,
			nullptr);
		state.F.CmdBuildMicromaps(pending.Commands, 1, &build);
		VkMemoryBarrier after{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		after.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		after.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, nullptr, 0,
			nullptr);

		return Result<void>::Success();
	}
}
