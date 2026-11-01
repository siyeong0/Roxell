#include "DeviceState.h"
#include "PreparedProgramCache.h"

namespace rox::rhi
{
	using namespace core;

	void Device::fillPreparedNativeSets(
		const PreparedBinding& binding,
		NativeFixedDescriptor* const (&descriptors)[MAXIMUM_FIXED_BINDING_LAYOUTS],
		NativeFixedBindingSet(&sets)[MAXIMUM_FIXED_BINDING_LAYOUTS]) noexcept
	{
		const Span<const BindingSetHandle> handles{ binding.Sets, binding.SetCount };
		for (uint32 layoutIndex = 0; layoutIndex < handles.Size(); ++layoutIndex)
		{
			rhi::BindingSetSlot const& set = m_State->m_BindingSets[handles[layoutIndex].Slot];
			rhi::BindingLayoutSlot const& layout = m_State->m_BindingLayouts[set.Layout.Slot];
			const bool bImmutable = set.Cache && !set.Tables && set.AccelStructCount == 0;
			if (bImmutable && set.Cache->bDescriptorsReady)
			{
				sets[layoutIndex].Descriptors = { set.Cache->Descriptors, set.Cache->DescriptorCount };
				sets[layoutIndex].Cache = set.Cache;
				continue;
			}
			uint32 buffer = 0;
			uint32 sampled = 0;
			uint32 storage = 0;
			uint32 sampler = 0;
			uint32 accel = 0;
			uint32 count = 0;
			for (uint32 rangeIndex = 0; rangeIndex < layout.RangeCount; ++rangeIndex)
			{
				const BindingRangeDesc& range = layout.Range(rangeIndex);
				if (range.Source == EBindingRangeSource::DescriptorTable)
				{
					const BoundDescriptorTableRange* row = findBoundDescriptorTable(set, rangeIndex);
					ASSERT(row != nullptr, "An immutable binding set captures every table range.");
					sets[layoutIndex].Tables[sets[layoutIndex].TableCount++] = {
						rangeIndex,
						m_State->m_DescriptorTables[row->Table.Slot].Native,
						{set.Tables->UsedEntries.Data() + row->UsedOffset, row->UsedCount} };
					continue;
				}
				for (uint32 element = 0; element < range.DescriptorCount; ++element)
				{
					NativeFixedDescriptor& value = descriptors[layoutIndex][count++];
					value = NativeFixedDescriptor{};
					if (range.Type == EDescriptorType::Sampler)
					{
						value.Resource = m_State->m_Samplers[set.Samplers[sampler++].Slot].Native;
					}
					else if (range.Type == EDescriptorType::AccelerationStructure)
					{
						value.Resource = m_State->m_AccelStructs[set.AccelStructs[accel++].Slot].Native;
					}
					else if (range.Type == EDescriptorType::SampledTexture || range.Type == EDescriptorType::StorageTexture)
					{
						const bool bRead = range.Type == EDescriptorType::SampledTexture;
						const uint32 index = bRead ? sampled++ : storage++;
						const TextureHandle handle = bRead ? set.SampledTextures[index] : set.StorageTextures[index];
						const TextureViewHandle view = bRead ? set.SampledViews[index] : set.StorageViews[index];
						value.Resource = m_State->m_Textures[handle.Slot].Native;
						if (view.IsValid())
						{
							value.View = m_State->m_TextureViews[view.Slot].Native;
						}
					}
					else
					{
						value.Resource = m_State->m_Buffers[set.Buffers[buffer].Slot].Native;
						value.ByteOffset = set.BufferRanges[buffer].ByteOffset;
						value.ByteCount = set.BufferRanges[buffer].ByteCount;
						if (value.ByteCount == 0)
						{
							value.ByteCount = m_State->m_Buffers[set.Buffers[buffer].Slot].ByteCount - value.ByteOffset;
						}
						if (set.BufferViews[buffer].IsValid())
						{
							value.View = m_State->m_BufferViews[set.BufferViews[buffer].Slot].Native;
						}

						++buffer;
					}
				}
			}
			sets[layoutIndex].Descriptors = { descriptors[layoutIndex], count };
			if (bImmutable && count <= 32)
			{
				for (uint32 index = 0; index < count; ++index)
				{
					set.Cache->Descriptors[index] = descriptors[layoutIndex][index];
				}
				set.Cache->DescriptorCount = count;
				set.Cache->bDescriptorsReady = true;
				sets[layoutIndex].Descriptors = { set.Cache->Descriptors, count };
			}
			sets[layoutIndex].Cache = set.Cache;
		}
	}

	void Device::fillPreparedNativeSets(
		const PreparedBinding& binding,
		NativeFixedDescriptor(&descriptors)[MAXIMUM_FIXED_BINDING_LAYOUTS][32],
		NativeFixedBindingSet(&sets)[MAXIMUM_FIXED_BINDING_LAYOUTS]) noexcept
	{
		NativeFixedDescriptor* pointers[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
		for (uint32 index = 0; index < binding.SetCount; ++index)
		{
			pointers[index] = descriptors[index];
			ASSERT(nativeFixedDescriptorCount(m_State->m_BindingLayouts[m_State->m_BindingSets[binding.Sets[index].Slot].Layout.Slot]) <= 32, "Fixed descriptors fit their bounded scratch.");
		}
		fillPreparedNativeSets(binding, pointers, sets);
	}

	[[nodiscard]] Result<void> Device::PrepareCommandLists(Span<const CommandListPreparationDesc> descriptions)
	{
		return PrepareCommandListGroup({ descriptions, {}, EPreparedQueuePolicy::SingleQueue });
	}

	[[nodiscard]] Result<void> Device::PrepareCommandListGroup(const CommandListGroupPreparationDesc& description)
	{
		ROX_PROFILE_SCOPE("RHI", "PrepareCommandListGroup");
		CallScope call(*this, false, true);
		if (m_State->m_bSubmissionFaulted)
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}const Span<const CommandListPreparationDesc> descriptions = description.Lists;
		ASSERT(description.QueuePolicy <= EPreparedQueuePolicy::MultiQueue, "The prepared queue policy must be valid.");
		if (description.QueuePolicy == EPreparedQueuePolicy::MultiQueue && backend::HAS_SUBMIT_PREPARED_COMMAND_LIST == false)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		ASSERT(!descriptions.IsEmpty(), "A prepared command group requires at least one list.");
		if (descriptions.Size() > 65536 || backend::HAS_PREPARE_RECORDING_GROUP == false)
		{
			return preparedFailure(EErrorCode::Unsupported);
		}
		for (const CommandListPreparationDesc& list : descriptions)
		{
			const CommandSlot* command = findPreparedControlCommand(list.List);
			ASSERT(command == nullptr || !command->bPoisoned, "Discard a command list after recording fails.");
		}
		SharedPtr<PreparedRecordingGroup> group;
		if (!MakeSharedInto(m_State->m_Resource, &group, m_State->m_Resource))
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}

