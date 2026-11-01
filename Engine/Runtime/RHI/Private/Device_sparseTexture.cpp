#include "DeviceState.h"

#include <Assertion.h>

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<SubmissionToken> Device::UpdateTextureTileMappings(TextureHandle texture, Span<const TextureTileMapping> mappings, EQueue queue)
	{
		CallScope call(*this, false);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(queue == EQueue::Graphics || queue == EQueue::Compute || queue == EQueue::Copy, "Sparse mappings require a valid queue.");
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot admit sparse mappings after submission failure.");
		}
		if (hasNativeQueueAccess(queue))
		{
			return Result<SubmissionToken>::Failure(nativeQueueAccessConflict());
		}

		if (!slot->Descriptor.bSparse || backend::HAS_UPDATE_TEXTURE_TILE_MAPPINGS == false)
		{
			return Result<SubmissionToken>::Failure(EErrorCode::Unsupported, "Rhi.SparseTexture", "The texture or backend does not support tile mapping.");
		}

		ASSERT(!mappings.IsEmpty() && mappings.Data() != nullptr, "Tile mapping batches require at least one mapping.");
		if (mappings.Size() > NumericLimits<uint32>::max() ||
			m_State->m_PreparedSparseTileCount > NumericLimits<usize>::max() - m_State->m_SparseTiles.Size() ||
			mappings.Size() > NumericLimits<usize>::max() - m_State->m_SparseTiles.Size() - m_State->m_PreparedSparseTileCount)
		{
			return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.TextureTiles", "The tile mapping batch exceeds available bookkeeping capacity.");
		}

		if (slot->RecordingSlot != NumericLimits<uint32>::max() || slot->PreparedReferences != 0 || slot->bSparseMappingPrepared || slot->bSparseMappingUnavailable)
		{
			return Result<SubmissionToken>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "A recording command list references the sparse texture.");
		}

		const uint32 count = slot->Descriptor.MipLevels * slot->Descriptor.ArrayLayers;
		Array<TextureSubresourceTiling> subresources(m_State->m_Resource);
		Array<NativeTextureTileMapping> native(m_State->m_Resource);
		Array<SparseTileKey> requested(m_State->m_Resource);
		Array<uint8> packedAction(m_State->m_Resource);

		if (!subresources.ResizeUninitialized(count) ||
			!native.Reserve(mappings.Size()) ||
			!requested.Reserve(mappings.Size()) ||
			!packedAction.ResizeUninitialized(slot->Descriptor.ArrayLayers) ||
			!m_State->m_SparseTiles.Reserve(m_State->m_SparseTiles.Size() + m_State->m_PreparedSparseTileCount + mappings.Size()))
		{
			return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not reserve tile mapping state.");
		}

		SetBytes(packedAction.Data(), 0, packedAction.Size());
		Result<TextureTiling> tiling = GetTextureTiling(texture, subresources.View());

		if (!tiling)
		{
			return Result<SubmissionToken>::Failure(tiling.GetError());
		}

		for (usize index = 0; index < mappings.Size(); ++index)
		{
			const TextureTileMapping& mapping = mappings[index];
			const uint32 subresource = mapping.Subresource.MipLevel + mapping.Subresource.ArrayLayer * slot->Descriptor.MipLevels;

			ASSERT(mapping.Subresource.ArrayLayer < slot->Descriptor.ArrayLayers &&
				mapping.Subresource.MipLevel < slot->Descriptor.MipLevels &&
				subresource < subresources.Size(),
				"The tile subresource is invalid.");

			const bool bPacked = mapping.Subresource.MipLevel >= tiling.Value().StandardMipCount;

			ASSERT(!(bPacked ? (mapping.Subresource.MipLevel != tiling.Value().StandardMipCount || tiling.Value().PackedTileCount == 0 || (tiling.Value().bSingleMipTail && mapping.Subresource.ArrayLayer != 0) || mapping.X >= tiling.Value().PackedTileCount || mapping.Y != 0 || mapping.Z != 0) : (mapping.X >= subresources[subresource].WidthTiles || mapping.Y >= subresources[subresource].HeightTiles || mapping.Z >= subresources[subresource].DepthTiles)),
				"The tile coordinate is outside the standard or packed mip range.");

			if (bPacked)
			{
				const uint8 action = mapping.Heap.IsValid() ? 1 : 2;
				uint8& previous = packedAction[mapping.Subresource.ArrayLayer];

				ASSERT(previous == 0 || previous == action, "A packed mip tail must be mapped or unmapped as a whole.");
				previous = action;
			}

			const SparseTileKey key{ texture, mapping.Subresource.MipLevel, mapping.Subresource.ArrayLayer, mapping.X, mapping.Y, mapping.Z };
			if (!requested.Add(key))
			{
				return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track requested tile mappings.");
			}
			void* heapNative = nullptr;

			if (mapping.Heap.IsValid())
			{
				HeapSlot* const heap = getHeap(mapping.Heap);

				ASSERT(heap->Descriptor.Usage == EHeapUsage::Textures &&
					heap->Descriptor.Memory == EBufferMemory::Device &&
					heap->LiveTextures == 0 &&
					mapping.HeapByteOffset % tiling.Value().TileBytes == 0 &&
					!(mapping.HeapByteOffset / tiling.Value().TileBytes > NumericLimits<uint32>::max()) &&
					mapping.HeapByteOffset <= heap->Descriptor.CapacityBytes &&
					tiling.Value().TileBytes <= heap->Descriptor.CapacityBytes - mapping.HeapByteOffset &&
					!(heap->SparseTileRefs > NumericLimits<uint32>::max() - mappings.Size()),
					"The tile heap or aligned heap range is invalid.");
				heapNative = heap->Native;
			}
			else ASSERT(mapping.HeapByteOffset == 0 && m_State->m_SparseTiles.Find(key) != nullptr, "The tile is not currently mapped.");

			if (!native.Add({ mapping.Subresource, mapping.X, mapping.Y, mapping.Z, heapNative,
							 static_cast<uint32>(mapping.HeapByteOffset / tiling.Value().TileBytes) }))
			{
				return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not prepare native tile mappings.");
			}
		}

		Sort(requested.Data(), requested.Data() + requested.Size());

		for (usize index = 0; index < requested.Size(); ++index)
		{
			const SparseTileKey& key = requested[index];

			ASSERT(index == 0 || key != requested[index - 1], "A tile appears twice in one mapping batch.");

			if (key.MipLevel != tiling.Value().StandardMipCount)
			{
				continue;
			}

			const usize first = index;

			while (index < requested.Size() && requested[index].MipLevel == key.MipLevel && requested[index].ArrayLayer == key.ArrayLayer)
			{
				++index;
			}

			ASSERT(index - first == tiling.Value().PackedTileCount, "Every packed mip tile in an array layer must be mapped together.");

			for (usize tile = first; tile < index; ++tile)
			{
				ASSERT(requested[tile].X == tile - first, "A packed mip tail must contain each tile exactly once.");
			}
			--index;
		}

		// Reserve the same uncertainty owner used by prepared mappings before native queue work.
		// Successful ordinary mappings keep their existing conservative heap retirement policy.
		SharedPtr<PreparedTextureMappings> owner;
		if (mappings.Size() > NumericLimits<usize>::max() / 2 ||
			!MakeSharedInto<PreparedTextureMappings>(m_State->m_Resource, &owner, m_State->m_Resource) ||
			!owner->Mappings.Reserve(mappings.Size()) ||
			!owner->RetainedHeaps.Reserve(mappings.Size() * 2))
		{
			return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not reserve sparse mapping failure ownership.");
		}
		usize ownerIndex = 0;
		while (ownerIndex < m_State->m_TextureMappings.Size() && m_State->m_TextureMappings[ownerIndex])
		{
			++ownerIndex;
		}
		if (ownerIndex == m_State->m_TextureMappings.Size() && !m_State->m_TextureMappings.Reserve(ownerIndex + 1))
		{
			return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not reserve sparse mapping failure registry storage.");
		}
		for (const TextureTileMapping& mapping : mappings)
		{
			const bool bAdded = owner->Mappings.Add(mapping);
			ASSERT(bAdded, "Sparse mapping failure storage is reserved before capture.");
			const SparseTileKey key{texture, mapping.Subresource.MipLevel, mapping.Subresource.ArrayLayer, mapping.X, mapping.Y, mapping.Z };
			if (HeapHandle const* previous = m_State->m_SparseTiles.Find(key))
			{
				const bool bRetained = owner->RetainedHeaps.Add(*previous);
				ASSERT(bRetained, "Sparse mapping failure storage covers replaced heaps.");
			}
			if (mapping.Heap.IsValid())
			{
				const bool bRetained = owner->RetainedHeaps.Add(mapping.Heap);
				ASSERT(bRetained, "Sparse mapping failure storage covers candidate heaps.");
			}
		}
		for (const HeapHandle heap : owner->RetainedHeaps)
		{
			if (owner->RetainedHeaps.Size() > UINT32_MAX || m_State->m_Heaps[heap.Slot].PreparedSparseReferences > UINT32_MAX - owner->RetainedHeaps.Size())
			{
				return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.TextureTiles", "Sparse mapping failure retention is exhausted.");
			}
		}
		owner->Texture = texture;
		owner->Queue = queue;
		owner->Generation = 0;
		owner->bPinsAcquired = true;
		++slot->PreparedReferences;
		m_State->m_PreparedSparseTileCount += owner->Mappings.Size();
		for (const HeapHandle heap : owner->RetainedHeaps)
		{
			++m_State->m_Heaps[heap.Slot].PreparedSparseReferences;
		}
		if (ownerIndex == m_State->m_TextureMappings.Size())
		{
			const bool bAdded = m_State->m_TextureMappings.Add(owner);
			ASSERT(bAdded, "Sparse mapping failure registry storage is reserved before handoff.");
		}
		else
		{
			m_State->m_TextureMappings[ownerIndex] = owner;
		}

		Result<uint64> submitted = backend::UpdateTextureTileMappings(m_State->m_Backend.State, slot->Native, slot->Descriptor, native.View(), queue);
		const uint64 acceptedSerial = submitted ? submitted.Value() : 0;
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
		if (submitted && g_bFailNextAcceptedSubmit)
		{
			g_bFailNextAcceptedSubmit = false;
			submitted = Result<uint64>::Failure(EErrorCode::ExternalFailure, "Rhi.TestAcceptedSubmit", "Injected failure after native sparse mapping submission.");
		}
