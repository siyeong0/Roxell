#include "SlangCompiler.h"

#include "../ShaderCompilerInternal.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	Result<Slang::ComPtr<slang::ISession>> SlangCompiler::createSession(const ShaderCompileRequest& request, ISlangFileSystem* filesystem, MemoryResource& resource)
	{
		using R = Result<Slang::ComPtr<slang::ISession>>;

		ASSERT(filesystem != nullptr, "Slang request sessions require explicit application source authority.");
		slang::IGlobalSession* global = m_GlobalSession.get();
		Array<slang::TargetDesc> targets(resource);
		Array<const char*> roots(resource);
		Array<slang::PreprocessorMacroDesc> macros(resource);

		for (const ShaderTargetProfile& profile : request.Targets)
		{
			slang::TargetDesc target;
			target.floatingPointMode = request.bPreciseFloatingPoint ? SLANG_FLOATING_POINT_MODE_PRECISE : SLANG_FLOATING_POINT_MODE_DEFAULT;
			target.format = profile.Target == shader::EShaderTarget::Dxil ? SLANG_DXIL : SLANG_SPIRV;
			target.profile = global->findProfile(
				profile.Target == shader::EShaderTarget::Dxil
					? (!request.EntryPoints.IsEmpty() && request.EntryPoints[0].Stage >= shader::EShaderStage::RayGeneration ? "lib_6_6" : "sm_6_6")
					: "spirv_1_6");
			if (target.profile == SLANG_PROFILE_UNKNOWN || !targets.Add(target))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		for (const String& root : request.IncludeRoots)
		{
			if (!roots.Add(root.CStr()))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		for (const ShaderDefine& define : request.Defines)
		{
			if (!macros.Add({define.Name.CStr(), define.Value.CStr()}))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		slang::CompilerOptionEntry options[7] = {};
		uint32 optionCount = 0;
		auto option = [&](slang::CompilerOptionName name, int value)
		{
			ASSERT(optionCount < Size(options), "Slang typed options must fit the fixed option storage.");
			options[optionCount].name = name;
			options[optionCount++].value.intValue0 = value;
		};

		option(slang::CompilerOptionName::Optimization, static_cast<int>(request.Optimization));
		option(slang::CompilerOptionName::DebugInformation, request.DebugInfo == EShaderDebugInfo::None ? SLANG_DEBUG_INFO_LEVEL_NONE : SLANG_DEBUG_INFO_LEVEL_STANDARD);
		option(slang::CompilerOptionName::VulkanUseEntryPointName, 1);
		option(slang::CompilerOptionName::PreserveParameters, 1);
		option(slang::CompilerOptionName::Language, request.Language == EShaderSourceLanguage::Hlsl ? SLANG_SOURCE_LANGUAGE_HLSL : SLANG_SOURCE_LANGUAGE_SLANG);
		option(slang::CompilerOptionName::LanguageVersion, SLANG_LANGUAGE_VERSION_2025);
		if (request.bWarningsAsErrors)
		{
			options[optionCount].name = slang::CompilerOptionName::WarningsAsErrors;
			options[optionCount].value.kind = slang::CompilerOptionValueKind::String;
			options[optionCount++].value.stringValue0 = "all";
		}

		slang::SessionDesc desc;
		desc.targets = targets.Data();
		desc.targetCount = static_cast<SlangInt>(targets.Size());
		desc.fileSystem = filesystem;
		desc.searchPaths = roots.Data();
		desc.searchPathCount = static_cast<SlangInt>(roots.Size());
		desc.preprocessorMacros = macros.Data();
		desc.preprocessorMacroCount = static_cast<SlangInt>(macros.Size());
		desc.defaultMatrixLayoutMode = request.MatrixOrder == shader::EShaderMatrixOrder::RowMajor ? SLANG_MATRIX_LAYOUT_ROW_MAJOR : SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
		desc.compilerOptionEntries = options;
		desc.compilerOptionEntryCount = optionCount;
		Slang::ComPtr<slang::ISession> session;
		if (SLANG_FAILED(global->createSession(desc, session.writeRef())))
		{
			return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "Pinned Slang request session initialization failed."));
		}

		return R::Success(Move(session));
	}
} // namespace rox::shader
