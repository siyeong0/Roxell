#pragma once

#include "DeviceCommands.h"
#include "DevicePipelines.h"
#include "DeviceResources.h"
#include "DeviceValidation.h"
#include "PreparedRecordingGroup.h"

#include <Algorithms.hpp>
#include <Array.hpp>
#include <Assertion.h>
#include <ByteOperations.hpp>
#include <ClockPrimitives.hpp>
#include <Comparison.hpp>
#include <ConsoleIo.hpp>
#include <HashMap.hpp>
#include <Device.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <SharedPtr.hpp>
#include <Synchronization.hpp>
#include <TextPrimitives.hpp>
#include <ThreadPrimitives.hpp>
#include <Trace.h>
#include <Types.h>
#include <Utility.hpp>
#include <Vocabulary.hpp>

#if ROXELL_RHI_BACKEND == 1
#include "D3D12/D3D12Backend.h"
namespace rox::rhi
{
	namespace backend = rox::d3d12;
}
#elif ROXELL_RHI_BACKEND == 2
#include "Vulkan/VulkanBackend.h"
namespace rox::rhi
{
	namespace backend = rox::vulkan;
}
#else
#include "Null/NullBackend.h"
namespace rox::rhi
{
	namespace backend = rox::nullrhi;
}
#endif

namespace rox::rhi
{
	struct AcquiredImageUse final
	{
		TextureHandle Image;
		SubmissionToken Token;
	};

	struct RecordedDescriptorTableUse final
	{
		DescriptorTableHandle Handle;
		void* Native = nullptr;
	};

	struct CommandSlot final
	{
		explicit CommandSlot(core::MemoryResource& resource) noexcept;

		[[nodiscard]] bool HasBufferState(BufferHandle buffer, ETrackedResourceState expected) const noexcept;

		[[nodiscard]] bool AddBufferUse(RecordedBufferUse use) noexcept;

		mutable core::ExecutionThreadId RecordingThread;

		core::SharedPtr<PreparedRecordingGroup> PreparedGroup;
		core::uint32 PreparedIndex = 0;
		mutable bool bPreparationEligible = true;

		core::Array<RecordedCopy> Copies;
		core::Array<RecordedTextureCopy> TextureCopies;

		core::Array<RecordedTextureUse> TextureUses;
		core::Array<RecordedBufferUse> BufferUses;

		core::Array<RecordedAccelUse> AccelUses;
		core::Array<RecordedAccelBuild> AccelBuildTargets;

		core::Array<OpacityMicromapHandle> OpacityMicromapUses;
		core::Array<OpacityMicromapHandle> OpacityMicromapBuildTargets;

		core::Array<RecordedPipelineUse> PipelineUses;
		core::Array<RecordedGraphicsPipelineUse> GraphicsPipelineUses;
		core::Array<RecordedRayPipelineUse> RayPipelineUses;

		core::Array<RecordedFramebufferUse> FramebufferUses;

		core::Array<GpuTimerHandle> TimerUses;

		core::Array<SamplerHandle> SamplerUses;
		core::Array<TextureViewHandle> TextureViewUses;
		core::Array<RecordedDescriptorTableUse> DescriptorTableUses;

		void* Native = nullptr;

		core::uint64 PreparedShaderTableCreations = 0;
		core::uint64 PreparedShaderTableReuses = 0;

		TextureHandle ColorTarget;
		TextureHandle ColorTargets[8]{};
		core::uint32 ColorCount = 0;

		TextureHandle DepthTarget;
		FramebufferHandle ActiveFramebuffer;

		core::uint32 ViewportCount = 0;
		core::uint32 ScissorCount = 0;

		bool bRendering = false;
		bool bReadOnlyDepth = false;
		bool bReadOnlyStencil = false;

		bool bAutomaticBarriers = true;
		bool bHasManualBufferUses = false;

		bool bPoisoned = false;

		bool bQuarantined = false;
		SubmissionToken QuarantineToken;

		SubmissionToken SubmitPrerequisite;

		core::uint32 MarkerDepth = 0;

		EQueue Queue = EQueue::Copy;

		core::uint32 Generation = 1;
		bool bLive = false;
		bool bExhausted = false;
		core::uint32 NextFree = core::NumericLimits<core::uint32>::max();
	};

