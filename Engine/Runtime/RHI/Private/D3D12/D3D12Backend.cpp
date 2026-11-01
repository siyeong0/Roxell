#include "D3D12Backend.h"
#include "D3D12Descriptors.h"
#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	PreparedAccelQueryOwner::~PreparedAccelQueryOwner() noexcept = default;

	void MergeBufferUses(const Buffer& source, Buffer& target) noexcept
	{
		for (uint32 index = 0; index < 3; ++index)
		{
			RecordResourceUse(target, static_cast<EQueue>(index), source.QueueUses[index]);
		}

		target.bUnknownAliases = target.bUnknownAliases || source.bUnknownAliases;
	}

	void InvalidateIdle(State& state) noexcept
	{
		state.bIdleIssued = false;
		state.bIdleConfirmed = false;
	}

	bool WasSubmissionAccepted(const State* opaque) noexcept
	{
		return opaque->bSubmissionMayBePending;
	}

	bool HasRetirementProof(const State* opaque) noexcept
	{
		const State& state = *opaque;
		return state.bIdleConfirmed || (state.Device && FAILED(state.Device->GetDeviceRemovedReason()));
	}

	uint64 GetPresentCopySerial(const State* opaque) noexcept
	{
		return opaque->PresentCopySerial;
	}

	void ExposeNativeQueues(State* opaque) noexcept
	{
		State& state = *opaque;
		state.bNativeQueuesExposed = true;
		InvalidateIdle(state);
	}

	void RetireAftermathMarkers(Pending& pending) noexcept
	{
		if (pending.Aftermath.Diagnostics != nullptr)
		{
			for (uint64 token : pending.AftermathMarkers)
			{
				(void)pending.Aftermath.Diagnostics->RetireMarker(token);
			}
		}

		pending.AftermathMarkers.Clear();
	}

	void RecordBarriers(Pending& pending, UINT count, const D3D12_RESOURCE_BARRIER* barriers) noexcept
	{
		++pending.Statistics.BarrierCalls;
		pending.Statistics.BarrierCount += count;
		pending.List->ResourceBarrier(count, barriers);
	}

	void CopyCommandDescriptors(State& state, Pending& pending, UINT count, D3D12_CPU_DESCRIPTOR_HANDLE destination, D3D12_CPU_DESCRIPTOR_HANDLE source, D3D12_DESCRIPTOR_HEAP_TYPE type) noexcept
	{
		++pending.Statistics.DescriptorWriteCalls;
		pending.Statistics.DescriptorCount += count;
		state.Device->CopyDescriptorsSimple(count, destination, source, type);
	}

	thread_local bool bPreparedErrors = false;

	PreparedErrorScope::PreparedErrorScope() noexcept
		: bPrevious(bPreparedErrors)
	{
		bPreparedErrors = true;
	}

	PreparedErrorScope::~PreparedErrorScope() noexcept
	{
		bPreparedErrors = bPrevious;
	}

	[[nodiscard]] Error NativeError(EErrorCode code, const char* id, const char* message) noexcept
	{
		if (bPreparedErrors)
		{
			Error error;
			error.Code = code;

			return error;
		}

		return MakeError(code, StringView(id, CStringLength(id)),
			StringView(message, CStringLength(message)));
	}

	[[nodiscard]] Error Failure(const char* id, const char* message) noexcept
	{
		if (bPreparedErrors)
		{
			Error error;
			error.Code = EErrorCode::ExternalFailure;

			return error;
		}

		return MakeError(EErrorCode::ExternalFailure, StringView(id, CStringLength(id)),
			StringView(message, CStringLength(message)));
	}

	[[nodiscard]] Result<void> EnsureEmptyTextureHeap(State& state, Pending& pending)
	{
		// The tested D3D12 driver faults when a sampler table has no resource heap bound.
		if (pending.EmptyTextureHeap)
		{
			return Result<void>::Success();
		}

		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		if (FAILED(state.Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(pending.EmptyTextureHeap.GetAddressOf()))))
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Descriptors", "Could not allocate the empty resource descriptor heap."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] Result<void> CheckActive(const State& state)
	{
		if (state.bLost || state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL)
		{
			return Result<void>::Failure(Failure("Rhi.D3D12Lost", "The D3D12 copy queue is unavailable."));
		}

		return Result<void>::Success();
	}

	[[nodiscard]] EFormatSupport QueryFormatSupport(const State* opaque, EFormat format) noexcept
	{
		const State& state = *opaque;
		const DxgiFormatMapping& mapping = GetDxgiFormatMapping(format);

		if (mapping.RtvFormat == DXGI_FORMAT_UNKNOWN)
		{
			return EFormatSupport::None;
		}

		D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
		support.Format = mapping.RtvFormat;

		if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))))
		{
			support.Support1 = D3D12_FORMAT_SUPPORT1_NONE;
			support.Support2 = D3D12_FORMAT_SUPPORT2_NONE;
		}

		EFormatSupport result = EFormatSupport::None;

		if (support.Support1 & D3D12_FORMAT_SUPPORT1_BUFFER)
		{
			result = result | EFormatSupport::Buffer;
		}

		if (support.Support1 & (D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_TEXTURE3D |
			D3D12_FORMAT_SUPPORT1_TEXTURECUBE))
		{
			result = result | EFormatSupport::Texture;
		}

		if (support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL)
		{
			result = result | EFormatSupport::DepthStencil;
		}

		if (support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET)
		{
			result = result | EFormatSupport::RenderTarget;
		}

		if (support.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE)
		{
			result = result | EFormatSupport::Blendable;
		}

		if (mapping.SrvFormat != support.Format)
		{
			support = {};
			support.Format = mapping.SrvFormat;

			if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))))
			{
				support.Support1 = D3D12_FORMAT_SUPPORT1_NONE;
				support.Support2 = D3D12_FORMAT_SUPPORT2_NONE;
			}
		}

		if (support.Support1 & D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER)
		{
			result = result | EFormatSupport::IndexBuffer;
		}

		if (support.Support1 & D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER)
		{
			result = result | EFormatSupport::VertexBuffer;
		}

		if (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD)
		{
			result = result | EFormatSupport::ShaderLoad;
		}

		if (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)
		{
			result = result | EFormatSupport::ShaderSample | EFormatSupport::SamplerMinMax;
		}

		if (support.Support1 & D3D12_FORMAT_SUPPORT1_BUFFER)
		{
			if (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD)
			{
				result = result | EFormatSupport::BufferShaderRead;
			}

			if (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)
			{
				result = result | EFormatSupport::BufferShaderLoad;
			}

			if (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)
			{
				result = result | EFormatSupport::BufferShaderStore;
			}
		}

		if (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)
		{
			result = result | EFormatSupport::ShaderUavLoad;
		}

		if (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)
		{
			result = result | EFormatSupport::ShaderUavStore;
		}

		if (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD)
		{
			result = result | EFormatSupport::ShaderAtomic;
		}

		return result;
	}

	[[nodiscard]] bool CreateResource(State& state, uint64 byteCount, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES initial, ComPtr<ID3D12Resource>& output, D3D12_RESOURCE_FLAGS flags)
	{
		D3D12_HEAP_PROPERTIES properties{};
		properties.Type = heap;
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = byteCount;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = flags;

		return SUCCEEDED(state.Device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, initial, nullptr,
			IID_PPV_ARGS(output.ReleaseAndGetAddressOf())));
	}

	void InvalidateCommandState(void* native) noexcept
	{
		Pending& pending = *static_cast<Pending*>(native);
		pending.InvalidatePreparedGraphics();
		pending.BoundTextureHeap = nullptr;
		pending.BoundSamplerHeap = nullptr;
		pending.LastMergedSampledTable = nullptr;
		pending.LastMergedStorageTable = nullptr;
		pending.LastMergedStorageTextureTable = nullptr;
		pending.LastMergedUniformTable = nullptr;
		pending.LastMergedResourceHeap = nullptr;
		pending.LastComputePipeline = nullptr;
		pending.LastComputeSampledTable = nullptr;
		pending.LastComputeSamplerTable = nullptr;
		pending.LastComputeStorageTable = nullptr;
		pending.LastComputeStorageTextureTable = nullptr;
		pending.LastComputeUniformTable = nullptr;
		pending.LastComputeTextureCount = 0;
		pending.LastComputeResourceHeap = nullptr;
		pending.LastComputeSamplerHeap = nullptr;
		pending.LastComputeGlobalResourceFirst = UINT32_MAX;
		pending.LastComputeGlobalSamplerFirst = UINT32_MAX;
		pending.LastFixedComputePipeline = nullptr;
		pending.LastPackedPipeline = nullptr;
		for (SharedPtr<PreparedBindingCache>& owner : pending.LastPackedSets)
		{
			owner.Reset();
		}
		pending.LastRangePipeline = nullptr;
		for (SharedPtr<PreparedBindingCache>& owner : pending.LastRangeSets)
		{
			owner.Reset();
		}
		pending.LastCombinedPipeline = nullptr;
		pending.LastCombinedSampledTable = nullptr;
		pending.LastCombinedSamplerTable = nullptr;
		pending.LastCombinedStorageTable = nullptr;
		pending.LastCombinedStorageTextureTable = nullptr;
		pending.LastCombinedUniformTable = nullptr;
		pending.LastCombinedTextureGpu = 0;
		pending.LastCombinedSamplerGpu = 0;
		pending.LastCombinedResourceHeap = nullptr;
		pending.LastCombinedSamplerHeap = nullptr;
		pending.LastTexturePipeline = nullptr;
		pending.LastTextureCount = 0;
		pending.LastStorageTextureCount = 0;
		pending.LastTextureHeap = nullptr;
		pending.LastSamplerHeap = nullptr;
		pending.bShadingRateSet = false;
	}

	void SetAutomaticBarriers(void* native, bool bEnabled) noexcept
	{
		static_cast<Pending*>(native)->bAutomaticBarriers = bEnabled;
	}

	[[nodiscard]] Result<void> SetBufferUavBarriers(void* native, void* bufferNative, bool bEnabled)
	{
		Buffer& buffer = *static_cast<Buffer*>(bufferNative);

		return SetUavBarrierPolicy(*static_cast<Pending*>(native), *buffer.Resource.Get(), bEnabled);
	}

	[[nodiscard]] Result<void> SetTextureUavBarriers(void* native, void* textureNative, bool bEnabled)
	{
		Texture& texture = *static_cast<Texture*>(textureNative);

		return SetUavBarrierPolicy(*static_cast<Pending*>(native), *texture.Resource.Get(), bEnabled);
	}

	[[nodiscard]] Result<Pending> Begin(State& state, EQueue queue)
	{
		Collect(&state);
		Pending pending(state.Resource);
		pending.Queue = queue;
		pending.Aftermath.Diagnostics = state.Diagnostics;
		Array<Pending>& recycled = queue == EQueue::Compute ? state.RecycledCompute : queue == EQueue::Copy ? state.RecycledCopy : state.RecycledDirect;
		// Reserve optional reuse storage while starting work; completion never allocates.
		usize recycleCapacity = 0;

		if (AddMemorySize(recycled.Size(), state.InFlight.Size(), &recycleCapacity) && AddMemorySize(recycleCapacity, 1, &recycleCapacity))
		{
			(void)recycled.Reserve(recycleCapacity);
		}

		if (!recycled.IsEmpty())
		{
			pending = Move(recycled.Back());
			recycled.PopBack();
			pending.Queue = queue;
			pending.Aftermath.Diagnostics = state.Diagnostics;
			pending.PreparedAccelBuilds.Clear();
			pending.PreparedAccelQueries.Clear();
			pending.PreparedAccelCompacts.Clear();
			pending.PreparedBindings.Clear();
			pending.PreparedIndirects.Clear();
			pending.bPrepared = false;
			pending.bPreparedFinished = false;
			pending.bSubmissionCredit = false;
			pending.TextureHeaps.Clear();
			pending.SamplerHeaps.Clear();
			pending.BindlessHeaps.Clear();
			pending.TextureHeapUsed = 0;
			pending.SamplerHeapUsed = 0;
			InvalidateCommandState(&pending);
			pending.ExternalFence.Reset();
			pending.ExternalFenceValue = 0;
			pending.bExternalAcquire = false;
			pending.bExternalTexture = false;
			pending.Statistics = {};
			pending.ResourceStates.Clear();
			pending.ResourceStateLookup.Clear();
			pending.UavPolicies.Clear();
			pending.bInvalidResourceState = false;
			pending.bMissingManualState = false;
			pending.bAutomaticBarriers = true;
			pending.bExplicitStateRequest = false;

			if (FAILED(pending.Allocator->Reset()) || FAILED(pending.List->Reset(pending.Allocator.Get(), nullptr)))
			{
				state.bLost = true;

				return Result<Pending>::Failure(Failure("Rhi.D3D12Commands", "Could not reset D3D12 copy commands."));
			}
		}
		else if (FAILED(state.Device->CreateCommandAllocator(queue == EQueue::Compute ? D3D12_COMMAND_LIST_TYPE_COMPUTE : queue == EQueue::Copy ? D3D12_COMMAND_LIST_TYPE_COPY : D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(pending.Allocator.GetAddressOf()))) ||
			FAILED(state.Device->CreateCommandList(0, queue == EQueue::Compute ? D3D12_COMMAND_LIST_TYPE_COMPUTE : queue == EQueue::Copy ? D3D12_COMMAND_LIST_TYPE_COPY : D3D12_COMMAND_LIST_TYPE_DIRECT, pending.Allocator.Get(), nullptr, IID_PPV_ARGS(pending.List.GetAddressOf()))))
		{
			return Result<Pending>::Failure(Failure("Rhi.D3D12Commands", "Could not create D3D12 copy commands."));
		}

		if (queue == EQueue::Graphics && state.SupportedShadingRates[0] != 0 && !pending.ShadingRateList && FAILED(pending.List.As(&pending.ShadingRateList)))
		{
			return Result<Pending>::Failure(Failure("Rhi.D3D12Commands", "The VRS command list interface is unavailable."));
		}

		if (queue == EQueue::Graphics && state.bMeshShader && !pending.MeshList && FAILED(pending.List.As(&pending.MeshList)))
		{
			return Result<Pending>::Failure(Failure("Rhi.D3D12Commands", "The mesh command list interface is unavailable."));
		}

		return Result<Pending>::Success(Move(pending));
	}

	[[nodiscard]] Result<uint64> Execute(State& state, Pending&& pending, bool bIndependent, const NativePreparedSubmissionDesc* preparation, uint32* queueWaitCount)
	{
		if (state.LastSerial >= MAXIMUM_SUBMISSION_SERIAL)
		{
			return Result<uint64>::Failure(Failure("Rhi.D3D12Submit", "The D3D12 submission serial is exhausted."));
		}

		if (pending.bInvalidResourceState)
		{
			return Result<uint64>::Failure(
				NativeError(pending.bMissingManualState ? EErrorCode::Conflict : EErrorCode::ExternalFailure, "Rhi.D3D12State", "The command list contains incompatible resource transitions or tracking failed."));
		}

		if (!pending.bSubmissionCredit &&
			(state.PreparedCredits >= NumericLimits<usize>::max() - state.InFlight.Size() ||
			 ! state.Submissions.TryReserveOne() ||
			 !state.InFlight.Reserve(state.InFlight.Size() + state.PreparedCredits + 1)))
		{
			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not reserve D3D12 submission storage."));
		}
		InvalidateIdle(state);

		if (!pending.bPreparedFinished)
		{
			RestoreTrackedStates(pending);
		}

		const HRESULT closeResult = pending.bPreparedFinished ? S_OK : pending.List->Close();
		if (FAILED(closeResult))
		{
			state.LastSubmissionNativeCode = static_cast<int32>(closeResult);
			state.LastSubmissionFailureStage = 1;
			return Result<uint64>::Failure(Failure("Rhi.D3D12Commands", "Could not close D3D12 copy commands."));
		}

		pending.Serial = state.LastSerial + 1;

		if (pending.bSubmissionCredit)
		{
			--state.PreparedCredits;
			state.Submissions.ReleaseCredit();
			pending.bSubmissionCredit = false;
		}

		if (!state.InFlight.Add(Move(pending)))
		{
			return Result<uint64>::Failure(Failure("Rhi.Allocation", "Could not track submitted D3D12 work."));
		}

		const EQueue submissionQueue = state.InFlight.Back().Queue;
		ID3D12CommandQueue* const queue = NativeQueue(state, submissionQueue);

		if (!bIndependent)
		{
			const EQueue queues[3]{ EQueue::Graphics, EQueue::Compute, EQueue::Copy };
			for (EQueue other : queues)
			{
				const uint64 serial = QueueLastSerial(state, other);

				if (other != submissionQueue && serial != 0 && queueWaitCount != nullptr)
				{
					++*queueWaitCount;
				}

				if (other != submissionQueue && serial != 0)
				{
					state.bSubmissionMayBePending = true;
					const HRESULT waitResult = queue->Wait(QueueFence(state, other), serial);
					if (FAILED(waitResult))
					{
						state.LastSubmissionNativeCode = static_cast<int32>(waitResult);
						state.LastSubmissionFailureStage = 2;
						state.bLost = true;

						return Result<uint64>::Failure(Failure("Rhi.D3D12Submit", "Could not order the D3D12 native queues."));
					}
				}
			}
		}

		if (preparation != nullptr)
		{
			for (const rhi::NativePreparedSubmissionWait& wait : preparation->Waits)
			{
				if (wait.Serial == 0 || wait.Queue == submissionQueue)
				{
					continue;
				}

				if (queueWaitCount != nullptr)
				{
					++*queueWaitCount;
				}

				state.bSubmissionMayBePending = true;
				const HRESULT waitResult = queue->Wait(QueueFence(state, wait.Queue), wait.Serial);
				if (FAILED(waitResult))
				{
					state.LastSubmissionNativeCode = static_cast<int32>(waitResult);
					state.LastSubmissionFailureStage = 2;
					state.bLost = true;

					return Result<uint64>::Failure(Failure("Rhi.D3D12Submit", "Could not enqueue a prepared dependency wait."));
				}
			}
		}

		const Pending& external = state.InFlight.Back();

		if (external.ExternalFence && external.bExternalAcquire)
		{
			state.bSubmissionMayBePending = true;
			const HRESULT waitResult = queue->Wait(external.ExternalFence.Get(), external.ExternalFenceValue);
			if (FAILED(waitResult))
			{
				state.LastSubmissionNativeCode = static_cast<int32>(waitResult);
				state.LastSubmissionFailureStage = 2;
				state.bLost = true;

				return Result<uint64>::Failure(Failure("Rhi.ExternalFence", "Could not enqueue the external fence wait."));
			}
		}

		ID3D12CommandList* lists[]{ state.InFlight.Back().List.Get() };
		state.bSubmissionMayBePending = true;
		queue->ExecuteCommandLists(1, lists);

		if (external.ExternalFence && !external.bExternalAcquire)
		{
			const HRESULT externalResult = queue->Signal(external.ExternalFence.Get(), external.ExternalFenceValue);
			if (FAILED(externalResult))
			{
				state.LastSubmissionNativeCode = static_cast<int32>(externalResult);
				state.LastSubmissionFailureStage = 4;
				state.bLost = true;

				return Result<uint64>::Failure(Failure("Rhi.ExternalFence", "Could not enqueue the external fence signal."));
			}
		}

		const HRESULT signalResult = queue->Signal(QueueFence(state, state.InFlight.Back().Queue), state.InFlight.Back().Serial);
		if (FAILED(signalResult))
		{
			state.LastSubmissionNativeCode = static_cast<int32>(signalResult);
			state.LastSubmissionFailureStage = 4;
			state.bLost = true;
			return Result<uint64>::Failure(Failure("Rhi.D3D12Submit", "Could not signal the D3D12 copy fence."));
		}

		state.LastSerial = state.InFlight.Back().Serial;
		state.Submissions.RecordSubmission(state.LastSerial, submissionQueue == EQueue::Compute ? SubmissionCompletion::ELane::Compute
			: submissionQueue == EQueue::Copy ? SubmissionCompletion::ELane::Copy
			: SubmissionCompletion::ELane::Direct);

		if (submissionQueue == EQueue::Compute)
		{
			state.LastComputeSerial = state.LastSerial;
		}
		else if (submissionQueue == EQueue::Copy)
		{
			state.LastCopySerial = state.LastSerial;
		}
		else
		{
			state.LastDirectSerial = state.LastSerial;
		}

		return Result<uint64>::Success(state.LastSerial);
	}

	[[nodiscard]] uint32 MemoryBudgetIndex(const State& state, EBufferMemory memory) noexcept
	{
		const D3D12_HEAP_TYPE type = memory == EBufferMemory::CpuWrite ? D3D12_HEAP_TYPE_UPLOAD : memory == EBufferMemory::CpuRead ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT;
		const D3D12_HEAP_PROPERTIES properties = state.Device->GetCustomHeapProperties(0, type);

		if (properties.MemoryPoolPreference == D3D12_MEMORY_POOL_L1)
		{
			return 0;
		}

		D3D12_FEATURE_DATA_ARCHITECTURE architecture{};

		if (properties.MemoryPoolPreference != D3D12_MEMORY_POOL_L0 || FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture))))
		{
			return UINT32_MAX;
		}

		return architecture.UMA ? 0u : 1u;
	}

	[[nodiscard]] uint64 GetDescriptorAccelerationStructure(const void* native) noexcept
	{
		return static_cast<const NativeAccelStruct*>(native)->Address;
	}

	[[nodiscard]] bool EnableRayDevice(State& state) noexcept
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
		if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))) || options.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
		{
			return false;
		}

		return SUCCEEDED(state.Device.As(&state.RayDevice));
	}

	[[nodiscard]] bool SupportsRayQuery(State& state) noexcept
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};

		return state.RayDevice && SUCCEEDED(state.Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))) && options.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
	}

	[[nodiscard]] bool SupportsMeshShader(State& state) noexcept
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 options{};
		if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options, sizeof(options))) || options.MeshShaderTier < D3D12_MESH_SHADER_TIER_1)
		{
			return false;
		}
		D3D12_FEATURE_DATA_SHADER_MODEL model{ D3D_SHADER_MODEL_6_5 };
		if (FAILED(state.Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) || model.HighestShaderModel < D3D_SHADER_MODEL_6_5)
		{
			return false;
		}

		ComPtr<ID3D12Device2> meshDevice;

		return SUCCEEDED(state.Device.As(&meshDevice));
	}

	[[nodiscard]] Result<void*> CreateCommandList(State* opaque, EQueue queue)
	{
		State& state = *opaque;
		Result<void> active = CheckActive(state);

		if (!active)
		{
			return Result<void*>::Failure(active.GetError());
		}

		Result<Pending> started = Begin(state, queue);

		if (!started)
		{
			return Result<void*>::Failure(started.GetError());
		}

		Pending* pending = CreateObject<Pending>(state.Resource, Move(started).TakeValue());

		if (pending == nullptr)
		{
			return Result<void*>::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 command state."));
		}

		pending->Queue = queue;

		return Result<void*>::Success(pending);
	}

	void* GetNativeCommandList(const void* native) noexcept
	{
		return static_cast<const Pending*>(native)->List.Get();
	}

	[[nodiscard]] HRESULT SerializeDirectRootSignature(const D3D12_ROOT_SIGNATURE_DESC& source, ID3DBlob** serialized, ID3DBlob** errors) noexcept
	{
		ASSERT(source.NumParameters <= 136, "Too many root parameters for direct indexing.");
		D3D12_ROOT_PARAMETER1 parameters[136]{};
		D3D12_DESCRIPTOR_RANGE1 ranges[136]{};
		for (UINT index = 0; index < source.NumParameters; ++index)
		{
			const D3D12_ROOT_PARAMETER& current = source.pParameters[index];
			D3D12_ROOT_PARAMETER1& target = parameters[index];
			target.ParameterType = current.ParameterType;
			target.ShaderVisibility = current.ShaderVisibility;

			if (current.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
			{
				ASSERT(current.DescriptorTable.NumDescriptorRanges == 1, "Each descriptor table has one range.");
				const D3D12_DESCRIPTOR_RANGE& range = current.DescriptorTable.pDescriptorRanges[0];
				ranges[index] = { range.RangeType,
								 range.NumDescriptors,
								 range.BaseShaderRegister,
								 range.RegisterSpace,
								 D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
									 (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER ? D3D12_DESCRIPTOR_RANGE_FLAG_NONE
																							 : D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE),
								 range.OffsetInDescriptorsFromTableStart };
				target.DescriptorTable = { 1, &ranges[index] };
			}
			else if (current.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
			{
				target.Constants = current.Constants;
			}
			else
			{
				target.Descriptor = { current.Descriptor.ShaderRegister, current.Descriptor.RegisterSpace,
									 D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE };
			}
		}

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned{};
		versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		versioned.Desc_1_1 = { source.NumParameters, parameters, source.NumStaticSamplers, source.pStaticSamplers,
							  source.Flags | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
								  D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED };

		return D3D12SerializeVersionedRootSignature(&versioned, serialized, errors);
	}

	void DiscardCommandList(State* opaque, void* native) noexcept
	{
		State& state = *opaque;
		Pending& pending = *static_cast<Pending*>(native);
		if (pending.bSubmissionCredit)
		{
			--state.PreparedCredits;
			state.Submissions.ReleaseCredit();
			pending.bSubmissionCredit = false;
		}

		RetireAftermathMarkers(pending);
		ReleasePendingGlobalDescriptors(state, pending);
		DestroyObject(state.Resource, &pending);
	}

	void Destroy(State* opaque) noexcept
	{
		State& state = *opaque;
		IGpuCrashDiagnostics* const diagnostics = state.Diagnostics;
		const bool bAttached = state.bAftermathAttached;

		if (bAttached && state.bLost)
		{
			NotifyAftermathDeviceLost(diagnostics);
		}

		state.InFlight.Clear();
		state.RecycledDirect.Clear();
		state.RecycledCompute.Clear();
		state.RecycledCopy.Clear();

		for (const Retired& retired : state.RetiredBuffers)
		{
			DestroyObject(state.Resource, retired.Item);
		}

		for (const RetiredTexture& retired : state.RetiredTextures)
		{
			DestroyObject(state.Resource, retired.Item);
		}

		for (const RetiredPipeline& retired : state.RetiredPipelines)
		{
			DestroyObject(state.Resource, retired.Item);
		}

		for (const RetiredGraphicsPipeline& retired : state.RetiredGraphicsPipelines)
		{
			DestroyObject(state.Resource, retired.Item);
		}

		for (const RetiredRayPipeline& retired : state.RetiredRayPipelines)
		{
			DestroyObject(state.Resource, retired.Item);
		}

		for (const RetiredFramebuffer& retired : state.RetiredFramebuffers)
		{
			DestroyObject(state.Resource, retired.Item);
		}

		if (state.Event)
		{
			CloseHandle(state.Event);
		}

		DestroyObject(state.Resource, &state);

		if (bAttached)
		{
			DetachAftermathDevice(diagnostics);
		}
	}

	[[nodiscard]] CommandListStatistics GetCommandListStatistics(const void* native) noexcept
	{
		return static_cast<const Pending*>(native)->Statistics;
	}

	[[nodiscard]] bool HasMissingManualState(const void* native) noexcept
	{
		return static_cast<const Pending*>(native)->bMissingManualState;
	}

	[[nodiscard]] DeviceLimits GetLimits(const State* opaque) noexcept
	{
		const State& state = *opaque;
		DeviceLimits result{};
		result.MaximumComputeWorkGroupSize[0] = D3D12_CS_THREAD_GROUP_MAX_X;
		result.MaximumComputeWorkGroupSize[1] = D3D12_CS_THREAD_GROUP_MAX_Y;
		result.MaximumComputeWorkGroupSize[2] = D3D12_CS_THREAD_GROUP_MAX_Z;
		result.MaximumComputeWorkGroupInvocations = D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP;

		for (uint32& count : result.MaximumComputeWorkGroupCount)
		{
			count = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
		}

		D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT addresses{};
		if (SUCCEEDED(state.Device->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &addresses, sizeof(addresses))) && addresses.MaxGPUVirtualAddressBitsPerResource < 64)
		{
			const uint64 maximum = uint64{ 1 } << addresses.MaxGPUVirtualAddressBitsPerResource;
			result.MaximumBufferByteCount = Min(result.MaximumBufferByteCount, maximum - 1);
			result.MaximumTextureByteCount = Min(result.MaximumTextureByteCount, maximum - 1);
		}

		return result;
	}

	Result<BackendData> CreateD3D12Backend(
		MemoryResource& resource,
		bool bEnableValidation,
		bool bEnableDeviceHeapIndexing,
		EAdapterPreference preference,
		NativeDeviceHandles externalDevice,
		NativeQueueHandle externalComputeQueue,
		NativeQueueHandle externalCopyQueue,
		IGpuCrashDiagnostics* diagnostics)
	{
		if (diagnostics != nullptr && externalDevice.Device != nullptr)
		{
			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.AftermathExternalDevice", "GPU crash diagnostics require an internally created D3D12 device.");
		}

		State* state = CreateObject<State>(resource, resource);

		if (state == nullptr)
		{
			return Result<BackendData>::Failure(Failure("Rhi.Allocation", "Could not allocate D3D12 state."));
		}

		UINT factoryFlags = 0;

		if (bEnableValidation)
		{
			ComPtr<ID3D12Debug> debug;

			if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()))))
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Validation", "The requested D3D12 debug layer is unavailable.");
			}

			debug->EnableDebugLayer();
			factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
		}

		ComPtr<IDXGIFactory6> factory;
		ComPtr<IDXGIAdapter1> adapter;
		bool bReady = SUCCEEDED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(factory.GetAddressOf())));

		if (!bReady && bEnableValidation)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Validation", "The requested DXGI debug factory is unavailable.");
		}

		if (bReady && externalDevice.Device != nullptr)
		{
			state->bNativeQueuesExposed = true;
			state->bIdleConfirmed = false;
			state->Device = static_cast<ID3D12Device*>(externalDevice.Device);
			state->Queue = static_cast<ID3D12CommandQueue*>(externalDevice.Queue);

			if (externalComputeQueue.Handle != nullptr)
			{
				state->ComputeQueue = static_cast<ID3D12CommandQueue*>(externalComputeQueue.Handle);
			}

			if (externalCopyQueue.Handle != nullptr)
			{
				state->CopyQueue = static_cast<ID3D12CommandQueue*>(externalCopyQueue.Handle);
			}

			ComPtr<ID3D12Device> queueDevice;
			ComPtr<ID3D12Device> computeQueueDevice;
			ComPtr<ID3D12Device> copyQueueDevice;
			ComPtr<IUnknown> deviceIdentity;
			ComPtr<IUnknown> queueIdentity;
			ComPtr<IUnknown> computeQueueIdentity;
			ComPtr<IUnknown> copyQueueIdentity;
			bReady = state->Queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT &&
				SUCCEEDED(state->Queue->GetDevice(IID_PPV_ARGS(queueDevice.GetAddressOf()))) &&
				SUCCEEDED(state->Device.As(&deviceIdentity)) && SUCCEEDED(queueDevice.As(&queueIdentity)) &&
				deviceIdentity.Get() == queueIdentity.Get() &&
				(state->ComputeQueue == nullptr ||
					(state->ComputeQueue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_COMPUTE &&
						SUCCEEDED(state->ComputeQueue->GetDevice(IID_PPV_ARGS(computeQueueDevice.GetAddressOf()))) &&
						SUCCEEDED(computeQueueDevice.As(&computeQueueIdentity)) && deviceIdentity.Get() == computeQueueIdentity.Get())) &&
				(state->CopyQueue == nullptr ||
					(state->CopyQueue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_COPY &&
						SUCCEEDED(state->CopyQueue->GetDevice(IID_PPV_ARGS(copyQueueDevice.GetAddressOf()))) &&
						SUCCEEDED(copyQueueDevice.As(&copyQueueIdentity)) && deviceIdentity.Get() == copyQueueIdentity.Get())) &&
				SUCCEEDED(factory->EnumAdapterByLuid(state->Device->GetAdapterLuid(), IID_PPV_ARGS(adapter.GetAddressOf())));

			if (!bReady)
			{
				Destroy(state);

				return Result<BackendData>::Failure(EErrorCode::InvalidArgument, "Rhi.D3D12ExternalDevice", "The external D3D12 device and direct queue must match an available adapter.");
			}
		}
		else if (bReady)
		{
			const DXGI_GPU_PREFERENCE gpuPreference = preference == EAdapterPreference::MinimumPower ? DXGI_GPU_PREFERENCE_MINIMUM_POWER : DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;

			for (UINT index = 0; preference != EAdapterPreference::Software; ++index)
			{
				ComPtr<IDXGIAdapter1> candidate;
				const HRESULT enumerated = factory->EnumAdapterByGpuPreference(index, gpuPreference, IID_PPV_ARGS(candidate.GetAddressOf()));

				if (enumerated == DXGI_ERROR_NOT_FOUND || FAILED(enumerated))
				{
					break;
				}

				if (!candidate)
				{
					continue;
				}

				DXGI_ADAPTER_DESC1 description{};
				if (FAILED(candidate->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
				{
					continue;
				}

				if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(state->Device.GetAddressOf()))))
				{
					adapter = Move(candidate);
					break;
				}
			}

			if (!state->Device && (preference == EAdapterPreference::Default || preference == EAdapterPreference::Software) && SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(adapter.GetAddressOf()))))
			{
				bReady = SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(state->Device.GetAddressOf())));
			}
			else
			{
				bReady = state->Device != nullptr;
			}
		}

		if (!bReady && preference != EAdapterPreference::Default)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.D3D12Adapter", "No D3D12 adapter matches the requested preference.");
		}

		if (bReady)
		{
			D3D12_COMMAND_QUEUE_DESC queue{};
			queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			bReady = (externalDevice.Device != nullptr ||
				SUCCEEDED(state->Device->CreateCommandQueue(&queue, IID_PPV_ARGS(state->Queue.GetAddressOf())))) &&
				SUCCEEDED(state->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(state->Fence.GetAddressOf())));
			queue.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			bReady = bReady &&
				(state->ComputeQueue != nullptr ||
					SUCCEEDED(state->Device->CreateCommandQueue(&queue, IID_PPV_ARGS(state->ComputeQueue.GetAddressOf())))) &&
				SUCCEEDED(state->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(state->ComputeFence.GetAddressOf())));
			queue.Type = D3D12_COMMAND_LIST_TYPE_COPY;
			bReady = bReady &&
				(state->CopyQueue != nullptr ||
					SUCCEEDED(state->Device->CreateCommandQueue(&queue, IID_PPV_ARGS(state->CopyQueue.GetAddressOf())))) &&
				SUCCEEDED(state->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(state->CopyFence.GetAddressOf())));
			state->Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			bReady = bReady && state->Event != nullptr;
			const D3D12_INDIRECT_ARGUMENT_TYPE types[3]{ D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW,
														D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED };
			const UINT strides[3]{ sizeof(IndirectDispatchArguments), sizeof(IndirectDrawArguments), sizeof(IndirectDrawIndexedArguments) };
			ID3D12CommandSignature** const signatures[3]{ state->DispatchSignature.GetAddressOf(), state->DrawSignature.GetAddressOf(),
														 state->DrawIndexedSignature.GetAddressOf() };
			for (uint32 index = 0; bReady && index < 3; ++index)
			{
				D3D12_INDIRECT_ARGUMENT_DESC argument{};
				argument.Type = types[index];
				D3D12_COMMAND_SIGNATURE_DESC signature{};
				signature.ByteStride = strides[index];
				signature.NumArgumentDescs = 1;
				signature.pArgumentDescs = &argument;
				bReady = SUCCEEDED(state->Device->CreateCommandSignature(&signature, nullptr, __uuidof(ID3D12CommandSignature), reinterpret_cast<void**>(signatures[index])));
			}
		}

		if (!bReady)
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.D3D12Device", "Could not create a D3D12 copy device."));
		}

		DXGI_ADAPTER_DESC1 adapterInfo{};
		(void)adapter.As(&state->BudgetAdapter);

		if (FAILED(adapter->GetDesc1(&adapterInfo)))
		{
			Destroy(state);

			return Result<BackendData>::Failure(Failure("Rhi.D3D12Adapter", "Could not read the selected D3D12 adapter."));
		}

		if (diagnostics != nullptr && adapterInfo.VendorId != 0x10DE)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.AftermathAdapter", "Nsight Aftermath requires an NVIDIA hardware adapter.");
		}

		if (diagnostics != nullptr)
		{
			const Result<void> attached = AttachAftermathD3D12(diagnostics, state->Device.Get());

			if (!attached)
			{
				Destroy(state);

				return Result<BackendData>::Failure(attached.GetError());
			}

			state->Diagnostics = diagnostics;
			state->bAftermathAttached = true;
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
		const bool bOptionsAvailable = SUCCEEDED(state->Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)));
		const bool bConservativeRaster = bOptionsAvailable && options.ConservativeRasterizationTier != D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED;
		const bool bSparseTextures = bOptionsAvailable && options.TiledResourcesTier != D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
		D3D12_FEATURE_DATA_D3D12_OPTIONS1 waveOptions{};
		const bool bOptions1Available = SUCCEEDED(state->Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &waveOptions, sizeof(waveOptions)));
		const bool bWaveSupported = bOptions1Available && waveOptions.WaveOps && waveOptions.WaveLaneCountMin != 0 && waveOptions.WaveLaneCountMax >= waveOptions.WaveLaneCountMin;
		D3D12_FEATURE_DATA_D3D12_OPTIONS2 boundsOptions{};
		ComPtr<ID3D12Device2> boundsDevice;
		const bool bDepthBounds =
			SUCCEEDED(state->Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &boundsOptions, sizeof(boundsOptions))) &&
			boundsOptions.DepthBoundsTestSupported &&
			SUCCEEDED(state->Device.As(&boundsDevice));
		D3D12_FEATURE_DATA_D3D12_OPTIONS4 native16Options{};
		const bool bShaderFloat16 =
			SUCCEEDED(state->Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &native16Options, sizeof(native16Options))) &&
			native16Options.Native16BitShaderOpsSupported != FALSE;
		const uint32 shadingRateTile = QuerySupportedShadingRates(*state);
		const bool bAccelerationInputs = EnableRayDevice(*state);
