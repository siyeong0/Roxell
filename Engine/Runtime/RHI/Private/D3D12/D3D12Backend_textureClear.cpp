#include "D3D12Backend.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<void> RecordStorageTextureClear(
		State& state,
		Pending& pending,
		Texture& texture,
		const TextureDesc& desc,
		TextureSubresource subresource,
		const ClearColor* color,
		uint32 clearValue)
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = 1;
		ComPtr<ID3D12DescriptorHeap> cpuHeap;

		if (FAILED(state.Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(cpuHeap.GetAddressOf()))))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12UIntClear", "Could not allocate an integer clear view."));
		}
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ComPtr<ID3D12DescriptorHeap> gpuHeap;

		if (FAILED(state.Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(gpuHeap.GetAddressOf()))))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12UIntClear", "Could not allocate a visible integer clear view."));
		}
		D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
		view.Format = GetDxgiFormatMapping(desc.Format).SrvFormat;

		if (desc.Dimension == ETextureDimension::Texture3D)
		{
			view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
			view.Texture3D.MipSlice = subresource.MipLevel;
			view.Texture3D.WSize = (desc.Depth >> subresource.MipLevel) ? (desc.Depth >> subresource.MipLevel) : 1;
		}
		else if (desc.Dimension == ETextureDimension::Texture1D && desc.ArrayLayers > 1)
		{
			view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
			view.Texture1DArray.MipSlice = subresource.MipLevel;
			view.Texture1DArray.FirstArraySlice = subresource.ArrayLayer;
			view.Texture1DArray.ArraySize = 1;
		}
		else if (desc.Dimension == ETextureDimension::Texture1D)
		{
			view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
			view.Texture1D.MipSlice = subresource.MipLevel;
		}
		else if (desc.ArrayLayers == 1)
		{
			view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			view.Texture2D.MipSlice = subresource.MipLevel;
		}
		else
		{
			view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			view.Texture2DArray.MipSlice = subresource.MipLevel;
			view.Texture2DArray.FirstArraySlice = subresource.ArrayLayer;
			view.Texture2DArray.ArraySize = 1;
		}

		const D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpuHeap->GetCPUDescriptorHandleForHeapStart();
		state.Device->CreateUnorderedAccessView(texture.Resource.Get(), nullptr, &view, cpu);
		CopyCommandDescriptors(state, pending, 1, gpuHeap->GetCPUDescriptorHandleForHeapStart(), cpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		if (!pending.AttachmentHeaps.Reserve(pending.AttachmentHeaps.Size() + 2))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain integer clear views."));
		}
		(void)pending.AttachmentHeaps.Add(Move(cpuHeap));
		(void)pending.AttachmentHeaps.Add(Move(gpuHeap));
		ID3D12DescriptorHeap* const visible = pending.AttachmentHeaps.Back().Get();
		const UINT subresourceIndex = subresource.MipLevel + subresource.ArrayLayer * desc.MipLevels;
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, subresourceIndex);
		pending.List->SetDescriptorHeaps(1, &visible);
		pending.BoundTextureHeap = visible;
		pending.BoundSamplerHeap = nullptr;
		pending.LastTexturePipeline = nullptr;

		if (color != nullptr && color->GetType() == EClearColorType::Float)
		{
			pending.List->ClearUnorderedAccessViewFloat(visible->GetGPUDescriptorHandleForHeapStart(), cpu, texture.Resource.Get(), color->GetFloat(),
				0, nullptr);
		}
		else
		{
			UINT values[4]{ clearValue, clearValue, clearValue, clearValue };
			if (color != nullptr)
			{
				for (uint32 index = 0; index < 4; ++index)
				{
					values[index] = color->GetType() == EClearColorType::Uint ? color->GetUint()[index] : static_cast<UINT>(color->GetSint()[index]);
				}
			}
			pending.List->ClearUnorderedAccessViewUint(visible->GetGPUDescriptorHandleForHeapStart(), cpu, texture.Resource.Get(), values,
				0, nullptr);
		}
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = texture.Resource.Get();
		RecordBarriers(pending, 1, &barrier);
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, subresourceIndex);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureClear(State* opaque, void* native, void* target, const TextureDesc& desc, TextureSubresource subresource, const ClearColor& color)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);

		if (pending.Queue == EQueue::Copy)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12TextureClear", "A D3D12 copy list cannot clear a texture.");
		}

		Texture& texture = *static_cast<Texture*>(target);

		if (!HasUsage(desc.Usage, ETextureUsage::RenderTarget))
		{
			return RecordStorageTextureClear(state, pending, texture, desc, subresource, &color, 0);
		}
		D3D12_CPU_DESCRIPTOR_HANDLE handle = texture.RenderTargetView->GetCPUDescriptorHandleForHeapStart();

		const UINT clearSlices = color.GetType() != EClearColorType::Float && desc.Dimension == ETextureDimension::Texture3D ? Max(1u, desc.Depth >> subresource.MipLevel) : 1;
		if (desc.MipLevels != 1 || desc.ArrayLayers != 1 || clearSlices > 1)
		{
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heapDesc.NumDescriptors = clearSlices;
			ComPtr<ID3D12DescriptorHeap> heap;

			if (FAILED(state.Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(heap.GetAddressOf()))))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12TextureClear", "Could not allocate a render target view."));
			}
			D3D12_RENDER_TARGET_VIEW_DESC view{};
			view.Format = GetDxgiFormatMapping(desc.Format).RtvFormat;

			if (desc.Dimension == ETextureDimension::Texture1D && desc.ArrayLayers > 1)
			{
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
				view.Texture1DArray.MipSlice = subresource.MipLevel;
				view.Texture1DArray.FirstArraySlice = subresource.ArrayLayer;
				view.Texture1DArray.ArraySize = 1;
			}
			else if (desc.Dimension == ETextureDimension::Texture1D)
			{
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
				view.Texture1D.MipSlice = subresource.MipLevel;
			}
			else if (desc.Dimension == ETextureDimension::Texture3D)
			{
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
				view.Texture3D.MipSlice = subresource.MipLevel;
				view.Texture3D.FirstWSlice = 0;
				view.Texture3D.WSize = (desc.Depth >> subresource.MipLevel) ? (desc.Depth >> subresource.MipLevel) : 1;
			}
			else if (desc.SampleCount > 1)
			{
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
				view.Texture2DMSArray.FirstArraySlice = subresource.ArrayLayer;
				view.Texture2DMSArray.ArraySize = 1;
			}
			else if (desc.ArrayLayers > 1)
			{
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
				view.Texture2DArray.MipSlice = subresource.MipLevel;
				view.Texture2DArray.FirstArraySlice = subresource.ArrayLayer;
				view.Texture2DArray.ArraySize = 1;
			}
			else
			{
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
				view.Texture2D.MipSlice = subresource.MipLevel;
			}
			handle = heap->GetCPUDescriptorHandleForHeapStart();
			if (color.GetType() != EClearColorType::Float && desc.Dimension == ETextureDimension::Texture3D)
			{
				view.Texture3D.WSize = 1;
				for (UINT slice = 0; slice < clearSlices; ++slice)
				{
					view.Texture3D.FirstWSlice = slice;
					const D3D12_CPU_DESCRIPTOR_HANDLE selected{
						handle.ptr + slice * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) };
					state.Device->CreateRenderTargetView(texture.Resource.Get(), &view, selected);
				}
			}
			else
			{
				state.Device->CreateRenderTargetView(texture.Resource.Get(), &view, handle);
			}

			if (!pending.AttachmentHeaps.Add(Move(heap)))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain a render target view."));
			}
		}

		if (color.GetType() != EClearColorType::Float)
		{
			D3D12IntegerClear clear;
			if (FAILED(state.IntegerClearCache.Capture(*state.Device.Get(), GetDxgiFormatMapping(desc.Format).RtvFormat, desc.SampleCount, color.GetType(), clear)) ||
				!pending.IntegerClears.Add(Move(clear)))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12IntegerClear", "Could not retain the exact integer clear pipeline."));
			}
		}
		const UINT subresourceIndex = subresource.MipLevel + subresource.ArrayLayer * desc.MipLevels;
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET, subresourceIndex);
		if (color.GetType() == EClearColorType::Float)
		{
			pending.List->ClearRenderTargetView(handle, color.GetFloat(), 0, nullptr);
		}
		else
		{
			for (UINT slice = 0; slice < clearSlices; ++slice)
			{
				const D3D12_CPU_DESCRIPTOR_HANDLE selected{
					handle.ptr + slice * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) };
				pending.IntegerClears.Back().Record(*pending.List.Get(), selected, Max(1u, desc.Width >> subresource.MipLevel),
					Max(1u, desc.Height >> subresource.MipLevel), color);
			}
			pending.LastTexturePipeline = nullptr;
		}
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON, subresourceIndex);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordTextureUIntClear(State* opaque, void* native, void* target, const TextureDesc& desc, TextureSubresource subresource, uint32 clearValue)
	{
		if (static_cast<Pending*>(native)->Queue == EQueue::Copy)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12TextureClear", "A D3D12 copy list cannot clear a texture.");
		}

		if (!HasUsage(desc.Usage, ETextureUsage::Storage))
		{
			State& state = *opaque;
			const FormatInfo& info = GetFormatInfo(desc.Format);
			const uint32 channels = static_cast<uint32>(info.bHasRed) + info.bHasGreen + info.bHasBlue + info.bHasAlpha;
			const uint32 channelBytes = info.BytesPerBlock / channels;
			const auto extent = [subresource](uint32 value) noexcept { return Max(1u, value >> subresource.MipLevel); };
			const uint64 byteCount = static_cast<uint64>(extent(desc.Width)) * extent(desc.Height) * extent(desc.Depth) * info.BytesPerBlock;
			Array<Byte> data(state.Resource);

			if (!data.ResizeUninitialized(static_cast<usize>(byteCount)))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not allocate exact integer attachment clear data."));
			}

			for (usize offset = 0; offset < data.Size(); offset += channelBytes)
			{
				CopyBytes(data.Data() + offset, &clearValue, channelBytes);
			}

			return RecordTextureWrite(opaque, native, target, desc, subresource, data.View());
		}

		return RecordStorageTextureClear(*opaque, *static_cast<Pending*>(native), *static_cast<Texture*>(target), desc,
			subresource, nullptr, clearValue);
	}

	[[nodiscard]] Result<void> RecordDepthClear(State* opaque, void* native, void* target, const TextureDesc& desc, TextureSubresource subresource, DepthStencilClear clear)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(target);
		D3D12_CPU_DESCRIPTOR_HANDLE handle = texture.DepthStencilView->GetCPUDescriptorHandleForHeapStart();

		if (desc.MipLevels != 1 || desc.ArrayLayers != 1)
		{
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			heapDesc.NumDescriptors = 1;
			ComPtr<ID3D12DescriptorHeap> heap;

			if (FAILED(state.Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(heap.GetAddressOf()))))
			{
				return Result<void>::Failure(Failure("Rhi.D3D12DepthClear", "Could not allocate a depth view."));
			}
			D3D12_DEPTH_STENCIL_VIEW_DESC view{};
			view.Format = GetDxgiFormatMapping(desc.Format).RtvFormat;

			if (desc.SampleCount > 1)
			{
				view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
				view.Texture2DMSArray.FirstArraySlice = subresource.ArrayLayer;
				view.Texture2DMSArray.ArraySize = 1;
			}
			else if (desc.Dimension == ETextureDimension::Texture1D)
			{
				view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
				view.Texture1DArray.MipSlice = subresource.MipLevel;
				view.Texture1DArray.FirstArraySlice = subresource.ArrayLayer;
				view.Texture1DArray.ArraySize = 1;
			}
			else
			{
				view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
				view.Texture2DArray.MipSlice = subresource.MipLevel;
				view.Texture2DArray.FirstArraySlice = subresource.ArrayLayer;
				view.Texture2DArray.ArraySize = 1;
			}
			handle = heap->GetCPUDescriptorHandleForHeapStart();
			state.Device->CreateDepthStencilView(texture.Resource.Get(), &view, handle);

			if (!pending.AttachmentHeaps.Add(Move(heap)))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain a depth view."));
			}
		}
		TransitionDepthSubresource(pending, texture, subresource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		const D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>((clear.bClearDepth ? D3D12_CLEAR_FLAG_DEPTH : 0) | (clear.bClearStencil ? D3D12_CLEAR_FLAG_STENCIL : 0));
		pending.List->ClearDepthStencilView(handle, flags, clear.Depth, clear.Stencil, 0, nullptr);
		TransitionDepthSubresource(pending, texture, subresource, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COMMON);

		return Result<void>::Success();
	}
}
#endif
