#include <RhiDiagnostics.h>

#include "AftermathBackend.h"

#include <MemoryResource.h>

namespace rox::rhi
{
	using namespace core;

	Result<InterfaceOwner<IGpuCrashDiagnostics>> CreateGpuCrashDiagnostics(GpuCrashDiagnosticsDesc desc, MemoryResource& resource)
	{
		ASSERT(resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe &&
			desc.MaximumMarkers != 0 &&
			desc.MaximumCrashDumpBytes != 0 &&
			desc.MaximumShaderBinaryBytes != 0 &&
			(!desc.bShaderDebugInfo || desc.OnShaderDebugInfo != nullptr),
			"GPU diagnostics require a thread-safe resource and valid callback budgets.");
		return CreateAftermathDiagnostics(desc, resource);
	}
}
