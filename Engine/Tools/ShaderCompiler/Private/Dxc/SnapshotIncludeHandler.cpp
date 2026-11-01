#include "SnapshotIncludeHandler.h"

#include <Types.h>
#include <Utility.hpp>

#if defined(_WIN32)
namespace rox::shader
{
	using namespace core;

	SnapshotIncludeHandler::SnapshotIncludeHandler(const ShaderBuildPlan& plan, IDxcUtils& utils, MemoryResource& resource, CancellationToken cancellation)
		: m_Plan(plan)
		, m_Resource(resource)
		, m_Cancellation(cancellation)
		, m_Utils(utils)
		, m_Aliases(resource)
	{
	}

	Result<void> SnapshotIncludeHandler::Initialize()
	{
		using R = Result<void>;

		for (usize index = 0; index < m_Plan.GetSourceAliasCount(); ++index)
		{
			const ShaderSourceAliasView alias = m_Plan.GetSourceAlias(index);
			Result<void> added = add(alias.Path, alias.SourceIndex, 0);
			if (!added)
			{
				return added;
			}
		}

		for (uint32 index = 0; index < m_Plan.GetSources().Size(); ++index)
		{
			Result<void> added = add(m_Plan.GetSources()[index].Identity, index, 0);
			if (!added)
			{
				return added;
			}
		}

		// DXC exposes candidate paths without their requesting source identity.
		// Propagate only declared include edges through each captured parent alias.
		for (usize index = 0; index < m_Aliases.Size(); ++index)
		{
			if (m_Cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			if (m_Aliases[index].SourceIndex == NO_SHADER_SOURCE || m_Aliases[index].bAmbiguous)
			{
				continue;
			}

			const uint32 source = m_Aliases[index].SourceIndex;
			const uint32 depth = m_Aliases[index].Depth;
			String parent(m_Resource);
			if (!parent.Assign(m_Aliases[index].Path))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			for (usize edgeIndex = 0; edgeIndex < m_Plan.GetSourceIncludeCount(); ++edgeIndex)
			{
				const ShaderSourceIncludeView edge = m_Plan.GetSourceInclude(edgeIndex);
				if (edge.Requester != source)
				{
					continue;
				}

				if (depth >= m_Plan.GetRequest().Limits.MaximumTraversalDepth)
				{
					return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXC native include aliases exceed the traversal bound."));
				}

				Result<String> path = CanonicalPath(parent, edge.RequestedPath, m_Resource);
				if (!path)
				{
					return R::Failure(path.GetError());
				}

				Result<void> added = add(path.Value(), edge.SourceIndex, depth + 1);
				if (!added)
				{
					return added;
				}
			}
		}

		return R::Success();
	}

	HRESULT STDMETHODCALLTYPE SnapshotIncludeHandler::QueryInterface(REFIID iid, void** output)
	{
		if (output == nullptr)
		{
			return E_POINTER;
		}

		*output = nullptr;
		if (!IsEqualIID(iid, __uuidof(IUnknown)) && !IsEqualIID(iid, __uuidof(IDxcIncludeHandler)))
		{
			return E_NOINTERFACE;
		}

		*output = static_cast<IDxcIncludeHandler*>(this);
		AddRef();

		return S_OK;
	}

	ULONG STDMETHODCALLTYPE SnapshotIncludeHandler::AddRef()
	{
		return 1;
	}

	ULONG STDMETHODCALLTYPE SnapshotIncludeHandler::Release()
	{
		return 1;
	}

	HRESULT STDMETHODCALLTYPE SnapshotIncludeHandler::LoadSource(LPCWSTR path, IDxcBlob** output)
	{
		if (output == nullptr)
		{
			return E_POINTER;
		}

		*output = nullptr;
		if (path == nullptr || m_Cancellation.IsCancellationRequested())
		{
			return E_FAIL;
		}

		usize count = 0;

		while (count <= 8192 && path[count] != 0)
		{
			++count;
		}

		if (count == 0 || count > 8192)
		{
			return E_INVALIDARG;
		}

		const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, static_cast<int>(count), nullptr, 0, nullptr, nullptr);
		if (length <= 0 || length > 16384)
		{
			return E_INVALIDARG;
		}

		Array<char> bytes(m_Resource);
		if (!bytes.Resize(static_cast<usize>(length)))
		{
			m_bAllocationFailure = true;

			return E_OUTOFMEMORY;
		}

		if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, static_cast<int>(count), bytes.Data(), length, nullptr, nullptr) != length)
		{
			return E_INVALIDARG;
		}

		// DXC uses native separators in callback paths; virtual identities use slashes.
		for (char& character : bytes)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}

		const StringView requested(bytes.Data(), bytes.Size());
		Result<String> canonical = CanonicalPath({}, requested, m_Resource);
		if (!canonical)
		{
			m_bAllocationFailure = canonical.GetError().Code == EErrorCode::ExternalFailure;

			return E_FAIL;
		}

		bool bKnown = false;
		const ShaderSourceSnapshot* snapshot = find(canonical.Value(), bKnown);
		if (m_bAmbiguousRequest)
		{
			return E_FAIL;
		}

		if (snapshot == nullptr && !bKnown)
		{
			Result<String> relative = CanonicalPath(m_Plan.GetRequest().Root.Identity, requested, m_Resource);
			if (!relative)
			{
				m_bAllocationFailure = relative.GetError().Code == EErrorCode::ExternalFailure;

				return E_FAIL;
			}

			snapshot = find(relative.Value(), bKnown);
			if (m_bAmbiguousRequest)
			{
				return E_FAIL;
			}
		}

		if (snapshot == nullptr)
		{
			return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
		}

		IDxcBlobEncoding* blob = nullptr;
		const HRESULT result = m_Utils.CreateBlob(snapshot->Source.Data(), static_cast<UINT32>(snapshot->Source.Size()), DXC_CP_UTF8, &blob);
		if (FAILED(result))
		{
			m_bAllocationFailure = result == E_OUTOFMEMORY;

			return result;
		}

		*output = blob;

		return S_OK;
	}

	Result<void> SnapshotIncludeHandler::add(StringView path, uint32 source, uint32 depth)
	{
		using R = Result<void>;

		for (SnapshotIncludeHandler::NativeAlias& alias : m_Aliases)
		{
			if (alias.Path.View() != path)
			{
				continue;
			}

			if (alias.SourceIndex != source)
			{
				alias.bAmbiguous = true;
			}

			return R::Success();
		}

		if (m_Aliases.Size() >= uint64(m_Plan.GetRequest().Limits.MaximumSources) * 8)
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXC native include aliases exceed the source bound."));
		}

		NativeAlias alias(m_Resource);
		alias.SourceIndex = source;
		alias.Depth = depth;
		if (!alias.Path.Assign(path) || !m_Aliases.Add(Move(alias)))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		return R::Success();
	}

	const ShaderSourceSnapshot* SnapshotIncludeHandler::find(StringView path, bool& bKnown)
	{
		for (const SnapshotIncludeHandler::NativeAlias& alias : m_Aliases)
		{
			if (alias.Path.View() != path)
			{
				continue;
			}

			bKnown = true;
			if (alias.bAmbiguous)
			{
				m_bAmbiguousRequest = true;

				return nullptr;
			}

			return alias.SourceIndex == NO_SHADER_SOURCE ? nullptr : &m_Plan.GetSources()[alias.SourceIndex];
		}

		return nullptr;
	}
} // namespace rox::shader
#endif
