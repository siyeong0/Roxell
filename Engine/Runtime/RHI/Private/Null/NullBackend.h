#pragma once

#include "../DeviceResources.h"
#include "../DevicePipelines.h"

namespace rox::nullrhi
{
	using namespace rox::rhi;

	struct State;

	CommandListStatistics GetCommandListStatistics(const void*) noexcept;
	inline constexpr bool HAS_HAS_MISSING_MANUAL_STATE = false;
	bool HasMissingManualState(const void*) noexcept;

	EFormatSupport QueryFormatSupport(const void*, EFormat) noexcept;
	core::Result<core::uint32> QueryTextureSampleCounts(const void*, const TextureDesc&);
	bool SupportsColorResolve(const void*, EFormat) noexcept;
	inline constexpr bool HAS_SUPPORTS_TEXTURE_RESOLVE = false;
	bool SupportsTextureResolve(const void*, const TextureDesc&, ETextureResolveMode, ETextureResolveMode) noexcept;
	inline constexpr bool HAS_PREPARE_TEXTURE_RESOLVE = false;
	core::Result<void> PrepareTextureResolve(void*, void*, NativeTextureResolve&);

	inline constexpr bool HAS_QUERY_COOP_VEC_FEATURES = false;
	core::Result<CoopVecFeatures> QueryCoopVecFeatures(const void*, core::Span<CoopVecFormatCombo>);
	inline constexpr bool HAS_GET_COOP_VEC_MATRIX_SIZE = false;
	core::Result<core::uint64> GetCoopVecMatrixSize(const void*, ECoopVecDataType, ECoopVecMatrixLayout, core::uint32, core::uint32);

	core::Result<MemoryRequirements> GetBufferMemoryRequirements(const void*, const BufferDesc&);
	core::Result<core::uint32> QueryMemoryBudget(const void*, core::Span<MemoryBudget>);

	core::Result<OpacityMicromapBuildSizes> GetOpacityMicromapBuildSizes(const void*, core::Span<const OpacityMicromapUsageCount>, EOpacityMicromapBuildFlags);
	core::Result<void*> CreateOpacityMicromap(void*, const OpacityMicromapDesc&, void*);
	core::Result<void> DestroyOpacityMicromap(void*, void*, void*);
	void DestroyOpacityMicromapNow(void*, void*) noexcept;
	core::Result<void> RecordBuildOpacityMicromap(void*, void*, void*, const OpacityMicromapBuildDesc&, void*, core::uint64, void*, core::uint64, void*, core::uint64);

	core::Result<AccelStructBuildSizes> GetAccelStructBuildSizes(const void*, const AccelStructBuildDesc&, core::Span<const NativeAccelTriangleGeometry>, core::Span<const NativeAccelAabbGeometry>);
	core::Result<void*> CreateAccelStruct(void*, const AccelStructDesc&, void*);
	core::Result<void> DestroyAccelStruct(void*, void*);
	void DestroyAccelStructNow(void*, void*) noexcept;
	core::uint64 GetAccelStructDeviceAddress(const void*, const void*) noexcept;
	inline constexpr bool HAS_PROPAGATE_ACCEL_STRUCT_LAST_USE = false;
	void PropagateAccelStructLastUse(const void*, const void*, void*) noexcept;
	inline constexpr bool HAS_PROPAGATE_ACCEL_STRUCT_LAST_USE_TO_BUFFER = false;
	void PropagateAccelStructLastUseToBuffer(const void*, const void*, void*) noexcept;
	inline constexpr bool HAS_GET_BOTTOM_LEVEL_ACCEL_STRUCT_COMPACTED_SIZE = false;
	core::Result<core::uint64> GetBottomLevelAccelStructCompactedSize(void*, void*);
	inline constexpr bool HAS_RECORD_COMPACT_BOTTOM_LEVEL_ACCEL_STRUCT = false;
	core::Result<void> RecordCompactBottomLevelAccelStruct(void*, void*, void*, void*);
	core::Result<void> RecordBuildBottomLevelAccelStruct(
		void*,
		void*,
		void*,
		const AccelStructBuildDesc&,
		core::Span<const NativeAccelTriangleGeometry>,
		core::Span<const NativeAccelAabbGeometry>,
		void*,
		core::uint64);
	core::Result<void> RecordBuildTopLevelAccelStruct(void*, void*, void*, const AccelStructBuildDesc&, core::Span<const NativeAccelInstance>, void*, core::uint64);
	core::Result<void> RecordBuildTopLevelAccelStructFromBuffer(void*, void*, void*, const AccelStructBuildDesc&, void*, core::uint64, void*, core::uint64);
	inline constexpr bool HAS_GET_CLUSTER_OPERATION_SIZE_INFO = false;
	core::Result<ClusterOperationSizeInfo> GetClusterOperationSizeInfo(const void*, const ClusterOperationParams&);
	inline constexpr bool HAS_RECORD_CLUSTER_OPERATION = false;
	core::Result<void> RecordClusterOperation(void*, void*, const ClusterOperationDesc&, void*, void*, void*, void*, void*, void*);
	core::Result<void> RecordSetAccelStructState(void*, void*, void*, EAccelStructState);
	core::Result<MemoryRequirements> GetTextureMemoryRequirements(const void*, const TextureDesc&);