	struct SubmissionResult final
	{
		core::Result<SubmissionToken> Completion;
		ESubmissionDisposition Disposition;
		ESubmissionInputState InputState;
	};

	class Device::CallScope final
	{
	public:
		CallScope(const Device& device, bool bRecordingAllowed, bool bPreparedAllowed = false);
		~CallScope();

		bool bPreviousPreparedErrors = false;
		bool bAllowsPrepared = false;

		const Device& Owner;
		CallScope* Previous = nullptr;

		CallScope* Root = nullptr;
		const CommandSlot* Command = nullptr;

		core::UniqueLock<core::Mutex> Lock;

		inline static thread_local CallScope* Current = nullptr;
		inline static thread_local bool bNativeCallback = false;
	};

	struct Device::FixedGraphicsBindings final
	{
		TextureHandle Sampled[32]{};
		TextureHandle Storage[32]{};
		BufferHandle Buffers[32]{};

		NativeSampledTexture NativeSampled[32]{};
		NativeStorageTexture NativeStorage[32]{};
		NativeGraphicsBuffer NativeBuffers[32]{};

		core::uint32 SampledCount = 0;
		core::uint32 StorageCount = 0;
		core::uint32 BufferCount = 0;
		core::uint32 AccelCount = 0;
	};

	struct Device::ExternalFenceSlot final
	{
		void* Native = nullptr;

		SubmissionToken LastUses[3]{};
		SubmissionToken LastSignalSubmission;
		core::uint64 LastSignal = 0;

		core::uint32 Generation = 1;
		core::uint32 NextFree = UINT32_MAX;
	};

	struct Device::StagingSubresource final
	{
		core::uint64 Offset = 0;
		TextureBufferFootprint Footprint;
	};

	struct Device::TextureSamplesEntry final
	{
		TextureDesc Description;
		core::uint32 Samples = 0;
	};

	struct Device::TextureRequirementsEntry final
	{
		TextureDesc Description;
		MemoryRequirements Requirements;
	};

	struct Device::State final
	{
		State(core::MemoryResource& resource, BackendData backend, EBackend kind) noexcept;

		mutable core::Mutex m_MetadataMutex;
		mutable core::uint32 m_ActiveCalls = 0;
		bool m_bParallelRecording = false;

		bool m_bSubmissionFaulted = false;
		core::EErrorCode m_FirstSubmissionFailure = core::EErrorCode::None;
		char m_FirstSubmissionDiagnostic[128]{};
		char m_FirstSubmissionMessage[256]{};
		bool m_bSubmissionFaultLogged = false;
		core::uint32 m_FirstSubmissionQueueWaitCount = 0;
		bool m_bFirstSubmissionMayBeAccepted = false;
		core::int32 m_FirstSubmissionNativeCode = 0;
		core::uint32 m_FirstSubmissionFailureStage = 0;

		TextureHandle m_RetainedPresentSource;
		SubmissionToken m_RetainedPresentToken;

		BufferHandle m_RetainedReadBuffer;
		TextureHandle m_RetainedReadTexture;

		void* m_NativeQueueAccessPointers[3]{};
		core::uint64 m_NativeQueueAccessGenerations[3]{};
		core::uint32 m_ActiveNativeCalls = 0;

		ParallelRecordingStatistics m_ParallelStatistics;

		core::MemoryResource& m_Resource;

		PreparedProgramCache m_PreparedPrograms;

		core::Array<HeapSlot> m_Heaps;
		core::Array<RetiredHeapRange> m_RetiredRanges;

		core::Array<BufferSlot> m_Buffers;

		core::Array<AccelStructSlot> m_AccelStructs;
		core::Array<OpacityMicromapSlot> m_OpacityMicromaps;

		core::Array<StagingTextureSlot> m_StagingTextures;
		core::Array<TextureSlot> m_Textures;

		mutable EFormatSupport m_NativeFormatSupport[static_cast<core::usize>(EFormat::COUNT)]{};

		mutable TextureSamplesEntry m_TextureSamples[128]{};
		mutable core::uint32 m_TextureSamplesCount = 0;
		mutable core::uint32 m_TextureSamplesCursor = 0;

		// Successful immutable queries only; protected by CallScope's metadata lock.
		mutable TextureRequirementsEntry m_TextureRequirements[128]{};
		mutable core::uint32 m_TextureRequirementsCount = 0;
		mutable core::uint32 m_TextureRequirementsCursor = 0;