#if ROXELL_RHI_WITH_NVAPI
		if (state->NvApi.Acquire() && bAccelerationInputs)
		{
			NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAPS omm{};
			NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAPS cluster{};
			state->bOpacityMicromap =
				NvAPI_D3D12_GetRaytracingCaps(state->RayDevice.Get(), NVAPI_D3D12_RAYTRACING_CAPS_TYPE_OPACITY_MICROMAP, &omm,
					sizeof(omm)) == NVAPI_OK &&
				(omm & NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_STANDARD) != 0;
			state->bRayTracingClusters =
				NvAPI_D3D12_GetRaytracingCaps(state->RayDevice.Get(), NVAPI_D3D12_RAYTRACING_CAPS_TYPE_CLUSTER_OPERATIONS, &cluster,
					sizeof(cluster)) == NVAPI_OK &&
				(cluster & NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAP_STANDARD) != 0;
			NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS options{};
			options.version = NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER;
			options.flags = (state->bOpacityMicromap ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_OMM_SUPPORT : 0) |
				(state->bRayTracingClusters ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_CLUSTER_SUPPORT : 0);

			if (options.flags != 0 && NvAPI_D3D12_SetCreatePipelineStateOptions(state->RayDevice.Get(), &options) != NVAPI_OK)
			{
				state->bOpacityMicromap = false;
				state->bRayTracingClusters = false;
			}
		}
