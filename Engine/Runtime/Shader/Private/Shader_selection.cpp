#include <Shader.h>

#include "ShaderWire.hpp"

#include <Assertion.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		template <class T>
		Result<uint32> findNamed(Span<const T> values, StringView name)
		{
			for (usize i = 0; i < values.Size(); ++i)
			{
				if (values[i].Name == name)
				{
					return Result<uint32>::Success(static_cast<uint32>(i));
				}
			}

			return Result<uint32>::Failure(MakeArtifactError("Shader name was not found.", EErrorCode::NotFound));
		}

		bool contains(Span<const String> values, StringView key)
		{
			for (const String& value : values)
			{
				if (value == key)
				{
					return true;
				}
			}

			return false;
		}

		Result<ShaderPipelineConstants> select(
			const Shader& shader,
			Span<const ShaderCodeChoice> choices,
			ShaderTargetProfile target,
			const ShaderOptions& options,
			MemoryResource& memory)
		{
			using R = Result<ShaderPipelineConstants>;
			for (const String& key : options.GetKeywords())
			{
				if (!contains(shader.GetSupportedOptions(), key))
				{
					return R::Failure(MakeArtifactError("Unknown shader keyword."));
				}
			}

			const ShaderCodeChoice* selected = nullptr;
			for (const ShaderCodeChoice& choice : choices)
			{
				if (choice.Keywords.Size() != options.GetKeywords().Size())
				{
					continue;
				}
				bool bMatches = true;
				for (const String& key : choice.Keywords)
				{
					if (!contains(options.GetKeywords(), key))
					{
						bMatches = false;
					}
				}
				if (bMatches)
				{
					ASSERT(selected == nullptr, "Validated shader choices have unique keyword combinations.");
					selected = &choice;
				}
			}
			if (!selected)
			{
				return R::Failure(MakeArtifactError("Shader keyword combination was not built.", EErrorCode::NotFound));
			}

			Array<ShaderEntrySelection> entries(memory);
			for (const String& entry : selected->Entries)
			{
				Result<uint32> variant = shader.FindVariant({entry, target});
				if (!variant)
				{
					return R::Failure(variant.GetError());
				}
				if (!entries.Add({shader, variant.Value()}))
				{
					return R::Failure(AllocationError());
				}
			}

			Result<ShaderLayout> layout = ShaderLayout::Create(entries.View(), memory);
			if (!layout)
			{
				return R::Failure(layout.GetError());
			}

			return ShaderPipelineConstants::Resolve(layout.Value(), options.GetPipelineConstants(), memory);
		}
	} // namespace

	Result<ShaderPropertyHandle> Shader::FindProperty(StringView name) const
	{
		Result<uint32> index = findNamed(GetProperties(), name);
		if (!index)
		{
			return Result<ShaderPropertyHandle>::Failure(index.GetError());
		}

		return Result<ShaderPropertyHandle>::Success({GetIdentity(), index.Value()});
	}

	Result<const ShaderProperty*> Shader::GetProperty(ShaderPropertyHandle handle) const
	{
		if (!IsValid() || GetIdentity() != handle.ShaderIdentity || handle.Index >= GetProperties().Size())
		{
			return Result<const ShaderProperty*>::Failure(MakeArtifactError("Property handle belongs to a different Shader."));
		}

		return Result<const ShaderProperty*>::Success(&GetProperties()[handle.Index]);
	}

	Result<uint32> Shader::FindPass(StringView name) const
	{
		return findNamed(GetPasses(), name);
	}

	Result<uint32> Shader::FindKernel(StringView name) const
	{
		return findNamed(GetKernels(), name);
	}

	Result<ShaderPassSelection> Shader::SelectPass(StringView name, ShaderTargetProfile target, const ShaderOptions& options, MemoryResource& memory) const
	{
		using R = Result<ShaderPassSelection>;
		Result<uint32> index = FindPass(name);
		if (!index)
		{
			return R::Failure(index.GetError());
		}

		const ShaderPass& pass = GetPasses()[index.Value()];
		Result<ShaderPipelineConstants> selected = select(*this, pass.Choices.View(), target, options, memory);
		if (!selected)
		{
			return R::Failure(selected.GetError());
		}

		ShaderPassSelection result;
		result.m_Constants = Move(selected).TakeValue();
		result.m_RenderState = pass.RenderState;
		return R::Success(Move(result));
	}

	Result<ShaderKernelSelection> Shader::SelectKernel(StringView name, ShaderTargetProfile target, const ShaderOptions& options, MemoryResource& memory) const
	{
		using R = Result<ShaderKernelSelection>;
		Result<uint32> index = FindKernel(name);
		if (!index)
		{
			return R::Failure(index.GetError());
		}

		Result<ShaderPipelineConstants> selected = select(*this, GetKernels()[index.Value()].Choices.View(), target, options, memory);
		if (!selected)
		{
			return R::Failure(selected.GetError());
		}

		ShaderKernelSelection result;
		result.m_Constants = Move(selected).TakeValue();
		return R::Success(Move(result));
	}
} // namespace rox::shader
