#include "VulkanState.h"
#include "VulkanBackend.h"

namespace rox::vulkan
{
	using namespace core;

	State::State(MemoryResource& resource) noexcept
		: Resource(resource)
		, Submissions(resource)
		, InFlight(resource)
		, Recycled(resource)
		, RetiredBuffers(resource)
		, RetiredAccelStructs(resource)
		, RetiredMicromaps(resource)
		, RetiredTextures(resource)
		, RetiredPipelines(resource)
		, RetiredGraphicsPipelines(resource)
		, RetiredRayPipelines(resource)
		, RetiredFramebuffers(resource)
		, DirectRenderPasses(resource)
	{
	}
}
