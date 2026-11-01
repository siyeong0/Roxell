#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../AftermathBackend.h"
#include "../DeviceResources.h"
#include "../DevicePipelines.h"
#include "../SubmissionCompletion.h"
#include "D3D12IntegerClear.h"
#include "D3D12State.h"
#include "DxgiFormat.h"
#include "NvApiSession.h"

#include <Algorithms.hpp>
#include <Array.hpp>
#include <ByteOperations.hpp>
#include <Comparison.hpp>
#include <ComparisonTypes.hpp>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <ScopeExit.hpp>
#include <SharedPtr.hpp>
#include <Synchronization.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <UniquePtr.hpp>
#include <Utility.hpp>
#include <Vocabulary.hpp>
#include <Windows.h>
#include <d3d12.h>
#include <d3d12compatibility.h>
#include <dxgi1_6.h>
#include <pix.h>
#include <wrl/client.h>
#if ROXELL_RHI_WITH_NVAPI
#include <nvapi.h>
#endif

namespace rox::d3d12
{
	using namespace rox::rhi;

	using Microsoft::WRL::ComPtr;
	inline constexpr core::uint64 MAXIMUM_BUFFER_BYTES = 256ull * 1024ull * 1024ull;

	void InvalidateIdle(State& state) noexcept;
	bool WasSubmissionAccepted(const State* opaque) noexcept;
	bool HasRetirementProof(const State* opaque) noexcept;
	core::uint64 GetPresentCopySerial(const State* opaque) noexcept;
	void ExposeNativeQueues(State* opaque) noexcept;

	void RetireAftermathMarkers(Pending& pending) noexcept;

	void RecordBarriers(Pending& pending, UINT count, const D3D12_RESOURCE_BARRIER* barriers) noexcept;

	void CopyCommandDescriptors(State& state, Pending& pending, UINT count, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, D3D12_DESCRIPTOR_HEAP_TYPE type) noexcept;

	[[nodiscard]] ID3D12Fence* QueueFence(const State& state, EQueue queue) noexcept;
	[[nodiscard]] ID3D12CommandQueue* NativeQueue(const State& state, EQueue queue) noexcept;
	[[nodiscard]] core::uint64 QueueLastSerial(const State& state, EQueue queue) noexcept;
	[[nodiscard]] core::uint64 CompletedSerial(const State& state) noexcept;

	[[nodiscard]] bool SupportsSamples(const State& state, DXGI_FORMAT format, core::uint32 sampleCount) noexcept;
	[[nodiscard]] core::Result<core::uint32> QueryTextureSampleCounts(const State* opaque, const TextureDesc& desc);
	[[nodiscard]] bool SupportsColorResolve(const State* opaque, EFormat format) noexcept;
	[[nodiscard]] bool SupportsTextureResolve(const State* opaque, const TextureDesc& desc, ETextureResolveMode mode, ETextureResolveMode stencilMode) noexcept;
	[[nodiscard]] core::Result<void> PrepareTextureResolve(void*, void* native, NativeTextureResolve& resolve);

	extern thread_local bool bPreparedErrors;

	struct PreparedErrorScope final
	{
		PreparedErrorScope() noexcept;
		~PreparedErrorScope() noexcept;

		bool bPrevious;
	};

	[[nodiscard]] core::Error NativeError(core::EErrorCode code, const char* id, const char* message) noexcept;
	[[nodiscard]] core::Error Failure(const char* id, const char* message) noexcept;

	[[nodiscard]] core::Result<CpuDescriptorRange> AllocateCpuDescriptors(State& state, D3D12_DESCRIPTOR_HEAP_TYPE type, core::uint32 count);
	void FreeCpuDescriptors(State& state, CpuDescriptorRange& range) noexcept;
	[[nodiscard]] core::Result<void> EnsureEmptyTextureHeap(State& state, Pending& pending);

	[[nodiscard]] D3D12_TEXTURE_ADDRESS_MODE AddressMode(ESamplerAddressMode mode) noexcept;
	[[nodiscard]] D3D12_STATIC_SAMPLER_DESC MakeStaticSampler(const SamplerDesc& sampler, core::uint32 slot) noexcept;
	[[nodiscard]] D3D12_SAMPLER_DESC MakeSamplerDesc(const SamplerDesc& sampler) noexcept;
	[[nodiscard]] core::Result<void*> CreateSampler(State* opaque, const SamplerDesc& desc);
	void DestroySampler(State* opaque, void* native) noexcept;

	[[nodiscard]] core::Result<void*> CreateTextureView(State* opaque, void* nativeTexture, const TextureDesc& source, const TextureViewDesc& desc);
	void DestroyTextureView(State* opaque, void* native) noexcept;

	[[nodiscard]] core::Result<void> CheckActive(const State& state);

	[[nodiscard]] EFormatSupport QueryFormatSupport(const State* opaque, EFormat format) noexcept;

