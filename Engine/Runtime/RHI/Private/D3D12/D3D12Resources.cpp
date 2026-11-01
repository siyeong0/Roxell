#include "D3D12Resources.h"
#include "D3D12Backend.h"

#ifdef _WIN32
namespace rox::d3d12
{
	using namespace core;

	DescriptorTable::DescriptorTable(MemoryResource& resource) noexcept
		: Textures(resource)
		, TextureViews(resource)
		, Buffers(resource)
		, SamplerValid(resource)
	{
	}

	RayTracingPipeline::RayTracingPipeline(MemoryResource& resource) noexcept
		: GroupHandles(resource)
		, LocalRootArguments(resource)
	{
	}
}
#endif
