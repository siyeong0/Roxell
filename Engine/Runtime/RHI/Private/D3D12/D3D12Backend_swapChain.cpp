#include "D3D12Backend.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	void FreeSwapChainTargets(State& state, SwapChain& swapChain) noexcept
	{
		for (uint32 index = 0; index < 3; ++index)
		{
			if (swapChain.Targets[index])
			{
				DestroyObject(state.Resource, swapChain.Targets[index]);
			}
			swapChain.Targets[index] = nullptr;
			swapChain.Images[index].Reset();
		}
	}

	[[nodiscard]] bool AcquireSwapChainImages(State& state, SwapChain& swapChain) noexcept
	{
		for (uint32 index = 0; index < swapChain.BufferCount; ++index)
		{
			if (FAILED(swapChain.Native->GetBuffer(index, IID_PPV_ARGS(swapChain.Images[index].GetAddressOf()))))
			{
				FreeSwapChainTargets(state, swapChain);

				return false;
			}
			Texture* target = CreateObject<Texture>(state.Resource);

			if (target == nullptr)
			{
				FreeSwapChainTargets(state, swapChain);

				return false;
			}
			swapChain.Targets[index] = target;
			target->Resource = swapChain.Images[index];
			target->bSwapChainImage = true;
			target->Format = swapChain.Format;
			D3D12_DESCRIPTOR_HEAP_DESC heap{};
			heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heap.NumDescriptors = 1;

			if (FAILED(state.Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(target->RenderTargetView.GetAddressOf()))))
			{
				FreeSwapChainTargets(state, swapChain);

				return false;
			}
			state.Device->CreateRenderTargetView(target->Resource.Get(), nullptr,
				target->RenderTargetView->GetCPUDescriptorHandleForHeapStart());
		}

		return true;
	}

	[[nodiscard]] Result<void*> CreateSwapChain(State* opaque, const SwapChainDesc& desc)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		if (desc.WindowKind != ENativeWindowKind::Win32)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Present", "D3D12 presentation requires a Win32 window.");
		}
		HWND window = reinterpret_cast<HWND>(desc.Window);

		if (IsWindow(window) == FALSE)
		{
			return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.NativeWindow", "The Win32 window is invalid.");
		}
		ComPtr<IDXGIFactory6> factory;

		if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf()))))
		{
			return Result<void*>::Failure(Failure("Rhi.D3D12Present", "Could not create a DXGI factory."));
		}
		DXGI_SWAP_CHAIN_DESC1 nativeDesc{};
		nativeDesc.Width = desc.Width;
		nativeDesc.Height = desc.Height;
		nativeDesc.Format = GetDxgiFormatMapping(desc.Format).RtvFormat;
		nativeDesc.SampleDesc.Count = 1;
		nativeDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		nativeDesc.BufferCount = desc.BufferCount;
		nativeDesc.Scaling = DXGI_SCALING_STRETCH;
		nativeDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		nativeDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		ComPtr<IDXGISwapChain1> created;

		if (FAILED(factory->CreateSwapChainForHwnd(state.Queue.Get(), window, &nativeDesc, nullptr, nullptr, created.GetAddressOf())))
		{
			return Result<void*>::Failure(Failure("Rhi.D3D12Present", "Could not create a window swapchain."));
		}
		SwapChain* swapChain = CreateObject<SwapChain>(state.Resource);

		if (swapChain == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a swapchain record."));
		}
		swapChain->Width = desc.Width;
		swapChain->Height = desc.Height;
		swapChain->BufferCount = desc.BufferCount;
		swapChain->bVSync = desc.bVSync;
		swapChain->Format = desc.Format;

		if (FAILED(created.As(&swapChain->Native)) || !AcquireSwapChainImages(state, *swapChain))
		{
			DestroyObject(state.Resource, swapChain);

			return Result<void*>::Failure(Failure("Rhi.D3D12Present", "Could not access swapchain images."));
		}

		const DXGI_COLOR_SPACE_TYPE colorSpace = desc.ColorSpace == EColorSpace::LinearSrgb ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
			: desc.ColorSpace == EColorSpace::Hdr10PqRec2020
			? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
			: DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
		UINT support = 0;

		if (FAILED(swapChain->Native->CheckColorSpaceSupport(colorSpace, &support)) ||
			(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0 ||
			FAILED(swapChain->Native->SetColorSpace1(colorSpace)))
		{
			FreeSwapChainTargets(state, *swapChain);
			DestroyObject(state.Resource, swapChain);

			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Present", "The requested display color space is unavailable.");
		}

		if (desc.bEnableHdrMetadata)
		{
			const HdrMetadata& source = desc.Metadata;
			DXGI_HDR_METADATA_HDR10 metadata{};
			for (uint32 index = 0; index < 2; ++index)
			{
				metadata.RedPrimary[index] = static_cast<UINT16>(source.RedPrimary[index] * 50000.0f + 0.5f);
				metadata.GreenPrimary[index] = static_cast<UINT16>(source.GreenPrimary[index] * 50000.0f + 0.5f);
				metadata.BluePrimary[index] = static_cast<UINT16>(source.BluePrimary[index] * 50000.0f + 0.5f);
				metadata.WhitePoint[index] = static_cast<UINT16>(source.WhitePoint[index] * 50000.0f + 0.5f);
			}
			// DXGI uses whole nits for the maximum, but 0.0001 nits for the minimum.
			metadata.MaxMasteringLuminance = static_cast<UINT>(source.MaximumLuminance + 0.5f);
			metadata.MinMasteringLuminance = static_cast<UINT>(source.MinimumLuminance * 10000.0f + 0.5f);
			metadata.MaxContentLightLevel = static_cast<UINT16>(source.MaximumContentLightLevel + 0.5f);
			metadata.MaxFrameAverageLightLevel = static_cast<UINT16>(source.MaximumFrameAverageLightLevel + 0.5f);

			if (FAILED(swapChain->Native->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata)))
			{
				FreeSwapChainTargets(state, *swapChain);
				DestroyObject(state.Resource, swapChain);

				return Result<void*>::Failure(Failure("Rhi.D3D12Present", "Could not apply HDR metadata."));
			}
		}
		(void)factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

		return Result<void*>::Success(swapChain);
	}

	[[nodiscard]] Result<void> ResizeSwapChainWithPolicy(State* opaque, void* native, uint32 width, uint32 height, bool bPoll)
	{
		State& state = *opaque;
		SwapChain& swapChain = *static_cast<SwapChain*>(native);
		// A queued resize may precede the current client extent; DXGI scales the buffers.
		Result<void> idle = bPoll ? WaitIdleBounded(&state, 0) : WaitIdle(&state);

		if (!idle)
		{
			return idle;
		}
		FreeSwapChainTargets(state, swapChain);

		if (FAILED(
			swapChain.Native->ResizeBuffers(swapChain.BufferCount, width, height, GetDxgiFormatMapping(swapChain.Format).RtvFormat, 0)))
		{
			swapChain.bUsable = AcquireSwapChainImages(state, swapChain);

			return Result<void>::Failure(Failure("Rhi.D3D12Present", "Could not resize the window swapchain."));
		}
		swapChain.Width = width;
		swapChain.Height = height;

		if (!AcquireSwapChainImages(state, swapChain))
		{
			swapChain.bUsable = false;

			return Result<void>::Failure(Failure("Rhi.D3D12Present", "Could not access resized swapchain images."));
		}
		swapChain.bUsable = true;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> ResizeSwapChain(State* opaque, void* native, uint32 width, uint32 height)
	{
		return ResizeSwapChainWithPolicy(opaque, native, width, height, false);
	}

	[[nodiscard]] Result<void> TryResizeSwapChain(State* opaque, void* native, uint32 width, uint32 height)
	{
		return ResizeSwapChainWithPolicy(opaque, native, width, height, true);
	}

	[[nodiscard]] Result<uint64> Present(State* opaque, void* native, void* sourceNative)
	{
		State& state = *opaque;
		state.PresentCopySerial = 0;
		state.bSubmissionMayBePending = false;
		InvalidateIdle(state);
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<uint64>::Failure(active.GetError());
		}

		SwapChain& swapChain = *static_cast<SwapChain*>(native);
		Texture& source = *static_cast<Texture*>(sourceNative);

		if (!swapChain.bUsable || swapChain.bAcquired)
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12Present", "The swapchain needs to be resized or recreated."));
		}

		const uint32 imageIndex = swapChain.Native->GetCurrentBackBufferIndex();

		if (imageIndex >= swapChain.BufferCount || !swapChain.Images[imageIndex] || state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL - 2)
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12Present", "The swapchain image is unavailable."));
		}

		Result<Pending> started = Begin(state);

		if (!started)
		{
			return Result<uint64>::Failure(started.GetError());
		}
		Pending pending = Move(started).TakeValue();
		ID3D12Resource& image = *swapChain.Images[imageIndex].Get();
		Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
		Transition(pending, image, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
		pending.List->CopyResource(&image, source.Resource.Get());
		Transition(pending, image, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
		Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
		Result<uint64> submitted = Execute(state, Move(pending));

		if (!submitted)
		{
			return submitted;
		}
		RecordResourceUse(source, EQueue::Graphics, submitted.Value());
		state.PresentCopySerial = submitted.Value();
		if (source.bUnknownAliases)
		{
			state.UnknownResourceUses[static_cast<uint32>(EQueue::Graphics)] = submitted.Value();
		}

		if (!state.Submissions.TryReserveOne())
		{
			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not track D3D12 presentation completion."));
		}

		if (FAILED(swapChain.Native->Present(swapChain.bVSync ? 1 : 0, 0)))
		{
			swapChain.bUsable = false;

			return Result<uint64>::Failure(Failure("Rhi.D3D12Present", "DXGI presentation failed."));
		}

		const uint64 presentSerial = state.LastSerial + 1;

		if (FAILED(state.Queue->Signal(state.Fence.Get(), presentSerial)))
		{
			state.bLost = true;

			return Result<uint64>::Failure(Failure("Rhi.D3D12Present", "Could not signal presentation completion."));
		}
		state.LastSerial = presentSerial;
		state.Submissions.RecordSubmission(presentSerial, SubmissionCompletion::ELane::Direct);
		state.LastDirectSerial = presentSerial;

		return Result<uint64>::Success(presentSerial);
	}

	[[nodiscard]] Result<void*> AcquireSwapChainImage(State* opaque, void* native, uint32)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		SwapChain& swapChain = *static_cast<SwapChain*>(native);

		if (!swapChain.bUsable || swapChain.bAcquired)
		{
			return Result<void*>::Failure(Failure("Rhi.D3D12Present", "The swapchain image is unavailable."));
		}

		const uint32 index = swapChain.Native->GetCurrentBackBufferIndex();

		if (index >= swapChain.BufferCount || swapChain.Targets[index] == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.D3D12Present", "The backbuffer render target is unavailable."));
		}
		swapChain.AcquiredIndex = index;
		swapChain.bAcquired = true;

		return Result<void*>::Success(swapChain.Targets[index]);
	}

	[[nodiscard]] Result<void> DiscardAcquiredSwapChainImage(void*, void* native)
	{
		SwapChain& swapChain = *static_cast<SwapChain*>(native);
		ASSERT(swapChain.bAcquired, "No swapchain image is acquired.");
		swapChain.bAcquired = false;
		swapChain.bUsable = false;

		return Result<void>::Success();
	}

	[[nodiscard]] Result<uint64> PresentSwapChainImage(State* opaque, void* native)
	{
		State& state = *opaque;
		state.PresentCopySerial = 0;
		InvalidateIdle(state);
		SwapChain& swapChain = *static_cast<SwapChain*>(native);

		struct CloseFailedAcquisition final
		{
			SwapChain& Owner;
			bool bSucceeded = false;

			~CloseFailedAcquisition() noexcept
			{
				if (!bSucceeded)
				{
					Owner.bAcquired = false;
					Owner.bUsable = false;
				}
			}
		} close{ swapChain };

		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<uint64>::Failure(active.GetError());
		}

		if (!swapChain.bUsable || !swapChain.bAcquired || state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL)
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12Present", "The acquired image is unavailable."));
		}

		if (!state.Submissions.TryReserveOne())
		{
			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not track D3D12 presentation completion."));
		}
		swapChain.bAcquired = false;

		if (state.LastComputeSerial != 0 && FAILED(state.Queue->Wait(state.ComputeFence.Get(), state.LastComputeSerial)))
		{
			state.bLost = true;

			return Result<uint64>::Failure(Failure("Rhi.D3D12Present", "Could not order presentation after compute work."));
		}

		if (state.LastCopySerial != 0 && FAILED(state.Queue->Wait(state.CopyFence.Get(), state.LastCopySerial)))
		{
			state.bLost = true;

			return Result<uint64>::Failure(Failure("Rhi.D3D12Present", "Could not order presentation after copy work."));
		}

		if (FAILED(swapChain.Native->Present(swapChain.bVSync ? 1 : 0, 0)))
		{
			swapChain.bUsable = false;

			return Result<uint64>::Failure(Failure("Rhi.D3D12Present", "DXGI presentation failed."));
		}

		const uint64 serial = state.LastSerial + 1;

		if (FAILED(state.Queue->Signal(state.Fence.Get(), serial)))
		{
			state.bLost = true;

			return Result<uint64>::Failure(Failure("Rhi.D3D12Present", "Could not signal presentation completion."));
		}
		state.LastSerial = serial;
		state.Submissions.RecordSubmission(serial, SubmissionCompletion::ELane::Direct);
		state.LastDirectSerial = serial;
		close.bSucceeded = true;

		return Result<uint64>::Success(serial);
	}

	void DestroySwapChainNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		SwapChain& swapChain = *static_cast<SwapChain*>(native);
		FreeSwapChainTargets(state, swapChain);
		DestroyObject(state.Resource, &swapChain);
	}

	[[nodiscard]] Result<void> DestroySwapChain(State* opaque, void* native)
	{
		Result<void> idle = WaitIdle(opaque);

		if (!idle)
		{
			return idle;
		}
		DestroySwapChainNow(opaque, native);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> TryDestroySwapChain(State* opaque, void* native)
	{
		Result<void> idle = WaitIdleBounded(opaque, 0);
		if (!idle)
		{
			return idle;
		}
		DestroySwapChainNow(opaque, native);

		return Result<void>::Success();
	}
}
#endif
