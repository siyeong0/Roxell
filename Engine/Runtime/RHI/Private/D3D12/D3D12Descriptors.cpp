#include "D3D12Descriptors.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	Result<CpuDescriptorRange> AllocateCpuDescriptors(State& state, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32 count)
	{
		using R = Result<CpuDescriptorRange>;
		if (!count || count > CpuDescriptorPage::CAPACITY)
		{
			return R::Failure(Failure("Rhi.D3D12CpuDescriptors", "Invalid CPU descriptor range."));
		}
		LockGuard lock(state.CpuDescriptorMutex);
		for (usize index = 0; index <= state.CpuDescriptorPages.Size(); ++index)
		{
			if (index == state.CpuDescriptorPages.Size())
			{
				CpuDescriptorPage page;
				page.Type = type;
				D3D12_DESCRIPTOR_HEAP_DESC desc{};
				desc.Type = type;
				desc.NumDescriptors = CpuDescriptorPage::CAPACITY;
				const HRESULT result = state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(page.Heap.GetAddressOf()));
				if (FAILED(result))
				{
					char message[192];
					FormatText(message, sizeof(message), "CPU descriptor page creation failed: HRESULT=0x%08lx type=%u pages=%llu.",
						static_cast<unsigned long>(result), static_cast<unsigned>(type),
						static_cast<unsigned long long>(state.CpuDescriptorPages.Size()));

					return R::Failure(Failure("Rhi.D3D12CpuDescriptors", message));
				}
				if (!state.CpuDescriptorPages.Add(Move(page)))
				{
					return R::Failure(Failure("Rhi.Allocation", "Could not retain a CPU descriptor page."));
				}
			}
			d3d12::CpuDescriptorPage& page = state.CpuDescriptorPages[index];
			if (page.Type != type || page.Live + count > CpuDescriptorPage::CAPACITY)
			{
				continue;
			}
			for (uint32 pass = 0; pass < 2; ++pass)
			{
				const uint32 begin = pass == 0 ? page.Search : 0u;
				const uint32 end = pass == 0 ? CpuDescriptorPage::CAPACITY : page.Search;
				uint32 freeCount = 0;
				for (uint32 slot = begin; slot < end; ++slot)
				{
					freeCount = page.Used[slot] ? 0u : freeCount + 1;
					if (freeCount != count)
					{
						continue;
					}
					const uint32 first = slot + 1 - count;
					for (uint32 used = first; used <= slot; ++used)
					{
						page.Used[used] = 1;
					}
					page.Search = slot + 1;
					page.Live += count;
					D3D12_CPU_DESCRIPTOR_HANDLE cpu = page.Heap->GetCPUDescriptorHandleForHeapStart();
					cpu.ptr += static_cast<SIZE_T>(first) * state.Device->GetDescriptorHandleIncrementSize(type);

					return R::Success({ page.Heap, cpu, static_cast<uint32>(index), first, count });
				}
			}
		}

		return R::Failure(Failure("Rhi.D3D12CpuDescriptors", "Could not allocate a CPU descriptor range."));
	}

	void FreeCpuDescriptors(State& state, CpuDescriptorRange& range) noexcept
	{
		if (!range.Count)
		{
			return;
		}
		LockGuard lock(state.CpuDescriptorMutex);
		d3d12::CpuDescriptorPage& page = state.CpuDescriptorPages[range.Page];
		// Owners release ranges only after their existing GPU retirement boundary.
		for (uint32 slot = range.First; slot < range.First + range.Count; ++slot)
		{
			page.Used[slot] = 0;
		}
		page.Live -= range.Count;
		page.Search = Min(page.Search, range.First);
		range = {};
	}

	[[nodiscard]] bool InitializeGlobalDescriptorHeaps(State& state)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = GLOBAL_RESOURCE_DESCRIPTOR_COUNT;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(state.GlobalResourceHeap.GetAddressOf()))))
		{
			return false;
		}
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		desc.NumDescriptors = GLOBAL_SAMPLER_DESCRIPTOR_COUNT;

		if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(state.GlobalSamplerHeap.GetAddressOf()))) ||
			!state.ResourceDescriptorUsed.Resize(GLOBAL_RESOURCE_DESCRIPTOR_COUNT) ||
			!state.SamplerDescriptorUsed.Resize(GLOBAL_SAMPLER_DESCRIPTOR_COUNT))
		{
			state.GlobalResourceHeap.Reset();
			state.GlobalSamplerHeap.Reset();
			state.ResourceDescriptorUsed.Reset();
			state.SamplerDescriptorUsed.Reset();

			return false;
		}

		for (uint8& used : state.ResourceDescriptorUsed)
		{
			used = 0;
		}

		for (uint8& used : state.SamplerDescriptorUsed)
		{
			used = 0;
		}

		return true;
	}

	[[nodiscard]] bool EnsureGlobalDescriptorHeaps(State& state)
	{
		LockGuard lock(state.DescriptorMutex);
		if (state.GlobalResourceHeap && state.GlobalSamplerHeap)
		{
			return true;
		}

		return InitializeGlobalDescriptorHeaps(state);
	}

	[[nodiscard]] Result<uint32> AllocateGlobalDescriptors(State& state, bool bSampler, uint32 count)
	{
		LockGuard lock(state.DescriptorMutex);
		Array<uint8>& used = bSampler ? state.SamplerDescriptorUsed : state.ResourceDescriptorUsed;
		uint32& search = bSampler ? state.SamplerDescriptorSearch : state.ResourceDescriptorSearch;
		const uint32 capacity = static_cast<uint32>(used.Size());

		if (count == 0 || count > capacity)
		{
			return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.D3D12DescriptorHeap", "The global descriptor heap has no matching range.");
		}

		for (uint32 pass = 0; pass < 2; ++pass)
		{
			const uint32 begin = pass == 0 ? search : 0;
			const uint32 end = pass == 0 ? capacity : search;
			uint32 freeCount = 0;

			for (uint32 index = begin; index < end; ++index)
			{
				freeCount = used[index] == 0 ? freeCount + 1 : 0;

				if (freeCount != count)
				{
					continue;
				}

				const uint32 first = index + 1 - count;

				for (uint32 entry = first; entry <= index; ++entry)
				{
					used[entry] = 1;
				}
				search = index + 1;

				return Result<uint32>::Success(first);
			}
		}

		return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.D3D12DescriptorHeap", bSampler ? "The global sampler heap is full." : "The global resource heap is full.");
	}

	void ReleaseGlobalDescriptors(State& state, bool bSampler, uint32 first, uint32 count) noexcept
	{
		LockGuard lock(state.DescriptorMutex);
		Array<uint8>& used = bSampler ? state.SamplerDescriptorUsed : state.ResourceDescriptorUsed;
		uint32& search = bSampler ? state.SamplerDescriptorSearch : state.ResourceDescriptorSearch;
		ASSERT(first <= used.Size() && count <= used.Size() - first, "A descriptor range must be within the global heap.");

		for (uint32 index = first; index < first + count; ++index)
		{
			ASSERT(used[index] != 0, "A descriptor range must be allocated before release.");
			used[index] = 0;
		}

		if (first < search)
		{
			search = first;
		}
	}

	[[nodiscard]] Result<uint32> AllocatePendingGlobalDescriptors(State& state, Pending& pending, bool bSampler, uint32 count)
	{
		Result<uint32> allocated = AllocateGlobalDescriptors(state, bSampler, count);

		if (!allocated)
		{
			return allocated;
		}

		if (!pending.GlobalDescriptorRanges.Add({ allocated.Value(), count, bSampler }))
		{
			ReleaseGlobalDescriptors(state, bSampler, allocated.Value(), count);

			return Result<uint32>::Failure(Failure("Rhi.Allocation", "Could not retain the global descriptor range."));
		}

		return allocated;
	}

	void ReleasePendingGlobalDescriptors(State& state, Pending& pending) noexcept
	{
		for (const Pending::GlobalDescriptorRange& range : pending.GlobalDescriptorRanges)
		{
			ReleaseGlobalDescriptors(state, range.bSampler, range.First, range.Count);
		}
		pending.GlobalDescriptorRanges.Clear();
	}

	[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GlobalDescriptorCpu(const State& state, bool bSampler, uint32 index) noexcept
	{
		const D3D12_DESCRIPTOR_HEAP_TYPE type = bSampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		D3D12_CPU_DESCRIPTOR_HANDLE handle = bSampler ? state.GlobalSamplerHeap->GetCPUDescriptorHandleForHeapStart() : state.GlobalResourceHeap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += static_cast<SIZE_T>(index) * state.Device->GetDescriptorHandleIncrementSize(type);

		return handle;
	}

	[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GlobalDescriptorGpu(const State& state, bool bSampler, uint32 index) noexcept
	{
		const D3D12_DESCRIPTOR_HEAP_TYPE type = bSampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		D3D12_GPU_DESCRIPTOR_HANDLE handle = bSampler ? state.GlobalSamplerHeap->GetGPUDescriptorHandleForHeapStart() : state.GlobalResourceHeap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += static_cast<UINT64>(index) * state.Device->GetDescriptorHandleIncrementSize(type);

		return handle;
	}

	[[nodiscard]] Result<void*> CreateDescriptorTable(State* opaque, const DescriptorTableDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}

		const uint32 limit = desc.Type == EDescriptorType::SampledTexture ? state.MaximumBindlessSampledTextures
			: desc.Type == EDescriptorType::Sampler ? state.MaximumBindlessSamplers
			: (desc.Type == EDescriptorType::StorageBuffer || desc.Type == EDescriptorType::ReadOnlyBuffer)
			? state.MaximumBindlessStorageBuffers
			: desc.Type == EDescriptorType::StorageTexture ? state.MaximumBindlessStorageTextures
			: desc.Type == EDescriptorType::UniformBuffer ? state.MaximumBindlessUniformBuffers
			: desc.Type == EDescriptorType::AccelerationStructure && state.RayDevice
			? state.MaximumBindlessSampledTextures
			: 0;

		if (desc.Capacity == 0 || desc.Capacity > limit)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12DescriptorTable", "The descriptor table exceeds the device capability.");
		}
		DescriptorTable* const table = CreateObject<DescriptorTable>(state.Resource, state.Resource);

		if (table == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a D3D12 descriptor table."));
		}

		if ((desc.Type == EDescriptorType::SampledTexture || desc.Type == EDescriptorType::StorageTexture) && !table->Textures.Resize(desc.Capacity))
		{
			DestroyObject(state.Resource, table);

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not track descriptor textures."));
		}

		if ((desc.Type == EDescriptorType::SampledTexture || desc.Type == EDescriptorType::StorageTexture) && !table->TextureViews.Resize(desc.Capacity))
		{
			DestroyObject(state.Resource, table);

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not track descriptor texture views."));
		}

		if ((desc.Type == EDescriptorType::StorageBuffer || desc.Type == EDescriptorType::ReadOnlyBuffer || desc.Type == EDescriptorType::UniformBuffer) && !table->Buffers.Resize(desc.Capacity))
		{
			DestroyObject(state.Resource, table);

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not track descriptor buffers."));
		}

		if (desc.Type == EDescriptorType::Sampler && !table->SamplerValid.Resize(desc.Capacity))
		{
			DestroyObject(state.Resource, table);

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not track descriptor samplers."));
		}

		for (uint32 index = 0; index < table->SamplerValid.Size(); ++index)
		{
			table->SamplerValid[index] = 0;
		}
		D3D12_DESCRIPTOR_HEAP_DESC heap{};
		heap.Type = desc.Type == EDescriptorType::Sampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap.NumDescriptors = desc.Capacity;
		heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		if (FAILED(state.Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(table->Heap.GetAddressOf()))))
		{
			DestroyObject(state.Resource, table);

			return Result<void*>::Failure(Failure("Rhi.D3D12DescriptorTable", "Could not allocate a shader visible descriptor heap."));
		}
		heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		if (FAILED(state.Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(table->CpuHeap.GetAddressOf()))))
		{
			DestroyObject(state.Resource, table);

			return Result<void*>::Failure(Failure("Rhi.D3D12DescriptorTable", "Could not allocate the descriptor source heap."));
		}

		if (state.bDeviceHeapIndexingEnabled && state.GlobalResourceHeap && state.GlobalSamplerHeap)
		{
			Result<uint32> allocated = AllocateGlobalDescriptors(state, desc.Type == EDescriptorType::Sampler, desc.Capacity);

			if (!allocated)
			{
				DestroyObject(state.Resource, table);

				return Result<void*>::Failure(allocated.GetError());
			}
			table->FirstGlobalDescriptor = allocated.Value();
		}
		table->Capacity = desc.Capacity;
		table->Type = desc.Type;
		table->BufferFormat = desc.BufferFormat;
		table->StructureStrideBytes = desc.StructureStrideBytes;

		return Result<void*>::Success(table);
	}

	void PublishTableDescriptor(State& state, const DescriptorTable& table, uint32 index) noexcept
	{
		const bool bSampler = table.Type == EDescriptorType::Sampler;
		const D3D12_DESCRIPTOR_HEAP_TYPE type = bSampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		D3D12_CPU_DESCRIPTOR_HANDLE source = table.CpuHeap->GetCPUDescriptorHandleForHeapStart();
		source.ptr += static_cast<SIZE_T>(index) * state.Device->GetDescriptorHandleIncrementSize(type);
		D3D12_CPU_DESCRIPTOR_HANDLE visible = table.Heap->GetCPUDescriptorHandleForHeapStart();
		visible.ptr += static_cast<SIZE_T>(index) * state.Device->GetDescriptorHandleIncrementSize(type);
		state.Device->CopyDescriptorsSimple(1, visible, source, type);

		if (table.FirstGlobalDescriptor == UINT32_MAX)
		{
			return;
		}
		state.Device->CopyDescriptorsSimple(1, GlobalDescriptorCpu(state, bSampler, table.FirstGlobalDescriptor + index), source, type);
	}

	[[nodiscard]] Result<void> WriteDescriptorTable(
		State* opaque,
		void* native,
		uint32 index,
		void* textureNative,
		void* viewNative,
		void* samplerNative,
		void* bufferNative,
		BufferRange range)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}

		DescriptorTable& table = *static_cast<DescriptorTable*>(native);

		ASSERT(index < table.Capacity, "The descriptor index exceeds the table capacity.");

		if (textureNative == nullptr && viewNative == nullptr && samplerNative == nullptr && bufferNative == nullptr)
		{
			// Empty entries are not shader-readable; the next write replaces the descriptor bytes.
			if (table.Type == EDescriptorType::SampledTexture || table.Type == EDescriptorType::StorageTexture)
			{
				table.Textures[index] = nullptr;
				table.TextureViews[index] = nullptr;
			}

			if (table.Type == EDescriptorType::StorageBuffer || table.Type == EDescriptorType::ReadOnlyBuffer || table.Type == EDescriptorType::UniformBuffer)
			{
				table.Buffers[index] = nullptr;
			}

			if (table.Type == EDescriptorType::Sampler)
			{
				table.SamplerValid[index] = 0;
			}

			return Result<void>::Success();
		}

		const D3D12_DESCRIPTOR_HEAP_TYPE heapType = table.Type == EDescriptorType::Sampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		ID3D12DescriptorHeap* const source =
			table.Type == EDescriptorType::Sampler
			? (samplerNative != nullptr ? static_cast<Sampler*>(samplerNative)->Descriptor.Get() : nullptr)
			: (viewNative != nullptr ? (bufferNative != nullptr ? static_cast<BufferView*>(viewNative)->Descriptor.Get()
				: static_cast<TextureView*>(viewNative)->Descriptor.Heap.Get())
				: textureNative != nullptr
				? (table.Type == EDescriptorType::StorageTexture ? static_cast<Texture*>(textureNative)->StorageView.Get()
					: static_cast<Texture*>(textureNative)->ShaderView.Get())
				: nullptr);
		D3D12_CPU_DESCRIPTOR_HANDLE destination = table.CpuHeap->GetCPUDescriptorHandleForHeapStart();
		destination.ptr += static_cast<SIZE_T>(index) * state.Device->GetDescriptorHandleIncrementSize(heapType);

		if ((table.Type == EDescriptorType::StorageBuffer || table.Type == EDescriptorType::ReadOnlyBuffer) && viewNative != nullptr)
		{
			state.Device->CopyDescriptorsSimple(
				1, destination, static_cast<BufferView*>(viewNative)->Descriptor->GetCPUDescriptorHandleForHeapStart(), heapType);
			PublishTableDescriptor(state, table, index);
			table.Buffers[index] = static_cast<Buffer*>(bufferNative);

			return Result<void>::Success();
		}

		if (table.Type == EDescriptorType::AccelerationStructure)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
			desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			desc.RaytracingAccelerationStructure.Location = GetDescriptorAccelerationStructure(bufferNative);
			state.Device->CreateShaderResourceView(nullptr, &desc, destination);
			PublishTableDescriptor(state, table, index);

			return Result<void>::Success();
		}

		if (table.Type == EDescriptorType::ReadOnlyBuffer)
		{
			Buffer& buffer = *static_cast<Buffer*>(bufferNative);
			D3D12_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format = table.StructureStrideBytes ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
			view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			const uint32 elementBytes = table.StructureStrideBytes ? table.StructureStrideBytes : 4u;
			view.Buffer.FirstElement = range.ByteOffset / elementBytes;
			view.Buffer.NumElements = static_cast<UINT>(range.ByteCount / elementBytes);
			view.Buffer.StructureByteStride = table.StructureStrideBytes;
			view.Buffer.Flags = table.StructureStrideBytes ? D3D12_BUFFER_SRV_FLAG_NONE : D3D12_BUFFER_SRV_FLAG_RAW;
			state.Device->CreateShaderResourceView(buffer.Resource.Get(), &view, destination);
			PublishTableDescriptor(state, table, index);
			table.Buffers[index] = &buffer;

			return Result<void>::Success();
		}

		if (table.Type == EDescriptorType::StorageBuffer)
		{
			Buffer& buffer = *static_cast<Buffer*>(bufferNative);
			D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
			view.Format = table.StructureStrideBytes ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
			view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			const uint32 elementBytes = table.StructureStrideBytes ? table.StructureStrideBytes : 4u;
			view.Buffer.FirstElement = range.ByteOffset / elementBytes;
			view.Buffer.NumElements = static_cast<UINT>(range.ByteCount / elementBytes);
			view.Buffer.StructureByteStride = table.StructureStrideBytes;
			view.Buffer.Flags = table.StructureStrideBytes ? D3D12_BUFFER_UAV_FLAG_NONE : D3D12_BUFFER_UAV_FLAG_RAW;
			state.Device->CreateUnorderedAccessView(buffer.Resource.Get(), nullptr, &view, destination);
			PublishTableDescriptor(state, table, index);
			table.Buffers[index] = &buffer;

			return Result<void>::Success();
		}

		if (table.Type == EDescriptorType::UniformBuffer)
		{
			Buffer& buffer = *static_cast<Buffer*>(bufferNative);
			D3D12_CONSTANT_BUFFER_VIEW_DESC view{};
			view.BufferLocation = buffer.Resource->GetGPUVirtualAddress() + range.ByteOffset;
			view.SizeInBytes = static_cast<UINT>(range.ByteCount);
			state.Device->CreateConstantBufferView(&view, destination);
			PublishTableDescriptor(state, table, index);
			table.Buffers[index] = &buffer;

			return Result<void>::Success();
		}

		ASSERT(source != nullptr, "The texture or sampler has no descriptor.");
		const D3D12_CPU_DESCRIPTOR_HANDLE sourceCpu = viewNative && textureNative ? static_cast<TextureView*>(viewNative)->Descriptor.Cpu : source->GetCPUDescriptorHandleForHeapStart();
		state.Device->CopyDescriptorsSimple(1, destination, sourceCpu, heapType);
		PublishTableDescriptor(state, table, index);

		if (table.Type == EDescriptorType::SampledTexture || table.Type == EDescriptorType::StorageTexture)
		{
			table.Textures[index] = static_cast<Texture*>(textureNative);
			table.TextureViews[index] = static_cast<TextureView*>(viewNative);
		}

		if (table.Type == EDescriptorType::Sampler)
		{
			table.SamplerValid[index] = 1;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> CopyDescriptorTableEntries(
		State* opaque,
		const void* sourceNative,
		uint32 sourceIndex,
		void* destinationNative,
		uint32 destinationIndex,
		uint32 count)
	{
		State& state = *opaque;
		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}
		const DescriptorTable& source = *static_cast<const DescriptorTable*>(sourceNative);
		DescriptorTable& destination = *static_cast<DescriptorTable*>(destinationNative);
		ASSERT(&source != &destination &&
			count &&
			sourceIndex < source.Capacity &&
			count <= source.Capacity - sourceIndex &&
			destinationIndex < destination.Capacity &&
			count <= destination.Capacity - destinationIndex &&
			source.Type == destination.Type &&
			source.BufferFormat == destination.BufferFormat &&
			source.StructureStrideBytes == destination.StructureStrideBytes,
			"Descriptor copy requires compatible nonempty table ranges.");
		const bool bSampler = source.Type == EDescriptorType::Sampler;
		const D3D12_DESCRIPTOR_HEAP_TYPE type = bSampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		const UINT increment = state.Device->GetDescriptorHandleIncrementSize(type);
		D3D12_CPU_DESCRIPTOR_HANDLE sourceCpu = source.CpuHeap->GetCPUDescriptorHandleForHeapStart();
		sourceCpu.ptr += static_cast<SIZE_T>(sourceIndex) * increment;
		D3D12_CPU_DESCRIPTOR_HANDLE destinationCpu = destination.CpuHeap->GetCPUDescriptorHandleForHeapStart();
		destinationCpu.ptr += static_cast<SIZE_T>(destinationIndex) * increment;
		state.Device->CopyDescriptorsSimple(count, destinationCpu, sourceCpu, type);
		D3D12_CPU_DESCRIPTOR_HANDLE destinationVisible = destination.Heap->GetCPUDescriptorHandleForHeapStart();
		destinationVisible.ptr += static_cast<SIZE_T>(destinationIndex) * increment;
		state.Device->CopyDescriptorsSimple(count, destinationVisible, sourceCpu, type);
		if (destination.FirstGlobalDescriptor != UINT32_MAX)
		{
			state.Device->CopyDescriptorsSimple(
				count, GlobalDescriptorCpu(state, bSampler, destination.FirstGlobalDescriptor + destinationIndex), sourceCpu, type);
		}
		for (uint32 index = 0; index < count; ++index)
		{
			if (!source.Textures.IsEmpty())
			{
				destination.Textures[destinationIndex + index] = source.Textures[sourceIndex + index];
				destination.TextureViews[destinationIndex + index] = source.TextureViews[sourceIndex + index];
			}
			if (!source.Buffers.IsEmpty())
			{
				destination.Buffers[destinationIndex + index] = source.Buffers[sourceIndex + index];
			}
			if (!source.SamplerValid.IsEmpty())
			{
				destination.SamplerValid[destinationIndex + index] = source.SamplerValid[sourceIndex + index];
			}
		}

		return Result<void>::Success();
	}

	void DestroyDescriptorTable(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DescriptorTable* const table = static_cast<DescriptorTable*>(native);

		if (table->FirstGlobalDescriptor != UINT32_MAX)
		{
			ReleaseGlobalDescriptors(state, table->Type == EDescriptorType::Sampler, table->FirstGlobalDescriptor, table->Capacity);
		}

		DestroyObject(state.Resource, table);
	}

	[[nodiscard]] uint32 GetDescriptorTableDeviceHeapIndex(const State* opaque, const void* native) noexcept
	{
		if (!opaque->bDeviceHeapIndexingEnabled)
		{
			return UINT32_MAX;
		}

		return static_cast<const DescriptorTable*>(native)->FirstGlobalDescriptor;
	}

	[[nodiscard]] D3D12_TEXTURE_ADDRESS_MODE AddressMode(ESamplerAddressMode mode) noexcept
	{
		switch (mode)
		{
		case ESamplerAddressMode::Repeat:
			return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		case ESamplerAddressMode::Mirror:
			return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		case ESamplerAddressMode::Border:
			return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		default:
			return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		}
	}

	[[nodiscard]] D3D12_STATIC_SAMPLER_DESC MakeStaticSampler(const SamplerDesc& sampler, uint32 slot) noexcept
	{
		constexpr D3D12_COMPARISON_FUNC COMPARE[]{ D3D12_COMPARISON_FUNC_NEVER,		   D3D12_COMPARISON_FUNC_LESS,
												  D3D12_COMPARISON_FUNC_EQUAL,		   D3D12_COMPARISON_FUNC_LESS_EQUAL,
												  D3D12_COMPARISON_FUNC_GREATER,	   D3D12_COMPARISON_FUNC_NOT_EQUAL,
												  D3D12_COMPARISON_FUNC_GREATER_EQUAL, D3D12_COMPARISON_FUNC_ALWAYS };
		const D3D12_FILTER_REDUCTION_TYPE reduction =
			sampler.bCompareEnabled ? D3D12_FILTER_REDUCTION_TYPE_COMPARISON
			: sampler.Reduction == ESamplerReduction::Minimum ? D3D12_FILTER_REDUCTION_TYPE_MINIMUM
			: sampler.Reduction == ESamplerReduction::Maximum ? D3D12_FILTER_REDUCTION_TYPE_MAXIMUM
			: D3D12_FILTER_REDUCTION_TYPE_STANDARD;
		D3D12_STATIC_SAMPLER_DESC native{};
		native.Filter =
			sampler.MaxAnisotropy > 1.0f
			? D3D12_ENCODE_ANISOTROPIC_FILTER(reduction)
			: D3D12_ENCODE_BASIC_FILTER(
				sampler.MinFilter == ESamplerFilter::Linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
				sampler.MagFilter == ESamplerFilter::Linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
				sampler.MipFilter == ESamplerFilter::Linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT, reduction);
		native.AddressU = AddressMode(sampler.AddressU);
		native.AddressV = AddressMode(sampler.AddressV);
		native.AddressW = AddressMode(sampler.AddressW);
		native.MipLODBias = sampler.MipBias;
		native.MaxAnisotropy = static_cast<UINT>(Ceil(sampler.MaxAnisotropy));
		native.ComparisonFunc = sampler.bCompareEnabled ? COMPARE[static_cast<uint32>(sampler.Compare)] : D3D12_COMPARISON_FUNC_ALWAYS;
		native.BorderColor = sampler.BorderColor == ESamplerBorderColor::OpaqueWhite ? D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE
			: sampler.BorderColor == ESamplerBorderColor::OpaqueBlack ? D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK
			: D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		native.MinLOD = sampler.MinLod;
		native.MaxLOD = sampler.MaxLod;
		native.ShaderRegister = slot;
		native.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		return native;
	}

	[[nodiscard]] D3D12_SAMPLER_DESC MakeSamplerDesc(const SamplerDesc& sampler) noexcept
	{
		const D3D12_STATIC_SAMPLER_DESC basic = MakeStaticSampler(sampler, 0);
		D3D12_SAMPLER_DESC native{};
		native.Filter = basic.Filter;
		native.AddressU = basic.AddressU;
		native.AddressV = basic.AddressV;
		native.AddressW = basic.AddressW;
		native.MipLODBias = basic.MipLODBias;
		native.MaxAnisotropy = basic.MaxAnisotropy;
		native.ComparisonFunc = basic.ComparisonFunc;
		const float borderRgb = sampler.BorderColor == ESamplerBorderColor::OpaqueWhite ? 1.0f : 0.0f;
		native.BorderColor[0] = native.BorderColor[1] = native.BorderColor[2] = borderRgb;
		native.BorderColor[3] = sampler.BorderColor == ESamplerBorderColor::TransparentBlack ? 0.0f : 1.0f;
		native.MinLOD = basic.MinLOD;
		native.MaxLOD = basic.MaxLOD;

		return native;
	}

	[[nodiscard]] Result<void*> CreateSampler(State* opaque, const SamplerDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}
		Sampler* const sampler = CreateObject<Sampler>(state.Resource);

		if (sampler == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a sampler."));
		}
		D3D12_DESCRIPTOR_HEAP_DESC heap{};
		heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		heap.NumDescriptors = 1;

		if (FAILED(state.Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(sampler->Descriptor.GetAddressOf()))))
		{
			DestroyObject(state.Resource, sampler);

			return Result<void*>::Failure(Failure("Rhi.D3D12Sampler", "Could not create a sampler descriptor."));
		}

		const D3D12_SAMPLER_DESC native = MakeSamplerDesc(desc);
		state.Device->CreateSampler(&native, sampler->Descriptor->GetCPUDescriptorHandleForHeapStart());

		return Result<void*>::Success(sampler);
	}

	void DestroySampler(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		Sampler* sampler = static_cast<Sampler*>(native);
		ReleaseCachedSamplerDescriptor(state, *sampler);
		DestroyObject(state.Resource, sampler);
	}
}
#endif
