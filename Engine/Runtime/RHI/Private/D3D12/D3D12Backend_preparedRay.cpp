#include "D3D12Backend.h"
#include "D3D12Descriptors.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	void DestroyPreparedShaderTable(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		PreparedRayTableOwner* table = static_cast<PreparedRayTableOwner*>(native);
		for (d3d12::PreparedRayTableOwner::DescriptorRange const& range : table->DescriptorRanges)
		{
			ReleaseGlobalDescriptors(state, range.bSampler, range.First, range.Count);
		}
		DestroyObject(state.Resource, table);
	}

	[[nodiscard]] Result<SharedPtr<PreparedShaderTable>> CreatePreparedShaderTable(State* opaque, void* pipelineNative, const NativeShaderTableDesc& table)
	{
		State& state = *opaque;
		const RayTracingPipeline& pipeline = *static_cast<const RayTracingPipeline*>(pipelineNative);
		using R = Result<SharedPtr<PreparedShaderTable>>;
		d3d12::PreparedRayTableOwner* nativeTable = CreateObject<PreparedRayTableOwner>(state.Resource, state.Resource);
		if (nativeTable == nullptr)
		{
			return R::Failure(Failure("Rhi.Allocation", "Could not allocate a native shader table owner."));
		}

		SharedPtr<PreparedShaderTable> owner;
		if (!MakeSharedInto(state.Resource, &owner, opaque, nativeTable))
		{
			DestroyPreparedShaderTable(opaque, nativeTable);

			return R::Failure(Failure("Rhi.Allocation", "Could not retain a native shader table owner."));
		}

		constexpr uint64 handleBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		constexpr uint64 handleAlignment = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
		constexpr uint64 baseAlignment = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
		uint64 localBytes = table.RayGenerationLocal.LocalData.Size();
		for (const rhi::NativeBindingSet& local : table.MissLocals)
		{
			localBytes = Max(localBytes, uint64(local.LocalData.Size()));
		}
		for (const rhi::NativeBindingSet& local : table.HitLocals)
		{
			localBytes = Max(localBytes, uint64(local.LocalData.Size()));
		}
		for (const rhi::NativeBindingSet& local : table.CallableLocals)
		{
			localBytes = Max(localBytes, uint64(local.LocalData.Size()));
		}
		const auto align = [](uint64 value, uint64 alignment) noexcept { return (value + alignment - 1) & ~(alignment - 1); };
		const uint64 stride = align(handleBytes + localBytes, handleAlignment);
		if (stride > D3D12_RAYTRACING_MAX_SHADER_RECORD_STRIDE)
		{
			return R::Failure(EErrorCode::Unsupported, "Rhi.PreparedRay", "The native shader record stride exceeds its device limit.");
		}
		const uint64 missOffset = align(stride, baseAlignment);
		const uint64 hitOffset = align(missOffset + table.MissShaderIndices.Size() * stride, baseAlignment);
		const uint64 callableOffset = align(hitOffset + table.HitGroupIndices.Size() * stride, baseAlignment);
		const uint64 bytes = align(callableOffset + table.CallableShaderIndices.Size() * stride, baseAlignment);
		uint32 argumentCount = 0;
		uint32 descriptorCount = 0;
		const auto countLocal = [&](uint32 group, const NativeBindingSet& local)
			{
				argumentCount += pipeline.LocalRootArgumentOffsets[group + 1] - pipeline.LocalRootArgumentOffsets[group];
				for (const rhi::NativeFixedBindingSet& set : local.FixedSets)
				{
					descriptorCount += static_cast<uint32>(set.Descriptors.Size());
				}
			};
		countLocal(table.RayGenerationShaderIndex, table.RayGenerationLocal);
		for (uint32 index = 0; index < table.MissShaderIndices.Size(); ++index)
		{
			countLocal(table.MissShaderIndices[index], table.MissLocals[index]);
		}
		for (uint32 index = 0; index < table.HitGroupIndices.Size(); ++index)
		{
			countLocal(table.GeneralShaderCount + table.HitGroupIndices[index], table.HitLocals[index]);
		}
		for (uint32 index = 0; index < table.CallableShaderIndices.Size(); ++index)
		{
			countLocal(table.CallableShaderIndices[index], table.CallableLocals[index]);
		}
		if (!nativeTable->DescriptorRanges.Reserve(argumentCount) ||
			!nativeTable->Resources.Reserve(descriptorCount) ||
			!nativeTable->StorageResources.Reserve(descriptorCount) ||
			!nativeTable->Accesses.Reserve(descriptorCount))
		{
			return R::Failure(Failure("Rhi.Allocation", "Could not retain native local record dependencies."));
		}
		nativeTable->bSharedHeaps = pipeline.bTypedLocalRoots;
		if (!CreateResource(state, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, nativeTable->Resource))
		{
			return R::Failure(Failure("Rhi.PreparedRay", "Could not create a prepared shader table."));
		}
		void* mapped = nullptr;
		D3D12_RANGE read{ 0, 0 };
		if (FAILED(nativeTable->Resource->Map(0, &read, &mapped)))
		{
			return R::Failure(Failure("Rhi.PreparedRay", "Could not map a prepared shader table."));
		}
		constexpr uint64 start = 0;
		ScopeExit unmap(
			[&]() noexcept
			{
				D3D12_RANGE written{ 0, static_cast<SIZE_T>(bytes) };
				nativeTable->Resource->Unmap(0, &written);
			});
		const auto copy = [&](uint64 offset, uint32 group, const NativeBindingSet& local) -> Result<void>
			{
				Byte* record = static_cast<Byte*>(mapped) + start + offset;
				CopyBytes(record, pipeline.GroupHandles.Data() + uint64(group) * handleBytes,
					static_cast<usize>(handleBytes));
				if (!local.LocalData.IsEmpty())
				{
					CopyBytes(record + handleBytes, local.LocalData.Data(), local.LocalData.Size());
				}
				for (uint32 index = pipeline.LocalRootArgumentOffsets[group]; index < pipeline.LocalRootArgumentOffsets[group + 1];
					++index)
				{
					rhi::NativeRayLocalRootArgument const& argument = pipeline.LocalRootArguments[index];
					if (argument.Kind == ERayLocalRootArgumentKind::Constants32)
					{
						continue;
					}
					ASSERT(argument.LocalLayoutIndex < local.FixedSets.Size(), "A typed record captures each ordered local set.");
					const Span<const NativeFixedDescriptor>& descriptors = local.FixedSets[argument.LocalLayoutIndex].Descriptors;
					ASSERT(argument.ByteOffset <= local.LocalData.Size() && sizeof(uint64) <= local.LocalData.Size() - argument.ByteOffset,
						"The validated local recipe fits its captured record bytes.");
					ASSERT(argument.DescriptorIndex <= descriptors.Size() && argument.Range.DescriptorCount <= descriptors.Size() - argument.DescriptorIndex,
						"Each native local root range is captured in full.");
					uint64 value = 0;
					if (argument.Kind == ERayLocalRootArgumentKind::DescriptorTable)
					{
						const bool bSampler = argument.Range.Type == EDescriptorType::Sampler;
						Result<uint32> allocated = AllocateGlobalDescriptors(state, bSampler, argument.Range.DescriptorCount);
						if (!allocated)
						{
							return Result<void>::Failure(allocated.GetError());
						}
						nativeTable->DescriptorRanges.AddRequired({ allocated.Value(), argument.Range.DescriptorCount, bSampler });
						value = GlobalDescriptorGpu(state, bSampler, allocated.Value()).ptr;
						for (uint32 element = 0; element < argument.Range.DescriptorCount; ++element)
						{
							Result<void> written =
								WriteNativeFixedDescriptor(state, nullptr, argument.Range, descriptors[argument.DescriptorIndex + element],
									GlobalDescriptorCpu(state, bSampler, allocated.Value() + element));
							if (!written)
							{
								return written;
							}
						}
					}
					else
					{
						const rhi::NativeFixedDescriptor& descriptor = descriptors[argument.DescriptorIndex];
						if (argument.Range.Type == EDescriptorType::AccelerationStructure)
						{
							value = static_cast<const NativeAccelStruct*>(descriptor.Resource)->Address;
						}
						else
						{
							ASSERT(descriptor.View == nullptr && !(descriptor.ByteOffset % (argument.Kind == ERayLocalRootArgumentKind::ConstantBufferAddress ? 256u : 4u) != 0), "buffer range.");
							value = static_cast<const Buffer*>(descriptor.Resource)->Resource->GetGPUVirtualAddress() + descriptor.ByteOffset;
						}
					}
					CopyBytes(record + handleBytes + argument.ByteOffset, &value, sizeof(value));
					for (uint32 element = 0; element < argument.Range.DescriptorCount; ++element)
					{
						const void* dependency = descriptors[argument.DescriptorIndex + element].Resource;
						nativeTable->Accesses.AddRequired({ descriptors[argument.DescriptorIndex + element], argument.Range.Type });
						if (argument.Range.Type == EDescriptorType::Sampler)
						{
							continue;
						}
						nativeTable->Resources.AddRequired(argument.Range.Type == EDescriptorType::AccelerationStructure
							? static_cast<const NativeAccelStruct*>(dependency)->Storage
							: argument.Range.Type == EDescriptorType::SampledTexture ||
							argument.Range.Type == EDescriptorType::StorageTexture
							? static_cast<const Texture*>(dependency)->Resource
							: static_cast<const Buffer*>(dependency)->Resource);
						if (argument.Range.Type == EDescriptorType::StorageTexture || argument.Range.Type == EDescriptorType::StorageBuffer)
						{
							nativeTable->StorageResources.AddRequired(nativeTable->Resources.Back().Get());
						}
					}
				}

				return Result<void>::Success();
			};
		SetBytes(static_cast<Byte*>(mapped) + start, 0, static_cast<usize>(bytes));
		Result<void> copied = copy(0, table.RayGenerationShaderIndex, table.RayGenerationLocal);
		if (!copied)
		{
			return R::Failure(copied.GetError());
		}
		for (uint32 index = 0; index < table.MissShaderIndices.Size(); ++index)
		{
			copied = copy(missOffset + index * stride, table.MissShaderIndices[index], table.MissLocals[index]);
			if (!copied)
			{
				return R::Failure(copied.GetError());
			}
		}
		for (uint32 index = 0; index < table.HitGroupIndices.Size(); ++index)
		{
			copied = copy(hitOffset + index * stride, table.GeneralShaderCount + table.HitGroupIndices[index], table.HitLocals[index]);
			if (!copied)
			{
				return R::Failure(copied.GetError());
			}
		}
		for (uint32 index = 0; index < table.CallableShaderIndices.Size(); ++index)
		{
			copied = copy(callableOffset + index * stride, table.CallableShaderIndices[index], table.CallableLocals[index]);
			if (!copied)
			{
				return R::Failure(copied.GetError());
			}
		}
		D3D12_RANGE written{ 0, static_cast<SIZE_T>(bytes) };
		nativeTable->Resource->Unmap(0, &written);
		unmap.Release();
		const D3D12_GPU_VIRTUAL_ADDRESS address = nativeTable->Resource->GetGPUVirtualAddress();
		nativeTable->Rays.RayGenerationShaderRecord = { address, stride };
		if (!table.MissShaderIndices.IsEmpty())
		{
			nativeTable->Rays.MissShaderTable = { address + missOffset, table.MissShaderIndices.Size() * stride, stride };
		}
		if (!table.HitGroupIndices.IsEmpty())
		{
			nativeTable->Rays.HitGroupTable = { address + hitOffset, table.HitGroupIndices.Size() * stride, stride };
		}
		if (!table.CallableShaderIndices.IsEmpty())
		{
			nativeTable->Rays.CallableShaderTable = { address + callableOffset, table.CallableShaderIndices.Size() * stride, stride };
		}

		return R::Success(Move(owner));
	}

	[[nodiscard]] Result<uint32> PrepareRayBinding(
		State* opaque,
		void* native,
		void* pipelineNative,
		const SharedPtr<PreparedShaderTable>& table,
		Span<const NativeFixedBindingSet> sets,
		Span<const NativePreparedTextureAccess>)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		RayTracingPipeline& pipeline = *static_cast<RayTracingPipeline*>(pipelineNative);
		using R = Result<uint32>;
		if (!pending.RayList && FAILED(pending.List.As(&pending.RayList)))
		{
			return R::Failure(Failure("Rhi.PreparedRay", "Ray dispatch commands are unavailable."));
		}
		MemoryResource& memory = state.Resource;
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
		if (!sets.IsEmpty())
		{
			Result<void> descriptors = WriteFixedBindingSets(opaque, native, pipelineNative, false, sets, &binding, &pipeline);
			if (!descriptors)
			{
				return R::Failure(descriptors.GetError());
			}
		}

		Result<void> storage = CapturePreparedStorage(binding, pipeline.Fixed, sets);
		if (!storage)
		{
			return Result<uint32>::Failure(Move(storage.GetError()));
		}

		ASSERT(table, "Prepared bindings capture a fully realized immutable shader table.");
		const PreparedRayTableOwner& nativeTable = *static_cast<const PreparedRayTableOwner*>(table->GetNative());
		if (nativeTable.bSharedHeaps)
		{
			binding.Resources = state.GlobalResourceHeap.Get();
			binding.Samplers = state.GlobalSamplerHeap.Get();
		}
		binding.RayTable = table;
		binding.Rays = nativeTable.Rays;

		const uint32 index = static_cast<uint32>(pending.PreparedBindings.Size());
		const bool bAdded = pending.PreparedBindings.Add(owner);
		ASSERT(bAdded, "Prepared ray bindings have reserved storage.");

		return R::Success(index);
	}

	[[nodiscard]] Result<void> RecordPreparedRayDispatch(void*, void* native, void* pipelineNative, uint32 index, const RayDispatch& dispatch)
	{
		Pending& pending = *static_cast<Pending*>(native);
		const RayTracingPipeline& pipeline = *static_cast<const RayTracingPipeline*>(pipelineNative);
		pending.InvalidatePreparedGraphics();
		const Pending::PreparedBinding& binding = pending.BindingAt(index);
		const PreparedRayTableOwner& localTable = *static_cast<const PreparedRayTableOwner*>(binding.RayTable->GetNative());
		for (ID3D12Resource* resource : localTable.StorageResources)
		{
			EmitUavBarrier(pending, *resource);
		}
		for (uint32 storage = 0; storage < binding.StorageCount; ++storage)
		{
			EmitUavBarrier(pending, *binding.Storage[storage]);
		}
		ID3D12DescriptorHeap* heaps[2]{};
		uint32 count = 0;
		if (binding.Resources != nullptr)
		{
			heaps[count++] = binding.Resources;
		}
		if (binding.Samplers != nullptr)
		{
			heaps[count++] = binding.Samplers;
		}
		pending.List->SetDescriptorHeaps(count, heaps);
		pending.List->SetComputeRootSignature(pipeline.RootSignature.Get());
		for (uint32 table = 0; table < binding.TableCount; ++table)
		{
			const uint32 root = (pipeline.PushConstantBytes != 0 ? 1u : 0u) + table;
			if (!binding.bRootDescriptors[table])
			{
				pending.List->SetComputeRootDescriptorTable(root, binding.Tables[table]);
			}
			else if (binding.RootTypes[table] == EDescriptorType::UniformBuffer)
			{
				pending.List->SetComputeRootConstantBufferView(root, binding.RootAddresses[table]);
			}
			else if (binding.RootTypes[table] == EDescriptorType::ReadOnlyBuffer)
			{
				pending.List->SetComputeRootShaderResourceView(root, binding.RootAddresses[table]);
			}
			else
			{
				pending.List->SetComputeRootUnorderedAccessView(root, binding.RootAddresses[table]);
			}
		}
		if (pipeline.PushConstantBytes != 0)
		{
			pending.List->SetComputeRoot32BitConstants(pipeline.PushRootIndex, pipeline.PushConstantBytes / 4,
				dispatch.PushConstants.Data(), 0);
		}
		pending.RayList->SetPipelineState1(pipeline.State.Get());
		D3D12_DISPATCH_RAYS_DESC rays = binding.Rays;
		rays.Width = dispatch.Width;
		rays.Height = dispatch.Height;
		rays.Depth = dispatch.Depth;
		pending.RayList->DispatchRays(&rays);
		pending.LastComputePipeline = nullptr;
		pending.LastFixedComputePipeline = nullptr;
		pending.LastTexturePipeline = nullptr;
		pending.BoundTextureHeap = binding.Resources;
		pending.BoundSamplerHeap = binding.Samplers;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordImmutableRayDispatch(
		State* opaque,
		void* native,
		void* pipelineNative,
		const SharedPtr<PreparedShaderTable>& table,
		Span<const NativeFixedBindingSet> globals,
		const RayDispatch& dispatch)
	{
		Pending& pending = *static_cast<Pending*>(native);
		if (!pending.PreparedBindings.Reserve(pending.PreparedBindings.Size() + 1))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain an immutable ray binding."));
		}
		const auto transition = [&](const NativeFixedDescriptor& descriptor, EDescriptorType type)
			{
				if (type == EDescriptorType::SampledTexture || type == EDescriptorType::StorageTexture)
				{
					Texture& texture = *static_cast<Texture*>(descriptor.Resource);
					TransitionTextureView(pending, texture, static_cast<const TextureView*>(descriptor.View),
						type == EDescriptorType::StorageTexture ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				}
				else if (type == EDescriptorType::UniformBuffer || type == EDescriptorType::ReadOnlyBuffer || type == EDescriptorType::StorageBuffer)
				{
					Buffer& buffer = *static_cast<Buffer*>(descriptor.Resource);
					const D3D12_RESOURCE_STATES target =
						type == EDescriptorType::UniformBuffer ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
						: type == EDescriptorType::StorageBuffer ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
					Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, target);
				}
				else if (type == EDescriptorType::AccelerationStructure)
				{
					EmitUavBarrier(pending, *static_cast<const NativeAccelStruct*>(descriptor.Resource)->Storage.Get());
				}
			};
		const RayTracingPipeline& pipeline = *static_cast<const RayTracingPipeline*>(pipelineNative);
		for (uint32 layout = 0; layout < globals.Size(); ++layout)
		{
			uint32 descriptor = 0;
			for (uint32 range = 0; range < pipeline.Fixed.RangeCounts[layout]; ++range)
			{
				const BindingRangeDesc& declaration = pipeline.Fixed.Ranges[layout][range];
				if (declaration.Source == EBindingRangeSource::DescriptorTable)
				{
					const NativeTableRangeBinding* tableRange = nullptr;
					for (uint32 index = 0; index < globals[layout].TableCount; ++index)
					{
						if (globals[layout].Tables[index].RangeIndex == range)
						{
							tableRange = &globals[layout].Tables[index];
							break;
						}
					}
					ASSERT(tableRange != nullptr && tableRange->Table != nullptr, "Every ray table range retains its native descriptor table.");
					TransitionTableUses(pending, *static_cast<const DescriptorTable*>(tableRange->Table),
						tableRange->UsedEntries, false);
					continue;
				}
				for (uint32 element = 0; element < declaration.DescriptorCount; ++element)
				{
					ASSERT(descriptor < globals[layout].Descriptors.Size(), "Every fixed ray descriptor is captured.");
					transition(globals[layout].Descriptors[descriptor++], declaration.Type);
				}
			}
			ASSERT(descriptor == globals[layout].Descriptors.Size(), "The fixed ray descriptor capture is exact.");
		}
		const PreparedRayTableOwner& owner = *static_cast<const PreparedRayTableOwner*>(table->GetNative());
		for (d3d12::PreparedRayTableOwner::Access const& access : owner.Accesses)
		{
			transition(access.Descriptor, access.Type);
		}
		Result<uint32> binding = PrepareRayBinding(opaque, native, pipelineNative, table, globals, {});
		if (!binding)
		{
			return Result<void>::Failure(binding.GetError());
		}

		return RecordPreparedRayDispatch(opaque, native, pipelineNative, binding.Value(), dispatch);
	}
}
#endif
