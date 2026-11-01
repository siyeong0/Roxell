#pragma once
#include "../AftermathBackend.h"
#include "../DeviceResources.h"
#include "../DevicePipelines.h"
#include "VulkanState.h"
#include "VulkanFormat.h"
#include <Algorithms.hpp>
#include <Assertion.h>
#include <ByteOperations.hpp>
#include <ClockPrimitives.hpp>
#include <Comparison.hpp>
#include <ComparisonTypes.hpp>
#include <Log.h>
#include <NumericLimits.hpp>
#include <ScopeExit.hpp>
#include <TextPrimitives.hpp>
#include <Utility.hpp>
#include <Vocabulary.hpp>
namespace rox::vulkan
{
	using namespace rox::rhi;
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	extern thread_local bool bReturnNotReadyOnNextAcquire;
	void ReturnNotReadyOnNextVulkanAcquireForTesting() noexcept;
#endif
	inline constexpr core::uint64 MAXIMUM_BUFFER_BYTES = 256ull * 1024ull * 1024ull;
	inline constexpr core::uint64 NANOSECONDS_PER_MILLISECOND = 1000000;

	void FillSpecializations(core::Span<const ShaderSpecialization> source, VkSpecializationMapEntry* entries, core::uint32* data, VkSpecializationInfo& info) noexcept;

	[[nodiscard]] bool SupportsNonzeroMipStencilViews(const State& state) noexcept;

	[[nodiscard]] core::Error Failure(const char* id, const char* message) noexcept;

	void InvalidateIdle(State& state) noexcept;
	bool WasSubmissionAccepted(const State* opaque) noexcept;
	bool HasRetirementProof(const State* opaque) noexcept;
	core::uint64 GetPresentCopySerial(const State* opaque) noexcept;

	void ExposeNativeQueues(State* opaque) noexcept;
	[[nodiscard]] core::Result<void> WaitNativeFence(State& state, VkFence fence, core::uint64 timeout);

	void FreeRayPipeline(State& state, RayTracingPipeline* pipeline) noexcept;

	[[nodiscard]] VkSemaphore QueueTimeline(const State& state, EQueue queue) noexcept;
	[[nodiscard]] VkQueue NativeQueue(const State& state, EQueue queue) noexcept;
	[[nodiscard]] VkCommandPool QueuePool(const State& state, EQueue queue) noexcept;
	[[nodiscard]] VkQueueFlags QueueFlags(const State& state, EQueue queue) noexcept;

	template <typename TCreateInfo> void SetQueueFamilySharing(const State& state, TCreateInfo& info) noexcept
	{
		info.sharingMode = state.ResourceQueueFamilyCount > 1 ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;

		if (state.ResourceQueueFamilyCount > 1)
		{
			info.queueFamilyIndexCount = state.ResourceQueueFamilyCount;
			info.pQueueFamilyIndices = state.ResourceQueueFamilies;
		}
	}

	[[nodiscard]] core::uint64 QueueLastSerial(const State& state, EQueue queue) noexcept;
	[[nodiscard]] core::uint64 GetQueueLastSubmittedSerial(const State* opaque, EQueue queue) noexcept;
	[[nodiscard]] bool ReadCompletedSerial(const State& state, core::uint64& completed) noexcept;

	extern thread_local bool bPreparedErrors;

	struct PreparedErrorScope final
	{
		PreparedErrorScope() noexcept;
		~PreparedErrorScope() noexcept;

		bool bPrevious;
	};

	[[nodiscard]] core::Error NativeError(core::EErrorCode code, const char* id, const char* message) noexcept;

	[[nodiscard]] core::Error Failure(const char* id, const char* message) noexcept;

	void RecordBarriers(
		State& state,
		Pending& pending,
		VkPipelineStageFlags sourceStage,
		VkPipelineStageFlags destinationStage,
		VkDependencyFlags flags,
		core::uint32 memoryCount,
		const VkMemoryBarrier* memory,
		core::uint32 bufferCount,
		const VkBufferMemoryBarrier* buffers,
		core::uint32 imageCount,
		const VkImageMemoryBarrier* images) noexcept;

	void WriteCommandDescriptors(State& state, Pending& pending, core::uint32 writeCount, const VkWriteDescriptorSet* writes, core::uint32 copyCount, const VkCopyDescriptorSet* copies) noexcept;

	[[nodiscard]] VkSamplerAddressMode VulkanAddress(ESamplerAddressMode mode) noexcept;
	[[nodiscard]] bool SupportsSampler(const State& state, const SamplerDesc& sampler) noexcept;
	[[nodiscard]] VkSamplerCreateInfo MakeSamplerCreateInfo(const SamplerDesc& sampler, VkSamplerReductionModeCreateInfo& reduction) noexcept;
	[[nodiscard]] core::Result<void*> CreateSampler(State* opaque, const SamplerDesc& desc);
	void DestroySampler(State* opaque, void* native) noexcept;

	[[nodiscard]] core::Result<void*> CreateTextureView(State* opaque, void* nativeTexture, const TextureDesc& source, const TextureViewDesc& desc);
	void DestroyTextureView(State* opaque, void* native) noexcept;

	[[nodiscard]] core::Result<void*> CreateBufferView(State* opaque, void* native, const BufferViewDesc& desc);
	void DestroyBufferView(State* opaque, void* native) noexcept;

	template <typename TType> [[nodiscard]] VkDescriptorType BufferDescriptorType(TType type, EFormat format) noexcept
	{
		if (type == TType::UniformBuffer)
		{
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		}

		if (format == EFormat::UNKNOWN)
		{
			return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		}

		return type == TType::ReadOnlyBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
	}

	[[nodiscard]] core::Result<void> ValidateTypedTableLimits(
		const State& state,
		core::uint32 storageCapacity,
		EFormat storageFormat,
		core::uint32 readOnlyCapacity,
		EFormat readOnlyFormat,
		core::uint32 sampledCapacity,
		core::uint32 imageStorageCapacity);

	[[nodiscard]] VkAccelerationStructureKHR GetDescriptorAccelerationStructure(const void* native) noexcept;
	[[nodiscard]] VkDescriptorType TableDescriptorType(EDescriptorType type, EFormat format) noexcept;
	void DestroyDescriptorTable(State* opaque, void* native) noexcept;

