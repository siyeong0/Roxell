#include "D3D12Backend.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] bool PrepareClusterInput(const ClusterOperationParams& params, NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS& input) noexcept
	{
		const uint8 flags = static_cast<uint8>(params.Flags);

		if ((flags & ~uint8{ 15 }) != 0 || (flags & 3) == 3 || params.MaxArgCount == 0 || params.Mode > EClusterOperationMode::GetSizes || params.Type > EClusterOperationType::BlasBuild)
		{
			return false;
		}
		input = {};
		input.maxArgCount = params.MaxArgCount;
		input.mode = static_cast<NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE>(params.Mode);
		input.flags = static_cast<NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAGS>(flags);

		switch (params.Type)
		{
		case EClusterOperationType::Move:
			if (params.MoveType > EClusterMoveType::Template || params.MaxMovedBytes == 0 || params.MaxMovedBytes > UINT32_MAX)
			{
				return false;
			}
			input.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_MOVE_CLUSTER_OBJECT;
			input.movesDesc.type = static_cast<NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE>(params.MoveType);
			input.movesDesc.maxBytesMoved = static_cast<NvU32>(params.MaxMovedBytes);
			break;
		case EClusterOperationType::BlasBuild:
			if (params.MaxClasPerBlasCount == 0 ||
				params.MaxTotalClasCount == 0 ||
				params.MaxClasPerBlasCount > params.MaxTotalClasCount ||
				params.MaxClasPerBlasCount > D3D12_RAYTRACING_MAX_PRIMITIVES_PER_BOTTOM_LEVEL_ACCELERATION_STRUCTURE)
			{
				return false;
			}
			input.type = NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS;
			input.clasDesc.maxClasCountPerArg = params.MaxClasPerBlasCount;
			input.clasDesc.maxTotalClasCount = params.MaxTotalClasCount;
			break;
		default:
			if (params.VertexFormat >= EFormat::COUNT ||
				params.MaxUniqueGeometryCount == 0 ||
				params.MaxUniqueGeometryCount > 256 ||
				params.MaxTriangleCount == 0 ||
				params.MaxTriangleCount > 256 ||
				params.MaxVertexCount == 0 ||
				params.MaxVertexCount > 256 ||
				params.MinPositionTruncateBitCount > 32 ||
				params.MaxGeometryIndex > NVAPI_D3D12_RAYTRACING_MAXIMUM_GEOMETRY_INDEX ||
				params.MaxTriangleCount > params.MaxTotalTriangleCount ||
				params.MaxVertexCount > params.MaxTotalVertexCount)
			{
				return false;
			}
			input.type = params.Type == EClusterOperationType::ClasBuild
				? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES
				: params.Type == EClusterOperationType::ClasBuildTemplates
				? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES
				: NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_INSTANTIATE_CLUSTER_TEMPLATES;
			input.trianglesDesc.vertexFormat = GetDxgiFormatMapping(params.VertexFormat).SrvFormat;

			if (input.trianglesDesc.vertexFormat == DXGI_FORMAT_UNKNOWN)
			{
				return false;
			}
			input.trianglesDesc.maxGeometryIndexValue = params.MaxGeometryIndex;
			input.trianglesDesc.maxUniqueGeometryCountPerArg = params.MaxUniqueGeometryCount;
			input.trianglesDesc.maxTriangleCountPerArg = params.MaxTriangleCount;
			input.trianglesDesc.maxVertexCountPerArg = params.MaxVertexCount;
			input.trianglesDesc.maxTotalTriangleCount = params.MaxTotalTriangleCount;
			input.trianglesDesc.maxTotalVertexCount = params.MaxTotalVertexCount;
			input.trianglesDesc.minPositionTruncateBitCount = params.MinPositionTruncateBitCount;
			break;
		}

		return true;
	}
