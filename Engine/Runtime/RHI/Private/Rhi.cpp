#include <Rhi.h>

#include "DeviceState.h"
#include "DeviceResources.h"
#include "DevicePipelines.h"

#include <Assertion.h>

namespace rox::rhi
{
	using namespace core;

	Result<void> CreateDevice(EBackend backend, MemoryResource& resource, UniquePtr<Device>* outDevice)
	{
		return CreateDevice(DeviceCreateDesc{ backend }, resource, outDevice);
	}

	Result<void> Device::Create(DeviceCreateDesc desc, MemoryResource& resource, UniquePtr<Device>* outDevice)
	{
		ASSERT(outDevice != nullptr, "Device creation requires an output owner.");
		ASSERT(!*outDevice, "Device creation requires an empty output owner.");

		ASSERT(desc.AdapterPreference <= EAdapterPreference::Software, "The adapter preference is invalid.");

		if (desc.bEnableDeviceHeapIndexing && desc.Backend != EBackend::D3D12)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.DeviceHeapIndexing", "Device descriptor heap indexing is available on D3D12 only.");
		}

		if (desc.Diagnostics != nullptr && (desc.Backend == EBackend::Null || desc.ExternalDevice.Device != nullptr))
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.AftermathDevice", "GPU diagnostics require an engine-created D3D12 or Vulkan device.");
		}

		ASSERT(desc.Diagnostics == nullptr || desc.Diagnostics->IsAftermathEnabled(), "GPU diagnostics must be initialized before device creation.");

		ASSERT((!desc.VulkanFeatures.bSparseResidency &&
					!desc.VulkanFeatures.bHostQueryReset &&
					!desc.VulkanFeatures.bBufferDeviceAddress &&
					!desc.VulkanFeatures.bShaderInt64 &&
					!desc.VulkanFeatures.bMeshShader &&
					!desc.VulkanFeatures.bTaskShader &&
					!desc.VulkanFeatures.bAccelerationStructure &&
					!desc.VulkanFeatures.bRayQuery &&
					!desc.VulkanFeatures.bRayTracingPipeline &&
					!desc.VulkanFeatures.bOpacityMicromap &&
					!desc.VulkanFeatures.bRayTracingClusters &&
					!desc.VulkanFeatures.bFragmentShadingRate &&
					!desc.VulkanFeatures.bAttachmentShadingRate &&
					!desc.VulkanFeatures.bPrimitiveShadingRate &&
					!desc.VulkanFeatures.bImageSlicedViewOf3D &&
					!desc.VulkanFeatures.bCooperativeVector &&
					!desc.VulkanFeatures.bCooperativeVectorTraining &&
					!desc.VulkanFeatures.bExternalMemory &&
					!desc.VulkanFeatures.bExternalTimelineSemaphore &&
					!desc.VulkanFeatures.bMemoryBudget &&
					!desc.VulkanFeatures.bShaderDrawParameters) ||
			(desc.Backend == EBackend::Vulkan && desc.ExternalDevice.Device != nullptr),
			"External Vulkan feature declarations require an external Vulkan device.");

		ASSERT(!desc.VulkanFeatures.bTaskShader || desc.VulkanFeatures.bMeshShader, "External Vulkan task shaders require mesh shaders.");

		ASSERT(!desc.VulkanFeatures.bAccelerationStructure || desc.VulkanFeatures.bBufferDeviceAddress, "External Vulkan acceleration structures require buffer device address.");

		ASSERT(!desc.VulkanFeatures.bCooperativeVectorTraining || desc.VulkanFeatures.bCooperativeVector, "External Vulkan cooperative vector training requires the base feature.");

		ASSERT(!desc.VulkanFeatures.bCooperativeVector || desc.VulkanFeatures.bBufferDeviceAddress, "External Vulkan cooperative vector conversion requires buffer device address.");

		ASSERT((!desc.VulkanFeatures.bRayQuery && !desc.VulkanFeatures.bRayTracingPipeline && !desc.VulkanFeatures.bOpacityMicromap && !desc.VulkanFeatures.bRayTracingClusters) ||
			desc.VulkanFeatures.bAccelerationStructure,
			"External Vulkan ray features require acceleration structures.");

		ASSERT((!desc.VulkanFeatures.bAttachmentShadingRate && !desc.VulkanFeatures.bPrimitiveShadingRate) || desc.VulkanFeatures.bFragmentShadingRate,
			"External Vulkan attachment and primitive shading rates require the pipeline feature.");

		ASSERT(desc.ExternalDevice.Device != nullptr ||
			(desc.ExternalDevice.Backend == EBackend::Null &&
					desc.ExternalDevice.Queue == nullptr &&
					desc.ExternalDevice.Instance == nullptr &&
					desc.ExternalDevice.PhysicalDevice == nullptr &&
					desc.ExternalDevice.QueueFamilyIndex == 0xFFFFFFFFu),
			"An incomplete external device description was supplied.");

		ASSERT(desc.ExternalDevice.Device == nullptr || (desc.ExternalDevice.Backend == desc.Backend && desc.ExternalDevice.Queue != nullptr),
			"The external device backend and queue must match the requested backend.");

		ASSERT(desc.ExternalDevice.Device == nullptr || (!desc.bEnableValidation && desc.AdapterPreference == EAdapterPreference::Default),
			"Validation and adapter preference must be selected when creating the external device.");

		ASSERT(desc.ExternalComputeQueue.Handle == nullptr ||
			(desc.ExternalDevice.Device != nullptr &&
					desc.ExternalComputeQueue.Backend == desc.Backend &&
					(desc.Backend != EBackend::Vulkan ||
							(desc.ExternalComputeQueue.QueueFamilyIndex != 0xFFFFFFFFu &&
									(desc.ExternalComputeQueue.Handle != desc.ExternalDevice.Queue || desc.ExternalComputeQueue.QueueFamilyIndex == desc.ExternalDevice.QueueFamilyIndex))) &&
					(desc.Backend != EBackend::D3D12 || desc.ExternalComputeQueue.QueueFamilyIndex == 0xFFFFFFFFu) &&
					desc.Backend != EBackend::Null),
			"The external compute queue needs its actual backend and queue family.");

		ASSERT(desc.ExternalComputeQueue.Handle != nullptr || (desc.ExternalComputeQueue.Backend == EBackend::Null && desc.ExternalComputeQueue.QueueFamilyIndex == 0xFFFFFFFFu),
			"An incomplete external compute queue was supplied.");

		ASSERT(desc.ExternalCopyQueue.Handle == nullptr ||
			(desc.ExternalDevice.Device != nullptr &&
					desc.ExternalCopyQueue.Backend == desc.Backend &&
					(desc.Backend != EBackend::Vulkan ||
							(desc.ExternalCopyQueue.QueueFamilyIndex != 0xFFFFFFFFu &&
									(desc.ExternalCopyQueue.Handle != desc.ExternalDevice.Queue || desc.ExternalCopyQueue.QueueFamilyIndex == desc.ExternalDevice.QueueFamilyIndex) &&
									(desc.ExternalComputeQueue.Handle == nullptr ||
											desc.ExternalCopyQueue.Handle != desc.ExternalComputeQueue.Handle ||
											desc.ExternalCopyQueue.Handle == desc.ExternalDevice.Queue))) &&
					(desc.Backend != EBackend::D3D12 || desc.ExternalCopyQueue.QueueFamilyIndex == 0xFFFFFFFFu) &&
					desc.Backend != EBackend::Null),
			"The external copy queue needs its actual backend and queue family.");

		ASSERT(desc.ExternalCopyQueue.Handle != nullptr || (desc.ExternalCopyQueue.Backend == EBackend::Null && desc.ExternalCopyQueue.QueueFamilyIndex == 0xFFFFFFFFu),
			"An incomplete external copy queue was supplied.");

