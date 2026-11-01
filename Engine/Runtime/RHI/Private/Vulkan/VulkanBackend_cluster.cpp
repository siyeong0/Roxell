#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] bool PrepareClusterInput(
		const ClusterOperationParams& params,
		VkClusterAccelerationStructureInputInfoNV& input,
		VkClusterAccelerationStructureClustersBottomLevelInputNV& bottom,
		VkClusterAccelerationStructureTriangleClusterInputNV& triangle,
		VkClusterAccelerationStructureMoveObjectsInputNV& move) noexcept
	{
		input = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
		input.maxAccelerationStructureCount = params.MaxArgCount;
		input.opMode = static_cast<VkClusterAccelerationStructureOpModeNV>(params.Mode);

		if ((static_cast<uint8>(params.Flags) & ~uint8{ 15 }) != 0 ||
			params.MaxArgCount == 0 ||
			params.Mode > EClusterOperationMode::GetSizes ||
			params.Type > EClusterOperationType::BlasBuild)
		{
			return false;
		}

		const uint8 flags = static_cast<uint8>(params.Flags);

		if ((flags & 3) == 3)
		{
			return false;
		}
		// Revision 4 adds this flag without changing the revision 3 structure ABI.
		constexpr VkBuildAccelerationStructureFlagsKHR ALLOW_CLUSTER_OPACITY_MICROMAPS = 0x00001000;

		if ((flags & 8) != 0)
		{
			input.flags |= ALLOW_CLUSTER_OPACITY_MICROMAPS;
		}

		if ((flags & 1) != 0)
		{
			input.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		}

		if ((flags & 2) != 0)
		{
			input.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
		}

		switch (params.Type)
		{
		case EClusterOperationType::Move:
			if (params.MoveType > EClusterMoveType::Template || params.MaxMovedBytes == 0)
			{
				return false;
			}
			input.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV;
			move = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_MOVE_OBJECTS_INPUT_NV };
			move.type = static_cast<VkClusterAccelerationStructureTypeNV>(params.MoveType);
			move.noMoveOverlap = (flags & 4) != 0 ? VK_TRUE : VK_FALSE;
			move.maxMovedBytes = params.MaxMovedBytes;
			input.opInput.pMoveObjects = &move;
			break;
		case EClusterOperationType::BlasBuild:
			if (params.MaxClasPerBlasCount == 0 || params.MaxTotalClasCount == 0 || params.MaxClasPerBlasCount > params.MaxTotalClasCount)
			{
				return false;
			}
			input.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
			bottom = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV };
			bottom.maxClusterCountPerAccelerationStructure = params.MaxClasPerBlasCount;
			bottom.maxTotalClusterCount = params.MaxTotalClasCount;
			input.opInput.pClustersBottomLevel = &bottom;
			break;
		default:
			if (params.MaxTriangleCount == 0 ||
				params.MaxVertexCount == 0 ||
				params.VertexFormat >= EFormat::COUNT ||
				params.MaxUniqueGeometryCount == 0 ||
				params.MinPositionTruncateBitCount > 32 ||
				params.MaxTotalTriangleCount == 0 ||
				params.MaxTotalVertexCount == 0 ||
				params.MaxTriangleCount > params.MaxTotalTriangleCount ||
				params.MaxVertexCount > params.MaxTotalVertexCount)
			{
				return false;
			}
			input.opType = params.Type == EClusterOperationType::ClasBuild
				? VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV
				: params.Type == EClusterOperationType::ClasBuildTemplates
				? VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV
				: VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV;
			triangle = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };
			triangle.vertexFormat = ConvertVulkanFormat(params.VertexFormat);
			triangle.maxGeometryIndexValue = params.MaxGeometryIndex;
			triangle.maxClusterUniqueGeometryCount = params.MaxUniqueGeometryCount;
			triangle.maxClusterTriangleCount = params.MaxTriangleCount;
			triangle.maxClusterVertexCount = params.MaxVertexCount;
			triangle.maxTotalTriangleCount = params.MaxTotalTriangleCount;
			triangle.maxTotalVertexCount = params.MaxTotalVertexCount;
			triangle.minPositionTruncateBitCount = params.MinPositionTruncateBitCount;
			input.opInput.pTriangleClusters = &triangle;
			break;
		}

		return true;
	}

	[[nodiscard]] Result<ClusterOperationSizeInfo> GetClusterOperationSizeInfo(const State* opaque, const ClusterOperationParams& params)
	{
		const State& state = *opaque;

		if (!state.bRayTracingClusters || !state.F.GetClusterBuildSizes)
		{
			return Result<ClusterOperationSizeInfo>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCluster", "Cluster operations are unavailable.");
		}

		if ((static_cast<uint8>(params.Flags) & 8) != 0 && (!state.bOpacityMicromap || state.ClusterExtensionRevision < 4))
		{
			return Result<ClusterOperationSizeInfo>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCluster", "Cluster opacity micromaps require micromaps and cluster extension revision 4.");
		}
		VkClusterAccelerationStructureInputInfoNV input{};
		VkClusterAccelerationStructureClustersBottomLevelInputNV bottom{};
		VkClusterAccelerationStructureTriangleClusterInputNV triangle{};
		VkClusterAccelerationStructureMoveObjectsInputNV move{};
		const bool bValidClusterInput = PrepareClusterInput(params, input, bottom, triangle, move);
		ASSERT(bValidClusterInput, "Cluster parameters must satisfy the native input contract.");

		if (params.Type != EClusterOperationType::Move &&
			params.Type != EClusterOperationType::BlasBuild &&
			(params.MaxVertexCount > state.ClusterProperties.maxVerticesPerCluster ||
			 params.MaxTriangleCount > state.ClusterProperties.maxTrianglesPerCluster ||
			 params.MaxGeometryIndex > state.ClusterProperties.maxClusterGeometryIndex))
		{
			return Result<ClusterOperationSizeInfo>::Failure(EErrorCode::InvalidArgument, "Rhi.ClusterLimits", "Cluster limits exceed the device properties.");
		}

		if (params.Type != EClusterOperationType::Move && params.Type != EClusterOperationType::BlasBuild)
		{
			VkFormatProperties formatProperties{};
			state.F.GetFormatProperties(state.Physical, ConvertVulkanFormat(params.VertexFormat), &formatProperties);

			if ((formatProperties.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR) == 0)
			{
				return Result<ClusterOperationSizeInfo>::Failure(EErrorCode::Unsupported, "Rhi.ClusterVertexFormat", "The device cannot use this cluster vertex format.");
			}
		}
		VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		state.F.GetClusterBuildSizes(state.Device, &input, &sizes);
		const uint64 alignment = params.Type == EClusterOperationType::BlasBuild || (params.Type == EClusterOperationType::Move &&
			params.MoveType == EClusterMoveType::BottomLevel)
			? state.ClusterProperties.clusterBottomLevelByteAlignment
			: params.Type == EClusterOperationType::ClasBuildTemplates ||
			(params.Type == EClusterOperationType::Move && params.MoveType == EClusterMoveType::Template)
			? state.ClusterProperties.clusterTemplateByteAlignment
			: state.ClusterProperties.clusterByteAlignment;

		if (alignment == 0 || state.ClusterProperties.clusterScratchByteAlignment == 0)
		{
			return Result<ClusterOperationSizeInfo>::Failure(Failure("Rhi.VulkanCluster", "The device returned invalid cluster alignment properties."));
		}

		return Result<ClusterOperationSizeInfo>::Success(
			{ sizes.accelerationStructureSize, sizes.buildScratchSize, alignment, state.ClusterProperties.clusterScratchByteAlignment });
	}

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

		if (!state.bRayTracingClusters || !state.F.CmdBuildClusters || pending.QueueKind == EQueue::Copy)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanCluster", "This command list cannot execute cluster operations.");
		}
		VkClusterAccelerationStructureCommandsInfoNV command{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV };
		VkClusterAccelerationStructureClustersBottomLevelInputNV bottom{};
		VkClusterAccelerationStructureTriangleClusterInputNV triangle{};
		VkClusterAccelerationStructureMoveObjectsInputNV move{};
		const bool bValidClusterInput = PrepareClusterInput(desc.Params, command.input, bottom, triangle, move);
		ASSERT(bValidClusterInput, "Cluster parameters must satisfy the native input contract.");

		auto address = [&](void* native, uint64 offset) -> VkDeviceAddress
			{
				if (!native)
				{
					return 0;
				}
				VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
				info.buffer = static_cast<Buffer*>(native)->Handle;

				return state.F.GetBufferDeviceAddress(state.Device, &info) + offset;
			};
		command.dstImplicitData = desc.Params.Mode == EClusterOperationMode::ImplicitDestinations ? address(nativeResult, desc.ResultOffsetBytes) : 0;
		command.scratchData = address(nativeScratch, desc.ScratchOffsetBytes);
		command.dstAddressesArray = { address(nativeAddresses, desc.AddressesOffsetBytes), desc.AddressesStrideBytes,
									 desc.AddressesStrideBytes * desc.Params.MaxArgCount };
		command.dstSizesArray = { address(nativeSizes, desc.SizesOffsetBytes), desc.SizesStrideBytes, desc.SizesStrideBytes * desc.Params.MaxArgCount };
		command.srcInfosArray = { address(nativeArgs, desc.IndirectArgsOffsetBytes), desc.IndirectArgsStrideBytes,
								 desc.IndirectArgsStrideBytes * desc.Params.MaxArgCount };
		command.srcInfosCount = address(nativeCount, desc.IndirectArgCountOffsetBytes);
		VkMemoryBarrier before{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		before.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
			VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1,
			&before, 0, nullptr, 0, nullptr);
		state.F.CmdBuildClusters(pending.Commands, &command);
		VkMemoryBarrier after{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		after.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		after.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
		RecordBarriers(state, pending, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
			&after, 0, nullptr, 0, nullptr);

		return Result<void>::Success();
	}
}
