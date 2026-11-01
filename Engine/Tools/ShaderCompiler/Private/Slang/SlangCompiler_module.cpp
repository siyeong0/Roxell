#include "SlangCompiler.h"

#include "SlangFileSystem.h"
#include "../ShaderCompilerInternal.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <ByteOperations.hpp>
#include <Sha256.h>
#include <StringView.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	Result<ShaderBuildOutput> SlangCompiler::Compile(const ShaderBuildPlan& plan, ShaderBuildReport& report, MemoryResource& resource, CancellationToken cancellation)
	{
		ASSERT(plan.IsValid() && plan.GetKind() == EShaderBuildPlanKind::SourceProgram, "Compilation requires a source-program plan.");

		return buildProgram(plan, report, resource, cancellation);
	}

	Result<ShaderBuildOutput> SlangCompiler::LinkProgram(const ShaderBuildPlan& plan, ShaderBuildReport& report, MemoryResource& resource, CancellationToken cancellation)
	{
		ASSERT(plan.IsValid() && plan.GetKind() == EShaderBuildPlanKind::LinkedProgram, "Linking requires a linked-program plan.");

		return buildProgram(plan, report, resource, cancellation);
	}

	Result<ShaderModuleArtifact> SlangCompiler::CompileModule(
		const ShaderBuildPlan& plan,
		ShaderModuleSelection selection,
		ShaderBuildReport& report,
		MemoryResource& resource,
		CancellationToken cancellation)
	{
		using R = Result<ShaderModuleArtifact>;

		ASSERT(plan.IsValid() && plan.GetKind() == EShaderBuildPlanKind::SourceModule, "Module compilation requires a source-module plan.");

		report.Diagnostics.Clear();
		report.bTruncated = false;
		report.bAllocationFailure = false;
		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		if (selection.ModuleName.IsEmpty() || selection.ModuleName.Size() > 256)
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module compilation requires a bounded root namespace."));
		}

		for (usize index = 0; index < selection.ModuleName.Size(); ++index)
		{
			const char value = selection.ModuleName[index];
			if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_' || (index != 0 && value >= '0' && value <= '9')))
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module namespace must be one bounded identifier."));
			}
		}

		Result<void> validated = ValidatePlan(plan, *this);
		if (!validated)
		{
			return R::Failure(Move(validated.GetError()));
		}

		const ShaderCompileRequest& request = plan.GetRequest();
		SnapshotFileSystem filesystem(plan, resource, cancellation);
		Result<Slang::ComPtr<slang::ISession>> created = createSession(request, &filesystem, resource);
		if (!created)
		{
			return R::Failure(Move(created.GetError()));
		}

		Slang::ComPtr<slang::ISession> session = Move(created).TakeValue();
		String moduleName(resource);
		if (!moduleName.Assign(selection.ModuleName))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		Slang::ComPtr<slang::IBlob> diagnostics;
		auto reportBlob = [&]()
		{
			if (diagnostics)
			{
				AddDiagnostics(report, {static_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize()}, request);
			}
		};

		slang::IModule* root = session->loadModuleFromSourceString(moduleName.CStr(), request.Root.Identity.CStr(), request.Root.Source.CStr(), diagnostics.writeRef());
		reportBlob();
		if (!root)
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Slang module source compilation failed."));
		}

		const SlangInt moduleCount = session->getLoadedModuleCount();
		if (moduleCount <= 0 || static_cast<uint64>(moduleCount) > request.Limits.MaximumModules)
		{
			return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module bundle exceeds its count bound."));
		}

		Array<ShaderModuleRecord> modules(resource);
		Array<Array<Byte>> moduleBytes(resource);
		Array<ShaderModuleEntryRecord> entries(resource);
		uint32 rootModule = 0;
		if (!modules.Reserve(static_cast<usize>(moduleCount)) || !moduleBytes.Reserve(static_cast<usize>(moduleCount)))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		uint64 aggregateBytes = 0;
		bool bRootFound = false;

		for (uint32 index = 0; index < static_cast<uint32>(moduleCount); ++index)
		{
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			slang::IModule* module = session->getLoadedModule(index);
			ShaderModuleRecord record(resource);
			const char* name = module->getName();
			const char* path = module->getFilePath();
			if (!name || !path || !record.Name.Assign(StringView(name)) || !record.SourceIdentity.Assign(StringView(path)))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			char canonicalIdentity[4097];
			if (record.SourceIdentity.Size() >= Size(canonicalIdentity))
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Module source identity exceeds its bound."));
			}

			for (usize character = 0; character < record.SourceIdentity.Size(); ++character)
			{
				canonicalIdentity[character] = record.SourceIdentity[character] == '\\' ? '/' : record.SourceIdentity[character];
			}

			if (!record.SourceIdentity.Assign({canonicalIdentity, record.SourceIdentity.Size()}))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			const ShaderSourceSnapshot* captured = plan.FindSource(record.SourceIdentity);
			if (!captured || !record.SourceIdentity.Assign(captured->Identity))
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Slang module identity is outside its captured source authority."));
			}

			Slang::ComPtr<slang::IBlob> serialized;
			const SlangResult encoded = module->serialize(serialized.writeRef());
			if (SLANG_FAILED(encoded) || !serialized)
			{
				return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "Slang module serialization failed."));
			}

			record.ByteCount = serialized->getBufferSize();
			if (!record.ByteCount ||
				record.ByteCount > request.Limits.MaximumModuleBytes ||
				aggregateBytes > request.Limits.MaximumAggregateModuleBytes ||
				record.ByteCount > request.Limits.MaximumAggregateModuleBytes - aggregateBytes)
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module IR exceeds its byte budget."));
			}

			Array<Byte> bytes(resource);
			if (!bytes.ResizeUninitialized(static_cast<usize>(record.ByteCount)))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			CopyBytes(bytes.Data(), serialized->getBufferPointer(), bytes.Size());
			record.IRHash = ComputeSha256(bytes.View());
			aggregateBytes += record.ByteCount;
			if (module == root)
			{
				rootModule = index;
				bRootFound = true;
			}

			modules.AddRequired(Move(record));
			moduleBytes.AddRequired(Move(bytes));
		}

		if (!bRootFound)
		{
			return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "Slang did not enumerate the captured root module."));
		}

		auto appendEntry = [&](slang::IEntryPoint* entry, uint32 moduleIndex, SlangStage checkedStage = SLANG_STAGE_NONE) -> Result<void>
		{
			slang::FunctionReflection* function = entry->getFunctionReflection();
			if (!function || !function->getName())
			{
				return Result<void>::Failure(MakeBuildError(EErrorCode::Unsupported, "Module entry metadata is unresolved."));
			}

			const StringView name = StringView(function->getName());

			for (const ShaderModuleEntryRecord& existing : entries)
			{
				if (existing.ModuleIndex == moduleIndex && existing.Name.View() == name)
				{
					return Result<void>::Success();
				}
			}

			SlangStage nativeStage = checkedStage;
			if (nativeStage == SLANG_STAGE_NONE)
			{
				slang::ProgramLayout* layout = entry->getLayout(0, diagnostics.writeRef());
				reportBlob();
				if (!layout || layout->getEntryPointCount() != 1)
				{
					return Result<void>::Failure(MakeBuildError(EErrorCode::Unsupported, "Generic entries without a stage layout require explicit selection."));
				}

				nativeStage = layout->getEntryPointByIndex(0)->getStage();
			}

			if (nativeStage != SLANG_STAGE_COMPUTE && nativeStage != SLANG_STAGE_VERTEX && nativeStage != SLANG_STAGE_FRAGMENT)
			{
				return Result<void>::Failure(MakeBuildError(EErrorCode::Unsupported, "Module entry stage is outside the tested profile."));
			}

			if (entries.Size() >= 256)
			{
				return Result<void>::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Module entry metadata exceeds its bound."));
			}

			ShaderModuleEntryRecord record(resource);
			record.ModuleIndex = moduleIndex;
			record.Stage = nativeStage == SLANG_STAGE_COMPUTE ? shader::EShaderStage::Compute
				: nativeStage == SLANG_STAGE_VERTEX			  ? shader::EShaderStage::Vertex
															: shader::EShaderStage::Pixel;
			Result<void> schema = captureSpecializationParameters(*entry, record, resource);
			if (!schema)
			{
				return schema;
			}

			if (!record.Name.Assign(name) || !entries.Add(Move(record)))
			{
				return Result<void>::Failure(MakeBuildAllocationError());
			}

			return Result<void>::Success();
		};

		for (const ShaderEntryPoint& requested : request.EntryPoints)
		{
			const SlangStage nativeStage = requested.Stage == shader::EShaderStage::Compute ? SLANG_STAGE_COMPUTE
				: requested.Stage == shader::EShaderStage::Vertex							? SLANG_STAGE_VERTEX
																							: SLANG_STAGE_FRAGMENT;
			Slang::ComPtr<slang::IEntryPoint> entry;
			const SlangResult checked = root->findAndCheckEntryPoint(requested.Name.CStr(), nativeStage, entry.writeRef(), diagnostics.writeRef());
			reportBlob();
			if (SLANG_FAILED(checked))
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Requested module entry is invalid."));
			}

			Result<void> appended = appendEntry(entry.get(), rootModule, nativeStage);
			if (!appended)
			{
				return R::Failure(Move(appended.GetError()));
			}
		}

		for (uint32 index = 0; index < static_cast<uint32>(moduleCount); ++index)
		{
			slang::IModule* module = session->getLoadedModule(index);

			for (SlangInt32 entryIndex = 0; entryIndex < module->getDefinedEntryPointCount(); ++entryIndex)
			{
				Slang::ComPtr<slang::IEntryPoint> entry;
				if (SLANG_FAILED(module->getDefinedEntryPoint(entryIndex, entry.writeRef())))
				{
					return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "Slang module entry enumeration failed."));
				}

				Result<void> appended = appendEntry(entry.get(), index);
				if (!appended)
				{
					return R::Failure(Move(appended.GetError()));
				}
			}
		}

		if (entries.Size() > 1)
		{
			Sort(
				entries.begin(), entries.end(),
				[](const ShaderModuleEntryRecord& left, const ShaderModuleEntryRecord& right)
				{
					if (left.ModuleIndex != right.ModuleIndex)
					{
						return left.ModuleIndex < right.ModuleIndex;
					}

					return left.Name.View() < right.Name.View();
				});
		}

		ModuleFileSystem moduleFilesystem;
		Result<Slang::ComPtr<slang::ISession>> verification = createSession(request, &moduleFilesystem, resource);
		if (!verification)
		{
			return R::Failure(Move(verification.GetError()));
		}

		Slang::ComPtr<slang::ISession> verificationSession = Move(verification).TakeValue();

		for (uint32 index = 0; index < modules.Size(); ++index)
		{
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			const ShaderModuleRecord& record = modules[index];
			const Span<Byte> bytes = moduleBytes[index].View();
			Slang::ComPtr<slang::IBlob> blob;
			blob.attach(slang_createBlob(bytes.Data(), bytes.Size()));
			if (!blob)
			{
				return R::Failure(MakeBuildAllocationError());
			}

			slang::IModule* restored = verificationSession->loadModuleFromIRBlob(record.Name.CStr(), record.SourceIdentity.CStr(), blob.get(), diagnostics.writeRef());
			reportBlob();
			if (!restored)
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Captured SDK module order cannot restore its isolated dependency bundle."));
			}
		}

		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		return ShaderModuleArtifact::Create(plan, selection.ModuleName, rootModule, Move(modules), Move(moduleBytes), Move(entries), resource);
	}
} // namespace rox::shader
