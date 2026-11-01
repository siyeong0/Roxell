#pragma once

#include <RhiDiagnostics.h>

namespace rox::rhi
{
	[[nodiscard]] core::Result<core::InterfaceOwner<IGpuCrashDiagnostics>> CreateAftermathDiagnostics(GpuCrashDiagnosticsDesc desc, core::MemoryResource& resource);

	struct AftermathVulkanOptions final
	{
		bool bCheckpoints = false;
		bool bResourceTracking = false;
		bool bShaderDebugInfo = false;
		bool bShaderErrorReporting = false;
	};

	[[nodiscard]] AftermathVulkanOptions GetAftermathVulkanOptions(IGpuCrashDiagnostics* diagnostics) noexcept;

	// Call after native initialization succeeds and before reporting the device ready.
	[[nodiscard]] core::Result<void> AttachAftermathD3D12(IGpuCrashDiagnostics* diagnostics, void* nativeDevice);
	[[nodiscard]] core::Result<void> AttachAftermathVulkan(IGpuCrashDiagnostics* diagnostics, bool bDiagnosticsConfig, bool bDiagnosticCheckpoints);
	void DetachAftermathDevice(IGpuCrashDiagnostics* diagnostics) noexcept;
	void NotifyAftermathDeviceLost(IGpuCrashDiagnostics* diagnostics) noexcept;

	[[nodiscard]] core::Result<void*> CreateAftermathD3D12Context(IGpuCrashDiagnostics* diagnostics, void* recordingCommandList);
	void ReleaseAftermathD3D12Context(IGpuCrashDiagnostics* diagnostics, void* nativeContext) noexcept;
	[[nodiscard]] core::Result<void> SetAftermathD3D12Marker(IGpuCrashDiagnostics* diagnostics, void* nativeContext, core::uint64 markerToken);

	[[nodiscard]] core::Result<void> UpdateAftermathD3D12Resource(IGpuCrashDiagnostics* diagnostics, void* nativeResource);
}
