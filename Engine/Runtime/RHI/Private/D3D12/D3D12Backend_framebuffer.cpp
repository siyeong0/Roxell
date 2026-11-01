#include "D3D12Backend.h"

#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<void*> CreateFramebuffer(State* opaque, const NativeFramebufferDesc& desc)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}
		Framebuffer* framebuffer = CreateObject<Framebuffer>(state.Resource);

		if (framebuffer == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a framebuffer."));
		}
		framebuffer->ColorCount = desc.ColorCount;
		framebuffer->Width = desc.Width;
		framebuffer->Height = desc.Height;
		framebuffer->ShadingRateImage = static_cast<Texture*>(desc.ShadingRateImage);

		if (desc.ColorCount != 0)
		{
			Result<CpuDescriptorRange> heap = AllocateCpuDescriptors(state, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, desc.ColorCount);
			if (!heap)
			{
				DestroyFramebufferNow(opaque, framebuffer);

				return Result<void*>::Failure(heap.GetError());
			}
			framebuffer->ColorViews = Move(heap).TakeValue();
		}

		for (uint32 color = 0; color < desc.ColorCount; ++color)
		{
			Texture* const texture = static_cast<Texture*>(desc.Colors[color]);
			framebuffer->Colors[color] = texture;
			if (GetFormatInfo(desc.ColorFormats[color]).Kind == EFormatKind::Integer &&
				FAILED(state.IntegerClearCache.Capture( *state.Device.Get(), GetDxgiFormatMapping(desc.ColorFormats[color]).RtvFormat, desc.SampleCount, GetFormatInfo(desc.ColorFormats[color]).bIsSigned ? EClearColorType::Sint : EClearColorType::Uint, framebuffer->IntegerClears[color])))
			{
				DestroyFramebufferNow(opaque, framebuffer);

				return Result<void*>::Failure(Failure("Rhi.D3D12IntegerClear", "Could not prepare the exact integer attachment clear."));
			}
			const TextureSubresource subresource = desc.ColorSubresources[color];
			framebuffer->ColorSubresources[color] =
				subresource.MipLevel + (texture->Resource->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
					? 0
					: subresource.ArrayLayer * texture->Resource->GetDesc().MipLevels);
			D3D12_RENDER_TARGET_VIEW_DESC view{};
			view.Format = GetDxgiFormatMapping(desc.ColorFormats[color]).RtvFormat;
			const D3D12_RESOURCE_DESC resourceDesc = texture->Resource->GetDesc();

			if (desc.SampleCount != 1)
			{
				if (resourceDesc.DepthOrArraySize > 1)
				{
					view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
					view.Texture2DMSArray.FirstArraySlice = subresource.ArrayLayer;
					view.Texture2DMSArray.ArraySize = 1;
				}
				else
				{
					view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
				}
			}
			else if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
			{
				if (resourceDesc.DepthOrArraySize == 1)
				{
					view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
					view.Texture1D.MipSlice = subresource.MipLevel;
				}
				else
				{
					view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
					view.Texture1DArray.MipSlice = subresource.MipLevel;
					view.Texture1DArray.FirstArraySlice = subresource.ArrayLayer;
					view.Texture1DArray.ArraySize = 1;
				}
			}
			else if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
			{
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
				view.Texture3D.MipSlice = subresource.MipLevel;
				view.Texture3D.FirstWSlice = subresource.ArrayLayer;
				view.Texture3D.WSize = 1;
			}
			else if (resourceDesc.DepthOrArraySize > 1)
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
			D3D12_CPU_DESCRIPTOR_HANDLE handle = framebuffer->ColorViews.Cpu;
			handle.ptr += static_cast<SIZE_T>(color) * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			state.Device->CreateRenderTargetView(texture->Resource.Get(), &view, handle);
		}
		framebuffer->Depth = static_cast<Texture*>(desc.Depth);
		framebuffer->DepthSubresource = desc.DepthSubresource;

		if (framebuffer->Depth != nullptr)
		{
			Result<CpuDescriptorRange> heap = AllocateCpuDescriptors(state, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 4);
			if (!heap)
			{
				DestroyFramebufferNow(opaque, framebuffer);

				return Result<void*>::Failure(heap.GetError());
			}
			framebuffer->DepthView = Move(heap).TakeValue();
			D3D12_DEPTH_STENCIL_VIEW_DESC view{};
			view.Format = GetDxgiFormatMapping(desc.DepthFormat).RtvFormat;
			const D3D12_RESOURCE_DESC resourceDesc = framebuffer->Depth->Resource->GetDesc();

			if (desc.SampleCount != 1)
			{
				if (resourceDesc.DepthOrArraySize > 1)
				{
					view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
					view.Texture2DMSArray.FirstArraySlice = desc.DepthSubresource.ArrayLayer;
					view.Texture2DMSArray.ArraySize = 1;
				}
				else
				{
					view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
				}
			}
			else if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
			{
				if (resourceDesc.DepthOrArraySize == 1)
				{
					view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
					view.Texture1D.MipSlice = desc.DepthSubresource.MipLevel;
				}
				else
				{
					view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
					view.Texture1DArray.MipSlice = desc.DepthSubresource.MipLevel;
					view.Texture1DArray.FirstArraySlice = desc.DepthSubresource.ArrayLayer;
					view.Texture1DArray.ArraySize = 1;
				}
			}
			else if (resourceDesc.DepthOrArraySize > 1)
			{
				view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
				view.Texture2DArray.MipSlice = desc.DepthSubresource.MipLevel;
				view.Texture2DArray.FirstArraySlice = desc.DepthSubresource.ArrayLayer;
				view.Texture2DArray.ArraySize = 1;
			}
			else
			{
				view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				view.Texture2D.MipSlice = desc.DepthSubresource.MipLevel;
			}
			for (uint32 flags = 0; flags < 4; ++flags)
			{
				view.Flags = static_cast<D3D12_DSV_FLAGS>(
					(flags & 1 ? D3D12_DSV_FLAG_READ_ONLY_DEPTH : 0) |
					((flags & 2) && GetFormatInfo(desc.DepthFormat).bHasStencil ? D3D12_DSV_FLAG_READ_ONLY_STENCIL : 0));
				D3D12_CPU_DESCRIPTOR_HANDLE handle = framebuffer->DepthView.Cpu;
				handle.ptr += flags * state.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
				state.Device->CreateDepthStencilView(framebuffer->Depth->Resource.Get(), &view, handle);
			}
		}

		return Result<void*>::Success(framebuffer);
	}

	void DestroyFramebufferNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		Framebuffer* framebuffer = static_cast<Framebuffer*>(native);
		FreeCpuDescriptors(state, framebuffer->ColorViews);
		FreeCpuDescriptors(state, framebuffer->DepthView);
		DestroyObject(state.Resource, framebuffer);
	}

	[[nodiscard]] Result<void> DestroyFramebuffer(State* opaque, void* native)
	{
		State& state = *opaque;

		if (state.bLost)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 queue is unavailable."));
		}

		Framebuffer& framebuffer = *static_cast<Framebuffer*>(native);

		if (framebuffer.LastUse > CompletedSerial(state))
		{
			if (!state.RetiredFramebuffers.Add({ &framebuffer, framebuffer.LastUse }))
			{
				return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retire a framebuffer."));
			}
		}
		else
		{
			DestroyFramebufferNow(opaque, native);
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBeginFramebuffer(State* opaque, void* native, void* target, DepthAttachmentOptions depthOptions)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Framebuffer& framebuffer = *static_cast<Framebuffer*>(target);
		D3D12_CPU_DESCRIPTOR_HANDLE views[8]{};
		for (uint32 color = 0; color < framebuffer.ColorCount; ++color)
		{
			Texture& texture = *framebuffer.Colors[color];
			Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET,
				framebuffer.ColorSubresources[color]);
			views[color] = framebuffer.ColorViews.Cpu;
			views[color].ptr += static_cast<SIZE_T>(color) * opaque->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}
		D3D12_CPU_DESCRIPTOR_HANDLE depthView{};
		if (framebuffer.Depth != nullptr)
		{
			const D3D12_RESOURCE_STATES readState = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			TextureSubresource plane = framebuffer.DepthSubresource;
			plane.Aspect = ETextureAspect::Depth;
			TransitionDepthSubresource(pending, *framebuffer.Depth, plane, D3D12_RESOURCE_STATE_COMMON,
				depthOptions.bReadOnly ? readState : D3D12_RESOURCE_STATE_DEPTH_WRITE);
			if (GetFormatInfo(framebuffer.Depth->Format).bHasStencil)
			{
				plane.Aspect = ETextureAspect::Stencil;
				TransitionDepthSubresource(pending, *framebuffer.Depth, plane, D3D12_RESOURCE_STATE_COMMON,
					depthOptions.bReadOnlyStencil ? readState : D3D12_RESOURCE_STATE_DEPTH_WRITE);
			}
			depthView = framebuffer.DepthView.Cpu;
			depthView.ptr += (uint32(depthOptions.bReadOnly) + 2u * depthOptions.bReadOnlyStencil) * opaque->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			const D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>(
				(depthOptions.LoadOp == EDepthLoadOp::Clear ? D3D12_CLEAR_FLAG_DEPTH : 0) |
				(GetFormatInfo(framebuffer.Depth->Format).bHasStencil && depthOptions.StencilLoadOp == EDepthLoadOp::Clear
					? D3D12_CLEAR_FLAG_STENCIL
					: 0));
			if (flags != 0)
			{
				pending.List->ClearDepthStencilView(depthView, flags, depthOptions.ClearValue, depthOptions.ClearStencil, 0, nullptr);
			}
		}

		if (framebuffer.ShadingRateImage != nullptr)
		{
			Transition(pending, *framebuffer.ShadingRateImage->Resource.Get(), D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);
		}

		if (framebuffer.ShadingRateImage != nullptr && pending.ShadingRateList)
		{
			pending.ShadingRateList->RSSetShadingRateImage(framebuffer.ShadingRateImage->Resource.Get());
		}
		pending.ReadOnlyDepthTarget = depthOptions.bReadOnly ? framebuffer.Depth : nullptr;
		pending.ReadOnlyStencilTarget = depthOptions.bReadOnlyStencil ? framebuffer.Depth : nullptr;
		pending.List->OMSetRenderTargets(framebuffer.ColorCount, framebuffer.ColorCount != 0 ? views : nullptr, FALSE,
			framebuffer.Depth != nullptr ? &depthView : nullptr);
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(framebuffer.Width), static_cast<float>(framebuffer.Height), 0.0f, 1.0f };
		D3D12_RECT scissor{ 0, 0, static_cast<LONG>(framebuffer.Width), static_cast<LONG>(framebuffer.Height) };
		pending.List->RSSetViewports(1, &viewport);
		pending.List->RSSetScissorRects(1, &scissor);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordEndFramebuffer(void*, void* native, void* target)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Framebuffer& framebuffer = *static_cast<Framebuffer*>(target);
		if (pending.bPrepared)
		{
			pending.ReadOnlyDepthTarget = nullptr;
			pending.ReadOnlyStencilTarget = nullptr;

			return Result<void>::Success();
		}

		for (uint32 color = 0; color < framebuffer.ColorCount; ++color)
		{
			Transition(pending, *framebuffer.Colors[color]->Resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON,
				framebuffer.ColorSubresources[color]);
		}

		if (framebuffer.Depth != nullptr)
		{
			TextureSubresource plane = framebuffer.DepthSubresource;
			plane.Aspect = ETextureAspect::Depth;
			const D3D12_RESOURCE_STATES readState = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			TransitionDepthSubresource(pending, *framebuffer.Depth, plane,
				pending.ReadOnlyDepthTarget != nullptr ? readState : D3D12_RESOURCE_STATE_DEPTH_WRITE,
				D3D12_RESOURCE_STATE_COMMON);
			if (GetFormatInfo(framebuffer.Depth->Format).bHasStencil)
			{
				plane.Aspect = ETextureAspect::Stencil;
				TransitionDepthSubresource(pending, *framebuffer.Depth, plane,
					pending.ReadOnlyStencilTarget != nullptr ? readState : D3D12_RESOURCE_STATE_DEPTH_WRITE,
					D3D12_RESOURCE_STATE_COMMON);
			}
		}
		pending.ReadOnlyDepthTarget = nullptr;
		pending.ReadOnlyStencilTarget = nullptr;

		if (framebuffer.ShadingRateImage != nullptr && pending.ShadingRateList)
		{
			pending.ShadingRateList->RSSetShadingRateImage(nullptr);
		}

		if (framebuffer.ShadingRateImage != nullptr)
		{
			Transition(pending, *framebuffer.ShadingRateImage->Resource.Get(), D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE,
				D3D12_RESOURCE_STATE_COMMON);
		}

		return Result<void>::Success();
	}
}
#endif
