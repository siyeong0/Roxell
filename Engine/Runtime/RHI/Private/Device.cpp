#include "DeviceState.h"
#include "DeviceResources.h"
#include "DevicePipelines.h"

#include <Assertion.h>
#include <Rhi.h>
#include <Synchronization.hpp>

namespace rox::rhi
{
	using namespace core;

#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	thread_local bool g_bFailNextAcceptedSubmit = false;
	thread_local bool g_bFailNextSubmissionReservation = false;
#endif
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	thread_local uint32 g_PreparedFailureCountdown = NumericLimits<uint32>::max();
#endif
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	thread_local bool g_bRejectPreparedFailureBeforeAcceptance = false;
#endif
	thread_local bool g_bPreparedErrors = false;

	Device::State::State(MemoryResource& resource, BackendData backend, EBackend kind) noexcept
		: m_Resource(resource)
		, m_PreparedPrograms(resource), m_Heaps(resource)
		, m_RetiredRanges(resource)
		, m_Buffers(resource)
		, m_AccelStructs(resource)
		, m_OpacityMicromaps(resource)
		, m_StagingTextures(resource)
		, m_Textures(resource)
		, m_SparseTiles(resource)
		, m_TextureMappings(resource)
		, m_Samplers(resource)
		, m_TextureViews(resource)
		, m_BufferViews(resource)
		, m_Shaders(resource)
		, m_ShaderLibraries(resource)
		, m_Commands(resource)
		, m_Timers(resource)
		, m_EventQueries(resource)
		, m_Pipelines(resource)
		, m_InputLayouts(resource)
		, m_GraphicsPipelines(resource)
		, m_RayTracingPipelines(resource)
		, m_ShaderTables(resource)
		, m_Framebuffers(resource)
		, m_SwapChains(resource)
		, m_AcquiredImageUses(resource)
		, m_BindingSets(resource)
		, m_BindingLayouts(resource)
		, m_DescriptorTables(resource)
		, m_ExternalFences(resource)
		, m_Backend(backend)
		, m_Kind(kind)
		, m_Owner(NextDeviceOwnerId())
	{
		const DeviceLimits nativeLimits = backend::HAS_GET_LIMITS ? backend::GetLimits(backend.State) : DeviceLimits{};
		DeviceLimits& limits = m_Backend.Capabilities.Limits;
		limits.MaximumBufferByteCount = nativeLimits.MaximumBufferByteCount;
		limits.MaximumUniformBufferRangeBytes = nativeLimits.MaximumUniformBufferRangeBytes;
		limits.MaximumStorageBufferRangeBytes = nativeLimits.MaximumStorageBufferRangeBytes;
		limits.UniformBufferOffsetAlignmentBytes = nativeLimits.UniformBufferOffsetAlignmentBytes;
		limits.StorageBufferOffsetAlignmentBytes = nativeLimits.StorageBufferOffsetAlignmentBytes;
		limits.MaximumTexture1DDimension = nativeLimits.MaximumTexture1DDimension;
		limits.MaximumTexture2DDimension = nativeLimits.MaximumTexture2DDimension;
		limits.MaximumTexture3DDimension = nativeLimits.MaximumTexture3DDimension;
		limits.MaximumTextureArrayLayers = nativeLimits.MaximumTextureArrayLayers;
		limits.MaximumTextureMipLevels = nativeLimits.MaximumTextureMipLevels;
		limits.MaximumTextureByteCount = nativeLimits.MaximumTextureByteCount;
		limits.MaximumSparseTextureByteCount = nativeLimits.MaximumSparseTextureByteCount;
		limits.MaximumBindingLayouts = nativeLimits.MaximumBindingLayouts;
		limits.MaximumBindingsPerLayout = nativeLimits.MaximumBindingsPerLayout;
		limits.MaximumPushConstantByteCount = nativeLimits.MaximumPushConstantByteCount;
		limits.TextureBufferOffsetAlignmentBytes = nativeLimits.TextureBufferOffsetAlignmentBytes;
		limits.TextureBufferRowPitchAlignmentBytes = nativeLimits.TextureBufferRowPitchAlignmentBytes;
		limits.MaximumComputeWorkGroupSize[0] = nativeLimits.MaximumComputeWorkGroupSize[0];
		limits.MaximumComputeWorkGroupSize[1] = nativeLimits.MaximumComputeWorkGroupSize[1];
		limits.MaximumComputeWorkGroupSize[2] = nativeLimits.MaximumComputeWorkGroupSize[2];
		limits.MaximumComputeWorkGroupInvocations = nativeLimits.MaximumComputeWorkGroupInvocations;
		limits.MaximumComputeWorkGroupCount[0] = nativeLimits.MaximumComputeWorkGroupCount[0];
		limits.MaximumComputeWorkGroupCount[1] = nativeLimits.MaximumComputeWorkGroupCount[1];
		limits.MaximumComputeWorkGroupCount[2] = nativeLimits.MaximumComputeWorkGroupCount[2];
		if (kind == EBackend::Null)
		{
			limits.MaximumTextureByteCount = 256ull * 1024ull * 1024ull;
			limits.MaximumBufferByteCount = backend.Capabilities.Limits.MaximumBufferByteCount;
		}

		limits.MaximumBindlessSampledTextures = backend.MaximumBindlessSampledTextures;
		limits.MaximumBindlessSamplers = backend.MaximumBindlessSamplers;
		limits.MaximumBindlessStorageBuffers = backend.MaximumBindlessStorageBuffers;
		limits.MaximumBindlessStorageTextures = backend.MaximumBindlessStorageTextures;
		limits.MaximumBindlessUniformBuffers = backend.MaximumBindlessUniformBuffers;
		limits.MaximumGraphicsBindlessStorageBuffers = backend.MaximumGraphicsBindlessStorageBuffers;
		limits.MaximumGraphicsBindlessStorageTextures = backend.MaximumGraphicsBindlessStorageTextures;
		limits.MaximumBindlessReadOnlyBuffers = Min(backend.MaximumBindlessStorageBuffers, backend.MaximumBindlessSampledTextures);

		DeviceFeatures& features = m_Backend.Capabilities.Features;
		features.bRayQuery = backend.bRayQuery;
		features.bSamplerFeedback = backend.bSamplerFeedback;
		features.bHeapDirectlyIndexed = backend.bHeapDirectlyIndexed;
		features.bRayTracingPipeline = backend.bRayTracingPipeline;
		features.bOpacityMicromap = backend.bOpacityMicromap;
		features.bRayTracingClusters = backend.bRayTracingClusters;
		features.bSliced3DStorageViews = backend.bSliced3DStorageViews;
		features.bCooperativeVectorMatrixConversion = backend.bCooperativeVector;
		features.bShaderSpecializations = kind == EBackend::Vulkan;
		features.bConstantBufferRanges = kind != EBackend::Null;
		features.bVariableRateShading = (limits.SupportedShadingRates[0] & ~1u) != 0;
	}

