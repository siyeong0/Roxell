#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::copyPreparedBuffer(CommandSlot& command, const BufferCopy& copy)
	{
		PreparedRecordingPhase* phase = preparedPhase(&command);
		const BufferSlot* source = findBuffer(copy.Source);
		const BufferSlot* destination = findBuffer(copy.Destination);
		ASSERT(!(phase == nullptr ||
				command.bRendering ||
				source == nullptr ||
				destination == nullptr ||
				source->bMapped ||
				destination->bMapped ||
				copy.Source == copy.Destination ||
				copy.ByteCount == 0 ||
				!HasUsage(source->Usage, EBufferUsage::CopySource) ||
				!HasUsage(destination->Usage, EBufferUsage::CopyDestination) ||
				!Fits(source->ByteCount, copy.SourceOffsetBytes, copy.ByteCount) ||
				!Fits(destination->ByteCount, copy.DestinationOffsetBytes, copy.ByteCount)),
			"Prepared recording requires valid admitted input and capacity.");
		ASSERT(!(!preparedBufferContains(*phase, copy.Source, EBufferState::CopySource, copy.SourceOffsetBytes, copy.ByteCount) ||
				!preparedBufferContains(*phase, copy.Destination, EBufferState::CopyDestination, copy.DestinationOffsetBytes, copy.ByteCount)),
			"Prepared recording requires valid admitted input and capacity.");

		ASSERT(!(phase->Used.BufferCopies >= phase->Capacity.BufferCopies), "Prepared recording requires valid admitted input and capacity.");

		const NativeCopy native{ source->Native, destination->Native, copy.SourceOffsetBytes, copy.DestinationOffsetBytes, copy.ByteCount };
		Result<void> result = recordNative([&] { return backend::RecordBufferCopy(m_State->m_Backend.State, command.Native, native); });
		if (!result)
		{
			return preparedEmission(command, Move(result));
		}

		++phase->Used.BufferCopies;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::validateBufferDesc(const BufferDesc& desc) const
	{
		if (HasUsage(desc.Usage, EBufferUsage::AccelBuildInput) && !HasUsage(desc.Usage, EBufferUsage::DeviceAddress))
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "Rhi.AccelBuildInput", "Acceleration structure build inputs require a device address.");
		}

		if (HasUsage(desc.Usage, EBufferUsage::AccelStorage) && (desc.Usage != (EBufferUsage::AccelStorage | EBufferUsage::DeviceAddress) || desc.Memory != EBufferMemory::Device))
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "Rhi.AccelStorage", "Acceleration structure storage requires device memory and a device address.");
		}

		if (HasUsage(desc.Usage, EBufferUsage::MicromapBuildInput) && !HasUsage(desc.Usage, EBufferUsage::DeviceAddress))
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "Rhi.OpacityMicromap", "Micromap build inputs require a device address.");
		}

		if (HasUsage(desc.Usage, EBufferUsage::MicromapStorage) && (desc.Usage != (EBufferUsage::MicromapStorage | EBufferUsage::DeviceAddress) || desc.Memory != EBufferMemory::Device))
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "Rhi.OpacityMicromap", "Micromap storage requires device memory and a device address.");
		}

		if ((HasUsage(desc.Usage, EBufferUsage::TypedReadOnly) || HasUsage(desc.Usage, EBufferUsage::TypedStorage)) && !HasUsage(desc.Usage, EBufferUsage::Storage))
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "Rhi.BufferDesc", "Typed buffer access requires Storage usage.");
		}

		if (desc.ByteCount == 0 ||
			desc.Usage == EBufferUsage::None ||
			desc.Memory > EBufferMemory::CpuRead ||
			(desc.Memory == EBufferMemory::CpuWrite && desc.Usage != EBufferUsage::CopySource) ||
			(desc.Memory == EBufferMemory::CpuRead && desc.Usage != EBufferUsage::CopyDestination) ||
			(static_cast<uint32>(desc.Usage) & ~KNOWN_USAGE_BITS) != 0)
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "Rhi.BufferDesc", "The buffer size or usage is invalid for this device.");
		}

		if (HasUsage(desc.Usage, EBufferUsage::DeviceAddress) && !m_State->m_Backend.Capabilities.Features.bBufferDeviceAddress)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BufferDeviceAddress", "The device does not support buffer device addresses.");
		}

		if (HasUsage(desc.Usage, EBufferUsage::AccelBuildInput) && !m_State->m_Backend.bAccelerationStructureBuildInput)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.AccelBuildInput", "The device does not support acceleration structure build input buffers.");
		}

		if (HasUsage(desc.Usage, EBufferUsage::AccelStorage) && !m_State->m_Backend.bAccelerationStructureBuildInput)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.AccelStorage", "The device does not support acceleration structure storage buffers.");
		}

		if ((HasUsage(desc.Usage, EBufferUsage::MicromapBuildInput) || HasUsage(desc.Usage, EBufferUsage::MicromapStorage)) && !m_State->m_Backend.bOpacityMicromap)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.OpacityMicromap", "The device does not support micromap buffers.");
		}

		if (desc.ByteCount > m_State->m_Backend.Capabilities.Limits.MaximumBufferByteCount)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BufferDesc", "The buffer exceeds the device size limit.");
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<MemoryRequirements> Device::GetBufferMemoryRequirements(const BufferDesc& desc) const
	{
		CallScope call(*this, true);
		Result<void> valid = validateBufferDesc(desc);

		if (!valid)
		{
			ASSERT(valid.GetError().Code != EErrorCode::InvalidArgument, "A valid buffer descriptor is required.");

			return Result<MemoryRequirements>::Failure(valid.GetError());
		}

		Result<MemoryRequirements> requirements = backend::GetBufferMemoryRequirements(m_State->m_Backend.State, desc);

		if (!requirements)
		{
			return requirements;
		}

		if (requirements.Value().ByteCount < desc.ByteCount || requirements.Value().AlignmentBytes == 0)
		{
			return Result<MemoryRequirements>::Failure(EErrorCode::ExternalFailure, "Rhi.MemoryRequirements", "The backend returned invalid buffer memory requirements.");
		}

		return requirements;
	}

	[[nodiscard]] Result<BufferHandle> Device::CreateBuffer(const BufferDesc& desc)
	{
		CallScope call(*this, false);

		return createBufferInternal(desc, {}, 0, 0);
	}

	[[nodiscard]] Result<BufferHandle> Device::ImportBuffer(const BufferDesc& desc, void* nativeBuffer)
	{
		CallScope call(*this, false);

		ASSERT(nativeBuffer != nullptr, "The native buffer handle must not be null.");

		if (desc.Memory != EBufferMemory::Device)
		{
			return Result<BufferHandle>::Failure(EErrorCode::Unsupported, "Rhi.NativeBuffer", "Imported buffers must use device memory.");
		}

		return createBufferInternal(desc, {}, 0, 0, nativeBuffer);
	}

	[[nodiscard]] Result<NativeResourceHandle> Device::GetNativeBuffer(BufferHandle handle)
	{
		CallScope call(*this, true);
		if (hasPreparedBuffer(handle))
		{
			Error error;
			error.Code = EErrorCode::Conflict;

			return Result<NativeResourceHandle>::Failure(Move(error));
		}
		BufferSlot* const slot = findBuffer(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (m_State->m_Kind == EBackend::Null)
		{
			return Result<NativeResourceHandle>::Failure(EErrorCode::Unsupported, "Rhi.NativeBuffer", "The Null backend has no native buffers.");
		}

		return Result<NativeResourceHandle>::Success({ m_State->m_Kind, backend::GetNativeBuffer(slot->Native) });
	}

	[[nodiscard]] Result<BufferHandle> Device::CreatePlacedBuffer(const BufferDesc& desc, HeapHandle heap, uint64 offset)
	{
		CallScope call(*this, false);
		Result<void> valid = validateBufferDesc(desc);

		if (!valid)
		{
			ASSERT(valid.GetError().Code != EErrorCode::InvalidArgument, "A valid buffer descriptor is required.");

			return Result<BufferHandle>::Failure(valid.GetError());
		}
		HeapSlot* const owner = getHeap(heap);

		ASSERT(!(owner->Descriptor.Usage != EHeapUsage::Buffers), "The heap does not accept buffers.");

		ASSERT(!(desc.Memory != owner->Descriptor.Memory), "The buffer and heap memory types differ.");

		Result<MemoryRequirements> requirements = GetBufferMemoryRequirements(desc);

		if (!requirements)
		{
			return Result<BufferHandle>::Failure(requirements.GetError());
		}

		if (requirements.Value().bDedicatedRequired)
		{
			return Result<BufferHandle>::Failure(EErrorCode::Unsupported, "Rhi.DedicatedAllocation", "The buffer requires a dedicated allocation and cannot be placed in a heap.");
		}

		const uint64 size = requirements.Value().ByteCount;
		const uint64 alignment = requirements.Value().AlignmentBytes;

		ASSERT(!(offset % alignment != 0 || offset > owner->Descriptor.CapacityBytes || size > owner->Descriptor.CapacityBytes - offset), "The placement is misaligned or outside heap capacity.");

		if (owner->LiveBuffers == NumericLimits<uint32>::max())
		{
			return Result<BufferHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Heap", "The heap buffer count is exhausted.");
		}

		Result<void> collected = collectRetiredRanges();

		if (!collected)
		{
			return Result<BufferHandle>::Failure(collected.GetError());
		}

		for (const BufferSlot& other : m_State->m_Buffers)
		{
			if (!other.bLive || other.Heap != heap)
			{
				continue;
			}

			if (offset < other.HeapOffset + other.AllocationSize && other.HeapOffset < offset + size)
			{
				return Result<BufferHandle>::Failure(EErrorCode::Conflict, "Rhi.Heap", "A live buffer already occupies this heap range.");
			}
		}

		for (const RetiredHeapRange& other : m_State->m_RetiredRanges)
		{
			if (other.Heap == heap && offset < other.Offset + other.Size && other.Offset < offset + size)
			{
				return Result<BufferHandle>::Failure(EErrorCode::Conflict, "Rhi.Heap", "GPU work still occupies this heap range.");
			}
		}

		return createBufferInternal(desc, heap, offset, size);
	}

	[[nodiscard]] Result<BufferHandle> Device::createBufferInternal(
		const BufferDesc& desc,
		HeapHandle heap,
		uint64 offset,
		uint64 allocationSize,
		void* nativeBuffer,
		void* adoptedBuffer)
	{
		Result<void> valid = validateBufferDesc(desc);

		if (!valid)
		{
			ASSERT(adoptedBuffer != nullptr || valid.GetError().Code != EErrorCode::InvalidArgument, "A valid buffer descriptor is required.");

			return Result<BufferHandle>::Failure(valid.GetError());
		}

		const bool bNewSlot = m_State->m_FreeBuffer == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Buffers.Size()) : m_State->m_FreeBuffer;

		if (bNewSlot && (m_State->m_Buffers.Size() >= NumericLimits<uint32>::max() || !m_State->m_Buffers.Add(BufferSlot(m_State->m_Resource))))
		{
			return Result<BufferHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "The device could not allocate a buffer handle slot.");
		}

		Result<void*> created = adoptedBuffer != nullptr ? Result<void*>::Success(adoptedBuffer)
			: nativeBuffer != nullptr ? backend::ImportBuffer(m_State->m_Backend.State, desc, nativeBuffer)
			: heap.IsValid()
			? backend::CreatePlacedBuffer(m_State->m_Backend.State, desc, m_State->m_Heaps[heap.Slot].Native, offset)
			: backend::CreateBuffer(m_State->m_Backend.State, desc);

		if (!created)
		{
			if (bNewSlot)
			{
				m_State->m_Buffers[index].NextFree = m_State->m_FreeBuffer;
				m_State->m_FreeBuffer = index;
			}

			return Result<BufferHandle>::Failure(created.GetError());
		}
		BufferSlot& slot = m_State->m_Buffers[index];

		if (!bNewSlot)
		{
			m_State->m_FreeBuffer = slot.NextFree;
		}

		slot.NextFree = NumericLimits<uint32>::max();
		slot.Native = created.Value();
		slot.ByteCount = desc.ByteCount;
		slot.Usage = desc.Usage;
		slot.Memory = desc.Memory;
		slot.Heap = heap;
		slot.HeapOffset = offset;
		slot.AllocationSize = allocationSize;
		slot.RecordedUses = 0;
		slot.ClusterMicromapReferences.Clear();
		slot.PermanentState = ETrackedResourceState::Unknown;
		slot.bMapped = false;
		slot.bExternalOwned = false;
		slot.bExternalShareable = false;
		slot.bLive = true;

		if (heap.IsValid())
		{
			++m_State->m_Heaps[heap.Slot].LiveBuffers;
		}

		return Result<BufferHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<void> Device::DestroyBuffer(BufferHandle handle)
	{
		CallScope call(*this, false);
		BufferSlot* const slot = findBuffer(handle, true);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		if (slot->RecordedUses != 0 || slot->BindingSetRefs != 0 || slot->bMapped)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "The buffer is mapped or referenced by a recording command list or binding set.");
		}
		bool bRetired = false;

		if (slot->Heap.IsValid())
		{
			const uint64 serial = backend::GetBufferLastUse(slot->Native);

			if (serial != 0)
			{
				bRetired = m_State->m_RetiredRanges.Add({ slot->Heap, slot->HeapOffset, slot->AllocationSize, serial });
				if (!bRetired)
				{
					if (hasAnyNativeQueueAccess())
					{
						return Result<void>::Failure(nativeQueueAccessConflict());
					}

					Result<void> idle = backend::WaitIdle(m_State->m_Backend.State);

					if (!idle)
					{
						return idle;
					}
				}
			}
		}

		if constexpr (backend::HAS_PROPAGATE_BUFFER_LAST_USE)
		{
			for (OpacityMicromapHandle micromap : slot->ClusterMicromapReferences)
			{
				const BufferHandle storage = m_State->m_OpacityMicromaps[micromap.Slot].Storage;
				backend::PropagateBufferLastUse(m_State->m_Backend.State, slot->Native, m_State->m_Buffers[storage.Slot].Native);
			}
		}

		Result<void> destroyed = backend::DestroyBuffer(m_State->m_Backend.State, slot->Native);

		if (!destroyed)
		{
			if (bRetired)
			{
				m_State->m_RetiredRanges.PopBack();
			}

			return destroyed;
		}

		if (slot->Heap.IsValid())
		{
			--m_State->m_Heaps[slot->Heap.Slot].LiveBuffers;
		}

		for (OpacityMicromapHandle micromapHandle : slot->ClusterMicromapReferences)
		{
			OpacityMicromapSlot& micromap = m_State->m_OpacityMicromaps[micromapHandle.Slot];
			--micromap.AccelStructRefs;
			--m_State->m_Buffers[micromap.Storage.Slot].BindingSetRefs;
		}
		slot->ClusterMicromapReferences.Clear();
		slot->Native = nullptr;
		slot->ByteCount = 0;
		slot->Usage = EBufferUsage::None;
		slot->Memory = EBufferMemory::Device;
		slot->Heap = {};
		slot->HeapOffset = 0;
		slot->AllocationSize = 0;
		slot->PermanentState = ETrackedResourceState::Unknown;
		slot->bLive = false;
		Advance(*slot);

		if (!slot->bExhausted)
		{
			slot->NextFree = m_State->m_FreeBuffer;
			m_State->m_FreeBuffer = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint64> Device::GetBufferDeviceAddress(BufferHandle handle)
	{
		CallScope call(*this, true);
		BufferSlot* const slot = findBuffer(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(!HasUsage(slot->Usage, EBufferUsage::DeviceAddress)), "The buffer was not created for device address access.");

		const uint64 address = backend::GetBufferDeviceAddress(m_State->m_Backend.State, slot->Native);

		if (address == 0)
		{
			return Result<uint64>::Failure(EErrorCode::ExternalFailure, "Rhi.BufferDeviceAddress", "The backend did not return a buffer device address.");
		}

		return Result<uint64>::Success(address);
	}

	[[nodiscard]] Result<void> Device::ReferenceBuffer(CommandListHandle list, BufferHandle buffer)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		BufferSlot* const slot = findBuffer(buffer);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		if (!command->AddBufferUse({ buffer, slot->Native }))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain a buffer referenced by GPU work.");
		}
		++slot->RecordedUses;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::ReferenceTexture(CommandListHandle list, TextureHandle texture)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		if (slot->bSwapChainImage || (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot))
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.NativeTextureReference", "The texture is a swapchain image or referenced by another command list.");
		}

		if (!command->TextureUses.Add({ texture, slot->Native }))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain a texture referenced by GPU work.");
		}
		slot->RecordingSlot = list.Slot;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::WriteBuffer(CommandListHandle list, BufferHandle destination, uint64 offset, Span<const Byte> data)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		BufferSlot* const slot = findBuffer(destination);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		if (m_State->m_bSubmissionFaulted)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot record after submission failure.");
		}

		ASSERT(!command->bRendering && !slot->bMapped, "Buffer writes require an unmapped buffer outside rendering.");
		ASSERT(HasUsage(slot->Usage, EBufferUsage::CopyDestination) && !data.IsEmpty() && data.Data() != nullptr && Fits(slot->ByteCount, offset, data.Size()), "Buffer write usage and range must be valid.");

		ASSERT(command->bAutomaticBarriers || slot->Memory != EBufferMemory::Device || command->HasBufferState(destination, ETrackedResourceState::CopyDestination),
			"Set the destination state before writing with automatic barriers disabled.");

		if (!command->AddBufferUse({ destination, slot->Native, ETrackedResourceState::CopyDestination }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track the buffer write.");
		}

		Result<void> recorded = backend::RecordBufferWrite(m_State->m_Backend.State, command->Native, slot->Native, offset, data);

		if (!recorded)
		{
			command->BufferUses.PopBack();
			poisonCommandList(list);

			return recorded;
		}
		++slot->RecordedUses;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::ClearBufferUInt(CommandListHandle list, BufferHandle buffer, uint32 value)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		BufferSlot* const slot = findBuffer(buffer);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		if (m_State->m_bSubmissionFaulted)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot record after submission failure.");
		}

		ASSERT(!command->bRendering && !slot->bMapped, "Buffer clears require an unmapped buffer outside rendering.");

		ASSERT((slot->ByteCount & 3u) == 0 && slot->Memory != EBufferMemory::CpuWrite && (HasUsage(slot->Usage, EBufferUsage::CopyDestination) || HasUsage(slot->Usage, EBufferUsage::Storage)),
			"Buffer clears require aligned copy destination or storage usage.");

		const ETrackedResourceState clearState =
			m_State->m_Kind == EBackend::D3D12 && command->Queue != EQueue::Copy && HasUsage(slot->Usage, EBufferUsage::Storage)
			? ETrackedResourceState::Storage
			: ETrackedResourceState::CopyDestination;

		ASSERT(command->bAutomaticBarriers || slot->Memory != EBufferMemory::Device || command->HasBufferState(buffer, clearState),
			"Set the buffer state before clearing with automatic barriers disabled.");

		if (!command->AddBufferUse({ buffer, slot->Native, clearState }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track the buffer clear.");
		}

		Result<void> recorded = backend::RecordBufferClear(m_State->m_Backend.State, command->Native, slot->Native, slot->ByteCount, value);

		if (!recorded)
		{
			command->BufferUses.PopBack();
			poisonCommandList(list);

			return recorded;
		}
		++slot->RecordedUses;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<Span<Byte>> Device::MapBuffer(BufferHandle handle, uint64 offset, uint64 byteCount, uint32 timeoutMilliseconds)
	{
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<Span<Byte>>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot map resources after submission failure.");
		}
		BufferSlot* const slot = findBuffer(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->Memory != EBufferMemory::Device && Fits(slot->ByteCount, offset, byteCount), "MapBuffer requires CPU-accessible memory and a contained range.");

		if (slot->bMapped || slot->RecordedUses != 0)
		{
			return Result<Span<Byte>>::Failure(EErrorCode::Conflict, "Rhi.BufferMap", "The buffer is mapped or referenced by a recording command list.");
		}

		if (slot->Heap.IsValid())
		{
			for (const BufferSlot& other : m_State->m_Buffers)
			{
				if (other.bLive && other.bMapped && other.Heap == slot->Heap)
				{
					return Result<Span<Byte>>::Failure(EErrorCode::Conflict, "Rhi.BufferMap", "Another buffer in the heap is mapped.");
				}
			}
		}

		Result<Byte*> mapped = backend::MapBuffer(m_State->m_Backend.State, slot->Native, offset, byteCount, timeoutMilliseconds);

		if (!mapped)
		{
			return Result<Span<Byte>>::Failure(mapped.GetError());
		}
		slot->bMapped = true;

		return Result<Span<Byte>>::Success({ mapped.Value(), static_cast<usize>(byteCount) });
	}

	[[nodiscard]] Result<void> Device::UnmapBuffer(BufferHandle handle)
	{
		CallScope call(*this, false);
		BufferSlot* const slot = findBuffer(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->bMapped, "UnmapBuffer requires a successful matching MapBuffer call.");

		Result<void> result = backend::UnmapBuffer(m_State->m_Backend.State, slot->Native);
		slot->bMapped = false;

		return result;
	}

	[[nodiscard]] Result<BufferViewHandle> Device::CreateBufferView(const BufferViewDesc& input)
	{
		CallScope call(*this, false);
		BufferSlot* const buffer = findBuffer(input.Buffer);

		ASSERT(buffer != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(input.Type > EBufferViewType::Storage || input.Format == EFormat::UNKNOWN || input.Format >= EFormat::COUNT), "A typed buffer view requires a valid type and explicit format.");

		const FormatInfo& format = GetFormatInfo(input.Format);
		BufferViewDesc desc = input;

		if (desc.Range.ByteCount == 0 && desc.Range.ByteOffset == 0)
		{
			desc.Range.ByteCount = buffer->ByteCount;
		}

		const EBufferUsage usage = desc.Type == EBufferViewType::Storage ? EBufferUsage::TypedStorage : EBufferUsage::TypedReadOnly;

		ASSERT(!(format.BlockSize != 1 ||
				format.BytesPerBlock == 0 ||
				format.bHasDepth ||
				format.bHasStencil ||
				!HasUsage(buffer->Usage, EBufferUsage::Storage | usage) ||
				desc.Range.ByteCount == 0 ||
				desc.Range.ByteOffset % format.BytesPerBlock != 0 ||
				desc.Range.ByteCount % format.BytesPerBlock != 0 ||
				!Fits(buffer->ByteCount, desc.Range.ByteOffset, desc.Range.ByteCount)),
			"The typed buffer range, format, or usage is invalid.");

		const EFormatSupport supported = queryNativeFormatSupport(desc.Format);
		EFormatSupport required = desc.Type == EBufferViewType::Storage ? EFormatSupport::BufferShaderStore : EFormatSupport::BufferShaderRead;

		if (m_State->m_Kind == EBackend::Vulkan)
		{
			if (HasUsage(buffer->Usage, EBufferUsage::TypedReadOnly))
			{
				required = required | EFormatSupport::BufferShaderRead;
			}

			if (HasUsage(buffer->Usage, EBufferUsage::TypedStorage))
			{
				required = required | EFormatSupport::BufferShaderStore;
			}
		}

		if (!HasFormatSupport(supported, required))
		{
			return Result<BufferViewHandle>::Failure(EErrorCode::Unsupported, "Rhi.BufferView", "The format does not support the requested typed buffer access.");
		}

		if (buffer->BindingSetRefs == NumericLimits<uint32>::max())
		{
			return Result<BufferViewHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.BufferView", "The buffer has too many view references.");
		}

		Result<void*> native = backend::CreateBufferView(m_State->m_Backend.State, buffer->Native, desc);

		if (!native)
		{
			return Result<BufferViewHandle>::Failure(native.GetError());
		}

		const bool bNewSlot = m_State->m_FreeBufferView == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_BufferViews.Size()) : m_State->m_FreeBufferView;

		if (bNewSlot && (m_State->m_BufferViews.Size() >= NumericLimits<uint32>::max() || !m_State->m_BufferViews.Add(BufferViewSlot{})))
		{
			backend::DestroyBufferView(m_State->m_Backend.State, native.Value());

			return Result<BufferViewHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate a buffer view slot.");
		}
		BufferViewSlot& view = m_State->m_BufferViews[index];

		if (!bNewSlot)
		{
			m_State->m_FreeBufferView = view.NextFree;
		}
		view.Native = native.Value();
		view.Descriptor = desc;
		view.BindingSetRefs = 0;
		view.bLive = true;
		view.NextFree = NumericLimits<uint32>::max();
		++buffer->BindingSetRefs;

		return Result<BufferViewHandle>::Success({ m_State->m_Owner, index, view.Generation });
	}

	[[nodiscard]] BufferViewSlot* Device::findBufferView(BufferViewHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_BufferViews.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		BufferViewSlot& view = m_State->m_BufferViews[handle.Slot];
		ASSERT(view.bLive && view.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &view;
	}

	[[nodiscard]] Result<void> Device::DestroyBufferView(BufferViewHandle handle)
	{
		CallScope call(*this, false);
		BufferViewSlot* const view = findBufferView(handle);

		ASSERT(view != nullptr, "RHI operations require valid handles for this device.");
		BufferSlot& buffer = m_State->m_Buffers[view->Descriptor.Buffer.Slot];

		if (view->BindingSetRefs != 0 || buffer.RecordedUses != 0)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.BufferView", "A binding or recorded buffer command still references the view.");
		}

		const uint64 serial = backend::GetBufferLastUse(buffer.Native);

		if (serial != 0)
		{
			Result<bool> complete = backend::IsComplete(m_State->m_Backend.State, serial);

			if (!complete)
			{
				return Result<void>::Failure(complete.GetError());
			}

			if (!complete.Value())
			{
				return Result<void>::Failure(EErrorCode::Conflict, "Rhi.BufferView", "The GPU is still using the view's buffer.");
			}
		}

		backend::DestroyBufferView(m_State->m_Backend.State, view->Native);
		--buffer.BindingSetRefs;
		view->Native = nullptr;
		view->bLive = false;
		Advance(*view);

		if (!view->bExhausted)
		{
			view->NextFree = m_State->m_FreeBufferView;
			m_State->m_FreeBufferView = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::validateBufferBindingFormat(EFormat format, bool bUniform, bool bReadOnly) const
	{
		if (format == EFormat::UNKNOWN)
		{
			return Result<void>::Success();
		}

		ASSERT(format < EFormat::COUNT && !bUniform, "Typed buffer formats require a valid format and a nonuniform binding.");

		const FormatInfo& info = GetFormatInfo(format);

		ASSERT(info.BlockSize == 1 && info.BytesPerBlock != 0 && !info.bHasDepth && !info.bHasStencil, "Typed buffer bindings require an uncompressed color format.");

		const EFormatSupport support = queryNativeFormatSupport(format);

		if (!HasFormatSupport(support, bReadOnly ? EFormatSupport::BufferShaderRead : EFormatSupport::BufferShaderStore))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BufferFormat", "The native format does not support the buffer binding access.");
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> Device::resolveBoundBufferView(
		BindingSetHandle handle,
		usize binding,
		BufferHandle buffer,
		EFormat format,
		bool bReadOnly,
		Span<const BufferViewHandle> inlineViews)
	{
		const BindingSetSlot* const set = findBindingSet(handle);
		const BufferViewHandle viewHandle =
			set != nullptr ? set->BufferViews[binding] : (!inlineViews.IsEmpty() ? inlineViews[binding] : BufferViewHandle{});
		if (format == EFormat::UNKNOWN && !viewHandle.IsValid())
		{
			return Result<void*>::Success(nullptr);
		}

		const BufferViewSlot* const view = findBufferView(viewHandle);

		ASSERT(view != nullptr && view->Descriptor.Buffer == buffer && view->Descriptor.Format == format && !((view->Descriptor.Type == EBufferViewType::ReadOnly) != bReadOnly),
			"A typed buffer binding requires a matching view in its binding set.");

		return Result<void*>::Success(view->Native);
	}

	[[nodiscard]] Result<void> Device::validateTypedBufferTables(uint32 storageCapacity, EFormat storageFormat, uint32 readOnlyCapacity, EFormat readOnlyFormat) const
	{
		ASSERT((storageCapacity != 0 || storageFormat == EFormat::UNKNOWN) && (readOnlyCapacity != 0 || readOnlyFormat == EFormat::UNKNOWN), "A typed table format requires a nonzero table capacity.");

		if (readOnlyCapacity > Min(m_State->m_Backend.MaximumBindlessStorageBuffers, m_State->m_Backend.MaximumBindlessSampledTextures) ||
			((storageFormat != EFormat::UNKNOWN || readOnlyFormat != EFormat::UNKNOWN) && !m_State->m_Backend.Capabilities.Features.bBindlessTypedBuffers))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.TypedDescriptorTable", "Typed indexing or the read-only buffer capacity is unavailable.");
		}

		Result<void> storage = validateBufferBindingFormat(storageFormat, false, false);

		if (!storage)
		{
			return storage;
		}

		return validateBufferBindingFormat(readOnlyFormat, false, true);
	}

	[[nodiscard]] Result<SubmissionToken> Device::UploadBuffer(BufferHandle destination, uint64 offset, Span<const Byte> data)
	{
		CallScope call(*this, false);
		if (hasNativeQueueAccess(EQueue::Copy))
		{
			return Result<SubmissionToken>::Failure(nativeQueueAccessConflict());
		}
		BufferSlot* const slot = findBuffer(destination);
		if (hasPreparedBuffer(destination))
		{
			Error error;
			error.Code = EErrorCode::Conflict;

			return Result<SubmissionToken>::Failure(Move(error));
		}

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(!HasUsage(slot->Usage, EBufferUsage::CopyDestination)), "The buffer does not permit copy destination use.");

		if (slot->bMapped)
		{
			return Result<SubmissionToken>::Failure(EErrorCode::Conflict, "Rhi.MappedBuffer", "The buffer is mapped.");
		}

		ASSERT(slot->PermanentState == ETrackedResourceState::Unknown || slot->PermanentState == ETrackedResourceState::CopyDestination, "The permanent buffer state does not allow upload.");

		ASSERT(!(data.IsEmpty() || data.Data() == nullptr || !Fits(slot->ByteCount, offset, data.Size())), "The upload byte range must fit in the buffer.");

		Result<CommandListHandle> list = CreateCommandList(EQueue::Copy);
		if (!list)
		{
			return Result<SubmissionToken>::Failure(list.GetError());
		}

		Result<void> recorded = WriteBuffer(list.Value(), destination, offset, data);

		if (!recorded)
		{
			DiscardCommandList(list.Value());

			return Result<SubmissionToken>::Failure(recorded.GetError());
		}
		const SubmitOutcome submitted = Submit(list.Value());
		if (submitted.Status == ESubmitStatus::Busy)
		{
			DiscardCommandList(list.Value());

			return Result<SubmissionToken>::Failure(nativeQueueAccessConflict());
		}
		if (submitted.Status == ESubmitStatus::Failed)
		{
			return Result<SubmissionToken>::Failure(EErrorCode::ExternalFailure, "Rhi.Submission", "The upload submission failed.");
		}

		return Result<SubmissionToken>::Success(submitted.Token);
	}

	[[nodiscard]] Result<void> Device::ReadBufferBlocking(BufferHandle source, uint64 offset, Span<Byte> output, SubmissionToken after, uint32 timeoutMilliseconds)
	{
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot admit readback after submission failure.");
		}
		if (hasNativeQueueAccess(EQueue::Copy))
		{
			return Result<void>::Failure(nativeQueueAccessConflict());
		}
		BufferSlot* const slot = findBuffer(source);
		if (hasPreparedBuffer(source))
		{
			Error error;
			error.Code = EErrorCode::Conflict;

			return Result<void>::Failure(Move(error));
		}

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(!HasUsage(slot->Usage, EBufferUsage::CopySource)), "The buffer does not permit copy source use.");

		if (slot->bMapped)
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.MappedBuffer", "The buffer is mapped.");
		}

		ASSERT(slot->PermanentState == ETrackedResourceState::Unknown || slot->PermanentState == ETrackedResourceState::CopySource, "The permanent buffer state does not allow readback.");

		ASSERT(!(!Fits(slot->ByteCount, offset, output.Size())), "The readback byte range must fit in the buffer.");

		const MonotonicClock::time_point started = MonotonicClock::now();

		if (after.Owner != 0 || after.Value != 0)
		{
			Result<void> waited = WaitForSubmission(after, timeoutMilliseconds);

			if (!waited)
			{
				return waited;
			}
		}

		const int64 elapsed = DurationCast<Milliseconds>(MonotonicClock::now() - started).count();
		const uint32 remaining = elapsed >= timeoutMilliseconds ? 0u : timeoutMilliseconds - static_cast<uint32>(elapsed);
		const uint64 before = backend::GetQueueLastSubmittedSerial(m_State->m_Backend.State, EQueue::Copy);
		Result<void> read = backend::ReadBuffer(m_State->m_Backend.State, slot->Native, offset, output, remaining);
		if (!read && backend::HAS_WAS_SUBMISSION_ACCEPTED && backend::WasSubmissionAccepted(m_State->m_Backend.State))
		{
			const uint64 afterSerial = backend::GetQueueLastSubmittedSerial(m_State->m_Backend.State, EQueue::Copy);
			if (afterSerial == before)
			{
				ASSERT(!m_State->m_RetainedReadBuffer.IsValid(), "A terminal device retains one uncertain readback buffer.");
				++slot->RecordedUses;
				m_State->m_RetainedReadBuffer = source;
				recordSubmissionFault(read.GetError());
			}
			else if (read.GetError().DiagnosticID == "Rhi.DeviceLost" || read.GetError().DiagnosticID == "Rhi.D3D12Wait" || read.GetError().DiagnosticID == "Rhi.VulkanWait")
			{
				recordSubmissionFault(read.GetError());
			}
		}

		return read;
	}

	[[nodiscard]] Result<void> Device::CopyBuffer(CommandListHandle list, const BufferCopy& copy)
	{
		CallScope call(*this, true, true);
		CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		if (m_State->m_bSubmissionFaulted)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot record after submission failure.");
		}
		if (command->PreparedGroup)
		{
			return copyPreparedBuffer(*command, copy);
		}

		ASSERT(!command->bRendering, "Buffer copies require no active rendering pass.");

		const BufferSlot* const source = findBuffer(copy.Source);
		const BufferSlot* const destination = findBuffer(copy.Destination);

		ASSERT(!(source == nullptr || destination == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!source->bMapped && !destination->bMapped, "Buffer copies require unmapped resources.");
		ASSERT(HasUsage(source->Usage, EBufferUsage::CopySource) && HasUsage(destination->Usage, EBufferUsage::CopyDestination), "Buffer copy usage must be valid.");
		ASSERT(Fits(source->ByteCount, copy.SourceOffsetBytes, copy.ByteCount) && Fits(destination->ByteCount, copy.DestinationOffsetBytes, copy.ByteCount), "Buffer copy ranges must be valid.");

		if (!command->bAutomaticBarriers && copy.Source == copy.Destination)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ManualBufferState", "A buffer self-copy requires internal staging transitions.");
		}

		ASSERT(command->bAutomaticBarriers ||
			((source->Memory != EBufferMemory::Device || command->HasBufferState(copy.Source, ETrackedResourceState::CopySource)) &&
			 (destination->Memory != EBufferMemory::Device || command->HasBufferState(copy.Destination, ETrackedResourceState::CopyDestination))),
			"Set both buffer states before copying with automatic barriers disabled.");
		RecordedCopy recorded{ copy.Source,
							  copy.Destination,
							  {source->Native, destination->Native, copy.SourceOffsetBytes, copy.DestinationOffsetBytes, copy.ByteCount} };
		if (!command->BufferUses.Reserve(command->BufferUses.Size() + 2) || !command->Copies.Add(recorded))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "The device could not record the buffer copy.");
		}

		Result<void> nativeResult = backend::RecordBufferCopy(m_State->m_Backend.State, command->Native, recorded.Native);

		if (!nativeResult)
		{
			command->Copies.PopBack();
			poisonCommandList(list);

			return nativeResult;
		}
		(void)command->AddBufferUse({ copy.Source, source->Native, ETrackedResourceState::CopySource });
		(void)command->AddBufferUse({ copy.Destination, destination->Native, ETrackedResourceState::CopyDestination });
		++m_State->m_Buffers[copy.Source.Slot].RecordedUses;
		++m_State->m_Buffers[copy.Destination.Slot].RecordedUses;

		return Result<void>::Success();
	}
}
