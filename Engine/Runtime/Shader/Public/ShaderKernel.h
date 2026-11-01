#pragma once

#include <ShaderPass.h>

namespace rox::shader
{

	struct ShaderKernel
	{
		core::String Name;

		core::Array<ShaderCodeChoice> Choices;

		explicit ShaderKernel(core::MemoryResource& memory)
			: Name(memory)
			, Choices(memory)
		{
		}

		core::StringView GetName() const noexcept { return Name; }
	};

	class ShaderKernelSelection
	{
		friend class Shader;
	public:
		const ShaderLayout& GetLayout() const noexcept { return m_Constants.GetLayout(); }
		const ShaderPipelineConstants& GetPipelineConstants() const noexcept { return m_Constants; }
		core::Span<const core::uint32> GetThreadGroupSize() const noexcept { return m_Constants.GetThreadGroupSize(); }
		ShaderFeatureRequirements GetRequirements() const noexcept { return m_Constants.GetFeatureRequirements(); }
		core::Span<const ShaderEntrySelection> GetStages() const noexcept { return GetLayout().GetSelections(); }
		core::Hash256 GetCodeHash() const noexcept { return GetLayout().GetSelectedCodeHash(); }

	private:
		ShaderPipelineConstants m_Constants;
	};

} // namespace rox::shader
