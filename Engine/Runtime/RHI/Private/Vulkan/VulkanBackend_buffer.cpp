#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<void*> CreateBufferView(State* opaque, void* native, const BufferViewDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		const uint64 alignment = state.DeviceProperties.limits.minTexelBufferOffsetAlignment;

		ASSERT(alignment != 0 && desc.Range.ByteOffset % alignment == 0, "The typed buffer offset is not aligned to the Vulkan device limit.");

		if (desc.Range.ByteCount / GetFormatInfo(desc.Format).BytesPerBlock > state.DeviceProperties.limits.maxTexelBufferElements)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanBufferView", "The typed buffer view exceeds the Vulkan element limit.");
		}
		BufferView* const view = CreateObject<BufferView>(state.Resource);

		if (view == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a typed buffer view."));
		}
		VkBufferViewCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
		info.buffer = static_cast<const Buffer*>(native)->Handle;
		info.format = ConvertVulkanFormat(desc.Format);
		info.offset = desc.Range.ByteOffset;
		info.range = desc.Range.ByteCount;

		if (state.F.CreateBufferView(state.Device, &info, nullptr, &view->Handle) != VK_SUCCESS)
		{
			DestroyObject(state.Resource, view);

			return Result<void*>::Failure(Failure("Rhi.VulkanBufferView", "Could not create a typed buffer view."));
		}

		return Result<void*>::Success(view);
	}

	void DestroyBufferView(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		BufferView* const view = static_cast<BufferView*>(native);
		state.F.DestroyBufferView(state.Device, view->Handle, nullptr);
		DestroyObject(state.Resource, view);
	}

	[[nodiscard]] Result<void> ValidateTypedTableLimits(
		const State& state,
		uint32 storageCapacity,
		EFormat storageFormat,
		uint32 readOnlyCapacity,
		EFormat readOnlyFormat,
		uint32 sampledCapacity,
		uint32 imageStorageCapacity)
	{
		const uint64 sampled = sampledCapacity + static_cast<uint64>(readOnlyFormat != EFormat::UNKNOWN ? readOnlyCapacity : 0);
		const uint64 imageStorage = imageStorageCapacity + static_cast<uint64>(storageFormat != EFormat::UNKNOWN ? storageCapacity : 0);
		const uint64 storage = (storageFormat == EFormat::UNKNOWN ? storageCapacity : 0) + static_cast<uint64>(readOnlyFormat == EFormat::UNKNOWN ? readOnlyCapacity : 0);

		if (sampled > state.MaximumBindlessSampledTextures ||
			imageStorage > state.MaximumBindlessStorageTextures ||
			storage > state.MaximumBindlessStorageBuffers ||
			((storageFormat != EFormat::UNKNOWN || readOnlyFormat != EFormat::UNKNOWN) && !state.bBindlessTypedBuffers))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTypedTable", "The combined typed and untyped tables exceed native descriptor limits.");
		}

		return Result<void>::Success();
	}

	void FreeBuffer(State& state, Buffer* buffer) noexcept
	{
		if (buffer == nullptr)
		{
			return;
		}

		if (buffer->Handle && buffer->bOwnsHandle)
		{
			state.F.DestroyBuffer(state.Device, buffer->Handle, nullptr);
		}

		if (buffer->Memory && buffer->bOwnsMemory)
		{
			state.F.FreeMemory(state.Device, buffer->Memory, nullptr);
		}

		DestroyObject(state.Resource, buffer);
	}

	[[nodiscard]] Result<Buffer*> NewBuffer(State& state, uint64 size, bool bHostVisible, VkBufferUsageFlags additionalUsage)
	{
		Buffer* buffer = CreateObject<Buffer>(state.Resource);

		if (!buffer)
		{
			return Result<Buffer*>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan buffer state."));
		}

		VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		info.size = size;
		info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | additionalUsage;
		SetQueueFamilySharing(state, info);

		if (state.F.CreateBuffer(state.Device, &info, nullptr, &buffer->Handle) != VK_SUCCESS)
		{
			FreeBuffer(state, buffer);

			return Result<Buffer*>::Failure(Failure("Rhi.VulkanBuffer", "Could not create a Vulkan buffer."));
		}

		VkMemoryDedicatedRequirements dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
		VkMemoryRequirements2 queried{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
		queried.pNext = &dedicated;
		VkBufferMemoryRequirementsInfo2 requirementsInfo{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
		requirementsInfo.buffer = buffer->Handle;
		state.F.GetBufferMemoryRequirements2(state.Device, &requirementsInfo, &queried);
		const VkMemoryRequirements& requirements = queried.memoryRequirements;
		const uint32 selected = SelectMemoryType(
			state, requirements.memoryTypeBits, bHostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			bHostVisible ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0, !bHostVisible);

		if (selected == UINT32_MAX)
		{
			FreeBuffer(state, buffer);

			return Result<Buffer*>::Failure(Failure("Rhi.VulkanMemory", "No compatible Vulkan memory type exists."));
		}

		buffer->Coherent = (state.MemoryProperties.memoryTypes[selected].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
		buffer->AllocationSize = requirements.size;
		buffer->ByteCount = size;
		VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		allocation.allocationSize = requirements.size;
		allocation.memoryTypeIndex = selected;
		VkMemoryDedicatedAllocateInfo dedicatedAllocation{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
		if (dedicated.requiresDedicatedAllocation || dedicated.prefersDedicatedAllocation)
		{
			dedicatedAllocation.buffer = buffer->Handle;
			allocation.pNext = &dedicatedAllocation;
		}

		VkMemoryAllocateFlagsInfo addressFlags{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
		if ((additionalUsage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0)
		{
			addressFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
			addressFlags.pNext = allocation.pNext;
			allocation.pNext = &addressFlags;
		}

		if (state.F.AllocateMemory(state.Device, &allocation, nullptr, &buffer->Memory) != VK_SUCCESS || state.F.BindBufferMemory(state.Device, buffer->Handle, buffer->Memory, 0) != VK_SUCCESS)
		{
			FreeBuffer(state, buffer);

			return Result<Buffer*>::Failure(Failure("Rhi.VulkanMemory", "Could not allocate or bind Vulkan buffer memory."));
		}

		return Result<Buffer*>::Success(buffer);
	}

	[[nodiscard]] uint64 GetBufferLastUse(const void* native) noexcept
	{
		return static_cast<const Buffer*>(native)->LastUse;
	}

	void PropagateBufferLastUse(const void*, const void* source, void* target) noexcept
	{
		const Buffer& input = *static_cast<const Buffer*>(source);
		Buffer& output = *static_cast<Buffer*>(target);

		if (input.LastUse > output.LastUse)
		{
			output.LastUse = input.LastUse;
			output.LastQueue = input.LastQueue;
		}
	}

	[[nodiscard]] uint64 GetBufferDeviceAddress(const State* opaque, const void* native) noexcept
	{
		const State& state = *opaque;
		VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		info.buffer = static_cast<const Buffer*>(native)->Handle;

		return state.F.GetBufferDeviceAddress(state.Device, &info);
	}

	[[nodiscard]] Result<void*> CreateBuffer(State* opaque, const BufferDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		const VkBufferUsageFlags usage = BufferUsageFlags(desc);
		Result<Buffer*> result = NewBuffer(state, desc.ByteCount, desc.Memory != EBufferMemory::Device, usage);

		if (!result)
		{
			return Result<void*>::Failure(result.GetError());
		}

		result.Value()->MemoryAccess = desc.Memory;

		return Result<void*>::Success(result.Value());
	}

	[[nodiscard]] Result<void*> ImportBuffer(State* opaque, const BufferDesc& desc, void* native)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Buffer* buffer = CreateObject<Buffer>(state.Resource);

		if (buffer == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a Vulkan imported buffer record."));
		}

		buffer->Handle = reinterpret_cast<VkBuffer>(native);
		buffer->ByteCount = desc.ByteCount;
		buffer->MemoryAccess = EBufferMemory::Device;
		buffer->bOwnsHandle = false;
		buffer->bOwnsMemory = false;

		return Result<void*>::Success(buffer);
	}

	[[nodiscard]] void* GetNativeBuffer(const void* native) noexcept
	{
		return reinterpret_cast<void*>(static_cast<const Buffer*>(native)->Handle);
	}

	void DestroyBufferNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		FreeBuffer(state, static_cast<Buffer*>(native));
	}

	[[nodiscard]] Result<void> DestroyBuffer(State* opaque, void* native)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Buffer& buffer = *static_cast<Buffer*>(native);
		uint64 complete = 0;

		if (!ReadCompletedSerial(state, complete))
		{
			return Result<void>::Failure(Failure("Rhi.VulkanTimeline", "Could not inspect Vulkan completion."));
		}

		if (buffer.LastUse > complete)
		{
			if (!state.RetiredBuffers.Add({ &buffer, buffer.LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track a retired Vulkan buffer."));
			}
		}
		else
		{
			FreeBuffer(state, &buffer);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint64> UploadBuffer(State* opaque, void* native, uint64 offset, Span<const Byte> data)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<uint64>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Result<Buffer*> staged = NewBuffer(state, data.Size(), true);

		if (!staged)
		{
			return Result<uint64>::Failure(staged.GetError());
		}

		Buffer* staging = staged.Value();
		void* mapped = nullptr;

		if (state.F.MapMemory(state.Device, staging->Memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
		{
			FreeBuffer(state, staging);

			return Result<uint64>::Failure(Failure("Rhi.VulkanMap", "Could not map Vulkan upload memory."));
		}

		CopyBytes(mapped, data.Data(), data.Size());
		VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.memory = staging->Memory;
		range.size = VK_WHOLE_SIZE;
		const bool bFlushed = staging->Coherent || state.F.FlushMappedMemoryRanges(state.Device, 1, &range) == VK_SUCCESS;
		state.F.UnmapMemory(state.Device, staging->Memory);

		if (!bFlushed)
		{
			FreeBuffer(state, staging);

			return Result<uint64>::Failure(Failure("Rhi.VulkanFlush", "Could not flush Vulkan upload memory."));
		}

		Result<Pending> started = Begin(state, EQueue::Copy);

		if (!started)
		{
			FreeBuffer(state, staging);

			return Result<uint64>::Failure(started.GetError());
		}

		Pending pending = Move(started).TakeValue();
		pending.QueueKind = EQueue::Copy;

		if (!pending.Temporary.Add(staging))
		{
			state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);
			FreeBuffer(state, staging);

			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not track Vulkan upload storage."));
		}

		Buffer& destination = *static_cast<Buffer*>(native);
		BufferTransferAccessBarrier(state, pending, destination.Handle, VK_ACCESS_TRANSFER_WRITE_BIT);
		VkBufferCopy copy{ 0, offset, data.Size() };
		state.F.CmdCopyBuffer(pending.Commands, staging->Handle, destination.Handle, 1, &copy);
		BufferTransferBarrier(state, pending, destination.Handle);
		Result<uint64> submitted = Execute(state, Move(pending));

		if (submitted)
		{
			destination.LastUse = submitted.Value();
			destination.LastQueue = EQueue::Copy;
		}

		return submitted;
	}

	[[nodiscard]] Result<Byte*> MapBuffer(State* opaque, void* native, uint64 offset, uint64, uint32 timeout)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<Byte*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Buffer& buffer = *static_cast<Buffer*>(native);

		if (buffer.LastUse != 0)
		{
			// Singular LastQueue metadata cannot describe independent prepared readers.
			// Match ordinary submissions' conservative frontier once explicit work exists.
			const MonotonicClock::time_point started = MonotonicClock::now();
			const EQueue queues[]{ EQueue::Graphics, EQueue::Compute, EQueue::Copy };
			for (const EQueue queue : queues)
			{
				if (!state.bHasExplicitPreparedWork && queue != buffer.LastQueue)
				{
					continue;
				}
				const uint64 serial = state.bHasExplicitPreparedWork ? QueueLastSerial(state, queue) : buffer.LastUse;
				if (serial == 0)
				{
					continue;
				}
				const uint64 elapsed = static_cast<uint64>( DurationCast<Milliseconds>(MonotonicClock::now() - started).count());
				const uint32 remaining = elapsed < timeout ? timeout - static_cast<uint32>(elapsed) : 0;
				Result<void> waited = Wait(&state, queue, serial, remaining);
				if (!waited)
				{
					return Result<Byte*>::Failure(waited.GetError());
				}
			}
		}
		void* base = nullptr;

		if (state.F.MapMemory(state.Device, buffer.Memory, 0, VK_WHOLE_SIZE, 0, &base) != VK_SUCCESS)
		{
			return Result<Byte*>::Failure(Failure("Rhi.VulkanMap", "Could not map the CPU buffer."));
		}

		if (buffer.MemoryAccess == EBufferMemory::CpuRead && !buffer.Coherent)
		{
			VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
			range.memory = buffer.Memory;
			const VkDeviceSize atom = state.DeviceProperties.limits.nonCoherentAtomSize;
			range.offset = buffer.MemoryOffset;
			const VkDeviceSize padded = ((buffer.ByteCount + atom - 1) / atom) * atom;
			range.size = Min(padded, buffer.AllocationSize - range.offset);

			if (state.F.InvalidateMappedMemoryRanges(state.Device, 1, &range) != VK_SUCCESS)
			{
				state.F.UnmapMemory(state.Device, buffer.Memory);

				return Result<Byte*>::Failure(Failure("Rhi.VulkanInvalidate", "Could not invalidate CPU readback memory."));
			}
		}

		return Result<Byte*>::Success(static_cast<Byte*>(base) + buffer.MemoryOffset + offset);
	}

	[[nodiscard]] Result<void> UnmapBuffer(State* opaque, void* native)
	{
		State& state = *opaque;
		Buffer& buffer = *static_cast<Buffer*>(native);
		bool bFlushed = true;

		if (buffer.MemoryAccess == EBufferMemory::CpuWrite && !buffer.Coherent)
		{
			VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
			range.memory = buffer.Memory;
			const VkDeviceSize atom = state.DeviceProperties.limits.nonCoherentAtomSize;
			range.offset = buffer.MemoryOffset;
			const VkDeviceSize padded = ((buffer.ByteCount + atom - 1) / atom) * atom;
			range.size = Min(padded, buffer.AllocationSize - range.offset);
			bFlushed = state.F.FlushMappedMemoryRanges(state.Device, 1, &range) == VK_SUCCESS;
		}
		state.F.UnmapMemory(state.Device, buffer.Memory);

		if (!bFlushed)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanFlush", "Could not flush CPU upload memory."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> ReadBuffer(State* opaque, void* native, uint64 offset, Span<Byte> output, uint32 timeout)
	{
		State& state = *opaque;
		state.bSubmissionMayBePending = false;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Result<Buffer*> staged = NewBuffer(state, output.Size(), true);

		if (!staged)
		{
			return Result<void>::Failure(staged.GetError());
		}

		Buffer* staging = staged.Value();
		Result<Pending> started = Begin(state, EQueue::Copy);

		if (!started)
		{
			FreeBuffer(state, staging);

			return Result<void>::Failure(started.GetError());
		}

		Pending pending = Move(started).TakeValue();
		pending.QueueKind = EQueue::Copy;

		if (!pending.Temporary.Add(staging))
		{
			state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);
			FreeBuffer(state, staging);

			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track Vulkan readback storage."));
		}

		Buffer& source = *static_cast<Buffer*>(native);
		BufferTransferAccessBarrier(state, pending, source.Handle, VK_ACCESS_TRANSFER_READ_BIT);
		VkBufferCopy copy{ offset, 0, output.Size() };
		state.F.CmdCopyBuffer(pending.Commands, source.Handle, staging->Handle, 1, &copy);
		BufferHostReadBarrier(state, pending, staging->Handle);
		Result<uint64> submitted = Execute(state, Move(pending));

		if (!submitted)
		{
			return Result<void>::Failure(submitted.GetError());
		}

		source.LastUse = submitted.Value();
		source.LastQueue = EQueue::Copy;
		Result<void> waited = Wait(&state, EQueue::Copy, submitted.Value(), timeout);

		if (!waited)
		{
			return waited;
		}

		void* mapped = nullptr;

		if (state.F.MapMemory(state.Device, staging->Memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanMap", "Could not map Vulkan readback memory."));
		}

		VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.memory = staging->Memory;
		range.size = VK_WHOLE_SIZE;
		const bool bInvalidated = staging->Coherent || state.F.InvalidateMappedMemoryRanges(state.Device, 1, &range) == VK_SUCCESS;

		if (bInvalidated)
		{
			CopyBytes(output.Data(), mapped, output.Size());
		}

		state.F.UnmapMemory(state.Device, staging->Memory);

		if (!bInvalidated)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanInvalidate", "Could not invalidate Vulkan readback memory."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBufferCopy(State* opaque, void* native, const NativeCopy& copy)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& source = *static_cast<Buffer*>(copy.Source);
		Buffer& destination = *static_cast<Buffer*>(copy.Destination);

		if (pending.bAutomaticBarriers)
		{
			BufferTransferAccessBarrier(state, pending, source.Handle, VK_ACCESS_TRANSFER_READ_BIT);
			BufferTransferAccessBarrier(state, pending, destination.Handle, VK_ACCESS_TRANSFER_WRITE_BIT);
		}

		if (&source == &destination)
		{
			Result<Buffer*> created = NewBuffer(state, copy.ByteCount, false);

			if (!created || !pending.Temporary.Add(created.Value()))
			{
				if (created)
				{
					FreeBuffer(state, created.Value());
				}

				return Result<void>::Failure(Failure("Rhi.VulkanStaging", "Could not allocate a self-copy buffer."));
			}
			VkBufferCopy first{ copy.SourceOffsetBytes, 0, copy.ByteCount };
			state.F.CmdCopyBuffer(pending.Commands, source.Handle, created.Value()->Handle, 1, &first);
			BufferTransferBarrier(state, pending, created.Value()->Handle);
			VkBufferCopy second{ 0, copy.DestinationOffsetBytes, copy.ByteCount };
			state.F.CmdCopyBuffer(pending.Commands, created.Value()->Handle, destination.Handle, 1, &second);
		}
		else
		{
			VkBufferCopy region{ copy.SourceOffsetBytes, copy.DestinationOffsetBytes, copy.ByteCount };
			state.F.CmdCopyBuffer(pending.Commands, source.Handle, destination.Handle, 1, &region);
		}

		if (pending.bAutomaticBarriers)
		{
			BufferTransferBarrier(state, pending, destination.Handle);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBufferWrite(State* opaque, void* native, void* destination, uint64 offset, Span<const Byte> data)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Result<Buffer*> staged = NewBuffer(state, data.Size(), true);

		if (!staged)
		{
			return Result<void>::Failure(staged.GetError());
		}
		Buffer* staging = staged.Value();
		void* mapped = nullptr;

		if (state.F.MapMemory(state.Device, staging->Memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
		{
			FreeBuffer(state, staging);

			return Result<void>::Failure(Failure("Rhi.VulkanMap", "Could not map a buffer write staging resource."));
		}

		CopyBytes(mapped, data.Data(), data.Size());
		VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.memory = staging->Memory;
		range.size = VK_WHOLE_SIZE;
		const bool bFlushed = staging->Coherent || state.F.FlushMappedMemoryRanges(state.Device, 1, &range) == VK_SUCCESS;
		state.F.UnmapMemory(state.Device, staging->Memory);

		if (!bFlushed)
		{
			FreeBuffer(state, staging);

			return Result<void>::Failure(Failure("Rhi.VulkanFlush", "Could not flush buffer write staging."));
		}

		if (!pending.Temporary.Add(staging))
		{
			FreeBuffer(state, staging);

			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain buffer write staging."));
		}

		Buffer& target = *static_cast<Buffer*>(destination);
		VkBufferMemoryBarrier before{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.buffer = target.Handle;
		before.offset = 0;
		before.size = VK_WHOLE_SIZE;

		if (pending.bAutomaticBarriers)
		{
			RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &before, 0,
				nullptr);
		}
		VkBufferCopy region{ 0, offset, data.Size() };
		state.F.CmdCopyBuffer(pending.Commands, staging->Handle, target.Handle, 1, &region);

		if (pending.bAutomaticBarriers)
		{
			BufferTransferBarrier(state, pending, target.Handle);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBufferClear(State* opaque, void* native, void* target, uint64 byteCount, uint32 value)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& buffer = *static_cast<Buffer*>(target);
		VkBufferMemoryBarrier before{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.buffer = buffer.Handle;
		before.offset = 0;
		before.size = VK_WHOLE_SIZE;

		if (pending.bAutomaticBarriers)
		{
			RecordBarriers(state, pending, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &before, 0,
				nullptr);
		}

		state.F.CmdFillBuffer(pending.Commands, buffer.Handle, 0, byteCount, value);
		VkBufferMemoryBarrier after{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		after.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		after.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		after.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		after.buffer = buffer.Handle;
		after.offset = 0;
		after.size = VK_WHOLE_SIZE;

		if (pending.bAutomaticBarriers)
		{
			RecordBarriers(state, pending, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &after, 0,
				nullptr);
		}

		return Result<void>::Success();
	}
}