	[[nodiscard]] bool CreateBindlessDescriptorSetLayout(State& state, EDescriptorType type, core::uint32 capacity, VkDescriptorSetLayout& output, EFormat format = EFormat::UNKNOWN);
	[[nodiscard]] bool CreateSampledDescriptorSetLayout(State& state, core::uint32 capacity, VkDescriptorSetLayout& output);
	[[nodiscard]] bool CreateSamplerDescriptorSetLayout(State& state, core::uint32 capacity, VkDescriptorSetLayout& output);
	[[nodiscard]] bool CreateStorageBufferDescriptorSetLayout(State& state, core::uint32 capacity, VkDescriptorSetLayout& output, EFormat format = EFormat::UNKNOWN);
	[[nodiscard]] bool CreateStorageTextureDescriptorSetLayout(State& state, core::uint32 capacity, VkDescriptorSetLayout& output);
	[[nodiscard]] bool CreateUniformBufferDescriptorSetLayout(State& state, core::uint32 capacity, VkDescriptorSetLayout& output);

	[[nodiscard]] core::Result<void*> CreateDescriptorTable(State* opaque, const DescriptorTableDesc& desc);
	[[nodiscard]] core::Result<void> CopyDescriptorTableEntries(
		State* opaque,
		const void* sourceNative,
		core::uint32 sourceIndex,
		void* destinationNative,
		core::uint32 destinationIndex,
		core::uint32 count);
	[[nodiscard]] core::Result<void> WriteDescriptorTable(
		State* opaque,
		void* native,
		core::uint32 index,
		void* textureNative,
		void* viewNative,
		void* samplerNative,
		void* bufferNative,
		BufferRange range);

	[[nodiscard]] bool LoadFunctions(State& state);
	[[nodiscard]] bool LoadInstanceFunctions(State& state);

	[[nodiscard]] EFormatSupport QueryFormatSupport(const State* opaque, EFormat format) noexcept;

	[[nodiscard]] bool LoadDeviceFunctions(State& state);

	void FreeBuffer(State& state, Buffer* buffer) noexcept;

	[[nodiscard]] core::uint32 SelectMemoryType(const State& state, core::uint32 memoryTypeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, bool bAllowFallback) noexcept;
	[[nodiscard]] core::uint32 MemoryBudgetIndex(const State& state, core::uint32 memoryType) noexcept;

	[[nodiscard]] core::Result<Buffer*> NewBuffer(State& state, core::uint64 size, bool bHostVisible, VkBufferUsageFlags additionalUsage = 0);

	void FreeTexture(State& state, Texture* texture) noexcept;

	[[nodiscard]] core::Result<VkRenderPass> CreateAttachmentRenderPass(
		State& state,
		core::Span<const EFormat> colors,
		EFormat depthFormat,
		core::uint32 sampleCount,
		bool bLoadDepth = false, bool bShadingRate = false, bool bReadOnlyDepth = false, bool bReadOnlyStencil = false);

	void FreePipeline(State& state, ComputePipeline* pipeline) noexcept;

	void FreeGraphicsPipeline(State& state, GraphicsPipeline* pipeline) noexcept;

	void FreeFramebuffer(State& state, Framebuffer* framebuffer) noexcept;

	[[nodiscard]] core::Result<core::uint32> QueryMemoryBudget(const State* opaque, core::Span<MemoryBudget> budgets);

	[[nodiscard]] VkImageCreateInfo MakeImageInfo(const State& state, const TextureDesc& desc, VkFormat format) noexcept;
	[[nodiscard]] core::Result<MemoryRequirements> GetTextureMemoryRequirements(const State* opaque, const TextureDesc& desc);

	[[nodiscard]] VkBufferUsageFlags BufferUsageFlags(const BufferDesc& desc) noexcept;
	[[nodiscard]] core::Result<MemoryRequirements> GetBufferMemoryRequirements(const State* opaque, const BufferDesc& desc);

	[[nodiscard]] core::Result<void*> CreateHeap(State* opaque, const HeapDesc& desc);
	[[nodiscard]] core::uint64 GetHeapCommittedBytes(const void* native) noexcept;
	void DestroyHeapNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyHeap(State* opaque, void* native);

	[[nodiscard]] core::Result<void*> CreatePlacedBuffer(State* opaque, const BufferDesc& desc, void* nativeHeap, core::uint64 offset);

	struct NativeMicromap final
	{
		VkMicromapEXT Handle = VK_NULL_HANDLE;
	};

	[[nodiscard]] core::Result<AccelStructBuildSizes> GetAccelStructBuildSizes(
		const State* opaque,
		const AccelStructBuildDesc& desc,
		core::Span<const NativeAccelTriangleGeometry> triangles,
		core::Span<const NativeAccelAabbGeometry> aabbs);

	struct NativeAccelStruct final
	{
		core::SharedPtr<PreparedAccelQueryOwner> PreparedQuery;
		core::SharedPtr<PreparedAccelQueryOwner> ProvisionalQuery;

		VkAccelerationStructureKHR Handle = VK_NULL_HANDLE;
		VkDeviceAddress ClusterAddress = 0;

		VkQueryPool CompactionQuery = VK_NULL_HANDLE;
		core::uint64 CompactionSerial = 0;

		core::uint64 LastUse = 0;
		EQueue LastQueue = EQueue::Graphics;
	};

	[[nodiscard]] core::Result<void> RecordSetAccelStructState(State* opaque, void* native, void*, EAccelStructState requested);

	[[nodiscard]] core::Result<Pending> Begin(State&, EQueue);

	[[nodiscard]] core::Result<core::uint64> Execute(State&, Pending&&, VkSemaphore, VkSemaphore, VkPipelineStageFlags, bool, const NativePreparedSubmissionDesc*, core::uint32*);
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
	[[nodiscard]] core::uint64 GetAccelStructDeviceAddress(const State* opaque, const void* native) noexcept;
	void PropagateAccelStructLastUse(const void*, const void* parent, void* child) noexcept;
	void PropagateAccelStructLastUseToBuffer(const void*, const void* source, void* nativeBuffer) noexcept;
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

	[[nodiscard]] core::Result<OpacityMicromapBuildSizes> GetOpacityMicromapBuildSizes(const State* opaque, core::Span<const OpacityMicromapUsageCount> counts, EOpacityMicromapBuildFlags flags);
	[[nodiscard]] core::Result<void*> CreateOpacityMicromap(State* opaque, const OpacityMicromapDesc& desc, void* nativeStorage);
	void DestroyOpacityMicromapNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyOpacityMicromap(State* opaque, void* native, void* nativeStorage);
	[[nodiscard]] core::Result<void> RecordBuildOpacityMicromap(
		State* opaque,
		void* nativeCommand,
		void* nativeMicromap,
		const OpacityMicromapBuildDesc& desc,
		void* nativeInput,
		core::uint64 inputOffset,
		void* nativeTriangles,
		core::uint64 triangleOffset,
		void* nativeScratch,
		core::uint64 scratchOffset);

	[[nodiscard]] VkAccelerationStructureKHR GetDescriptorAccelerationStructure(const void* native) noexcept;

