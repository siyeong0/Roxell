#pragma once

#include <ShaderLayout.h>

namespace rox::shader
{
	enum class EShaderCullMode : core::uint32
	{
		None,
		Front,
		Back
	};

	enum class EShaderCompare : core::uint32
	{
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always
	};

	enum class EShaderBlendFactor : core::uint32
	{
		Zero,
		One,
		SourceAlpha,
		OneMinusSourceAlpha,
		DestinationAlpha,
		OneMinusDestinationAlpha,
		SourceColor,
		OneMinusSourceColor,
		DestinationColor,
		OneMinusDestinationColor
	};

	enum class EShaderBlendOperation : core::uint32
	{
		Add,
		Subtract,
		ReverseSubtract,
		Minimum,
		Maximum
	};

	// Attachment formats, sample counts and vertex buffers are supplied by the renderer.
	struct ShaderRenderState
	{
		EShaderCullMode Cull = EShaderCullMode::Back;

		EShaderCompare DepthCompare = EShaderCompare::LessEqual;
		bool bDepthWrite = true;

		bool bBlend = false;

		EShaderBlendFactor SourceColor = EShaderBlendFactor::One;
		EShaderBlendFactor DestinationColor = EShaderBlendFactor::Zero;
		EShaderBlendOperation ColorOperation = EShaderBlendOperation::Add;

		EShaderBlendFactor SourceAlpha = EShaderBlendFactor::One;
		EShaderBlendFactor DestinationAlpha = EShaderBlendFactor::Zero;
		EShaderBlendOperation AlphaOperation = EShaderBlendOperation::Add;

		core::uint32 ColorWriteMask = 15;
	};

	// Entry names are stable across canonical ordering. An exact keyword set selects one combination.
	struct ShaderCodeChoice
	{
		core::Array<core::String> Keywords;

		core::Array<core::String> Entries;

		explicit ShaderCodeChoice(core::MemoryResource& memory)
			: Keywords(memory)
			, Entries(memory)
		{
		}
	};

	struct ShaderPass
	{
		core::String Name;
		core::String Tag;

		ShaderRenderState RenderState;

		core::Array<ShaderCodeChoice> Choices;

		explicit ShaderPass(core::MemoryResource& memory)
			: Name(memory)
			, Tag(memory)
			, Choices(memory)
		{
		}

		core::StringView GetName() const noexcept { return Name; }
		const ShaderRenderState& GetRenderState() const noexcept { return RenderState; }
	};

	// A selected drawing recipe. This does not begin a Ruda RenderPass or issue a draw.
	class ShaderPassSelection
	{
		friend class Shader;
	public:
		const ShaderLayout& GetLayout() const noexcept { return m_Constants.GetLayout(); }
		const ShaderPipelineConstants& GetPipelineConstants() const noexcept { return m_Constants; }
		const ShaderRenderState& GetRenderState() const noexcept { return m_RenderState; }
		ShaderFeatureRequirements GetRequirements() const noexcept { return m_Constants.GetFeatureRequirements(); }
		core::Span<const ShaderEntrySelection> GetStages() const noexcept { return GetLayout().GetSelections(); }
		core::Hash256 GetCodeHash() const noexcept { return GetLayout().GetSelectedCodeHash(); }

	private:
		ShaderPipelineConstants m_Constants;

		ShaderRenderState m_RenderState;
	};

} // namespace rox::shader
