#pragma once
#ifdef _WIN32
#include "../DeviceCommands.h"
#include "D3D12IntegerClear.h"
#include <Algorithms.hpp>
#include <Array.hpp>
#include <SharedPtr.hpp>
#include <UniquePtr.hpp>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
namespace rox::d3d12
{
	using namespace rox::rhi;
	using Microsoft::WRL::ComPtr;

	struct Buffer final
	{
		ComPtr<ID3D12Resource> Resource;

		core::uint64 LastUse = 0;
		EQueue LastQueue = EQueue::Graphics;
		core::uint64 QueueUses[3]{};

		bool bUnknownAliases = false;

		bool bCommitted = false;
		EBufferMemory Memory = EBufferMemory::Device;
		bool bStorage = false;

		core::uint64 MappedOffset = 0;
		core::uint64 MappedBytes = 0;
	};

	struct Heap final
	{
		ComPtr<ID3D12Heap> Native;
		HeapDesc Descriptor;
	};

	struct Texture final
	{
		ComPtr<ID3D12Resource> Resource;
		ComPtr<ID3D12Resource> FeedbackPair;

		EFormat Format = EFormat::UNKNOWN;
		bool bSwapChainImage = false;

		ComPtr<ID3D12DescriptorHeap> RenderTargetView;
		ComPtr<ID3D12DescriptorHeap> DepthStencilView;
		ComPtr<ID3D12DescriptorHeap> ShaderView;
		ComPtr<ID3D12DescriptorHeap> StorageView;

		core::uint64 LastUse = 0;
		EQueue LastQueue = EQueue::Graphics;
		core::uint64 QueueUses[3]{};

		bool bUnknownAliases = false;
	};

	template <typename Resource> void RecordResourceUse(Resource& resource, EQueue queue, core::uint64 serial) noexcept
	{
		core::uint64& use = resource.QueueUses[static_cast<core::uint32>(queue)];
		use = core::Max(use, serial);
		if (serial > resource.LastUse)
		{
			resource.LastUse = serial;
			resource.LastQueue = queue;
		}
	}

	void MergeBufferUses(const Buffer& source, Buffer& target) noexcept;

	struct Sampler final
	{
		ComPtr<ID3D12DescriptorHeap> Descriptor;
		core::uint32 FixedDescriptorIndex = 0xffffffffu;
	};

	struct BufferView final
	{
		ComPtr<ID3D12DescriptorHeap> Descriptor;
	};

	struct CpuDescriptorRange final
	{
		ComPtr<ID3D12DescriptorHeap> Heap;
		D3D12_CPU_DESCRIPTOR_HANDLE Cpu{};

		core::uint32 Page = 0;
		core::uint32 First = 0;
		core::uint32 Count = 0;
	};

	struct TextureView final
	{
		CpuDescriptorRange Descriptor;
		TextureViewDesc Range;
	};

	inline D3D12_CPU_DESCRIPTOR_HANDLE TextureDescriptorCpu(const Texture& texture, const void* view, bool bStorage)
	{
		return view ? static_cast<const TextureView*>(view)->Descriptor.Cpu : (bStorage ? texture.StorageView : texture.ShaderView)->GetCPUDescriptorHandleForHeapStart();
	}

	struct DescriptorTable final
	{
		explicit DescriptorTable(core::MemoryResource& resource) noexcept;

		ComPtr<ID3D12DescriptorHeap> Heap;
		ComPtr<ID3D12DescriptorHeap> CpuHeap;

		core::Array<Texture*> Textures;
		core::Array<TextureView*> TextureViews;
		core::Array<Buffer*> Buffers;
		core::Array<core::uint8> SamplerValid;

		EDescriptorType Type = EDescriptorType::SampledTexture;
		EFormat BufferFormat = EFormat::UNKNOWN;
		core::uint32 StructureStrideBytes = 0;

		core::uint32 Capacity = 0;

		core::uint32 FirstGlobalDescriptor = UINT32_MAX;
	};

	struct ComputePipeline final
	{
		NativeFixedLayoutState Fixed;

		ComPtr<ID3D12RootSignature> RootSignature;
		ComPtr<ID3D12PipelineState> State;

		ComPtr<ID3D12DescriptorHeap> DefaultSamplers;

		core::uint32 BufferCount = 0;
		core::uint32 SampledTextureCount = 0;