	[[nodiscard]] bool PrepareClusterInput(
		const ClusterOperationParams& params,
		VkClusterAccelerationStructureInputInfoNV& input,
		VkClusterAccelerationStructureClustersBottomLevelInputNV& bottom,
		VkClusterAccelerationStructureTriangleClusterInputNV& triangle,
		VkClusterAccelerationStructureMoveObjectsInputNV& move) noexcept;
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

	[[nodiscard]] core::Result<core::uint32> QueryTextureSampleCounts(const State* opaque, const TextureDesc& desc);
	[[nodiscard]] bool SupportsColorResolve(const State* opaque, EFormat format) noexcept;

	[[nodiscard]] core::Result<void*> CreateTextureInternal(
		State* opaque,
		const TextureDesc& desc,
		Heap* placedHeap,
		core::uint64 offset,
		VkImage imported = VK_NULL_HANDLE, ENativeTextureContents contents = ENativeTextureContents::Discard);
	[[nodiscard]] core::Result<void*> CreateTexture(State* opaque, const TextureDesc& desc);

	[[nodiscard]] core::Result<void*> ImportTexture(State* opaque, const TextureDesc& desc, void* native, ENativeTextureContents contents);
	[[nodiscard]] void* GetNativeTexture(const void* native) noexcept;

	[[nodiscard]] core::Result<void*> CreatePlacedTexture(State* opaque, const TextureDesc& desc, void* heap, core::uint64 offset);

	[[nodiscard]] core::uint64 GetBufferLastUse(const void* native) noexcept;
	void PropagateBufferLastUse(const void*, const void* source, void* target) noexcept;
	[[nodiscard]] core::uint64 GetBufferDeviceAddress(const State* opaque, const void* native) noexcept;

	[[nodiscard]] core::uint64 GetTextureLastUse(const void* native) noexcept;

	[[nodiscard]] core::Result<void*> CreateBuffer(State* opaque, const BufferDesc& desc);

	[[nodiscard]] core::Result<void*> ImportBuffer(State* opaque, const BufferDesc& desc, void* native);
	[[nodiscard]] void* GetNativeBuffer(const void* native) noexcept;

	void Collect(State* opaque) noexcept;

	void DestroyBufferNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyBuffer(State* opaque, void* native);

	void DestroyTextureNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyTexture(State* opaque, void* native);

	[[nodiscard]] core::Result<Pending> Begin(State& state, EQueue queue);

	void Barrier(State& state, Pending& pending) noexcept;
	void BufferTransferBarrier(State& state, Pending& pending, VkBuffer buffer) noexcept;
	void BufferTransferAccessBarrier(State& state, Pending& pending, VkBuffer buffer, VkAccessFlags access) noexcept;
	void BufferHostReadBarrier(State& state, Pending& pending, VkBuffer buffer) noexcept;
	void EmitTextureBarrier(
		State& state,
		Pending& pending,
		const Texture& texture,
		VkImageLayout before,
		VkImageLayout next,
		core::uint32 baseMip,
		core::uint32 mipCount,
		core::uint32 baseLayer,
		core::uint32 layerCount) noexcept;

	[[nodiscard]] Pending::TextureLayoutState* TrackTextureLayout(Pending& pending, Texture& texture) noexcept;
	[[nodiscard]] bool TextureHasLayout(const Pending& pending, const Texture& texture, VkImageLayout layout, const TextureViewDesc* range = nullptr) noexcept;
	[[nodiscard]] VkImageLayout SampledTextureLayout(const Pending& pending, const Texture& texture, const TextureViewDesc* range = nullptr) noexcept;
	[[nodiscard]] VkImageLayout TextureLayout(const Texture& texture, VkImageLayout layout) noexcept;
	[[nodiscard]] VkImageLayout DepthAttachmentLayout(EFormat format, bool bDepthReadOnly, bool bStencilReadOnly) noexcept;
	[[nodiscard]] bool SupportsAttachmentSamplingLayout(const State& state, VkImageLayout layout) noexcept;

	[[nodiscard]] VkImageLayout MapTextureState(ETextureState requested) noexcept;
	[[nodiscard]] VkImageLayout PreparedTextureLayout(const Texture& texture, core::Span<const NativePreparedTextureAccess> intents, const PreparedTextureAccessDesc& requested) noexcept;

	void RequireTextureLayout(State& state, Pending& pending, Texture& texture, VkImageLayout next, core::uint32 subresource = UINT32_MAX) noexcept;
	void RequireTextureLayoutRange(State& state, Pending& pending, Texture& texture, VkImageLayout next, const TextureViewDesc& range) noexcept;

	[[nodiscard]] bool DescriptorTableTexturesHaveLayout(const Pending& pending, const DescriptorTable& table, VkImageLayout layout) noexcept;
	void RequireDescriptorTableTextureLayouts(State& state, Pending& pending, const DescriptorTable& table, VkImageLayout layout) noexcept;

	void CommitTextureLayouts(Pending& pending) noexcept;

	[[nodiscard]] core::Result<void> PrepareSwapChainTarget(Pending& pending, Texture& texture);

	[[nodiscard]] core::Result<core::uint64> Execute(
		State& state,
		Pending&& pending,
		VkSemaphore acquired = VK_NULL_HANDLE,
		VkSemaphore presentReady = VK_NULL_HANDLE,
		VkPipelineStageFlags acquireStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
		bool bIndependent = false,
		const NativePreparedSubmissionDesc* preparation = nullptr,
		core::uint32* queueWaitCount = nullptr);

	[[nodiscard]] core::Result<core::uint64> UploadBuffer(State* opaque, void* native, core::uint64 offset, core::Span<const core::Byte> data);

	[[nodiscard]] core::Result<bool> IsComplete(const State* opaque, core::uint64 serial);
	[[nodiscard]] core::Result<bool> IsQueueComplete(const State* opaque, EQueue queue, core::uint64 serial);
	[[nodiscard]] core::Result<void> Wait(State* opaque, EQueue queue, core::uint64 serial, core::uint32 timeout);
	[[nodiscard]] core::Result<void> QueueWaitForSubmission(State* opaque, EQueue waitingQueue, EQueue executionQueue, core::uint64 serial);

	[[nodiscard]] core::Result<TextureTiling> GetTextureTiling(const void*, const void* native, core::Span<TextureSubresourceTiling> subresources);
	[[nodiscard]] core::Result<core::uint64> UpdateTextureTileMappings(State* opaque, void* native, const TextureDesc& desc, core::Span<const NativeTextureTileMapping> mappings, EQueue queueKind);