	Device::~Device() noexcept
	{
		ASSERT(m_State->m_ManagedOwner.load(MemoryOrderAcquire) == 0, "A device must outlive its registered managed owner.");
		ASSERT(!hasAnyNativeQueueAccess(), "End native queue CPU admission before destroying the device.");
		ASSERT(!m_State->m_bParallelRecording && m_State->m_ActiveCalls == 0, "Join recording workers and end the phase before destroying the device.");
		ASSERT(backend::HAS_HAS_RETIREMENT_PROOF == false || backend::HasRetirementProof(m_State->m_Backend.State), "Drain the RHI device or establish device loss before destruction.");

		m_State->m_PreparedPrograms.Clear();
		collectPreparedTextureMappings(true, true);
		for (CommandSlot& slot : m_State->m_Commands)
		{
			if (slot.bLive && slot.Native != nullptr)
			{
				backend::DiscardCommandList(m_State->m_Backend.State, slot.Native);
			}
			slot.PreparedGroup.Reset();
		}

		for (BindingSetSlot& slot : m_State->m_BindingSets)
		{
			slot.Cache.Reset();
		}

		for
			(ExternalFenceSlot& slot : m_State->m_ExternalFences)
		{
			if (slot.Native != nullptr)
			{
				backend::DestroyExternalFence(m_State->m_Backend.State, slot.Native);
			}
		}

		for (TimerSlot& slot : m_State->m_Timers)
		{
			if (slot.bLive)
			{
				backend::DestroyGpuTimer(m_State->m_Backend.State, slot.Native);
			}
		}

		for (AccelStructSlot& slot : m_State->m_AccelStructs)
		{
			if (slot.bLive)
			{
				backend::DestroyAccelStructNow(m_State->m_Backend.State, slot.Native);
			}
		}

		for (OpacityMicromapSlot& slot : m_State->m_OpacityMicromaps)
		{
			if (slot.bLive)
			{
				backend::DestroyOpacityMicromapNow(m_State->m_Backend.State, slot.Native);
			}
		}

		for (BufferViewSlot& slot : m_State->m_BufferViews)
		{
			if (slot.bLive)
			{
				backend::DestroyBufferView(m_State->m_Backend.State, slot.Native);
			}
		}

		for (BufferSlot& slot : m_State->m_Buffers)
		{
			if (!slot.bLive)
			{
				continue;
			}

			if (slot.bMapped)
			{
				(void)backend::UnmapBuffer(m_State->m_Backend.State, slot.Native);
			}

			backend::DestroyBufferNow(m_State->m_Backend.State, slot.Native);
		}

		for (SwapChainSlot& slot : m_State->m_SwapChains)
		{
			ASSERT(!slot.bLive || !slot.AcquiredImage.IsValid(), "Release every acquired swapchain image before device destruction.");

			if (slot.bLive)
			{
				backend::DestroySwapChainNow
				(m_State->m_Backend.State, slot.Native);
			}
		}

		for (FramebufferSlot& slot : m_State->m_Framebuffers)
		{
			if (slot.bLive)
			{
				backend::DestroyFramebufferNow(m_State->m_Backend.State, slot.Native);
			}
		}

		for (DescriptorTableSlot& slot : m_State->m_DescriptorTables)
		{
			if (slot.bLive)
			{
				backend::DestroyDescriptorTable(m_State->m_Backend.State, slot.Native);
			}

			for (DescriptorTableVersion& version : slot.RetiredVersions)
			{
				backend::DestroyDescriptorTable(m_State->m_Backend.State, version.Native);
			}
		}

		for (TextureViewSlot& slot : m_State->m_TextureViews)
		{
			if (slot.bLive)
			{
				backend::DestroyTextureView(m_State->m_Backend.State, slot.Native);
			}
		}

		for (TextureSlot& slot : m_State->m_Textures)
		{
			if (slot.bLive && !slot.bSwapChainImage)
			{
				backend::DestroyTextureNow(m_State->m_Backend.State, slot.Native);
			}
		}

		for (SamplerSlot& slot : m_State->m_Samplers)
		{
			if (slot.bLive)
			{
				backend::DestroySampler
				(m_State->m_Backend.State, slot.Native);
			}
		}

		for
			(HeapSlot& slot : m_State->m_Heaps)
		{
			if (slot.bLive)
			{
				backend::DestroyHeapNow
				(m_State->m_Backend.State, slot.Native);
			}
		}

		for
			(PipelineSlot& slot : m_State->m_Pipelines)
		{
			if (slot.bLive)
			{
				backend::DestroyComputePipelineNow(m_State->m_Backend.State, slot.Native);
			}
		}

		for (GraphicsPipelineSlot& slot : m_State->m_GraphicsPipelines)
		{
			if (slot.bLive)
			{
				backend::DestroyGraphicsPipelineNow
				(m_State->m_Backend.State, slot.Native);
			}
		}

		for (ShaderTableSlot& slot : m_State->m_ShaderTables)
		{
			slot.PreparedTable.Reset();
		}

		for (RayTracingPipelineSlot& slot : m_State->m_RayTracingPipelines)
		{
			if (slot.bLive)
			{
				backend::DestroyRayTracingPipelineNow
				(m_State->m_Backend.State, slot.Native);
			}
		}

		backend::Destroy
		(m_State->m_Backend.State);
	}

