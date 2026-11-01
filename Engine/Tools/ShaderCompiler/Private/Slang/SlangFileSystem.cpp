#include "SlangFileSystem.h"

#include <ByteOperations.hpp>
#include <ByteReader.h>
#include <StringView.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>

#include <slang-com-ptr.h>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		bool sameGuid(const SlangUUID& a, const SlangUUID& b)
		{
			return CompareBytes(&a, &b, sizeof(a)) == 0;
		}

	} // namespace

	SnapshotFileSystem::SnapshotFileSystem(const ShaderBuildPlan& plan, MemoryResource& resource, CancellationToken cancellation)
		: m_Plan(plan)
		, m_Resource(resource)
		, m_Cancellation(cancellation)
	{
	}

	SlangResult SLANG_MCALL SnapshotFileSystem::queryInterface(const SlangUUID& guid, void** output) noexcept
	{
		if (output == nullptr)
		{
			return SLANG_E_INVALID_ARG;
		}

		*output = castAs(guid);

		return *output != nullptr ? SLANG_OK : SLANG_E_NO_INTERFACE;
	}

	void* SLANG_MCALL SnapshotFileSystem::castAs(const SlangUUID& guid) noexcept
	{
		return sameGuid(guid, ISlangUnknown::getTypeGuid()) || sameGuid(guid, ISlangCastable::getTypeGuid()) || sameGuid(guid, ISlangFileSystem::getTypeGuid()) ||
				sameGuid(guid, ISlangFileSystemExt::getTypeGuid())
			? static_cast<ISlangFileSystemExt*>(this)
			: nullptr;
	}

	uint32_t SLANG_MCALL SnapshotFileSystem::addRef() noexcept
	{
		return 1;
	}

	uint32_t SLANG_MCALL SnapshotFileSystem::release() noexcept
	{
		return 1;
	}

	SlangResult SLANG_MCALL SnapshotFileSystem::loadFile(const char* path, ISlangBlob** output) noexcept
	{
		if (output == nullptr)
		{
			return SLANG_E_INVALID_ARG;
		}

		*output = nullptr;
		if (path == nullptr || m_Cancellation.IsCancellationRequested())
		{
			return SLANG_FAIL;
		}

		Result<String> canonical = CanonicalPath({}, StringView(path), m_Resource);
		if (!canonical)
		{
			return SLANG_E_NOT_FOUND;
		}

		const ShaderSourceSnapshot* snapshot = m_Plan.FindSource(canonical.Value());
		if (snapshot == nullptr)
		{
			return SLANG_E_NOT_FOUND;
		}

		// The SDK owns this boundary copy and releases it before the request-local session dies.
		*output = slang_createBlob(snapshot->Source.CStr(), snapshot->Source.Size() == 0 ? 1 : snapshot->Source.Size());

		return *output != nullptr ? SLANG_OK : SLANG_FAIL;
	}

	SlangResult SLANG_MCALL SnapshotFileSystem::getFileUniqueIdentity(const char* path, ISlangBlob** output) noexcept
	{
		return getPath(PathKind::Canonical, path, output);
	}

	SlangResult SLANG_MCALL SnapshotFileSystem::calcCombinedPath(SlangPathType fromType, const char* fromPath, const char* path, ISlangBlob** output) noexcept
	{
		if (output == nullptr || fromPath == nullptr || path == nullptr)
		{
			return SLANG_E_INVALID_ARG;
		}

		*output = nullptr;
		if (m_Cancellation.IsCancellationRequested())
		{
			return SLANG_FAIL;
		}

		String directory(m_Resource);
		StringView base{StringView(fromPath)};
		if (fromType == SLANG_PATH_TYPE_FILE)
		{
			// Relative includes belong to the resolved source identity, including redirects.
			Result<String> canonical = CanonicalPath({}, base, m_Resource);
			if (!canonical)
			{
				return SLANG_E_NOT_FOUND;
			}

			if (const ShaderSourceSnapshot* snapshot = m_Plan.FindSource(canonical.Value()))
			{
				base = snapshot->Identity;
				const ShaderIncludeResolution resolved = m_Plan.ResolveInclude(base, StringView(path));
				if (resolved.Status == EShaderIncludeResolution::Missing)
				{
					return SLANG_E_NOT_FOUND;
				}

				if (resolved.Status == EShaderIncludeResolution::Resolved)
				{
					return pathBlob(resolved.Source->Identity, output);
				}
			}
		}
		else if (fromType == SLANG_PATH_TYPE_DIRECTORY)
		{
			if (!directory.Assign(base) || (!directory.IsEmpty() && !directory.Append("/")) || !directory.Append(StringView(path)))
			{
				return SLANG_FAIL;
			}

			base = {};
		}
		else
		{
			return SLANG_E_INVALID_ARG;
		}

		Result<String> combined = CanonicalPath(base, fromType == SLANG_PATH_TYPE_DIRECTORY ? directory.View() : StringView(path), m_Resource);
		if (!combined)
		{
			return SLANG_E_NOT_FOUND;
		}

		return pathBlob(combined.Value(), output);
	}

	SlangResult SLANG_MCALL SnapshotFileSystem::getPathType(const char* path, SlangPathType* output) noexcept
	{
		if (path == nullptr || output == nullptr)
		{
			return SLANG_E_INVALID_ARG;
		}

		const StringView raw{StringView(path)};
		if (raw.IsEmpty() || raw == StringView("."))
		{
			*output = SLANG_PATH_TYPE_DIRECTORY;

			return SLANG_OK;
		}

		Result<String> canonical = CanonicalPath({}, raw, m_Resource);
		if (!canonical)
		{
			return SLANG_E_NOT_FOUND;
		}

		if (m_Plan.FindSource(canonical.Value()) != nullptr)
		{
			*output = SLANG_PATH_TYPE_FILE;

			return SLANG_OK;
		}

		const StringView directory = canonical.Value().View();
		auto contains = [&](StringView file)
		{
			return file.Size() > directory.Size() && file.StartsWith(directory) && file[directory.Size()] == '/';
		};

		for (const ShaderSourceSnapshot& source : m_Plan.GetSources())
		{
			if (contains(source.Identity))
			{
				*output = SLANG_PATH_TYPE_DIRECTORY;

				return SLANG_OK;
			}
		}

		for (usize index = 0; index < m_Plan.GetSourceAliasCount(); ++index)
		{
			if (contains(m_Plan.GetSourceAlias(index).Path))
			{
				*output = SLANG_PATH_TYPE_DIRECTORY;

				return SLANG_OK;
			}
		}

		return SLANG_E_NOT_FOUND;
	}

	SlangResult SLANG_MCALL SnapshotFileSystem::getPath(PathKind kind, const char* path, ISlangBlob** output) noexcept
	{
		if (path == nullptr || output == nullptr)
		{
			return SLANG_E_INVALID_ARG;
		}

		*output = nullptr;
		if (kind == PathKind::OperatingSystem)
		{
			return SLANG_E_NOT_IMPLEMENTED;
		}

		if (kind != PathKind::Simplified && kind != PathKind::Canonical && kind != PathKind::Display)
		{
			return SLANG_E_INVALID_ARG;
		}

		Result<String> canonical = CanonicalPath({}, StringView(path), m_Resource);
		if (!canonical)
		{
			return SLANG_E_NOT_FOUND;
		}

		if (const ShaderSourceSnapshot* snapshot = m_Plan.FindSource(canonical.Value()))
		{
			return pathBlob(snapshot->Identity, output);
		}

		return kind == PathKind::Canonical ? SLANG_E_NOT_FOUND : pathBlob(canonical.Value(), output);
	}

	void SLANG_MCALL SnapshotFileSystem::clearCache() noexcept
	{
	}

	SlangResult SLANG_MCALL SnapshotFileSystem::enumeratePathContents(const char*, FileSystemContentsCallBack, void*) noexcept
	{
		return SLANG_E_NOT_IMPLEMENTED;
	}

	::OSPathKind SLANG_MCALL SnapshotFileSystem::getOSPathKind() noexcept
	{
		return OSPathKind::None;
	}

	SlangResult SnapshotFileSystem::pathBlob(const String& path, ISlangBlob** output) noexcept
	{
		*output = slang_createBlob(path.CStr(), path.Size() + 1);

		return *output != nullptr ? SLANG_OK : SLANG_FAIL;
	}
} // namespace rox::shader

