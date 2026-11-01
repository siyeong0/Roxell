#include "D3D12Backend.h"

#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<void> RecordBeginMarker(State* opaque, void* native, const char* name)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);

		if (state.Diagnostics != nullptr && GetAftermathVulkanOptions(state.Diagnostics).bCheckpoints)
		{
			const Result<uint64> marker = state.Diagnostics->RegisterMarker(StringView(name, CStringLength(name)));

			if (!marker)
			{
				return Result<void>::Failure(marker.GetError());
			}

			if (pending.Aftermath.Native == nullptr)
			{
				const Result<void*> context = CreateAftermathD3D12Context(state.Diagnostics, pending.List.Get());

				if (!context)
				{
					(void)state.Diagnostics->RetireMarker(marker.Value());

					return Result<void>::Failure(context.GetError());
				}
				pending.Aftermath.Native = context.Value();
			}

			const Result<void> recorded = SetAftermathD3D12Marker(state.Diagnostics, pending.Aftermath.Native, marker.Value());

			if (!recorded)
			{
				(void)state.Diagnostics->RetireMarker(marker.Value());

				return recorded;
			}

			if (!pending.AftermathMarkers.Add(marker.Value()))
			{
				(void)state.Diagnostics->RetireMarker(marker.Value());

				return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain an Aftermath command marker.");
			}
		}
		PIXBeginEvent(pending.List.Get(), PIX_COLOR_DEFAULT, name);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordEndMarker(void*, void* native)
	{
		Pending& pending = *static_cast<Pending*>(native);
		PIXEndEvent(pending.List.Get());

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> CreateGpuTimer(State* opaque)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}
		GpuTimer* timer = CreateObject<GpuTimer>(state.Resource);

		if (timer == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 timer state."));
		}
		D3D12_QUERY_HEAP_DESC query{};
		query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		query.Count = 2;
		D3D12_FEATURE_DATA_D3D12_OPTIONS3 options{};
		const bool bCopyTimestamps = SUCCEEDED(state.Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options, sizeof(options))) && options.CopyQueueTimestampQueriesSupported;
		D3D12_QUERY_HEAP_DESC copyQuery = query;
		copyQuery.Type = D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP;

		if (FAILED(state.Device->CreateQueryHeap(&query, IID_PPV_ARGS(timer->Queries.GetAddressOf()))) ||
			(bCopyTimestamps && FAILED(state.Device->CreateQueryHeap(&copyQuery, IID_PPV_ARGS(timer->CopyQueries.GetAddressOf())))) ||
			!CreateResource(state, 2 * sizeof(uint64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, timer->Readback) ||
			FAILED(state.Queue->GetTimestampFrequency(&timer->DirectFrequency)) ||
			FAILED(state.ComputeQueue->GetTimestampFrequency(&timer->ComputeFrequency)) ||
			(bCopyTimestamps && FAILED(state.CopyQueue->GetTimestampFrequency(&timer->CopyFrequency))) ||
			timer->DirectFrequency == 0 ||
			timer->ComputeFrequency == 0)
		{
			DestroyObject(state.Resource, timer);

			return Result<void*>::Failure(Failure("Rhi.D3D12Timer", "Could not create a D3D12 timestamp query."));
		}

		return Result<void*>::Success(timer);
	}

	void DestroyGpuTimer(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<GpuTimer*>(native));
	}

	[[nodiscard]] Result<void> RecordGpuTimerBegin(void*, void* nativeList, void* nativeTimer)
	{
		Pending& pending = *static_cast<Pending*>(nativeList);
		GpuTimer& timer = *static_cast<GpuTimer*>(nativeTimer);

		if (pending.Queue == EQueue::Copy && timer.CopyFrequency == 0)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Timer", "Copy queue timestamps are unavailable on this device.");
		}
		timer.Frequency = pending.Queue == EQueue::Compute ? timer.ComputeFrequency : pending.Queue == EQueue::Copy ? timer.CopyFrequency : timer.DirectFrequency;
		ID3D12QueryHeap* const queries = pending.Queue == EQueue::Copy ? timer.CopyQueries.Get() : timer.Queries.Get();
		pending.List->EndQuery(queries, D3D12_QUERY_TYPE_TIMESTAMP, 0);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordGpuTimerEnd(void*, void* nativeList, void* nativeTimer)
	{
		Pending& pending = *static_cast<Pending*>(nativeList);
		GpuTimer& timer = *static_cast<GpuTimer*>(nativeTimer);
		ID3D12QueryHeap* const queries = pending.Queue == EQueue::Copy ? timer.CopyQueries.Get() : timer.Queries.Get();
		pending.List->EndQuery(queries, D3D12_QUERY_TYPE_TIMESTAMP, 1);
		pending.List->ResolveQueryData(queries, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, timer.Readback.Get(), 0);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<double> ReadGpuTimer(void*, void* native)
	{
		GpuTimer& timer = *static_cast<GpuTimer*>(native);
		D3D12_RANGE range{ 0, 2 * sizeof(uint64) };
		void* data = nullptr;

		if (FAILED(timer.Readback->Map(0, &range, &data)))
		{
			return Result<double>::Failure(Failure("Rhi.D3D12Timer", "Could not map D3D12 timestamp results."));
		}

		const uint64* timestamps = static_cast<const uint64*>(data);
		const uint64 ticks = timestamps[1] - timestamps[0];
		D3D12_RANGE written{ 0, 0 };
		timer.Readback->Unmap(0, &written);

		return Result<double>::Success(static_cast<double>(ticks) / static_cast<double>(timer.Frequency));
	}
}
#endif
