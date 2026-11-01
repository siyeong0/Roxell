#include "D3D12Backend.h"
#include "D3D12Descriptors.h"

#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<void> BindComputeDescriptorTables(
		State& state,
		Pending& pending,
		const ComputePipeline& pipeline,
		DescriptorTable* sampled,
		DescriptorTable* samplers,
		DescriptorTable* storage,
		DescriptorTable* storageTextures,
		DescriptorTable* uniformBuffers,
		DescriptorTable* readOnlyBuffers,
		Span<const NativeComputeTexture> fixedBindings,
		D3D12_CPU_DESCRIPTOR_HANDLE fixedCpu,
		D3D12_CPU_DESCRIPTOR_HANDLE fixedSamplerCpu,
		ID3D12DescriptorHeap* fixedHeap,
		ID3D12DescriptorHeap* fixedSamplerHeap,
		Span<const NativeComputeBuffer> buffers)
	{
		if (pipeline.bDeviceHeapIndexed)
		{
			Result<void> typed = PrepareGlobalTypedBuffers(state, pending, buffers);

			if (!typed)
			{
				return typed;
			}
			ID3D12DescriptorHeap* const heaps[2]{ state.GlobalResourceHeap.Get(), state.GlobalSamplerHeap.Get() };
			if (pending.BoundTextureHeap != heaps[0] || pending.BoundSamplerHeap != heaps[1])
			{
				pending.List->SetDescriptorHeaps(2, heaps);
				pending.BoundTextureHeap = heaps[0];
				pending.BoundSamplerHeap = heaps[1];
			}
			pending.List->SetComputeRootSignature(pipeline.RootSignature.Get());
			const UINT resourceStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

			if (!fixedBindings.IsEmpty())
			{
				const D3D12_CPU_DESCRIPTOR_HANDLE resourceStart = state.GlobalResourceHeap->GetCPUDescriptorHandleForHeapStart();
				const uint32 first = static_cast<uint32>((fixedCpu.ptr - resourceStart.ptr) / resourceStride);
				const uint32 root = (pipeline.PushConstantBytes != 0 ? 1u : 0u) + pipeline.BufferCount;

				for (uint32 index = 0; index < fixedBindings.Size(); ++index)
				{
					pending.List->SetComputeRootDescriptorTable(root + index, GlobalDescriptorGpu(state, false, first + index));
				}

				if (pipeline.SampledTextureCount != 0)
				{
					const D3D12_CPU_DESCRIPTOR_HANDLE samplerStart = state.GlobalSamplerHeap->GetCPUDescriptorHandleForHeapStart();
					const uint32 firstSampler = static_cast<uint32>((fixedSamplerCpu.ptr - samplerStart.ptr) / samplerStride);

					for (uint32 index = 0; index < pipeline.SampledTextureCount; ++index)
					{
						pending.List->SetComputeRootDescriptorTable(root + static_cast<uint32>(fixedBindings.Size()) + index,
							GlobalDescriptorGpu(state, true, firstSampler + index));
					}
				}
			}

			auto bindResource = [&](DescriptorTable* table, uint32 root)
				{
					if (table == nullptr)
					{
						return;
					}
					D3D12_GPU_DESCRIPTOR_HANDLE address = state.GlobalResourceHeap->GetGPUDescriptorHandleForHeapStart();
					address.ptr += static_cast<UINT64>(table->FirstGlobalDescriptor) * resourceStride;
					pending.List->SetComputeRootDescriptorTable(root, address);
				};
			bindResource(sampled, pipeline.BindlessRootIndex);
			bindResource(storage, pipeline.BindlessStorageBufferRootIndex);
			bindResource(storageTextures, pipeline.BindlessStorageTextureRootIndex);
			bindResource(uniformBuffers, pipeline.BindlessUniformBufferRootIndex);
			bindResource(readOnlyBuffers, pipeline.BindlessReadOnlyBufferRootIndex);

			if (samplers != nullptr)
			{
				D3D12_GPU_DESCRIPTOR_HANDLE address = state.GlobalSamplerHeap->GetGPUDescriptorHandleForHeapStart();
				address.ptr += static_cast<UINT64>(samplers->FirstGlobalDescriptor) * samplerStride;
				pending.List->SetComputeRootDescriptorTable(pipeline.BindlessSamplerRootIndex, address);
			}

			return Result<void>::Success();
		}

		const uint32 fixedCount = static_cast<uint32>(fixedBindings.Size());
		ID3D12DescriptorHeap* resourceHeap = fixedHeap;
		DescriptorTable* const resources[5]{ sampled, storage, storageTextures, uniformBuffers, readOnlyBuffers };
		uint32 offsets[5]{};
		const uint32 typedCount = CountTypedBuffers(buffers);
		uint32 total = fixedCount;
		uint32 count = 0;

		for (uint32 index = 0; index < 5; ++index)
		{
			offsets[index] = total;

			if (resources[index] != nullptr)
			{
				total += resources[index]->Capacity;
				++count;
			}
		}

		const uint32 typedOffset = total;
		total += typedCount;
		bool bReuseFixed = (fixedCount != 0 || typedCount != 0) &&
			(count != 0 || typedCount != 0) &&
			pending.LastComputePipeline == &pipeline &&
			pending.LastComputeSampledTable == sampled &&
			pending.LastComputeSamplerTable == samplers &&
			pending.LastComputeStorageTable == storage &&
			pending.LastComputeStorageTextureTable == storageTextures &&
			pending.LastComputeUniformTable == uniformBuffers &&
			pending.LastComputeReadOnlyTable == readOnlyBuffers &&
			pending.LastComputeTextureCount == fixedCount &&
			pending.LastComputeResourceHeap != nullptr;

		if (typedCount != 0)
		{
			bReuseFixed = bReuseFixed && pending.LastTypedComputeCount == buffers.Size();

			for (uint32 index = 0; bReuseFixed && index < buffers.Size(); ++index)
			{
				bReuseFixed = pending.LastTypedComputeViews[index] == buffers[index].View;
			}
		}

		if (bReuseFixed)
		{
			for (uint32 index = 0; index < fixedCount; ++index)
			{
				const NativeComputeTexture& binding = fixedBindings[index];

				if (pending.LastComputeTextures[index] != binding.Texture ||
					pending.LastComputeViews[index] != binding.View ||
					pending.LastComputeSamplers[index] != binding.Sampler ||
					pending.LastComputeTypes[index] != binding.Type)
				{
					bReuseFixed = false;
					break;
				}
			}
		}

		if (typedCount != 0 || count > 1 || (count != 0 && fixedCount != 0))
		{
			if (bReuseFixed)
			{
				resourceHeap = pending.LastComputeResourceHeap;
			}
			else if (typedCount == 0 &&
				fixedCount == 0 &&
				pending.LastMergedSampledTable == sampled &&
				pending.LastMergedStorageTable == storage &&
				pending.LastMergedStorageTextureTable == storageTextures &&
				pending.LastMergedUniformTable == uniformBuffers &&
				pending.LastMergedReadOnlyTable == readOnlyBuffers)
			{
				resourceHeap = pending.LastMergedResourceHeap;
			}
			else
			{
				D3D12_DESCRIPTOR_HEAP_DESC desc{};
				desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				desc.NumDescriptors = total;
				desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				ComPtr<ID3D12DescriptorHeap> combined;

				if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(combined.GetAddressOf()))))
				{
					return Result<void>::Failure(Failure("Rhi.D3D12Descriptors", "Could not combine the compute descriptor tables."));
				}

				const UINT stride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				D3D12_CPU_DESCRIPTOR_HANDLE destination = combined->GetCPUDescriptorHandleForHeapStart();
				D3D12_CPU_DESCRIPTOR_HANDLE fixedSource = fixedCpu;

				for (uint32 index = 0; index < fixedCount; ++index)
				{
					CopyCommandDescriptors(state, pending, 1, destination, fixedSource, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					destination.ptr += stride;
					fixedSource.ptr += stride;
				}

				for (DescriptorTable* const table : resources)
				{
					if (table == nullptr)
					{
						continue;
					}
					D3D12_CPU_DESCRIPTOR_HANDLE source = table->CpuHeap->GetCPUDescriptorHandleForHeapStart();

					for (uint32 index = 0; index < table->Capacity; ++index)
					{
						const bool bValid =
							(table->Type == EDescriptorType::StorageBuffer || table->Type == EDescriptorType::UniformBuffer ||
								table->Type == EDescriptorType::ReadOnlyBuffer)
							? table->Buffers[index] != nullptr
							: table->Textures[index] != nullptr;

						if (bValid)
						{
							CopyCommandDescriptors(state, pending, 1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						}
						destination.ptr += stride;
						source.ptr += stride;
					}
				}
				CopyTypedBufferDescriptors(state, pending, buffers, destination);

				if (!pending.BindlessHeaps.Add(Move(combined)))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain combined compute descriptors."));
				}
				resourceHeap = pending.BindlessHeaps.Back().Get();
				pending.LastMergedSampledTable = sampled;
				pending.LastMergedStorageTable = storage;
				pending.LastMergedStorageTextureTable = storageTextures;
				pending.LastMergedUniformTable = uniformBuffers;
				pending.LastMergedReadOnlyTable = readOnlyBuffers;
				pending.LastMergedResourceHeap = resourceHeap;
			}
		}
		else if (count == 1)
		{
			DescriptorTable* const table = sampled != nullptr ? sampled
				: storage != nullptr ? storage
				: storageTextures != nullptr ? storageTextures
				: uniformBuffers != nullptr ? uniformBuffers
				: readOnlyBuffers;
			resourceHeap = table->Heap.Get();
			bool bRetained = false;

			for (Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> const& heap : pending.BindlessHeaps)
			{
				if (heap.Get() == resourceHeap)
				{
					bRetained = true;
					break;
				}
			}

			if (!bRetained && !pending.BindlessHeaps.Add(table->Heap))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain the compute resource table."));
			}
		}
		ID3D12DescriptorHeap* samplerHeap = fixedSamplerHeap;

		if (samplers != nullptr && pipeline.SampledTextureCount != 0 && bReuseFixed && pending.LastComputeSamplerHeap != nullptr)
		{
			samplerHeap = pending.LastComputeSamplerHeap;
		}
		else if (samplers != nullptr && pipeline.SampledTextureCount != 0)
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc{};
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			desc.NumDescriptors = pipeline.SampledTextureCount + samplers->Capacity;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			ComPtr<ID3D12DescriptorHeap> combined;

			if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(combined.GetAddressOf()))))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12Descriptors", "Could not combine fixed and bindless compute samplers."));
			}

			const UINT stride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			D3D12_CPU_DESCRIPTOR_HANDLE destination = combined->GetCPUDescriptorHandleForHeapStart();
			D3D12_CPU_DESCRIPTOR_HANDLE source = fixedSamplerCpu;

			for (uint32 index = 0; index < pipeline.SampledTextureCount; ++index)
			{
				CopyCommandDescriptors(state, pending, 1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
				destination.ptr += stride;
				source.ptr += stride;
			}
			source = samplers->CpuHeap->GetCPUDescriptorHandleForHeapStart();

			for (uint32 index = 0; index < samplers->Capacity; ++index)
			{
				if (samplers->SamplerValid[index] != 0)
				{
					CopyCommandDescriptors(state, pending, 1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
				}
				destination.ptr += stride;
				source.ptr += stride;
			}

			if (!pending.BindlessHeaps.Add(Move(combined)))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain combined compute samplers."));
			}
			samplerHeap = pending.BindlessHeaps.Back().Get();
		}
		else if (samplers != nullptr)
		{
			samplerHeap = samplers->Heap.Get();
			bool bRetained = false;

			for (Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> const& heap : pending.BindlessHeaps)
			{
				if (heap.Get() == samplers->Heap.Get())
				{
					bRetained = true;
					break;
				}
			}

			if (!bRetained && !pending.BindlessHeaps.Add(samplers->Heap))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain the compute sampler table."));
			}

			if (resourceHeap == nullptr)
			{
				Result<void> ensured = EnsureEmptyTextureHeap(state, pending);

				if (!ensured)
				{
					return ensured;
				}
				resourceHeap = pending.EmptyTextureHeap.Get();
			}
		}

		if (resourceHeap == nullptr)
		{
			return Result<void>::Success();
		}

		if (typedCount != 0)
		{
			pending.TypedBufferGpu = resourceHeap->GetGPUDescriptorHandleForHeapStart();
			pending.TypedBufferGpu.ptr += static_cast<UINT64>(typedOffset) * pending.TypedBufferStride;
		}
		ID3D12DescriptorHeap* const heaps[2]{ resourceHeap, samplerHeap };
		if (pending.BoundTextureHeap != heaps[0] || pending.BoundSamplerHeap != heaps[1])
		{
			pending.List->SetDescriptorHeaps(heaps[1] != nullptr ? 2u : 1u, heaps);
			pending.BoundTextureHeap = heaps[0];
			pending.BoundSamplerHeap = heaps[1];
		}

		if (pipeline.bHeapDirectlyIndexed)
		{
			pending.List->SetComputeRootSignature(pipeline.RootSignature.Get());
		}

		if (fixedCount != 0)
		{
			D3D12_GPU_DESCRIPTOR_HANDLE fixed = resourceHeap->GetGPUDescriptorHandleForHeapStart();

			if (resourceHeap == fixedHeap)
			{
				fixed.ptr += fixedCpu.ptr - fixedHeap->GetCPUDescriptorHandleForHeapStart().ptr;
			}

			const UINT stride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			const uint32 root = (pipeline.PushConstantBytes != 0 ? 1u : 0u) + pipeline.BufferCount;

			for (uint32 index = 0; index < fixedCount; ++index)
			{
				pending.List->SetComputeRootDescriptorTable(root + index, fixed);
				fixed.ptr += stride;
			}
		}

		if (pipeline.SampledTextureCount != 0)
		{
			D3D12_GPU_DESCRIPTOR_HANDLE fixed = samplerHeap->GetGPUDescriptorHandleForHeapStart();

			if (samplerHeap == fixedSamplerHeap)
			{
				fixed.ptr += fixedSamplerCpu.ptr - fixedSamplerHeap->GetCPUDescriptorHandleForHeapStart().ptr;
			}

			const UINT stride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			const uint32 root = (pipeline.PushConstantBytes != 0 ? 1u : 0u) + pipeline.BufferCount + fixedCount;

			for (uint32 index = 0; index < pipeline.SampledTextureCount; ++index)
			{
				pending.List->SetComputeRootDescriptorTable(root + index, fixed);
				fixed.ptr += stride;
			}
		}
		D3D12_GPU_DESCRIPTOR_HANDLE address = resourceHeap->GetGPUDescriptorHandleForHeapStart();

		if (sampled != nullptr)
		{
			address.ptr += static_cast<UINT64>(offsets[0]) * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			pending.List->SetComputeRootDescriptorTable(pipeline.BindlessRootIndex, address);
		}

		if (storage != nullptr)
		{
			address = resourceHeap->GetGPUDescriptorHandleForHeapStart();
			address.ptr += static_cast<UINT64>(offsets[1]) * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			pending.List->SetComputeRootDescriptorTable(pipeline.BindlessStorageBufferRootIndex, address);
		}

		if (storageTextures != nullptr)
		{
			address = resourceHeap->GetGPUDescriptorHandleForHeapStart();
			address.ptr += static_cast<UINT64>(offsets[2]) * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			pending.List->SetComputeRootDescriptorTable(pipeline.BindlessStorageTextureRootIndex, address);
		}

		if (uniformBuffers != nullptr)
		{
			address = resourceHeap->GetGPUDescriptorHandleForHeapStart();
			address.ptr += static_cast<UINT64>(offsets[3]) * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			pending.List->SetComputeRootDescriptorTable(pipeline.BindlessUniformBufferRootIndex, address);
		}

		if (readOnlyBuffers != nullptr)
		{
			address = resourceHeap->GetGPUDescriptorHandleForHeapStart();
			address.ptr += static_cast<UINT64>(offsets[4]) * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			pending.List->SetComputeRootDescriptorTable(pipeline.BindlessReadOnlyBufferRootIndex, address);
		}

		if (samplers != nullptr)
		{
			D3D12_GPU_DESCRIPTOR_HANDLE samplerAddress = samplerHeap->GetGPUDescriptorHandleForHeapStart();
			samplerAddress.ptr += static_cast<UINT64>(pipeline.SampledTextureCount) * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			pending.List->SetComputeRootDescriptorTable(pipeline.BindlessSamplerRootIndex, samplerAddress);
		}

		if ((fixedCount != 0 && count != 0) || typedCount != 0)
		{
			pending.LastTypedComputeCount = static_cast<uint32>(buffers.Size());

			for (uint32 index = 0; index < buffers.Size(); ++index)
			{
				pending.LastTypedComputeViews[index] = buffers[index].View;
			}
			pending.LastComputePipeline = &pipeline;
			pending.LastComputeSampledTable = sampled;
			pending.LastComputeSamplerTable = samplers;
			pending.LastComputeStorageTable = storage;
			pending.LastComputeStorageTextureTable = storageTextures;
			pending.LastComputeUniformTable = uniformBuffers;
			pending.LastComputeReadOnlyTable = readOnlyBuffers;
			pending.LastComputeTextureCount = fixedCount;
			pending.LastComputeResourceHeap = resourceHeap;
			pending.LastComputeSamplerHeap = samplers != nullptr && pipeline.SampledTextureCount != 0 ? samplerHeap : nullptr;

			for (uint32 index = 0; index < fixedCount; ++index)
			{
				const NativeComputeTexture& binding = fixedBindings[index];
				pending.LastComputeTextures[index] = binding.Texture;
				pending.LastComputeViews[index] = binding.View;
				pending.LastComputeSamplers[index] = binding.Sampler;
				pending.LastComputeTypes[index] = binding.Type;
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordComputeDispatch(
		State* opaque,
		void* native,
		void* pipelineNative,
		Span<const NativeComputeBuffer> buffers,
		Span<const NativeComputeTexture> textures,
		uint32 groupsX,
		uint32 groupsY,
		uint32 groupsZ,
		void* indirectNative,
		uint64 indirectOffset,
		Span<const Byte> pushConstants,
		void* bindlessNative,
		void* samplerNative,
		void* storageBufferNative,
		void* storageTextureNative,
		void* uniformBufferNative,
		void* readOnlyBufferNative,
		void* accelNative)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		ComputePipeline& pipeline = *static_cast<ComputePipeline*>(pipelineNative);
		if (pipeline.Fixed.LayoutCount == 0)
		{
			pending.List->SetComputeRootSignature(pipeline.RootSignature.Get());
		}
		pending.List->SetPipelineState(pipeline.State.Get());
		const uint32 rootOffset = pipeline.PushConstantBytes != 0 ? 1 : 0;

		if (rootOffset != 0)
		{
			pending.List->SetComputeRoot32BitConstants(0, pipeline.PushConstantBytes / 4, pushConstants.Data(), 0);
		}

		if (pipeline.Fixed.LayoutCount == 0)
		{
			for (usize index = 0; index < buffers.Size(); ++index)
			{
				Buffer& buffer = *static_cast<Buffer*>(buffers[index].Buffer);
				const bool bUniform = buffers[index].Type == EComputeBindingType::UniformBuffer;
				const bool bReadOnly = buffers[index].Type == EComputeBindingType::ReadOnlyBuffer;
				bool bFirst = true;

				for (usize previous = 0; previous < index; ++previous)
				{
					if (buffers[previous].Buffer == buffers[index].Buffer)
					{
						bFirst = false;
						break;
					}
				}

				if (bFirst)
				{
					Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
						bUniform ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
						: bReadOnly ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
						: D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				}

				if (buffers[index].View != nullptr)
				{
					continue;
				}

				if (bUniform)
				{
					pending.List->SetComputeRootConstantBufferView(rootOffset + static_cast<UINT>(index),
						buffer.Resource->GetGPUVirtualAddress() + buffers[index].ByteOffset);
				}
				else if (bReadOnly)
				{
					pending.List->SetComputeRootShaderResourceView(rootOffset + static_cast<UINT>(index),
						buffer.Resource->GetGPUVirtualAddress() + buffers[index].ByteOffset);
				}
				else
				{
					pending.List->SetComputeRootUnorderedAccessView(rootOffset + static_cast<UINT>(index),
						buffer.Resource->GetGPUVirtualAddress() + buffers[index].ByteOffset);
				}
			}

			if (accelNative != nullptr)
			{
				pending.List->SetComputeRootShaderResourceView(pipeline.AccelRootIndex,
					static_cast<const NativeAccelStruct*>(accelNative)->Address);
			}
			D3D12_CPU_DESCRIPTOR_HANDLE fixedCpu{};
			D3D12_CPU_DESCRIPTOR_HANDLE fixedSamplerCpu{};
			ID3D12DescriptorHeap* fixedHeap = nullptr;
			ID3D12DescriptorHeap* fixedSamplerHeap = nullptr;

			if (pipeline.bDeviceHeapIndexed && !textures.IsEmpty())
			{
				bool bReuse =
					pending.LastComputePipeline == &pipeline &&
					pending.LastComputeResourceHeap == state.GlobalResourceHeap.Get() &&
					pending.LastComputeSamplerHeap == (pipeline.SampledTextureCount != 0 ? state.GlobalSamplerHeap.Get() : nullptr) &&
					pending.LastComputeTextureCount == textures.Size() &&
					pending.LastComputeGlobalResourceFirst != UINT32_MAX;

				for (usize index = 0; bReuse && index < textures.Size(); ++index)
				{
					bReuse = pending.LastComputeTextures[index] == textures[index].Texture &&
						pending.LastComputeViews[index] == textures[index].View &&
						pending.LastComputeSamplers[index] == textures[index].Sampler &&
						pending.LastComputeTypes[index] == textures[index].Type;
				}

				if (bReuse)
				{
					fixedCpu = GlobalDescriptorCpu(state, false, pending.LastComputeGlobalResourceFirst);

					if (pipeline.SampledTextureCount != 0)
					{
						fixedSamplerCpu = GlobalDescriptorCpu(state, true, pending.LastComputeGlobalSamplerFirst);
					}

					for (const NativeComputeTexture& binding : textures)
					{
						Texture& texture = *static_cast<Texture*>(binding.Texture);
						TransitionTextureView(pending, texture, static_cast<const TextureView*>(binding.View),
							binding.Type == EComputeTextureBindingType::StorageTexture
							? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
							: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					}
				}
				else
				{
					Result<uint32> resourceRange = AllocatePendingGlobalDescriptors(state, pending, false, static_cast<uint32>(textures.Size()));

					if (!resourceRange)
					{
						return Result<void>::Failure(resourceRange.GetError());
					}
					fixedCpu = GlobalDescriptorCpu(state, false, resourceRange.Value());
					uint32 samplerFirst = UINT32_MAX;

					if (pipeline.SampledTextureCount != 0)
					{
						Result<uint32> samplerRange = AllocatePendingGlobalDescriptors(state, pending, true, pipeline.SampledTextureCount);

						if (!samplerRange)
						{
							return Result<void>::Failure(samplerRange.GetError());
						}
						samplerFirst = samplerRange.Value();
						fixedSamplerCpu = GlobalDescriptorCpu(state, true, samplerRange.Value());
					}

					const UINT resourceStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
					D3D12_CPU_DESCRIPTOR_HANDLE resourceCpu = fixedCpu;
					D3D12_CPU_DESCRIPTOR_HANDLE samplerCpu = fixedSamplerCpu;
					uint32 samplerIndex = 0;

					for (const NativeComputeTexture& binding : textures)
					{
						Texture& texture = *static_cast<Texture*>(binding.Texture);
						const bool bStorage = binding.Type == EComputeTextureBindingType::StorageTexture;
						TransitionTextureView(pending, texture, static_cast<const TextureView*>(binding.View),
							bStorage ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
							: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
						CopyCommandDescriptors(state, pending, 1, resourceCpu, TextureDescriptorCpu(texture, binding.View, bStorage),
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

						if (!bStorage)
						{
							D3D12_CPU_DESCRIPTOR_HANDLE source = pipeline.DefaultSamplers->GetCPUDescriptorHandleForHeapStart();
							source.ptr += static_cast<SIZE_T>(samplerIndex) * samplerStride;

							if (binding.Sampler != nullptr)
							{
								source = static_cast<const Sampler*>(binding.Sampler)->Descriptor->GetCPUDescriptorHandleForHeapStart();
							}
							CopyCommandDescriptors(state, pending, 1, samplerCpu, source, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
							samplerCpu.ptr += samplerStride;
							++samplerIndex;
						}
						resourceCpu.ptr += resourceStride;
					}
					pending.LastComputePipeline = &pipeline;
					pending.LastComputeResourceHeap = state.GlobalResourceHeap.Get();
					pending.LastComputeSamplerHeap = pipeline.SampledTextureCount != 0 ? state.GlobalSamplerHeap.Get() : nullptr;
					pending.LastComputeGlobalResourceFirst = resourceRange.Value();
					pending.LastComputeGlobalSamplerFirst = samplerFirst;
					pending.LastComputeTextureCount = static_cast<uint32>(textures.Size());

					for (usize index = 0; index < textures.Size(); ++index)
					{
						pending.LastComputeTextures[index] = textures[index].Texture;
						pending.LastComputeViews[index] = textures[index].View;
						pending.LastComputeSamplers[index] = textures[index].Sampler;
						pending.LastComputeTypes[index] = textures[index].Type;
					}
				}
			}
			else if (!textures.IsEmpty())
			{
				bool bReuseFixed =
					pending.LastFixedComputePipeline == &pipeline &&
					pending.LastFixedComputeTextureCount == textures.Size() &&
					!pending.TextureHeaps.IsEmpty() &&
					pending.LastFixedComputeResourceHeap == pending.TextureHeaps.Back().Get() &&
					(pipeline.SampledTextureCount == 0 || (!pending.SamplerHeaps.IsEmpty() && pending.LastFixedComputeSamplerHeap == pending.SamplerHeaps.Back().Get()));

				for (usize index = 0; bReuseFixed && index < textures.Size(); ++index)
				{
					const NativeComputeTexture& previous = pending.LastFixedComputeTextures[index];
					bReuseFixed = previous.Texture == textures[index].Texture && previous.View == textures[index].View &&
						previous.Sampler == textures[index].Sampler && previous.Type == textures[index].Type;
				}
				constexpr UINT DESCRIPTORS_PER_HEAP = 256;

				if (!bReuseFixed && (pending.TextureHeaps.IsEmpty() || pending.TextureHeapUsed + textures.Size() > DESCRIPTORS_PER_HEAP))
				{
					D3D12_DESCRIPTOR_HEAP_DESC desc{};
					desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
					desc.NumDescriptors = DESCRIPTORS_PER_HEAP;
					desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
					ComPtr<ID3D12DescriptorHeap> heap;

					if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap.GetAddressOf()))))
					{
						return Result<void>::Failure(Failure("Rhi.D3D12Descriptors", "Could not allocate compute texture descriptors."));
					}

					if (!pending.TextureHeaps.Add(Move(heap)))
					{
						return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain compute texture descriptors."));
					}
					pending.TextureHeapUsed = 0;
				}

				if (!bReuseFixed && pipeline.SampledTextureCount != 0 && (pending.SamplerHeaps.IsEmpty() || pending.SamplerHeapUsed + pipeline.SampledTextureCount > DESCRIPTORS_PER_HEAP))
				{
					D3D12_DESCRIPTOR_HEAP_DESC desc{};
					desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
					desc.NumDescriptors = DESCRIPTORS_PER_HEAP;
					desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
					ComPtr<ID3D12DescriptorHeap> samplerHeap;

					if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(samplerHeap.GetAddressOf()))))
					{
						return Result<void>::Failure(Failure("Rhi.D3D12Descriptors", "Could not allocate compute samplers."));
					}

					if (!pending.SamplerHeaps.Add(Move(samplerHeap)))
					{
						return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain compute samplers."));
					}
					pending.SamplerHeapUsed = 0;
				}
				ID3D12DescriptorHeap* const heap = pending.TextureHeaps.Back().Get();
				ID3D12DescriptorHeap* const samplerHeap = pipeline.SampledTextureCount != 0 ? pending.SamplerHeaps.Back().Get() : nullptr;
				fixedHeap = heap;
				fixedSamplerHeap = samplerHeap;
				const UINT stride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
				D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
				D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
				D3D12_CPU_DESCRIPTOR_HANDLE samplerCpu =
					samplerHeap != nullptr ? samplerHeap->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};
				D3D12_GPU_DESCRIPTOR_HANDLE samplerGpu =
					samplerHeap != nullptr ? samplerHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
				const uint32 resourceFirst = bReuseFixed ? pending.LastFixedComputeResourceFirst : pending.TextureHeapUsed;
				const uint32 samplerFirst = bReuseFixed ? pending.LastFixedComputeSamplerFirst : pending.SamplerHeapUsed;
				cpu.ptr += static_cast<SIZE_T>(resourceFirst) * stride;
				gpu.ptr += static_cast<UINT64>(resourceFirst) * stride;
				samplerCpu.ptr += static_cast<SIZE_T>(samplerFirst) * samplerStride;
				samplerGpu.ptr += static_cast<UINT64>(samplerFirst) * samplerStride;
				fixedCpu = cpu;
				fixedSamplerCpu = samplerCpu;

				if (pending.BoundTextureHeap != heap || pending.BoundSamplerHeap != samplerHeap)
				{
					ID3D12DescriptorHeap* heaps[2]{ heap, samplerHeap };
					pending.List->SetDescriptorHeaps(samplerHeap != nullptr ? 2 : 1, heaps);
					pending.BoundTextureHeap = heap;
					pending.BoundSamplerHeap = samplerHeap;
				}

				uint32 samplerIndex = 0;

				for (usize index = 0; index < textures.Size(); ++index)
				{
					Texture& texture = *static_cast<Texture*>(textures[index].Texture);
					const bool bStorage = textures[index].Type == EComputeTextureBindingType::StorageTexture;
					TransitionTextureView(pending, texture, static_cast<const TextureView*>(textures[index].View),
						bStorage ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

					if (!bReuseFixed)
					{
						CopyCommandDescriptors(state, pending, 1, cpu, TextureDescriptorCpu(texture, textures[index].View, bStorage),
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					}
					pending.List->SetComputeRootDescriptorTable(rootOffset + pipeline.BufferCount + static_cast<UINT>(index), gpu);

					if (!bStorage)
					{
						D3D12_CPU_DESCRIPTOR_HANDLE sourceSampler = pipeline.DefaultSamplers->GetCPUDescriptorHandleForHeapStart();
						sourceSampler.ptr += static_cast<SIZE_T>(samplerIndex) * samplerStride;

						if (textures[index].Sampler != nullptr)
						{
							sourceSampler = static_cast<const Sampler*>(textures[index].Sampler)->Descriptor->GetCPUDescriptorHandleForHeapStart();
						}

						if (!bReuseFixed)
						{
							CopyCommandDescriptors(state, pending, 1, samplerCpu, sourceSampler, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
						}
						pending.List->SetComputeRootDescriptorTable(
							rootOffset + pipeline.BufferCount + static_cast<UINT>(textures.Size()) + samplerIndex, samplerGpu);
						samplerCpu.ptr += samplerStride;
						samplerGpu.ptr += samplerStride;
						++samplerIndex;
					}
					cpu.ptr += stride;
					gpu.ptr += stride;
				}

				if (!bReuseFixed)
				{
					pending.LastFixedComputePipeline = &pipeline;
					pending.LastFixedComputeTextureCount = static_cast<uint32>(textures.Size());
					pending.LastFixedComputeResourceHeap = heap;
					pending.LastFixedComputeSamplerHeap = samplerHeap;
					pending.LastFixedComputeResourceFirst = resourceFirst;
					pending.LastFixedComputeSamplerFirst = samplerFirst;

					for (usize index = 0; index < textures.Size(); ++index)
					{
						pending.LastFixedComputeTextures[index] = textures[index];
					}
					pending.TextureHeapUsed += static_cast<uint32>(textures.Size());
					pending.SamplerHeapUsed += samplerIndex;
				}
			}

			if (bindlessNative != nullptr)
			{
				DescriptorTable& table = *static_cast<DescriptorTable*>(bindlessNative);

				for (usize index = 0; index < table.Textures.Size(); ++index)
				{
					Texture* const texture = table.Textures[index];

					if (texture == nullptr)
					{
						continue;
					}
					bool bFirst = true;

					for (usize previous = 0; previous < index; ++previous)
					{
						if (table.Textures[previous] == texture && table.TextureViews[previous] == table.TextureViews[index])
						{
							bFirst = false;
							break;
						}
					}

					if (!bFirst)
					{
						continue;
					}
					TransitionTextureView(pending, *texture, table.TextureViews[index], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				}
			}

			if (storageBufferNative != nullptr)
			{
				DescriptorTable& table = *static_cast<DescriptorTable*>(storageBufferNative);

				for (usize index = 0; index < table.Buffers.Size(); ++index)
				{
					Buffer* const buffer = table.Buffers[index];

					if (buffer == nullptr)
					{
						continue;
					}
					bool bFirst = true;

					for (usize previous = 0; previous < index; ++previous)
					{
						if (table.Buffers[previous] == buffer)
						{
							bFirst = false;
							break;
						}
					}

					if (bFirst)
					{
						Transition(pending, *buffer->Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					}
				}
			}

			if (storageTextureNative != nullptr)
			{
				DescriptorTable& table = *static_cast<DescriptorTable*>(storageTextureNative);

				for (usize index = 0; index < table.Textures.Size(); ++index)
				{
					Texture* const texture = table.Textures[index];

					if (texture == nullptr)
					{
						continue;
					}
					bool bFirst = true;

					for (usize previous = 0; previous < index; ++previous)
					{
						if (table.Textures[previous] == texture && table.TextureViews[previous] == table.TextureViews[index])
						{
							bFirst = false;
							break;
						}
					}

					if (!bFirst)
					{
						continue;
					}
					TransitionTextureView(pending, *texture, table.TextureViews[index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				}
			}

			if (uniformBufferNative != nullptr)
			{
				DescriptorTable& table = *static_cast<DescriptorTable*>(uniformBufferNative);

				for (usize index = 0; index < table.Buffers.Size(); ++index)
				{
					Buffer* const buffer = table.Buffers[index];

					if (buffer == nullptr)
					{
						continue;
					}
					bool bFirst = true;

					for (usize previous = 0; previous < index; ++previous)
					{
						if (table.Buffers[previous] == buffer)
						{
							bFirst = false;
							break;
						}
					}

					if (bFirst)
					{
						Transition(pending, *buffer->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
							D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
					}
				}
			}

			if (readOnlyBufferNative != nullptr)
			{
				DescriptorTable& table = *static_cast<DescriptorTable*>(readOnlyBufferNative);

				for (usize index = 0; index < table.Buffers.Size(); ++index)
				{
					Buffer* const buffer = table.Buffers[index];

					if (buffer == nullptr)
					{
						continue;
					}
					bool bFirst = true;

					for (usize previous = 0; previous < index; ++previous)
					{
						if (table.Buffers[previous] == buffer)
						{
							bFirst = false;
							break;
						}
					}

					if (bFirst)
					{
						Transition(pending, *buffer->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
							D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					}
				}
			}

			Result<void> boundTables = BindComputeDescriptorTables(
				state, pending, pipeline, static_cast<DescriptorTable*>(bindlessNative), static_cast<DescriptorTable*>(samplerNative),
				static_cast<DescriptorTable*>(storageBufferNative), static_cast<DescriptorTable*>(storageTextureNative),
				static_cast<DescriptorTable*>(uniformBufferNative), static_cast<DescriptorTable*>(readOnlyBufferNative), textures, fixedCpu,
				fixedSamplerCpu, fixedHeap, fixedSamplerHeap, buffers);

			if (!boundTables)
			{
				return boundTables;
			}

			if (pipeline.bHeapDirectlyIndexed)
			{
				if (rootOffset != 0)
				{
					pending.List->SetComputeRoot32BitConstants(0, pipeline.PushConstantBytes / 4, pushConstants.Data(), 0);
				}

				for (usize index = 0; index < buffers.Size(); ++index)
				{
					const rhi::NativeComputeBuffer& binding = buffers[index];

					if (binding.View != nullptr)
					{
						continue;
					}

					const Buffer& buffer = *static_cast<Buffer*>(binding.Buffer);
					const UINT root = rootOffset + static_cast<UINT>(index);
					const D3D12_GPU_VIRTUAL_ADDRESS address = buffer.Resource->GetGPUVirtualAddress() + binding.ByteOffset;

					if (binding.Type == EComputeBindingType::UniformBuffer)
					{
						pending.List->SetComputeRootConstantBufferView(root, address);
					}
					else if (binding.Type == EComputeBindingType::ReadOnlyBuffer)
					{
						pending.List->SetComputeRootShaderResourceView(root, address);
					}
					else
					{
						pending.List->SetComputeRootUnorderedAccessView(root, address);
					}
				}

				if (accelNative != nullptr)
				{
					pending.List->SetComputeRootShaderResourceView(pipeline.AccelRootIndex,
						static_cast<const NativeAccelStruct*>(accelNative)->Address);
				}
			}
			BindTypedBufferRoots(pending, buffers, rootOffset, false);
		}

		if (indirectNative != nullptr)
		{
			Buffer& arguments = *static_cast<Buffer*>(indirectNative);
			Transition(pending, *arguments.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			pending.List->ExecuteIndirect(state.DispatchSignature.Get(), 1, arguments.Resource.Get(), indirectOffset, nullptr, 0);
			Transition(pending, *arguments.Resource.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
		}
		else
		{
			pending.List->Dispatch(groupsX, groupsY, groupsZ);
		}

		for (usize index = 0; index < buffers.Size(); ++index)
		{
			const NativeComputeBuffer& binding = buffers[index];
			bool bFirst = true;

			for (usize previous = 0; previous < index; ++previous)
			{
				if (buffers[previous].Buffer == binding.Buffer)
				{
					bFirst = false;
					break;
				}
			}

			if (!bFirst)
			{
				continue;
			}

			Buffer& buffer = *static_cast<Buffer*>(binding.Buffer);
			Transition(pending, *buffer.Resource.Get(),
				binding.Type == EComputeBindingType::UniformBuffer ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
				: binding.Type == EComputeBindingType::ReadOnlyBuffer ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
				: D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_COMMON);
		}

		for (const NativeComputeTexture& binding : textures)
		{
			Texture& texture = *static_cast<Texture*>(binding.Texture);
			Transition(pending, *texture.Resource.Get(),
				binding.Type == EComputeTextureBindingType::StorageTexture ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_COMMON);
		}

		if (bindlessNative != nullptr)
		{
			DescriptorTable& table = *static_cast<DescriptorTable*>(bindlessNative);

			for (usize index = 0; index < table.Textures.Size(); ++index)
			{
				Texture* const texture = table.Textures[index];

				if (texture == nullptr)
				{
					continue;
				}
				bool bFirst = true;

				for (usize previous = 0; previous < index; ++previous)
				{
					if (table.Textures[previous] == texture)
					{
						bFirst = false;
						break;
					}
				}

				if (bFirst)
				{
					Transition(pending, *texture->Resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_COMMON);
				}
			}
		}

		if (storageBufferNative != nullptr)
		{
			DescriptorTable& table = *static_cast<DescriptorTable*>(storageBufferNative);

			for (usize index = 0; index < table.Buffers.Size(); ++index)
			{
				Buffer* const buffer = table.Buffers[index];

				if (buffer == nullptr)
				{
					continue;
				}
				bool bFirst = true;

				for (usize previous = 0; previous < index; ++previous)
				{
					if (table.Buffers[previous] == buffer)
					{
						bFirst = false;
						break;
					}
				}

				if (bFirst)
				{
					Transition(pending, *buffer->Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
				}
			}
		}

		if (storageTextureNative != nullptr)
		{
			DescriptorTable& table = *static_cast<DescriptorTable*>(storageTextureNative);

			for (usize index = 0; index < table.Textures.Size(); ++index)
			{
				Texture* const texture = table.Textures[index];

				if (texture == nullptr)
				{
					continue;
				}
				bool bFirst = true;

				for (usize previous = 0; previous < index; ++previous)
				{
					if (table.Textures[previous] == texture)
					{
						bFirst = false;
						break;
					}
				}

				if (bFirst)
				{
					Transition(pending, *texture->Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
				}
			}
		}

		if (uniformBufferNative != nullptr)
		{
			DescriptorTable& table = *static_cast<DescriptorTable*>(uniformBufferNative);

			for (usize index = 0; index < table.Buffers.Size(); ++index)
			{
				Buffer* const buffer = table.Buffers[index];

				if (buffer == nullptr)
				{
					continue;
				}
				bool bFirst = true;

				for (usize previous = 0; previous < index; ++previous)
				{
					if (table.Buffers[previous] == buffer)
					{
						bFirst = false;
						break;
					}
				}

				if (bFirst)
				{
					Transition(pending, *buffer->Resource.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
						D3D12_RESOURCE_STATE_COMMON);
				}
			}
		}

		if (readOnlyBufferNative != nullptr)
		{
			DescriptorTable& table = *static_cast<DescriptorTable*>(readOnlyBufferNative);

			for (usize index = 0; index < table.Buffers.Size(); ++index)
			{
				Buffer* const buffer = table.Buffers[index];

				if (buffer == nullptr)
				{
					continue;
				}
				bool bFirst = true;

				for (usize previous = 0; previous < index; ++previous)
				{
					if (table.Buffers[previous] == buffer)
					{
						bFirst = false;
						break;
					}
				}

				if (bFirst)
				{
					Transition(pending, *buffer->Resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_COMMON);
				}
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordComputeDispatchMultiple(
		State* opaque,
		void* native,
		void* pipelineNative,
		Span<const NativeBindingSet> sets,
		uint32 groupsX,
		uint32 groupsY,
		uint32 groupsZ,
		void* indirectNative,
		uint64 indirectOffset,
		Span<const Byte> pushConstants,
		void* sampledTable,
		void* samplerTable,
		void* storageBufferTable,
		void* storageTextureTable,
		void* uniformBufferTable,
		void* readOnlyBufferNative,
		void* accelNative)
	{
		NativeComputeBuffer buffers[32]{};
		NativeComputeTexture textures[32]{};
		uint32 bufferCount = 0;
		uint32 textureCount = 0;

		for (const NativeBindingSet& set : sets)
		{
			for (const NativeComputeBuffer& buffer : set.Buffers)
			{
				ASSERT(bufferCount < 32, "Too many multi-layout compute buffers.");
				buffers[bufferCount++] = buffer;
			}

			for (const NativeComputeTexture& texture : set.Textures)
			{
				ASSERT(textureCount < 32, "Too many multi-layout compute textures.");
				textures[textureCount++] = texture;
			}
		}

		return RecordComputeDispatch(opaque, native, pipelineNative, { buffers, bufferCount }, { textures, textureCount }, groupsX, groupsY,
			groupsZ, indirectNative, indirectOffset, pushConstants, sampledTable, samplerTable, storageBufferTable,
			storageTextureTable, uniformBufferTable, readOnlyBufferNative, accelNative);
	}
}
#endif
