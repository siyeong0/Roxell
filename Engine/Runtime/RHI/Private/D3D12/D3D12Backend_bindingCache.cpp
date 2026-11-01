#include "../PreparedBindingCache.h"
#include "D3D12Backend.h"

#include <Trace.h>
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	namespace
	{
		constexpr uint32 FIXED_RESOURCE_DESCRIPTOR_COUNT = 64u * 1024u;
		constexpr uint32 FIXED_SAMPLER_DESCRIPTOR_COUNT = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
		static_assert(FIXED_RESOURCE_DESCRIPTOR_COUNT <= D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1);

		bool ensureFixedDescriptorHeaps(State& state)
		{
			LockGuard lock(state.DescriptorMutex);
			FixedDescriptorHeap& resources = state.FixedResourceDescriptors;
			FixedDescriptorHeap& samplers = state.FixedSamplerDescriptors;
			if (resources.Native && samplers.Native)
			{
				return true;
			}
			D3D12_DESCRIPTOR_HEAP_DESC resourceDesc{};
			resourceDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			resourceDesc.NumDescriptors = FIXED_RESOURCE_DESCRIPTOR_COUNT;
			resourceDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			D3D12_DESCRIPTOR_HEAP_DESC samplerDesc{};
			samplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			samplerDesc.NumDescriptors = FIXED_SAMPLER_DESCRIPTOR_COUNT;
			samplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			if (!resources.Used.Resize(FIXED_RESOURCE_DESCRIPTOR_COUNT) ||
				!samplers.Used.Resize(FIXED_SAMPLER_DESCRIPTOR_COUNT) ||
				FAILED(state.Device->CreateDescriptorHeap(&resourceDesc, IID_PPV_ARGS(resources.Native.GetAddressOf()))) ||
				FAILED(state.Device->CreateDescriptorHeap(&samplerDesc, IID_PPV_ARGS(samplers.Native.GetAddressOf()))))
			{
				resources.Native.Reset();
				samplers.Native.Reset();
				resources.Used.Reset();
				samplers.Used.Reset();

				return false;
			}
			for (unsigned char& used : resources.Used)
			{
				used = 0;
			}
			for (unsigned char& used : samplers.Used)
			{
				used = 0;
			}

			return true;
		}

		bool allocateFixedDescriptors(State& state, bool bSampler, uint32 count, uint32& first)
		{
			LockGuard lock(state.DescriptorMutex);
			FixedDescriptorHeap& heap = bSampler ? state.FixedSamplerDescriptors : state.FixedResourceDescriptors;
			const uint32 capacity = static_cast<uint32>(heap.Used.Size());
			ASSERT(heap.LiveCount <= capacity, "Cache occupancy remains within its fixed heap capacity.");
			if (count == 0 || count > capacity - heap.LiveCount)
			{
				return false;
			}
			for (uint32 pass = 0; pass < 2; ++pass)
			{
				const uint32 begin = pass == 0 ? heap.Search : 0;
				const uint32 end = pass == 0 ? capacity : heap.Search;
				uint32 freeCount = 0;
				for (uint32 index = begin; index < end; ++index)
				{
					freeCount = heap.Used[index] == 0 ? freeCount + 1 : 0;
					if (freeCount != count)
					{
						continue;
					}
					first = index + 1 - count;
					for (uint32 entry = first; entry <= index; ++entry)
					{
						heap.Used[entry] = 1;
					}
					heap.Search = index + 1;
					heap.LiveCount += count;
					heap.HighWater = Max(heap.HighWater, heap.LiveCount);

					return true;
				}
			}

			return false;
		}

		void releaseFixedDescriptors(State& state, bool bSampler, uint32 first, uint32 count) noexcept
		{
			LockGuard lock(state.DescriptorMutex);
			FixedDescriptorHeap& heap = bSampler ? state.FixedSamplerDescriptors : state.FixedResourceDescriptors;
			ASSERT(first <= heap.Used.Size() && count <= heap.Used.Size() - first, "A cached descriptor range remains within its dedicated heap.");
			ASSERT(count <= heap.LiveCount, "Cache occupancy balances allocated descriptor owners.");
			for (uint32 index = first; index < first + count; ++index)
			{
				ASSERT(heap.Used[index] != 0, "Only allocated cache descriptors are released.");
				heap.Used[index] = 0;
			}
			heap.LiveCount -= count;
			if (first < heap.Search)
			{
				heap.Search = first;
			}
		}

		D3D12_CPU_DESCRIPTOR_HANDLE fixedDescriptorCpu(const State& state, bool bSampler, uint32 index)
		{
			const FixedDescriptorHeap& heap = bSampler ? state.FixedSamplerDescriptors : state.FixedResourceDescriptors;
			const D3D12_DESCRIPTOR_HEAP_TYPE type = bSampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			D3D12_CPU_DESCRIPTOR_HANDLE handle = heap.Native->GetCPUDescriptorHandleForHeapStart();
			handle.ptr += static_cast<SIZE_T>(index) * state.Device->GetDescriptorHandleIncrementSize(type);

			return handle;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE fixedDescriptorGpu(const State& state, bool bSampler, uint32 index)
		{
			const FixedDescriptorHeap& heap = bSampler ? state.FixedSamplerDescriptors : state.FixedResourceDescriptors;
			const D3D12_DESCRIPTOR_HEAP_TYPE type = bSampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			D3D12_GPU_DESCRIPTOR_HANDLE handle = heap.Native->GetGPUDescriptorHandleForHeapStart();
			handle.ptr += static_cast<UINT64>(index) * state.Device->GetDescriptorHandleIncrementSize(type);

			return handle;
		}

		struct FixedDescriptorRealization final
		{
			// BindingSetSlot owns this cache and pins every physical object until after cache destruction.
			// PreparedReferences pins that set while Pending retains these descriptor ranges.
			BindingRangeDesc Ranges[MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};
			NativeFixedDescriptor Descriptors[32]{};
			D3D12_GPU_DESCRIPTOR_HANDLE Tables[MAXIMUM_BINDING_RANGES_PER_LAYOUT]{};

			uint32 RangeCount = 0;
			uint32 DescriptorCount = 0;

			uint32 Space = 0;

			uint32 ResourceFirst = 0;
			uint32 ResourceCount = 0;

			uint32 SamplerFirst = 0;
			uint32 SamplerCount = 0;

			bool bGraphics = false;
			bool bImmutableTuples = false;
		};

		void destroyFixedDescriptors(State* opaque, void* native) noexcept
		{
			State& state = *opaque;
			FixedDescriptorRealization* realization = static_cast<FixedDescriptorRealization*>(native);
			if (realization->ResourceCount)
			{
				releaseFixedDescriptors(state, false, realization->ResourceFirst, realization->ResourceCount);
			}
			if (realization->SamplerCount)
			{
				releaseFixedDescriptors(state, true, realization->SamplerFirst, realization->SamplerCount);
			}
			DestroyObject(state.Resource, realization);
		}

		bool matchesFixedDescriptors(const FixedDescriptorRealization& retained, const NativeFixedLayoutState& fixed, uint32 layout, const NativeFixedBindingSet& set, bool bGraphics) noexcept
		{
			if (retained.RangeCount != fixed.RangeCounts[layout] || retained.Space != fixed.Spaces[layout] || retained.bGraphics != bGraphics || retained.DescriptorCount != set.Descriptors.Size())
			{
				return false;
			}
			// RHI validated the exact pipeline layout. This binding-set owner pins immutable ranges and tuples.
			if (retained.bImmutableTuples && set.Cache && set.Cache->bDescriptorsReady && set.Descriptors.Data() == set.Cache->Descriptors && set.Descriptors.Size() == set.Cache->DescriptorCount)
			{
				return true;
			}
			for (uint32 index = 0; index < retained.RangeCount; ++index)
			{
				const BindingRangeDesc& left = retained.Ranges[index];
				const BindingRangeDesc& right = fixed.Ranges[layout][index];
				if (left.Type != right.Type ||
					left.Binding != right.Binding ||
					left.DescriptorCount != right.DescriptorCount ||
					left.BufferFormat != right.BufferFormat ||
					left.Visibility != right.Visibility ||
					left.StructureStrideBytes != right.StructureStrideBytes ||
					left.Source != right.Source)
				{
					return false;
				}
			}

			for (uint32 index = 0; index < retained.DescriptorCount; ++index)
			{
				const NativeFixedDescriptor& left = retained.Descriptors[index];
				const rhi::NativeFixedDescriptor& right = set.Descriptors[index];
				if (left.Resource != right.Resource || left.View != right.View || left.ByteOffset != right.ByteOffset || left.ByteCount != right.ByteCount || left.bUsed != right.bUsed)
				{
					return false;
				}
			}

			return true;
		}

		Result<SharedPtr<PreparedDescriptorSet>> createFixedDescriptors(
			State& state,
			Pending& pending,
			const NativeFixedLayoutState& fixed,
			uint32 layout,
			const NativeFixedBindingSet& set,
			bool bGraphics)
		{
			using R = Result<SharedPtr<PreparedDescriptorSet>>;
			FixedDescriptorRealization* realization = CreateObject<FixedDescriptorRealization>(state.Resource);
			if (realization == nullptr)
			{
				return R::Failure(Failure("Rhi.Allocation", "Could not own fixed descriptor storage."));
			}
			SharedPtr<PreparedDescriptorSet> owner;
			if (!MakeSharedInto(state.Resource, &owner, &state, realization))
			{
				destroyFixedDescriptors(&state, realization);

				return R::Failure(Failure("Rhi.Allocation", "Could not retain fixed descriptor storage."));
			}
			realization->RangeCount = fixed.RangeCounts[layout];
			realization->Space = fixed.Spaces[layout];
			realization->bGraphics = bGraphics;
			realization->DescriptorCount = static_cast<uint32>(set.Descriptors.Size());
			realization->bImmutableTuples = set.Cache && set.Cache->bDescriptorsReady && set.Descriptors.Data() == set.Cache->Descriptors && set.Descriptors.Size() == set.Cache->DescriptorCount;
			uint32 resourceCount = 0;
			uint32 samplerCount = 0;
			for (uint32 index = 0; index < realization->RangeCount; ++index)
			{
				const BindingRangeDesc& range = fixed.Ranges[layout][index];
				realization->Ranges[index] = range;
				if (range.Type != EDescriptorType::Sampler)
				{
					resourceCount += range.DescriptorCount;
				}
				else if (range.DescriptorCount != 1)
				{
					samplerCount += range.DescriptorCount;
				}
			}
			for (uint32 index = 0; index < realization->DescriptorCount; ++index)
			{
				realization->Descriptors[index] = set.Descriptors[index];
			}
			if (resourceCount)
			{
				if (!allocateFixedDescriptors(state, false, resourceCount, realization->ResourceFirst))
				{
					return R::Failure(Failure("Rhi.DescriptorCache", "Fixed resource heap is full."));
				}
				realization->ResourceCount = resourceCount;
			}
			if (samplerCount)
			{
				if (!allocateFixedDescriptors(state, true, samplerCount, realization->SamplerFirst))
				{
					return R::Failure(Failure("Rhi.DescriptorCache", "Fixed sampler heap is full."));
				}
				realization->SamplerCount = samplerCount;
			}
			uint32 resource = realization->ResourceFirst;
			uint32 sampler = realization->SamplerFirst;
			uint32 descriptor = 0;
			for (uint32 rangeIndex = 0; rangeIndex < realization->RangeCount; ++rangeIndex)
			{
				const BindingRangeDesc& range = realization->Ranges[rangeIndex];
				const bool bSampler = range.Type == EDescriptorType::Sampler;
				if (bSampler && range.DescriptorCount == 1)
				{
					const rhi::NativeFixedDescriptor& value = set.Descriptors[descriptor++];
					Sampler& sharedSampler = *static_cast<Sampler*>(value.Resource);
					if (sharedSampler.FixedDescriptorIndex == 0xffffffffu)
					{
						uint32 first = 0;
						if (!allocateFixedDescriptors(state, true, 1, first))
						{
							return R::Failure(Failure("Rhi.DescriptorCache", "Fixed sampler heap is full."));
						}
						Result<void> written = WriteNativeFixedDescriptor(state, &pending, range, value, fixedDescriptorCpu(state, true, first));
						if (!written)
						{
							releaseFixedDescriptors(state, true, first, 1);

							return R::Failure(Move(written.GetError()));
						}
						// BindingSetRefs and PreparedReferences retain this immutable native sampler.
						// Its singleton descriptor is released by DestroySampler, never by a realization.
						sharedSampler.FixedDescriptorIndex = first;
					}
					else
					{
						++pending.Statistics.SharedSamplerDescriptorReuses;
					}
					realization->Tables[rangeIndex] = fixedDescriptorGpu(state, true, sharedSampler.FixedDescriptorIndex);
					continue;
				}
				uint32& first = bSampler ? sampler : resource;
				realization->Tables[rangeIndex] = fixedDescriptorGpu(state, bSampler, first);
				for (uint32 element = 0; element < range.DescriptorCount; ++element)
				{
					Result<void> written = WriteNativeFixedDescriptor(state, &pending, range, set.Descriptors[descriptor++], fixedDescriptorCpu(state, bSampler, first++));
					if (!written)
					{
						return R::Failure(written.GetError());
					}
				}
			}
			++pending.Statistics.DescriptorTableMaterializations;

			return R::Success(Move(owner));
		}
	} // namespace
	Result<bool> PrepareCachedFixedBindingSets(
		State& state,
		Pending& pending,
		const NativeFixedLayoutState& fixed,
		bool bGraphics,
		Span<const NativeFixedBindingSet> sets,
		Pending::PreparedBinding& prepared)
	{
		ROX_PROFILE_DEEP_SCOPE("D3D12", "PrepareCachedFixedBindingSets");
		using R = Result<bool>;
		ScopeExit captureHighWater(
			[&]() noexcept
			{
				pending.Statistics.DescriptorCacheResourceHighWater = state.FixedResourceDescriptors.HighWater;
				pending.Statistics.DescriptorCacheSamplerHighWater = state.FixedSamplerDescriptors.HighWater;
			});
		ASSERT(sets.Size() == fixed.LayoutCount && sets.Size() <= MAXIMUM_FIXED_BINDING_LAYOUTS, "The retained fixed layout bounds every descriptor cache owner.");
		if (!fixed.bPreparedFixedCacheEligible)
		{
			return R::Success(false);
		}
		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			if (!sets[layout].Cache || sets[layout].TableCount || sets[layout].Descriptors.Size() > 32)
			{
				return R::Success(false);
			}
			ASSERT(fixed.FixedDescriptorCounts[layout] == sets[layout].Descriptors.Size(), "Every fixed descriptor has one validated physical tuple.");
		}
		if (!ensureFixedDescriptorHeaps(state))
		{
			++pending.Statistics.DescriptorCacheFallbacks;

			return R::Success(false);
		}
		SharedPtr<PreparedDescriptorSet> owners[MAXIMUM_FIXED_BINDING_LAYOUTS];
		bool reused[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			const rhi::NativeFixedBindingSet& set = sets[layout];
			for (uint32 index = 0; index < PreparedBindingCache::CAPACITY; ++index)
			{
				const SharedPtr<PreparedDescriptorSet>& candidate = set.Cache->Get(index);
				if (candidate && matchesFixedDescriptors(*static_cast<FixedDescriptorRealization*>(candidate->GetNative()), fixed, layout, set, bGraphics))
				{
					owners[layout] = candidate;
					reused[layout] = true;
					break;
				}
			}
			if (!owners[layout])
			{
				Result<SharedPtr<PreparedDescriptorSet>> created = createFixedDescriptors(state, pending, fixed, layout, set, bGraphics);
				if (!created)
				{
					++pending.Statistics.DescriptorCacheFallbacks;
					// A full arena or unavailable cache owner falls back to command-owned heaps.
					// No cache or prepared binding has captured a partial realization.
					return R::Success(false);
				}
				owners[layout] = Move(created).TakeValue();
			}
		}
		prepared.Resources = state.FixedResourceDescriptors.Native.Get();
		prepared.Samplers = state.FixedSamplerDescriptors.Native.Get();
		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			const FixedDescriptorRealization& realization = *static_cast<FixedDescriptorRealization*>(owners[layout]->GetNative());
			for (uint32 range = 0; range < realization.RangeCount; ++range)
			{
				prepared.Tables[prepared.TableCount++] = realization.Tables[range];
			}
			if (reused[layout])
			{
				++pending.Statistics.DescriptorTableCacheHits;
			}
			else
			{
				sets[layout].Cache->Capture(owners[layout]);
			}
			prepared.FixedDescriptors[layout] = Move(owners[layout]);
		}

		return R::Success(true);
	}

	void ReleaseCachedSamplerDescriptor(State& state, Sampler& sampler) noexcept
	{
		if (sampler.FixedDescriptorIndex != 0xffffffffu)
		{
			releaseFixedDescriptors(state, true, sampler.FixedDescriptorIndex, 1);
			sampler.FixedDescriptorIndex = 0xffffffffu;
		}
	}
}
#endif

namespace rox::d3d12
{
	void DestroyDescriptorSetRealization(State* opaque, void* native) noexcept
	{
		destroyFixedDescriptors(opaque, native);
	}
}
