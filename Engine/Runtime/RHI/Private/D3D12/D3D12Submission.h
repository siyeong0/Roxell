#pragma once

#include "../DevicePipelines.h"
#include "../PreparedProgramCache.h"
#ifdef _WIN32
#include "../AftermathBackend.h"
#include "../PreparedBindingCache.h"
#include "D3D12Resources.h"

namespace rox::d3d12
{
	using namespace rox::rhi;

	struct AftermathPendingContext final
	{
		AftermathPendingContext() noexcept = default;
		AftermathPendingContext(const AftermathPendingContext&) = delete;
		AftermathPendingContext& operator=(const AftermathPendingContext&) = delete;

		AftermathPendingContext(AftermathPendingContext&& other) noexcept;
		AftermathPendingContext& operator=(AftermathPendingContext&& other) noexcept;
		~AftermathPendingContext() noexcept;

		IGpuCrashDiagnostics* Diagnostics = nullptr;
		void* Native = nullptr;
	};

	struct State;

	struct PreparedAccelQueryOwner final
	{
		PreparedAccelQueryOwner() noexcept = default;
		~PreparedAccelQueryOwner() noexcept;

		core::SharedPtr<PreparedAccelQueryOwner> Previous;
		State* Device = nullptr;
		core::uint64 Generation = 0;

		core::uint64 Serial = 0;
		EQueue Queue = EQueue::Graphics;
		bool bUnavailable = false;

		ComPtr<ID3D12Resource> Output;
		ComPtr<ID3D12Resource> Readback;
	};

	struct PreparedRayTableOwner final
	{
		explicit PreparedRayTableOwner(core::MemoryResource& memory) noexcept;

		struct DescriptorRange final
		{
			core::uint32 First = 0;
			core::uint32 Count = 0;
			bool bSampler = false;
		};

		core::Array<DescriptorRange> DescriptorRanges;

		core::Array<ComPtr<ID3D12Resource>> Resources;
		core::Array<ID3D12Resource*> StorageResources;

		struct Access final
		{
			NativeFixedDescriptor Descriptor;
			EDescriptorType Type = EDescriptorType::UniformBuffer;
		};

		core::Array<Access> Accesses;

		bool bSharedHeaps = false;

		ComPtr<ID3D12Resource> Resource;

		D3D12_DISPATCH_RAYS_DESC Rays{};
	};

	struct Pending final
	{
		D3D12_GPU_DESCRIPTOR_HANDLE TypedBufferGpu{};
		UINT TypedBufferStride = 0;

		void* LastTypedComputeViews[32]{};
		core::uint32 LastTypedComputeCount = 0;

		void* LastTypedGraphicsViews[32]{};
		core::uint32 LastTypedGraphicsCount = 0;

		void* LastGlobalTypedViews[32]{};
		core::uint32 LastGlobalTypedCount = 0;
		D3D12_GPU_DESCRIPTOR_HANDLE LastGlobalTypedGpu{};

		CommandListStatistics Statistics;

		Texture* ReadOnlyDepthTarget = nullptr;
		Texture* ReadOnlyStencilTarget = nullptr;

		struct GlobalDescriptorRange final
		{
			core::uint32 First = 0;
			core::uint32 Count = 0;
			bool bSampler = false;
		};

		struct PreparedBinding final
		{
			explicit PreparedBinding(core::MemoryResource& resource) noexcept;

			ID3D12DescriptorHeap* Resources = nullptr;
			ID3D12DescriptorHeap* Samplers = nullptr;
			D3D12_GPU_DESCRIPTOR_HANDLE Tables[64]{};

			D3D12_GPU_VIRTUAL_ADDRESS RootAddresses[64]{};
			EDescriptorType RootTypes[64]{};
			bool bRootDescriptors[64]{};

			core::Array<ID3D12Resource*> Storage;
			core::uint32 StorageCount = 0;

			core::uint32 TableCount = 0;

			D3D12_VERTEX_BUFFER_VIEW Vertices[4]{};
			core::uint32 VertexCount = 0;

			D3D12_INDEX_BUFFER_VIEW Index{};

			core::SharedPtr<PreparedShaderTable> RayTable;

			core::SharedPtr<PreparedDescriptorSet> FixedDescriptors[MAXIMUM_FIXED_BINDING_LAYOUTS];

			D3D12_DISPATCH_RAYS_DESC Rays{};
		};

		struct PreparedAccelBuild final
		{
			explicit PreparedAccelBuild(core::MemoryResource& resource) noexcept;

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC Build{};
			core::Array<D3D12_RAYTRACING_GEOMETRY_DESC> Geometries;
		};