	core::Result<void*> CreateHeap(void*, const HeapDesc&);
	inline constexpr bool HAS_GET_HEAP_COMMITTED_BYTES = false;
	core::uint64 GetHeapCommittedBytes(const void*) noexcept;
	core::Result<void> DestroyHeap(void*, void*);
	void DestroyHeapNow(void*, void*) noexcept;

	core::Result<void*> CreateBuffer(State*, const BufferDesc&);
	core::Result<void*> ImportBuffer(void*, const BufferDesc&, void*);
	void* GetNativeBuffer(const void*) noexcept;
	core::Result<void*> CreatePlacedBuffer(void*, const BufferDesc&, void*, core::uint64);
	core::uint64 GetBufferLastUse(const void*) noexcept;
	inline constexpr bool HAS_PROPAGATE_BUFFER_LAST_USE = false;
	void PropagateBufferLastUse(const void*, const void*, void*) noexcept;
	core::uint64 GetBufferDeviceAddress(const void*, const void*) noexcept;
	core::Result<void> DestroyBuffer(State*, void*);
	void DestroyBufferNow(State*, void*) noexcept;
	core::Result<core::Byte*> MapBuffer(void*, void*, core::uint64, core::uint64, core::uint32);
	core::Result<void> UnmapBuffer(void*, void*);

	core::Result<void*> CreateTexture(State*, const TextureDesc&);
	inline constexpr bool HAS_CREATE_SAMPLER_FEEDBACK_TEXTURE = false;
	core::Result<void*> CreateSamplerFeedbackTexture(void*, void*, const TextureDesc&, const SamplerFeedbackDesc&);
	inline constexpr bool HAS_IMPORT_SAMPLER_FEEDBACK_TEXTURE = false;
	core::Result<void*> ImportSamplerFeedbackTexture(void*, void*, const TextureDesc&, const SamplerFeedbackDesc&, void*);
	core::Result<void*> ImportTexture(void*, const TextureDesc&, void*, ENativeTextureContents);
	void* GetNativeTexture(const void*) noexcept;
	inline constexpr bool HAS_GET_TEXTURE_TILING = false;
	core::Result<TextureTiling> GetTextureTiling(const void*, const void*, core::Span<TextureSubresourceTiling>);
	inline constexpr bool HAS_UPDATE_TEXTURE_TILE_MAPPINGS = false;
	core::Result<core::uint64> UpdateTextureTileMappings(void*, void*, const TextureDesc&, core::Span<const NativeTextureTileMapping>, EQueue);
	inline constexpr bool HAS_PREPARE_TEXTURE_TILE_MAPPINGS = false;
	core::Result<void> PrepareTextureTileMappings(void*, void*, const TextureDesc&, core::Span<const NativeTextureTileMapping>, EQueue, core::MemoryBlock&);
	NativePreparedSubmissionResult SubmitPreparedTextureMappings(void*, core::MemoryBlock&, core::Span<const NativePreparedSubmissionWait>);
	void DestroyPreparedTextureMappings(void*, core::MemoryBlock&) noexcept;
	core::Result<void*> CreatePlacedTexture(void*, const TextureDesc&, void*, core::uint64);
	core::uint64 GetTextureLastUse(const void*) noexcept;
	core::Result<void> DestroyTexture(State*, void*);
	void DestroyTextureNow(State*, void*) noexcept;
	core::Result<void*> CreateTextureView(void*, void*, const TextureDesc&, const TextureViewDesc&);
	void DestroyTextureView(void*, void*) noexcept;

