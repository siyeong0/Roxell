#pragma once

#include "../Dxil/DxcPointer.hpp"
#include "../ShaderCompilerInternal.h"

#include <ShaderBuildPlan.h>

#if defined(_WIN32)
namespace rox::shader
{
	// COM callback names and native result types follow the DXC ABI.
	class SnapshotIncludeHandler final : public IDxcIncludeHandler
	{
		struct NativeAlias final
		{
			core::String Path;
			core::uint32 SourceIndex = NO_SHADER_SOURCE;
			core::uint32 Depth = 0;
			bool bAmbiguous = false;

			explicit NativeAlias(core::MemoryResource& resource) noexcept
				: Path(resource)
			{
			}
		};

	public:
		SnapshotIncludeHandler(const ShaderBuildPlan& plan, IDxcUtils& utils, core::MemoryResource& resource, core::CancellationToken cancellation);
		core::Result<void> Initialize();

		bool HasAllocationFailure() const noexcept
		{
			return m_bAllocationFailure;
		}

		bool HasAmbiguousRequest() const noexcept
		{
			return m_bAmbiguousRequest;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override;
		ULONG STDMETHODCALLTYPE AddRef() override;
		ULONG STDMETHODCALLTYPE Release() override;
		HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR path, IDxcBlob** output) override;

	private:
		core::Result<void> add(core::StringView path, core::uint32 source, core::uint32 depth);
		const ShaderSourceSnapshot* find(core::StringView path, bool& bKnown);

	private:
		const ShaderBuildPlan& m_Plan;
		core::MemoryResource& m_Resource;
		core::CancellationToken m_Cancellation;
		IDxcUtils& m_Utils;
		core::Array<NativeAlias> m_Aliases;
		bool m_bAllocationFailure = false;
		bool m_bAmbiguousRequest = false;
	};

} // namespace rox::shader
#endif
