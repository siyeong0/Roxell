#include "ShaderCompilerInternal.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <Cancellation.h>
#include <Sha256.h>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	Result<void> ValidateRequest(const ShaderCompileRequest& request, EShaderCompiler compiler)
	{
		using R = Result<void>;

		if (!IsCanonicalIdentity(request.Root.Identity) ||
			request.Root.Source.IsEmpty() ||
			request.Root.Source.Size() > request.Limits.MaximumSourceBytes ||
			request.Root.Source.Size() > request.Limits.MaximumAggregateSourceBytes ||
			!request.Limits.MaximumSources ||
			compiler > EShaderCompiler::Dxc ||
			request.Purpose > EShaderSourceBuildPurpose::Module ||
			(request.Purpose == EShaderSourceBuildPurpose::Program && request.EntryPoints.IsEmpty()) ||
			request.EntryPoints.Size() > 256 ||
			request.Targets.IsEmpty() ||
			request.Targets.Size() > 2 ||
			request.Defines.Size() > 1024 ||
			request.IncludeRoots.Size() > 128 ||
			request.Optimization > EShaderOptimization::Maximum ||
			request.DebugInfo > EShaderDebugInfo::Standard ||
			(request.MatrixOrder != shader::EShaderMatrixOrder::RowMajor && request.MatrixOrder != shader::EShaderMatrixOrder::ColumnMajor) ||
			request.Language > EShaderSourceLanguage::Hlsl ||
			request.LanguageVersion != 2021 ||
			request.EngineProfileVersion != 1)
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Invalid shader compile request or typed options."));
		}

		if (compiler == EShaderCompiler::Dxc && request.Language != EShaderSourceLanguage::Hlsl)
		{
			return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXC accepts HLSL snapshots only."));
		}

		if (request.Purpose == EShaderSourceBuildPurpose::Module &&
			(!request.Limits.MaximumModules || request.Limits.MaximumModules > 1024 || !request.Limits.MaximumModuleBytes ||
				request.Limits.MaximumModuleBytes > 64u * 1024u * 1024u || !request.Limits.MaximumAggregateModuleBytes ||
				request.Limits.MaximumAggregateModuleBytes > 1024ull * 1024ull * 1024ull))
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module limits exceed the bounded IR profile."));
		}

		for (usize i = 0; i < request.EntryPoints.Size(); ++i)
		{
			const ShaderEntryPoint& entry = request.EntryPoints[i];
			if (entry.Name.IsEmpty() || entry.Name.Size() > 4096 || entry.Name.View().Contains('\0'))
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Invalid shader entry name."));
			}

			if (entry.Stage != shader::EShaderStage::Compute &&
				entry.Stage != shader::EShaderStage::Vertex &&
				entry.Stage != shader::EShaderStage::Pixel &&
				!(compiler == EShaderCompiler::Slang &&
					(entry.Stage == shader::EShaderStage::Mesh || entry.Stage == shader::EShaderStage::Task ||
						(entry.Stage >= shader::EShaderStage::RayGeneration && entry.Stage <= shader::EShaderStage::Callable))))
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "This compiler profile admits tested compute, vertex and pixel stages."));
			}

			for (usize j = 0; j < i; ++j)
			{
				if (request.EntryPoints[j].Name == entry.Name)
				{
					return R::Failure(MakeBuildError(EErrorCode::Conflict, "Duplicate requested shader entry name."));
				}
			}
		}

		for (usize i = 0; i < request.Targets.Size(); ++i)
		{
			const ShaderTargetProfile& target = request.Targets[i];
			if (target.Target > shader::EShaderTarget::SpirV ||
				target.ProfileVersion != 1 ||
				target.ShaderModelMajor != 6 ||
				target.ShaderModelMinor != 6 ||
				target.SpirVMajor != 1 ||
				target.SpirVMinor != 6 ||
				target.EnvironmentMajor != 1 ||
				target.EnvironmentMinor != 3)
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Requested shader target profile is unsupported by this compiler adapter."));
			}

			if (compiler == EShaderCompiler::Dxc &&
				target.Target == shader::EShaderTarget::SpirV &&
				(request.DebugInfo != EShaderDebugInfo::None ||
					AnyOf(request.EntryPoints.begin(), request.EntryPoints.end(), [](const ShaderEntryPoint& entry) { return entry.Stage != shader::EShaderStage::Compute; })))
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXC SPIR-V accepts only compute entries without debug instrumentation."));
			}

			if (compiler == EShaderCompiler::Dxc && target.Target == shader::EShaderTarget::SpirV && request.bPreciseFloatingPoint)
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXC precise SPIR-V requires unrepresented SignedZeroInfNanPreserve device capabilities."));
			}

			for (usize j = 0; j < i; ++j)
			{
				if (request.Targets[j] == target)
				{
					return R::Failure(MakeBuildError(EErrorCode::Conflict, "Duplicate requested shader target."));
				}
			}
		}

		for (const String& root : request.IncludeRoots)
		{
			if (!IsCanonicalIdentity(root))
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Invalid ordered virtual include root."));
			}
		}

		for (usize i = 0; i < request.Defines.Size(); ++i)
		{
			const ShaderDefine& define = request.Defines[i];
			if (define.Name.IsEmpty() ||
				define.Name.Size() > 4096 ||
				define.Value.Size() > 4096 ||
				define.Value.View().Contains('\0') ||
				define.Value.View().Contains('\n') ||
				define.Value.View().Contains('\r'))
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Invalid shader define."));
			}

			for (usize c = 0; c < define.Name.Size(); ++c)
			{
				const char character = define.Name[c];
				if (!((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || character == '_' || (c != 0 && character >= '0' && character <= '9')))
				{
					return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Shader define name is not an identifier."));
				}
			}

			for (usize j = 0; j < i; ++j)
			{
				if (request.Defines[j].Name == define.Name)
				{
					return R::Failure(MakeBuildError(EErrorCode::Conflict, "Duplicate shader define name."));
				}
			}
		}

		return R::Success();
	}

	Result<void> ValidateProfiles(const ShaderCompileRequest& request, Span<const ShaderCompilerProfile> profiles, uint32 abi)
	{
		const bool bRay = (!request.EntryPoints.IsEmpty() && request.EntryPoints[0].Stage >= shader::EShaderStage::RayGeneration);
		if (!bRay && request.RayDeclaredLimits != shader::ShaderRayDeclaredLimits{})
		{
			return Result<void>::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Ray declared limits require an exact ray compiler profile."));
		}

		if (!request.LocalRootRecipes.IsEmpty())
		{
			if (!bRay ||
				request.LocalRootRecipes.Size() > request.EntryPoints.Size() ||
				NoneOf(request.Targets.begin(), request.Targets.end(), [](const ShaderTargetProfile& target) { return target.Target == shader::EShaderTarget::Dxil; }))
			{
				return Result<void>::Failure(MakeBuildError(EErrorCode::Unsupported, "Declared local root recipes require the current DXIL ray profile."));
			}

			uint64 argumentCount = 0;

			for (usize index = 0; index < request.LocalRootRecipes.Size(); ++index)
			{
				const ShaderLocalRootRecipe& recipe = request.LocalRootRecipes[index];
				argumentCount += recipe.Arguments.Size();
				if (recipe.Arguments.IsEmpty() ||
					argumentCount > 4096 ||
					NoneOf(request.EntryPoints.begin(), request.EntryPoints.end(), [&](const ShaderEntryPoint& entry) { return entry.Name == recipe.EntryPointName; }))
				{
					return Result<void>::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Invalid local root recipe export or argument count."));
				}

				for (usize previous = 0; previous < index; ++previous)
				{
					if (request.LocalRootRecipes[previous].EntryPointName == recipe.EntryPointName)
					{
						return Result<void>::Failure(MakeBuildError(EErrorCode::Conflict, "Duplicate local root recipe export."));
					}
				}

				for (const ShaderLocalRootArgumentDeclaration& argument : recipe.Arguments)
				{
					const bool constants = argument.Kind == shader::EShaderLocalRootArgumentKind::Constants32;
					if (argument.Kind > shader::EShaderLocalRootArgumentKind::DescriptorTable ||
						argument.BindingName.IsEmpty() ||
						argument.BindingName.Size() > 4096 ||
						argument.BindingName.View().Contains('\0') ||
						!IsValidUtf8(argument.BindingName) ||
						(constants ? argument.ValueCount == 0 || argument.ValueCount > shader::MAXIMUM_DXIL_LOCAL_ROOT_BYTES / 4 : argument.ValueCount != 0))
					{
						return Result<void>::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Invalid local root argument declaration."));
					}
				}
			}
		}

		for (const ShaderEntryPoint& entry : request.EntryPoints)
		{
			const bool bRayStage = entry.Stage >= shader::EShaderStage::RayGeneration && entry.Stage <= shader::EShaderStage::Callable;
			if (bRay != bRayStage)
			{
				return Result<void>::Failure(MakeBuildError(EErrorCode::Unsupported, "Ray profiles accept only ray exports; other profiles do not accept ray exports."));
			}
		}

		for (const ShaderTargetProfile& target : request.Targets)
		{
			if (request.Purpose == EShaderSourceBuildPurpose::Module)
			{
				const auto moduleProfile = FindIf(
					profiles.begin(), profiles.end(),
					[&](const ShaderCompilerProfile& profile)
					{
						return profile.Language == request.Language && profile.Target == target && profile.LanguageVersion == request.LanguageVersion &&
							profile.EngineProfileVersion == request.EngineProfileVersion && profile.ShaderAbiVersion == abi && profile.bModuleCompilation;
					});
				if (moduleProfile == profiles.end())
				{
					return Result<void>::Failure(MakeBuildError(EErrorCode::Unsupported, "Module compilation is unavailable for this exact profile."));
				}
			}

			for (const ShaderEntryPoint& entry : request.EntryPoints)
			{
				const auto found = FindIf(
					profiles.begin(), profiles.end(),
					[&](const ShaderCompilerProfile& profile)
					{
						return profile.Language == request.Language && profile.Target == target && profile.LanguageVersion == request.LanguageVersion &&
							profile.EngineProfileVersion == request.EngineProfileVersion && profile.ShaderAbiVersion == abi &&
							(profile.StageMask & (uint64{1} << static_cast<uint32>(entry.Stage))) != 0;
					});
				if (found == profiles.end())
				{
					return Result<void>::Failure(MakeBuildError(
						EErrorCode::Unsupported,
						"The exact requested language, target and stage tuple "
						"is unavailable in this compiler."));
				}
			}
		}

		return Result<void>::Success();
	}

	Result<void> ValidatePlan(const ShaderBuildPlan& plan, const IShaderCompiler& compiler)
	{
		ASSERT(plan.IsValid(), "Compilation requires a valid prepared shader plan.");

		if (!(plan.GetToolchainIdentity() == compiler.GetToolchainIdentity()))
		{
			return Result<void>::Failure(MakeBuildError(EErrorCode::Conflict, "Shader plan and compiler toolchain identities do not match."));
		}

		return ValidateProfiles(plan.GetRequest(), compiler.GetProfiles(), compiler.GetToolchainIdentity().ShaderAbiVersion);
	}

	Result<void> PrepareBuildIdentity(
		ShaderBuildOutput& output,
		const ShaderCompileRequest& request,
		const SourceClosure& closure,
		const ShaderToolchainIdentity& toolchain,
		MemoryResource& resource,
		CancellationToken cancellation)
	{
		using R = Result<void>;

		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		Sha256 identity;
		usize hashedBytes = 0;
		bool bValid = true;
		auto raw = [&](const void* source, usize count)
		{
			ASSERT(count == 0 || source != nullptr, "Nonempty shader build identity writes require source bytes.");
			if (!bValid || count > 64u * 1024u * 1024u - hashedBytes)
			{
				bValid = false;
				return;
			}

			identity.Update({static_cast<const Byte*>(source), count});
			hashedBytes += count;
		};

		auto u32 = [&](uint32 value)
		{
			Byte wordBytes[4];
			ByteWriter writer({wordBytes, 4});
			writer.WriteU32LE(value);
			raw(wordBytes, 4);
		};

		auto text = [&](StringView value)
		{
			u32(static_cast<uint32>(value.Size()));
			raw(value.Data(), value.Size());
		};

		text("RX-SHADER-BUILD");
		text(toolchain.SdkIdentity);
		text(toolchain.CompilerVersion);
		u32(static_cast<uint32>(request.Purpose));
		u32(toolchain.AdapterVersion);
		u32(static_cast<uint32>(toolchain.Compiler));
		u32(static_cast<uint32>(request.Language));
		u32(request.LanguageVersion);
		u32(request.EngineProfileVersion);
		u32(toolchain.ShaderAbiVersion);
		u32(static_cast<uint32>(request.Optimization));
		u32(static_cast<uint32>(request.DebugInfo));
		u32(static_cast<uint32>(request.MatrixOrder));
		u32(request.bWarningsAsErrors ? 1 : 0);
		u32(request.bPreciseFloatingPoint ? 1 : 0);
		{
			u32(request.RayDeclaredLimits.MaximumPayloadBytes);
			u32(request.RayDeclaredLimits.MaximumAttributeBytes);
			u32(request.RayDeclaredLimits.MaximumCallableBytes);
		}

		{
			u32(static_cast<uint32>(request.LocalRootRecipes.Size()));

			for (const ShaderLocalRootRecipe& recipe : request.LocalRootRecipes)
			{
				text(recipe.EntryPointName);
				u32(static_cast<uint32>(recipe.Arguments.Size()));

				for (const ShaderLocalRootArgumentDeclaration& argument : recipe.Arguments)
				{
					u32(static_cast<uint32>(argument.Kind));
					text(argument.BindingName);
					u32(argument.ValueCount);
				}
			}
		}

		text(request.Root.Identity);
		Array<uint32> indices(resource);

		if (!indices.Resize(closure.GetSources().Size()))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		for (uint32 i = 0; i < indices.Size(); ++i)
		{
			indices[i] = i;
		}

		if (indices.Size() > 1)
		{
			Sort(indices.begin(), indices.end(), [&](uint32 a, uint32 b) { return closure.GetSources()[a].Identity.View() < closure.GetSources()[b].Identity.View(); });
		}

		u32(static_cast<uint32>(indices.Size()));

		for (uint32 i : indices)
		{
			const ShaderSourceSnapshot& snapshot = closure.GetSources()[i];
			text(snapshot.Identity);
			text(snapshot.Source);
			raw(snapshot.ContentHash.Bytes.data(), 32);
			ShaderDependency dependency(resource);
			dependency.ContentHash = snapshot.ContentHash;
			if (!dependency.Identity.Assign(snapshot.Identity) || !output.Dependencies.Add(Move(dependency)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		if (!indices.Resize(closure.GetAliases().Size()))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		for (uint32 i = 0; i < indices.Size(); ++i)
		{
			indices[i] = i;
		}

		if (indices.Size() > 1)
		{
			Sort(indices.begin(), indices.end(), [&](uint32 a, uint32 b) { return closure.GetAliases()[a].Path.View() < closure.GetAliases()[b].Path.View(); });
		}

		u32(static_cast<uint32>(indices.Size()));

		for (uint32 i : indices)
		{
			const SourceAlias& alias = closure.GetAliases()[i];
			text(alias.Path);
			text(closure.GetSources()[alias.SourceIndex].Identity);
		}

		if (!indices.Resize(closure.GetIncludeEdges().Size()))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		for (uint32 i = 0; i < indices.Size(); ++i)
		{
			indices[i] = i;
		}

		if (indices.Size() > 1)
		{
			Sort(
				indices.begin(), indices.end(),
				[&](uint32 a, uint32 b)
				{
					const SourceIncludeEdge& leftEdge = closure.GetIncludeEdges()[a];
					const SourceIncludeEdge& rightEdge = closure.GetIncludeEdges()[b];
					const StringView sourceX = closure.GetSources()[leftEdge.Requester].Identity.View();
					const StringView sourceY = closure.GetSources()[rightEdge.Requester].Identity.View();

					return sourceX == sourceY ? leftEdge.RequestedPath.View() < rightEdge.RequestedPath.View() : sourceX < sourceY;
				});
		}

		u32(static_cast<uint32>(indices.Size()));

		for (uint32 i : indices)
		{
			const SourceIncludeEdge& edge = closure.GetIncludeEdges()[i];
			text(closure.GetSources()[edge.Requester].Identity);
			text(edge.RequestedPath);
			u32(edge.SourceIndex == NO_SHADER_SOURCE ? 0 : 1);
			if (edge.SourceIndex != NO_SHADER_SOURCE)
			{
				text(closure.GetSources()[edge.SourceIndex].Identity);
			}
		}

		if (!indices.Resize(request.Defines.Size()))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		for (uint32 i = 0; i < indices.Size(); ++i)
		{
			indices[i] = i;
		}

		if (indices.Size() > 1)
		{
			Sort(indices.begin(), indices.end(), [&](uint32 a, uint32 b) { return request.Defines[a].Name.View() < request.Defines[b].Name.View(); });
		}

		u32(static_cast<uint32>(indices.Size()));

		for (uint32 i : indices)
		{
			text(request.Defines[i].Name);
			text(request.Defines[i].Value);
		}

		u32(static_cast<uint32>(request.IncludeRoots.Size()));

		for (const String& root : request.IncludeRoots)
		{
			text(root);
		}

		if (!indices.Resize(request.EntryPoints.Size()))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		for (uint32 i = 0; i < indices.Size(); ++i)
		{
			indices[i] = i;
		}

		if (indices.Size() > 1)
		{
			Sort(indices.begin(), indices.end(), [&](uint32 a, uint32 b) { return request.EntryPoints[a].Name.View() < request.EntryPoints[b].Name.View(); });
		}

		u32(static_cast<uint32>(indices.Size()));

		for (uint32 i : indices)
		{
			text(request.EntryPoints[i].Name);
			u32(static_cast<uint32>(request.EntryPoints[i].Stage));

			for (uint32 group : request.EntryPoints[i].ThreadGroupSize)
			{
				u32(group);
			}
		}

		if (!indices.Resize(request.Targets.Size()))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		for (uint32 i = 0; i < indices.Size(); ++i)
		{
			indices[i] = i;
		}

		if (indices.Size() > 1)
		{
			Sort(indices.begin(), indices.end(), [&](uint32 a, uint32 b) { return request.Targets[a] < request.Targets[b]; });
		}

		u32(static_cast<uint32>(indices.Size()));

		for (uint32 i : indices)
		{
			const ShaderTargetProfile& profile = request.Targets[i];
			u32(static_cast<uint32>(profile.Target));
			u32(profile.ShaderModelMajor);
			u32(profile.ShaderModelMinor);
			u32(profile.SpirVMajor);
			u32(profile.SpirVMinor);
			u32(profile.EnvironmentMajor);
			u32(profile.EnvironmentMinor);
			u32(profile.ProfileVersion);
		}

		if (!bValid)
		{
			return R::Failure(MakeBuildAllocationError());
		}

		output.BuildIdentity = identity.Finalize();

		return R::Success();
	}

	Result<void> FinishBuild(shader::ShaderBuilder& draft, ShaderBuildOutput& output, const ShaderBuildPlan& plan, MemoryResource& resource, CancellationToken cancellation)
	{
		using R = Result<void>;

		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		output.BuildIdentity = plan.GetBuildIdentity();
		if (!output.Dependencies.Reserve(plan.GetDependencies().Size()))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		for (const ShaderDependency& source : plan.GetDependencies())
		{
			ShaderDependency dependency(resource);
			dependency.ContentHash = source.ContentHash;
			if (!dependency.Identity.Assign(source.Identity) || !output.Dependencies.Add(Move(dependency)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		draft.BuildProvenance = output.BuildIdentity;
		Result<Shader> canonical = Move(draft).Build(resource, {}, cancellation);
		if (!canonical)
		{
			return R::Failure(canonical.GetError());
		}

		output.Artifact = Move(canonical).TakeValue();
		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		return R::Success();
	}
} // namespace rox::shader
