#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	Device::CallScope::CallScope(const Device& device, bool bRecordingAllowed, bool bPreparedAllowed)
		: Owner(device)
		, Previous(Current)
		, Lock(device.m_State->m_MetadataMutex, DeferLock)
	{
		ASSERT(!bNativeCallback, "Native callbacks cannot reenter RHI device operations.");
		for (CallScope* scope = Previous; scope != nullptr; scope = scope->Previous)
		{
			if (&scope->Owner == &device)
			{
				Root = scope->Root;
				break;
			}
		}

		if (Root == nullptr)
		{
			Root = this;
			Lock.lock();
			++device.m_State->m_ActiveCalls;
		}
		ASSERT(bRecordingAllowed || !device.m_State->m_bParallelRecording, "Resource mutation, submission, and waits require EndParallelRecording first.");
		bAllowsPrepared = bPreparedAllowed;
		bPreviousPreparedErrors = g_bPreparedErrors;
		g_bPreparedErrors = bPreparedAllowed;
		Current = this;
	}

	Device::CallScope::~CallScope()
	{
		Current = Previous;
		g_bPreparedErrors = bPreviousPreparedErrors;

		if (Root != this)
		{
			return;
		}

		if (Command != nullptr)
		{
			Command->RecordingThread = {};
		}
		--Owner.m_State->m_ActiveCalls;
	}

	void Device::claimCommand(const CommandSlot& command) const noexcept
	{
		CallScope* scope = CallScope::Current;
		ASSERT(scope != nullptr && &scope->Owner == this, "Command lookup requires a device call scope.");
		scope = scope->Root;
		const ExecutionThreadId current = CurrentExecutionThreadId();
		ASSERT(!command.PreparedGroup || !command.PreparedGroup->bRecording || command.PreparedGroup->Mode != EPreparedRecordingMode::Serial || command.PreparedGroup->RecordingThread == current,
			"Serial prepared recording requires its owning thread.");

		ASSERT(command.RecordingThread == ExecutionThreadId{} || command.RecordingThread == current, "A command list requires exclusive recording-thread ownership.");
		ASSERT(scope->Command == nullptr || scope->Command == &command, "A device call records into one command list.");
		command.RecordingThread = current;
		scope->Command = &command;
	}

	[[nodiscard]] Result<void> Device::BeginParallelRecording()
	{
		CallScope call(*this, true);

		if (m_State->m_bSubmissionFaulted)
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot record after submission failure.");
		}

		ASSERT(!m_State->m_bParallelRecording && m_State->m_ActiveCalls == 1, "Beginning parallel recording requires an idle device.");

		if (m_State->m_Resource.GetThreadSafety() != EMemoryResourceThreadSafety::ThreadSafe)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ParallelRecording", "Parallel recording requires a thread-safe memory resource.");
		}

		m_State->m_ParallelStatistics = {};
		m_State->m_bParallelRecording = true;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<ParallelRecordingStatistics> Device::EndParallelRecording()
	{
		CallScope call(*this, true);

		ASSERT(m_State->m_bParallelRecording && m_State->m_ActiveCalls == 1, "Join all recording workers before ending the active phase.");

		for (rhi::CommandSlot const& command : m_State->m_Commands)
		{
			ASSERT(!command.PreparedGroup || !command.PreparedGroup->bRecording, "Prepared recording must end through its prepared group.");
		}

		m_State->m_bParallelRecording = false;

		return Result<ParallelRecordingStatistics>::Success(m_State->m_ParallelStatistics);
	}

	[[nodiscard]] Result<CommandListHandle> Device::CreateCommandList(EQueue queue)
	{
		CallScope call(*this, false);

		if (m_State->m_bSubmissionFaulted)
		{
			return Result<CommandListHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.DeviceFaulted", "The device cannot admit new execution after submission failure.");
		}

		if ((queue != EQueue::Copy || !m_State->m_Backend.Capabilities.Features.bCopy) &&
			(queue != EQueue::Compute || !m_State->m_Backend.Capabilities.Features.bCompute) &&
			(queue != EQueue::Graphics || !m_State->m_Backend.Capabilities.Features.bGraphics))
		{
			return Result<CommandListHandle>::Failure(EErrorCode::Unsupported, "Rhi.Queue", "The requested queue is not available through this RHI device.");
		}

		const bool bNewSlot = m_State->m_FreeCommand == NumericLimits<uint32>::max();
		const uint32 index = bNewSlot ? static_cast<uint32>(m_State->m_Commands.Size()) : m_State->m_FreeCommand;

		if (bNewSlot && (m_State->m_Commands.Size() >= NumericLimits<uint32>::max() || !m_State->m_Commands.Add(CommandSlot(m_State->m_Resource))))
		{
			return Result<CommandListHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "The device could not allocate a command list slot.");
		}
		CommandSlot& slot = m_State->m_Commands[index];
		Result<void*> native = backend::CreateCommandList(m_State->m_Backend.State, queue);

		if (!native)
		{
			if (bNewSlot)
			{
				slot.NextFree = m_State->m_FreeCommand;
				m_State->m_FreeCommand = index;
			}

			return Result<CommandListHandle>::Failure(native.GetError());
		}

		if (!bNewSlot)
		{
			m_State->m_FreeCommand = slot.NextFree;
		}

		slot.NextFree = NumericLimits<uint32>::max();
		slot.Native = native.Value();
		slot.PreparedShaderTableCreations = 0;
		slot.PreparedShaderTableReuses = 0;
		slot.PreparedGroup.Reset();
		slot.bPreparationEligible = true;
		slot.Queue = queue;
		slot.ColorTarget = {};
		slot.DepthTarget = {};
		slot.ColorCount = 0;
		slot.ActiveFramebuffer = {};
		slot.ViewportCount = 0;
		slot.ScissorCount = 0;
		slot.bRendering = false;
		slot.bReadOnlyDepth = false;
		slot.bAutomaticBarriers = true;
		slot.bHasManualBufferUses = false;
		slot.bPoisoned = false;
		slot.SubmitPrerequisite = {};
		slot.MarkerDepth = 0;
		slot.bLive = true;

		return Result<CommandListHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<CommandListStatistics> Device::GetCommandListStatistics(CommandListHandle list) const
	{
		CallScope call(*this, true, true);
		const CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		CommandListStatistics statistics = backend::GetCommandListStatistics(command->Native);
		statistics.PreparedShaderTableCreations = command->PreparedShaderTableCreations;
		statistics.PreparedShaderTableReuses = command->PreparedShaderTableReuses;

		return Result<CommandListStatistics>::Success(statistics);
	}

	[[nodiscard]] Result<NativeCommandListHandle> Device::GetNativeCommandList(CommandListHandle list)
	{
		CallScope call(*this, false);
		CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		if (m_State->m_Kind == EBackend::Null)
		{
			return Result<NativeCommandListHandle>::Failure(EErrorCode::Unsupported, "Rhi.NativeCommandList", "The Null backend has no native command list.");
		}

		command->bPreparationEligible = false;
		backend::InvalidateCommandState(command->Native);

		return Result<NativeCommandListHandle>::Success({ m_State->m_Kind, backend::GetNativeCommandList(command->Native) });
	}

	[[nodiscard]] Result<void> Device::ClearState(CommandListHandle list)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		if (m_State->m_Kind == EBackend::Null)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ClearState", "The Null backend has no native command state.");
		}

		backend::InvalidateCommandState(command->Native);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::SetEnableAutomaticBarriers(CommandListHandle list, bool bEnabled)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		backend::SetAutomaticBarriers(command->Native, bEnabled);
		command->bAutomaticBarriers = bEnabled;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::SetEnableUavBarriersForBuffer(CommandListHandle list, BufferHandle buffer, bool bEnabled)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		BufferSlot* const slot = findBuffer(buffer);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!(command->Queue == EQueue::Copy || !HasUsage(slot->Usage, EBufferUsage::Storage) || slot->bMapped), "A non-mapped storage buffer and graphics or compute list are required.");

		if (m_State->m_Kind == EBackend::Vulkan && !bEnabled && command->Queue != EQueue::Compute)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanUavBarriers", "Vulkan graphics UAV barriers are not resource-specific.");
		}

		Result<void> referenced = ReferenceBuffer(list, buffer);

		if (!referenced)
		{
			poisonCommandList(list);

			return referenced;
		}

		Result<void> changed = backend::SetBufferUavBarriers(command->Native, slot->Native, bEnabled);

		if (!changed)
		{
			poisonCommandList(list);
		}

		return changed;
	}

	[[nodiscard]] Result<void> Device::SetEnableUavBarriersForTexture(CommandListHandle list, TextureHandle texture, bool bEnabled)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!(command->Queue == EQueue::Copy || !HasUsage(slot->Descriptor.Usage, ETextureUsage::Storage)), "A storage texture and graphics or compute list are required.");

		if (m_State->m_Kind == EBackend::Vulkan && !bEnabled && command->Queue != EQueue::Compute)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.VulkanUavBarriers", "Vulkan graphics UAV barriers are not resource-specific.");
		}

		Result<void> referenced = ReferenceTexture(list, texture);

		if (!referenced)
		{
			poisonCommandList(list);

			return referenced;
		}

		Result<void> changed = backend::SetTextureUavBarriers(command->Native, slot->Native, bEnabled);

		if (!changed)
		{
			poisonCommandList(list);
		}

		return changed;
	}

	void Device::poisonCommandList(CommandListHandle handle) noexcept
	{
		ASSERT(handle.IsValid() && handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Commands.Size(), "Only a live command list can be poisoned.");
		CommandSlot& command = m_State->m_Commands[handle.Slot];
		ASSERT(command.bLive && command.Generation == handle.Generation, "Only a live command list can be poisoned.");
		if (command.Native != nullptr)
		{
			backend::DiscardCommandList(m_State->m_Backend.State, command.Native);
			command.Native = nullptr;
		}
		command.bPoisoned = true;
	}

	void Device::DiscardCommandList(CommandListHandle list)
	{
		CallScope call(*this, false, true);
		ASSERT(list.IsValid() && list.Owner == m_State->m_Owner && list.Slot < m_State->m_Commands.Size(), "RHI operations require valid handles for this device.");
		CommandSlot& slot = m_State->m_Commands[list.Slot];
		ASSERT(slot.bLive && slot.Generation == list.Generation, "Discard requires a live command list.");
		CommandSlot* const command = &slot;
		claimCommand(*command);

		if (command->Native != nullptr)
		{
			backend::DiscardCommandList(m_State->m_Backend.State, command->Native);
		}
		resetRecordedTimers(*command);
		releaseRecordedUses(*command);
		command->Native = nullptr;
		command->Copies.Clear();
		command->TextureCopies.Clear();
		command->TextureUses.Clear();
		command->BufferUses.Clear();
		command->bHasManualBufferUses = false;
		command->AccelUses.Clear();
		command->AccelBuildTargets.Clear();
		command->OpacityMicromapUses.Clear();
		command->OpacityMicromapBuildTargets.Clear();
		command->PipelineUses.Clear();
		command->GraphicsPipelineUses.Clear();
		command->RayPipelineUses.Clear();
		command->FramebufferUses.Clear();
		command->TimerUses.Clear();
		command->SamplerUses.Clear();
		command->TextureViewUses.Clear();
		command->DescriptorTableUses.Clear();
		command->MarkerDepth = 0;
		command->bLive = false;
		Advance(*command);

		if (!command->bExhausted)
		{
			command->NextFree = m_State->m_FreeCommand;
			m_State->m_FreeCommand = list.Slot;
		}
	}

	[[nodiscard]] Result<void> Device::BeginMarker(CommandListHandle list, const char* name)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(name == nullptr || name[0] == '\0' || CStringLength(name) > 255 || command->MarkerDepth == 64), "The marker name or nesting depth is invalid.");

		if (!m_State->m_Backend.Capabilities.Features.bDebugMarkers)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.Marker", "GPU debug markers are unavailable on this device.");
		}

		Result<void> recorded = backend::RecordBeginMarker(m_State->m_Backend.State, command->Native, name);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}
		++command->MarkerDepth;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::EndMarker(CommandListHandle list)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		if (!m_State->m_Backend.Capabilities.Features.bDebugMarkers)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.Marker", "GPU debug markers are unavailable on this device.");
		}

		ASSERT(command->MarkerDepth != 0, "EndMarker closes an open debug marker on this command list.");

		Result<void> recorded = backend::RecordEndMarker(m_State->m_Backend.State, command->Native);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}
		--command->MarkerDepth;

		return Result<void>::Success();
	}

	void Device::resetRecordedTimers(CommandSlot& command) noexcept
	{
		for (GpuTimerHandle timer : command.TimerUses)
		{
			TimerSlot& slot = m_State->m_Timers[timer.Slot];
			slot.State = ETimerState::Idle;
			slot.RecordingSlot = NumericLimits<uint32>::max();
			slot.Token = {};
		}
	}
}
