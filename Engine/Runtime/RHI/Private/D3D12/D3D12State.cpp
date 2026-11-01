#include "D3D12State.h"
#include "D3D12Backend.h"

#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	State::State(MemoryResource& resource) noexcept
		: CpuDescriptorPages(resource)
		, Resource(resource)
		, IntegerClearCache(resource)
		, Submissions(resource)
		, InFlight(resource)
		, RecycledDirect(resource)
		, RecycledCompute(resource)
		, RecycledCopy(resource)
		, RetiredBuffers(resource)
		, RetiredTextures(resource)
		, RetiredPipelines(resource)
		, RetiredGraphicsPipelines(resource)
		, RetiredRayPipelines(resource)
		, RetiredFramebuffers(resource)
		, ResourceDescriptorUsed(resource)
		, SamplerDescriptorUsed(resource)
		, FixedResourceDescriptors(resource)
		, FixedSamplerDescriptors(resource)
		, CoopVecFormats(resource)
	{
	}
}
#endif