		struct PreparedAccelQuery final
		{
			void* Source = nullptr;
			core::SharedPtr<PreparedAccelQueryOwner> Owner;
		};

		struct PreparedAccelCompact final
		{
			void* Source = nullptr;
			void* Target = nullptr;
		};

		core::Array<PreparedAccelBuild> PreparedAccelBuilds;
		core::Array<PreparedAccelQuery> PreparedAccelQueries;
		core::Array<PreparedAccelCompact> PreparedAccelCompacts;

		core::Array<core::SharedPtr<NativeBindingProgram>> PreparedBindings;
		[[nodiscard]] const PreparedBinding& BindingAt(core::uint32 index) const noexcept
		{
			return *static_cast<const PreparedBinding*>(PreparedBindings[index]->Data);
		}
		core::Array<ComPtr<ID3D12CommandSignature>> PreparedIndirects;

		core::Array<D3D12IntegerClear> IntegerClears;

		// Borrowed identities are valid only within the retained prepared list and current render pass.
		void* LastPreparedGraphicsPipeline = nullptr;
		core::uint32 LastPreparedGraphicsBinding = UINT32_MAX;
		bool bPreparedIndexBound = false;

		void InvalidatePreparedGraphics() noexcept;

		bool bPrepared = false;
		bool bPreparedFinished = false;
		bool bSubmissionCredit = false;

		struct ResourceState final
		{
			ID3D12Resource* Resource = nullptr;
			UINT Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;
		};

		struct UavBarrierPolicy final
		{
			ID3D12Resource* Resource = nullptr;
			bool bEnabled = true;
			bool bFirstBarrierPlaced = false;
		};

		explicit Pending(core::MemoryResource& resource) noexcept;

		ComPtr<ID3D12Fence> ExternalFence;
		core::uint64 ExternalFenceValue = 0;
		bool bExternalAcquire = false;
		bool bExternalTexture = false;

		ComPtr<ID3D12CommandAllocator> Allocator;
		ComPtr<ID3D12GraphicsCommandList> List;
		ComPtr<ID3D12GraphicsCommandList1> ResolveList;
		ComPtr<ID3D12GraphicsCommandList4> RayList;
		ComPtr<ID3D12GraphicsCommandList5> ShadingRateList;
		ComPtr<ID3D12GraphicsCommandList6> MeshList;

		EShadingRate CurrentShadingRate = EShadingRate::Rate1x1;
		EShadingRateCombiner CurrentPrimitiveCombiner{}, CurrentImageCombiner{};
		bool bShadingRateSet = false;

		core::Array<ComPtr<ID3D12Resource>> Temporary;

		core::Array<ComPtr<ID3D12DescriptorHeap>> TextureHeaps;
		core::Array<ComPtr<ID3D12DescriptorHeap>> SamplerHeaps;
		core::Array<ComPtr<ID3D12DescriptorHeap>> BindlessHeaps;
		core::Array<GlobalDescriptorRange> GlobalDescriptorRanges;
		ComPtr<ID3D12DescriptorHeap> EmptyTextureHeap;

		core::Array<ComPtr<ID3D12DescriptorHeap>> AttachmentHeaps;

		core::Array<ResourceState> ResourceStates;
		core::Array<core::usize> ResourceStateLookup;

		core::Array<UavBarrierPolicy> UavPolicies;

		AftermathPendingContext Aftermath;
		core::Array<core::uint64> AftermathMarkers;

		bool bInvalidResourceState = false;
		bool bMissingManualState = false;
		bool bAutomaticBarriers = true;
		bool bExplicitStateRequest = false;

		core::uint32 TextureHeapUsed = 0;
		core::uint32 SamplerHeapUsed = 0;

		core::uint32 PreparedTextureHeapCapacity = 256;
		core::uint32 PreparedSamplerHeapCapacity = 256;

		ID3D12DescriptorHeap* BoundTextureHeap = nullptr;
		ID3D12DescriptorHeap* BoundSamplerHeap = nullptr;

		const DescriptorTable* LastMergedSampledTable = nullptr;
		const DescriptorTable* LastMergedStorageTable = nullptr;
		const DescriptorTable* LastMergedStorageTextureTable = nullptr;
		const DescriptorTable* LastMergedUniformTable = nullptr;
		const DescriptorTable* LastMergedReadOnlyTable = nullptr;
		ID3D12DescriptorHeap* LastMergedResourceHeap = nullptr;

