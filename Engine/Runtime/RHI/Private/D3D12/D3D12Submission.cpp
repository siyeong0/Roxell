#include "D3D12Submission.h"
#include "D3D12Backend.h"
#include "D3D12Descriptors.h"
#ifdef _WIN32
namespace rox::d3d12
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

	[[nodiscard]] ID3D12Fence* QueueFence(const State& state, EQueue queue) noexcept
	{
		return queue == EQueue::Compute ? state.ComputeFence.Get() : queue == EQueue::Copy ? state.CopyFence.Get() : state.Fence.Get();
	}

	[[nodiscard]] ID3D12CommandQueue* NativeQueue(const State& state, EQueue queue) noexcept
	{
		return queue == EQueue::Compute ? state.ComputeQueue.Get() : queue == EQueue::Copy ? state.CopyQueue.Get() : state.Queue.Get();
	}

	[[nodiscard]] uint64 QueueLastSerial(const State& state, EQueue queue) noexcept
	{
		return queue == EQueue::Compute ? state.LastComputeSerial : queue == EQueue::Copy ? state.LastCopySerial : state.LastDirectSerial;
	}

	[[nodiscard]] uint64 CompletedSerial(const State& state) noexcept
	{
		const uint64 direct = state.Fence->GetCompletedValue();
		const uint64 compute = state.ComputeFence->GetCompletedValue();
		const uint64 copy = state.CopyFence->GetCompletedValue();

		if (direct == NumericLimits<uint64>::max() || compute == NumericLimits<uint64>::max() || copy == NumericLimits<uint64>::max())
		{
			return NumericLimits<uint64>::max();
		}

		return state.Submissions.CompletedPrefix(direct, compute, copy);
	}

	[[nodiscard]] Result<bool> IsComplete(const State* opaque, uint64 serial)
	{
		const State& state = *opaque;

		ASSERT(serial <= state.LastSerial, "The submission token is not known to this device.");

		const uint64 completed = CompletedSerial(state);

		if (completed == NumericLimits<uint64>::max())
		{
			return Result<bool>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 device was removed."));
		}

		return Result<bool>::Success(completed >= serial);
	}

	[[nodiscard]] Result<bool> IsQueueComplete(const State* opaque, EQueue queue, uint64 serial)
	{
		const State& state = *opaque;

		ASSERT(serial <= QueueLastSerial(state, queue), "The submission token is not known to this device.");

		const uint64 completed = QueueFence(state, queue)->GetCompletedValue();

		if (completed == NumericLimits<uint64>::max())
		{
			return Result<bool>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 device was removed."));
		}

		return Result<bool>::Success(completed >= serial);
	}

	[[nodiscard]] Result<void> Wait(State* opaque, EQueue queue, uint64 serial, uint32 timeout)
	{
		State& state = *opaque;
		Result<bool> complete = IsQueueComplete(&state, queue, serial);

		if (!complete)
		{
			return Result<void>::Failure(complete.GetError());
		}

		if (complete.Value())
		{
			return Result<void>::Success();
		}

		if (FAILED(QueueFence(state, queue)->SetEventOnCompletion(serial, state.Event)))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Wait", "Could not register a D3D12 fence wait."));
		}

		const ULONGLONG started = GetTickCount64();
		const DWORD duration = timeout == INFINITE ? INFINITE - 1 : timeout;

		for (;;)
		{
			const ULONGLONG elapsed = GetTickCount64() - started;
			const DWORD remaining = elapsed < duration ? duration - static_cast<DWORD>(elapsed) : 0;
			const DWORD result = WaitForSingleObject(state.Event, remaining); complete = IsQueueComplete(&state, queue, serial);

			if (!complete)
			{
				return Result<void>::Failure(complete.GetError());
			}

			if (complete.Value())
			{
				break;
			}

			if (result == WAIT_TIMEOUT || GetTickCount64() - started >= duration)
			{
				return Result<void>::Failure(EErrorCode::Timeout, "Rhi.Timeout", "The D3D12 fence wait timed out.");
			}

			if (result != WAIT_OBJECT_0)
			{
				return Result<void>::Failure(Failure("Rhi.D3D12Wait", "The D3D12 fence wait failed."));
			}
		}
		Collect(&state);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> QueueWaitForSubmission(State* opaque, EQueue waitingQueue, EQueue executionQueue, uint64 serial)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}

		ASSERT(serial <= QueueLastSerial(state, executionQueue), "The submission token is not known to this queue.");

		if (QueueFence(state, waitingQueue) == QueueFence(state, executionQueue))
		{
			return Result<void>::Success();
		}
		ID3D12CommandQueue* const queue = NativeQueue(state, waitingQueue);
		InvalidateIdle(state);

		if (FAILED(queue->Wait(QueueFence(state, executionQueue), serial)))
		{
			state.bLost = true;

			return Result<void>::Failure(Failure("Rhi.D3D12Wait", "Could not order the D3D12 queues."));
		}

		return Result<void>::Success();
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
		const EQueue submissionQueue = pending.Queue;
		uint64 crossQueueSerials[3]{};
		bool bUnknownAliases = pending.ExternalFence != nullptr;
		bool bUnknownBufferAliases = pending.ExternalFence != nullptr && !pending.bExternalTexture;
		const auto require = [&](uint64 serial, EQueue queue)
			{
				if (serial == 0 || queue == submissionQueue)
				{
					return;
				}

				uint64& required = crossQueueSerials[static_cast<uint8>(queue)];

				if (serial > required)
				{
					required = serial;
				}
			};
		const auto requireResource = [&](const auto& resource)
			{
				bUnknownAliases = bUnknownAliases || resource.bUnknownAliases;
				for (uint32 index = 0; index < 3; ++index)
				{
					require(resource.QueueUses[index], static_cast<EQueue>(index));
				}
			};
		const auto requireBuffer = [&](const Buffer& buffer)
			{
				bUnknownBufferAliases |= buffer.bUnknownAliases;
				requireResource(buffer);
			};
		for (uint32 index = 0; index < 3; ++index)
		{
			require(state.UnknownResourceUses[index], static_cast<EQueue>(index));
		}
		for (const RecordedAccelUse& use : accelUses)
		{
			requireBuffer(*static_cast<const NativeAccelStruct*>(use.Native)->StorageBuffer);
		}
		for (const RecordedCopy& copy : copies)
		{
			const Buffer& source = *static_cast<const Buffer*>(copy.Native.Source);
			const Buffer& destination = *static_cast<const Buffer*>(copy.Native.Destination);
			requireBuffer(source);
			requireBuffer(destination);
		}

		for (const RecordedTextureCopy& copy : textures)
		{
			const Texture& source = *static_cast<const Texture*>(copy.Native.Source);
			const Texture& destination = *static_cast<const Texture*>(copy.Native.Destination);
			requireResource(source);
			requireResource(destination);
		}

		for (const RecordedTextureUse& use : textureUses)
		{
			const Texture& texture = *static_cast<const Texture*>(use.Native);
			requireResource(texture);
		}

		for (const RecordedBufferUse& use : bufferUses)
		{
			const Buffer& buffer = *static_cast<const Buffer*>(use.Native);
			requireBuffer(buffer);
		}

		const EQueue queues[3]{ EQueue::Graphics, EQueue::Compute, EQueue::Copy };
		for (EQueue sourceQueue : queues)
		{
			const uint64 serial = crossQueueSerials[static_cast<uint8>(sourceQueue)];

			if (serial == 0 || pending.bPrepared)
			{
				continue;
			}

			Result<void> ordered = QueueWaitForSubmission(&state, submissionQueue, sourceQueue, serial);

			if (!ordered)
			{
				ReleasePendingGlobalDescriptors(state, pending);
				DestroyObject(state.Resource, &pending);

				return Result<uint64>::Failure(ordered.GetError());
			}
		}

		// Ordinary work joins every native resource's queue frontier. Unknown aliases retain
		// the device-wide barrier; prepared work keeps its validated explicit dependency graph.
		const bool bIndependent = (!pending.bPrepared && !bUnknownAliases) || (preparation != nullptr && preparation->bExplicitDependencies);
		Result<uint64> submitted = Execute(state, Move(pending), bIndependent, preparation, queueWaitCount);

		if (!submitted)
		{
			ReleasePendingGlobalDescriptors(state, pending);
		}

		if (pending.bSubmissionCredit)
		{
			--state.PreparedCredits;
			state.Submissions.ReleaseCredit();
			pending.bSubmissionCredit = false;
		}
		DestroyObject(state.Resource, &pending);

		if (submitted)
		{
			if (bUnknownBufferAliases)
			{
				state.UnknownBufferUses[static_cast<uint32>(submissionQueue)] = submitted.Value();
			}
			if (bUnknownAliases)
			{
				state.UnknownResourceUses[static_cast<uint32>(submissionQueue)] = submitted.Value();
			}
			for (const RecordedCopy& copy : copies)
			{
				RecordResourceUse(*static_cast<Buffer*>(copy.Native.Source), submissionQueue, submitted.Value());
				RecordResourceUse(*static_cast<Buffer*>(copy.Native.Destination), submissionQueue, submitted.Value());
			}
			for (const RecordedTextureCopy& copy : textures)
			{
				RecordResourceUse(*static_cast<Texture*>(copy.Native.Source), submissionQueue, submitted.Value());
				RecordResourceUse(*static_cast<Texture*>(copy.Native.Destination), submissionQueue, submitted.Value());
			}
			for (const RecordedTextureUse& use : textureUses)
			{
				RecordResourceUse(*static_cast<Texture*>(use.Native), submissionQueue, submitted.Value());
			}
			for (const RecordedBufferUse& use : bufferUses)
			{
				RecordResourceUse(*static_cast<Buffer*>(use.Native), submissionQueue, submitted.Value());
			}
			for (const RecordedAccelUse& use : accelUses)
			{
				RecordResourceUse(*static_cast<NativeAccelStruct*>(use.Native)->StorageBuffer, submissionQueue, submitted.Value());
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

	void Collect(State* opaque) noexcept
	{
		State& state = *opaque;
		const uint64 complete = CompletedSerial(state);
		state.Submissions.Advance(state.Fence->GetCompletedValue(), state.ComputeFence->GetCompletedValue(),
			state.CopyFence->GetCompletedValue());

		for (usize index = 0; index < state.InFlight.Size();)
		{
			if (state.InFlight[index].Serial > complete)
			{
				++index;
				continue;
			}
			Pending pending = Move(state.InFlight[index]);
			state.InFlight.RemoveAtSwap(index);
			RetireAftermathMarkers(pending);
			ReleasePendingGlobalDescriptors(state, pending);
			pending.PreparedBindings.Clear();
			pending.Temporary.Clear();
			pending.BindlessHeaps.Clear();
			pending.AttachmentHeaps.Clear();
			Array<Pending>& recycled = pending.Queue == EQueue::Compute ? state.RecycledCompute : pending.Queue == EQueue::Copy ? state.RecycledCopy : state.RecycledDirect;

			if (recycled.Size() < recycled.Capacity())
			{
				const bool bRecycled = recycled.Add(Move(pending));
				ASSERT(bRecycled, "Completed D3D12 commands reuse preallocated owner storage.");
			}
		}

		for (usize index = 0; index < state.RetiredBuffers.Size();)
		{
			if (state.RetiredBuffers[index].Serial > complete)
			{
				++index;
				continue;
			}

			DestroyObject(state.Resource, state.RetiredBuffers[index].Item);
			state.RetiredBuffers.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredTextures.Size();)
		{
			if (state.RetiredTextures[index].Serial > complete)
			{
				++index;
				continue;
			}

			DestroyObject(state.Resource, state.RetiredTextures[index].Item);
			state.RetiredTextures.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredPipelines.Size();)
		{
			if (state.RetiredPipelines[index].Serial > complete)
			{
				++index;
				continue;
			}

			DestroyObject(state.Resource, state.RetiredPipelines[index].Item);
			state.RetiredPipelines.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredGraphicsPipelines.Size();)
		{
			if (state.RetiredGraphicsPipelines[index].Serial > complete)
			{
				++index;
				continue;
			}

			DestroyObject(state.Resource, state.RetiredGraphicsPipelines[index].Item);
			state.RetiredGraphicsPipelines.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredRayPipelines.Size();)
		{
			if (state.RetiredRayPipelines[index].Serial > complete)
			{
				++index;
				continue;
			}

			DestroyObject(state.Resource, state.RetiredRayPipelines[index].Item);
			state.RetiredRayPipelines.RemoveAtSwap(index);
		}

		for (usize index = 0; index < state.RetiredFramebuffers.Size();)
		{
			if (state.RetiredFramebuffers[index].Serial > complete)
			{
				++index;
				continue;
			}

			DestroyObject(state.Resource, state.RetiredFramebuffers[index].Item);
			state.RetiredFramebuffers.RemoveAtSwap(index);
		}
	}

	[[nodiscard]] uint64 GetQueueLastSubmittedSerial(const State* opaque, EQueue queue) noexcept
	{
		return QueueLastSerial(*opaque, queue);
	}

	[[nodiscard]] Result<void> WaitIdleBounded(State* opaque, uint32 timeout)
	{
		State& state = *opaque;
		// Native queue users can submit between RHI calls without invalidating a cached fence.
		if (state.bNativeQueuesExposed)
		{
			InvalidateIdle(state);
		}

		if (!state.Device || !state.Event)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Idle", "The D3D12 device is unavailable."));
		}

		if (FAILED(state.Device->GetDeviceRemovedReason()))
		{
			state.bLost = true;

			return Result<void>::Failure(Failure("Rhi.DeviceLost", "The D3D12 device was removed."));
		}

		if (state.bIdleConfirmed)
		{
			return Result<void>::Success();
		}

		const ULONGLONG started = GetTickCount64();
		const DWORD duration = timeout == INFINITE ? INFINITE - 1 : timeout;
		constexpr EQueue queues[3]{ EQueue::Graphics, EQueue::Compute, EQueue::Copy };
		if (!state.bIdleIssued)
		{
			if (state.IdleFenceValue == NumericLimits<uint64>::max())
			{
				return Result<void>::Failure(Failure("Rhi.D3D12Idle", "The idle fence sequence is exhausted."));
			}
			++state.IdleFenceValue;

			for (uint32 index = 0; index < 3; ++index)
			{
				if (!state.IdleFences[index] && FAILED(state.Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(state.IdleFences[index].GetAddressOf()))))
				{
					return Result<void>::Failure(Failure("Rhi.D3D12Idle", "Could not create an idle fence."));
				}

				if (FAILED(NativeQueue(state, queues[index])->Signal(state.IdleFences[index].Get(), state.IdleFenceValue)))
				{
					state.bLost = true;

					return Result<void>::Failure(Failure(FAILED(state.Device->GetDeviceRemovedReason()) ? "Rhi.DeviceLost" : "Rhi.D3D12Idle", "Could not publish a D3D12 idle fence."));
				}
			}

			state.bIdleIssued = true;
		}

		for (uint32 index = 0; index < 3; ++index)
		{
			ID3D12Fence* fence = state.IdleFences[index].Get();
			uint64 completed = fence->GetCompletedValue();

			if (completed == NumericLimits<uint64>::max())
			{
				state.bLost = true;

				return Result<void>::Failure(Failure("Rhi.DeviceLost", "The D3D12 idle fence observed removal."));
			}

			if (completed >= state.IdleFenceValue)
			{
				continue;
			}

			if (FAILED(fence->SetEventOnCompletion(state.IdleFenceValue, state.Event)))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12Idle", "Could not observe a D3D12 idle fence."));
			}

			for (;;)
			{
				const ULONGLONG elapsed = GetTickCount64() - started;
				const DWORD remaining = elapsed < duration ? duration - static_cast<DWORD>(elapsed) : 0;
				const DWORD waited = WaitForSingleObject(state.Event, remaining);
				completed = fence->GetCompletedValue();

				if (completed == NumericLimits<uint64>::max())
				{
					state.bLost = true;

					return Result<void>::Failure(Failure("Rhi.DeviceLost", "The D3D12 idle fence observed removal."));
				}

				if (completed >= state.IdleFenceValue)
				{
					break;
				}

				if (waited == WAIT_TIMEOUT || GetTickCount64() - started >= duration)
				{
					return Result<void>::Failure(EErrorCode::Timeout, "Rhi.Timeout", "The D3D12 idle wait timed out.");
				}

				if (waited != WAIT_OBJECT_0)
				{
					return Result<void>::Failure(Failure("Rhi.D3D12Idle", "The D3D12 idle event wait failed."));
				}
			}
		}

		state.bIdleConfirmed = true;
		Collect(&state);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> WaitIdle(State* opaque)
	{
		Result<void> waited = WaitIdleBounded(opaque, NumericLimits<uint32>::max());

		while (!waited && waited.GetError().Code == EErrorCode::Timeout)
		{
			waited = WaitIdleBounded(opaque, NumericLimits<uint32>::max());
		}

		return waited;
	}

	AftermathPendingContext::AftermathPendingContext(AftermathPendingContext&& other) noexcept
		: Diagnostics(other.Diagnostics)
		, Native(other.Native)
	{
		other.Diagnostics = nullptr;
		other.Native = nullptr;
	}

	AftermathPendingContext::~AftermathPendingContext() noexcept
	{
		ReleaseAftermathD3D12Context(Diagnostics, Native);
	}

	PreparedRayTableOwner::PreparedRayTableOwner(MemoryResource& memory) noexcept
		: DescriptorRanges(memory)
		, Resources(memory)
		, StorageResources(memory)
		, Accesses(memory)
	{
	}

	Pending::PreparedBinding::PreparedBinding(MemoryResource& resource) noexcept
		: Storage(resource)
	{
	}

	Pending::PreparedAccelBuild::PreparedAccelBuild(MemoryResource& resource) noexcept
		: Geometries(resource)
	{
	}

	void Pending::InvalidatePreparedGraphics() noexcept
	{
		LastPreparedGraphicsPipeline = nullptr;
		LastPreparedGraphicsBinding = UINT32_MAX;
		bPreparedIndexBound = false;
	}

	Pending::Pending(MemoryResource& resource) noexcept
		: PreparedAccelBuilds(resource)
		, PreparedAccelQueries(resource)
		, PreparedAccelCompacts(resource)
		, PreparedBindings(resource)
		, PreparedIndirects(resource)
		, IntegerClears(resource)
		, Temporary(resource)
		, TextureHeaps(resource)
		, SamplerHeaps(resource)
		, BindlessHeaps(resource)
		, GlobalDescriptorRanges(resource)
		, AttachmentHeaps(resource)
		, ResourceStates(resource)
		, ResourceStateLookup(resource)
		, UavPolicies(resource)
		, AftermathMarkers(resource)
	{
	}

	AftermathPendingContext& AftermathPendingContext::operator=(AftermathPendingContext&& other) noexcept
	{
		if (this != &other)
		{
			ReleaseAftermathD3D12Context(Diagnostics, Native);
			Diagnostics = other.Diagnostics;
			Native = other.Native;
			other.Diagnostics = nullptr;
			other.Native = nullptr;
		}

		return *this;
	}
}
#endif
