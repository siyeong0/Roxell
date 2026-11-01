#pragma once

#include <Array.hpp>
#include <SharedPtr.hpp>
#include <UniquePtr.hpp>
#include <MemoryResource.h>
#include <Result.hpp>
#include <RhiBindingTypes.h>
#include <RhiCommandPreparation.h>
#include <RhiCommandTypes.h>
#include <RhiCoopVecTypes.h>
#include <RhiDeviceTypes.h>
#include <RhiDiagnostics.h>
#include <RhiInterop.h>
#include <RhiPipelineTypes.h>
#include <RhiPresentationTypes.h>
#include <RhiRayTracingTypes.h>
#include <RhiResourceTypes.h>
#include <RhiTypes.h>
#include <Synchronization.hpp>

namespace rox::rhi
{
	struct AccelBuildSignature;
	struct AccelStructSlot;
	struct BindingLayoutSlot;
	struct BindingSetSlot;
	struct BoundDescriptorTableRange;
	struct BoundDescriptorTables;
	struct BufferSlot;
	struct BufferViewSlot;
	struct CommandSlot;
	struct DescriptorTableSlot;
	struct DescriptorTableEntry;
	struct DescriptorTableVersion;
	struct EventQuerySlot;
	struct FramebufferSlot;
	struct GraphicsPipelineSlot;
	struct HeapSlot;
	struct InputLayoutSlot;
	struct NativeComputePipelineDesc;
	struct NativeComputeTexture;
	struct NativeDescriptorHeapBinding;
	struct NativeFixedBindingSet;
	struct NativeFixedDescriptor;
	struct NativeGraphicsBuffer;
	struct NativeGraphicsPipelineDesc;
	struct NativeSampledTexture;
	struct NativeStorageTexture;
	struct NativeTextureResolve;
	struct NativeVertexBuffer;
	struct OpacityMicromapSlot;
	struct PipelineSlot;
	struct PreparedBinding;
	struct PreparedRecordingAccelBuild;
	class PreparedRecordingGroup;
	struct PreparedRecordingIndirect;
	struct PreparedRecordingList;
	struct PreparedRecordingPhase;
	struct PreparedRecordingPlannedAccel;
	class PreparedTextureMappings;
	struct RayTracingPipelineSlot;
	struct RecordedDescriptorTableUse;
	struct SamplerSlot;
	struct ShaderLibrarySlot;
	struct ShaderSlot;
	struct ShaderTableSlot;
	struct StagingTextureSlot;
	struct SubmissionResult;
	struct SwapChainSlot;
	struct TextureSlot;
	struct TextureViewSlot;
	struct TimerSlot;

	// A device owns every handle it creates. Destroy operations invalidate handles immediately;
	// submitted native work retains backing resources until its GPU use completes.
	// Callers must discard or submit recorded command lists before destroying their referenced resources.
	class Device final
	{
	public:
		Device(const Device&) = delete;
		Device& operator=(const Device&) = delete;

		// Advanced managed integration; the registered owner outlives its submitted work.
		[[nodiscard]] core::Result<void> RegisterManagedOwner(core::uint64 owner);
		void UnregisterManagedOwner(core::uint64 owner) noexcept;

		[[nodiscard]] NativeSubmissionOutcome SubmitManagedPrepared(core::uint64 owner, CommandListHandle list, core::Span<const SubmissionToken> prerequisites = {});

		[[nodiscard]] EBackend GetBackend() const noexcept;
		[[nodiscard]] bool IsAftermathEnabled() const noexcept;
		[[nodiscard]] IGpuCrashDiagnostics* GetGpuCrashDiagnostics() const noexcept;
		[[nodiscard]] const GpuCapabilities& GetCapabilities() const noexcept;

		// Exported OS handles are owned by the caller. Imports borrow their input handle.
		// Imported resources start externally owned; release/acquire enqueue GPU synchronization.
		[[nodiscard]] ExternalInteropCapabilities GetExternalInteropCapabilities() const;

		[[nodiscard]] core::Result<BufferHandle> CreateExternalBuffer(const BufferDesc& desc);
		[[nodiscard]] core::Result<BufferHandle> ImportExternalBuffer(const ExternalBufferMemoryView& memory);
		[[nodiscard]] core::Result<ExternalBufferMemory> ExportExternalBufferMemory(BufferHandle handle);

		[[nodiscard]] core::Result<TextureHandle> CreateExternalTexture(const TextureDesc& desc);
		[[nodiscard]] core::Result<TextureHandle> ImportExternalTexture(const ExternalTextureMemoryView& memory);
		[[nodiscard]] core::Result<ExternalTextureMemory> ExportExternalTextureMemory(TextureHandle handle);

		[[nodiscard]] core::Result<ExternalFenceHandle> CreateExternalFence(core::uint64 initialValue = 0);
		[[nodiscard]] core::Result<ExternalFenceHandle> ImportExternalFence(const ExternalFenceView& fence);
		[[nodiscard]] core::Result<ExternalFence> ExportExternalFence(ExternalFenceHandle handle);
		[[nodiscard]] core::Result<core::uint64> GetExternalFenceValue(ExternalFenceHandle handle);
		[[nodiscard]] core::Result<void> DestroyExternalFence(ExternalFenceHandle handle);

		[[nodiscard]] core::Result<SubmissionToken> SignalExternalFence(EQueue queue, ExternalFenceHandle fence, core::uint64 value);
		[[nodiscard]] core::Result<SubmissionToken> WaitExternalFence(EQueue queue, ExternalFenceHandle fence, core::uint64 value);

		[[nodiscard]] core::Result<SubmissionToken> ReleaseExternalBuffer(BufferHandle resource, EQueue queue, ExternalFenceHandle fence, core::uint64 value);
		[[nodiscard]] core::Result<SubmissionToken> AcquireExternalBuffer(BufferHandle resource, EQueue queue, ExternalFenceHandle fence, core::uint64 value);

		[[nodiscard]] core::Result<SubmissionToken> ReleaseExternalTexture(TextureHandle resource, EQueue queue, ExternalFenceHandle fence, core::uint64 value);
		[[nodiscard]] core::Result<SubmissionToken> AcquireExternalTexture(TextureHandle resource, EQueue queue, ExternalFenceHandle fence, core::uint64 value);

		[[nodiscard]] core::Result<NativeDeviceHandles> GetNativeDeviceHandles() const;
		[[nodiscard]] core::Result<NativeQueueHandle> GetNativeQueue(EQueue queue) const;

		// Scoped admission for CPU submits on this device's exact physical native queue.
		// Engine submits/waits touching an alias reject until the caller ends admission.
		[[nodiscard]] core::Result<NativeQueueAccessHandle> BeginNativeQueueAccess(EQueue queue);
		[[nodiscard]] core::Result<void> EndNativeQueueAccess(NativeQueueAccessHandle handle);

		// Exact admission token permits one finished state-only boundary list on its physical queue.
		[[nodiscard]] NativeSubmissionOutcome SubmitPreparedNativeBoundary(CommandListHandle list, NativeQueueAccessHandle admission);

		// Actual post-execute native state/layout from one retained, finished prepared phase.
		// The state does not grant queue admission or prove GPU completion.
		[[nodiscard]] core::Result<NativeBufferState> GetPreparedNativeBufferState(CommandListHandle list, BufferHandle buffer);
		[[nodiscard]] core::Result<NativeTextureState> GetPreparedNativeTextureState(CommandListHandle list, TextureHandle texture);

		[[nodiscard]] core::Result<CoopVecFeatures> QueryCoopVecFeatures(core::Span<CoopVecFormatCombo> formats) const;
		[[nodiscard]] core::Result<core::uint64> GetCoopVecMatrixSize(ECoopVecDataType type, ECoopVecMatrixLayout layout, core::uint32 rows, core::uint32 columns) const;

		[[nodiscard]] core::Result<EFormatSupport> QueryFormatSupport(EFormat format) const;
		// SampleCount is ignored; mask uses numeric bits 1|2|4|8 for exact descriptor support.
		[[nodiscard]] core::Result<core::uint32> QueryTextureSampleCounts(const TextureDesc& desc) const;

		[[nodiscard]] core::Result<MemoryRequirements> GetBufferMemoryRequirements(const BufferDesc& desc) const;

		// An empty span queries the heap count. Otherwise provide capacity for every heap.
		// Failure leaves outputs unchanged; unavailable native budget queries return Unsupported.
		[[nodiscard]] core::Result<core::uint32> QueryMemoryBudget(core::Span<MemoryBudget> budgets) const;

		[[nodiscard]] core::Result<AccelStructBuildSizes> GetAccelStructBuildSizes(const AccelStructBuildDesc& desc) const;
		[[nodiscard]] core::Result<OpacityMicromapBuildSizes> GetOpacityMicromapBuildSizes(
			core::Span<const OpacityMicromapUsageCount> counts,
			EOpacityMicromapBuildFlags flags = EOpacityMicromapBuildFlags::None) const;
		[[nodiscard]] core::Result<OpacityMicromapHandle> CreateOpacityMicromap(const OpacityMicromapDesc& desc);
		[[nodiscard]] core::Result<void> DestroyOpacityMicromap(OpacityMicromapHandle handle);

		// Retains the micromap until this list is submitted or discarded; records no GPU command.
		[[nodiscard]] core::Result<void> ReferenceOpacityMicromap(CommandListHandle list, OpacityMicromapHandle handle);
		[[nodiscard]] core::Result<void> BuildOpacityMicromap(
			CommandListHandle list,
			OpacityMicromapHandle target,
			const OpacityMicromapBuildDesc& desc,
			BufferHandle scratch,
			core::uint64 scratchOffsetBytes = 0);

		[[nodiscard]] core::Result<AccelStructHandle> CreateAccelStruct(const AccelStructDesc& desc);
		[[nodiscard]] core::Result<void> DestroyAccelStruct(AccelStructHandle handle);
		[[nodiscard]] core::Result<core::uint64> GetAccelStructDeviceAddress(AccelStructHandle handle);

