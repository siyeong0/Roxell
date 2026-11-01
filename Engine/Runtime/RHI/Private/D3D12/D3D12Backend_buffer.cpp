#include "D3D12Backend.h"
#include "D3D12Descriptors.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<MemoryRequirements> GetBufferMemoryRequirements(const State* opaque, const BufferDesc& desc)
	{
		const State& state = *opaque;
		D3D12_RESOURCE_DESC resource{};
		resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resource.Width = desc.ByteCount;
		resource.Height = 1;
		resource.DepthOrArraySize = 1;
		resource.MipLevels = 1;
		resource.SampleDesc.Count = 1;
		resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resource.Flags = HasUsage(desc.Usage, EBufferUsage::Storage) ||
			(HasUsage(desc.Usage, EBufferUsage::AccelStorage) || HasUsage(desc.Usage, EBufferUsage::MicromapStorage))
			? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			: D3D12_RESOURCE_FLAG_NONE;
		const D3D12_RESOURCE_ALLOCATION_INFO info = state.Device->GetResourceAllocationInfo(0, 1, &resource);

		if (info.SizeInBytes == NumericLimits<uint64>::max() || info.Alignment == 0)
		{
			return Result<MemoryRequirements>::Failure(Failure("Rhi.D3D12Memory", "Could not query buffer memory requirements."));
		}

		const uint32 budgetIndex = MemoryBudgetIndex(state, desc.Memory);

		return Result<MemoryRequirements>::Success({ info.SizeInBytes, info.Alignment, false, false, budgetIndex, budgetIndex });
	}

	[[nodiscard]] Result<void*> CreateBuffer(State* opaque, const BufferDesc& desc)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		Buffer* buffer = CreateObject<Buffer>(state.Resource);

		if (buffer == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a D3D12 buffer record."));
		}

		buffer->Memory = desc.Memory;
		buffer->bCommitted = true;
		buffer->bStorage = HasUsage(desc.Usage, EBufferUsage::Storage);
		const D3D12_HEAP_TYPE heap = desc.Memory == EBufferMemory::CpuWrite ? D3D12_HEAP_TYPE_UPLOAD : desc.Memory == EBufferMemory::CpuRead ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT;
		const D3D12_RESOURCE_STATES initial =
			(HasUsage(desc.Usage, EBufferUsage::AccelStorage) || HasUsage(desc.Usage, EBufferUsage::MicromapStorage))
			? D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
			: desc.Memory == EBufferMemory::CpuWrite ? D3D12_RESOURCE_STATE_GENERIC_READ
			: desc.Memory == EBufferMemory::CpuRead ? D3D12_RESOURCE_STATE_COPY_DEST
			: D3D12_RESOURCE_STATE_COMMON;

		if (!CreateResource(state, desc.ByteCount, heap, initial, buffer->Resource, HasUsage(desc.Usage, EBufferUsage::Storage) || (HasUsage(desc.Usage, EBufferUsage::AccelStorage) || HasUsage(desc.Usage, EBufferUsage::MicromapStorage)) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE))
		{
			DestroyObject(state.Resource, buffer);

			return Result<void*>::Failure(Failure("Rhi.D3D12Buffer", "Could not create a D3D12 buffer."));
		}

		return Result<void*>::Success(buffer);
	}

	void DestroyBufferNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<Buffer*>(native));
	}

	[[nodiscard]] Result<void> DestroyBuffer(State* opaque, void* native)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}

		Buffer& buffer = *static_cast<Buffer*>(native);

		if (buffer.LastUse > CompletedSerial(state))
		{
			if (!state.RetiredBuffers.Add({ &buffer, buffer.LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track a retired D3D12 buffer."));
			}
		}
		else
		{
			DestroyBufferNow(opaque, native);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> ImportBuffer(State* opaque, const BufferDesc& desc, void* native)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		ID3D12Resource* resource = static_cast<ID3D12Resource*>(native);
		const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();

		if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
			resourceDesc.Width < desc.ByteCount ||
			(HasUsage(desc.Usage, EBufferUsage::Storage) && (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0))
		{
			return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.NativeBuffer", "The D3D12 resource does not match the buffer descriptor.");
		}
		ComPtr<ID3D12Device> owner;

		if (FAILED(resource->GetDevice(IID_PPV_ARGS(owner.GetAddressOf()))) || owner.Get() != state.Device.Get())
		{
			return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.NativeBuffer", "The D3D12 resource belongs to another device.");
		}
		Buffer* buffer = CreateObject<Buffer>(state.Resource);

		if (buffer == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a D3D12 imported buffer record."));
		}
		buffer->Resource = resource;
		buffer->bUnknownAliases = true;
		buffer->Memory = EBufferMemory::Device;
		buffer->bStorage = HasUsage(desc.Usage, EBufferUsage::Storage);

		return Result<void*>::Success(buffer);
	}

	[[nodiscard]] void* GetNativeBuffer(const void* native) noexcept
	{
		return static_cast<const Buffer*>(native)->Resource.Get();
	}

	[[nodiscard]] uint64 GetBufferLastUse(const void* native) noexcept
	{
		return static_cast<const Buffer*>(native)->LastUse;
	}

	void PropagateBufferLastUse(const void*, const void* source, void* target) noexcept
	{
		const Buffer& input = *static_cast<const Buffer*>(source);
		Buffer& output = *static_cast<Buffer*>(target);

		MergeBufferUses(input, output);
	}

	[[nodiscard]] uint64 GetBufferDeviceAddress(const void*, const void* native) noexcept
	{
		return static_cast<const Buffer*>(native)->Resource->GetGPUVirtualAddress();
	}

	[[nodiscard]] Result<uint64> UploadBuffer(State* opaque, void* native, uint64 offset, Span<const Byte> data)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<uint64>::Failure(active.GetError());
		}

		ComPtr<ID3D12Resource> staging;

		if (!CreateResource(state, data.Size(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, staging))
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12Staging", "Could not create an upload buffer."));
		}

		void* mapped = nullptr;

		if (FAILED(staging->Map(0, nullptr, &mapped)))
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12Map", "Could not map the upload buffer."));
		}

		CopyBytes(mapped, data.Data(), data.Size());
		staging->Unmap(0, nullptr);
		Result<Pending> started = Begin(state, EQueue::Copy);

		if (!started)
		{
			return Result<uint64>::Failure(started.GetError());
		}

		Pending pending = Move(started).TakeValue();

		if (!pending.Temporary.Add(staging))
		{
			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not track upload storage."));
		}

		Buffer& buffer = *static_cast<Buffer*>(native);
		Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		pending.List->CopyBufferRegion(buffer.Resource.Get(), offset, staging.Get(), 0, data.Size());
		Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
		Result<uint64> submitted = Execute(state, Move(pending));

		if (submitted)
		{
			RecordResourceUse(buffer, EQueue::Copy, submitted.Value());
			if (buffer.bUnknownAliases)
			{
				state.UnknownResourceUses[static_cast<uint32>(EQueue::Copy)] = submitted.Value();
				state.UnknownBufferUses[static_cast<uint32>(EQueue::Copy)] = submitted.Value();
			}
		}

		return submitted;
	}

	[[nodiscard]] Result<Byte*> MapBuffer(State* opaque, void* native, uint64 offset, uint64 byteCount, uint32 timeout)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<Byte*>::Failure(active.GetError());
		}

		Buffer& buffer = *static_cast<Buffer*>(native);

		const ULONGLONG started = GetTickCount64();
		const EQueue queues[]{ EQueue::Graphics, EQueue::Compute, EQueue::Copy };
		for (const EQueue queue : queues)
		{
			const uint32 index = static_cast<uint32>(queue);
			// Committed UPLOAD/READBACK allocations cannot alias texture memory. Keep
			// unknown buffer/native-fence dependencies, but do not stall a fresh CPU
			// staging buffer behind unrelated imported texture work. Placed/imported
			// storage retains the conservative all-resource frontier.
			const uint64 unknown = buffer.bCommitted && buffer.Memory != EBufferMemory::Device ? state.UnknownBufferUses[index] : state.UnknownResourceUses[index];
			const uint64 serial = buffer.bUnknownAliases ? QueueLastSerial(state, queue) : Max(buffer.QueueUses[index], unknown);
			if (serial == 0)
			{
				continue;
			}
			const uint64 elapsed = GetTickCount64() - started;
			const uint32 remaining = elapsed < timeout ? timeout - static_cast<uint32>(elapsed) : 0;
			Result<void> waited = Wait(&state, queue, serial, remaining);
			if (!waited)
			{
				return Result<Byte*>::Failure(waited.GetError());
			}
		}
		D3D12_RANGE readRange{ 0, 0 };
		if (buffer.Memory == EBufferMemory::CpuRead)
		{
			readRange = { static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + byteCount) };
		}
		void* base = nullptr;

		if (FAILED(buffer.Resource->Map(0, &readRange, &base)))
		{
			return Result<Byte*>::Failure(Failure("Rhi.D3D12Map", "Could not map the CPU buffer."));
		}
		buffer.MappedOffset = offset;
		buffer.MappedBytes = byteCount;

		return Result<Byte*>::Success(static_cast<Byte*>(base) + offset);
	}

	[[nodiscard]] Result<void> UnmapBuffer(void*, void* native)
	{
		Buffer& buffer = *static_cast<Buffer*>(native);
		D3D12_RANGE written{ 0, 0 };
		if (buffer.Memory == EBufferMemory::CpuWrite)
		{
			written = { static_cast<SIZE_T>(buffer.MappedOffset), static_cast<SIZE_T>(buffer.MappedOffset + buffer.MappedBytes) };
		}
		buffer.Resource->Unmap(0, &written);
		buffer.MappedOffset = 0;
		buffer.MappedBytes = 0;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> ReadBuffer(State* opaque, void* native, uint64 offset, Span<Byte> output, uint32 timeout)
	{
		State& state = *opaque;
		state.bSubmissionMayBePending = false;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return active;
		}

		ComPtr<ID3D12Resource> staging;

		if (!CreateResource(state, output.Size(), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, staging))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Staging", "Could not create a readback buffer."));
		}

		Result<Pending> started = Begin(state, EQueue::Copy);

		if (!started)
		{
			return Result<void>::Failure(started.GetError());
		}

		Pending pending = Move(started).TakeValue();

		if (!pending.Temporary.Add(staging))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track readback storage."));
		}

		Buffer& buffer = *static_cast<Buffer*>(native);
		Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
		pending.List->CopyBufferRegion(staging.Get(), 0, buffer.Resource.Get(), offset, output.Size());
		Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
		Result<uint64> submitted = Execute(state, Move(pending));

		if (!submitted)
		{
			return Result<void>::Failure(submitted.GetError());
		}

		RecordResourceUse(buffer, EQueue::Copy, submitted.Value());
		if (buffer.bUnknownAliases)
		{
			state.UnknownResourceUses[static_cast<uint32>(EQueue::Copy)] = submitted.Value();
			state.UnknownBufferUses[static_cast<uint32>(EQueue::Copy)] = submitted.Value();
		}

		Result<void> waited = Wait(&state, EQueue::Copy, submitted.Value(), timeout);

		if (!waited)
		{
			return waited;
		}

		void* mapped = nullptr;
		D3D12_RANGE readRange{ 0, output.Size() };
		if (FAILED(staging->Map(0, &readRange, &mapped)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Map", "Could not map the readback buffer."));
		}

		CopyBytes(output.Data(), mapped, output.Size());
		D3D12_RANGE writtenRange{ 0, 0 };
		staging->Unmap(0, &writtenRange);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBufferCopy(State* opaque, void* native, const NativeCopy& copy)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& source = *static_cast<Buffer*>(copy.Source);
		Buffer& destination = *static_cast<Buffer*>(copy.Destination);

		if (&source == &destination)
		{
			ComPtr<ID3D12Resource> temporary;

			if (!CreateResource(state, copy.ByteCount, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, temporary) || !pending.Temporary.Add(temporary))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12Staging", "Could not track a self-copy buffer."));
			}

			Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
			Transition(pending, *temporary.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
			pending.List->CopyBufferRegion(temporary.Get(), 0, source.Resource.Get(), copy.SourceOffsetBytes, copy.ByteCount);
			Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
			Transition(pending, *temporary.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
			pending.List->CopyBufferRegion(destination.Resource.Get(), copy.DestinationOffsetBytes, temporary.Get(), 0, copy.ByteCount);
			Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
			Transition(pending, *temporary.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
		}
		else
		{
			if (source.Memory == EBufferMemory::Device)
			{
				Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
			}

			if (destination.Memory == EBufferMemory::Device)
			{
				Transition(pending, *destination.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
			}

			pending.List->CopyBufferRegion(destination.Resource.Get(), copy.DestinationOffsetBytes, source.Resource.Get(), copy.SourceOffsetBytes,
				copy.ByteCount);

			if (source.Memory == EBufferMemory::Device)
			{
				Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
			}

			if (destination.Memory == EBufferMemory::Device)
			{
				Transition(pending, *destination.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBufferWrite(State* opaque, void* native, void* destination, uint64 offset, Span<const Byte> data)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& buffer = *static_cast<Buffer*>(destination);
		ComPtr<ID3D12Resource> staging;

		if (!CreateResource(state, data.Size(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, staging))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Staging", "Could not create a buffer write staging resource."));
		}

		void* mapped = nullptr;

		if (FAILED(staging->Map(0, nullptr, &mapped)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Map", "Could not map a buffer write staging resource."));
		}

		CopyBytes(mapped, data.Data(), data.Size());
		staging->Unmap(0, nullptr);

		if (!pending.Temporary.Add(staging))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain buffer write staging."));
		}

		if (buffer.Memory == EBufferMemory::Device)
		{
			Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		}

		pending.List->CopyBufferRegion(buffer.Resource.Get(), offset, staging.Get(), 0, data.Size());

		if (buffer.Memory == EBufferMemory::Device)
		{
			Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordStorageBufferClear(State& state, Pending& pending, Buffer& buffer, uint64 byteCount, uint32 value)
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = 1;
		ComPtr<ID3D12DescriptorHeap> cpuHeap;

		if (FAILED(state.Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(cpuHeap.GetAddressOf()))))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12BufferClear", "Could not allocate a buffer clear view."));
		}
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ComPtr<ID3D12DescriptorHeap> gpuHeap;

		if (FAILED(state.Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(gpuHeap.GetAddressOf()))))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12BufferClear", "Could not allocate a visible buffer clear view."));
		}
		D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
		view.Format = DXGI_FORMAT_R32_UINT;
		view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		view.Buffer.NumElements = static_cast<UINT>(byteCount / sizeof(uint32));
		const D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpuHeap->GetCPUDescriptorHandleForHeapStart();
		state.Device->CreateUnorderedAccessView(buffer.Resource.Get(), nullptr, &view, cpu);
		CopyCommandDescriptors(state, pending, 1, gpuHeap->GetCPUDescriptorHandleForHeapStart(), cpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		if (!pending.AttachmentHeaps.Reserve(pending.AttachmentHeaps.Size() + 2))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain buffer clear views."));
		}
		(void)pending.AttachmentHeaps.Add(Move(cpuHeap));
		(void)pending.AttachmentHeaps.Add(Move(gpuHeap));
		ID3D12DescriptorHeap* const visible = pending.AttachmentHeaps.Back().Get();
		Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		pending.List->SetDescriptorHeaps(1, &visible);
		const UINT values[4]{ value, value, value, value };
		pending.List->ClearUnorderedAccessViewUint(visible->GetGPUDescriptorHandleForHeapStart(), cpu, buffer.Resource.Get(), values, 0,
			nullptr);
		InvalidateCommandState(&pending);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBufferClear(State* opaque, void* native, void* target, uint64 byteCount, uint32 value)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& buffer = *static_cast<Buffer*>(target);

		if (buffer.bStorage && pending.Queue != EQueue::Copy)
		{
			return RecordStorageBufferClear(state, pending, buffer, byteCount, value);
		}
		constexpr uint64 CHUNK_BYTES = 1024ull * 1024ull;
		const uint64 stagedBytes = byteCount < CHUNK_BYTES ? byteCount : CHUNK_BYTES;
		ComPtr<ID3D12Resource> staging;

		if (!CreateResource(state, stagedBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, staging))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Staging", "Could not create a clear pattern buffer."));
		}
		void* mapped = nullptr;

		if (FAILED(staging->Map(0, nullptr, &mapped)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Map", "Could not map the clear pattern buffer."));
		}

		for (uint64 offset = 0; offset < stagedBytes; offset += 4)
		{
			CopyBytes(static_cast<Byte*>(mapped) + offset, &value, 4);
		}
		staging->Unmap(0, nullptr);

		if (!pending.Temporary.Add(staging))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track the clear pattern buffer."));
		}

		if (buffer.Memory == EBufferMemory::Device)
		{
			Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		}

		for (uint64 offset = 0; offset < byteCount; offset += stagedBytes)
		{
			const uint64 remaining = byteCount - offset;
			pending.List->CopyBufferRegion(buffer.Resource.Get(), offset, staging.Get(), 0,
				remaining < stagedBytes ? remaining : stagedBytes);
		}

		if (buffer.Memory == EBufferMemory::Device)
		{
			Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> CreateBufferView(State* opaque, void* native, const BufferViewDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}

		const uint64 stride = GetFormatInfo(desc.Format).BytesPerBlock;
		const uint64 elements = desc.Range.ByteCount / stride;

		if (elements > (1ull << D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP))
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12BufferView", "The typed buffer view exceeds the native element limit.");
		}
		BufferView* const view = CreateObject<BufferView>(state.Resource);

		if (view == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a typed buffer view."));
		}
		D3D12_DESCRIPTOR_HEAP_DESC heap{};
		heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap.NumDescriptors = 1;

		if (FAILED(state.Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(view->Descriptor.GetAddressOf()))))
		{
			DestroyObject(state.Resource, view);

			return Result<void*>::Failure(Failure("Rhi.D3D12BufferView", "Could not allocate a typed descriptor."));
		}

		Buffer& buffer = *static_cast<Buffer*>(native);
		const DXGI_FORMAT format = GetDxgiFormatMapping(desc.Format).SrvFormat;
		const D3D12_CPU_DESCRIPTOR_HANDLE cpu = view->Descriptor->GetCPUDescriptorHandleForHeapStart();

		if (desc.Type == EBufferViewType::ReadOnly)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC info{};
			info.Format = format;
			info.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			info.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			info.Buffer.FirstElement = desc.Range.ByteOffset / stride;
			info.Buffer.NumElements = static_cast<UINT>(elements);
			state.Device->CreateShaderResourceView(buffer.Resource.Get(), &info, cpu);
		}
		else
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC info{};
			info.Format = format;
			info.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			info.Buffer.FirstElement = desc.Range.ByteOffset / stride;
			info.Buffer.NumElements = static_cast<UINT>(elements);
			state.Device->CreateUnorderedAccessView(buffer.Resource.Get(), nullptr, &info, cpu);
		}

		return Result<void*>::Success(view);
	}

	void DestroyBufferView(State* opaque, void* native) noexcept
	{
		DestroyObject(opaque->Resource, static_cast<BufferView*>(native));
	}
}
#endif