		const ComputePipeline* LastComputePipeline = nullptr;
		const DescriptorTable* LastComputeSampledTable = nullptr;
		const DescriptorTable* LastComputeSamplerTable = nullptr;
		const DescriptorTable* LastComputeStorageTable = nullptr;
		const DescriptorTable* LastComputeStorageTextureTable = nullptr;
		const DescriptorTable* LastComputeUniformTable = nullptr;
		const DescriptorTable* LastComputeReadOnlyTable = nullptr;

		void* LastComputeTextures[32]{};
		void* LastComputeViews[32]{};
		void* LastComputeSamplers[32]{};
		EComputeTextureBindingType LastComputeTypes[32]{};
		core::uint32 LastComputeTextureCount = 0;

		ID3D12DescriptorHeap* LastComputeResourceHeap = nullptr;
		ID3D12DescriptorHeap* LastComputeSamplerHeap = nullptr;
		core::uint32 LastComputeGlobalResourceFirst = UINT32_MAX;
		core::uint32 LastComputeGlobalSamplerFirst = UINT32_MAX;

		const ComputePipeline* LastFixedComputePipeline = nullptr;
		NativeComputeTexture LastFixedComputeTextures[32]{};
		core::uint32 LastFixedComputeTextureCount = 0;
		ID3D12DescriptorHeap* LastFixedComputeResourceHeap = nullptr;
		ID3D12DescriptorHeap* LastFixedComputeSamplerHeap = nullptr;
		core::uint32 LastFixedComputeResourceFirst = 0;
		core::uint32 LastFixedComputeSamplerFirst = 0;

		void* LastPackedPipeline = nullptr;
		bool bLastPackedGraphics = false;
		core::uint32 LastPackedSetCount = 0;
		core::SharedPtr<PreparedBindingCache> LastPackedSets[MAXIMUM_FIXED_BINDING_LAYOUTS];
		ID3D12DescriptorHeap* LastPackedResources = nullptr;
		ID3D12DescriptorHeap* LastPackedSamplers = nullptr;

		void* LastRangePipeline = nullptr;
		bool bLastRangeGraphics = false;
		core::uint32 LastRangeSetCount = 0;
		core::SharedPtr<PreparedBindingCache> LastRangeSets[MAXIMUM_FIXED_BINDING_LAYOUTS];
		ID3D12DescriptorHeap* LastRangeResources = nullptr;
		ID3D12DescriptorHeap* LastRangeSamplers = nullptr;
		core::uint32 LastRangeResourceFirst = 0;
		core::uint32 LastRangeSamplerFirst = 0;

		const GraphicsPipeline* LastCombinedPipeline = nullptr;
		const DescriptorTable* LastCombinedSampledTable = nullptr;
		const DescriptorTable* LastCombinedSamplerTable = nullptr;
		const DescriptorTable* LastCombinedStorageTable = nullptr;
		const DescriptorTable* LastCombinedStorageTextureTable = nullptr;
		const DescriptorTable* LastCombinedUniformTable = nullptr;
		const DescriptorTable* LastCombinedReadOnlyTable = nullptr;

		UINT64 LastCombinedTextureGpu = 0;
		UINT64 LastCombinedSamplerGpu = 0;
		ID3D12DescriptorHeap* LastCombinedResourceHeap = nullptr;
		ID3D12DescriptorHeap* LastCombinedSamplerHeap = nullptr;

		const GraphicsPipeline* LastTexturePipeline = nullptr;
		void* LastTextures[32]{};
		void* LastSamplers[32]{};
		void* LastViews[32]{};
		core::uint32 LastTextureCount = 0;

		void* LastStorageTextures[32]{};
		void* LastStorageViews[32]{};
		core::uint32 LastStorageTextureCount = 0;

		ID3D12DescriptorHeap* LastTextureHeap = nullptr;
		ID3D12DescriptorHeap* LastSamplerHeap = nullptr;
		D3D12_GPU_DESCRIPTOR_HANDLE LastTextureGpu{};
		D3D12_GPU_DESCRIPTOR_HANDLE LastSamplerGpu{};

		EQueue Queue = EQueue::Copy;
		core::uint64 Serial = 0;
	};

	struct RetiredFramebuffer final
	{
		Framebuffer* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct Retired final
	{
		Buffer* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct RetiredTexture final
	{
		Texture* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct RetiredPipeline final
	{
		ComputePipeline* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct RetiredGraphicsPipeline final
	{
		GraphicsPipeline* Item = nullptr;
		core::uint64 Serial = 0;
	};

	struct RetiredRayPipeline final
	{
		RayTracingPipeline* Item = nullptr;
		core::uint64 Serial = 0;
	};
}
#endif
