#pragma once
#include "RecordedResourceUses.h"
#include <RhiDeviceTypes.h>
#include <RhiDiagnostics.h>
#include <Array.hpp>
#include <MemoryBlock.h>
#include <NumericLimits.hpp>
#include <SharedPtr.hpp>
#if ROXELL_RHI_BACKEND == 1
namespace rox::d3d12
{
	struct State;
}
#elif ROXELL_RHI_BACKEND == 2
namespace rox::vulkan
{
	struct State;
}
#else
namespace rox::nullrhi
{
	struct State;
}
#endif

namespace rox::rhi
{
#if ROXELL_RHI_BACKEND == 1
	using SelectedBackendState = d3d12::State;
#elif ROXELL_RHI_BACKEND == 2
	using SelectedBackendState = vulkan::State;
#else
	using SelectedBackendState = nullrhi::State;
#endif

	struct BackendData final
	{
		SelectedBackendState* State = nullptr;

		GpuCapabilities Capabilities{};

		core::uint32 MaximumBindlessSampledTextures = 0;
		core::uint32 MaximumBindlessSamplers = 0;
		core::uint32 MaximumBindlessStorageBuffers = 0;
		core::uint32 MaximumBindlessStorageTextures = 0;
		core::uint32 MaximumBindlessUniformBuffers = 0;

		core::uint32 MaximumGraphicsBindlessStorageBuffers = 0;
		core::uint32 MaximumGraphicsBindlessStorageTextures = 0;

		NativeDeviceHandles NativeHandles;
		void* NativeComputeQueue = nullptr;
		void* NativeCopyQueue = nullptr;

		bool bAccelerationStructureBuildInput = false;
		bool bRayQuery = false;
		bool bRayTracingPipeline = false;
		bool bSamplerFeedback = false;

		core::uint32 NativeComputeQueueFamilyIndex = 0xFFFFFFFFu;
		core::uint32 NativeCopyQueueFamilyIndex = 0xFFFFFFFFu;

		bool bHeapDirectlyIndexed = false;
		bool bSliced3DStorageViews = false;

		bool bCooperativeVector = false;
		bool bCooperativeVectorTraining = false;

		bool bOpacityMicromap = false;
		bool bRayTracingClusters = false;

		IGpuCrashDiagnostics* Diagnostics = nullptr;
		bool bAftermathEnabled = false;
	};

	[[nodiscard]] core::uint64 NextDeviceOwnerId() noexcept;

	struct HeapSlot final
	{
		void* Native = nullptr;
		HeapDesc Descriptor;

		core::uint32 LiveBuffers = 0;
		core::uint32 LiveTextures = 0;

		core::uint32 SparseTileRefs = 0;
		core::uint32 PreparedSparseReferences = 0;

		core::uint32 Generation = 1;
		bool bUntrackedSparseUses = false;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct RetiredHeapRange final
	{
		HeapHandle Heap;
		core::uint64 Offset = 0;
		core::uint64 Size = 0;

		core::uint64 Serial = 0;
	};

	struct BufferSlot final
	{
		explicit BufferSlot(core::MemoryResource& resource) noexcept;

		void* Native = nullptr;

		core::Array<OpacityMicromapHandle> ClusterMicromapReferences;

		core::uint64 ByteCount = 0;
		EBufferUsage Usage = EBufferUsage::None;
		EBufferMemory Memory = EBufferMemory::Device;

		HeapHandle Heap;
		core::uint64 HeapOffset = 0;
		core::uint64 AllocationSize = 0;

		core::usize RecordedUses = 0;
		core::uint32 BindingSetRefs = 0;

		core::uint64 AliasEpoch = 0;
		ETrackedResourceState PermanentState = ETrackedResourceState::Unknown;

		core::uint32 Generation = 1;
		bool bLive = false;

		bool bMapped = false;

