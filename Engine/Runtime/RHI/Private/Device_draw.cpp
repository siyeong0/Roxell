#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::Draw(CommandListHandle handle, PreparedBindingRef ref, const DrawArguments& draw)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(phase != nullptr && ref.List == handle && ref.Phase == preparedList(command)->ActivePhase && ref.Slot < phase->Bindings.Size() && command->bRendering,
			"Prepared drawing uses a binding from the active render phase.");

		rhi::PreparedBinding const& binding = phase->Bindings[ref.Slot];
		ASSERT(binding.GraphicsPipeline.IsValid() &&
			!binding.bMeshShader &&
			draw.PushConstants.Size() == binding.PushConstantBytes &&
			matchesRendering(*command, m_State->m_GraphicsPipelines[binding.GraphicsPipeline.Slot]) &&
			command->ViewportCount == 1 &&
			command->ScissorCount == 1,
			"Prepared drawing matches its captured vertex pipeline and rendering state.");
		ASSERT(draw.VertexCount != 0 &&
			draw.InstanceCount != 0 &&
			draw.FirstVertex <= UINT32_MAX - draw.VertexCount &&
			draw.FirstInstance <= UINT32_MAX - draw.InstanceCount &&
			preparedVertexBounds(binding, uint64(draw.FirstVertex) + draw.VertexCount, uint64(draw.FirstInstance) + draw.InstanceCount),
			"Prepared drawing stays inside its captured vertex and instance ranges.");
		ASSERT(phase->Used.Draws < phase->Capacity.Draws, "Prepared drawing stays within its admitted capacity.");

		Result<void> result = recordNative(
			[&]
			{
				return backend::RecordPreparedDraw(m_State->m_Backend.State, command->Native, binding.NativePipeline, binding.NativeIndex,
					draw);
			});
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}

		++phase->Used.Draws;

		return Result<void>::Success();
	}

	Result<void> Device::DrawIndexed(CommandListHandle handle, Span<const PreparedIndexedDraw> draws)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		ASSERT(phase && phase->Used.Draws <= phase->Capacity.Draws && draws.Size() <= phase->Capacity.Draws - phase->Used.Draws, "Indexed draw batches stay inside their active phase capacity.");
		for (const rhi::PreparedIndexedDraw& entry : draws)
		{
			const PreparedBindingRef ref = entry.Binding;
			const DrawIndexedArguments& draw = entry.Draw;
			ASSERT(phase != nullptr && ref.List == handle && ref.Phase == preparedList(command)->ActivePhase && ref.Slot < phase->Bindings.Size() && command->bRendering,
				"Prepared indexed drawing uses a binding from the active render phase.");

			rhi::PreparedBinding const& binding = phase->Bindings[ref.Slot];
			ASSERT(binding.GraphicsPipeline.IsValid() &&
				!binding.bMeshShader &&
				binding.IndexBuffer.IsValid() &&
				draw.PushConstants.Size() == binding.PushConstantBytes &&
				matchesRendering(*command, m_State->m_GraphicsPipelines[binding.GraphicsPipeline.Slot]) &&
				command->ViewportCount == 1 &&
				command->ScissorCount == 1,
				"Prepared indexed drawing matches its captured indexed pipeline and rendering state.");
			ASSERT(draw.IndexCount != 0 &&
				draw.InstanceCount != 0 &&
				draw.FirstIndex <= UINT32_MAX - draw.IndexCount &&
				draw.FirstInstance <= UINT32_MAX - draw.InstanceCount &&
				preparedVertexBounds(binding, 1, uint64(draw.FirstInstance) + draw.InstanceCount),
				"Prepared indexed drawing stays inside its captured vertex and instance ranges.");
			const uint32 elementBytes = binding.IndexFormat == EFormat::R16_UINT ? 2 : 4;
			ASSERT(uint64(draw.FirstIndex) + draw.IndexCount <= binding.IndexRange.ByteCount / elementBytes, "Prepared indexed drawing stays inside its captured index range.");
		}

		Result<void> result = recordNative(
			[&]() -> Result<void>
			{
				for (const rhi::PreparedIndexedDraw& entry : draws)
				{
					rhi::PreparedBinding const& binding = phase->Bindings[entry.Binding.Slot];
					Result<void> emitted = backend::RecordDrawIndexedArguments( m_State->m_Backend.State, command->Native, binding.NativePipeline, binding.NativeIndex, entry.Draw);
					if (!emitted)
					{
						return emitted;
					}
					++phase->Used.Draws;
				}

				return Result<void>::Success();
			});
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::DrawIndexed(CommandListHandle handle, PreparedBindingRef ref, const DrawIndexedArguments& draw)
	{
		const PreparedIndexedDraw entry{ ref, draw };

		return DrawIndexed(handle, { &entry, 1 });
	}

	[[nodiscard]] Result<void> Device::resolveExtraVertexBuffers(
		const GraphicsPipelineSlot& pipeline,
		Span<const VertexBufferBinding> bindings,
		uint64 lastVertex,
		uint64 lastInstance,
		BufferHandle primary,
		BufferHandle index,
		BufferHandle arguments,
		BufferHandle count,
		Span<const BufferHandle> fixed,
		const DescriptorTableSlot* storageBuffers,
		const DescriptorTableSlot* uniformBuffers,
		const DescriptorTableSlot* readOnlyBuffers,
		NativeVertexBuffer(&native)[3])
	{
		ASSERT(bindings.Size() == pipeline.ExtraVertexBufferCount, "The extra vertex buffer count does not match the pipeline.");

		for (usize binding = 0; binding < bindings.Size(); ++binding)
		{
			const VertexBufferBinding& input = bindings[binding];
			BufferSlot* const buffer = findBuffer(input.Buffer);

			ASSERT(buffer != nullptr, "RHI operations require valid handles for this device.");

			const VertexBufferSlotDesc& declared = pipeline.ExtraVertexBufferSlots[binding];
			const uint64 elements = declared.bPerInstance ? lastInstance : lastVertex;

			ASSERT(HasUsage(buffer->Usage, EBufferUsage::Vertex) &&
				!buffer->bMapped &&
				input.OffsetBytes <= buffer->ByteCount &&
				elements * declared.StrideBytes <= buffer->ByteCount - input.OffsetBytes &&
				input.Buffer != primary &&
				input.Buffer != index &&
				input.Buffer != arguments &&
				input.Buffer != count,
				"RHI binding arguments must match their declared resources and ranges.");

			for (usize previous = 0; previous < binding; ++previous)
			{
				ASSERT(bindings[previous].Buffer != input.Buffer, "RHI binding arguments must match their declared resources and ranges.");
			}

			for (BufferHandle handle : fixed)
			{
				ASSERT(handle != input.Buffer, "RHI binding arguments must match their declared resources and ranges.");
			}

			Result<void> aliases = checkGraphicsBufferTableAliases(storageBuffers, fixed, input.Buffer, index, arguments, count);

			if (!aliases)
			{
				return aliases;
			}
			aliases = checkGraphicsBufferTableAliases(uniformBuffers, fixed, input.Buffer, index, arguments, count);

			if (!aliases)
			{
				return aliases;
			}
			aliases = checkGraphicsBufferTableAliases(readOnlyBuffers, fixed, input.Buffer, index, arguments, count);

			if (!aliases)
			{
				return aliases;
			}
			native[binding] = { buffer->Native, input.OffsetBytes, buffer->ByteCount, declared.StrideBytes };
		}

		return Result<void>::Success();
	}

	void Device::trackExtraVertexBuffers(CommandSlot& command, Span<const VertexBufferBinding> bindings) noexcept
	{
		for (const VertexBufferBinding& binding : bindings)
		{
			BufferSlot& buffer = m_State->m_Buffers[binding.Buffer.Slot];
			(void)command.AddBufferUse({ binding.Buffer, buffer.Native, ETrackedResourceState::Vertex });
			++buffer.RecordedUses;
		}
	}

	[[nodiscard]] Result<void> Device::validateShadingRate(
		EShadingRate rate,
		EShadingRateCombiner primitive,
		EShadingRateCombiner image,
		bool bHasImage,
		uint32 sampleCount) const noexcept
	{
		ASSERT(rate <= EShadingRate::Rate4x4, "The shading rate is outside the RHI enumeration.");

		ASSERT(primitive <= EShadingRateCombiner::ApplyRelative &&
			image <= EShadingRateCombiner::ApplyRelative &&
			(image == EShadingRateCombiner::Passthrough || bHasImage),
			"The combiner or shading rate image is invalid.");

		if (primitive != EShadingRateCombiner::Passthrough && !m_State->m_Backend.Capabilities.Features.bPrimitiveShadingRate)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ShadingRate", "Primitive shading rates are unavailable.");
		}

		if (!m_State->m_Backend.Capabilities.Features.bShadingRateNonTrivialCombiners && (primitive > EShadingRateCombiner::Override || image > EShadingRateCombiner::Override))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ShadingRate", "Nontrivial shading rate combiners are unavailable.");
		}

		if (rate == EShadingRate::Rate1x1)
		{
			return Result<void>::Success();
		}

		const uint32 sampleIndex = sampleCount == 1 ? 0 : sampleCount == 2 ? 1 : sampleCount == 4 ? 2 : 3;
		const uint32 bit = 1u << static_cast<uint32>(rate);

		if ((m_State->m_Backend.Capabilities.Limits.SupportedShadingRates[sampleIndex] & bit) == 0)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.ShadingRate", "The device does not support this rate at the pipeline sample count.");
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::drawInternal(CommandListHandle list, const GraphicsState& drawState, const DrawArguments& draw, bool bMesh)
	{
		CommandSlot* const command = findCommand(list);
		GraphicsPipelineSlot* const pipeline = findGraphicsPipeline(drawState.Pipeline);

		ASSERT(!(command == nullptr || pipeline == nullptr), "RHI operations require valid handles for this device.");

		Result<SharedPtr<BoundDescriptorTables>> heapBindings = captureDescriptorHeapBindings(drawState.Resources.DeviceHeaps, pipeline->bDeviceHeapIndexed);
		if (!heapBindings)
		{
			ASSERT(heapBindings.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(heapBindings.GetError());
		}

		Result<void> heapValid = validateOrdinaryDeviceHeapUses(*command, heapBindings.Value().Get(), drawState.Resources.Sets, {});
		if (!heapValid)
		{
			ASSERT(heapValid.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return heapValid;
		}

		ASSERT(pipeline->bMeshShader == bMesh, "The pipeline must match the graphics command family.");

		Result<AccelStructSlot*> accelResult = resolveAccelStruct(pipeline->AccelStructSlot, AccelStructHandle{});

		if (!accelResult)
		{
			ASSERT(accelResult.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(accelResult.GetError());
		}
		AccelStructSlot* const accel = accelResult.Value();
		const bool bHasImage = command->ActiveFramebuffer.IsValid() && m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].ShadingRateImage.IsValid();
		Result<void> shadingRate = validateShadingRate(drawState.ShadingRate, drawState.PrimitiveShadingRateCombiner, drawState.ImageShadingRateCombiner, bHasImage, pipeline->SampleCount);

		if (!shadingRate)
		{
			ASSERT(shadingRate.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return shadingRate;
		}

		ASSERT(!(!command->bRendering ||
				draw.VertexCount == 0 ||
				draw.InstanceCount == 0 ||
				draw.FirstVertex > NumericLimits<uint32>::max() - draw.VertexCount ||
				draw.FirstInstance > NumericLimits<uint32>::max() - draw.InstanceCount ||
				!matchesRendering(*command, *pipeline) ||
				draw.PushConstants.Size() != pipeline->PushConstantBytes ||
				!isValidBlendConstant(drawState.BlendConstant)),
			"Draw arguments and pipeline must match the active rendering pass.");
		BufferSlot* vertex = nullptr;

		if (pipeline->VertexStrideBytes != 0)
		{
			vertex = findBuffer(drawState.VertexBuffer);

			ASSERT(vertex != nullptr, "RHI operations require valid handles for this device.");

			const uint64 lastVertex = static_cast<uint64>(draw.FirstVertex) + draw.VertexCount;

			ASSERT(HasUsage(vertex->Usage, EBufferUsage::Vertex) &&
				drawState.VertexBufferOffsetBytes <= vertex->ByteCount &&
				lastVertex * pipeline->VertexStrideBytes <= vertex->ByteCount - drawState.VertexBufferOffsetBytes,
				"Draw vertex buffer usage and range must be valid.");
		}
		else
		{
			ASSERT(!drawState.VertexBuffer.IsValid() && drawState.VertexBufferOffsetBytes == 0, "A zero-stride pipeline requires no vertex stream.");
		}
		FixedGraphicsBindings fixed{};
		Result<void> fixedResult =
			resolveFixedGraphicsBindings(drawState.Resources.Sets, drawState.ExtraVertexBuffers, *command, *pipeline, list.Slot, drawState.VertexBuffer, {}, {}, {}, fixed);
		if (!fixedResult)
		{
			ASSERT(fixedResult.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return fixedResult;
		}

		const Span<const TextureHandle> textures{ fixed.Sampled, fixed.SampledCount };
		const Span<const TextureHandle> fixedStorage{ fixed.Storage, fixed.StorageCount };
		const Span<const BufferHandle> buffers{ fixed.Buffers, fixed.BufferCount };
		NativeSampledTexture(&sampled)[32] = fixed.NativeSampled;
		NativeStorageTexture(&nativeStorage)[32] = fixed.NativeStorage;
		NativeGraphicsBuffer(&nativeBuffers)[32] = fixed.NativeBuffers;
		Result<DescriptorTableSlot*> bindless =
			resolveGraphicsDescriptorTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!bindless)
		{
			ASSERT(bindless.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(bindless.GetError());
		}

		Result<DescriptorTableSlot*> bindlessSamplers =
			resolveGraphicsSamplerTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!bindlessSamplers)
		{
			ASSERT(bindlessSamplers.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(bindlessSamplers.GetError());
		}

		Result<DescriptorTableSlot*> storageBuffers =
			resolveGraphicsStorageBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!storageBuffers)
		{
			ASSERT(storageBuffers.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(storageBuffers.GetError());
		}

		Result<DescriptorTableSlot*> storageTextures =
			resolveGraphicsStorageTextureTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!storageTextures)
		{
			ASSERT(storageTextures.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(storageTextures.GetError());
		}

		Result<DescriptorTableSlot*> uniformBuffers =
			resolveGraphicsUniformBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!uniformBuffers)
		{
			ASSERT(uniformBuffers.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(uniformBuffers.GetError());
		}

		Result<DescriptorTableSlot*> readOnlyBuffers =
			resolveGraphicsReadOnlyBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!readOnlyBuffers)
		{
			ASSERT(readOnlyBuffers.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(readOnlyBuffers.GetError());
		}

		Result<void> readOnlyAliases = checkBufferTableAliases(readOnlyBuffers.Value(), buffers);

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}
		readOnlyAliases = checkBufferTableOverlap(storageBuffers.Value(), readOnlyBuffers.Value());

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}
		readOnlyAliases = checkBufferTableOverlap(uniformBuffers.Value(), readOnlyBuffers.Value());

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}

		Result<void> aliases = checkGraphicsBufferTableAliases(uniformBuffers.Value(), buffers, drawState.VertexBuffer);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}

		Result<void> readOnlyGraphicsAliases = checkGraphicsBufferTableAliases(readOnlyBuffers.Value(), buffers, drawState.VertexBuffer);

		if (!readOnlyGraphicsAliases)
		{
			ASSERT(readOnlyGraphicsAliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyGraphicsAliases;
		}
		aliases = checkGraphicsBufferTableAliases(storageBuffers.Value(), buffers, drawState.VertexBuffer);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkBufferTableOverlap(storageBuffers.Value(), uniformBuffers.Value());

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkStorageTableTextureAliases(storageTextures.Value(), bindless.Value());

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkGraphicsFixedTextureTableAliases(bindless.Value(), storageTextures.Value(), textures, fixedStorage);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		NativeVertexBuffer extra[3]{};
		Result<void> extraChecked =
			resolveExtraVertexBuffers(*pipeline, drawState.ExtraVertexBuffers, static_cast<uint64>(draw.FirstVertex) + draw.VertexCount,
				static_cast<uint64>(draw.FirstInstance) + draw.InstanceCount, drawState.VertexBuffer, {}, {}, {},
				buffers, storageBuffers.Value(), uniformBuffers.Value(), readOnlyBuffers.Value(), extra);

		if (!extraChecked)
		{
			ASSERT(extraChecked.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return extraChecked;
		}

		if (!reserveBindingTableUses(*command, bindless.Value(), bindlessSamplers.Value(), storageBuffers.Value(), storageTextures.Value(),
			uniformBuffers.Value(), readOnlyBuffers.Value(), textures.Size() + fixedStorage.Size(),
			(vertex != nullptr ? 1 : 0) + drawState.ExtraVertexBuffers.Size() + buffers.Size() +
			(accel != nullptr ? 1 : 0),
			singleBindingSet(drawState.Resources.Sets)))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track graphics descriptor table resources.");
		}

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + (vertex != nullptr ? 1 : 0) +
			drawState.ExtraVertexBuffers.Size() +
			buffers.Size() + fixed.AccelCount + (accel != nullptr ? 1u : 0u)))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track the vertex buffer.");
		}

		if (!command->TextureUses.Reserve(command->TextureUses.Size() + textures.Size() + fixedStorage.Size()) ||
			!reserveAccelStructUse(*command, accel) ||
			!command->AccelUses.Reserve(command->AccelUses.Size() + fixed.AccelCount + (accel != nullptr ? 1u : 0u)) ||
			!command->GraphicsPipelineUses.Add({ drawState.Pipeline, pipeline->Native }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track the graphics pipeline.");
		}

		if (!drawState.ExtraVertexBuffers.IsEmpty())
		{
			Result<void> bound =
				backend::RecordBindExtraVertexBuffers(m_State->m_Backend.State, command->Native, { extra, drawState.ExtraVertexBuffers.Size() });
			if (!bound)
			{
				command->GraphicsPipelineUses.PopBack();
				poisonCommandList(list);

				return bound;
			}
		}
		trackGraphicsTextures(*command, list.Slot, textures);
		trackGraphicsTextures(*command, list.Slot, fixedStorage, ETrackedResourceState::Storage);
		trackGraphicsBindingSets(*command, drawState.Resources.Sets);
		trackGraphicsBuffers(*command, *pipeline, buffers);
		trackAccelStruct(*command, AccelStructHandle{}, accel);
		trackExtraVertexBuffers(*command, drawState.ExtraVertexBuffers);
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, bindless.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, bindlessSamplers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, storageBuffers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, storageTextures.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, uniformBuffers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, readOnlyBuffers.Value());

		if (vertex != nullptr)
		{
			(void)command->AddBufferUse({ drawState.VertexBuffer, vertex->Native, ETrackedResourceState::Vertex });
			++vertex->RecordedUses;
		}
		trackDeviceHeapUses(*command, heapBindings.Value().Get());
		++pipeline->RecordedUses;
		Result<void> recorded = recordNative(
			[&]
			{
				Result<void> heapsRecorded = recordDescriptorHeapBindings(*command, true, heapBindings.Value().Get());
				if (!heapsRecorded)
				{
					return heapsRecorded;
				}

				Result<void> rangesRecorded = recordRangeBindingSets(*command, pipeline->Native, true, drawState.Resources.Sets);
				if (!rangesRecorded)
				{
					return rangesRecorded;
				}

				return backend::RecordDraw(
					m_State->m_Backend.State, command->Native, pipeline->Native, vertex != nullptr ? vertex->Native : nullptr,
					drawState.VertexBufferOffsetBytes, vertex != nullptr ? vertex->ByteCount : 0, pipeline->VertexStrideBytes, { sampled, textures.Size() },
					{ nativeStorage, fixedStorage.Size() }, { nativeBuffers, buffers.Size() },
					bindless.Value() != nullptr ? bindless.Value()->Native : nullptr,
					bindlessSamplers.Value() != nullptr ? bindlessSamplers.Value()->Native : nullptr,
					storageBuffers.Value() != nullptr ? storageBuffers.Value()->Native : nullptr,
					storageTextures.Value() != nullptr ? storageTextures.Value()->Native : nullptr,
					uniformBuffers.Value() != nullptr ? uniformBuffers.Value()->Native : nullptr,
					readOnlyBuffers.Value() != nullptr ? readOnlyBuffers.Value()->Native : nullptr,
					accel != nullptr ? accel->Native : nullptr, drawState, draw);
			});
		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::Draw(CommandListHandle list, const GraphicsState& drawState, const DrawArguments& draw)
	{
		CallScope call(*this, true);

		return drawInternal(list, drawState, draw, false);
	}

	[[nodiscard]] bool Device::meshGroupsFit(uint32 x, uint32 y, uint32 z, const uint32* maximum, uint32 total) noexcept
	{
		if (x == 0 || y == 0 || z == 0 || x > maximum[0] || y > maximum[1] || z > maximum[2] || x > total || y > total / x)
		{
			return false;
		}

		return z <= total / (x * y);
	}

	[[nodiscard]] Result<void> Device::DrawIndexed(CommandListHandle list, const GraphicsState& drawState, const DrawIndexedArguments& draw)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		GraphicsPipelineSlot* const pipeline = findGraphicsPipeline(drawState.Pipeline);
		BufferSlot* const vertex = findBuffer(drawState.VertexBuffer);
		BufferSlot* const index = findBuffer(drawState.IndexBuffer);

		ASSERT(!(command == nullptr || pipeline == nullptr || index == nullptr || (pipeline->VertexStrideBytes != 0 && vertex == nullptr)), "RHI operations require valid handles for this device.");

		Result<SharedPtr<BoundDescriptorTables>> heapBindings = captureDescriptorHeapBindings(drawState.Resources.DeviceHeaps, pipeline->bDeviceHeapIndexed);
		if (!heapBindings)
		{
			ASSERT(heapBindings.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(heapBindings.GetError());
		}

		Result<void> heapValid = validateOrdinaryDeviceHeapUses(*command, heapBindings.Value().Get(), drawState.Resources.Sets, {});
		if (!heapValid)
		{
			ASSERT(heapValid.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return heapValid;
		}

		ASSERT(!pipeline->bMeshShader, "A bMesh pipeline cannot use indexed drawing.");

		Result<AccelStructSlot*> accelResult = resolveAccelStruct(pipeline->AccelStructSlot, AccelStructHandle{});

		if (!accelResult)
		{
			ASSERT(accelResult.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(accelResult.GetError());
		}
		AccelStructSlot* const accel = accelResult.Value();
		const bool bHasImage = command->ActiveFramebuffer.IsValid() && m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].ShadingRateImage.IsValid();
		Result<void> shadingRate = validateShadingRate(drawState.ShadingRate, drawState.PrimitiveShadingRateCombiner, drawState.ImageShadingRateCombiner, bHasImage, pipeline->SampleCount);

		if (!shadingRate)
		{
			ASSERT(shadingRate.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return shadingRate;
		}

		const uint64 indexSize = drawState.IndexFormat == EFormat::R16_UINT ? 2 : 4;

		ASSERT(!(!command->bRendering ||
				!matchesRendering(*command, *pipeline) ||
				draw.PushConstants.Size() != pipeline->PushConstantBytes ||
				!isValidBlendConstant(drawState.BlendConstant) ||
				!HasUsage(index->Usage, EBufferUsage::Index) ||
				(drawState.IndexFormat != EFormat::R16_UINT && drawState.IndexFormat != EFormat::R32_UINT) ||
				draw.IndexCount == 0 ||
				draw.InstanceCount == 0 ||
				draw.FirstIndex > NumericLimits<uint32>::max() - draw.IndexCount ||
				draw.FirstInstance > NumericLimits<uint32>::max() - draw.InstanceCount ||
				(pipeline->VertexStrideBytes != 0 &&
				 (!HasUsage(vertex->Usage, EBufferUsage::Vertex) ||
				  drawState.VertexBufferOffsetBytes > vertex->ByteCount ||
				  pipeline->VertexStrideBytes > vertex->ByteCount - drawState.VertexBufferOffsetBytes)) ||
				(pipeline->VertexStrideBytes == 0 && (drawState.VertexBuffer.Owner != 0 || drawState.VertexBufferOffsetBytes != 0)) ||
				drawState.IndexBufferOffsetBytes > index->ByteCount ||
				drawState.IndexBufferOffsetBytes % indexSize != 0 ||
				(static_cast<uint64>(draw.FirstIndex) + draw.IndexCount) * indexSize > index->ByteCount - drawState.IndexBufferOffsetBytes),
			"Indexed draw arguments and pipeline must match the active rendering pass.");
		FixedGraphicsBindings fixed{};
		Result<void> fixedResult =
			resolveFixedGraphicsBindings(drawState.Resources.Sets, drawState.ExtraVertexBuffers, *command, *pipeline, list.Slot, drawState.VertexBuffer,
				drawState.IndexBuffer, {}, {}, fixed);
		if (!fixedResult)
		{
			ASSERT(fixedResult.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return fixedResult;
		}

		const Span<const TextureHandle> textures{ fixed.Sampled, fixed.SampledCount };
		const Span<const TextureHandle> fixedStorage{ fixed.Storage, fixed.StorageCount };
		const Span<const BufferHandle> buffers{ fixed.Buffers, fixed.BufferCount };
		NativeSampledTexture(&sampled)[32] = fixed.NativeSampled;
		NativeStorageTexture(&nativeStorage)[32] = fixed.NativeStorage;
		NativeGraphicsBuffer(&nativeBuffers)[32] = fixed.NativeBuffers;
		Result<DescriptorTableSlot*> bindless =
			resolveGraphicsDescriptorTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!bindless)
		{
			ASSERT(bindless.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(bindless.GetError());
		}

		Result<DescriptorTableSlot*> bindlessSamplers =
			resolveGraphicsSamplerTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!bindlessSamplers)
		{
			ASSERT(bindlessSamplers.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(bindlessSamplers.GetError());
		}

		Result<DescriptorTableSlot*> storageBuffers =
			resolveGraphicsStorageBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!storageBuffers)
		{
			ASSERT(storageBuffers.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(storageBuffers.GetError());
		}

		Result<DescriptorTableSlot*> storageTextures =
			resolveGraphicsStorageTextureTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!storageTextures)
		{
			ASSERT(storageTextures.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(storageTextures.GetError());
		}

		Result<DescriptorTableSlot*> uniformBuffers =
			resolveGraphicsUniformBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!uniformBuffers)
		{
			ASSERT(uniformBuffers.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(uniformBuffers.GetError());
		}

		Result<DescriptorTableSlot*> readOnlyBuffers =
			resolveGraphicsReadOnlyBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!readOnlyBuffers)
		{
			ASSERT(readOnlyBuffers.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(readOnlyBuffers.GetError());
		}

		Result<void> readOnlyAliases = checkBufferTableAliases(readOnlyBuffers.Value(), buffers);

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}
		readOnlyAliases = checkBufferTableOverlap(storageBuffers.Value(), readOnlyBuffers.Value());

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}
		readOnlyAliases = checkBufferTableOverlap(uniformBuffers.Value(), readOnlyBuffers.Value());

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}

		Result<void> aliases = checkGraphicsBufferTableAliases(uniformBuffers.Value(), buffers, drawState.VertexBuffer, drawState.IndexBuffer);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}

		Result<void> readOnlyGraphicsAliases = checkGraphicsBufferTableAliases(readOnlyBuffers.Value(), buffers, drawState.VertexBuffer, drawState.IndexBuffer);

		if (!readOnlyGraphicsAliases)
		{
			ASSERT(readOnlyGraphicsAliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyGraphicsAliases;
		}
		aliases = checkGraphicsBufferTableAliases(storageBuffers.Value(), buffers, drawState.VertexBuffer, drawState.IndexBuffer);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkBufferTableOverlap(storageBuffers.Value(), uniformBuffers.Value());

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkStorageTableTextureAliases(storageTextures.Value(), bindless.Value());

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkGraphicsFixedTextureTableAliases(bindless.Value(), storageTextures.Value(), textures, fixedStorage);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		NativeVertexBuffer extra[3]{};
		Result<void> extraChecked = resolveExtraVertexBuffers(
			*pipeline, drawState.ExtraVertexBuffers, 1, static_cast<uint64>(draw.FirstInstance) + draw.InstanceCount,
			drawState.VertexBuffer, drawState.IndexBuffer, {}, {}, buffers, storageBuffers.Value(), uniformBuffers.Value(), readOnlyBuffers.Value(), extra);

		if (!extraChecked)
		{
			ASSERT(extraChecked.GetError().Code != EErrorCode::InvalidArgument, "Draw bindings and state must be valid.");
			poisonCommandList(list);

			return extraChecked;
		}

		if (!reserveBindingTableUses(*command, bindless.Value(), bindlessSamplers.Value(), storageBuffers.Value(), storageTextures.Value(),
			uniformBuffers.Value(), readOnlyBuffers.Value(), textures.Size() + fixedStorage.Size(),
			2 + drawState.ExtraVertexBuffers.Size() + buffers.Size() + (accel != nullptr ? 1 : 0),
			singleBindingSet(drawState.Resources.Sets)))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track graphics descriptor table resources.");
		}

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + 2 + drawState.ExtraVertexBuffers.Size() + buffers.Size() + fixed.AccelCount + (accel != nullptr ? 1u : 0u)) ||
			!command->TextureUses.Reserve(command->TextureUses.Size() + textures.Size() + fixedStorage.Size()) ||
			!reserveAccelStructUse(*command, accel) ||
			!command->AccelUses.Reserve(command->AccelUses.Size() + fixed.AccelCount + (accel != nullptr ? 1u : 0u)) ||
			!command->GraphicsPipelineUses.Add({ drawState.Pipeline, pipeline->Native }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track indexed draw resources.");
		}

		if (!drawState.ExtraVertexBuffers.IsEmpty())
		{
			Result<void> bound =
				backend::RecordBindExtraVertexBuffers(m_State->m_Backend.State, command->Native, { extra, drawState.ExtraVertexBuffers.Size() });
			if (!bound)
			{
				command->GraphicsPipelineUses.PopBack();
				poisonCommandList(list);

				return bound;
			}
		}
		trackGraphicsTextures(*command, list.Slot, textures);
		trackGraphicsTextures(*command, list.Slot, fixedStorage, ETrackedResourceState::Storage);
		trackGraphicsBindingSets(*command, drawState.Resources.Sets);
		trackGraphicsBuffers(*command, *pipeline, buffers);
		trackAccelStruct(*command, AccelStructHandle{}, accel);
		trackExtraVertexBuffers(*command, drawState.ExtraVertexBuffers);
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, bindless.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, bindlessSamplers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, storageBuffers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, storageTextures.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, uniformBuffers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, readOnlyBuffers.Value());

		if (drawState.VertexBuffer == drawState.IndexBuffer)
		{
			(void)command->AddBufferUse({ drawState.VertexBuffer, vertex->Native, ETrackedResourceState::Vertex | ETrackedResourceState::Index });
			++vertex->RecordedUses;
		}
		else
		{
			if (vertex != nullptr)
			{
				(void)command->AddBufferUse({ drawState.VertexBuffer, vertex->Native, ETrackedResourceState::Vertex });
				++vertex->RecordedUses;
			}
			(void)command->AddBufferUse({ drawState.IndexBuffer, index->Native, ETrackedResourceState::Index });
			++index->RecordedUses;
		}
		trackDeviceHeapUses(*command, heapBindings.Value().Get());
		++pipeline->RecordedUses;
		Result<void> recorded = recordNative(
			[&]
			{
				Result<void> heapsRecorded = recordDescriptorHeapBindings(*command, true, heapBindings.Value().Get());
				if (!heapsRecorded)
				{
					return heapsRecorded;
				}

				Result<void> rangesRecorded = recordRangeBindingSets(*command, pipeline->Native, true, drawState.Resources.Sets);
				if (!rangesRecorded)
				{
					return rangesRecorded;
				}

				return backend::RecordDrawIndexed(
					m_State->m_Backend.State, command->Native, pipeline->Native, vertex != nullptr ? vertex->Native : nullptr, index->Native,
					drawState.VertexBufferOffsetBytes, vertex != nullptr ? vertex->ByteCount : 0, drawState.IndexBufferOffsetBytes, pipeline->VertexStrideBytes,
					{ sampled, textures.Size() }, { nativeStorage, fixedStorage.Size() }, { nativeBuffers, buffers.Size() },
					bindless.Value() != nullptr ? bindless.Value()->Native : nullptr,
					bindlessSamplers.Value() != nullptr ? bindlessSamplers.Value()->Native : nullptr,
					storageBuffers.Value() != nullptr ? storageBuffers.Value()->Native : nullptr,
					storageTextures.Value() != nullptr ? storageTextures.Value()->Native : nullptr,
					uniformBuffers.Value() != nullptr ? uniformBuffers.Value()->Native : nullptr,
					readOnlyBuffers.Value() != nullptr ? readOnlyBuffers.Value()->Native : nullptr,
					accel != nullptr ? accel->Native : nullptr, drawState, draw);
			});
		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}
}
