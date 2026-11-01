#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	[[nodiscard]] Result<void> PrepareRecordingGroup(State* opaque, Span<const NativePreparedList> lists)
	{
		State& state = *opaque;
		for (const rhi::NativePreparedList& list : lists)
		{
			for (const rhi::NativePreparedPhase& phase : list.Phases)
			{
				for (usize first = 0; first < phase.Textures.Size(); ++first)
				{
					const rhi::NativePreparedTextureAccess& a = phase.Textures[first];
					const Texture& texture = *static_cast<const Texture*>(a.Resource);
					if (!GetFormatInfo(texture.Format).bHasStencil)
					{
						continue;
					}
					for (usize second = first + 1; second < phase.Textures.Size(); ++second)
					{
						const rhi::NativePreparedTextureAccess& b = phase.Textures[second];
						if (a.Resource != b.Resource ||
							a.Intent.BaseMipLevel >= b.Intent.BaseMipLevel + b.Intent.MipLevelCount ||
							b.Intent.BaseMipLevel >= a.Intent.BaseMipLevel + a.Intent.MipLevelCount ||
							a.Intent.BaseArrayLayer >= b.Intent.BaseArrayLayer + b.Intent.ArrayLayerCount ||
							b.Intent.BaseArrayLayer >= a.Intent.BaseArrayLayer + a.Intent.ArrayLayerCount)
						{
							continue;
						}
						ASSERT(PreparedTextureLayout(texture, phase.Textures, a.Intent) == PreparedTextureLayout(texture, phase.Textures, b.Intent),
							"Depth and stencil intents require incompatible physical image layouts.");
					}
				}
			}
		}
		usize submissionCapacity = 0;
		if (!AddMemorySize(state.InFlight.Size(), state.PreparedCredits, &submissionCapacity) ||
			!AddMemorySize(submissionCapacity, lists.Size(), &submissionCapacity) ||
			!state.InFlight.Reserve(submissionCapacity) ||
			!state.Submissions.TryReserveCredits(lists.Size()))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not reserve prepared submission storage."));
		}

		state.PreparedCredits += lists.Size();
		for (const rhi::NativePreparedList& description : lists)
		{
			Pending& pending = *static_cast<Pending*>(description.Commands);
			pending.bSubmissionCredit = true;
			pending.bPrepared = true;
		}
		for (usize listIndex = 0; listIndex < lists.Size(); ++listIndex)
		{
			const rhi::NativePreparedList& description = lists[listIndex];
			Pending& pending = *static_cast<Pending*>(description.Commands);
			usize textureCount = 0;
			usize bufferCount = 0;
			for (const rhi::NativePreparedPhase& phase : description.Phases)
			{
				textureCount += phase.Textures.Size();
				bufferCount += phase.Buffers.Size();
			}
			if (!pending.TextureLayouts.Reserve(textureCount) ||
				!pending.PreparedTextures.Reserve(textureCount) ||
				!pending.PreparedHostReads.Reserve(bufferCount) ||
				!pending.PreparedAccelBuilds.Reserve(description.AccelBuildCount) ||
				!pending.PreparedAccelQueries.Reserve(description.AccelQueryCount) ||
				!pending.PreparedAccelCompacts.Reserve(description.AccelCompactCount) ||
				!pending.PreparedBindings.Reserve(description.BindingCount))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not reserve prepared state storage."));
			}
			for (const rhi::NativePreparedPhase& phase : description.Phases)
			{
				for (const rhi::NativePreparedBufferAccess& access : phase.Buffers)
				{
					Buffer* buffer = static_cast<Buffer*>(access.Resource);
					if (buffer->MemoryAccess == EBufferMemory::CpuRead)
					{
						(void)pending.PreparedHostReads.Add(buffer);
					}
				}
				for (const rhi::NativePreparedTextureAccess& access : phase.Textures)
				{
					Texture& texture = *static_cast<Texture*>(access.Resource);
					Result<void> acquired = PrepareSwapChainTarget(pending, texture);
					if (!acquired)
					{
						return acquired;
					}
					Pending::TextureLayoutState* tracked = TrackTextureLayout(pending, texture);
					const uint64 count = uint64(texture.MipLevels) * texture.ArrayLayers;
					if (tracked == nullptr ||
						count > UINT32_MAX ||
						!tracked->SubresourceLayouts.Reserve(static_cast<usize>(count)) ||
						!texture.SubresourceLayouts.Reserve(static_cast<usize>(count)))
					{
						return Result<void>::Failure(Failure("Rhi.Allocation", "Could not reserve prepared texture states."));
					}
					const bool bAdded = pending.PreparedTextures.Add(access);
					ASSERT(bAdded, "The prepared texture commit intents are captured cold.");
				}
			}

			// Seed only from accepted native state and actual DAG/same-queue ancestors.
			// An unrelated earlier list is not an authority for this list's entry layout.
			for (vulkan::Pending::TextureLayoutState& tracked : pending.TextureLayouts)
			{
				Texture& texture = *tracked.Item;
				for (usize previous = 0; previous < listIndex; ++previous)
				{
					const uint32 lane = static_cast<uint32>(lists[previous].Queue);
					if (description.DependencyFrontier[lane] < previous + 1)
					{
						continue;
					}
					for (const rhi::NativePreparedPhase& phase : lists[previous].Phases)
					{
						for (const rhi::NativePreparedTextureAccess& access : phase.Textures)
						{
							if (access.Resource != &texture)
							{
								continue;
							}
							if (tracked.SubresourceLayouts.IsEmpty())
							{
								const bool bResized = tracked.SubresourceLayouts.Resize(texture.MipLevels * texture.ArrayLayers);
								for (VkImageLayout& layout : tracked.SubresourceLayouts)
								{
									layout = tracked.WholeLayout;
								}
								ASSERT(bResized, "The texture seed has reserved subresource storage.");
							}
							for (uint32 layer = 0; layer < access.Intent.ArrayLayerCount; ++layer)
							{
								for (uint32 mip = 0; mip < access.Intent.MipLevelCount; ++mip)
								{
									const uint32 index = access.Intent.BaseMipLevel + mip + (access.Intent.BaseArrayLayer + layer) * texture.MipLevels;
									tracked.SubresourceLayouts[index] = PreparedTextureLayout(texture, phase.Textures, access.Intent);
								}
							}
						}
					}
				}
			}
		}

		// Shared read intents may not each transition the same image independently. Validate raw
		// layouts after seeding: the public tracked-state flags cannot distinguish GENERAL from
		// SHADER_READ_ONLY_OPTIMAL for every texture usage profile.
		struct SharedTextureAccess final
		{
			const NativePreparedTextureAccess* Access = nullptr;
			uint32 List = 0;
			Span<const NativePreparedTextureAccess> Intents;
		};

		Array<SharedTextureAccess> sharedAccesses(state.Resource);
		usize sharedCount = 0;
		for (const rhi::NativePreparedList& list : lists)
		{
			for (const rhi::NativePreparedPhase& phase : list.Phases)
			{
				if (!AddMemorySize(sharedCount, phase.Textures.Size(), &sharedCount))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Prepared texture access count overflow."));
				}
			}
		}
		if (!sharedAccesses.Reserve(sharedCount))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not validate shared prepared layouts."));
		}
		for (uint32 listIndex = 0; listIndex < lists.Size(); ++listIndex)
		{
			for (const rhi::NativePreparedPhase& phase : lists[listIndex].Phases)
			{
				for (const rhi::NativePreparedTextureAccess& access : phase.Textures)
				{
					(void)sharedAccesses.Add({ &access, listIndex, phase.Textures });
				}
			}
		}
		if (!sharedAccesses.IsEmpty())
		{
			Sort(sharedAccesses.begin(), sharedAccesses.end(), [](const SharedTextureAccess& left, const SharedTextureAccess& right) noexcept
				{ return Less<void*>{}(left.Access->Resource, right.Access->Resource); });
		}
		for (usize begin = 0; begin < sharedAccesses.Size();)
		{
			usize end = begin + 1;
			while (end < sharedAccesses.Size() && sharedAccesses[end].Access->Resource == sharedAccesses[begin].Access->Resource)
			{
				++end;
			}
			for (usize index = begin; index < end; ++index)
			{
				SharedTextureAccess const& current = sharedAccesses[index];
				for (usize previous = begin; previous < index; ++previous)
				{
					SharedTextureAccess const& prior = sharedAccesses[previous];
					const uint32 lane = static_cast<uint32>(lists[prior.List].Queue);
					const uint32 currentLane = static_cast<uint32>(lists[current.List].Queue);
					if (current.List == prior.List || lists[current.List].DependencyFrontier[lane] >= prior.List + 1 || lists[prior.List].DependencyFrontier[currentLane] >= current.List + 1)
					{
						continue;
					}
					const PreparedTextureAccessDesc& a = current.Access->Intent;
					const PreparedTextureAccessDesc& b = prior.Access->Intent; const uint32 firstMip = Max(a.BaseMipLevel, b.BaseMipLevel);
					uint32 const lastMip = Min(a.BaseMipLevel + a.MipLevelCount, b.BaseMipLevel + b.MipLevelCount); const uint32 firstLayer = Max(a.BaseArrayLayer, b.BaseArrayLayer); const uint32 lastLayer =
						Min(a.BaseArrayLayer + a.ArrayLayerCount, b.BaseArrayLayer + b.ArrayLayerCount);
					const Texture& texture = *static_cast<const Texture*>(current.Access->Resource);
					for (uint32 layer = firstLayer; layer < lastLayer; ++layer)
					{
						for (uint32 mip = firstMip; mip < lastMip; ++mip)
						{
							TextureViewDesc range;
							range.BaseMipLevel = mip;
							range.MipLevelCount = 1;
							range.BaseArrayLayer = layer;
							range.ArrayLayerCount = 1;
							if (!TextureHasLayout(*static_cast<Pending*>(lists[current.List].Commands), texture, PreparedTextureLayout(texture, current.Intents, a), &range) ||
								!TextureHasLayout(*static_cast<Pending*>(lists[prior.List].Commands), texture, PreparedTextureLayout(texture, prior.Intents, b), &range))
							{
								Error error;
								error.Code = EErrorCode::Conflict;

								return Result<void>::Failure(Move(error));
							}
						}
					}
				}
			}
			begin = end;
		}

		return Result<void>::Success();
	}

	void ResetPreparedCommandList(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		if (pending.bSubmissionCredit)
		{
			--state.PreparedCredits;
			state.Submissions.ReleaseCredit();
			pending.bSubmissionCredit = false;
		}
		for (vulkan::Pending::DescriptorPoolRecord const& pool : pending.DescriptorPools)
		{
			state.F.DestroyDescriptorPool(state.Device, pool.Handle, nullptr);
		}

		pending.DescriptorPools.Clear();
		pending.PreparedAccelBuilds.Clear();
		for (vulkan::Pending::PreparedAccelQuery const& query : pending.PreparedAccelQueries)
		{
			NativeAccelStruct& accel = *static_cast<NativeAccelStruct*>(query.Source);
			if (accel.ProvisionalQuery && accel.ProvisionalQuery->Generation == query.Owner->Generation)
			{
				accel.ProvisionalQuery.Reset();
			}
		}
		pending.PreparedAccelQueries.Clear();
		pending.PreparedAccelCompacts.Clear();
		pending.PreparedBindings.Clear();
		pending.DepthResolves.Clear();
		pending.TextureLayouts.Clear();
		pending.PreparedHostReads.Clear();
		pending.PreparedTextures.Clear();
		pending.bInvalidTextureLayouts = false;
		pending.SwapChainOwner = nullptr;
		pending.SwapChainImageIndex = 0;
		pending.SwapChainSemaphoreIndex = 0;
		pending.bPrepared = false;
	}

	[[nodiscard]] Result<void> CapturePreparedStorage(Pending::PreparedBinding& binding, const NativeFixedLayoutState& fixed, Span<const NativeFixedBindingSet> sets)
	{
		uint32 buffers = 0;
		uint32 textures = 0;
		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			for (uint32 index = 0; index < fixed.RangeCounts[layout]; ++index)
			{
				const BindingRangeDesc& range = fixed.Ranges[layout][index];
				const NativeTableRangeBinding* table = FindNativeTableRange(sets[layout], index);
				const uint32 count = table != nullptr ? static_cast<uint32>(table->UsedEntries.Size()) : range.DescriptorCount;
				if (range.Type == EDescriptorType::StorageBuffer)
				{
					buffers += count;
				}
				else if (range.Type == EDescriptorType::StorageTexture)
				{
					textures += count;
				}
			}
		}
		if (!binding.StorageBuffers.Reserve(buffers) || !binding.StorageTextures.Reserve(textures) || !binding.StorageRanges.Reserve(textures))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not capture selected storage hazards."));
		}

		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			uint32 descriptor = 0;
			for (uint32 rangeIndex = 0; rangeIndex < fixed.RangeCounts[layout]; ++rangeIndex)
			{
				const BindingRangeDesc& range = fixed.Ranges[layout][rangeIndex];
				const NativeTableRangeBinding* tableBinding = FindNativeTableRange(sets[layout], rangeIndex);
				if (range.Type != EDescriptorType::StorageBuffer && range.Type != EDescriptorType::StorageTexture)
				{
					if (tableBinding == nullptr)
					{
						descriptor += range.DescriptorCount;
					}
					continue;
				}

				const uint32 count = tableBinding != nullptr ? static_cast<uint32>(tableBinding->UsedEntries.Size()) : range.DescriptorCount;
				for (uint32 element = 0; element < count; ++element)
				{
					void* resource = nullptr;
					void* viewNative = nullptr;
					if (tableBinding != nullptr)
					{
						const DescriptorTable& table = *static_cast<DescriptorTable*>(tableBinding->Table);
						const uint32 entry = tableBinding->UsedEntries[element];
						if (range.Type == EDescriptorType::StorageBuffer)
						{
							resource = table.Buffers[entry];
						}
						else if (range.Type == EDescriptorType::StorageTexture)
						{
							resource = table.Textures[entry];
							viewNative = table.TextureViews[entry];
						}
					}
					else
					{
						const rhi::NativeFixedDescriptor& value = sets[layout].Descriptors[descriptor++];
						resource = value.Resource;
						viewNative = value.View;
					}
					if (range.Type == EDescriptorType::StorageBuffer)
					{
						binding.StorageBuffers.AddRequired(static_cast<Buffer*>(resource));
					}
					else if (range.Type == EDescriptorType::StorageTexture)
					{
						Texture* texture = static_cast<Texture*>(resource);
						const TextureView* view = static_cast<const TextureView*>(viewNative);
						TextureViewDesc extent;
						if (view != nullptr)
						{
							extent = view->Descriptor;
						}
						else
						{
							extent.MipLevelCount = texture->MipLevels;
							extent.ArrayLayerCount = texture->ArrayLayers;
						}
						binding.StorageTextures.AddRequired(texture);
						binding.StorageRanges.AddRequired(extent);
					}
				}
			}
		}
		binding.BufferCount = static_cast<uint32>(binding.StorageBuffers.Size());
		binding.TextureCount = static_cast<uint32>(binding.StorageTextures.Size());

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint32> PrepareBinding(
		State* opaque,
		void* native,
		void* pipeline,
		bool bGraphics,
		Span<const NativeFixedBindingSet> sets,
		Span<const NativePreparedTextureAccess> intents,
		const NativePreparedVertexInput& input,
		Span<const NativeDescriptorHeapBinding> heaps,
		SharedPtr<NativeBindingProgram>* program)
	{
		if (!heaps.IsEmpty())
		{
			return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.DescriptorHeap", "Unnamed device heaps are unavailable.");
		}
		Pending& pending = *static_cast<Pending*>(native);
		const NativeFixedLayoutState& fixed = bGraphics ? static_cast<const GraphicsPipeline*>(pipeline)->Fixed : static_cast<const ComputePipeline*>(pipeline)->Fixed;
		if (program && *program)
		{
			const Pending::PreparedBinding& retained = *static_cast<const Pending::PreparedBinding*>((*program)->Data);
			bool bMatches = retained.Count == sets.Size();
			for (uint32 layout = 0; bMatches && layout < sets.Size(); ++layout)
			{
				bMatches =
					retained.DescriptorOwners[layout] &&
					DescriptorSetMatches(*static_cast<const DescriptorSetRealization*>(retained.DescriptorOwners[layout]->GetNative()),
						fixed, layout, sets[layout], pending, bGraphics, false, intents, true);
			}
			if (bMatches)
			{
				const uint32 index = static_cast<uint32>(pending.PreparedBindings.Size());
				if (!pending.PreparedBindings.Add(*program))
				{
					return Result<uint32>::Failure(Failure("Rhi.Allocation", "Could not retain a binding program."));
				}
				++pending.Statistics.BindingProgramReuses;

				return Result<uint32>::Success(index);
			}
		}
		++pending.Statistics.BindingProgramCreations;
		MemoryResource& memory = opaque->Resource;
		SharedPtr<NativeBindingProgram> owner;
		if (!MakeSharedInto(memory, &owner))
		{
			return Result<uint32>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingProgram", "Could not retain a prepared binding program.");
		}
		owner->Resource = &memory;
		owner->Data = CreateObject<Pending::PreparedBinding>(memory, memory);
		if (!owner->Data)
		{
			return Result<uint32>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingProgram", "Could not retain a prepared binding program.");
		}
		Pending::PreparedBinding& binding = *static_cast<Pending::PreparedBinding*>(owner->Data);
		binding.VertexCount = input.VertexCount;
		for (uint32 slot = 0; slot < input.VertexCount; ++slot)
		{
			binding.Vertices[slot] = static_cast<const Buffer*>(input.Vertices[slot].Buffer)->Handle;
			binding.VertexOffsets[slot] = input.Vertices[slot].Offset;
		}
		if (input.IndexBuffer != nullptr)
		{
			binding.Index = static_cast<const Buffer*>(input.IndexBuffer)->Handle;
			binding.IndexOffsetBytes = input.IndexRange.ByteOffset;
			binding.IndexType = input.IndexFormat == EFormat::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
		}

		if (!sets.IsEmpty())
		{
			Result<void> result = WriteFixedBindingSets(opaque, native, pipeline, bGraphics, sets, &binding, intents);
			if (!result)
			{
				return Result<uint32>::Failure(result.GetError());
			}
		}

		Result<void> storage = CapturePreparedStorage(binding, fixed, sets);
		if (!storage)
		{
			return Result<uint32>::Failure(storage.GetError());
		}

		const uint32 index = static_cast<uint32>(pending.PreparedBindings.Size());
		const bool bAdded = pending.PreparedBindings.Add(owner);
		ASSERT(bAdded, "Every immutable prepared binding has reserved storage.");
		bool bReusable = true;
		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			bReusable &= sets[layout].TableCount == 0 && static_cast<bool>(binding.DescriptorOwners[layout]);
		}
		if (program && bReusable)
		{
			owner->RetainedBytes = sizeof(Pending::PreparedBinding) + binding.StorageBuffers.Capacity() * sizeof(Buffer*) +
				binding.StorageTextures.Capacity() * sizeof(Texture*) + binding.StorageRanges.Capacity() * sizeof(TextureViewDesc);
			*program = owner;
		}

		return Result<uint32>::Success(index);
	}

	[[nodiscard]] Result<void> RecordPreparedPhase(State* opaque, void* native, const NativePreparedPhase& phase)
	{
		const PreparedErrorScope errorScope;
		Pending& pending = *static_cast<Pending*>(native);
		pending.LastPreparedDraw = UINT32_MAX;

		for (const rhi::NativePreparedBufferAccess& access : phase.Buffers)
		{
			Result<void> result = RecordSetBufferState(opaque, native, access.Resource, access.State);
			if (!result)
			{
				return result;
			}
		}
		for (const rhi::NativePreparedTextureAccess& access : phase.Textures)
		{
			for (uint32 layer = 0; layer < access.Intent.ArrayLayerCount; ++layer)
			{
				for (uint32 mip = 0; mip < access.Intent.MipLevelCount; ++mip)
				{
					const TextureSubresource subresource{ access.Intent.BaseMipLevel + mip, access.Intent.BaseArrayLayer + layer,
														 access.Intent.Aspect };
					State& state = *opaque;
					Texture& texture = *static_cast<Texture*>(access.Resource);
					pending.bExplicitStateRequest = true;
					RequireTextureLayout(state, pending, texture, PreparedTextureLayout(texture, phase.Textures, access.Intent),
						subresource.MipLevel + subresource.ArrayLayer * texture.MipLevels);
					pending.bExplicitStateRequest = false;
					if (pending.bInvalidTextureLayouts)
					{
						return Result<void>::Failure(NativeError(EErrorCode::Conflict, "Rhi.PreparedLayout", "The merged plane layouts were not captured."));
					}
				}
			}
		}

		return Result<void>::Success();
	}

	void BindPrepared(State& state, Pending& pending, void* native, bool bGraphics, uint32 index) noexcept
	{
		const Pending::PreparedBinding& binding = pending.BindingAt(index);
		const NativeFixedLayoutState& fixed = bGraphics ? static_cast<const GraphicsPipeline*>(native)->Fixed : static_cast<const ComputePipeline*>(native)->Fixed;
		const VkPipelineLayout layout = bGraphics ? static_cast<const GraphicsPipeline*>(native)->Layout : static_cast<const ComputePipeline*>(native)->Layout;
		for (uint32 set = 0; set < binding.Count; ++set)
		{
			state.F.CmdBindDescriptorSets(pending.Commands, bGraphics ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE,
				layout, fixed.Spaces[set], 1, &binding.Sets[set], 0, nullptr);
		}
	}

	void BindPreparedCompute(State* opaque, void* native, void* pipelineNative, uint32 binding, Span<const Byte> pushConstants)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		ComputePipeline& pipeline = *static_cast<ComputePipeline*>(pipelineNative);
		const Pending::PreparedBinding& captured = pending.BindingAt(binding);
		for (uint32 index = 0; index < captured.BufferCount; ++index)
		{
			(void)RecordShaderBufferBarrier(opaque, native, captured.StorageBuffers[index]);
		}
		for (uint32 index = 0; index < captured.TextureCount; ++index)
		{
			rhi::TextureViewDesc const& extent = captured.StorageRanges[index];
			EmitTextureBarrier(state, pending, *captured.StorageTextures[index], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
				extent.BaseMipLevel, extent.MipLevelCount, extent.BaseArrayLayer, extent.ArrayLayerCount);
		}
		BindPrepared(state, pending, pipelineNative, false, binding);
		state.F.CmdBindPipeline(pending.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Handle);
		if (pipeline.PushConstantBytes != 0)
		{
			state.F.CmdPushConstants(pending.Commands, pipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pipeline.PushConstantBytes,
				pushConstants.Data());
		}
	}

	[[nodiscard]] Result<void> RecordPreparedDispatch(State* opaque, void* native, void* pipelineNative, uint32 binding, const ComputeDispatch& dispatch)
	{
		BindPreparedCompute(opaque, native, pipelineNative, binding, dispatch.PushConstants);
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		state.F.CmdDispatch(pending.Commands, dispatch.GroupsX, dispatch.GroupsY, dispatch.GroupsZ);

		return Result<void>::Success();
	}

	void BindPreparedGraphics(State* opaque, void* native, void* pipelineNative, uint32 binding, Span<const Byte> pushConstants)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(pipelineNative);
		if (pending.LastPreparedDraw != UINT32_MAX)
		{
			const Pending::PreparedBinding& previous = pending.BindingAt(pending.LastPreparedDraw);
			if (previous.BufferCount != 0 || previous.TextureCount != 0)
			{
				state.F.CmdEndRenderPass(pending.Commands);
				for (uint32 index = 0; index < previous.BufferCount; ++index)
				{
					(void)RecordShaderBufferBarrier(opaque, native, previous.StorageBuffers[index]);
				}
				for (uint32 index = 0; index < previous.TextureCount; ++index)
				{
					rhi::TextureViewDesc const& extent = previous.StorageRanges[index];
					EmitTextureBarrier(state, pending, *previous.StorageTextures[index], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
						extent.BaseMipLevel, extent.MipLevelCount, extent.BaseArrayLayer, extent.ArrayLayerCount);
				}

				state.F.CmdBeginRenderPass(pending.Commands, &pending.ResumeRendering, VK_SUBPASS_CONTENTS_INLINE);
			}
		}

		pending.LastPreparedDraw = binding;
		BindPrepared(state, pending, pipelineNative, true, binding);
		state.F.CmdBindPipeline(pending.Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Handle);
		if (pipeline.PushConstantBytes != 0)
		{
			state.F.CmdPushConstants(pending.Commands, pipeline.Layout, pipeline.ShaderStages, 0, pipeline.PushConstantBytes,
				pushConstants.Data());
		}

		ApplyBlendConstant(state, pending, pipeline, {});
		ApplyShadingRate(state, pending, EShadingRate::Rate1x1, EShadingRateCombiner::Passthrough, EShadingRateCombiner::Passthrough);
		const Pending::PreparedBinding& captured = pending.BindingAt(binding);
		if (captured.VertexCount != 0)
		{
			state.F.CmdBindVertexBuffers(pending.Commands, 0, captured.VertexCount, captured.Vertices, captured.VertexOffsets);
		}
	}

	[[nodiscard]] Result<void> RecordPreparedMeshDispatch(State* opaque, void* native, void* pipelineNative, uint32 binding, const MeshDispatch& dispatch)
	{
		State& state = *opaque;
		if (state.F.CmdDrawMeshTasks == nullptr)
		{
			return Result<void>::Failure(NativeError(EErrorCode::ExternalFailure, "Rhi.MeshShader", "The prepared mesh command is unavailable."));
		}

		BindPreparedGraphics(opaque, native, pipelineNative, binding, dispatch.PushConstants);
		Pending& pending = *static_cast<Pending*>(native);
		if (static_cast<const GraphicsPipeline*>(pipelineNative)->bStencil)
		{
			state.F.CmdSetStencilReference(pending.Commands, VK_STENCIL_FACE_FRONT_AND_BACK, dispatch.StencilReference);
		}
		state.F.CmdDrawMeshTasks(pending.Commands, dispatch.GroupsX, dispatch.GroupsY, dispatch.GroupsZ);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordPreparedDraw(State* opaque, void* native, void* pipelineNative, uint32 binding, const DrawArguments& draw)
	{
		BindPreparedGraphics(opaque, native, pipelineNative, binding, draw.PushConstants);
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		if (static_cast<const GraphicsPipeline*>(pipelineNative)->bStencil)
		{
			state.F.CmdSetStencilReference(pending.Commands, VK_STENCIL_FACE_FRONT_AND_BACK, draw.StencilReference);
		}
		state.F.CmdDraw(pending.Commands, draw.VertexCount, draw.InstanceCount, draw.FirstVertex, draw.FirstInstance);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordDrawIndexedArguments(State* opaque, void* native, void* pipelineNative, uint32 binding, const DrawIndexedArguments& draw)
	{
		BindPreparedGraphics(opaque, native, pipelineNative, binding, draw.PushConstants);
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		const Pending::PreparedBinding& captured = pending.BindingAt(binding);
		state.F.CmdBindIndexBuffer(pending.Commands, captured.Index, captured.IndexOffsetBytes, captured.IndexType);
		if (static_cast<const GraphicsPipeline*>(pipelineNative)->bStencil)
		{
			state.F.CmdSetStencilReference(pending.Commands, VK_STENCIL_FACE_FRONT_AND_BACK, draw.StencilReference);
		}
		state.F.CmdDrawIndexed(pending.Commands, draw.IndexCount, draw.InstanceCount, draw.FirstIndex, draw.BaseVertex, draw.FirstInstance);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint32> PrepareIndirect(State* opaque, void*, const NativePreparedIndirect& indirect)
	{
		const State& state = *opaque;
		if (indirect.Kind == EPreparedIndirectKind::Mesh && state.F.CmdDrawMeshTasksIndirect == nullptr)
		{
			return Result<uint32>::Failure(NativeError(EErrorCode::Unsupported, "Rhi.PreparedIndirect", "The native indirect mesh command is unavailable."));
		}

		return Result<uint32>::Success(0);
	}

	[[nodiscard]] Result<void> RecordPreparedIndirect(
		State* opaque,
		void* native,
		void* pipelineNative,
		uint32 binding,
		const NativePreparedIndirect& indirect,
		Span<const Byte> pushConstants)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		const Buffer& arguments = *static_cast<Buffer*>(indirect.Arguments);
		const Buffer* count = static_cast<Buffer*>(indirect.CountBuffer);
		if (indirect.Kind == EPreparedIndirectKind::Compute)
		{
			BindPreparedCompute(opaque, native, pipelineNative, binding, pushConstants);
			state.F.CmdDispatchIndirect(pending.Commands, arguments.Handle, indirect.ArgumentsOffsetBytes);

			return Result<void>::Success();
		}

		BindPreparedGraphics(opaque, native, pipelineNative, binding, pushConstants);
		if (static_cast<const GraphicsPipeline*>(pipelineNative)->bStencil)
		{
			state.F.CmdSetStencilReference(pending.Commands, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
		}
		if (indirect.Kind == EPreparedIndirectKind::Mesh)
		{
			state.F.CmdDrawMeshTasksIndirect(pending.Commands, arguments.Handle, indirect.ArgumentsOffsetBytes, 1, indirect.Stride);

			return Result<void>::Success();
		}
		if (indirect.Kind == EPreparedIndirectKind::DrawIndexed)
		{
			const Pending::PreparedBinding& captured = pending.BindingAt(binding);
			state.F.CmdBindIndexBuffer(pending.Commands, captured.Index, captured.IndexOffsetBytes, captured.IndexType);
			if (count != nullptr)
			{
				state.F.CmdDrawIndexedIndirectCount(pending.Commands, arguments.Handle, indirect.ArgumentsOffsetBytes, count->Handle,
					indirect.CountBufferOffsetBytes, indirect.MaximumCount, indirect.Stride);
			}
			else if (state.bMultiDrawIndirect && indirect.MaximumCount <= state.DeviceProperties.limits.maxDrawIndirectCount)
			{
				state.F.CmdDrawIndexedIndirect(pending.Commands, arguments.Handle, indirect.ArgumentsOffsetBytes, indirect.MaximumCount,
					indirect.Stride);
			}
			else
			{
				for (uint32 index = 0; index < indirect.MaximumCount; ++index)
				{
					state.F.CmdDrawIndexedIndirect(pending.Commands, arguments.Handle,
						indirect.ArgumentsOffsetBytes + uint64(index) * indirect.Stride, 1, indirect.Stride);
				}
			}
		}
		else if (count != nullptr)
		{
			state.F.CmdDrawIndirectCount(pending.Commands, arguments.Handle, indirect.ArgumentsOffsetBytes, count->Handle,
				indirect.CountBufferOffsetBytes, indirect.MaximumCount, indirect.Stride);
		}
		else if (state.bMultiDrawIndirect && indirect.MaximumCount <= state.DeviceProperties.limits.maxDrawIndirectCount)
		{
			state.F.CmdDrawIndirect(pending.Commands, arguments.Handle, indirect.ArgumentsOffsetBytes, indirect.MaximumCount, indirect.Stride);
		}
		else
		{
			for (uint32 index = 0; index < indirect.MaximumCount; ++index)
			{
				state.F.CmdDrawIndirect(pending.Commands, arguments.Handle,
					indirect.ArgumentsOffsetBytes + uint64(index) * indirect.Stride, 1, indirect.Stride);
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordPreparedRendering(
		State* opaque,
		void* native,
		void* target,
		Span<const EAttachmentLoad> loads,
		Span<const ClearColor> colors,
		DepthAttachmentOptions depthOptions)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Framebuffer& framebuffer = *static_cast<Framebuffer*>(target);
		pending.LastPreparedDraw = UINT32_MAX;
		Result<void> result = RecordBeginFramebuffer(opaque, native, target, depthOptions);
		if (!result)
		{
			return result;
		}
		ASSERT(loads.Size() == framebuffer.ColorCount && (colors.IsEmpty() || colors.Size() == framebuffer.ColorCount), "Prepared color loads and clears match the retained framebuffer.");

		VkClearAttachment attachments[8]{};
		uint32 clearCount = 0;
		for (uint32 index = 0; index < framebuffer.ColorCount; ++index)
		{
			if (loads[index] != EAttachmentLoad::Clear)
			{
				continue;
			}
			ASSERT(colors.Size() == framebuffer.ColorCount, "Cleared attachments require clear values.");

			VkClearAttachment& attachment = attachments[clearCount++];
			attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			attachment.colorAttachment = index;
			const rhi::ClearColor& color = colors[index];
			attachment.clearValue.color = MakeClearColor(color);
		}
		if (clearCount != 0)
		{
			VkClearRect rect{ {{0, 0}, {framebuffer.Width, framebuffer.Height}}, 0, 1 };
			state.F.CmdClearAttachments(pending.Commands, clearCount, attachments, 1, &rect);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> FinishPreparedCommandList(State* opaque, void* native)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		for (const vulkan::Buffer* buffer : pending.PreparedHostReads)
		{
			BufferHostReadBarrier(state, pending, buffer->Handle);
		}
		if (pending.bInvalidTextureLayouts || state.F.EndCommandBuffer(pending.Commands) != VK_SUCCESS)
		{
			Error error;
			error.Code = EErrorCode::ExternalFailure;

			return Result<void>::Failure(Move(error));
		}

		pending.bPreparedFinished = true;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> QueueWaitForPreparedSubmission(State* opaque, EQueue waitingQueue, EQueue executionQueue, uint64 serial)
	{
		const PreparedErrorScope errorScope;

		return QueueWaitForSubmission(opaque, waitingQueue, executionQueue, serial);
	}

	[[nodiscard]] Result<uint32> DescribePreparedNativeBoundary(const void*, const void* native, const void* resourceNative, bool bTexture) noexcept
	{
		using R = Result<uint32>;
		const Pending& pending = *static_cast<const Pending*>(native);
		if (pending.bPreparedFinished && !pending.bInvalidTextureLayouts)
		{
			if (!bTexture)
			{
				return R::Success(0); // Buffers have no image layout or queue-family transfer in this profile.
			}
			const Texture& texture = *static_cast<const Texture*>(resourceNative);
			for (vulkan::Pending::TextureLayoutState const& tracked : pending.TextureLayouts)
			{
				if (tracked.Item != &texture)
				{
					continue;
				}
				VkImageLayout layout = tracked.WholeLayout;
				bool bUniform = true;
				if (!tracked.SubresourceLayouts.IsEmpty())
				{
					layout = tracked.SubresourceLayouts[0];
					for (VkImageLayout const actual : tracked.SubresourceLayouts)
					{
						bUniform &= actual == layout;
					}
				}
				if (bUniform && layout != VK_IMAGE_LAYOUT_UNDEFINED)
				{
					return R::Success(static_cast<uint32>(layout));
				}
			}
		}
		Error error;
		error.Code = EErrorCode::Unsupported;

		return R::Failure(Move(error));
	}
}