		core::HashMap<SparseTileKey, HeapHandle, SparseTileHash> m_SparseTiles;
		core::Array<core::SharedPtr<PreparedTextureMappings>> m_TextureMappings;
		core::usize m_PreparedSparseTileCount = 0;
		core::uint32 m_NextTextureMappingGeneration = 1;

		core::Array<SamplerSlot> m_Samplers;
		core::Array<TextureViewSlot> m_TextureViews;
		core::Array<BufferViewSlot> m_BufferViews;

		core::Array<ShaderSlot> m_Shaders;
		core::Array<ShaderLibrarySlot> m_ShaderLibraries;

		core::Array<CommandSlot> m_Commands;

		core::Array<TimerSlot> m_Timers;
		core::Array<EventQuerySlot> m_EventQueries;

		core::Array<PipelineSlot> m_Pipelines;
		core::Array<InputLayoutSlot> m_InputLayouts;
		core::Array<GraphicsPipelineSlot> m_GraphicsPipelines;

		core::Array<RayTracingPipelineSlot> m_RayTracingPipelines;
		core::Array<ShaderTableSlot> m_ShaderTables;

		core::Array<FramebufferSlot> m_Framebuffers;

		core::Array<SwapChainSlot> m_SwapChains;
		core::Array<AcquiredImageUse> m_AcquiredImageUses;

		core::Array<BindingSetSlot> m_BindingSets;
		core::Array<BindingLayoutSlot> m_BindingLayouts;
		core::Array<DescriptorTableSlot> m_DescriptorTables;

		core::Array<ExternalFenceSlot> m_ExternalFences;

		core::uint32 m_FreeExternalFence = UINT32_MAX;
		core::uint32 m_FreeBuffer = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeAccelStruct = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeOpacityMicromap = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeStagingTexture = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeHeap = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeTexture = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeSampler = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeTextureView = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeBufferView = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeShader = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeShaderLibrary = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeCommand = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeTimer = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeEventQuery = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreePipeline = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeInputLayout = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeGraphicsPipeline = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeRayTracingPipeline = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeShaderTable = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeFramebuffer = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeSwapChain = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeBindingSet = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeBindingLayout = core::NumericLimits<core::uint32>::max();
		core::uint32 m_FreeDescriptorTable = core::NumericLimits<core::uint32>::max();

		core::uint64 m_AliasEpoch = 0;
		core::uint64 m_BufferAliasEpoch = 0;

		core::Atomic<core::uint64> m_ManagedOwner{ 0 };

		BackendData m_Backend;
		EBackend m_Kind;
		core::uint64 m_Owner;
	};

	template <typename TFunction> [[nodiscard]] core::Result<void> Device::recordNative(TFunction&& function)
	{
		const CommandSlot* claimed = CallScope::Current->Root->Command;
		if (claimed != nullptr && !claimed->PreparedGroup)
		{
			claimed->bPreparationEligible = false;
		}

		if (!m_State->m_bParallelRecording)
		{
			return function();
		}
		CallScope& scope = *CallScope::Current->Root;
		ASSERT(scope.Command != nullptr && scope.Lock.owns_lock(), "Native recording requires exclusive command ownership.");
		++m_State->m_ParallelStatistics.NativeCalls;
		++m_State->m_ActiveNativeCalls;
		m_State->m_ParallelStatistics.PeakConcurrentNativeCalls = core::Max(m_State->m_ParallelStatistics.PeakConcurrentNativeCalls, m_State->m_ActiveNativeCalls);
		// Serial prepared groups have one admitted recording thread. Keep their metadata
		// lock through native emission instead of releasing and reacquiring it per draw.
		if (claimed->PreparedGroup && claimed->PreparedGroup->Mode == EPreparedRecordingMode::Serial)
		{
			core::Result<void> result = function();
			--m_State->m_ActiveNativeCalls;

			return result;
		}
		scope.Lock.unlock();
		core::Result<void> result = function();
		scope.Lock.lock();
		--m_State->m_ActiveNativeCalls;

		return result;
	}

