#pragma once

#include <InterfaceOwner.hpp>
#include <MemoryResource.h>
#include <Result.hpp>
#include <RhiInterop.h>
#include <Span.hpp>
#include <StringView.hpp>
#include <Types.h>

namespace rox::rhi
{
	enum class EGpuCrashDumpStatus : core::uint8
	{
		NotStarted,
		Collecting,
		Finished,
		Failed,
		Unknown
	};

	// Create this process-wide owner before creating any D3D12 or Vulkan device.
	// Its resource must be thread-safe and must outlive every attached device and callback.
	// Callbacks may run on SDK threads and must not destroy this owner or its resource.
	struct GpuCrashDiagnosticsDesc final
	{
		// UTF-8 absolute path; null loads the DLL beside the application executable.
		const char* RuntimeLibraryPath = nullptr;

		void* CallbackUser = nullptr;
		void (*OnCrashDump)(void*, core::Span<const core::Byte>) noexcept = nullptr;
		void (*OnShaderDebugInfo)(void*, core::Span<const core::Byte>) noexcept = nullptr;

		core::uint32 MaximumMarkers = 4096;
		core::uint64 MaximumShaderBinaryBytes = 32ull * 1024 * 1024;
		core::uint64 MaximumCrashDumpBytes = 64ull * 1024 * 1024;

		bool bMarkers = true;
		bool bResourceTracking = true;
		bool bShaderDebugInfo = false;
		bool bShaderErrorReporting = false;
	};

	// SDK types and binaries remain private to RHI. The callback spans are valid only
	// during the callback; all retained marker, shader and dump bytes use the Core resource.
	class IGpuCrashDiagnostics
	{
	public:
		IGpuCrashDiagnostics(const IGpuCrashDiagnostics&) = delete;
		IGpuCrashDiagnostics& operator=(const IGpuCrashDiagnostics&) = delete;
		virtual ~IGpuCrashDiagnostics() = default;

		[[nodiscard]] virtual bool IsAftermathEnabled() const noexcept = 0;
		[[nodiscard]] virtual core::Result<EGpuCrashDumpStatus> GetCrashDumpStatus() const = 0;

		// Passing an empty output returns the required byte count. A short output fails.
		[[nodiscard]] virtual core::Result<core::uint64> CopyLatestCrashDump(core::Span<core::Byte> output) const = 0;

		[[nodiscard]] virtual core::Result<core::uint64> RegisterMarker(core::StringView text) = 0;
		// Retire only after the associated GPU work completed. Device loss retains markers
		// until Aftermath has finished processing the crash dump.
		[[nodiscard]] virtual core::Result<void> RetireMarker(core::uint64 token) = 0;

		[[nodiscard]] virtual core::Result<core::uint64> RegisterShaderBinary(EBackend backend, core::uint64 shaderHash, core::Span<const core::Byte> bytecode) = 0;
		[[nodiscard]] virtual core::Result<core::uint64> ComputeShaderBinaryHash(EBackend backend, core::Span<const core::Byte> bytecode) const = 0;
		[[nodiscard]] virtual core::Result<void> UnregisterShaderBinary(core::uint64 token) = 0;
		[[nodiscard]] virtual core::Result<core::uint64> CopyShaderBinary(EBackend backend, core::uint64 shaderHash, core::Span<core::Byte> output) const = 0;

	protected:
		IGpuCrashDiagnostics() = default;
	};

	// The optional SDK library is loaded dynamically. An absent or failed SDK is
	// reported as a Result failure so an explicit diagnostics request is never silent.
	[[nodiscard]] core::Result<core::InterfaceOwner<IGpuCrashDiagnostics>> CreateGpuCrashDiagnostics(GpuCrashDiagnosticsDesc desc, core::MemoryResource& resource);
}