		bool bExternalOwned = false;
		bool bExternalShareable = false;

		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct AccelStructSlot final
	{
		explicit AccelStructSlot(core::MemoryResource& resource) noexcept;

		void* Native = nullptr;

		core::Array<BufferHandle> ClusterReferences;
		core::Array<AccelStructHandle> ClusterBottomReferences;
		// Weak generation-checked dependencies; ordinary BLAS lifetime remains caller-owned.
		core::Array<AccelStructHandle> BottomReferences;
		core::Array<OpacityMicromapHandle> OpacityMicromapReferences;
		core::uint32 ClusterParentRefs = 0;

		BufferHandle Storage;
		core::uint64 ByteOffset = 0;
		core::uint64 ByteCount = 0;
		core::uint64 CompactedBytes = 0;

		EAccelStructType Type = EAccelStructType::BottomLevel;
		AccelBuildSignature Signature;

		core::Array<core::uint64> PreparedBuildShape;
		core::Array<AccelStructHandle> PreparedBottomReferences;
		core::uint32 PreparedParentRefs = 0;
		bool bPreparedBuildShape = false;

		core::uint64 BuildRevision = 0;

		core::uint64 QueryGeneration = 0;
		core::uint64 QueryBuildRevision = 0;
		core::uint64 NextQueryGeneration = 1;

		core::uint32 PreparedReferences = 0;
		core::uint32 BindingSetRefs = 0;
		core::uint32 TableClosureRefs = 0;

		bool bUnavailable = false;
		bool bQueryUnavailable = false;

		core::usize RecordedUses = 0;

		core::uint32 Generation = 1;
		bool bLive = false;

		bool bBuilt = false;
		bool bAllowUpdate = false;
		bool bAllowCompaction = false;

		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct StagingTextureSlot final
	{
		BufferHandle Buffer;
		TextureDesc Descriptor;
		EBufferMemory Access = EBufferMemory::Device;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct TextureSlot final
	{
		void* Native = nullptr;
		TextureDesc Descriptor;

		TextureHandle PairedTexture;
		SamplerFeedbackDesc FeedbackDescriptor;

		core::uint64 ByteCount = 0;

		HeapHandle Heap;
		core::uint64 HeapOffset = 0;
		core::uint64 AllocationSize = 0;

		core::uint32 RecordingSlot = core::NumericLimits<core::uint32>::max();

		core::uint32 FramebufferRefs = 0;
		core::uint32 BindingSetRefs = 0;
		core::uint32 ViewRefs = 0;
		core::uint32 FeedbackRefs = 0;

		core::usize SharedReadUses = 0;
		core::usize PreparedReferences = 0;
		bool bSharedReadPrepared = false;

		core::uint64 AliasEpoch = 0;

		SwapChainHandle SwapChainOwner;
		core::usize AcquiredUnsubmittedUses = 0;
		core::usize AcquiredAcceptedUses = 0;
		bool bAcquisitionClosed = false;
		bool bAcquisitionPrepared = false;
		bool bAcquisitionReleaseAccepted = false;

		ETrackedResourceState PermanentState = ETrackedResourceState::Unknown;

		core::uint32 Generation = 1;
		bool bLive = false;

		bool bSwapChainImage = false;

		bool bExternalOwned = false;
		bool bExternalShareable = false;

		bool bSamplerFeedback = false;

		bool bSparseMappingPrepared = false;
		bool bSparseMappingUnavailable = false;

		bool bSubmitted = false;

		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	// Device-owned storage. Native payload cleanup is performed by the control thread
	// after discard, completion, or proven device teardown, before this owner is released.
	class PreparedTextureMappings final
	{
	public:
		explicit PreparedTextureMappings(core::MemoryResource& memory) noexcept;
		~PreparedTextureMappings();
		PreparedTextureMappings(const PreparedTextureMappings&) = delete;
		PreparedTextureMappings& operator=(const PreparedTextureMappings&) = delete;

	public:
		TextureHandle Texture;
		EQueue Queue = EQueue::Graphics;

		core::Array<TextureTileMapping> Mappings;
		core::Array<NativeTextureTileMapping> NativeMappings;
		core::Array<HeapHandle> RetainedHeaps;

		core::MemoryBlock NativeStorage;

		SubmissionToken Completion;
		core::uint32 Generation = 1;

		bool bSubmitted = false;
		bool bUncertain = false;
		bool bPinsAcquired = false;
	};

	struct SparseTileKey final
	{
		TextureHandle Texture;
		core::uint32 MipLevel = 0;
		core::uint32 ArrayLayer = 0;

		core::uint32 X = 0;
		core::uint32 Y = 0;
		core::uint32 Z = 0;
		core::StrongOrdering operator<=>(const SparseTileKey&) const = default;
	};

	struct SparseTileHash final
	{
		[[nodiscard]] core::usize operator()(const SparseTileKey& key) const noexcept;
	};

	struct SamplerSlot final
	{
		void* Native = nullptr;
		SamplerDesc Descriptor;

		core::uint64 LastUse = 0;
		core::usize RecordedUses = 0;
		core::uint32 BindingSetRefs = 0;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct BufferViewSlot final
	{
		void* Native = nullptr;
		BufferViewDesc Descriptor;

		core::uint32 BindingSetRefs = 0;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct TextureViewSlot final
	{
		void* Native = nullptr;
		TextureViewDesc Descriptor;

		core::uint64 LastUse = 0;
		core::usize RecordedUses = 0;
		core::uint32 BindingSetRefs = 0;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct ShaderSlot final
	{
		explicit ShaderSlot(core::MemoryResource& resource) noexcept;

		core::Array<core::Byte> Bytecode;
		char EntryPoint[64]{};
		EShaderStage Stage = EShaderStage::Compute;

		ShaderLibraryHandle Library;
		core::uint32 LibraryEntry = 0;

		core::uint32 Generation = 1;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
		bool bLive = false;
		bool bExhausted = false;
	};

	struct ShaderLibraryEntry final
	{
		explicit ShaderLibraryEntry(core::MemoryResource& resource) noexcept;

		core::Array<core::Byte> Bytecode;
		char EntryPoint[64]{};
		EShaderStage Stage = EShaderStage::Compute;
	};

	struct ShaderLibrarySlot final
	{
		explicit ShaderLibrarySlot(core::MemoryResource& resource) noexcept;

		core::Array<ShaderLibraryEntry> Entries;

		core::uint32 References = 0;

		core::uint32 Generation = 1;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
		bool bLive = false;
		bool bExhausted = false;
	};

	struct FramebufferSlot final
	{
		void* Native = nullptr;

		TextureHandle Colors[8]{};
		TextureSubresource ColorSubresources[8]{};

		TextureHandle Depth;
		TextureSubresource DepthSubresource{};

		TextureHandle ShadingRateImage;

		core::uint32 ColorCount = 0;

		core::uint32 Width = 0;
		core::uint32 Height = 0;

		core::usize RecordedUses = 0;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct SwapChainSlot final
	{
		void* Native = nullptr;
		SwapChainDesc Descriptor;

		TextureHandle AcquiredImage;
		TextureHandle FailedAcquisitionImage;

		core::uint64 OwnerGeneration = 1;
		core::uint64 AcquireGeneration = 0;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct OpacityMicromapSlot final
	{
		void* Native = nullptr;

		BufferHandle Storage;
		core::uint64 ByteOffset = 0;
		core::uint64 ByteCount = 0;

		core::usize RecordedUses = 0;
		core::uint32 AccelStructRefs = 0;

		core::uint32 Generation = 1;
		bool bLive = false;

		bool bBuilt = false;

		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	enum class ETimerState : core::uint8
	{
		Idle,
		Prepared,
		Recording,
		Recorded,
		Submitted
	};

	struct TimerSlot final
	{
		void* Native = nullptr;

		SubmissionToken Token;
		core::uint32 RecordingSlot = core::NumericLimits<core::uint32>::max();

		core::uint32 Generation = 1;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
		ETimerState State = ETimerState::Idle;
		bool bLive = false;
		bool bExhausted = false;

		bool bRetired = false;
	};

	struct EventQuerySlot final
	{
		SubmissionToken Token;

		core::uint32 Generation = 1;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
		bool bLive = false;
		bool bArmed = false;
		bool bExhausted = false;
	};
}
