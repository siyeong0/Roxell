#include "ShaderCompilerInternal.h"

#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	Result<ShaderSourceSnapshot> CreateShaderSourceSnapshot(StringView identity, StringView source, MemoryResource& resource)
	{
		if (!IsCanonicalIdentity(identity) || !IsValidUtf8(source) || source.Contains('\0'))
		{
			return Result<ShaderSourceSnapshot>::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Shader snapshot identity or UTF-8 source is invalid."));
		}

		ShaderSourceSnapshot result(resource);
		if (!result.Identity.Assign(identity) || !result.Source.Assign(source))
		{
			return Result<ShaderSourceSnapshot>::Failure(MakeBuildAllocationError());
		}

		result.ContentHash = HashText(source);

		return Result<ShaderSourceSnapshot>::Success(Move(result));
	}
} // namespace rox::shader

namespace rox::shader
{
	using namespace core;

	bool IsCanonicalIdentity(StringView identity)
	{
		if (identity.IsEmpty() || identity.Size() > 4096 || !IsValidUtf8(identity) || identity[0] == '/' || identity.EndsWith("/"))
		{
			return false;
		}

		usize start = 0;

		for (usize i = 0; i <= identity.Size(); ++i)
		{
			if (i < identity.Size() &&
				(static_cast<unsigned char>(identity[i]) < 32 || identity[i] == ':' || identity[i] == '\\' || identity[i] == '"' || identity[i] == '<' || identity[i] == '>'))
			{
				return false;
			}

			if (i == identity.Size() || identity[i] == '/')
			{
				StringView part = identity.Substr(start, i - start);
				if (part.IsEmpty() || part == StringView(".") || part == StringView(".."))
				{
					return false;
				}

				start = i + 1;
			}
		}

		return true;
	}

	Result<String> CanonicalPath(StringView base, StringView path, MemoryResource& resource)
	{
		using R = Result<String>;

		if (path.IsEmpty() || path.Size() > 4096 || path[0] == '/' || path.Contains(':') || path.Contains('\\') || !IsValidUtf8(path))
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Shader virtual path is invalid."));
		}

		String joined(resource);
		usize slash = StringView::NPOS;

		for (usize i = 0; i < base.Size(); ++i)
		{
			if (base[i] == '/')
			{
				slash = i;
			}
		}

		if ((slash != StringView::NPOS && !joined.Assign(base.Substr(0, slash + 1))) || !joined.Append(path))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		String normalized(resource);
		Array<usize> componentStarts(resource);
		const StringView text = joined.View();
		usize start = 0;

		for (usize i = 0; i <= text.Size(); ++i)
		{
			if (i != text.Size() && text[i] != '/')
			{
				continue;
			}

			StringView part = text.Substr(start, i - start);
			start = i + 1;
			if (part.IsEmpty() || part == StringView("."))
			{
				continue;
			}

			if (part == StringView(".."))
			{
				if (componentStarts.IsEmpty())
				{
					return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Shader path escapes the virtual root."));
				}

				normalized.Truncate(componentStarts.Back());
				componentStarts.RemoveAt(componentStarts.Size() - 1);
				continue;
			}

			if (!componentStarts.Add(normalized.Size()) || (!normalized.IsEmpty() && !normalized.Append("/")) || !normalized.Append(part))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		if (!IsCanonicalIdentity(normalized))
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Shader path is not a canonical virtual identity."));
		}

		return R::Success(Move(normalized));
	}
} // namespace rox::shader
