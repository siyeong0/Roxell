#include <RhiDiagnostics.h>
#include <SystemMemoryResource.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	using namespace core;

	TEST(RhiDiagnosticsDeathTest, ZeroMarkerBudgetViolatesTheContract)
	{
		GpuCrashDiagnosticsDesc desc;
		desc.MaximumMarkers = 0;
		EXPECT_DEATH((void)CreateGpuCrashDiagnostics(desc, SystemMemoryResource::Get()), "valid callback budgets");
	}

	TEST(RhiDiagnosticsDeathTest, ShaderDebugInfoRequiresACallback)
	{
		GpuCrashDiagnosticsDesc desc;
		desc.bShaderDebugInfo = true;
		EXPECT_DEATH((void)CreateGpuCrashDiagnostics(desc, SystemMemoryResource::Get()), "valid callback budgets");
	}
}