		Result<void> initialized = group->Initialize(description, m_State->m_PreparedPrograms);
		if (!initialized)
		{
			return initialized;
		}

		if (!group->PlannedAccels.Reserve(m_State->m_AccelStructs.Size()))
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}
		for (uint32 slot = 0; slot < m_State->m_AccelStructs.Size(); ++slot)
		{
			rhi::AccelStructSlot const& accel = m_State->m_AccelStructs[slot];
			(void)group->PlannedAccels.Add({ nullptr,
											{m_State->m_Owner, slot, accel.Generation},
											0,
											0,
											accel.BuildRevision,
											accel.bLive && accel.bBuilt && !accel.bUnavailable,
											accel.bAllowUpdate,
											accel.bAllowCompaction,
											false });
		}

		Array<uint32> tablePins(m_State->m_Resource);
		Array<uint32> rayPipelinePins(m_State->m_Resource);
		Array<uint8> timerPins(m_State->m_Resource);
		if (!tablePins.Reserve(m_State->m_ShaderTables.Size()) || !rayPipelinePins.Reserve(m_State->m_RayTracingPipelines.Size()))
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}
		for (usize slot = 0; slot < m_State->m_ShaderTables.Size(); ++slot)
		{
			(void)tablePins.Add(0);
		}
		for (usize slot = 0; slot < m_State->m_RayTracingPipelines.Size(); ++slot)
		{
			(void)rayPipelinePins.Add(0);
		}

		Array<NativePreparedList> native(m_State->m_Resource);
		if (!native.Reserve(group->Lists.Size()))
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}
		for (uint32 listIndex = 0; listIndex < group->Lists.Size(); ++listIndex)
		{
			rhi::PreparedRecordingList& list = group->Lists[listIndex];
			CommandSlot* command = findPreparedControlCommand(list.Handle);
			if (command == nullptr ||
				command->PreparedGroup ||
				!command->BufferUses.IsEmpty() ||
				!command->TextureUses.IsEmpty() ||
				!command->bPreparationEligible ||
				!command->bAutomaticBarriers ||
				!command->Copies.IsEmpty() ||
				!command->TextureCopies.IsEmpty() ||
				!command->PipelineUses.IsEmpty() ||
				!command->GraphicsPipelineUses.IsEmpty() ||
				!command->FramebufferUses.IsEmpty() ||
				!command->AccelUses.IsEmpty() ||
				!command->TimerUses.IsEmpty() ||
				command->MarkerDepth != 0 ||
				command->bRendering ||
				list.Phases.IsEmpty() ||
				list.Phases.Size() > 65536)
			{
				return preparedFailure(EErrorCode::Conflict);
			}
			if (listIndex == 0)
			{
				group->Queue = command->Queue;
			}
			if (description.QueuePolicy == EPreparedQueuePolicy::SingleQueue && command->Queue != group->Queue)
			{
				return preparedFailure(EErrorCode::Unsupported);
			}
			list.Queue = command->Queue;
			group->BuildDependencyFrontier(listIndex);
			for (uint32 previous = 0; previous < listIndex; ++previous)
			{
				ASSERT(group->Lists[previous].Handle != list.Handle, "Prepared command arguments must match their declared resources, ranges, and execution order.");
			}

			uint64 bufferUses = 0;
			uint64 textureUses = 0;
			uint64 bindingCount = 0;
			uint64 fixedResourceDescriptors = 0;
			uint64 fixedSamplerDescriptors = 0;
			uint64 commandCount = 0;
			uint64 attachmentCount = 0;
			uint64 indirectCount = 0;
			uint64 accelUses = 0;
			uint64 accelBuilds = 0;
			uint64 accelQueries = 0;
			uint64 accelCompacts = 0;
			uint32 timerCount = 0;
			if (!list.NativePhases.Reserve(list.Phases.Size()))
			{
				return preparedFailure(EErrorCode::ExternalFailure);
			}
			for (rhi::PreparedRecordingPhase& phase : list.Phases)
			{
				if (phase.GpuTimer.IsValid())
				{
					if (timerPins.IsEmpty() && !timerPins.Resize(m_State->m_Timers.Size()))
					{
						return preparedFailure(EErrorCode::ExternalFailure);
					}
					const TimerSlot* timer = findGpuTimer(phase.GpuTimer);
					if (timer == nullptr || timer->State != ETimerState::Idle || timerPins[phase.GpuTimer.Slot])
					{
						return preparedFailure(EErrorCode::Conflict);
					}
					timerPins[phase.GpuTimer.Slot] = 1;
					++timerCount;
				}
				Result<void> accelPrepared = prepareAccelPhase(*group, listIndex, static_cast<uint32>(&phase - list.Phases.Data()));
				if (!accelPrepared)
				{
					return accelPrepared;
				}
				if (!phase.NativeBuffers.Reserve(phase.Buffers.Size()) || !phase.NativeTextures.Reserve(phase.Textures.Size()) || !phase.BuildAccessLookup())
				{
					return preparedFailure(EErrorCode::ExternalFailure);
				}
				// Exact access-program handles and ranges preserve immutable descriptor checks.
				// Availability, mappings and external ownership remain live execution checks.
				const bool bValidatedAccess = phase.AccessProgram->bValidated && phase.AccessProgram->ValidatedQueue == list.Queue;
				for (rhi::PreparedBufferAccessDesc& access : phase.Buffers)
				{
					BufferSlot* buffer = findBuffer(access.Buffer);
					ASSERT(buffer != nullptr &&
						!buffer->bMapped &&
						buffer->PermanentState == ETrackedResourceState::Unknown &&
						(description.QueuePolicy != EPreparedQueuePolicy::MultiQueue || (!buffer->bExternalOwned && !buffer->bExternalShareable)) &&
						(access.State != EBufferState::AccelStorage || preparedAccelStorageDeclared(phase, access.Buffer)),
						"Prepared command arguments must match their declared resources, ranges, and execution order.");
					if (bValidatedAccess)
					{
						continue;
					}
					ASSERT(access.State <= EBufferState::VertexAndReadOnlyStorage &&
						(access.State != EBufferState::AccelStorage || preparedAccelStorageDeclared(phase, access.Buffer)) &&
						(buffer->Memory != EBufferMemory::CpuRead || access.State == EBufferState::CopyDestination) &&
						(buffer->Memory != EBufferMemory::CpuWrite ||
								(access.State != EBufferState::CopyDestination &&
										access.State != EBufferState::Storage &&
										access.State != EBufferState::AccelStorage &&
										access.State != EBufferState::AccelScratch)) &&
						(access.State != EBufferState::VertexAndReadOnlyStorage || list.Queue == EQueue::Graphics) &&
						(list.Queue != EQueue::Copy || access.State == EBufferState::CopySource || access.State == EBufferState::CopyDestination),
						"Prepared command arguments must match their declared resources, ranges, and execution order.");
					if (access.Range.ByteCount == 0 && access.Range.ByteOffset <= buffer->ByteCount)
					{
						access.Range.ByteCount = buffer->ByteCount - access.Range.ByteOffset;
					}

					const EBufferUsage usage =
						access.State == EBufferState::CopySource ? EBufferUsage::CopySource
						: access.State == EBufferState::CopyDestination ? EBufferUsage::CopyDestination
						: access.State == EBufferState::Uniform ? EBufferUsage::Uniform
						: access.State == EBufferState::AccelBuildInput ? EBufferUsage::AccelBuildInput | EBufferUsage::DeviceAddress
						: access.State == EBufferState::AccelStorage ? EBufferUsage::AccelStorage | EBufferUsage::DeviceAddress
						: access.State == EBufferState::AccelScratch ? EBufferUsage::Storage | EBufferUsage::DeviceAddress
						: access.State == EBufferState::Storage || access.State == EBufferState::ReadOnlyStorage ? EBufferUsage::Storage
						: access.State == EBufferState::Vertex ? EBufferUsage::Vertex
						: access.State == EBufferState::Index ? EBufferUsage::Index
						: access.State == EBufferState::VertexAndReadOnlyStorage ? EBufferUsage::Vertex | EBufferUsage::Storage
						: access.State == EBufferState::VertexAndIndex ? EBufferUsage::Vertex | EBufferUsage::Index
						: EBufferUsage::Indirect;
					ASSERT(!((static_cast<uint32>(buffer->Usage) & static_cast<uint32>(usage)) != static_cast<uint32>(usage)) &&
						Fits(buffer->ByteCount, access.Range.ByteOffset, access.Range.ByteCount),
						"Prepared command arguments must match their declared resources, ranges, and execution order.");
					ASSERT(!(phase.HasBufferStateConflict(access.Buffer)), "Resource access, aliases, and dependencies must match the prepared command declaration.");
					if (!phase.AccessProgram->bValidated)
					{
						(void)phase.NativeBuffers.Add({ buffer->Native, access.State });
					}
				}
				phase.CompleteBufferLookup();
				for (rhi::PreparedTextureAccessDesc const& access : phase.Textures)
				{
					TextureSlot* texture = findTexture(access.Texture);
					ASSERT(texture != nullptr && texture->PermanentState == ETrackedResourceState::Unknown, "Prepared texture access requires a live texture without permanent state.");
					if (texture->bSparseMappingUnavailable)
					{
						return preparedFailure(EErrorCode::ExternalFailure);
					}
					if (texture->bSparseMappingPrepared)
					{
						return preparedFailure(EErrorCode::Conflict);
					}
					if (texture->bSamplerFeedback ||
						(description.QueuePolicy == EPreparedQueuePolicy::MultiQueue && (texture->bExternalOwned || texture->bExternalShareable || texture->Descriptor.bSparse)))
					{
						return preparedFailure(EErrorCode::Unsupported);
					}

					if (texture->bSwapChainImage)
					{
						SwapChainSlot* owner = findSwapChain(texture->SwapChainOwner);
						if (owner == nullptr ||
							owner->AcquiredImage != access.Texture ||
							list.Queue != EQueue::Graphics ||
							access.BaseMipLevel != 0 ||
							access.MipLevelCount != 1 ||
							access.BaseArrayLayer != 0 ||
							access.ArrayLayerCount != 1 ||
							texture->AcquiredUnsubmittedUses ||
							texture->bAcquisitionReleaseAccepted)
						{
							return preparedFailure(EErrorCode::Conflict);
						}

						Result<void> planned = group->AcquiredImages.Add(
							access.Texture, texture->SwapChainOwner, owner->OwnerGeneration, owner->AcquireGeneration, listIndex,
							static_cast<uint32>(&phase - list.Phases.Data()), access.State);
						if (!planned)
						{
							return planned;
						}
					}

					if (bValidatedAccess)
					{
						continue;
					}
					ASSERT((!texture->Descriptor.bSparse || description.QueuePolicy != EPreparedQueuePolicy::MultiQueue) &&
						access.State <= ETextureState::Present &&
						(access.State != ETextureState::Present || texture->bSwapChainImage) &&
						access.State != ETextureState::ShadingRateSource &&
						preparedTextureAspectSupported(texture->Descriptor, access.Aspect) &&
						(list.Queue != EQueue::Copy || access.State == ETextureState::CopySource || access.State == ETextureState::CopyDestination) &&
						(list.Queue == EQueue::Graphics || (access.State != ETextureState::RenderTarget && access.State != ETextureState::DepthRead && access.State != ETextureState::DepthWrite && access.State != ETextureState::ResolveSource && access.State != ETextureState::ResolveDestination)) &&
						access.MipLevelCount != 0 &&
						access.BaseMipLevel < texture->Descriptor.MipLevels &&
						access.MipLevelCount <= texture->Descriptor.MipLevels - access.BaseMipLevel &&
						access.ArrayLayerCount != 0 &&
						access.BaseArrayLayer < texture->Descriptor.ArrayLayers &&
						access.ArrayLayerCount <= texture->Descriptor.ArrayLayers - access.BaseArrayLayer,
						"Prepared command arguments must match their declared resources, ranges, and execution order.");

					if (access.State == ETextureState::DepthWrite && (!GetFormatInfo(texture->Descriptor.Format).bHasDepth || texture->Descriptor.Dimension != ETextureDimension::Texture2D))
					{
						return preparedFailure(EErrorCode::Unsupported);
					}

					if ((access.State == ETextureState::ResolveSource || access.State == ETextureState::ResolveDestination) &&
						((!IsColorTargetFormat(texture->Descriptor.Format) && !IsDepthFormat(texture->Descriptor.Format)) ||
								texture->Descriptor.Dimension != ETextureDimension::Texture2D ||
								(access.State == ETextureState::ResolveSource ? texture->Descriptor.SampleCount == 1 : texture->Descriptor.SampleCount != 1)))
					{
						return preparedFailure(EErrorCode::Unsupported);
					}

					const ETextureUsage usage =
						access.State == ETextureState::CopySource || access.State == ETextureState::ResolveSource
						? ETextureUsage::CopySource
						: access.State == ETextureState::CopyDestination || access.State == ETextureState::ResolveDestination
						? ETextureUsage::CopyDestination
						: access.State == ETextureState::Storage ? ETextureUsage::Storage
						: access.State == ETextureState::RenderTarget || access.State == ETextureState::Present
						? ETextureUsage::RenderTarget
						: access.State == ETextureState::DepthWrite ? ETextureUsage::DepthStencil
						: access.State == ETextureState::DepthRead ? ETextureUsage::DepthStencil | ETextureUsage::ShaderRead
						: ETextureUsage::ShaderRead;
					ASSERT(!((static_cast<uint32>(texture->Descriptor.Usage) & static_cast<uint32>(usage)) != static_cast<uint32>(usage)),
						"Prepared command arguments must match their declared resources, ranges, and execution order.");

					for (const uint32 index : phase.FindTextureAccesses(access.Texture))
					{
						rhi::PreparedTextureAccessDesc const& other = phase.Textures[index];
						ASSERT(&access == &other || access.Texture != other.Texture || access.State == other.State || (access.Aspect != ETextureAspect::Default && other.Aspect != ETextureAspect::Default && access.Aspect != other.Aspect) || !(access.BaseMipLevel < uint64(other.BaseMipLevel) + other.MipLevelCount) || !(other.BaseMipLevel < uint64(access.BaseMipLevel) + access.MipLevelCount) || !(access.BaseArrayLayer < uint64(other.BaseArrayLayer) + other.ArrayLayerCount) || !(other.BaseArrayLayer < uint64(access.BaseArrayLayer) + access.ArrayLayerCount),
							"Resource access, aliases, and dependencies must match the prepared command declaration.");
					}
					if (!phase.AccessProgram->bValidated)
					{
						(void)phase.NativeTextures.Add({ texture->Native, access });
					}
				}
				phase.AccessProgram->ValidatedQueue = list.Queue;
				phase.AccessProgram->bValidated = true;
				for (rhi::PreparedRecordingResolve& resolve : phase.Resolves)
				{
					const TextureResolve& tuple = resolve.Description;
					const TextureSlot* source = findTexture(tuple.Source);
					const TextureSlot* destination = findTexture(tuple.Destination);
					ASSERT(source != nullptr && destination != nullptr && tuple.Source != tuple.Destination && list.Queue == EQueue::Graphics,
						"Prepared command arguments must match their declared resources, ranges, and execution order.");

					Result<void> valid = validateTextureResolve(tuple, *source, *destination, &resolve.Native);
					if (!valid)
					{
						return valid;
					}
					TextureViewDesc sourceExtent;
					sourceExtent.BaseMipLevel = tuple.SourceSubresource.MipLevel;
					sourceExtent.BaseArrayLayer = tuple.SourceSubresource.ArrayLayer;
					sourceExtent.Aspect = tuple.Mode == ETextureResolveMode::None ? ETextureAspect::Stencil : tuple.SourceSubresource.Aspect;
					TextureViewDesc destinationExtent = sourceExtent;
					destinationExtent.BaseMipLevel = tuple.DestinationSubresource.MipLevel;
					destinationExtent.BaseArrayLayer = tuple.DestinationSubresource.ArrayLayer;
					ASSERT(preparedTextureContains(phase, tuple.Source, ETextureState::ResolveSource, &sourceExtent) &&
						preparedTextureContains(phase, tuple.Destination, ETextureState::ResolveDestination, &destinationExtent),
						"Resource access, aliases, and dependencies must match the prepared command declaration.");
					if (tuple.StencilMode != ETextureResolveMode::None)
					{
						sourceExtent.Aspect = destinationExtent.Aspect = ETextureAspect::Stencil;
						ASSERT(preparedTextureContains(phase, tuple.Source, ETextureState::ResolveSource, &sourceExtent) &&
							preparedTextureContains(phase, tuple.Destination, ETextureState::ResolveDestination, &destinationExtent),
							"Resource access, aliases, and dependencies must match the prepared command declaration.");
					}
				}
				for (rhi::PreparedRecordingTextureBufferTransfer& transfer : phase.TextureBufferCopies)
				{
					const TextureBufferCopy& copy = transfer.Description.Copy;
					BufferSlot* buffer = findBuffer(copy.Buffer);
					TextureSlot* texture = findTexture(copy.Texture);
					ASSERT(buffer != nullptr &&
						texture != nullptr &&
						!buffer->bMapped &&
						transfer.Description.Direction <= ETextureBufferCopyDirection::TextureToBuffer &&
						copy.BufferOffsetBytes % 512 == 0,
						"Prepared command arguments must match their declared resources, ranges, and execution order.");

					const TextureDesc& desc = texture->Descriptor;
					const FormatInfo& format = GetFormatInfo(desc.Format);
					if (desc.Dimension != ETextureDimension::Texture2D || desc.SampleCount != 1)
					{
						return preparedFailure(EErrorCode::Unsupported);
					}
					ASSERT(preparedTextureAspectSupported(desc, copy.Subresource.Aspect) &&
						copy.Subresource.MipLevel < desc.MipLevels &&
						copy.Subresource.ArrayLayer < desc.ArrayLayers &&
						(!format.bHasDepth || list.Queue == EQueue::Graphics),
						"Prepared command arguments must match their declared resources, ranges, and execution order.");

					const bool bUpload = transfer.Description.Direction == ETextureBufferCopyDirection::BufferToTexture;
					const EBufferState bufferState = bUpload ? EBufferState::CopySource : EBufferState::CopyDestination;
					const ETextureState textureState = bUpload ? ETextureState::CopyDestination : ETextureState::CopySource;
					ASSERT(HasUsage(buffer->Usage, bUpload ? EBufferUsage::CopySource : EBufferUsage::CopyDestination) &&
						HasUsage(desc.Usage, bUpload ? ETextureUsage::CopyDestination : ETextureUsage::CopySource),
						"Prepared command arguments must match their declared resources, ranges, and execution order.");

					Result<TextureBufferFootprint> footprint = calculateTextureBufferFootprint(desc, copy.Subresource);
					if (!footprint)
					{
						return preparedFailure(footprint.GetError().Code);
					}
					ASSERT(Fits(buffer->ByteCount, copy.BufferOffsetBytes, footprint.Value().ByteCount),
						"Prepared command arguments must match their declared resources, ranges, and execution order.");
					TextureViewDesc extent;
					extent.BaseMipLevel = copy.Subresource.MipLevel;
					extent.BaseArrayLayer = copy.Subresource.ArrayLayer;
					extent.Aspect = copy.Subresource.Aspect;
					ASSERT(preparedBufferContains(phase, copy.Buffer, bufferState, copy.BufferOffsetBytes, footprint.Value().ByteCount) &&
						preparedTextureContains(phase, copy.Texture, textureState, &extent),
						"Resource access, aliases, and dependencies must match the prepared command declaration.");

					transfer.Native = { buffer->Native, texture->Native, copy.Subresource, footprint.Value(), copy.BufferOffsetBytes };
					if constexpr (backend::HAS_PREPARE_TEXTURE_BUFFER_COPY)
					{
						Result<void> captured = backend::PrepareTextureBufferCopy(m_State->m_Backend.State, transfer.Native);
						if (!captured)
						{
							return preparedFailure(captured.GetError().Code);
						}
					}
					transfer.Native.bPrepared = true;
				}
				for (rhi::PreparedBinding& binding : phase.Bindings)
				{
					binding.Execution = m_State->m_PreparedPrograms.FindBinding(binding);
					Result<void> valid = validatePreparedBinding(*command, phase, binding, list.Queue);
					if (!valid)
					{
						return valid;
					}
					if (!binding.Execution && !binding.DeviceHeaps && !binding.ShaderTable.IsValid())
					{
						bool bFixed = true;
						for (uint32 i = 0; i < binding.SetCount; ++i)
						{
							rhi::BindingSetSlot const& set = m_State->m_BindingSets[binding.Sets[i].Slot];
							bFixed &= !set.Tables && set.AccelStructCount == 0;
						}
						if (bFixed)
						{
							SharedPtr<PreparedBindingProgram> candidate;
							if (!MakeSharedInto(m_State->m_Resource, &candidate))
							{
								return preparedFailure(EErrorCode::ExternalFailure);
							}
							candidate->Shape = binding;
							candidate->Shape.Execution.Reset();
							binding.Execution = candidate;
						}
					}
					if (binding.Execution && binding.Execution->bDescriptorCountsReady)
					{
						fixedResourceDescriptors += binding.Execution->ResourceDescriptorCount;
						fixedSamplerDescriptors += binding.Execution->SamplerDescriptorCount;
					}
					else
					{
						uint64 resources = 0, samplers = 0;
						for (uint32 setIndex = 0; setIndex < binding.SetCount; ++setIndex)
						{
							rhi::BindingLayoutSlot const& layout = m_State->m_BindingLayouts[m_State->m_BindingSets[binding.Sets[setIndex].Slot].Layout.Slot];
							for (uint32 rangeIndex = 0; rangeIndex < layout.RangeCount; ++rangeIndex)
							{
								const BindingRangeDesc& range = layout.Range(rangeIndex);
								if (range.Source == EBindingRangeSource::Fixed)
								{
									(range.Type == EDescriptorType::Sampler ? samplers : resources) += range.DescriptorCount;
								}
							}
						}
						fixedResourceDescriptors += resources;
						fixedSamplerDescriptors += samplers;
						if (binding.Execution)
						{
							binding.Execution->ResourceDescriptorCount = resources;
							binding.Execution->SamplerDescriptorCount = samplers;
							binding.Execution->bDescriptorCountsReady = true;
						}
					}
				}
				for (rhi::PreparedRecordingAttachment const& attachment : phase.Attachments)
				{
					FramebufferSlot* framebuffer = findFramebuffer(attachment.Framebuffer);
					ASSERT(framebuffer != nullptr && list.Queue == EQueue::Graphics, "Prepared attachments require a live framebuffer and graphics list.");
					if ((framebuffer->ColorCount == 0 && !framebuffer->Depth.IsValid()) ||
						framebuffer->ColorCount > 8 ||
						framebuffer->ColorCount > m_State->m_Backend.Capabilities.Limits.MaximumColorAttachments ||
						framebuffer->ShadingRateImage.IsValid())
					{
						return preparedFailure(EErrorCode::Unsupported);
					}

					ASSERT(attachment.ColorCount == framebuffer->ColorCount, "Prepared color load count matches the framebuffer.");
					ASSERT(!attachment.bReadOnlyDepth || (framebuffer->Depth.IsValid() && attachment.DepthLoad == EDepthLoadOp::Load),
						"Read-only depth requires an existing attachment and load operation.");
					ASSERT(framebuffer->Depth.IsValid() || attachment.DepthLoad == EDepthLoadOp::Load, "A prepared pass without depth cannot clear depth.");
					ASSERT(!attachment.bReadOnlyStencil || attachment.StencilLoad == EDepthLoadOp::Load, "Read-only stencil requires a load operation.");
					ASSERT
					((framebuffer->Depth.IsValid() && GetFormatInfo(m_State->m_Textures[framebuffer->Depth.Slot].Descriptor.Format).bHasStencil) ||
						(attachment.StencilLoad == EDepthLoadOp::Load && !attachment.bReadOnlyStencil),
						"Prepared stencil operations require a stencil attachment.");

					TextureViewDesc extent;
					for (uint32 color = 0; color < framebuffer->ColorCount; ++color)
					{
						const TextureDesc& target = m_State->m_Textures[framebuffer->Colors[color].Slot].Descriptor;
						const FormatInfo& format = GetFormatInfo(target.Format);
						if ((format.Kind != EFormatKind::Normalized && format.Kind != EFormatKind::Float && format.Kind != EFormatKind::Integer) ||
							format.BlockSize != 1 ||
							target.Dimension != ETextureDimension::Texture2D ||
							!HasFormatSupport(queryNativeFormatSupport(target.Format), EFormatSupport::RenderTarget))
						{
							return preparedFailure(EErrorCode::Unsupported);
						}

						Result<uint32> samples = queryNativeTextureSampleCounts(target);
						if (!samples || (samples.Value() & target.SampleCount) == 0)
						{
							return preparedFailure(!samples ? samples.GetError().Code : EErrorCode::Unsupported);
						}
						extent.BaseMipLevel = framebuffer->ColorSubresources[color].MipLevel;
						extent.BaseArrayLayer = framebuffer->ColorSubresources[color].ArrayLayer;
						ASSERT(preparedTextureContains(phase, framebuffer->Colors[color], ETextureState::RenderTarget, &extent),
							"Resource access, aliases, and dependencies must match the prepared command declaration.");
					}

					if (framebuffer->Depth.IsValid())
					{
						const TextureDesc& depth = m_State->m_Textures[framebuffer->Depth.Slot].Descriptor;
						if (!GetFormatInfo(depth.Format).bHasDepth || depth.Dimension != ETextureDimension::Texture2D)
						{
							return preparedFailure(EErrorCode::Unsupported);
						}

						extent.BaseMipLevel = framebuffer->DepthSubresource.MipLevel;
						extent.BaseArrayLayer = framebuffer->DepthSubresource.ArrayLayer;
						extent.Aspect = ETextureAspect::Depth;
						Result<uint32> samples = queryNativeTextureSampleCounts(depth);
						if (!samples || (samples.Value() & depth.SampleCount) == 0)
						{
							return preparedFailure(!samples ? samples.GetError().Code : EErrorCode::Unsupported);
						}
						const ETextureState depthState = attachment.bReadOnlyDepth ? ETextureState::DepthRead : ETextureState::DepthWrite;
						ASSERT(preparedTextureContains(phase, framebuffer->Depth, depthState, &extent), "Resource access, aliases, and dependencies must match the prepared command declaration.");
						if (GetFormatInfo(depth.Format).bHasStencil)
						{
							extent.Aspect = ETextureAspect::Stencil;
							ASSERT(preparedTextureContains(phase, framebuffer->Depth, attachment.bReadOnlyStencil ? ETextureState::DepthRead : ETextureState::DepthWrite, &extent),
								"Resource access, aliases, and dependencies must match the prepared command declaration.");
						}
						extent.Aspect = ETextureAspect::Default;
					}
				}
				GraphicsPipelineHandle previousCompatiblePipeline;
				for (rhi::PreparedBinding const& binding : phase.Bindings)
				{
					if (!binding.GraphicsPipeline.IsValid())
					{
						continue;
					}

					if (binding.GraphicsPipeline == previousCompatiblePipeline)
					{
						continue;
					} const GraphicsPipelineSlot& pipeline = m_State->m_GraphicsPipelines[binding.GraphicsPipeline.Slot];
					if (phase.Attachments.IsEmpty() ||
						pipeline.ViewportCount != 1 ||
						pipeline.ColorCount > 8 ||
						(pipeline.DepthFormat != EFormat::UNKNOWN && !GetFormatInfo(pipeline.DepthFormat).bHasDepth))
					{
						return preparedFailure(EErrorCode::Unsupported);
					}

					bool bCompatible = false;
					for (rhi::PreparedRecordingAttachment const& attachment : phase.Attachments)
					{
						rhi::FramebufferSlot const& framebuffer = m_State->m_Framebuffers[attachment.Framebuffer.Slot];
						const TextureHandle sampleTarget = framebuffer.ColorCount != 0 ? framebuffer.Colors[0] : framebuffer.Depth;
						const TextureDesc& target = m_State->m_Textures[sampleTarget.Slot].Descriptor;
						const rhi::EFormat depthFormat = framebuffer.Depth.IsValid() ? m_State->m_Textures[framebuffer.Depth.Slot].Descriptor.Format : EFormat::UNKNOWN;
						if (pipeline.ColorCount != framebuffer.ColorCount ||
							pipeline.SampleCount != target.SampleCount ||
							pipeline.DepthFormat != depthFormat ||
							((attachment.bReadOnlyDepth && pipeline.bWritesDepth) || (attachment.bReadOnlyStencil && pipeline.bWritesStencil)))
						{
							continue;
						}
						bool bColorsMatch = true;
						for (uint32 color = 0; color < framebuffer.ColorCount; ++color)
						{
							if (pipeline.ColorFormats[color] != m_State->m_Textures[framebuffer.Colors[color].Slot].Descriptor.Format)
							{
								bColorsMatch = false;
								break;
							}
						}
						if (bColorsMatch)
						{
							bCompatible = true;
							break;
						}
					}
					ASSERT(bCompatible, "Resource access, aliases, and dependencies must match the prepared command declaration.");
					previousCompatiblePipeline = binding.GraphicsPipeline;
				}
				for (rhi::PreparedBinding const& binding : phase.Bindings)
				{
					if (binding.ShaderTable.IsValid())
					{
						rhi::ShaderTableSlot const& table = m_State->m_ShaderTables[binding.ShaderTable.Slot];
						uint32& count = tablePins[binding.ShaderTable.Slot];
						uint32& pipelineCount = rayPipelinePins[table.Pipeline.Slot];
						if (count == UINT32_MAX ||
							pipelineCount == UINT32_MAX ||
							table.PreparedReferences >= UINT32_MAX - count ||
							m_State->m_RayTracingPipelines[table.Pipeline.Slot].RecordedUses >= UINT32_MAX - pipelineCount)
						{
							return preparedFailure(EErrorCode::Conflict);
						}
						++count;
						++pipelineCount;
					}
				}
				for (rhi::PreparedRecordingIndirect& indirect : phase.Indirects)
				{
					Result<void> valid = validatePreparedIndirect(phase, indirect, list.Queue);
					if (!valid)
					{
						return valid;
					}
				}

				bufferUses += phase.Buffers.Size();
				textureUses += phase.Textures.Size();
				bindingCount += phase.Bindings.Size();
				attachmentCount += phase.Attachments.Size();
				indirectCount += phase.Indirects.Size();
				accelUses += phase.AccelStructs.Size();
				accelBuilds += phase.AccelBuilds.Size();
				accelQueries += phase.AccelCompactionQueries.Size();
				accelCompacts += phase.AccelCompactions.Size();
				const PreparedCommandCapacity& capacity = phase.Capacity;
				commandCount += uint64(capacity.BufferCopies) + capacity.TextureCopies + capacity.Dispatches + capacity.Draws +
					capacity.RenderPasses + capacity.ViewportChanges + capacity.ScissorChanges + capacity.MemoryBarriers +
					capacity.RayDispatches + capacity.AccelBuilds + capacity.AccelCompactionQueries + capacity.AccelCompactions;
				(void)list.NativePhases.Add({ phase.NativeBuffers.View(), phase.NativeTextures.View() });
			}
			if (bufferUses > UINT32_MAX ||
				textureUses > UINT32_MAX ||
				bindingCount > UINT32_MAX ||
				commandCount > UINT32_MAX ||
				indirectCount > UINT32_MAX ||
				accelUses > UINT32_MAX ||
				accelBuilds > UINT32_MAX ||
				accelQueries > UINT32_MAX ||
				accelCompacts > UINT32_MAX ||
				!command->AccelUses.Reserve(static_cast<usize>(accelUses)) ||
				!command->TimerUses.Reserve(timerCount) ||
				!command->DescriptorTableUses.Reserve(static_cast<usize>(bindingCount) * 64) ||
				!command->RayPipelineUses.Reserve(static_cast<usize>(bindingCount)) ||
				!command->BufferUses.Reserve(static_cast<usize>(bufferUses)) ||
				!command->TextureUses.Reserve(static_cast<usize>(textureUses)) ||
				!command->PipelineUses.Reserve(static_cast<usize>(bindingCount)) ||
				!command->GraphicsPipelineUses.Reserve(static_cast<usize>(bindingCount)) ||
				!command->FramebufferUses.Reserve(static_cast<usize>(attachmentCount)) ||
				!command->SamplerUses.Reserve(static_cast<usize>(bindingCount) * 32) ||
				!command->TextureViewUses.Reserve(static_cast<usize>(bindingCount) * 64))
			{
				return preparedFailure(EErrorCode::ExternalFailure);
			}
			NativePreparedList nativeList;
			nativeList.Commands = command->Native;
			nativeList.Phases = list.NativePhases.View();
			nativeList.BindingCount = static_cast<uint32>(bindingCount);
			nativeList.FixedResourceDescriptorCount = fixedResourceDescriptors;
			nativeList.FixedSamplerDescriptorCount = fixedSamplerDescriptors;
			nativeList.CommandCount = static_cast<uint32>(commandCount);
			nativeList.IndirectCount = static_cast<uint32>(indirectCount);
			nativeList.AccelBuildCount = static_cast<uint32>(accelBuilds);
			nativeList.AccelQueryCount = static_cast<uint32>(accelQueries);
			nativeList.AccelCompactCount = static_cast<uint32>(accelCompacts);
			nativeList.Queue = list.Queue;
			Copy(Begin(list.DependencyFrontier), End(list.DependencyFrontier),
				Begin(nativeList.DependencyFrontier));
			(void)native.Add(nativeList);
		}
		for (rhi::CommandSlot const& command : m_State->m_Commands)
		{
			if (!command.bLive || command.PreparedGroup)
			{
				continue;
			}
			for (rhi::PreparedRecordingList const& list : group->Lists)
			{
				for (rhi::PreparedRecordingPhase const& phase : list.Phases)
				{
					for (rhi::PreparedBufferAccessDesc const& access : phase.Buffers)
					{
						for (rhi::RecordedBufferUse const& use : command.BufferUses)
						{
							if (access.Buffer == use.Handle)
							{
								return preparedFailure(EErrorCode::Conflict);
							}
						}
					}
					for (rhi::PreparedTextureAccessDesc const& access : phase.Textures)
					{
						for (rhi::RecordedTextureUse const& use : command.TextureUses)
						{
							if (access.Texture == use.Handle)
							{
								return preparedFailure(EErrorCode::Conflict);
							}
						}
					}
				}
			}
		}

		// Conflicting outstanding preparations cannot supply another group's hypothetical entry state.
		for (rhi::CommandSlot const& live : m_State->m_Commands)
		{
			if (!live.bLive || !live.PreparedGroup)
			{
				continue;
			}
			for (rhi::PreparedRecordingList const& list : group->Lists)
			{
				for (rhi::PreparedRecordingPhase const& phase : list.Phases)
				{
					rhi::PreparedRecordingList const& prior = live.PreparedGroup->Lists[live.PreparedIndex];
					for (rhi::PreparedRecordingPhase const& oldPhase : prior.Phases)
					{
						for (rhi::PreparedBufferAccessDesc const& access : phase.Buffers)
						{
							for (rhi::PreparedBufferAccessDesc const& old : oldPhase.Buffers)
							{
								if (access.Buffer == old.Buffer &&
									(access.State != old.State ||
									 access.State == EBufferState::CopyDestination ||
									 access.State == EBufferState::Storage ||
									 access.State == EBufferState::AccelScratch ||
									 (access.State == EBufferState::AccelStorage && (preparedAccelStorageWrites(phase, access.Buffer) || preparedAccelStorageWrites(oldPhase, old.Buffer)))))
								{
									return preparedFailure(EErrorCode::Conflict);
								}
							}
						}
						for (rhi::PreparedTextureAccessDesc const& access : phase.Textures)
						{
							for (rhi::PreparedTextureAccessDesc const& old : oldPhase.Textures)
							{
								if (access.Texture == old.Texture)
								{
									if (access.State != old.State ||
										(access.State != ETextureState::CopySource && access.State != ETextureState::ShaderRead && access.State != ETextureState::ResolveSource))
									{
										return preparedFailure(EErrorCode::Conflict);
									}

									rhi::CommandSlot const& fresh = m_State->m_Commands[list.Handle.Slot];
									for (uint32 layer = 0; layer < access.ArrayLayerCount; ++layer)
									{
										for (uint32 mip = 0; mip < access.MipLevelCount; ++mip)
										{
											if (m_State->m_Kind == EBackend::Vulkan &&
												backend::GetTextureSubresourceState( m_State->m_Backend.State, fresh.Native, m_State->m_Textures[access.Texture.Slot].Native, { access.BaseMipLevel + mip, access.BaseArrayLayer + layer }) != expectedVulkanSharedReadState(m_State->m_Textures[access.Texture.Slot].Descriptor, access.State))
											{
												return preparedFailure(EErrorCode::Conflict);
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}

		ASSERT(group->AcquiredImages.HasCompleteReleases(), "Prepared command arguments must match their declared resources, ranges, and execution order.");
		if (!group->AcquiredImages.GetEntries().IsEmpty() &&
			(m_State->m_AcquiredImageUses.Size() > NumericLimits<usize>::max() - m_State->m_Commands.Size() || !m_State->m_AcquiredImageUses.Reserve(m_State->m_AcquiredImageUses.Size() + m_State->m_Commands.Size())))
		{
			return preparedFailure(EErrorCode::ExternalFailure);
		}

		Result<void> dependenciesValid = validatePreparedDependencies(*group);
		if (!dependenciesValid)
		{
			return dependenciesValid;
		}

		Result<void> prepared = backend::PrepareRecordingGroup(m_State->m_Backend.State, native.View());
		if (!prepared)
		{
			for (rhi::NativePreparedList const& item : native)
			{
				backend::ResetPreparedCommandList(m_State->m_Backend.State, item.Commands);
			}

			return prepared;
		}
		NativeFixedDescriptor fixedDescriptors[MAXIMUM_FIXED_BINDING_LAYOUTS][32]{};
		for (rhi::PreparedRecordingList& list : group->Lists)
		{
			rhi::CommandSlot& command = m_State->m_Commands[list.Handle.Slot];
			for (rhi::PreparedRecordingPhase& phase : list.Phases)
			{
				Result<void> accelCaptured = capturePreparedAccelPhase(command, phase);
				if (!accelCaptured)
				{
					for (rhi::NativePreparedList const& item : native)
					{
						backend::ResetPreparedCommandList(m_State->m_Backend.State, item.Commands);
					}

					return accelCaptured;
				}
				for (rhi::PreparedBinding& binding : phase.Bindings)
				{
					const auto capture = [&]() -> Result<uint32>
						{
							// A retained D3D12 program consumes only the current command-list owner.
							// Do not construct descriptor/table scratch that this path never reads.
							if (m_State->m_Kind == EBackend::D3D12 && binding.Execution && binding.Execution->Program)
							{
								return backend::PrepareBinding(m_State->m_Backend.State, command.Native, binding.NativePipeline,
									binding.GraphicsPipeline.IsValid(), {}, phase.NativeTextures.View(),
									binding.NativeVertexInput, {}, &binding.Execution->Program);
							}
							NativeFixedBindingSet sets[MAXIMUM_FIXED_BINDING_LAYOUTS]{};
							fillPreparedNativeSets(binding, fixedDescriptors, sets);
							NativeDescriptorHeapBinding heaps[32]{};
							fillNativeDeviceHeaps(binding.DeviceHeaps.Get(), heaps);

							return binding.ShaderTable.IsValid()
								? capturePreparedRayBinding(command, phase, binding, { sets, binding.SetCount })
								: backend::PrepareBinding(m_State->m_Backend.State, command.Native, binding.NativePipeline,
									binding.GraphicsPipeline.IsValid(), { sets, binding.SetCount },
									phase.NativeTextures.View(), binding.NativeVertexInput,
									{ heaps, binding.DeviceHeaps ? binding.DeviceHeaps->Ranges.Size() : 0 },
									binding.Execution ? &binding.Execution->Program : nullptr);
						};
					Result<uint32> result = capture();
					if (!result)
					{
						for (rhi::NativePreparedList const& item : native)
						{
							backend::ResetPreparedCommandList(m_State->m_Backend.State, item.Commands);
						}

						return Result<void>::Failure(Move(result.GetError()));
					}
					binding.NativeIndex = result.Value();
				}
				for (rhi::PreparedRecordingResolve& resolve : phase.Resolves)
				{
					if constexpr (backend::HAS_PREPARE_TEXTURE_RESOLVE)
					{
						Result<void> captured = backend::PrepareTextureResolve(m_State->m_Backend.State, command.Native, resolve.Native);
						if (!captured)
						{
							for (rhi::NativePreparedList const& item : native)
							{
								backend::ResetPreparedCommandList(m_State->m_Backend.State, item.Commands);
							}

							return Result<void>::Failure(Move(captured.GetError()));
						}
					}
				}
				for (rhi::PreparedRecordingIndirect& indirect : phase.Indirects)
				{
					Result<uint32> captured = backend::PrepareIndirect(m_State->m_Backend.State, command.Native, indirect.Native);
					if (!captured)
					{
						for (rhi::NativePreparedList const& item : native)
						{
							backend::ResetPreparedCommandList(m_State->m_Backend.State, item.Commands);
						}

						return Result<void>::Failure(Move(captured.GetError()));
					}
					indirect.Native.NativeIndex = captured.Value();
				}
			}
		}
		for (uint32 index = 0; index < group->Lists.Size(); ++index)
		{
			rhi::CommandSlot& command = m_State->m_Commands[group->Lists[index].Handle.Slot];
			command.PreparedGroup = group;
			command.PreparedIndex = index;
			for (rhi::PreparedRecordingPhase const& phase : group->Lists[index].Phases)
			{
				if (phase.GpuTimer.IsValid())
				{
					const bool bAdded = command.TimerUses.Add(phase.GpuTimer);
					ASSERT(bAdded, "Prepared timer ownership is reserved before publication.");
					rhi::TimerSlot& timer = m_State->m_Timers[phase.GpuTimer.Slot];
					timer.State = ETimerState::Prepared;
					timer.RecordingSlot = group->Lists[index].Handle.Slot;
					timer.bRetired = false;
				}
				for (rhi::PreparedBufferAccessDesc const& access : phase.Buffers)
				{
					(void)command.BufferUses.Add({ access.Buffer, m_State->m_Buffers[access.Buffer.Slot].Native, ETrackedResourceState::Unknown });
					++m_State->m_Buffers[access.Buffer.Slot].RecordedUses;
				}
				for (rhi::PreparedTextureAccessDesc const& access : phase.Textures)
				{
					(void)command.TextureUses.Add({ access.Texture, m_State->m_Textures[access.Texture.Slot].Native });
					++m_State->m_Textures[access.Texture.Slot].PreparedReferences;
					if (m_State->m_Textures[access.Texture.Slot].bSwapChainImage)
					{
						++m_State->m_Textures[access.Texture.Slot].AcquiredUnsubmittedUses;
						m_State->m_Textures[access.Texture.Slot].bAcquisitionPrepared = true;
					}
				}
				for (rhi::PreparedAccelStructAccessDesc const& access : phase.AccelStructs)
				{
					rhi::AccelStructSlot& accel = m_State->m_AccelStructs[access.AccelStruct.Slot];
					(void)command.AccelUses.Add(
						{ access.AccelStruct, accel.Native,
						 access.State == EAccelStructState::BuildWrite || access.State == EAccelStructState::CopyDestination });
					++accel.RecordedUses;
					++accel.PreparedReferences;
				}
				for (rhi::PreparedRecordingAccelQuery const& query : phase.AccelCompactionQueries)
				{
					++m_State->m_AccelStructs[query.Description.Source.Slot].NextQueryGeneration;
				}
				for (rhi::PreparedBinding const& binding : phase.Bindings)
				{
					if (binding.ShaderTable.IsValid())
					{
						rhi::ShaderTableSlot& table = m_State->m_ShaderTables[binding.ShaderTable.Slot];
						++table.PreparedReferences;
						(void)command.RayPipelineUses.Add({ table.Pipeline, binding.NativePipeline });
						++m_State->m_RayTracingPipelines[table.Pipeline.Slot].RecordedUses;
					}
					else if (binding.GraphicsPipeline.IsValid())
					{
						(void)command.GraphicsPipelineUses.Add({ binding.GraphicsPipeline, binding.NativePipeline });
						++m_State->m_GraphicsPipelines[binding.GraphicsPipeline.Slot].RecordedUses;
					}
					else
					{
						(void)command.PipelineUses.Add({ binding.ComputePipeline, binding.NativePipeline });
						++m_State->m_Pipelines[binding.ComputePipeline.Slot].RecordedUses;
					}
					if (binding.DeviceHeaps)
					{
						for (rhi::BoundDescriptorTableRange const& row : binding.DeviceHeaps->Ranges)
						{
							trackDescriptorTableOwner(command, row.Table);
						}
					}
					for (uint32 set = 0; set < binding.SetCount; ++set)
					{
						rhi::BindingSetSlot& owner = m_State->m_BindingSets[binding.Sets[set].Slot];
						++owner.PreparedReferences;
						if (owner.Tables)
						{
							for (rhi::BoundDescriptorTableRange const& row : owner.Tables->Ranges)
							{
								trackDescriptorTableOwner(command, row.Table);
							}
						}
						for (uint32 at = 0; at < owner.SamplerCount; ++at)
						{
							const SamplerHandle handle = owner.Samplers[at];
							(void)command.SamplerUses.Add(handle);
							++m_State->m_Samplers[handle.Slot].RecordedUses;
						}
						for (uint32 at = 0; at < owner.TextureCount + owner.StorageTextureCount; ++at)
						{
							const TextureViewHandle handle = at < owner.TextureCount ? owner.SampledViews[at] : owner.StorageViews[at - owner.TextureCount];
							if (handle.IsValid())
							{
								(void)command.TextureViewUses.Add(handle);
								++m_State->m_TextureViews[handle.Slot].RecordedUses;
							}
						}
					}
				}
				for (rhi::PreparedRecordingAttachment const& attachment : phase.Attachments)
				{
					(void)command.FramebufferUses.Add({ attachment.Framebuffer, m_State->m_Framebuffers[attachment.Framebuffer.Slot].Native });
					++m_State->m_Framebuffers[attachment.Framebuffer.Slot].RecordedUses;
				}
			}
		}

		m_State->m_PreparedPrograms.Retain(*group);

		return Result<void>::Success();
	}
}
