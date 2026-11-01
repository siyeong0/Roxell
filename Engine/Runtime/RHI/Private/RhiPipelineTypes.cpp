#include <RhiPipelineTypes.h>

namespace rox::rhi
{
	using namespace core;

	ShaderInput ShaderInput::Code(const Span<const Byte> code, const char* const entryPoint) noexcept
	{
		ShaderInput input;
		input.m_Kind = EKind::Code;
		input.m_Code = code;
		input.m_EntryPoint = entryPoint;
		return input;
	}

	ShaderInput ShaderInput::Shader(const ShaderHandle shader) noexcept
	{
		ShaderInput input;
		input.m_Kind = EKind::Shader;
		input.m_Shader = shader;
		return input;
	}
}
