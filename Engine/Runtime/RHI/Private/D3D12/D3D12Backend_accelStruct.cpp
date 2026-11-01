#include "D3D12Backend.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<AccelStructBuildSizes> GetAccelStructBuildSizes(
		const State* opaque,
		const AccelStructBuildDesc& desc,
		Span<const NativeAccelTriangleGeometry> triangles,
		Span<const NativeAccelAabbGeometry> aabbs)
	{
		const State& state = *opaque;

		if (!state.RayDevice)
		{
			return Result<AccelStructBuildSizes>::Failure(EErrorCode::Unsupported, "Rhi.D3D12AccelStruct", "The device has no ray tracing acceleration structures.");
		}

		Array<D3D12_RAYTRACING_GEOMETRY_DESC> geometries(state.Resource);

		for (const NativeAccelTriangleGeometry& triangle : triangles)
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

			if (!geometries.Add(geometry))
			{
				return Result<AccelStructBuildSizes>::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 acceleration structure geometries."));
			}
		}

		for (const NativeAccelAabbGeometry& aabb : aabbs)
		{
			D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
			geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
			geometry.Flags = aabb.bOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
			geometry.AABBs.AABBCount = aabb.Count;
			geometry.AABBs.AABBs.StartAddress = static_cast<const Buffer*>(aabb.Buffer)->Resource->GetGPUVirtualAddress() + aabb.OffsetBytes;
			geometry.AABBs.AABBs.StrideInBytes = aabb.StrideBytes;

			if (!geometries.Add(geometry))
			{
				return Result<AccelStructBuildSizes>::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 acceleration structure geometries."));
			}
		}
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
		inputs.Type = desc.Type == EAccelStructType::BottomLevel ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		inputs.Flags = desc.bAllowUpdate ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

		if (desc.bAllowCompaction)
		{
			inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
		}
		inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		inputs.NumDescs = desc.Type == EAccelStructType::BottomLevel ? static_cast<UINT>(geometries.Size()) : desc.InstanceCount;
		inputs.pGeometryDescs = desc.Type == EAccelStructType::BottomLevel ? geometries.Data() : nullptr;
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
#if ROXELL_RHI_WITH_NVAPI
		if (HasOpacityMicromaps(triangles))
		{
			Array<NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX> extended(state.Resource);
			Array<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT> counts(state.Resource);
			Result<void> converted = ConvertMicromapGeometries({ geometries.Data(), geometries.Size() }, triangles, extended, counts);
			if (!converted)
			{
				return Result<AccelStructBuildSizes>::Failure(converted.GetError());
			}

			NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX extendedInputs = MicromapAccelInputs(inputs, extended.Data());
			NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS parameters{};
			parameters.version = NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS_VER;
			parameters.pDesc = &extendedInputs;
			parameters.pInfo = &info;

			if (NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx(state.RayDevice.Get(), &parameters) != NVAPI_OK)
			{
				return Result<AccelStructBuildSizes>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not query a micromap acceleration structure build."));
			}
		}
		else