		[[nodiscard]] core::Result<AccelCompactionResult> TryGetBottomLevelAccelStructCompactedSize(AccelStructHandle source);
		[[nodiscard]] core::Result<core::uint64> GetBottomLevelAccelStructCompactedSize(AccelStructHandle source);
		[[nodiscard]] core::Result<void> CompactBottomLevelAccelStruct(CommandListHandle list, AccelStructHandle source, AccelStructHandle target);

		[[nodiscard]] core::Result<void> BuildBottomLevelAccelStruct(
			CommandListHandle list,
			AccelStructHandle target,
			const AccelStructBuildDesc& desc,
			BufferHandle scratch,
			core::uint64 scratchOffsetBytes = 0);
		[[nodiscard]] core::Result<void> BuildTopLevelAccelStruct(
			CommandListHandle list,
			AccelStructHandle target,
			const AccelStructBuildDesc& desc,
			core::Span<const AccelInstanceDesc> instances,
			BufferHandle scratch,
			core::uint64 scratchOffsetBytes = 0);
		[[nodiscard]] core::Result<void> BuildTopLevelAccelStructFromBuffer(
			CommandListHandle list,
			AccelStructHandle target,
			const AccelStructBuildDesc& desc,
			BufferHandle instances,
			core::uint64 instanceOffsetBytes,
			core::Span<const AccelStructHandle> referencedBlas,
			BufferHandle scratch,
			core::uint64 scratchOffsetBytes = 0);

		[[nodiscard]] core::Result<ClusterOperationSizeInfo> GetClusterOperationSizeInfo(const ClusterOperationParams& params) const;
		[[nodiscard]] core::Result<void> ExecuteMultiIndirectClusterOperation(CommandListHandle list, const ClusterOperationDesc& desc);

		[[nodiscard]] core::Result<MemoryRequirements> GetTextureMemoryRequirements(const TextureDesc& desc) const;

		[[nodiscard]] core::Result<HeapHandle> CreateHeap(const HeapDesc& desc);
		// Actual currently owned native heap allocation extent; zero when lazy memory is not materialized.
		[[nodiscard]] core::Result<core::uint64> GetHeapCommittedBytes(HeapHandle heap) const;
		[[nodiscard]] core::Result<void> DestroyHeap(HeapHandle heap);
		// Buffer heaps only. Returns Conflict while live resources or retired GPU ranges remain.
		[[nodiscard]] core::Result<void> TryDestroyHeap(HeapHandle heap);

		[[nodiscard]] core::Result<BufferHandle> CreateBuffer(const BufferDesc& desc);
		[[nodiscard]] core::Result<BufferHandle> ImportBuffer(const BufferDesc& desc, void* nativeBuffer);
		[[nodiscard]] core::Result<NativeResourceHandle> GetNativeBuffer(BufferHandle handle);
		// Live and retired heap ranges cannot overlap; dedicated allocations cannot be placed.
		[[nodiscard]] core::Result<BufferHandle> CreatePlacedBuffer(const BufferDesc& desc, HeapHandle heap, core::uint64 offsetBytes);
		[[nodiscard]] core::Result<void> DestroyBuffer(BufferHandle handle);
		[[nodiscard]] core::Result<core::uint64> GetBufferDeviceAddress(BufferHandle handle);

		// Mapping excludes recorded uses and waits for the entire native buffer's submitted GPU uses.
		// Native devices conservatively join all queues after explicit prepared multi-queue work.
		// Logical subranges do not provide independent mapping or state ownership.
		[[nodiscard]] core::Result<core::Span<core::Byte>> MapBuffer(BufferHandle handle, core::uint64 offsetBytes, core::uint64 byteCount, core::uint32 timeoutMilliseconds);
		[[nodiscard]] core::Result<void> UnmapBuffer(BufferHandle handle);

		[[nodiscard]] core::Result<TextureHandle> CreateTexture(const TextureDesc& desc);
		[[nodiscard]] core::Result<TextureHandle> ImportTexture(const TextureDesc& desc, void* nativeTexture, ENativeTextureContents contents);
		[[nodiscard]] core::Result<NativeResourceHandle> GetNativeTexture(TextureHandle handle);
		[[nodiscard]] core::Result<TextureTiling> GetTextureTiling(TextureHandle handle, core::Span<TextureSubresourceTiling> subresources);
		[[nodiscard]] core::Result<SubmissionToken> UpdateTextureTileMappings(TextureHandle texture, core::Span<const TextureTileMapping> mappings, EQueue queue = EQueue::Graphics);

		// Cold capture retains replaced/candidate heaps and reserves completion storage without changing mappings.
		// Submit rejects invalid prerequisites before native work; accepted/unknown results retain physical owners.
		[[nodiscard]] core::Result<PreparedTextureMappingsHandle> PrepareTextureTileMappings(TextureHandle texture, core::Span<const TextureTileMapping> mappings, EQueue queue = EQueue::Graphics);
		[[nodiscard]] NativeSubmissionOutcome SubmitPreparedTextureMappings(PreparedTextureMappingsHandle mapping, core::Span<const SubmissionToken> prerequisites = {});
		[[nodiscard]] core::Result<void> DiscardPreparedTextureMappings(PreparedTextureMappingsHandle mapping);

		// The heap class must match the texture; overlapping native image aliasing is unsupported.
		[[nodiscard]] core::Result<TextureHandle> CreatePlacedTexture(const TextureDesc& desc, HeapHandle heap, core::uint64 offsetBytes);
		[[nodiscard]] core::Result<void> DestroyTexture(TextureHandle handle);

		[[nodiscard]] core::Result<SamplerFeedbackHandle> CreateSamplerFeedbackTexture(TextureHandle pairedTexture, const SamplerFeedbackDesc& desc);
		[[nodiscard]] core::Result<SamplerFeedbackHandle> ImportSamplerFeedbackTexture(TextureHandle pairedTexture, const SamplerFeedbackDesc& desc, void* nativeFeedbackTexture);
		[[nodiscard]] core::Result<void> DestroySamplerFeedbackTexture(SamplerFeedbackHandle handle);
		[[nodiscard]] core::Result<NativeResourceHandle> GetNativeSamplerFeedbackTexture(SamplerFeedbackHandle handle);

		[[nodiscard]] core::Result<TextureViewHandle> CreateTextureView(const TextureViewDesc& desc);
		[[nodiscard]] core::Result<void> DestroyTextureView(TextureViewHandle handle);

		[[nodiscard]] core::Result<BufferViewHandle> CreateBufferView(const BufferViewDesc& desc);
		[[nodiscard]] core::Result<void> DestroyBufferView(BufferViewHandle handle);

		[[nodiscard]] core::Result<SamplerHandle> CreateSampler(const SamplerDesc& desc);
		[[nodiscard]] core::Result<void> DestroySampler(SamplerHandle handle);

		[[nodiscard]] core::Result<SwapChainHandle> CreateSwapChain(const SwapChainDesc& desc);
		[[nodiscard]] core::Result<void> ResizeSwapChain(SwapChainHandle handle, core::uint32 width, core::uint32 height);
		// The returned identity remains current until presentation or discard closes it.
		[[nodiscard]] AcquireOutcome AcquireSwapChainImage(SwapChainHandle handle, core::uint32 timeoutMilliseconds);
		// Query rejects stale acquisitions; mutations require the exact current identity.
		[[nodiscard]] core::Result<TextureDesc> QueryAcquiredSwapChainImage(AcquiredSwapChainImage acquisition);
		[[nodiscard]] core::Result<void> DiscardAcquiredSwapChainImage(AcquiredSwapChainImage acquisition);
		// Native presentation attempts close acquisition authority even if presentation fails.
		[[nodiscard]] PresentOutcome PresentSwapChainImage(AcquiredSwapChainImage acquisition);
		[[nodiscard]] PresentOutcome CopyAndPresent(SwapChainHandle handle, TextureHandle source);
		[[nodiscard]] core::Result<void> DestroySwapChain(SwapChainHandle handle);

		// Poll shared-device retirement without waiting for GPU progress. Timeout preserves the chain.
		[[nodiscard]] core::Result<void> TryResizeSwapChain(SwapChainHandle handle, core::uint32 width, core::uint32 height);
		[[nodiscard]] core::Result<void> TryDestroySwapChain(SwapChainHandle handle);

		[[nodiscard]] core::Result<SubmissionToken> UploadTexture(TextureHandle destination, core::Span<const core::Byte> data);
		[[nodiscard]] core::Result<SubmissionToken> UploadTextureSubresource(TextureHandle destination, TextureSubresource subresource, core::Span<const core::Byte> data);
		[[nodiscard]] core::Result<void> ReadTextureBlocking(TextureHandle source, core::Span<core::Byte> output, SubmissionToken after, core::uint32 timeoutMilliseconds);
		[[nodiscard]] core::Result<void> ReadTextureSubresourceBlocking(
			TextureHandle source,
			TextureSubresource subresource,
			core::Span<core::Byte> output,
			SubmissionToken after,
			core::uint32 timeoutMilliseconds);

		[[nodiscard]] core::Result<SubmissionToken> UploadBuffer(BufferHandle destination, core::uint64 offsetBytes, core::Span<const core::Byte> data);
		[[nodiscard]] core::Result<void> ReadBufferBlocking(BufferHandle source, core::uint64 offsetBytes, core::Span<core::Byte> output, SubmissionToken after, core::uint32 timeoutMilliseconds);

		// Create resources and lists before entering; join recording workers before ending.
		// During this phase only command recording and immutable queries are permitted.
		[[nodiscard]] core::Result<void> BeginParallelRecording();
		[[nodiscard]] core::Result<ParallelRecordingStatistics> EndParallelRecording();

		[[nodiscard]] core::Result<CommandListHandle> CreateCommandList(EQueue queue);

		// Cold preparation is atomic for fresh lists. The span convenience preserves one-queue ordering.
		[[nodiscard]] core::Result<void> PrepareCommandLists(core::Span<const CommandListPreparationDesc> orderedLists);
		[[nodiscard]] core::Result<void> PrepareCommandListGroup(const CommandListGroupPreparationDesc& description);