	[[nodiscard]] core::Result<core::Byte*> MapBuffer(State* opaque, void* native, core::uint64 offset, core::uint64, core::uint32 timeout);
	[[nodiscard]] core::Result<void> UnmapBuffer(State* opaque, void* native);
	[[nodiscard]] core::Result<void> ReadBuffer(State* opaque, void* native, core::uint64 offset, core::Span<core::Byte> output, core::uint32 timeout);

	[[nodiscard]] core::Result<core::uint64> UploadTexture(State* opaque, void* native, const TextureDesc& desc, TextureSubresource subresource, core::Span<const core::Byte> data);
	[[nodiscard]] core::Result<void> ReadTexture(State* opaque, void* native, const TextureDesc& desc, TextureSubresource subresource, core::Span<core::Byte> output, core::uint32 timeout);

	[[nodiscard]] core::Result<void*> CreateCommandList(State* opaque, EQueue queue);
	void* GetNativeCommandList(const void* native) noexcept;

	void InvalidateCommandState(void* native) noexcept;
	void SetAutomaticBarriers(void* native, bool bEnabled) noexcept;
	[[nodiscard]] bool ShouldRecordBufferUavBarrier(const Pending& pending, VkBuffer resource) noexcept;
	[[nodiscard]] bool ShouldRecordTextureUavBarrier(const Pending& pending, VkImage resource) noexcept;
	void MarkBufferUavBarrierPlaced(Pending& pending, VkBuffer resource) noexcept;
	void MarkTextureUavBarrierPlaced(Pending& pending, VkImage resource) noexcept;
	[[nodiscard]] core::Result<void> SetBufferUavBarriers(void* native, void* bufferNative, bool bEnabled);
	[[nodiscard]] core::Result<void> SetTextureUavBarriers(void* native, void* textureNative, bool bEnabled);

	[[nodiscard]] core::Result<void> RecordBeginMarker(State* opaque, void* native, const char* name);
	[[nodiscard]] core::Result<void> RecordEndMarker(State* opaque, void* native);

	struct GpuTimer final
	{
		VkQueryPool Queries = VK_NULL_HANDLE;
		core::uint32 ValidBits = 0;
	};

