#include "DeviceState.h"

namespace rox::rhi
{
	using namespace core;

	[[nodiscard]] Result<void> Device::recordIndirectPrepared(CommandListHandle handle, PreparedIndirectRef ref, EPreparedIndirectKind kind, Span<const Byte> pushConstants)
	{
		CallScope call(*this, true, true);
		CommandSlot* command = findCommand(handle);
		PreparedRecordingPhase* phase = preparedPhase(command);
		const bool bCompute = kind == EPreparedIndirectKind::Compute;
		ASSERT(phase != nullptr && ref.List == handle && ref.Phase == preparedList(command)->ActivePhase && ref.Slot < phase->Indirects.Size() && command->bRendering != bCompute,
			"Prepared indirect operations require a matching selector and rendering state.");

		rhi::PreparedRecordingIndirect const& indirect = phase->Indirects[ref.Slot];
		rhi::PreparedBinding const& binding = phase->Bindings[indirect.Description.BindingSlot];
		ASSERT(indirect.Description.Kind == kind &&
			pushConstants.Size() == binding.PushConstantBytes &&
			(bCompute || (matchesRendering(*command, m_State->m_GraphicsPipelines[binding.GraphicsPipeline.Slot]) && command->ViewportCount == 1 && command->ScissorCount == 1)),
			"Prepared indirect operations require matching kind, constants, and rendering state.");

		const uint32 units = indirect.Description.MaximumCount;
		uint32& used = bCompute ? phase->Used.Dispatches : phase->Used.Draws;
		const uint32 capacity = bCompute ? phase->Capacity.Dispatches : phase->Capacity.Draws;
		ASSERT(used <= capacity && units <= capacity - used, "Prepared indirect operations stay within their admitted capacity.");

		Result<void> result = recordNative(
			[&]
			{
				return backend::RecordPreparedIndirect(m_State->m_Backend.State, command->Native, binding.NativePipeline, binding.NativeIndex,
					indirect.Native, pushConstants);
			});
		if (!result)
		{
			return preparedEmission(*command, Move(result));
		}
		used += units;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::DispatchComputeIndirect(CommandListHandle handle, PreparedIndirectRef ref, Span<const Byte> pushConstants)
	{
		return recordIndirectPrepared(handle, ref, EPreparedIndirectKind::Compute, pushConstants);
	}

	[[nodiscard]] Result<void> Device::DrawIndirect(CommandListHandle handle, PreparedIndirectRef ref, Span<const Byte> pushConstants)
	{
		return recordIndirectPrepared(handle, ref, EPreparedIndirectKind::Draw, pushConstants);
	}

	[[nodiscard]] Result<void> Device::DrawIndexedIndirect(CommandListHandle handle, PreparedIndirectRef ref, Span<const Byte> pushConstants)
	{
		return recordIndirectPrepared(handle, ref, EPreparedIndirectKind::DrawIndexed, pushConstants);
	}

	[[nodiscard]] Result<void> Device::DispatchMeshIndirect(CommandListHandle handle, PreparedIndirectRef ref, Span<const Byte> pushConstants)
	{
		return recordIndirectPrepared(handle, ref, EPreparedIndirectKind::Mesh, pushConstants);
	}

	[[nodiscard]] Result<void> Device::DispatchComputeIndirect(CommandListHandle list, const ComputeDispatchIndirect& dispatch)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		PipelineSlot* const pipeline = findPipeline(dispatch.Pipeline);
		BufferSlot* const arguments = findBuffer(dispatch.Arguments);

		ASSERT(command != nullptr && pipeline != nullptr && arguments != nullptr, "RHI operations require valid handles for this device.");
		Result<void> recorded = dispatchComputeMultiple(list, *pipeline, dispatch.Pipeline, dispatch.Bindings.Sets,
			dispatch.PushConstants, 0, 0, 0, dispatch.Arguments,
			dispatch.ArgumentsOffsetBytes,
			{}, {}, {}, {}, {}, {}, {},
			dispatch.Bindings.DeviceHeaps);
		if (!recorded)
		{
			ASSERT(recorded.GetError().Code != EErrorCode::InvalidArgument, "Indirect dispatch arguments and bindings must be valid.");
			poisonCommandList(list);
		}

		return recorded;
	}