		core::uint32 PushConstantBytes = 0;

		core::uint32 BindlessRootIndex = UINT32_MAX;
		core::uint32 BindlessSamplerRootIndex = UINT32_MAX;
		core::uint32 BindlessStorageBufferRootIndex = UINT32_MAX;
		core::uint32 BindlessReadOnlyBufferRootIndex = UINT32_MAX;
		core::uint32 BindlessStorageTextureRootIndex = UINT32_MAX;
		core::uint32 BindlessUniformBufferRootIndex = UINT32_MAX;

		core::uint32 AccelRootIndex = UINT32_MAX;

		bool bHeapDirectlyIndexed = false;
		bool bDeviceHeapIndexed = false;

		core::uint64 LastUse = 0;
	};

	struct GraphicsPipeline final
	{
		NativeFixedLayoutState Fixed;

		ComPtr<ID3D12RootSignature> RootSignature;
		ComPtr<ID3D12PipelineState> State;

		ComPtr<ID3D12DescriptorHeap> DefaultSamplers;

		core::uint32 PushConstantBytes = 0;

		core::uint32 SampledTextureCount = 0;
		core::uint32 GraphicsBufferCount = 0;
		core::uint32 StorageTextureCount = 0;

		core::uint32 StorageTextureRootIndex = UINT32_MAX;

		core::uint32 BindlessRootIndex = 0;
		core::uint32 BindlessSamplerRootIndex = 0;
		core::uint32 BindlessStorageBufferRootIndex = UINT32_MAX;
		core::uint32 BindlessReadOnlyBufferRootIndex = UINT32_MAX;
		core::uint32 BindlessStorageTextureRootIndex = UINT32_MAX;
		core::uint32 BindlessUniformBufferRootIndex = UINT32_MAX;

		core::uint32 AccelRootIndex = UINT32_MAX;

		D3D_PRIMITIVE_TOPOLOGY Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		bool bMeshShader = false;

		bool bStencil = false;

		DepthBoundsDesc DepthBounds;

		bool bBlendConstant = false;

		bool bHeapDirectlyIndexed = false;
		bool bDeviceHeapIndexed = false;

		core::uint64 LastUse = 0;
	};

	struct RayTracingPipeline final
	{
		NativeFixedLayoutState Fixed;

		explicit RayTracingPipeline(core::MemoryResource& resource) noexcept;

		ComPtr<ID3D12RootSignature> RootSignature;
		ComPtr<ID3D12StateObject> State;

		ComPtr<ID3D12RootSignature> LocalRootSignatures[64];

		ComPtr<ID3D12DescriptorHeap> DefaultSamplers;

		core::Array<core::Byte> GroupHandles;

		core::Array<NativeRayLocalRootArgument> LocalRootArguments;
		core::uint32 LocalRootArgumentOffsets[65]{};

		bool bTypedLocalRoots = false;

		core::uint32 LocalSamplerOffset[64]{};

		core::uint32 BufferCount = 0;
		core::uint32 TextureCount = 0;
		core::uint32 SampledTextureCount = 0;

		core::uint32 AccelRootIndex = UINT32_MAX;

		core::uint32 PushRootIndex = UINT32_MAX;
		core::uint32 PushConstantBytes = 0;

		core::uint64 LastUse = 0;
	};

	struct Framebuffer final
	{
		Texture* Colors[8]{};
		D3D12IntegerClear IntegerClears[8];
		UINT ColorSubresources[8]{};

		Texture* Depth = nullptr;

		Texture* ShadingRateImage = nullptr;
		TextureSubresource DepthSubresource{};

		CpuDescriptorRange ColorViews;
		CpuDescriptorRange DepthView;

		core::uint32 ColorCount = 0;

		core::uint32 Width = 0;
		core::uint32 Height = 0;

		core::uint64 LastUse = 0;
	};

	struct SwapChain final
	{
		ComPtr<IDXGISwapChain4> Native;
		EFormat Format = EFormat::RGBA8_UNORM;

		ComPtr<ID3D12Resource> Images[3];
		Texture* Targets[3]{};

		bool bUsable = true;
		bool bAcquired = false;
		core::uint32 AcquiredIndex = 0;

		core::uint32 Width = 0;
		core::uint32 Height = 0;

		core::uint32 BufferCount = 0;
		bool bVSync = true;
	};
}
#endif
