#include "D3D12Backend.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<TextureTiling> GetTextureTiling(const State* opaque, const void* native, Span<TextureSubresourceTiling> subresources)
	{
		const State& state = *opaque;
		const Texture& texture = *static_cast<const Texture*>(native);
		const D3D12_RESOURCE_DESC resource = texture.Resource->GetDesc();
		const uint32 count = static_cast<uint32>(resource.MipLevels) * (resource.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : resource.DepthOrArraySize);
		Array<D3D12_SUBRESOURCE_TILING> nativeTilings(state.Resource);

		if (!subresources.IsEmpty() && !nativeTilings.ResizeUninitialized(count))
		{
			return Result<TextureTiling>::Failure(Failure("Rhi.Allocation", "Could not allocate texture tiling query output."));
		}
		UINT tileCount = 0;
		D3D12_PACKED_MIP_INFO packed{};
		D3D12_TILE_SHAPE shape{};
		UINT tilingCount = subresources.IsEmpty() ? 0u : count;
		state.Device->GetResourceTiling(texture.Resource.Get(), &tileCount, &packed, &shape, &tilingCount, 0,
			subresources.IsEmpty() ? nullptr : nativeTilings.Data());

		if (!subresources.IsEmpty() && tilingCount != count)
		{
			return Result<TextureTiling>::Failure(Failure("Rhi.D3D12TextureTiling", "The device returned incomplete subresource tiling."));
		}

		for (uint32 index = 0; index < tilingCount; ++index)
		{
			D3D12_SUBRESOURCE_TILING const& value = nativeTilings[index];
			subresources[index] = { value.WidthInTiles, value.HeightInTiles, value.DepthInTiles, value.StartTileIndexInOverallResource };
		}
		TextureTiling result{};
		result.TileBytes = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
		result.TileCount = tileCount;
		result.SubresourceCount = count;
		result.StandardMipCount = packed.NumStandardMips;
		result.PackedMipCount = packed.NumPackedMips;
		result.PackedTileCount = packed.NumTilesForPackedMips;
		result.FirstPackedTile = packed.StartTileIndexInOverallResource;
		result.Shape = { shape.WidthInTexels, shape.HeightInTexels, shape.DepthInTexels };

		return Result<TextureTiling>::Success(result);
	}

	[[nodiscard]] Result<uint64> UpdateTextureTileMappings(State* opaque, void* native, const TextureDesc& desc, Span<const NativeTextureTileMapping> mappings, EQueue queueKind)
	{
		State& state = *opaque;
		InvalidateIdle(state);
		state.bSubmissionMayBePending = false;
		Texture& texture = *static_cast<Texture*>(native);
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<uint64>::Failure(active.GetError());
		}

		if (state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL || !state.Submissions.TryReserveOne())
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12TextureTiles", "Could not reserve a tile mapping submission."));
		}

		for (uint32 index = 0; index < 3; ++index)
		{
			const EQueue producer = static_cast<EQueue>(index);
			const uint64 serial = texture.bUnknownAliases ? QueueLastSerial(state, producer) : Max(texture.QueueUses[index], state.UnknownResourceUses[index]);
			if (serial == 0 || producer == queueKind)
			{
				continue;
			}
			state.bSubmissionMayBePending = true;
			Result<void> ordered = QueueWaitForSubmission(&state, queueKind, producer, serial);
			if (!ordered)
			{
				return Result<uint64>::Failure(ordered.GetError());
			}
		}

		Array<D3D12_TILED_RESOURCE_COORDINATE> coordinates(state.Resource);
		Array<D3D12_TILE_RANGE_FLAGS> flags(state.Resource);
		Array<UINT> heapOffsets(state.Resource);
		Array<UINT> rangeCounts(state.Resource);

		if (!coordinates.ResizeUninitialized(mappings.Size()) ||
			!flags.ResizeUninitialized(mappings.Size()) ||
			!heapOffsets.ResizeUninitialized(mappings.Size()) ||
			!rangeCounts.ResizeUninitialized(mappings.Size()))
		{
			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not allocate tile mapping regions."));
		}
		ID3D12CommandQueue* const queue = NativeQueue(state, queueKind);

		for (usize index = 0; index < mappings.Size(); ++index)
		{
			const NativeTextureTileMapping& mapping = mappings[index];
			coordinates[index] = {};
			coordinates[index].X = mapping.X;
			coordinates[index].Y = mapping.Y;
			coordinates[index].Z = mapping.Z;
			coordinates[index].Subresource = mapping.Subresource.MipLevel + mapping.Subresource.ArrayLayer * desc.MipLevels;
			flags[index] = mapping.Heap == nullptr ? D3D12_TILE_RANGE_FLAG_NULL : D3D12_TILE_RANGE_FLAG_NONE;
			heapOffsets[index] = mapping.HeapTileOffset;
			rangeCounts[index] = 1;
		}

		for (usize first = 0; first < mappings.Size();)
		{
			const void* const nativeHeap = mappings[first].Heap;
			usize end = first + 1;

			while (end < mappings.Size() && mappings[end].Heap == nativeHeap)
			{
				++end;
			}
			ID3D12Heap* const heap = nativeHeap == nullptr ? nullptr : static_cast<const Heap*>(nativeHeap)->Native.Get();
			const UINT regionCount = static_cast<UINT>(end - first);
			state.bSubmissionMayBePending = true;
			queue->UpdateTileMappings(texture.Resource.Get(), regionCount, coordinates.Data() + first, nullptr, heap, regionCount,
				flags.Data() + first, heap == nullptr ? nullptr : heapOffsets.Data() + first,
				rangeCounts.Data() + first, D3D12_TILE_MAPPING_FLAG_NONE);
			first = end;
		}

		const uint64 serial = state.LastSerial + 1;

		if (FAILED(queue->Signal(QueueFence(state, queueKind), serial)))
		{
			state.bLost = true;

			return Result<uint64>::Failure(Failure("Rhi.D3D12TextureTiles", "Could not signal the tile mapping fence."));
		}
		state.LastSerial = serial;
		state.Submissions.RecordSubmission(serial, queueKind == EQueue::Compute ? SubmissionCompletion::ELane::Compute
			: queueKind == EQueue::Copy ? SubmissionCompletion::ELane::Copy
			: SubmissionCompletion::ELane::Direct);

		if (queueKind == EQueue::Compute)
		{
			state.LastComputeSerial = serial;
		}
		else if (queueKind == EQueue::Copy)
		{
			state.LastCopySerial = serial;
		}
		else
		{
			state.LastDirectSerial = serial;
		}
		RecordResourceUse(texture, queueKind, serial);
		if (texture.bUnknownAliases)
		{
			state.UnknownResourceUses[static_cast<uint32>(queueKind)] = serial;
		}

		return Result<uint64>::Success(serial);
	}

	[[nodiscard]] Result<void> PrepareTextureTileMappings(
		State* opaque,
		void* native,
		const TextureDesc& desc,
		Span<const NativeTextureTileMapping> mappings,
		EQueue queueKind,
		MemoryBlock& storage)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);
		if (!active)
		{
			return active;
		}

		usize rows = 0;
		usize bytes = 0;
		constexpr usize ROW_BYTES = sizeof(D3D12_TILED_RESOURCE_COORDINATE) + sizeof(D3D12_TILE_RANGE_FLAGS) + sizeof(UINT) * 2;
		if (state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL ||
			!MultiplyMemorySize(mappings.Size(), ROW_BYTES, &rows) ||
			!AddMemorySize(sizeof(PreparedTextureMappingsStorage), rows, &bytes) ||
			!MemoryBlock::CreateInto(state.Resource, &storage, { bytes, alignof(PreparedTextureMappingsStorage) }))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not reserve sparse mapping storage."));
		}

		d3d12::PreparedTextureMappingsStorage* capture = ConstructAt(static_cast<PreparedTextureMappingsStorage*>(storage.GetData()));
		if (!state.Submissions.TryReserveCredits(1))
		{
			storage.Reset();

			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not reserve sparse completion storage."));
		}
		capture->Target = static_cast<Texture*>(native);
		capture->Mappings = mappings.Data();
		capture->Count = mappings.Size();
		capture->Queue = queueKind;
		capture->bCredit = true;
		const usize count = mappings.Size();
		D3D12_TILED_RESOURCE_COORDINATE* coordinates = reinterpret_cast<D3D12_TILED_RESOURCE_COORDINATE*>(capture + 1);
		D3D12_TILE_RANGE_FLAGS* flags = reinterpret_cast<D3D12_TILE_RANGE_FLAGS*>(coordinates + count);
		UINT* offsets = reinterpret_cast<UINT*>(flags + count);
		UINT* counts = offsets + count;

		for (usize index = 0; index < count; ++index)
		{
			const rhi::NativeTextureTileMapping& mapping = mappings[index];
			coordinates[index] = { mapping.X, mapping.Y, mapping.Z,
								  mapping.Subresource.MipLevel + mapping.Subresource.ArrayLayer * desc.MipLevels };
			flags[index] = mapping.Heap == nullptr ? D3D12_TILE_RANGE_FLAG_NULL : D3D12_TILE_RANGE_FLAG_NONE;
			offsets[index] = mapping.HeapTileOffset;
			counts[index] = 1;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] NativePreparedSubmissionResult SubmitPreparedTextureMappings(State* opaque, MemoryBlock& storage, Span<const NativePreparedSubmissionWait> prerequisites)
	{
		State& state = *opaque;
		PreparedTextureMappingsStorage& capture = *static_cast<PreparedTextureMappingsStorage*>(storage.GetData());
		NativePreparedSubmissionResult result;
		if (state.bLost || state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL)
		{
			result.ErrorCode = EErrorCode::ExternalFailure;

			return result;
		}

		ID3D12CommandQueue* const queue = NativeQueue(state, capture.Queue);
		uint64 required[3]{ state.LastDirectSerial, state.LastComputeSerial, state.LastCopySerial };
		for (const rhi::NativePreparedSubmissionWait& prerequisite : prerequisites)
		{
			uint64& value = required[static_cast<uint32>(prerequisite.Queue)];
			value = Max(value, prerequisite.Serial);
		}
		for (uint32 index = 0; index < 3; ++index)
		{
			const EQueue producer = static_cast<EQueue>(index);
			if (required[index] == 0 || NativeQueue(state, producer) == queue)
			{
				continue;
			}
			result.bAccepted = true;
			++result.QueueWaitCount;
			Result<void> waited = QueueWaitForPreparedSubmission(&state, capture.Queue, producer, required[index]);
			if (!waited)
			{
				result.ErrorCode = waited.GetError().Code;

				return result;
			}
		}

		InvalidateIdle(state);
		const usize count = capture.Count;
		const D3D12_TILED_RESOURCE_COORDINATE* coordinates = reinterpret_cast<const D3D12_TILED_RESOURCE_COORDINATE*>(&capture + 1);
		const D3D12_TILE_RANGE_FLAGS* flags = reinterpret_cast<const D3D12_TILE_RANGE_FLAGS*>(coordinates + count);
		const UINT* offsets = reinterpret_cast<const UINT*>(flags + count);
		const UINT* counts = offsets + count;
		for (usize first = 0; first < count;)
		{
			const void* nativeHeap = capture.Mappings[first].Heap;
			usize end = first + 1;
			while (end < count && capture.Mappings[end].Heap == nativeHeap)
			{
				++end;
			}
			ID3D12Heap* const heap = nativeHeap == nullptr ? nullptr : static_cast<const Heap*>(nativeHeap)->Native.Get();
			const UINT regionCount = static_cast<UINT>(end - first);
			result.bAccepted = true;
			queue->UpdateTileMappings(capture.Target->Resource.Get(), regionCount, coordinates + first, nullptr, heap, regionCount,
				flags + first, heap == nullptr ? nullptr : offsets + first, counts + first,
				D3D12_TILE_MAPPING_FLAG_NONE);
			first = end;
		}

		const uint64 serial = state.LastSerial + 1;
		if (FAILED(queue->Signal(QueueFence(state, capture.Queue), serial)))
		{
			state.bLost = true;
			result.ErrorCode = EErrorCode::ExternalFailure;

			return result;
		}
		ASSERT(capture.bCredit, "Sparse submission consumes its reserved completion credit once.");
		state.Submissions.ReleaseCredit();
		capture.bCredit = false;
		state.LastSerial = serial;
		state.Submissions.RecordSubmission(serial, capture.Queue == EQueue::Compute ? SubmissionCompletion::ELane::Compute
			: capture.Queue == EQueue::Copy ? SubmissionCompletion::ELane::Copy
			: SubmissionCompletion::ELane::Direct);
		if (capture.Queue == EQueue::Compute)
		{
			state.LastComputeSerial = serial;
		}
		else if (capture.Queue == EQueue::Copy)
		{
			state.LastCopySerial = serial;
		}
		else
		{
			state.LastDirectSerial = serial;
		}
		RecordResourceUse(*capture.Target, capture.Queue, serial);
		if (capture.Target->bUnknownAliases)
		{
			state.UnknownResourceUses[static_cast<uint32>(capture.Queue)] = serial;
		}
		result.Serial = serial;

		return result;
	}

	void DestroyPreparedTextureMappings(State* opaque, MemoryBlock& storage) noexcept
	{
		State& state = *opaque;
		PreparedTextureMappingsStorage& capture = *static_cast<PreparedTextureMappingsStorage*>(storage.GetData());
		if (capture.bCredit)
		{
			state.Submissions.ReleaseCredit();
		}
		storage.Reset();
	}
}
#endif
