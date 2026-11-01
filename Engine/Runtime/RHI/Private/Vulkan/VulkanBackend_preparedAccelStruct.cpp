#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] VkDeviceAddress PreparedBufferAddress(State& state, const Buffer& buffer) noexcept
	{
		VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		info.buffer = buffer.Handle;

		return state.F.GetBufferDeviceAddress(state.Device, &info);
	}

	[[nodiscard]] Result<uint32> PrepareAccelBuild(State* opaque, void* native, const NativePreparedAccelBuild& source)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		using R = Result<uint32>;
		if (!state.bAccelerationStructure || !state.F.CmdBuildAccelerationStructures || pending.QueueKind == EQueue::Copy)
		{
			return R::Failure(EErrorCode::Unsupported, "Rhi.PreparedAccel", "Ray build commands are unavailable.");
		}
		Pending::PreparedAccelBuild captured(state.Resource);
		const usize count = source.Build.Type == EAccelStructType::TopLevel ? 1u : source.Triangles.Size() + source.Aabbs.Size();
		if (!captured.Geometries.Reserve(count) || !captured.Ranges.Reserve(count))
		{
			return R::Failure(Failure("Rhi.Allocation", "Could not retain prepared build geometries."));
		}
		for (const rhi::NativeAccelTriangleGeometry& triangle : source.Triangles)
		{
			VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			geometry.flags = triangle.bOpaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
			VkAccelerationStructureGeometryTrianglesDataKHR& vertices = geometry.geometry.triangles;
			vertices.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			vertices.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			vertices.vertexData.deviceAddress = PreparedBufferAddress(state, *static_cast<const Buffer*>(triangle.VertexBuffer)) + triangle.VertexOffsetBytes;
			vertices.vertexStride = triangle.VertexStrideBytes;
			vertices.maxVertex = triangle.VertexCount - 1;
			vertices.indexType = VK_INDEX_TYPE_NONE_KHR;
			if (triangle.IndexBuffer != nullptr)
			{
				vertices.indexType = triangle.IndexFormat == EFormat::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
				vertices.indexData.deviceAddress = PreparedBufferAddress(state, *static_cast<const Buffer*>(triangle.IndexBuffer)) + triangle.IndexOffsetBytes;
			}
			const uint32 primitives = triangle.IndexBuffer != nullptr ? triangle.IndexCount / 3 : triangle.VertexCount / 3;
			(void)captured.Geometries.Add(geometry);
			(void)captured.Ranges.Add({ primitives, 0, 0, 0 });
		}
		for (const rhi::NativeAccelAabbGeometry& aabb : source.Aabbs)
		{
			VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
			geometry.flags = aabb.bOpaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
			geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
			geometry.geometry.aabbs.data.deviceAddress = PreparedBufferAddress(state, *static_cast<const Buffer*>(aabb.Buffer)) + aabb.OffsetBytes;
			geometry.geometry.aabbs.stride = aabb.StrideBytes;
			(void)captured.Geometries.Add(geometry);
			(void)captured.Ranges.Add({ aabb.Count, 0, 0, 0 });
		}
		if (source.Build.Type == EAccelStructType::TopLevel)
		{
			VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
			geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
			geometry.geometry.instances.data.deviceAddress = PreparedBufferAddress(state, *static_cast<const Buffer*>(source.Instances)) + source.InstanceOffsetBytes;
			if (geometry.geometry.instances.data.deviceAddress == 0 || geometry.geometry.instances.data.deviceAddress % 16 != 0)
			{
				return R::Failure(Failure("Rhi.PreparedAccel", "The native instance address is invalid."));
			}
			(void)captured.Geometries.Add(geometry);
			(void)captured.Ranges.Add({ source.Build.InstanceCount, 0, 0, 0 });
		}
		VkAccelerationStructureBuildGeometryInfoKHR& build = captured.Build;
		build.type = source.Build.Type == EAccelStructType::BottomLevel ? VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		build.flags = source.Build.bAllowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0;
		if (source.Build.bAllowCompaction)
		{
			build.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
		}
		build.mode = source.Build.bPerformUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build.dstAccelerationStructure = static_cast<const NativeAccelStruct*>(source.Target)->Handle;
		build.srcAccelerationStructure = source.Build.bPerformUpdate ? build.dstAccelerationStructure : VK_NULL_HANDLE;
		build.scratchData.deviceAddress = PreparedBufferAddress(state, *static_cast<const Buffer*>(source.Scratch)) + source.ScratchOffsetBytes;
		if (build.scratchData.deviceAddress == 0 || build.scratchData.deviceAddress % state.AccelScratchAlignment != 0)
		{
			return R::Failure(Failure("Rhi.PreparedAccel", "The native scratch address is invalid."));
		}
		build.geometryCount = static_cast<uint32>(captured.Geometries.Size());
		const uint32 index = static_cast<uint32>(pending.PreparedAccelBuilds.Size());
		const bool bAdded = pending.PreparedAccelBuilds.Add(Move(captured));
		ASSERT(bAdded, "Prepared AS build storage is reserved cold.");

		return R::Success(index);
	}

	[[nodiscard]] Result<void> RecordPreparedAccelBuild(State* opaque, void* native, uint32 index)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		vulkan::Pending::PreparedAccelBuild& captured = pending.PreparedAccelBuilds[index];
		captured.Build.pGeometries = captured.Geometries.Data();
		VkAccelerationStructureBuildRangeInfoKHR const* ranges = captured.Ranges.Data();
		state.F.CmdBuildAccelerationStructures(pending.Commands, 1, &captured.Build, &ranges);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint32> PrepareAccelQuery(State* opaque, void* native, void* source, uint64 generation)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		using R = Result<uint32>;
		SharedPtr<PreparedAccelQueryOwner> owner;
		if (!MakeSharedInto(state.Resource, &owner))
		{
			return R::Failure(Failure("Rhi.Allocation", "Could not retain a prepared compaction query."));
		}
		owner->Device = &state;
		owner->Generation = generation;
		owner->Write = reinterpret_cast<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>( state.F.GetDeviceProcAddr(state.Device, "vkCmdWriteAccelerationStructuresPropertiesKHR"));
		VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
		info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
		info.queryCount = 1;
		if (owner->Write == nullptr || state.F.CreateQueryPool(state.Device, &info, nullptr, &owner->Pool) != VK_SUCCESS)
		{
			return R::Failure(Failure("Rhi.PreparedAccel", "Could not create a prepared compaction query."));
		}
		const uint32 index = static_cast<uint32>(pending.PreparedAccelQueries.Size());
		NativeAccelStruct& accel = *static_cast<NativeAccelStruct*>(source);
		if (accel.PreparedQuery)
		{
			accel.PreparedQuery->Previous.Reset();
		}
		owner->Previous = accel.PreparedQuery;
		const bool bAdded = pending.PreparedAccelQueries.Add({ source, Move(owner) });
		accel.ProvisionalQuery = pending.PreparedAccelQueries.Back().Owner;
		ASSERT(bAdded, "Prepared query storage is reserved cold.");

		return R::Success(index);
	}

	[[nodiscard]] Result<void> RecordPreparedAccelQuery(State* opaque, void* native, uint32 index)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		vulkan::Pending::PreparedAccelQuery const& query = pending.PreparedAccelQueries[index];
		vulkan::PreparedAccelQueryOwner const& owner = *query.Owner;
		state.F.CmdResetQueryPool(pending.Commands, owner.Pool, 0, 1);
		const VkAccelerationStructureKHR source = static_cast<const NativeAccelStruct*>(query.Source)->Handle;
		owner.Write(pending.Commands, 1, &source, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, owner.Pool, 0);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<AccelCompactionResult> TryGetAccelCompactedSize(State* opaque, void* source, uint64 generation)
	{
		using R = Result<AccelCompactionResult>;
		State& state = *opaque;
		const SharedPtr<PreparedAccelQueryOwner>& owner = static_cast<const NativeAccelStruct*>(source)->PreparedQuery;
		if (!owner || owner->Generation != generation || state.bLost)
		{
			return R::Success({ EAccelCompactionQueryState::Unavailable, 0 });
		}
		if (owner->Serial == 0)
		{
			return R::Success({});
		}
		Result<bool> completed = IsComplete(opaque, owner->Serial);
		if (!completed || !completed.Value())
		{
			return R::Success({ completed ? EAccelCompactionQueryState::Pending : EAccelCompactionQueryState::Unavailable, 0 });
		}
		if (owner->bUnavailable)
		{
			return R::Success({ EAccelCompactionQueryState::Unavailable, 0 });
		}
		uint64 bytes = 0;
		const VkResult result = state.F.GetQueryPoolResults(state.Device, owner->Pool, 0, 1, sizeof(bytes), &bytes, sizeof(bytes), VK_QUERY_RESULT_64_BIT);
		if (result == VK_NOT_READY)
		{
			return R::Success({});
		}
		if (result != VK_SUCCESS)
		{
			return R::Failure(Failure("Rhi.PreparedAccel", "Could not read a completed compaction query."));
		}

		return R::Success({ EAccelCompactionQueryState::Ready, bytes });
	}

	[[nodiscard]] Result<uint32> PrepareAccelCompact(State* opaque, void* native, void* source, void* target)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		using R = Result<uint32>;
		const PFN_vkCmdCopyAccelerationStructureKHR copy = reinterpret_cast<PFN_vkCmdCopyAccelerationStructureKHR>( state.F.GetDeviceProcAddr(state.Device, "vkCmdCopyAccelerationStructureKHR"));
		if (copy == nullptr)
		{
			return R::Failure(EErrorCode::Unsupported, "Rhi.PreparedAccel", "Ray copy commands are unavailable.");
		}
		const uint32 index = static_cast<uint32>(pending.PreparedAccelCompacts.Size());
		const bool bAdded = pending.PreparedAccelCompacts.Add({ source, target, copy });
		ASSERT(bAdded, "Prepared compaction storage is reserved cold.");

		return R::Success(index);
	}

	[[nodiscard]] Result<void> RecordPreparedAccelCompact(void*, void* native, uint32 index)
	{
		Pending& pending = *static_cast<Pending*>(native);
		vulkan::Pending::PreparedAccelCompact const& compact = pending.PreparedAccelCompacts[index];
		VkCopyAccelerationStructureInfoKHR copy{ VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
		copy.src = static_cast<const NativeAccelStruct*>(compact.Source)->Handle;
		copy.dst = static_cast<const NativeAccelStruct*>(compact.Target)->Handle;
		copy.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
		compact.Copy(pending.Commands, &copy);

		return Result<void>::Success();
	}

	void CommitPreparedAccelQuery(void*, void* source, uint64 generation, EQueue queue, uint64 serial, bool bUncertain) noexcept
	{
		NativeAccelStruct& accel = *static_cast<NativeAccelStruct*>(source);
		ASSERT(accel.ProvisionalQuery && accel.ProvisionalQuery->Generation == generation, "The accepted query retains its exact cold generation.");
		vulkan::PreparedAccelQueryOwner& owner = *accel.ProvisionalQuery;
		owner.Queue = queue;
		owner.Serial = serial;
		owner.bUnavailable = bUncertain;
		accel.PreparedQuery = Move(accel.ProvisionalQuery);
	}

	void CancelPreparedAccelQuery(void*, void* source, uint64 generation) noexcept
	{
		NativeAccelStruct& accel = *static_cast<NativeAccelStruct*>(source);
		if (accel.ProvisionalQuery && accel.ProvisionalQuery->Generation == generation)
		{
			accel.ProvisionalQuery.Reset();
		}
	}
}
