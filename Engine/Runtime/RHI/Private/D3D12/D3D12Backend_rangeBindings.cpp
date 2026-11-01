#include "D3D12Backend.h"
#include "D3D12Descriptors.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<void> WriteNativeFixedDescriptor(
		State& state,
		Pending* pending,
		const BindingRangeDesc& range,
		const NativeFixedDescriptor& value,
		D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		using R = Result<void>;
		const bool bSampler = range.Type == EDescriptorType::Sampler;
		const D3D12_DESCRIPTOR_HEAP_TYPE type = bSampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		const auto copy = [&](D3D12_CPU_DESCRIPTOR_HANDLE source)
			{
				if (pending != nullptr)
				{
					CopyCommandDescriptors(state, *pending, 1, destination, source, type);
				}
				else
				{
					state.Device->CopyDescriptorsSimple(1, destination, source, type);
				}
			};
		if (bSampler)
		{
			copy(static_cast<const Sampler*>(value.Resource)->Descriptor->GetCPUDescriptorHandleForHeapStart());
		}
		else if (range.Type == EDescriptorType::AccelerationStructure)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
			desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			desc.RaytracingAccelerationStructure.Location = static_cast<const NativeAccelStruct*>(value.Resource)->Address;
			state.Device->CreateShaderResourceView(nullptr, &desc, destination);
			if (pending != nullptr)
			{
				++pending->Statistics.DescriptorWriteCalls;
				++pending->Statistics.DescriptorCount;
			}
		}
		else if (range.Type == EDescriptorType::SampledTexture || range.Type == EDescriptorType::StorageTexture)
		{
			const Texture& texture = *static_cast<const Texture*>(value.Resource);
			copy(TextureDescriptorCpu(texture, value.View, range.Type == EDescriptorType::StorageTexture));
		}
		else
		{
			const Buffer& buffer = *static_cast<const Buffer*>(value.Resource);
			if (value.View != nullptr)
			{
				copy(static_cast<const BufferView*>(value.View)->Descriptor->GetCPUDescriptorHandleForHeapStart());
			}
			else if (range.Type == EDescriptorType::UniformBuffer)
			{
				ASSERT(value.ByteOffset % 256 == 0 && value.ByteCount % 256 == 0 && value.ByteCount <= 65536, "A constant buffer range must meet native alignment and size limits.");
				const D3D12_CONSTANT_BUFFER_VIEW_DESC desc{ buffer.Resource->GetGPUVirtualAddress() + value.ByteOffset,
														   static_cast<UINT>(value.ByteCount) };
				state.Device->CreateConstantBufferView(&desc, destination);
				if (pending != nullptr)
				{
					++pending->Statistics.DescriptorWriteCalls;
					++pending->Statistics.DescriptorCount;
				}
			}
			else
			{
				const uint32 elementBytes = range.StructureStrideBytes != 0 ? range.StructureStrideBytes : 4;
				ASSERT(value.ByteOffset % elementBytes == 0 &&
					value.ByteCount % elementBytes == 0 &&
					value.ByteCount / elementBytes <= UINT32_MAX,
					"The native buffer descriptor exceeds its element bounds.");
				if (range.Type == EDescriptorType::ReadOnlyBuffer)
				{
					D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
					desc.Format = range.StructureStrideBytes != 0 ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
					desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
					desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					desc.Buffer = { value.ByteOffset / elementBytes, static_cast<UINT>(value.ByteCount / elementBytes),
								   range.StructureStrideBytes,
								   range.StructureStrideBytes != 0 ? D3D12_BUFFER_SRV_FLAG_NONE : D3D12_BUFFER_SRV_FLAG_RAW };
					state.Device->CreateShaderResourceView(buffer.Resource.Get(), &desc, destination);
					if (pending != nullptr)
					{
						++pending->Statistics.DescriptorWriteCalls;
						++pending->Statistics.DescriptorCount;
					}
				}
				else
				{
					D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
					desc.Format = range.StructureStrideBytes != 0 ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
					desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
					desc.Buffer.FirstElement = value.ByteOffset / elementBytes;
					desc.Buffer.NumElements = static_cast<UINT>(value.ByteCount / elementBytes);
					desc.Buffer.StructureByteStride = range.StructureStrideBytes;
					desc.Buffer.Flags = range.StructureStrideBytes != 0 ? D3D12_BUFFER_UAV_FLAG_NONE : D3D12_BUFFER_UAV_FLAG_RAW;
					state.Device->CreateUnorderedAccessView(buffer.Resource.Get(), nullptr, &desc, destination);
					if (pending != nullptr)
					{
						++pending->Statistics.DescriptorWriteCalls;
						++pending->Statistics.DescriptorCount;
					}
				}
			}
		}

		return R::Success();
	}

	[[nodiscard]] Result<uint32> CaptureTableArenaRange(State& state, Pending& pending, DescriptorTable& table)
	{
		if (table.FirstGlobalDescriptor != UINT32_MAX)
		{
			++pending.Statistics.DescriptorTableCacheHits;

			return Result<uint32>::Success(table.FirstGlobalDescriptor);
		}
		if (!EnsureGlobalDescriptorHeaps(state))
		{
			return Result<uint32>::Failure(Failure("Rhi.Allocation", "Could not initialize immutable table arenas."));
		}
		const bool bSampler = table.Type == EDescriptorType::Sampler;
		Result<uint32> allocated = AllocateGlobalDescriptors(state, bSampler, table.Capacity);
		if (!allocated)
		{
			return Result<uint32>::Failure(Move(allocated.GetError()));
		}
		const D3D12_DESCRIPTOR_HEAP_TYPE type = bSampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		CopyCommandDescriptors(state, pending, table.Capacity, GlobalDescriptorCpu(state, bSampler, allocated.Value()),
			table.CpuHeap->GetCPUDescriptorHandleForHeapStart(), type);
		table.FirstGlobalDescriptor = allocated.Value();
		++pending.Statistics.DescriptorTableMaterializations;

		return allocated;
	}

	void TransitionTableUses(Pending& pending, const DescriptorTable& table, Span<const uint32> used, bool bGraphics) noexcept
	{
		for (const uint32 index : used)
		{
			if (table.Type == EDescriptorType::SampledTexture || table.Type == EDescriptorType::StorageTexture)
			{
				d3d12::Texture& texture = *table.Textures[index];
				const bool bStorage = table.Type == EDescriptorType::StorageTexture;
				const d3d12::TextureView* sampledView = table.TextureViews[index];
				const bool bStencil = sampledView != nullptr && sampledView->Range.Aspect == ETextureAspect::Stencil;
				if (bStorage || &texture != (bStencil ? pending.ReadOnlyStencilTarget : pending.ReadOnlyDepthTarget))
				{
					TransitionTextureView(pending, texture, table.TextureViews[index],
						bStorage ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						: bGraphics
						? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
						: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				}
			}
			else if (table.Type == EDescriptorType::UniformBuffer || table.Type == EDescriptorType::ReadOnlyBuffer || table.Type == EDescriptorType::StorageBuffer)
			{
				d3d12::Buffer& buffer = *table.Buffers[index];
				const D3D12_RESOURCE_STATES target =
					table.Type == EDescriptorType::UniformBuffer ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
					: table.Type == EDescriptorType::StorageBuffer ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
					: bGraphics ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
					: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, target);
			}
		}
	}

	[[nodiscard]] Result<void> RecordDeviceHeapUses(State* opaque, void* native, bool bGraphics, Span<const NativeDescriptorHeapBinding> heaps)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		if (!state.bDeviceHeapIndexingEnabled)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorHeap", "Device heap indexing was not enabled.");
		}
		for (const rhi::NativeDescriptorHeapBinding& heap : heaps)
		{
			TransitionTableUses(pending, *static_cast<const DescriptorTable*>(heap.Table), heap.UsedEntries, bGraphics);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> WriteFixedBindingSets(
		State* opaque,
		void* native,
		void* pipelineNative,
		bool bGraphics,
		Span<const NativeFixedBindingSet> sets,
		Pending::PreparedBinding* prepared,
		RayTracingPipeline* rayPipeline,
		bool bReuseRayHeap)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		const NativeFixedLayoutState& fixed = rayPipeline != nullptr ? rayPipeline->Fixed
			: bGraphics ? static_cast<GraphicsPipeline*>(pipelineNative)->Fixed
			: static_cast<ComputePipeline*>(pipelineNative)->Fixed;
		ASSERT(sets.Size() == fixed.LayoutCount && fixed.LayoutCount <= MAXIMUM_FIXED_BINDING_LAYOUTS, "Fixed sets match the native pipeline.");
		const bool bDeviceHeapIndexed =
			rayPipeline != nullptr ||
			(bGraphics ? static_cast<const GraphicsPipeline*>(pipelineNative)->bDeviceHeapIndexed : static_cast<const ComputePipeline*>(pipelineNative)->bDeviceHeapIndexed);
		const bool bPackedHeapIndexing = bReuseRayHeap ||
			(rayPipeline == nullptr &&
					!bDeviceHeapIndexed &&
					(bGraphics ? static_cast<const GraphicsPipeline*>(pipelineNative)->bHeapDirectlyIndexed : static_cast<const ComputePipeline*>(pipelineNative)->bHeapDirectlyIndexed));
		bool bReusePackedHeap = bPackedHeapIndexing &&
			!bReuseRayHeap &&
			prepared == nullptr &&
			pending.LastPackedPipeline == pipelineNative &&
			pending.bLastPackedGraphics == bGraphics &&
			pending.LastPackedSetCount == sets.Size();
		for (uint32 index = 0; bReusePackedHeap && index < sets.Size(); ++index)
		{
			bReusePackedHeap = sets[index].Cache && pending.LastPackedSets[index].Get() == sets[index].Cache.Get();
		}
		if (prepared != nullptr && !bDeviceHeapIndexed && !bPackedHeapIndexing && !sets.IsEmpty())
		{
			Result<bool> cached = PrepareCachedFixedBindingSets(state, pending, fixed, bGraphics, sets, *prepared);
			if (!cached)
			{
				return Result<void>::Failure(cached.GetError());
			}
			if (cached.Value())
			{
				return Result<void>::Success();
			}
		}
		uint32 resourceCount = 0;
		uint32 samplerCount = 0;
		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			for (uint32 range = 0; range < fixed.RangeCounts[layout]; ++range)
			{
				const BindingRangeDesc& item = fixed.Ranges[layout][range];
				if (item.Source == EBindingRangeSource::Fixed && item.BufferMode != EBufferBindingMode::RootDescriptor)
				{
					(item.Type == EDescriptorType::Sampler ? samplerCount : resourceCount) += item.DescriptorCount;
				}
			}
		} uint32 fixedTextures = 0; uint32 fixedSamplers = 0; uint32 tableCounts[7]
		{}; uint32 tableCursors[7]
		{};
		const auto tableKind = [](EDescriptorType type) ->
			uint32
			{
				switch (type)
				{
				case EDescriptorType::SampledTexture: return 0;
				case EDescriptorType::StorageBuffer: return 1;
				case EDescriptorType::StorageTexture: return 2;
				case EDescriptorType::UniformBuffer: return 3;
				case EDescriptorType::ReadOnlyBuffer: return 4;
				case EDescriptorType::AccelerationStructure: return 5;
				case EDescriptorType::Sampler: return 6;
				default: return 7;
				}
			};
		if (bPackedHeapIndexing)
		{
			for (
				uint32 layout = 0; layout < fixed.LayoutCount; ++layout)
			{
				for (uint32 index = 0; index < fixed.RangeCounts[layout]; ++index)
				{
					const BindingRangeDesc& range = fixed.Ranges[layout][index];

					if (range.Source == EBindingRangeSource::DescriptorTable)
					{
						const uint32 kind = tableKind(range.Type);
						ASSERT(kind < 7 && (bReuseRayHeap || tableCounts[kind] == 0), "Packed tables have supported types.");
						tableCounts[kind] += range.DescriptorCount;
					}
					else if (range.Type == EDescriptorType::Sampler)
					{
						fixedSamplers += range.DescriptorCount;
					}
					else if (range.Type == EDescriptorType::SampledTexture || range.Type == EDescriptorType::StorageTexture)
					{
						fixedTextures += range.DescriptorCount;
					}
				}
			}
			resourceCount += tableCounts[0] + tableCounts[1] + tableCounts[2] + tableCounts[3] + tableCounts[4] + tableCounts[5];
			samplerCount += tableCounts[6];
		}
		const uint32 resourceCapacity = bPackedHeapIndexing ? Max(resourceCount, 256u) : (prepared != nullptr ? pending.PreparedTextureHeapCapacity : 256u);
		const uint32 samplerCapacity = bPackedHeapIndexing ? Max(samplerCount, 256u) : (prepared != nullptr ? pending.PreparedSamplerHeapCapacity : 256u);
		const uint32 resourceBase = bReuseRayHeap ? pending.TextureHeapUsed : 0;
		const uint32 samplerBase = bReuseRayHeap ? pending.SamplerHeapUsed : 0;
		uint32 fixedTextureCursor = resourceBase;
		uint32 fixedSamplerCursor = samplerBase;
		uint32 fixedBufferCursor = resourceBase + fixedTextures + tableCounts[0] + tableCounts[1] + tableCounts[2] + tableCounts[3] + tableCounts[4] + tableCounts[5];
		bool bSharedHeaps = rayPipeline != nullptr ? (rayPipeline->bTypedLocalRoots && !bReuseRayHeap)
			: bGraphics ? static_cast<const GraphicsPipeline*>(pipelineNative)->bDeviceHeapIndexed
			: static_cast<const ComputePipeline*>(pipelineNative)->bDeviceHeapIndexed;
		for (uint32 layout = 0; layout < fixed.LayoutCount; ++layout)
		{
			for (uint32 index = 0; index < fixed.RangeCounts[layout]; ++index)
			{
				bSharedHeaps |= !bPackedHeapIndexing && fixed.Ranges[layout][index].Source == EBindingRangeSource::DescriptorTable;
			}
		}
		bool bCacheFixedRanges = prepared == nullptr && rayPipeline == nullptr && !bPackedHeapIndexing && !sets.IsEmpty();
		for (uint32 index = 0; bCacheFixedRanges && index < sets.Size(); ++index)
		{
			bCacheFixedRanges = sets[index].Cache && sets[index].Cache->bDescriptorsReady && sets[index].TableCount == 0;
		}
		bool bReuseFixedRanges = bCacheFixedRanges && pending.LastRangePipeline == pipelineNative && pending.bLastRangeGraphics == bGraphics && pending.LastRangeSetCount == sets.Size();
		for (uint32 index = 0; bReuseFixedRanges && index < sets.Size(); ++index)
		{
			bReuseFixedRanges = pending.LastRangeSets[index].Get() == sets[index].Cache.Get();
		}
		const usize originalClaims = pending.GlobalDescriptorRanges.Size();
		ScopeExit rollbackClaims([&]() noexcept {
			while (pending.GlobalDescriptorRanges.Size() > originalClaims)
			{
				const Pending::GlobalDescriptorRange range = pending.GlobalDescriptorRanges.Back();
				ReleaseGlobalDescriptors(state, range.bSampler, range.First, range.Count);
				pending.GlobalDescriptorRanges.PopBack();
			}
			});
		uint32 sharedResources = 0;
		uint32 sharedSamplers = 0;
		if (bSharedHeaps)
		{
			if (!EnsureGlobalDescriptorHeaps(state))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not initialize shared descriptor arenas."));
			}

			if (resourceCount != 0 && !bReuseFixedRanges)
			{
				Result<uint32> allocated = AllocatePendingGlobalDescriptors(state, pending, false, resourceCount);
				if (!allocated)
				{
					return Result<void>::Failure(allocated.GetError());
				}
				sharedResources = allocated.Value();
			}
			if (samplerCount != 0 && !bReuseFixedRanges)
			{
				Result<uint32> allocated = AllocatePendingGlobalDescriptors(state, pending, true, samplerCount);
				if (!allocated)
				{
					return Result<void>::Failure(allocated.GetError());
				}
				sharedSamplers = allocated.Value();
			}
		}
		const bool bNewResource =
			!bSharedHeaps &&
			resourceCount != 0 &&
			(!bReuseRayHeap && !bReusePackedHeap && !bReuseFixedRanges && (bPackedHeapIndexing || pending.TextureHeaps.IsEmpty() || pending.TextureHeapUsed + resourceCount > resourceCapacity));
		const bool bNewSampler =
			!bSharedHeaps &&
			samplerCount != 0 &&
			(!bReuseRayHeap && !bReusePackedHeap && !bReuseFixedRanges && (bPackedHeapIndexing || pending.SamplerHeaps.IsEmpty() || pending.SamplerHeapUsed + samplerCount > samplerCapacity));
		if (!pending.TextureHeaps.Reserve(pending.TextureHeaps.Size() + (bNewResource ? 1u : 0u)) || !pending.SamplerHeaps.Reserve(pending.SamplerHeaps.Size() + (bNewSampler ? 1u : 0u)))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain native descriptor heaps."));
		}
		ComPtr<ID3D12DescriptorHeap> resources;
		ComPtr<ID3D12DescriptorHeap> samplers;
		const auto createHeap = [&](bool bSampler, ComPtr<ID3D12DescriptorHeap>& owner)
			{
				D3D12_DESCRIPTOR_HEAP_DESC desc{};
				desc.Type = bSampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				desc.NumDescriptors = bSampler ? samplerCapacity : resourceCapacity;
				desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				return SUCCEEDED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(owner.GetAddressOf())));
			};
		if ((bNewResource && !createHeap(false, resources)) || (bNewSampler && !createHeap(true, samplers)))
		{
			return Result<void>::Failure(Failure("Rhi.BindingRange", "Could not allocate native descriptor heaps."));
		}
		if (bNewResource)
		{
			(void)pending.TextureHeaps.Add(Move(resources));
			pending.TextureHeapUsed = 0;
		}
		if (bNewSampler)
		{
			(void)pending.SamplerHeaps.Add(Move(samplers));
			pending.SamplerHeapUsed = 0;
		}
		ID3D12DescriptorHeap* heaps[2]{};
		uint32 heapCount = 0;
		ID3D12DescriptorHeap* resourceHeap = bReuseFixedRanges ? pending.LastRangeResources : bReusePackedHeap ? pending.LastPackedResources :
			bReuseRayHeap && !pending.TextureHeaps.IsEmpty() ? pending.TextureHeaps.Back().Get() : bSharedHeaps ? state.GlobalResourceHeap.Get()
			: resourceCount != 0 ? pending.TextureHeaps.Back().Get()
			: nullptr;
		if (!bSharedHeaps && prepared != nullptr && resourceCount == 0 && samplerCount != 0)
		{
			Result<void> empty = EnsureEmptyTextureHeap(state, pending);
			if (!empty)
			{
				return empty;
			}
			resourceHeap = pending.EmptyTextureHeap.Get();
		}
		ID3D12DescriptorHeap* samplerHeap = bReuseFixedRanges ? pending.LastRangeSamplers : bReusePackedHeap ? pending.LastPackedSamplers :
			bReuseRayHeap && !pending.SamplerHeaps.IsEmpty() ? pending.SamplerHeaps.Back().Get() : bSharedHeaps ? state.GlobalSamplerHeap.Get()
			: samplerCount != 0 ? pending.SamplerHeaps.Back().Get()
			: nullptr;
		if (resourceHeap != nullptr)
		{
			heaps[heapCount++] = resourceHeap;
		}
		if (samplerHeap != nullptr)
		{
			heaps[heapCount++] = samplerHeap;
		}
		if (prepared == nullptr)
		{
			pending.List->SetDescriptorHeaps(heapCount, heaps);
			pending.BoundTextureHeap = resourceHeap;
			pending.BoundSamplerHeap = samplerHeap;
			pending.LastComputePipeline = nullptr; pending.LastFixedComputePipeline = nullptr;
			pending.LastTexturePipeline = nullptr;
			if (bGraphics)
			{
				pending.List->SetGraphicsRootSignature(
					static_cast<GraphicsPipeline*>
					(pipelineNative)->RootSignature.Get());
			}
			else
			{
				pending.List->SetComputeRootSignature(
					rayPipeline != nullptr ? rayPipeline->RootSignature.Get()
					: static_cast<ComputePipeline*>(pipelineNative)->RootSignature.Get());
			}
		}
		else
		{
			prepared->Resources = resourceHeap;
			prepared->Samplers = samplerHeap;
		}
		const uint32 rootOffset =
			(rayPipeline != nullptr ? rayPipeline->PushConstantBytes
				: bGraphics ? static_cast<GraphicsPipeline*>(pipelineNative)->PushConstantBytes
				: static_cast<ComputePipeline*> (pipelineNative)->PushConstantBytes) != 0 ? 1u : 0u;
		const uint32 rangeResourceFirst = bReuseFixedRanges ? pending.LastRangeResourceFirst : bSharedHeaps ? sharedResources : pending.TextureHeapUsed;
		const uint32 rangeSamplerFirst = bReuseFixedRanges ? pending.LastRangeSamplerFirst : bSharedHeaps ? sharedSamplers : pending.SamplerHeapUsed; uint32 reusedResourceCursor = rangeResourceFirst;
		uint32 reusedSamplerCursor = rangeSamplerFirst;
		uint32 root = rootOffset;
		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			uint32 descriptor = 0;
			for (uint32 rangeIndex = 0; rangeIndex < fixed.RangeCounts[layout]; ++rangeIndex, ++root)
			{
				const BindingRangeDesc& range = fixed.Ranges[layout][rangeIndex];
				if (range.Source == EBindingRangeSource::DescriptorTable)
				{
					const NativeTableRangeBinding* captured = nullptr;
					for (uint32 index = 0; index < sets[layout].TableCount; ++index)
					{
						if (sets[layout].Tables[index].RangeIndex == rangeIndex)
						{
							captured = &sets[layout].Tables[index];
						}
					}
					ASSERT(captured != nullptr && captured->Table != nullptr, "Every immutable native table range is captured.");
					DescriptorTable& table = *static_cast<DescriptorTable*>(captured->Table);
					if (bPackedHeapIndexing)
					{
						const uint32 kind = tableKind(range.Type);
						const bool bSamplerTable = kind == 6;
						uint32 offset = bSamplerTable ? samplerBase + fixedSamplers : resourceBase + fixedTextures;
						if (!bSamplerTable)
						{
							for
								(uint32 previous = 0; previous < kind; ++previous)
							{
								offset += tableCounts[previous];
							}
						}
						offset += tableCursors[kind];
						tableCursors[kind] += table.Capacity;
						ID3D12DescriptorHeap* heap = bSamplerTable ? samplerHeap : resourceHeap;
						const D3D12_DESCRIPTOR_HEAP_TYPE type = bSamplerTable ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
						const UINT stride = state.Device->GetDescriptorHandleIncrementSize(type);
						D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
						D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
						cpu.ptr += SIZE_T(offset) * stride;
						gpu.ptr += UINT64(offset) * stride;
						if (!bReusePackedHeap)
						{
							CopyCommandDescriptors(state, pending, table.Capacity, cpu, table.CpuHeap->GetCPUDescriptorHandleForHeapStart(), type);
						}
						if (prepared != nullptr)
						{
							prepared->Tables[prepared->TableCount++] = gpu;
						}
						else
						{
							TransitionTableUses(pending, table, captured->UsedEntries, bGraphics);
							if (bGraphics)
							{
								pending.List->SetGraphicsRootDescriptorTable(root, gpu);
							}
							else
							{
								pending.List->SetComputeRootDescriptorTable(root, gpu);
							}
						}
						continue;
					}
					Result<uint32> arena = CaptureTableArenaRange(state, pending, table);
					if (!arena)
					{
						return Result<void>::Failure(Move(arena.GetError()));
					}
					const D3D12_GPU_DESCRIPTOR_HANDLE gpu = GlobalDescriptorGpu(state, range.Type == EDescriptorType::Sampler, arena.Value());
					if (prepared != nullptr)
					{
						prepared->Tables[
							prepared->TableCount++] = gpu;
					}
					else
					{
						TransitionTableUses(pending, table, captured->UsedEntries, bGraphics);
						if (bGraphics)
						{
							pending.List->SetGraphicsRootDescriptorTable(root, gpu);
						}
						else
						{
							pending.List->SetComputeRootDescriptorTable(root, gpu);
						}
					}
					continue;
				}
				if (range.BufferMode == EBufferBindingMode::RootDescriptor)
				{
					ASSERT(descriptor < sets[layout].Descriptors.Size(), "A root descriptor retains its buffer.");
					const NativeFixedDescriptor& value = sets[layout].Descriptors[descriptor++];
					const Buffer& buffer = *static_cast<const Buffer*>(value.Resource);
					const D3D12_GPU_VIRTUAL_ADDRESS address = buffer.Resource->GetGPUVirtualAddress() + value.ByteOffset;
					if (prepared != nullptr)
					{
						const uint32 index = prepared->TableCount++;
						prepared->bRootDescriptors[index] = true;
						prepared->RootTypes[index] = range.Type;
						prepared->RootAddresses[index] = address;
					}
					else
					{
						const D3D12_RESOURCE_STATES target = range.Type == EDescriptorType::UniformBuffer
							? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
							: range.Type == EDescriptorType::StorageBuffer ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
							: bGraphics ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
							: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
						if (value.bUsed)
						{
							Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, target);
						}
						if (bGraphics)
						{
							if (range.Type == EDescriptorType::UniformBuffer)
							{
								pending.List->SetGraphicsRootConstantBufferView(root, address);
							}
							else if (range.Type == EDescriptorType::ReadOnlyBuffer)
							{
								pending.List->SetGraphicsRootShaderResourceView(root, address);
							}
							else
							{
								pending.List->SetGraphicsRootUnorderedAccessView(root, address);
							}
						}
						else
						{
							if (range.Type == EDescriptorType::UniformBuffer)
							{
								pending.List->SetComputeRootConstantBufferView(root, address);
							}
							else
								if (range.Type == EDescriptorType::ReadOnlyBuffer)
								{
									pending.List->SetComputeRootShaderResourceView(root, address);
								}
								else
								{
									pending.List->SetComputeRootUnorderedAccessView(root, address);
								}
						}
					}
					continue;
				}
				const bool bSampler = range.Type == EDescriptorType::Sampler;
				const D3D12_DESCRIPTOR_HEAP_TYPE type = bSampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				const UINT stride = state.Device->GetDescriptorHandleIncrementSize(type);
				ID3D12DescriptorHeap* heap = bSampler ? samplerHeap : resourceHeap;
				D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
				D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
				const bool bTexture = range.Type == EDescriptorType::SampledTexture || range.Type == EDescriptorType::StorageTexture;
				uint32& used = bReuseFixedRanges ? (bSampler ? reusedSamplerCursor : reusedResourceCursor) :
					bPackedHeapIndexing ? (bSampler ? fixedSamplerCursor : (bTexture ? fixedTextureCursor : fixedBufferCursor)) :
					(bSharedHeaps ? (bSampler ? sharedSamplers : sharedResources)
						: (bSampler ? pending.SamplerHeapUsed : pending.TextureHeapUsed));
				cpu.ptr += SIZE_T(used) * stride;
				gpu.ptr += UINT64(used) * stride;
				if (prepared != nullptr)
				{
					prepared->Tables[prepared->TableCount++] = gpu;
				}
				else
				{
					if (bGraphics)
					{
						pending.List->SetGraphicsRootDescriptorTable(root, gpu);
					}
					else
					{
						pending.List->SetComputeRootDescriptorTable(root, gpu);
					}
				}
				for (uint32 element = 0; element < range.DescriptorCount; ++element)
				{
					ASSERT(descriptor < sets[layout].Descriptors.Size(), "Every native range element is captured.");
					const rhi::NativeFixedDescriptor& value = sets[layout].Descriptors[descriptor++];
					if (prepared == nullptr && value.bUsed && (range.Type == EDescriptorType::SampledTexture || range.Type == EDescriptorType::StorageTexture))
					{
						Texture& texture = *static_cast<Texture*>(value.Resource);
						const bool bStorage = range.Type == EDescriptorType::StorageTexture;
						const TextureView* sampledView = static_cast<const TextureView*>(value.View);
						const bool bStencil = sampledView != nullptr && sampledView->Range.Aspect == ETextureAspect::Stencil;
						if (bStorage || &texture != (bStencil ? pending.ReadOnlyStencilTarget : pending.ReadOnlyDepthTarget))
						{
							TransitionTextureView(pending, texture, static_cast<const TextureView*>(value.View),
								bStorage ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
								: bGraphics ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
								D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
								: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
						}
					}
					else if (prepared == nullptr &&
						value.bUsed &&
						(range.Type == EDescriptorType::UniformBuffer || range.Type == EDescriptorType::ReadOnlyBuffer || range.Type == EDescriptorType::StorageBuffer))
					{
						Buffer& buffer = *static_cast<Buffer*>(value.Resource);
						const D3D12_RESOURCE_STATES target =
							range.Type == EDescriptorType::UniformBuffer ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
							: range.Type == EDescriptorType::StorageBuffer ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
							: bGraphics ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
							: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
						Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, target);
					}
					if (!bReusePackedHeap && !bReuseFixedRanges)
					{
						Result<void> written = WriteNativeFixedDescriptor(state, &pending, range, value, cpu);
						if (!written)
						{
							return written;
						}
					}

					cpu.ptr += stride;
				}
				used += range.DescriptorCount;
			}
			ASSERT(descriptor == sets[layout].Descriptors.Size(), "Descriptor snapshots have exact native range counts.");
		}
		if (bPackedHeapIndexing && !bReusePackedHeap && resourceCount != 0)
		{
			pending.TextureHeapUsed = resourceBase + resourceCount;
		}
		if (bPackedHeapIndexing && !bReusePackedHeap && samplerCount != 0)
		{
			pending.SamplerHeapUsed = samplerBase + samplerCount;
		}
		if (bPackedHeapIndexing && !bReuseRayHeap && prepared == nullptr && !bReusePackedHeap)
		{
			pending.LastPackedPipeline = pipelineNative;
			pending.bLastPackedGraphics = bGraphics;
			pending.LastPackedSetCount = static_cast<uint32>(sets.Size());
			for (uint32 index = 0; index < sets.Size(); ++index)
			{
				pending.LastPackedSets[index] = sets[index].Cache;
			}
			pending.LastPackedResources = resourceHeap;
			pending.LastPackedSamplers = samplerHeap;
		}
		if (bCacheFixedRanges && !bReuseFixedRanges)
		{
			pending.LastRangePipeline = pipelineNative;
			pending.bLastRangeGraphics = bGraphics;
			pending.LastRangeSetCount = static_cast<uint32>(sets.Size());
			for (uint32 index = 0; index < sets.Size(); ++index)
			{
				pending.LastRangeSets[index] = sets[index].Cache;
			}
			pending.LastRangeResources = resourceHeap;
			pending.LastRangeSamplers = samplerHeap;
			pending.LastRangeResourceFirst = rangeResourceFirst;
			pending.LastRangeSamplerFirst = rangeSamplerFirst;
		}
		rollbackClaims.Release();

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordFixedBindingSets(State* opaque, void* native, void* pipelineNative, bool bGraphics, Span<const NativeFixedBindingSet> sets)
	{
		return WriteFixedBindingSets(opaque, native, pipelineNative, bGraphics, sets);
	}
}
#endif
