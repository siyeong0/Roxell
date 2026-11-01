#include "D3D12Backend.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<uint32> PrepareAccelBuild(State* opaque, void* native, const NativePreparedAccelBuild& source)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		using R = Result<uint32>;
		if (!state.RayDevice || pending.Queue == EQueue::Copy || (!pending.RayList && FAILED(pending.List.As(&pending.RayList))))
		{
			return R::Failure(EErrorCode::Unsupported, "Rhi.PreparedAccel", "Ray build commands are unavailable.");
		}
		Pending::PreparedAccelBuild captured(state.Resource);
		if (!captured.Geometries.Reserve(source.Triangles.Size() + source.Aabbs.Size()))
		{
			return R::Failure(Failure("Rhi.Allocation", "Could not retain prepared build geometries."));
		}
		for (const NativeAccelTriangleGeometry& triangle : source.Triangles)
		{
			D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
			geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			geometry.Flags = triangle.bOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
			geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			geometry.Triangles.VertexCount = triangle.VertexCount;
			geometry.Triangles.VertexBuffer.StartAddress = static_cast<const Buffer*>(triangle.VertexBuffer)->Resource->GetGPUVirtualAddress() + triangle.VertexOffsetBytes;
			geometry.Triangles.VertexBuffer.StrideInBytes = triangle.VertexStrideBytes;

			if (triangle.IndexBuffer != nullptr)
			{
				geometry.Triangles.IndexFormat = triangle.IndexFormat == EFormat::R16_UINT ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
				geometry.Triangles.IndexCount = triangle.IndexCount;
				geometry.Triangles.IndexBuffer = static_cast<const Buffer*>(triangle.IndexBuffer)->Resource->GetGPUVirtualAddress() + triangle.IndexOffsetBytes;
			}

			if (!captured.Geometries.Add(geometry))
			{
				return R::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 acceleration structure geometries."));
			}
		}

		for (const NativeAccelAabbGeometry& aabb : source.Aabbs)
		{
			D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
			geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
			geometry.Flags = aabb.bOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
			geometry.AABBs.AABBCount = aabb.Count;
			geometry.AABBs.AABBs.StartAddress = static_cast<const Buffer*>(aabb.Buffer)->Resource->GetGPUVirtualAddress() + aabb.OffsetBytes;
			geometry.AABBs.AABBs.StrideInBytes = aabb.StrideBytes;

			if (!captured.Geometries.Add(geometry))
			{
				return R::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 acceleration structure geometries."));
			}
		}
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& build = captured.Build;
		build.DestAccelerationStructureData = static_cast<const NativeAccelStruct*>(source.Target)->Address;
		build.ScratchAccelerationStructureData = static_cast<const Buffer*>(source.Scratch)->Resource->GetGPUVirtualAddress() + source.ScratchOffsetBytes;
		build.Inputs.Type = source.Build.Type == EAccelStructType::BottomLevel ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		build.Inputs.NumDescs = source.Build.Type == EAccelStructType::BottomLevel ? static_cast<UINT>(captured.Geometries.Size()) : source.Build.InstanceCount;
		build.Inputs.Flags = source.Build.bAllowUpdate ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		if (source.Build.bAllowCompaction)
		{
			build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
		}
		if (source.Build.bPerformUpdate)
		{
			build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
			build.SourceAccelerationStructureData = build.DestAccelerationStructureData;
		}
		if (source.Build.Type == EAccelStructType::TopLevel)
		{
			build.Inputs.InstanceDescs = static_cast<const Buffer*>(source.Instances)->Resource->GetGPUVirtualAddress() + source.InstanceOffsetBytes;
		}
		const uint32 index = static_cast<uint32>(pending.PreparedAccelBuilds.Size());
		const bool bAdded = pending.PreparedAccelBuilds.Add(Move(captured));
		ASSERT(bAdded, "Prepared AS build storage is reserved cold.");

		return R::Success(index);
	}

	[[nodiscard]] Result<void> RecordPreparedAccelBuild(void*, void* native, uint32 index)
	{
		Pending& pending = *static_cast<Pending*>(native);
		d3d12::Pending::PreparedAccelBuild& captured = pending.PreparedAccelBuilds[index];
		if (captured.Build.Inputs.Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL)
		{
			captured.Build.Inputs.pGeometryDescs = captured.Geometries.Data();
		}
		pending.RayList->BuildRaytracingAccelerationStructure(&captured.Build, 0, nullptr);

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
		if ((!pending.RayList && FAILED(pending.List.As(&pending.RayList))) ||
			!CreateResource(state, sizeof(uint64), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, owner->Output, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) ||
			!CreateResource(state, sizeof(uint64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, owner->Readback))
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

	[[nodiscard]] Result<void> RecordPreparedAccelQuery(void*, void* native, uint32 index)
	{
		Pending& pending = *static_cast<Pending*>(native);
		d3d12::Pending::PreparedAccelQuery const& query = pending.PreparedAccelQueries[index];
		d3d12::PreparedAccelQueryOwner const& owner = *query.Owner;
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC info{};
		info.DestBuffer = owner.Output->GetGPUVirtualAddress();
		info.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
		const uint64 address = static_cast<const NativeAccelStruct*>(query.Source)->Address;
		pending.RayList->EmitRaytracingAccelerationStructurePostbuildInfo(&info, 1, &address);
		EmitUavBarrier(pending, *owner.Output.Get());
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = owner.Output.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		RecordBarriers(pending, 1, &barrier);
		pending.List->CopyBufferRegion(owner.Readback.Get(), 0, owner.Output.Get(), 0, sizeof(uint64));

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
		void* mapped = nullptr;
		D3D12_RANGE readRange{ 0, sizeof(uint64) };
		if (FAILED(owner->Readback->Map(0, &readRange, &mapped)))
		{
			return R::Failure(Failure("Rhi.PreparedAccel", "Could not read a completed compaction query."));
		}
		uint64 bytes = 0;
		CopyBytes(&bytes, mapped, sizeof(bytes));
		D3D12_RANGE written{ 0, 0 };
		owner->Readback->Unmap(0, &written);

		return R::Success({ EAccelCompactionQueryState::Ready, bytes });
	}

	[[nodiscard]] Result<uint32> PrepareAccelCompact(State* opaque, void* native, void* source, void* target)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		using R = Result<uint32>;
		if (!pending.RayList && FAILED(pending.List.As(&pending.RayList)))
		{
			return R::Failure(Failure("Rhi.PreparedAccel", "Ray copy commands are unavailable."));
		}
		const uint32 index = static_cast<uint32>(pending.PreparedAccelCompacts.Size());
		const bool bAdded = pending.PreparedAccelCompacts.Add({ source, target });
		ASSERT(bAdded, "Prepared compaction storage is reserved cold.");
		(void)state;

		return R::Success(index);
	}

	[[nodiscard]] Result<void> RecordPreparedAccelCompact(void*, void* native, uint32 index)
	{
		Pending& pending = *static_cast<Pending*>(native);
		d3d12::Pending::PreparedAccelCompact const& compact = pending.PreparedAccelCompacts[index];
		pending.RayList->CopyRaytracingAccelerationStructure(static_cast<const NativeAccelStruct*>(compact.Target)->Address,
			static_cast<const NativeAccelStruct*>(compact.Source)->Address,
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);

		return Result<void>::Success();
	}

	void CommitPreparedAccelQuery(void*, void* source, uint64 generation, EQueue queue, uint64 serial, bool bUncertain) noexcept
	{
		NativeAccelStruct& accel = *static_cast<NativeAccelStruct*>(source);
		ASSERT(accel.ProvisionalQuery && accel.ProvisionalQuery->Generation == generation, "The accepted query retains its exact cold generation.");
		d3d12::PreparedAccelQueryOwner& owner = *accel.ProvisionalQuery;
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
#endif
