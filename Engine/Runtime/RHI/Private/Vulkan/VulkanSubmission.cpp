#include "VulkanSubmission.h"
#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	bool ReserveSubmission(State* opaque) noexcept
	{
		State& state = *opaque;
		if (state.PreparedCredits >= NumericLimits<usize>::max() - state.InFlight.Size())
		{
			return false;
		}
		return state.Submissions.TryReserveOne() && state.InFlight.Reserve(state.InFlight.Size() + state.PreparedCredits + 1);
	}

	[[nodiscard]] Result<uint64> SubmitCommandListImpl(
		State* opaque,
		void* native,
		Span<const RecordedCopy> copies,
		Span<const RecordedTextureCopy> textures,
		Span<const RecordedTextureUse> textureUses,
		Span<const RecordedBufferUse> bufferUses,
		Span<const RecordedAccelUse> accelUses,
		Span<const RecordedPipelineUse> pipelineUses,
		Span<const RecordedGraphicsPipelineUse> graphicsPipelineUses,
		Span<const RecordedRayPipelineUse> rayPipelineUses,
		Span<const RecordedFramebufferUse> framebufferUses,
		const NativePreparedSubmissionDesc* preparation,
		uint32* queueWaitCount)
	{
		State& state = *opaque;
		state.bSubmissionMayBePending = false;
		state.LastSubmissionNativeCode = 0;
		state.LastSubmissionFailureStage = 0;
		Pending& pending = *static_cast<Pending*>(native);

		struct ErrorFlag final
		{
			bool bPrevious;

			~ErrorFlag() noexcept { bPreparedErrors = bPrevious; }
		};

		const ErrorFlag errorFlag{ bPreparedErrors };
		bPreparedErrors = pending.bPrepared;
		const EQueue submissionQueue = pending.QueueKind;
		SwapChain* const swapChain = pending.SwapChainOwner;
		const uint32 imageIndex = pending.SwapChainImageIndex;
		const uint32 semaphoreIndex = pending.SwapChainSemaphoreIndex;

		if (!pending.bPrepared && swapChain == nullptr && (state.bSeparateComputeQueue || state.bSeparateCopyQueue))
		{
			uint64 crossQueueSerials[3]{};
			const auto require = [&](uint64 serial, EQueue queue)
				{
					if (serial == 0 || QueueTimeline(state, queue) == QueueTimeline(state, submissionQueue))
					{
						return;
					}

					const uint32 index = static_cast<uint32>(queue);

					if (serial > crossQueueSerials[index])
					{
						crossQueueSerials[index] = serial;
					}
				};
			for (const RecordedCopy& copy : copies)
			{
				const Buffer& source = *static_cast<const Buffer*>(copy.Native.Source);
				const Buffer& destination = *static_cast<const Buffer*>(copy.Native.Destination);
				require(source.LastUse, source.LastQueue);
				require(destination.LastUse, destination.LastQueue);
			}

			for (const RecordedTextureCopy& copy : textures)
			{
				const Texture& source = *static_cast<const Texture*>(copy.Native.Source);
				const Texture& destination = *static_cast<const Texture*>(copy.Native.Destination);
				require(source.LastUse, source.LastQueue);
				require(destination.LastUse, destination.LastQueue);
			}

			for (const RecordedTextureUse& use : textureUses)
			{
				const Texture& texture = *static_cast<const Texture*>(use.Native);
				require(texture.LastUse, texture.LastQueue);
			}

			for (const RecordedBufferUse& use : bufferUses)
			{
				const Buffer& buffer = *static_cast<const Buffer*>(use.Native);
				require(buffer.LastUse, buffer.LastQueue);
			}
			constexpr EQueue QUEUES[3]{ EQueue::Graphics, EQueue::Compute, EQueue::Copy };
			for (EQueue queue : QUEUES)
			{
				const uint64 serial = crossQueueSerials[static_cast<uint32>(queue)];

				if (serial == 0)
				{
					continue;
				}

				Result<void> ordered = QueueWaitForSubmission(&state, submissionQueue, queue, serial);

				if (!ordered)
				{
					DiscardCommandList(&state, &pending);

					return Result<uint64>::Failure(ordered.GetError());
				}
			}
		}

		for (const RecordedCopy& copy : copies)
		{
			const Buffer& destination = *static_cast<const Buffer*>(copy.Native.Destination);

			if (!pending.bPreparedFinished && destination.MemoryAccess == EBufferMemory::CpuRead)
			{
				BufferHostReadBarrier(state, pending, destination.Handle);
			}
		}

		for (const RecordedBufferUse& use : bufferUses)
		{
			const Buffer& buffer = *static_cast<const Buffer*>(use.Native);

			if (!pending.bPreparedFinished && buffer.MemoryAccess == EBufferMemory::CpuRead)
			{
				BufferHostReadBarrier(state, pending, buffer.Handle);
			}
		}

		if (preparation != nullptr && preparation->bExplicitDependencies)
		{
			state.bHasExplicitPreparedWork = true;
		}
		// Singular LastQueue metadata cannot join independent prepared read branches. Keep the
		// ordinary route conservative once explicit queue DAGs have entered this device.
		const bool bIndependent = (!pending.bPrepared && !state.bHasExplicitPreparedWork) || (preparation != nullptr && preparation->bExplicitDependencies);
		const bool bFirstAcquiredUse = swapChain != nullptr && (preparation == nullptr || preparation->bAcquiredFirstUse);
		const bool bAcquiredRelease = swapChain != nullptr && (preparation == nullptr || preparation->bAcquiredRelease);
		const VkSemaphore acquired = bFirstAcquiredUse ? swapChain->AcquireSemaphores[semaphoreIndex] : VK_NULL_HANDLE;
		const VkSemaphore presentReady = bAcquiredRelease ? swapChain->PresentSemaphores[imageIndex] : VK_NULL_HANDLE;
		Result<uint64> submitted =
			Execute(state, Move(pending), acquired, presentReady,
				swapChain != nullptr ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT, bIndependent, preparation,
				queueWaitCount);
		if (pending.bSubmissionCredit)
		{
			--state.PreparedCredits;
			state.Submissions.ReleaseCredit();
			pending.bSubmissionCredit = false;
		}
		DestroyObject(state.Resource, &pending);

		if (swapChain != nullptr && !submitted)
		{
			swapChain->bUsable = false;
		}

		if (submitted)
		{
			if (swapChain != nullptr)
			{
				if (bFirstAcquiredUse)
				{
					swapChain->AcquireSerials[semaphoreIndex] = submitted.Value();
					swapChain->bAcquireConsumed = true;
				}
				swapChain->SubmittedSerial = submitted.Value();
				swapChain->bReady = bAcquiredRelease;
			}

			for (const RecordedCopy& copy : copies)
			{
				static_cast<Buffer*>(copy.Native.Source)->LastUse = submitted.Value();
				static_cast<Buffer*>(copy.Native.Destination)->LastUse = submitted.Value();
				static_cast<Buffer*>(copy.Native.Source)->LastQueue = submissionQueue;
				static_cast<Buffer*>(copy.Native.Destination)->LastQueue = submissionQueue;
			}

			for (const RecordedTextureCopy& copy : textures)
			{
				static_cast<Texture*>(copy.Native.Source)->LastUse = submitted.Value();
				static_cast<Texture*>(copy.Native.Destination)->LastUse = submitted.Value();
				static_cast<Texture*>(copy.Native.Source)->LastQueue = submissionQueue;
				static_cast<Texture*>(copy.Native.Destination)->LastQueue = submissionQueue;
			}

			for (const RecordedTextureUse& use : textureUses)
			{
				static_cast<Texture*>(use.Native)->LastUse = submitted.Value();
				static_cast<Texture*>(use.Native)->LastQueue = submissionQueue;
			}

			for (const RecordedBufferUse& use : bufferUses)
			{
				static_cast<Buffer*>(use.Native)->LastUse = submitted.Value();
				static_cast<Buffer*>(use.Native)->LastQueue = submissionQueue;
			}

			for (const RecordedAccelUse& use : accelUses)
			{
				static_cast<NativeAccelStruct*>(use.Native)->LastUse = submitted.Value();
				static_cast<NativeAccelStruct*>(use.Native)->LastQueue = submissionQueue;
			}

			for (const RecordedPipelineUse& use : pipelineUses)
			{
				static_cast<ComputePipeline*>(use.Native)->LastUse = submitted.Value();
			}

			for (const RecordedGraphicsPipelineUse& use : graphicsPipelineUses)
			{
				static_cast<GraphicsPipeline*>(use.Native)->LastUse = submitted.Value();
			}

			for (const RecordedRayPipelineUse& use : rayPipelineUses)
			{
				static_cast<RayTracingPipeline*>(use.Native)->LastUse = submitted.Value();
			}

			for (const RecordedFramebufferUse& use : framebufferUses)
			{
				static_cast<Framebuffer*>(use.Native)->LastUse = submitted.Value();
			}
		}

		return submitted;
	}

	[[nodiscard]] Result<uint64> SubmitCommandList(
		State* opaque,
		void* native,
		Span<const RecordedCopy> copies,
		Span<const RecordedTextureCopy> textures,
		Span<const RecordedTextureUse> textureUses,
		Span<const RecordedBufferUse> bufferUses,
		Span<const RecordedAccelUse> accelUses,
		Span<const RecordedPipelineUse> pipelineUses,
		Span<const RecordedGraphicsPipelineUse> graphicsPipelineUses,
		Span<const RecordedRayPipelineUse> rayPipelineUses,
		Span<const RecordedFramebufferUse> framebufferUses)
	{
		return SubmitCommandListImpl(opaque, native, copies, textures, textureUses, bufferUses, accelUses, pipelineUses,
			graphicsPipelineUses, rayPipelineUses, framebufferUses);
	}

	[[nodiscard]] NativePreparedSubmissionResult SubmitPreparedCommandList(
		State* opaque,
		void* native,
		const NativePreparedSubmissionDesc& preparation,
		Span<const RecordedTextureUse> textureUses,
		Span<const RecordedBufferUse> bufferUses,
		Span<const RecordedAccelUse> accelUses,
		Span<const RecordedPipelineUse> pipelineUses,
		Span<const RecordedGraphicsPipelineUse> graphicsPipelineUses,
		Span<const RecordedRayPipelineUse> rayPipelineUses,
		Span<const RecordedFramebufferUse> framebufferUses)
	{
		NativePreparedSubmissionResult outcome;
		Result<uint64> submitted =
			SubmitCommandListImpl(opaque, native, {}, {}, textureUses, bufferUses, accelUses, pipelineUses, graphicsPipelineUses,
				rayPipelineUses, framebufferUses, &preparation, &outcome.QueueWaitCount);
		outcome.bAccepted = opaque->bSubmissionMayBePending;
		if (submitted)
		{
			outcome.Serial = submitted.Value();
			outcome.bAccepted = true;
		}
		else
		{
			outcome.ErrorCode = submitted.GetError().Code;
			outcome.NativeCode = opaque->LastSubmissionNativeCode;
			outcome.FailureStage = opaque->LastSubmissionFailureStage;
		}

		return outcome;
	}

	[[nodiscard]] Result<void> WaitNativeFence(State& state, VkFence fence, uint64 timeout)
	{
		const VkResult result = state.F.WaitForFences(state.Device, 1, &fence, VK_TRUE, timeout);

		if (result == VK_TIMEOUT)
		{
			return Result<void>::Failure(EErrorCode::Timeout, "Rhi.Timeout", "The Vulkan native fence wait timed out.");
		}

		if (result == VK_ERROR_DEVICE_LOST)
		{
			state.bLost = true;
			state.bDeviceLossConfirmed = true;

			return Result<void>::Failure(Failure("Rhi.DeviceLost", "Vulkan reported device loss."));
		}

		if (result != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanIdle", "Could not observe the native Vulkan fence."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] VkSemaphore QueueTimeline(const State& state, EQueue queue) noexcept
	{
		return queue == EQueue::Compute ? state.ComputeTimeline : queue == EQueue::Copy ? state.CopyTimeline : state.Timeline;
	}

	[[nodiscard]] VkQueue NativeQueue(const State& state, EQueue queue) noexcept
	{
		return queue == EQueue::Compute ? state.ComputeQueue : queue == EQueue::Copy ? state.CopyQueue : state.Queue;
	}

	[[nodiscard]] VkCommandPool QueuePool(const State& state, EQueue queue) noexcept
	{
		return queue == EQueue::Compute ? state.ComputePool : queue == EQueue::Copy ? state.CopyPool : state.Pool;
	}

	[[nodiscard]] VkQueueFlags QueueFlags(const State& state, EQueue queue) noexcept
	{
		return queue == EQueue::Compute ? state.ComputeQueueFlags : queue == EQueue::Copy ? state.CopyQueueFlags : state.GraphicsQueueFlags;
	}

	[[nodiscard]] uint64 QueueLastSerial(const State& state, EQueue queue) noexcept
	{
		const VkSemaphore timeline = QueueTimeline(state, queue);

		return timeline == state.ComputeTimeline && state.bSeparateComputeQueue ? state.LastComputeSerial
			: timeline == state.CopyTimeline && state.bSeparateCopyQueue ? state.LastCopySerial
			: state.LastGraphicsSerial;
	}

	[[nodiscard]] uint64 GetQueueLastSubmittedSerial(const State* opaque, EQueue queue) noexcept
	{
		return QueueLastSerial(*opaque, queue);
	}

	[[nodiscard]] bool ReadCompletedSerial(const State& state, uint64& completed) noexcept
	{
		uint64 graphics = 0;

		if (state.F.GetSemaphoreCounterValue(state.Device, state.Timeline, &graphics) != VK_SUCCESS)
		{
			return false;
		}

		uint64 compute = graphics;

		if (state.bSeparateComputeQueue)
		{
			if (state.F.GetSemaphoreCounterValue(state.Device, state.ComputeTimeline, &compute) != VK_SUCCESS)
			{
				return false;
			}
		}

		uint64 copy = graphics;

		if (state.bSeparateCopyQueue)
		{
			if (state.F.GetSemaphoreCounterValue(state.Device, state.CopyTimeline, &copy) != VK_SUCCESS)
			{
				return false;
			}
		}

		completed = state.Submissions.CompletedPrefix(graphics, compute, copy);

		return true;
	}

	void Collect(State* opaque) noexcept
	{
		State& state = *opaque;
		uint64 complete = 0;

		if (!ReadCompletedSerial(state, complete))
		{
			return;
		}

		uint64 graphicsCompleted = 0;
		uint64 computeCompleted = 0;

		if (state.F.GetSemaphoreCounterValue(state.Device, state.Timeline, &graphicsCompleted) != VK_SUCCESS)
		{
			return;
		}

		computeCompleted = graphicsCompleted;

		if (state.bSeparateComputeQueue && state.F.GetSemaphoreCounterValue(state.Device, state.ComputeTimeline, &computeCompleted) != VK_SUCCESS)
		{
			return;
		}

		uint64 copyCompleted = graphicsCompleted;

		if (state.bSeparateCopyQueue && state.F.GetSemaphoreCounterValue(state.Device, state.CopyTimeline, &copyCompleted) != VK_SUCCESS)
		{
			return;
		}

		state.Submissions.Advance(graphicsCompleted, computeCompleted, copyCompleted);

		for (usize index = 0; index < state.InFlight.Size();)
		{
			if (state.InFlight[index].Serial > complete)
			{
				++index;
				continue;
			}

			Pending pending = Move(state.InFlight[index]);
			state.InFlight.RemoveAtSwap(index);

			for (uint64 token : pending.AftermathMarkers)
			{
				(void)state.Diagnostics->RetireMarker(token);
			}

			for (Buffer* temporary : pending.Temporary)
			{
				FreeBuffer(state, temporary);
			}

			for (vulkan::Pending::DescriptorPoolRecord const& pool : pending.DescriptorPools)
			{
				state.F.DestroyDescriptorPool(state.Device, pool.Handle, nullptr);
			}

			for (VkFramebuffer framebuffer : pending.Framebuffers)
			{
				state.F.DestroyFramebuffer(state.Device, framebuffer, nullptr);
			}

			if (state.Recycled.Size() == state.Recycled.Capacity() || !state.Recycled.Add({ pending.Commands, pending.Pool, pending.QueueKind }))
			{
				state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);
			}
		}

		for (usize index = 0; index < state.RetiredAccelStructs.Size();)
		{
			if (state.RetiredAccelStructs[index].Serial > complete)
			{
				++index;
				continue;
			}

			DestroyAccelStructNow(&state, state.RetiredAccelStructs[index].Item);
			state.RetiredAccelStructs.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredMicromaps.Size();)
		{
			if (state.RetiredMicromaps[index].Serial > complete)
			{
				++index;
				continue;
			}

			DestroyOpacityMicromapNow(&state, state.RetiredMicromaps[index].Item);
			state.RetiredMicromaps.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredBuffers.Size();)
		{
			if (state.RetiredBuffers[index].Serial > complete)
			{
				++index;
				continue;
			}

			FreeBuffer(state, state.RetiredBuffers[index].Item);
			state.RetiredBuffers.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredFramebuffers.Size();)
		{
			if (state.RetiredFramebuffers[index].Serial > complete)
			{
				++index;
				continue;
			}

			FreeFramebuffer(state, state.RetiredFramebuffers[index].Item);
			state.RetiredFramebuffers.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredTextures.Size();)
		{
			if (state.RetiredTextures[index].Serial > complete)
			{
				++index;
				continue;
			}

			FreeTexture(state, state.RetiredTextures[index].Item);
			state.RetiredTextures.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredPipelines.Size();)
		{
			if (state.RetiredPipelines[index].Serial > complete)
			{
				++index;
				continue;
			}

			FreePipeline(state, state.RetiredPipelines[index].Item);
			state.RetiredPipelines.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredGraphicsPipelines.Size();)
		{
			if (state.RetiredGraphicsPipelines[index].Serial > complete)
			{
				++index;
				continue;
			}

			FreeGraphicsPipeline(state, state.RetiredGraphicsPipelines[index].Item);
			state.RetiredGraphicsPipelines.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredRayPipelines.Size();)
		{
			if (state.RetiredRayPipelines[index].Serial > complete)
			{
				++index;
				continue;
			}

			FreeRayPipeline(state, state.RetiredRayPipelines[index].Item);
			state.RetiredRayPipelines.RemoveAtSwap(index);
		}
	}

	[[nodiscard]] Result<bool> IsComplete(const State* opaque, uint64 serial)
	{
		const State& state = *opaque;

		ASSERT(serial <= state.LastSerial, "The submission token is not known to this device.");

		uint64 completed = 0;

		if (!ReadCompletedSerial(state, completed))
		{
			return Result<bool>::Failure(Failure("Rhi.VulkanTimeline", "Could not inspect Vulkan completion."));
		}

		return Result<bool>::Success(completed >= serial);
	}

	[[nodiscard]] Result<bool> IsQueueComplete(const State* opaque, EQueue queue, uint64 serial)
	{
		const State& state = *opaque;

		ASSERT(serial <= QueueLastSerial(state, queue), "The submission token is not known to this device.");

		uint64 completed = 0;

		if (state.F.GetSemaphoreCounterValue(state.Device, QueueTimeline(state, queue), &completed) != VK_SUCCESS)
		{
			return Result<bool>::Failure(Failure("Rhi.VulkanTimeline", "Could not inspect Vulkan queue completion."));
		}

		return Result<bool>::Success(completed >= serial);
	}

	[[nodiscard]] Result<void> Wait(State* opaque, EQueue queue, uint64 serial, uint32 timeout)
	{
		State& state = *opaque;

		ASSERT(serial <= QueueLastSerial(state, queue), "The submission token is not known to this device.");

		VkSemaphoreWaitInfo info{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
		info.semaphoreCount = 1;
		const VkSemaphore timeline = QueueTimeline(state, queue);
		info.pSemaphores = &timeline;
		info.pValues = &serial;
		const VkResult result = state.F.WaitSemaphores(state.Device, &info, static_cast<uint64>(timeout) * NANOSECONDS_PER_MILLISECOND);

		if (result == VK_TIMEOUT)
		{
			return Result<void>::Failure(EErrorCode::Timeout, "Rhi.Timeout", "The Vulkan copy wait timed out.");
		}

		if (result != VK_SUCCESS)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanWait", "Vulkan copy wait failed."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> QueueWaitForSubmission(State* opaque, EQueue waitingQueue, EQueue executionQueue, uint64 serial)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		ASSERT(serial <= QueueLastSerial(state, executionQueue), "The submission token is not known to this queue.");

		if (QueueTimeline(state, waitingQueue) == QueueTimeline(state, executionQueue))
		{
			return Result<void>::Success();
		}

		const VkSemaphore source = QueueTimeline(state, executionQueue);
		const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkTimelineSemaphoreSubmitInfo timeline{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
		timeline.waitSemaphoreValueCount = 1;
		timeline.pWaitSemaphoreValues = &serial;
		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.pNext = &timeline;
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &source;
		submit.pWaitDstStageMask = &stage;
		const VkQueue queue = NativeQueue(state, waitingQueue);
		InvalidateIdle(state);
		const VkResult orderedResult = state.F.QueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
		state.bDeviceLossConfirmed |= orderedResult == VK_ERROR_DEVICE_LOST;

		if (orderedResult != VK_SUCCESS)
		{
			state.bLost = true;

			return Result<void>::Failure(Failure("Rhi.VulkanWait", "Could not order the Vulkan queues."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> WaitIdleBounded(State* opaque, uint32 timeout)
	{
		State& state = *opaque;
		// Native queue users can submit between RHI calls without invalidating a cached fence.
		if (state.bNativeQueuesExposed)
		{
			InvalidateIdle(state);
		}

		if (state.bDeviceLossConfirmed)
		{
			return Result<void>::Failure(Failure("Rhi.DeviceLost", "Vulkan reported device loss."));
		}

		if (state.bIdleConfirmed)
		{
			return Result<void>::Success();
		}

		const MonotonicClock::time_point start = MonotonicClock::now();
		const uint64 duration = static_cast<uint64>(timeout) * NANOSECONDS_PER_MILLISECOND;
		const auto remaining = [&]()
			{
				const uint64 elapsed = static_cast<uint64>( DurationCast<Nanoseconds>(MonotonicClock::now() - start).count());

				return elapsed < duration ? duration - elapsed : 0;
			};
		constexpr EQueue queues[3]{ EQueue::Graphics, EQueue::Compute, EQueue::Copy };
		if (!state.bIdleIssued)
		{
			for (uint32 index = 0; index < 3; ++index)
			{
				if (state.bIdleFencePending[index])
				{
					Result<void> waited = WaitNativeFence(state, state.IdleFences[index], remaining());

					if (!waited)
					{
						return waited;
					}

					state.bIdleFencePending[index] = false;
				}

				if (!state.IdleFences[index])
				{
					VkFenceCreateInfo create{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
					if (state.F.CreateFence(state.Device, &create, nullptr, &state.IdleFences[index]) != VK_SUCCESS)
					{
						return Result<void>::Failure(Failure("Rhi.VulkanIdle", "Could not create a Vulkan idle fence."));
					}
				}
				else if (state.F.ResetFences(state.Device, 1, &state.IdleFences[index]) != VK_SUCCESS)
				{
					return Result<void>::Failure(Failure("Rhi.VulkanIdle", "Could not reset a Vulkan idle fence."));
				}

				VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
				const VkResult result = state.F.QueueSubmit(NativeQueue(state, queues[index]), 1, &submit, state.IdleFences[index]);

				if (result != VK_SUCCESS)
				{
					state.bDeviceLossConfirmed |= result == VK_ERROR_DEVICE_LOST;

					return Result<void>::Failure(Failure(state.bDeviceLossConfirmed ? "Rhi.DeviceLost" : "Rhi.VulkanIdle", "Could not submit the Vulkan idle fence."));
				}

				state.bIdleFencePending[index] = true;
			}

			state.bIdleIssued = true;
		}

		for (uint32 index = 0; index < 3; ++index)
		{
			Result<void> waited = WaitNativeFence(state, state.IdleFences[index], remaining());

			if (!waited)
			{
				return waited;
			}

			state.bIdleFencePending[index] = false;

			if (state.bSparseFencePending[index])
			{
				waited = WaitNativeFence(state, state.SparseFences[index], remaining());

				if (!waited)
				{
					return waited;
				}

				state.bSparseFencePending[index] = false;
			}
		}

		for (SwapChain* swapChain = state.SwapChains; swapChain != nullptr; swapChain = swapChain->Next)
		{
			for (uint32 index = 0; index < 3; ++index)
			{
				if (!swapChain->bAcquirePending[index])
				{
					continue;
				}

				Result<void> waited = WaitNativeFence(state, swapChain->AcquireFences[index], remaining());

				if (!waited)
				{
					return waited;
				}

				swapChain->bAcquirePending[index] = false;
			}

			if (swapChain->bUnboundedPresent)
			{
				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BoundedPresentation", "Bounded presentation waits require swapchain maintenance1.");
			}

			for (uint32 index = 0; index < swapChain->ImageCount; ++index)
			{
				if (!swapChain->bPresentPending[index])
				{
					continue;
				}

				Result<void> waited = WaitNativeFence(state, swapChain->PresentFences[index], remaining());

				if (!waited)
				{
					return waited;
				}

				swapChain->bPresentPending[index] = false;
			}
		}

		state.bIdleConfirmed = true;
		Collect(&state);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> WaitIdle(State* opaque)
	{
		State& state = *opaque;

		if (state.bNativeQueuesExposed)
		{
			InvalidateIdle(state);
		}

		if (state.bDeviceLossConfirmed)
		{
			return Result<void>::Failure(Failure("Rhi.DeviceLost", "Vulkan reported device loss."));
		}

		if (state.bIdleConfirmed)
		{
			return Result<void>::Success();
		}

		Result<void> bounded = WaitIdleBounded(opaque, NumericLimits<uint32>::max());

		while (!bounded && bounded.GetError().Code == EErrorCode::Timeout)
		{
			bounded = WaitIdleBounded(opaque, NumericLimits<uint32>::max());
		}

		if (bounded || bounded.GetError().Code != EErrorCode::Unsupported)
		{
			return bounded;
		}

		if (!state.Device || !state.F.DeviceWaitIdle)
		{
			return Result<void>::Failure(Failure("Rhi.VulkanIdle", "The Vulkan device is unavailable."));
		}

		const VkResult result = state.F.DeviceWaitIdle(state.Device);

		if (result != VK_SUCCESS)
		{
			state.bDeviceLossConfirmed |= result == VK_ERROR_DEVICE_LOST;

			return Result<void>::Failure(Failure(state.bDeviceLossConfirmed ? "Rhi.DeviceLost" : "Rhi.VulkanIdle", "Could not wait for Vulkan device idle."));
		}

		for (SwapChain* swapChain = state.SwapChains; swapChain != nullptr; swapChain = swapChain->Next)
		{
			for (uint32 index = 0; index < 3; ++index)
			{
				if (!swapChain->bAcquirePending[index])
				{
					continue;
				}

				Result<void> waited = WaitNativeFence(state, swapChain->AcquireFences[index], NumericLimits<uint64>::max());

				if (!waited)
				{
					return waited;
				}

				swapChain->bAcquirePending[index] = false;
			}

			for (uint32 index = 0; index < swapChain->ImageCount; ++index)
			{
				if (!swapChain->bPresentPending[index])
				{
					continue;
				}

				Result<void> waited = WaitNativeFence(state, swapChain->PresentFences[index], NumericLimits<uint64>::max());

				if (!waited)
				{
					return waited;
				}

				swapChain->bPresentPending[index] = false;
			}

			swapChain->bUnboundedPresent = false;
		}

		for (bool& pending : state.bIdleFencePending)
		{
			pending = false;
		}

		for (bool& pending : state.bSparseFencePending)
		{
			pending = false;
		}

		state.bIdleConfirmed = true;

		if (state.Timeline)
		{
			Collect(&state);
		}

		return Result<void>::Success();
	}

	DepthResolve::~DepthResolve()
	{
		if (Framebuffer)
		{
			DestroyFramebuffer(Device, Framebuffer, nullptr);
		}
		if (Pass)
		{
			DestroyPass(Device, Pass, nullptr);
		}
		for (VkImageView view : Views)
		{
			if (view)
			{
				DestroyView(Device, view, nullptr);
			}
		}
	}

	Pending::PreparedBinding::PreparedBinding(MemoryResource& resource) noexcept
		: StorageBuffers(resource)
		, StorageTextures(resource)
		, StorageRanges(resource)
	{
	}

	Pending::PreparedAccelBuild::PreparedAccelBuild(MemoryResource& resource) noexcept
		: Geometries(resource)
		, Ranges(resource)
	{
	}

	Pending::TextureLayoutState::TextureLayoutState(MemoryResource& resource) noexcept
		: SubresourceLayouts(resource)
	{
	}

	Pending::Pending(MemoryResource& resource) noexcept
		: PreparedAccelBuilds(resource)
		, PreparedAccelQueries(resource)
		, PreparedAccelCompacts(resource)
		, PreparedBindings(resource)
		, DepthResolves(resource)
		, PreparedHostReads(resource)
		, PreparedTextures(resource)
		, Temporary(resource)
		, TextureLayouts(resource)
		, GraphicsStorageBuffers(resource)
		, GraphicsStorageImages(resource)
		, BufferUavPolicies(resource)
		, TextureUavPolicies(resource)
		, DescriptorPools(resource)
		, DescriptorOwners(resource)
		, Framebuffers(resource)
		, AftermathMarkers(resource)
	{
	}

	PreparedAccelQueryOwner::~PreparedAccelQueryOwner() noexcept
	{
		if (Pool != VK_NULL_HANDLE)
		{
			Device->F.DestroyQueryPool(Device->Device, Pool, nullptr);
		}
	}

	PreparedRayTableOwner::~PreparedRayTableOwner() noexcept
	{
		if (Resource != nullptr)
		{
			FreeBuffer(*Device, Resource);
		}
	}
}
