#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	thread_local bool bReturnNotReadyOnNextAcquire = false;
#endif
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	void ReturnNotReadyOnNextVulkanAcquireForTesting() noexcept
	{
		bReturnNotReadyOnNextAcquire = true;
	}
#endif
	void FillSpecializations(Span<const ShaderSpecialization> source, VkSpecializationMapEntry* entries, uint32* data, VkSpecializationInfo& info) noexcept
	{
		for (usize index = 0; index < source.Size(); ++index)
		{
			entries[index] = { source[index].ConstantID, static_cast<uint32>(index * sizeof(uint32)), sizeof(uint32) };
			data[index] = source[index].Value;
		}

		info.mapEntryCount = static_cast<uint32>(source.Size());
		info.pMapEntries = entries;
		info.dataSize = source.Size() * sizeof(uint32);
		info.pData = data;
	}

	[[nodiscard]] bool SupportsNonzeroMipStencilViews(const State& state) noexcept
	{
		const VkPhysicalDeviceProperties& properties = state.DeviceProperties;
		// This exact driver/device tuple ignores the stencil aspect's selected base mip.
		return properties.vendorID != 0x10deu || properties.deviceID != 0x2f04u || properties.driverVersion != 2588114944u;
	}

	void InvalidateIdle(State& state) noexcept
	{
		state.bIdleIssued = false;
		state.bIdleConfirmed = false;
	}

	bool WasSubmissionAccepted(const State* opaque) noexcept
	{
		return opaque->bSubmissionMayBePending;
	}

	bool HasRetirementProof(const State* opaque) noexcept
	{
		const State& state = *opaque;
		return state.bIdleConfirmed || state.bDeviceLossConfirmed;
	}

	uint64 GetPresentCopySerial(const State* opaque) noexcept
	{
		return opaque->PresentCopySerial;
	}

	void ExposeNativeQueues(State* opaque) noexcept
	{
		State& state = *opaque;
		state.bNativeQueuesExposed = true;
		InvalidateIdle(state);
	}

	void FreeRayPipeline(State& state, RayTracingPipeline* pipeline) noexcept
	{
		if (pipeline == nullptr)
		{
			return;
		}

		if (pipeline->Handle)
		{
			state.F.DestroyPipeline(state.Device, pipeline->Handle, nullptr);
		}

		if (pipeline->Layout)
		{
			state.F.DestroyPipelineLayout(state.Device, pipeline->Layout, nullptr);
		}

		for (VkDescriptorSetLayout layout : pipeline->SetLayouts)
		{
			if (layout)
			{
				state.F.DestroyDescriptorSetLayout(state.Device, layout, nullptr);
			}
		}

		for (const VkSampler(&layoutSamplers)[8] : pipeline->Samplers)
		{
			for (VkSampler sampler : layoutSamplers)
			{
				if (sampler)
				{
					state.F.DestroySampler(state.Device, sampler, nullptr);
				}
			}
		}

		for (const VkSampler(&groupSamplers)[8] : pipeline->LocalSamplers)
		{
			for (VkSampler sampler : groupSamplers)
			{
				if (sampler)
				{
					state.F.DestroySampler(state.Device, sampler, nullptr);
				}
			}
		}

		DestroyObject(state.Resource, pipeline);
	}

	thread_local bool bPreparedErrors = false;

	PreparedErrorScope::PreparedErrorScope() noexcept
		: bPrevious(bPreparedErrors)
	{
		bPreparedErrors = true;
	}

	PreparedErrorScope::~PreparedErrorScope() noexcept
	{
		bPreparedErrors = bPrevious;
	}

	[[nodiscard]] Error Failure(const char* id, const char* message) noexcept
	{
		if (bPreparedErrors)
		{
			Error error;
			error.Code = EErrorCode::ExternalFailure;

			return error;
		}

		return MakeError(EErrorCode::ExternalFailure, StringView(id, CStringLength(id)),
			StringView(message, CStringLength(message)));
	}

	[[nodiscard]] Error NativeError(EErrorCode code, const char* id, const char* message) noexcept
	{
		if (bPreparedErrors)
		{
			Error error;
			error.Code = code;

			return error;
		}

		return MakeError(code, StringView(id, CStringLength(id)),
			StringView(message, CStringLength(message)));
	}

	void RecordBarriers(
		State& state,
		Pending& pending,
		VkPipelineStageFlags sourceStage,
		VkPipelineStageFlags destinationStage,
		VkDependencyFlags flags,
		uint32 memoryCount,
		const VkMemoryBarrier* memory,
		uint32 bufferCount,
		const VkBufferMemoryBarrier* buffers,
		uint32 imageCount,
		const VkImageMemoryBarrier* images) noexcept
	{
		++pending.Statistics.BarrierCalls;
		pending.Statistics.BarrierCount += static_cast<uint64>(memoryCount) + bufferCount + imageCount;
		state.F.CmdPipelineBarrier(pending.Commands, sourceStage, destinationStage, flags, memoryCount, memory, bufferCount, buffers,
			imageCount, images);
	}

	void WriteCommandDescriptors(State& state, Pending& pending, uint32 writeCount, const VkWriteDescriptorSet* writes, uint32 copyCount, const VkCopyDescriptorSet* copies) noexcept
	{
		++pending.Statistics.DescriptorWriteCalls;

		for (uint32 index = 0; index < writeCount; ++index)
		{
			pending.Statistics.DescriptorCount += writes[index].descriptorCount;
		}

		for (uint32 index = 0; index < copyCount; ++index)
		{
			pending.Statistics.DescriptorCount += copies[index].descriptorCount;
		}

		state.F.UpdateDescriptorSets(state.Device, writeCount, writes, copyCount, copies);
	}

	[[nodiscard]] VkAccelerationStructureKHR GetDescriptorAccelerationStructure(const void* native) noexcept
	{
		return static_cast<const NativeAccelStruct*>(native)->Handle;
	}

	[[nodiscard]] uint32 SelectMemoryType(const State& state, uint32 memoryTypeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, bool bAllowFallback) noexcept
	{
		uint32 selected = UINT32_MAX;

		for (uint32 index = 0; index < state.MemoryProperties.memoryTypeCount; ++index)
		{
			const VkMemoryPropertyFlags flags = state.MemoryProperties.memoryTypes[index].propertyFlags;

			if ((memoryTypeBits & (1u << index)) == 0 || (flags & required) != required)
			{
				continue;
			}

			if (selected == UINT32_MAX || (flags & preferred) == preferred)
			{
				selected = index;
			}

			if ((flags & preferred) == preferred)
			{
				return selected;
			}
		}

		if (selected == UINT32_MAX && bAllowFallback)
		{
			for (uint32 index = 0; index < state.MemoryProperties.memoryTypeCount; ++index)
			{
				if ((memoryTypeBits & (1u << index)) != 0)
				{
					return index;
				}
			}
		}

		return selected;
	}

	[[nodiscard]] uint32 MemoryBudgetIndex(const State& state, uint32 memoryType) noexcept
	{
		return memoryType == UINT32_MAX ? UINT32_MAX : state.MemoryProperties.memoryTypes[memoryType].heapIndex;
	}

	void FreeFramebuffer(State& state, Framebuffer* framebuffer) noexcept
	{
		if (framebuffer == nullptr)
		{
			return;
		}

		if (framebuffer->Handle)
		{
			state.F.DestroyFramebuffer(state.Device, framebuffer->Handle, nullptr);
		}

		for (VkImageView view : framebuffer->AttachmentViews)
		{
			if (view)
			{
				state.F.DestroyImageView(state.Device, view, nullptr);
			}
		}

		if (framebuffer->bOwnRenderPass && framebuffer->RenderPass)
		{
			state.F.DestroyRenderPass(state.Device, framebuffer->RenderPass, nullptr);
		}

		if (framebuffer->bOwnRenderPass && framebuffer->DepthLoadRenderPass)
		{
			state.F.DestroyRenderPass(state.Device, framebuffer->DepthLoadRenderPass, nullptr);
		}

		if (framebuffer->DepthReadOnlyRenderPass)
		{
			state.F.DestroyRenderPass(state.Device, framebuffer->DepthReadOnlyRenderPass, nullptr);
		}

		for (VkRenderPass pass : framebuffer->MixedDepthStencilRenderPasses)
		{
			if (pass != VK_NULL_HANDLE)
			{
				state.F.DestroyRenderPass(state.Device, pass, nullptr);
			}
		}
		DestroyObject(state.Resource, framebuffer);
	}

	[[nodiscard]] Result<Pending> Begin(State& state, EQueue queue)
	{
		Collect(&state);
		// Reserve optional reuse storage while starting work; completion never allocates.
		usize recycleCapacity = 0;

		if (AddMemorySize(state.Recycled.Size(), state.InFlight.Size(), &recycleCapacity) && AddMemorySize(recycleCapacity, 1, &recycleCapacity))
		{
			(void)state.Recycled.Reserve(recycleCapacity);
		}

		Pending pending(state.Resource);
		pending.QueueKind = queue;

		for (usize index = 0; index < state.Recycled.Size(); ++index)
		{
			if (state.Recycled[index].Queue != queue)
			{
				continue;
			}

			pending.Commands = state.Recycled[index].Commands;
			pending.Pool = state.Recycled[index].Pool;
			state.Recycled.RemoveAtSwap(index);
			break;
		}

		if (pending.Commands != VK_NULL_HANDLE)
		{
			if (state.F.ResetCommandBuffer(pending.Commands, 0) != VK_SUCCESS)
			{
				state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);

				return Result<Pending>::Failure(Failure("Rhi.VulkanCommands", "Could not reset Vulkan commands."));
			}
		}
		else
		{
			// Vulkan externally synchronizes recording through the command pool.
			// A pool belongs to one list and is recycled only after that submission completes.
			VkCommandPoolCreateInfo pool{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			pool.queueFamilyIndex = queue == EQueue::Compute ? state.ComputeQueueFamily : queue == EQueue::Copy ? state.CopyQueueFamily : state.QueueFamily;

			if (state.F.CreateCommandPool(state.Device, &pool, nullptr, &pending.Pool) != VK_SUCCESS)
			{
				return Result<Pending>::Failure(Failure("Rhi.VulkanCommands", "Could not allocate a command pool."));
			}

			VkCommandBufferAllocateInfo allocation{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			allocation.commandPool = pending.Pool;
			allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocation.commandBufferCount = 1;

			if (state.F.AllocateCommandBuffers(state.Device, &allocation, &pending.Commands) != VK_SUCCESS)
			{
				state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);

				return Result<Pending>::Failure(Failure("Rhi.VulkanCommands", "Could not allocate Vulkan commands."));
			}
		}

		VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		if (state.F.BeginCommandBuffer(pending.Commands, &begin) != VK_SUCCESS)
		{
			state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);

			return Result<Pending>::Failure(Failure("Rhi.VulkanCommands", "Could not begin Vulkan copy commands."));
		}

		return Result<Pending>::Success(Move(pending));
	}

	[[nodiscard]] Result<uint64> Execute(
		State& state,
		Pending&& pending,
		VkSemaphore acquired,
		VkSemaphore presentReady,
		VkPipelineStageFlags acquireStage,
		bool bIndependent,
		const NativePreparedSubmissionDesc* preparation,
		uint32* queueWaitCount)
	{
		const auto releaseUnsubmitted = [&]()
			{
				state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);

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
			};
		if (!pending.bPrepared &&
			(state.PreparedCredits >= NumericLimits<usize>::max() - state.InFlight.Size() ||
			 !state.Submissions.TryReserveOne() ||
			 !state.InFlight.Reserve(state.InFlight.Size() + state.PreparedCredits + 1)))
		{
			releaseUnsubmitted();
			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not track Vulkan copy work."));
		}
		InvalidateIdle(state);

		const VkResult endResult = pending.bPreparedFinished || pending.bInvalidTextureLayouts ? VK_SUCCESS : state.F.EndCommandBuffer(pending.Commands);
		if (pending.bInvalidTextureLayouts || endResult != VK_SUCCESS || state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL)
		{
			if (endResult != VK_SUCCESS)
			{
				state.LastSubmissionNativeCode = static_cast<int32>(endResult);
				state.LastSubmissionFailureStage = 1;
				state.bDeviceLossConfirmed |= endResult == VK_ERROR_DEVICE_LOST;
			}
			releaseUnsubmitted();

			return Result<uint64>::Failure(
				NativeError( pending.bMissingManualState ? EErrorCode::Conflict : EErrorCode::ExternalFailure, "Rhi.VulkanCommands", "Could not finish Vulkan copy commands."));
		}

		pending.Serial = state.LastSerial + 1;

		if (pending.bSubmissionCredit)
		{
			--
				state.PreparedCredits;
			state.Submissions.ReleaseCredit();
			pending.bSubmissionCredit = false;
		}

		if (!state.InFlight.Add(Move(pending)))
		{
			releaseUnsubmitted();

			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not track Vulkan copy work."));
		}

		Pending& tracked = state.InFlight.Back();
		const VkSemaphore targetTimeline = QueueTimeline(state, tracked.QueueKind);
		const uint64 signalValue = tracked.Serial;
		VkSemaphore waits[4]{};
		VkPipelineStageFlags stages[4]{};
		uint64 waitValues[4]{};
		uint32 waitCount = 0;

		if (!bIndependent)
		{
			constexpr EQueue QUEUES[3]{ EQueue::Graphics, EQueue::Compute, EQueue::Copy };
			for (EQueue queue : QUEUES)
			{
				const VkSemaphore timeline = QueueTimeline(state, queue);
				const uint64 serial = QueueLastSerial(state, queue);

				if (timeline == targetTimeline || serial == 0)
				{
					continue;
				}

				uint32 index = 0;

				while (index < waitCount && waits[index] != timeline)
				{
					++index;
				}

				if (index == waitCount)
				{
					waits[waitCount] = timeline;
					stages[waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
					waitValues[waitCount++] = serial;
				}
				else if (serial > waitValues[index])
				{
					waitValues[index] = serial;
				}
			}
		}

		if (preparation != nullptr)
		{
			for (const rhi::NativePreparedSubmissionWait& wait : preparation->Waits)
			{
				const VkSemaphore sourceTimeline = QueueTimeline(state, wait.Queue);
				if (sourceTimeline == targetTimeline || wait.Serial == 0)
				{
					continue;
				}

				uint32 index = 0;
				while (index < waitCount && waits[index] != sourceTimeline)
				{
					++index;
				}

				if (index == waitCount)
				{
					waits[waitCount] = sourceTimeline;
					stages[waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
					waitValues[waitCount++] = wait.Serial;
				}
				else
				{
					waitValues[index] = Max(waitValues[index], wait.Serial);
				}
			}
		}

		if (acquired != VK_NULL_HANDLE)
		{
			waits[waitCount] = acquired;
			stages[waitCount] = acquireStage;
			waitValues[waitCount++] = 0;
		}

		if (tracked.ExternalSemaphore != VK_NULL_HANDLE && tracked.bExternalAcquire)
		{
			waits[waitCount] = tracked.ExternalSemaphore;
			stages[waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			waitValues[waitCount++] = tracked.ExternalSemaphoreValue;
		}

		VkSemaphore signals[3]{ targetTimeline, presentReady };
		uint64 signalValues[3]{ signalValue, 0 };
		uint32 signalCount = presentReady != VK_NULL_HANDLE ? 2u : 1u;

		if (tracked.ExternalSemaphore != VK_NULL_HANDLE && !tracked.bExternalAcquire)
		{
			signals[signalCount] = tracked.ExternalSemaphore;
			signalValues[signalCount++] = tracked.ExternalSemaphoreValue;
		}

		VkTimelineSemaphoreSubmitInfo timeline{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
		timeline.waitSemaphoreValueCount = waitCount;
		timeline.pWaitSemaphoreValues = waitValues;
		timeline.signalSemaphoreValueCount = signalCount;
		timeline.pSignalSemaphoreValues = signalValues;
		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.pNext = &timeline;
		submit.waitSemaphoreCount = waitCount;
		submit.pWaitSemaphores = waits;
		submit.pWaitDstStageMask = stages;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &tracked.Commands;
		submit.signalSemaphoreCount = signalCount;
		submit.pSignalSemaphores = signals;
		VkQueue nativeQueue = NativeQueue(state, tracked.QueueKind);
		VkSubmitInfo batches[2]{ submit, {VK_STRUCTURE_TYPE_SUBMIT_INFO} };
		VkTimelineSemaphoreSubmitInfo completed{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
		uint32 batchCount = 1;

		if (tracked.ExternalSemaphore != VK_NULL_HANDLE && !tracked.bExternalAcquire)
		{
			// Signals in one batch are unordered. A later batch makes our completion
			// token cover the external signal operation and its semaphore lifetime.
			timeline.signalSemaphoreValueCount = signalCount - 1;
			timeline.pSignalSemaphoreValues = signalValues + 1;
			batches[0].signalSemaphoreCount = signalCount - 1;
			batches[0].pSignalSemaphores = signals + 1;
			completed.signalSemaphoreValueCount = 1;
			completed.pSignalSemaphoreValues = &signalValue;
			batches[1].pNext = &completed;
			batches[1].signalSemaphoreCount = 1;
			batches[1].pSignalSemaphores = &targetTimeline;
			batchCount = 2;
		}

		if (queueWaitCount != nullptr)
		{
			*queueWaitCount = waitCount;
		}

		state.bSubmissionMayBePending = true;
		const VkResult submittedResult = state.F.QueueSubmit(nativeQueue, batchCount, batches, VK_NULL_HANDLE);

		if (submittedResult != VK_SUCCESS)
		{
			state.LastSubmissionNativeCode = static_cast<int32>(submittedResult);
			state.LastSubmissionFailureStage = 3;
			state.bLost = true;
			state.bDeviceLossConfirmed |= submittedResult == VK_ERROR_DEVICE_LOST;

			return Result<uint64>::Failure(Failure("Rhi.VulkanSubmit", "Could not submit Vulkan copy work."));
		}

		state.LastSerial = signalValue;
		const rhi::SubmissionCompletion::ELane lane =
			targetTimeline == state.ComputeTimeline && state.bSeparateComputeQueue ? SubmissionCompletion::ELane::Compute
			: targetTimeline == state.CopyTimeline && state.bSeparateCopyQueue ? SubmissionCompletion::ELane::Copy
			: SubmissionCompletion::ELane::Direct;
		state.Submissions.RecordSubmission(signalValue, lane);

		if (lane == SubmissionCompletion::ELane::Compute)
		{
			state.LastComputeSerial = signalValue;
		}
		else if (lane == SubmissionCompletion::ELane::Copy)
		{
			state.LastCopySerial = signalValue;
		}
		else
		{
			state.LastGraphicsSerial = signalValue;
		}

		CommitTextureLayouts(tracked);

		return Result<uint64>::Success(signalValue);
	}

	[[nodiscard]] Result<void> PrepareSwapChainTarget(Pending& pending, Texture& texture)
	{
		SwapChain* const swapChain = texture.SwapChainOwner;

		if (swapChain == nullptr)
		{
			return Result<void>::Success();
		}

		if (!swapChain->bAcquired || swapChain->bReady || swapChain->AcquiredImageIndex != texture.SwapChainImageIndex || (pending.SwapChainOwner != nullptr && pending.SwapChainOwner != swapChain))
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.VulkanPresent", "The swapchain image is not available for this command list.");
		}

		pending.SwapChainOwner = swapChain;
		pending.SwapChainImageIndex = texture.SwapChainImageIndex;
		pending.SwapChainSemaphoreIndex = swapChain->AcquiredSemaphoreIndex;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> CreateCommandList(State* opaque, EQueue queue)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.VulkanLost", "The Vulkan queue is unavailable."));
		}

		Result<Pending> started = Begin(state, queue);

		if (!started)
		{
			return Result<void*>::Failure(started.GetError());
		}

		Pending pending = Move(started).TakeValue();
		pending.QueueKind = queue;
		Pending* list = CreateObject<Pending>(state.Resource, Move(pending));

		if (list == nullptr)
		{
			state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);

			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate Vulkan command state."));
		}

		return Result<void*>::Success(list);
	}

	void* GetNativeCommandList(const void* native) noexcept
	{
		return reinterpret_cast<void*>(static_cast<const Pending*>(native)->Commands);
	}

	void InvalidateCommandState(void* native) noexcept
	{
		Pending& pending = *static_cast<Pending*>(native);
		pending.LastPreparedDraw = UINT32_MAX;
		pending.LastTexturePipeline = nullptr;
		pending.LastGraphicsAccelStruct = nullptr;
		pending.LastTextureCount = 0;
		pending.LastStorageTextureCount = 0;
		pending.LastGraphicsBufferCount = 0;
		pending.LastTextureSet = VK_NULL_HANDLE;
		pending.LastTextureSetCount = 0;
		pending.LastComputePipeline = nullptr;
		pending.LastComputeAccelStruct = nullptr;
		pending.LastComputeBufferCount = 0;
		pending.LastComputeTextureCount = 0;
		pending.LastComputeSet = VK_NULL_HANDLE;
		pending.RangeStorageTableCount = 0;
		pending.bShadingRateSet = false;
	}

	void SetAutomaticBarriers(void* native, bool bEnabled) noexcept
	{
		static_cast<Pending*>(native)->bAutomaticBarriers = bEnabled;
	}

	[[nodiscard]] bool ShouldRecordBufferUavBarrier(const Pending& pending, VkBuffer resource) noexcept
	{
		for (const Pending::BufferUavBarrierPolicy& policy : pending.BufferUavPolicies)
		{
			if (policy.Resource == resource)
			{
				return policy.bEnabled || !policy.bFirstBarrierPlaced;
			}
		}

		return true;
	}

	[[nodiscard]] bool ShouldRecordTextureUavBarrier(const Pending& pending, VkImage resource) noexcept
	{
		for (const Pending::TextureUavBarrierPolicy& policy : pending.TextureUavPolicies)
		{
			if (policy.Resource == resource)
			{
				return policy.bEnabled || !policy.bFirstBarrierPlaced;
			}
		}

		return true;
	}

	void MarkBufferUavBarrierPlaced(Pending& pending, VkBuffer resource) noexcept
	{
		for (Pending::BufferUavBarrierPolicy& policy : pending.BufferUavPolicies)
		{
			if (policy.Resource == resource)
			{
				policy.bFirstBarrierPlaced = true;

				return;
			}
		}
	}

	void MarkTextureUavBarrierPlaced(Pending& pending, VkImage resource) noexcept
	{
		for (Pending::TextureUavBarrierPolicy& policy : pending.TextureUavPolicies)
		{
			if (policy.Resource == resource)
			{
				policy.bFirstBarrierPlaced = true;

				return;
			}
		}
	}

	[[nodiscard]] Result<void> SetBufferUavBarriers(void* native, void* bufferNative, bool bEnabled)
	{
		Pending& pending = *static_cast<Pending*>(native);
		const VkBuffer resource = static_cast<const Buffer*>(bufferNative)->Handle;

		for (Pending::BufferUavBarrierPolicy& policy : pending.BufferUavPolicies)
		{
			if (policy.Resource != resource)
			{
				continue;
			}

			policy.bEnabled = bEnabled;
			policy.bFirstBarrierPlaced = false;

			return Result<void>::Success();
		}

		if (!pending.BufferUavPolicies.Add({ resource, bEnabled }))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not record the Vulkan buffer UAV barrier policy."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> SetTextureUavBarriers(void* native, void* textureNative, bool bEnabled)
	{
		Pending& pending = *static_cast<Pending*>(native);
		const VkImage resource = static_cast<const Texture*>(textureNative)->Handle;

		for (Pending::TextureUavBarrierPolicy& policy : pending.TextureUavPolicies)
		{
			if (policy.Resource != resource)
			{
				continue;
			}

			policy.bEnabled = bEnabled;
			policy.bFirstBarrierPlaced = false;

			return Result<void>::Success();
		}

		if (!pending.TextureUavPolicies.Add({ resource, bEnabled }))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not record the Vulkan texture UAV barrier policy."));
		}

		return Result<void>::Success();
	}

	void DiscardCommandList(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		if (pending.bSubmissionCredit)
		{
			--state.PreparedCredits;
			state.Submissions.ReleaseCredit();
			pending.bSubmissionCredit = false;
		}

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

		state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);
		DestroyObject(state.Resource, &pending);
	}

	void Destroy(State* opaque) noexcept
	{
		State& state = *opaque;
		IGpuCrashDiagnostics* const diagnostics = state.Diagnostics;
		const bool bAttached = state.bAftermathAttached;

		if (state.bLost && bAttached)
		{
			NotifyAftermathDeviceLost(diagnostics);
		}

		for (Pending& pending : state.InFlight)
		{
			for (uint64 token : pending.AftermathMarkers)
			{
				(void)diagnostics->RetireMarker(token);
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

			if (pending.Pool)
			{
				state.F.DestroyCommandPool(state.Device, pending.Pool, nullptr);
			}
		}

		state.InFlight.Clear();

		for (const RetiredAccelStruct& retired : state.RetiredAccelStructs)
		{
			DestroyAccelStructNow(&state, retired.Item);
		}

		for (const RetiredMicromap& retired : state.RetiredMicromaps)
		{
			DestroyOpacityMicromapNow(&state, retired.Item);
		}

		for (const Retired& retired : state.RetiredBuffers)
		{
			FreeBuffer(state, retired.Item);
		}

		for (const RetiredFramebuffer& retired : state.RetiredFramebuffers)
		{
			FreeFramebuffer(state, retired.Item);
		}

		for (const RetiredTexture& retired : state.RetiredTextures)
		{
			FreeTexture(state, retired.Item);
		}

		for (const RetiredPipeline& retired : state.RetiredPipelines)
		{
			FreePipeline(state, retired.Item);
		}

		for (const RetiredGraphicsPipeline& retired : state.RetiredGraphicsPipelines)
		{
			FreeGraphicsPipeline(state, retired.Item);
		}

		for (const RetiredRayPipeline& retired : state.RetiredRayPipelines)
		{
			FreeRayPipeline(state, retired.Item);
		}

		for (vulkan::State::RecycledCommandBuffer const& recycled : state.Recycled)
		{
			state.F.DestroyCommandPool(state.Device, recycled.Pool, nullptr);
		}

		if (state.ComputeTimeline && state.ComputeTimeline != state.Timeline)
		{
			state.F.DestroySemaphore(state.Device, state.ComputeTimeline, nullptr);
		}

		if (state.CopyTimeline && state.CopyTimeline != state.Timeline && state.CopyTimeline != state.ComputeTimeline)
		{
			state.F.DestroySemaphore(state.Device, state.CopyTimeline, nullptr);
		}

		if (state.Timeline)
		{
			state.F.DestroySemaphore(state.Device, state.Timeline, nullptr);
		}

		for (VkFence fence : state.IdleFences)
		{
			if (fence)
			{
				state.F.DestroyFence(state.Device, fence, nullptr);
			}
		}

		for (VkFence fence : state.SparseFences)
		{
			if (fence)
			{
				state.F.DestroyFence(state.Device, fence, nullptr);
			}
		}

		for (VkSemaphore ready : state.SparseReady)
		{
			if (ready)
			{
				state.F.DestroySemaphore(state.Device, ready, nullptr);
			}
		}

		if (state.Pool)
		{
			state.F.DestroyCommandPool(state.Device, state.Pool, nullptr);
		}

		if (state.ComputePool)
		{
			state.F.DestroyCommandPool(state.Device, state.ComputePool, nullptr);
		}

		if (state.CopyPool)
		{
			state.F.DestroyCommandPool(state.Device, state.CopyPool, nullptr);
		}

		if (state.RenderPass)
		{
			state.F.DestroyRenderPass(state.Device, state.RenderPass, nullptr);
		}

		if (state.DepthRenderPass)
		{
			state.F.DestroyRenderPass(state.Device, state.DepthRenderPass, nullptr);
		}

		if (state.DepthLoadRenderPass)
		{
			state.F.DestroyRenderPass(state.Device, state.DepthLoadRenderPass, nullptr);
		}

		for (const DirectRenderPass& pass : state.DirectRenderPasses)
		{
			state.F.DestroyRenderPass(state.Device, pass.Handle, nullptr);
		}

		if (state.Device && state.F.DestroyDevice && !state.bExternalDevice)
		{
			state.F.DestroyDevice(state.Device, nullptr);
		}

		if (state.DebugMessenger && state.F.DestroyDebugMessenger)
		{
			state.F.DestroyDebugMessenger(state.Instance, state.DebugMessenger, nullptr);
		}

		if (state.Instance && state.F.DestroyInstance && !state.bExternalDevice)
		{
			state.F.DestroyInstance(state.Instance, nullptr);
		}
#ifdef _WIN32
		if (state.Library)
		{
			FreeLibrary(state.Library);
		}
#else
		if (state.Library)
		{
			dlclose(state.Library);
		}
#endif
		DestroyObject(state.Resource, &state);

		if (bAttached)
		{
			DetachAftermathDevice(diagnostics);
		}
	}

	[[nodiscard]] CommandListStatistics GetCommandListStatistics(const void* native) noexcept
	{
		return static_cast<const Pending*>(native)->Statistics;
	}

	[[nodiscard]] bool HasMissingManualState(const void* native) noexcept
	{
		return static_cast<const Pending*>(native)->bMissingManualState;
	}

	[[nodiscard]] DeviceLimits GetLimits(const State* opaque) noexcept
	{
		const State& state = *opaque;
		DeviceLimits result{};
		const VkPhysicalDeviceLimits& native = state.DeviceProperties.limits;

		for (unsigned axis = 0; axis < 3; ++axis)
		{
			result.MaximumComputeWorkGroupSize[axis] = native.maxComputeWorkGroupSize[axis];
			result.MaximumComputeWorkGroupCount[axis] = native.maxComputeWorkGroupCount[axis];
		}

		result.MaximumComputeWorkGroupInvocations = native.maxComputeWorkGroupInvocations;
		result.MaximumUniformBufferRangeBytes = native.maxUniformBufferRange;
		result.MaximumStorageBufferRangeBytes = native.maxStorageBufferRange;
		result.UniformBufferOffsetAlignmentBytes = static_cast<uint32>(native.minUniformBufferOffsetAlignment);
		result.StorageBufferOffsetAlignmentBytes = static_cast<uint32>(native.minStorageBufferOffsetAlignment);
		result.MaximumTexture1DDimension = native.maxImageDimension1D;
		result.MaximumTexture2DDimension = native.maxImageDimension2D;
		result.MaximumTexture3DDimension = native.maxImageDimension3D;
		result.MaximumTextureArrayLayers = native.maxImageArrayLayers;
		result.MaximumBindingLayouts = Min(result.MaximumBindingLayouts, native.maxBoundDescriptorSets);
		result.MaximumPushConstantByteCount = Min(result.MaximumPushConstantByteCount, native.maxPushConstantsSize);
		VkPhysicalDeviceMaintenance3Properties allocation{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES };
		VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		properties.pNext = &allocation;
		state.F.GetPhysicalDeviceProperties2(state.Physical, &properties);

		if (allocation.maxMemoryAllocationSize != 0)
		{
			result.MaximumTextureByteCount = Min(result.MaximumTextureByteCount, static_cast<uint64>(allocation.maxMemoryAllocationSize));
			result.MaximumBufferByteCount = Min(result.MaximumBufferByteCount, static_cast<uint64>(allocation.maxMemoryAllocationSize));
		}

		return result;
	}
}