	[[nodiscard]] Result<void> Device::drawIndirectInternal(CommandListHandle list, const GraphicsDrawIndirect& draw, bool bMesh)
	{
		CommandSlot* const command = findCommand(list);
		GraphicsPipelineSlot* const pipeline = findGraphicsPipeline(draw.Pipeline);
		BufferSlot* const arguments = findBuffer(draw.Arguments);
		const bool bCounted = draw.CountBuffer.IsValid();
		BufferSlot* const count = bCounted ? findBuffer(draw.CountBuffer) : nullptr;

		ASSERT(!(command == nullptr || pipeline == nullptr || arguments == nullptr || (bCounted && count == nullptr)), "RHI operations require valid handles for this device.");

		Result<SharedPtr<BoundDescriptorTables>> heapBindings = captureDescriptorHeapBindings(draw.Bindings.DeviceHeaps, pipeline->bDeviceHeapIndexed);
		if (!heapBindings)
		{
			ASSERT(heapBindings.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(heapBindings.GetError());
		}

		Result<void> heapValid = validateOrdinaryDeviceHeapUses(*command, heapBindings.Value().Get(), draw.Bindings.Sets, {});
		if (!heapValid)
		{
			ASSERT(heapValid.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return heapValid;
		}

		ASSERT(pipeline->bMeshShader == bMesh, "The pipeline must match the indirect command family.");

		Result<AccelStructSlot*> accelResult = resolveAccelStruct(pipeline->AccelStructSlot, AccelStructHandle{});

		if (!accelResult)
		{
			ASSERT(accelResult.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(accelResult.GetError());
		}
		AccelStructSlot* const accel = accelResult.Value();

		if (bMesh && !m_State->m_Backend.Capabilities.Features.bMeshIndirect)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.MeshShader", "Indirect bMesh dispatch is unavailable on this device.");
		}

		ASSERT(!bMesh || (!bCounted && draw.DrawCount == 1), "Indirect bMesh dispatch requires one argument record.");

		const bool bHasImage = command->ActiveFramebuffer.IsValid() && m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].ShadingRateImage.IsValid();
		Result<void> shadingRate = validateShadingRate(draw.ShadingRate, draw.PrimitiveShadingRateCombiner, draw.ImageShadingRateCombiner, bHasImage, pipeline->SampleCount);

		if (!shadingRate)
		{
			ASSERT(shadingRate.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return shadingRate;
		}

		if (bCounted && !m_State->m_Backend.Capabilities.Features.bDrawIndirectCount)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.DrawIndirectCount", "The device does not support indirect draw counts.");
		}

		ASSERT(!(!command->bRendering ||
				draw.DrawCount == 0 ||
				draw.DrawCount > 65535 ||
				(bCounted && draw.DrawCount > m_State->m_Backend.Capabilities.Limits.MaximumCountedIndirectDraws) ||
				!matchesRendering(*command, *pipeline) ||
				draw.PushConstants.Size() != pipeline->PushConstantBytes ||
				!isValidBlendConstant(draw.BlendConstant) ||
				!HasUsage(arguments->Usage, EBufferUsage::Indirect) ||
				(bCounted &&
						(!HasUsage(count->Usage, EBufferUsage::Indirect) ||
								draw.CountBuffer == draw.Arguments ||
								draw.CountBuffer == draw.VertexBuffer ||
								draw.CountBufferOffsetBytes % 4 != 0 ||
								!Fits(count->ByteCount, draw.CountBufferOffsetBytes, sizeof(uint32)))) ||
				(!bCounted && draw.CountBufferOffsetBytes != 0) ||
				draw.ArgumentsOffsetBytes % 4 != 0 ||
				!Fits(arguments->ByteCount, draw.ArgumentsOffsetBytes, static_cast<uint64>(draw.DrawCount) * (bMesh ? sizeof(IndirectMeshArguments) : sizeof(IndirectDrawArguments)))),
			"Indirect draw arguments and count ranges must match the pipeline and buffer usage.");
		BufferSlot* vertex = nullptr;

		if (pipeline->VertexStrideBytes != 0)
		{
			vertex = findBuffer(draw.VertexBuffer);

			ASSERT(vertex != nullptr, "RHI operations require valid handles for this device.");

			ASSERT(HasUsage(vertex->Usage, EBufferUsage::Vertex) && draw.VertexBuffer != draw.Arguments && Fits(vertex->ByteCount, draw.VertexBufferOffsetBytes, pipeline->VertexStrideBytes),
				"Indirect draw requires a valid vertex range.");
		}
		else
		{
			ASSERT(!draw.VertexBuffer.IsValid() && draw.VertexBufferOffsetBytes == 0, "A zero-stride pipeline requires no vertex stream.");
		}
		FixedGraphicsBindings fixed{};
		Result<void> fixedResult = resolveFixedGraphicsBindings(draw.Bindings.Sets, draw.ExtraVertexBuffers, *command, *pipeline, list.Slot, draw.VertexBuffer, {},
			draw.Arguments, draw.CountBuffer, fixed);

		if (!fixedResult)
		{
			ASSERT(fixedResult.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
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
			ASSERT(bindless.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(bindless.GetError());
		}

		Result<DescriptorTableSlot*> bindlessSamplers =
			resolveGraphicsSamplerTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!bindlessSamplers)
		{
			ASSERT(bindlessSamplers.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(bindlessSamplers.GetError());
		}

		Result<DescriptorTableSlot*> storageBuffers =
			resolveGraphicsStorageBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!storageBuffers)
		{
			ASSERT(storageBuffers.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(storageBuffers.GetError());
		}

		Result<DescriptorTableSlot*> storageTextures =
			resolveGraphicsStorageTextureTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!storageTextures)
		{
			ASSERT(storageTextures.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(storageTextures.GetError());
		}

		Result<DescriptorTableSlot*> uniformBuffers =
			resolveGraphicsUniformBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!uniformBuffers)
		{
			ASSERT(uniformBuffers.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(uniformBuffers.GetError());
		}

		Result<DescriptorTableSlot*> readOnlyBuffers =
			resolveGraphicsReadOnlyBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!readOnlyBuffers)
		{
			ASSERT(readOnlyBuffers.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(readOnlyBuffers.GetError());
		}

		Result<void> readOnlyAliases = checkBufferTableAliases(readOnlyBuffers.Value(), buffers);

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}
		readOnlyAliases = checkBufferTableOverlap(storageBuffers.Value(), readOnlyBuffers.Value());

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}
		readOnlyAliases = checkBufferTableOverlap(uniformBuffers.Value(), readOnlyBuffers.Value());

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}

		if (bCounted)
		{
			for (const BufferHandle buffer : buffers)
			{
				ASSERT(buffer != draw.CountBuffer, "The count buffer cannot also be bound for shader access.");
			}
		}

		Result<void> aliases =
			checkGraphicsBufferTableAliases(uniformBuffers.Value(), buffers, draw.VertexBuffer, {}, draw.Arguments, draw.CountBuffer);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}

