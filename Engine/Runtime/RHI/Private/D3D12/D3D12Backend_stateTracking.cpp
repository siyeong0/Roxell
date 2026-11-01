#include "D3D12Backend.h"

#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] UINT SubresourceCount(const D3D12_RESOURCE_DESC& desc) noexcept
	{
		const UINT planes = desc.Format == DXGI_FORMAT_R24G8_TYPELESS || desc.Format == DXGI_FORMAT_R32G8X24_TYPELESS ? 2u : 1u;

		return static_cast<UINT>(desc.MipLevels) * (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : desc.DepthOrArraySize) *
			planes;
	}

	[[nodiscard]] bool ReserveTrackedStates(Pending& pending, usize count) noexcept
	{
		return pending.ResourceStates.Reserve(count) && pending.ResourceStateLookup.Reserve(count);
	}

	[[nodiscard]] bool ReserveAdditionalTrackedState(Pending& pending) noexcept
	{
		const usize count = pending.ResourceStates.Size();

		return count != NumericLimits<usize>::max() && ReserveTrackedStates(pending, count + 1);
	}

	[[nodiscard]] usize TrackedStatePosition(const Pending& pending, const ID3D12Resource& resource, UINT subresource) noexcept
	{
		usize begin = 0;
		usize end = pending.ResourceStateLookup.Size();
		const Less<const ID3D12Resource*> less;
		while (begin < end)
		{
			const usize middle = begin + (end - begin) / 2;
			d3d12::Pending::ResourceState const& tracked = pending.ResourceStates[pending.ResourceStateLookup[middle]];
			if (less(tracked.Resource, &resource) || (tracked.Resource == &resource && tracked.Subresource < subresource))
			{
				begin = middle + 1;
			}
			else
			{
				end = middle;
			}
		}

		return begin;
	}

	[[nodiscard]] usize TrackedStateIndex(const Pending& pending, const ID3D12Resource& resource, UINT subresource) noexcept
	{
		const usize position = TrackedStatePosition(pending, resource, subresource);
		if (position < pending.ResourceStateLookup.Size())
		{
			unsigned long long const index = pending.ResourceStateLookup[position];
			d3d12::Pending::ResourceState const& tracked = pending.ResourceStates[index];
			if (tracked.Resource == &resource && tracked.Subresource == subresource)
			{
				return index;
			}
		}

		return NumericLimits<usize>::max();
	}

	[[nodiscard]] Pair<usize, usize> TrackedResourceRange(const Pending& pending, const ID3D12Resource& resource) noexcept
	{
		const usize begin = TrackedStatePosition(pending, resource, 0);
		const usize whole = TrackedStatePosition(pending, resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
		const usize end = whole < pending.ResourceStateLookup.Size() && pending.ResourceStates[pending.ResourceStateLookup[whole]].Resource == &resource ? whole + 1 : whole;

		return { begin, end };
	}

	[[nodiscard]] bool AddTrackedState(Pending& pending, ID3D12Resource& resource, UINT subresource, D3D12_RESOURCE_STATES state) noexcept
	{
		const usize count = pending.ResourceStates.Size();
		ASSERT(pending.ResourceStateLookup.Size() == count, "Tracked states retain one lookup row per state.");
		if (!ReserveAdditionalTrackedState(pending))
		{
			return false;
		}
		const usize position = TrackedStatePosition(pending, resource, subresource);
		pending.ResourceStates.AddRequired({ &resource, subresource, state });
		pending.ResourceStateLookup.InsertRequired(position, count);

		return true;
	}

	[[nodiscard]] D3D12_RESOURCE_STATES TrackedState(const Pending& pending, const ID3D12Resource& resource, UINT subresource) noexcept
	{
		const usize exact = TrackedStateIndex(pending, resource, subresource);
		if (exact != NumericLimits<usize>::max())
		{
			return pending.ResourceStates[exact].State;
		}
		if (subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			const usize whole = TrackedStateIndex(pending, resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
			if (whole != NumericLimits<usize>::max())
			{
				return pending.ResourceStates[whole].State;
			}
		}

		return D3D12_RESOURCE_STATE_COMMON;
	}

	void SeedTrackedState(Pending& pending, ID3D12Resource& resource, UINT subresource, D3D12_RESOURCE_STATES state) noexcept
	{
		const usize exact = TrackedStateIndex(pending, resource, subresource);
		if (exact == NumericLimits<usize>::max() && !ReserveAdditionalTrackedState(pending))
		{
			pending.bInvalidResourceState = true;

			return;
		}
		if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			const Pair<usize, usize> range = TrackedResourceRange(pending, resource);
			for (unsigned long long row = range.first; row < range.second; ++row)
			{
				pending.ResourceStates[pending.ResourceStateLookup[row]].State = state;
			}
		}
		if (exact != NumericLimits<usize>::max())
		{
			pending.ResourceStates[exact].State = state;
		}
		else if (!AddTrackedState(pending, resource, subresource, state))
		{
			pending.bInvalidResourceState = true;
		}
	}

	[[nodiscard]] ETrackedResourceState MapTrackedState(D3D12_RESOURCE_STATES native) noexcept
	{
		if (native == D3D12_RESOURCE_STATE_COMMON)
		{
			return ETrackedResourceState::Common;
		}

		uint32 bits = 0;
		const auto add = [&](D3D12_RESOURCE_STATES flag, ETrackedResourceState state) noexcept
			{
				if ((native & flag) != 0)
				{
					bits |= static_cast<uint32>(state);
				}
			};
		add(D3D12_RESOURCE_STATE_COPY_SOURCE, ETrackedResourceState::CopySource);
		add(D3D12_RESOURCE_STATE_COPY_DEST, ETrackedResourceState::CopyDestination);
		add(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, ETrackedResourceState::ShaderRead);
		add(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, ETrackedResourceState::ShaderRead);
		add(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, ETrackedResourceState::Storage);
		add(D3D12_RESOURCE_STATE_RENDER_TARGET, ETrackedResourceState::RenderTarget);
		add(D3D12_RESOURCE_STATE_DEPTH_WRITE, ETrackedResourceState::DepthWrite);
		add(D3D12_RESOURCE_STATE_DEPTH_READ, ETrackedResourceState::DepthRead);
		add(D3D12_RESOURCE_STATE_RESOLVE_SOURCE, ETrackedResourceState::ResolveSource);
		add(D3D12_RESOURCE_STATE_RESOLVE_DEST, ETrackedResourceState::ResolveDestination);
		add(D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE, ETrackedResourceState::ShadingRateSource);

		return static_cast<ETrackedResourceState>(bits);
	}

	[[nodiscard]] ETrackedResourceState GetTextureSubresourceState(const void*, const void* native, const void* textureNative, TextureSubresource subresource) noexcept
	{
		const Pending& pending = *static_cast<const Pending*>(native);
		const Texture& texture = *static_cast<const Texture*>(textureNative);
		const D3D12_RESOURCE_DESC desc = texture.Resource->GetDesc();
		const UINT layers = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : desc.DepthOrArraySize;
		const UINT index = subresource.MipLevel + subresource.ArrayLayer * static_cast<UINT>(desc.MipLevels);
		const D3D12_RESOURCE_STATES depth = TrackedState(pending, *texture.Resource.Get(), index);

		if (GetFormatInfo(texture.Format).bHasStencil)
		{
			const D3D12_RESOURCE_STATES stencil = TrackedState(pending, *texture.Resource.Get(), index + static_cast<UINT>(desc.MipLevels) * layers);

			if (depth != stencil)
			{
				return ETrackedResourceState::Unknown;
			}
		}

		return MapTrackedState(depth);
	}

	void EmitTransition(Pending& pending, ID3D12Resource& resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource) noexcept
	{
		if (before == after)
		{
			return;
		}

		if (before == D3D12_RESOURCE_STATE_COMMON && resource.GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		{
			return;
		}
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = &resource;
		barrier.Transition.Subresource = subresource;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		RecordBarriers(pending, 1, &barrier);
	}

	void EmitUavBarrier(Pending& pending, ID3D12Resource& resource) noexcept
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = &resource;
		RecordBarriers(pending, 1, &barrier);
	}

	[[nodiscard]] Result<void> SetUavBarrierPolicy(Pending& pending, ID3D12Resource& resource, bool bEnabled)
	{
		for (Pending::UavBarrierPolicy& policy : pending.UavPolicies)
		{
			if (policy.Resource != &resource)
			{
				continue;
			}
			policy.bEnabled = bEnabled;
			policy.bFirstBarrierPlaced = false;

			return Result<void>::Success();
		}

		if (!pending.UavPolicies.Add({ &resource, bEnabled }))
		{
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not record the UAV barrier policy.");
		}

		return Result<void>::Success();
	}

	[[nodiscard]] bool ShouldEmitUavBarrier(Pending& pending, ID3D12Resource& resource) noexcept
	{
		for (Pending::UavBarrierPolicy& policy : pending.UavPolicies)
		{
			if (policy.Resource != &resource)
			{
				continue;
			}

			if (!policy.bEnabled && policy.bFirstBarrierPlaced && !pending.bExplicitStateRequest)
			{
				return false;
			}
			policy.bFirstBarrierPlaced = true;

			return true;
		}

		return true;
	}

	[[nodiscard]] bool SatisfiesManualState(D3D12_RESOURCE_STATES tracked, D3D12_RESOURCE_STATES required) noexcept
	{
		return tracked == required ||
			(tracked == (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) && required == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	void TransitionTracked(Pending& pending, ID3D12Resource& resource, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES after, UINT subresource) noexcept
	{
		if (after == D3D12_RESOURCE_STATE_COMMON)
		{
			return;
		}

		if (!pending.bAutomaticBarriers && !pending.bExplicitStateRequest)
		{
			if (subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
			{
				if (subresource >= SubresourceCount(desc) || !SatisfiesManualState(TrackedState(pending, resource, subresource), after))
				{
					pending.bMissingManualState = true;
					pending.bInvalidResourceState = true;
				}

				return;
			}

			for (UINT index = 0; index < SubresourceCount(desc); ++index)
			{
				if (!SatisfiesManualState(TrackedState(pending, resource, index), after))
				{
					pending.bMissingManualState = true;
					pending.bInvalidResourceState = true;

					return;
				}
			}

			return;
		}

		if (subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			if (subresource >= SubresourceCount(desc))
			{
				pending.bInvalidResourceState = true;

				return;
			}

			const D3D12_RESOURCE_STATES before = TrackedState(pending, resource, subresource);
			const usize exact = TrackedStateIndex(pending, resource, subresource);
			if (exact != NumericLimits<usize>::max())
			{
				pending.ResourceStates[exact].State = after;
			}
			else if (!AddTrackedState(pending, resource, subresource, after))
			{
				pending.bInvalidResourceState = true;

				return;
			}
			EmitTransition(pending, resource, before, after, subresource);

			if (before == after && after == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && ShouldEmitUavBarrier(pending, resource))
			{
				EmitUavBarrier(pending, resource);
			}

			return;
		}

		const D3D12_RESOURCE_STATES baseline = TrackedState(pending, resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
		bool bWhole = false;
		bool bMixed = false;

		const Pair<usize, usize> range = TrackedResourceRange(pending, resource);
		for (unsigned long long row = range.first; row < range.second; ++row)
		{
			d3d12::Pending::ResourceState const& tracked = pending.ResourceStates[pending.ResourceStateLookup[row]];
			if (tracked.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
			{
				bWhole = true;
			}
			else if (tracked.State != baseline)
			{
				bMixed = true;
			}
		}
		if (!bWhole && !ReserveAdditionalTrackedState(pending))
		{
			pending.bInvalidResourceState = true;

			return;
		}
		bool bUavBarrier = false;

		if (!bMixed)
		{
			EmitTransition(pending, resource, baseline, after, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
			bUavBarrier = baseline == after;
		}
		else
		{
			for (UINT index = 0; index < SubresourceCount(desc); ++index)
			{
				const D3D12_RESOURCE_STATES before = TrackedState(pending, resource, index);
				EmitTransition(pending, resource, before, after, index);
				bUavBarrier |= before == after;
			}
		}

		if (bUavBarrier && after == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && ShouldEmitUavBarrier(pending, resource))
		{
			EmitUavBarrier(pending, resource);
		}

		for (unsigned long long row = range.first; row < range.second; ++row)
		{
			pending.ResourceStates[pending.ResourceStateLookup[row]].State = after;
		}
		if (!bWhole && !AddTrackedState(pending, resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, after))
		{
			pending.bInvalidResourceState = true;
		}
	}

	void Transition(Pending& pending, ID3D12Resource& resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource) noexcept
	{
		const D3D12_RESOURCE_DESC desc = resource.GetDesc();
		(void)before;
		TransitionTracked(pending, resource, desc, after, subresource);
	}

	void TransitionDepthSubresource(Pending& pending, Texture& texture, TextureSubresource subresource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) noexcept
	{
		ID3D12Resource& resource = *texture.Resource.Get();
		const D3D12_RESOURCE_DESC desc = resource.GetDesc();
		const UINT layerCount = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : desc.DepthOrArraySize;

		if (subresource.MipLevel >= desc.MipLevels || subresource.ArrayLayer >= layerCount)
		{
			pending.bInvalidResourceState = true;

			return;
		}

		const UINT planeSize = static_cast<UINT>(desc.MipLevels) * layerCount;
		const UINT index = subresource.MipLevel + subresource.ArrayLayer * static_cast<UINT>(desc.MipLevels);
		if (subresource.Aspect != ETextureAspect::Stencil)
		{
			Transition(pending, resource, before, after, index);
		}

		if (GetFormatInfo(texture.Format).bHasStencil && subresource.Aspect != ETextureAspect::Depth)
		{
			Transition(pending, resource, before, after, index + planeSize);
		}
	}

	void TransitionTextureView(Pending& pending, Texture& texture, const TextureView* view, D3D12_RESOURCE_STATES next) noexcept
	{
		ID3D12Resource& resource = *texture.Resource.Get();

		if (view == nullptr)
		{
			if (GetFormatInfo(texture.Format).bHasDepth)
			{
				const D3D12_RESOURCE_DESC native = resource.GetDesc();
				for (UINT layer = 0; layer < native.DepthOrArraySize; ++layer)
				{
					for (UINT mip = 0; mip < native.MipLevels; ++mip)
					{
						TransitionDepthSubresource(pending, texture, { mip, layer, ETextureAspect::Depth }, D3D12_RESOURCE_STATE_COMMON,
							next);
					}
				}
			}
			else
			{
				Transition(pending, resource, D3D12_RESOURCE_STATE_COMMON, next);
			}

			return;
		}

		const TextureViewDesc& range = view->Range;
		const D3D12_RESOURCE_DESC desc = resource.GetDesc();

		for (uint32 layer = range.BaseArrayLayer; layer < range.BaseArrayLayer + range.ArrayLayerCount; ++layer)
		{
			for (uint32 mip = range.BaseMipLevel; mip < range.BaseMipLevel + range.MipLevelCount; ++mip)
			{
				if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
				{
					TransitionDepthSubresource(pending, texture,
						{ mip, layer, range.Aspect == ETextureAspect::Default ? ETextureAspect::Depth : range.Aspect },
						D3D12_RESOURCE_STATE_COMMON, next);
				}
				else
				{
					Transition(pending, resource, D3D12_RESOURCE_STATE_COMMON, next, mip + layer * static_cast<UINT>(desc.MipLevels));
				}
			}
		}
	}

	void RestoreTrackedStates(Pending& pending) noexcept
	{
		// The existing lookup is ordered by resource and subresource. Visit each
		// resource once instead of rescanning the complete state journal per entry.
		for (usize begin = 0; begin < pending.ResourceStateLookup.Size();)
		{
			ID3D12Resource& resource = *pending.ResourceStates[pending.ResourceStateLookup[begin]].Resource;
			usize end = begin + 1;
			while (end < pending.ResourceStateLookup.Size() && pending.ResourceStates[pending.ResourceStateLookup[end]].Resource == &resource)
			{
				++end;
			}
			const usize first = begin;
			begin = end;
			const D3D12_RESOURCE_DESC desc = resource.GetDesc();
			if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			{
				continue;
			}

			d3d12::Pending::ResourceState const& last = pending.ResourceStates[pending.ResourceStateLookup[end - 1]];
			const D3D12_RESOURCE_STATES baseline = last.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ? last.State : D3D12_RESOURCE_STATE_COMMON;
			bool bMixed = false;
			for (usize row = first; row < end; ++row)
			{
				d3d12::Pending::ResourceState const& tracked = pending.ResourceStates[pending.ResourceStateLookup[row]];
				bMixed |= tracked.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES && tracked.State != baseline;
			}
			if (!bMixed)
			{
				EmitTransition(pending, resource, baseline, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
			}
			else if (baseline == D3D12_RESOURCE_STATE_COMMON)
			{
				for (usize row = first; row < end; ++row)
				{
					d3d12::Pending::ResourceState const& tracked = pending.ResourceStates[pending.ResourceStateLookup[row]];
					if (tracked.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
					{
						EmitTransition(pending, resource, tracked.State, D3D12_RESOURCE_STATE_COMMON, tracked.Subresource);
					}
				}
			}
			else
			{
				for (UINT subresource = 0; subresource < SubresourceCount(desc); ++subresource)
				{
					EmitTransition(pending, resource, TrackedState(pending, resource, subresource), D3D12_RESOURCE_STATE_COMMON,
						subresource);
				}
			}
		}
	}

	[[nodiscard]] Result<void> RecordShaderMemoryBarrier(void*, void* native)
	{
		Pending& pending = *static_cast<Pending*>(native);
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = nullptr;
		RecordBarriers(pending, 1, &barrier);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordShaderBufferBarrier(void*, void* native, void* bufferNative)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& buffer = *static_cast<Buffer*>(bufferNative);
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = buffer.Resource.Get();
		RecordBarriers(pending, 1, &barrier);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordShaderTextureBarrier(void*, void* native, void* textureNative)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(textureNative);
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = texture.Resource.Get();
		RecordBarriers(pending, 1, &barrier);

		return Result<void>::Success();
	}

	[[nodiscard]] D3D12_RESOURCE_STATES MapTextureState(ETextureState state, EQueue queue) noexcept
	{
		D3D12_RESOURCE_STATES next = D3D12_RESOURCE_STATE_COMMON;

		switch (state)
		{
		case ETextureState::CopySource:
			next = D3D12_RESOURCE_STATE_COPY_SOURCE;
			break;
		case ETextureState::CopyDestination:
			next = D3D12_RESOURCE_STATE_COPY_DEST;
			break;
		case ETextureState::ResolveSource:
			next = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			break;
		case ETextureState::ResolveDestination:
			next = D3D12_RESOURCE_STATE_RESOLVE_DEST;
			break;
		case ETextureState::ShaderRead:
			next = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

			if (queue == EQueue::Graphics)
			{
				next |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			}
			break;
		case ETextureState::Storage:
			next = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			break;
		case ETextureState::Present:
			next = D3D12_RESOURCE_STATE_PRESENT;
			break;
		case ETextureState::RenderTarget:
			next = D3D12_RESOURCE_STATE_RENDER_TARGET;
			break;
		case ETextureState::DepthWrite:
			next = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			break;
		case ETextureState::DepthRead:
			next = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			break;
		case ETextureState::ShadingRateSource:
			next = D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
			break;

		default:
		{
			break;
		}
		}

		return next;
	}

	[[nodiscard]] Result<void> RecordPrepareTextureForShaderAccess(void*, void* native, void* textureNative, const TextureViewDesc* range, ETextureState requested)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(textureNative);
		const D3D12_RESOURCE_STATES next = MapTextureState(requested, pending.Queue);
		pending.bExplicitStateRequest = true;

		if (range == nullptr)
		{
			Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, next);
		}
		else
		{
			const UINT mipLevels = texture.Resource->GetDesc().MipLevels;

			for (uint32 layer = range->BaseArrayLayer; layer < range->BaseArrayLayer + range->ArrayLayerCount; ++layer)
			{
				for (uint32 mip = range->BaseMipLevel; mip < range->BaseMipLevel + range->MipLevelCount; ++mip)
				{
					const TextureSubresource subresource{ mip, layer, range->Aspect };
					if ((texture.Resource->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
					{
						TransitionDepthSubresource(pending, texture, subresource, D3D12_RESOURCE_STATE_COMMON, next);
					}
					else
					{
						Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, next, mip + layer * mipLevels);
					}
				}
			}
		}
		pending.bExplicitStateRequest = false;

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12ShaderTexture", "Could not prepare the shader texture state."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordBeginTrackingTextureState(void*, void* native, void* textureNative, const TextureSubresource* subresource, ETextureState state)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(textureNative);
		const D3D12_RESOURCE_DESC desc = texture.Resource->GetDesc();
		const D3D12_RESOURCE_STATES next = MapTextureState(state, pending.Queue);

		if (subresource == nullptr)
		{
			SeedTrackedState(pending, *texture.Resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, next);
		}
		else
		{
			const UINT index = subresource->MipLevel + subresource->ArrayLayer * static_cast<UINT>(desc.MipLevels);
			if (subresource->Aspect != ETextureAspect::Stencil)
			{
				SeedTrackedState(pending, *texture.Resource.Get(), index, next);
			}

			if (GetFormatInfo(texture.Format).bHasStencil && subresource->Aspect != ETextureAspect::Depth)
			{
				const UINT layers = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : desc.DepthOrArraySize;
				SeedTrackedState(pending, *texture.Resource.Get(), index + static_cast<UINT>(desc.MipLevels) * layers, next);
			}
		}

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12TextureState", "Could not seed the texture state."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordSetTextureState(void*, void* native, void* textureNative, const TextureSubresource* subresource, ETextureState state)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Texture& texture = *static_cast<Texture*>(textureNative);
		const D3D12_RESOURCE_STATES next = MapTextureState(state, pending.Queue);
		pending.bExplicitStateRequest = true;

		if (subresource == nullptr)
		{
			Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, next);
		}
		else if ((texture.Resource->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
		{
			TransitionDepthSubresource(pending, texture, *subresource, D3D12_RESOURCE_STATE_COMMON, next);
		}
		else
		{
			const UINT index = subresource->MipLevel + subresource->ArrayLayer * texture.Resource->GetDesc().MipLevels;
			Transition(pending, *texture.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, next, index);
		}
		pending.bExplicitStateRequest = false;

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12TextureState", "Could not track the requested texture state."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] D3D12_RESOURCE_STATES MapBufferState(EBufferState state, EQueue queue) noexcept
	{
		D3D12_RESOURCE_STATES next = D3D12_RESOURCE_STATE_COMMON;

		switch (state)
		{
		case EBufferState::CopySource:
			next = D3D12_RESOURCE_STATE_COPY_SOURCE;
			break;
		case EBufferState::CopyDestination:
			next = D3D12_RESOURCE_STATE_COPY_DEST;
			break;
		case EBufferState::Uniform:
		case EBufferState::Vertex:
			next = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
			break;
		case EBufferState::AccelScratch:
		case EBufferState::Storage:
			next = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			break;
		case EBufferState::ReadOnlyStorage:
			next = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

			if (queue == EQueue::Graphics)
			{
				next |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			}
			break;
		case EBufferState::Index:
			next = D3D12_RESOURCE_STATE_INDEX_BUFFER;
			break;
		case EBufferState::VertexAndReadOnlyStorage:
			next = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			break;
		case EBufferState::VertexAndIndex:
			next = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
			break;
		case EBufferState::Indirect:
			next = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
			break;
		case EBufferState::AccelBuildInput:
			next = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			break;
		case EBufferState::AccelStorage:
			next = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
			break;

		default:
		{
			break;
		}
		}

		return next;
	}

	[[nodiscard]] Result<void> RecordBeginTrackingBufferState(void*, void* native, void* bufferNative, EBufferState state)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& buffer = *static_cast<Buffer*>(bufferNative);
		SeedTrackedState(pending, *buffer.Resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, MapBufferState(state, pending.Queue));

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12BufferState", "Could not seed the buffer state."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> RecordSetBufferState(void*, void* native, void* bufferNative, EBufferState state)
	{
		Pending& pending = *static_cast<Pending*>(native);
		Buffer& buffer = *static_cast<Buffer*>(bufferNative);
		const D3D12_RESOURCE_STATES next = MapBufferState(state, pending.Queue);
		pending.bExplicitStateRequest = true;
		Transition(pending, *buffer.Resource.Get(), D3D12_RESOURCE_STATE_COMMON, next);
		pending.bExplicitStateRequest = false;

		if (pending.bInvalidResourceState)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12BufferState", "Could not track the requested buffer state."));
		}

		return Result<void>::Success();
	}
}
#endif
