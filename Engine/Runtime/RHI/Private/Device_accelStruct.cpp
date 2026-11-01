#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::reserveMicromapReferences(AccelStructSlot& target, Span<const OpacityMicromapHandle> handles)
	{
		if (!target.OpacityMicromapReferences.Reserve(target.OpacityMicromapReferences.Size() + handles.Size()) || !target.ClusterReferences.Reserve(target.ClusterReferences.Size() + handles.Size()))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain acceleration structure micromaps.");
		}

		for (OpacityMicromapHandle handle : handles)
		{
			const OpacityMicromapSlot& micromap = m_State->m_OpacityMicromaps[handle.Slot];
			bool bRetained = false;

			for (OpacityMicromapHandle prior : target.OpacityMicromapReferences)
			{
				if (prior == handle)
				{
					bRetained = true;
					break;
				}
			}

			if (!bRetained && micromap.AccelStructRefs == UINT32_MAX)
			{
				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.MicromapReference", "Too many acceleration structures reference the micromap.");
			}
			bRetained = false;

			for (BufferHandle prior : target.ClusterReferences)
			{
				if (prior == micromap.Storage)
				{
					bRetained = true;
					break;
				}
			}

			if (!bRetained && m_State->m_Buffers[micromap.Storage.Slot].BindingSetRefs == UINT32_MAX)
			{
				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.MicromapReference", "Too many references retain the micromap storage.");
			}
		}

		return Result<void>::Success();
	}

	void Device::retainMicromapReferences(AccelStructSlot& target, Span<const OpacityMicromapHandle> handles)
	{
		// Retain dependencies through rebuilds and discarded recordings until target destruction.
		for (OpacityMicromapHandle handle : handles)
		{
			OpacityMicromapSlot& micromap = m_State->m_OpacityMicromaps[handle.Slot];
			bool bRetained = false;

			for (OpacityMicromapHandle prior : target.OpacityMicromapReferences)
			{
				if (prior == handle)
				{
					bRetained = true;
					break;
				}
			}

			if (!bRetained)
			{
				(void)target.OpacityMicromapReferences.Add(handle);
				++micromap.AccelStructRefs;
			}
			bRetained = false;

			for (BufferHandle prior : target.ClusterReferences)
			{
				if (prior == micromap.Storage)
				{
					bRetained = true;
					break;
				}
			}

			if (!bRetained)
			{
				(void)target.ClusterReferences.Add(micromap.Storage);
				++m_State->m_Buffers[micromap.Storage.Slot].BindingSetRefs;
			}
		}
	}

	[[nodiscard]] AccelBuildSignature Device::makeAccelBuildSignature(const AccelStructBuildDesc& desc) noexcept
	{
		AccelBuildSignature signature{ 14695981039346656037ull, 7809847782465536322ull };
		const auto add = [&signature](uint64 value) noexcept
			{
				signature.First = (signature.First ^ value) * 1099511628211ull;
				signature.Second = (signature.Second ^ (value + 0x9e3779b97f4a7c15ull)) * 14029467366897019727ull;
			};
		add(static_cast<uint64>(desc.Type));
		add(desc.bAllowUpdate);
		add(desc.bAllowCompaction);
		add(desc.InstanceCount);
		add(desc.Triangles.Size());

		for (const AccelTriangleGeometryDesc& triangle : desc.Triangles)
		{
			add(triangle.VertexStrideBytes);
			add(triangle.VertexCount);
			add(triangle.IndexBuffer.IsValid());
			add(triangle.IndexCount);
			add(static_cast<uint64>(triangle.IndexFormat));
			add(triangle.bOpaque);
			add(triangle.OpacityMicromap.Owner);
			add(triangle.OpacityMicromap.Slot);
			add(triangle.OpacityMicromap.Generation);
			add(triangle.OpacityMicromapIndexBuffer.IsValid());
			add(triangle.OpacityMicromapIndexStrideBytes);
			add(static_cast<uint64>(triangle.OpacityMicromapIndexFormat));
			add(triangle.OpacityMicromapBaseTriangle);
			add(triangle.OpacityMicromapUsageCounts.Size());

			for (const OpacityMicromapUsageCount& count : triangle.OpacityMicromapUsageCounts)
			{
				add(count.Count);
				add(count.SubdivisionLevel);
				add(static_cast<uint64>(count.Format));
			}
		}
		add(desc.Aabbs.Size());

		for (const AccelAabbGeometryDesc& aabb : desc.Aabbs)
		{
			add(aabb.StrideBytes);
			add(aabb.Count);
			add(aabb.bOpaque);
		}

		return signature;
	}

	[[nodiscard]] Result<AccelStructBuildSizes> Device::GetAccelStructBuildSizes(const AccelStructBuildDesc& desc) const
	{
		Result<AccelStructBuildSizes> sizes = getAccelStructBuildSizes(desc);
		ASSERT(sizes || sizes.GetError().Code != EErrorCode::InvalidArgument || sizes.GetError().DiagnosticID != "Rhi.AccelStructBuildDesc",
			"Acceleration structure build geometry, usage, alignment, and ranges must be valid.");

		return sizes;
	}

	[[nodiscard]] Result<AccelStructHandle> Device::CreateAccelStruct(const AccelStructDesc& desc)
	{
		CallScope call(*this, false);

		ASSERT(desc.Type == EAccelStructType::BottomLevel || desc.Type == EAccelStructType::TopLevel || desc.Type == EAccelStructType::ClusterBottomLevel,
			"Acceleration structure creation requires a valid type.");
		BufferSlot* const storage = findBuffer(desc.Storage);
		ASSERT(storage != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(HasUsage(storage->Usage, EBufferUsage::AccelStorage) &&
			desc.ByteOffset % 256 == 0 &&
			desc.ByteCount != 0 &&
			desc.ByteOffset <= storage->ByteCount &&
			desc.ByteCount <= storage->ByteCount - desc.ByteOffset,
			"Acceleration structure storage requires valid usage, alignment, and range.");

		if (!m_State->m_Backend.bAccelerationStructureBuildInput)
		{
			return Result<AccelStructHandle>::Failure(EErrorCode::Unsupported, "Rhi.AccelStruct", "The device does not support acceleration structures.");
		}

		if (desc.Type == EAccelStructType::ClusterBottomLevel && !m_State->m_Backend.bRayTracingClusters)
		{
			return Result<AccelStructHandle>::Failure(EErrorCode::Unsupported, "Rhi.ClusterAccelStruct", "The device does not support cluster acceleration structures.");
		}

		for (const AccelStructSlot& other : m_State->m_AccelStructs)
		{
			if (!other.bLive || other.Storage != desc.Storage)
			{
				continue;
			}

			if (desc.ByteOffset < other.ByteOffset + other.ByteCount && other.ByteOffset < desc.ByteOffset + desc.ByteCount)
			{
				return Result<AccelStructHandle>::Failure(EErrorCode::Conflict, "Rhi.AccelStructStorage", "Another acceleration structure occupies the storage range.");
			}
		}

		if (storage->BindingSetRefs == NumericLimits<uint32>::max())
		{
			return Result<AccelStructHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.AccelStructStorage", "The storage buffer has too many acceleration structure references.");
		}

		const bool bNewSlot = m_State->m_FreeAccelStruct == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_AccelStructs.Size()) : m_State->m_FreeAccelStruct;

		if (bNewSlot && (m_State->m_AccelStructs.Size() >= NumericLimits<uint32>::max() || !m_State->m_AccelStructs.Add(AccelStructSlot(m_State->m_Resource))))
		{
			return Result<AccelStructHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate an acceleration structure handle slot.");
		}

		Result<void*> created = backend::CreateAccelStruct(m_State->m_Backend.State, desc, storage->Native);

		if (!created)
		{
			if (bNewSlot)
			{
				m_State->m_AccelStructs[index].NextFree = m_State->m_FreeAccelStruct;
				m_State->m_FreeAccelStruct = index;
			}

			return Result<AccelStructHandle>::Failure(created.GetError());
		}
		AccelStructSlot& slot = m_State->m_AccelStructs[index];

		if (!bNewSlot)
		{
			m_State->m_FreeAccelStruct = slot.NextFree;
		}
		slot.Native = created.Value();
		slot.ClusterReferences.Clear();
		slot.ClusterBottomReferences.Clear();
		slot.BottomReferences.Clear();
		slot.OpacityMicromapReferences.Clear();
		slot.ClusterParentRefs = 0;
		slot.Storage = desc.Storage;
		slot.ByteOffset = desc.ByteOffset;
		slot.ByteCount = desc.ByteCount;
		slot.Type = desc.Type;
		slot.bBuilt = false;
		slot.bAllowUpdate = false;
		slot.bAllowCompaction = false;
		slot.CompactedBytes = 0;
		slot.Signature = {};
		slot.BuildRevision = 0;
		slot.QueryGeneration = 0;
		slot.QueryBuildRevision = 0;
		slot.NextQueryGeneration = 1;
		slot.PreparedReferences = 0;
		slot.BindingSetRefs = 0;
		slot.TableClosureRefs = 0;
		slot.bUnavailable = false;
		slot.bQueryUnavailable = false;
		slot.bPreparedBuildShape = false;
		slot.PreparedBuildShape.Clear();
		slot.PreparedBottomReferences.Clear();
		slot.PreparedParentRefs = 0;
		slot.bLive = true;
		slot.NextFree = NumericLimits<uint32>::max();
		++storage->BindingSetRefs;

		return Result<AccelStructHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyAccelStruct(AccelStructHandle handle)
	{
		CallScope call(*this, false);
		AccelStructSlot* const slot = findAccelStruct(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->RecordedUses != 0 || slot->ClusterParentRefs != 0 || slot->PreparedReferences != 0 || slot->BindingSetRefs != 0 || slot->PreparedParentRefs != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.AccelStruct", "The acceleration structure is referenced by a command or cluster TLAS.");
		}

		if constexpr (backend::HAS_PROPAGATE_ACCEL_STRUCT_LAST_USE)
		{
			for (rhi::AccelStructHandle const child : slot->PreparedBottomReferences)
			{
				backend::PropagateAccelStructLastUse(m_State->m_Backend.State, slot->Native, m_State->m_AccelStructs[child.Slot].Native);
			}

			for (AccelStructHandle child : slot->ClusterBottomReferences)
			{
				backend::PropagateAccelStructLastUse(m_State->m_Backend.State, slot->Native, m_State->m_AccelStructs[child.Slot].Native);
			}
		}

		if constexpr (backend::HAS_PROPAGATE_ACCEL_STRUCT_LAST_USE_TO_BUFFER)
		{
			backend::PropagateAccelStructLastUseToBuffer(m_State->m_Backend.State, slot->Native, m_State->m_Buffers[slot->Storage.Slot].Native);

			for (BufferHandle reference : slot->ClusterReferences)
			{
				backend::PropagateAccelStructLastUseToBuffer(m_State->m_Backend.State, slot->Native, m_State->m_Buffers[reference.Slot].Native);
			}
		}

		Result<void> destroyed = backend::DestroyAccelStruct(m_State->m_Backend.State, slot->Native);

		if (!destroyed)
		{
			return destroyed;
		}

		for (AccelStructHandle child : slot->ClusterBottomReferences)
		{
			--m_State->m_AccelStructs[child.Slot].ClusterParentRefs;
		}
		slot->ClusterBottomReferences.Clear();
		for (rhi::AccelStructHandle const child : slot->PreparedBottomReferences)
		{
			--m_State->m_AccelStructs[child.Slot].PreparedParentRefs;
		}
		slot->PreparedBottomReferences.Clear();

		for (OpacityMicromapHandle micromap : slot->OpacityMicromapReferences)
		{
			--m_State->m_OpacityMicromaps[micromap.Slot].AccelStructRefs;
		}
		slot->OpacityMicromapReferences.Clear();

		for (BufferHandle reference : slot->ClusterReferences)
		{
			--m_State->m_Buffers[reference.Slot].BindingSetRefs;
		}
		slot->ClusterReferences.Clear();
		--m_State->m_Buffers[slot->Storage.Slot].BindingSetRefs;
		slot->Native = nullptr;
		slot->Storage = {};
		slot->ByteOffset = 0;
		slot->ByteCount = 0;
		slot->CompactedBytes = 0;
		slot->bBuilt = false;
		slot->bAllowUpdate = false;
		slot->bAllowCompaction = false;
		slot->Signature = {};
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeAccelStruct;
			m_State->m_FreeAccelStruct = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint64> Device::GetAccelStructDeviceAddress(AccelStructHandle handle)
	{
		CallScope call(*this, true);
		AccelStructSlot* const slot = findAccelStruct(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		const uint64 address = backend::GetAccelStructDeviceAddress(m_State->m_Backend.State, slot->Native);

		if (address == 0)
		{
			return Result<uint64>::Failure(EErrorCode::ExternalFailure, "Rhi.AccelStructAddress", "The backend did not return an acceleration structure device address.");
		}

		return Result<uint64>::Success(address);
	}

	[[nodiscard]] Result<uint64> Device::GetBottomLevelAccelStructCompactedSize(AccelStructHandle source)
	{
		CallScope call(*this, false);
		AccelStructSlot* const accel = findAccelStruct(source);

		ASSERT(accel != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(accel->Type == EAccelStructType::BottomLevel && accel->bBuilt && accel->bAllowCompaction, "Compaction size requires a built BLAS that allows compaction.");
		if (accel->RecordedUses != 0)
		{
			return Result<uint64>::Failure(EErrorCode::Conflict, "Rhi.AccelStructCompaction", "Recorded commands still retain the compaction source.");
		}

		if (accel->QueryGeneration != 0)
		{
			Result<AccelCompactionResult> queried = TryGetBottomLevelAccelStructCompactedSize(source);
			if (queried && queried.Value().State == EAccelCompactionQueryState::Ready)
			{
				return Result<uint64>::Success(queried.Value().ByteCount);
			}

			return Result<uint64>::Failure(EErrorCode::Conflict, "Rhi.AccelStructCompaction", "The prepared compaction proof is not ready for this build revision.");
		}
		if (accel->CompactedBytes != 0)
		{
			return Result<uint64>::Success(accel->CompactedBytes);
		}

		if (accel->PreparedReferences != 0)
		{
			return Result<uint64>::Failure(EErrorCode::Conflict, "Rhi.AccelStructCompaction", "A prepared group retains the acceleration structure.");
		}

		if constexpr (backend::HAS_GET_BOTTOM_LEVEL_ACCEL_STRUCT_COMPACTED_SIZE == false)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.AccelStructCompaction", "The backend does not support acceleration structure compaction.");
		}

		Result<uint64> size = backend::GetBottomLevelAccelStructCompactedSize(m_State->m_Backend.State, accel->Native);

		if (!size)
		{
			return size;
		}

		if (size.Value() == 0 || size.Value() > accel->ByteCount)
		{
			return Result<uint64>::Failure(EErrorCode::ExternalFailure, "Rhi.AccelStructCompaction", "The driver returned an invalid compacted size.");
		}
		accel->CompactedBytes = size.Value();

		return size;
	}

	[[nodiscard]] Result<void> Device::CompactBottomLevelAccelStruct(CommandListHandle list, AccelStructHandle source, AccelStructHandle target)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		AccelStructSlot* const original = findAccelStruct(source);
		AccelStructSlot* const compacted = findAccelStruct(target);

		ASSERT(!(command == nullptr || original == nullptr || compacted == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!(command->Queue == EQueue::Copy || command->bRendering), "Compaction requires a graphics or compute command list outside rendering.");

		if (original->bUnavailable || original->PreparedReferences != 0 || compacted->PreparedReferences != 0 || compacted->RecordedUses != 0 || compacted->BuildRevision == UINT64_MAX)
		{
			poisonCommandList(list);

			return preparedFailure(EErrorCode::Conflict);
		}
		ASSERT(!(original->Type != EAccelStructType::BottomLevel ||
				compacted->Type != EAccelStructType::BottomLevel ||
				!original->bBuilt ||
				!original->bAllowCompaction ||
				original->CompactedBytes == 0 ||
				compacted->bBuilt ||
				original->Storage == compacted->Storage ||
				compacted->ByteCount < original->CompactedBytes),
			"The source or destination is not suitable for compaction.");

		if constexpr (backend::HAS_RECORD_COMPACT_BOTTOM_LEVEL_ACCEL_STRUCT == false)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.AccelStructCompaction", "The backend does not support acceleration structure compaction.");
		}

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + 2) ||
			!command->AccelUses.Reserve(command->AccelUses.Size() + 2) ||
			!command->AccelBuildTargets.Reserve(command->AccelBuildTargets.Size() + 1))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track acceleration structure compaction.");
		}

		Result<void> retainedMicromaps =
			reserveMicromapReferences(*compacted, { original->OpacityMicromapReferences.Data(), original->OpacityMicromapReferences.Size() });
		if (!retainedMicromaps)
		{
			poisonCommandList(list);

			return retainedMicromaps;
		}

		Result<void> recorded = backend::RecordCompactBottomLevelAccelStruct(m_State->m_Backend.State, command->Native, original->Native, compacted->Native);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}
		retainMicromapReferences(*compacted, { original->OpacityMicromapReferences.Data(), original->OpacityMicromapReferences.Size() });
		const BufferHandle storages[2]{ original->Storage, compacted->Storage };
		const AccelStructHandle structures[2]{ source, target };
		for (uint32 index = 0; index < 2; ++index)
		{
			BufferSlot& storage = m_State->m_Buffers[storages[index].Slot];
			AccelStructSlot& accel = m_State->m_AccelStructs[structures[index].Slot];
			(void)command->AddBufferUse({ storages[index], storage.Native, ETrackedResourceState::AccelStorage });
			(void)command->AccelUses.Add({ structures[index], accel.Native });
			++storage.RecordedUses;
			++accel.RecordedUses;
		}
		(void)command->AccelBuildTargets.Add({ target, original->Signature, original->bAllowUpdate, original->bAllowCompaction });

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::BuildBottomLevelAccelStruct(
		CommandListHandle list,
		AccelStructHandle target,
		const AccelStructBuildDesc& desc,
		BufferHandle scratch,
		uint64 scratchOffset)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		AccelStructSlot* const accel = findAccelStruct(target);
		BufferSlot* const scratchBuffer = findBuffer(scratch);

		ASSERT(!(command == nullptr || accel == nullptr || scratchBuffer == nullptr), "RHI operations require valid handles for this device.");
		if (accel->PreparedReferences != 0 || accel->TableClosureRefs != 0 || accel->BuildRevision == UINT64_MAX)
		{
			poisonCommandList(list);

			return preparedFailure(EErrorCode::Conflict);
		}

		ASSERT(!(command->Queue == EQueue::Copy || command->bRendering), "Builds require a graphics or compute command list outside rendering.");

		ASSERT(!(desc.Type != EAccelStructType::BottomLevel || accel->Type != EAccelStructType::BottomLevel), "The build and target must be bottom level.");

		const AccelBuildSignature signature = makeAccelBuildSignature(desc);

		ASSERT(!(desc.bPerformUpdate && (!accel->bBuilt || !accel->bAllowUpdate || accel->Signature != signature || !canBuildAfterStateRequests(*command, target, accel->RecordedUses))),
			"The target has no compatible completed build allowing updates.");

		if (accel->bBuilt && accel->bAllowCompaction && !desc.bPerformUpdate)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.AccelStructBuild", "A BLAS built for compaction cannot be rebuilt.");
		}

		if (!canBuildAfterStateRequests(*command, target, accel->RecordedUses))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.AccelStructBuild", "The target is referenced by a recorded command.");
		}
		BufferSlot& storage = m_State->m_Buffers[accel->Storage.Slot];

		ASSERT(!(scratch == accel->Storage ||
				scratchBuffer->Memory != EBufferMemory::Device ||
				scratchBuffer->bMapped ||
				!HasUsage(scratchBuffer->Usage, EBufferUsage::Storage) ||
				!HasUsage(scratchBuffer->Usage, EBufferUsage::DeviceAddress)),
			"The scratch buffer has invalid usage or overlaps the result.");

		Result<AccelStructBuildSizes> sizes = GetAccelStructBuildSizes(desc);

		if (!sizes)
		{
			ASSERT(sizes.GetError().Code != EErrorCode::InvalidArgument, "Acceleration structure build arguments must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(sizes.GetError());
		}

		const uint64 requiredScratch = desc.bPerformUpdate ? sizes.Value().UpdateScratchBytes : sizes.Value().BuildScratchBytes;
		const uint64 scratchAddress = backend::GetBufferDeviceAddress(m_State->m_Backend.State, scratchBuffer->Native);

		ASSERT(!((!desc.bPerformUpdate && sizes.Value().ResultBytes > accel->ByteCount) ||
				requiredScratch == 0 ||
				scratchAddress == 0 ||
				scratchAddress > NumericLimits<uint64>::max() - scratchOffset ||
				(scratchAddress + scratchOffset) % sizes.Value().ScratchAlignmentBytes != 0 ||
				scratchOffset > scratchBuffer->ByteCount ||
				requiredScratch > scratchBuffer->ByteCount - scratchOffset),
			"The result or scratch range is too small or misaligned.");

		Array<NativeAccelTriangleGeometry> native(m_State->m_Resource);
		Array<NativeAccelAabbGeometry> nativeAabbs(m_State->m_Resource);

		for (const AccelTriangleGeometryDesc& triangle : desc.Triangles)
		{
			ASSERT(!(triangle.VertexBuffer == accel->Storage ||
					triangle.VertexBuffer == scratch ||
					triangle.IndexBuffer == accel->Storage ||
					triangle.IndexBuffer == scratch ||
					triangle.OpacityMicromapIndexBuffer == accel->Storage ||
					triangle.OpacityMicromapIndexBuffer == scratch),
				"Build inputs cannot overlap result or scratch buffers.");

			const BufferSlot& vertices = m_State->m_Buffers[triangle.VertexBuffer.Slot];
			const BufferSlot* const indices = triangle.IndexBuffer.IsValid() ? &m_State->m_Buffers[triangle.IndexBuffer.Slot] : nullptr;
			const OpacityMicromapSlot* const micromap = triangle.OpacityMicromap.IsValid() ? findOpacityMicromap(triangle.OpacityMicromap) : nullptr;
			const BufferSlot* const micromapIndices = triangle.OpacityMicromapIndexBuffer.IsValid() ? &m_State->m_Buffers[triangle.OpacityMicromapIndexBuffer.Slot] : nullptr;

			if (!native.Add(
				{ vertices.Native, triangle.VertexOffsetBytes, triangle.VertexStrideBytes, triangle.VertexCount,
				 indices != nullptr ? indices->Native : nullptr, triangle.IndexOffsetBytes, triangle.IndexCount, triangle.IndexFormat,
				 triangle.bOpaque, micromap != nullptr ? micromap->Native : nullptr,
				 micromapIndices != nullptr ? micromapIndices->Native : nullptr, triangle.OpacityMicromapIndexOffsetBytes,
				 micromapIndices != nullptr
					 ? (triangle.OpacityMicromapIndexStrideBytes != 0 ? triangle.OpacityMicromapIndexStrideBytes
																 : (triangle.OpacityMicromapIndexFormat == EFormat::R16_UINT ? 2u : 4u))
					 : 0u,
				 triangle.OpacityMicromapIndexFormat, triangle.OpacityMicromapBaseTriangle, triangle.OpacityMicromapUsageCounts }))
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track build geometry.");
			}
		}

		for (const AccelAabbGeometryDesc& aabb : desc.Aabbs)
		{
			ASSERT(!(aabb.Buffer == accel->Storage || aabb.Buffer == scratch), "Build inputs cannot overlap result or scratch buffers.");

			if (!nativeAabbs.Add({ m_State->m_Buffers[aabb.Buffer.Slot].Native, aabb.OffsetBytes, aabb.StrideBytes, aabb.Count, aabb.bOpaque }))
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track build geometry.");
			}
		}

		Array<OpacityMicromapHandle> micromapReferences(m_State->m_Resource);

		if (!micromapReferences.Reserve(desc.Triangles.Size()))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not prepare acceleration structure micromap dependencies.");
		}

		for (const rhi::AccelTriangleGeometryDesc& triangle : desc.Triangles)
		{
			if (triangle.OpacityMicromap.IsValid())
			{
				(void)micromapReferences.Add(triangle.OpacityMicromap);
			}
		}

		Result<void> retainedMicromaps = reserveMicromapReferences(*accel, { micromapReferences.Data(), micromapReferences.Size() });
		if (!retainedMicromaps)
		{
			poisonCommandList(list);

			return retainedMicromaps;
		}

		const usize inputCount = desc.Triangles.Size() * 4 + desc.Aabbs.Size();

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + inputCount + 2) ||
			!command->AccelUses.Reserve(command->AccelUses.Size() + 1) ||
			!command->OpacityMicromapUses.Reserve(command->OpacityMicromapUses.Size() + desc.Triangles.Size()) ||
			!command->AccelBuildTargets.Reserve(command->AccelBuildTargets.Size() + 1))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track acceleration structure build resources.");
		}

		Result<void> recorded = backend::RecordBuildBottomLevelAccelStruct(
			m_State->m_Backend.State, command->Native, accel->Native, desc, { native.Data(), native.Size() }, { nativeAabbs.Data(), nativeAabbs.Size() },
			scratchBuffer->Native, scratchOffset);
		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}
		retainMicromapReferences(*accel, { micromapReferences.Data(), micromapReferences.Size() });
		for (const AccelTriangleGeometryDesc& triangle : desc.Triangles)
		{
			const BufferHandle inputs[3]{ triangle.VertexBuffer, triangle.IndexBuffer, triangle.OpacityMicromapIndexBuffer };
			for (BufferHandle input : inputs)
			{
				if (!input.IsValid())
				{
					continue;
				}
				(void)command->AddBufferUse({ input, m_State->m_Buffers[input.Slot].Native, ETrackedResourceState::AccelBuildInput });
				++m_State->m_Buffers[input.Slot].RecordedUses;
			}

			if (triangle.OpacityMicromap.IsValid())
			{
				OpacityMicromapSlot& omm = m_State->m_OpacityMicromaps[triangle.OpacityMicromap.Slot];
				BufferSlot& ommStorage = m_State->m_Buffers[omm.Storage.Slot];
				(void)command->AddBufferUse({ omm.Storage, ommStorage.Native, ETrackedResourceState::AccelStorage });
				++ommStorage.RecordedUses;
				(void)command->OpacityMicromapUses.Add(triangle.OpacityMicromap);
				++omm.RecordedUses;
			}
		}

		for (const AccelAabbGeometryDesc& aabb : desc.Aabbs)
		{
			(void)command->AddBufferUse({ aabb.Buffer, m_State->m_Buffers[aabb.Buffer.Slot].Native, ETrackedResourceState::AccelBuildInput });
			++m_State->m_Buffers[aabb.Buffer.Slot].RecordedUses;
		}
		(void)command->AddBufferUse({ scratch, scratchBuffer->Native, ETrackedResourceState::Storage });
		(void)command->AddBufferUse({ accel->Storage, storage.Native, ETrackedResourceState::AccelStorage });
		++scratchBuffer->RecordedUses;
		++storage.RecordedUses;
		(void)command->AccelUses.Add({ target, accel->Native });
		(void)command->AccelBuildTargets.Add({ target, signature, desc.bAllowUpdate, desc.bAllowCompaction });
		++accel->RecordedUses;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::BuildTopLevelAccelStruct(
		CommandListHandle list,
		AccelStructHandle target,
		const AccelStructBuildDesc& desc,
		Span<const AccelInstanceDesc> instances,
		BufferHandle scratch,
		uint64 scratchOffset)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		AccelStructSlot* const accel = findAccelStruct(target);
		BufferSlot* const scratchBuffer = findBuffer(scratch);

		ASSERT(!(command == nullptr || accel == nullptr || scratchBuffer == nullptr), "RHI operations require valid handles for this device.");
		if (accel->PreparedReferences != 0 || accel->TableClosureRefs != 0 || accel->BuildRevision == UINT64_MAX)
		{
			poisonCommandList(list);

			return preparedFailure(EErrorCode::Conflict);
		}

		ASSERT(!(command->Queue == EQueue::Copy || command->bRendering), "Builds require a graphics or compute command list outside rendering.");

		ASSERT(!(desc.Type != EAccelStructType::TopLevel ||
				accel->Type != EAccelStructType::TopLevel ||
				desc.InstanceCount == 0 ||
				desc.InstanceCount != instances.Size() ||
				!desc.Triangles.IsEmpty() ||
				!desc.Aabbs.IsEmpty()),
			"The top level instance count or target is invalid.");

		const AccelBuildSignature signature = makeAccelBuildSignature(desc);

		ASSERT(!(desc.bPerformUpdate && (!accel->bBuilt || !accel->bAllowUpdate || accel->Signature != signature || !canBuildAfterStateRequests(*command, target, accel->RecordedUses))),
			"The target has no compatible completed build allowing updates.");

		if (!canBuildAfterStateRequests(*command, target, accel->RecordedUses))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.AccelStructBuild", "The target is referenced by a recorded command.");
		}

		ASSERT(!(scratch == accel->Storage ||
				scratchBuffer->Memory != EBufferMemory::Device ||
				scratchBuffer->bMapped ||
				!HasUsage(scratchBuffer->Usage, EBufferUsage::Storage) ||
				!HasUsage(scratchBuffer->Usage, EBufferUsage::DeviceAddress)),
			"The scratch buffer has invalid usage or overlaps the result.");

		Result<AccelStructBuildSizes> sizes = GetAccelStructBuildSizes(desc);

		if (!sizes)
		{
			ASSERT(sizes.GetError().Code != EErrorCode::InvalidArgument, "Acceleration structure build arguments must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(sizes.GetError());
		}

		const uint64 requiredScratch = desc.bPerformUpdate ? sizes.Value().UpdateScratchBytes : sizes.Value().BuildScratchBytes;
		const uint64 scratchAddress = backend::GetBufferDeviceAddress(m_State->m_Backend.State, scratchBuffer->Native);

		ASSERT(!((!desc.bPerformUpdate && sizes.Value().ResultBytes > accel->ByteCount) ||
				requiredScratch == 0 ||
				scratchAddress == 0 ||
				scratchAddress > NumericLimits<uint64>::max() - scratchOffset ||
				(scratchAddress + scratchOffset) % sizes.Value().ScratchAlignmentBytes != 0 ||
				scratchOffset > scratchBuffer->ByteCount ||
				requiredScratch > scratchBuffer->ByteCount - scratchOffset),
			"The result or scratch range is too small or misaligned.");

		Array<NativeAccelInstance> native(m_State->m_Resource);

		for (const AccelInstanceDesc& instance : instances)
		{
			const AccelStructSlot* const bottom = findAccelStruct(instance.BottomLevel);
			bool bBuilt = bottom != nullptr && bottom->bBuilt;

			for (const RecordedAccelBuild& recorded : command->AccelBuildTargets)
			{
				if (recorded.Handle == instance.BottomLevel)
				{
					bBuilt = true;
					break;
				}
			}

			ASSERT(!(bottom == nullptr ||
					!bBuilt ||
					(bottom->Type != EAccelStructType::BottomLevel && bottom->Type != EAccelStructType::ClusterBottomLevel) ||
					bottom->Storage == scratch ||
					instance.InstanceId > 0xFFFFFF ||
					instance.HitGroupIndex > 0xFFFFFF ||
					(static_cast<uint8>(instance.Flags) & ~uint8{ 0x0F }) != 0),
				"A top level instance has an invalid BLAS or field.");

			for (float value : instance.Transform)
			{
				ASSERT(!(!IsFinite(value)), "An instance transform is not finite.");
			}
			NativeAccelInstance entry{};
			CopyBytes(entry.Transform, instance.Transform, sizeof(entry.Transform));
			entry.BottomLevelAddress = backend::GetAccelStructDeviceAddress(m_State->m_Backend.State, bottom->Native);
			entry.InstanceId = instance.InstanceId;
			entry.HitGroupIndex = instance.HitGroupIndex;
			entry.Mask = instance.Mask;
			entry.Flags = instance.Flags;

			if (entry.BottomLevelAddress == 0 || !native.Add(entry))
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.AccelInstance", "Could not prepare a top level instance.");
			}
		}

		if (!accel->ClusterBottomReferences.Reserve(accel->ClusterBottomReferences.Size() + instances.Size()) || !accel->BottomReferences.Reserve(accel->BottomReferences.Size() + instances.Size()))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain cluster BLAS references.");
		}

		for (const AccelInstanceDesc& instance : instances)
		{
			const AccelStructSlot& bottom = m_State->m_AccelStructs[instance.BottomLevel.Slot];

			if (bottom.Type != EAccelStructType::ClusterBottomLevel && bottom.OpacityMicromapReferences.IsEmpty())
			{
				continue;
			}
			bool bRetained = false;

			for (AccelStructHandle prior : accel->ClusterBottomReferences)
			{
				if (prior == instance.BottomLevel)
				{
					bRetained = true;
					break;
				}
			}

			if (!bRetained && bottom.ClusterParentRefs == UINT32_MAX)
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.ClusterReference", "Too many cluster TLAS references.");
			}
		}

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + instances.Size() + 2) ||
			!command->AccelUses.Reserve(command->AccelUses.Size() + instances.Size() + 1) ||
			!command->AccelBuildTargets.Reserve(command->AccelBuildTargets.Size() + 1))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track top level build resources.");
		}

		Result<void> recorded = backend::RecordBuildTopLevelAccelStruct(
			m_State->m_Backend.State, command->Native, accel->Native, desc, { native.Data(), native.Size() }, scratchBuffer->Native, scratchOffset);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		for (const AccelInstanceDesc& instance : instances)
		{
			AccelStructSlot& bottom = m_State->m_AccelStructs[instance.BottomLevel.Slot];
			bool bKnownBottom = false;

			for (AccelStructHandle prior : accel->BottomReferences)
			{
				if (prior == instance.BottomLevel)
				{
					bKnownBottom = true;
					break;
				}
			}

			if (!bKnownBottom)
			{
				const bool bAdded = accel->BottomReferences.Add(instance.BottomLevel);
				ASSERT(bAdded, "Reserved BLAS reference capacity must remain available.");
			}

			if (bottom.Type == EAccelStructType::ClusterBottomLevel || !bottom.OpacityMicromapReferences.IsEmpty())
			{
				bool bRetained = false;

				for (AccelStructHandle prior : accel->ClusterBottomReferences)
				{
					if (prior == instance.BottomLevel)
					{
						bRetained = true;
						break;
					}
				}

				if (!bRetained)
				{
					(void)accel->ClusterBottomReferences.Add(instance.BottomLevel);
					++bottom.ClusterParentRefs;
				}
			}
			(void)command->AddBufferUse({ bottom.Storage, m_State->m_Buffers[bottom.Storage.Slot].Native, ETrackedResourceState::AccelStorage });
			(void)command->AccelUses.Add({ instance.BottomLevel, bottom.Native });
			++m_State->m_Buffers[bottom.Storage.Slot].RecordedUses;
			++bottom.RecordedUses;
		}
		(void)command->AddBufferUse({ scratch, scratchBuffer->Native, ETrackedResourceState::Storage });
		(void)command->AddBufferUse({ accel->Storage, m_State->m_Buffers[accel->Storage.Slot].Native, ETrackedResourceState::AccelStorage });
		(void)command->AccelUses.Add({ target, accel->Native });
		(void)command->AccelBuildTargets.Add({ target, signature, desc.bAllowUpdate, desc.bAllowCompaction });
		++scratchBuffer->RecordedUses;
		++m_State->m_Buffers[accel->Storage.Slot].RecordedUses;
		++accel->RecordedUses;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::BuildTopLevelAccelStructFromBuffer(
		CommandListHandle list,
		AccelStructHandle target,
		const AccelStructBuildDesc& desc,
		BufferHandle instances,
		uint64 instanceOffset,
		Span<const AccelStructHandle> referencedBlas,
		BufferHandle scratch,
		uint64 scratchOffset)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		AccelStructSlot* const accel = findAccelStruct(target);
		BufferSlot* const input = findBuffer(instances);
		BufferSlot* const scratchBuffer = findBuffer(scratch);

		ASSERT(!(command == nullptr || accel == nullptr || input == nullptr || scratchBuffer == nullptr), "RHI operations require valid handles for this device.");

		if (accel->PreparedReferences != 0 || accel->TableClosureRefs != 0 || accel->BuildRevision == UINT64_MAX)
		{
			poisonCommandList(list);

			return preparedFailure(EErrorCode::Conflict);
		}

		ASSERT(!(command->Queue == EQueue::Copy || command->bRendering), "Builds require a graphics or compute command list outside rendering.");

		ASSERT(!(desc.Type != EAccelStructType::TopLevel ||
				accel->Type != EAccelStructType::TopLevel ||
				desc.InstanceCount == 0 ||
				!desc.Triangles.IsEmpty() ||
				!desc.Aabbs.IsEmpty() ||
				referencedBlas.IsEmpty()),
			"The top level instance count, target, or BLAS references are invalid.");

		const AccelBuildSignature signature = makeAccelBuildSignature(desc);

		ASSERT(!(desc.bPerformUpdate && (!accel->bBuilt || !accel->bAllowUpdate || accel->Signature != signature || !canBuildAfterStateRequests(*command, target, accel->RecordedUses))),
			"The target has no compatible completed build allowing updates.");

		if (!canBuildAfterStateRequests(*command, target, accel->RecordedUses))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.AccelStructBuild", "The target is referenced by a recorded command.");
		}

		ASSERT(!(instances == scratch ||
				instances == accel->Storage ||
				input->Memory != EBufferMemory::Device ||
				input->bMapped ||
				!HasUsage(input->Usage, EBufferUsage::AccelBuildInput) ||
				!HasUsage(input->Usage, EBufferUsage::DeviceAddress) ||
				instanceOffset % 16 != 0 ||
				instanceOffset > input->ByteCount ||
				static_cast<uint64>(desc.InstanceCount) * sizeof(AccelGpuInstanceDesc) > input->ByteCount - instanceOffset),
			"The instance buffer usage, alignment, or range is invalid.");

		ASSERT(!(scratch == accel->Storage ||
				scratchBuffer->Memory != EBufferMemory::Device ||
				scratchBuffer->bMapped ||
				!HasUsage(scratchBuffer->Usage, EBufferUsage::Storage) ||
				!HasUsage(scratchBuffer->Usage, EBufferUsage::DeviceAddress)),
			"The scratch buffer has invalid usage or overlaps the result.");

		Result<AccelStructBuildSizes> sizes = GetAccelStructBuildSizes(desc);

		if (!sizes)
		{
			ASSERT(sizes.GetError().Code != EErrorCode::InvalidArgument, "Acceleration structure build arguments must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(sizes.GetError());
		}

		const uint64 requiredScratch = desc.bPerformUpdate ? sizes.Value().UpdateScratchBytes : sizes.Value().BuildScratchBytes;
		const uint64 scratchAddress = backend::GetBufferDeviceAddress(m_State->m_Backend.State, scratchBuffer->Native);

		ASSERT(!((!desc.bPerformUpdate && sizes.Value().ResultBytes > accel->ByteCount) ||
				requiredScratch == 0 ||
				scratchAddress == 0 ||
				scratchAddress > NumericLimits<uint64>::max() - scratchOffset ||
				(scratchAddress + scratchOffset) % sizes.Value().ScratchAlignmentBytes != 0 ||
				scratchOffset > scratchBuffer->ByteCount ||
				requiredScratch > scratchBuffer->ByteCount - scratchOffset),
			"The result or scratch range is too small or misaligned.");

		for (AccelStructHandle handle : referencedBlas)
		{
			const AccelStructSlot* const bottom = findAccelStruct(handle);
			bool bBuilt = bottom != nullptr && bottom->bBuilt;

			for (const RecordedAccelBuild& recorded : command->AccelBuildTargets)
			{
				if (recorded.Handle == handle)
				{
					bBuilt = true;
					break;
				}
			}

			ASSERT(!(bottom == nullptr || !bBuilt || (bottom->Type != EAccelStructType::BottomLevel && bottom->Type != EAccelStructType::ClusterBottomLevel) || bottom->Storage == scratch),
				"A referenced BLAS is invalid or has not been built.");
		}

		if (!accel->ClusterBottomReferences.Reserve(accel->ClusterBottomReferences.Size() + referencedBlas.Size()) ||
			!accel->BottomReferences.Reserve(accel->BottomReferences.Size() + referencedBlas.Size()))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain cluster BLAS references.");
		}

		for (AccelStructHandle handle : referencedBlas)
		{
			const AccelStructSlot& bottom = m_State->m_AccelStructs[handle.Slot];

			if (bottom.Type != EAccelStructType::ClusterBottomLevel && bottom.OpacityMicromapReferences.IsEmpty())
			{
				continue;
			}
			bool bRetained = false;

			for (AccelStructHandle prior : accel->ClusterBottomReferences)
			{
				if (prior == handle)
				{
					bRetained = true;
					break;
				}
			}

			if (!bRetained && bottom.ClusterParentRefs == UINT32_MAX)
			{
				poisonCommandList(list);

				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.ClusterReference", "Too many cluster TLAS references.");
			}
		}

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + referencedBlas.Size() + 3) ||
			!command->AccelUses.Reserve(command->AccelUses.Size() + referencedBlas.Size() + 1) ||
			!command->AccelBuildTargets.Reserve(command->AccelBuildTargets.Size() + 1))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track top level build resources.");
		}

		Result<void> recorded = backend::RecordBuildTopLevelAccelStructFromBuffer(
			m_State->m_Backend.State, command->Native, accel->Native, desc, input->Native, instanceOffset, scratchBuffer->Native, scratchOffset);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}
		(void)command->AddBufferUse({ instances, input->Native, ETrackedResourceState::AccelBuildInput });
		++input->RecordedUses;

		for (AccelStructHandle handle : referencedBlas)
		{
			AccelStructSlot& bottom = m_State->m_AccelStructs[handle.Slot];
			bool bKnownBottom = false;

			for (AccelStructHandle prior : accel->BottomReferences)
			{
				if (prior == handle)
				{
					bKnownBottom = true;
					break;
				}
			}

			if (!bKnownBottom)
			{
				const bool bAdded = accel->BottomReferences.Add(handle);
				ASSERT(bAdded, "Reserved BLAS reference capacity must remain available.");
			}

			if (bottom.Type == EAccelStructType::ClusterBottomLevel || !bottom.OpacityMicromapReferences.IsEmpty())
			{
				bool bRetained = false;

				for (AccelStructHandle prior : accel->ClusterBottomReferences)
				{
					if (prior == handle)
					{
						bRetained = true;
						break;
					}
				}

				if (!bRetained)
				{
					(void)accel->ClusterBottomReferences.Add(handle);
					++bottom.ClusterParentRefs;
				}
			}
			(void)command->AddBufferUse({ bottom.Storage, m_State->m_Buffers[bottom.Storage.Slot].Native, ETrackedResourceState::AccelStorage });
			(void)command->AccelUses.Add({ handle, bottom.Native });
			++m_State->m_Buffers[bottom.Storage.Slot].RecordedUses;
			++bottom.RecordedUses;
		}
		(void)command->AddBufferUse({ scratch, scratchBuffer->Native, ETrackedResourceState::Storage });
		(void)command->AddBufferUse({ accel->Storage, m_State->m_Buffers[accel->Storage.Slot].Native, ETrackedResourceState::AccelStorage });
		(void)command->AccelUses.Add({ target, accel->Native });
		(void)command->AccelBuildTargets.Add({ target, signature, desc.bAllowUpdate, desc.bAllowCompaction });
		++scratchBuffer->RecordedUses;
		++m_State->m_Buffers[accel->Storage.Slot].RecordedUses;
		++accel->RecordedUses;

		return Result<void>::Success();
	}

	[[nodiscard]] bool Device::canBuildAfterStateRequests(const CommandSlot& command, AccelStructHandle handle, usize recordedUses) const noexcept
	{
		usize localUses = 0;

		for (const RecordedAccelUse& use : command.AccelUses)
		{
			if (use.Handle != handle)
			{
				continue;
			}

			if (!use.bBuildWriteState)
			{
				return false;
			}
			++localUses;
		}

		return localUses == recordedUses;
	}

	[[nodiscard]] Result<void> Device::SetAccelStructState(CommandListHandle list, AccelStructHandle handle, EAccelStructState state)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		AccelStructSlot* const accel = findAccelStruct(handle);

		ASSERT(!(command == nullptr || accel == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!(command->Queue == EQueue::Copy || command->bRendering || (state != EAccelStructState::BuildWrite && state != EAccelStructState::ShaderRead)),
			"A graphics or compute list outside rendering and a valid state are required.");

		if (state == EAccelStructState::ShaderRead && !accel->bBuilt)
		{
			bool bBuiltHere = false;

			for (const RecordedAccelBuild& build : command->AccelBuildTargets)
			{
				if (build.Handle == handle)
				{
					bBuiltHere = true;
					break;
				}
			}

			ASSERT(!(!bBuiltHere), "Shader access requires a built acceleration structure.");
		}

		if (!reserveAccelStructUse(*command, accel))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain the acceleration structure state request.");
		}

		Result<void> recorded = backend::RecordSetAccelStructState(m_State->m_Backend.State, command->Native, accel->Native, state);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}
		trackAccelStruct(*command, handle, accel, state == EAccelStructState::BuildWrite);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<AccelStructSlot*> Device::resolveAccelStruct(uint32 slot, AccelStructHandle handle) noexcept
	{
		if (slot == 0xFFFFFFFFu)
		{
			if (handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0)
			{
				return Result<AccelStructSlot*>::Success(nullptr);
			}

			ASSERT(false, "A pipeline without an acceleration structure binding requires an empty handle.");
		}
		AccelStructSlot* const accel = findAccelStruct(handle);

		ASSERT(accel != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(accel->Type == EAccelStructType::TopLevel && accel->bBuilt, "A built top level acceleration structure is required.");

		return Result<AccelStructSlot*>::Success(accel);
	}

	[[nodiscard]] Result<void> Device::trackBottomLevelSubmissionUses(CommandSlot& command)
	{
		const usize count = command.AccelUses.Size();
		usize additionalCapacity = 0;
		for (usize index = 0; index < count; ++index)
		{
			const Array<AccelStructHandle>& references = m_State->m_AccelStructs[command.AccelUses[index].Handle.Slot].BottomReferences;
			if (references.Size() > NumericLimits<usize>::max() - additionalCapacity)
			{
				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track indirect bottom level acceleration structure access.");
			}
			additionalCapacity += references.Size();
		}

		if (additionalCapacity > NumericLimits<usize>::max() - command.BufferUses.Size() ||
			additionalCapacity > NumericLimits<usize>::max() - command.AccelUses.Size() ||
			!command.BufferUses.Reserve(command.BufferUses.Size() + additionalCapacity) ||
			!command.AccelUses.Reserve(command.AccelUses.Size() + additionalCapacity))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track indirect bottom level acceleration structure access.");
		}

		for (usize index = 0; index < count; ++index)
		{
			const AccelStructSlot& parent = m_State->m_AccelStructs[command.AccelUses[index].Handle.Slot];

			for (AccelStructHandle handle : parent.BottomReferences)
			{
				AccelStructSlot* const bottom = findAccelStruct(handle);

				if (bottom == nullptr)
				{
					continue;
				}
				bool bTracked = false;

				for (const RecordedAccelUse& use : command.AccelUses)
				{
					if (use.Handle == handle)
					{
						bTracked = true;
						break;
					}
				}

				if (bTracked)
				{
					continue;
				}
				trackAccelStruct(command, handle, bottom);
			}
		}

		return Result<void>::Success();
	}

	void Device::trackAccelStruct(CommandSlot& command, AccelStructHandle handle, AccelStructSlot* accel, bool bBuildWriteState) noexcept
	{
		if (accel == nullptr)
		{
			return;
		}
		BufferSlot& storage = m_State->m_Buffers[accel->Storage.Slot];
		(void)command.AddBufferUse({ accel->Storage, storage.Native, ETrackedResourceState::AccelStorage });
		(void)command.AccelUses.Add({ handle, accel->Native, bBuildWriteState });
		++storage.RecordedUses;
		++accel->RecordedUses;
	}

	[[nodiscard]] bool Device::reserveAccelStructUse(CommandSlot& command, const AccelStructSlot* accel) noexcept
	{
		return accel == nullptr || (command.BufferUses.Reserve(command.BufferUses.Size() + 1) && command.AccelUses.Reserve(command.AccelUses.Size() + 1));
	}

	[[nodiscard]] Result < AccelStructBuildSizes > Device::getAccelStructBuildSizes(const AccelStructBuildDesc& desc) const
	{
		CallScope call(*this, true);
		ASSERT(desc.Type == EAccelStructType::BottomLevel || desc.Type == EAccelStructType::TopLevel, "Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");
		ASSERT((!desc.bPerformUpdate || desc.bAllowUpdate) && (!desc.bAllowCompaction || desc.Type == EAccelStructType::BottomLevel),
			"Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");

		if (desc.Type == EAccelStructType::BottomLevel)
		{
			ASSERT((!desc.Triangles.IsEmpty() || !desc.Aabbs.IsEmpty()) &&
				(desc.Triangles.IsEmpty() || desc.Aabbs.IsEmpty()) &&
				desc.InstanceCount == 0 &&
				!(desc.Aabbs.Size() > NumericLimits<uint32>::max()) &&
				!(desc.Triangles.Size() > NumericLimits<uint32>::max() - desc.Aabbs.Size()),
				"Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");
		}
		else if (desc.Type == EAccelStructType::TopLevel)
		{
			ASSERT(desc.Triangles.IsEmpty() && desc.Aabbs.IsEmpty() && desc.InstanceCount != 0, "Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");
		}

		if (!m_State->m_Backend.bAccelerationStructureBuildInput)
		{
			return Result<AccelStructBuildSizes>::Failure(EErrorCode::Unsupported, "Rhi.AccelStructBuildSizes", "The device does not support acceleration structure builds.");
		}

		Array<NativeAccelTriangleGeometry> native(m_State->m_Resource);
		Array<NativeAccelAabbGeometry> nativeAabbs(m_State->m_Resource);

		for (const AccelTriangleGeometryDesc& triangle : desc.Triangles)
		{
			const BufferSlot* const vertices = findBuffer(triangle.VertexBuffer);

			ASSERT(vertices != nullptr &&
				vertices->Memory == EBufferMemory::Device &&
				HasUsage(vertices->Usage, EBufferUsage::AccelBuildInput) &&
				HasUsage(vertices->Usage, EBufferUsage::DeviceAddress) &&
				triangle.VertexStrideBytes >= 12 &&
				triangle.VertexStrideBytes % 4 == 0 &&
				triangle.VertexCount >= 3 &&
				triangle.VertexOffsetBytes % 4 == 0 &&
				triangle.VertexOffsetBytes <= vertices->ByteCount &&
				!(static_cast<uint64>(triangle.VertexCount - 1) * triangle.VertexStrideBytes + 12 > vertices->ByteCount - triangle.VertexOffsetBytes),
				"Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");

			const bool bIndexed = triangle.IndexBuffer.IsValid();

			ASSERT((bIndexed || (triangle.IndexCount == 0 && triangle.IndexOffsetBytes == 0 && triangle.VertexCount % 3 == 0)) &&
				(!bIndexed || (triangle.IndexCount >= 3 && triangle.IndexCount % 3 == 0 && (triangle.IndexFormat == EFormat::R16_UINT || triangle.IndexFormat == EFormat::R32_UINT))),
				"Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");

			const BufferSlot* indices = nullptr;

			if (bIndexed)
			{
				indices = findBuffer(triangle.IndexBuffer);
				const uint64 elementBytes = triangle.IndexFormat == EFormat::R16_UINT ? 2 : 4;

				ASSERT(indices != nullptr &&
					indices->Memory == EBufferMemory::Device &&
					HasUsage(indices->Usage, EBufferUsage::AccelBuildInput) &&
					HasUsage(indices->Usage, EBufferUsage::DeviceAddress) &&
					triangle.IndexOffsetBytes % elementBytes == 0 &&
					triangle.IndexOffsetBytes <= indices->ByteCount &&
					!(static_cast<uint64>(triangle.IndexCount) * elementBytes > indices->ByteCount - triangle.IndexOffsetBytes),
					"Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");
			}

			const OpacityMicromapSlot* micromap = nullptr;
			const BufferSlot* micromapIndices = nullptr;
			uint32 micromapStride = 0;

			if (triangle.OpacityMicromap.IsValid())
			{
				if (!m_State->m_Backend.bOpacityMicromap)
				{
					return Result<AccelStructBuildSizes>::Failure(EErrorCode::Unsupported, "Rhi.OpacityMicromap", "The device does not support opacity micromap build input.");
				}
				micromap = findOpacityMicromap(triangle.OpacityMicromap);

				ASSERT(micromap != nullptr && micromap->bBuilt && !triangle.OpacityMicromapUsageCounts.IsEmpty(),
					"Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");

				if (triangle.OpacityMicromapIndexBuffer.IsValid())
				{
					micromapIndices = findBuffer(triangle.OpacityMicromapIndexBuffer);
					const uint32 elementBytes = triangle.OpacityMicromapIndexFormat == EFormat::R16_UINT ? 2u : 4u;
					micromapStride = triangle.OpacityMicromapIndexStrideBytes != 0 ? triangle.OpacityMicromapIndexStrideBytes : elementBytes;
					const uint64 primitives = bIndexed ? triangle.IndexCount / 3 : triangle.VertexCount / 3;

					ASSERT(micromapIndices != nullptr &&
						micromapIndices->Memory == EBufferMemory::Device &&
						HasUsage(micromapIndices->Usage, EBufferUsage::AccelBuildInput) &&
						HasUsage(micromapIndices->Usage, EBufferUsage::DeviceAddress) &&
						(triangle.OpacityMicromapIndexFormat == EFormat::R16_UINT || triangle.OpacityMicromapIndexFormat == EFormat::R32_UINT) &&
						micromapStride % elementBytes == 0 &&
						triangle.OpacityMicromapIndexOffsetBytes % elementBytes == 0 &&
						triangle.OpacityMicromapIndexOffsetBytes <= micromapIndices->ByteCount &&
						(primitives - 1) * micromapStride + elementBytes <= micromapIndices->ByteCount - triangle.OpacityMicromapIndexOffsetBytes,
						"Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");
				}
				else
				{
					ASSERT(triangle.OpacityMicromapIndexOffsetBytes == 0 && triangle.OpacityMicromapIndexStrideBytes == 0,
						"Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");
				}
			}
			else
			{
				ASSERT(!triangle.OpacityMicromapIndexBuffer.IsValid() &&
					triangle.OpacityMicromapIndexOffsetBytes == 0 &&
					triangle.OpacityMicromapIndexStrideBytes == 0 &&
					triangle.OpacityMicromapUsageCounts.IsEmpty(),
					"Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");
			}

			if (!native.Add({ vertices->Native, triangle.VertexOffsetBytes, triangle.VertexStrideBytes, triangle.VertexCount,
							 indices != nullptr ? indices->Native : nullptr, triangle.IndexOffsetBytes, triangle.IndexCount,
							 triangle.IndexFormat, triangle.bOpaque, micromap != nullptr ? micromap->Native : nullptr,
							 micromapIndices != nullptr ? micromapIndices->Native : nullptr, triangle.OpacityMicromapIndexOffsetBytes,
							 micromapStride, triangle.OpacityMicromapIndexFormat, triangle.OpacityMicromapBaseTriangle,
							 triangle.OpacityMicromapUsageCounts }))
			{
				return Result<AccelStructBuildSizes>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate acceleration structure geometry.");
			}
		}

		for (const AccelAabbGeometryDesc& aabb : desc.Aabbs)
		{
			const BufferSlot* const buffer = findBuffer(aabb.Buffer);

			ASSERT(buffer != nullptr &&
				buffer->Memory == EBufferMemory::Device &&
				HasUsage(buffer->Usage, EBufferUsage::AccelBuildInput) &&
				HasUsage(buffer->Usage, EBufferUsage::DeviceAddress) &&
				aabb.StrideBytes >= 24 &&
				aabb.StrideBytes % 8 == 0 &&
				aabb.Count != 0 &&
				aabb.OffsetBytes % 8 == 0 &&
				aabb.OffsetBytes <= buffer->ByteCount &&
				!(static_cast<uint64>(aabb.Count - 1) * aabb.StrideBytes + 24 > buffer->ByteCount - aabb.OffsetBytes),
				"Acceleration structure geometry, storage, usage, and ranges must satisfy the build contract.");

			if (!nativeAabbs.Add({ buffer->Native, aabb.OffsetBytes, aabb.StrideBytes, aabb.Count, aabb.bOpaque }))
			{
				return Result<AccelStructBuildSizes>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate acceleration structure geometry.");
			}
		}

		return backend::GetAccelStructBuildSizes(m_State->m_Backend.State, desc, { native.Data(), native.Size() },
			{ nativeAabbs.Data(), nativeAabbs.Size() });
	}
}
