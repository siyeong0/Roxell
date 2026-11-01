#include "D3D12Backend.h"

#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<void*> WrapSamplerFeedbackTexture(State& state, Texture& pairedTexture, ID3D12Resource* resource)
	{
		Texture* const texture = CreateObject<Texture>(state.Resource);

		if (texture == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a D3D12 sampler feedback record."));
		}
		texture->Resource = resource;
		D3D12_DESCRIPTOR_HEAP_DESC viewDesc{};
		viewDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		viewDesc.NumDescriptors = 1;

		if (FAILED(state.Device->CreateDescriptorHeap(&viewDesc, IID_PPV_ARGS(texture->StorageView.GetAddressOf()))))
		{
			DestroyObject(state.Resource, texture);

			return Result<void*>::Failure(Failure("Rhi.D3D12SamplerFeedback", "Could not create a sampler feedback binding descriptor."));
		}
		state.FeedbackDevice->CreateSamplerFeedbackUnorderedAccessView(pairedTexture.Resource.Get(), texture->Resource.Get(),
			texture->StorageView->GetCPUDescriptorHandleForHeapStart());
		texture->FeedbackPair = pairedTexture.Resource;

		return Result<void*>::Success(texture);
	}

	[[nodiscard]] Result<void*> CreateSamplerFeedbackTexture(State* opaque, void* paired, const TextureDesc&, const SamplerFeedbackDesc& desc)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		if (!state.FeedbackDevice)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.SamplerFeedback", "The D3D12 adapter does not support sampler feedback.");
		}

		Texture& pairedTexture = *static_cast<Texture*>(paired);
		const D3D12_RESOURCE_DESC pair = pairedTexture.Resource->GetDesc();
		D3D12_RESOURCE_DESC1 resource{};
		resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resource.Alignment = pair.Alignment;
		resource.Width = pair.Width;
		resource.Height = pair.Height;
		resource.DepthOrArraySize = pair.DepthOrArraySize;
		resource.MipLevels = pair.MipLevels;
		resource.Format = desc.Format == ESamplerFeedbackFormat::MinMip ? DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE : DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE;
		resource.SampleDesc = { 1, 0 };
		resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		resource.SamplerFeedbackMipRegion = { desc.RegionWidth, desc.RegionHeight, 1 };
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		ComPtr<ID3D12Resource> native;

		if (FAILED(state.FeedbackDevice->CreateCommittedResource2(&heap, D3D12_HEAP_FLAG_NONE, &resource, D3D12_RESOURCE_STATE_COMMON,
			nullptr, nullptr, IID_PPV_ARGS(native.GetAddressOf()))))
		{
			return Result<void*>::Failure(Failure("Rhi.D3D12SamplerFeedback", "Could not create a D3D12 sampler feedback texture."));
		}

		return WrapSamplerFeedbackTexture(state, pairedTexture, native.Get());
	}

	[[nodiscard]] Result<void*> ImportSamplerFeedbackTexture(State* opaque, void* paired, const TextureDesc&, const SamplerFeedbackDesc& desc, void* native)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		if (!state.FeedbackDevice)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.SamplerFeedback", "The D3D12 adapter does not support sampler feedback.");
		}

		Texture& pairedTexture = *static_cast<Texture*>(paired);
		ID3D12Resource* resource = static_cast<ID3D12Resource*>(native);
		ComPtr<ID3D12Resource2> resource2;
		ComPtr<ID3D12Device> owner;

		if (FAILED(resource->QueryInterface(IID_PPV_ARGS(resource2.GetAddressOf()))) || FAILED(resource->GetDevice(IID_PPV_ARGS(owner.GetAddressOf()))) || owner.Get() != state.Device.Get())
		{
			return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.SamplerFeedback", "The native feedback texture is not a resource of this D3D12 device.");
		}

		const D3D12_RESOURCE_DESC pair = pairedTexture.Resource->GetDesc();
		const D3D12_RESOURCE_DESC1 actual = resource2->GetDesc1();
		const DXGI_FORMAT expectedFormat = desc.Format == ESamplerFeedbackFormat::MinMip ? DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE : DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE;

		if (actual.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			actual.Width != pair.Width ||
			actual.Height != pair.Height ||
			actual.DepthOrArraySize != pair.DepthOrArraySize ||
			actual.MipLevels != pair.MipLevels ||
			actual.Format != expectedFormat ||
			actual.SampleDesc.Count != 1 ||
			(actual.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0 ||
			actual.SamplerFeedbackMipRegion.Width != desc.RegionWidth ||
			actual.SamplerFeedbackMipRegion.Height != desc.RegionHeight ||
			actual.SamplerFeedbackMipRegion.Depth != 1)
		{
			return Result<void*>::Failure(EErrorCode::InvalidArgument, "Rhi.SamplerFeedback", "The native feedback resource does not match its pair and descriptor.");
		}

		return WrapSamplerFeedbackTexture(state, pairedTexture, resource);
	}

	[[nodiscard]] Result<void> RecordSetSamplerFeedbackState(void*, void* native, void* feedback, ESamplerFeedbackState state)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(feedback);
		D3D12_RESOURCE_STATES next = D3D12_RESOURCE_STATE_COMMON;

		switch (state)
		{
		case ESamplerFeedbackState::Storage:
			next = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			break;
		case ESamplerFeedbackState::ResolveSource:
			next = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			break;
		case ESamplerFeedbackState::ResolveDestination:
			next = D3D12_RESOURCE_STATE_RESOLVE_DEST;
			break;
		case ESamplerFeedbackState::CopySource:
			next = D3D12_RESOURCE_STATE_COPY_SOURCE;
			break;
		case ESamplerFeedbackState::CopyDestination:
			next = D3D12_RESOURCE_STATE_COPY_DEST;
			break;

		default:
		{
			break;
		}
		}
		pending.bExplicitStateRequest = true;
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, next);
		pending.bExplicitStateRequest = false;

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12SamplerFeedback", "Could not track the sampler feedback state."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordSamplerFeedbackClear(State* opaque, void* native, void* feedback)
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(feedback);
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		ComPtr<ID3D12DescriptorHeap> cpuHeap;

		if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(cpuHeap.GetAddressOf()))))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12SamplerFeedback", "Could not create a sampler feedback clear descriptor."));
		}
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ComPtr<ID3D12DescriptorHeap> gpuHeap;

		if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(gpuHeap.GetAddressOf()))))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12SamplerFeedback", "Could not create a visible sampler feedback clear descriptor."));
		}

		const D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpuHeap->GetCPUDescriptorHandleForHeapStart();
		state.FeedbackDevice->CreateSamplerFeedbackUnorderedAccessView(texture.FeedbackPair.Get(), texture.Resource.Get(), cpu);
		CopyCommandDescriptors(state, pending, 1, gpuHeap->GetCPUDescriptorHandleForHeapStart(), cpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		if (!pending.AttachmentHeaps.Reserve(pending.AttachmentHeaps.Size() + 2))
		{
			return Result<void>::Failure(Failure("Rhi.Allocation", "Could not retain sampler feedback clear descriptors."));
		}
		(void)pending.AttachmentHeaps.Add(Move(cpuHeap));
		(void)pending.AttachmentHeaps.Add(Move(gpuHeap));
		ID3D12DescriptorHeap* const visible = pending.AttachmentHeaps.Back().Get();
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		pending.List->SetDescriptorHeaps(1, &visible);
		pending.BoundTextureHeap = visible;
		pending.BoundSamplerHeap = nullptr;
		pending.LastTexturePipeline = nullptr;
		const UINT clear[4]{ 0, 0, 0, 0 };
		pending.List->ClearUnorderedAccessViewUint(visible->GetGPUDescriptorHandleForHeapStart(), cpu, texture.Resource.Get(), clear, 0,
			nullptr);
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordSamplerFeedbackDecode(void*, void* native, void* feedback, void* destination)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(feedback);
		Buffer& buffer = *static_cast<Buffer*>(destination);

		if (!pending.ResolveList && FAILED(pending.List.As(&pending.ResolveList)))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12SamplerFeedback", "The D3D12 command list does not support feedback decode.");
		}
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = buffer.Resource.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_DEST;
		RecordBarriers(pending, 1, &barrier);
		pending.ResolveList->ResolveSubresourceRegion(buffer.Resource.Get(), 0, 0, 0, texture.Resource.Get(), UINT_MAX, nullptr,
			DXGI_FORMAT_R8_UINT, D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK);
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
		RecordBarriers(pending, 1, &barrier);
		Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_COMMON);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordSamplerFeedbackDecodeTexture(void*, void* native, void* feedback, void* destination)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Texture& source = *static_cast<Texture*>(feedback);
		Texture& target = *static_cast<Texture*>(destination);

		if (!pending.ResolveList && FAILED(pending.List.As(&pending.ResolveList)))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.D3D12SamplerFeedback", "The D3D12 command list does not support feedback decode.");
		}
		Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
		Transition(pending, *target.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RESOLVE_DEST);
		pending.ResolveList->ResolveSubresourceRegion(target.Resource.Get(), UINT_MAX, 0, 0, source.Resource.Get(), UINT_MAX, nullptr,
			DXGI_FORMAT_R8_UINT, D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK);
		Transition(pending, *source.Resource.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_COMMON);
		Transition(pending, *target.Resource.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COMMON);

		return Result<void>::Success();
	}
}
#endif