	core::Result<void*> CreateBufferView(void*, void*, const BufferViewDesc&);
	void DestroyBufferView(void*, void*) noexcept;

	core::Result<void*> CreateSampler(void*, const SamplerDesc&);
	void DestroySampler(void*, void*) noexcept;

	core::Result<void*> CreateDescriptorTable(void*, const DescriptorTableDesc&);
	inline constexpr bool HAS_GET_DESCRIPTOR_TABLE_DEVICE_HEAP_INDEX = false;
	core::uint32 GetDescriptorTableDeviceHeapIndex(const void*, const void*) noexcept;
	core::Result<void> WriteDescriptorTable(void*, void*, core::uint32, void*, void*, void*, void*, BufferRange);
	core::Result<void> CopyDescriptorTableEntries(void*, const void*, core::uint32, void*, core::uint32, core::uint32);
	void DestroyDescriptorTable(void*, void*) noexcept;

	core::Result<void*> CreateSwapChain(void*, const SwapChainDesc&);
	core::Result<void> ResizeSwapChain(void*, void*, core::uint32, core::uint32);
	core::Result<void> TryResizeSwapChain(void*, void*, core::uint32, core::uint32);
	core::Result<void*> AcquireSwapChainImage(void*, void*, core::uint32);
	core::Result<core::uint64> PresentSwapChainImage(void*, void*);
	core::Result<core::uint64> Present(void*, void*, void*);
	inline constexpr bool HAS_GET_PRESENT_COPY_SERIAL = false;
	core::uint64 GetPresentCopySerial(const void*) noexcept;
	core::Result<void> DestroySwapChain(void*, void*);
	core::Result<void> TryDestroySwapChain(void*, void*);
	void DestroySwapChainNow(void*, void*) noexcept;

	core::Result<core::uint64> UploadBuffer(State*, void*, core::uint64, core::Span<const core::Byte>);
	core::Result<void> ReadBuffer(void*, void*, core::uint64, core::Span<core::Byte>, core::uint32);
	core::Result<core::uint64> UploadTexture(State*, void*, const TextureDesc&, TextureSubresource, core::Span<const core::Byte>);
	core::Result<void> ReadTexture(void*, void*, const TextureDesc&, TextureSubresource, core::Span<core::Byte>, core::uint32);

	core::Result<void*> CreateCommandList(State*, EQueue);
	void* GetNativeCommandList(const void*) noexcept;
	void InvalidateCommandState(void*) noexcept;
	void SetAutomaticBarriers(void*, bool) noexcept;
	core::Result<void> SetBufferUavBarriers(void*, void*, bool);
	core::Result<void> SetTextureUavBarriers(void*, void*, bool);
	core::Result<void> RecordShaderMemoryBarrier(void*, void*);
	core::Result<void> RecordShaderBufferBarrier(void*, void*, void*);
	core::Result<void> RecordShaderTextureBarrier(void*, void*, void*);
	core::Result<void> RecordPrepareTextureForShaderAccess(void*, void*, void*, const TextureViewDesc*, ETextureState);
	core::Result<void> RecordSetTextureState(void*, void*, void*, const TextureSubresource*, ETextureState);
	core::Result<void> RecordBeginTrackingTextureState(void*, void*, void*, const TextureSubresource*, ETextureState);
	core::Result<void> RecordSetSamplerFeedbackState(void*, void*, void*, ESamplerFeedbackState);
	ETrackedResourceState GetTextureSubresourceState(const void*, const void*, const void*, TextureSubresource) noexcept;
	core::Result<void> RecordSetBufferState(void*, void*, void*, EBufferState);
	core::Result<void> RecordBeginTrackingBufferState(void*, void*, void*, EBufferState);