namespace rox::shader
{
	using namespace core;

	SlangResult SLANG_MCALL ModuleFileSystem::queryInterface(const SlangUUID& guid, void** output) noexcept
	{
		if (output == nullptr)
		{
			return SLANG_E_INVALID_ARG;
		}

		*output = castAs(guid);

		return *output != nullptr ? SLANG_OK : SLANG_E_NO_INTERFACE;
	}

	void* SLANG_MCALL ModuleFileSystem::castAs(const SlangUUID& guid) noexcept
	{
		const SlangUUID unknown = ISlangUnknown::getTypeGuid();
		const SlangUUID castable = ISlangCastable::getTypeGuid();
		const SlangUUID filesystem = ISlangFileSystem::getTypeGuid();

		return CompareBytes(&guid, &unknown, sizeof(guid)) == 0 || CompareBytes(&guid, &castable, sizeof(guid)) == 0 || CompareBytes(&guid, &filesystem, sizeof(guid)) == 0
			? static_cast<ISlangFileSystem*>(this)
			: nullptr;
	}

	uint32_t SLANG_MCALL ModuleFileSystem::addRef() noexcept
	{
		return 1;
	}

	uint32_t SLANG_MCALL ModuleFileSystem::release() noexcept
	{
		return 1;
	}

	SlangResult SLANG_MCALL ModuleFileSystem::loadFile(const char*, ISlangBlob** output) noexcept
	{
		if (output == nullptr)
		{
			return SLANG_E_INVALID_ARG;
		}

		*output = nullptr;

		return SLANG_E_NOT_FOUND;
	}
} // namespace rox::shader