#endif
		if (!submitted)
		{
			const bool bUncertain = acceptedSerial != 0 || (backend::HAS_WAS_SUBMISSION_ACCEPTED && backend::WasSubmissionAccepted(m_State->m_Backend.State));
			if (bUncertain)
			{
				for (const HeapHandle heap : owner->RetainedHeaps)
				{
					m_State->m_Heaps[heap.Slot].bUntrackedSparseUses = true;
				}
				owner->bSubmitted = true;
				owner->bUncertain = true;
				if (acceptedSerial != 0)
				{
					owner->Completion = MakeSubmissionToken(m_State->m_Owner, queue, acceptedSerial);
				} slot->bSparseMappingUnavailable = true;
				m_State->m_bFirstSubmissionMayBeAccepted = true;
				recordSubmissionFault(submitted.GetError());
			}
			else
			{
				releasePreparedTextureMappings(*owner);
				m_State->m_TextureMappings[ownerIndex].Reset();
			}

			return Result<SubmissionToken>::Failure(submitted.GetError());
		}

		for (const HeapHandle heap : owner->RetainedHeaps)
		{
			m_State->m_Heaps[heap.Slot].bUntrackedSparseUses = true;
		}
		for (const TextureTileMapping& mapping : mappings)
		{
			const SparseTileKey key{ texture, mapping.Subresource.MipLevel, mapping.Subresource.ArrayLayer, mapping.X, mapping.Y, mapping.Z };
			HeapHandle* const old = m_State->m_SparseTiles.Find(key);

			if (old != nullptr)
			{
				--m_State->m_Heaps[old->Slot].SparseTileRefs;

				if (mapping.Heap.IsValid())
				{
					*old = mapping.Heap;
				}
				else
				{
					(void)m_State->m_SparseTiles.Erase(key);
				}
			}
			else
			{
				(void)m_State->m_SparseTiles.EmplaceRequired(key, mapping.Heap);
			}

			if (mapping.Heap.IsValid())
			{
				++m_State->m_Heaps[mapping.Heap.Slot].SparseTileRefs;
			}
		}

		releasePreparedTextureMappings(*owner);
		m_State->m_TextureMappings[ownerIndex].Reset();

		return Result<SubmissionToken>::Success(MakeSubmissionToken(m_State->m_Owner, queue, submitted.Value()));
	}

	[[nodiscard]] Result<PreparedTextureMappingsHandle> Device::PrepareTextureTileMappings(TextureHandle texture, Span<const TextureTileMapping> mappings, EQueue queue)
	{
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot prepare sparse mappings after submission failure.");
		}
		collectPreparedTextureMappings(false);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");
		ASSERT(queue == EQueue::Graphics || queue == EQueue::Compute || queue == EQueue::Copy, "Sparse mappings require a valid queue.");

		if (!slot->Descriptor.bSparse || !m_State->m_Backend.Capabilities.Features.bSparseTextures || backend::HAS_PREPARE_TEXTURE_TILE_MAPPINGS == false)
		{
			return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::Unsupported, "Rhi.SparseTexture", "The texture or backend does not support tile mapping.");
		}

		ASSERT(!mappings.IsEmpty() && mappings.Data() != nullptr, "Tile mapping batches require at least one mapping.");
		if (mappings.Size() > NumericLimits<uint32>::max() ||
			mappings.Size() > NumericLimits<usize>::max() - m_State->m_SparseTiles.Size())
		{
			return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.TextureTiles", "The tile mapping batch exceeds available bookkeeping capacity.");
		}

		if (slot->RecordingSlot != NumericLimits<uint32>::max() || slot->PreparedReferences != 0 || slot->bSparseMappingPrepared || slot->bSparseMappingUnavailable)
		{
			return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "A recording command list references the sparse texture.");
		}

		if ((queue == EQueue::Compute && !m_State->m_Backend.Capabilities.Features.bCompute) || (queue == EQueue::Graphics && !m_State->m_Backend.Capabilities.Features.bGraphics))
		{
			return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::Unsupported, "Rhi.Queue", "The selected queue is unavailable.");
		}

		if (m_State->m_NextTextureMappingGeneration == 0 ||
			mappings.Size() > NumericLimits<usize>::max() / 2 ||
			m_State->m_PreparedSparseTileCount > NumericLimits<usize>::max() - m_State->m_SparseTiles.Size() ||
			mappings.Size() > NumericLimits<usize>::max() - m_State->m_SparseTiles.Size() - m_State->m_PreparedSparseTileCount)
		{
			return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.TextureTiles", "Prepared mapping identity or storage capacity is exhausted.");
		}
		SharedPtr<PreparedTextureMappings> owner;
		if (!MakeSharedInto<PreparedTextureMappings>(m_State->m_Resource, &owner, m_State->m_Resource) || !owner->Mappings.Reserve(mappings.Size()) || !owner->RetainedHeaps.Reserve(mappings.Size() * 2))
		{
			return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not reserve prepared mapping ownership.");
		}
		for (const rhi::TextureTileMapping& mapping : mappings)
		{
			(void)owner->Mappings.Add(mapping);
		}

		Sort(owner->Mappings.Data(), owner->Mappings.Data() + owner->Mappings.Size(),
			[](const TextureTileMapping& left, const TextureTileMapping& right)
			{
				if (left.Subresource.ArrayLayer != right.Subresource.ArrayLayer)
				{
					return left.Subresource.ArrayLayer < right.Subresource.ArrayLayer;
				}
				if (left.Subresource.MipLevel != right.Subresource.MipLevel)
				{
					return left.Subresource.MipLevel < right.Subresource.MipLevel;
				}
				if (left.Z != right.Z)
				{
					return left.Z < right.Z;
				}
				if (left.Y != right.Y)
				{
					return left.Y < right.Y;
				}

				return left.X < right.X;
			});
		mappings = owner->Mappings.View();
		usize mappingSlot = 0;
		while (mappingSlot < m_State->m_TextureMappings.Size() && m_State->m_TextureMappings[mappingSlot])
		{
			++mappingSlot;
		}
		if (mappingSlot > UINT32_MAX || (mappingSlot == m_State->m_TextureMappings.Size() && !m_State->m_TextureMappings.Reserve(mappingSlot + 1)))
		{
			return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not reserve prepared mapping registry storage.");
		}
		const uint32 count = slot->Descriptor.MipLevels * slot->Descriptor.ArrayLayers;
		Array<TextureSubresourceTiling> subresources(m_State->m_Resource);
		Array<NativeTextureTileMapping>& native = owner->NativeMappings;
		Array<SparseTileKey> requested(m_State->m_Resource);
		Array<uint8> packedAction(m_State->m_Resource);

		if (!subresources.ResizeUninitialized(count) ||
			!native.Reserve(mappings.Size()) ||
			!requested.Reserve(mappings.Size()) ||
			!packedAction.ResizeUninitialized(slot->Descriptor.ArrayLayers) ||
			!m_State->m_SparseTiles.Reserve(m_State->m_SparseTiles.Size() + m_State->m_PreparedSparseTileCount + mappings.Size()))
		{
			return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not reserve tile mapping state.");
		}

		SetBytes(packedAction.Data(), 0, packedAction.Size());
		Result<TextureTiling> tiling = GetTextureTiling(texture, subresources.View());

		if (!tiling)
		{
			return Result<PreparedTextureMappingsHandle>::Failure(tiling.GetError());
		}

		for (usize index = 0; index < mappings.Size(); ++index)
		{
			const TextureTileMapping& mapping = mappings[index];
			const uint32 subresource = mapping.Subresource.MipLevel + mapping.Subresource.ArrayLayer * slot->Descriptor.MipLevels;

			ASSERT(mapping.Subresource.ArrayLayer < slot->Descriptor.ArrayLayers &&
				mapping.Subresource.MipLevel < slot->Descriptor.MipLevels &&
				subresource < subresources.Size() &&
				(mapping.Subresource.Aspect == ETextureAspect::Default || !(mapping.Subresource.Aspect != (IsDepthFormat(slot->Descriptor.Format) ? ETextureAspect::Depth : ETextureAspect::Color))),
				"The tile subresource is invalid.");

			const bool bPacked = mapping.Subresource.MipLevel >= tiling.Value().StandardMipCount;

			ASSERT(!(bPacked ? (mapping.Subresource.MipLevel != tiling.Value().StandardMipCount || tiling.Value().PackedTileCount == 0 || (tiling.Value().bSingleMipTail && mapping.Subresource.ArrayLayer != 0) || mapping.X >= tiling.Value().PackedTileCount || mapping.Y != 0 || mapping.Z != 0) : (mapping.X >= subresources[subresource].WidthTiles || mapping.Y >= subresources[subresource].HeightTiles || mapping.Z >= subresources[subresource].DepthTiles)),
				"The tile coordinate is outside the standard or packed mip range.");

			if (bPacked)
			{
				const uint8 action = mapping.Heap.IsValid() ? 1 : 2;
				uint8& previous = packedAction[mapping.Subresource.ArrayLayer];

				ASSERT(previous == 0 || previous == action, "A packed mip tail must be mapped or unmapped as a whole.");
				previous = action;
			}

			const SparseTileKey key{ texture, mapping.Subresource.MipLevel, mapping.Subresource.ArrayLayer, mapping.X, mapping.Y, mapping.Z };
			if (!requested.Add(key))
			{
				return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track requested tile mappings.");
			}
			void* heapNative = nullptr;

			if (mapping.Heap.IsValid())
			{
				HeapSlot* const heap = getHeap(mapping.Heap);

				ASSERT(heap->Descriptor.Usage == EHeapUsage::Textures &&
					heap->Descriptor.Memory == EBufferMemory::Device &&
					heap->LiveTextures == 0 &&
					mapping.HeapByteOffset % tiling.Value().TileBytes == 0 &&
					!(mapping.HeapByteOffset / tiling.Value().TileBytes > NumericLimits<uint32>::max()) &&
					mapping.HeapByteOffset <= heap->Descriptor.CapacityBytes &&
					tiling.Value().TileBytes <= heap->Descriptor.CapacityBytes - mapping.HeapByteOffset &&
					!(heap->SparseTileRefs > NumericLimits<uint32>::max() - mappings.Size()),
					"The tile heap or aligned heap range is invalid.");
				heapNative = heap->Native;
			}
			else ASSERT(mapping.HeapByteOffset == 0 && m_State->m_SparseTiles.Find(key) != nullptr, "The tile is not currently mapped.");

			const HeapHandle* const previousHeap = m_State->m_SparseTiles.Find(key);
			if (previousHeap != nullptr)
			{
				(void)owner->RetainedHeaps.Add(*previousHeap);
			}
			if (mapping.Heap.IsValid())
			{
				(void)owner->RetainedHeaps.Add(mapping.Heap);
			}
			if (!native.Add({ mapping.Subresource, mapping.X, mapping.Y, mapping.Z, heapNative,
							 static_cast<uint32>(mapping.HeapByteOffset / tiling.Value().TileBytes) }))
			{
				return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not prepare native tile mappings.");
			}
		}

		Sort(requested.Data(), requested.Data() + requested.Size());

		for (usize index = 0; index < requested.Size(); ++index)
		{
			const SparseTileKey& key = requested[index];

			ASSERT(index == 0 || key != requested[index - 1], "A tile appears twice in one mapping batch.");

			if (key.MipLevel != tiling.Value().StandardMipCount)
			{
				continue;
			}

			const usize first = index;

			while (index < requested.Size() && requested[index].MipLevel == key.MipLevel && requested[index].ArrayLayer == key.ArrayLayer)
			{
				++index;
			}

			ASSERT(index - first == tiling.Value().PackedTileCount, "Every packed mip tile in an array layer must be mapped together.");

			for (usize tile = first; tile < index; ++tile)
			{
				ASSERT(requested[tile].X == tile - first, "A packed mip tail must contain each tile exactly once.");
			}
			--index;
		}

		// One tail is captured in a single physical heap and a contiguous tile interval.
		for (usize index = 1; index < mappings.Size(); ++index)
		{
			const rhi::TextureTileMapping& current = mappings[index];
			const rhi::TextureTileMapping& previous = mappings[index - 1];
			ASSERT(current.Subresource.MipLevel != tiling.Value().StandardMipCount ||
				current.Subresource.MipLevel != previous.Subresource.MipLevel ||
				current.Subresource.ArrayLayer != previous.Subresource.ArrayLayer ||
				!current.Heap.IsValid() ||
				(current.Heap == previous.Heap && current.HeapByteOffset - previous.HeapByteOffset == tiling.Value().TileBytes),
				"A packed mip tail requires contiguous storage in one heap.");
		}
		if (owner->RetainedHeaps.Size() > UINT32_MAX)
		{
			return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.TextureTiles", "Sparse heap retention exceeds the native reference range.");
		}
		for (rhi::HeapHandle const heap : owner->RetainedHeaps)
		{
			if (m_State->m_Heaps[heap.Slot].PreparedSparseReferences > UINT32_MAX - owner->RetainedHeaps.Size())
			{
				return Result<PreparedTextureMappingsHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.TextureTiles", "The sparse heap retention count is exhausted.");
			}
		}

		Result<void> prepared = backend::PrepareTextureTileMappings(m_State->m_Backend.State, slot->Native, slot->Descriptor, owner->NativeMappings.View(), queue, owner->NativeStorage);
		if (!prepared)
		{
			if (!owner->NativeStorage.IsEmpty())
			{
				backend::DestroyPreparedTextureMappings(m_State->m_Backend.State, owner->NativeStorage);
			}

			return Result<PreparedTextureMappingsHandle>::Failure(prepared.GetError());
		}
		owner->Texture = texture;
		owner->Queue = queue;
		owner->Generation = m_State->m_NextTextureMappingGeneration++;
		owner->bPinsAcquired = true;
		++slot->PreparedReferences;
		slot->bSparseMappingPrepared = true;
		for (rhi::HeapHandle const heap : owner->RetainedHeaps)
		{
			++m_State->m_Heaps[heap.Slot].PreparedSparseReferences;
		}
		m_State->m_PreparedSparseTileCount += owner->Mappings.Size();
		const PreparedTextureMappingsHandle handle{ m_State->m_Owner, static_cast<uint32>(mappingSlot), owner->Generation };
		if (mappingSlot == m_State->m_TextureMappings.Size())
		{
			const bool bAdded = m_State->m_TextureMappings.Add(Move(owner));
			ASSERT(bAdded, "Cold mapping registry capacity is reserved before native preparation.");
		}
		else
		{
			m_State->m_TextureMappings[mappingSlot] = Move(owner);
		}

		return Result<PreparedTextureMappingsHandle>::Success(handle);
	}

	[[nodiscard]] PreparedTextureMappings* Device::findPreparedTextureMappings(PreparedTextureMappingsHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_TextureMappings.Size(), "A nonempty handle must belong to this device and name an allocated slot.");

		rhi::PreparedTextureMappings* owner = m_State->m_TextureMappings[handle.Slot].Get();

		ASSERT(owner != nullptr && owner->Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation and permitted ownership.");
		return owner;
	}

	void Device::releasePreparedTextureMappings(PreparedTextureMappings& owner) noexcept
	{
		if (!owner.NativeStorage.IsEmpty())
		{
			backend::DestroyPreparedTextureMappings(m_State->m_Backend.State, owner.NativeStorage);
		}
		if (owner.bPinsAcquired)
		{
			rhi::TextureSlot& texture = m_State->m_Textures[owner.Texture.Slot];
			ASSERT(texture.PreparedReferences != 0, "Sparse mapping texture pin is released once.");
			--texture.PreparedReferences;
			texture.bSparseMappingPrepared = false;
			for (rhi::HeapHandle const heap : owner.RetainedHeaps)
			{
				rhi::HeapSlot& slot = m_State->m_Heaps[heap.Slot];
				ASSERT(slot.PreparedSparseReferences != 0, "Sparse heap pins are released once.");
				--slot.PreparedSparseReferences;
			}
			owner.bPinsAcquired = false;
			m_State->m_PreparedSparseTileCount -= owner.Mappings.Size();
		}
	}

	void Device::collectPreparedTextureMappings(bool bIdle, bool bDestroy) noexcept
	{
		for (SharedPtr<rhi::PreparedTextureMappings>& owner : m_State->m_TextureMappings)
		{
			if (!owner)
			{
				continue;
			}
			bool bRetired = bDestroy || (bIdle && owner->bSubmitted && !owner->bUncertain);
			if (!bRetired && owner->bSubmitted && !owner->bUncertain && owner->Completion.IsValid())
			{
				const Result<bool> complete = backend::IsQueueComplete(m_State->m_Backend.State, owner->Completion.Queue, owner->Completion.Value >> 2);
				bRetired = complete && complete.Value();
			}
			if (bRetired)
			{
				releasePreparedTextureMappings(*owner);
				owner.Reset();
			}
		}
	}

	[[nodiscard]] Result<void> Device::DiscardPreparedTextureMappings(PreparedTextureMappingsHandle handle)
	{
		CallScope call(*this, false);
		PreparedTextureMappings* owner = findPreparedTextureMappings(handle);
		if (owner == nullptr || owner->bSubmitted)
		{
			Error error;
			error.Code = owner == nullptr ? EErrorCode::InvalidArgument : EErrorCode::Conflict;

			return Result<void>::Failure(Move(error));
		}
		releasePreparedTextureMappings(*owner);
		m_State->m_TextureMappings[handle.Slot].Reset();

		return Result<void>::Success();
	}

	[[nodiscard]] NativeSubmissionOutcome Device::SubmitPreparedTextureMappings(PreparedTextureMappingsHandle handle, Span<const SubmissionToken> prerequisites)
	{
		CallScope call(*this, false);
		NativeSubmissionOutcome outcome;
		if (m_State->m_bSubmissionFaulted)
		{
			outcome.ErrorCode = m_State->m_FirstSubmissionFailure;

			return outcome;
		}
		PreparedTextureMappings* owner = findPreparedTextureMappings(handle);
		if (owner == nullptr || owner->bSubmitted)
		{
			outcome.ErrorCode = EErrorCode::InvalidArgument;

			return outcome;
		}
		if (hasNativeQueueAccess(owner->Queue))
		{
			outcome.ErrorCode = EErrorCode::Conflict;

			return outcome;
		}
		if (!prerequisites.IsEmpty() && prerequisites.Data() == nullptr)
		{
			outcome.ErrorCode = EErrorCode::InvalidArgument;

			return outcome;
		}
		for (const rhi::SubmissionToken prerequisite : prerequisites)
		{
			if (!isKnownToken(prerequisite) || (prerequisite.Value >> 2) > backend::GetQueueLastSubmittedSerial(m_State->m_Backend.State, prerequisite.Queue))
			{
				outcome.ErrorCode = EErrorCode::InvalidArgument;

				return outcome;
			}
		}
		NativePreparedSubmissionWait waits[3]{};
		uint32 count = 0;
		uint64 required[3]{};
		for (const rhi::SubmissionToken prerequisite : prerequisites)
		{
			uint64& serial = required[static_cast<uint32>(prerequisite.Queue)];
			serial = Max(serial, prerequisite.Value >> 2);
		}
		for (uint32 index = 0; index < 3; ++index)
		{
			if (required[index] != 0)
			{
				waits[count++] = { static_cast<EQueue>(index), required[index] };
			}
		}
		NativePreparedSubmissionResult submitted =
			backend::SubmitPreparedTextureMappings(m_State->m_Backend.State, owner->NativeStorage, { waits, count });
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
		if (submitted.bAccepted && submitted.Serial != 0 && g_bFailNextAcceptedSubmit)
		{
			g_bFailNextAcceptedSubmit = false;
			submitted.ErrorCode = EErrorCode::ExternalFailure;
		}
#endif
		outcome.ErrorCode = submitted.bAccepted && submitted.Serial == 0 && submitted.ErrorCode == EErrorCode::None ? EErrorCode::ExternalFailure : submitted.ErrorCode;
		outcome.QueueWaitCount = submitted.QueueWaitCount;
		if (outcome.ErrorCode != EErrorCode::None && (submitted.bAccepted || outcome.ErrorCode != EErrorCode::Conflict))
		{
			Error error;
			error.Code = outcome.ErrorCode;
			recordSubmissionFault(error);
		}
		if (!submitted.bAccepted)
		{
			return outcome;
		}
		owner->bSubmitted = true;
		owner->bUncertain = submitted.ErrorCode != EErrorCode::None || submitted.Serial == 0;
		rhi::TextureSlot& texture = m_State->m_Textures[owner->Texture.Slot];
		texture.bSparseMappingPrepared = false;
		texture.bSparseMappingUnavailable |= owner->bUncertain;
		outcome.Disposition = owner->bUncertain ? ESubmissionDisposition::Indeterminate : ESubmissionDisposition::Accepted;
		if (submitted.Serial != 0)
		{
			outcome.Token = MakeSubmissionToken(m_State->m_Owner, owner->Queue, submitted.Serial);
			owner->Completion = outcome.Token;
		}
		if (owner->bUncertain)
		{
			return outcome;
		}
		for (rhi::TextureTileMapping const& mapping : owner->Mappings)
		{
			const SparseTileKey key{ owner->Texture, mapping.Subresource.MipLevel, mapping.Subresource.ArrayLayer, mapping.X, mapping.Y,
									mapping.Z };
			rhi::HeapHandle* old = m_State->m_SparseTiles.Find(key);
			if (old != nullptr)
			{
				--m_State->m_Heaps[old->Slot].SparseTileRefs;
				if (mapping.Heap.IsValid())
				{
					*old = mapping.Heap;
				}
				else
				{
					(void)m_State->m_SparseTiles.Erase(key);
				}
			}
			else
			{
				const HashMap<SparseTileKey, HeapHandle, SparseTileHash>::InsertResult inserted = m_State->m_SparseTiles.EmplaceRequired(key, mapping.Heap);
				ASSERT(inserted.bInserted, "Sparse tile dictionary capacity is reserved before submission.");
			}
			if (mapping.Heap.IsValid())
			{
				++m_State->m_Heaps[mapping.Heap.Slot].SparseTileRefs;
			}
		}

		return outcome;
	}

	[[nodiscard]] usize Device::uncertainTextureMappingPins(TextureHandle texture) const noexcept
	{
		usize count = 0;
		for (SharedPtr<rhi::PreparedTextureMappings> const& owner : m_State->m_TextureMappings)
		{
			if (owner && owner->Texture == texture && owner->bSubmitted && owner->bUncertain)
			{
				++count;
			}
		}

		return count;
	}

	void Device::releaseUncertainTextureMappings(TextureHandle texture) noexcept
	{
		for (SharedPtr<rhi::PreparedTextureMappings>& owner : m_State->m_TextureMappings)
		{
			if (owner && owner->Texture == texture && owner->bSubmitted && owner->bUncertain)
			{
				releasePreparedTextureMappings(*owner);
				owner.Reset();
			}
		}
	}
}