	core::Result<void> RecordBeginMarker(void*, void*, const char*);
	core::Result<void> RecordEndMarker(void*, void*);

	core::Result<void*> CreateGpuTimer(void*);
	void DestroyGpuTimer(void*, void*) noexcept;
	core::Result<void> RecordGpuTimerBegin(void*, void*, void*);
	core::Result<void> RecordGpuTimerEnd(void*, void*, void*);
	core::Result<double> ReadGpuTimer(void*, void*);

	core::Result<void> RecordBufferCopy(void*, void*, const NativeCopy&);

	inline constexpr bool HAS_RECORD_COOP_VEC_MATRICES = false;
	core::Result<void> RecordCoopVecMatrices(void*, void*, core::Span<const NativeCoopVecMatrixConversion>, bool);

	core::Result<void> RecordBufferWrite(State*, void*, void*, core::uint64, core::Span<const core::Byte>);
	core::Result<void> RecordBufferClear(void*, void*, void*, core::uint64, core::uint32);

	core::Result<void> RecordTextureCopy(void*, void*, const NativeTextureCopy&);
	core::Result<void> RecordBufferToTexture(void*, void*, const NativeTextureBufferCopy&);
	core::Result<void> RecordTextureToBuffer(void*, void*, const NativeTextureBufferCopy&);
	inline constexpr bool HAS_PREPARE_TEXTURE_BUFFER_COPY = false;
	core::Result<void> PrepareTextureBufferCopy(void*, NativeTextureBufferCopy&);
	core::Result<void> RecordTextureWrite(State*, void*, void*, const TextureDesc&, TextureSubresource, core::Span<const core::Byte>);
	core::Result<void> RecordTextureResolve(void*, void*, const NativeTextureResolve&);
	core::Result<void> RecordTextureClear(void*, void*, void*, const TextureDesc&, TextureSubresource, const ClearColor&);
	core::Result<void> RecordTextureUIntClear(void*, void*, void*, const TextureDesc&, TextureSubresource, core::uint32);

	core::Result<void> RecordSamplerFeedbackClear(void*, void*, void*);
	core::Result<void> RecordSamplerFeedbackDecode(void*, void*, void*, void*);
	core::Result<void> RecordSamplerFeedbackDecodeTexture(void*, void*, void*, void*);

	core::Result<void> RecordDepthClear(void*, void*, void*, const TextureDesc&, TextureSubresource, DepthStencilClear);

	core::Result<void*> CreateComputePipeline(void*, const NativeComputePipelineDesc&, core::Span<const NativeBindingLayoutDesc>);
	core::Result<void> DestroyComputePipeline(void*, void*);
	void DestroyComputePipelineNow(void*, void*) noexcept;

	core::Result<void> RecordComputeDispatch(
		void*,
		void*,
		void*,
		core::Span<const NativeComputeBuffer>,
		core::Span<const NativeComputeTexture>,
		core::uint32,
		core::uint32,
		core::uint32,
		void*,
		core::uint64,
		core::Span<const core::Byte>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*);
	inline constexpr bool HAS_RECORD_COMPUTE_DISPATCH_MULTIPLE = false;
	core::Result<void> RecordComputeDispatchMultiple(
		void*,
		void*,
		void*,
		core::Span<const NativeBindingSet>,
		core::uint32,
		core::uint32,
		core::uint32,
		void*,
		core::uint64,
		core::Span<const core::Byte>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*);

	core::Result<void*> CreateGraphicsPipeline(void*, const NativeGraphicsPipelineDesc&, core::Span<const NativeGraphicsBindingLayoutDesc>);
	core::Result<void> DestroyGraphicsPipeline(void*, void*);
	void DestroyGraphicsPipelineNow(void*, void*) noexcept;

