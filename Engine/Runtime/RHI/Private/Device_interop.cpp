#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<NativeQueueAccessHandle> Device::BeginNativeQueueAccess(EQueue queue)
	{
		CallScope call(*this, false);
		using R = Result<NativeQueueAccessHandle>;
		if (m_State->m_bSubmissionFaulted)
		{
			return R::Failure(m_State->m_FirstSubmissionFailure, "Rhi.SubmissionFault", "The device no longer accepts native work.");
		}

		Result<NativeQueueHandle> native = GetNativeQueue(queue);
		if (!native)
		{
			return R::Failure(Move(native.GetError()));
		}
		if (hasNativeQueueAccess(queue))
		{
			Error error;
			error.Code = EErrorCode::Conflict;

			return R::Failure(Move(error));
		}

		const uint32 index = static_cast<uint32>(queue);
		if (m_State->m_NativeQueueAccessGenerations[index] == UINT64_MAX)
		{
			Error error;
			error.Code = EErrorCode::Unsupported;

			return R::Failure(Move(error));
		}
		m_State->m_NativeQueueAccessPointers[index] = native.Value().Handle;
		++m_State->m_NativeQueueAccessGenerations[index];

		return R::Success({ m_State->m_Owner, m_State->m_NativeQueueAccessGenerations[index], queue });
	}

	[[nodiscard]] Result<void> Device::EndNativeQueueAccess(const NativeQueueAccessHandle handle)
	{
		CallScope call(*this, false);
		const uint32 index = static_cast<uint32>(handle.Queue);
		ASSERT(handle.Owner == m_State->m_Owner && handle.Generation != 0 && index < 3 && handle.Generation == m_State->m_NativeQueueAccessGenerations[index] && m_State->m_NativeQueueAccessPointers[index] != nullptr,
			"Ending native queue access requires a live admission from this device.");
		m_State->m_NativeQueueAccessPointers[index] = nullptr;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<NativeDeviceHandles> Device::GetNativeDeviceHandles() const
	{
		CallScope call(*this, true);

		if (m_State->m_bSubmissionFaulted)
		{
			return Result<NativeDeviceHandles>::Failure(m_State->m_FirstSubmissionFailure, "Rhi.DeviceFaulted", "The device no longer exposes native execution handles.");
		}

		if (m_State->m_Kind == EBackend::Null)
		{
			return Result<NativeDeviceHandles>::Failure(EErrorCode::Unsupported, "Rhi.NativeHandles", "The Null device has no native GPU handles.");
		}

		if constexpr (backend::HAS_EXPOSE_NATIVE_QUEUES)
		{
			backend::ExposeNativeQueues(m_State->m_Backend.State);
		}

		return Result<NativeDeviceHandles>::Success(m_State->m_Backend.NativeHandles);
	}

	[[nodiscard]] Result<NativeQueueHandle> Device::GetNativeQueue(const EQueue queue) const
	{
		CallScope call(*this, true);
		ASSERT(queue == EQueue::Graphics || queue == EQueue::Compute || queue == EQueue::Copy, "Native queue queries require a valid queue kind.");
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<NativeQueueHandle>::Failure(m_State->m_FirstSubmissionFailure, "Rhi.DeviceFaulted", "The device no longer exposes native queues.");
		}

		if (m_State->m_Kind == EBackend::Null ||
			(queue == EQueue::Graphics && !m_State->m_Backend.Capabilities.Features.bGraphics) ||
			(queue == EQueue::Compute && !m_State->m_Backend.Capabilities.Features.bCompute) ||
			(queue == EQueue::Copy && !m_State->m_Backend.Capabilities.Features.bCopy))
		{
			return Result<NativeQueueHandle>::Failure(EErrorCode::Unsupported, "Rhi.Queue", "The native queue is unavailable.");
		}
		void* const native = queue == EQueue::Compute ? m_State->m_Backend.NativeComputeQueue
			: queue == EQueue::Copy && m_State->m_Backend.NativeCopyQueue != nullptr ? m_State->m_Backend.NativeCopyQueue
			: m_State->m_Backend.NativeHandles.Queue;

		if (native == nullptr)
		{
			return Result<NativeQueueHandle>::Failure(EErrorCode::ExternalFailure, "Rhi.Queue", "The native queue handle is unavailable.");
		}

		if constexpr (backend::HAS_EXPOSE_NATIVE_QUEUES)
		{
			backend::ExposeNativeQueues(m_State->m_Backend.State);
		}

		return Result<NativeQueueHandle>::Success({ m_State->m_Kind, native,
														 queue == EQueue::Compute ? m_State->m_Backend.NativeComputeQueueFamilyIndex
														 : queue == EQueue::Copy ? m_State->m_Backend.NativeCopyQueueFamilyIndex
																				  : m_State->m_Backend.NativeHandles.QueueFamilyIndex });
	}

	[[nodiscard]] PreparedRecordingPhase* Device::findNativeCapturePhase(CommandListHandle handle, uint32 phaseIndex) noexcept
	{
		CommandSlot* command = findPreparedControlCommand(handle);
		PreparedRecordingList* list = preparedList(command);
		if (list == nullptr ||
			command->PreparedGroup->bRecording ||
			command->PreparedGroup->bRecorded ||
			list->bConsumed ||
			list->bFinished ||
			list->ActivePhase != UINT32_MAX ||
			list->FailureCode != EErrorCode::None ||
			phaseIndex >= list->Phases.Size())
		{
			return nullptr;
		}
		rhi::PreparedRecordingPhase& phase = list->Phases[phaseIndex];

		return phase.Capacity.NativeCallbacks ? &phase : nullptr;
	}

	[[nodiscard]] Result<NativeResourceHandle> Device::GetPreparedNativeBuffer(CommandListHandle handle, uint32 phaseIndex, BufferHandle buffer)
	{
		CallScope call(*this, false);
		using R = Result<NativeResourceHandle>;
		const PreparedRecordingPhase* phase = findNativeCapturePhase(handle, phaseIndex);
		if (phase != nullptr)
		{
			for (rhi::PreparedBufferAccessDesc const& access : phase->Buffers)
			{
				if (access.Buffer == buffer)
				{
					BufferSlot* slot = findBuffer(buffer);
					ASSERT(slot != nullptr, "Prepared phase retains its validated buffer.");
					if (m_State->m_Kind != EBackend::Null)
					{
						void* native = backend::GetNativeBuffer(slot->Native);
						if (native != nullptr)
						{
							return R::Success({ m_State->m_Kind, native });
						}
					}
					Error error;
					error.Code = EErrorCode::Unsupported;

					return R::Failure(Move(error));
				}
			}
		}
		ASSERT(false, "Native resource queries require a resource declared in the requested prepared phase.");
	}

	[[nodiscard]] Result<NativeResourceHandle> Device::GetPreparedNativeTexture(CommandListHandle handle, uint32 phaseIndex, TextureHandle texture)
	{
		CallScope call(*this, false);
		using R = Result<NativeResourceHandle>;
		const PreparedRecordingPhase* phase = findNativeCapturePhase(handle, phaseIndex);
		if (phase != nullptr)
		{
			for (rhi::PreparedTextureAccessDesc const& access : phase->Textures)
			{
				if (access.Texture == texture)
				{
					TextureSlot* slot = findTexture(texture);
					ASSERT(slot != nullptr, "Prepared phase retains its validated texture.");
					if (m_State->m_Kind != EBackend::Null)
					{
						void* native = backend::GetNativeTexture(slot->Native);
						if (native != nullptr)
						{
							return R::Success({ m_State->m_Kind, native });
						}
					}
					Error error;
					error.Code = EErrorCode::Unsupported;

					return R::Failure(Move(error));
				}
			}
		}
		ASSERT(false, "Native resource queries require a resource declared in the requested prepared phase.");
	}

	[[nodiscard]] Result<void> Device::RecordPreparedNative(CommandListHandle handle, NativeCommandCallback callback, void* userDataOrNull)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(!(phase == nullptr || callback == nullptr || command->bRendering), "Prepared recording requires valid admitted input and capacity.");
		if (m_State->m_Kind == EBackend::Null || !phase->AccelStructs.IsEmpty())
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		ASSERT(!(phase->Used.NativeCallbacks >= phase->Capacity.NativeCallbacks), "Prepared recording requires valid admitted input and capacity.");
		const NativeCommandListHandle native{ m_State->m_Kind, backend::GetNativeCommandList(command->Native) };
		if (native.Handle == nullptr)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}

		Result<void> result = recordNative(
			[&]
			{
				backend::InvalidateCommandState(command->Native);
				CallScope::bNativeCallback = true;
				const EErrorCode code = callback(userDataOrNull, native);
				CallScope::bNativeCallback = false;
				backend::InvalidateCommandState(command->Native);
				if (code == EErrorCode::None)
				{
					return Result<void>::Success();
				}
				Error error;
				error.Code = code;

				return Result<void>::Failure(Move(error));
			});
		++phase->Used.NativeCallbacks;

		return preparedEmission(*command, Move(result));
	}

	[[nodiscard]] NativeSubmissionOutcome Device::SubmitPreparedNativeBoundary(CommandListHandle handle, NativeQueueAccessHandle admission)
	{
		CallScope call(*this, false, true);
		NativeSubmissionOutcome rejected;
		rejected.ErrorCode = EErrorCode::Conflict;
		CommandSlot* command = findPreparedControlCommand(handle);
		PreparedRecordingList* list = preparedList(command);
		ASSERT(list != nullptr && !list->bConsumed, "Prepared submission requires a live prepared command list.");
		if (!ownsNativeQueueAccess(admission, command->Queue) || command->PreparedGroup->Lists.Size() != 1 || list->Phases.Size() != 1 || !command->PreparedGroup->bRecorded || !list->bFinished)
		{
			return rejected;
		}
		const PreparedRecordingPhase& phase = list->Phases[0];
		const PreparedCommandCapacity& c = phase.Capacity;
		const PreparedCommandCapacity& u = phase.Used;
		if ((!phase.Buffers.Size() && !phase.Textures.Size()) ||
			!phase.Bindings.IsEmpty() ||
			!phase.Attachments.IsEmpty() ||
			!phase.TextureBufferCopies.IsEmpty() ||
			!phase.Resolves.IsEmpty() ||
			!phase.Indirects.IsEmpty() ||
			!phase.AccelStructs.IsEmpty() ||
			!phase.AccelBuilds.IsEmpty() ||
			!phase.AccelCompactionQueries.IsEmpty() ||
			!phase.AccelCompactions.IsEmpty() ||
			c.BufferCopies ||
			c.TextureCopies ||
			c.Dispatches ||
			c.Draws ||
			c.RenderPasses ||
			c.ViewportChanges ||
			c.ScissorChanges ||
			c.MemoryBarriers ||
			c.RayDispatches ||
			c.AccelBuilds ||
			c.AccelCompactionQueries ||
			c.AccelCompactions ||
			c.NativeCallbacks != 1 ||
			u.BufferCopies ||
			u.TextureCopies ||
			u.Dispatches ||
			u.Draws ||
			u.RenderPasses ||
			u.ViewportChanges ||
			u.ScissorChanges ||
			u.MemoryBarriers ||
			u.RayDispatches ||
			u.AccelBuilds ||
			u.AccelCompactionQueries ||
			u.AccelCompactions ||
			u.NativeCallbacks)
		{
			return rejected;
		}

		return submitPreparedWithAdmission(handle, {}, &admission);
	}

	[[nodiscard]] Result<NativeBufferState> Device::GetPreparedNativeBufferState(CommandListHandle list, BufferHandle buffer)
	{
		ASSERT(buffer.IsValid(), "Native buffer state requires a valid buffer handle.");
		Result<uint32> state = getPreparedNativeBoundaryState(list, buffer, {});
		if (!state)
		{
			return Result<NativeBufferState>::Failure(state.GetError());
		}

		return Result<NativeBufferState>::Success(NativeBufferState::FromNative(m_State->m_Kind, state.Value()));
	}

	[[nodiscard]] Result<NativeTextureState> Device::GetPreparedNativeTextureState(CommandListHandle list, TextureHandle texture)
	{
		ASSERT(texture.IsValid(), "Native texture state requires a valid texture handle.");
		Result<uint32> state = getPreparedNativeBoundaryState(list, {}, texture);
		if (!state)
		{
			return Result<NativeTextureState>::Failure(state.GetError());
		}

		return Result<NativeTextureState>::Success(NativeTextureState::FromNative(m_State->m_Kind, state.Value()));
	}

	[[nodiscard]] void* Device::nativeQueueIdentity(EQueue queue) const noexcept
	{
		return queue == EQueue::Compute ? m_State->m_Backend.NativeComputeQueue : queue == EQueue::Copy && m_State->m_Backend.NativeCopyQueue != nullptr ? m_State->m_Backend.NativeCopyQueue : m_State->m_Backend.NativeHandles.Queue;
	}

	[[nodiscard]] bool Device::hasNativeQueueAccess(EQueue queue) const noexcept
	{
		const void* identity = nativeQueueIdentity(queue);
		for (const void* held : m_State->m_NativeQueueAccessPointers)
		{
			if (held != nullptr && held == identity)
			{
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] bool Device::hasAnyNativeQueueAccess() const noexcept
	{
		for (const void* held : m_State->m_NativeQueueAccessPointers)
		{
			if (held != nullptr)
			{
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] bool Device::ownsNativeQueueAccess(NativeQueueAccessHandle handle, EQueue queue) const noexcept
	{
		const uint32 index = static_cast<uint32>(handle.Queue);

		return handle.Owner == m_State->m_Owner &&
			handle.Generation != 0 &&
			index < 3 &&
			handle.Queue == queue &&
			handle.Generation == m_State->m_NativeQueueAccessGenerations[index] &&
			m_State->m_NativeQueueAccessPointers[index] != nullptr &&
			m_State->m_NativeQueueAccessPointers[index] == nativeQueueIdentity(queue);
	}

	[[nodiscard]] Error Device::nativeQueueAccessConflict() noexcept
	{
		Error error;
		error.Code = EErrorCode::Conflict;

		return error;
	}

	[[nodiscard]] Result<uint32> Device::getPreparedNativeBoundaryState(CommandListHandle handle, BufferHandle buffer, TextureHandle texture)
	{
		CallScope call(*this, false);
		using R = Result<uint32>;
		CommandSlot* command = findPreparedControlCommand(handle);
		PreparedRecordingList* list = preparedList(command);
		ASSERT(list != nullptr && !command->PreparedGroup->bRecording && list->bFinished && !list->bConsumed && list->FailureCode == EErrorCode::None && list->Phases.Size() == 1,
			"Native boundary state requires one successfully finished retained phase.");
		ASSERT(buffer.IsValid() != texture.IsValid(), "A native state query selects exactly one resource.");
		const PreparedRecordingPhase& phase = list->Phases[0];
		void* resource = nullptr;
		if (buffer.IsValid())
		{
			for (rhi::PreparedBufferAccessDesc const& access : phase.Buffers)
			{
				if (access.Buffer == buffer)
				{
					resource = findBuffer(buffer)->Native;
				}
			}
		}
		if (texture.IsValid())
		{
			for (rhi::PreparedTextureAccessDesc const& access : phase.Textures)
			{
				if (access.Texture == texture)
				{
					resource = findTexture(texture)->Native;
				}
			}
		}
		ASSERT(resource != nullptr, "Native state queries require a resource declared in the retained phase.");
		if constexpr (backend::HAS_DESCRIBE_PREPARED_NATIVE_BOUNDARY == false)
		{
			Error error;
			error.Code = EErrorCode::Unsupported;

			return R::Failure(Move(error));
		}

		return backend::DescribePreparedNativeBoundary(m_State->m_Backend.State, command->Native, resource, texture.IsValid());
	}
}

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] ExternalInteropCapabilities Device::GetExternalInteropCapabilities() const
	{
		CallScope call(*this, true);

		return backend::HAS_GET_EXTERNAL_INTEROP_CAPABILITIES ? backend::GetExternalInteropCapabilities(m_State->m_Backend.State)
			: ExternalInteropCapabilities{};
	}

	[[nodiscard]] Error Device::externalInteropError(EErrorCode code, const char* message)
	{
		return MakeError(code, "Rhi.ExternalInterop", StringView(message, CStringLength(message)));
	}

	[[nodiscard]] bool Device::matchesExternalDevice(const GpuIdentity& identity) const
	{
		const GpuIdentity own = GetExternalInteropCapabilities().Device;

		if (identity.bUuidValid && own.bUuidValid)
		{
			return CompareBytes(identity.DeviceUuid, own.DeviceUuid, sizeof(own.DeviceUuid)) == 0;
		}

		return identity.bLuidValid && own.bLuidValid && CompareBytes(identity.DeviceLuid, own.DeviceLuid, sizeof(own.DeviceLuid)) == 0 && (identity.NodeMask & own.NodeMask) != 0;
	}

	[[nodiscard]] Result<BufferHandle> Device::CreateExternalBuffer(const BufferDesc& desc)
	{
		CallScope call(*this, false);

		return makeExternalBuffer(desc, nullptr);
	}

	[[nodiscard]] Result<BufferHandle> Device::ImportExternalBuffer(const ExternalBufferMemoryView& memory)
	{
		CallScope call(*this, false);

		return makeExternalBuffer(memory.Descriptor, &memory);
	}

	[[nodiscard]] Result<BufferHandle> Device::makeExternalBuffer(const BufferDesc& desc, const ExternalBufferMemoryView* memoryOrNull)
	{
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<BufferHandle>::Failure(m_State->m_FirstSubmissionFailure, "Rhi.DeviceFaulted", "The device no longer accepts external GPU ownership or work.");
		}

		Result<void> valid = validateBufferDesc(desc);

		if (!valid)
		{
			ASSERT(memoryOrNull != nullptr || valid.GetError().Code != EErrorCode::InvalidArgument,
				"Resource creation requires a valid descriptor; external imports validate untrusted metadata.");
			return Result<BufferHandle>::Failure(valid.GetError());
		}

		ASSERT(memoryOrNull != nullptr || desc.Memory == EBufferMemory::Device, "External resource creation requires dedicated device memory without sparse mapping.");

		if (desc.Memory != EBufferMemory::Device || (memoryOrNull != nullptr && (!memoryOrNull->Handle.IsValid() || !memoryOrNull->bDedicated || !matchesExternalDevice(memoryOrNull->Device))))
		{
			return Result<BufferHandle>::Failure(externalInteropError( EErrorCode::InvalidArgument, "External buffers require device memory and a dedicated allocation on the same GPU."));
		}

		if constexpr (backend::HAS_CREATE_EXTERNAL_BUFFER == false)
		{
			return Result<BufferHandle>::Failure(externalInteropError(EErrorCode::Unsupported, "External buffer memory is unavailable."));
		}

		NativeExternalMemoryView imported;
		if (memoryOrNull != nullptr)
		{
			imported = { memoryOrNull->Handle, memoryOrNull->Device, memoryOrNull->AllocationBytes,
				memoryOrNull->MemoryTypeIndex, memoryOrNull->bDedicated };
		}

		Result<void*> native = backend::CreateExternalBuffer(m_State->m_Backend.State, desc, memoryOrNull != nullptr ? &imported : nullptr);

		if (!native)
		{
			return Result<BufferHandle>::Failure(native.GetError());
		}

		Result<BufferHandle> published = createBufferInternal(desc, {}, 0, 0, nullptr, native.Value());
		if (!published)
		{
			backend::DestroyBufferNow(m_State->m_Backend.State, native.Value());

			return published;
		}

		rhi::BufferSlot& slot = m_State->m_Buffers[published.Value().Slot];
		slot.bExternalShareable = true;
		slot.bExternalOwned = memoryOrNull != nullptr;

		return published;
	}

	[[nodiscard]] Result<TextureHandle> Device::CreateExternalTexture(const TextureDesc& desc)
	{
		CallScope call(*this, false);

		return makeExternalTexture(desc, nullptr);
	}

	[[nodiscard]] Result<TextureHandle> Device::ImportExternalTexture(const ExternalTextureMemoryView& memory)
	{
		CallScope call(*this, false);

		return makeExternalTexture(memory.Descriptor, &memory);
	}

	[[nodiscard]] Result<TextureHandle> Device::makeExternalTexture(const TextureDesc& desc, const ExternalTextureMemoryView* memoryOrNull)
	{
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<TextureHandle>::Failure(m_State->m_FirstSubmissionFailure, "Rhi.DeviceFaulted", "The device no longer accepts external GPU ownership or work.");
		}

		Result<uint64> valid = validateTextureDesc(desc);

		if (!valid)
		{
			ASSERT(memoryOrNull != nullptr || valid.GetError().Code != EErrorCode::InvalidArgument,
				"Resource creation requires a valid descriptor; external imports validate untrusted metadata.");
			return Result<TextureHandle>::Failure(valid.GetError());
		}

		ASSERT(memoryOrNull != nullptr || !desc.bSparse, "External resource creation requires dedicated device memory without sparse mapping.");

		if (desc.bSparse || (memoryOrNull != nullptr && (!memoryOrNull->Handle.IsValid() || !memoryOrNull->bDedicated || !matchesExternalDevice(memoryOrNull->Device))))
		{
			return Result<TextureHandle>::Failure(externalInteropError( EErrorCode::InvalidArgument, "External textures require a dedicated non-sparse allocation on the same GPU."));
		}

		if constexpr (backend::HAS_CREATE_EXTERNAL_TEXTURE == false)
		{
			return Result<TextureHandle>::Failure(externalInteropError(EErrorCode::Unsupported, "External texture memory is unavailable."));
		}

		NativeExternalMemoryView imported;
		if (memoryOrNull != nullptr)
		{
			imported = { memoryOrNull->Handle, memoryOrNull->Device, memoryOrNull->AllocationBytes,
				memoryOrNull->MemoryTypeIndex, memoryOrNull->bDedicated };
		}

		Result<void*> native = backend::CreateExternalTexture(m_State->m_Backend.State, desc, memoryOrNull != nullptr ? &imported : nullptr);

		if (!native)
		{
			return Result<TextureHandle>::Failure(native.GetError());
		}

		Result<TextureHandle> published =
			createTextureInternal(desc, {}, 0, 0, nullptr, ENativeTextureContents::Discard, native.Value());
		if (!published)
		{
			backend::DestroyTextureNow(m_State->m_Backend.State, native.Value());

			return published;
		}

		rhi::TextureSlot& slot = m_State->m_Textures[published.Value().Slot];
		slot.bExternalShareable = true;
		slot.bExternalOwned = memoryOrNull != nullptr;

		return published;
	}

	[[nodiscard]] Result<ExternalBufferMemory> Device::ExportExternalBufferMemory(BufferHandle handle)
	{
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<ExternalBufferMemory>::Failure(m_State->m_FirstSubmissionFailure, "Rhi.DeviceFaulted", "The device no longer accepts external GPU ownership or work.");
		}
		BufferSlot* slot = findBuffer(handle, true);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->bExternalShareable, "Export requires a buffer created for external sharing.");
		if constexpr (backend::HAS_EXPORT_EXTERNAL_MEMORY == false)
		{
			return Result<ExternalBufferMemory>::Failure(externalInteropError(EErrorCode::Unsupported, "External memory export is unavailable."));
		}

		Result<NativeExternalMemory> exported = backend::ExportExternalMemory(m_State->m_Backend.State, slot->Native, false);

		if (!exported)
		{
			return Result<ExternalBufferMemory>::Failure(exported.GetError());
		}

		NativeExternalMemory memory = Move(exported).TakeValue();

		return Result<ExternalBufferMemory>::Success(
			{ { slot->ByteCount, slot->Usage, slot->Memory }, Move(memory.Handle), memory.Device, memory.AllocationBytes,
			 memory.MemoryTypeIndex, memory.bDedicated
			});
	}

	[[nodiscard]] Result<ExternalTextureMemory> Device::ExportExternalTextureMemory(TextureHandle handle)
	{
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<ExternalTextureMemory>::Failure(m_State->m_FirstSubmissionFailure, "Rhi.DeviceFaulted", "The device no longer accepts external GPU ownership or work.");
		}
		TextureSlot* slot = findTexture(handle, true);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(slot->bExternalShareable, "Export requires a texture created for external sharing.");
		if constexpr (backend::HAS_EXPORT_EXTERNAL_MEMORY == false)
		{
			return Result<ExternalTextureMemory>::Failure(externalInteropError(EErrorCode::Unsupported, "External memory export is unavailable."));
		}

		Result<NativeExternalMemory> exported = backend::ExportExternalMemory(m_State->m_Backend.State, slot->Native, true);

		if (!exported)
		{
			return Result<ExternalTextureMemory>::Failure(exported.GetError());
		}

		NativeExternalMemory memory = Move(exported).TakeValue();

		return Result<ExternalTextureMemory>::Success(
			{ slot->Descriptor, Move(memory.Handle), memory.Device, memory.AllocationBytes,
			 memory.MemoryTypeIndex, memory.bDedicated
			});
	}

	[[nodiscard]] Device::ExternalFenceSlot* Device::findExternalFence(ExternalFenceHandle handle)
	{
		return handle.Owner == m_State->m_Owner && handle.Generation != 0 && handle.Slot < m_State->m_ExternalFences.Size() &&
			   m_State->m_ExternalFences[handle.Slot].Generation == handle.Generation && m_State->m_ExternalFences[handle.Slot].Native != nullptr
			? &m_State->m_ExternalFences[handle.Slot]
			: nullptr;
	}

	[[nodiscard]] Result<ExternalFenceHandle> Device::CreateExternalFence(uint64 initialValue)
	{
		CallScope call(*this, false);

		return makeExternalFence(initialValue, nullptr);
	}

	[[nodiscard]] Result<ExternalFenceHandle> Device::ImportExternalFence(const ExternalFenceView& fence)
	{
		CallScope call(*this, false);

		return makeExternalFence(0, &fence);
	}

	[[nodiscard]] Result<ExternalFenceHandle> Device::makeExternalFence(uint64 initialValue, const ExternalFenceView* fence)
	{
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<ExternalFenceHandle>::Failure(m_State->m_FirstSubmissionFailure, "Rhi.DeviceFaulted", "The device no longer accepts external GPU ownership or work.");
		}

		ASSERT(initialValue != UINT64_MAX, "An external fence initial value must leave room for subsequent signals.");
		if (fence != nullptr && (!fence->Handle.IsValid() || !matchesExternalDevice(fence->Device)))
		{
			return Result<ExternalFenceHandle>::Failure(externalInteropError(EErrorCode::InvalidArgument, "The external fence identity or initial value is invalid."));
		}

		if constexpr (backend::HAS_CREATE_EXTERNAL_FENCE == false)
		{
			return Result<ExternalFenceHandle>::Failure(externalInteropError(EErrorCode::Unsupported, "External timeline fences are unavailable."));
		}

		const bool bNew = m_State->m_FreeExternalFence == UINT32_MAX;

		if (bNew && (m_State->m_ExternalFences.Size() >= UINT32_MAX || !m_State->m_ExternalFences.Reserve(m_State->m_ExternalFences.Size() + 1)))
		{
			return Result<ExternalFenceHandle>::Failure(externalInteropError(EErrorCode::ExternalFailure, "Could not reserve an external fence slot."));
		}

		Result<void*> native = backend::CreateExternalFence(m_State->m_Backend.State, initialValue, fence != nullptr ? &fence->Handle : nullptr);

		if (!native)
		{
			return Result<ExternalFenceHandle>::Failure(native.GetError());
		}
		ExternalFenceSlot slot;
		slot.Native = native.Value();
		slot.LastSignal = initialValue;
		const uint32 index = bNew ? static_cast<uint32>(m_State->m_ExternalFences.Size()) : m_State->m_FreeExternalFence;

		if (bNew)
		{
			const bool bAdded = m_State->m_ExternalFences.Add(slot);
			ASSERT(bAdded, "An external fence insertion must use its reserved capacity.");
		}
		else
		{
			slot.Generation = m_State->m_ExternalFences[index].Generation;
			m_State->m_FreeExternalFence = m_State->m_ExternalFences[index].NextFree;
			m_State->m_ExternalFences[index] = slot;
		}

		return Result<ExternalFenceHandle>::Success({ m_State->m_Owner, index, slot.Generation });
	}

	[[nodiscard]] Result<ExternalFence> Device::ExportExternalFence(ExternalFenceHandle handle)
	{
		CallScope call(*this, false);
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<ExternalFence>::Failure(m_State->m_FirstSubmissionFailure, "Rhi.DeviceFaulted", "The device no longer accepts external GPU ownership or work.");
		}

		Device::ExternalFenceSlot* slot = findExternalFence(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		return backend::ExportExternalFence(m_State->m_Backend.State, slot->Native);
	}

	[[nodiscard]] Result<uint64> Device::GetExternalFenceValue(ExternalFenceHandle handle)
	{
		CallScope call(*this, false);
		ExternalFenceSlot* slot = findExternalFence(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		return backend::GetExternalFenceValue(m_State->m_Backend.State, slot->Native);
	}

	[[nodiscard]] Result<void> Device::DestroyExternalFence(ExternalFenceHandle handle)
	{
		CallScope call(*this, false);
		ExternalFenceSlot* slot = findExternalFence(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		for (const SubmissionToken token : slot->LastUses)
		{
			if (!token.IsValid())
			{
				continue;
			}

			Result<bool> complete = IsSubmissionComplete(token);

			if (!complete)
			{
				return Result<void>::Failure(complete.GetError());
			}

			if (!complete.Value())
			{
				return Result<void>::Failure(externalInteropError(EErrorCode::Conflict, "GPU operations still reference the external fence."));
			}
		}

		backend::DestroyExternalFence(m_State->m_Backend.State, slot->Native);
		slot->Native = nullptr;

		if (slot->Generation != UINT32_MAX)
		{
			++slot->Generation;
			slot->NextFree = m_State->m_FreeExternalFence;
			m_State->m_FreeExternalFence = handle.Slot;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<SubmissionToken> Device::SignalExternalFence(EQueue queue, ExternalFenceHandle fence, uint64 value)
	{
		CallScope call(*this, false);

		return transferExternalResource({}, {}, queue, fence, value, false);
	}

	[[nodiscard]] Result<SubmissionToken> Device::WaitExternalFence(EQueue queue, ExternalFenceHandle fence, uint64 value)
	{
		CallScope call(*this, false);

		return transferExternalResource({}, {}, queue, fence, value, true);
	}

	[[nodiscard]] Result<SubmissionToken> Device::ReleaseExternalBuffer(const BufferHandle buffer, const EQueue queue, const ExternalFenceHandle fence, const uint64 value)
	{
		CallScope call(*this, false);
		ASSERT(buffer.IsValid(), "External buffer transfer requires a valid buffer handle.");

		return transferExternalResource(buffer, {}, queue, fence, value, false);
	}

	[[nodiscard]] Result<SubmissionToken> Device::AcquireExternalBuffer(const BufferHandle buffer, const EQueue queue, const ExternalFenceHandle fence, const uint64 value)
	{
		CallScope call(*this, false);
		ASSERT(buffer.IsValid(), "External buffer transfer requires a valid buffer handle.");

		return transferExternalResource(buffer, {}, queue, fence, value, true);
	}

	[[nodiscard]] Result<SubmissionToken> Device::ReleaseExternalTexture(const TextureHandle texture, const EQueue queue, const ExternalFenceHandle fence, const uint64 value)
	{
		CallScope call(*this, false);
		ASSERT(texture.IsValid(), "External texture transfer requires a valid texture handle.");

		return transferExternalResource({}, texture, queue, fence, value, false);
	}

	[[nodiscard]] Result<SubmissionToken> Device::AcquireExternalTexture(const TextureHandle texture, const EQueue queue, const ExternalFenceHandle fence, const uint64 value)
	{
		CallScope call(*this, false);
		ASSERT(texture.IsValid(), "External texture transfer requires a valid texture handle.");

		return transferExternalResource({}, texture, queue, fence, value, true);
	}

	[[nodiscard]] Result<SubmissionToken> Device::transferExternalResource(
		BufferHandle buffer,
		TextureHandle texture,
		EQueue queue,
		ExternalFenceHandle fence,
		uint64 value,
		bool bAcquire)
	{
		if (m_State->m_bSubmissionFaulted)
		{
			return Result<SubmissionToken>::Failure(m_State->m_FirstSubmissionFailure, "Rhi.DeviceFaulted", "The device no longer accepts external GPU ownership or work.");
		}

		Device::ExternalFenceSlot* synchronization = findExternalFence(fence);
		BufferSlot* bufferSlot = buffer.IsValid() ? findBuffer(buffer, true) : nullptr;
		TextureSlot* textureSlot = texture.IsValid() ? findTexture(texture, true) : nullptr;

		ASSERT(!(synchronization == nullptr || (buffer.IsValid() && bufferSlot == nullptr) || (texture.IsValid() && textureSlot == nullptr)), "RHI operations require valid handles for this device.");

		ASSERT(value != UINT64_MAX && (bAcquire || value > synchronization->LastSignal), "External signal values must increase and cannot equal UINT64_MAX.");

		if ((bufferSlot != nullptr &&
			 (!bufferSlot->bExternalShareable ||
			  bufferSlot->bExternalOwned != bAcquire ||
			  bufferSlot->RecordedUses != 0 ||
			  bufferSlot->bMapped ||
			  bufferSlot->PermanentState != ETrackedResourceState::Unknown)) ||
			(textureSlot != nullptr &&
			 (!textureSlot->bExternalShareable ||
			  textureSlot->bExternalOwned != bAcquire ||
			  textureSlot->RecordingSlot != UINT32_MAX ||
			  textureSlot->PermanentState != ETrackedResourceState::Unknown)))
		{
			return Result<SubmissionToken>::Failure(externalInteropError( EErrorCode::Conflict, "The resource is recorded, permanently constrained, or owned by the other participant."));
		}

		if (!bAcquire)
		{
			Result<uint64> current = GetExternalFenceValue(fence);

			if (!current)
			{
				return Result<SubmissionToken>::Failure(current.GetError());
			}

			if (value <= current.Value())
			{
				return Result<SubmissionToken>::Failure(externalInteropError( EErrorCode::InvalidArgument, "The signal value must exceed the externally visible timeline value."));
			}
		}

		Result<CommandListHandle> command = CreateCommandList(queue);

		if (!command)
		{
			return Result<SubmissionToken>::Failure(command.GetError());
		}

		CommandSlot* recorded = findCommand(command.Value());

		if (bufferSlot != nullptr)
		{
			if (!recorded->BufferUses.Add({ buffer, bufferSlot->Native }))
			{
				(void)DiscardCommandList(command.Value());

				return Result<SubmissionToken>::Failure(externalInteropError(EErrorCode::ExternalFailure, "Could not retain the shared buffer."));
			}
			++bufferSlot->RecordedUses;
		}

		if (textureSlot != nullptr)
		{
			if (!recorded->TextureUses.Add({ texture, textureSlot->Native }))
			{
				(void)DiscardCommandList(command.Value());

				return Result<SubmissionToken>::Failure(externalInteropError(EErrorCode::ExternalFailure, "Could not retain the shared texture."));
			}
			textureSlot->RecordingSlot = command.Value().Slot;
		}

		Result<void> transfer =
			backend::RecordExternalTransfer(m_State->m_Backend.State, recorded->Native,
				bufferSlot != nullptr ? bufferSlot->Native
				: textureSlot != nullptr ? textureSlot->Native
				: nullptr,
				textureSlot != nullptr, synchronization->Native, value, bAcquire);

		if (!transfer)
		{
			(void)DiscardCommandList(command.Value());

			return Result<SubmissionToken>::Failure(transfer.GetError());
		}

		if (!bAcquire && synchronization->LastSignalSubmission.IsValid() && synchronization->LastSignalSubmission.Queue != queue)
		{
			recorded->SubmitPrerequisite = synchronization->LastSignalSubmission;
		}

		const SubmitOutcome submitted = Submit(command.Value());

		if (submitted.Status == ESubmitStatus::Busy)
		{
			DiscardCommandList(command.Value());

			return Result<SubmissionToken>::Failure(EErrorCode::Conflict, "Rhi.ExternalAdmission", "Retry the external transfer when queue admission is available.");
		}
		if (submitted.Status == ESubmitStatus::Failed)
		{
			return Result<SubmissionToken>::Failure(m_State->m_FirstSubmissionFailure, "Rhi.DeviceFaulted", "The external transfer failed and the device stopped accepting work.");
		}

		synchronization->LastUses[static_cast<uint32>(queue)] = submitted.Token;

		if (!bAcquire)
		{
			synchronization->LastSignal = value;
			synchronization->LastSignalSubmission = submitted.Token;
		}

		if (bufferSlot != nullptr)
		{
			bufferSlot->bExternalOwned = !bAcquire;
		}

		if (textureSlot != nullptr)
		{
			textureSlot->bExternalOwned = !bAcquire;
		}

		return Result<SubmissionToken>::Success(submitted.Token);
	}
}