	[[nodiscard]] core::Result<void*> CreateGpuTimer(State* opaque);
	void DestroyGpuTimer(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> RecordGpuTimerBegin(State* opaque, void* nativeList, void* nativeTimer);
	[[nodiscard]] core::Result<void> RecordGpuTimerEnd(State* opaque, void* nativeList, void* nativeTimer);
	[[nodiscard]] core::Result<double> ReadGpuTimer(State* opaque, void* native);

	[[nodiscard]] core::Result<void> RecordBufferCopy(State* opaque, void* native, const NativeCopy& copy);

	[[nodiscard]] VkComponentTypeKHR ConvertCoopVecType(ECoopVecDataType type) noexcept;
	[[nodiscard]] ECoopVecDataType FromCoopVecType(VkComponentTypeKHR type) noexcept;
	[[nodiscard]] VkCooperativeVectorMatrixLayoutNV ConvertCoopVecLayout(ECoopVecMatrixLayout layout) noexcept;
	[[nodiscard]] core::uint64 CoopVecElementBytes(ECoopVecDataType type) noexcept;
	[[nodiscard]] core::Result<CoopVecFeatures> QueryCoopVecFeatures(const State* opaque, core::Span<CoopVecFormatCombo> formats);
	[[nodiscard]] core::Result<core::uint64> GetCoopVecMatrixSize(const State* opaque, ECoopVecDataType type, ECoopVecMatrixLayout layout, core::uint32 rows, core::uint32 columns);
	[[nodiscard]] core::Result<void> RecordCoopVecMatrices(State* opaque, void* native, core::Span<const NativeCoopVecMatrixConversion> conversions, bool bAutomatic);

	[[nodiscard]] core::Result<void> RecordBufferWrite(State* opaque, void* native, void* destination, core::uint64 offset, core::Span<const core::Byte> data);
	[[nodiscard]] core::Result<void> RecordBufferClear(State* opaque, void* native, void* target, core::uint64 byteCount, core::uint32 value);

	[[nodiscard]] core::Result<void> RecordTextureCopy(State* opaque, void* native, const NativeTextureCopy& copy);
	[[nodiscard]] core::Result<void> RecordTextureWrite(
		State* opaque,
		void* native,
		void* destinationNative,
		const TextureDesc& desc,
		TextureSubresource subresource,
		core::Span<const core::Byte> data);

	[[nodiscard]] core::Result<void> PrepareTextureBufferCopy(void*, NativeTextureBufferCopy& copy);
	[[nodiscard]] core::Result<void> RecordTextureBuffer(State* opaque, void* native, const NativeTextureBufferCopy& copy, bool bUpload);
	[[nodiscard]] core::Result<void> RecordBufferToTexture(State* opaque, void* native, const NativeTextureBufferCopy& copy);
	[[nodiscard]] core::Result<void> RecordTextureToBuffer(State* opaque, void* native, const NativeTextureBufferCopy& copy);

	[[nodiscard]] VkClearColorValue MakeClearColor(const ClearColor& color) noexcept;
	[[nodiscard]] core::Result<void> RecordColorClearValue(State& state, Pending& pending, Texture& texture, TextureSubresource subresource, const VkClearColorValue& value);
	[[nodiscard]] core::Result<void> RecordTextureClear(State* opaque, void* native, void* target, const TextureDesc&, TextureSubresource subresource, const ClearColor& color);
	[[nodiscard]] core::Result<void> RecordTextureUIntClear(State* opaque, void* native, void* target, const TextureDesc&, TextureSubresource subresource, core::uint32 clearValue);

	[[nodiscard]] core::Result<void> RecordDepthClear(State* opaque, void* native, void* target, const TextureDesc&, TextureSubresource subresource, DepthStencilClear clear);

	[[nodiscard]] core::Result<void*> CreateComputePipeline(State* opaque, const NativeComputePipelineDesc& desc, core::Span<const NativeBindingLayoutDesc> layouts);
	void DestroyComputePipelineNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyComputePipeline(State* opaque, void* native);

	[[nodiscard]] core::Result<void> RecordComputeStorageBufferBarriers(State& state, Pending& pending, core::Span<const NativeBindingSet> sets, const DescriptorTable* table);
	[[nodiscard]] core::Result<void> RecordComputeStorageTextureBarriers(State& state, Pending& pending, core::Span<const NativeBindingSet> sets, const DescriptorTable* table);
	[[nodiscard]] core::Result<void> RecordComputeInputBarriers(
		State& state,
		Pending& pending,
		core::Span<const NativeBindingSet> sets,
		const DescriptorTable* sampledTable,
		const DescriptorTable* storageBufferTable,
		const DescriptorTable* storageTextureTable,
		const DescriptorTable* uniformBufferTable,
		const DescriptorTable* readOnlyBufferTable,
		Buffer* indirect,
		bool bAccelStruct);

	[[nodiscard]] core::Result<void> RecordRangeComputeDispatch(
		State& state,
		Pending& pending,
		const ComputePipeline& pipeline,
		core::Span<const NativeBindingSet> sets,
		core::uint32 groupsX,
		core::uint32 groupsY,
		core::uint32 groupsZ,
		void* indirectNative,
		core::uint64 indirectOffset,
		core::Span<const core::Byte> pushConstants);
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

	[[nodiscard]] VkResolveModeFlagBits ResolveMode(ETextureResolveMode mode) noexcept;
	[[nodiscard]] bool SupportsTextureResolve(const State* opaque, const TextureDesc& desc, ETextureResolveMode mode, ETextureResolveMode stencilMode) noexcept;
	[[nodiscard]] core::Result<void> PrepareTextureResolve(State* opaque, void* native, NativeTextureResolve& resolve);
	[[nodiscard]] core::Result<void> RecordTextureResolve(State* opaque, void* native, const NativeTextureResolve& resolve);

	[[nodiscard]] core::Result<VkRenderPass> CreateAttachmentRenderPass(
		State& state,
		core::Span<const EFormat> colors,
		EFormat depthFormat,
		core::uint32 sampleCount,
		bool bLoadDepth,
		bool bShadingRate,
		bool bReadOnlyDepth,
		bool bReadOnlyStencil);

	[[nodiscard]] core::Result<VkRenderPass> GetDirectRenderPass(
		State& state,
		EFormat colorFormat,
		EFormat depthFormat,
		core::uint32 sampleCount,
		bool bLoadDepth,
		bool bReadOnlyDepth = false, bool bReadOnlyStencil = false);

	void FlushGraphicsStorageWrites(State& state, Pending& pending) noexcept;
	void SynchronizeGraphicsStorage(State& state, Pending& pending) noexcept;

	[[nodiscard]] core::Result<void*> CreateFramebuffer(State* opaque, const NativeFramebufferDesc& desc);
	void DestroyFramebufferNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyFramebuffer(State* opaque, void* native);

	[[nodiscard]] core::Result<void> RecordBeginFramebuffer(State* opaque, void* native, void* target, DepthAttachmentOptions depthOptions);
	[[nodiscard]] core::Result<void> RecordEndFramebuffer(State* opaque, void* native, void* target);

	inline constexpr VkPrimitiveTopology VULKAN_TOPOLOGIES[]{ VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
															 VK_PRIMITIVE_TOPOLOGY_LINE_LIST,	  VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
															 VK_PRIMITIVE_TOPOLOGY_POINT_LIST,	  VK_PRIMITIVE_TOPOLOGY_PATCH_LIST };

	inline constexpr VkCompareOp VULKAN_COMPARE[]{ VK_COMPARE_OP_NEVER,
												  VK_COMPARE_OP_LESS,
												  VK_COMPARE_OP_EQUAL,
												  VK_COMPARE_OP_LESS_OR_EQUAL,
												  VK_COMPARE_OP_GREATER,
												  VK_COMPARE_OP_NOT_EQUAL,
												  VK_COMPARE_OP_GREATER_OR_EQUAL,
												  VK_COMPARE_OP_ALWAYS };

	inline constexpr VkStencilOp VULKAN_STENCIL_OPERATIONS[]{ VK_STENCIL_OP_KEEP,
															 VK_STENCIL_OP_ZERO,
															 VK_STENCIL_OP_REPLACE,
															 VK_STENCIL_OP_INCREMENT_AND_CLAMP,
															 VK_STENCIL_OP_DECREMENT_AND_CLAMP,
															 VK_STENCIL_OP_INVERT,
															 VK_STENCIL_OP_INCREMENT_AND_WRAP,
															 VK_STENCIL_OP_DECREMENT_AND_WRAP };

	inline constexpr VkBlendFactor VULKAN_BLEND_FACTORS[]{ VK_BLEND_FACTOR_ZERO,
														  VK_BLEND_FACTOR_ONE,
														  VK_BLEND_FACTOR_SRC_COLOR,
														  VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
														  VK_BLEND_FACTOR_DST_COLOR,
														  VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
														  VK_BLEND_FACTOR_SRC_ALPHA,
														  VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
														  VK_BLEND_FACTOR_DST_ALPHA,
														  VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
														  VK_BLEND_FACTOR_CONSTANT_COLOR,
														  VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
														  VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
														  VK_BLEND_FACTOR_SRC1_COLOR,
														  VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
														  VK_BLEND_FACTOR_SRC1_ALPHA,
														  VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA };

	inline constexpr VkBlendOp VULKAN_BLEND_OPERATIONS[]{ VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT,
														 VK_BLEND_OP_MIN, VK_BLEND_OP_MAX };

	[[nodiscard]] core::Result<void*> CreateGraphicsPipeline(State* opaque, const NativeGraphicsPipelineDesc& desc, core::Span<const NativeGraphicsBindingLayoutDesc> layouts);
	void DestroyGraphicsPipelineNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyGraphicsPipeline(State* opaque, void* native);

	[[nodiscard]] core::Result<void> RecordSetViewport(State* opaque, void* native, core::Span<const ViewportDesc> viewports);
	[[nodiscard]] core::Result<void> RecordSetScissor(State* opaque, void* native, core::Span<const ScissorDesc> scissors);

	[[nodiscard]] core::Result<void> BindGraphicsResourcesMultiple(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		core::Span<const NativeSampledTexture> textures,
		core::Span<const NativeStorageTexture> storageTextures,
		core::Span<const NativeGraphicsBuffer> buffers,
		void* accelNative);
	[[nodiscard]] core::Result<void> BindGraphicsResources(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		core::Span<const NativeSampledTexture> textures,
		core::Span<const NativeStorageTexture> storageTextures,
		core::Span<const NativeGraphicsBuffer> buffers,
		void* accelNative);
	[[nodiscard]] core::Result<VkDescriptorSet> CaptureGraphicsSampledTable(State& state, Pending& pending, const DescriptorTable& table);
	[[nodiscard]] core::Result<void> BindGraphicsDescriptorTable(
		State& state,
		Pending& pending,
		const GraphicsPipeline& pipeline,
		void* native,
		void* samplerNative,
		void* storageNative,
		void* storageTextureNative,
		void* uniformNative,
		void* readOnlyBufferNative);
	[[nodiscard]] core::Result<void> TrackGraphicsStorageWrites(
		Pending& pending,
		core::Span<const NativeStorageTexture> storageTextures,
		core::Span<const NativeGraphicsBuffer> buffers,
		const DescriptorTable* storageBufferTable,
		const DescriptorTable* storageTextureTable);

	[[nodiscard]] core::Result<void> RecordBindExtraVertexBuffers(State* opaque, void* native, core::Span<const NativeVertexBuffer> buffers);

	void ApplyBlendConstant(State& state, Pending& pending, const GraphicsPipeline& pipeline, const BlendColor& color) noexcept;
	void ApplyShadingRate(State& state, Pending& pending, EShadingRate rate, EShadingRateCombiner primitive, EShadingRateCombiner image) noexcept;

	[[nodiscard]] core::Result<void> RecordDraw(
		State* opaque,
		void* native,
		void* pipelineNative,
		void* vertexNative,
		core::uint64 vertexOffset,
		core::uint64,
		core::uint32,
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
		core::uint64,
		core::uint64 indexOffset,
		core::uint32,
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
		core::uint64,
		core::uint32,
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
		core::uint64,
		core::uint64 indexOffset,
		core::uint32,
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

	[[nodiscard]] VkImageLayout MapTextureState(ETextureState requested) noexcept;

	[[nodiscard]] const NativeTableRangeBinding* FindNativeTableRange(const NativeFixedBindingSet& set, core::uint32 rangeIndex) noexcept;
	[[nodiscard]] VkImageLayout FixedDescriptorImageLayout(
		Pending& pending,
		Texture& texture,
		TextureView* view,
		EDescriptorType type,
		core::Span<const NativePreparedTextureAccess> intents,
		bool bPrepared) noexcept;
	[[nodiscard]] VkShaderStageFlags FixedDescriptorStages(const BindingRangeDesc& range, bool bGraphics, bool bRay) noexcept;

	void DestroyDescriptorSetRealization(State* opaque, void* native) noexcept;
	[[nodiscard]] bool DescriptorSetMatches(
		const DescriptorSetRealization& realization,
		const NativeFixedLayoutState& fixed,
		core::uint32 layout,
		const NativeFixedBindingSet& set,
		Pending& pending,
		bool bGraphics,
		bool bRay,
		core::Span<const NativePreparedTextureAccess> intents,
		bool bPrepared) noexcept;
	[[nodiscard]] core::Result<void> InitializeDescriptorSetStorage(
		State& state,
		DescriptorSetRealization& realization,
		const NativeFixedLayoutState& fixed,
		core::uint32 layout,
		const NativeFixedBindingSet& set,
		bool bGraphics,
		bool bRay);
	[[nodiscard]] core::Result<void> WriteDescriptorSetValues(
		State& state,
		Pending& pending,
		DescriptorSetRealization& realization,
		const NativeFixedBindingSet& set,
		core::Span<const NativePreparedTextureAccess> intents,
		bool bPrepared);
	[[nodiscard]] core::Result<core::SharedPtr<PreparedDescriptorSet>> CreateDescriptorSetRealization(
		State& state,
		Pending& pending,
		const NativeFixedLayoutState& fixed,
		core::uint32 layout,
		const NativeFixedBindingSet& set,
		bool bGraphics,
		bool bRay,
		core::Span<const NativePreparedTextureAccess> intents,
		bool bPrepared);

	[[nodiscard]] core::Result<void> WriteCachedFixedBindingSets(
		State& state,
		Pending& pending,
		const NativeFixedLayoutState& fixed,
		VkPipelineLayout pipelineLayout,
		bool bGraphics,
		bool bRay,
		core::Span<const NativeFixedBindingSet> sets,
		Pending::PreparedBinding* prepared,
		core::Span<const NativePreparedTextureAccess> intents);
	[[nodiscard]] core::Result<void> WriteFixedBindingSets(
		State* opaque,
		void* native,
		void* pipelineNative,
		bool bGraphics,
		core::Span<const NativeFixedBindingSet> sets,
		Pending::PreparedBinding* prepared = nullptr,
		core::Span<const NativePreparedTextureAccess> intents = {},
		RayTracingPipeline* rayPipeline = nullptr);
	[[nodiscard]] core::Result<void> RecordFixedBindingSets(State* opaque, void* native, void* pipelineNative, bool bGraphics, core::Span<const NativeFixedBindingSet> sets);

	[[nodiscard]] core::Result<void*> CreateRayTracingPipeline(State* opaque, const NativeRayPipelineDesc& desc);
	void DestroyRayTracingPipelineNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroyRayTracingPipeline(State* opaque, void* native);

	[[nodiscard]] core::Result<void> RecordRayResourceBarriers(State& state, Pending& pending, core::Span<const NativeBindingSet> sets, bool bBefore);
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

	void DiscardCommandList(State* opaque, void* native) noexcept;

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

	[[nodiscard]] core::Result<void> WaitIdleBounded(State* opaque, core::uint32 timeout);
	[[nodiscard]] core::Result<void> WaitIdle(State* opaque);

	void Destroy(State* opaque) noexcept;

	void FreeSwapChainTargets(State& state, SwapChain& swapChain) noexcept;
	void FreeSwapChain(State& state, SwapChain* swapChain) noexcept;
	[[nodiscard]] core::Result<void> ResetSwapChainSemaphores(State& state, SwapChain& swapChain);

	[[nodiscard]] core::Result<void> CreateNativeSwapChain(State& state, SwapChain& swapChain, core::uint32 width, core::uint32 height);
	[[nodiscard]] core::Result<void*> CreateSwapChain(State* opaque, const SwapChainDesc& desc);
	[[nodiscard]] core::Result<void> ResizeSwapChainWithPolicy(State* opaque, void* native, core::uint32 width, core::uint32 height, bool bPoll);
	[[nodiscard]] core::Result<void> ResizeSwapChain(State* opaque, void* native, core::uint32 width, core::uint32 height);
	[[nodiscard]] core::Result<void> TryResizeSwapChain(State* opaque, void* native, core::uint32 width, core::uint32 height);

	[[nodiscard]] core::Result<void> PrepareAcquireFence(State& state, SwapChain& swapChain, core::uint32 index, core::uint32 timeoutMilliseconds);
	[[nodiscard]] core::Result<void> PreparePresentFence(State& state, SwapChain& swapChain, VkPresentInfoKHR& present, VkSwapchainPresentFenceInfoEXT& info);
	void ObservePresentResult(State& state, SwapChain& swapChain, core::uint32 imageIndex, VkResult result) noexcept;

	// The acquire operation for this image is the native presentation-engine proof.
	// Maintenance fences additionally make host fence reset/destruction observable.
	[[nodiscard]] core::Result<void> RetireImagePresentation(State& state, SwapChain& swapChain, core::uint32 index, core::uint32 timeoutMilliseconds);

	[[nodiscard]] core::Result<void> DiscardAcquiredSwapChainImage(State* opaque, void* native);
	[[nodiscard]] core::Result<void*> AcquireSwapChainImage(State* opaque, void* native, core::uint32 timeoutMilliseconds);
	[[nodiscard]] core::Result<core::uint64> PresentSwapChainImage(State* opaque, void* native);

	[[nodiscard]] core::Result<core::uint64> Present(State* opaque, void* native, void* sourceNative);

	void DestroySwapChainNow(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> DestroySwapChain(State* opaque, void* native);
	[[nodiscard]] core::Result<void> TryDestroySwapChain(State* opaque, void* native);

	[[nodiscard]] core::Result<void> RecordShaderMemoryBarrier(State* opaque, void* native);
	[[nodiscard]] core::Result<void> RecordShaderBufferBarrier(State* opaque, void* native, void* bufferNative);
	[[nodiscard]] core::Result<void> RecordShaderTextureBarrier(State* opaque, void* native, void* textureNative);
	[[nodiscard]] core::Result<void> RecordPrepareTextureForShaderAccess(State* opaque, void* native, void* textureNative, const TextureViewDesc* range, ETextureState);

	[[nodiscard]] VkImageLayout MapTextureState(ETextureState requested) noexcept;

	[[nodiscard]] VkImageLayout ExplicitTextureLayout(const Pending& pending, const Texture& texture, const TextureSubresource* subresource, ETextureState requested) noexcept;

	[[nodiscard]] core::Result<void> RecordBeginTrackingTextureState(void*, void* native, void* textureNative, const TextureSubresource* subresource, ETextureState requested);
	[[nodiscard]] core::Result<void> RecordSetTextureState(State* opaque, void* native, void* textureNative, const TextureSubresource* subresource, ETextureState requested);
	[[nodiscard]] ETrackedResourceState GetTextureSubresourceState(const void*, const void* native, const void* textureNative, TextureSubresource subresource) noexcept;

	[[nodiscard]] core::Result<void> RecordBeginTrackingBufferState(void*, void*, void*, EBufferState);
	[[nodiscard]] core::Result<void> RecordSetBufferState(State* opaque, void* native, void* bufferNative, EBufferState requested);

	[[nodiscard]] CommandListStatistics GetCommandListStatistics(const void* native) noexcept;
	[[nodiscard]] bool HasMissingManualState(const void* native) noexcept;

	[[nodiscard]] core::Result<void> PrepareRecordingGroup(State* opaque, core::Span<const NativePreparedList> lists);
	void ResetPreparedCommandList(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<void> CapturePreparedStorage(Pending::PreparedBinding& binding, const NativeFixedLayoutState& fixed, core::Span<const NativeFixedBindingSet> sets);

	[[nodiscard]] core::Result<core::uint32> PrepareBinding(
		State* opaque,
		void* native,
		void* pipeline,
		bool bGraphics,
		core::Span<const NativeFixedBindingSet> sets,
		core::Span<const NativePreparedTextureAccess> intents,
		const NativePreparedVertexInput& input,
		core::Span<const NativeDescriptorHeapBinding> heaps,
		core::SharedPtr<NativeBindingProgram>* program);

	[[nodiscard]] core::Result<void> RecordPreparedPhase(State* opaque, void* native, const NativePreparedPhase& phase);

	void BindPrepared(State& state, Pending& pending, void* native, bool bGraphics, core::uint32 index) noexcept;

	void BindPreparedCompute(State* opaque, void* native, void* pipelineNative, core::uint32 binding, core::Span<const core::Byte> pushConstants);
	[[nodiscard]] core::Result<void> RecordPreparedDispatch(State* opaque, void* native, void* pipelineNative, core::uint32 binding, const ComputeDispatch& dispatch);

	void BindPreparedGraphics(State* opaque, void* native, void* pipelineNative, core::uint32 binding, core::Span<const core::Byte> pushConstants);
	[[nodiscard]] core::Result<void> RecordPreparedMeshDispatch(State* opaque, void* native, void* pipelineNative, core::uint32 binding, const MeshDispatch& dispatch);
	[[nodiscard]] core::Result<void> RecordPreparedDraw(State* opaque, void* native, void* pipelineNative, core::uint32 binding, const DrawArguments& draw);
	[[nodiscard]] core::Result<void> RecordDrawIndexedArguments(State* opaque, void* native, void* pipelineNative, core::uint32 binding, const DrawIndexedArguments& draw);

	[[nodiscard]] core::Result<core::uint32> PrepareIndirect(State* opaque, void*, const NativePreparedIndirect& indirect);
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

	[[nodiscard]] core::Result<void> FinishPreparedCommandList(State* opaque, void* native);

	[[nodiscard]] core::Result<void> QueueWaitForPreparedSubmission(State* opaque, EQueue waitingQueue, EQueue executionQueue, core::uint64 serial);

	[[nodiscard]] core::Result<core::uint32> DescribePreparedNativeBoundary(const void*, const void* native, const void* resourceNative, bool bTexture) noexcept;

	[[nodiscard]] VkDeviceAddress PreparedBufferAddress(State& state, const Buffer& buffer) noexcept;

	[[nodiscard]] core::Result<core::uint32> PrepareAccelBuild(State* opaque, void* native, const NativePreparedAccelBuild& source);
	[[nodiscard]] core::Result<void> RecordPreparedAccelBuild(State* opaque, void* native, core::uint32 index);

	[[nodiscard]] core::Result<core::uint32> PrepareAccelQuery(State* opaque, void* native, void* source, core::uint64 generation);
	[[nodiscard]] core::Result<void> RecordPreparedAccelQuery(State* opaque, void* native, core::uint32 index);

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
		core::Span<const NativePreparedTextureAccess> intents);
	[[nodiscard]] core::Result<void> RecordPreparedRayDispatch(State* opaque, void* native, void* pipelineNative, core::uint32 index, const RayDispatch& dispatch);
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
		EQueue Queue = EQueue::Graphics;

		core::uint32 ImageCount = 0;
		core::uint32 OpaqueCount = 0;

		VkSemaphore Ready = VK_NULL_HANDLE;
		VkFence Fence = VK_NULL_HANDLE;

		bool bCredit = false;
		core::usize Capacity = 0;
	};

	[[nodiscard]] core::Result<void> PrepareTextureTileMappings(
		State* opaque,
		void* native,
		const TextureDesc& desc,
		core::Span<const NativeTextureTileMapping> mappings,
		EQueue queueKind,
		core::MemoryBlock& storage);
	void DestroyPreparedTextureMappings(State* opaque, core::MemoryBlock& storage) noexcept;
	[[nodiscard]] NativePreparedSubmissionResult SubmitPreparedTextureMappings(State* opaque, core::MemoryBlock& storage, core::Span<const NativePreparedSubmissionWait> prerequisites);

	[[nodiscard]] DeviceLimits GetLimits(const State* opaque) noexcept;

	struct ExternalFenceRecord final
	{
		VkSemaphore Semaphore = VK_NULL_HANDLE;
		ExternalHandle ExportableHandle;
		EExternalHandleType Type = EExternalHandleType::None;
	};

	[[nodiscard]] constexpr EExternalHandleType VulkanExternalHandleType() noexcept
	{
#ifdef _WIN32
		return EExternalHandleType::OpaqueWin32;
#else
		return EExternalHandleType::OpaqueFd;
#endif
	}

	[[nodiscard]] constexpr VkExternalMemoryHandleTypeFlagBits VulkanExternalMemoryType() noexcept
	{
#ifdef _WIN32
		return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
		return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
	}

	[[nodiscard]] constexpr VkExternalSemaphoreHandleTypeFlagBits VulkanExternalSemaphoreType() noexcept
	{
#ifdef _WIN32
		return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
		return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
	}

	[[nodiscard]] bool ExternalMemoryFeatures(VkExternalMemoryFeatureFlags flags) noexcept;
	[[nodiscard]] bool ExternalBufferSupported(const State& state, VkBufferUsageFlags usage);
	[[nodiscard]] bool ExternalSemaphoreSupported(const State& state);

	[[nodiscard]] ExternalInteropCapabilities GetExternalInteropCapabilities(const State* opaque);

	[[nodiscard]] core::Result<ExternalHandle> DuplicateExternalHandle(ExternalHandleView source);

	[[nodiscard]] core::Result<VkDeviceMemory> AllocateExternalMemory(
		State& state,
		const VkMemoryRequirements& requirements,
		VkBuffer buffer,
		VkImage image,
		bool bDeviceAddress,
		const NativeExternalMemoryView* imported,
		core::uint32& memoryType);
	[[nodiscard]] core::Result<void*> CreateExternalBuffer(State* opaque, const BufferDesc& desc, const NativeExternalMemoryView* imported);
	[[nodiscard]] core::Result<void*> CreateExternalTexture(State* opaque, const TextureDesc& desc, const NativeExternalMemoryView* imported);
	[[nodiscard]] core::Result<NativeExternalMemory> ExportExternalMemory(State* opaque, void* native, bool bTexture);

	[[nodiscard]] core::Result<void*> CreateExternalFence(State* opaque, core::uint64 initial, const ExternalHandleView* imported);
	void DestroyExternalFence(State* opaque, void* native) noexcept;
	[[nodiscard]] core::Result<ExternalFence> ExportExternalFence(State* opaque, void* native);
	[[nodiscard]] core::Result<core::uint64> GetExternalFenceValue(State* opaque, void* native);

	[[nodiscard]] core::Result<void> RecordExternalTransfer(State* opaque, void* commands, void* resource, bool bTexture, void* fence, core::uint64 value, bool bAcquire);

	[[nodiscard]] core::Result<BackendData> FinishVulkanBackend(State* state, core::MemoryResource& resource);
	[[nodiscard]] core::Result<BackendData> CreateExternalVulkanBackend(
		core::MemoryResource& resource,
		NativeDeviceHandles external,
		NativeQueueHandle externalComputeQueue,
		NativeQueueHandle externalCopyQueue,
		ExternalVulkanFeatures enabledFeatures);

	VKAPI_ATTR VkBool32 VKAPI_CALL ValidationMessage(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT,
		const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
		void*) noexcept;

	[[nodiscard]] core::uint32 AdapterRank(VkPhysicalDeviceType type, EAdapterPreference preference) noexcept;

	core::Result<BackendData> CreateVulkanBackend(
		core::MemoryResource& resource,
		bool bEnableValidation,
		EAdapterPreference preference,
		NativeDeviceHandles externalDevice,
		NativeQueueHandle externalComputeQueue,
		NativeQueueHandle externalCopyQueue,
		ExternalVulkanFeatures enabledFeatures,
		IGpuCrashDiagnostics* diagnostics);
}

namespace rox::vulkan
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
	inline constexpr bool HAS_CREATE_SAMPLER_FEEDBACK_TEXTURE = false;
	core::Result<void*> CreateSamplerFeedbackTexture(void*, void*, const TextureDesc&, const SamplerFeedbackDesc&);
	inline constexpr bool HAS_IMPORT_SAMPLER_FEEDBACK_TEXTURE = false;
	core::Result<void*> ImportSamplerFeedbackTexture(void*, void*, const TextureDesc&, const SamplerFeedbackDesc&, void*);
	inline constexpr bool HAS_GET_TEXTURE_TILING = true;
	inline constexpr bool HAS_UPDATE_TEXTURE_TILE_MAPPINGS = true;
	inline constexpr bool HAS_PREPARE_TEXTURE_TILE_MAPPINGS = true;
	inline constexpr bool HAS_GET_DESCRIPTOR_TABLE_DEVICE_HEAP_INDEX = false;
	core::uint32 GetDescriptorTableDeviceHeapIndex(const void*, const void*) noexcept;
	inline constexpr bool HAS_GET_PRESENT_COPY_SERIAL = true;
	core::Result<void> RecordSetSamplerFeedbackState(void*, void*, void*, ESamplerFeedbackState);
	inline constexpr bool HAS_RECORD_COOP_VEC_MATRICES = true;
	inline constexpr bool HAS_PREPARE_TEXTURE_BUFFER_COPY = true;
	core::Result<void> RecordSamplerFeedbackClear(void*, void*, void*);
	core::Result<void> RecordSamplerFeedbackDecode(void*, void*, void*, void*);
	core::Result<void> RecordSamplerFeedbackDecodeTexture(void*, void*, void*, void*);
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
	inline constexpr bool HAS_GET_TEXTURE_BUFFER_FOOTPRINT = false;
	core::Result<TextureBufferFootprint> GetTextureBufferFootprint(void*, const TextureDesc&, TextureSubresource);
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
	inline constexpr bool HAS_RECORD_DEVICE_HEAP_USES = false;
	core::Result<void> RecordDeviceHeapUses(void*, void*, bool, core::Span<const NativeDescriptorHeapBinding>);
}

namespace rox::vulkan
{
}

namespace rox::vulkan
{
	void DestroyPreparedShaderTable(State* opaque, void* native) noexcept;

	void DestroyDescriptorSetRealization(State* opaque, void* native) noexcept;
}
