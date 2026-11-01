#include "SourceClosure.h"

#include "ShaderCompilerInternal.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <Cancellation.h>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		template <class Less> bool addIndex(Array<uint32>& indices, uint32 index, Less less)
		{
			usize position = 0;
			if (!indices.IsEmpty())
			{
				position = static_cast<usize>(LowerBound(indices.begin(), indices.end(), index, less) - indices.begin());
			}

			if (!indices.Add(index))
			{
				return false;
			}

			Rotate(indices.begin() + position, indices.end() - 1, indices.end());

			return true;
		}
	} // namespace

	uint64 SourceClosure::GetOwnedByteCount() const noexcept
	{
		uint64 bytes = m_Sources.Capacity() * sizeof(ShaderSourceSnapshot) + m_States.Capacity() * sizeof(uint8) + m_Aliases.Capacity() * sizeof(SourceAlias) +
			m_IncludeEdges.Capacity() * sizeof(SourceIncludeEdge) + (m_SourceIndex.Capacity() + m_AliasIndex.Capacity()) * sizeof(uint32);
		for (const ShaderSourceSnapshot& source : m_Sources)
		{
			bytes += source.Identity.Capacity() + 1;
			bytes += source.Source.Capacity() + 1;
		}

		for (const SourceAlias& alias : m_Aliases)
		{
			bytes += alias.Path.Capacity() + 1;
		}

		for (const SourceIncludeEdge& edge : m_IncludeEdges)
		{
			bytes += edge.RequestedPath.Capacity() + 1;
		}

		return bytes;
	}

	SourceClosure::SourceClosure(const ShaderCompileRequest& request, IShaderSourceResolver& resolver, MemoryResource& resource, CancellationToken cancellation) noexcept
		: SourceClosure(request, resource)
	{
		m_Resolver = &resolver;
		m_Cancellation = cancellation;
	}

	SourceClosure::SourceClosure(const ShaderCompileRequest& request, MemoryResource& resource) noexcept
		: m_Sources(resource)
		, m_States(resource)
		, m_Aliases(resource)
		, m_IncludeEdges(resource)
		, m_SourceIndex(resource)
		, m_AliasIndex(resource)
		, m_Request(request)
		, m_Resolver(nullptr)
		, m_Resource(resource)
	{
	}

	Result<uint32> SourceClosure::add(const ShaderSourceSnapshot& snapshot, StringView requestedAlias)
	{
		using R = Result<uint32>;

		if (!IsCanonicalIdentity(snapshot.Identity) ||
			snapshot.Source.Size() > m_Request.Limits.MaximumSourceBytes ||
			HashText(snapshot.Source) != snapshot.ContentHash ||
			!IsValidUtf8(snapshot.Source) ||
			snapshot.Source.View().Contains('\0'))
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Resolved shader snapshot is malformed or its hash does not match its bytes."));
		}

		uint32 index = findSource(snapshot.Identity);
		if (index != NO_SHADER_SOURCE)
		{
			if (m_Sources[index].ContentHash != snapshot.ContentHash || m_Sources[index].Source != snapshot.Source)
			{
				return R::Failure(MakeBuildError(EErrorCode::Conflict, "One shader source identity resolved to different immutable bytes."));
			}
		}
		else
		{
			index = static_cast<uint32>(m_Sources.Size());
			if (findAlias(snapshot.Identity) != NO_SHADER_SOURCE)
			{
				return R::Failure(MakeBuildError(EErrorCode::Conflict, "A shader alias conflicts with another canonical source identity."));
			}

			if (m_Sources.Size() >= m_Request.Limits.MaximumSources || snapshot.Source.Size() > m_Request.Limits.MaximumAggregateSourceBytes - m_TotalBytes)
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Shader source closure exceeds its configured bounds."));
			}

			ShaderSourceSnapshot record(m_Resource);
			record.ContentHash = snapshot.ContentHash;
			if (!record.Identity.Assign(snapshot.Identity) || !record.Source.Assign(snapshot.Source) || !m_Sources.Add(Move(record)) || !m_States.Add(0))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			if (!addIndex(m_SourceIndex, index, [&](uint32 left, uint32 right) { return m_Sources[left].Identity.View() < m_Sources[right].Identity.View(); }))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			m_TotalBytes += snapshot.Source.Size();
		}

		const uint32 existingAlias = findAlias(requestedAlias);
		if (existingAlias != NO_SHADER_SOURCE)
		{
			return m_Aliases[existingAlias].SourceIndex == index ? R::Success(index) : R::Failure(MakeBuildError(EErrorCode::Conflict, "Shader source alias changed identity."));
		}

		const uint32 canonicalSource = findSource(requestedAlias);
		if (canonicalSource != NO_SHADER_SOURCE && canonicalSource != index)
		{
			return R::Failure(MakeBuildError(EErrorCode::Conflict, "A shader alias conflicts with another canonical source identity."));
		}

		if (m_Aliases.Size() >= m_Request.Limits.MaximumSources)
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Shader source aliases exceed the configured source count bound."));
		}

		SourceAlias newAlias(m_Resource);
		newAlias.SourceIndex = index;
		if (!newAlias.Path.Assign(requestedAlias) || !m_Aliases.Add(Move(newAlias)))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		if (!addIndex(
				m_AliasIndex, static_cast<uint32>(m_Aliases.Size() - 1), [&](uint32 left, uint32 right) { return m_Aliases[left].Path.View() < m_Aliases[right].Path.View(); }))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		return R::Success(index);
	}

	void SourceClosure::Freeze() noexcept
	{
		m_Resolver = nullptr;
		m_Cancellation = {};
		m_States.Clear();
	}

	Result<void> SourceClosure::Initialize()
	{
		ASSERT(m_Resolver != nullptr, "A frozen shader source closure cannot resolve application sources.");
		if (m_Cancellation.IsCancellationRequested())
		{
			return Result<void>::Failure(MakeBuildCancelledError());
		}

		Result<uint32> root = add(m_Request.Root, m_Request.Root.Identity);
		if (!root)
		{
			return Result<void>::Failure(root.GetError());
		}

		return visitRecursive(root.Value(), 0);
	}

	uint32 SourceClosure::findSource(StringView path) const
	{
		if (m_SourceIndex.IsEmpty())
		{
			return NO_SHADER_SOURCE;
		}

		const auto found = LowerBound(m_SourceIndex.begin(), m_SourceIndex.end(), path, [&](uint32 index, StringView text) { return m_Sources[index].Identity.View() < text; });

		return found != m_SourceIndex.end() && m_Sources[*found].Identity.View() == path ? *found : NO_SHADER_SOURCE;
	}

	uint32 SourceClosure::findAlias(StringView path) const
	{
		if (m_AliasIndex.IsEmpty())
		{
			return NO_SHADER_SOURCE;
		}

		const auto found = LowerBound(m_AliasIndex.begin(), m_AliasIndex.end(), path, [&](uint32 index, StringView text) { return m_Aliases[index].Path.View() < text; });

		return found != m_AliasIndex.end() && m_Aliases[*found].Path.View() == path ? *found : NO_SHADER_SOURCE;
	}

	const ShaderSourceSnapshot* SourceClosure::Find(StringView path) const
	{
		const uint32 alias = findAlias(path);
		const uint32 source = alias != NO_SHADER_SOURCE ? m_Aliases[alias].SourceIndex : findSource(path);

		return source != NO_SHADER_SOURCE ? &m_Sources[source] : nullptr;
	}

	Result<void> SourceClosure::recordEdge(uint32 requester, uint32 source, StringView requested)
	{
		using R = Result<void>;

		ASSERT(
			requester < m_Sources.Size() &&
			(source == NO_SHADER_SOURCE || source < m_Sources.Size()), "Include edges refer to captured sources or an unresolved optional source.");

		for (const SourceIncludeEdge& edge : m_IncludeEdges)
		{
			if (edge.Requester != requester || edge.RequestedPath.View() != requested)
			{
				continue;
			}

			return edge.SourceIndex == source ? R::Success() : R::Failure(MakeBuildError(EErrorCode::Conflict, "One source include edge changed its resolved identity."));
		}

		if (m_IncludeEdges.Size() >= uint64(m_Request.Limits.MaximumSources) * 4)
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Shader include edges exceed their configured count bound."));
		}

		SourceIncludeEdge edge(m_Resource);
		edge.Requester = requester;
		edge.SourceIndex = source;
		if (!edge.RequestedPath.Assign(requested) || !m_IncludeEdges.Add(Move(edge)))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		return R::Success();
	}

	Result<void> SourceClosure::resolve(uint32 requester, StringView requested, uint32 depth)
	{
		ASSERT(requester < m_Sources.Size(), "Captured shader include requesters must exist in the source closure.");
		if (m_Cancellation.IsCancellationRequested())
		{
			return Result<void>::Failure(MakeBuildCancelledError());
		}

		const StringView requesting = m_Sources[requester].Identity.View();
		Result<String> candidate = CanonicalPath(requesting, requested, m_Resource);
		if (!candidate)
		{
			return Result<void>::Failure(candidate.GetError());
		}

		Result<ShaderSourceSnapshot> result = m_Resolver->Resolve(requesting, requested, m_Resource, m_Cancellation);

		for (usize i = 0; !result && result.GetError().Code == EErrorCode::NotFound && i < m_Request.IncludeRoots.Size(); ++i)
		{
			String rooted(m_Resource);
			if (!rooted.Assign(m_Request.IncludeRoots[i]) || !rooted.Append("/") || !rooted.Append(requested))
			{
				return Result<void>::Failure(MakeBuildAllocationError());
			}

			candidate = CanonicalPath({}, rooted, m_Resource);
			if (!candidate)
			{
				return Result<void>::Failure(candidate.GetError());
			}

			result = m_Resolver->Resolve({}, rooted, m_Resource, m_Cancellation);
		}

		if (m_Cancellation.IsCancellationRequested())
		{
			return Result<void>::Failure(MakeBuildCancelledError());
		}

		if (!result && result.GetError().Code == EErrorCode::NotFound)
		{
			return recordEdge(requester, NO_SHADER_SOURCE, requested);
		}

		if (!result)
		{
			return Result<void>::Failure(result.GetError());
		}

		Result<uint32> index = add(result.Value(), candidate.Value());
		if (!index)
		{
			return Result<void>::Failure(index.GetError());
		}

		Result<void> edge = recordEdge(requester, index.Value(), requested);
		if (!edge)
		{
			return edge;
		}

		return visitRecursive(index.Value(), depth + 1);
	}

	Result<void> SourceClosure::visitRecursive(uint32 index, uint32 depth)
	{
		ASSERT(index < m_Sources.Size(), "Shader source traversal requires a captured source index.");
		using R = Result<void>;

		if (depth > m_Request.Limits.MaximumTraversalDepth || m_States[index] == 1)
		{
			return R::Failure(MakeBuildError(EErrorCode::Conflict, "Shader include/import cycle or excessive traversal depth."));
		}

		if (m_States[index] == 2)
		{
			return R::Success();
		}

		m_States[index] = 1;
		// The snapshot's character storage remains stable while captured snapshots move.
		String source(m_Resource);
		if (!source.Assign(m_Sources[index].Source))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		const StringView text = source.View();
		bool bBlockComment = false;
		usize lineStart = 0;

		while (lineStart < text.Size())
		{
			if (m_Cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			usize end = text.Find('\n', lineStart);
			if (end == StringView::NPOS)
			{
				end = text.Size();
			}

			Array<char> cleanBytes(m_Resource);
			bool bQuoted = false;
			bool bEscaped = false;

			for (usize i = lineStart; i < end; ++i)
			{
				if (bBlockComment)
				{
					if (i + 1 < end && text[i] == '*' && text[i + 1] == '/')
					{
						bBlockComment = false;
						++i;
					}

					continue;
				}

				if (!bQuoted && i + 1 < end && text[i] == '/' && text[i + 1] == '/')
				{
					break;
				}

				if (!bQuoted && i + 1 < end && text[i] == '/' && text[i + 1] == '*')
				{
					bBlockComment = true;
					++i;
					continue;
				}

				if (!cleanBytes.Add(text[i]))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				if (bEscaped)
				{
					bEscaped = false;
				}
				else if (bQuoted && text[i] == '\\')
				{
					bEscaped = true;
				}
				else if (text[i] == '"')
				{
					bQuoted = !bQuoted;
				}
			}

			String clean(m_Resource);
			if (!clean.Assign({cleanBytes.Data(), cleanBytes.Size()}))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			lineStart = end + 1;
			StringView line = clean.View();
			usize begin = 0;

			while (begin < line.Size() && (line[begin] == ' ' || line[begin] == '\t' || line[begin] == '\r'))
			{
				++begin;
			}

			line = line.Substr(begin);
			bool bInclude = false;
			bool bImport = false;
			if (line.StartsWith("#"))
			{
				line = line.Substr(1);

				while (!line.IsEmpty() && (line[0] == ' ' || line[0] == '\t'))
				{
					line = line.Substr(1);
				}

				if (line.StartsWith("include"))
				{
					bInclude = true;
					line = line.Substr(7);
				}
			}
			else if (line.StartsWith("import ") || line.StartsWith("import\t"))
			{
				bImport = true;
				line = line.Substr(6);
			}

			if (!bInclude && !bImport)
			{
				continue;
			}

			while (!line.IsEmpty() && (line[0] == ' ' || line[0] == '\t'))
			{
				line = line.Substr(1);
			}

			String path(m_Resource);
			if (bInclude || (!line.IsEmpty() && line[0] == '"'))
			{
				if (line.IsEmpty() || (line[0] != '"' && line[0] != '<'))
				{
					return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Macro-generated shader includes are unsupported by immutable source closure discovery."));
				}

				const char terminator = line[0] == '"' ? '"' : '>';
				const usize close = line.Find(terminator, 1);
				if (close == StringView::NPOS || !path.Assign(line.Substr(1, close - 1)))
				{
					return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Malformed shader include path."));
				}
			}
			else
			{
				usize n = 0;

				while (n < line.Size() && line[n] != ';' && line[n] != ' ' && line[n] != '\t' && line[n] != '\r')
				{
					++n;
				}

				for (usize i = 0; i < n; ++i)
				{
					const char character = line[i] == '.' ? '/' : line[i];
					if (!path.Append({&character, 1}))
					{
						return R::Failure(MakeBuildAllocationError());
					}
				}

				if (!path.Append(".slang"))
				{
					return R::Failure(MakeBuildAllocationError());
				}
			}

			Result<void> resolved = resolve(index, path, depth);
			if (!resolved)
			{
				return resolved;
			}
		}

		m_States[index] = 2;

		return R::Success();
	}
} // namespace rox::shader