	core::Result<void*> CreateFramebuffer(void*, const NativeFramebufferDesc&);
	core::Result<void> DestroyFramebuffer(void*, void*);
	void DestroyFramebufferNow(void*, void*) noexcept;
	core::Result<void> RecordBeginFramebuffer(void*, void*, void*, DepthAttachmentOptions);

	core::Result<void> RecordSetViewport(void*, void*, core::Span<const ViewportDesc>);
	core::Result<void> RecordSetScissor(void*, void*, core::Span<const ScissorDesc>);
	core::Result<void> RecordBindExtraVertexBuffers(void*, void*, core::Span<const NativeVertexBuffer>);

	core::Result<void> RecordDraw(
		void*,
		void*,
		void*,
		void*,
		core::uint64,
		core::uint64,
		core::uint32,
		core::Span<const NativeSampledTexture>,
		core::Span<const NativeStorageTexture>,
		core::Span<const NativeGraphicsBuffer>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		const GraphicsState&,
		const DrawArguments&);
	core::Result<void> RecordDrawIndexed(
		void*,
		void*,
		void*,
		void*,
		void*,
		core::uint64,
		core::uint64,
		core::uint64,
		core::uint32,
		core::Span<const NativeSampledTexture>,
		core::Span<const NativeStorageTexture>,
		core::Span<const NativeGraphicsBuffer>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		const GraphicsState&,
		const DrawIndexedArguments&);
	core::Result<void> RecordDrawIndirect(
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		core::uint64,
		core::uint64,
		core::uint32,
		core::Span<const NativeSampledTexture>,
		core::Span<const NativeStorageTexture>,
		core::Span<const NativeGraphicsBuffer>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		const GraphicsDrawIndirect&);
	core::Result<void> RecordDrawIndexedIndirect(
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		core::uint64,
		core::uint64,
		core::uint64,
		core::uint32,
		core::Span<const NativeSampledTexture>,
		core::Span<const NativeStorageTexture>,
		core::Span<const NativeGraphicsBuffer>,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		void*,
		const GraphicsDrawIndexedIndirect&);
	core::Result<void> RecordEndFramebuffer(void*, void*, void*);

	inline constexpr bool HAS_RESERVE_SUBMISSION = false;
	bool ReserveSubmission(void*) noexcept;
	core::Result<core::uint64> SubmitCommandList(
		State*,
		void*,
		core::Span<const RecordedCopy>,
		core::Span<const RecordedTextureCopy>,
		core::Span<const RecordedTextureUse>,
		core::Span<const RecordedBufferUse>,
		core::Span<const RecordedAccelUse>,
		core::Span<const RecordedPipelineUse>,
		core::Span<const RecordedGraphicsPipelineUse>,
		core::Span<const RecordedRayPipelineUse>,
		core::Span<const RecordedFramebufferUse>);
	inline constexpr bool HAS_SUBMIT_PREPARED_COMMAND_LIST = false;
	NativePreparedSubmissionResult SubmitPreparedCommandList(
		void*,
		void*,
		const NativePreparedSubmissionDesc&,
		core::Span<const RecordedTextureUse>,
		core::Span<const RecordedBufferUse>,
		core::Span<const RecordedAccelUse>,
		core::Span<const RecordedPipelineUse>,
		core::Span<const RecordedGraphicsPipelineUse>,
		core::Span<const RecordedRayPipelineUse>,
		core::Span<const RecordedFramebufferUse>);
	void DiscardCommandList(State*, void*) noexcept;

	core::Result<bool> IsComplete(const State*, core::uint64);
	core::Result<bool> IsQueueComplete(const State*, EQueue, core::uint64);
	core::uint64 GetQueueLastSubmittedSerial(const State*, EQueue) noexcept;
	core::Result<void> Wait(State*, EQueue, core::uint64, core::uint32);
	core::Result<void> QueueWaitForSubmission(State*, EQueue, EQueue, core::uint64);

	void Collect(void*) noexcept;
	core::Result<void> WaitIdle(void*);
	core::Result<void> WaitIdleBounded(void*, core::uint32);
	inline constexpr bool HAS_HAS_RETIREMENT_PROOF = false;
	bool HasRetirementProof(const void*) noexcept;
	void Destroy(State*) noexcept;

