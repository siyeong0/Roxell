#include "D3D12Backend.h"

#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<void*> CreateTextureView(State* opaque, void* nativeTexture, const TextureDesc& source, const TextureViewDesc& desc)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void*>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}
		TextureView* const view = CreateObject<TextureView>(state.Resource);

		if (view == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a texture view."));
		}
		Result<CpuDescriptorRange> heap = AllocateCpuDescriptors(state, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
		if (!heap)
		{
			DestroyObject(state.Resource, view);

			return Result<void*>::Failure(heap.GetError());
		}
		view->Descriptor = Move(heap).TakeValue();

		Texture& texture = *static_cast<Texture*>(nativeTexture);
		const DXGI_FORMAT format =
			desc.Aspect == ETextureAspect::Stencil
			? (source.Format == EFormat::D24S8 ? DXGI_FORMAT_X24_TYPELESS_G8_UINT : DXGI_FORMAT_X32_TYPELESS_G8X24_UINT)
			: GetDxgiFormatMapping(desc.Format == EFormat::UNKNOWN ? source.Format : desc.Format).SrvFormat;
		const D3D12_CPU_DESCRIPTOR_HANDLE cpu = view->Descriptor.Cpu;

		if (desc.Type == ETextureViewType::Sampled)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC native{};
			native.Format = format;
			// Stencil plane formats expose their only component as G; the public scalar view returns R.
			native.Shader4ComponentMapping =
				desc.Aspect == ETextureAspect::Stencil
				? D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
					D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
					D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1)
				: D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			switch (desc.Dimension)
			{
			case ETextureViewDimension::Texture2D:
				if (source.SampleCount == 1)
				{
					native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					native.Texture2D.MostDetailedMip = desc.BaseMipLevel;
					native.Texture2D.MipLevels = desc.MipLevelCount;
					native.Texture2D.PlaneSlice = desc.Aspect == ETextureAspect::Stencil ? 1u : 0u;
				}
				else
				{
					if (source.ArrayLayers > 1)
					{
						native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
						native.Texture2DMSArray.FirstArraySlice = desc.BaseArrayLayer;
						native.Texture2DMSArray.ArraySize = 1;
					}
					else
					{
						native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
					}
				}
				break;
			case ETextureViewDimension::Texture2DArray:
				if (source.SampleCount != 1)
				{
					native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
					native.Texture2DMSArray.FirstArraySlice = desc.BaseArrayLayer;
					native.Texture2DMSArray.ArraySize = desc.ArrayLayerCount;
				}
				else
				{
					native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
					native.Texture2DArray.MostDetailedMip = desc.BaseMipLevel;
					native.Texture2DArray.MipLevels = desc.MipLevelCount;
					native.Texture2DArray.FirstArraySlice = desc.BaseArrayLayer;
					native.Texture2DArray.ArraySize = desc.ArrayLayerCount;
					native.Texture2DArray.PlaneSlice = desc.Aspect == ETextureAspect::Stencil ? 1u : 0u;
				}
				break;
			case ETextureViewDimension::Cube:
				native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
				native.TextureCube.MostDetailedMip = desc.BaseMipLevel;
				native.TextureCube.MipLevels = desc.MipLevelCount;
				break;
			case ETextureViewDimension::CubeArray:
				native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
				native.TextureCubeArray.MostDetailedMip = desc.BaseMipLevel;
				native.TextureCubeArray.MipLevels = desc.MipLevelCount;
				native.TextureCubeArray.First2DArrayFace = desc.BaseArrayLayer;
				native.TextureCubeArray.NumCubes = desc.ArrayLayerCount / 6;
				break;
			case ETextureViewDimension::Texture3D:
				native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
				native.Texture3D.MostDetailedMip = desc.BaseMipLevel;
				native.Texture3D.MipLevels = desc.MipLevelCount;
				break;
			case ETextureViewDimension::Texture1D:
				native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
				native.Texture1D.MostDetailedMip = desc.BaseMipLevel;
				native.Texture1D.MipLevels = desc.MipLevelCount;
				break;
			case ETextureViewDimension::Texture1DArray:
				native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
				native.Texture1DArray.MostDetailedMip = desc.BaseMipLevel;
				native.Texture1DArray.MipLevels = desc.MipLevelCount;
				native.Texture1DArray.FirstArraySlice = desc.BaseArrayLayer;
				native.Texture1DArray.ArraySize = desc.ArrayLayerCount;
				break;

			default:
			{
				break;
			}
			}
			state.Device->CreateShaderResourceView(texture.Resource.Get(), &native, cpu);
		}
		else
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC native{};
			native.Format = format;

			if (desc.Dimension == ETextureViewDimension::Texture3D)
			{
				native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
				native.Texture3D.MipSlice = desc.BaseMipLevel;
				native.Texture3D.FirstWSlice = desc.BaseDepthSlice;
				const uint32 mipDepth = (source.Depth >> desc.BaseMipLevel) ? (source.Depth >> desc.BaseMipLevel) : 1;
				native.Texture3D.WSize = desc.DepthSliceCount != 0 ? desc.DepthSliceCount : mipDepth - desc.BaseDepthSlice;
			}
			else if (desc.Dimension == ETextureViewDimension::Texture1D)
			{
				native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
				native.Texture1D.MipSlice = desc.BaseMipLevel;
			}
			else if (desc.Dimension == ETextureViewDimension::Texture1DArray)
			{
				native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
				native.Texture1DArray.MipSlice = desc.BaseMipLevel;
				native.Texture1DArray.FirstArraySlice = desc.BaseArrayLayer;
				native.Texture1DArray.ArraySize = desc.ArrayLayerCount;
			}
			else if (desc.Dimension == ETextureViewDimension::Texture2D)
			{
				native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
				native.Texture2D.MipSlice = desc.BaseMipLevel;
			}
			else
			{
				native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
				native.Texture2DArray.MipSlice = desc.BaseMipLevel;
				native.Texture2DArray.FirstArraySlice = desc.BaseArrayLayer;
				native.Texture2DArray.ArraySize = desc.ArrayLayerCount;
				native.Texture2DArray.PlaneSlice = desc.Aspect == ETextureAspect::Stencil ? 1u : 0u;
			}
			state.Device->CreateUnorderedAccessView(texture.Resource.Get(), nullptr, &native, cpu);
		}
		view->Range = desc;

		return Result<void*>::Success(view);
	}

	void DestroyTextureView(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		TextureView* view = static_cast<TextureView*>(native);
		FreeCpuDescriptors(state, view->Descriptor);
		DestroyObject(state.Resource, view);
	}

	[[nodiscard]] Result<void*> CreateTextureInternal(State* opaque, const TextureDesc& desc, Heap* placedHeap, uint64 offset, ID3D12Resource* imported)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		const DxgiFormatMapping& mapping = GetDxgiFormatMapping(desc.Format);
		const bool bDepth = HasUsage(desc.Usage, ETextureUsage::DepthStencil);
		const DXGI_FORMAT format = bDepth || desc.bMutableFormat ? mapping.ResourceFormat : mapping.SrvFormat;

		if (format == DXGI_FORMAT_UNKNOWN)
		{
			return Result<void*>::Failure(Failure("Rhi.D3D12Format", "No D3D12 texture format mapping exists."));
		}

		if (!SupportsSamples(state, bDepth ? mapping.RtvFormat : mapping.SrvFormat, desc.SampleCount))
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.Multisample", "The D3D12 device does not support this sample count.");
		}

		Texture* texture = CreateObject<Texture>(state.Resource);

		if (texture == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a D3D12 texture record."));
		}

		texture->Format = desc.Format;
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		const D3D12_RESOURCE_DESC nativeDesc = MakeTextureResourceDesc(desc, format);
		HRESULT result = S_OK;

		if (imported != nullptr)
		{
			texture->Resource = imported;
			texture->bUnknownAliases = true;
		}
		else if (desc.bSparse)
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
			if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) || options.TiledResourcesTier == D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED)
			{
				DestroyObject(state.Resource, texture);

				return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12SparseTexture", "The D3D12 adapter does not support tiled resources.");
			}

			result = state.Device->CreateReservedResource(&nativeDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(texture->Resource.GetAddressOf()));
		}
		else if (placedHeap == nullptr)
		{
			result = state.Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &nativeDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(texture->Resource.GetAddressOf()));
		}
		else
		{
			result = state.Device->CreatePlacedResource(placedHeap->Native.Get(), offset, &nativeDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(texture->Resource.GetAddressOf()));
		}

		if (FAILED(result))
		{
			DestroyObject(state.Resource, texture);

			return Result<void*>::Failure(Failure("Rhi.D3D12Texture", "Could not create a D3D12 texture."));
		}

		if (HasUsage(desc.Usage, ETextureUsage::RenderTarget))
		{
			D3D12_DESCRIPTOR_HEAP_DESC viewDesc{};
			viewDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			viewDesc.NumDescriptors = 1;

			if (FAILED(state.Device->CreateDescriptorHeap(&viewDesc, IID_PPV_ARGS(texture->RenderTargetView.GetAddressOf()))))
			{
				DestroyObject(state.Resource, texture);

				return Result<void*>::Failure(Failure("Rhi.D3D12Texture", "Could not create a render target view."));
			}

			D3D12_RENDER_TARGET_VIEW_DESC renderView{};
			renderView.Format = mapping.RtvFormat;

			if (desc.SampleCount != 1)
			{
				if (desc.ArrayLayers > 1)
				{
					renderView.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
					renderView.Texture2DMSArray.ArraySize = 1;
				}
				else
				{
					renderView.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
				}
			}
			else if (desc.Dimension == ETextureDimension::Texture1D && desc.ArrayLayers > 1)
			{
				renderView.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
				renderView.Texture1DArray.ArraySize = 1;
			}
			else if (desc.Dimension == ETextureDimension::Texture1D)
			{
				renderView.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
			}
			else if (desc.Dimension == ETextureDimension::Texture3D)
			{
				renderView.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
				renderView.Texture3D.WSize = desc.Depth;
			}
			else if (desc.ArrayLayers > 1)
			{
				renderView.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
				renderView.Texture2DArray.ArraySize = 1;
			}
			else
			{
				renderView.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			}

			state.Device->CreateRenderTargetView(texture->Resource.Get(), &renderView,
				texture->RenderTargetView->GetCPUDescriptorHandleForHeapStart());
		}

		if (bDepth)
		{
			D3D12_DESCRIPTOR_HEAP_DESC viewDesc{};
			viewDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			viewDesc.NumDescriptors = 4;

			if (FAILED(state.Device->CreateDescriptorHeap(&viewDesc, IID_PPV_ARGS(texture->DepthStencilView.GetAddressOf()))))
			{
				DestroyObject(state.Resource, texture);

				return Result<void*>::Failure(Failure("Rhi.D3D12Texture", "Could not create a depth view."));
			}

			D3D12_DEPTH_STENCIL_VIEW_DESC depthView{};
			depthView.Format = mapping.RtvFormat;

			if (desc.SampleCount != 1)
			{
				if (desc.ArrayLayers > 1)
				{
					depthView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
					depthView.Texture2DMSArray.ArraySize = 1;
				}
				else
				{
					depthView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
				}
			}
			else if (desc.Dimension == ETextureDimension::Texture1D && desc.ArrayLayers > 1)
			{
				depthView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
				depthView.Texture1DArray.ArraySize = 1;
			}
			else if (desc.Dimension == ETextureDimension::Texture1D)
			{
				depthView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
			}
			else if (desc.ArrayLayers > 1)
			{
				depthView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
				depthView.Texture2DArray.ArraySize = 1;
			}
			else
			{
				depthView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			}

			for (UINT mask = 0; mask < 4; ++mask)
			{
				depthView.Flags = static_cast<D3D12_DSV_FLAGS>(
					(mask & 1 ? D3D12_DSV_FLAG_READ_ONLY_DEPTH : 0) |
					((mask & 2) && GetFormatInfo(desc.Format).bHasStencil ? D3D12_DSV_FLAG_READ_ONLY_STENCIL : 0));
				D3D12_CPU_DESCRIPTOR_HANDLE selectedView = texture->DepthStencilView->GetCPUDescriptorHandleForHeapStart();
				selectedView.ptr += mask * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
				state.Device->CreateDepthStencilView(texture->Resource.Get(), &depthView, selectedView);
			}
		}

		if (HasUsage(desc.Usage, ETextureUsage::ShaderRead))
		{
			D3D12_DESCRIPTOR_HEAP_DESC viewHeap{};
			viewHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			viewHeap.NumDescriptors = 1;

			if (FAILED(state.Device->CreateDescriptorHeap(&viewHeap, IID_PPV_ARGS(texture->ShaderView.GetAddressOf()))))
			{
				DestroyObject(state.Resource, texture);

				return Result<void*>::Failure(Failure("Rhi.D3D12Texture", "Could not create a shader resource view heap."));
			}

			D3D12_SHADER_RESOURCE_VIEW_DESC shaderView{};
			shaderView.Format = mapping.SrvFormat;
			shaderView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			if (desc.SampleCount != 1)
			{
				if (desc.ArrayLayers > 1)
				{
					shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
					shaderView.Texture2DMSArray.ArraySize = desc.ArrayLayers;
				}
				else
				{
					shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
				}
			}
			else if (desc.Dimension == ETextureDimension::Texture1D && desc.ArrayLayers == 1)
			{
				shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
				shaderView.Texture1D.MipLevels = desc.MipLevels;
			}
			else if (desc.Dimension == ETextureDimension::Texture1D)
			{
				shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
				shaderView.Texture1DArray.ArraySize = desc.ArrayLayers;
				shaderView.Texture1DArray.MipLevels = desc.MipLevels;
			}
			else if (desc.Dimension == ETextureDimension::Texture3D)
			{
				shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
				shaderView.Texture3D.MipLevels = desc.MipLevels;
			}
			else if (desc.Dimension == ETextureDimension::Cube && desc.ArrayLayers == 6)
			{
				shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
				shaderView.TextureCube.MipLevels = desc.MipLevels;
			}
			else if (desc.Dimension == ETextureDimension::Cube)
			{
				shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
				shaderView.TextureCubeArray.MipLevels = desc.MipLevels;
				shaderView.TextureCubeArray.NumCubes = desc.ArrayLayers / 6;
			}
			else if (desc.ArrayLayers == 1)
			{
				shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				shaderView.Texture2D.MipLevels = desc.MipLevels;
			}
			else
			{
				shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				shaderView.Texture2DArray.ArraySize = desc.ArrayLayers;
				shaderView.Texture2DArray.MipLevels = desc.MipLevels;
			}

			state.Device->CreateShaderResourceView(texture->Resource.Get(), &shaderView,
				texture->ShaderView->GetCPUDescriptorHandleForHeapStart());
		}

		if (HasUsage(desc.Usage, ETextureUsage::Storage))
		{
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDesc.NumDescriptors = 1;

			if (FAILED(state.Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(texture->StorageView.GetAddressOf()))))
			{
				DestroyObject(state.Resource, texture);

				return Result<void*>::Failure(Failure("Rhi.D3D12Texture", "Could not create a storage image descriptor."));
			}

			D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
			view.Format = mapping.SrvFormat;

			if (desc.Dimension == ETextureDimension::Texture3D)
			{
				view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
				view.Texture3D.WSize = desc.Depth;
			}
			else if (desc.Dimension == ETextureDimension::Texture1D && desc.ArrayLayers == 1)
			{
				view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
			}
			else if (desc.Dimension == ETextureDimension::Texture1D)
			{
				view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
				view.Texture1DArray.ArraySize = desc.ArrayLayers;
			}
			else if (desc.ArrayLayers == 1)
			{
				view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			}
			else
			{
				view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
				view.Texture2DArray.ArraySize = desc.ArrayLayers;
			}

			state.Device->CreateUnorderedAccessView(texture->Resource.Get(), nullptr, &view,
				texture->StorageView->GetCPUDescriptorHandleForHeapStart());
		}

		return Result<void*>::Success(texture);
	}

	[[nodiscard]] Result<void*> CreateTexture(State* opaque, const TextureDesc& desc)
	{
		return CreateTextureInternal(opaque, desc, nullptr, 0);
	}

	[[nodiscard]] Result<void*> ImportTexture(State* opaque, const TextureDesc& desc, void* native, ENativeTextureContents)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		ID3D12Resource* resource = static_cast<ID3D12Resource*>(native);
		const DxgiFormatMapping& mapping = GetDxgiFormatMapping(desc.Format);
		const DXGI_FORMAT format = HasUsage(desc.Usage, ETextureUsage::DepthStencil) || desc.bMutableFormat ? mapping.ResourceFormat : mapping.SrvFormat;
		const D3D12_RESOURCE_DESC expected = MakeTextureResourceDesc(desc, format);
		const D3D12_RESOURCE_DESC actual = resource->GetDesc();

		if (actual.Dimension != expected.Dimension ||
			actual.Width != expected.Width ||
			actual.Height != expected.Height ||
			actual.DepthOrArraySize != expected.DepthOrArraySize ||
			actual.MipLevels != expected.MipLevels ||
			actual.Format != expected.Format ||
			actual.SampleDesc.Count != expected.SampleDesc.Count ||
			(actual.Flags & expected.Flags) != expected.Flags)
		{
			return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.NativeTexture", "The D3D12 resource does not match the texture descriptor.");
		}
		ComPtr<ID3D12Device> owner;

		if (FAILED(resource->GetDevice(IID_PPV_ARGS(owner.GetAddressOf()))) || owner.Get() != state.Device.Get())
		{
			return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.NativeTexture", "The D3D12 texture belongs to another device.");
		}

		return CreateTextureInternal(opaque, desc, nullptr, 0, resource);
	}

	[[nodiscard]] void* GetNativeTexture(const void* native) noexcept
	{
		return static_cast<const Texture*>(native)->Resource.Get();
	}

	[[nodiscard]] Result<void*> CreatePlacedTexture(State* opaque, const TextureDesc& desc, void* heap, uint64 offset)
	{
		return CreateTextureInternal(opaque, desc, static_cast<Heap*>(heap), offset);
	}

	[[nodiscard]] uint64 GetTextureLastUse(const void* native) noexcept
	{
		return static_cast<const Texture*>(native)->LastUse;
	}

	void DestroyTextureNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<Texture*>(native));
	}

	[[nodiscard]] Result<void> DestroyTexture(State* opaque, void* native)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}

		Texture& texture = *static_cast<Texture*>(native);

		if (texture.LastUse > CompletedSerial(state))
		{
			if (!state.RetiredTextures.Add({ &texture, texture.LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not track a retired D3D12 texture."));
			}
		}
		else
		{
			DestroyTextureNow(opaque, native);
		}

		return Result<void>::Success();
	}
}
#endif