namespace rox::rhi
{
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	void ReturnNotReadyOnNextVulkanAcquireForTesting() noexcept
	{
		vulkan::ReturnNotReadyOnNextVulkanAcquireForTesting();
	}
#endif
}

namespace rox::vulkan
{
	using namespace core;

	Result<void*> CreateSamplerFeedbackTexture(void*, void*, const TextureDesc&, const SamplerFeedbackDesc&)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void*> ImportSamplerFeedbackTexture(void*, void*, const TextureDesc&, const SamplerFeedbackDesc&, void*)
	{
		return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	uint32 GetDescriptorTableDeviceHeapIndex(const void*, const void*) noexcept
	{
		ASSERT(false, "An unsupported backend operation must not be called.");
		return {};
	}

	Result<void> RecordSetSamplerFeedbackState(void*, void*, void*, ESamplerFeedbackState)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordSamplerFeedbackClear(void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordSamplerFeedbackDecode(void*, void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordSamplerFeedbackDecodeTexture(void*, void*, void*, void*)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<TextureBufferFootprint> GetTextureBufferFootprint(void*, const TextureDesc&, TextureSubresource)
	{
		return Result<TextureBufferFootprint>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}

	Result<void> RecordDeviceHeapUses(void*, void*, bool, Span<const NativeDescriptorHeapBinding>)
	{
		return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.BackendFeature", "The compiled backend does not support this operation.");
	}
}