	[[nodiscard]] EBackend Device::GetBackend() const noexcept
	{
		CallScope call
		(*this, true);
		return m_State->m_Kind;
	}

	[[nodiscard]] bool Device::IsAftermathEnabled() const noexcept
	{
		CallScope call
		(*this, true);
		return m_State->m_Backend.bAftermathEnabled;
	}

	[[nodiscard]] IGpuCrashDiagnostics* Device::GetGpuCrashDiagnostics() const noexcept
	{
		CallScope call(*this, true);
		return m_State->m_Backend.Diagnostics;
	}

	[[nodiscard]] const GpuCapabilities& Device::GetCapabilities() const noexcept
	{
		return m_State->m_Backend.Capabilities;
	}

	[[nodiscard]] TextureSlot* Device::findTexture(TextureHandle handle, bool bAllowExternal) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Textures.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		TextureSlot& slot = m_State->m_Textures[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");
		if (CallScope::Current->Root->Command != nullptr && (slot.bSparseMappingPrepared || slot.bSparseMappingUnavailable))
		{
			return nullptr;
		}
		if (CallScope::Current->Root->Command != nullptr && !CallScope::Current->Root->Command->PreparedGroup)
		{
			for (const CommandSlot& command : m_State->m_Commands)
			{
				if (!command.PreparedGroup)
				{
					continue;
				}
				for (const PreparedRecordingPhase& phase : command.PreparedGroup->Lists[command.PreparedIndex].Phases)
				{
					for (const PreparedTextureAccessDesc& access : phase.Textures)
					{
						if (access.Texture == handle)
						{
							return nullptr;
						}
					}
				}
			}
		}

		ASSERT(!slot.bAcquisitionClosed && (bAllowExternal || !slot.bExternalOwned), "Texture access requires an open acquisition and permitted resource ownership.");
		return &slot;
	}