		[[nodiscard]] core::Result<void> BeginPreparedRecording(core::Span<const CommandListHandle> lists, EPreparedRecordingMode mode = EPreparedRecordingMode::Serial);
		[[nodiscard]] core::Result<void> BeginPreparedAccessPhase(CommandListHandle list, core::uint32 phase);

		[[nodiscard]] PreparedBindingRef GetPreparedBindings(CommandListHandle list, core::uint32 phase, core::uint32 slot);
		[[nodiscard]] PreparedAttachmentRef GetPreparedAttachment(CommandListHandle list, core::uint32 phase, core::uint32 slot);

		// Prepared selectors and indirect command budgets are programmer preconditions.
		// A native emission failure poisons the list.
		[[nodiscard]] PreparedIndirectRef GetPreparedIndirect(CommandListHandle list, core::uint32 phase, core::uint32 slot) const;
		[[nodiscard]] core::Result<void> DispatchComputeIndirect(CommandListHandle list, PreparedIndirectRef ref, core::Span<const core::Byte> pushConstants = {});
		[[nodiscard]] core::Result<void> DrawIndirect(CommandListHandle list, PreparedIndirectRef ref, core::Span<const core::Byte> pushConstants = {});
		[[nodiscard]] core::Result<void> DrawIndexedIndirect(CommandListHandle list, PreparedIndirectRef ref, core::Span<const core::Byte> pushConstants = {});
		[[nodiscard]] core::Result<void> DispatchMeshIndirect(CommandListHandle list, PreparedIndirectRef ref, core::Span<const core::Byte> pushConstants = {});

		[[nodiscard]] core::Result<void> DispatchCompute(CommandListHandle list, PreparedBindingRef binding, const ComputeDispatch& dispatch);

		// Payload-free mesh pipelines only; shares the phase Draws capacity.
		[[nodiscard]] core::Result<void> DispatchMesh(CommandListHandle list, PreparedBindingRef binding, const MeshDispatch& dispatch);
		[[nodiscard]] core::Result<void> Draw(CommandListHandle list, PreparedBindingRef binding, const DrawArguments& draw);
		// Indexed and nonindexed draws share Capacity.Draws. GPU index contents must address valid vertex records.
		[[nodiscard]] core::Result<void> DrawIndexed(CommandListHandle list, core::Span<const PreparedIndexedDraw> draws);
		[[nodiscard]] core::Result<void> DrawIndexed(CommandListHandle list, PreparedBindingRef binding, const DrawIndexedArguments& draw);

		// Clear colors match the framebuffer count when any color is cleared; Load-only passes may omit them.
		[[nodiscard]] core::Result<void> BeginRendering(CommandListHandle list, PreparedAttachmentRef attachment, const RenderPassClearValues& clearValues);
		[[nodiscard]] PreparedTextureBufferCopyRef GetPreparedTextureBufferCopy(CommandListHandle list, core::uint32 phase, core::uint32 slot) const;
		// The call direction must match the captured tuple. Both overloads share TextureCopies capacity.
		[[nodiscard]] core::Result<void> CopyBufferToTexture(CommandListHandle list, PreparedTextureBufferCopyRef copy);
		[[nodiscard]] core::Result<void> CopyTextureToBuffer(CommandListHandle list, PreparedTextureBufferCopyRef copy);

		[[nodiscard]] PreparedResolveRef GetPreparedResolve(CommandListHandle list, core::uint32 phase, core::uint32 slot) const;
		// Captured whole color resolve; shares TextureCopies capacity with texture copies/transfers.
		[[nodiscard]] core::Result<void> ResolveTexture(CommandListHandle list, PreparedResolveRef resolve);

		[[nodiscard]] core::Result<void> FinishPreparedCommandList(CommandListHandle list);
		// Code-only queue admission for a finished prepared group; failures may follow native acceptance.
		[[nodiscard]] core::Result<void> QueueWaitForPreparedSubmission(CommandListHandle list, SubmissionToken token);
		// Join workers before ending. A valid call always closes the interval, including on failure.
		[[nodiscard]] core::Result<ParallelRecordingStatistics> EndPreparedRecording();
		// Join workers before cancellation. Retained command lists must then be discarded.
		void CancelPreparedRecording();
		[[nodiscard]] SubmitOutcome SubmitPrepared(CommandListHandle list, core::Span<const SubmissionToken> prerequisites = {});

		[[nodiscard]] core::Result<CommandListStatistics> GetCommandListStatistics(CommandListHandle list) const;

		// Record synchronously inside the current prepared phase; any callback error poisons the list.
		// The callback restores phase resource states and cannot submit, retain the borrow or reenter RHI.
		[[nodiscard]] core::Result<void> RecordPreparedNative(CommandListHandle list, NativeCommandCallback callback, void* userDataOrNull = nullptr);

		// Cold capture from an unrecorded retained prepared phase with native callback capacity.
		// The returned resource remains borrowed through that group's discard or GPU retirement.
		[[nodiscard]] core::Result<NativeResourceHandle> GetPreparedNativeBuffer(CommandListHandle list, core::uint32 phaseIndex, BufferHandle buffer);
		[[nodiscard]] core::Result<NativeResourceHandle> GetPreparedNativeTexture(CommandListHandle list, core::uint32 phaseIndex, TextureHandle texture);
		[[nodiscard]] core::Result<NativeCommandListHandle> GetNativeCommandList(CommandListHandle list);

		[[nodiscard]] core::Result<void> ClearState(CommandListHandle list);
		[[nodiscard]] core::Result<void> SetEnableAutomaticBarriers(CommandListHandle list, bool bEnabled);
		[[nodiscard]] core::Result<void> SetEnableUavBarriersForBuffer(CommandListHandle list, BufferHandle buffer, bool bEnabled);
		[[nodiscard]] core::Result<void> SetEnableUavBarriersForTexture(CommandListHandle list, TextureHandle texture, bool bEnabled);

		[[nodiscard]] core::Result<void> ReferenceBuffer(CommandListHandle list, BufferHandle buffer);
		[[nodiscard]] core::Result<void> ReferenceTexture(CommandListHandle list, TextureHandle texture);

		[[nodiscard]] core::Result<void> ShaderMemoryBarrier(CommandListHandle list);
		[[nodiscard]] core::Result<void> ShaderMemoryBarrier(CommandListHandle list, BufferHandle buffer);
		[[nodiscard]] core::Result<void> ShaderMemoryBarrier(CommandListHandle list, TextureHandle texture);

		[[nodiscard]] core::Result<void> PrepareTextureForShaderAccess(CommandListHandle list, TextureHandle texture);
		[[nodiscard]] core::Result<void> PrepareTextureForShaderAccess(CommandListHandle list, TextureViewHandle view);

		[[nodiscard]] core::Result<void> SetTextureState(CommandListHandle list, TextureHandle texture, ETextureState state);
		[[nodiscard]] core::Result<void> SetPermanentTextureState(CommandListHandle list, TextureHandle texture, ETextureState state);
		[[nodiscard]] core::Result<void> BeginTrackingTextureState(CommandListHandle list, TextureHandle texture, ETextureState state);

		[[nodiscard]] core::Result<void> ReferenceSamplerFeedbackTexture(CommandListHandle list, SamplerFeedbackHandle feedback);
		[[nodiscard]] core::Result<void> SetSamplerFeedbackTextureState(CommandListHandle list, SamplerFeedbackHandle feedback, ESamplerFeedbackState state);

		[[nodiscard]] core::Result<void> SetTextureState(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, ETextureState state);
		[[nodiscard]] core::Result<void> BeginTrackingTextureState(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, ETextureState state);
		[[nodiscard]] core::Result<ETrackedResourceState> GetTextureSubresourceState(CommandListHandle list, TextureHandle texture, TextureSubresource subresource);

		// Buffer state transitions apply to the whole native resource, including all binding ranges.
		[[nodiscard]] core::Result<void> SetBufferState(CommandListHandle list, BufferHandle buffer, EBufferState state);
		[[nodiscard]] core::Result<void> SetPermanentBufferState(CommandListHandle list, BufferHandle buffer, EBufferState state);
		[[nodiscard]] core::Result<void> CommitBarriers(CommandListHandle list);
		[[nodiscard]] core::Result<void> BeginTrackingBufferState(CommandListHandle list, BufferHandle buffer, EBufferState state);

		[[nodiscard]] core::Result<void> SetAccelStructState(CommandListHandle list, AccelStructHandle accel, EAccelStructState state);

		[[nodiscard]] core::Result<void> SetResourceStatesForBindingSet(CommandListHandle list, BindingSetHandle bindingSet);
		[[nodiscard]] core::Result<void> SetResourceStatesForFramebuffer(CommandListHandle list, const RenderPassLayout& layout);
		[[nodiscard]] core::Result<ETrackedResourceState> GetBufferState(CommandListHandle list, BufferHandle buffer);

		[[nodiscard]] core::Result<void> BeginMarker(CommandListHandle list, const char* name);
		[[nodiscard]] core::Result<void> EndMarker(CommandListHandle list);

		[[nodiscard]] core::Result<EventQueryHandle> CreateEventQuery();
		[[nodiscard]] core::Result<void> DestroyEventQuery(EventQueryHandle query);
		[[nodiscard]] core::Result<void> SetEventQuery(EventQueryHandle query, EQueue queue);
		[[nodiscard]] core::Result<bool> PollEventQuery(EventQueryHandle query) const;
		[[nodiscard]] core::Result<void> WaitEventQuery(EventQueryHandle query, core::uint32 timeoutMilliseconds);
		[[nodiscard]] core::Result<void> ResetEventQuery(EventQueryHandle query);

		[[nodiscard]] core::Result<GpuTimerHandle> CreateGpuTimer();
		[[nodiscard]] core::Result<void> DestroyGpuTimer(GpuTimerHandle timer);
		[[nodiscard]] core::Result<void> BeginGpuTimer(CommandListHandle list, GpuTimerHandle timer);
		[[nodiscard]] core::Result<void> EndGpuTimer(CommandListHandle list, GpuTimerHandle timer);
		[[nodiscard]] core::Result<bool> PollGpuTimer(GpuTimerHandle timer) const;
		[[nodiscard]] core::Result<core::float64> ReadGpuTimerSeconds(GpuTimerHandle timer, core::uint32 timeoutMilliseconds);
		[[nodiscard]] core::Result<void> ResetGpuTimer(GpuTimerHandle timer);