#endif
		{
			state.RayDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
		}

		if (info.ResultDataMaxSizeInBytes == 0 || info.ScratchDataSizeInBytes == 0)
		{
			return Result<AccelStructBuildSizes>::Failure(Failure("Rhi.D3D12AccelStruct", "The driver returned invalid acceleration structure build sizes."));
		}

		return Result<AccelStructBuildSizes>::Success(
			{ info.ResultDataMaxSizeInBytes, info.ScratchDataSizeInBytes, desc.bAllowUpdate ? info.UpdateScratchDataSizeInBytes : 0,
			 D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT });
	}

	[[nodiscard]] Result<void> RecordSetAccelStructState(void*, void* native, void* accelNative, EAccelStructState)
	{
		Pending& pending = *static_cast<Pending*>(native);
		NativeAccelStruct& accel = *static_cast<NativeAccelStruct*>(accelNative);
		EmitUavBarrier(pending, *accel.Storage.Get());

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> CreateAccelStruct(State* opaque, const AccelStructDesc& desc, void* nativeBuffer)
	{
		State& state = *opaque;

		if (!state.RayDevice)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12AccelStruct", "The device has no ray tracing acceleration structures.");
		}

		d3d12::NativeAccelStruct* const native = CreateObject<NativeAccelStruct>(state.Resource);

		if (native == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a D3D12 acceleration structure record."));
		}
		native->StorageBuffer = static_cast<Buffer*>(nativeBuffer);
		native->Storage = native->StorageBuffer->Resource;
		native->Address = native->Storage->GetGPUVirtualAddress() + desc.ByteOffset;

		return Result<void*>::Success(native);
	}

	void DestroyAccelStructNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<NativeAccelStruct*>(native));
	}

	[[nodiscard]] Result<void> DestroyAccelStruct(State* opaque, void* native)
	{
		DestroyAccelStructNow(opaque, native);

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
		static_assert(sizeof(AccelGpuInstanceDesc) == sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(nativeCommand);

		if (!state.RayDevice || pending.Queue == EQueue::Copy)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12AccelStruct", "This command list cannot build acceleration structures.");
		}

		if (!pending.RayList && FAILED(pending.List.As(&pending.RayList)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "The command list does not expose ray tracing build commands."));
		}

		Buffer& input = *static_cast<Buffer*>(nativeInstances);
		Buffer& scratch = *static_cast<Buffer*>(nativeScratch);
		Transition(pending, *input.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		Transition(pending, *scratch.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not track top level build input state."));
		}
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
		build.DestAccelerationStructureData = static_cast<const NativeAccelStruct*>(nativeAccel)->Address;
		build.ScratchAccelerationStructureData = scratch.Resource->GetGPUVirtualAddress() + scratchOffset;
		build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		build.Inputs.Flags = desc.bAllowUpdate ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

		if (desc.bPerformUpdate)
		{
			build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
			build.SourceAccelerationStructureData = build.DestAccelerationStructureData;
		}
		build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		build.Inputs.NumDescs = desc.InstanceCount;
		build.Inputs.InstanceDescs = input.Resource->GetGPUVirtualAddress() + instanceOffset;
		pending.RayList->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = static_cast<NativeAccelStruct*>(nativeAccel)->Storage.Get();
		RecordBarriers(pending, 1, &barrier);
		Transition(pending, *input.Resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
		Transition(pending, *scratch.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not restore top level build input state."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] uint64 GetAccelStructDeviceAddress(const void*, const void* native) noexcept
	{
		return static_cast<const NativeAccelStruct*>(native)->Address;
	}

	void PropagateAccelStructLastUseToBuffer(const void*, const void* source, void* destination) noexcept
	{
		const Buffer& origin = *static_cast<const NativeAccelStruct*>(source)->StorageBuffer;
		Buffer& target = *static_cast<Buffer*>(destination);

		MergeBufferUses(origin, target);
	}

	void PropagateAccelStructLastUse(const State* opaque, const void* source, void* destination) noexcept
	{
		PropagateAccelStructLastUseToBuffer(opaque, source, static_cast<NativeAccelStruct*>(destination)->StorageBuffer);
	}

	[[nodiscard]] Result<uint64> GetBottomLevelAccelStructCompactedSize(State* opaque, void* native)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<uint64>::Failure(active.GetError());
		}
		ComPtr<ID3D12Resource> output;
		ComPtr<ID3D12Resource> readback;

		if (!CreateResource(state, sizeof(uint64), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, output, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) ||
			!CreateResource(state, sizeof(uint64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, readback))
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not allocate compacted size readback resources."));
		}

		Result<Pending> started = Begin(state, EQueue::Graphics);

		if (!started)
		{
			return Result<uint64>::Failure(started.GetError());
		}
		Pending pending = Move(started).TakeValue();
		const NativeAccelStruct* source = static_cast<const NativeAccelStruct*>(native);

		if ((!pending.RayList && FAILED(pending.List.As(&pending.RayList))) || !pending.Temporary.Add(source->Storage) || !pending.Temporary.Add(output) || !pending.Temporary.Add(readback))
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not retain compacted size query resources."));
		}
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC info{};
		info.DestBuffer = output->GetGPUVirtualAddress();
		info.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
		const uint64 address = source->Address;
		pending.RayList->EmitRaytracingAccelerationStructurePostbuildInfo(&info, 1, &address);
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = output.Get();
		RecordBarriers(pending, 1, &barrier);
		barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = output.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		RecordBarriers(pending, 1, &barrier);
		pending.List->CopyBufferRegion(readback.Get(), 0, output.Get(), 0, sizeof(uint64));
		Result<uint64> submitted = Execute(state, Move(pending));

		if (!submitted)
		{
			return Result<uint64>::Failure(submitted.GetError());
		}

		Result<void> waited = Wait(&state, EQueue::Graphics, submitted.Value(), NumericLimits<uint32>::max());

		if (!waited)
		{
			return Result<uint64>::Failure(waited.GetError());
		}
		void* mapped = nullptr;
		D3D12_RANGE range{ 0, sizeof(uint64) };
		if (FAILED(readback->Map(0, &range, &mapped)))
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not map compacted size readback."));
		}

		uint64 bytes = 0;
		CopyBytes(&bytes, mapped, sizeof(bytes));
		D3D12_RANGE written{ 0, 0 };
		readback->Unmap(0, &written);

		return Result<uint64>::Success(bytes);
	}

	[[nodiscard]] Result<void> RecordCompactBottomLevelAccelStruct(State* opaque, void* nativeCommand, void* nativeSource, void* nativeTarget)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(nativeCommand);

		if (!state.RayDevice || pending.Queue == EQueue::Copy || (!pending.RayList && FAILED(pending.List.As(&pending.RayList))))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "This command list cannot compact acceleration structures."));
		}

		const NativeAccelStruct* source = static_cast<const NativeAccelStruct*>(nativeSource);
		const NativeAccelStruct* target = static_cast<const NativeAccelStruct*>(nativeTarget);
		pending.RayList->CopyRaytracingAccelerationStructure(target->Address, source->Address,
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = target->Storage.Get();
		RecordBarriers(pending, 1, &barrier);

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

		if (!state.RayDevice || pending.Queue == EQueue::Copy)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12AccelStruct", "This command list cannot build acceleration structures.");
		}

		if (!pending.RayList && FAILED(pending.List.As(&pending.RayList)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "The command list does not expose ray tracing build commands."));
		}

		Array<D3D12_RAYTRACING_GEOMETRY_DESC> geometries(state.Resource);

		for (const NativeAccelTriangleGeometry& triangle : triangles)
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

			if (!geometries.Add(geometry))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 build geometry."));
			}
			Transition(pending, *static_cast<Buffer*>(triangle.VertexBuffer)->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			if (triangle.IndexBuffer != nullptr)
			{
				Transition(pending, *static_cast<Buffer*>(triangle.IndexBuffer)->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			}

			if (triangle.OpacityMicromapIndexBuffer != nullptr)
			{
				Transition(pending, *static_cast<Buffer*>(triangle.OpacityMicromapIndexBuffer)->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			}
		}

		for (const NativeAccelAabbGeometry& aabb : aabbs)
		{
			D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
			geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
			geometry.Flags = aabb.bOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
			geometry.AABBs.AABBCount = aabb.Count;
			geometry.AABBs.AABBs.StartAddress = static_cast<const Buffer*>(aabb.Buffer)->Resource->GetGPUVirtualAddress() + aabb.OffsetBytes;
			geometry.AABBs.AABBs.StrideInBytes = aabb.StrideBytes;

			if (!geometries.Add(geometry))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 build geometry."));
			}
			Transition(pending, *static_cast<Buffer*>(aabb.Buffer)->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}

		Buffer& scratch = *static_cast<Buffer*>(nativeScratch);
		Transition(pending, *scratch.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not track acceleration structure build buffer states."));
		}
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
		build.DestAccelerationStructureData = static_cast<const NativeAccelStruct*>(nativeAccel)->Address;
		build.ScratchAccelerationStructureData = scratch.Resource->GetGPUVirtualAddress() + scratchOffset;
		build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		build.Inputs.Flags = desc.bAllowUpdate ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

		if (desc.bAllowCompaction)
		{
			build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
		}
		build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

		if (desc.bPerformUpdate)
		{
			build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
			build.SourceAccelerationStructureData = build.DestAccelerationStructureData;
		}
		build.Inputs.NumDescs = static_cast<UINT>(geometries.Size());
		build.Inputs.pGeometryDescs = geometries.Data();
#if ROXELL_RHI_WITH_NVAPI
		if (HasOpacityMicromaps(triangles))
		{
			Array<NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX> extended(state.Resource);
			Array<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT> counts(state.Resource);
			Result<void> converted = ConvertMicromapGeometries({ geometries.Data(), geometries.Size() }, triangles, extended, counts);
			if (!converted)
			{
				return converted;
			}
			NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC_EX extendedBuild{};
			extendedBuild.inputs = MicromapAccelInputs(build.Inputs, extended.Data());
			extendedBuild.destAccelerationStructureData = build.DestAccelerationStructureData;
			extendedBuild.sourceAccelerationStructureData = build.SourceAccelerationStructureData;
			extendedBuild.scratchAccelerationStructureData = build.ScratchAccelerationStructureData;
			NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS parameters{};
			parameters.version = NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS_VER;
			parameters.pDesc = &extendedBuild;

			if (NvAPI_D3D12_BuildRaytracingAccelerationStructureEx(pending.RayList.Get(), &parameters) != NVAPI_OK)
			{
				return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not record a micromap acceleration structure build."));
			}
		}
		else
#endif
		{
			pending.RayList->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
		}
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = static_cast<NativeAccelStruct*>(nativeAccel)->Storage.Get();
		RecordBarriers(pending, 1, &barrier);

		for (const NativeAccelTriangleGeometry& triangle : triangles)
		{
			Transition(pending, *static_cast<Buffer*>(triangle.VertexBuffer)->Resource.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);

			if (triangle.IndexBuffer != nullptr)
			{
				Transition(pending, *static_cast<Buffer*>(triangle.IndexBuffer)->Resource.Get(),
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
			}

			if (triangle.OpacityMicromapIndexBuffer != nullptr)
			{
				Transition(pending, *static_cast<Buffer*>(triangle.OpacityMicromapIndexBuffer)->Resource.Get(),
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
			}
		}

		for (const NativeAccelAabbGeometry& aabb : aabbs)
		{
			Transition(pending, *static_cast<Buffer*>(aabb.Buffer)->Resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_COMMON);
		}
		Transition(pending, *scratch.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not restore acceleration structure build buffer states."));
		}

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

		if (!state.RayDevice || pending.Queue == EQueue::Copy)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12AccelStruct", "This command list cannot build acceleration structures.");
		}

		if (!pending.RayList && FAILED(pending.List.As(&pending.RayList)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "The command list does not expose ray tracing build commands."));
		}
		ComPtr<ID3D12Resource> upload;
		const uint64 uploadBytes = instances.Size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

		if (!CreateResource(state, uploadBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, upload))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not create a top level instance upload buffer."));
		}
		D3D12_RAYTRACING_INSTANCE_DESC* mapped = nullptr;

		if (FAILED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped))))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not map top level instances."));
		}

		for (usize index = 0; index < instances.Size(); ++index)
		{
			const NativeAccelInstance& source = instances[index];
			D3D12_RAYTRACING_INSTANCE_DESC& destination = mapped[index];
			CopyBytes(destination.Transform, source.Transform, sizeof(destination.Transform));
			destination.InstanceID = source.InstanceId;
			destination.InstanceMask = source.Mask;
			destination.InstanceContributionToHitGroupIndex = source.HitGroupIndex;
			destination.Flags = static_cast<UINT>(source.Flags);
			destination.AccelerationStructure = source.BottomLevelAddress;
		}
		upload->Unmap(0, nullptr);

		if (!pending.Temporary.Add(Move(upload)))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain top level instance upload storage."));
		}

		Buffer& scratch = *static_cast<Buffer*>(nativeScratch);
		Transition(pending, *scratch.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not track top level scratch state."));
		}
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
		build.DestAccelerationStructureData = static_cast<const NativeAccelStruct*>(nativeAccel)->Address;
		build.ScratchAccelerationStructureData = scratch.Resource->GetGPUVirtualAddress() + scratchOffset;
		build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		build.Inputs.Flags = desc.bAllowUpdate ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		build.Inputs.NumDescs = static_cast<UINT>(instances.Size());

		if (desc.bPerformUpdate)
		{
			build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
			build.SourceAccelerationStructureData = build.DestAccelerationStructureData;
		}
		build.Inputs.InstanceDescs = pending.Temporary.Back()->GetGPUVirtualAddress();
		pending.RayList->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = static_cast<NativeAccelStruct*>(nativeAccel)->Storage.Get();
		RecordBarriers(pending, 1, &barrier);
		Transition(pending, *scratch.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12AccelStruct", "Could not restore top level scratch state."));
		}

		return Result<void>::Success();
	}
}
#endif
