#include "D3D12Backend.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] Result<uint32> QueryMemoryBudget(const State* opaque, Span<MemoryBudget> budgets)
	{
		const State& state = *opaque;

		if (state.bLost)
		{
			return Result<uint32>::Failure(Failure("Rhi.DeviceLost", "The D3D12 device is unavailable."));
		}

		if (!state.BudgetAdapter)
		{
			return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.MemoryBudget", "The DXGI adapter does not expose native memory budgets.");
		}
		constexpr uint32 COUNT = 2;

		if (budgets.Size() != 0 && budgets.Size() < COUNT)
		{
			return Result<uint32>::Failure(EErrorCode::InvalidArgument, "Rhi.MemoryBudget", "The output span cannot hold every native memory segment.");
		}
		MemoryBudget snapshot[COUNT]{};
		for (uint32 index = 0; index < COUNT; ++index)
		{
			DXGI_QUERY_VIDEO_MEMORY_INFO info{};
			const DXGI_MEMORY_SEGMENT_GROUP segment = index == 0 ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL;
			const HRESULT queried = state.BudgetAdapter->QueryVideoMemoryInfo(0, segment, &info);

			if (queried == E_NOINTERFACE || queried == DXGI_ERROR_UNSUPPORTED)
			{
				return Result<uint32>::Failure(EErrorCode::Unsupported, "Rhi.MemoryBudget", "The native memory segment budget is unavailable.");
			}

			if (FAILED(queried))
			{
				return Result<uint32>::Failure(Failure("Rhi.MemoryBudget", "Could not query the native memory segment budget."));
			}
			snapshot[index] = { index, index == 0, EMemoryBudgetAccuracy::DriverEstimate, info.Budget, info.CurrentUsage };
		}

		if (budgets.Size() != 0)
		{
			for (uint32 index = 0; index < COUNT; ++index)
			{
				budgets[index] = snapshot[index];
			}
		}

		return Result<uint32>::Success(COUNT);
	}

	[[nodiscard]] D3D12_RESOURCE_DESC MakeTextureResourceDesc(const TextureDesc& desc, DXGI_FORMAT format) noexcept
	{
		D3D12_RESOURCE_DESC resource{};
		resource.Dimension = desc.Dimension == ETextureDimension::Texture3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
			: desc.Dimension == ETextureDimension::Texture1D ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
			: D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resource.Width = desc.Width;
		resource.Height = desc.Height;
		resource.DepthOrArraySize = static_cast<UINT16>(desc.Dimension == ETextureDimension::Texture3D ? desc.Depth : desc.ArrayLayers);
		resource.MipLevels = static_cast<UINT16>(desc.MipLevels);
		resource.Format = format;
		resource.SampleDesc.Count = desc.SampleCount;
		resource.Layout = desc.bSparse ? D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE : D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resource.Alignment = desc.bSparse ? D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT : 0;

		if (HasUsage(desc.Usage, ETextureUsage::RenderTarget))
		{
			resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}

		if (HasUsage(desc.Usage, ETextureUsage::Storage))
		{
			resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		if (HasUsage(desc.Usage, ETextureUsage::DepthStencil))
		{
			resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		}

		return resource;
	}

	[[nodiscard]] Result<MemoryRequirements> GetTextureMemoryRequirements(const State* opaque, const TextureDesc& desc)
	{
		const State& state = *opaque;
		const DxgiFormatMapping& mapping = GetDxgiFormatMapping(desc.Format);
		const bool bDepth = HasUsage(desc.Usage, ETextureUsage::DepthStencil);
		const DXGI_FORMAT format = bDepth || desc.bMutableFormat ? mapping.ResourceFormat : mapping.SrvFormat;

		if (format == DXGI_FORMAT_UNKNOWN || !SupportsSamples(state, bDepth ? mapping.RtvFormat : mapping.SrvFormat, desc.SampleCount))
		{
			return Result<MemoryRequirements>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Texture", "The texture format or sample count is unavailable.");
		}

		const D3D12_RESOURCE_DESC resource = MakeTextureResourceDesc(desc, format);
		const D3D12_RESOURCE_ALLOCATION_INFO info = state.Device->GetResourceAllocationInfo(0, 1, &resource);

		if (info.SizeInBytes == NumericLimits<uint64>::max() || info.Alignment == 0)
		{
			return Result<MemoryRequirements>::Failure(Failure("Rhi.D3D12Memory", "Could not query texture memory requirements."));
		}

		const uint32 budgetIndex = MemoryBudgetIndex(state, EBufferMemory::Device);

		return Result<MemoryRequirements>::Success({ info.SizeInBytes, info.Alignment, false, false, budgetIndex, budgetIndex });
	}

	[[nodiscard]] Result<void*> CreateHeap(State* opaque, const HeapDesc& desc)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}
		Heap* heap = CreateObject<Heap>(state.Resource);

		if (heap == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 heap state."));
		}
		heap->Descriptor = desc;
		D3D12_HEAP_DESC native{};
		native.SizeInBytes = desc.CapacityBytes;
		native.Alignment = desc.Usage == EHeapUsage::Attachments ? D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		native.Properties.Type = desc.Memory == EBufferMemory::CpuWrite ? D3D12_HEAP_TYPE_UPLOAD : desc.Memory == EBufferMemory::CpuRead ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT;
		native.Properties.CreationNodeMask = 1;
		native.Properties.VisibleNodeMask = 1;
		native.Flags = desc.Usage == EHeapUsage::Buffers ? D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS
			: desc.Usage == EHeapUsage::Textures ? D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES
			: D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;

		if (FAILED(state.Device->CreateHeap(&native, IID_PPV_ARGS(heap->Native.GetAddressOf()))))
		{
			DestroyObject(state.Resource, heap);

			return Result<void*>::Failure(Failure("Rhi.D3D12Heap", "Could not create a D3D12 buffer heap."));
		}

		return Result<void*>::Success(heap);
	}

	[[nodiscard]] uint64 GetHeapCommittedBytes(const void* native) noexcept
	{
		const Heap& heap = *static_cast<const Heap*>(native);

		return heap.Native.Get() != nullptr ? heap.Native->GetDesc().SizeInBytes : 0;
	}

	void DestroyHeapNow(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		DestroyObject(state.Resource, static_cast<Heap*>(native));
	}

	[[nodiscard]] Result<void> DestroyHeap(State* opaque, void* native)
	{
		DestroyHeapNow(opaque, native);

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void*> CreatePlacedBuffer(State* opaque, const BufferDesc& desc, void* nativeHeap, uint64 offset)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		Heap& heap = *static_cast<Heap*>(nativeHeap);
		Buffer* buffer = CreateObject<Buffer>(state.Resource);

		if (buffer == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate placed buffer state."));
		}
		buffer->Memory = desc.Memory;
		buffer->bStorage = HasUsage(desc.Usage, EBufferUsage::Storage);
		D3D12_RESOURCE_DESC resource{};
		resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resource.Width = desc.ByteCount;
		resource.Height = 1;
		resource.DepthOrArraySize = 1;
		resource.MipLevels = 1;
		resource.SampleDesc.Count = 1;
		resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resource.Flags = HasUsage(desc.Usage, EBufferUsage::Storage) ||
			(HasUsage(desc.Usage, EBufferUsage::AccelStorage) || HasUsage(desc.Usage, EBufferUsage::MicromapStorage))
			? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			: D3D12_RESOURCE_FLAG_NONE;
		const D3D12_RESOURCE_STATES initial =
			(HasUsage(desc.Usage, EBufferUsage::AccelStorage) || HasUsage(desc.Usage, EBufferUsage::MicromapStorage))
			? D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
			: desc.Memory == EBufferMemory::CpuWrite ? D3D12_RESOURCE_STATE_GENERIC_READ
			: desc.Memory == EBufferMemory::CpuRead ? D3D12_RESOURCE_STATE_COPY_DEST
			: D3D12_RESOURCE_STATE_COMMON;

		if (FAILED(state.Device->CreatePlacedResource(heap.Native.Get(), offset, &resource, initial, nullptr,
			IID_PPV_ARGS(buffer->Resource.GetAddressOf()))))
		{
			DestroyObject(state.Resource, buffer);

			return Result<void*>::Failure(Failure("Rhi.D3D12Heap", "Could not place a D3D12 buffer."));
		}

		return Result<void*>::Success(buffer);
	}
}
#endif