	template <typename TPipeline>
	[[nodiscard]] core::Result<void> Device::deriveDirectTableCapacities(TPipeline& pipeline)
	{
		if (!pipeline.bHeapDirectlyIndexed || pipeline.bDeviceHeapIndexed)
		{
			return core::Result<void>::Success();
		}
		for (const BindingLayoutHandle handle : pipeline.Layouts)
		{
			BindingLayoutSlot* const layout = findBindingLayout(handle);
			ASSERT(layout != nullptr, "RHI operations require valid handles for this device.");
			for (core::uint32 index = 0; index < layout->RangeCount; ++index)
			{
				const BindingRangeDesc range = layout->Range(index);
				if (range.Source != EBindingRangeSource::DescriptorTable)
				{
					continue;
				}
				core::uint32* capacity = nullptr;
				switch (range.Type)
				{
				case EDescriptorType::SampledTexture: capacity = &pipeline.BindlessSampledTextureCapacity; break;
				case EDescriptorType::StorageTexture: capacity = &pipeline.BindlessStorageTextureCapacity; break;
				case EDescriptorType::UniformBuffer: capacity = &pipeline.BindlessUniformBufferCapacity; break;
				case EDescriptorType::StorageBuffer:
					capacity = &pipeline.BindlessStorageBufferCapacity;
					pipeline.BindlessStorageBufferFormat = range.BufferFormat;
					break;
				case EDescriptorType::ReadOnlyBuffer:
					capacity = &pipeline.BindlessReadOnlyBufferCapacity;
					pipeline.BindlessReadOnlyBufferFormat = range.BufferFormat;
					break;
				case EDescriptorType::Sampler: capacity = &pipeline.BindlessSamplerCapacity; break;
				case EDescriptorType::AccelerationStructure: break;
				}
				if (capacity == nullptr || *capacity != 0)
				{
					return core::Result<void>::Failure(core::EErrorCode::Unsupported, "Rhi.DescriptorHeap", "Direct heap indexing requires at most one table of each supported type.");
				}
				*capacity = range.DescriptorCount;
			}
		}
		return core::Result<void>::Success();
	}

	template <typename TPipeline>
	[[nodiscard]] TextureViewHandle Device::computeTextureView(const TPipeline& pipeline, const BindingSetSlot* set, core::usize index) noexcept
	{
		if (set == nullptr)
		{
			return {};
		}

		const bool bStorage = pipeline.Textures[index].Type == EComputeTextureBindingType::StorageTexture;
		core::usize viewIndex = 0;

		for (core::usize previous = 0; previous < index; ++previous)
		{
			if ((pipeline.Textures[previous].Type == EComputeTextureBindingType::StorageTexture) == bStorage)
			{
				++viewIndex;
			}
		}

		return bStorage ? set->StorageViews[viewIndex] : set->SampledViews[viewIndex];
	}

