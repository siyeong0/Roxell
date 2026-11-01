#include "ShaderCompilerInternal.h"

#include <Assertion.h>

namespace rox::shader
{
	using namespace core;

	Error MakeBuildError(EErrorCode code, StringView message)
	{
		return MakeError(code, "ROX-SHADER-BUILD", message);
	}

	Error MakeBuildAllocationError()
	{
		return MakeBuildError(EErrorCode::ExternalFailure, "Shader build storage allocation failed.");
	}

	Error MakeBuildCancelledError()
	{
		return MakeBuildError(EErrorCode::Cancelled, "Shader build was cancelled.");
	}

	Hash256 HashText(StringView text)
	{
		return ComputeSha256({reinterpret_cast<const Byte*>(text.Data()), text.Size()});
	}

	void RemoveUnusedBinding(shader::ShaderVariant& variant, uint32 index)
	{
		ASSERT(index < variant.Bindings.Size(), "Unused shader binding index must be valid.");
		variant.ResourceUses.RemoveIf([index](const shader::ShaderResourceUse& use) noexcept { return use.BindingIndex == index; });

		for (shader::ShaderResourceUse& use : variant.ResourceUses)
		{
			if (use.BindingIndex > index)
			{
				--use.BindingIndex;
			}
		}

		variant.Bindings.RemoveAt(index);
	}
} // namespace rox::shader
