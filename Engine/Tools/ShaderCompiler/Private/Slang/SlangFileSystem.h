#pragma once

#include "../ShaderCompilerInternal.h"

#include <ShaderBuildPlan.h>

#include <slang.h>

namespace rox::shader
{
	// Slang callback names and fixed-width return types follow the SDK ABI.
	class SnapshotFileSystem final : public ISlangFileSystemExt
	{
	public:
		SnapshotFileSystem(const ShaderBuildPlan& plan, core::MemoryResource& resource, core::CancellationToken cancellation);
		SlangResult SLANG_MCALL queryInterface(const SlangUUID& guid, void** output) noexcept override;
		void* SLANG_MCALL castAs(const SlangUUID& guid) noexcept override;
		uint32_t SLANG_MCALL addRef() noexcept override;
		uint32_t SLANG_MCALL release() noexcept override;
		SlangResult SLANG_MCALL loadFile(const char* path, ISlangBlob** output) noexcept override;
		SlangResult SLANG_MCALL getFileUniqueIdentity(const char* path, ISlangBlob** output) noexcept override;
		SlangResult SLANG_MCALL calcCombinedPath(SlangPathType fromType, const char* fromPath, const char* path, ISlangBlob** output) noexcept override;
		SlangResult SLANG_MCALL getPathType(const char* path, SlangPathType* output) noexcept override;
		SlangResult SLANG_MCALL getPath(PathKind kind, const char* path, ISlangBlob** output) noexcept override;
		void SLANG_MCALL clearCache() noexcept override;
		SlangResult SLANG_MCALL enumeratePathContents(const char*, FileSystemContentsCallBack, void*) noexcept override;
		OSPathKind SLANG_MCALL getOSPathKind() noexcept override;

	private:
		static SlangResult pathBlob(const core::String& path, ISlangBlob** output) noexcept;

	private:
		const ShaderBuildPlan& m_Plan;
		core::MemoryResource& m_Resource;
		core::CancellationToken m_Cancellation;
	};

	class ModuleFileSystem final : public ISlangFileSystem
	{
	public:
		SlangResult SLANG_MCALL queryInterface(const SlangUUID&, void**) noexcept override;
		void* SLANG_MCALL castAs(const SlangUUID&) noexcept override;
		uint32_t SLANG_MCALL addRef() noexcept override;
		uint32_t SLANG_MCALL release() noexcept override;
		SlangResult SLANG_MCALL loadFile(const char*, ISlangBlob**) noexcept override;
	};

} // namespace rox::shader
