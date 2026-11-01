#pragma once

#include <Cancellation.h>
#include <ShaderCompiler.h>

namespace rox::shader
{
	struct SourceAlias
	{
		core::String Path;
		core::uint32 SourceIndex = 0;

		explicit SourceAlias(core::MemoryResource& resource)
			: Path(resource)
		{
		}
	};

	inline constexpr core::uint32 NO_SHADER_SOURCE = 0xffffffffu;

	struct SourceIncludeEdge
	{
		core::uint32 Requester = NO_SHADER_SOURCE;
		core::uint32 SourceIndex = NO_SHADER_SOURCE;
		core::String RequestedPath;

		explicit SourceIncludeEdge(core::MemoryResource& resource)
			: RequestedPath(resource)
		{
		}

		SourceIncludeEdge(SourceIncludeEdge&&) noexcept = default;
		SourceIncludeEdge& operator=(SourceIncludeEdge&&) noexcept = default;
		SourceIncludeEdge(const SourceIncludeEdge&) = delete;
		SourceIncludeEdge& operator=(const SourceIncludeEdge&) = delete;
	};

	class SourceClosure
	{
	public:
		SourceClosure(const ShaderCompileRequest& request, IShaderSourceResolver& resolver, core::MemoryResource& resource, core::CancellationToken cancellation) noexcept;
		SourceClosure(const ShaderCompileRequest& request, core::MemoryResource& resource) noexcept;
		core::Result<void> Initialize();
		void Freeze() noexcept;

		const ShaderSourceSnapshot* Find(core::StringView path) const;

		const core::Array<ShaderSourceSnapshot>& GetSources() const noexcept
		{
			return m_Sources;
		}

		const core::Array<SourceAlias>& GetAliases() const noexcept
		{
			return m_Aliases;
		}

		const core::Array<SourceIncludeEdge>& GetIncludeEdges() const noexcept
		{
			return m_IncludeEdges;
		}

		const ShaderCompileRequest& GetRequest() const noexcept
		{
			return m_Request;
		}

		core::uint64 GetOwnedByteCount() const noexcept;

	private:
		core::uint32 findSource(core::StringView path) const;
		core::uint32 findAlias(core::StringView path) const;
		core::Result<core::uint32> add(const ShaderSourceSnapshot& snapshot, core::StringView requestedAlias);
		core::Result<void> recordEdge(core::uint32 requester, core::uint32 source, core::StringView requested);
		core::Result<void> visitRecursive(core::uint32 index, core::uint32 depth);
		core::Result<void> resolve(core::uint32 requester, core::StringView path, core::uint32 depth);

	private:
		core::Array<ShaderSourceSnapshot> m_Sources;
		core::Array<core::uint8> m_States;
		core::Array<SourceAlias> m_Aliases;
		core::Array<SourceIncludeEdge> m_IncludeEdges;
		core::Array<core::uint32> m_SourceIndex;
		core::Array<core::uint32> m_AliasIndex;

		const ShaderCompileRequest& m_Request;
		IShaderSourceResolver* m_Resolver;
		core::MemoryResource& m_Resource;
		core::CancellationToken m_Cancellation;

		core::uint64 m_TotalBytes = 0;
	};

} // namespace rox::shader