	template <typename TPipeline>
	[[nodiscard]] core::Result<void> Device::validateComputeTextures(
		const TPipeline& pipeline,
		core::Span<const TextureHandle> sampled,
		core::Span<const TextureHandle> storage,
		CommandSlot& command,
		core::uint32 listSlot,
		BindingSetHandle bindingSet,
		NativeComputeTexture(&native)[8]) noexcept
	{
		BindingSetSlot* const set = bindingSet.IsValid() ? findBindingSet(bindingSet) : nullptr;

		if (set != nullptr && set->SamplerCount != 0 && !command.SamplerUses.Reserve(command.SamplerUses.Size() + set->SamplerCount))
		{
			return core::Result<void>::Failure(core::EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain recorded samplers.");
		}

		if (set != nullptr && !command.TextureViewUses.Reserve(command.TextureViewUses.Size() + set->TextureCount + set->StorageTextureCount))
		{
			return core::Result<void>::Failure(core::EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not retain recorded texture views.");
		}

		ASSERT(sampled.Size() == pipeline.SampledTextureCount && storage.Size() == pipeline.StorageTextureCount, "The texture counts do not match the pipeline.");

		const bool bRanges = set != nullptr && m_State->m_BindingLayouts[set->Layout.Slot].RangeCount != 0;
		core::usize sampledIndex = 0;
		core::usize storageIndex = 0;

		for (core::usize index = 0; index < pipeline.TextureCount; ++index)
		{
			const bool bWrite = pipeline.Textures[index].Type == EComputeTextureBindingType::StorageTexture;
			const core::usize samplerIndex = sampledIndex;
			const TextureHandle handle = bWrite ? storage[storageIndex++] : sampled[sampledIndex++];
			SamplerSlot* const sampler =
				!bWrite && set != nullptr && set->SamplerCount != 0 && m_State->m_BindingLayouts[set->Layout.Slot].RangeCount == 0
				? findSampler(set->Samplers[samplerIndex])
				: nullptr;
			const SamplerDesc& samplerDesc = sampler != nullptr ? sampler->Descriptor : pipeline.Textures[index].Sampler;
			TextureSlot* const texture = findTexture(handle);

			ASSERT(texture != nullptr, "RHI operations require a live handle owned by this device.");

			ASSERT(HasUsage(texture->Descriptor.Usage, bWrite ? ETextureUsage::Storage : ETextureUsage::ShaderRead) || (bWrite && texture->bSamplerFeedback), "The texture lacks its declared usage.");

			if (texture->bSamplerFeedback)
			{
				bool bPaired = false;

				for (const TextureHandle sampledHandle : sampled)
				{
					bPaired |= sampledHandle == texture->PairedTexture;
				}

				ASSERT(bPaired, "The paired texture must be sampled in the same compute layout.");
			}

			ASSERT(bRanges || bWrite || !samplerDesc.bCompareEnabled || IsDepthFormat(texture->Descriptor.Format), "Comparison sampling requires a depth texture.");

			if (!bRanges &&
				!bWrite &&
				(samplerDesc.MinFilter == ESamplerFilter::Linear ||
				 samplerDesc.MagFilter == ESamplerFilter::Linear ||
				 samplerDesc.MaxAnisotropy > 1.0f ||
				 samplerDesc.Reduction != ESamplerReduction::Standard))
			{
				const EFormatSupport support = backend::QueryFormatSupport(m_State->m_Backend.State, sampledViewFormat(set, samplerIndex, texture->Descriptor.Format));

				if (((samplerDesc.MinFilter == ESamplerFilter::Linear || samplerDesc.MagFilter == ESamplerFilter::Linear || samplerDesc.MaxAnisotropy > 1.0f) &&
					 !HasFormatSupport(support, EFormatSupport::ShaderSample)) ||
					(samplerDesc.Reduction != ESamplerReduction::Standard && !HasFormatSupport(support, EFormatSupport::SamplerMinMax)))
				{
					return core::Result<void>::Failure(core::EErrorCode::Unsupported, "Rhi.ComputeTexture", "The texture format does not support the sampler.");
				}
			}

			if (bWrite ? (texture->RecordingSlot != UINT32_MAX && texture->RecordingSlot != listSlot) : !canReadTexture(*texture, listSlot))
			{
				return core::Result<void>::Failure(core::EErrorCode::Conflict, "Rhi.RecordedUse", "Another command list references the sampled texture.");
			}

			for (core::usize previous = 0; previous < index; ++previous)
			{
				ASSERT(native[previous].Texture != texture->Native ||
					(!bWrite && native[previous].Type != EComputeTextureBindingType::StorageTexture) ||
					textureViewsDisjoint(computeTextureView(pipeline, set, index), computeTextureView(pipeline, set, previous)),
					"Writable texture bindings overlap in mip or layer range.");
			}
			native[index] = { texture->Native, sampler != nullptr ? sampler->Native : nullptr, pipeline.Textures[index].Type,
							 set != nullptr ? (bWrite ? (set->StorageViews[storageIndex - 1].IsValid()
															 ? m_State->m_TextureViews[set->StorageViews[storageIndex - 1].Slot].Native
															 : nullptr)
													  : (set->SampledViews[sampledIndex - 1].IsValid()
															 ? m_State->m_TextureViews[set->SampledViews[sampledIndex - 1].Slot].Native
															 : nullptr))
											: nullptr };
		}

		return core::Result<void>::Success();
	}

	[[nodiscard]] inline BindingSetHandle Device::singleBindingSet(core::Span<const BindingSetHandle> sets) noexcept
	{
		return sets.Size() == 1 ? sets[0] : BindingSetHandle{};
	}
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	void FailPreparedSubmitAfterForTesting(core::uint32 acceptedCount, bool bRejectBeforeAcceptance) noexcept;
	void FailNextAcceptedSubmitForTesting() noexcept;
	void FailNextSubmissionReservationForTesting() noexcept;
#endif
}
