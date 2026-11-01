#include "D3D12Backend.h"
#include "D3D12Descriptors.h"

#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	namespace
	{
		Result<void> applyDepthBounds(Pending& pending, const GraphicsPipeline& pipeline)
		{
			if (!pipeline.DepthBounds.bEnable)
			{
				return Result<void>::Success();
			}
			if (!pending.ResolveList && FAILED(pending.List.As(&pending.ResolveList)))
			{
				return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.DepthBounds", "The command list does not support depth bounds testing.");
			}
			pending.ResolveList->OMSetDepthBounds(pipeline.DepthBounds.Minimum, pipeline.DepthBounds.Maximum);

			return Result<void>::Success();
		}
	} // namespace

	[[nodiscard]] Result<void> RecordSetViewport(void*, void* native, Span<const ViewportDesc> viewports)
	{
		Pending& pending = *static_cast<Pending*>(native);
		D3D12_VIEWPORT regions[16]{};
		for (usize index = 0; index < viewports.Size(); ++index)
		{
			const ViewportDesc& viewport = viewports[index];
			regions[index] = { viewport.X, viewport.Y, viewport.Width, viewport.Height, viewport.MinDepth, viewport.MaxDepth };
		}
		pending.List->RSSetViewports(static_cast<UINT>(viewports.Size()), regions);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordSetScissor(void*, void* native, Span<const ScissorDesc> scissors)
	{
		Pending& pending = *static_cast<Pending*>(native);
		D3D12_RECT regions[16]{};
		for (usize index = 0; index < scissors.Size(); ++index)
		{
			const ScissorDesc& scissor = scissors[index];
			regions[index] = { static_cast<LONG>(scissor.X), static_cast<LONG>(scissor.Y), static_cast<LONG>(scissor.X + scissor.Width),
							  static_cast<LONG>(scissor.Y + scissor.Height) };
		}
		pending.List->RSSetScissorRects(static_cast<UINT>(scissors.Size()), regions);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> BindFixedTextures(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		Span<const NativeSampledTexture> textures,
		Span<const NativeStorageTexture> storageTextures)
	{
		if (pipeline.Fixed.LayoutCount != 0)
		{
			return Result<void>::Success();
		}
		if (textures.IsEmpty() && storageTextures.IsEmpty())
		{
			return Result<void>::Success();
		}

		for (usize index = 0; index < textures.Size(); ++index)
		{
			bool bFirst = true;

			for (usize previous = 0; previous < index; ++previous)
			{
				if (textures[previous].Texture == textures[index].Texture && textures[previous].View == textures[index].View)
				{
					bFirst = false;
					break;
				}
			}

			if (!bFirst)
			{
				continue;
			}

			Texture& texture = *static_cast<Texture*>(textures[index].Texture);

			const TextureView* sampledView = static_cast<const TextureView*>(textures[index].View);
			const bool bStencil = sampledView != nullptr && sampledView->Range.Aspect == ETextureAspect::Stencil;
			if (&texture == (bStencil ? pending.ReadOnlyStencilTarget : pending.ReadOnlyDepthTarget))
			{
				continue;
			}
			TransitionTextureView(pending, texture, static_cast<const TextureView*>(textures[index].View),
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}

		for (usize index = 0; index < storageTextures.Size(); ++index)
		{
			bool bFirst = true;

			for (usize previous = 0; previous < index; ++previous)
			{
				if (storageTextures[previous].Texture == storageTextures[index].Texture)
				{
					bFirst = false;
					break;
				}
			}

			if (bFirst)
			{
				Texture& texture = *static_cast<Texture*>(storageTextures[index].Texture);
				TransitionTextureView(pending, texture, static_cast<const TextureView*>(storageTextures[index].View),
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			}
		}

		if (pipeline.bDeviceHeapIndexed)
		{
			bool bReuse = pending.LastTexturePipeline == &pipeline &&
				pending.LastTextureHeap == state.GlobalResourceHeap.Get() &&
				pending.LastSamplerHeap == state.GlobalSamplerHeap.Get() &&
				pending.LastTextureCount == textures.Size() &&
				pending.LastStorageTextureCount == storageTextures.Size();

			for (usize index = 0; bReuse && index < textures.Size(); ++index)
			{
				bReuse = pending.LastTextures[index] == textures[index].Texture && pending.LastViews[index] == textures[index].View && pending.LastSamplers[index] == textures[index].Sampler;
			}

			for (usize index = 0; bReuse && index < storageTextures.Size(); ++index)
			{
				bReuse = pending.LastStorageTextures[index] == storageTextures[index].Texture && pending.LastStorageViews[index] == storageTextures[index].View;
			}

			if (bReuse)
			{
				return Result<void>::Success();
			}

			Result<uint32> resourceRange = AllocatePendingGlobalDescriptors( state, pending, false, static_cast<uint32>(textures.Size() + storageTextures.Size()));

			if (!resourceRange)
			{
				return Result<void>::Failure(resourceRange.GetError());
			}

			uint32 samplerFirst = 0;

			if (!textures.IsEmpty())
			{
				Result<uint32> samplerRange = AllocatePendingGlobalDescriptors(state, pending, true, static_cast<uint32>(textures.Size()));

				if (!samplerRange)
				{
					return Result<void>::Failure(samplerRange.GetError());
				}
				samplerFirst = samplerRange.Value();
			}
			pending.LastTextureGpu = GlobalDescriptorGpu(state, false, resourceRange.Value());
			pending.LastSamplerGpu = textures.IsEmpty() ? D3D12_GPU_DESCRIPTOR_HANDLE{} : GlobalDescriptorGpu(state, true, samplerFirst);
			pending.LastTextureHeap = state.GlobalResourceHeap.Get();
			pending.LastSamplerHeap = state.GlobalSamplerHeap.Get();
			pending.LastTexturePipeline = &pipeline;
			pending.LastTextureCount = static_cast<uint32>(textures.Size());
			pending.LastStorageTextureCount = static_cast<uint32>(storageTextures.Size());
			const UINT resourceStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			D3D12_CPU_DESCRIPTOR_HANDLE resourceCpu = GlobalDescriptorCpu(state, false, resourceRange.Value());
			D3D12_CPU_DESCRIPTOR_HANDLE samplerCpu =
				textures.IsEmpty() ? D3D12_CPU_DESCRIPTOR_HANDLE{} : GlobalDescriptorCpu(state, true, samplerFirst);
			for (usize index = 0; index < textures.Size(); ++index)
			{
				pending.LastTextures[index] = textures[index].Texture;
				pending.LastViews[index] = textures[index].View;
				pending.LastSamplers[index] = textures[index].Sampler;
				const Texture& texture = *static_cast<const Texture*>(textures[index].Texture);
				CopyCommandDescriptors(state, pending, 1, resourceCpu, TextureDescriptorCpu(texture, textures[index].View, false),
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				D3D12_CPU_DESCRIPTOR_HANDLE source = pipeline.DefaultSamplers->GetCPUDescriptorHandleForHeapStart();
				source.ptr += static_cast<SIZE_T>(index) * samplerStride;

				if (textures[index].Sampler != nullptr)
				{
					source = static_cast<const Sampler*>(textures[index].Sampler)->Descriptor->GetCPUDescriptorHandleForHeapStart();
				}
				CopyCommandDescriptors(state, pending, 1, samplerCpu, source, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
				resourceCpu.ptr += resourceStride;
				samplerCpu.ptr += samplerStride;
			}

			for (usize index = 0; index < storageTextures.Size(); ++index)
			{
				pending.LastStorageTextures[index] = storageTextures[index].Texture;
				pending.LastStorageViews[index] = storageTextures[index].View;
				const Texture& texture = *static_cast<const Texture*>(storageTextures[index].Texture);
				CopyCommandDescriptors(state, pending, 1, resourceCpu, TextureDescriptorCpu(texture, storageTextures[index].View, true),
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				resourceCpu.ptr += resourceStride;
			}

			return Result<void>::Success();
		}
		bool bReuse = pending.LastTexturePipeline == &pipeline && pending.LastTextureCount == textures.Size() && pending.LastStorageTextureCount == storageTextures.Size();

		for (usize index = 0; bReuse && index < textures.Size(); ++index)
		{
			bReuse = pending.LastTextures[index] == textures[index].Texture && pending.LastSamplers[index] == textures[index].Sampler && pending.LastViews[index] == textures[index].View;
		}

		for (usize index = 0; bReuse && index < storageTextures.Size(); ++index)
		{
			bReuse = pending.LastStorageTextures[index] == storageTextures[index].Texture && pending.LastStorageViews[index] == storageTextures[index].View;
		}

		if (bReuse)
		{
			ID3D12DescriptorHeap* heaps[2]{ pending.LastTextureHeap, pending.LastSamplerHeap };
			if (pending.BoundTextureHeap != heaps[0] || pending.BoundSamplerHeap != heaps[1])
			{
				pending.List->SetDescriptorHeaps(2, heaps);
				pending.BoundTextureHeap = heaps[0];
				pending.BoundSamplerHeap = heaps[1];
			}

			const UINT textureStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			D3D12_GPU_DESCRIPTOR_HANDLE textureGpu = pending.LastTextureGpu;
			D3D12_GPU_DESCRIPTOR_HANDLE samplerGpu = pending.LastSamplerGpu;

			for (usize index = 0; index < textures.Size(); ++index)
			{
				pending.List->SetGraphicsRootDescriptorTable((pipeline.PushConstantBytes != 0 ? 1u : 0u) + static_cast<UINT>(index),
					textureGpu);
				pending.List->SetGraphicsRootDescriptorTable(
					(pipeline.PushConstantBytes != 0 ? 1u : 0u) + pipeline.SampledTextureCount + static_cast<UINT>(index), samplerGpu);
				textureGpu.ptr += textureStride;
				samplerGpu.ptr += samplerStride;
			}

			for (usize index = 0; index < storageTextures.Size(); ++index)
			{
				pending.List->SetGraphicsRootDescriptorTable(pipeline.StorageTextureRootIndex + static_cast<UINT>(index), textureGpu);
				textureGpu.ptr += textureStride;
			}

			return Result<void>::Success();
		}
		constexpr UINT DESCRIPTORS_PER_HEAP = 256;

		if (pending.TextureHeaps.IsEmpty() || pending.TextureHeapUsed + textures.Size() + storageTextures.Size() > DESCRIPTORS_PER_HEAP)
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc{};
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			desc.NumDescriptors = DESCRIPTORS_PER_HEAP;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			ComPtr<ID3D12DescriptorHeap> heap;

			if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap.GetAddressOf()))))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12Descriptors", "Could not allocate sampled texture descriptors."));
			}

			if (!pending.TextureHeaps.Add(Move(heap)))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain sampled texture descriptors."));
			}
			pending.TextureHeapUsed = 0;
		}

		if (pending.SamplerHeaps.IsEmpty() || pending.SamplerHeapUsed + textures.Size() > DESCRIPTORS_PER_HEAP)
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc{};
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			desc.NumDescriptors = DESCRIPTORS_PER_HEAP;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			ComPtr<ID3D12DescriptorHeap> heap;

			if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap.GetAddressOf()))))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12Descriptors", "Could not allocate sampler descriptors."));
			}

			if (!pending.SamplerHeaps.Add(Move(heap)))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain sampler descriptors."));
			}
			pending.SamplerHeapUsed = 0;
		}
		ID3D12DescriptorHeap* heaps[2]{ pending.TextureHeaps.Back().Get(), pending.SamplerHeaps.Back().Get() };
		const UINT textureStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		D3D12_CPU_DESCRIPTOR_HANDLE textureCpu = heaps[0]->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE samplerCpu = heaps[1]->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE textureGpu = heaps[0]->GetGPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE samplerGpu = heaps[1]->GetGPUDescriptorHandleForHeapStart();
		textureCpu.ptr += static_cast<SIZE_T>(pending.TextureHeapUsed) * textureStride;
		textureGpu.ptr += static_cast<UINT64>(pending.TextureHeapUsed) * textureStride;
		samplerCpu.ptr += static_cast<SIZE_T>(pending.SamplerHeapUsed) * samplerStride;
		samplerGpu.ptr += static_cast<UINT64>(pending.SamplerHeapUsed) * samplerStride;
		pending.LastTextureGpu = textureGpu;
		pending.LastSamplerGpu = samplerGpu;
		pending.LastTextureHeap = heaps[0];
		pending.LastSamplerHeap = heaps[1];
		pending.LastTexturePipeline = &pipeline;
		pending.LastTextureCount = static_cast<uint32>(textures.Size());
		pending.LastStorageTextureCount = static_cast<uint32>(storageTextures.Size());

		for (usize index = 0; index < textures.Size(); ++index)
		{
			pending.LastTextures[index] = textures[index].Texture;
			pending.LastSamplers[index] = textures[index].Sampler;
			pending.LastViews[index] = textures[index].View;
			const Texture& texture = *static_cast<const Texture*>(textures[index].Texture);
			CopyCommandDescriptors(state, pending, 1, textureCpu, TextureDescriptorCpu(texture, textures[index].View, false),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			D3D12_CPU_DESCRIPTOR_HANDLE sourceSampler = pipeline.DefaultSamplers->GetCPUDescriptorHandleForHeapStart();
			sourceSampler.ptr += index * samplerStride;

			if (textures[index].Sampler != nullptr)
			{
				sourceSampler = static_cast<const Sampler*>(textures[index].Sampler)->Descriptor->GetCPUDescriptorHandleForHeapStart();
			}
			CopyCommandDescriptors(state, pending, 1, samplerCpu, sourceSampler, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			textureCpu.ptr += textureStride;
			samplerCpu.ptr += samplerStride;
		}

		for (usize index = 0; index < storageTextures.Size(); ++index)
		{
			pending.LastStorageTextures[index] = storageTextures[index].Texture;
			pending.LastStorageViews[index] = storageTextures[index].View;
			const Texture& texture = *static_cast<const Texture*>(storageTextures[index].Texture);
			CopyCommandDescriptors(state, pending, 1, textureCpu, TextureDescriptorCpu(texture, storageTextures[index].View, true),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			textureCpu.ptr += textureStride;
		}
		pending.TextureHeapUsed += static_cast<uint32>(textures.Size() + storageTextures.Size());
		pending.SamplerHeapUsed += static_cast<uint32>(textures.Size());

		if (pending.BoundTextureHeap != heaps[0] || pending.BoundSamplerHeap != heaps[1])
		{
			pending.List->SetDescriptorHeaps(2, heaps);
			pending.BoundTextureHeap = heaps[0];
			pending.BoundSamplerHeap = heaps[1];
		}

		for (usize index = 0; index < textures.Size(); ++index)
		{
			pending.List->SetGraphicsRootDescriptorTable((pipeline.PushConstantBytes != 0 ? 1u : 0u) + static_cast<UINT>(index),
				textureGpu);
			pending.List->SetGraphicsRootDescriptorTable(
				(pipeline.PushConstantBytes != 0 ? 1u : 0u) + pipeline.SampledTextureCount + static_cast<UINT>(index), samplerGpu);
			textureGpu.ptr += textureStride;
			samplerGpu.ptr += samplerStride;
		}

		for (usize index = 0; index < storageTextures.Size(); ++index)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.StorageTextureRootIndex + static_cast<UINT>(index), textureGpu);
			textureGpu.ptr += textureStride;
		}

		return Result<void>::Success();
	}

	void RestoreFixedStorageTextures(Pending& pending, Span<const NativeStorageTexture> storageTextures) noexcept
	{
		for (usize index = 0; index < storageTextures.Size(); ++index)
		{
			bool bFirst = true;

			for (usize previous = 0; previous < index; ++previous)
			{
				if (storageTextures[previous].Texture == storageTextures[index].Texture)
				{
					bFirst = false;
					break;
				}
			}

			if (bFirst)
			{
				Texture& texture = *static_cast<Texture*>(storageTextures[index].Texture);
				Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
			}
		}
	}

	[[nodiscard]] Result<void> BindCombinedGraphicsDescriptors(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		const DescriptorTable* sampled,
		const DescriptorTable* samplers,
		const DescriptorTable* storage,
		const DescriptorTable* storageTextures,
		const DescriptorTable* uniform,
		const DescriptorTable* readOnly,
		Span<const NativeGraphicsBuffer> buffers)
	{
		const DescriptorTable* const tables[]{ sampled, storage, storageTextures, uniform, readOnly };
		const uint32 fixedCount = pipeline.SampledTextureCount + pipeline.StorageTextureCount;
		const UINT resourceStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		const uint32 typedCount = CountTypedBuffers(buffers);
		bool bReuse = pending.LastCombinedPipeline == &pipeline &&
			pending.LastCombinedSampledTable == sampled &&
			pending.LastCombinedSamplerTable == samplers &&
			pending.LastCombinedStorageTable == storage &&
			pending.LastCombinedStorageTextureTable == storageTextures &&
			pending.LastCombinedUniformTable == uniform &&
			pending.LastCombinedReadOnlyTable == readOnly &&
			pending.LastCombinedTextureGpu == pending.LastTextureGpu.ptr &&
			pending.LastCombinedSamplerGpu == pending.LastSamplerGpu.ptr;

		if (typedCount != 0)
		{
			bReuse = bReuse && pending.LastTypedGraphicsCount == buffers.Size();

			for (uint32 index = 0; bReuse && index < buffers.Size(); ++index)
			{
				bReuse = pending.LastTypedGraphicsViews[index] == buffers[index].View;
			}
		}
		ID3D12DescriptorHeap* resourceHeap = bReuse ? pending.LastCombinedResourceHeap : nullptr;
		ID3D12DescriptorHeap* samplerHeap = bReuse ? pending.LastCombinedSamplerHeap : nullptr;

		if (!bReuse)
		{
			uint32 count = fixedCount;

			for (const DescriptorTable* const table : tables)
			{
				if (table != nullptr)
				{
					count += table->Capacity;
				}
			}
			D3D12_DESCRIPTOR_HEAP_DESC desc{};
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			desc.NumDescriptors = count + typedCount;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			ComPtr<ID3D12DescriptorHeap> combined;

			if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(combined.GetAddressOf()))))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12Descriptors", "Could not combine fixed and bindless graphics descriptors."));
			}
			D3D12_CPU_DESCRIPTOR_HANDLE destination = combined->GetCPUDescriptorHandleForHeapStart();
			D3D12_CPU_DESCRIPTOR_HANDLE source{};
			if (fixedCount != 0)
			{
				source = pending.LastTextureHeap->GetCPUDescriptorHandleForHeapStart();
				source.ptr += static_cast<SIZE_T>(pending.LastTextureGpu.ptr - pending.LastTextureHeap->GetGPUDescriptorHandleForHeapStart().ptr);
			}

			for (uint32 index = 0; index < fixedCount; ++index)
			{
				CopyCommandDescriptors(state, pending, 1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				destination.ptr += resourceStride;
				source.ptr += resourceStride;
			}

			for (const DescriptorTable* const table : tables)
			{
				if (table == nullptr)
				{
					continue;
				}
				source = table->CpuHeap->GetCPUDescriptorHandleForHeapStart();

				for (uint32 index = 0; index < table->Capacity; ++index)
				{
					const bool bValid = table->Type == EDescriptorType::SampledTexture || table->Type == EDescriptorType::StorageTexture
						? table->Textures[index] != nullptr
						: table->Buffers[index] != nullptr;

					if (bValid)
					{
						CopyCommandDescriptors(state, pending, 1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					}
					destination.ptr += resourceStride;
					source.ptr += resourceStride;
				}
			}
			CopyTypedBufferDescriptors(state, pending, buffers, destination);

			if (!pending.BindlessHeaps.Add(Move(combined)))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain combined graphics descriptors."));
			}
			resourceHeap = pending.BindlessHeaps.Back().Get();
			samplerHeap = pipeline.SampledTextureCount != 0 ? pending.LastSamplerHeap : nullptr;

			if (samplers != nullptr)
			{
				if (pipeline.SampledTextureCount == 0)
				{
					samplerHeap = samplers->Heap.Get();
				}
				else
				{
					D3D12_DESCRIPTOR_HEAP_DESC samplerDesc{};
					samplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
					samplerDesc.NumDescriptors = pipeline.SampledTextureCount + samplers->Capacity;
					samplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
					ComPtr<ID3D12DescriptorHeap> combinedSamplers;

					if (FAILED(state.Device->CreateDescriptorHeap(&samplerDesc, IID_PPV_ARGS(combinedSamplers.GetAddressOf()))))
					{
						return Result<void>::Failure(Failure("Rhi.D3D12Descriptors", "Could not combine fixed and bindless graphics samplers."));
					}
					D3D12_CPU_DESCRIPTOR_HANDLE samplerDestination = combinedSamplers->GetCPUDescriptorHandleForHeapStart();
					D3D12_CPU_DESCRIPTOR_HANDLE samplerSource = pending.LastSamplerHeap->GetCPUDescriptorHandleForHeapStart();
					samplerSource.ptr += static_cast<SIZE_T>(pending.LastSamplerGpu.ptr - pending.LastSamplerHeap->GetGPUDescriptorHandleForHeapStart().ptr);

					for (uint32 index = 0; index < pipeline.SampledTextureCount; ++index)
					{
						CopyCommandDescriptors(state, pending, 1, samplerDestination, samplerSource, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
						samplerDestination.ptr += samplerStride;
						samplerSource.ptr += samplerStride;
					}
					samplerSource = samplers->CpuHeap->GetCPUDescriptorHandleForHeapStart();

					for (uint32 index = 0; index < samplers->Capacity; ++index)
					{
						if (samplers->SamplerValid[index] != 0)
						{
							CopyCommandDescriptors(state, pending, 1, samplerDestination, samplerSource,
								D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
						}
						samplerDestination.ptr += samplerStride;
						samplerSource.ptr += samplerStride;
					}

					if (!pending.BindlessHeaps.Add(Move(combinedSamplers)))
					{
						return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain combined graphics samplers."));
					}
					samplerHeap = pending.BindlessHeaps.Back().Get();
				}
			}

			if (samplers != nullptr && samplerHeap == samplers->Heap.Get() && !pending.BindlessHeaps.Add(samplerHeap))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain graphics sampler descriptors."));
			}
			pending.LastTypedGraphicsCount = static_cast<uint32>(buffers.Size());

			for (uint32 index = 0; index < buffers.Size(); ++index)
			{
				pending.LastTypedGraphicsViews[index] = buffers[index].View;
			}
			pending.LastCombinedPipeline = &pipeline;
			pending.LastCombinedSampledTable = sampled;
			pending.LastCombinedSamplerTable = samplers;
			pending.LastCombinedStorageTable = storage;
			pending.LastCombinedStorageTextureTable = storageTextures;
			pending.LastCombinedUniformTable = uniform;
			pending.LastCombinedReadOnlyTable = readOnly;
			pending.LastCombinedTextureGpu = pending.LastTextureGpu.ptr;
			pending.LastCombinedSamplerGpu = pending.LastSamplerGpu.ptr;
			pending.LastCombinedResourceHeap = resourceHeap;
			pending.LastCombinedSamplerHeap = samplerHeap;
		}
		ID3D12DescriptorHeap* heaps[2]{ resourceHeap, samplerHeap };
		if (pending.BoundTextureHeap != resourceHeap || pending.BoundSamplerHeap != samplerHeap)
		{
			pending.List->SetDescriptorHeaps(samplerHeap != nullptr ? 2u : 1u, heaps);
			pending.BoundTextureHeap = resourceHeap;
			pending.BoundSamplerHeap = samplerHeap;
		}
		D3D12_GPU_DESCRIPTOR_HANDLE resourceAddress = resourceHeap->GetGPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE samplerAddress =
			samplerHeap != nullptr ? samplerHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
		for (uint32 index = 0; index < pipeline.SampledTextureCount; ++index)
		{
			pending.List->SetGraphicsRootDescriptorTable((pipeline.PushConstantBytes != 0 ? 1u : 0u) + index, resourceAddress);
			pending.List->SetGraphicsRootDescriptorTable((pipeline.PushConstantBytes != 0 ? 1u : 0u) + pipeline.SampledTextureCount + index,
				samplerAddress);
			resourceAddress.ptr += resourceStride;
			samplerAddress.ptr += samplerStride;
		}

		for (uint32 index = 0; index < pipeline.StorageTextureCount; ++index)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.StorageTextureRootIndex + index, resourceAddress);
			resourceAddress.ptr += resourceStride;
		}

		if (sampled != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessRootIndex, resourceAddress);
			resourceAddress.ptr += static_cast<UINT64>(sampled->Capacity) * resourceStride;
		}

		if (storage != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessStorageBufferRootIndex, resourceAddress);
			resourceAddress.ptr += static_cast<UINT64>(storage->Capacity) * resourceStride;
		}

		if (storageTextures != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessStorageTextureRootIndex, resourceAddress);
			resourceAddress.ptr += static_cast<UINT64>(storageTextures->Capacity) * resourceStride;
		}

		if (uniform != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessUniformBufferRootIndex, resourceAddress);
			resourceAddress.ptr += static_cast<UINT64>(uniform->Capacity) * resourceStride;
		}

		if (readOnly != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessReadOnlyBufferRootIndex, resourceAddress);
			resourceAddress.ptr += static_cast<UINT64>(readOnly->Capacity) * resourceStride;
		}
		pending.TypedBufferGpu = resourceAddress;

		if (samplers != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessSamplerRootIndex, samplerAddress);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> BindGraphicsDescriptorTable(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		void* native,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		Span<const NativeGraphicsBuffer> buffers)
	{
		if (pipeline.Fixed.LayoutCount != 0)
		{
			return Result<void>::Success();
		}
		if (native != nullptr)
		{
			DescriptorTable& table = *static_cast<DescriptorTable*>(native);

			for (usize index = 0; index < table.Textures.Size(); ++index)
			{
				Texture* const texture = table.Textures[index];

				const d3d12::TextureView* sampledView = table.TextureViews[index];
				const bool bStencil = sampledView != nullptr && sampledView->Range.Aspect == ETextureAspect::Stencil;
				if (texture == nullptr || texture == (bStencil ? pending.ReadOnlyStencilTarget : pending.ReadOnlyDepthTarget))
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

				if (bFirst)
				{
					TransitionTextureView(pending, *texture, table.TextureViews[index],
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				}
			}
		}

		if (uniformNative != nullptr)
		{
			DescriptorTable& table = *static_cast<DescriptorTable*>(uniformNative);

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
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				}
			}
		}

		if (storageNative != nullptr)
		{
			DescriptorTable& table = *static_cast<DescriptorTable*>(storageNative);

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

				if (bFirst)
				{
					TransitionTextureView(pending, *texture, table.TextureViews[index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				}
			}
		}
		ID3D12DescriptorHeap* resourceHeap = nullptr;
		const DescriptorTable* const sampled = static_cast<const DescriptorTable*>(native);
		const DescriptorTable* const storage = static_cast<const DescriptorTable*>(storageNative);
		const DescriptorTable* const storageTextures = static_cast<const DescriptorTable*>(storageTextureNative);
		const DescriptorTable* const uniform = static_cast<const DescriptorTable*>(uniformNative);
		const DescriptorTable* const readOnly = static_cast<const DescriptorTable*>(readOnlyBufferNative);
		const DescriptorTable* const tables[]{ sampled, storage, storageTextures, uniform, readOnly };
		const DescriptorTable* const samplers = static_cast<const DescriptorTable*>(samplerNative);

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
			if (pipeline.Fixed.LayoutCount == 0)
			{
				pending.List->SetGraphicsRootSignature(pipeline.RootSignature.Get());
			}
			const UINT resourceStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			auto bindResource = [&](const DescriptorTable* table, uint32 root)
				{
					if (table == nullptr)
					{
						return;
					}
					ASSERT(table->FirstGlobalDescriptor != UINT32_MAX, "A device-indexed graphics table must have a global descriptor range.");
					D3D12_GPU_DESCRIPTOR_HANDLE address = state.GlobalResourceHeap->GetGPUDescriptorHandleForHeapStart();
					address.ptr += static_cast<UINT64>(table->FirstGlobalDescriptor) * resourceStride;
					pending.List->SetGraphicsRootDescriptorTable(root, address);
				};
			bindResource(sampled, pipeline.BindlessRootIndex);
			bindResource(storage, pipeline.BindlessStorageBufferRootIndex);
			bindResource(storageTextures, pipeline.BindlessStorageTextureRootIndex);
			bindResource(uniform, pipeline.BindlessUniformBufferRootIndex);
			bindResource(readOnly, pipeline.BindlessReadOnlyBufferRootIndex);

			if (samplers != nullptr)
			{
				ASSERT(samplers->FirstGlobalDescriptor != UINT32_MAX, "A device-indexed graphics sampler table must have a global descriptor range.");
				D3D12_GPU_DESCRIPTOR_HANDLE address = state.GlobalSamplerHeap->GetGPUDescriptorHandleForHeapStart();
				address.ptr += static_cast<UINT64>(samplers->FirstGlobalDescriptor) * samplerStride;
				pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessSamplerRootIndex, address);
			}

			return Result<void>::Success();
		}

		if (readOnly != nullptr ||
			CountTypedBuffers(buffers) != 0 ||
			(pipeline.SampledTextureCount + pipeline.StorageTextureCount != 0 &&
			 (native != nullptr || samplerNative != nullptr || storageNative != nullptr || storageTextureNative != nullptr || uniformNative != nullptr)))
		{
			return BindCombinedGraphicsDescriptors(state, pending, pipeline, sampled, samplers, storage, storageTextures, uniform, readOnly,
				buffers);
		}

		uint32 resourceCount = 0;
		uint32 descriptorCount = 0;

		for (const DescriptorTable* const table : tables)
		{
			if (table == nullptr)
			{
				continue;
			}
			++resourceCount;
			descriptorCount += table->Capacity;
		}

		if (resourceCount > 1)
		{
			if (pending.LastMergedSampledTable == sampled &&
				pending.LastMergedStorageTable == storage &&
				pending.LastMergedStorageTextureTable == storageTextures &&
				pending.LastMergedUniformTable == uniform)
			{
				resourceHeap = pending.LastMergedResourceHeap;
			}
			else
			{
				D3D12_DESCRIPTOR_HEAP_DESC desc{};
				desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				desc.NumDescriptors = descriptorCount;
				desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				ComPtr<ID3D12DescriptorHeap> combined;

				if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(combined.GetAddressOf()))))
				{
					return Result<void>::Failure(Failure("Rhi.D3D12Descriptors", "Could not combine graphics descriptor tables."));
				}

				const UINT stride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				D3D12_CPU_DESCRIPTOR_HANDLE destination = combined->GetCPUDescriptorHandleForHeapStart();

				for (const DescriptorTable* const table : tables)
				{
					if (table == nullptr)
					{
						continue;
					}
					D3D12_CPU_DESCRIPTOR_HANDLE source = table->CpuHeap->GetCPUDescriptorHandleForHeapStart();

					for (uint32 index = 0; index < table->Capacity; ++index)
					{
						const bool bValid = table->Type == EDescriptorType::SampledTexture || table->Type == EDescriptorType::StorageTexture
							? table->Textures[index] != nullptr
							: table->Buffers[index] != nullptr;

						if (bValid)
						{
							CopyCommandDescriptors(state, pending, 1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						}
						destination.ptr += stride;
						source.ptr += stride;
					}
				}

				if (!pending.BindlessHeaps.Add(Move(combined)))
				{
					return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain combined graphics descriptors."));
				}
				resourceHeap = pending.BindlessHeaps.Back().Get();
				pending.LastMergedSampledTable = sampled;
				pending.LastMergedStorageTable = storage;
				pending.LastMergedStorageTextureTable = storageTextures;
				pending.LastMergedUniformTable = uniform;
				pending.LastMergedResourceHeap = resourceHeap;
			}
		}
		else if (resourceCount == 1)
		{
			resourceHeap = (sampled != nullptr ? sampled : storage != nullptr ? storage : storageTextures != nullptr ? storageTextures : uniform) ->Heap.Get();
		}
		ID3D12DescriptorHeap* heaps[2]{ resourceHeap,
									   samplerNative != nullptr ? static_cast<DescriptorTable*>(samplerNative)->Heap.Get() : nullptr };
		if (heaps[0] == nullptr && heaps[1] != nullptr)
		{
			Result<void> ensured = EnsureEmptyTextureHeap(state, pending);

			if (!ensured)
			{
				return ensured;
			}
			heaps[0] = pending.EmptyTextureHeap.Get();
		}

		for (ID3D12DescriptorHeap* heap : heaps)
		{
			if (heap == nullptr)
			{
				continue;
			}
			bool bRetained = false;

			for (Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> const& held : pending.BindlessHeaps)
			{
				if (held.Get() == heap)
				{
					bRetained = true;
					break;
				}
			}

			if (!bRetained && !pending.BindlessHeaps.Add(heap))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain graphics descriptor table heap."));
			}
		}

		if (heaps[0] != nullptr || heaps[1] != nullptr)
		{
			const UINT count = heaps[0] != nullptr && heaps[1] != nullptr ? 2u : 1u;
			ID3D12DescriptorHeap* const* const bound = heaps[0] != nullptr ? heaps : &heaps[1];

			if (pending.BoundTextureHeap != heaps[0] || pending.BoundSamplerHeap != heaps[1])
			{
				pending.List->SetDescriptorHeaps(count, bound);
				pending.BoundTextureHeap = heaps[0];
				pending.BoundSamplerHeap = heaps[1];
			}

			if (native != nullptr)
			{
				pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessRootIndex, heaps[0]->GetGPUDescriptorHandleForHeapStart());
			}

			if (storageNative != nullptr)
			{
				D3D12_GPU_DESCRIPTOR_HANDLE address = heaps[0]->GetGPUDescriptorHandleForHeapStart();

				if (sampled != nullptr)
				{
					address.ptr += static_cast<UINT64>(sampled->Capacity) * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				}
				pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessStorageBufferRootIndex, address);
			}

			if (storageTextureNative != nullptr)
			{
				D3D12_GPU_DESCRIPTOR_HANDLE address = heaps[0]->GetGPUDescriptorHandleForHeapStart();
				const uint32 preceding = (sampled != nullptr ? sampled->Capacity : 0) + (storage != nullptr ? storage->Capacity : 0);
				address.ptr += static_cast<UINT64>(preceding) * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessStorageTextureRootIndex, address);
			}

			if (uniformNative != nullptr)
			{
				D3D12_GPU_DESCRIPTOR_HANDLE address = heaps[0]->GetGPUDescriptorHandleForHeapStart();
				const uint32 preceding = (sampled != nullptr ? sampled->Capacity : 0) + (storage != nullptr ? storage->Capacity : 0) +
					(storageTextures != nullptr ? storageTextures->Capacity : 0);
				address.ptr += static_cast<UINT64>(preceding) * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessUniformBufferRootIndex, address);
			}

			if (heaps[1] != nullptr)
			{
				pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessSamplerRootIndex,
					heaps[1]->GetGPUDescriptorHandleForHeapStart());
			}
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RefreshDirectGraphicsRoot(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		void* sampled,
		void* samplers,
		void* storage,
		void* storageTextures,
		void* uniform,
		void* readOnlyBufferNative)
	{
		if (!pipeline.bHeapDirectlyIndexed || pipeline.Fixed.LayoutCount != 0)
		{
			return Result<void>::Success();
		}

		if (pipeline.bDeviceHeapIndexed)
		{
			D3D12_GPU_DESCRIPTOR_HANDLE resource = pending.LastTextureGpu;
			D3D12_GPU_DESCRIPTOR_HANDLE sampler = pending.LastSamplerGpu;
			const UINT resourceStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			const UINT root = pipeline.PushConstantBytes != 0 ? 1u : 0u;

			for (UINT index = 0; index < pipeline.SampledTextureCount; ++index)
			{
				pending.List->SetGraphicsRootDescriptorTable(root + index, resource);
				pending.List->SetGraphicsRootDescriptorTable(root + pipeline.SampledTextureCount + index, sampler);
				resource.ptr += resourceStride;
				sampler.ptr += samplerStride;
			}

			for (UINT index = 0; index < pipeline.StorageTextureCount; ++index)
			{
				pending.List->SetGraphicsRootDescriptorTable(pipeline.StorageTextureRootIndex + index, resource);
				resource.ptr += resourceStride;
			}

			return Result<void>::Success();
		}
		if (pipeline.Fixed.LayoutCount == 0)
		{
			pending.List->SetGraphicsRootSignature(pipeline.RootSignature.Get());
		}
		const UINT resourceStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const UINT samplerStride = state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		ASSERT(pending.BoundTextureHeap != nullptr, "Direct graphics indexing requires a bound resource heap.");
		D3D12_GPU_DESCRIPTOR_HANDLE resource = pending.BoundTextureHeap->GetGPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE sampler{};
		if (pending.BoundSamplerHeap != nullptr)
		{
			sampler = pending.BoundSamplerHeap->GetGPUDescriptorHandleForHeapStart();
		}

		const UINT fixedRoot = pipeline.PushConstantBytes != 0 ? 1u : 0u;

		for (UINT index = 0; index < pipeline.SampledTextureCount; ++index)
		{
			pending.List->SetGraphicsRootDescriptorTable(fixedRoot + index, resource);
			pending.List->SetGraphicsRootDescriptorTable(fixedRoot + pipeline.SampledTextureCount + index, sampler);
			resource.ptr += resourceStride;
			sampler.ptr += samplerStride;
		}

		for (UINT index = 0; index < pipeline.StorageTextureCount; ++index)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.StorageTextureRootIndex + index, resource);
			resource.ptr += resourceStride;
		}

		if (sampled != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessRootIndex, resource);
			resource.ptr += static_cast<UINT64>(static_cast<DescriptorTable*>(sampled)->Capacity) * resourceStride;
		}

		if (storage != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessStorageBufferRootIndex, resource);
			resource.ptr += static_cast<UINT64>(static_cast<DescriptorTable*>(storage)->Capacity) * resourceStride;
		}

		if (storageTextures != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessStorageTextureRootIndex, resource);
			resource.ptr += static_cast<UINT64>(static_cast<DescriptorTable*>(storageTextures)->Capacity) * resourceStride;
		}

		if (uniform != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessUniformBufferRootIndex, resource);
			resource.ptr += static_cast<UINT64>(static_cast<DescriptorTable*>(uniform)->Capacity) * resourceStride;
		}

		if (readOnlyBufferNative != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessReadOnlyBufferRootIndex, resource);
		}

		if (samplers != nullptr)
		{
			pending.List->SetGraphicsRootDescriptorTable(pipeline.BindlessSamplerRootIndex, sampler);
		}

		return Result<void>::Success();
	}

	void RestoreGraphicsDescriptorTable(Pending& pending, void* native, void* storageNative, void* storageTextureNative, void* uniformNative, void* readOnlyBufferNative) noexcept
	{
		if (native != nullptr)
		{
			DescriptorTable& table = *static_cast<DescriptorTable*>(native);

			for (usize index = 0; index < table.Textures.Size(); ++index)
			{
				Texture* const texture = table.Textures[index];

				const d3d12::TextureView* sampledView = table.TextureViews[index];
				const bool bStencil = sampledView != nullptr && sampledView->Range.Aspect == ETextureAspect::Stencil;
				if (texture == nullptr || texture == (bStencil ? pending.ReadOnlyStencilTarget : pending.ReadOnlyDepthTarget))
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
					Transition(pending, *texture->Resource.Get(),
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_COMMON);
				}
			}
		}

		if (uniformNative != nullptr)
		{
			DescriptorTable& table = *static_cast<DescriptorTable*>(uniformNative);

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
					Transition(pending, *buffer->Resource.Get(),
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_COMMON);
				}
			}
		}

		if (storageNative != nullptr)
		{
			DescriptorTable& table = *static_cast<DescriptorTable*>(storageNative);

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
	}

	void BindGraphicsBuffers(Pending& pending, const GraphicsPipeline& pipeline, Span<const NativeGraphicsBuffer> buffers) noexcept
	{
		if (pipeline.Fixed.LayoutCount != 0)
		{
			return;
		}
		const UINT root = (pipeline.PushConstantBytes != 0 ? 1u : 0u) + 2 * pipeline.SampledTextureCount;

		for (usize index = 0; index < buffers.Size(); ++index)
		{
			Buffer& buffer = *static_cast<Buffer*>(buffers[index].Buffer);
			const bool bUniform = buffers[index].Type == EGraphicsBufferBindingType::UniformBuffer;
			const bool bReadOnly = buffers[index].Type == EGraphicsBufferBindingType::ReadOnlyBuffer;
			const D3D12_RESOURCE_STATES state =
				bUniform ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
				: bReadOnly ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
				: D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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
				Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, state);
			}

			if (buffers[index].View != nullptr)
			{
				continue;
			}

			if (bUniform)
			{
				pending.List->SetGraphicsRootConstantBufferView(static_cast<UINT>(root + index),
					buffer.Resource->GetGPUVirtualAddress() + buffers[index].ByteOffset);
			}
			else if (bReadOnly)
			{
				pending.List->SetGraphicsRootShaderResourceView(static_cast<UINT>(root + index),
					buffer.Resource->GetGPUVirtualAddress() + buffers[index].ByteOffset);
			}
			else
			{
				pending.List->SetGraphicsRootUnorderedAccessView(static_cast<UINT>(root + index),
					buffer.Resource->GetGPUVirtualAddress() + buffers[index].ByteOffset);
			}
		}
		BindTypedBufferRoots(pending, buffers, root, true);
	}

	void RestoreGraphicsBuffers(Pending& pending, Span<const NativeGraphicsBuffer> buffers) noexcept
	{
		for (usize index = 0; index < buffers.Size(); ++index)
		{
			const NativeGraphicsBuffer& binding = buffers[index];
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
			const D3D12_RESOURCE_STATES state =
				binding.Type == EGraphicsBufferBindingType::UniformBuffer ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
				: binding.Type == EGraphicsBufferBindingType::ReadOnlyBuffer
				? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
				: D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			Transition(pending, *buffer.Resource.Get(), state, D3D12_RESOURCE_STATE_COMMON);
		}
	}

	[[nodiscard]] Result<void> RecordBindExtraVertexBuffers(void*, void* native, Span<const NativeVertexBuffer> buffers)
	{
		Pending& pending = *static_cast<Pending*>(native);
		D3D12_VERTEX_BUFFER_VIEW views[3]{};
		for (usize index = 0; index < buffers.Size(); ++index)
		{
			const Buffer& buffer = *static_cast<Buffer*>(buffers[index].Buffer);
			Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			views[index] = { buffer.Resource->GetGPUVirtualAddress() + buffers[index].Offset,
							static_cast<UINT>(buffers[index].ByteCount - buffers[index].Offset), buffers[index].Stride };
		}

		if (!buffers.IsEmpty())
		{
			pending.List->IASetVertexBuffers(1, static_cast<UINT>(buffers.Size()), views);
		}

		return Result<void>::Success();
	}

	void ApplyBlendConstant(Pending& pending, const GraphicsPipeline& pipeline, const BlendColor& color) noexcept
	{
		if (!pipeline.bBlendConstant)
		{
			return;
		}

		const float value[4]{ color.R, color.G, color.B, color.A };
		pending.List->OMSetBlendFactor(value);
	}

	[[nodiscard]] uint32 QuerySupportedShadingRates(State& state) noexcept
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS6 options{};
		if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options, sizeof(options))) || options.VariableShadingRateTier == D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED)
		{
			return 0;
		}
		state.SupportedShadingRates[0] = 0x0fu;
		state.SupportedShadingRates[1] = 0x0fu;
		state.SupportedShadingRates[2] = 0x0fu;
		state.SupportedShadingRates[3] = 0x01u;

		if (options.AdditionalShadingRatesSupported)
		{
			state.SupportedShadingRates[0] |= 0x70u;
			state.SupportedShadingRates[1] |= 0x10u;
		}

		return options.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2 ? options.ShadingRateImageTileSize : 0;
	}

	void ApplyShadingRate(Pending& pending, EShadingRate rate, EShadingRateCombiner primitive, EShadingRateCombiner image) noexcept
	{
		if (!pending.ShadingRateList || (pending.bShadingRateSet && pending.CurrentShadingRate == rate && pending.CurrentPrimitiveCombiner == primitive && pending.CurrentImageCombiner == image))
		{
			return;
		}
		constexpr D3D12_SHADING_RATE RATES[7]{ D3D12_SHADING_RATE_1X1, D3D12_SHADING_RATE_1X2, D3D12_SHADING_RATE_2X1,
											  D3D12_SHADING_RATE_2X2, D3D12_SHADING_RATE_2X4, D3D12_SHADING_RATE_4X2,
											  D3D12_SHADING_RATE_4X4 };
		constexpr D3D12_SHADING_RATE_COMBINER COMBINERS[5]{ D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_OVERRIDE,
														   D3D12_SHADING_RATE_COMBINER_MIN, D3D12_SHADING_RATE_COMBINER_MAX,
														   D3D12_SHADING_RATE_COMBINER_SUM };
		const D3D12_SHADING_RATE_COMBINER selected[2]{ COMBINERS[static_cast<uint32>(primitive)],
													  COMBINERS[static_cast<uint32>(image)] };
		pending.ShadingRateList->RSSetShadingRate(RATES[static_cast<uint32>(rate)], selected);
		pending.CurrentShadingRate = rate;
		pending.CurrentPrimitiveCombiner = primitive;
		pending.CurrentImageCombiner = image;
		pending.bShadingRateSet = true;
	}

	[[nodiscard]] Result<void> RecordDraw(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* vertexNative,
		uint64 vertexOffset,
		uint64 vertexBytes,
		uint32 stride,
		Span<const NativeSampledTexture> textures,
		Span<const NativeStorageTexture> storageTextures,
		Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsState& drawState,
		const DrawArguments& draw)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(pipelineNative);
		if (pipeline.Fixed.LayoutCount == 0)
		{
			pending.List->SetGraphicsRootSignature(pipeline.RootSignature.Get());
		}
		pending.List->SetPipelineState(pipeline.State.Get());
		Result<void> bounds = applyDepthBounds(pending, pipeline);
		if (!bounds)
		{
			return bounds;
		}

		if (pipeline.bStencil)
		{
			pending.List->OMSetStencilRef(draw.StencilReference);
		}
		ApplyBlendConstant(pending, pipeline, drawState.BlendConstant);
		ApplyShadingRate(pending, drawState.ShadingRate, drawState.PrimitiveShadingRateCombiner, drawState.ImageShadingRateCombiner);
		Result<void> bound = BindFixedTextures(state, pending, pipeline, textures, storageTextures);

		if (!bound)
		{
			return bound;
		}
		bound = BindGraphicsDescriptorTable(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative, buffers);

		if (!bound)
		{
			return bound;
		}
		bound = RefreshDirectGraphicsRoot(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		if (!bound)
		{
			return bound;
		}
		BindGraphicsBuffers(pending, pipeline, buffers);

		if (accelNative != nullptr)
		{
			pending.List->SetGraphicsRootShaderResourceView(pipeline.AccelRootIndex,
				static_cast<const NativeAccelStruct*>(accelNative)->Address);
		}

		if (pipeline.PushConstantBytes != 0)
		{
			pending.List->SetGraphicsRoot32BitConstants(0, pipeline.PushConstantBytes / 4, draw.PushConstants.Data(), 0);
		}

		if (pipeline.bMeshShader)
		{
			pending.MeshList->DispatchMesh(draw.VertexCount, draw.InstanceCount, draw.FirstVertex);
		}
		else
		{
			pending.List->IASetPrimitiveTopology(pipeline.Topology);

			if (vertexNative != nullptr)
			{
				Buffer& vertex = *static_cast<Buffer*>(vertexNative);
				Transition(pending, *vertex.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
				D3D12_VERTEX_BUFFER_VIEW view{ vertex.Resource->GetGPUVirtualAddress() + vertexOffset,
											  static_cast<UINT>(vertexBytes - vertexOffset), stride };
				pending.List->IASetVertexBuffers(0, 1, &view);
			}
			pending.List->DrawInstanced(draw.VertexCount, draw.InstanceCount, draw.FirstVertex, draw.FirstInstance);
		}
		RestoreGraphicsBuffers(pending, buffers);
		RestoreFixedStorageTextures(pending, storageTextures);
		RestoreGraphicsDescriptorTable(pending, bindlessNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordDrawIndexed(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* vertexNative,
		void* indexNative,
		uint64 vertexOffset,
		uint64 vertexBytes,
		uint64 indexOffset,
		uint32 stride,
		Span<const NativeSampledTexture> textures,
		Span<const NativeStorageTexture> storageTextures,
		Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsState& drawState,
		const DrawIndexedArguments& draw)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(pipelineNative);
		Buffer* vertex = static_cast<Buffer*>(vertexNative);
		Buffer& index = *static_cast<Buffer*>(indexNative);
		if (pipeline.Fixed.LayoutCount == 0)
		{
			pending.List->SetGraphicsRootSignature(pipeline.RootSignature.Get());
		}
		pending.List->SetPipelineState(pipeline.State.Get());
		Result<void> bounds = applyDepthBounds(pending, pipeline);
		if (!bounds)
		{
			return bounds;
		}

		if (pipeline.bStencil)
		{
			pending.List->OMSetStencilRef(draw.StencilReference);
		}
		ApplyBlendConstant(pending, pipeline, drawState.BlendConstant);
		ApplyShadingRate(pending, drawState.ShadingRate, drawState.PrimitiveShadingRateCombiner, drawState.ImageShadingRateCombiner);
		Result<void> bound = BindFixedTextures(state, pending, pipeline, textures, storageTextures);

		if (!bound)
		{
			return bound;
		}
		bound = BindGraphicsDescriptorTable(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative, buffers);

		if (!bound)
		{
			return bound;
		}
		bound = RefreshDirectGraphicsRoot(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		if (!bound)
		{
			return bound;
		}
		BindGraphicsBuffers(pending, pipeline, buffers);

		if (accelNative != nullptr)
		{
			pending.List->SetGraphicsRootShaderResourceView(pipeline.AccelRootIndex,
				static_cast<const NativeAccelStruct*>(accelNative)->Address);
		}

		if (pipeline.PushConstantBytes != 0)
		{
			pending.List->SetGraphicsRoot32BitConstants(0, pipeline.PushConstantBytes / 4, draw.PushConstants.Data(), 0);
		}
		pending.List->IASetPrimitiveTopology(pipeline.Topology);
		const bool bSharedVertexIndex = vertex != nullptr && vertex->Resource.Get() == index.Resource.Get();
		if (vertex != nullptr)
		{
			Transition(pending, *vertex->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
				bSharedVertexIndex ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER
				: D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			D3D12_VERTEX_BUFFER_VIEW vertexView{ vertex->Resource->GetGPUVirtualAddress() + vertexOffset,
												static_cast<UINT>(vertexBytes - vertexOffset), stride };
			pending.List->IASetVertexBuffers(0, 1, &vertexView);
		}
		if (!bSharedVertexIndex)
		{
			Transition(pending, *index.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		}
		D3D12_INDEX_BUFFER_VIEW indexView{ index.Resource->GetGPUVirtualAddress() + indexOffset,
										  static_cast<UINT>(index.Resource->GetDesc().Width - indexOffset),
										  drawState.IndexFormat == EFormat::R16_UINT ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT };
		pending.List->IASetIndexBuffer(&indexView);
		pending.List->DrawIndexedInstanced(draw.IndexCount, draw.InstanceCount, draw.FirstIndex, draw.BaseVertex, draw.FirstInstance);
		RestoreGraphicsBuffers(pending, buffers);
		RestoreFixedStorageTextures(pending, storageTextures);
		RestoreGraphicsDescriptorTable(pending, bindlessNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordDrawIndirect(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* argumentNative,
		void* vertexNative,
		void* countNative,
		uint64 vertexOffset,
		uint64 vertexBytes,
		uint32 stride,
		Span<const NativeSampledTexture> textures,
		Span<const NativeStorageTexture> storageTextures,
		Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsDrawIndirect& draw)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(pipelineNative);
		Buffer& arguments = *static_cast<Buffer*>(argumentNative);
		if (pipeline.Fixed.LayoutCount == 0)
		{
			pending.List->SetGraphicsRootSignature(pipeline.RootSignature.Get());
		}
		pending.List->SetPipelineState(pipeline.State.Get());
		Result<void> bounds = applyDepthBounds(pending, pipeline);
		if (!bounds)
		{
			return bounds;
		}

		if (pipeline.bStencil)
		{
			pending.List->OMSetStencilRef(draw.StencilReference);
		}
		ApplyBlendConstant(pending, pipeline, draw.BlendConstant);
		ApplyShadingRate(pending, draw.ShadingRate, draw.PrimitiveShadingRateCombiner, draw.ImageShadingRateCombiner);
		Result<void> bound = BindFixedTextures(state, pending, pipeline, textures, storageTextures);

		if (!bound)
		{
			return bound;
		}
		bound = BindGraphicsDescriptorTable(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative, buffers);

		if (!bound)
		{
			return bound;
		}
		bound = RefreshDirectGraphicsRoot(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		if (!bound)
		{
			return bound;
		}
		BindGraphicsBuffers(pending, pipeline, buffers);

		if (accelNative != nullptr)
		{
			pending.List->SetGraphicsRootShaderResourceView(pipeline.AccelRootIndex,
				static_cast<const NativeAccelStruct*>(accelNative)->Address);
		}

		if (pipeline.PushConstantBytes != 0)
		{
			pending.List->SetGraphicsRoot32BitConstants(0, pipeline.PushConstantBytes / 4, draw.PushConstants.Data(), 0);
		}

		if (!pipeline.bMeshShader)
		{
			pending.List->IASetPrimitiveTopology(pipeline.Topology);

			if (vertexNative != nullptr)
			{
				Buffer& vertex = *static_cast<Buffer*>(vertexNative);
				Transition(pending, *vertex.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
				D3D12_VERTEX_BUFFER_VIEW view{ vertex.Resource->GetGPUVirtualAddress() + vertexOffset,
											  static_cast<UINT>(vertexBytes - vertexOffset), stride };
				pending.List->IASetVertexBuffers(0, 1, &view);
			}
		}
		Transition(pending, *arguments.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		Buffer* const count = static_cast<Buffer*>(countNative);

		if (count != nullptr)
		{
			Transition(pending, *count->Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		}
		pending.List->ExecuteIndirect(pipeline.bMeshShader ? state.MeshSignature.Get() : state.DrawSignature.Get(), draw.DrawCount,
			arguments.Resource.Get(), draw.ArgumentsOffsetBytes, count != nullptr ? count->Resource.Get() : nullptr,
			draw.CountBufferOffsetBytes);

		if (count != nullptr)
		{
			Transition(pending, *count->Resource.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
		}
		Transition(pending, *arguments.Resource.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
		RestoreGraphicsBuffers(pending, buffers);
		RestoreFixedStorageTextures(pending, storageTextures);
		RestoreGraphicsDescriptorTable(pending, bindlessNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordDrawIndexedIndirect(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* argumentNative,
		void* vertexNative,
		void* indexNative,
		void* countNative,
		uint64 vertexOffset,
		uint64 vertexBytes,
		uint64 indexOffset,
		uint32 stride,
		Span<const NativeSampledTexture> textures,
		Span<const NativeStorageTexture> storageTextures,
		Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsDrawIndexedIndirect& draw)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		GraphicsPipeline& pipeline = *static_cast<GraphicsPipeline*>(pipelineNative);
		Buffer& arguments = *static_cast<Buffer*>(argumentNative);
		Buffer* vertex = static_cast<Buffer*>(vertexNative);
		Buffer& index = *static_cast<Buffer*>(indexNative);
		if (pipeline.Fixed.LayoutCount == 0)
		{
			pending.List->SetGraphicsRootSignature(pipeline.RootSignature.Get());
		}
		pending.List->SetPipelineState(pipeline.State.Get());
		Result<void> bounds = applyDepthBounds(pending, pipeline);
		if (!bounds)
		{
			return bounds;
		}

		if (pipeline.bStencil)
		{
			pending.List->OMSetStencilRef(draw.StencilReference);
		}
		ApplyBlendConstant(pending, pipeline, draw.BlendConstant);
		ApplyShadingRate(pending, draw.ShadingRate, draw.PrimitiveShadingRateCombiner, draw.ImageShadingRateCombiner);
		Result<void> bound = BindFixedTextures(state, pending, pipeline, textures, storageTextures);

		if (!bound)
		{
			return bound;
		}
		bound = BindGraphicsDescriptorTable(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative, buffers);

		if (!bound)
		{
			return bound;
		}
		bound = RefreshDirectGraphicsRoot(state, pending, pipeline, bindlessNative, samplerNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		if (!bound)
		{
			return bound;
		}
		BindGraphicsBuffers(pending, pipeline, buffers);

		if (accelNative != nullptr)
		{
			pending.List->SetGraphicsRootShaderResourceView(pipeline.AccelRootIndex,
				static_cast<const NativeAccelStruct*>(accelNative)->Address);
		}

		if (pipeline.PushConstantBytes != 0)
		{
			pending.List->SetGraphicsRoot32BitConstants(0, pipeline.PushConstantBytes / 4, draw.PushConstants.Data(), 0);
		}
		pending.List->IASetPrimitiveTopology(pipeline.Topology);
		const bool bSharedVertexIndex = vertex != nullptr && vertex->Resource.Get() == index.Resource.Get();
		if (vertex != nullptr)
		{
			Transition(pending, *vertex->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
				bSharedVertexIndex ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER
				: D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			D3D12_VERTEX_BUFFER_VIEW vertexView{ vertex->Resource->GetGPUVirtualAddress() + vertexOffset,
												static_cast<UINT>(vertexBytes - vertexOffset), stride };
			pending.List->IASetVertexBuffers(0, 1, &vertexView);
		}
		if (!bSharedVertexIndex)
		{
			Transition(pending, *index.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		}
		D3D12_INDEX_BUFFER_VIEW indexView{ index.Resource->GetGPUVirtualAddress() + indexOffset,
										  static_cast<UINT>(index.Resource->GetDesc().Width - indexOffset),
										  draw.IndexFormat == EFormat::R16_UINT ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT };
		pending.List->IASetIndexBuffer(&indexView);
		Transition(pending, *arguments.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		Buffer* const count = static_cast<Buffer*>(countNative);

		if (count != nullptr)
		{
			Transition(pending, *count->Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		}
		pending.List->ExecuteIndirect(state.DrawIndexedSignature.Get(), draw.DrawCount, arguments.Resource.Get(), draw.ArgumentsOffsetBytes,
			count != nullptr ? count->Resource.Get() : nullptr, draw.CountBufferOffsetBytes);

		if (count != nullptr)
		{
			Transition(pending, *count->Resource.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
		}
		Transition(pending, *arguments.Resource.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
		RestoreGraphicsBuffers(pending, buffers);
		RestoreFixedStorageTextures(pending, storageTextures);
		RestoreGraphicsDescriptorTable(pending, bindlessNative, storageNative, storageTextureNative, uniformNative, readOnlyBufferNative);

		return Result<void>::Success();
	}
}
#endif