		Result<void> readOnlyGraphicsAliases =
			checkGraphicsBufferTableAliases(readOnlyBuffers.Value(), buffers, draw.VertexBuffer, {}, draw.Arguments, draw.CountBuffer);
		if (!readOnlyGraphicsAliases)
		{
			ASSERT(readOnlyGraphicsAliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyGraphicsAliases;
		}
		aliases = checkGraphicsBufferTableAliases(storageBuffers.Value(), buffers, draw.VertexBuffer, {}, draw.Arguments, draw.CountBuffer);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkBufferTableOverlap(storageBuffers.Value(), uniformBuffers.Value());

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkStorageTableTextureAliases(storageTextures.Value(), bindless.Value());

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkGraphicsFixedTextureTableAliases(bindless.Value(), storageTextures.Value(), textures, fixedStorage);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		NativeVertexBuffer extra[3]{};
		Result<void> extraChecked =
			resolveExtraVertexBuffers(*pipeline, draw.ExtraVertexBuffers, 1, 1, draw.VertexBuffer, {}, draw.Arguments, draw.CountBuffer,
				buffers, storageBuffers.Value(), uniformBuffers.Value(), readOnlyBuffers.Value(), extra);

		if (!extraChecked)
		{
			ASSERT(extraChecked.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return extraChecked;
		}

		if (!reserveBindingTableUses(*command, bindless.Value(), bindlessSamplers.Value(), storageBuffers.Value(), storageTextures.Value(),
			uniformBuffers.Value(), readOnlyBuffers.Value(), textures.Size() + fixedStorage.Size(),
			(vertex != nullptr ? 2 : 1) + (bCounted ? 1 : 0) + draw.ExtraVertexBuffers.Size() + buffers.Size() +
			(accel != nullptr ? 1 : 0),
			singleBindingSet(draw.Bindings.Sets)))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track graphics descriptor table resources.");
		}

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + (vertex != nullptr ? 2 : 1) + (bCounted ? 1 : 0) + draw.ExtraVertexBuffers.Size() + buffers.Size() + fixed.AccelCount + (accel != nullptr ? 1u : 0u)) ||
			!command->TextureUses.Reserve(command->TextureUses.Size() + textures.Size() + fixedStorage.Size()) ||
			!reserveAccelStructUse(*command, accel) ||
			!command->AccelUses.Reserve(command->AccelUses.Size() + fixed.AccelCount + (accel != nullptr ? 1u : 0u)) ||
			!command->GraphicsPipelineUses.Add({ draw.Pipeline, pipeline->Native }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track indirect draw resources.");
		}