	inline constexpr bool HAS_CREATE_RAY_TRACING_PIPELINE = false;
	core::Result<void*> CreateRayTracingPipeline(void*, const NativeRayPipelineDesc&);
	core::Result<void> DestroyRayTracingPipeline(void*, void*);
	void DestroyRayTracingPipelineNow(void*, void*) noexcept;
	inline constexpr bool HAS_RECORD_RAY_DISPATCH = false;
	core::Result<void> RecordRayDispatch(
		void*,
		void*,
		void*,
		const NativeShaderTableDesc&,
		core::Span<const NativeBindingSet>,
		core::Span<const NativeFixedBindingSet>,
		core::Span<const core::Byte>,
		void*,
		core::uint32,
		core::uint32,
		core::uint32);
	inline constexpr bool HAS_RECORD_FIXED_BINDING_SETS = false;
	core::Result<void> RecordFixedBindingSets(void*, void*, void*, bool, core::Span<const NativeFixedBindingSet>);
	inline constexpr bool HAS_PREPARE_ACCEL_BUILD = false;
	core::Result<core::uint32> PrepareAccelBuild(void*, void*, const NativePreparedAccelBuild&);
	core::Result<void> RecordPreparedAccelBuild(void*, void*, core::uint32);

	inline constexpr bool HAS_PREPARE_ACCEL_QUERY = false;
	core::Result<core::uint32> PrepareAccelQuery(void*, void*, void*, core::uint64);
	core::Result<void> RecordPreparedAccelQuery(void*, void*, core::uint32);
	void CommitPreparedAccelQuery(void*, void*, core::uint64, EQueue, core::uint64, bool) noexcept;
	void CancelPreparedAccelQuery(void*, void*, core::uint64) noexcept;
	inline constexpr bool HAS_TRY_GET_ACCEL_COMPACTED_SIZE = false;
	core::Result<AccelCompactionResult> TryGetAccelCompactedSize(void*, void*, core::uint64);

	inline constexpr bool HAS_PREPARE_ACCEL_COMPACT = false;
	core::Result<core::uint32> PrepareAccelCompact(void*, void*, void*, void*);
	core::Result<void> RecordPreparedAccelCompact(void*, void*, core::uint32);

	inline constexpr bool HAS_CREATE_PREPARED_SHADER_TABLE = false;
	core::Result<core::SharedPtr<PreparedShaderTable>> CreatePreparedShaderTable(void*, void*, const NativeShaderTableDesc&);
	inline constexpr bool HAS_PREPARE_RAY_BINDING = false;
	core::Result<core::uint32> PrepareRayBinding(
		void*,
		void*,
		void*,
		const core::SharedPtr<PreparedShaderTable>&,
		core::Span<const NativeFixedBindingSet>,
		core::Span<const NativePreparedTextureAccess>);
	inline constexpr bool HAS_RECORD_PREPARED_RAY_DISPATCH = false;
	core::Result<void> RecordPreparedRayDispatch(void*, void*, void*, core::uint32, const RayDispatch&);

	inline constexpr bool HAS_GET_LIMITS = false;
	DeviceLimits GetLimits(const void*) noexcept;
	inline constexpr bool HAS_GET_TEXTURE_BUFFER_FOOTPRINT = false;
	core::Result<TextureBufferFootprint> GetTextureBufferFootprint(void*, const TextureDesc&, TextureSubresource);

	inline constexpr bool HAS_GET_EXTERNAL_INTEROP_CAPABILITIES = false;
	ExternalInteropCapabilities GetExternalInteropCapabilities(const void*);
	inline constexpr bool HAS_CREATE_EXTERNAL_BUFFER = false;
	core::Result<void*> CreateExternalBuffer(void*, const BufferDesc&, const NativeExternalMemoryView*);
	inline constexpr bool HAS_CREATE_EXTERNAL_TEXTURE = false;
	core::Result<void*> CreateExternalTexture(void*, const TextureDesc&, const NativeExternalMemoryView*);
	inline constexpr bool HAS_EXPORT_EXTERNAL_MEMORY = false;
	core::Result<NativeExternalMemory> ExportExternalMemory(void*, void*, bool);