#endif
#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] Result<ClusterOperationSizeInfo> GetClusterOperationSizeInfo(const State* opaque, const ClusterOperationParams& params)
	{
		const State& state = *opaque;

		if (!state.bRayTracingClusters || !state.RayDevice)
		{
			return Result<ClusterOperationSizeInfo>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Cluster", "Cluster operations are unavailable.");
		}

		if ((static_cast<uint8>(params.Flags) & 8) != 0 && !state.bOpacityMicromap)
		{
			return Result<ClusterOperationSizeInfo>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Cluster", "Cluster opacity micromaps are unavailable.");
		}
		NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS input{};
		const bool bValidClusterInput = PrepareClusterInput(params, input);
		ASSERT(bValidClusterInput, "Cluster parameters must satisfy the native input contract.");
		NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO sizes{};
		NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS query{};
		query.version = NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS_VER;
		query.pInput = &input;
		query.pInfo = &sizes;
		const NvAPI_Status status = NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo(state.RayDevice.Get(), &query);

		if (status == NVAPI_INVALID_ARGUMENT && params.Type == EClusterOperationType::Move && (static_cast<uint8>(params.Flags) & 8) != 0)
		{
			return Result<ClusterOperationSizeInfo>::Failure(
				EErrorCode::Unsupported,
				"Rhi.D3D12Cluster",
				"NVAPI rejected opacity micromap cluster move requirements (NVAPI_INVALID_ARGUMENT).",
				"Keep the cluster allocation or rebuild at the destination; do not remove AllowOpacityMicromaps.");
		}

		if (status != NVAPI_OK)
		{
			return Result<ClusterOperationSizeInfo>::Failure(Failure("Rhi.D3D12Cluster", "The driver rejected the cluster size query."));
		}

		const uint64 resultAlignment =
			params.Type == EClusterOperationType::BlasBuild ||
			(params.Type == EClusterOperationType::Move && params.MoveType == EClusterMoveType::BottomLevel)
			? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT
			: params.Type == EClusterOperationType::ClasBuildTemplates ||
			(params.Type == EClusterOperationType::Move && params.MoveType == EClusterMoveType::Template)
			? NVAPI_D3D12_RAYTRACING_CLUSTER_TEMPLATE_BYTE_ALIGNMENT
			: NVAPI_D3D12_RAYTRACING_CLAS_BYTE_ALIGNMENT;

		return Result<ClusterOperationSizeInfo>::Success({ sizes.resultDataMaxSizeInBytes, sizes.scratchDataSizeInBytes,
																resultAlignment, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT });
	}
#endif
#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] Result<void> RecordClusterOperation(
		State* opaque,
		void* nativeCommand,
		const ClusterOperationDesc& desc,
		void* nativeScratch,
		void* nativeCount,
		void* nativeArgs,
		void* nativeAddresses,
		void* nativeSizes,
		void* nativeResult)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(nativeCommand);

		if (!state.bRayTracingClusters || !state.RayDevice || pending.Queue == EQueue::Copy)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Cluster", "This command list cannot execute cluster operations.");
		}
		NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC command{};
		const bool bValidClusterInput = PrepareClusterInput(desc.Params, command.inputs);
		ASSERT(bValidClusterInput, "Cluster parameters must satisfy the native input contract.");

		if (!pending.RayList && FAILED(pending.List.As(&pending.RayList)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Cluster", "The command list does not expose ray tracing build commands."));
		}

		const auto address = [](void* native, uint64 offset) -> D3D12_GPU_VIRTUAL_ADDRESS
			{ return native ? static_cast<Buffer*>(native)->Resource->GetGPUVirtualAddress() + offset : 0; };
		const auto prepare = [&](void* native, D3D12_RESOURCE_STATES required)
			{
				if (native)
				{
					Transition(pending, *static_cast<Buffer*>(native)->Resource.Get(), D3D12_RESOURCE_STATE_COMMON, required);
				}
			};
		prepare(nativeScratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		prepare(nativeCount, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		prepare(nativeArgs, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		prepare(nativeAddresses, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		prepare(nativeSizes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		prepare(nativeResult, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Cluster", "Could not prepare cluster operation resource states."));
		}
		command.batchResultData = desc.Params.Mode == EClusterOperationMode::ImplicitDestinations ? address(nativeResult, desc.ResultOffsetBytes) : 0;
		command.batchScratchData = address(nativeScratch, desc.ScratchOffsetBytes);
		command.indirectArgCount = address(nativeCount, desc.IndirectArgCountOffsetBytes);
		command.indirectArgArray = { address(nativeArgs, desc.IndirectArgsOffsetBytes), desc.IndirectArgsStrideBytes };
		command.destinationAddressArray = { address(nativeAddresses, desc.AddressesOffsetBytes), desc.AddressesStrideBytes };
		command.resultSizeArray = { address(nativeSizes, desc.SizesOffsetBytes), desc.SizesStrideBytes };
		// CLAS and templates can be referenced through GPU addresses in other buffers.
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		RecordBarriers(pending, 1, &barrier);
		NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS execute{};
		execute.version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER;
		execute.pDesc = &command;
		const NvAPI_Status status = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(pending.RayList.Get(), &execute);

		if (status != NVAPI_OK)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Cluster", "The driver rejected the cluster operation."));
		}
		RecordBarriers(pending, 1, &barrier);

		return Result<void>::Success();
	}
#else
	Result<ClusterOperationSizeInfo> GetClusterOperationSizeInfo(const State*, const ClusterOperationParams&)
	{
		return Result<ClusterOperationSizeInfo>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Cluster", "Cluster operations require NVAPI support.");
	}

	Result<void> RecordClusterOperation(State*, void*, const ClusterOperationDesc&, void*, void*, void*, void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Cluster", "Cluster operations require NVAPI support.");
	}
#endif
}
#endif
