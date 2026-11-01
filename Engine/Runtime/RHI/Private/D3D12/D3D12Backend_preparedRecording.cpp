#include "D3D12Backend.h"
#include "D3D12Descriptors.h"

#include <Trace.h>
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<void> PrepareRecordingGroup(State* opaque, Span<const NativePreparedList> lists)
	{
		ROX_PROFILE_DEEP_SCOPE("D3D12", "PrepareRecordingGroup");
		State& state = *opaque;
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
		for (const rhi::NativePreparedList& description : lists)
		{
			Pending& pending = *static_cast<Pending*>(description.Commands);
			uint64 states = 0;
			pending.PreparedTextureHeapCapacity = static_cast<uint32>(ClampValue<uint64>(
				description.FixedResourceDescriptorCount, 256, D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1));
			pending.PreparedSamplerHeapCapacity = static_cast<uint32>(ClampValue<uint64>(
				description.FixedSamplerDescriptorCount, 256, D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE));
			for (const rhi::NativePreparedPhase& phase : description.Phases)
			{
				states += phase.Buffers.Size();
				for (const rhi::NativePreparedTextureAccess& access : phase.Textures)
				{
					const Texture& texture = *static_cast<const Texture*>(access.Resource);
					const D3D12_RESOURCE_DESC desc = texture.Resource->GetDesc();
					states += uint64(desc.MipLevels) * desc.DepthOrArraySize * (GetFormatInfo(texture.Format).bHasStencil ? 2u : 1u) + 1;
				}
			}
			if (states > UINT32_MAX ||
				!ReserveTrackedStates(pending, static_cast<usize>(states)) ||
				!pending.UavPolicies.Reserve(static_cast<usize>(states)) ||
				!pending.PreparedAccelBuilds.Reserve(description.AccelBuildCount) ||
				!pending.PreparedAccelQueries.Reserve(description.AccelQueryCount) ||
				!pending.PreparedAccelCompacts.Reserve(description.AccelCompactCount) ||
				!pending.PreparedBindings.Reserve(description.BindingCount) ||
				!pending.PreparedIndirects.Reserve(description.IndirectCount))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not reserve prepared state storage."));
			}
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

		pending.InvalidatePreparedGraphics();
		pending.PreparedAccelBuilds.Clear();
		for (d3d12::Pending::PreparedAccelQuery const& query : pending.PreparedAccelQueries)
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
		ReleasePendingGlobalDescriptors(state, pending);
		pending.PreparedIndirects.Clear();
		pending.TextureHeaps.Clear();
		pending.SamplerHeaps.Clear();
		pending.TextureHeapUsed = 0;
		pending.SamplerHeapUsed = 0;
		pending.ResourceStates.Clear();
		pending.ResourceStateLookup.Clear();
		pending.bPrepared = false;
	}

	[[nodiscard]] Result<void> CapturePreparedStorage(
		Pending::PreparedBinding& binding,
		const NativeFixedLayoutState& fixed,
		Span<const NativeFixedBindingSet> sets,
		Span<const NativeDescriptorHeapBinding> heaps)
	{
		if (!fixed.bHasStorage && heaps.IsEmpty())
		{
			return Result<void>::Success();
		}
		usize required = 0;
		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			for (uint32 rangeIndex = 0; rangeIndex < fixed.RangeCounts[layout]; ++rangeIndex)
			{
				const BindingRangeDesc& range = fixed.Ranges[layout][rangeIndex];
				if (range.Type != EDescriptorType::StorageBuffer && range.Type != EDescriptorType::StorageTexture)
				{
					continue;
				}
				if (range.Source == EBindingRangeSource::Fixed)
				{
					required += range.DescriptorCount;
				}
				else
				{
					for (uint32 index = 0; index < sets[layout].TableCount; ++index)
					{
						if (sets[layout].Tables[index].RangeIndex == rangeIndex)
						{
							required += sets[layout].Tables[index].UsedEntries.Size();
						}
					}
				}
			}
		}
		for (const rhi::NativeDescriptorHeapBinding& heap : heaps)
		{
			if (heap.Type == EDescriptorType::StorageBuffer || heap.Type == EDescriptorType::StorageTexture)
			{
				required += heap.UsedEntries.Size();
			}
		}
		if (!binding.Storage.Reserve(required))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain prepared storage hazards."));
		}

		for (uint32 layout = 0; layout < sets.Size(); ++layout)
		{
			uint32 descriptor = 0;
			for (uint32 rangeIndex = 0; rangeIndex < fixed.RangeCounts[layout]; ++rangeIndex)
			{
				const BindingRangeDesc& range = fixed.Ranges[layout][rangeIndex];
				if (range.Source == EBindingRangeSource::DescriptorTable)
				{
					if (range.Type != EDescriptorType::StorageBuffer && range.Type != EDescriptorType::StorageTexture)
					{
						continue;
					}
					for (uint32 index = 0; index < sets[layout].TableCount; ++index)
					{
						const NativeTableRangeBinding& captured = sets[layout].Tables[index];
						if (captured.RangeIndex != rangeIndex)
						{
							continue;
						}
						const DescriptorTable& table = *static_cast<const DescriptorTable*>(captured.Table);
						for (const uint32 entry : captured.UsedEntries)
						{
							binding.Storage.AddRequired(range.Type == EDescriptorType::StorageBuffer
								? table.Buffers[entry]->Resource.Get()
								: table.Textures[entry]->Resource.Get());
						}
					}
					continue;
				}
				for (uint32 element = 0; element < range.DescriptorCount; ++element)
				{
					const rhi::NativeFixedDescriptor& value = sets[layout].Descriptors[descriptor++];
					if (range.Type == EDescriptorType::StorageBuffer)
					{
						binding.Storage.AddRequired(static_cast<const Buffer*>(value.Resource)->Resource.Get());
					}
					else if (range.Type == EDescriptorType::StorageTexture)
					{
						binding.Storage.AddRequired(static_cast<const Texture*>(value.Resource)->Resource.Get());
					}
				}
			}
		}
		for (const rhi::NativeDescriptorHeapBinding& heap : heaps)
		{
			if (heap.Type != EDescriptorType::StorageBuffer && heap.Type != EDescriptorType::StorageTexture)
			{
				continue;
			}
			const DescriptorTable& table = *static_cast<const DescriptorTable*>(heap.Table);
			for (const uint32 entry : heap.UsedEntries)
			{
				binding.Storage.AddRequired(heap.Type == EDescriptorType::StorageBuffer ? table.Buffers[entry]->Resource.Get()
					: table.Textures[entry]->Resource.Get());
			}
		}
		binding.StorageCount = static_cast<uint32>(binding.Storage.Size());

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint32> PrepareBinding(
		State* opaque,
		void* native,
		void* pipeline,
		bool bGraphics,
		Span<const NativeFixedBindingSet> sets,
		Span<const NativePreparedTextureAccess>,
		const NativePreparedVertexInput& input,
		Span<const NativeDescriptorHeapBinding> heaps,
		SharedPtr<NativeBindingProgram>* program)
	{
		ROX_PROFILE_DEEP_SCOPE("D3D12", "PrepareBinding");
		Pending& pending = *static_cast<Pending*>(native);
		if (program && *program)
		{
			++pending.Statistics.BindingProgramReuses;
			if (bGraphics && static_cast<const GraphicsPipeline*>(pipeline)->DepthBounds.bEnable && !pending.ResolveList && FAILED(pending.List.As(&pending.ResolveList)))
			{
				return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.BindingProgram", "The command list does not support the prepared program.");
			}
			const uint32 index = static_cast<uint32>(pending.PreparedBindings.Size());
			if (!pending.PreparedBindings.Add(*program))
			{
				return Result<uint32>::Failure(EErrorCode::ExternalFailure, "Rhi.BindingProgram", "Could not retain a prepared binding program.");
			}

			return Result<uint32>::Success(index);
		}
		MemoryResource& memory = opaque->Resource;
		++pending.Statistics.BindingProgramCreations;
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
		if (bGraphics && static_cast<const GraphicsPipeline*>(pipeline)->DepthBounds.bEnable && !pending.ResolveList && FAILED(pending.List.As(&pending.ResolveList)))
		{
			return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.DepthBounds", "The prepared command list does not support depth bounds testing.");
		}
		binding.VertexCount = input.VertexCount;
		for (uint32 slot = 0; slot < input.VertexCount; ++slot)
		{
			const NativeVertexBuffer& vertex = input.Vertices[slot];
			const Buffer& resource = *static_cast<const Buffer*>(vertex.Buffer);
			binding.Vertices[slot] = { resource.Resource->GetGPUVirtualAddress() + vertex.Offset, static_cast<UINT>(vertex.ByteCount),
									  vertex.Stride };
		}
		if (input.IndexBuffer != nullptr)
		{
			const Buffer& resource = *static_cast<const Buffer*>(input.IndexBuffer);
			binding.Index = { resource.Resource->GetGPUVirtualAddress() + input.IndexRange.ByteOffset,
							 static_cast<UINT>(input.IndexRange.ByteCount),
							 input.IndexFormat == EFormat::R16_UINT ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT };
		}

		const bool bDeviceHeapIndexed = bGraphics ? static_cast<const GraphicsPipeline*>(pipeline)->bDeviceHeapIndexed : static_cast<const ComputePipeline*>(pipeline)->bDeviceHeapIndexed;
		if (!sets.IsEmpty() || bDeviceHeapIndexed)
		{
			Result<void> result = WriteFixedBindingSets(opaque, native, pipeline, bGraphics, sets, &binding);
			if (!result)
			{
				return Result<uint32>::Failure(result.GetError());
			}
		}

		const NativeFixedLayoutState& fixed = bGraphics ? static_cast<const GraphicsPipeline*>(pipeline)->Fixed : static_cast<const ComputePipeline*>(pipeline)->Fixed;
		Result<void> storage = CapturePreparedStorage(binding, fixed, sets, heaps);
		if (!storage)
		{
			return Result<uint32>::Failure(Move(storage.GetError()));
		}

		bool bPersistent = heaps.IsEmpty() && !bDeviceHeapIndexed;
		for (uint32 i = 0; i < sets.Size(); ++i)
		{
			bPersistent &= sets[i].TableCount == 0 && bool(binding.FixedDescriptors[i]);
		}
		owner->RetainedBytes = sizeof(Pending::PreparedBinding) + binding.Storage.Capacity() * sizeof(ID3D12Resource*);
		if (program && bPersistent)
		{
			*program = owner;
		}
		const uint32 index = static_cast<uint32>(pending.PreparedBindings.Size());
		const bool bAdded = pending.PreparedBindings.Add(owner);
		ASSERT(bAdded, "Every immutable prepared binding has reserved storage.");

		return Result<uint32>::Success(index);
	}

	[[nodiscard]] Result<void> RecordPreparedPhase(State* opaque, void* native, const NativePreparedPhase& phase)
	{
		const PreparedErrorScope errorScope;
		static_cast<Pending*>(native)->InvalidatePreparedGraphics();

		for (const rhi::NativePreparedBufferAccess& access : phase.Buffers)
		{
			Buffer& buffer = *static_cast<Buffer*>(access.Resource);
			if (buffer.Memory != EBufferMemory::Device)
			{
				continue;
			}

			if (access.State == EBufferState::AccelStorage)
			{
				// AS storage remains in its immutable native AS state; UAV visibility orders build/copy/trace.
				EmitUavBarrier(*static_cast<Pending*>(native), *buffer.Resource.Get());
				continue;
			}
			Result<void> result = RecordSetBufferState(opaque, native, access.Resource, access.State);
			if (!result)
			{
				return result;
			}
		}
		for (const rhi::NativePreparedTextureAccess& access : phase.Textures)
		{
			const Texture& texture = *static_cast<const Texture*>(access.Resource);
			const D3D12_RESOURCE_DESC descriptor = texture.Resource->GetDesc();
			const uint32 layers = descriptor.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : descriptor.DepthOrArraySize;
			if (!GetFormatInfo(texture.Format).bHasDepth &&
				access.Intent.BaseMipLevel == 0 &&
				access.Intent.MipLevelCount == descriptor.MipLevels &&
				access.Intent.BaseArrayLayer == 0 &&
				access.Intent.ArrayLayerCount == layers)
			{
				// Whole color accesses share one state journal row. Mixed prior states still
				// transition individually inside the ordinary whole-resource state tracker.
				Result<void> result = RecordSetTextureState(opaque, native, access.Resource, nullptr, access.Intent.State);
				if (!result)
				{
					return result;
				}
				continue;
			}
			for (uint32 layer = 0; layer < access.Intent.ArrayLayerCount; ++layer)
			{
				for (uint32 mip = 0; mip < access.Intent.MipLevelCount; ++mip)
				{
					const TextureSubresource subresource{ access.Intent.BaseMipLevel + mip, access.Intent.BaseArrayLayer + layer,
														 access.Intent.Aspect };
					Result<void> result = RecordSetTextureState(opaque, native, access.Resource, &subresource, access.Intent.State);
					if (!result)
					{
						return result;
					}
				}
			}
		}

		return Result<void>::Success();
	}

	void BindPrepared(Pending& pending, void* native, bool bGraphics, uint32 index) noexcept
	{
		const Pending::PreparedBinding& binding = pending.BindingAt(index);
		const Pending::PreparedBinding* previous =
			bGraphics && pending.LastPreparedGraphicsPipeline && pending.LastPreparedGraphicsBinding < pending.PreparedBindings.Size()
			? &pending.BindingAt(pending.LastPreparedGraphicsBinding)
			: nullptr;
		const bool bSameHeaps = previous && previous->Resources == binding.Resources && previous->Samplers == binding.Samplers;
		ID3D12DescriptorHeap* heaps[2]{};
		UINT count = 0;
		if (binding.Resources != nullptr)
		{
			heaps[count++] = binding.Resources;
		}
		if (binding.Samplers != nullptr)
		{
			heaps[count++] = binding.Samplers;
		}
		if (count != 0 && !bSameHeaps)
		{
			pending.List->SetDescriptorHeaps(count, heaps);
		}

		const uint32 pushBytes = bGraphics ? static_cast<const GraphicsPipeline*>(native)->PushConstantBytes : static_cast<const ComputePipeline*>(native)->PushConstantBytes;
		if (bGraphics)
		{
			if (!previous || static_cast<GraphicsPipeline*>(pending.LastPreparedGraphicsPipeline)->RootSignature.Get() != static_cast<GraphicsPipeline*>(native)->RootSignature.Get())
			{
				pending.List->SetGraphicsRootSignature(static_cast<GraphicsPipeline*>(native)->RootSignature.Get());
			}
		}
		else
		{
			pending.List->SetComputeRootSignature(static_cast<ComputePipeline*>(native)->RootSignature.Get());
		}
		for (UINT table = 0; table < binding.TableCount; ++table)
		{
			const UINT root = table + (pushBytes != 0 ? 1u : 0u);
			if (binding.bRootDescriptors[table])
			{
				const D3D12_GPU_VIRTUAL_ADDRESS address = binding.RootAddresses[table];
				const EDescriptorType type = binding.RootTypes[table];
				if (bGraphics)
				{
					if (type == EDescriptorType::UniformBuffer)
					{
						pending.List->SetGraphicsRootConstantBufferView(root, address);
					}
					else if (type == EDescriptorType::ReadOnlyBuffer)
					{
						pending.List->SetGraphicsRootShaderResourceView(root, address);
					}
					else
					{
						pending.List->SetGraphicsRootUnorderedAccessView(root, address);
					}
				}
				else if (type == EDescriptorType::UniformBuffer)
				{
					pending.List->SetComputeRootConstantBufferView(root, address);
				}
				else if (type == EDescriptorType::ReadOnlyBuffer)
				{
					pending.List->SetComputeRootShaderResourceView(root, address);
				}
				else
				{
					pending.List->SetComputeRootUnorderedAccessView(root, address);
				}
				continue;
			}
			if (bGraphics)
			{
				if (bSameHeaps &&
					table < previous->TableCount &&
					!previous->bRootDescriptors[table] &&
					previous->Tables[table].ptr == binding.Tables[table].ptr &&
					static_cast<GraphicsPipeline*>(pending.LastPreparedGraphicsPipeline)->RootSignature.Get() == static_cast<GraphicsPipeline*>(native)->RootSignature.Get())
				{
					continue;
				}
				pending.List->SetGraphicsRootDescriptorTable(root, binding.Tables[table]);
			}
			else
			{
				pending.List->SetComputeRootDescriptorTable(root, binding.Tables[table]);
			}
		}
	}

	void BindPreparedCompute(void*, void* native, void* pipelineNative, uint32 binding, Span<const Byte> pushConstants)
	{
		Pending& pending = *static_cast<Pending*>(native);
		ComputePipeline& pipeline = *static_cast<ComputePipeline*>(pipelineNative);
		pending.InvalidatePreparedGraphics();
		const Pending::PreparedBinding& captured = pending.BindingAt(binding);
		for (uint32 index = 0; index < captured.StorageCount; ++index)
		{
			EmitUavBarrier(pending, *captured.Storage[index]);
		}
		BindPrepared(pending, pipelineNative, false, binding);
		pending.List->SetPipelineState(pipeline.State.Get());
		if (pipeline.PushConstantBytes != 0)
		{
			pending.List->SetComputeRoot32BitConstants(0, pipeline.PushConstantBytes / 4, pushConstants.Data(), 0);
		}
	}

	[[nodiscard]] Result<void> RecordPreparedDispatch(void*, void* native, void* pipelineNative, uint32 binding, const ComputeDispatch& dispatch)
	{
		BindPreparedCompute(nullptr, native, pipelineNative, binding, dispatch.PushConstants);
		Pending& pending = *static_cast<Pending*>(native);
		pending.List->Dispatch(dispatch.GroupsX, dispatch.GroupsY, dispatch.GroupsZ);

		return Result<void>::Success();
	}

	void BindPreparedGraphics(void* native, void* pipelineNative, uint32 binding, Span<const Byte> pushConstants)
	{
		Pending& pending = *static_cast<Pending*>(native);
		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(pipelineNative);
		const Pending::PreparedBinding& captured = pending.BindingAt(binding);
		for (uint32 index = 0; index < captured.StorageCount; ++index)
		{
			EmitUavBarrier(pending, *captured.Storage[index]);
		}
		if (pending.LastPreparedGraphicsPipeline != pipelineNative || pending.LastPreparedGraphicsBinding != binding)
		{
			const Pending::PreparedBinding* previous =
				pending.LastPreparedGraphicsPipeline && pending.LastPreparedGraphicsBinding < pending.PreparedBindings.Size()
				? &pending.BindingAt(pending.LastPreparedGraphicsBinding)
				: nullptr;
			BindPrepared(pending, pipelineNative, true, binding);
			if (pending.LastPreparedGraphicsPipeline != pipelineNative)
			{
				pending.List->SetPipelineState(pipeline.State.Get());
				if (!pipeline.bMeshShader)
				{
					pending.List->IASetPrimitiveTopology(pipeline.Topology);
				}
			}
			if (!previous)
			{
				const float blend[4]{};
				pending.List->OMSetBlendFactor(blend);
			}
			bool bSameVertices = previous && previous->VertexCount == captured.VertexCount;
			for (uint32 stream = 0; bSameVertices && stream < captured.VertexCount; ++stream)
			{
				bSameVertices = previous->Vertices[stream].BufferLocation == captured.Vertices[stream].BufferLocation &&
					previous->Vertices[stream].SizeInBytes == captured.Vertices[stream].SizeInBytes &&
					previous->Vertices[stream].StrideInBytes == captured.Vertices[stream].StrideInBytes;
			}
			if (captured.VertexCount != 0 && !bSameVertices)
			{
				pending.List->IASetVertexBuffers(0, captured.VertexCount, captured.Vertices);
			}
			pending.bPreparedIndexBound &= previous && previous->Index.BufferLocation == captured.Index.BufferLocation &&
				previous->Index.SizeInBytes == captured.Index.SizeInBytes &&
				previous->Index.Format == captured.Index.Format;
			pending.LastPreparedGraphicsPipeline = pipelineNative;
			pending.LastPreparedGraphicsBinding = binding;
		}
		if (pipeline.DepthBounds.bEnable)
		{
			ASSERT(pending.ResolveList, "Prepared depth bounds retain their command-list interface before recording.");
			pending.ResolveList->OMSetDepthBounds(pipeline.DepthBounds.Minimum, pipeline.DepthBounds.Maximum);
		}
		// Push values and storage visibility belong to each draw, even when its recipe repeats.
		if (pipeline.PushConstantBytes != 0)
		{
			pending.List->SetGraphicsRoot32BitConstants(0, pipeline.PushConstantBytes / 4, pushConstants.Data(), 0);
		}
	}

	[[nodiscard]] Result<void> RecordPreparedMeshDispatch(void*, void* native, void* pipelineNative, uint32 binding, const MeshDispatch& dispatch)
	{
		Pending& pending = *static_cast<Pending*>(native);
		if (!pending.MeshList)
		{
			return Result<void>::Failure(NativeError(EErrorCode::ExternalFailure, "Rhi.MeshShader", "The prepared mesh command list is unavailable."));
		}

		BindPreparedGraphics(native, pipelineNative, binding, dispatch.PushConstants);
		pending.List->OMSetStencilRef(dispatch.StencilReference);
		pending.MeshList->DispatchMesh(dispatch.GroupsX, dispatch.GroupsY, dispatch.GroupsZ);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordPreparedDraw(void*, void* native, void* pipelineNative, uint32 binding, const DrawArguments& draw)
	{
		BindPreparedGraphics(native, pipelineNative, binding, draw.PushConstants);
		Pending& pending = *static_cast<Pending*>(native);
		pending.List->OMSetStencilRef(draw.StencilReference);
		pending.List->DrawInstanced(draw.VertexCount, draw.InstanceCount, draw.FirstVertex, draw.FirstInstance);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordDrawIndexedArguments(void*, void* native, void* pipelineNative, uint32 binding, const DrawIndexedArguments& draw)
	{
		BindPreparedGraphics(native, pipelineNative, binding, draw.PushConstants);
		Pending& pending = *static_cast<Pending*>(native);
		if (!pending.bPreparedIndexBound)
		{
			pending.List->IASetIndexBuffer(&pending.BindingAt(binding).Index);
			pending.bPreparedIndexBound = true;
		}
		pending.List->OMSetStencilRef(draw.StencilReference);
		pending.List->DrawIndexedInstanced(draw.IndexCount, draw.InstanceCount, draw.FirstIndex, draw.BaseVertex, draw.FirstInstance);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint32> PrepareIndirect(State* opaque, void* native, const NativePreparedIndirect& indirect)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		D3D12_INDIRECT_ARGUMENT_DESC argument{};
		ComPtr<ID3D12CommandSignature> signature;
		uint32 packedBytes = 0;
		switch (indirect.Kind)
		{
		case EPreparedIndirectKind::Compute:
			argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
			packedBytes = sizeof(IndirectDispatchArguments);
			signature = state.DispatchSignature;
			break;
		case EPreparedIndirectKind::Draw:
			argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
			packedBytes = sizeof(IndirectDrawArguments);
			signature = state.DrawSignature;
			break;
		case EPreparedIndirectKind::DrawIndexed:
			argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
			packedBytes = sizeof(IndirectDrawIndexedArguments);
			signature = state.DrawIndexedSignature;
			break;
		case EPreparedIndirectKind::Mesh:
			argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
			packedBytes = sizeof(IndirectMeshArguments);
			signature = state.MeshSignature;
			break;

		default:
		{
			break;
		}
		}

		if (indirect.Stride != packedBytes)
		{
			D3D12_COMMAND_SIGNATURE_DESC description{};
			description.ByteStride = indirect.Stride;
			description.NumArgumentDescs = 1;
			description.pArgumentDescs = &argument;
			signature.Reset();
			if (FAILED(state.Device->CreateCommandSignature(&description, nullptr, IID_PPV_ARGS(signature.GetAddressOf()))))
			{
				return Result<uint32>::Failure(NativeError(EErrorCode::ExternalFailure, "Rhi.PreparedIndirect", "Could not create the prepared indirect command signature."));
			}
		}
		if (!signature)
		{
			return Result<uint32>::Failure(NativeError(EErrorCode::Unsupported, "Rhi.PreparedIndirect", "The native indirect command signature is unavailable."));
		}

		const uint32 index = static_cast<uint32>(pending.PreparedIndirects.Size());
		const bool bAdded = pending.PreparedIndirects.Add(Move(signature));
		ASSERT(bAdded, "Prepared indirect signature capacity must be reserved.");

		return Result<uint32>::Success(index);
	}

	[[nodiscard]] Result<void> RecordPreparedIndirect(
		State* opaque,
		void* native,
		void* pipelineNative,
		uint32 binding,
		const NativePreparedIndirect& indirect,
		Span<const Byte> pushConstants)
	{
		Pending& pending = *static_cast<Pending*>(native);
		if (indirect.Kind == EPreparedIndirectKind::Compute)
		{
			BindPreparedCompute(opaque, native, pipelineNative, binding, pushConstants);
		}
		else
		{
			BindPreparedGraphics(native, pipelineNative, binding, pushConstants);
			if (indirect.Kind == EPreparedIndirectKind::DrawIndexed)
			{
				if (!pending.bPreparedIndexBound)
				{
					pending.List->IASetIndexBuffer(&pending.BindingAt(binding).Index);
					pending.bPreparedIndexBound = true;
				}
			}
		}

		const Buffer& arguments = *static_cast<Buffer*>(indirect.Arguments);
		const Buffer* count = static_cast<Buffer*>(indirect.CountBuffer);
		if (indirect.Kind != EPreparedIndirectKind::Compute)
		{
			pending.List->OMSetStencilRef(0);
		}
		pending.List->ExecuteIndirect(pending.PreparedIndirects[indirect.NativeIndex].Get(), indirect.MaximumCount,
			arguments.Resource.Get(), indirect.ArgumentsOffsetBytes,
			count != nullptr ? count->Resource.Get() : nullptr, indirect.CountBufferOffsetBytes);
		// PrepareIndirect creates draw/dispatch-only signatures. They change no root,
		// vertex or index bindings, so ExecuteIndirect preserves the captured graphics state.
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
		Pending& pending = *static_cast<Pending*>(native);
		Framebuffer& framebuffer = *static_cast<Framebuffer*>(target);
		pending.InvalidatePreparedGraphics();
		Result<void> result = RecordBeginFramebuffer(opaque, native, target, depthOptions);
		if (!result)
		{
			return result;
		}
		ASSERT(loads.Size() == framebuffer.ColorCount && (colors.IsEmpty() || colors.Size() == framebuffer.ColorCount), "Prepared color loads and clears match the retained framebuffer.");

		const UINT descriptorStride = opaque->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		for (uint32 index = 0; index < framebuffer.ColorCount; ++index)
		{
			if (loads[index] != EAttachmentLoad::Clear)
			{
				continue;
			}
			ASSERT(colors.Size() == framebuffer.ColorCount, "Cleared attachments require clear values.");

			const rhi::ClearColor& color = colors[index];
			D3D12_CPU_DESCRIPTOR_HANDLE view = framebuffer.ColorViews.Cpu;
			view.ptr += static_cast<SIZE_T>(index) * descriptorStride;
			if (color.GetType() == EClearColorType::Float)
			{
				pending.List->ClearRenderTargetView(view, color.GetFloat(), 0, nullptr);
			}
			else
			{
				framebuffer.IntegerClears[index].Record(*pending.List.Get(), view, framebuffer.Width, framebuffer.Height, color);
			}
		}

		pending.LastTexturePipeline = nullptr;
		depthOptions.LoadOp = EDepthLoadOp::Load;
		depthOptions.StencilLoadOp = EDepthLoadOp::Load;

		return RecordBeginFramebuffer(opaque, native, target, depthOptions);
	}

	[[nodiscard]] Result<void> FinishPreparedCommandList(void*, void* native)
	{
		Pending& pending = *static_cast<Pending*>(native);
		RestoreTrackedStates(pending);
		if (pending.bInvalidResourceState || FAILED(pending.List->Close()))
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
		if (pending.bPreparedFinished && !pending.bInvalidResourceState)
		{
			if (!bTexture)
			{
				const Buffer& buffer = *static_cast<const Buffer*>(resourceNative);
				// Fixed host heaps retain their required state. Device buffers decay to COMMON
				// when this ExecuteCommandLists operation completes, before the SDK queue work.
				const D3D12_RESOURCE_STATES state = buffer.Memory == EBufferMemory::CpuWrite ? D3D12_RESOURCE_STATE_GENERIC_READ
					: buffer.Memory == EBufferMemory::CpuRead ? D3D12_RESOURCE_STATE_COPY_DEST
					: D3D12_RESOURCE_STATE_COMMON;

				return R::Success(static_cast<uint32>(state));
			}
			// FinishPreparedCommandList records RestoreTrackedStates before Close. That
			// function restores every tracked texture subresource to COMMON. Its tracking
			// rows intentionally retain the pre-close states and cannot describe this edge.
			return R::Success(static_cast<uint32>(D3D12_RESOURCE_STATE_COMMON));
		}
		Error error;
		error.Code = EErrorCode::Unsupported;

		return R::Failure(Move(error));
	}
}
#endif