	inline constexpr bool HAS_CREATE_EXTERNAL_FENCE = false;
	core::Result<void*> CreateExternalFence(void*, core::uint64, const ExternalHandleView*);
	core::Result<ExternalFence> ExportExternalFence(void*, void*);
	core::Result<core::uint64> GetExternalFenceValue(void*, void*);
	void DestroyExternalFence(void*, void*) noexcept;
	core::Result<void> RecordExternalTransfer(void*, void*, void*, bool, void*, core::uint64, bool);

	inline constexpr bool HAS_EXPOSE_NATIVE_QUEUES = false;
	void ExposeNativeQueues(void*) noexcept;
	inline constexpr bool HAS_WAS_SUBMISSION_ACCEPTED = false;
	bool WasSubmissionAccepted(const void*) noexcept;

	inline constexpr bool HAS_PREPARE_RECORDING_GROUP = true;
	core::Result<void> PrepareRecordingGroup(void*, core::Span<const NativePreparedList>);
	void ResetPreparedCommandList(void*, void*) noexcept;
	core::Result<core::uint32> PrepareBinding(
		void*,
		void*,
		void*,
		bool,
		core::Span<const NativeFixedBindingSet>,
		core::Span<const NativePreparedTextureAccess>,
		const NativePreparedVertexInput&,
		core::Span<const NativeDescriptorHeapBinding>,
		core::SharedPtr<NativeBindingProgram>*);

	core::Result<void> RecordPreparedPhase(void*, void*, const NativePreparedPhase&);
	core::Result<void> RecordPreparedDispatch(void*, void*, void*, core::uint32, const ComputeDispatch&);
	inline constexpr bool HAS_RECORD_PREPARED_MESH_DISPATCH = true;
	core::Result<void> RecordPreparedMeshDispatch(void*, void*, void*, core::uint32, const MeshDispatch&);
	core::Result<void> RecordPreparedDraw(void*, void*, void*, core::uint32, const DrawArguments&);
	core::Result<void> RecordDrawIndexedArguments(void*, void*, void*, core::uint32, const DrawIndexedArguments&);

	core::Result<void> RecordPreparedRendering(void*, void*, void*, core::Span<const EAttachmentLoad>, core::Span<const ClearColor>, DepthAttachmentOptions);

	core::Result<void> FinishPreparedCommandList(void*, void*);
	core::Result<void> QueueWaitForPreparedSubmission(State*, EQueue, EQueue, core::uint64);

	inline constexpr bool HAS_PREPARE_INDIRECT = true;
	core::Result<core::uint32> PrepareIndirect(void*, void*, const NativePreparedIndirect&);
	inline constexpr bool HAS_RECORD_PREPARED_INDIRECT = true;
	core::Result<void> RecordPreparedIndirect(void*, void*, void*, core::uint32, const NativePreparedIndirect&, core::Span<const core::Byte>);

	inline constexpr bool HAS_RECORD_IMMUTABLE_RAY_DISPATCH = false;
	core::Result<void> RecordImmutableRayDispatch(void*, void*, void*, const core::SharedPtr<PreparedShaderTable>&, core::Span<const NativeFixedBindingSet>, const RayDispatch&);

	inline constexpr bool HAS_DESCRIBE_PREPARED_NATIVE_BOUNDARY = false;
	core::Result<core::uint32> DescribePreparedNativeBoundary(const void*, const void*, const void*, bool) noexcept;

	inline constexpr bool HAS_DISCARD_ACQUIRED_SWAP_CHAIN_IMAGE = false;
	core::Result<void> DiscardAcquiredSwapChainImage(void*, void*);

	inline constexpr bool HAS_RECORD_DEVICE_HEAP_USES = false;
	core::Result<void> RecordDeviceHeapUses(void*, void*, bool, core::Span<const NativeDescriptorHeapBinding>);

	core::Result<BackendData> CreateNullBackend(core::MemoryResource& resource);
}
