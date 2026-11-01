#include <RhiCommandPreparation.h>

namespace rox::rhi
{
	PreparedBindingDesc PreparedBindingDesc::Compute(const ComputePipelineHandle pipeline) noexcept
	{
		PreparedBindingDesc desc;
		desc.Pipeline = pipeline;
		return desc;
	}

	PreparedBindingDesc PreparedBindingDesc::Graphics(const GraphicsPipelineHandle pipeline) noexcept
	{
		PreparedBindingDesc desc;
		desc.Pipeline = pipeline;
		return desc;
	}

	PreparedBindingDesc PreparedBindingDesc::Ray(const ShaderTableHandle table) noexcept
	{
		PreparedBindingDesc desc;
		desc.Pipeline = table;
		return desc;
	}
}