#endif
		const Result<void> cooperative = InitCoopVec(*state);

		if (!cooperative)
		{
			Destroy(state);

			return Result<BackendData>::Failure(cooperative.GetError());
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS7 feedbackOptions{};
		D3D12_FEATURE_DATA_SHADER_MODEL feedbackShaderModel{ D3D_SHADER_MODEL_6_5 };
		if (SUCCEEDED(state->Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &feedbackOptions, sizeof(feedbackOptions))) &&
			feedbackOptions.SamplerFeedbackTier != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED &&
			SUCCEEDED(state->Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &feedbackShaderModel, sizeof(feedbackShaderModel))) &&
			feedbackShaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_5)
		{
			(void)state->Device.As(&state->FeedbackDevice);
		}

		D3D12_FEATURE_DATA_SHADER_MODEL directShaderModel{ D3D_SHADER_MODEL_6_6 };
		D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignature{ D3D_ROOT_SIGNATURE_VERSION_1_1 };
		state->bHeapDirectlyIndexed =
			bOptionsAvailable && options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 &&
			SUCCEEDED(state->Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &directShaderModel, sizeof(directShaderModel))) &&
			directShaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_6 &&
			SUCCEEDED(state->Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rootSignature, sizeof(rootSignature))) &&
			rootSignature.HighestVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1;

		if (bEnableDeviceHeapIndexing && !state->bHeapDirectlyIndexed)
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::Unsupported, "Rhi.D3D12DescriptorHeap", "The D3D12 device does not support direct descriptor heap indexing.");
		}

		state->bDeviceHeapIndexingEnabled = bEnableDeviceHeapIndexing;
		if (bEnableDeviceHeapIndexing && !InitializeGlobalDescriptorHeaps(*state))
		{
			Destroy(state);

			return Result<BackendData>::Failure(EErrorCode::ExternalFailure, "Rhi.D3D12DescriptorHeap", "Could not create the requested device descriptor heaps.");
		}

		state->bMeshShader = SupportsMeshShader(*state);

		if (state->bMeshShader)
		{
			D3D12_INDIRECT_ARGUMENT_DESC argument{};
			argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
			D3D12_COMMAND_SIGNATURE_DESC signature{};
			signature.ByteStride = sizeof(IndirectMeshArguments);
			signature.NumArgumentDescs = 1;
			signature.pArgumentDescs = &argument;
			state->bMeshIndirect = SUCCEEDED(state->Device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(state->MeshSignature.GetAddressOf())));
		}

		const uint32 boundedTableCapacity = options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2 ? 1024u : 0u;
		// Tier 2+ SRV tables can address the full resource heap already allocated by this backend.
		state->MaximumBindlessSampledTextures = boundedTableCapacity != 0 ? GLOBAL_RESOURCE_DESCRIPTOR_COUNT : 0u;
		state->MaximumBindlessSamplers = boundedTableCapacity;
		state->MaximumBindlessStorageBuffers = boundedTableCapacity;
		state->MaximumBindlessStorageTextures = boundedTableCapacity;
		state->MaximumBindlessUniformBuffers = boundedTableCapacity;
		uint64 graphicsFrequency = 0;
		uint64 computeFrequency = 0;
		uint64 copyFrequency = 0;
		const bool bGraphicsTimestamps = SUCCEEDED(state->Queue->GetTimestampFrequency(&graphicsFrequency)) && graphicsFrequency != 0;
		const bool bComputeTimestamps = SUCCEEDED(state->ComputeQueue->GetTimestampFrequency(&computeFrequency)) && computeFrequency != 0;
		D3D12_FEATURE_DATA_D3D12_OPTIONS3 timestampOptions{};
		const bool bCopyTimestamps =
			SUCCEEDED(state->Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &timestampOptions, sizeof(timestampOptions))) &&
			timestampOptions.CopyQueueTimestampQueriesSupported &&
			SUCCEEDED(state->CopyQueue->GetTimestampFrequency(&copyFrequency)) &&
			copyFrequency != 0;

		GpuCapabilities capabilities
		{};
		capabilities.Features.bGraphics = true;
		capabilities.Features.bCompute = true;
		capabilities.Features.bCopy = true;
		capabilities.Limits.MaximumBufferByteCount = MAXIMUM_BUFFER_BYTES;
		capabilities.Limits.MaximumColorAttachments = 8;
		capabilities.Features.bDrawIndexedIndirectCount = true;
		capabilities.Features.bDebugMarkers = true;
		capabilities.Limits.StorageBufferOffsetAlignmentBytes = 4;
		capabilities.Features.bGeometryShader = true;
		capabilities.Features.bTessellationShader = true;
		capabilities.Features.bWireframe = true;
		capabilities.Features.bDepthClamp = true;
		capabilities.Features.bDepthBiasClamp = true;
		capabilities.Features.bConservativeRasterization = bConservativeRaster;
		capabilities.Limits.MaximumBindlessSampledTextures = 0;
		capabilities.Limits.MaximumBindlessSamplers = 0;
		capabilities.Limits.MaximumBindlessStorageBuffers = 0;
		capabilities.Limits.MaximumBindlessStorageTextures = 0;
		capabilities.Limits.MaximumBindlessUniformBuffers = 0;
		capabilities.Limits.UniformBufferOffsetAlignmentBytes = 256;
		capabilities.Limits.MaximumGraphicsBindlessStorageBuffers = 0;
		capabilities.Limits.MaximumGraphicsBindlessStorageTextures = 0;
		capabilities.Limits.MaximumViewports = 16;
		capabilities.Features.bDrawIndirectCount = true;
		capabilities.Limits.MaximumCountedIndirectDraws = 65535;
		capabilities.Limits.MinimumWaveLaneCount = bWaveSupported ? waveOptions.WaveLaneCountMin : 0u;
		capabilities.Limits.MaximumWaveLaneCount = bWaveSupported ? waveOptions.WaveLaneCountMax : 0u;
		capabilities.Limits.MaximumDualSourceColorAttachments = 1u;
		capabilities.Features.bBufferDeviceAddress = true;
		capabilities.Features.bShaderInt64 = bOptions1Available && waveOptions.Int64ShaderOps;
		capabilities.Adapter.VendorID = adapterInfo.VendorId;
		capabilities.Adapter.DeviceID = adapterInfo.DeviceId;
		capabilities.Adapter.bSoftwareAdapter = (adapterInfo.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
		capabilities.Features.bSamplerReductionMinMax = true;
		capabilities.Limits.SupportedShadingRates[0] = state->SupportedShadingRates[0];
		capabilities.Limits.SupportedShadingRates[1] = state->SupportedShadingRates[1];
		capabilities.Limits.SupportedShadingRates[2] = state->SupportedShadingRates[2];
		capabilities.Limits.SupportedShadingRates[3] = state->SupportedShadingRates[3];
		capabilities.Limits.ShadingRateImageTileWidth = shadingRateTile;
		capabilities.Limits.ShadingRateImageTileHeight = shadingRateTile;
		capabilities.Features.bShadingRateImage = shadingRateTile != 0;
		capabilities.Features.bPrimitiveShadingRate = shadingRateTile != 0;
		capabilities.Features.bShadingRateNonTrivialCombiners = true;
		capabilities.Features.bMeshShader = state->bMeshShader;
		capabilities.Features.bTaskShader = state->bMeshShader;
		capabilities.Limits.MaximumMeshGroups[0] = state->bMeshShader ? 65535u : 0u;
		capabilities.Limits.MaximumMeshGroups[1] = state->bMeshShader ? 65535u : 0u;
		capabilities.Limits.MaximumMeshGroups[2] = state->bMeshShader ? 65535u : 0u;
		capabilities.Limits.MaximumMeshGroupTotalCount = state->bMeshShader ? 1u << 22 : 0u;
		capabilities.Features.bMeshIndirect = state->bMeshIndirect;
		capabilities.Features.
			bSparseTextures = bSparseTextures;
		capabilities.Features.bBoundedPresentationWait = true;
		capabilities.Limits.MaximumBindlessReadOnlyBuffers = 0;
		capabilities.Features.bBindlessTypedBuffers = true;
		capabilities.Features.bShaderDrawParameters = true;
		capabilities.Features.bIndependentBlend = true;
		capabilities.Limits.MaximumTaskGroups[0] = state->bMeshShader ? 65535u : 0u;
		capabilities.Limits.MaximumTaskGroups[1] = state->bMeshShader ? 65535u : 0u;
		capabilities.Limits.MaximumTaskGroups[2] = state->bMeshShader ? 65535u : 0u;
		capabilities.Limits.MaximumTaskGroupTotalCount = state->bMeshShader ? 1u << 22 : 0u;
		capabilities.Limits.MaximumMeshWorkGroupSize[0] = state->bMeshShader ? 128u : 0u;
		capabilities.Limits.MaximumMeshWorkGroupSize[1] = state->bMeshShader ? 128u : 0u;
		capabilities.Limits.MaximumMeshWorkGroupSize[2] = state->bMeshShader ? 128u : 0u;
		capabilities.Limits.MaximumMeshWorkGroupInvocations = state->bMeshShader ? 128u : 0u;
		capabilities.Limits.MaximumMeshOutputVertices = state->bMeshShader ? 256u : 0u;
		capabilities.Limits.MaximumMeshOutputPrimitives = state->bMeshShader ? 256u : 0u;
		capabilities.Limits.MaximumMeshOutputComponents = state->bMeshShader ? 128u : 0u;
		capabilities.Limits.MaximumMeshOutputByteCount = state->bMeshShader ? 32768u : 0u;
		capabilities.Limits.MeshOutputPerVertexGranularity = state->bMeshShader ? 32u : 0u;
		capabilities.Limits.MeshOutputPerPrimitiveGranularity = state->bMeshShader ? 32u : 0u;
		capabilities.Features.bMultiDrawIndirect = true;
		capabilities.Features.bIndirectFirstInstance = true;
		capabilities.Limits.MaximumTaskWorkGroupSize[0] = state->bMeshShader ? 128u : 0u;
		capabilities.Limits.MaximumTaskWorkGroupSize[1] = state->bMeshShader ? 128u : 0u;
		capabilities.Limits.MaximumTaskWorkGroupSize[2] = state->bMeshShader ? 128u : 0u;
		capabilities.Limits.MaximumTaskWorkGroupInvocations = state->bMeshShader ? 128u : 0u;
		capabilities.Limits.MaximumTaskPayloadByteCount = state->bMeshShader ? 16384u : 0u;
		capabilities.Limits.MaximumTaskSharedByteCount = state->bMeshShader ? 32768u : 0u;
		capabilities.Limits.MaximumTaskPayloadAndSharedByteCount = state->bMeshShader ? 32768u : 0u;
		capabilities.Limits.MaximumMeshSharedByteCount = state->bMeshShader ? 28672u : 0u;
		capabilities.Limits.MaximumMeshPayloadAndSharedByteCount = state->bMeshShader ? 28672u : 0u;
		capabilities.Limits.MaximumMeshPayloadAndOutputByteCount = state->bMeshShader ? 48128u : 0u;
		capabilities.Features.bAccelerationStructure = bAccelerationInputs;
		capabilities.Features.bRayTracingPipeline = bAccelerationInputs;
		capabilities.Limits.MaximumAccelGeometryCount =
			bAccelerationInputs ? uint64{ D3D12_RAYTRACING_MAX_GEOMETRIES_PER_BOTTOM_LEVEL_ACCELERATION_STRUCTURE } : 0;
		capabilities.Limits.MaximumAccelPrimitiveCount =
			bAccelerationInputs ? uint64{ D3D12_RAYTRACING_MAX_PRIMITIVES_PER_BOTTOM_LEVEL_ACCELERATION_STRUCTURE } : 0;
		capabilities.Limits.MaximumAccelInstanceCount =
			bAccelerationInputs ? uint64{ D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE } : 0;
		capabilities.Limits.MaximumRayRecursionDepth = bAccelerationInputs ? D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH : 0u;
		capabilities.Limits.MaximumRayAttributeByteCount = bAccelerationInputs ? D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES : 0u;
		capabilities.Limits.MaximumRayDispatchInvocations =
			bAccelerationInputs ? uint64{ D3D12_RAYTRACING_MAX_RAY_GENERATION_SHADER_THREADS } : 0;
		capabilities.Limits.MaximumRayDispatchDimensions[0] = bAccelerationInputs ? uint64{ D3D12_RAYTRACING_MAX_RAY_GENERATION_SHADER_THREADS } : 0;
		capabilities.Limits.MaximumRayDispatchDimensions[1] =
			bAccelerationInputs ? uint64{ D3D12_RAYTRACING_MAX_RAY_GENERATION_SHADER_THREADS } : 0;
		capabilities.Limits.MaximumRayDispatchDimensions[2] =
			bAccelerationInputs ? uint64{ D3D12_RAYTRACING_MAX_RAY_GENERATION_SHADER_THREADS } : 0;
		capabilities.Features.bUniformBufferArrayDynamicIndexing = true;
		capabilities.Features.bSampledImageArrayDynamicIndexing = true;
		capabilities.Features.bStorageBufferArrayDynamicIndexing = true;
		capabilities.Features.bStorageImageArrayDynamicIndexing = true;
		capabilities.Limits.MaximumDescriptorTableAccelerationStructures = bAccelerationInputs ? boundedTableCapacity : 0u;
		capabilities.Features.bDeviceHeapIndexing = state->bDeviceHeapIndexingEnabled;
		capabilities.Features.bShaderDemoteToHelperInvocation = false;
		capabilities.Features.bSampleRateShading = true;
		capabilities.Features.bSeparateDepthStencilAttachmentAccess = true;
		capabilities.Features.bShaderLocalSizeId = false;
		capabilities.Features.bNonzeroMipStencilViews = true;
		capabilities.Features.bGraphicsTimestamps = bGraphicsTimestamps && bComputeTimestamps;
		capabilities.Features.bComputeTimestamps = bGraphicsTimestamps && bComputeTimestamps;
		capabilities.Features.bCopyTimestamps = bGraphicsTimestamps && bComputeTimestamps && bCopyTimestamps;
		capabilities.Features.
			bDepthBounds = bDepthBounds;
		capabilities.Features.bTypedUavLoadAdditionalFormats = bOptionsAvailable && options.TypedUAVLoadAdditionalFormats != FALSE;
		capabilities.Features.bShaderImageGatherExtended = false;
		capabilities.Features.bShaderStorageImageReadWithoutFormat = false;
		capabilities.Features.bShaderStorageImageWriteWithoutFormat = false;
		capabilities.Features.
			bShaderFloat16 = bShaderFloat16;

		return Result<BackendData>::Success(
			{ state,
			 capabilities,
			 state->MaximumBindlessSampledTextures,
			 state->MaximumBindlessSamplers,
			 state->MaximumBindlessStorageBuffers,
			 state->MaximumBindlessStorageTextures,
			 state->MaximumBindlessUniformBuffers,
			 state->MaximumBindlessStorageBuffers,
			 state->MaximumBindlessStorageTextures,
			 {EBackend::D3D12, state->Device.Get(), state->Queue.Get()},
			 state->ComputeQueue.Get(),
			 state->CopyQueue.Get(),
			 bAccelerationInputs,
			 SupportsRayQuery(*state),
			 bAccelerationInputs,
			 state->FeedbackDevice != nullptr,
			 0xFFFFFFFFu,
			 0xFFFFFFFFu,
			 state->bHeapDirectlyIndexed,
			 true,
			 state->bCooperativeVector,
			 state->bCooperativeVectorTraining,
			 state->bOpacityMicromap,
			 state->bRayTracingClusters,
			 state->Diagnostics,
			 state->bAftermathAttached });
	}
}
#endif
