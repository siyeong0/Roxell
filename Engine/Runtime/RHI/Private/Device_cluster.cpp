#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::reserveClusterMicromapReferences(BufferSlot& target, Span<const OpacityMicromapHandle> handles)
	{
		if (!target.ClusterMicromapReferences.Reserve(target.ClusterMicromapReferences.Size() + handles.Size()))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain cluster micromaps.");
		}

		const auto retained = [&](OpacityMicromapHandle handle) noexcept
			{
				for (OpacityMicromapHandle prior : target.ClusterMicromapReferences)
				{
					if (prior == handle)
					{
						return true;
					}
				}

				return false;
			};
		for (OpacityMicromapHandle handle : handles)
		{
			if (retained(handle))
			{
				continue;
			}

			const OpacityMicromapSlot& micromap = m_State->m_OpacityMicromaps[handle.Slot];
			usize storageReferences = 0;

			for (OpacityMicromapHandle other : handles)
			{
				if (!retained(other) && m_State->m_OpacityMicromaps[other.Slot].Storage == micromap.Storage)
				{
					++storageReferences;
				}
			}

			if (micromap.AccelStructRefs == UINT32_MAX || storageReferences > UINT32_MAX - m_State->m_Buffers[micromap.Storage.Slot].BindingSetRefs)
			{
				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.ClusterMicromap", "Too many references retain the cluster micromap.");
			}
		}

		return Result<void>::Success();
	}

	void Device::retainClusterMicromapReferences(BufferSlot& target, Span<const OpacityMicromapHandle> handles)
	{
		// Keep dependencies through rebuilds and discarded recordings until buffer destruction.
		for (OpacityMicromapHandle handle : handles)
		{
			bool bRetained = false;

			for (OpacityMicromapHandle prior : target.ClusterMicromapReferences)
			{
				if (prior == handle)
				{
					bRetained = true;
					break;
				}
			}

			if (bRetained)
			{
				continue;
			}
			OpacityMicromapSlot& micromap = m_State->m_OpacityMicromaps[handle.Slot];
			(void)target.ClusterMicromapReferences.Add(handle);
			++micromap.AccelStructRefs;
			++m_State->m_Buffers[micromap.Storage.Slot].BindingSetRefs;
		}
	}

	[[nodiscard]] Result<ClusterOperationSizeInfo> Device::GetClusterOperationSizeInfo(const ClusterOperationParams& params) const
	{
		CallScope call(*this, true);

		if (!m_State->m_Backend.bRayTracingClusters || backend::HAS_GET_CLUSTER_OPERATION_SIZE_INFO == false)
		{
			return Result<ClusterOperationSizeInfo>::Failure(EErrorCode::Unsupported, "Rhi.Cluster", "Cluster operations are unavailable.");
		}

		return backend::GetClusterOperationSizeInfo(m_State->m_Backend.State, params);
	}

	[[nodiscard]] Result<void> Device::ExecuteMultiIndirectClusterOperation(CommandListHandle list, const ClusterOperationDesc& desc)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		if (!m_State->m_Backend.bRayTracingClusters || backend::HAS_RECORD_CLUSTER_OPERATION == false)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.Cluster", "Cluster operations are unavailable.");
		}

		ASSERT(!(command->Queue == EQueue::Copy || command->bRendering), "Cluster operations need a graphics or compute list outside rendering.");

		Result<ClusterOperationSizeInfo> sizes = GetClusterOperationSizeInfo(desc.Params);

		if (!sizes)
		{
			ASSERT(sizes.GetError().Code != EErrorCode::InvalidArgument, "Cluster operation parameters must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(sizes.GetError());
		}

		const EClusterOperationMode mode = desc.Params.Mode;
		const bool bNeedsResult = mode != EClusterOperationMode::GetSizes;
		const bool bNeedsAddresses = mode != EClusterOperationMode::GetSizes;
		const bool bNeedsSizes = true;

		ASSERT(!(desc.ResultByteCount != 0 && (mode != EClusterOperationMode::ExplicitDestinations || desc.Params.Type == EClusterOperationType::Move)),
			"A queried result extent requires an explicit build operation.");

		const uint64 requiredResultBytes = desc.ResultByteCount != 0 ? desc.ResultByteCount : sizes.Value().ResultMaxSizeInBytes;
		const uint64 requiredArgSize = [&]() -> uint64
			{
				switch (desc.Params.Type)
				{
				case EClusterOperationType::Move:
					return sizeof(ClusterMoveInfo);
				case EClusterOperationType::BlasBuild:
					return sizeof(ClusterBlasBuildInfo);
				case EClusterOperationType::ClasBuild:
					return sizeof(ClusterTriangleBuildInfo);
				case EClusterOperationType::ClasBuildTemplates:
					return sizeof(ClusterTriangleTemplateBuildInfo);
				case EClusterOperationType::ClasInstantiateTemplates:
					return sizeof(ClusterInstantiateInfo);

				default:
				{
					break;
				}
				}

				return 0;
			}();
		BufferSlot* const scratch = findBuffer(desc.Scratch);
		BufferSlot* const count = findBuffer(desc.IndirectArgCount);
		BufferSlot* const args = findBuffer(desc.IndirectArgs);
		BufferSlot* const addresses = bNeedsAddresses ? findBuffer(desc.Addresses) : nullptr;
		BufferSlot* const outputSizes = bNeedsSizes ? findBuffer(desc.Sizes) : nullptr;
		BufferSlot* const result = bNeedsResult ? findBuffer(desc.Result) : nullptr;

		ASSERT(!(!scratch || !count || !args || (bNeedsAddresses && !addresses) || (bNeedsSizes && !outputSizes) || (bNeedsResult && !result)),
			"RHI operations require valid handles for this device.");

		const auto validInput = [](const BufferSlot* slot) noexcept
			{
				return slot->Memory == EBufferMemory::Device && !slot->bMapped && HasUsage(slot->Usage, EBufferUsage::AccelBuildInput) && HasUsage(slot->Usage, EBufferUsage::DeviceAddress);
			};
		const auto validOutput = [](const BufferSlot* slot) noexcept
			{
				return slot->Memory == EBufferMemory::Device && !slot->bMapped && HasUsage(slot->Usage, EBufferUsage::Storage) && HasUsage(slot->Usage, EBufferUsage::DeviceAddress);
			};
		const auto inRange =
			[](const BufferSlot* slot, uint64 offset, uint64 stride, uint64 count, uint64 element) noexcept
			{
				return stride >= element &&
					offset <= slot->ByteCount &&
					count != 0 &&
					(count - 1) <= (slot->ByteCount - offset - ((slot->ByteCount - offset) >= element ? element : 0)) / stride &&
					element <= slot->ByteCount - offset;
			};
		ASSERT(!(!validOutput(scratch) ||
				!validInput(count) ||
				!validInput(args) ||
				(bNeedsAddresses && !validOutput(addresses)) ||
				(bNeedsSizes && !validOutput(outputSizes)) ||
				(bNeedsResult &&
				 (result->Memory != EBufferMemory::Device || result->bMapped || !HasUsage(result->Usage, EBufferUsage::AccelStorage) || !HasUsage(result->Usage, EBufferUsage::DeviceAddress))) ||
				desc.IndirectArgCountOffsetBytes % 4 != 0 ||
				desc.IndirectArgCountOffsetBytes > count->ByteCount ||
				count->ByteCount - desc.IndirectArgCountOffsetBytes < 4 ||
				desc.IndirectArgsOffsetBytes % 8 != 0 ||
				desc.IndirectArgsStrideBytes % 8 != 0 ||
				desc.IndirectArgsStrideBytes <= requiredArgSize ||
				!inRange(args, desc.IndirectArgsOffsetBytes, desc.IndirectArgsStrideBytes, desc.Params.MaxArgCount, requiredArgSize) ||
				(bNeedsAddresses &&
				 (desc.AddressesOffsetBytes % 8 != 0 ||
				  desc.AddressesStrideBytes % 8 != 0 ||
				  !inRange(addresses, desc.AddressesOffsetBytes, desc.AddressesStrideBytes, desc.Params.MaxArgCount, 8))) ||
				(bNeedsSizes && (desc.SizesOffsetBytes % 4 != 0 || desc.SizesStrideBytes % 4 != 0 || !inRange(outputSizes, desc.SizesOffsetBytes, desc.SizesStrideBytes, desc.Params.MaxArgCount, 4))) ||
				(sizes.Value().ScratchSizeInBytes != 0 && (desc.ScratchOffsetBytes > scratch->ByteCount || sizes.Value().ScratchSizeInBytes > scratch->ByteCount - desc.ScratchOffsetBytes)) ||
				(bNeedsResult &&
				 (desc.ResultOffsetBytes > result->ByteCount ||
				  requiredResultBytes > result->ByteCount - desc.ResultOffsetBytes ||
				  (desc.ResultByteCount != 0 && desc.ResultByteCount % sizes.Value().ResultAlignmentBytes != 0)))),
			"Cluster buffer usage, alignment, or range is invalid.");

		const auto address = [&](BufferSlot* slot) -> uint64
			{ return backend::GetBufferDeviceAddress(m_State->m_Backend.State, slot->Native); };
		const uint64 scratchAddress = address(scratch);
		const uint64 resultAddress = bNeedsResult ? address(result) : 0;
		ASSERT(scratchAddress != 0 &&
			(scratchAddress + desc.ScratchOffsetBytes) % sizes.Value().ScratchAlignmentBytes == 0 &&
			(!bNeedsResult || (resultAddress + desc.ResultOffsetBytes) % sizes.Value().ResultAlignmentBytes == 0), "Cluster storage is misaligned.");
		AccelStructSlot* const blas = desc.ResultBlas.IsValid() ? findAccelStruct(desc.ResultBlas) : nullptr;

		ASSERT(!(desc.ResultBlas.IsValid() &&
				(blas == nullptr ||
				 blas->Type != EAccelStructType::ClusterBottomLevel ||
				 desc.Params.Type != EClusterOperationType::BlasBuild ||
				 mode != EClusterOperationMode::ExplicitDestinations ||
				 blas->Storage != desc.Result ||
				 blas->ByteOffset != desc.ResultOffsetBytes ||
				 blas->ByteCount < requiredResultBytes)), "Cluster BLAS result handle or range is invalid.");
		if (blas && !canBuildAfterStateRequests(*command, desc.ResultBlas, blas->RecordedUses))
		{
			poisonCommandList(list);

			return preparedFailure(EErrorCode::Conflict);
		}

		if ((blas && desc.ReferencedBuffers.IsEmpty()) || (bNeedsResult && desc.Params.Type != EClusterOperationType::BlasBuild && result->BindingSetRefs != 0))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.ClusterResult", "A referenced cluster result cannot be rebuilt.");
		}

		const auto alreadyRetained = [&](BufferHandle handle, usize index) noexcept
			{
				if (!blas)
				{
					return false;
				}

				for (BufferHandle prior : blas->ClusterReferences)
				{
					if (prior == handle)
					{
						return true;
					}
				}

				for (usize prior = 0; prior < index; ++prior)
				{
					if (desc.ReferencedBuffers[prior] == handle)
					{
						return true;
					}
				}

				return false;
			};
		usize newReferences = 0;
		Array<OpacityMicromapHandle> micromapReferences(m_State->m_Resource);
		const auto collectMicromap = [&](OpacityMicromapHandle handle) -> bool
			{
				for (OpacityMicromapHandle prior : micromapReferences)
				{
					if (prior == handle)
					{
						return true;
					}
				}

				return micromapReferences.Add(handle);
			};
		for (OpacityMicromapHandle handle : desc.ReferencedMicromaps)
		{
			if (!collectMicromap(handle))
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not collect cluster micromap references.");
			}
		}

		for (usize index = 0; index < desc.ReferencedBuffers.Size(); ++index)
		{
			const BufferHandle reference = desc.ReferencedBuffers[index];
			const BufferSlot* const slot = findBuffer(reference);

			ASSERT(!(!slot ||
					!HasUsage(slot->Usage, EBufferUsage::DeviceAddress) ||
					(!HasUsage(slot->Usage, EBufferUsage::AccelBuildInput) && !HasUsage(slot->Usage, EBufferUsage::AccelStorage)) ||
					slot->Memory != EBufferMemory::Device),
				"A referenced input buffer is invalid.");

			if (blas && !alreadyRetained(reference, index) && slot->BindingSetRefs == UINT32_MAX)
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.ClusterReference", "Too many persistent cluster BLAS references.");
			}

			if (blas && !alreadyRetained(reference, index))
			{
				++newReferences;
			}

			for (OpacityMicromapHandle handle : slot->ClusterMicromapReferences)
			{
				if (!collectMicromap(handle))
				{
					poisonCommandList(list);

					return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not inherit cluster micromap references.");
				}
			}
		}

		if (!micromapReferences.IsEmpty() && !m_State->m_Backend.bOpacityMicromap)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ClusterMicromap", "Cluster opacity micromaps are unavailable.");
		}

		ASSERT(!(!micromapReferences.IsEmpty() && (static_cast<uint8>(desc.Params.Flags) & 8) == 0), "Cluster micromap references require the opacity micromap operation flag.");

		const auto overlaps = [&](const BufferSlot* left, const BufferSlot* right) noexcept
			{
				if (left == nullptr || right == nullptr)
				{
					return false;
				}

				if (left == right)
				{
					return true;
				}

				const void* const native = backend::GetNativeBuffer(left->Native);

				if (native != nullptr && native == backend::GetNativeBuffer(right->Native))
				{
					return true;
				}

				if (!left->Heap.IsValid() || left->Heap != right->Heap)
				{
					return false;
				}

				return left->HeapOffset < right->HeapOffset + right->AllocationSize && right->HeapOffset < left->HeapOffset + left->AllocationSize;
			};
		ASSERT(!(overlaps(scratch, count) ||
				overlaps(scratch, args) ||
				overlaps(scratch, addresses) ||
				overlaps(scratch, outputSizes) ||
				overlaps(scratch, result) ||
				overlaps(result, count) ||
				overlaps(result, args) ||
				overlaps(result, addresses) ||
				overlaps(result, outputSizes) ||
				overlaps(addresses, outputSizes) ||
				overlaps(addresses, args) ||
				overlaps(addresses, count) ||
				overlaps(outputSizes, args) ||
				overlaps(outputSizes, count)),
			"Cluster output, scratch, and indirect data overlap.");

		const auto prepared = [&](BufferHandle handle, const BufferSlot* slot, ETrackedResourceState state) noexcept
			{
				return (slot->PermanentState == ETrackedResourceState::Unknown || slot->PermanentState == state) && (command->bAutomaticBarriers || command->HasBufferState(handle, state));
			};
		if (!prepared(desc.Scratch, scratch, ETrackedResourceState::Storage) ||
			!prepared(desc.IndirectArgCount, count, ETrackedResourceState::AccelBuildInput) ||
			!prepared(desc.IndirectArgs, args, ETrackedResourceState::AccelBuildInput) ||
			(bNeedsAddresses && !prepared(desc.Addresses, addresses, ETrackedResourceState::Storage)) ||
			(bNeedsSizes && !prepared(desc.Sizes, outputSizes, ETrackedResourceState::Storage)) ||
			(bNeedsResult && !prepared(desc.Result, result, ETrackedResourceState::AccelStorage)))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.ClusterState", "Prepare cluster buffer states before recording.");
		}

		for (BufferHandle handle : desc.ReferencedBuffers)
		{
			const BufferSlot* const slot = findBuffer(handle);
			const ETrackedResourceState state = HasUsage(slot->Usage, EBufferUsage::AccelStorage) ? ETrackedResourceState::AccelStorage : ETrackedResourceState::AccelBuildInput;

			if (!prepared(handle, slot, state) || overlaps(slot, scratch) || overlaps(slot, result) || overlaps(slot, addresses) || overlaps(slot, outputSizes))
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.ClusterReference", "A cluster reference aliases output or has an invalid state.");
			}
		}

		for (OpacityMicromapHandle handle : micromapReferences)
		{
			const OpacityMicromapSlot* const micromap = findOpacityMicromap(handle);

			ASSERT(!(!micromap || !micromap->bBuilt), "A cluster micromap reference is invalid or unbuilt.");

			const BufferSlot* const storage = findBuffer(micromap->Storage);

			if (!storage ||
				!prepared(micromap->Storage, storage, ETrackedResourceState::AccelStorage) ||
				overlaps(storage, scratch) ||
				overlaps(storage, result) ||
				overlaps(storage, addresses) ||
				overlaps(storage, outputSizes) ||
				overlaps(storage, count) ||
				overlaps(storage, args))
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.ClusterMicromap", "Micromap storage aliases cluster data or has an invalid state.");
			}
		}

		if (result)
		{
			Result<void> reservedMicromaps =
				reserveClusterMicromapReferences(*result, { micromapReferences.Data(), micromapReferences.Size() });
			if (!reservedMicromaps)
			{
				poisonCommandList(list);

				return reservedMicromaps;
			}
		}

		if (blas && !blas->ClusterReferences.Reserve(blas->ClusterReferences.Size() + newReferences))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain cluster BLAS references.");
		}

		const uint64 bufferUses = 3 + (bNeedsAddresses ? 1 : 0) + (bNeedsSizes ? 1 : 0) + (bNeedsResult ? 1 : 0) + desc.ReferencedBuffers.Size() + micromapReferences.Size() + (blas ? 1 : 0);

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + bufferUses) ||
			!command->OpacityMicromapUses.Reserve(command->OpacityMicromapUses.Size() + micromapReferences.Size()) ||
			(blas && (!command->AccelUses.Reserve(command->AccelUses.Size() + 1) || !command->AccelBuildTargets.Reserve(command->AccelBuildTargets.Size() + 1))))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track cluster operation resources.");
		}
		const auto track = [&](BufferHandle handle, BufferSlot* slot, ETrackedResourceState state) {
			(void)command->AddBufferUse({ handle, slot->Native, state });
			++slot->RecordedUses;
			};
		track(desc.Scratch, scratch, ETrackedResourceState::Storage);
		track(desc.IndirectArgCount, count, ETrackedResourceState::AccelBuildInput);
		track
		(desc.IndirectArgs, args, ETrackedResourceState::AccelBuildInput);
		if (bNeedsAddresses)
		{
			track(desc.Addresses, addresses, ETrackedResourceState::Storage);
		}
		if (bNeedsSizes)
		{
			track(desc.Sizes, outputSizes, ETrackedResourceState::Storage);
		}
		if (bNeedsResult)
		{
			track(desc.Result, result, ETrackedResourceState::AccelStorage);
		}

		for (OpacityMicromapHandle handle : micromapReferences)
		{
			OpacityMicromapSlot& micromap = m_State->m_OpacityMicromaps[handle.Slot];
			track(micromap.Storage, &m_State->m_Buffers[micromap.Storage.Slot], ETrackedResourceState::AccelStorage);
			(void)command->OpacityMicromapUses.Add(handle);
			++micromap.RecordedUses;
		}
		for (BufferHandle handle : desc.ReferencedBuffers)
		{
			BufferSlot* const slot = findBuffer(handle);
			track(handle, slot, HasUsage(slot->Usage, EBufferUsage::AccelStorage) ? ETrackedResourceState::AccelStorage : ETrackedResourceState::AccelBuildInput);
		}

		if (blas)
		{
			track(blas->Storage, findBuffer(blas->Storage), ETrackedResourceState::AccelStorage);
			(void)command->AccelUses.Add({ desc.ResultBlas, blas->Native }); (void)command->AccelBuildTargets.Add({ desc.ResultBlas, {}, false, false });
			++blas->RecordedUses;
		}

		if (command->bAutomaticBarriers)
		{
			for (BufferHandle handle : desc.ReferencedBuffers)
			{
				const BufferSlot* const reference = findBuffer(handle);
				Result<void> preparedNative = backend::RecordSetBufferState(
					m_State->m_Backend.State, command->Native, reference->Native,
					HasUsage(reference->Usage, EBufferUsage::AccelStorage) ? EBufferState::AccelStorage
					: EBufferState::AccelBuildInput);

				if (!preparedNative)
				{
					poisonCommandList(list);

					return preparedNative;
				}
			}

			for (OpacityMicromapHandle handle : micromapReferences)
			{
				const BufferHandle storage = m_State->m_OpacityMicromaps[handle.Slot].Storage;
				Result<void> preparedNative = backend::RecordSetBufferState( m_State->m_Backend.State, command->Native, m_State->m_Buffers[storage.Slot].Native, EBufferState::AccelStorage);

				if
					(!preparedNative)
				{
					poisonCommandList(list);

					return preparedNative;
				}
			}
		}

		Result<void> recorded = backend::RecordClusterOperation(
			m_State->m_Backend.State, command->Native, desc, scratch->Native, count->Native, args->Native,
			addresses ? addresses->Native : nullptr, outputSizes ? outputSizes->Native : nullptr,
			result ? result->Native : nullptr);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		if (result)
		{
			retainClusterMicromapReferences(*result, { micromapReferences.Data(), micromapReferences.Size() });
		}

		for (usize index = 0; index < desc.ReferencedBuffers.Size(); ++index)
		{
			const BufferHandle handle = desc.ReferencedBuffers[index];
			BufferSlot* const slot = findBuffer(handle);

			if (blas && !alreadyRetained(handle, index))
			{
				(void)blas->ClusterReferences.Add(handle);
				++slot->BindingSetRefs;
			}
		}

		return Result<void>::Success();
	}
}
