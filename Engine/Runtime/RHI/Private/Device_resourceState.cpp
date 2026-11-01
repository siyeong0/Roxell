#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] bool Device::hasPreparedBuffer(BufferHandle handle) const noexcept
	{
		for (rhi::CommandSlot const& command : m_State->m_Commands)
		{
			if (!command.PreparedGroup)
			{
				continue;
			}
			for (rhi::PreparedRecordingPhase const& phase : command.PreparedGroup->Lists[command.PreparedIndex].Phases)
			{
				for (rhi::PreparedBufferAccessDesc const& access : phase.Buffers)
				{
					if (access.Buffer == handle)
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	[[nodiscard]] Result<void> Device::preparedMemoryBarrier(CommandSlot& command)
	{
		PreparedRecordingPhase* phase = preparedPhase(&command);
		ASSERT(!(phase == nullptr || command.bRendering || command.Queue == EQueue::Copy), "Prepared recording requires valid admitted input and capacity.");

		ASSERT(!(phase->Used.MemoryBarriers >= phase->Capacity.MemoryBarriers), "Prepared recording requires valid admitted input and capacity.");

		Result<void> result = recordNative([&] { return backend::RecordShaderMemoryBarrier(m_State->m_Backend.State, command.Native); });
		if (!result)
		{
			return preparedEmission(command, Move(result));
		}

		++phase->Used.MemoryBarriers;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::ShaderMemoryBarrier(CommandListHandle list)
	{
		CallScope call(*this, true, true);
		CommandSlot* const command = findCommand(list);
		if (command != nullptr && command->PreparedGroup)
		{
			return preparedMemoryBarrier(*command);
		}

		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(command->Queue != EQueue::Copy && !command->bRendering, "A shader memory barrier requires a graphics or compute list outside rendering.");

		Result<void> recorded = backend::RecordShaderMemoryBarrier(m_State->m_Backend.State, command->Native);

		if (!recorded)
		{
			poisonCommandList(list);
		}

		return recorded;
	}

	[[nodiscard]] Result<void> Device::ShaderMemoryBarrier(CommandListHandle list, BufferHandle buffer)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		BufferSlot* const slot = findBuffer(buffer);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!(command->Queue == EQueue::Copy || command->bRendering || !HasUsage(slot->Usage, EBufferUsage::Storage)),
			"A storage buffer and graphics or compute list outside rendering are required.");

		if (slot->bMapped)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.ShaderMemoryBarrier", "A mapped buffer cannot be used by a GPU barrier.");
		}

		if (!command->AddBufferUse({ buffer, slot->Native }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain the shader barrier buffer.");
		}
		++slot->RecordedUses;
		Result<void> recorded = backend::RecordShaderBufferBarrier(m_State->m_Backend.State, command->Native, slot->Native);

		if (!recorded)
		{
			poisonCommandList(list);
		}

		return recorded;
	}

	[[nodiscard]] Result<void> Device::ShaderMemoryBarrier(CommandListHandle list, TextureHandle texture)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!(command->Queue == EQueue::Copy || command->bRendering || !HasUsage(slot->Descriptor.Usage, ETextureUsage::Storage)),
			"A storage texture and graphics or compute list outside rendering are required.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the texture.");
		}

		if (!command->TextureUses.Add({ texture, slot->Native, false, ETrackedResourceState::Storage }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain the shader barrier texture.");
		}
		slot->RecordingSlot = list.Slot;
		Result<void> recorded = backend::RecordShaderTextureBarrier(m_State->m_Backend.State, command->Native, slot->Native);

		if (!recorded)
		{
			poisonCommandList(list);
		}

		return recorded;
	}

	[[nodiscard]] Result<void> Device::PrepareTextureForShaderAccess(CommandListHandle list, TextureHandle texture)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!(command->Queue == EQueue::Copy || command->bRendering || (!HasUsage(slot->Descriptor.Usage, ETextureUsage::ShaderRead) && !HasUsage(slot->Descriptor.Usage, ETextureUsage::Storage))),
			"A shader texture and graphics or compute list outside rendering are required.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the texture.");
		}

		const ETextureState state = slot->PermanentState == ETrackedResourceState::ShaderRead ? ETextureState::ShaderRead
			: HasUsage(slot->Descriptor.Usage, ETextureUsage::Storage) ? ETextureState::Storage
			: ETextureState::ShaderRead;

		ASSERT(slot->PermanentState == ETrackedResourceState::Unknown || slot->PermanentState == TextureUseState(state), "The prepared state must match the permanent texture state.");

		if (!command->TextureUses.Add(
			{ texture, slot->Native, false, TextureUseState(state), false, false, state == ETextureState::ShaderRead }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track the prepared texture.");
		}

		slot->RecordingSlot = list.Slot;
		Result<void> recorded = backend::RecordPrepareTextureForShaderAccess(m_State->m_Backend.State, command->Native, slot->Native, nullptr, state);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::PrepareTextureForShaderAccess(CommandListHandle list, TextureViewHandle view)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureViewSlot* const selected = findTextureView(view);

		ASSERT(!(command == nullptr || selected == nullptr), "RHI operations require valid handles for this device.");

		const TextureHandle handle = selected->Descriptor.Texture;
		TextureSlot* const slot = findTexture(handle);

		ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

		ASSERT(!(command->Queue == EQueue::Copy || command->bRendering), "A graphics or compute list outside rendering is required.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the texture.");
		}

		const ETextureState state = selected->Descriptor.Type == ETextureViewType::Storage ? ETextureState::Storage : ETextureState::ShaderRead;

		ASSERT(slot->PermanentState == ETrackedResourceState::Unknown || slot->PermanentState == TextureUseState(state), "The prepared view must match the permanent texture state.");

		if (!command->TextureUses.Add({ handle, slot->Native, false, TextureUseState(state), false, false, state == ETextureState::ShaderRead && selected->Descriptor.BaseMipLevel == 0 && selected->Descriptor.MipLevelCount == slot->Descriptor.MipLevels && selected->Descriptor.BaseArrayLayer == 0 && selected->Descriptor.ArrayLayerCount == slot->Descriptor.ArrayLayers }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track the prepared texture.");
		}

		slot->RecordingSlot = list.Slot;
		Result<void> recorded = backend::RecordPrepareTextureForShaderAccess(m_State->m_Backend.State, command->Native, slot->Native, &selected->Descriptor, state);

		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::setTextureStateInternal(
		CommandListHandle list,
		TextureHandle texture,
		const TextureSubresource* subresource,
		ETextureState state,
		bool bBeginTracking)
	{
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");
		if ((state == ETextureState::ResolveSource || state == ETextureState::ResolveDestination) &&
			((!IsColorTargetFormat(slot->Descriptor.Format) && !IsDepthFormat(slot->Descriptor.Format)) ||
					slot->Descriptor.Dimension != ETextureDimension::Texture2D ||
					(state == ETextureState::ResolveSource ? slot->Descriptor.SampleCount == 1 : slot->Descriptor.SampleCount != 1)))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ResolveState", "Resolve states require a supported 2D attachment and matching sample count.");
		}
		ETextureUsage required = ETextureUsage::None;

		switch (state)
		{
		case ETextureState::CopySource:
			required = ETextureUsage::CopySource;
			break;
		case ETextureState::CopyDestination:
			required = ETextureUsage::CopyDestination;
			break;
		case ETextureState::ResolveSource:
			required = ETextureUsage::CopySource;
			break;
		case ETextureState::ResolveDestination:
			required = ETextureUsage::CopyDestination;
			break;
		case ETextureState::ShaderRead:
			required = ETextureUsage::ShaderRead;
			break;
		case ETextureState::Storage:
			required = ETextureUsage::Storage;
			break;
		case ETextureState::Present:
			// Presentation boundaries are captured by complete prepared acquisition plans.
			break;
		case ETextureState::RenderTarget:
			required = ETextureUsage::RenderTarget;
			break;
		case ETextureState::DepthWrite:
			required = ETextureUsage::DepthStencil;
			break;
		case ETextureState::DepthRead:
			required = ETextureUsage::DepthStencil;
			break;
		case ETextureState::ShadingRateSource:
			required = ETextureUsage::ShadingRateSource;
			break;

		default:
		{
			break;
		}
		}

		const bool bClearDestination =
			state == ETextureState::CopyDestination &&
			((m_State->m_Kind == EBackend::Vulkan &&
							(HasUsage(slot->Descriptor.Usage, ETextureUsage::RenderTarget) ||
									HasUsage(slot->Descriptor.Usage, ETextureUsage::DepthStencil) ||
									HasUsage(slot->Descriptor.Usage, ETextureUsage::Storage))) ||
					(IsIntegerStorageFormat(slot->Descriptor.Format) && HasUsage(slot->Descriptor.Usage, ETextureUsage::RenderTarget)));

		ASSERT(!(required == ETextureUsage::None ||
				(!HasUsage(slot->Descriptor.Usage, required) && !bClearDestination) ||
				command->bRendering ||
				(command->Queue == EQueue::Copy && state != ETextureState::CopySource && state != ETextureState::CopyDestination) ||
				(command->Queue == EQueue::Compute &&
				 (state == ETextureState::RenderTarget ||
				  state == ETextureState::DepthWrite ||
				  state == ETextureState::DepthRead ||
				  state == ETextureState::ShadingRateSource ||
				  state == ETextureState::ResolveSource ||
				  state == ETextureState::ResolveDestination)) ||
				(subresource != nullptr &&
				 (!preparedTextureAspectSupported(slot->Descriptor, subresource->Aspect) ||
				  subresource->MipLevel >= slot->Descriptor.MipLevels ||
				  subresource->ArrayLayer >= slot->Descriptor.ArrayLayers))),
			"The state, usage, queue, or subresource is invalid.");

		if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the texture.");
		}

		ASSERT(slot->PermanentState == ETrackedResourceState::Unknown || TextureUseState(state) == slot->PermanentState, "The requested state must match the permanent texture state.");

		if (bBeginTracking)
		{
			ASSERT(slot->PermanentState == ETrackedResourceState::Unknown, "An initial tracking state requires a texture without a permanent state.");

			for (const RecordedTextureUse& use : command->TextureUses)
			{
				ASSERT(use.Handle != texture || use.bTrackingSeed, "Begin tracking before the first texture use.");
			}

			for (const RecordedTextureCopy& copy : command->TextureCopies)
			{
				ASSERT(copy.SourceHandle != texture && copy.DestinationHandle != texture, "Begin tracking before the first texture use.");
			}
		}

		if (!command->TextureUses.Add({ texture, slot->Native, bBeginTracking, TextureUseState(state) }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain the transitioned texture.");
		}
		slot->RecordingSlot = list.Slot;
		Result<void> recorded =
			bBeginTracking ? backend::RecordBeginTrackingTextureState(m_State->m_Backend.State, command->Native, slot->Native, subresource, state)
				: backend::RecordSetTextureState(m_State->m_Backend.State, command->Native, slot->Native, subresource, state);

		if (!recorded)
		{
			poisonCommandList(list);
		}

		return recorded;
	}

	[[nodiscard]] Result<void> Device::SetTextureState(CommandListHandle list, TextureHandle texture, ETextureState state)
	{
		CallScope call(*this, true);

		return setTextureStateInternal(list, texture, nullptr, state);
	}

	[[nodiscard]] Result<void> Device::SetPermanentTextureState(CommandListHandle list, TextureHandle texture, ETextureState state)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		TextureSlot* const slot = findTexture(texture);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(!slot->bSwapChainImage && !slot->bSamplerFeedback && slot->FeedbackRefs == 0 && slot->PermanentState == ETrackedResourceState::Unknown,
			"A permanent texture state requires an ordinary texture without an existing permanent state.");

		for (const RecordedTextureUse& use : command->TextureUses)
		{
			ASSERT(use.Handle != texture || !use.bPermanentTransition, "A command list sets each texture's permanent state once.");
		}

		Result<void> recorded = setTextureStateInternal(list, texture, nullptr, state);

		if (!recorded)
		{
			return recorded;
		}
		command->TextureUses.Back().bPermanentTransition = true;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::BeginTrackingTextureState(CommandListHandle list, TextureHandle texture, ETextureState state)
	{
		CallScope call(*this, true);

		return setTextureStateInternal(list, texture, nullptr, state, true);
	}

	[[nodiscard]] Result<void> Device::SetTextureState(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, ETextureState state)
	{
		CallScope call(*this, true);

		return setTextureStateInternal(list, texture, &subresource, state);
	}

	[[nodiscard]] Result<void> Device::BeginTrackingTextureState(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, ETextureState state)
	{
		CallScope call(*this, true);

		return setTextureStateInternal(list, texture, &subresource, state, true);
	}

	[[nodiscard]] Result<ETrackedResourceState> Device::GetTextureSubresourceState(CommandListHandle list, TextureHandle texture, TextureSubresource subresource)
	{
		CallScope call(*this, true);
		const CommandSlot* const command = findCommand(list);
		const TextureSlot* const slot = findTexture(texture);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(subresource.Aspect == ETextureAspect::Default && subresource.MipLevel < slot->Descriptor.MipLevels && subresource.ArrayLayer < slot->Descriptor.ArrayLayers, "The queried texture subresource must be valid.");

		if (!canReadTexture(*slot, list.Slot))
		{
			return Result<ETrackedResourceState>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list already references the texture.");
		}

		if (slot->PermanentState != ETrackedResourceState::Unknown)
		{
			return Result<ETrackedResourceState>::Success(slot->PermanentState);
		}

		return Result<ETrackedResourceState>::Success(
			backend::GetTextureSubresourceState(m_State->m_Backend.State, command->Native, slot->Native, subresource));
	}

	[[nodiscard]] Result<void> Device::setBufferStateInternal(CommandListHandle list, BufferHandle buffer, EBufferState state, bool bBeginTracking)
	{
		CommandSlot* const command = findCommand(list);
		BufferSlot* const slot = findBuffer(buffer);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");
		EBufferUsage required = EBufferUsage::None;

		switch (state)
		{
		case EBufferState::CopySource:
			required = EBufferUsage::CopySource;
			break;
		case EBufferState::CopyDestination:
			required = EBufferUsage::CopyDestination;
			break;
		case EBufferState::Uniform:
			required = EBufferUsage::Uniform;
			break;
		case EBufferState::Storage:
		case EBufferState::ReadOnlyStorage:
			required = EBufferUsage::Storage;
			break;
		case EBufferState::Vertex:
			required = EBufferUsage::Vertex;
			break;
		case EBufferState::Index:
			required = EBufferUsage::Index;
			break;
		case EBufferState::VertexAndReadOnlyStorage:
			required = EBufferUsage::Vertex;
			break;
		case EBufferState::VertexAndIndex:
			required = EBufferUsage::Vertex;
			break;
		case EBufferState::Indirect:
			required = EBufferUsage::Indirect;
			break;
		case EBufferState::AccelBuildInput:
			required = EBufferUsage::AccelBuildInput;
			break;
		case EBufferState::AccelStorage:
			required = EBufferUsage::AccelStorage;
			break;

		default:
		{
			break;
		}
		}

		const bool bStorageClearState = state == EBufferState::CopyDestination && HasUsage(slot->Usage, EBufferUsage::Storage);
		const bool bMicromapState = (state == EBufferState::AccelBuildInput && HasUsage(slot->Usage, EBufferUsage::MicromapBuildInput)) ||
			(state == EBufferState::AccelStorage && HasUsage(slot->Usage, EBufferUsage::MicromapStorage));

		ASSERT(!(required == EBufferUsage::None ||
				(!HasUsage(slot->Usage, required) && !bStorageClearState && !bMicromapState) ||
				(state == EBufferState::VertexAndIndex && !HasUsage(slot->Usage, EBufferUsage::Index)) ||
				(state == EBufferState::VertexAndReadOnlyStorage && (!HasUsage(slot->Usage, EBufferUsage::Storage) || command->Queue != EQueue::Graphics)) ||
				slot->Memory != EBufferMemory::Device ||
				slot->bMapped ||
				command->bRendering ||
				(command->Queue == EQueue::Copy && state != EBufferState::CopySource && state != EBufferState::CopyDestination) ||
				(command->Queue == EQueue::Compute && (state == EBufferState::Vertex || state == EBufferState::Index || state == EBufferState::VertexAndIndex))),
			"The state, usage, queue, or memory type is invalid.");

		if (bBeginTracking)
		{
			ASSERT(slot->PermanentState == ETrackedResourceState::Unknown, "An initial tracking state requires a buffer without a permanent state.");

			for (const RecordedBufferUse& use : command->BufferUses)
			{
				ASSERT(use.Handle != buffer || use.bTrackingSeed, "Begin tracking before the first buffer use.");
			}
		}
		ETrackedResourceState tracked = ETrackedResourceState::Unknown;

		switch (state)
		{
		case EBufferState::CopySource:
			tracked = ETrackedResourceState::CopySource;
			break;
		case EBufferState::CopyDestination:
			tracked = ETrackedResourceState::CopyDestination;
			break;
		case EBufferState::Uniform:
			tracked = ETrackedResourceState::Uniform;
			break;
		case EBufferState::Storage:
			tracked = ETrackedResourceState::Storage;
			break;
		case EBufferState::ReadOnlyStorage:
			tracked = ETrackedResourceState::ShaderRead;
			break;
		case EBufferState::Vertex:
			tracked = ETrackedResourceState::Vertex;
			break;
		case EBufferState::Index:
			tracked = ETrackedResourceState::Index;
			break;
		case EBufferState::VertexAndReadOnlyStorage:
			tracked = ETrackedResourceState::Vertex | ETrackedResourceState::ShaderRead;
			break;
		case EBufferState::VertexAndIndex:
			tracked = ETrackedResourceState::Vertex | ETrackedResourceState::Index;
			break;
		case EBufferState::Indirect:
			tracked = ETrackedResourceState::Indirect;
			break;
		case EBufferState::AccelBuildInput:
			tracked = ETrackedResourceState::AccelBuildInput;
			break;
		case EBufferState::AccelStorage:
			tracked = ETrackedResourceState::AccelStorage;
			break;

		default:
		{
			break;
		}
		}

		ASSERT(slot->PermanentState == ETrackedResourceState::Unknown || tracked == slot->PermanentState, "The requested state must match the permanent buffer state.");

		if (!command->AddBufferUse({ buffer, slot->Native, tracked, bBeginTracking, false, true }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain the transitioned buffer.");
		}
		++slot->RecordedUses;
		Result<void> recorded =
			bBeginTracking ? backend::RecordBeginTrackingBufferState(m_State->m_Backend.State, command->Native, slot->Native, state)
				: backend::RecordSetBufferState(m_State->m_Backend.State, command->Native, slot->Native, state);

		if (!recorded)
		{
			poisonCommandList(list);
		}

		return recorded;
	}

	[[nodiscard]] Result<void> Device::SetBufferState(CommandListHandle list, BufferHandle buffer, EBufferState state)
	{
		CallScope call(*this, true);

		return setBufferStateInternal(list, buffer, state);
	}

	[[nodiscard]] Result<void> Device::SetPermanentBufferState(CommandListHandle list, BufferHandle buffer, EBufferState state)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		BufferSlot* const slot = findBuffer(buffer);

		ASSERT(!(command == nullptr || slot == nullptr), "RHI operations require valid handles for this device.");

		ASSERT(slot->PermanentState == ETrackedResourceState::Unknown, "The buffer must not already have a permanent state.");

		for (const RecordedBufferUse& use : command->BufferUses)
		{
			ASSERT(use.Handle != buffer || !use.bPermanentTransition, "A command list sets each buffer's permanent state once.");
		}

		Result<void> recorded = setBufferStateInternal(list, buffer, state);

		if (!recorded)
		{
			return recorded;
		}
		command->BufferUses.Back().bPermanentTransition = true;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::CommitBarriers(CommandListHandle list)
	{
		CallScope call(*this, true);

		CommandSlot* const command = findCommand(list);
		ASSERT(command != nullptr, "RHI operations require valid handles for this device.");

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::BeginTrackingBufferState(CommandListHandle list, BufferHandle buffer, EBufferState state)
	{
		CallScope call(*this, true);

		return setBufferStateInternal(list, buffer, state, true);
	}

	[[nodiscard]] Result<void> Device::SetResourceStatesForBindingSet(CommandListHandle list, BindingSetHandle bindingSet)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		BindingSetSlot* const set = findBindingSet(bindingSet);

		ASSERT(!(command == nullptr || set == nullptr), "RHI operations require valid handles for this device.");

		const BindingLayoutSlot* const layout = findBindingLayout(set->Layout);

		ASSERT(layout != nullptr, "RHI operations require valid handles for this device.");

		const bool bCompute = layout->ComputeCount != 0 || layout->ComputeTextureCount != 0;

		ASSERT(!(command->bRendering || (bCompute ? command->Queue == EQueue::Copy : command->Queue != EQueue::Graphics)), "A binding set requires a compatible queue outside rendering.");

		const auto bufferState = [&](uint32 index) noexcept
			{
				if (bCompute)
				{
					const EComputeBindingType type = layout->ComputeBuffer(index).Type;

					return type == EComputeBindingType::UniformBuffer ? EBufferState::Uniform
						: type == EComputeBindingType::ReadOnlyBuffer ? EBufferState::ReadOnlyStorage
						: EBufferState::Storage;
				}

				const EGraphicsBufferBindingType type = layout->GraphicsBuffer(index).Type;

				return type == EGraphicsBufferBindingType::UniformBuffer ? EBufferState::Uniform
					: type == EGraphicsBufferBindingType::ReadOnlyBuffer ? EBufferState::ReadOnlyStorage
					: EBufferState::Storage;
			};
		for (uint32 index = 0; index < set->BufferCount; ++index)
		{
			const BufferSlot* const buffer = findBuffer(set->Buffers[index]);

			ASSERT(buffer != nullptr, "RHI operations require valid handles for this device.");

			const EBufferState state = bufferState(index);
			const EBufferUsage required = state == EBufferState::Uniform ? EBufferUsage::Uniform : EBufferUsage::Storage;
			const ETrackedResourceState tracked = state == EBufferState::Uniform ? ETrackedResourceState::Uniform
				: state == EBufferState::ReadOnlyStorage ? ETrackedResourceState::ShaderRead
				: ETrackedResourceState::Storage;

			ASSERT(!(!HasUsage(buffer->Usage, required) || buffer->Memory != EBufferMemory::Device || buffer->bMapped), "A buffer cannot enter its binding state.");

			ASSERT(buffer->PermanentState == ETrackedResourceState::Unknown || buffer->PermanentState == tracked, "A binding must match the permanent buffer state.");
		}

		for (uint32 kind = 0; kind < 2; ++kind)
		{
			const bool bStorage = kind != 0;
			const uint32 count = bStorage ? set->StorageTextureCount : set->TextureCount;
			const ETrackedResourceState tracked = bStorage ? ETrackedResourceState::Storage : ETrackedResourceState::ShaderRead;

			for (uint32 index = 0; index < count; ++index)
			{
				const TextureHandle texture = bStorage ? set->StorageTextures[index] : set->SampledTextures[index];
				const TextureSlot* const slot = findTexture(texture);

				ASSERT(slot != nullptr, "RHI operations require valid handles for this device.");

				if (slot->RecordingSlot != NumericLimits<uint32>::max() && slot->RecordingSlot != list.Slot)
				{
					poisonCommandList(list);

					return Result<void>::Failure(EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list references a binding texture.");
				}

				ASSERT(slot->bSamplerFeedback || slot->PermanentState == ETrackedResourceState::Unknown || slot->PermanentState == tracked, "A binding must match the permanent texture state.");
			}
		}

		usize textureUseCount = static_cast<usize>(set->TextureCount) + set->StorageTextureCount;

		for (uint32 kind = 0; kind < 2; ++kind)
		{
			const bool bStorage = kind != 0;
			const uint32 count = bStorage ? set->StorageTextureCount : set->TextureCount;

			for (uint32 index = 0; index < count; ++index)
			{
				const TextureViewHandle view = bStorage ? set->StorageViews[index] : set->SampledViews[index];

				if (!view.IsValid())
				{
					continue;
				}

				const TextureViewDesc& desc = m_State->m_TextureViews[view.Slot].Descriptor;
				const usize mipCount = desc.MipLevelCount;
				const usize layerCount = desc.ArrayLayerCount;

				ASSERT(!(mipCount == 0 || layerCount == 0 || mipCount > NumericLimits<usize>::max() / layerCount), "A binding view has an invalid subresource range.");

				const usize extraUses = mipCount * layerCount - 1;

				if (textureUseCount > NumericLimits<usize>::max() - extraUses)
				{
					poisonCommandList(list);

					return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingSetState", "The binding state record count is too large.");
				}
				textureUseCount += extraUses;
			}
		}

		if (command->BufferUses.Size() > NumericLimits<usize>::max() - set->BufferCount ||
			command->TextureUses.Size() > NumericLimits<usize>::max() - textureUseCount)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingSetState", "The command list cannot hold the binding state records.");
		}

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + set->BufferCount) || !command->TextureUses.Reserve(command->TextureUses.Size() + textureUseCount))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not reserve binding state records.");
		}

		for (uint32 index = 0; index < set->BufferCount; ++index)
		{
			Result<void> result = SetBufferState(list, set->Buffers[index], bufferState(index));

			if (!result)
			{
				return result;
			}
		}

		for (uint32 kind = 0; kind < 2; ++kind)
		{
			const bool bStorage = kind != 0;
			const uint32 count = bStorage ? set->StorageTextureCount : set->TextureCount;
			const ETextureState state = bStorage ? ETextureState::Storage : ETextureState::ShaderRead;

			for (uint32 index = 0; index < count; ++index)
			{
				const TextureHandle texture = bStorage ? set->StorageTextures[index] : set->SampledTextures[index];
				const TextureViewHandle view = bStorage ? set->StorageViews[index] : set->SampledViews[index];

				if (bStorage && m_State->m_Textures[texture.Slot].bSamplerFeedback)
				{
					Result<void> result = SetSamplerFeedbackTextureState(list, { texture.Owner, texture.Slot, texture.Generation },
						ESamplerFeedbackState::Storage);

					if (!result)
					{
						return result;
					}
					continue;
				}

				if (!view.IsValid())
				{
					Result<void> result = SetTextureState(list, texture, state);

					if (!result)
					{
						return result;
					}
					continue;
				}

				const TextureViewDesc& desc = m_State->m_TextureViews[view.Slot].Descriptor;

				for (uint32 mip = desc.BaseMipLevel; mip < desc.BaseMipLevel + desc.MipLevelCount; ++mip)
				{
					for (uint32 layer = desc.BaseArrayLayer; layer < desc.BaseArrayLayer + desc.ArrayLayerCount; ++layer)
					{
						Result<void> result = SetTextureState(list, texture, { mip, layer }, state);
						if (!result)
						{
							return result;
						}
					}
				}
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<ETrackedResourceState> Device::GetBufferState(CommandListHandle list, BufferHandle buffer)
	{
		CallScope call(*this, true);
		const CommandSlot* const command = findCommand(list);

		ASSERT(!(command == nullptr || findBuffer(buffer) == nullptr), "RHI operations require valid handles for this device.");

		for (usize index = command->BufferUses.Size(); index != 0; --index)
		{
			const RecordedBufferUse& use = command->BufferUses[index - 1];

			if (use.Handle == buffer && use.State != ETrackedResourceState::Unknown)
			{
				return Result<ETrackedResourceState>::Success(use.State);
			}
		}

		return Result<ETrackedResourceState>::Success(m_State->m_Buffers[buffer.Slot].PermanentState);
	}

	[[nodiscard]] bool Device::canReadTexture(const TextureSlot& texture, uint32 listSlot) noexcept
	{
		return texture.RecordingSlot == UINT32_MAX || texture.RecordingSlot == listSlot || (texture.RecordingSlot == SHARED_TEXTURE_READ_SLOT && texture.bSharedReadPrepared);
	}

	void Device::trackTextureReadOrWrite(CommandSlot& command, uint32 listSlot, TextureHandle handle, TextureSlot& texture, ETrackedResourceState state) noexcept
	{
		const bool bShared = state == ETrackedResourceState::ShaderRead && texture.bSharedReadPrepared && (texture.RecordingSlot == UINT32_MAX || texture.RecordingSlot == SHARED_TEXTURE_READ_SLOT);
		const bool bAdded = command.TextureUses.Add({ handle, texture.Native, false, state, false, bShared });
		ASSERT(bAdded, "Texture use capacity must be reserved before recording.");

		if (bShared)
		{
			++texture.SharedReadUses;
			texture.RecordingSlot = SHARED_TEXTURE_READ_SLOT;
		}
		else
		{
			texture.RecordingSlot = listSlot;
		}
	}
}
