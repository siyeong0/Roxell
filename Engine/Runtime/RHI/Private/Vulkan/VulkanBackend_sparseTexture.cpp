#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<TextureTiling> GetTextureTiling(const void*, const void* native, Span<TextureSubresourceTiling> subresources)
	{
		const Texture& texture = *static_cast<const Texture*>(native);

		if (!texture.bSparse)
		{
			return Result<TextureTiling>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTextureTiling", "Only sparse images expose tile placement.");
		}

		const VkSparseImageMemoryRequirements& sparse = texture.SparseRequirements;
		const VkExtent3D& shape = sparse.formatProperties.imageGranularity;
		const uint64 tileBytes = texture.SparseMemoryRequirements.alignment;
		const uint32 standard = sparse.imageMipTailFirstLod;
		const uint64 packedTiles = sparse.imageMipTailSize / tileBytes;
		const bool bSingleTail = (sparse.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT) != 0;
		uint64 nextTile = 0;
		uint64 firstPacked = 0;

		for (uint32 layer = 0; layer < texture.ArrayLayers; ++layer)
		{
			for (uint32 mip = 0; mip < texture.MipLevels; ++mip)
			{
				const uint32 index = mip + layer * texture.MipLevels;

				if (mip >= standard)
				{
					if (!subresources.IsEmpty())
					{
						subresources[index] = { 0, 0, 0, UINT32_MAX };
					}
					continue;
				}

				const uint64 width = Max<uint64>(1, static_cast<uint64>(texture.Width) >> mip);
				const uint64 height = Max<uint64>(1, static_cast<uint64>(texture.Height) >> mip);
				const uint64 wide = (width + shape.width - 1) / shape.width;
				const uint64 high = (height + shape.height - 1) / shape.height;

				if (nextTile + wide * high > NumericLimits<uint32>::max())
				{
					return Result<TextureTiling>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTextureTiling", "The sparse tile index exceeds the RHI range.");
				}

				if (!subresources.IsEmpty())
				{
					subresources[index] = { static_cast<uint32>(wide), static_cast<uint32>(high), 1,
										   static_cast<uint32>(nextTile) };
				}
				nextTile += wide * high;
			}

			if (layer == 0 && !bSingleTail)
			{
				firstPacked = nextTile;
			}

			if (!bSingleTail)
			{
				nextTile += packedTiles;
			}
		}

		if (bSingleTail)
		{
			firstPacked = nextTile;
			nextTile += packedTiles;
		}

		if (nextTile > NumericLimits<uint32>::max() || packedTiles > NumericLimits<uint32>::max())
		{
			return Result<TextureTiling>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTextureTiling", "The sparse tile count exceeds the RHI range.");
		}
		TextureTiling result{};
		result.TileBytes = static_cast<uint32>(tileBytes);
		result.TileCount = static_cast<uint32>(nextTile);
		result.SubresourceCount = texture.MipLevels * texture.ArrayLayers;
		result.StandardMipCount = standard;
		result.PackedMipCount = texture.MipLevels - standard;
		result.PackedTileCount = static_cast<uint32>(packedTiles);
		result.FirstPackedTile = packedTiles == 0 ? 0 : static_cast<uint32>(firstPacked);
		result.bSingleMipTail = bSingleTail;
		result.Shape = { shape.width, shape.height, shape.depth };

		return Result<TextureTiling>::Success(result);
	}

	[[nodiscard]] Result<uint64> UpdateTextureTileMappings(State* opaque, void* native, const TextureDesc& desc, Span<const NativeTextureTileMapping> mappings, EQueue queueKind)
	{
		State& state = *opaque;
		Texture& texture = *static_cast<Texture*>(native);
		state.bSubmissionMayBePending = false;

		if (state.bLost)
		{
			return Result<uint64>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		if ((QueueFlags(state, queueKind) & VK_QUEUE_SPARSE_BINDING_BIT) == 0)
		{
			return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTextureTiles", "The selected queue does not support sparse binding.");
		}

		if (!texture.bSparse || state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL || !state.Submissions.TryReserveOne())
		{
			return Result<uint64>::Failure(Failure("Rhi.VulkanTextureTiles", "Could not reserve a sparse binding submission."));
		}

		Array<VkSparseImageMemoryBind> imageBinds(state.Resource);
		Array<VkSparseMemoryBind> opaqueBinds(state.Resource);

		if (!imageBinds.Reserve(mappings.Size()) || !opaqueBinds.Reserve(mappings.Size()))
		{
			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan sparse image bindings."));
		}

		const VkSparseImageMemoryRequirements& sparse = texture.SparseRequirements;
		const VkExtent3D& granularity = sparse.formatProperties.imageGranularity;
		const uint64 tileBytes = texture.SparseMemoryRequirements.alignment;
		const bool bSingleTail = (sparse.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT) != 0;

		for (const NativeTextureTileMapping& mapping : mappings)
		{
			VkDeviceMemory memory = VK_NULL_HANDLE;

			if (mapping.Heap != nullptr)
			{
				Heap& heap = *static_cast<Heap*>(mapping.Heap);
				uint32 type = heap.MemoryTypeIndex;

				if (heap.Memory == VK_NULL_HANDLE)
				{
					for (uint32 index = 0; index < state.MemoryProperties.memoryTypeCount; ++index)
					{
						if ((texture.SparseMemoryRequirements.memoryTypeBits & (1u << index)) != 0 &&
							(state.MemoryProperties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
						{
							type = index;
							break;
						}
					}

					if (type == UINT32_MAX)
					{
						return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTextureTiles", "The image has no compatible device-local heap memory type.");
					}
					VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
					allocation.allocationSize = heap.Descriptor.CapacityBytes;
					allocation.memoryTypeIndex = type;

					if (state.F.AllocateMemory(state.Device, &allocation, nullptr, &heap.Memory) != VK_SUCCESS)
					{
						return Result<uint64>::Failure(Failure("Rhi.VulkanTextureTiles", "Could not allocate sparse texture heap memory."));
					}
					heap.MemoryTypeIndex = type;
				}

				if ((texture.SparseMemoryRequirements.memoryTypeBits & (1u << heap.MemoryTypeIndex)) == 0)
				{
					return Result<uint64>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTextureTiles", "The heap memory type is incompatible with this sparse image.");
				}
				memory = heap.Memory;
			}

			const uint64 heapOffset = static_cast<uint64>(mapping.HeapTileOffset) * tileBytes;

			if (mapping.Subresource.MipLevel >= sparse.imageMipTailFirstLod)
			{
				VkSparseMemoryBind bind{};
				bind.resourceOffset =
					sparse.imageMipTailOffset +
					(bSingleTail ? 0 : static_cast<uint64>(mapping.Subresource.ArrayLayer) * sparse.imageMipTailStride) +
					static_cast<uint64>(mapping.X) * tileBytes;
				bind.size = tileBytes;
				bind.memory = memory;
				bind.memoryOffset = memory == VK_NULL_HANDLE ? 0 : heapOffset;
				const bool bAdded = opaqueBinds.Add(bind);
				ASSERT(bAdded, "Reserved sparse tail binding storage must accept the mapping.");
			}
			else
			{
				const uint32 mip = mapping.Subresource.MipLevel;
				const uint32 mipWidth = Max(1u, desc.Width >> mip);
				const uint32 mipHeight = Max(1u, desc.Height >> mip);
				const uint32 x = mapping.X * granularity.width;
				const uint32 y = mapping.Y * granularity.height;
				VkSparseImageMemoryBind bind{};
				bind.subresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, mapping.Subresource.ArrayLayer };
				bind.offset = { static_cast<int32>(x), static_cast<int32>(y), 0 };
				bind.extent = { Min(granularity.width, mipWidth - x), Min(granularity.height, mipHeight - y), 1 };
				bind.memory = memory;
				bind.memoryOffset = memory == VK_NULL_HANDLE ? 0 : heapOffset;
				const bool bAdded = imageBinds.Add(bind);
				ASSERT(bAdded, "Reserved sparse image binding storage must accept the mapping.");
			}
		}

		const VkSemaphore timelineSemaphore = QueueTimeline(state, queueKind);
		VkSemaphore& ready = state.SparseReady[static_cast<uint32>(queueKind)];

		if (ready == VK_NULL_HANDLE)
		{
			VkSemaphoreCreateInfo info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			if (state.F.CreateSemaphore(state.Device, &info, nullptr, &ready) != VK_SUCCESS)
			{
				return Result<uint64>::Failure(Failure("Rhi.VulkanTextureTiles", "Could not create sparse binding synchronization."));
			}
		}
		VkSemaphore waits[2]{ timelineSemaphore, QueueTimeline(state, texture.LastQueue) };
		uint64 values[2]{ QueueLastSerial(state, queueKind), texture.LastUse };
		uint32 waitCount = values[0] != 0 ? 1u : 0u;

		if (values[1] != 0 && waits[1] != timelineSemaphore)
		{
			waits[waitCount] = waits[1];
			values[waitCount++] = values[1];
		}
		VkSparseImageMemoryBindInfo imageInfo{};
		imageInfo.image = texture.Handle;
		imageInfo.bindCount = static_cast<uint32>(imageBinds.Size());
		imageInfo.pBinds = imageBinds.Data();
		VkSparseImageOpaqueMemoryBindInfo opaqueInfo{};
		opaqueInfo.image = texture.Handle;
		opaqueInfo.bindCount = static_cast<uint32>(opaqueBinds.Size());
		opaqueInfo.pBinds = opaqueBinds.Data();
		const uint64 serial = state.LastSerial + 1;
		VkTimelineSemaphoreSubmitInfo timeline{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
		timeline.waitSemaphoreValueCount = waitCount;
		timeline.pWaitSemaphoreValues = values;
		const uint64 binaryValue = 0;
		timeline.signalSemaphoreValueCount = 1;
		timeline.pSignalSemaphoreValues = &binaryValue;
		VkBindSparseInfo submit{ VK_STRUCTURE_TYPE_BIND_SPARSE_INFO };
		submit.pNext = &timeline;
		submit.waitSemaphoreCount = waitCount;
		submit.pWaitSemaphores = waits;
		submit.imageBindCount = imageBinds.IsEmpty() ? 0u : 1u;
		submit.pImageBinds = imageBinds.IsEmpty() ? nullptr : &imageInfo;
		submit.imageOpaqueBindCount = opaqueBinds.IsEmpty() ? 0u : 1u;
		submit.pImageOpaqueBinds = opaqueBinds.IsEmpty() ? nullptr : &opaqueInfo;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &ready;
		const uint32 fenceIndex = static_cast<uint32>(queueKind);

		if (state.bSparseFencePending[fenceIndex])
		{
			Result<void> waited = WaitNativeFence(state, state.SparseFences[fenceIndex], 5'000 * NANOSECONDS_PER_MILLISECOND);

			if (!waited)
			{
				return Result<uint64>::Failure(waited.GetError());
			}
			state.bSparseFencePending[fenceIndex] = false;
		}

		if (!state.SparseFences[fenceIndex])
		{
			VkFenceCreateInfo create{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			if (state.F.CreateFence(state.Device, &create, nullptr, &state.SparseFences[fenceIndex]) != VK_SUCCESS)
			{
				return Result<uint64>::Failure(Failure("Rhi.VulkanTextureTiles", "Could not create the sparse completion fence."));
			}
		}
		else if (state.F.ResetFences(state.Device, 1, &state.SparseFences[fenceIndex]) != VK_SUCCESS)
		{
			return Result<uint64>::Failure(Failure("Rhi.VulkanTextureTiles", "Could not reset the sparse completion fence."));
		}
		InvalidateIdle(state);
		state.bSubmissionMayBePending = true;
		const VkResult sparseResult = state.F.QueueBindSparse(NativeQueue(state, queueKind), 1, &submit, state.SparseFences[fenceIndex]);
		state.bSparseFencePending[fenceIndex] = sparseResult == VK_SUCCESS;
		state.bDeviceLossConfirmed |= sparseResult == VK_ERROR_DEVICE_LOST;

		if (sparseResult != VK_SUCCESS)
		{
			state.bLost = true;

			return Result<uint64>::Failure(Failure("Rhi.VulkanTextureTiles", "Could not submit sparse image bindings."));
		}
		// Sparse binding does not participate in ordinary command submission order.
		// Join it back into the queue timeline with an explicit semaphore wait.
		VkTimelineSemaphoreSubmitInfo completion{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
		completion.waitSemaphoreValueCount = 1;
		completion.pWaitSemaphoreValues = &binaryValue;
		completion.signalSemaphoreValueCount = 1;
		completion.pSignalSemaphoreValues = &serial;
		const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkSubmitInfo join{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		join.pNext = &completion;
		join.waitSemaphoreCount = 1;
		join.pWaitSemaphores = &ready;
		join.pWaitDstStageMask = &stage;
		join.signalSemaphoreCount = 1;
		join.pSignalSemaphores = &timelineSemaphore;
		const VkResult joinedResult = state.F.QueueSubmit(NativeQueue(state, queueKind), 1, &join, VK_NULL_HANDLE);
		state.bDeviceLossConfirmed |= joinedResult == VK_ERROR_DEVICE_LOST;

		if (joinedResult != VK_SUCCESS)
		{
			state.bLost = true;

			return Result<uint64>::Failure(Failure("Rhi.VulkanTextureTiles", "Could not publish sparse binding completion."));
		}
		state.LastSerial = serial;
		const rhi::SubmissionCompletion::ELane lane =
			timelineSemaphore == state.ComputeTimeline && state.bSeparateComputeQueue ? SubmissionCompletion::ELane::Compute
			: timelineSemaphore == state.CopyTimeline && state.bSeparateCopyQueue ? SubmissionCompletion::ELane::Copy
			: SubmissionCompletion::ELane::Direct;
		state.Submissions.RecordSubmission(serial, lane);

		if (lane == SubmissionCompletion::ELane::Compute)
		{
			state.LastComputeSerial = serial;
		}
		else if (lane == SubmissionCompletion::ELane::Copy)
		{
			state.LastCopySerial = serial;
		}
		else
		{
			state.LastGraphicsSerial = serial;
		}
		texture.LastUse = serial;
		texture.LastQueue = queueKind;

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
		Texture& texture = *static_cast<Texture*>(native);
		if (state.bLost || state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}
		if ((QueueFlags(state, queueKind) & VK_QUEUE_SPARSE_BINDING_BIT) == 0)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTextureTiles", "The selected queue does not support sparse binding.");
		}
		usize rows = 0;
		usize bytes = 0;
		if (!MultiplyMemorySize(mappings.Size(), sizeof(VkSparseImageMemoryBind) + sizeof(VkSparseMemoryBind), &rows) ||
			!AddMemorySize(sizeof(PreparedTextureMappingsStorage), rows, &bytes) ||
			!MemoryBlock::CreateInto(state.Resource, &storage, { bytes, alignof(PreparedTextureMappingsStorage) }))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not reserve sparse mapping storage."));
		}
		vulkan::PreparedTextureMappingsStorage* capture = ConstructAt(static_cast<PreparedTextureMappingsStorage*>(storage.GetData()));
		capture->Target = &texture;
		capture->Queue = queueKind;
		capture->Capacity = mappings.Size();
		VkSparseImageMemoryBind* imageBinds = reinterpret_cast<VkSparseImageMemoryBind*>(capture + 1);
		VkSparseMemoryBind* opaqueBinds = reinterpret_cast<VkSparseMemoryBind*>(imageBinds + capture->Capacity);
		const VkSparseImageMemoryRequirements& sparse = texture.SparseRequirements;
		const VkExtent3D& granularity = sparse.formatProperties.imageGranularity;
		const uint64 tileBytes = texture.SparseMemoryRequirements.alignment;
		const bool bSingleTail = (sparse.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT) != 0;
		for (const NativeTextureTileMapping& mapping : mappings)
		{
			VkDeviceMemory memory = VK_NULL_HANDLE;

			if (mapping.Heap != nullptr)
			{
				Heap& heap = *static_cast<Heap*>(mapping.Heap);
				uint32 type = heap.MemoryTypeIndex;

				if (heap.Memory == VK_NULL_HANDLE)
				{
					for (uint32 index = 0; index < state.MemoryProperties.memoryTypeCount; ++index)
					{
						if ((texture.SparseMemoryRequirements.memoryTypeBits & (1u << index)) != 0 &&
							(state.MemoryProperties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
						{
							type = index;
							break;
						}
					}

					if (type == UINT32_MAX)
					{
						return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTextureTiles", "The image has no compatible device-local heap memory type.");
					}
					VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
					allocation.allocationSize = heap.Descriptor.CapacityBytes;
					allocation.memoryTypeIndex = type;

					if (state.F.AllocateMemory(state.Device, &allocation, nullptr, &heap.Memory) != VK_SUCCESS)
					{
						return Result<void>::Failure(Failure("Rhi.VulkanTextureTiles", "Could not allocate sparse texture heap memory."));
					}
					heap.MemoryTypeIndex = type;
				}

				if ((texture.SparseMemoryRequirements.memoryTypeBits & (1u << heap.MemoryTypeIndex)) == 0)
				{
					return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTextureTiles", "The heap memory type is incompatible with this sparse image.");
				}
				memory = heap.Memory;
			}

			const uint64 heapOffset = static_cast<uint64>(mapping.HeapTileOffset) * tileBytes;

			if (mapping.Subresource.MipLevel >= sparse.imageMipTailFirstLod)
			{
				VkSparseMemoryBind bind{};
				bind.resourceOffset =
					sparse.imageMipTailOffset +
					(bSingleTail ? 0 : static_cast<uint64>(mapping.Subresource.ArrayLayer) * sparse.imageMipTailStride) +
					static_cast<uint64>(mapping.X) * tileBytes;
				bind.size = tileBytes;
				bind.memory = memory;
				bind.memoryOffset = memory == VK_NULL_HANDLE ? 0 : heapOffset;
				opaqueBinds[capture->OpaqueCount++] = bind;
			}
			else
			{
				const uint32 mip = mapping.Subresource.MipLevel;
				const uint32 mipWidth = Max(1u, desc.Width >> mip);
				const uint32 mipHeight = Max(1u, desc.Height >> mip);
				const uint32 x = mapping.X * granularity.width;
				const uint32 y = mapping.Y * granularity.height;
				VkSparseImageMemoryBind bind{};
				bind.subresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, mapping.Subresource.ArrayLayer };
				bind.offset = { static_cast<int32>(x), static_cast<int32>(y), 0 };
				bind.extent = { Min(granularity.width, mipWidth - x), Min(granularity.height, mipHeight - y), 1 };
				bind.memory = memory;
				bind.memoryOffset = memory == VK_NULL_HANDLE ? 0 : heapOffset;
				imageBinds[capture->ImageCount++] = bind;
			}
		}

		VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		if (state.F.CreateSemaphore(state.Device, &semaphoreInfo, nullptr, &capture->Ready) != VK_SUCCESS ||
			state.F.CreateFence(state.Device, &fenceInfo, nullptr, &capture->Fence) != VK_SUCCESS ||
			!state.Submissions.TryReserveCredits(1))
		{
			if (capture->Ready != VK_NULL_HANDLE)
			{
				state.F.DestroySemaphore(state.Device, capture->Ready, nullptr);
			}
			if (capture->Fence != VK_NULL_HANDLE)
			{
				state.F.DestroyFence(state.Device, capture->Fence, nullptr);
			}
			storage.Reset();

			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not reserve sparse completion storage."));
		}
		capture->bCredit = true;

		return Result<void>::Success();
	}

	void DestroyPreparedTextureMappings(State* opaque, MemoryBlock& storage) noexcept
	{
		State& state = *opaque;
		PreparedTextureMappingsStorage& capture = *static_cast<PreparedTextureMappingsStorage*>(storage.GetData());
		if (capture.bCredit)
		{
			state.Submissions.ReleaseCredit();
		}
		state.F.DestroySemaphore(state.Device, capture.Ready, nullptr);
		state.F.DestroyFence(state.Device, capture.Fence, nullptr);
		storage.Reset();
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
		uint64 required[3]{};
		for (uint32 index = 0; index < 3; ++index)
		{
			required[index] = QueueLastSerial(state, static_cast<EQueue>(index));
		}
		for (const rhi::NativePreparedSubmissionWait& prerequisite : prerequisites)
		{
			uint64& value = required[static_cast<uint32>(prerequisite.Queue)];
			value = Max(value, prerequisite.Serial);
		}
		VkSemaphore waits[3]{};
		uint64 values[3]{};
		uint32 waitCount = 0;
		for (uint32 index = 0; index < 3; ++index)
		{
			if (required[index] == 0)
			{
				continue;
			}
			const VkSemaphore semaphore = QueueTimeline(state, static_cast<EQueue>(index));
			uint32 position = 0;
			while (position < waitCount && waits[position] != semaphore)
			{
				++position;
			}
			if (position == waitCount)
			{
				waits[waitCount++] = semaphore;
			}
			values[position] = Max(values[position], required[index]);
		}
		const VkSparseImageMemoryBind* imageBinds = reinterpret_cast<const VkSparseImageMemoryBind*>(&capture + 1);
		const VkSparseMemoryBind* opaqueBinds = reinterpret_cast<const VkSparseMemoryBind*>(imageBinds + capture.Capacity);
		VkSparseImageMemoryBindInfo images{};
		images.image = capture.Target->Handle;
		images.bindCount = capture.ImageCount;
		images.pBinds = imageBinds;
		VkSparseImageOpaqueMemoryBindInfo tails{};
		tails.image = capture.Target->Handle;
		tails.bindCount = capture.OpaqueCount;
		tails.pBinds = opaqueBinds;
		const uint64 binaryValue = 0;
		VkTimelineSemaphoreSubmitInfo timeline{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
		timeline.waitSemaphoreValueCount = waitCount;
		timeline.pWaitSemaphoreValues = values;
		timeline.signalSemaphoreValueCount = 1;
		timeline.pSignalSemaphoreValues = &binaryValue;
		VkBindSparseInfo submit{ VK_STRUCTURE_TYPE_BIND_SPARSE_INFO };
		submit.pNext = &timeline;
		submit.waitSemaphoreCount = waitCount;
		submit.pWaitSemaphores = waits;
		submit.imageBindCount = capture.ImageCount == 0 ? 0u : 1u;
		submit.pImageBinds = capture.ImageCount == 0 ? nullptr : &images;
		submit.imageOpaqueBindCount = capture.OpaqueCount == 0 ? 0u : 1u;
		submit.pImageOpaqueBinds = capture.OpaqueCount == 0 ? nullptr : &tails;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &capture.Ready;
		InvalidateIdle(state);
		// Sparse binding is explicitly joined to the normal timeline, including same-queue consumers.
		result.bAccepted = true;
		result.QueueWaitCount = waitCount;
		const VkResult bound = state.F.QueueBindSparse(NativeQueue(state, capture.Queue), 1, &submit, capture.Fence);
		state.bDeviceLossConfirmed |= bound == VK_ERROR_DEVICE_LOST;
		if (bound != VK_SUCCESS)
		{
			state.bLost = true;
			result.ErrorCode = EErrorCode::ExternalFailure;

			return result;
		}
		const uint64 serial = state.LastSerial + 1;
		const VkSemaphore timelineSemaphore = QueueTimeline(state, capture.Queue);
		const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkTimelineSemaphoreSubmitInfo completion{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
		completion.waitSemaphoreValueCount = 1;
		completion.pWaitSemaphoreValues = &binaryValue;
		completion.signalSemaphoreValueCount = 1;
		completion.pSignalSemaphoreValues = &serial;
		VkSubmitInfo join{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		join.pNext = &completion;
		join.waitSemaphoreCount = 1;
		join.pWaitSemaphores = &capture.Ready;
		join.pWaitDstStageMask = &stage;
		join.signalSemaphoreCount = 1;
		join.pSignalSemaphores = &timelineSemaphore;
		++result.QueueWaitCount;
		const VkResult joined = state.F.QueueSubmit(NativeQueue(state, capture.Queue), 1, &join, VK_NULL_HANDLE);
		state.bDeviceLossConfirmed |= joined == VK_ERROR_DEVICE_LOST;
		if (joined != VK_SUCCESS)
		{
			state.bLost = true;
			result.ErrorCode = EErrorCode::ExternalFailure;

			return result;
		}
		ASSERT(capture.bCredit, "Sparse submission consumes its reserved completion credit once.");
		state.Submissions.ReleaseCredit();
		capture.bCredit = false;
		state.LastSerial = serial;
		const rhi::SubmissionCompletion::ELane lane =
			timelineSemaphore == state.ComputeTimeline && state.bSeparateComputeQueue ? SubmissionCompletion::ELane::Compute
			: timelineSemaphore == state.CopyTimeline && state.bSeparateCopyQueue ? SubmissionCompletion::ELane::Copy
			: SubmissionCompletion::ELane::Direct;
		state.Submissions.RecordSubmission(serial, lane);
		if (lane == SubmissionCompletion::ELane::Compute)
		{
			state.LastComputeSerial = serial;
		}
		else if (lane == SubmissionCompletion::ELane::Copy)
		{
			state.LastCopySerial = serial;
		}
		else
		{
			state.LastGraphicsSerial = serial;
		}
		capture.Target->LastUse = serial;
		capture.Target->LastQueue = capture.Queue;
		result.Serial = serial;

		return result;
	}
}
