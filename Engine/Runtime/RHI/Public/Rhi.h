#pragma once
#include <Device.h>
namespace rox::rhi
{
	// The output owner must be non-null and empty. Failure leaves it empty.
	// The resource outlives the device; drain GPU work before releasing the owner.
	[[nodiscard]] core::Result<void> CreateDevice(EBackend backend, core::MemoryResource& resource, core::UniquePtr<Device>* outDevice);
	[[nodiscard]] core::Result<void> CreateDevice(DeviceCreateDesc desc, core::MemoryResource& resource, core::UniquePtr<Device>* outDevice);
}