		if (!draw.ExtraVertexBuffers.IsEmpty())
		{
			Result<void> bound =
				backend::RecordBindExtraVertexBuffers(m_State->m_Backend.State, command->Native, { extra, draw.ExtraVertexBuffers.Size() });
			if (!bound)
			{
				command->GraphicsPipelineUses.PopBack();
				poisonCommandList(list);

				return bound;
			}
		}
		trackGraphicsTextures(*command, list.Slot, textures);
		trackGraphicsTextures(*command, list.Slot, fixedStorage, ETrackedResourceState::Storage);
		trackGraphicsBindingSets(*command, draw.Bindings.Sets);
		trackGraphicsBuffers(*command, *pipeline, buffers);
		trackAccelStruct(*command, AccelStructHandle{}, accel);
		trackExtraVertexBuffers(*command, draw.ExtraVertexBuffers);
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, bindless.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, bindlessSamplers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, storageBuffers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, storageTextures.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, uniformBuffers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, readOnlyBuffers.Value());
		(void)command->AddBufferUse({ draw.Arguments, arguments->Native, ETrackedResourceState::Indirect });
		++arguments->RecordedUses;

		if (bCounted)
		{
			(void)command->AddBufferUse({ draw.CountBuffer, count->Native, ETrackedResourceState::Indirect });
			++count->RecordedUses;
		}

		if (vertex != nullptr)
		{
			(void)command->AddBufferUse({ draw.VertexBuffer, vertex->Native, ETrackedResourceState::Vertex });
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

				Result<void> rangesRecorded = recordRangeBindingSets(*command, pipeline->Native, true, draw.Bindings.Sets);
				if (!rangesRecorded)
				{
					return rangesRecorded;
				}

				return backend::RecordDrawIndirect(
					m_State->m_Backend.State, command->Native, pipeline->Native, arguments->Native, vertex != nullptr ? vertex->Native : nullptr,
					bCounted ? count->Native : nullptr, draw.VertexBufferOffsetBytes, vertex != nullptr ? vertex->ByteCount : 0,
					pipeline->VertexStrideBytes, { sampled, textures.Size() }, { nativeStorage, fixedStorage.Size() },
					{ nativeBuffers, buffers.Size() }, bindless.Value() != nullptr ? bindless.Value()->Native : nullptr,
					bindlessSamplers.Value() != nullptr ? bindlessSamplers.Value()->Native : nullptr,
					storageBuffers.Value() != nullptr ? storageBuffers.Value()->Native : nullptr,
					storageTextures.Value() != nullptr ? storageTextures.Value()->Native : nullptr,
					uniformBuffers.Value() != nullptr ? uniformBuffers.Value()->Native : nullptr,
					readOnlyBuffers.Value() != nullptr ? readOnlyBuffers.Value()->Native : nullptr,
					accel != nullptr ? accel->Native : nullptr, draw);
			});
		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> Device::DrawIndirect(CommandListHandle list, const GraphicsDrawIndirect& draw)
	{
		CallScope call(*this, true);

		return drawIndirectInternal(list, draw, false);
	}

	[[nodiscard]] Result<void> Device::DispatchMeshIndirect(CommandListHandle list, const MeshDispatchIndirect& dispatch)
	{
		CallScope call(*this, true);
		GraphicsDrawIndirect
			draw{};
		draw.Pipeline = dispatch.Pipeline;
		draw.Bindings.DeviceHeaps = dispatch.Bindings.DeviceHeaps;
		draw.Arguments = dispatch.Arguments;
		draw.ArgumentsOffsetBytes = dispatch.ArgumentsOffsetBytes;
		draw.PushConstants = dispatch.PushConstants;
		draw.Bindings.Sets = dispatch.Bindings.Sets;
		draw.StencilReference = dispatch.StencilReference;
		draw.BlendConstant = dispatch.BlendConstant;
		draw.ShadingRate = dispatch.ShadingRate;
		draw.PrimitiveShadingRateCombiner = dispatch.PrimitiveShadingRateCombiner;
		draw.ImageShadingRateCombiner = dispatch.ImageShadingRateCombiner;

		return drawIndirectInternal(list, draw, true);
	}

	[[nodiscard]] Result<void> Device::DrawIndexedIndirect(CommandListHandle list, const GraphicsDrawIndexedIndirect& draw)
	{
		CallScope call(*this, true);
		CommandSlot* const command = findCommand(list);
		GraphicsPipelineSlot* const pipeline = findGraphicsPipeline(draw.Pipeline);
		BufferSlot* const arguments = findBuffer(draw.Arguments);
		BufferSlot* const vertex = findBuffer(draw.VertexBuffer);
		BufferSlot* const index = findBuffer(draw.IndexBuffer);
		const bool bCounted = draw.CountBuffer.IsValid();
		BufferSlot* const count = bCounted ? findBuffer(draw.CountBuffer) : nullptr;

		ASSERT(!(command == nullptr || pipeline == nullptr || arguments == nullptr || index == nullptr || (pipeline->VertexStrideBytes != 0 && vertex == nullptr) || (bCounted && count == nullptr)),
			"RHI operations require valid handles for this device.");

		Result<SharedPtr<BoundDescriptorTables>> heapBindings = captureDescriptorHeapBindings(draw.Bindings.DeviceHeaps, pipeline->bDeviceHeapIndexed);
		if (!heapBindings)
		{
			ASSERT(heapBindings.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(heapBindings.GetError());
		}

		Result<void> heapValid = validateOrdinaryDeviceHeapUses(*command, heapBindings.Value().Get(), draw.Bindings.Sets, {});
		if (!heapValid)
		{
			ASSERT(heapValid.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return heapValid;
		}

		ASSERT(!pipeline->bMeshShader, "A bMesh pipeline cannot use indexed indirect drawing.");

		Result<AccelStructSlot*> accelResult = resolveAccelStruct(pipeline->AccelStructSlot, AccelStructHandle{});

		if (!accelResult)
		{
			ASSERT(accelResult.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(accelResult.GetError());
		}
		AccelStructSlot* const accel = accelResult.Value();
		const bool bHasImage = command->ActiveFramebuffer.IsValid() && m_State->m_Framebuffers[command->ActiveFramebuffer.Slot].ShadingRateImage.IsValid();
		Result<void> shadingRate = validateShadingRate(draw.ShadingRate, draw.PrimitiveShadingRateCombiner, draw.ImageShadingRateCombiner, bHasImage, pipeline->SampleCount);

		if (!shadingRate)
		{
			ASSERT(shadingRate.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return shadingRate;
		}

		if (bCounted && !m_State->m_Backend.Capabilities.Features.bDrawIndexedIndirectCount)
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.DrawIndexedIndirectCount", "The device does not support indirect draw counts.");
		}

		const uint64 indexSize = draw.IndexFormat == EFormat::R16_UINT ? 2 : 4;

		ASSERT(!(!command->bRendering ||
				!matchesRendering(*command, *pipeline) ||
				draw.PushConstants.Size() != pipeline->PushConstantBytes ||
				!isValidBlendConstant(draw.BlendConstant) ||
				draw.DrawCount == 0 ||
				draw.DrawCount > 65535 ||
				(bCounted && draw.DrawCount > m_State->m_Backend.Capabilities.Limits.MaximumCountedIndirectDraws) ||
				!HasUsage(arguments->Usage, EBufferUsage::Indirect) ||
				!HasUsage(index->Usage, EBufferUsage::Index) ||
				draw.Arguments == draw.VertexBuffer ||
				draw.Arguments == draw.IndexBuffer ||
				(bCounted &&
				 (!HasUsage(count->Usage, EBufferUsage::Indirect) ||
				  draw.CountBuffer == draw.Arguments ||
				  draw.CountBuffer == draw.VertexBuffer ||
				  draw.CountBuffer == draw.IndexBuffer ||
				  draw.CountBufferOffsetBytes % 4 != 0 ||
				  !Fits(count->ByteCount, draw.CountBufferOffsetBytes, sizeof(uint32)))) ||
				(!bCounted && draw.CountBufferOffsetBytes != 0) ||
				(draw.IndexFormat != EFormat::R16_UINT && draw.IndexFormat != EFormat::R32_UINT) ||
				draw.ArgumentsOffsetBytes % 4 != 0 ||
				draw.IndexBufferOffsetBytes % indexSize != 0 ||
				!Fits(arguments->ByteCount, draw.ArgumentsOffsetBytes, static_cast<uint64>(draw.DrawCount) * sizeof(IndirectDrawIndexedArguments)) ||
				(pipeline->VertexStrideBytes != 0 && (!HasUsage(vertex->Usage, EBufferUsage::Vertex) || !Fits(vertex->ByteCount, draw.VertexBufferOffsetBytes, pipeline->VertexStrideBytes))) ||
				(pipeline->VertexStrideBytes == 0 && (draw.VertexBuffer.Owner != 0 || draw.VertexBufferOffsetBytes != 0)) ||
				!Fits(index->ByteCount, draw.IndexBufferOffsetBytes, indexSize)),
			"Indexed indirect draw arguments and count ranges must match the pipeline and buffer usage.");
		FixedGraphicsBindings fixed{};
		Result<void> fixedResult = resolveFixedGraphicsBindings(draw.Bindings.Sets, draw.ExtraVertexBuffers, *command, *pipeline, list.Slot, draw.VertexBuffer,
			draw.IndexBuffer, draw.Arguments, draw.CountBuffer, fixed);

		if (!fixedResult)
		{
			ASSERT(fixedResult.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
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
			ASSERT(bindless.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(bindless.GetError());
		}

		Result<DescriptorTableSlot*> bindlessSamplers =
			resolveGraphicsSamplerTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!bindlessSamplers)
		{
			ASSERT(bindlessSamplers.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(bindlessSamplers.GetError());
		}

		Result<DescriptorTableSlot*> storageBuffers =
			resolveGraphicsStorageBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!storageBuffers)
		{
			ASSERT(storageBuffers.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(storageBuffers.GetError());
		}

		Result<DescriptorTableSlot*> storageTextures =
			resolveGraphicsStorageTextureTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!storageTextures)
		{
			ASSERT(storageTextures.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(storageTextures.GetError());
		}

		Result<DescriptorTableSlot*> uniformBuffers =
			resolveGraphicsUniformBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!uniformBuffers)
		{
			ASSERT(uniformBuffers.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(uniformBuffers.GetError());
		}

		Result<DescriptorTableSlot*> readOnlyBuffers =
			resolveGraphicsReadOnlyBufferTable(*pipeline, DescriptorTableHandle{}, *command, list.Slot);

		if (!readOnlyBuffers)
		{
			ASSERT(readOnlyBuffers.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return Result<void>::Failure(readOnlyBuffers.GetError());
		}

		Result<void> readOnlyAliases = checkBufferTableAliases(readOnlyBuffers.Value(), buffers);

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}
		readOnlyAliases = checkBufferTableOverlap(storageBuffers.Value(), readOnlyBuffers.Value());

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}
		readOnlyAliases = checkBufferTableOverlap(uniformBuffers.Value(), readOnlyBuffers.Value());

		if (!readOnlyAliases)
		{
			ASSERT(readOnlyAliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyAliases;
		}

		Result<void> aliases = checkGraphicsBufferTableAliases(uniformBuffers.Value(), buffers, draw.VertexBuffer, draw.IndexBuffer, draw.Arguments, draw.CountBuffer);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}

		Result<void> readOnlyGraphicsAliases = checkGraphicsBufferTableAliases(readOnlyBuffers.Value(), buffers, draw.VertexBuffer, draw.IndexBuffer, draw.Arguments, draw.CountBuffer);

		if (!readOnlyGraphicsAliases)
		{
			ASSERT(readOnlyGraphicsAliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return readOnlyGraphicsAliases;
		}
		aliases = checkGraphicsBufferTableAliases(storageBuffers.Value(), buffers, draw.VertexBuffer, draw.IndexBuffer, draw.Arguments, draw.CountBuffer);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkBufferTableOverlap(storageBuffers.Value(), uniformBuffers.Value());

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkStorageTableTextureAliases(storageTextures.Value(), bindless.Value());

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		aliases = checkGraphicsFixedTextureTableAliases(bindless.Value(), storageTextures.Value(), textures, fixedStorage);

		if (!aliases)
		{
			ASSERT(aliases.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return aliases;
		}
		NativeVertexBuffer extra[3]{};
		Result<void> extraChecked = resolveExtraVertexBuffers(
			*pipeline, draw.ExtraVertexBuffers, 1, 1, draw.VertexBuffer, draw.IndexBuffer, draw.Arguments, draw.CountBuffer, buffers,
			storageBuffers.Value(), uniformBuffers.Value(), readOnlyBuffers.Value(), extra);

		if (!extraChecked)
		{
			ASSERT(extraChecked.GetError().Code != EErrorCode::InvalidArgument, "Indirect draw bindings and state must be valid.");
			poisonCommandList(list);

			return extraChecked;
		}

		if (!reserveBindingTableUses(*command, bindless.Value(), bindlessSamplers.Value(), storageBuffers.Value(), storageTextures.Value(),
			uniformBuffers.Value(), readOnlyBuffers.Value(), textures.Size() + fixedStorage.Size(),
			(bCounted ? 4 : 3) + draw.ExtraVertexBuffers.Size() + buffers.Size() + (accel != nullptr ? 1 : 0),
			singleBindingSet(draw.Bindings.Sets)))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track graphics descriptor table resources.");
		}

		for (const BufferHandle buffer : buffers)
		{
			ASSERT(!bCounted || buffer != draw.CountBuffer, "Indexed indirect draw arguments and count ranges must match the pipeline and buffer usage.");
		}

		if (!command->BufferUses.Reserve(command->BufferUses.Size() + 3 + (bCounted ? 1 : 0) + draw.ExtraVertexBuffers.Size() + buffers.Size() + fixed.AccelCount + (accel != nullptr ? 1u : 0u)) ||
			!command->TextureUses.Reserve(command->TextureUses.Size() + textures.Size() + fixedStorage.Size()) ||
			!reserveAccelStructUse(*command, accel) ||
			!command->AccelUses.Reserve(command->AccelUses.Size() + fixed.AccelCount + (accel != nullptr ? 1u : 0u)) ||
			!command->GraphicsPipelineUses.Add({ draw.Pipeline, pipeline->Native }))
		{
			poisonCommandList(list);

			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not track indexed indirect draw resources.");
		}

		if (!draw.ExtraVertexBuffers.IsEmpty())
		{
			Result<void> bound =
				backend::RecordBindExtraVertexBuffers(m_State->m_Backend.State, command->Native, { extra, draw.ExtraVertexBuffers.Size() });
			if (!bound)
			{
				command->GraphicsPipelineUses.PopBack();
				poisonCommandList(list);

				return bound;
			}
		}
		trackGraphicsTextures(*command, list.Slot, textures);
		trackGraphicsTextures(*command, list.Slot, fixedStorage, ETrackedResourceState::Storage);
		trackGraphicsBindingSets(*command, draw.Bindings.Sets);
		trackGraphicsBuffers(*command, *pipeline, buffers);
		trackAccelStruct(*command, AccelStructHandle{}, accel);
		trackExtraVertexBuffers(*command, draw.ExtraVertexBuffers);
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, bindless.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, bindlessSamplers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, storageBuffers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, storageTextures.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, uniformBuffers.Value());
		trackDescriptorTableUse(*command, list.Slot, DescriptorTableHandle{}, readOnlyBuffers.Value());
		(void)command->AddBufferUse({ draw.Arguments, arguments->Native, ETrackedResourceState::Indirect });
		if (draw.VertexBuffer == draw.IndexBuffer)
		{
			(void)command->AddBufferUse({ draw.VertexBuffer, vertex->Native, ETrackedResourceState::Vertex | ETrackedResourceState::Index });
		}
		else
		{
			if (vertex != nullptr)
			{
				(void)command->AddBufferUse({ draw.VertexBuffer, vertex->Native, ETrackedResourceState::Vertex });
			}
			(void)command->AddBufferUse({ draw.IndexBuffer, index->Native, ETrackedResourceState::Index });
		}

		if (bCounted)
		{
			(void)command->AddBufferUse({ draw.CountBuffer, count->Native, ETrackedResourceState::Indirect });
			++count->RecordedUses;
		}
		++arguments->RecordedUses;
		if (vertex != nullptr)
		{
			++vertex->RecordedUses;
		}

		if (draw.VertexBuffer != draw.IndexBuffer)
		{
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

				Result<void> rangesRecorded = recordRangeBindingSets(*command, pipeline->Native, true, draw.Bindings.Sets);
				if (!rangesRecorded)
				{
					return rangesRecorded;
				}

				return backend::RecordDrawIndexedIndirect(
					m_State->m_Backend.State, command->Native, pipeline->Native, arguments->Native, vertex != nullptr ? vertex->Native : nullptr,
					index->Native, bCounted ? count->Native : nullptr, draw.VertexBufferOffsetBytes, vertex != nullptr ? vertex->ByteCount : 0,
					draw.IndexBufferOffsetBytes, pipeline->VertexStrideBytes, { sampled, textures.Size() }, { nativeStorage, fixedStorage.Size() },
					{ nativeBuffers, buffers.Size() }, bindless.Value() != nullptr ? bindless.Value()->Native : nullptr,
					bindlessSamplers.Value() != nullptr ? bindlessSamplers.Value()->Native : nullptr,
					storageBuffers.Value() != nullptr ? storageBuffers.Value()->Native : nullptr,
					storageTextures.Value() != nullptr ? storageTextures.Value()->Native : nullptr,
					uniformBuffers.Value() != nullptr ? uniformBuffers.Value()->Native : nullptr,
					readOnlyBuffers.Value() != nullptr ? readOnlyBuffers.Value()->Native : nullptr,
					accel != nullptr ? accel->Native : nullptr, draw);
			});
		if (!recorded)
		{
			poisonCommandList(list);

			return recorded;
		}

		return Result<void>::Success();
	}
}
