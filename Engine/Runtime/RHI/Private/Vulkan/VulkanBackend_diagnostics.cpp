#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<void> RecordBeginMarker(State* opaque, void* native, const char* name)
	{
		State& state = *opaque;

		if (!state.F.CmdBeginDebugUtilsLabel && !state.F.CmdSetCheckpoint)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanMarker", "Vulkan debug utils are unavailable."));
		}

		Pending& pending = *static_cast<Pending*>(native);

		if (state.F.CmdSetCheckpoint)
		{
			Result<uint64> marker = state.Diagnostics->RegisterMarker(StringView(name, CStringLength(name)));

			if (!marker)
			{
				return Result<void>::Failure(marker.GetError());
			}

			if (!pending.AftermathMarkers.Add(marker.Value()))
			{
				(void)state.Diagnostics->RetireMarker(marker.Value());

				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain Vulkan checkpoint marker."));
			}
			state.F.CmdSetCheckpoint(pending.Commands, reinterpret_cast<const void*>(static_cast<uintptr>(marker.Value())));
		}

		if (state.F.CmdBeginDebugUtilsLabel)
		{
			VkDebugUtilsLabelEXT label{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
			label.pLabelName = name;
			state.F.CmdBeginDebugUtilsLabel(pending.Commands, &label);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordEndMarker(State* opaque, void* native)
	{
		State& state = *opaque;

		if (!state.F.CmdEndDebugUtilsLabel && !state.F.CmdSetCheckpoint)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanMarker", "Vulkan debug utils are unavailable."));
		}

		Pending& pending = *static_cast<Pending*>(native);

		if (state.F.CmdEndDebugUtilsLabel)
		{
			state.F.CmdEndDebugUtilsLabel(pending.Commands);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> CreateGpuTimer(State* opaque)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		if (state.TimestampValidBits == 0 || state.DeviceProperties.limits.timestampPeriod <= 0.0f)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTimer", "The graphics queue does not support timestamps.");
		}
		GpuTimer* timer = CreateObject<GpuTimer>(state.Resource);

		if (timer == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan timer state."));
		}
		VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
		info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = 2;

		if (state.F.CreateQueryPool(state.Device, &info, nullptr, &timer->Queries) != VK_SUCCESS)
		{
			DestroyObject(state.Resource, timer);

			return Result<void*>::Failure(Failure("Rhi.VulkanTimer", "Could not create a Vulkan timestamp query pool."));
		}

		return Result<void*>::Success(timer);
	}

	void DestroyGpuTimer(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		GpuTimer* timer = static_cast<GpuTimer*>(native);

		if (timer->Queries)
		{
			state.F.DestroyQueryPool(state.Device, timer->Queries, nullptr);
		}

		DestroyObject(state.Resource, timer);
	}

	[[nodiscard]] Result<void> RecordGpuTimerBegin(State* opaque, void* nativeList, void* nativeTimer)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(nativeList);
		GpuTimer& timer = *static_cast<GpuTimer*>(nativeTimer);
		const uint32 validBits = pending.QueueKind == EQueue::Compute ? state.ComputeTimestampValidBits
			: pending.QueueKind == EQueue::Copy ? state.CopyTimestampValidBits
			: state.TimestampValidBits;

		if (validBits == 0)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTimer", "The command queue does not support timestamps.");
		}

		const VkQueueFlags queueFlags = QueueFlags(state, pending.QueueKind);

		if ((queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0)
		{
			state.F.CmdResetQueryPool(pending.Commands, timer.Queries, 0, 2);
		}
		else
		{
			if (!state.bHostQueryReset)
			{
				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanTimer", "The copy queue requires host query reset for timestamps.");
			}
			state.F.ResetQueryPool(state.Device, timer.Queries, 0, 2);
		}
		timer.ValidBits = validBits;
		state.F.CmdWriteTimestamp(pending.Commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timer.Queries, 0);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordGpuTimerEnd(State* opaque, void* nativeList, void* nativeTimer)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(nativeList);
		GpuTimer& timer = *static_cast<GpuTimer*>(nativeTimer);
		state.F.CmdWriteTimestamp(pending.Commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timer.Queries, 1);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<double> ReadGpuTimer(State* opaque, void* native)
	{
		State& state = *opaque;
		GpuTimer& timer = *static_cast<GpuTimer*>(native);
		uint64 timestamps[2]{};
		if (state.F.GetQueryPoolResults(state.Device, timer.Queries, 0, 2, sizeof(timestamps), timestamps, sizeof(uint64),
			VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
		{
			return Result<double>::Failure(Failure("Rhi.VulkanTimer", "Could not read Vulkan timestamp results."));
		}

		const uint32 validBits = timer.ValidBits;
		const uint64 mask = validBits >= 64 ? UINT64_MAX : (1ull << validBits) - 1;
		const uint64 ticks = (timestamps[1] - timestamps[0]) & mask;

		return Result<double>::Success(static_cast<double>(ticks) *
			static_cast<double>(state.DeviceProperties.limits.timestampPeriod) * 1e-9);
	}
}
