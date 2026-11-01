#include "D3D12Backend.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	[[nodiscard]] ExternalInteropCapabilities GetExternalInteropCapabilities(const State* opaque)
	{
		const State& state = *opaque;
		ExternalInteropCapabilities result;
		const LUID luid = state.Device->GetAdapterLuid();
		static_assert(sizeof(luid) == sizeof(result.Device.DeviceLuid));
		CopyBytes(result.Device.DeviceLuid, &luid, sizeof(luid));
		result.Device.bLuidValid = true;
		result.Device.NodeMask = 1;
		LARGE_INTEGER driverVersion{};
		if (state.BudgetAdapter && SUCCEEDED(state.BudgetAdapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion)))
		{
			result.AdapterDriverVersion = static_cast<uint64>(driverVersion.QuadPart);
			result.bAdapterDriverVersionValid = true;
		}
		result.MemoryHandleType = EExternalHandleType::D3D12Resource;
		result.FenceHandleType = EExternalHandleType::D3D12Fence;
		result.bBuffers = result.bTextures = result.bTimelineFences = true;

		return result;
	}

	[[nodiscard]] Result<ComPtr<ID3D12Resource>> MakeSharedResource(State& state, const D3D12_RESOURCE_DESC& desc, const NativeExternalMemoryView* imported)
	{
		ComPtr<ID3D12Resource> resource;

		if (imported != nullptr)
		{
			if (imported->Handle.Type != EExternalHandleType::D3D12Resource)
			{
				return Result<ComPtr<ID3D12Resource>>::Failure(EErrorCode::Unsupported, "Rhi.ExternalMemory", "D3D12 imports require a D3D12 resource NT handle.");
			}

			if (FAILED(state.Device->OpenSharedHandle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(imported->Handle.Value)),
				IID_PPV_ARGS(resource.GetAddressOf()))))
			{
				return Result<ComPtr<ID3D12Resource>>::Failure(Failure("Rhi.ExternalMemory", "Could not open the shared D3D12 resource."));
			}

			const D3D12_RESOURCE_DESC actual = resource->GetDesc();

			if (actual.Dimension != desc.Dimension ||
				actual.Width != desc.Width ||
				actual.Height != desc.Height ||
				actual.DepthOrArraySize != desc.DepthOrArraySize ||
				actual.MipLevels != desc.MipLevels ||
				actual.Format != desc.Format ||
				actual.SampleDesc.Count != desc.SampleDesc.Count ||
				(actual.Flags & desc.Flags) != desc.Flags)
			{
				return Result<ComPtr<ID3D12Resource>>::Failure(EErrorCode::InvalidArgument, "Rhi.ExternalMemory", "The shared resource does not match the requested descriptor.");
			}
		}
		else
		{
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = D3D12_HEAP_TYPE_DEFAULT;

			// D3D11 OpenSharedResource1 requires reflected D3D11 metadata, not merely an NT handle.
			// Keep the compatibility profile narrow: single-plane SDR compositor images.
			const bool bCompositorImage =
				desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
				desc.DepthOrArraySize == 1 &&
				desc.MipLevels == 1 &&
				desc.SampleDesc.Count == 1 &&
				(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
						desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
						desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
						desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
			HRESULT created = E_FAIL;
			const char* operation = "CreateCommittedResource";
			if (bCompositorImage)
			{
				ComPtr<ID3D12CompatibilityDevice> compatibility;
				operation = "QueryID3D12CompatibilityDevice";
				created = state.Device.As(&compatibility);
				if (SUCCEEDED(created))
				{
					D3D12_RESOURCE_DESC sharedDesc = desc;
					sharedDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
					D3D11_RESOURCE_FLAGS flags{};
					flags.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
					if ((desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0)
					{
						flags.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
					}
					if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
					{
						flags.BindFlags |= D3D11_BIND_RENDER_TARGET;
					}
					if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
					{
						flags.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
					}
					operation = "CreateSharedResource";
					created = compatibility->CreateSharedResource(&heap, D3D12_HEAP_FLAG_SHARED, &sharedDesc, D3D12_RESOURCE_STATE_COMMON,
						nullptr, &flags, D3D12_COMPATIBILITY_SHARED_FLAG_NONE, nullptr, nullptr,
						IID_PPV_ARGS(resource.GetAddressOf()));
				}
			}
			else
			{
				created = state.Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(resource.GetAddressOf()));
			}
			if (FAILED(created))
			{
				char diagnostic[192];
				FormatText(diagnostic, sizeof(diagnostic), "%s failed with HRESULT 0x%08lx.", operation,
					static_cast<unsigned long>(created));

				return Result<ComPtr<ID3D12Resource>>::Failure(Failure("Rhi.ExternalMemory", diagnostic));
			}
		}

		return Result<ComPtr<ID3D12Resource>>::Success(Move(resource));
	}

	[[nodiscard]] Result<void*> CreateExternalBuffer(State* opaque, const BufferDesc& desc, const NativeExternalMemoryView* imported)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		if (HasUsage(desc.Usage, EBufferUsage::AccelStorage) || HasUsage(desc.Usage, EBufferUsage::MicromapStorage))
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.ExternalMemory", "Acceleration structure storage cannot use the common-state external contract.");
		}
		D3D12_RESOURCE_DESC native{};
		native.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		native.Width = desc.ByteCount;
		native.Height = native.DepthOrArraySize = native.MipLevels = 1;
		native.SampleDesc.Count = 1;
		native.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		if (HasUsage(desc.Usage, EBufferUsage::Storage))
		{
			native.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		Result<ComPtr<ID3D12Resource>> shared = MakeSharedResource(state, native, imported);

		if (!shared)
		{
			return Result<void*>::Failure(shared.GetError());
		}

		return ImportBuffer(opaque, desc, shared.Value().Get());
	}

	[[nodiscard]] Result<void*> CreateExternalTexture(State* opaque, const TextureDesc& desc, const NativeExternalMemoryView* imported)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		const DxgiFormatMapping& mapping = GetDxgiFormatMapping(desc.Format);
		const DXGI_FORMAT format = HasUsage(desc.Usage, ETextureUsage::DepthStencil) || desc.bMutableFormat ? mapping.ResourceFormat : mapping.SrvFormat;
		const D3D12_RESOURCE_DESC native = MakeTextureResourceDesc(desc, format);
		Result<ComPtr<ID3D12Resource>> shared = MakeSharedResource(state, native, imported);

		if (!shared)
		{
			return Result<void*>::Failure(shared.GetError());
		}

		return ImportTexture(opaque, desc, shared.Value().Get(), ENativeTextureContents::Preserve);
	}

	[[nodiscard]] Result<NativeExternalMemory> ExportExternalMemory(State* opaque, void* native, bool bTexture)
	{
		State& state = *opaque;
		ID3D12Resource* resource = bTexture ? static_cast<Texture*>(native)->Resource.Get() : static_cast<Buffer*>(native)->Resource.Get();
		HANDLE handle = nullptr;

		if (FAILED(state.Device->CreateSharedHandle(resource, nullptr, GENERIC_ALL, nullptr, &handle)))
		{
			return Result<NativeExternalMemory>::Failure(Failure("Rhi.ExternalMemory", "Could not export the D3D12 resource handle."));
		}
		NativeExternalMemory result;
		result.Handle = ExternalHandle::Adopt(EExternalHandleType::D3D12Resource, static_cast<uint64>(reinterpret_cast<uintptr_t>(handle)));
		result.Device = GetExternalInteropCapabilities(opaque).Device;
		const D3D12_RESOURCE_DESC desc = resource->GetDesc();
		result.AllocationBytes = state.Device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;

		return Result<NativeExternalMemory>::Success(Move(result));
	}

	[[nodiscard]] Result<void*> CreateExternalFence(State* opaque, uint64 initial, const ExternalHandleView* imported)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		if (imported != nullptr && imported->Type != EExternalHandleType::D3D12Fence)
		{
			return Result<void*>::Failure(EErrorCode::Unsupported, "Rhi.ExternalFence", "D3D12 fence imports require a shared D3D12 fence NT handle.");
		}

		d3d12::ExternalFenceRecord* record = CreateObject<ExternalFenceRecord>(state.Resource);

		if (record == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate a shared fence record."));
		}

		const HRESULT status = imported == nullptr
			? state.Device->CreateFence(initial, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(record->Fence.GetAddressOf()))
			: state.Device->OpenSharedHandle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(imported->Value)),
				IID_PPV_ARGS(record->Fence.GetAddressOf()));

		if (FAILED(status))
		{
			DestroyObject(state.Resource, record);

			return Result<void*>::Failure(Failure("Rhi.ExternalFence", "Could not create or import a shared D3D12 fence."));
		}

		return Result<void*>::Success(record);
	}

	void DestroyExternalFence(State* opaque, void* native) noexcept
	{
		DestroyObject(opaque->Resource, static_cast<ExternalFenceRecord*>(native));
	}

	[[nodiscard]] Result<ExternalFence> ExportExternalFence(State* opaque, void* native)
	{
		State& state = *opaque;
		HANDLE handle = nullptr;

		if (FAILED(state.Device->CreateSharedHandle(static_cast<ExternalFenceRecord*>(native)->Fence.Get(), nullptr, GENERIC_ALL, nullptr,
			&handle)))
		{
			return Result<ExternalFence>::Failure(Failure("Rhi.ExternalFence", "Could not export the shared fence."));
		}

		return Result<ExternalFence>::Success(
			{ ExternalHandle::Adopt(EExternalHandleType::D3D12Fence, static_cast<uint64>(reinterpret_cast<uintptr_t>(handle))),
			 GetExternalInteropCapabilities(opaque).Device });
	}

	[[nodiscard]] Result<uint64> GetExternalFenceValue(void*, void* native)
	{
		const uint64 value = static_cast<ExternalFenceRecord*>(native)->Fence->GetCompletedValue();

		if (value == UINT64_MAX)
		{
			return Result<uint64>::Failure(Failure("Rhi.ExternalFence", "The shared fence device was removed."));
		}

		return Result<uint64>::Success(value);
	}

	[[nodiscard]] Result<void> RecordExternalTransfer(void*, void* commands, void* resource, bool bTexture, void* fence, uint64 value, bool bAcquire)
	{
		Pending& pending = *static_cast<Pending*>(commands);
		pending.ExternalFence = static_cast<ExternalFenceRecord*>(fence)->Fence;
		pending.ExternalFenceValue = value;
		pending.bExternalAcquire = bAcquire;
		pending.bExternalTexture = resource != nullptr && bTexture;
		// Every RHI submission restores ordinary resources to COMMON before signaling.
		// The peer must also return resources in COMMON before signaling its fence value.
		return Result<void>::Success();
	}
}
#endif