		[[nodiscard]] core::Result<void> CopyBuffer(CommandListHandle list, const BufferCopy& copy);

		[[nodiscard]] core::Result<void> ConvertCoopVecMatrices(CommandListHandle list, core::Span<const CoopVecMatrixConversion> conversions);

		[[nodiscard]] core::Result<void> WriteBuffer(CommandListHandle list, BufferHandle destination, core::uint64 offsetBytes, core::Span<const core::Byte> data);
		[[nodiscard]] core::Result<void> ClearBufferUInt(CommandListHandle list, BufferHandle buffer, core::uint32 value);

		[[nodiscard]] core::Result<void> CopyTexture(CommandListHandle list, const TextureCopy& copy);
		[[nodiscard]] core::Result<void> WriteTexture(CommandListHandle list, TextureHandle destination, TextureSubresource subresource, core::Span<const core::Byte> data);
		[[nodiscard]] core::Result<void> CopyTextureRegion(CommandListHandle list, const TextureCopyRegion& copy);

		[[nodiscard]] core::Result<TextureBufferFootprint> GetTextureBufferFootprint(TextureHandle texture, TextureSubresource subresource);

		[[nodiscard]] core::Result<StagingTextureHandle> CreateStagingTexture(const TextureDesc& desc, EBufferMemory access);
		[[nodiscard]] core::Result<void> DestroyStagingTexture(StagingTextureHandle staging);
		[[nodiscard]] core::Result<StagingTextureMapping> MapStagingTexture(StagingTextureHandle staging, TextureSubresource subresource, core::uint32 timeoutMilliseconds);
		[[nodiscard]] core::Result<void> UnmapStagingTexture(StagingTextureHandle staging);
		[[nodiscard]] core::Result<void> CopyStagingTextureToTexture(
			CommandListHandle list,
			StagingTextureHandle source,
			TextureSubresource sourceSubresource,
			TextureHandle destination,
			TextureSubresource destinationSubresource);
		[[nodiscard]] core::Result<void> CopyTextureToStagingTexture(
			CommandListHandle list,
			TextureHandle source,
			TextureSubresource sourceSubresource,
			StagingTextureHandle destination,
			TextureSubresource destinationSubresource);

		[[nodiscard]] core::Result<void> CopyBufferToTexture(CommandListHandle list, const TextureBufferCopy& copy);
		[[nodiscard]] core::Result<void> CopyTextureToBuffer(CommandListHandle list, const TextureBufferCopy& copy);

		[[nodiscard]] core::Result<bool> QueryTextureResolveSupport(const TextureDesc& source, ETextureResolveMode mode, ETextureResolveMode stencilMode = ETextureResolveMode::None);
		[[nodiscard]] core::Result<void> ResolveTexture(CommandListHandle list, const TextureResolve& resolve);