	[[nodiscard]] SwapChainSlot* Device::findSwapChain(SwapChainHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_SwapChains.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		SwapChainSlot& slot = m_State->m_SwapChains[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");
		return &slot;
	}

	[[nodiscard]] PipelineSlot* Device::findPipeline(ComputePipelineHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Pipelines.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		PipelineSlot& slot = m_State->m_Pipelines[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	[[nodiscard]] GraphicsPipelineSlot* Device::findGraphicsPipeline(GraphicsPipelineHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_GraphicsPipelines.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		GraphicsPipelineSlot& slot = m_State->m_GraphicsPipelines[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");
		return &slot;
	}

	[[nodiscard]] InputLayoutSlot* Device::findInputLayout(InputLayoutHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_InputLayouts.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		InputLayoutSlot& slot = m_State->m_InputLayouts[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");
		return &slot;
	}

	[[nodiscard]] RayTracingPipelineSlot* Device::findRayTracingPipeline(RayTracingPipelineHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_RayTracingPipelines.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		RayTracingPipelineSlot& slot = m_State->m_RayTracingPipelines[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");
		return &slot;
	}

	[[nodiscard]] ShaderTableSlot* Device::findShaderTable(ShaderTableHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_ShaderTables.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		ShaderTableSlot& slot = m_State->m_ShaderTables[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");
		return &slot;
	}
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	void FailPreparedSubmitAfterForTesting(uint32 acceptedCount, bool bRejectBeforeAcceptance) noexcept
	{
		g_PreparedFailureCountdown = acceptedCount; g_bRejectPreparedFailureBeforeAcceptance = bRejectBeforeAcceptance;
	}
#endif
#if defined(ROXELL_RHI_CONTRACT_TEST_HOOKS)
	void FailNextAcceptedSubmitForTesting() noexcept
	{
		g_bFailNextAcceptedSubmit = true;
	}

	void FailNextSubmissionReservationForTesting() noexcept
	{
		g_bFailNextSubmissionReservation = true;
	}
#endif

	[[nodiscard]] Result<EFormatSupport> Device::QueryFormatSupport(EFormat format) const
	{
		CallScope call(*this, true);

		ASSERT(format < EFormat::COUNT, "The queried format must be a valid RHI format value.");

		uint32 support = static_cast<uint32>(queryNativeFormatSupport(format));

		if (!IsColorTargetFormat(format))
		{
			support &= ~static_cast<uint32>(EFormatSupport::RenderTarget | EFormatSupport::Blendable);
		}

		if (!IsStorageColorFormat(format) && !IsIntegerStorageFormat(format))
		{
			support &= ~static_cast<uint32>(EFormatSupport::ShaderUavLoad | EFormatSupport::ShaderUavStore | EFormatSupport::ShaderAtomic);
		}

		return Result<EFormatSupport>::Success(static_cast<EFormatSupport>(support));
	}

	[[nodiscard]] Result<uint32> Device::QueryTextureSampleCounts(const TextureDesc& descriptor) const
	{
		CallScope call(*this, true);
		TextureDesc desc = descriptor;
		desc.SampleCount = 1;
		Result<uint64> validated = validateTextureDesc(desc);
		if (!validated)
		{
			ASSERT(validated.GetError().Code != EErrorCode::InvalidArgument, "A valid texture descriptor is required.");
			return Result<uint32>::Failure(Move(validated.GetError()));
		}

		Result<uint32> queried = queryNativeTextureSampleCounts(desc);
		if (!queried)
		{
			return queried;
		}

		uint32 mask = queried.Value() & 15u;
		const bool bDepth = HasUsage(desc.Usage, ETextureUsage::DepthStencil);
		const uint32 allowedUsage = bDepth
			? static_cast<uint32>(ETextureUsage::DepthStencil) | TEXTURE_COPY_BITS | static_cast<uint32>(ETextureUsage::ShaderRead)
			: static_cast<uint32>(ETextureUsage::RenderTarget) | TEXTURE_COPY_BITS | static_cast<uint32>(ETextureUsage::ShaderRead);
		if (desc.Dimension != ETextureDimension::Texture2D ||
			desc.MipLevels != 1 ||
			desc.bSparse ||
			!HasUsage(desc.Usage, bDepth ? ETextureUsage::DepthStencil : ETextureUsage::RenderTarget) ||
			(static_cast<uint32>(desc.Usage) & ~allowedUsage) != 0)
		{
			mask &= 1u;
		}
		for (uint32 samples = 2; samples <= 8; samples *= 2)
		{
			if (validated.Value() > m_State->m_Backend.Capabilities.Limits.MaximumTextureByteCount / samples)
			{
				mask &= ~samples;
			}
		}

		return Result<uint32>::Success(mask);
	}

	[[nodiscard]] BufferSlot* Device::findBuffer(BufferHandle handle, bool bAllowExternal) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Buffers.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		BufferSlot& slot = m_State->m_Buffers[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");
		if (CallScope::Current->Root->Command != nullptr && !CallScope::Current->Root->Command->PreparedGroup)
		{
			for (rhi::CommandSlot const& command : m_State->m_Commands)
			{
				if (!command.PreparedGroup)
				{
					continue;
				}
				for (rhi::PreparedRecordingPhase const& phase : command.PreparedGroup->Lists[command.PreparedIndex].Phases)
				{
					for (rhi::PreparedBufferAccessDesc const& access : phase.Buffers)
					{
						if (access.Buffer == handle)
						{
							return nullptr;
						}
					}
				}
			}
		}

		ASSERT(bAllowExternal || !slot.bExternalOwned, "GPU access requires RHI ownership of the resource.");
		return &slot;
	}

	[[nodiscard]] const BufferSlot* Device::findBuffer(BufferHandle handle, bool bAllowExternal) const noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Buffers.Size(), "A nonempty handle must belong to this device and name an allocated slot.");

		const BufferSlot& slot = m_State->m_Buffers[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		ASSERT(bAllowExternal || !slot.bExternalOwned, "GPU access requires RHI ownership of the resource.");
		return &slot;
	}

	[[nodiscard]] AccelStructSlot* Device::findAccelStruct(AccelStructHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_AccelStructs.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		AccelStructSlot& slot = m_State->m_AccelStructs[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");
		if (CallScope::Current->Root->Command != nullptr && !CallScope::Current->Root->Command->PreparedGroup && slot.PreparedReferences != 0)
		{
			return nullptr;
		}

		return &slot;
	}

	[[nodiscard]] OpacityMicromapSlot* Device::findOpacityMicromap(OpacityMicromapHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_OpacityMicromaps.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		OpacityMicromapSlot& slot = m_State->m_OpacityMicromaps[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	[[nodiscard]] const OpacityMicromapSlot* Device::findOpacityMicromap(OpacityMicromapHandle handle) const noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_OpacityMicromaps.Size(), "A nonempty handle must belong to this device and name an allocated slot.");

		const OpacityMicromapSlot& slot = m_State->m_OpacityMicromaps[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	[[nodiscard]] StagingTextureSlot* Device::findStagingTexture(StagingTextureHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_StagingTextures.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		StagingTextureSlot& slot = m_State->m_StagingTextures[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");

		return &slot;
	}

	[[nodiscard]] CommandSlot* Device::findCommand(CommandListHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Commands.Size(), "A nonempty handle must belong to this device and name an allocated slot.");
		CommandSlot& slot = m_State->m_Commands[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");
		ASSERT(!slot.bPoisoned, "Discard a command list after recording fails.");

		ASSERT(!slot.PreparedGroup || CallScope::Current->Root->bAllowsPrepared, "Prepared command lists require a prepared recording operation.");
		claimCommand(slot);

		return &slot;
	}

	[[nodiscard]] const CommandSlot* Device::findCommand(CommandListHandle handle) const noexcept
	{
		if (!handle.IsValid())
		{
			ASSERT(handle.Owner == 0 && handle.Slot == 0 && handle.Generation == 0, "An optional resource handle must be empty or valid.");
			return nullptr;
		}
		ASSERT(handle.Owner == m_State->m_Owner && handle.Slot < m_State->m_Commands.Size(), "A nonempty handle must belong to this device and name an allocated slot.");

		const CommandSlot& slot = m_State->m_Commands[handle.Slot];
		ASSERT(slot.bLive && slot.Generation == handle.Generation, "A nonempty handle must name a live resource with its current generation.");
		ASSERT(!slot.bPoisoned, "Discard a command list after recording fails.");

		ASSERT(!slot.PreparedGroup || CallScope::Current->Root->bAllowsPrepared, "Prepared command lists require a prepared recording operation.");
		claimCommand(slot);

		return &slot;
	}

	uint64 NextDeviceOwnerId() noexcept
	{
		static Atomic<uint64> next{ 1 };
		const uint64 value = next.fetch_add(1, MemoryOrderRelaxed);
		ASSERT(value != 0, "RHI device owner identifiers must not wrap.");

		return value;
	}

	Result<void> Device::RegisterManagedOwner(uint64 owner)
	{
		ASSERT(owner != 0, "Managed registration requires a nonzero owner generation.");
		uint64 expected = 0;
		if (!m_State->m_ManagedOwner.compare_exchange_strong(expected, owner, MemoryOrderAcqRel))
		{
			return Result<void>::Failure(EErrorCode::Conflict, "Rhi.ManagedOwner", "The device already has a managed owner.");
		}

		return Result<void>::Success();
	}

	void Device::UnregisterManagedOwner(uint64 owner) noexcept
	{
		ASSERT(owner != 0, "Managed release requires a nonzero owner generation.");
		uint64 expected = owner;
		const bool bReleased = m_State->m_ManagedOwner.compare_exchange_strong(expected, 0, MemoryOrderAcqRel);
		ASSERT(bReleased, "Only the registered managed owner releases its device exactly once.");
	}

	NativeSubmissionOutcome Device::SubmitManagedPrepared(uint64 owner, CommandListHandle list, Span<const SubmissionToken> prerequisites)
	{
		ASSERT(owner != 0 && m_State->m_ManagedOwner.load(MemoryOrderAcquire) == owner, "Managed submission requires the registered owner generation.");
		return submitPreparedManaged(list, prerequisites);
	}

	Device::Device(CreationKey, UniquePtr<State>&& state) noexcept
		: m_State(Move(state))
	{
		ASSERT(m_State, "A device requires initialized implementation state.");
	}

}