#if ROXELL_RHI_BACKEND == 1
		constexpr EBackend COMPILED_BACKEND = EBackend::D3D12;
#elif ROXELL_RHI_BACKEND == 2
		constexpr EBackend COMPILED_BACKEND = EBackend::Vulkan;
#else
		constexpr EBackend COMPILED_BACKEND = EBackend::Null;
#endif
		if (desc.Backend != COMPILED_BACKEND)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.Backend", "The requested backend is not compiled into this application.");
		}
#if ROXELL_RHI_BACKEND == 1
		Result<BackendData> created = backend::CreateD3D12Backend(resource, desc.bEnableValidation, desc.bEnableDeviceHeapIndexing, desc.AdapterPreference, desc.ExternalDevice, desc.ExternalComputeQueue, desc.ExternalCopyQueue, desc.Diagnostics);
#elif ROXELL_RHI_BACKEND == 2
		Result<BackendData> created = backend::CreateVulkanBackend(resource, desc.bEnableValidation, desc.AdapterPreference, desc.ExternalDevice, desc.ExternalComputeQueue, desc.ExternalCopyQueue, desc.VulkanFeatures, desc.Diagnostics);
#else
		if (desc.ExternalDevice.Device != nullptr || desc.bEnableValidation || desc.AdapterPreference != EAdapterPreference::Default)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "Rhi.NullDevice", "The Null backend does not support external devices, validation layers, or adapter selection.");
		}
		Result<BackendData> created = backend::CreateNullBackend(resource);
#endif
		if (!created)
		{
			return Result<void>::Failure(created.GetError());
		}
		BackendData instance = created.Value();
		UniquePtr<State> state = MakeUnique<State>(resource, resource, instance, desc.Backend);

		if (!state)
		{
			backend::Destroy(instance.State);
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "The RHI device could not be allocated.");
		}

		UniquePtr<Device> owner = MakeUnique<Device>(resource, CreationKey{}, Move(state));
		if (!owner)
		{
			Device cleanup(CreationKey{}, Move(state));
			return Result<void>::Failure(EErrorCode::ExternalFailure, "Rhi.Allocation", "Could not allocate the device owner.");
		}

		*outDevice = Move(owner);
		return Result<void>::Success();
	}

	Result<void> CreateDevice(DeviceCreateDesc desc, MemoryResource& resource, UniquePtr<Device>* outDevice)
	{
		return Device::Create(desc, resource, outDevice);
	}
} // namespace rox::rhi
