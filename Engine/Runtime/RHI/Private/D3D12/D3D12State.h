#pragma once

#ifdef _WIN32
#include "../SubmissionCompletion.h"
#include "D3D12Submission.h"
#include "NvApiSession.h"

#include <Synchronization.hpp>

namespace rox::d3d12
{
	using namespace rox::rhi;

	struct FixedDescriptorHeap final
	{
		explicit FixedDescriptorHeap(core::MemoryResource& resource) noexcept
			: Used(resource)
		{
		}

		ComPtr<ID3D12DescriptorHeap> Native;
		core::Array<core::uint8> Used;

		core::uint32 Search = 0;
		core::uint32 LiveCount = 0;
		core::uint32 HighWater = 0;
	};

	struct CpuDescriptorPage final
	{
		static constexpr core::uint32 CAPACITY = 1024;

		ComPtr<ID3D12DescriptorHeap> Heap;
		D3D12_DESCRIPTOR_HEAP_TYPE Type{};

		core::uint32 Search = 0;
		core::uint32 Live = 0;
		core::uint8 Used[CAPACITY]{};
	};

	struct State final
	{
		core::Mutex DescriptorMutex;
		core::Mutex CpuDescriptorMutex;
		core::Array<CpuDescriptorPage> CpuDescriptorPages;

		explicit State(core::MemoryResource& resource) noexcept;

		core::MemoryResource& Resource;

		D3D12IntegerClearCache IntegerClearCache;

		IGpuCrashDiagnostics* Diagnostics = nullptr;
		bool bAftermathAttached = false;

		NvApiSession NvApi;

		bool bOpacityMicromap = false;
		bool bRayTracingClusters = false;

		bool bCooperativeVector = false;
		bool bNvCooperativeVector = false;
		bool bCooperativeVectorTraining = false;
		bool bCoopVecTrainingFloat16 = false;
		bool bCoopVecTrainingFloat32 = false;
#if defined(DIRECT3D_LINEAR_ALGEBRA)

		ComPtr<ID3D12DevicePreview> LinAlgDevice;
#endif

		ComPtr<ID3D12Device> Device;
		ComPtr<IDXGIAdapter3> BudgetAdapter;

		ComPtr<ID3D12Device8> FeedbackDevice;

		ComPtr<ID3D12Device5> RayDevice;

		ComPtr<ID3D12CommandQueue> Queue;
		ComPtr<ID3D12CommandQueue> ComputeQueue;
		ComPtr<ID3D12CommandQueue> CopyQueue;

		ComPtr<ID3D12Fence> Fence;
		ComPtr<ID3D12Fence> ComputeFence;
		ComPtr<ID3D12Fence> CopyFence;

		ComPtr<ID3D12CommandSignature> DispatchSignature;
		ComPtr<ID3D12CommandSignature> DrawSignature;
		ComPtr<ID3D12CommandSignature> DrawIndexedSignature;
		ComPtr<ID3D12CommandSignature> MeshSignature;

		HANDLE Event = nullptr;

		SubmissionCompletion Submissions;
		core::Array<Pending> InFlight;
		core::usize PreparedCredits = 0;

		core::Array<Pending> RecycledDirect;
		core::Array<Pending> RecycledCompute;
		core::Array<Pending> RecycledCopy;

		core::Array<Retired> RetiredBuffers;
		core::Array<RetiredTexture> RetiredTextures;
		core::Array<RetiredPipeline> RetiredPipelines;
		core::Array<RetiredGraphicsPipeline> RetiredGraphicsPipelines;
		core::Array<RetiredRayPipeline> RetiredRayPipelines;
		core::Array<RetiredFramebuffer> RetiredFramebuffers;

		ComPtr<ID3D12DescriptorHeap> GlobalResourceHeap;
		ComPtr<ID3D12DescriptorHeap> GlobalSamplerHeap;
		core::Array<core::uint8> ResourceDescriptorUsed;
		core::Array<core::uint8> SamplerDescriptorUsed;

		FixedDescriptorHeap FixedResourceDescriptors;
		FixedDescriptorHeap FixedSamplerDescriptors;

		core::Array<CoopVecFormatCombo> CoopVecFormats;

		core::uint32 ResourceDescriptorSearch = 0;
		core::uint32 SamplerDescriptorSearch = 0;

		core::uint64 LastSerial = 0;

		core::uint64 LastDirectSerial = 0;
		core::uint64 LastComputeSerial = 0;
		core::uint64 LastCopySerial = 0;

		core::uint32 MaximumBindlessSampledTextures = 0;
		core::uint32 MaximumBindlessSamplers = 0;
		core::uint32 MaximumBindlessStorageBuffers = 0;
		core::uint32 MaximumBindlessStorageTextures = 0;
		core::uint32 MaximumBindlessUniformBuffers = 0;

		core::uint32 SupportedShadingRates[4]{};

		bool bMeshShader = false;
		bool bMeshIndirect = false;

		bool bHeapDirectlyIndexed = false;

		bool bLost = false;
		bool bDeviceHeapIndexingEnabled = false;

		ComPtr<ID3D12Fence> IdleFences[3];
		core::uint64 IdleFenceValue = 0;
		bool bIdleIssued = false;
		bool bIdleConfirmed = true;

		bool bNativeQueuesExposed = false;

		bool bSubmissionMayBePending = false;
		core::uint64 PresentCopySerial = 0;

		core::int32 LastSubmissionNativeCode = 0;
		core::uint32 LastSubmissionFailureStage = 0;

		// Imported wrappers may alias owned resources without sharing native tracking records.
		core::uint64 UnknownResourceUses[3]{};
		core::uint64 UnknownBufferUses[3]{};
	};
}
#endif