		[[nodiscard]] core::Result<void> ClearColorTexture(CommandListHandle list, TextureHandle texture, const ClearColor& color);
		[[nodiscard]] core::Result<void> ClearColorTextureSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, const ClearColor& color);
		[[nodiscard]] core::Result<void> ClearTextureUInt(CommandListHandle list, TextureHandle texture, core::uint32 value);
		[[nodiscard]] core::Result<void> ClearTextureInt(CommandListHandle list, TextureHandle texture, core::int32 value);
		[[nodiscard]] core::Result<void> ClearTextureIntSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, core::int32 value);

		[[nodiscard]] core::Result<void> ClearSamplerFeedbackTexture(CommandListHandle list, SamplerFeedbackHandle feedback);
		[[nodiscard]] core::Result<void> DecodeSamplerFeedbackTexture(CommandListHandle list, SamplerFeedbackHandle feedback, BufferHandle destination);
		[[nodiscard]] core::Result<void> DecodeSamplerFeedbackTexture(CommandListHandle list, SamplerFeedbackHandle feedback, TextureHandle destination);

		[[nodiscard]] core::Result<void> ClearTextureUIntSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, core::uint32 value);

		[[nodiscard]] core::Result<void> ClearDepthTexture(CommandListHandle list, TextureHandle texture, core::float32 depth);
		[[nodiscard]] core::Result<void> ClearDepthTextureSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, core::float32 depth);
		[[nodiscard]] core::Result<void> ClearDepthStencilTexture(CommandListHandle list, TextureHandle texture, const DepthStencilClear& clear);
		[[nodiscard]] core::Result<void> ClearDepthStencilTextureSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, const DepthStencilClear& clear);

		[[nodiscard]] core::Result<ShaderHandle> CreateShader(const ShaderDesc& desc);
		[[nodiscard]] core::Result<void> DestroyShader(ShaderHandle shader);

		[[nodiscard]] core::Result<ShaderLibraryHandle> CreateShaderLibrary(core::Span<const ShaderLibraryEntryDesc> entries);
		[[nodiscard]] core::Result<ShaderHandle> AcquireShader(ShaderLibraryHandle library, EShaderStage stage, const char* entryPoint);
		[[nodiscard]] core::Result<void> DestroyShaderLibrary(ShaderLibraryHandle library);

		[[nodiscard]] core::Result<ComputePipelineHandle> CreateComputePipeline(const ComputePipelineDesc& desc);
		[[nodiscard]] core::Result<core::uint32> GetComputeDescriptorHeapIndex(ComputePipelineHandle pipeline, EDescriptorType type, core::uint32 entry);
		[[nodiscard]] core::Result<void> DestroyComputePipeline(ComputePipelineHandle pipeline);

		[[nodiscard]] core::Result<void> DispatchCompute(CommandListHandle list, const ComputeBindings& bindings, const ComputeDispatch& dispatch);
		[[nodiscard]] core::Result<void> DispatchComputeIndirect(CommandListHandle list, const ComputeDispatchIndirect& dispatch);

		[[nodiscard]] core::Result<InputLayoutHandle> CreateInputLayout(const InputLayoutDesc& desc);
		[[nodiscard]] core::Result<void> DestroyInputLayout(InputLayoutHandle layout);

		[[nodiscard]] core::Result<GraphicsPipelineHandle> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc);
		[[nodiscard]] core::Result<core::uint32> GetGraphicsDescriptorHeapIndex(GraphicsPipelineHandle pipeline, EDescriptorType type, core::uint32 entry);
		[[nodiscard]] core::Result<void> DestroyGraphicsPipeline(GraphicsPipelineHandle pipeline);

		[[nodiscard]] core::Result<RayTracingPipelineHandle> CreateRayTracingPipeline(const RayTracingPipelineDesc& desc);
		[[nodiscard]] core::Result<void> DestroyRayTracingPipeline(RayTracingPipelineHandle pipeline);

		[[nodiscard]] core::Result<ShaderTableHandle> CreateShaderTable(const ShaderTableDesc& desc);
		[[nodiscard]] core::Result<void> DestroyShaderTable(ShaderTableHandle table);

		[[nodiscard]] PreparedAccelStructBuildRef GetPreparedAccelStructBuild(CommandListHandle list, core::uint32 phase, core::uint32 slot) const;
		[[nodiscard]] PreparedAccelStructCompactionQueryRef GetPreparedAccelStructCompactionQuery(CommandListHandle list, core::uint32 phase, core::uint32 slot) const;
		[[nodiscard]] PreparedAccelStructCompactRef GetPreparedAccelStructCompact(CommandListHandle list, core::uint32 phase, core::uint32 slot) const;

		[[nodiscard]] core::Result<void> BuildAccelStructPrepared(CommandListHandle list, PreparedAccelStructBuildRef build);
		[[nodiscard]] core::Result<void> QueryAccelStructCompactedSizePrepared(CommandListHandle list, PreparedAccelStructCompactionQueryRef query);
		[[nodiscard]] core::Result<void> CompactAccelStructPrepared(CommandListHandle list, PreparedAccelStructCompactRef compact);

		[[nodiscard]] core::Result<void> DispatchRays(CommandListHandle list, PreparedBindingRef bindings, const RayDispatch& dispatch);
		[[nodiscard]] core::Result<void> DispatchRays(CommandListHandle list, const RayBindings& bindings, const RayDispatch& dispatch);

		[[nodiscard]] core::Result<BindingLayoutHandle> CreateBindingLayout(const BindingLayoutDesc& desc);
		[[nodiscard]] core::Result<void> DestroyBindingLayout(BindingLayoutHandle layout);

		[[nodiscard]] core::Result<BindingSetHandle> CreateBindingSet(const BindingSetDesc& desc);
		[[nodiscard]] core::Result<void> DestroyBindingSet(BindingSetHandle set);

		[[nodiscard]] core::Result<DescriptorTableHandle> CreateDescriptorTable(const DescriptorTableDesc& desc);
		[[nodiscard]] core::Result<core::uint32> GetDescriptorTableDeviceHeapIndex(DescriptorTableHandle table, core::uint32 entry);

		[[nodiscard]] core::Result<void> WriteDescriptorTable(DescriptorTableHandle table, const DescriptorTableWrite& write);
		// Copies initialized descriptors to a distinct idle table with the same type and interpretation.
		// Source descriptors and accepted GPU users remain unchanged; failure preserves destination ownership.
		[[nodiscard]] core::Result<void> CopyDescriptorTableEntries(
			DescriptorTableHandle source,
			core::uint32 sourceIndex,
			DescriptorTableHandle destination,
			core::uint32 destinationIndex,
			core::uint32 count);
		[[nodiscard]] core::Result<void> ClearDescriptorTableEntry(DescriptorTableHandle table, core::uint32 index);
		[[nodiscard]] core::Result<void> ResizeDescriptorTable(DescriptorTableHandle table, core::uint32 capacity, bool bKeepContents = true);
		[[nodiscard]] core::Result<void> DestroyDescriptorTable(DescriptorTableHandle table);

		[[nodiscard]] core::Result<FramebufferHandle> CreateFramebuffer(const FramebufferDesc& desc);
		[[nodiscard]] core::Result<void> DestroyFramebuffer(FramebufferHandle framebuffer);

		[[nodiscard]] core::Result<void> BeginRendering(CommandListHandle list, const RenderPassDesc& pass);

		[[nodiscard]] core::Result<void> SetViewport(CommandListHandle list, const ViewportDesc& viewport);
		[[nodiscard]] core::Result<void> SetViewports(CommandListHandle list, core::Span<const ViewportDesc> viewports);

		[[nodiscard]] core::Result<void> SetScissor(CommandListHandle list, const ScissorDesc& scissor);
		[[nodiscard]] core::Result<void> SetScissors(CommandListHandle list, core::Span<const ScissorDesc> scissors);

		[[nodiscard]] core::Result<void> Draw(CommandListHandle list, const GraphicsState& state, const DrawArguments& draw);

		[[nodiscard]] core::Result<void> DispatchMesh(CommandListHandle list, const GraphicsState& state, const MeshDispatch& dispatch);
		[[nodiscard]] core::Result<void> DispatchMeshIndirect(CommandListHandle list, const MeshDispatchIndirect& dispatch);

		[[nodiscard]] core::Result<void> DrawIndexed(CommandListHandle list, const GraphicsState& state, const DrawIndexedArguments& draw);

		[[nodiscard]] core::Result<void> DrawIndirect(CommandListHandle list, const GraphicsDrawIndirect& draw);

		[[nodiscard]] core::Result<void> DrawIndexedIndirect(CommandListHandle list, const GraphicsDrawIndexedIndirect& draw);

		[[nodiscard]] core::Result<void> EndRendering(CommandListHandle list);

		// Submitted consumes the list and returns its completion token. Busy preserves the
		// exact list for retry. Failed consumes it and stops admission on this device.
		// The device retains native retirement facts and reports the first terminal error.
		[[nodiscard]] SubmitOutcome Submit(CommandListHandle list);
		// Requires a live, caller-owned list outside a prepared interval. Releases recording
		// pins without allocation or GPU waits, including after a recording failure.
		void DiscardCommandList(CommandListHandle list);

		[[nodiscard]] core::Result<bool> IsSubmissionComplete(SubmissionToken token) const;
		[[nodiscard]] core::Result<void> WaitForSubmission(SubmissionToken token, core::uint32 timeoutMilliseconds);

		[[nodiscard]] core::Result<void> QueueWaitForSubmission(EQueue waitingQueue, SubmissionToken token);

		[[nodiscard]] core::Result<void> WaitIdle();
		// Bounded queue/presentation drain; Timeout retains all owners. Rhi.DeviceLost
		// means native device loss was observed and resource owners may be released.
		[[nodiscard]] core::Result<void> WaitIdle(core::uint32 timeoutMilliseconds);

		void Collect() noexcept;

		~Device() noexcept;

		[[nodiscard]] static core::Result<void> Create(DeviceCreateDesc desc, core::MemoryResource& resource, core::UniquePtr<Device>* outDevice);

	private:
		static constexpr core::uint32 SHARED_TEXTURE_READ_SLOT = UINT32_MAX - 1;

		struct State;
		class CallScope;
		struct FixedGraphicsBindings;
		struct ExternalFenceSlot;
		struct StagingSubresource;
		struct TextureSamplesEntry;
		struct TextureRequirementsEntry;

		void claimCommand(const CommandSlot& command) const noexcept;

		template <typename TFunction> [[nodiscard]] core::Result<void> recordNative(TFunction&& function);

		[[nodiscard]] static AccelStructBuildDesc preparedBuildDescription(const PreparedRecordingAccelBuild& build) noexcept;
		[[nodiscard]] core::Span<const core::uint64> preparedAccelShape(const PreparedRecordingPlannedAccel& planned) const noexcept;
		[[nodiscard]] core::Span<const AccelStructHandle> preparedAccelReferences(const PreparedRecordingPlannedAccel& planned) const noexcept;
		[[nodiscard]] static bool preparedAccelContains(const PreparedRecordingPhase& phase, AccelStructHandle handle, EAccelStructState state) noexcept;
		[[nodiscard]] bool preparedAccelStorageDeclared(const PreparedRecordingPhase& phase, BufferHandle buffer) const noexcept;
		[[nodiscard]] bool preparedAccelStorageWrites(const PreparedRecordingPhase& phase, BufferHandle buffer) const noexcept;
		[[nodiscard]] static bool preparedShapeEqual(core::Span<const core::uint64> left, core::Span<const core::uint64> right) noexcept;

		[[nodiscard]] core::Result<void> capturePreparedBuildShape(PreparedRecordingAccelBuild& build);
		[[nodiscard]] core::Result<void> validatePreparedBuild(PreparedRecordingGroup& group, core::uint32 listIndex, core::uint32 phaseIndex, PreparedRecordingAccelBuild& build);
		[[nodiscard]] static bool preparedAccelOperationsComplete(const PreparedRecordingPhase& phase) noexcept;

		[[nodiscard]] core::Result<void> prepareAccelPhase(PreparedRecordingGroup& group, core::uint32 listIndex, core::uint32 phaseIndex);
		[[nodiscard]] core::Result<void> capturePreparedAccelPhase(CommandSlot& command, PreparedRecordingPhase& phase);

		void commitPreparedAccelMetadata(CommandSlot& command, ESubmissionDisposition disposition, core::uint64 serial) noexcept;

		[[nodiscard]] core::Result<void> validatePreparedRayLocalBindings(const ShaderTableSlot& table, const PreparedRecordingPhase& phase);
		[[nodiscard]] core::Result<void> realizeRayShaderTable(ShaderTableSlot& table, const RayTracingPipelineSlot& pipeline);
		[[nodiscard]] core::Result<core::uint32> capturePreparedRayBinding(
			CommandSlot& command,
			const PreparedRecordingPhase& phase,
			const PreparedBinding& binding,
			core::Span<const NativeFixedBindingSet> sets);

		[[nodiscard]] core::Result<void> dispatchTypedRays(
			CommandListHandle list,
			CommandSlot& command,
			ShaderTableSlot& table,
			RayTracingPipelineSlot& pipeline,
			const RayBindings& dispatchBindings,
			const RayDispatch& dispatch);

		[[nodiscard]] core::Result<void> validatePreparedDependencies(const PreparedRecordingGroup& group);
		[[nodiscard]] CommandSlot* findPreparedControlCommand(CommandListHandle handle) noexcept;

		void fillPreparedNativeSets(
			const PreparedBinding& binding,
			NativeFixedDescriptor* const (&descriptors)[MAXIMUM_FIXED_BINDING_LAYOUTS],
			NativeFixedBindingSet(&sets)[MAXIMUM_FIXED_BINDING_LAYOUTS]) noexcept;
		void fillPreparedNativeSets(
			const PreparedBinding& binding,
			NativeFixedDescriptor(&descriptors)[MAXIMUM_FIXED_BINDING_LAYOUTS][32],
			NativeFixedBindingSet(&sets)[MAXIMUM_FIXED_BINDING_LAYOUTS]) noexcept;

		[[nodiscard]] static core::Result<void> preparedFailure(core::EErrorCode code) noexcept;
		[[nodiscard]] PreparedRecordingList* preparedList(CommandSlot* command) noexcept;
		[[nodiscard]] PreparedRecordingPhase* preparedPhase(CommandSlot* command) noexcept;
		[[nodiscard]] core::Result<void> preparedEmission(CommandSlot& command, core::Result<void> result) noexcept;

		[[nodiscard]] bool preparedBufferContains(const PreparedRecordingPhase& phase, BufferHandle buffer, EBufferState state, core::uint64 offset, core::uint64 bytes) const noexcept;

		[[nodiscard]] static ETrackedResourceState expectedVulkanSharedReadState(const TextureDesc& texture, ETextureState state) noexcept;
		[[nodiscard]] static bool preparedTextureAspectSupported(const TextureDesc& desc, ETextureAspect aspect) noexcept;
		[[nodiscard]] bool preparedTextureContains(const PreparedRecordingPhase& phase, TextureHandle texture, ETextureState state, const TextureViewDesc* view = nullptr) const noexcept;

		[[nodiscard]] static core::uint32 preparedVertexAlignment(EFormat format) noexcept;
		[[nodiscard]] core::Result<void> validatePreparedVertexInput(PreparedRecordingPhase& phase, PreparedBinding& binding, const GraphicsPipelineSlot& pipeline);
		[[nodiscard]] static bool preparedVertexBounds(const PreparedBinding& binding, core::uint64 vertexEnd, core::uint64 instanceEnd) noexcept;

		[[nodiscard]] core::Result<void> validatePreparedBinding(CommandSlot& command, PreparedRecordingPhase& phase, PreparedBinding& binding, EQueue queue);
		[[nodiscard]] core::Result<void> validatePreparedIndirect(PreparedRecordingPhase& phase, PreparedRecordingIndirect& indirect, EQueue queue);

		[[nodiscard]] core::Result<void> recordPreparedGpuTimer(CommandSlot& command, PreparedRecordingPhase& phase, bool bBegin);

		[[nodiscard]] core::Result<void> recordIndirectPrepared(CommandListHandle handle, PreparedIndirectRef ref, EPreparedIndirectKind kind, core::Span<const core::Byte> pushConstants);

		[[nodiscard]] NativeSubmissionOutcome submitPreparedWithAdmission(CommandListHandle handle, core::Span<const SubmissionToken> prerequisites, const NativeQueueAccessHandle* admission);
		[[nodiscard]] NativeSubmissionOutcome submitPreparedManaged(CommandListHandle handle, core::Span<const SubmissionToken> prerequisites);

		[[nodiscard]] core::Result<void> copyPreparedBuffer(CommandSlot& command, const BufferCopy& copy);
		[[nodiscard]] core::Result<void> copyPreparedTexture(CommandSlot& command, const TextureCopy& copy);

		[[nodiscard]] core::Result<void> endPreparedRendering(CommandSlot& command);
		[[nodiscard]] core::Result<void> setPreparedViewport(CommandSlot& command, const ViewportDesc& viewport);
		[[nodiscard]] core::Result<void> setPreparedScissor(CommandSlot& command, const ScissorDesc& scissor);

		[[nodiscard]] core::Result<void> copyPreparedTextureRegion(CommandSlot& command, const TextureCopyRegion& copy);

		[[nodiscard]] bool hasPreparedBuffer(BufferHandle handle) const noexcept;
		[[nodiscard]] core::Result<void> preparedMemoryBarrier(CommandSlot& command);

		[[nodiscard]] core::Result<core::SharedPtr<BoundDescriptorTables>> captureDescriptorHeapBindings(core::Span<const DescriptorHeapBinding> inputs, bool bEnabled);
		void fillNativeDeviceHeaps(const BoundDescriptorTables* owner, NativeDescriptorHeapBinding(&native)[32]) noexcept;
		[[nodiscard]] core::Result<void> validateDeviceHeapUses(
			CommandSlot& command,
			const BoundDescriptorTables* owner,
			const PreparedRecordingPhase* phase = nullptr, core::usize additionalUses = 0);
		[[nodiscard]] core::Result<void> validateOrdinaryDeviceHeapUses(
			CommandSlot& command,
			const BoundDescriptorTables* owner,
			core::Span<const BindingSetHandle> sets,
			core::Span<const DescriptorTableHandle> named,
			core::Span<const TextureHandle> sampled = {},
			core::Span<const TextureHandle> storage = {});
		void trackDeviceHeapUses(CommandSlot& command, const BoundDescriptorTables* owner) noexcept;
		[[nodiscard]] core::Result<void> recordDescriptorHeapBindings(CommandSlot& command, bool bGraphics, const BoundDescriptorTables* owner);

		[[nodiscard]] PreparedRecordingPhase* findNativeCapturePhase(CommandListHandle handle, core::uint32 phaseIndex) noexcept;

		[[nodiscard]] HeapSlot* getHeap(HeapHandle handle) noexcept;
		[[nodiscard]] core::Result<void> collectRetiredRanges();

		[[nodiscard]] core::Result<void> destroyHeapImpl(HeapHandle handle, bool bWait);

		[[nodiscard]] static core::Error externalInteropError(core::EErrorCode code, const char* message);
		[[nodiscard]] bool matchesExternalDevice(const GpuIdentity& identity) const;

		[[nodiscard]] core::Result<BufferHandle> makeExternalBuffer(const BufferDesc& desc, const ExternalBufferMemoryView* memoryOrNull);

		[[nodiscard]] core::Result<TextureHandle> makeExternalTexture(const TextureDesc& desc, const ExternalTextureMemoryView* memoryOrNull);

		[[nodiscard]] ExternalFenceSlot* findExternalFence(ExternalFenceHandle handle);

		[[nodiscard]] core::Result<ExternalFenceHandle> makeExternalFence(core::uint64 initialValue, const ExternalFenceView* fence);

		[[nodiscard]] core::Result<SubmissionToken> transferExternalResource(BufferHandle buffer, TextureHandle texture, EQueue queue, ExternalFenceHandle fence, core::uint64 value, bool bAcquire);

		[[nodiscard]] core::Result<void> validateBufferDesc(const BufferDesc& desc) const;

		[[nodiscard]] core::Result<BufferHandle> createBufferInternal(
			const BufferDesc& desc,
			HeapHandle heap,
			core::uint64 offset,
			core::uint64 allocationSize,
			void* nativeBuffer = nullptr, void* adoptedBuffer = nullptr);

		[[nodiscard]] core::Result<void> reserveMicromapReferences(AccelStructSlot& target, core::Span<const OpacityMicromapHandle> handles);
		void retainMicromapReferences(AccelStructSlot& target, core::Span<const OpacityMicromapHandle> handles);

		[[nodiscard]] static AccelBuildSignature makeAccelBuildSignature(const AccelStructBuildDesc& desc) noexcept;

		[[nodiscard]] bool canBuildAfterStateRequests(const CommandSlot& command, AccelStructHandle handle, core::usize recordedUses) const noexcept;

		[[nodiscard]] core::Result<AccelStructSlot*> resolveAccelStruct(core::uint32 slot, AccelStructHandle handle) noexcept;
		[[nodiscard]] core::Result<void> trackBottomLevelSubmissionUses(CommandSlot& command);
		void trackAccelStruct(CommandSlot& command, AccelStructHandle handle, AccelStructSlot* accel, bool bBuildWriteState = false) noexcept;
		[[nodiscard]] bool reserveAccelStructUse(CommandSlot& command, const AccelStructSlot* accel) noexcept;

		[[nodiscard]] core::Result<void> reserveClusterMicromapReferences(BufferSlot& target, core::Span<const OpacityMicromapHandle> handles);
		void retainClusterMicromapReferences(BufferSlot& target, core::Span<const OpacityMicromapHandle> handles);

		[[nodiscard]] core::Result<core::uint64> validateTextureDesc(const TextureDesc& desc) const;

		[[nodiscard]] core::Result<TextureHandle> createTextureInternal(
			const TextureDesc& desc,
			HeapHandle heap,
			core::uint64 offset,
			core::uint64 allocationSize,
			void* nativeTexture = nullptr, ENativeTextureContents contents = ENativeTextureContents::Discard, void* adoptedTexture = nullptr);

		[[nodiscard]] PreparedTextureMappings* findPreparedTextureMappings(PreparedTextureMappingsHandle handle) noexcept;
		void releasePreparedTextureMappings(PreparedTextureMappings& owner) noexcept;
		void collectPreparedTextureMappings(bool bIdle, bool bDestroy = false) noexcept;

		[[nodiscard]] core::usize uncertainTextureMappingPins(TextureHandle texture) const noexcept;
		void releaseUncertainTextureMappings(TextureHandle texture) noexcept;

		[[nodiscard]] TextureSlot* findSamplerFeedback(SamplerFeedbackHandle handle) noexcept;

		[[nodiscard]] core::Result<SamplerFeedbackHandle> createSamplerFeedbackTextureInternal(TextureHandle pairedTexture, const SamplerFeedbackDesc& desc, void* nativeFeedbackTexture);

		[[nodiscard]] TextureViewSlot* findTextureView(TextureViewHandle handle) noexcept;

		[[nodiscard]] BufferViewSlot* findBufferView(BufferViewHandle handle) noexcept;

		[[nodiscard]] core::Result<void> validateBufferBindingFormat(EFormat format, bool bUniform, bool bReadOnly) const;

		[[nodiscard]] core::Result<void*> resolveBoundBufferView(
			BindingSetHandle handle,
			core::usize binding,
			BufferHandle buffer,
			EFormat format,
			bool bReadOnly,
			core::Span<const BufferViewHandle> inlineViews = {});

		[[nodiscard]] core::Result<void> validateTypedBufferTables(core::uint32 storageCapacity, EFormat storageFormat, core::uint32 readOnlyCapacity, EFormat readOnlyFormat) const;

		[[nodiscard]] SamplerSlot* findSampler(SamplerHandle handle) noexcept;

		[[nodiscard]] core::Result<bool> isDescriptorTableVersionBusy(const DescriptorTableVersion& version) const;
		void releaseDescriptorEntry(const DescriptorTableEntry& entry) noexcept;
		[[nodiscard]] bool tryRetainDescriptorEntry(const DescriptorTableEntry& entry) noexcept;
		[[nodiscard]] core::Result<void> collectDescriptorTableVersions(DescriptorTableSlot& table);
		[[nodiscard]] DescriptorTableVersion& findDescriptorTableVersion(const RecordedDescriptorTableUse& use) noexcept;
		[[nodiscard]] core::Result<void> checkDescriptorTableIdle(DescriptorTableSlot& table);

		[[nodiscard]] core::Result<void> publishDescriptorTableVersion(DescriptorTableSlot& table, const DescriptorTableDesc& desc, core::Array<DescriptorTableEntry>&& entries);
		[[nodiscard]] core::Result<void> updateDescriptorTableEntry(DescriptorTableSlot& table, core::uint32 index, const DescriptorTableEntry& replacement);

		[[nodiscard]] core::Result<void*> createReplayedDescriptorTable(const DescriptorTableDesc& desc, const core::Array<DescriptorTableEntry>& entries, core::uint32 copied);

		[[nodiscard]] DescriptorTableSlot* findDescriptorTable(DescriptorTableHandle handle) noexcept;
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveDescriptorTable(
			core::uint32 capacity,
			EDescriptorType type,
			DescriptorTableHandle handle,
			CommandSlot& command,
			core::uint32 listSlot,
			EFormat format = EFormat::UNKNOWN);

		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveComputeDescriptorTable(const PipelineSlot& pipeline, DescriptorTableHandle handle, CommandSlot& command, core::uint32 listSlot);
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveComputeSamplerTable(const PipelineSlot& pipeline, DescriptorTableHandle handle, CommandSlot& command, core::uint32 listSlot);
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveComputeReadOnlyBufferTable(const PipelineSlot& pipeline, DescriptorTableHandle handle, CommandSlot& command, core::uint32 listSlot);
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveComputeStorageBufferTable(const PipelineSlot& pipeline, DescriptorTableHandle handle, CommandSlot& command, core::uint32 listSlot);
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveComputeStorageTextureTable(const PipelineSlot& pipeline, DescriptorTableHandle handle, CommandSlot& command, core::uint32 listSlot);
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveComputeUniformBufferTable(const PipelineSlot& pipeline, DescriptorTableHandle handle, CommandSlot& command, core::uint32 listSlot);

		[[nodiscard]] core::Result<void> checkStorageTableTextureAliases(const DescriptorTableSlot* storage, const DescriptorTableSlot* sampled);
		[[nodiscard]] core::Result<void> checkGraphicsFixedTextureTableAliases(
			const DescriptorTableSlot* sampled,
			const DescriptorTableSlot* storage,
			core::Span<const TextureHandle> fixedSampled,
			core::Span<const TextureHandle> fixedStorage);

		[[nodiscard]] core::Result<void> checkBufferTableAliases(const DescriptorTableSlot* table, core::Span<const BufferHandle> fixed, BufferHandle arguments = {}) const;

		[[nodiscard]] core::Result<void> checkBufferTableOverlap(const DescriptorTableSlot* storage, const DescriptorTableSlot* uniform);

		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveGraphicsTextureTable(
			core::uint32 capacity,
			EDescriptorType type,
			DescriptorTableHandle handle,
			CommandSlot& command,
			core::uint32 listSlot);
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveGraphicsDescriptorTable(const GraphicsPipelineSlot& pipeline, DescriptorTableHandle handle, CommandSlot& command, core::uint32 listSlot);
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveGraphicsStorageTextureTable(
			const GraphicsPipelineSlot& pipeline,
			DescriptorTableHandle handle,
			CommandSlot& command,
			core::uint32 listSlot);
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveGraphicsSamplerTable(const GraphicsPipelineSlot& pipeline, DescriptorTableHandle handle, CommandSlot& command, core::uint32 listSlot);
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveGraphicsUniformBufferTable(
			const GraphicsPipelineSlot& pipeline,
			DescriptorTableHandle handle,
			CommandSlot& command,
			core::uint32 listSlot);
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveGraphicsReadOnlyBufferTable(
			const GraphicsPipelineSlot& pipeline,
			DescriptorTableHandle handle,
			CommandSlot& command,
			core::uint32 listSlot);
		[[nodiscard]] core::Result<DescriptorTableSlot*> resolveGraphicsStorageBufferTable(
			const GraphicsPipelineSlot& pipeline,
			DescriptorTableHandle handle,
			CommandSlot& command,
			core::uint32 listSlot);

		[[nodiscard]] core::Result<void> checkGraphicsBufferTableAliases(
			const DescriptorTableSlot* table,
			core::Span<const BufferHandle> fixed,
			BufferHandle vertex,
			BufferHandle index = {},
			BufferHandle arguments = {},
			BufferHandle count = {}) const;

		[[nodiscard]] bool reserveBindingTableUses(
			CommandSlot& command,
			const DescriptorTableSlot* sampled,
			const DescriptorTableSlot* samplers,
			const DescriptorTableSlot* storage,
			const DescriptorTableSlot* storageTextures,
			const DescriptorTableSlot* uniform,
			const DescriptorTableSlot* readOnly,
			core::usize fixedTextures,
			core::usize fixedBuffers,
			BindingSetHandle bindingSet);
		void trackDescriptorTableUse(CommandSlot& command, core::uint32 listSlot, DescriptorTableHandle handle, DescriptorTableSlot* table) noexcept;

		[[nodiscard]] ShaderSlot* findShader(ShaderHandle handle) noexcept;
		[[nodiscard]] core::Result<void> resolveShader(ShaderHandle handle, EShaderStage stage, core::Span<const core::Byte>& bytecode, const char*& entryPoint) noexcept;
		[[nodiscard]] core::Result<void> resolveGraphicsShaders(NativeGraphicsPipelineDesc& desc) noexcept;

		[[nodiscard]] ShaderLibrarySlot* findShaderLibrary(ShaderLibraryHandle handle) noexcept;

		[[nodiscard]] core::Result<void> resizeSwapChainWithPolicy(SwapChainHandle handle, core::uint32 width, core::uint32 height, bool bPoll);

		[[nodiscard]] core::Result<SubmissionToken> copyAndPresentInternal(SwapChainHandle handle, TextureHandle source);

		[[nodiscard]] core::Result<void> destroySwapChainWithPolicy(SwapChainHandle handle, bool bPoll);

		void recycleClosedAcquiredImages() noexcept;
		void collectAcquiredImageUses(bool bOwnedIdle) noexcept;
		[[nodiscard]] bool hasRetainedAcquiredImages(SwapChainHandle owner) const noexcept;
		void retireSwapChainImage(SwapChainSlot& swapChain) noexcept;

		[[nodiscard]] core::Result<AcquiredSwapChainImage> acquireSwapChainImageInternal(SwapChainHandle handle, core::uint32 timeoutMilliseconds);

		[[nodiscard]] core::Result<SubmissionToken> presentSwapChainImageInternal(AcquiredSwapChainImage acquisition);

		[[nodiscard]] core::Result<void> setTextureStateInternal(CommandListHandle list, TextureHandle texture, const TextureSubresource* subresource, ETextureState state, bool bBeginTracking = false);

		[[nodiscard]] core::Result<void> setBufferStateInternal(CommandListHandle list, BufferHandle buffer, EBufferState state, bool bBeginTracking = false);

		[[nodiscard]] const TimerSlot* findGpuTimer(GpuTimerHandle handle) const noexcept;
		[[nodiscard]] TimerSlot* findGpuTimer(GpuTimerHandle handle) noexcept;

		[[nodiscard]] const EventQuerySlot* findEventQuery(EventQueryHandle handle) const noexcept;
		[[nodiscard]] EventQuerySlot* findEventQuery(EventQueryHandle handle) noexcept;

		[[nodiscard]] core::Result<TextureBufferFootprint> calculateTextureBufferFootprint(const TextureDesc& desc, TextureSubresource subresource) const;

		[[nodiscard]] core::Result<void> copyTextureBuffer(CommandListHandle list, const TextureBufferCopy& copy, bool bUpload);

		[[nodiscard]] core::Result<StagingSubresource> describeStagingSubresource(const TextureDesc& desc, TextureSubresource requested) const;

		[[nodiscard]] core::Result<void> copyStagingTexture(
			CommandListHandle list,
			StagingTextureHandle staging,
			TextureSubresource stagingSubresource,
			TextureHandle texture,
			TextureSubresource textureSubresource,
			bool bUpload);

		[[nodiscard]] core::Result<void> validateTextureResolve(const TextureResolve& resolve, const TextureSlot& source, const TextureSlot& destination, NativeTextureResolve* outNative);

		[[nodiscard]] core::Result<void> clearIntegerTextureSubresource(CommandListHandle list, TextureHandle texture, TextureSubresource subresource, core::uint32 value, bool bSigned);

		[[nodiscard]] core::Result<ComputePipelineHandle> createComputePipelineMultiple(const ComputePipelineDesc& input, const NativeComputePipelineDesc& desc);

		[[nodiscard]] core::Result<void> dispatchComputeMultiple(
			CommandListHandle list,
			PipelineSlot& pipeline,
			ComputePipelineHandle pipelineHandle,
			core::Span<const BindingSetHandle> setHandles,
			core::Span<const core::Byte> pushConstants,
			core::uint32 groupsX,
			core::uint32 groupsY,
			core::uint32 groupsZ,
			BufferHandle argumentHandle,
			core::uint64 argumentOffset,
			DescriptorTableHandle sampledTableHandle,
			DescriptorTableHandle samplerTableHandle,
			DescriptorTableHandle storageBufferTableHandle,
			DescriptorTableHandle storageTextureTableHandle,
			DescriptorTableHandle uniformBufferTableHandle,
			DescriptorTableHandle readOnlyBufferTableHandle,
			AccelStructHandle accelHandle,
			core::Span<const DescriptorHeapBinding> deviceHeaps);

		static void validateGraphicsState(const NativeGraphicsPipelineDesc& desc) noexcept;
		[[nodiscard]] static bool isValidBlendConstant(const BlendColor& color) noexcept;

		[[nodiscard]] bool matchesRendering(const CommandSlot& command, const GraphicsPipelineSlot& pipeline) const noexcept;
		[[nodiscard]] FramebufferSlot* findFramebuffer(FramebufferHandle handle) noexcept;

		[[nodiscard]] core::Result<BindingLayoutHandle> createRangeBindingLayout(const BindingLayoutDesc& desc);
		[[nodiscard]] core::Result<void> recordRangeBindingSets(CommandSlot& command, void* pipeline, bool bGraphics, core::Span<const BindingSetHandle> handles);
		[[nodiscard]] core::Result<void> validateRangePipelineLayouts(core::Span<const BindingLayoutHandle> handles, EBindingLayoutKind kind, bool bAdditionalBindings);
		[[nodiscard]] core::uint32 rangeBufferStructureStride(const BindingLayoutSlot& layout, core::usize binding) noexcept;

		[[nodiscard]] core::Result<core::SharedPtr<BoundDescriptorTables>> prepareBoundDescriptorTables(const BindingLayoutSlot& layout, core::Span<const BindingTable> bindings);
		[[nodiscard]] const BoundDescriptorTableRange* findBoundDescriptorTable(const BindingSetSlot& set, core::uint32 rangeIndex) const noexcept;
		[[nodiscard]] static core::uint32 nativeFixedDescriptorCount(const BindingLayoutSlot& layout) noexcept;
		void trackDescriptorTableOwner(CommandSlot& command, DescriptorTableHandle handle) noexcept;
		[[nodiscard]] core::Result<void> prepareOrdinaryBoundTableUses(CommandSlot& command, core::Span<const BindingSetHandle> handles);
		void trackBoundTableUses(CommandSlot& command, const BoundDescriptorTableRange& row, const BoundDescriptorTables& owner) noexcept;

		[[nodiscard]] BindingLayoutSlot* findBindingLayout(BindingLayoutHandle handle) noexcept;

		template <typename TPipeline>
		[[nodiscard]] core::Result<void> deriveDirectTableCapacities(TPipeline& pipeline);

		[[nodiscard]] BindingSetSlot* findBindingSet(BindingSetHandle handle) noexcept;

		[[nodiscard]] BufferRange resolveBufferRange(BindingSetHandle bindingSet, core::usize binding, core::uint64 fullByteCount, core::Span<const BufferViewHandle> inlineViews = {}) noexcept;
		[[nodiscard]] static bool canShareBuffer(bool bCurrentReadOnly, bool bPreviousReadOnly, BufferRange current, BufferRange previous) noexcept;

		[[nodiscard]] core::Span<const TextureHandle> resolveGraphicsTextures(
			BindingLayoutHandle expectedLayout,
			BindingSetHandle bindingSet,
			core::Span<const TextureHandle> inlineTextures) noexcept;

		[[nodiscard]] core::Span<const TextureHandle> resolveComputeTextures(
			BindingLayoutHandle expectedLayout,
			BindingSetHandle bindingSet,
			core::Span<const TextureHandle> inlineTextures) noexcept;
		[[nodiscard]] core::Span<const TextureHandle> resolveComputeStorageTextures(
			BindingLayoutHandle expectedLayout,
			BindingSetHandle bindingSet,
			core::Span<const TextureHandle> inlineTextures) noexcept;
		[[nodiscard]] core::Span<const TextureHandle> resolveGraphicsStorageTextures(
			BindingLayoutHandle expectedLayout,
			BindingSetHandle bindingSet,
			core::Span<const TextureHandle> inlineTextures) noexcept;
		[[nodiscard]] core::Span<const BufferHandle> resolveGraphicsBuffers(BindingLayoutHandle expectedLayout, BindingSetHandle bindingSet) noexcept;

		[[nodiscard]] bool textureViewsDisjoint(TextureViewHandle first, TextureViewHandle second) const noexcept;

		template <typename TPipeline>
		[[nodiscard]] static TextureViewHandle computeTextureView(const TPipeline& pipeline, const BindingSetSlot* set, core::usize index) noexcept;

		[[nodiscard]] EFormat sampledViewFormat(const BindingSetSlot* set, core::usize index, EFormat defaultFormat) const noexcept;

		template <typename TPipeline>
		[[nodiscard]] core::Result<void> validateComputeTextures(
			const TPipeline& pipeline,
			core::Span<const TextureHandle> sampled,
			core::Span<const TextureHandle> storage,
			CommandSlot& command,
			core::uint32 listSlot,
			BindingSetHandle bindingSet,
			NativeComputeTexture(&native)[8]) noexcept;

		[[nodiscard]] core::Result<void> validateGraphicsBuffers(
			const GraphicsPipelineSlot& pipeline,
			core::Span<const BufferHandle> handles,
			BindingSetHandle bindingSet,
			BufferHandle vertex,
			BufferHandle index,
			BufferHandle arguments,
			NativeGraphicsBuffer* native) noexcept;
		void trackGraphicsBuffers(CommandSlot& command, const GraphicsPipelineSlot& pipeline, core::Span<const BufferHandle> handles) noexcept;

		[[nodiscard]] core::Result<void> resolveExtraVertexBuffers(
			const GraphicsPipelineSlot& pipeline,
			core::Span<const VertexBufferBinding> bindings,
			core::uint64 lastVertex,
			core::uint64 lastInstance,
			BufferHandle primary,
			BufferHandle index,
			BufferHandle arguments,
			BufferHandle count,
			core::Span<const BufferHandle> fixed,
			const DescriptorTableSlot* storageBuffers,
			const DescriptorTableSlot* uniformBuffers,
			const DescriptorTableSlot* readOnlyBuffers,
			NativeVertexBuffer(&native)[3]);
		void trackExtraVertexBuffers(CommandSlot& command, core::Span<const VertexBufferBinding> bindings) noexcept;

		[[nodiscard]] core::Result<void> validateShadingRate(EShadingRate rate, EShadingRateCombiner primitive, EShadingRateCombiner image, bool bHasImage, core::uint32 sampleCount) const noexcept;

		[[nodiscard]] core::Result<void> drawInternal(CommandListHandle list, const GraphicsState& drawState, const DrawArguments& draw, bool bMesh);

		[[nodiscard]] static bool meshGroupsFit(core::uint32 x, core::uint32 y, core::uint32 z, const core::uint32* maximum, core::uint32 total) noexcept;

		[[nodiscard]] static BindingSetHandle singleBindingSet(core::Span<const BindingSetHandle> sets) noexcept;

		[[nodiscard]] core::Result<void> resolveFixedGraphicsBindings(
			core::Span<const BindingSetHandle> sets,
			core::Span<const VertexBufferBinding> extraVertexBuffers,
			CommandSlot& command,
			const GraphicsPipelineSlot& pipeline,
			core::uint32 listSlot,
			BufferHandle vertex,
			BufferHandle index,
			BufferHandle arguments,
			BufferHandle count,
			FixedGraphicsBindings& output);
		void trackGraphicsBindingSets(CommandSlot& command, core::Span<const BindingSetHandle> sets) noexcept;

		[[nodiscard]] core::Result<void> drawIndirectInternal(CommandListHandle list, const GraphicsDrawIndirect& draw, bool bMesh);

		[[nodiscard]] SubmissionResult submitInternal(CommandListHandle list);

		[[nodiscard]] bool isKnownToken(SubmissionToken token) const noexcept;

		void retireGpuTimersAfterIdle() noexcept;
		void releaseQuarantinedCommands(bool bRequireCompletion = false) noexcept;

		void recordSubmissionFault(const core::Error& error) noexcept;
		void flushSubmissionFault() noexcept;

		void releasePresentRetention(bool bRequireCompletion) noexcept;
		void releaseFailedAcquisition() noexcept;
		void releaseFailedReadback() noexcept;

		void releaseRecordedUses(CommandSlot& command) noexcept;

		[[nodiscard]] core::Result<void> copyPreparedTextureBuffer(CommandListHandle handle, PreparedTextureBufferCopyRef ref, ETextureBufferCopyDirection direction);

		[[nodiscard]] void* nativeQueueIdentity(EQueue queue) const noexcept;
		[[nodiscard]] bool hasNativeQueueAccess(EQueue queue) const noexcept;
		[[nodiscard]] bool hasAnyNativeQueueAccess() const noexcept;
		[[nodiscard]] bool ownsNativeQueueAccess(NativeQueueAccessHandle handle, EQueue queue) const noexcept;
		[[nodiscard]] static core::Error nativeQueueAccessConflict() noexcept;

		[[nodiscard]] core::Result<core::uint32> getPreparedNativeBoundaryState(CommandListHandle list, BufferHandle buffer, TextureHandle texture);

		[[nodiscard]] core::Result<AccelStructBuildSizes> getAccelStructBuildSizes(const AccelStructBuildDesc& desc) const;

		void resetRecordedTimers(CommandSlot& command) noexcept;

		[[nodiscard]] static bool canReadTexture(const TextureSlot& texture, core::uint32 listSlot) noexcept;
		void trackTextureReadOrWrite(CommandSlot& command, core::uint32 listSlot, TextureHandle handle, TextureSlot& texture, ETrackedResourceState state) noexcept;

		[[nodiscard]] core::Result<void> validateGraphicsTextures(
			CommandSlot& command,
			const GraphicsPipelineSlot& pipeline,
			core::uint32 listSlot,
			BindingSetHandle bindingSet,
			core::Span<const TextureHandle> handles,
			NativeSampledTexture* native) noexcept;
		[[nodiscard]] core::Result<void> validateGraphicsStorageTextures(
			CommandSlot& command,
			const GraphicsPipelineSlot& pipeline,
			core::uint32 listSlot,
			BindingSetHandle bindingSet,
			core::Span<const TextureHandle> sampled,
			core::Span<const TextureHandle> handles,
			NativeStorageTexture* native) noexcept;
		void trackGraphicsTextures(CommandSlot& command, core::uint32 listSlot, core::Span<const TextureHandle> handles, ETrackedResourceState state = ETrackedResourceState::ShaderRead) noexcept;
		void trackBoundSamplers(CommandSlot& command, BindingSetHandle bindingSet) noexcept;
		void trackBoundViews(CommandSlot& command, BindingSetHandle bindingSet) noexcept;

		[[nodiscard]] BufferSlot* findBuffer(BufferHandle handle, bool bAllowExternal = false) noexcept;
		[[nodiscard]] const BufferSlot* findBuffer(BufferHandle handle, bool bAllowExternal = false) const noexcept;
		[[nodiscard]] AccelStructSlot* findAccelStruct(AccelStructHandle handle) noexcept;
		[[nodiscard]] OpacityMicromapSlot* findOpacityMicromap(OpacityMicromapHandle handle) noexcept;
		[[nodiscard]] const OpacityMicromapSlot* findOpacityMicromap(OpacityMicromapHandle handle) const noexcept;
		[[nodiscard]] StagingTextureSlot* findStagingTexture(StagingTextureHandle handle) noexcept;
		[[nodiscard]] CommandSlot* findCommand(CommandListHandle handle) noexcept;
		void poisonCommandList(CommandListHandle handle) noexcept;
		[[nodiscard]] const CommandSlot* findCommand(CommandListHandle handle) const noexcept;
		[[nodiscard]] TextureSlot* findTexture(TextureHandle handle, bool bAllowExternal = false) noexcept;
		[[nodiscard]] SwapChainSlot* findSwapChain(SwapChainHandle handle) noexcept;
		[[nodiscard]] PipelineSlot* findPipeline(ComputePipelineHandle handle) noexcept;
		[[nodiscard]] GraphicsPipelineSlot* findGraphicsPipeline(GraphicsPipelineHandle handle) noexcept;
		[[nodiscard]] InputLayoutSlot* findInputLayout(InputLayoutHandle handle) noexcept;
		[[nodiscard]] RayTracingPipelineSlot* findRayTracingPipeline(RayTracingPipelineHandle handle) noexcept;
		[[nodiscard]] ShaderTableSlot* findShaderTable(ShaderTableHandle handle) noexcept;

		[[nodiscard]] EFormatSupport queryNativeFormatSupport(EFormat format) const noexcept;
		[[nodiscard]] core::Result<core::uint32> queryNativeTextureSampleCounts(const TextureDesc& description) const;
		[[nodiscard]] static bool sameTextureDescriptor(const TextureDesc& left, const TextureDesc& right) noexcept;


		struct CreationKey final
		{
			explicit CreationKey() noexcept = default;
		};

	public:
		Device(CreationKey, core::UniquePtr<State>&& state) noexcept;

	private:
		core::UniquePtr<State> m_State;
	};
}