	[[nodiscard]] bool CreateResource(
		State& state,
		core::uint64 byteCount,
		D3D12_HEAP_TYPE heap,
		D3D12_RESOURCE_STATES initial,
		ComPtr<ID3D12Resource>& output,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

	[[nodiscard]] UINT SubresourceCount(const D3D12_RESOURCE_DESC& desc) noexcept;
	[[nodiscard]] bool ReserveTrackedStates(Pending& pending, core::usize count) noexcept;
	[[nodiscard]] bool ReserveAdditionalTrackedState(Pending& pending) noexcept;
	[[nodiscard]] core::usize TrackedStatePosition(const Pending& pending, const ID3D12Resource& resource, UINT subresource) noexcept;
	[[nodiscard]] core::usize TrackedStateIndex(const Pending& pending, const ID3D12Resource& resource, UINT subresource) noexcept;
	[[nodiscard]] core::Pair<core::usize, core::usize> TrackedResourceRange(const Pending& pending, const ID3D12Resource& resource) noexcept;
	[[nodiscard]] bool AddTrackedState(Pending& pending, ID3D12Resource& resource, UINT subresource, D3D12_RESOURCE_STATES state) noexcept;
	[[nodiscard]] D3D12_RESOURCE_STATES TrackedState(const Pending& pending, const ID3D12Resource& resource, UINT subresource) noexcept;
	void SeedTrackedState(Pending& pending, ID3D12Resource& resource, UINT subresource, D3D12_RESOURCE_STATES state) noexcept;
	[[nodiscard]] ETrackedResourceState MapTrackedState(D3D12_RESOURCE_STATES native) noexcept;
	[[nodiscard]] ETrackedResourceState GetTextureSubresourceState(const void*, const void* native, const void* textureNative, TextureSubresource subresource) noexcept;

	void EmitTransition(Pending& pending, ID3D12Resource& resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource) noexcept;
	void EmitUavBarrier(Pending& pending, ID3D12Resource& resource) noexcept;
	[[nodiscard]] core::Result<void> SetUavBarrierPolicy(Pending& pending, ID3D12Resource& resource, bool bEnabled);
	[[nodiscard]] bool ShouldEmitUavBarrier(Pending& pending, ID3D12Resource& resource) noexcept;
	[[nodiscard]] bool SatisfiesManualState(D3D12_RESOURCE_STATES tracked, D3D12_RESOURCE_STATES required) noexcept;
	void TransitionTracked(Pending& pending, ID3D12Resource& resource, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES after, UINT subresource) noexcept;
	void Transition(Pending& pending, ID3D12Resource& resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) noexcept;
	void TransitionDepthSubresource(Pending& pending, Texture& texture, TextureSubresource subresource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) noexcept;
	void TransitionTextureView(Pending& pending, Texture& texture, const TextureView* view, D3D12_RESOURCE_STATES next) noexcept;
	void RestoreTrackedStates(Pending& pending) noexcept;

	void Collect(State* opaque) noexcept;

	void InvalidateCommandState(void* native) noexcept;
	void SetAutomaticBarriers(void* native, bool bEnabled) noexcept;
	[[nodiscard]] core::Result<void> SetBufferUavBarriers(void* native, void* bufferNative, bool bEnabled);
	[[nodiscard]] core::Result<void> SetTextureUavBarriers(void* native, void* textureNative, bool bEnabled);

	[[nodiscard]] core::Result<Pending> Begin(State& state, EQueue queue = EQueue::Graphics);
	[[nodiscard]] core::Result<core::uint64> Execute(
		State& state,
		Pending&& pending,
		bool bIndependent = false, const NativePreparedSubmissionDesc* preparation = nullptr, core::uint32* queueWaitCount = nullptr);

	[[nodiscard]] core::uint32 MemoryBudgetIndex(const State& state, EBufferMemory memory) noexcept;
	[[nodiscard]] core::Result<MemoryRequirements> GetBufferMemoryRequirements(const State* opaque, const BufferDesc& desc);
	[[nodiscard]] core::Result<core::uint32> QueryMemoryBudget(const State* opaque, core::Span<MemoryBudget> budgets);

	[[nodiscard]] D3D12_RESOURCE_DESC MakeTextureResourceDesc(const TextureDesc& desc, DXGI_FORMAT format) noexcept;
	[[nodiscard]] core::Result<MemoryRequirements> GetTextureMemoryRequirements(const State* opaque, const TextureDesc& desc);

	[[nodiscard]] core::Result<void*> CreateHeap(State* opaque, const HeapDesc& desc);
	[[nodiscard]] core::uint64 GetHeapCommittedBytes(const void* native) noexcept;
	void DestroyHeapNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyHeap(State* opaque, void* native);

	[[nodiscard]] core::Result<void*> CreatePlacedBuffer(State* opaque, const BufferDesc& desc, void* nativeHeap, core::uint64 offset);

	struct NativeMicromap final
	{
		ComPtr<ID3D12Resource> Storage;
		core::uint64 Address = 0;
	};
#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] core::Result<void> AppendMicromapUsageCounts(core::Span<const OpacityMicromapUsageCount> source, core::Array<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT>& destination);
	[[nodiscard]] NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BUILD_FLAGS MicromapFlags(EOpacityMicromapBuildFlags flags) noexcept;
	[[nodiscard]] bool HasOpacityMicromaps(core::Span<const NativeAccelTriangleGeometry> triangles) noexcept;
	[[nodiscard]] core::Result<void> ConvertMicromapGeometries(
		core::Span<const D3D12_RAYTRACING_GEOMETRY_DESC> source,
		core::Span<const NativeAccelTriangleGeometry> triangles,
		core::Array<NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX>& destination,
		core::Array<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT>& counts);
	[[nodiscard]] NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX MicromapAccelInputs(
		const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& source,
		const NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX* geometries) noexcept;
#endif
	[[nodiscard]] core::Result<OpacityMicromapBuildSizes> GetOpacityMicromapBuildSizes(
		[[maybe_unused]] const State* opaque,
		[[maybe_unused]] core::Span<const OpacityMicromapUsageCount> counts,
		[[maybe_unused]] EOpacityMicromapBuildFlags flags);
	[[nodiscard]] core::Result<void*> CreateOpacityMicromap(State* opaque, const OpacityMicromapDesc& desc, void* nativeStorage);
	void DestroyOpacityMicromapNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyOpacityMicromap(State* opaque, void* native, void*);
	[[nodiscard]] core::Result<void> RecordBuildOpacityMicromap(
		[[maybe_unused]] State* opaque,
		[[maybe_unused]] void* nativeCommand,
		[[maybe_unused]] void* nativeMicromap,
		[[maybe_unused]] const OpacityMicromapBuildDesc& desc,
		[[maybe_unused]] void* nativeInput,
		[[maybe_unused]] core::uint64 inputOffset,
		[[maybe_unused]] void* nativeTriangles,
		[[maybe_unused]] core::uint64 triangleOffset,
		[[maybe_unused]] void* nativeScratch,
		[[maybe_unused]] core::uint64 scratchOffset);

	[[nodiscard]] core::Result<AccelStructBuildSizes> GetAccelStructBuildSizes(
		const State* opaque,
		const AccelStructBuildDesc& desc,
		core::Span<const NativeAccelTriangleGeometry> triangles,
		core::Span<const NativeAccelAabbGeometry> aabbs);

	struct NativeAccelStruct final
	{
		core::SharedPtr<PreparedAccelQueryOwner> PreparedQuery;
		core::SharedPtr<PreparedAccelQueryOwner> ProvisionalQuery;

		ComPtr<ID3D12Resource> Storage;
		Buffer* StorageBuffer = nullptr;

		core::uint64 Address = 0;
	};

	[[nodiscard]] core::Result<void> RecordSetAccelStructState(void*, void* native, void* accelNative, EAccelStructState);

	[[nodiscard]] core::Result<void> Wait(State*, EQueue, core::uint64, core::uint32);

	[[nodiscard]] core::Result<void*> CreateAccelStruct(State* opaque, const AccelStructDesc& desc, void* nativeBuffer);
	void DestroyAccelStructNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyAccelStruct(State* opaque, void* native);
	[[nodiscard]] core::Result<void> RecordBuildTopLevelAccelStructFromBuffer(
		State* opaque,
		void* nativeCommand,
		void* nativeAccel,
		const AccelStructBuildDesc& desc,
		void* nativeInstances,
		core::uint64 instanceOffset,
		void* nativeScratch,
		core::uint64 scratchOffset);
	[[nodiscard]] core::uint64 GetAccelStructDeviceAddress(const void*, const void* native) noexcept;
	void PropagateAccelStructLastUseToBuffer(const void*, const void* source, void* destination) noexcept;
	void PropagateAccelStructLastUse(const State* opaque, const void* source, void* destination) noexcept;
	[[nodiscard]] core::Result<core::uint64> GetBottomLevelAccelStructCompactedSize(State* opaque, void* native);
	[[nodiscard]] core::Result<void> RecordCompactBottomLevelAccelStruct(State* opaque, void* nativeCommand, void* nativeSource, void* nativeTarget);
	[[nodiscard]] core::Result<void> RecordBuildBottomLevelAccelStruct(
		State* opaque,
		void* nativeCommand,
		void* nativeAccel,
		const AccelStructBuildDesc& desc,
		core::Span<const NativeAccelTriangleGeometry> triangles,
		core::Span<const NativeAccelAabbGeometry> aabbs,
		void* nativeScratch,
		core::uint64 scratchOffset);
	[[nodiscard]] core::Result<void> RecordBuildTopLevelAccelStruct(
		State* opaque,
		void* nativeCommand,
		void* nativeAccel,
		const AccelStructBuildDesc& desc,
		core::Span<const NativeAccelInstance> instances,
		void* nativeScratch,
		core::uint64 scratchOffset);
	[[nodiscard]] core::uint64 GetDescriptorAccelerationStructure(const void* native) noexcept;
#if ROXELL_RHI_WITH_NVAPI
	using NativeClusterTriangleArgs =
		NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS;
	using NativeClusterTemplateArgs =
		NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS;
	using NativeClusterInstantiateArgs =
		NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_INSTANTIATE_TEMPLATE_ARGS;
	using NativeClusterBlasArgs = NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS;
	using NativeClusterMoveArgs = NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_MOVE_ARGS;
	static_assert(sizeof(ClusterTriangleBuildInfo) == sizeof(NativeClusterTriangleArgs));
	static_assert(sizeof(ClusterTriangleTemplateBuildInfo) == sizeof(NativeClusterTemplateArgs));
	static_assert(sizeof(ClusterInstantiateInfo) == sizeof(NativeClusterInstantiateArgs));
	static_assert(sizeof(ClusterBlasBuildInfo) == sizeof(NativeClusterBlasArgs));
	static_assert(sizeof(ClusterMoveInfo) == sizeof(NativeClusterMoveArgs));
	static_assert(offsetof(ClusterTriangleBuildInfo, ClusterId) == offsetof(NativeClusterTriangleArgs, clusterId));
	static_assert(offsetof(ClusterTriangleBuildInfo, ClusterFlags) ==
		offsetof(NativeClusterTriangleArgs, clusterFlags));
	static_assert(offsetof(ClusterTriangleBuildInfo, BaseGeometryIndexAndFlags) ==
		offsetof(NativeClusterTriangleArgs, baseGeometryIndexAndFlags));
	static_assert(offsetof(ClusterTriangleBuildInfo, IndexBufferStride) ==
		offsetof(NativeClusterTriangleArgs, indexBufferStride));
	static_assert(offsetof(ClusterTriangleBuildInfo, VertexBufferStride) ==
		offsetof(NativeClusterTriangleArgs, vertexBufferStride));
	static_assert(offsetof(ClusterTriangleBuildInfo, GeometryIndexAndFlagsBufferStride) ==
		offsetof(NativeClusterTriangleArgs, geometryIndexAndFlagsBufferStride));
	static_assert(offsetof(ClusterTriangleBuildInfo, OpacityMicromapIndexBufferStride) ==
		offsetof(NativeClusterTriangleArgs, opacityMicromapIndexBufferStride));
	static_assert(offsetof(ClusterTriangleBuildInfo, IndexBufferAddress) ==
		offsetof(NativeClusterTriangleArgs, indexBuffer));
	static_assert(offsetof(ClusterTriangleBuildInfo, VertexBufferAddress) ==
		offsetof(NativeClusterTriangleArgs, vertexBuffer));
	static_assert(offsetof(ClusterTriangleBuildInfo, GeometryIndexAndFlagsBufferAddress) ==
		offsetof(NativeClusterTriangleArgs, geometryIndexAndFlagsBuffer));
	static_assert(offsetof(ClusterTriangleBuildInfo, OpacityMicromapArrayAddress) ==
		offsetof(NativeClusterTriangleArgs, opacityMicromapArray));
	static_assert(offsetof(ClusterTriangleBuildInfo, OpacityMicromapIndexBufferAddress) ==
		offsetof(NativeClusterTriangleArgs, opacityMicromapIndexBuffer));
	static_assert(offsetof(ClusterTriangleTemplateBuildInfo, InstantiationBoundingBoxLimitAddress) ==
		offsetof(NativeClusterTemplateArgs, instantiationBoundingBoxLimit));
	static_assert(offsetof(ClusterInstantiateInfo, ClusterTemplateAddress) ==
		offsetof(NativeClusterInstantiateArgs, clusterTemplate));
	static_assert(offsetof(ClusterInstantiateInfo, VertexBufferAddress) ==
		offsetof(NativeClusterInstantiateArgs, vertexBuffer));
	static_assert(offsetof(ClusterInstantiateInfo, VertexBufferStride) ==
		offsetof(NativeClusterInstantiateArgs, vertexBuffer) +
		offsetof(D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE, StrideInBytes));
	static_assert(offsetof(ClusterBlasBuildInfo, ClusterReferencesAddress) ==
		offsetof(NativeClusterBlasArgs, clusterVAs));
	static_assert(static_cast<core::uint8>(EClusterOperationFlags::AllowOpacityMicromaps) ==
		NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM);
	static_assert(static_cast<core::uint8>(EClusterOperationMode::GetSizes) ==
		NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_GET_SIZES);
	static_assert(static_cast<core::uint8>(EClusterMoveType::Template) ==
		NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE_TEMPLATE);

	[[nodiscard]] bool PrepareClusterInput(const ClusterOperationParams& params, NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS& input) noexcept;

#endif

	[[nodiscard]] core::Result<ClusterOperationSizeInfo> GetClusterOperationSizeInfo(const State* opaque, const ClusterOperationParams& params);
	[[nodiscard]] core::Result<void> RecordClusterOperation(
		State* opaque,
		void* nativeCommand,
		const ClusterOperationDesc& desc,
		void* nativeScratch,
		void* nativeCount,
		void* nativeArgs,
		void* nativeAddresses,
		void* nativeSizes,
		void* nativeResult);
#if ROXELL_RHI_WITH_NVAPI
	[[nodiscard]] NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE ToNvCoopType(ECoopVecDataType type) noexcept;
	[[nodiscard]] ECoopVecDataType FromNvCoopType(NVAPI_COOPERATIVE_VECTOR_COMPONENT_TYPE type) noexcept;
#endif
	[[nodiscard]] core::uint64 CoopVecElementBytes(ECoopVecDataType type) noexcept;
	[[nodiscard]] core::uint64 CoopVecStride(ECoopVecDataType type, ECoopVecMatrixLayout layout, core::uint32 rows, core::uint32 columns) noexcept;
#if defined(DIRECT3D_LINEAR_ALGEBRA)
	[[nodiscard]] D3D12_LINEAR_ALGEBRA_DATATYPE ToLinAlgType(ECoopVecDataType type) noexcept;
	[[nodiscard]] core::Result<void> InitLinAlg(State& state);
	[[nodiscard]] core::Result<core::uint64> GetLinAlgMatrixSize(const State& state, ECoopVecDataType type, ECoopVecMatrixLayout layout, core::uint32 rows, core::uint32 columns);
	[[nodiscard]] core::Result<void> RecordLinAlgMatrices(State& state, Pending& pending, core::Span<const NativeCoopVecMatrixConversion> conversions, bool automatic);
#endif
	[[nodiscard]] core::Result<void> InitCoopVec(State& state);
	[[nodiscard]] core::Result<CoopVecFeatures> QueryCoopVecFeatures(const State* opaque, core::Span<CoopVecFormatCombo> formats);
	[[nodiscard]] core::Result<core::uint64> GetCoopVecMatrixSize(const State* opaque, ECoopVecDataType type, ECoopVecMatrixLayout layout, core::uint32 rows, core::uint32 columns);
	[[nodiscard]] core::Result<void> RecordCoopVecMatrices(State* opaque, void* native, core::Span<const NativeCoopVecMatrixConversion> conversions, bool bAutomatic);

	[[nodiscard]] bool EnableRayDevice(State& state) noexcept;
	[[nodiscard]] bool SupportsRayQuery(State& state) noexcept;
	[[nodiscard]] bool SupportsMeshShader(State& state) noexcept;

	[[nodiscard]] core::Result<void*> CreateBuffer(State* opaque, const BufferDesc& desc);
	void DestroyBufferNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyBuffer(State* opaque, void* native);

	[[nodiscard]] core::Result<void*> CreateTextureInternal(State* opaque, const TextureDesc& desc, Heap* placedHeap, core::uint64 offset, ID3D12Resource* imported = nullptr);
	[[nodiscard]] core::Result<void*> CreateTexture(State* opaque, const TextureDesc& desc);

	[[nodiscard]] core::Result<void*> ImportBuffer(State* opaque, const BufferDesc& desc, void* native);
	[[nodiscard]] void* GetNativeBuffer(const void* native) noexcept;

	[[nodiscard]] core::Result<void*> ImportTexture(State* opaque, const TextureDesc& desc, void* native, ENativeTextureContents);
	[[nodiscard]] void* GetNativeTexture(const void* native) noexcept;

	[[nodiscard]] core::Result<void*> CreatePlacedTexture(State* opaque, const TextureDesc& desc, void* heap, core::uint64 offset);

	[[nodiscard]] core::uint64 GetBufferLastUse(const void* native) noexcept;
	void PropagateBufferLastUse(const void*, const void* source, void* target) noexcept;
	[[nodiscard]] core::uint64 GetBufferDeviceAddress(const void*, const void* native) noexcept;

	[[nodiscard]] core::uint64 GetTextureLastUse(const void* native) noexcept;

	void DestroyTextureNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyTexture(State* opaque, void* native);

	[[nodiscard]] core::Result<core::uint64> UploadBuffer(State* opaque, void* native, core::uint64 offset, core::Span<const core::Byte> data);

	[[nodiscard]] core::Result<bool> IsComplete(const State* opaque, core::uint64 serial);
	[[nodiscard]] core::Result<bool> IsQueueComplete(const State* opaque, EQueue queue, core::uint64 serial);

	[[nodiscard]] core::Result<void> Wait(State* opaque, EQueue queue, core::uint64 serial, core::uint32 timeout);
	[[nodiscard]] core::Result<void> QueueWaitForSubmission(State* opaque, EQueue waitingQueue, EQueue executionQueue, core::uint64 serial);
	[[nodiscard]] core::uint64 GetQueueLastSubmittedSerial(const State* opaque, EQueue queue) noexcept;

	[[nodiscard]] core::Result<core::Byte*> MapBuffer(State* opaque, void* native, core::uint64 offset, core::uint64 byteCount, core::uint32 timeout);
	[[nodiscard]] core::Result<void> UnmapBuffer(void*, void* native);
	[[nodiscard]] core::Result<void> ReadBuffer(State* opaque, void* native, core::uint64 offset, core::Span<core::Byte> output, core::uint32 timeout);

	[[nodiscard]] core::Result<core::uint64> UploadTexture(State* opaque, void* native, const TextureDesc& desc, TextureSubresource subresource, core::Span<const core::Byte> data);
	[[nodiscard]] core::Result<void> ReadTexture(State* opaque, void* native, const TextureDesc& desc, TextureSubresource subresource, core::Span<core::Byte> output, core::uint32 timeout);

	[[nodiscard]] core::Result<void*> CreateCommandList(State* opaque, EQueue queue);
	void* GetNativeCommandList(const void* native) noexcept;

	[[nodiscard]] core::Result<void> RecordBeginMarker(State* opaque, void* native, const char* name);
	[[nodiscard]] core::Result<void> RecordEndMarker(void*, void* native);

	struct GpuTimer final
	{
		ComPtr<ID3D12QueryHeap> Queries;
		ComPtr<ID3D12QueryHeap> CopyQueries;

		ComPtr<ID3D12Resource> Readback;

		core::uint64 DirectFrequency = 0;
		core::uint64 ComputeFrequency = 0;
		core::uint64 CopyFrequency = 0;
		core::uint64 Frequency = 0;
	};

	[[nodiscard]] core::Result<void*> CreateGpuTimer(State* opaque);
	void DestroyGpuTimer(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> RecordGpuTimerBegin(void*, void* nativeList, void* nativeTimer);
	[[nodiscard]] core::Result<void> RecordGpuTimerEnd(void*, void* nativeList, void* nativeTimer);
	[[nodiscard]] core::Result<double> ReadGpuTimer(void*, void* native);

	[[nodiscard]] core::Result<void> RecordBufferCopy(State* opaque, void* native, const NativeCopy& copy);
	[[nodiscard]] core::Result<void> RecordBufferWrite(State* opaque, void* native, void* destination, core::uint64 offset, core::Span<const core::Byte> data);
	[[nodiscard]] core::Result<void> RecordStorageBufferClear(State& state, Pending& pending, Buffer& buffer, core::uint64 byteCount, core::uint32 value);
	[[nodiscard]] core::Result<void> RecordBufferClear(State* opaque, void* native, void* target, core::uint64 byteCount, core::uint32 value);

	[[nodiscard]] core::Result<void> RecordTextureCopy(void*, void* native, const NativeTextureCopy& copy);
	[[nodiscard]] core::Result<void> RecordTextureWrite(
		State* opaque,
		void* native,
		void* destinationNative,
		const TextureDesc& desc,
		TextureSubresource subresource,
		core::Span<const core::Byte> data);

	[[nodiscard]] core::Result<TextureBufferFootprint> GetTextureBufferFootprint(State* opaque, const TextureDesc& desc, TextureSubresource subresource);
	[[nodiscard]] core::Result<void> PrepareTextureBufferCopy(State* opaque, NativeTextureBufferCopy& copy);
	[[nodiscard]] core::Result<void> RecordTextureBuffer(State* opaque, void* native, const NativeTextureBufferCopy& copy, bool bUpload);
	[[nodiscard]] core::Result<void> RecordBufferToTexture(State* opaque, void* native, const NativeTextureBufferCopy& copy);
	[[nodiscard]] core::Result<void> RecordTextureToBuffer(State* opaque, void* native, const NativeTextureBufferCopy& copy);

	[[nodiscard]] core::Result<void> RecordTextureResolve(State* opaque, void* native, const NativeTextureResolve& resolve);

	[[nodiscard]] core::Result<void> RecordStorageTextureClear(
		State& state,
		Pending& pending,
		Texture& texture,
		const TextureDesc& desc,
		TextureSubresource subresource,
		const ClearColor* color,
		core::uint32 clearValue);
	[[nodiscard]] core::Result<void> RecordTextureClear(State* opaque, void* native, void* target, const TextureDesc& desc, TextureSubresource subresource, const ClearColor& color);

	[[nodiscard]] core::Result<void> RecordStorageTextureClear(
		State& state,
		Pending& pending,
		Texture& texture,
		const TextureDesc& desc,
		TextureSubresource subresource,
		const ClearColor* color,
		core::uint32 clearValue);
	[[nodiscard]] core::Result<void> RecordTextureUIntClear(State* opaque, void* native, void* target, const TextureDesc& desc, TextureSubresource subresource, core::uint32 clearValue);

	[[nodiscard]] core::Result<void*> WrapSamplerFeedbackTexture(State& state, Texture& pairedTexture, ID3D12Resource* resource);
	[[nodiscard]] core::Result<void*> CreateSamplerFeedbackTexture(State* opaque, void* paired, const TextureDesc&, const SamplerFeedbackDesc& desc);
	[[nodiscard]] core::Result<void*> ImportSamplerFeedbackTexture(State* opaque, void* paired, const TextureDesc&, const SamplerFeedbackDesc& desc, void* native);
	[[nodiscard]] core::Result<void> RecordSetSamplerFeedbackState(void*, void* native, void* feedback, ESamplerFeedbackState state);
	[[nodiscard]] core::Result<void> RecordSamplerFeedbackClear(State* opaque, void* native, void* feedback);
	[[nodiscard]] core::Result<void> RecordSamplerFeedbackDecode(void*, void* native, void* feedback, void* destination);
	[[nodiscard]] core::Result<void> RecordSamplerFeedbackDecodeTexture(void*, void* native, void* feedback, void* destination);

	[[nodiscard]] core::Result<TextureTiling> GetTextureTiling(const State* opaque, const void* native, core::Span<TextureSubresourceTiling> subresources);
	[[nodiscard]] core::Result<core::uint64> UpdateTextureTileMappings(State* opaque, void* native, const TextureDesc& desc, core::Span<const NativeTextureTileMapping> mappings, EQueue queueKind);

	[[nodiscard]] core::Result<void> RecordDepthClear(State* opaque, void* native, void* target, const TextureDesc& desc, TextureSubresource subresource, DepthStencilClear clear);

	[[nodiscard]] HRESULT SerializeDirectRootSignature(const D3D12_ROOT_SIGNATURE_DESC& source, ID3DBlob** serialized, ID3DBlob** errors) noexcept;

	[[nodiscard]] core::Result<void*> CreateComputePipeline(State* opaque, const NativeComputePipelineDesc& desc, core::Span<const NativeBindingLayoutDesc> layouts);
	void DestroyComputePipelineNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyComputePipeline(State* opaque, void* native);

	[[nodiscard]] core::Result<void> BindComputeDescriptorTables(
		State& state,
		Pending& pending,
		const ComputePipeline& pipeline,
		DescriptorTable* sampled,
		DescriptorTable* samplers,
		DescriptorTable* storage,
		DescriptorTable* storageTextures,
		DescriptorTable* uniformBuffers,
		DescriptorTable* readOnlyBuffers,
		core::Span<const NativeComputeTexture> fixedBindings,
		D3D12_CPU_DESCRIPTOR_HANDLE fixedCpu,
		D3D12_CPU_DESCRIPTOR_HANDLE fixedSamplerCpu,
		ID3D12DescriptorHeap* fixedHeap,
		ID3D12DescriptorHeap* fixedSamplerHeap,
		core::Span<const NativeComputeBuffer> buffers);
	[[nodiscard]] core::Result<void> RecordComputeDispatch(
		State* opaque,
		void* native,
		void* pipelineNative,
		core::Span<const NativeComputeBuffer> buffers,
		core::Span<const NativeComputeTexture> textures,
		core::uint32 groupsX,
		core::uint32 groupsY,
		core::uint32 groupsZ,
		void* indirectNative,
		core::uint64 indirectOffset,
		core::Span<const core::Byte> pushConstants,
		void* bindlessNative,
		void* samplerNative,
		void* storageBufferNative,
		void* storageTextureNative,
		void* uniformBufferNative,
		void* readOnlyBufferNative,
		void* accelNative);
	[[nodiscard]] core::Result<void> RecordComputeDispatchMultiple(
		State* opaque,
		void* native,
		void* pipelineNative,
		core::Span<const NativeBindingSet> sets,
		core::uint32 groupsX,
		core::uint32 groupsY,
		core::uint32 groupsZ,
		void* indirectNative,
		core::uint64 indirectOffset,
		core::Span<const core::Byte> pushConstants,
		void* sampledTable,
		void* samplerTable,
		void* storageBufferTable,
		void* storageTextureTable,
		void* uniformBufferTable,
		void* readOnlyBufferNative,
		void* accelNative);

	[[nodiscard]] core::Result<void*> CreateRayTracingPipeline(State* opaque, const NativeRayPipelineDesc& desc);
	void DestroyRayTracingPipelineNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyRayTracingPipeline(State* opaque, void* native);

	[[nodiscard]] core::Result<void> BindRayTextures(State& state, Pending& pending, const RayTracingPipeline& pipeline, core::Span<const NativeComputeTexture> textures);
	[[nodiscard]] core::Result<void> RecordRayDispatch(
		State* opaque,
		void* command,
		void* native,
		const NativeShaderTableDesc& table,
		core::Span<const NativeBindingSet> sets,
		core::Span<const NativeFixedBindingSet> fixedSets,
		core::Span<const core::Byte> pushConstants,
		void* accelNative,
		core::uint32 width,
		core::uint32 height,
		core::uint32 depth);

	inline constexpr D3D_PRIMITIVE_TOPOLOGY D3D_TOPOLOGIES[]{
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
		D3D_PRIMITIVE_TOPOLOGY_LINELIST,	 D3D_PRIMITIVE_TOPOLOGY_LINESTRIP,
		D3D_PRIMITIVE_TOPOLOGY_POINTLIST,	 D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST };

	inline constexpr D3D12_PRIMITIVE_TOPOLOGY_TYPE D3D_TOPOLOGY_TYPES[]{
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,		D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT,	D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH };

	inline constexpr D3D12_COMPARISON_FUNC D3D_COMPARE[]{
		D3D12_COMPARISON_FUNC_NEVER,		 D3D12_COMPARISON_FUNC_LESS,	D3D12_COMPARISON_FUNC_EQUAL,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,	 D3D12_COMPARISON_FUNC_GREATER, D3D12_COMPARISON_FUNC_NOT_EQUAL,
		D3D12_COMPARISON_FUNC_GREATER_EQUAL, D3D12_COMPARISON_FUNC_ALWAYS };

	inline constexpr D3D12_STENCIL_OP D3D_STENCIL_OPERATIONS[]{
		D3D12_STENCIL_OP_KEEP,	   D3D12_STENCIL_OP_ZERO,	D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_INCR_SAT,
		D3D12_STENCIL_OP_DECR_SAT, D3D12_STENCIL_OP_INVERT, D3D12_STENCIL_OP_INCR,	  D3D12_STENCIL_OP_DECR };

	inline constexpr D3D12_BLEND D3D_BLEND_FACTORS[]{
		D3D12_BLEND_ZERO,			D3D12_BLEND_ONE,		   D3D12_BLEND_SRC_COLOR,
		D3D12_BLEND_INV_SRC_COLOR,	D3D12_BLEND_DEST_COLOR,	   D3D12_BLEND_INV_DEST_COLOR,
		D3D12_BLEND_SRC_ALPHA,		D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_DEST_ALPHA,
		D3D12_BLEND_INV_DEST_ALPHA, D3D12_BLEND_BLEND_FACTOR,  D3D12_BLEND_INV_BLEND_FACTOR,
		D3D12_BLEND_SRC_ALPHA_SAT,	D3D12_BLEND_SRC1_COLOR,	   D3D12_BLEND_INV_SRC1_COLOR,
		D3D12_BLEND_SRC1_ALPHA,		D3D12_BLEND_INV_SRC1_ALPHA };

	inline constexpr D3D12_BLEND_OP D3D_BLEND_OPERATIONS[]{ D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_SUBTRACT,
														   D3D12_BLEND_OP_REV_SUBTRACT, D3D12_BLEND_OP_MIN,
														   D3D12_BLEND_OP_MAX }; /*
	 * Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
	 *
	 * Permission is hereby granted, free of charge, to any person obtaining a copy
	 * of this software and associated documentation files (the "Software"), to deal
	 * in the Software without restriction, including without limitation the rights
	 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	 * copies of the Software, and to permit persons to whom the Software is
	 * furnished to do so, subject to the following conditions:
	 *
	 * The above copyright notice and this permission notice shall be included in
	 * all copies or substantial portions of the Software.
	 *
	 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
	 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	 * THE SOFTWARE.
	 */

	[[nodiscard]] core::Result<void*> CreateGraphicsPipeline(State* opaque, const NativeGraphicsPipelineDesc& desc, core::Span<const NativeGraphicsBindingLayoutDesc> layouts);
	void DestroyGraphicsPipelineNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyGraphicsPipeline(State* opaque, void* native);

	[[nodiscard]] core::Result<void> RecordSetViewport(void*, void* native, core::Span<const ViewportDesc> viewports);
	[[nodiscard]] core::Result<void> RecordSetScissor(void*, void* native, core::Span<const ScissorDesc> scissors);

	[[nodiscard]] core::Result<void> BindFixedTextures(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		core::Span<const NativeSampledTexture> textures,
		core::Span<const NativeStorageTexture> storageTextures);
	void RestoreFixedStorageTextures(Pending& pending, core::Span<const NativeStorageTexture> storageTextures) noexcept;

	[[nodiscard]] core::Result<void> BindCombinedGraphicsDescriptors(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		const DescriptorTable* sampled,
		const DescriptorTable* samplers,
		const DescriptorTable* storage,
		const DescriptorTable* storageTextures,
		const DescriptorTable* uniform,
		const DescriptorTable* readOnly,
		core::Span<const NativeGraphicsBuffer> buffers);
	[[nodiscard]] core::Result<void> BindGraphicsDescriptorTable(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		void* native,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		core::Span<const NativeGraphicsBuffer> buffers);
	[[nodiscard]] core::Result<void> RefreshDirectGraphicsRoot(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		void* sampled,
		void* samplers,
		void* storage,
		void* storageTextures,
		void* uniform,
		void* readOnlyBufferNative);
	void RestoreGraphicsDescriptorTable(Pending& pending, void* native, void* storageNative, void* storageTextureNative, void* uniformNative, void* readOnlyBufferNative) noexcept;

	void BindGraphicsBuffers(Pending& pending, const GraphicsPipeline& pipeline, core::Span<const NativeGraphicsBuffer> buffers) noexcept;
	void RestoreGraphicsBuffers(Pending& pending, core::Span<const NativeGraphicsBuffer> buffers) noexcept;

	[[nodiscard]] core::Result<void> RecordBindExtraVertexBuffers(void*, void* native, core::Span<const NativeVertexBuffer> buffers);

	void ApplyBlendConstant(Pending& pending, const GraphicsPipeline& pipeline, const BlendColor& color) noexcept;

	[[nodiscard]] core::uint32 QuerySupportedShadingRates(State& state) noexcept;
	void ApplyShadingRate(Pending& pending, EShadingRate rate, EShadingRateCombiner primitive, EShadingRateCombiner image) noexcept;

	[[nodiscard]] core::Result<void> RecordDraw(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* vertexNative,
		core::uint64 vertexOffset,
		core::uint64 vertexBytes,
		core::uint32 stride,
		core::Span<const NativeSampledTexture> textures,
		core::Span<const NativeStorageTexture> storageTextures,
		core::Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsState& drawState,
		const DrawArguments& draw);
	[[nodiscard]] core::Result<void> RecordDrawIndexed(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* vertexNative,
		void* indexNative,
		core::uint64 vertexOffset,
		core::uint64 vertexBytes,
		core::uint64 indexOffset,
		core::uint32 stride,
		core::Span<const NativeSampledTexture> textures,
		core::Span<const NativeStorageTexture> storageTextures,
		core::Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsState& drawState,
		const DrawIndexedArguments& draw);
	[[nodiscard]] core::Result<void> RecordDrawIndirect(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* argumentNative,
		void* vertexNative,
		void* countNative,
		core::uint64 vertexOffset,
		core::uint64 vertexBytes,
		core::uint32 stride,
		core::Span<const NativeSampledTexture> textures,
		core::Span<const NativeStorageTexture> storageTextures,
		core::Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsDrawIndirect& draw);
	[[nodiscard]] core::Result<void> RecordDrawIndexedIndirect(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* argumentNative,
		void* vertexNative,
		void* indexNative,
		void* countNative,
		core::uint64 vertexOffset,
		core::uint64 vertexBytes,
		core::uint64 indexOffset,
		core::uint32 stride,
		core::Span<const NativeSampledTexture> textures,
		core::Span<const NativeStorageTexture> storageTextures,
		core::Span<const NativeGraphicsBuffer> buffers,
		void* bindlessNative,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative,
		void* accelNative,
		const GraphicsDrawIndexedIndirect& draw);

	[[nodiscard]] core::Result<void> WriteNativeFixedDescriptor(
		State& state,
		Pending* pending,
		const BindingRangeDesc& range,
		const NativeFixedDescriptor& value,
		D3D12_CPU_DESCRIPTOR_HANDLE destination);
	[[nodiscard]] core::Result<core::uint32> CaptureTableArenaRange(State& state, Pending& pending, DescriptorTable& table);
	void TransitionTableUses(Pending& pending, const DescriptorTable& table, core::Span<const core::uint32> used, bool bGraphics) noexcept;
	[[nodiscard]] core::Result<void> RecordDeviceHeapUses(State* opaque, void* native, bool bGraphics, core::Span<const NativeDescriptorHeapBinding> heaps);

	[[nodiscard]] core::Result<void> WriteFixedBindingSets(
		State* opaque,
		void* native,
		void* pipelineNative,
		bool bGraphics,
		core::Span<const NativeFixedBindingSet> sets,
		Pending::PreparedBinding* prepared = nullptr, RayTracingPipeline* rayPipeline = nullptr, bool bReuseRayHeap = false);
	[[nodiscard]] core::Result<bool> PrepareCachedFixedBindingSets(
		State& state,
		Pending& pending,
		const NativeFixedLayoutState& fixed,
		bool bGraphics,
		core::Span<const NativeFixedBindingSet> sets,
		Pending::PreparedBinding& prepared);
	void ReleaseCachedSamplerDescriptor(State& state, Sampler& sampler) noexcept;
	[[nodiscard]] core::Result<void> RecordFixedBindingSets(State* opaque, void* native, void* pipelineNative, bool bGraphics, core::Span<const NativeFixedBindingSet> sets);

	[[nodiscard]] core::Result<void*> CreateFramebuffer(State* opaque, const NativeFramebufferDesc& desc);
	void DestroyFramebufferNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyFramebuffer(State* opaque, void* native);

	[[nodiscard]] core::Result<void> RecordBeginFramebuffer(State* opaque, void* native, void* target, DepthAttachmentOptions depthOptions);
	[[nodiscard]] core::Result<void> RecordEndFramebuffer(void*, void* native, void* target);

	[[nodiscard]] bool ReserveSubmission(State* opaque) noexcept;
	[[nodiscard]] core::Result<core::uint64> SubmitCommandListImpl(
		State* opaque,
		void* native,
		core::Span<const RecordedCopy> copies,
		core::Span<const RecordedTextureCopy> textures,
		core::Span<const RecordedTextureUse> textureUses,
		core::Span<const RecordedBufferUse> bufferUses,
		core::Span<const RecordedAccelUse> accelUses,
		core::Span<const RecordedPipelineUse> pipelineUses,
		core::Span<const RecordedGraphicsPipelineUse> graphicsPipelineUses,
		core::Span<const RecordedRayPipelineUse> rayPipelineUses,
		core::Span<const RecordedFramebufferUse> framebufferUses,
		const NativePreparedSubmissionDesc* preparation = nullptr, core::uint32* queueWaitCount = nullptr);
	[[nodiscard]] core::Result<core::uint64> SubmitCommandList(
		State* opaque,
		void* native,
		core::Span<const RecordedCopy> copies,
		core::Span<const RecordedTextureCopy> textures,
		core::Span<const RecordedTextureUse> textureUses,
		core::Span<const RecordedBufferUse> bufferUses,
		core::Span<const RecordedAccelUse> accelUses,
		core::Span<const RecordedPipelineUse> pipelineUses,
		core::Span<const RecordedGraphicsPipelineUse> graphicsPipelineUses,
		core::Span<const RecordedRayPipelineUse> rayPipelineUses,
		core::Span<const RecordedFramebufferUse> framebufferUses);
	[[nodiscard]] NativePreparedSubmissionResult SubmitPreparedCommandList(
		State* opaque,
		void* native,
		const NativePreparedSubmissionDesc& preparation,
		core::Span<const RecordedTextureUse> textureUses,
		core::Span<const RecordedBufferUse> bufferUses,
		core::Span<const RecordedAccelUse> accelUses,
		core::Span<const RecordedPipelineUse> pipelineUses,
		core::Span<const RecordedGraphicsPipelineUse> graphicsPipelineUses,
		core::Span<const RecordedRayPipelineUse> rayPipelineUses,
		core::Span<const RecordedFramebufferUse> framebufferUses);

	void DiscardCommandList(State* opaque, void* native) noexcept;

	[[nodiscard]] core::Result<void> WaitIdleBounded(State* opaque, core::uint32 timeout);
	[[nodiscard]] core::Result<void> WaitIdle(State* opaque);

	void FreeSwapChainTargets(State& state, SwapChain& swapChain) noexcept;
	[[nodiscard]] bool AcquireSwapChainImages(State& state, SwapChain& swapChain) noexcept;

	[[nodiscard]] core::Result<void*> CreateSwapChain(State* opaque, const SwapChainDesc& desc);
	[[nodiscard]] core::Result<void> ResizeSwapChainWithPolicy(State* opaque, void* native, core::uint32 width, core::uint32 height, bool bPoll);
	[[nodiscard]] core::Result<void> ResizeSwapChain(State* opaque, void* native, core::uint32 width, core::uint32 height);
	[[nodiscard]] core::Result<void> TryResizeSwapChain(State* opaque, void* native, core::uint32 width, core::uint32 height);

	[[nodiscard]] core::Result<core::uint64> Present(State* opaque, void* native, void* sourceNative);

	[[nodiscard]] core::Result<void*> AcquireSwapChainImage(State* opaque, void* native, core::uint32);
	[[nodiscard]] core::Result<void> DiscardAcquiredSwapChainImage(void*, void* native);
	[[nodiscard]] core::Result<core::uint64> PresentSwapChainImage(State* opaque, void* native);

	void DestroySwapChainNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroySwapChain(State* opaque, void* native);
	[[nodiscard]] core::Result<void> TryDestroySwapChain(State* opaque, void* native);

	void Destroy(State* opaque) noexcept;

	[[nodiscard]] core::Result<void> RecordShaderMemoryBarrier(void*, void* native);
	[[nodiscard]] core::Result<void> RecordShaderBufferBarrier(void*, void* native, void* bufferNative);
	[[nodiscard]] core::Result<void> RecordShaderTextureBarrier(void*, void* native, void* textureNative);

	[[nodiscard]] D3D12_RESOURCE_STATES MapTextureState(ETextureState state, EQueue queue) noexcept;
	[[nodiscard]] core::Result<void> RecordPrepareTextureForShaderAccess(void*, void* native, void* textureNative, const TextureViewDesc* range, ETextureState requested);
	[[nodiscard]] core::Result<void> RecordBeginTrackingTextureState(void*, void* native, void* textureNative, const TextureSubresource* subresource, ETextureState state);
	[[nodiscard]] core::Result<void> RecordSetTextureState(void*, void* native, void* textureNative, const TextureSubresource* subresource, ETextureState state);

	[[nodiscard]] D3D12_RESOURCE_STATES MapBufferState(EBufferState state, EQueue queue) noexcept;
	[[nodiscard]] core::Result<void> RecordBeginTrackingBufferState(void*, void* native, void* bufferNative, EBufferState state);
	[[nodiscard]] core::Result<void> RecordSetBufferState(void*, void* native, void* bufferNative, EBufferState state);

	[[nodiscard]] CommandListStatistics GetCommandListStatistics(const void* native) noexcept;
	[[nodiscard]] bool HasMissingManualState(const void* native) noexcept;

	[[nodiscard]] core::Result<void> PrepareRecordingGroup(State* opaque, core::Span<const NativePreparedList> lists);
	void ResetPreparedCommandList(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> CapturePreparedStorage(
		Pending::PreparedBinding& binding,
		const NativeFixedLayoutState& fixed,
		core::Span<const NativeFixedBindingSet> sets,
		core::Span<const NativeDescriptorHeapBinding> heaps = {});
	[[nodiscard]] core::Result<core::uint32> PrepareBinding(
		State* opaque,
		void* native,
		void* pipeline,
		bool bGraphics,
		core::Span<const NativeFixedBindingSet> sets,
		core::Span<const NativePreparedTextureAccess>,
		const NativePreparedVertexInput& input,
		core::Span<const NativeDescriptorHeapBinding> heaps,
		core::SharedPtr<NativeBindingProgram>* program);
	[[nodiscard]] core::Result<void> RecordPreparedPhase(State* opaque, void* native, const NativePreparedPhase& phase);
	void BindPrepared(Pending& pending, void* native, bool bGraphics, core::uint32 index) noexcept;

	void BindPreparedCompute(void*, void* native, void* pipelineNative, core::uint32 binding, core::Span<const core::Byte> pushConstants);
	[[nodiscard]] core::Result<void> RecordPreparedDispatch(void*, void* native, void* pipelineNative, core::uint32 binding, const ComputeDispatch& dispatch);

	void BindPreparedGraphics(void* native, void* pipelineNative, core::uint32 binding, core::Span<const core::Byte> pushConstants);
	[[nodiscard]] core::Result<void> RecordPreparedMeshDispatch(void*, void* native, void* pipelineNative, core::uint32 binding, const MeshDispatch& dispatch);
	[[nodiscard]] core::Result<void> RecordPreparedDraw(void*, void* native, void* pipelineNative, core::uint32 binding, const DrawArguments& draw);
	[[nodiscard]] core::Result<void> RecordDrawIndexedArguments(void*, void* native, void* pipelineNative, core::uint32 binding, const DrawIndexedArguments& draw);

	[[nodiscard]] core::Result<core::uint32> PrepareIndirect(State* opaque, void* native, const NativePreparedIndirect& indirect);
	[[nodiscard]] core::Result<void> RecordPreparedIndirect(
		State* opaque,
		void* native,
		void* pipelineNative,
		core::uint32 binding,
		const NativePreparedIndirect& indirect,
		core::Span<const core::Byte> pushConstants);

	[[nodiscard]] core::Result<void> RecordPreparedRendering(
		State* opaque,
		void* native,
		void* target,
		core::Span<const EAttachmentLoad> loads,
		core::Span<const ClearColor> colors,
		DepthAttachmentOptions depthOptions);

	[[nodiscard]] core::Result<void> FinishPreparedCommandList(void*, void* native);

	[[nodiscard]] core::Result<void> QueueWaitForPreparedSubmission(State* opaque, EQueue waitingQueue, EQueue executionQueue, core::uint64 serial);

	[[nodiscard]] core::Result<core::uint32> DescribePreparedNativeBoundary(const void*, const void* native, const void* resourceNative, bool bTexture) noexcept;

	[[nodiscard]] core::Result<core::uint32> PrepareAccelBuild(State* opaque, void* native, const NativePreparedAccelBuild& source);
	[[nodiscard]] core::Result<void> RecordPreparedAccelBuild(void*, void* native, core::uint32 index);

	[[nodiscard]] core::Result<core::uint32> PrepareAccelQuery(State* opaque, void* native, void* source, core::uint64 generation);
	[[nodiscard]] core::Result<void> RecordPreparedAccelQuery(void*, void* native, core::uint32 index);

	[[nodiscard]] core::Result<bool> IsComplete(const State*, core::uint64);
	[[nodiscard]] core::Result<AccelCompactionResult> TryGetAccelCompactedSize(State* opaque, void* source, core::uint64 generation);

	[[nodiscard]] core::Result<core::uint32> PrepareAccelCompact(State* opaque, void* native, void* source, void* target);
	[[nodiscard]] core::Result<void> RecordPreparedAccelCompact(void*, void* native, core::uint32 index);
	void CommitPreparedAccelQuery(void*, void* source, core::uint64 generation, EQueue queue, core::uint64 serial, bool bUncertain) noexcept;
	void CancelPreparedAccelQuery(void*, void* source, core::uint64 generation) noexcept;

	void DestroyPreparedShaderTable(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<core::SharedPtr<PreparedShaderTable>> CreatePreparedShaderTable(State* opaque, void* pipelineNative, const NativeShaderTableDesc& table);
	[[nodiscard]] core::Result<core::uint32> PrepareRayBinding(
		State* opaque,
		void* native,
		void* pipelineNative,
		const core::SharedPtr<PreparedShaderTable>& table,
		core::Span<const NativeFixedBindingSet> sets,
		core::Span<const NativePreparedTextureAccess>);
	[[nodiscard]] core::Result<void> RecordPreparedRayDispatch(void*, void* native, void* pipelineNative, core::uint32 index, const RayDispatch& dispatch);
	[[nodiscard]] core::Result<void> RecordImmutableRayDispatch(
		State* opaque,
		void* native,
		void* pipelineNative,
		const core::SharedPtr<PreparedShaderTable>& table,
		core::Span<const NativeFixedBindingSet> globals,
		const RayDispatch& dispatch);

	struct PreparedTextureMappingsStorage final
	{
		Texture* Target = nullptr;
		const NativeTextureTileMapping* Mappings = nullptr;
		core::usize Count = 0;

		EQueue Queue = EQueue::Graphics;
		bool bCredit = false;
	};

	[[nodiscard]] core::Result<void> PrepareTextureTileMappings(
		State* opaque,
		void* native,
		const TextureDesc& desc,
		core::Span<const NativeTextureTileMapping> mappings,
		EQueue queueKind,
		core::MemoryBlock& storage);
	[[nodiscard]] NativePreparedSubmissionResult SubmitPreparedTextureMappings(State* opaque, core::MemoryBlock& storage, core::Span<const NativePreparedSubmissionWait> prerequisites);
	void DestroyPreparedTextureMappings(State* opaque, core::MemoryBlock& storage) noexcept;

	[[nodiscard]] DeviceLimits GetLimits(const State* opaque) noexcept;

	struct ExternalFenceRecord final
	{
		ComPtr<ID3D12Fence> Fence;
	};

	[[nodiscard]] ExternalInteropCapabilities GetExternalInteropCapabilities(const State* opaque);

	[[nodiscard]] core::Result<ComPtr<ID3D12Resource>> MakeSharedResource(State& state, const D3D12_RESOURCE_DESC& desc, const NativeExternalMemoryView* imported);
	[[nodiscard]] core::Result<void*> CreateExternalBuffer(State* opaque, const BufferDesc& desc, const NativeExternalMemoryView* imported);
	[[nodiscard]] core::Result<void*> CreateExternalTexture(State* opaque, const TextureDesc& desc, const NativeExternalMemoryView* imported);
	[[nodiscard]] core::Result<NativeExternalMemory> ExportExternalMemory(State* opaque, void* native, bool bTexture);

	[[nodiscard]] core::Result<void*> CreateExternalFence(State* opaque, core::uint64 initial, const ExternalHandleView* imported);
	void DestroyExternalFence(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<ExternalFence> ExportExternalFence(State* opaque, void* native);
	[[nodiscard]] core::Result<core::uint64> GetExternalFenceValue(void*, void* native);

	[[nodiscard]] core::Result<void> RecordExternalTransfer(void*, void* commands, void* resource, bool bTexture, void* fence, core::uint64 value, bool bAcquire);

	core::Result<BackendData> CreateD3D12Backend(
		core::MemoryResource& resource,
		bool bEnableValidation,
		bool bEnableDeviceHeapIndexing,
		EAdapterPreference preference,
		NativeDeviceHandles externalDevice,
		NativeQueueHandle externalComputeQueue,
		NativeQueueHandle externalCopyQueue,
		IGpuCrashDiagnostics* diagnostics);
}
#endif

namespace rox::d3d12
{
	using namespace rox::rhi;
	inline constexpr bool HAS_HAS_MISSING_MANUAL_STATE = true;
	inline constexpr bool HAS_SUPPORTS_TEXTURE_RESOLVE = true;
	inline constexpr bool HAS_PREPARE_TEXTURE_RESOLVE = true;
	inline constexpr bool HAS_QUERY_COOP_VEC_FEATURES = true;
	inline constexpr bool HAS_GET_COOP_VEC_MATRIX_SIZE = true;
	inline constexpr bool HAS_PROPAGATE_ACCEL_STRUCT_LAST_USE = true;
	inline constexpr bool HAS_PROPAGATE_ACCEL_STRUCT_LAST_USE_TO_BUFFER = true;
	inline constexpr bool HAS_GET_BOTTOM_LEVEL_ACCEL_STRUCT_COMPACTED_SIZE = true;
	inline constexpr bool HAS_RECORD_COMPACT_BOTTOM_LEVEL_ACCEL_STRUCT = true;
	inline constexpr bool HAS_GET_CLUSTER_OPERATION_SIZE_INFO = true;
	inline constexpr bool HAS_RECORD_CLUSTER_OPERATION = true;
	inline constexpr bool HAS_GET_HEAP_COMMITTED_BYTES = true;
	inline constexpr bool HAS_PROPAGATE_BUFFER_LAST_USE = true;
	inline constexpr bool HAS_CREATE_SAMPLER_FEEDBACK_TEXTURE = true;
	inline constexpr bool HAS_IMPORT_SAMPLER_FEEDBACK_TEXTURE = true;
	inline constexpr bool HAS_GET_TEXTURE_TILING = true;
	inline constexpr bool HAS_UPDATE_TEXTURE_TILE_MAPPINGS = true;
	inline constexpr bool HAS_PREPARE_TEXTURE_TILE_MAPPINGS = true;
	inline constexpr bool HAS_GET_DESCRIPTOR_TABLE_DEVICE_HEAP_INDEX = true;
	inline constexpr bool HAS_GET_PRESENT_COPY_SERIAL = true;
	inline constexpr bool HAS_RECORD_COOP_VEC_MATRICES = true;
	inline constexpr bool HAS_PREPARE_TEXTURE_BUFFER_COPY = true;
	inline constexpr bool HAS_RECORD_COMPUTE_DISPATCH_MULTIPLE = true;
	inline constexpr bool HAS_RESERVE_SUBMISSION = true;
	inline constexpr bool HAS_SUBMIT_PREPARED_COMMAND_LIST = true;
	inline constexpr bool HAS_HAS_RETIREMENT_PROOF = true;
	inline constexpr bool HAS_CREATE_RAY_TRACING_PIPELINE = true;
	inline constexpr bool HAS_RECORD_RAY_DISPATCH = true;
	inline constexpr bool HAS_RECORD_FIXED_BINDING_SETS = true;
	inline constexpr bool HAS_PREPARE_ACCEL_BUILD = true;
	inline constexpr bool HAS_PREPARE_ACCEL_QUERY = true;
	inline constexpr bool HAS_TRY_GET_ACCEL_COMPACTED_SIZE = true;
	inline constexpr bool HAS_PREPARE_ACCEL_COMPACT = true;
	inline constexpr bool HAS_CREATE_PREPARED_SHADER_TABLE = true;
	inline constexpr bool HAS_PREPARE_RAY_BINDING = true;
	inline constexpr bool HAS_RECORD_PREPARED_RAY_DISPATCH = true;
	inline constexpr bool HAS_GET_LIMITS = true;
	inline constexpr bool HAS_GET_TEXTURE_BUFFER_FOOTPRINT = true;
	inline constexpr bool HAS_GET_EXTERNAL_INTEROP_CAPABILITIES = true;
	inline constexpr bool HAS_CREATE_EXTERNAL_BUFFER = true;
	inline constexpr bool HAS_CREATE_EXTERNAL_TEXTURE = true;
	inline constexpr bool HAS_EXPORT_EXTERNAL_MEMORY = true;
	inline constexpr bool HAS_CREATE_EXTERNAL_FENCE = true;
	inline constexpr bool HAS_EXPOSE_NATIVE_QUEUES = true;
	inline constexpr bool HAS_WAS_SUBMISSION_ACCEPTED = true;
	inline constexpr bool HAS_PREPARE_RECORDING_GROUP = true;
	inline constexpr bool HAS_RECORD_PREPARED_MESH_DISPATCH = true;
	inline constexpr bool HAS_PREPARE_INDIRECT = true;
	inline constexpr bool HAS_RECORD_PREPARED_INDIRECT = true;
	inline constexpr bool HAS_RECORD_IMMUTABLE_RAY_DISPATCH = true;
	inline constexpr bool HAS_DESCRIBE_PREPARED_NATIVE_BOUNDARY = true;
	inline constexpr bool HAS_DISCARD_ACQUIRED_SWAP_CHAIN_IMAGE = true;
	inline constexpr bool HAS_RECORD_DEVICE_HEAP_USES = true;
}

namespace rox::d3d12
{
	core::Result<void*> CreateBufferView(State*, void*, const BufferViewDesc&);
	void DestroyBufferView(State*, void*) noexcept;

	core::Result<void*> CreateDescriptorTable(State*, const DescriptorTableDesc&);
	core::uint32 GetDescriptorTableDeviceHeapIndex(const State*, const void*) noexcept;
	core::Result<void> WriteDescriptorTable(State*, void*, core::uint32, void*, void*, void*, void*, BufferRange);
	core::Result<void> CopyDescriptorTableEntries(State*, const void*, core::uint32, void*, core::uint32, core::uint32);
	void DestroyDescriptorTable(State*, void*) noexcept;
}

namespace rox::d3d12
{
	void DestroyPreparedShaderTable(State* opaque, void* native) noexcept;

	void DestroyDescriptorSetRealization(State* opaque, void* native) noexcept;
}
