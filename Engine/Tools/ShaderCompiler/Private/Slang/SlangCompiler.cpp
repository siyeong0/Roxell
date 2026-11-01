#include "SlangCompiler.h"

#include "../Dxil/DxilReflection.h"
#include "SlangFileSystem.h"
#include "SlangReflector.h"
#include "../Spirv/SpirvMetadataReader.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <ByteOperations.hpp>
#include <ByteReader.h>
#include <Cancellation.h>
#include <StringView.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utility.hpp>
#include <Vocabulary.hpp>

#include <slang-com-ptr.h>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		SlangStage stage(shader::EShaderStage value)
		{
			switch (value)
			{
			case shader::EShaderStage::Vertex:
				return SLANG_STAGE_VERTEX;
			case shader::EShaderStage::Pixel:
				return SLANG_STAGE_FRAGMENT;
			case shader::EShaderStage::Task:
				return SLANG_STAGE_AMPLIFICATION;
			case shader::EShaderStage::Mesh:
				return SLANG_STAGE_MESH;
			case shader::EShaderStage::RayGeneration:
				return SLANG_STAGE_RAY_GENERATION;
			case shader::EShaderStage::Intersection:
				return SLANG_STAGE_INTERSECTION;
			case shader::EShaderStage::AnyHit:
				return SLANG_STAGE_ANY_HIT;
			case shader::EShaderStage::ClosestHit:
				return SLANG_STAGE_CLOSEST_HIT;
			case shader::EShaderStage::Miss:
				return SLANG_STAGE_MISS;
			case shader::EShaderStage::Callable:
				return SLANG_STAGE_CALLABLE;
			default:
				return SLANG_STAGE_COMPUTE;
			}
		}

		Result<uint64> spirvCapabilities(Span<const Byte> bytes, CancellationToken cancellation, [[maybe_unused]] uint32 abi)
		{
			using R = Result<uint64>;

			if (bytes.Size() < 20 || bytes.Size() % 4 != 0)
			{
				return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "Slang produced malformed SPIR-V framing."));
			}

			ByteReader reader(bytes);
			(void)reader.ReadBytes(20);
			uint64 result = 0;

			while (reader.CanRead(4))
			{
				if (cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				const uint32 instruction = reader.ReadU32LE();
				const uint32 count = instruction >> 16;
				const uint32 opcode = instruction & 0xffffu;
				if (count == 0 || count - 1 > reader.GetRemaining() / 4)
				{
					return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "Slang produced truncated SPIR-V instructions."));
				}

				if (opcode == 17)
				{
					if (count != 2)
					{
						return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "Malformed SPIR-V capability instruction."));
					}

					const uint32 capability = reader.ReadU32LE();

					switch (capability)
					{
					case 0:
					case 1:
					case 43:
					case 44:
					case 46:
					case 47:
					case 50:
					case 51:
					case 61:
					case 62:
					case 63:
					case 64:
					case 65:
					case 66:
					case 67:
					case 68:
					case 25:
					case 49:
					case 55:
					case 56:
						break;
					case 9:
						result |= static_cast<uint64>(shader::EShaderCapability::Float16);
						break;
					case 10:
						result |= static_cast<uint64>(shader::EShaderCapability::Float64);
						break;
					case 11:
						result |= static_cast<uint64>(shader::EShaderCapability::Int64);
						break;
					case 12:
						result |= static_cast<uint64>(shader::EShaderCapability::Int64) | static_cast<uint64>(shader::EShaderCapability::Atomic);
						break;
					case 21:
						result |= static_cast<uint64>(shader::EShaderCapability::Atomic);
						break;
					case 5283:
						result |= static_cast<uint64>(shader::EShaderCapability::Mesh);
						break;
					case 4479:
						result |= static_cast<uint64>(shader::EShaderCapability::RayTracing);
						break;
					case 28:
					case 29:
					case 30:
					case 31:
					case 5301:
					case 5302:
					case 5304:
					case 5305:
					case 5306:
					case 5307:
					case 5308:
					case 5309:
					case 5311:
					case 5312:
						break;
					case 4427:
						result |= static_cast<uint64>(shader::EShaderCapability::DrawParameters);
						break;
					case 35:
					case 5379:
						break;
					default:
					{
						char message[192] = "SPIR-V requires a capability that this engine shader ABI cannot express: ";
						const usize prefix = CStringLength(message);
						const ToCharsResult number = ToChars(message + prefix, message + sizeof(message) - 1, capability);

						return R::Failure(MakeBuildError(EErrorCode::Unsupported, {message, static_cast<usize>(number.ptr - message)}));
					}
					}
				}
				else
				{
					if ((opcode >= 227 && opcode <= 242) || opcode == 318 || opcode == 319)
					{
						result |= static_cast<uint64>(shader::EShaderCapability::Atomic);
					}

					(void)reader.ReadBytes((count - 1) * 4);
				}
			}

			return R::Success(result);
		}

	} // namespace

	SlangCompiler::SlangCompiler(const ShaderCompilerOptions& options, MemoryResource& resource) noexcept
		: m_Toolchain(resource)
	{
		m_Toolchain.ShaderAbiVersion = options.ShaderAbiVersion;
		m_Toolchain.AdapterVersion = 2;
	}

	Result<void> SlangCompiler::Initialize(MemoryResource& resource)
	{
		using R = Result<void>;

		if (m_GlobalSession.get() == nullptr)
		{
			Slang::ComPtr<slang::IGlobalSession> initializedGlobal;
			if (SLANG_FAILED(slang::createGlobalSession(initializedGlobal.writeRef())) || initializedGlobal.get() == nullptr)
			{
				return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "Pinned Slang global session initialization failed."));
			}

			// Slang's platform loader adds the DLL/shared-object suffix itself. Supply
			// the locked runtime directory so it resolves dxcompiler and its dxil sibling.
			const StringView dxcLibrary(ROXELL_SHADER_DXC_LIBRARY_PATH);
			usize directoryEnd = dxcLibrary.Size();

			while (directoryEnd > 0 && dxcLibrary[directoryEnd - 1] != '/' && dxcLibrary[directoryEnd - 1] != '\\')
			{
				--directoryEnd;
			}

			if (directoryEnd == 0)
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Pinned DXC runtime directory is missing."));
			}

			String dxcDirectory(resource);
			if (!dxcDirectory.Assign(dxcLibrary.Substr(0, directoryEnd - 1)))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			initializedGlobal->setDownstreamCompilerPath(SLANG_PASS_THROUGH_DXC, dxcDirectory.CStr());
			m_GlobalSession = Move(initializedGlobal);
		}

		if (!m_Toolchain.SdkIdentity.Assign(ROXELL_SHADER_SDK_SHA256) ||
			!m_Toolchain.CompilerVersion.Assign(StringView(m_GlobalSession->getBuildTagString())))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		return R::Success();
	}

	Span<const ShaderCompilerProfile> SlangCompiler::GetProfiles() const noexcept
	{
		static constexpr ShaderCompilerProfile profiles[]{
#if defined(_WIN32)
			CompilerProfile(EShaderSourceLanguage::Slang, shader::EShaderTarget::Dxil, shader::SHADER_ABI_VERSION, false, true, true),
			CompilerProfile(EShaderSourceLanguage::Hlsl, shader::EShaderTarget::Dxil), MeshCompilerProfile(shader::EShaderTarget::Dxil),
			RayCompilerProfile(shader::EShaderTarget::Dxil),
#endif
			CompilerProfile(EShaderSourceLanguage::Slang, shader::EShaderTarget::SpirV, shader::SHADER_ABI_VERSION, true, true, true),
			CompilerProfile(EShaderSourceLanguage::Hlsl, shader::EShaderTarget::SpirV, shader::SHADER_ABI_VERSION, true), MeshCompilerProfile(shader::EShaderTarget::SpirV),
			RayCompilerProfile(shader::EShaderTarget::SpirV)};

		return {profiles, Size(profiles)};
	}

	const ShaderToolchainIdentity& SlangCompiler::GetToolchainIdentity() const noexcept
	{
		return m_Toolchain;
	}

	ShaderCompilerCapabilities SlangCompiler::GetCapabilities() const noexcept
	{
		ShaderCompilerCapabilities capabilities;

		for (const ShaderCompilerProfile& profile : GetProfiles())
		{
			capabilities.bDxil |= profile.Target.Target == shader::EShaderTarget::Dxil;
			capabilities.bSpirV |= profile.Target.Target == shader::EShaderTarget::SpirV;
			capabilities.StageMask |= profile.StageMask;
		}

		return capabilities;
	}

	Result<ShaderBuildOutput> SlangCompiler::buildProgram(const ShaderBuildPlan& plan, ShaderBuildReport& report, MemoryResource& resource, CancellationToken cancellation)
	{
		using R = Result<ShaderBuildOutput>;

		report.Diagnostics.Clear();
		report.bTruncated = false;
		report.bAllocationFailure = false;
		if (cancellation.IsCancellationRequested())
		{
			return R::Failure(MakeBuildCancelledError());
		}

		Result<void> checked = ValidatePlan(plan, *this);
		if (!checked)
		{
			return R::Failure(Move(checked.GetError()));
		}

		const ShaderCompileRequest& request = plan.GetRequest();
		const bool bLink = plan.GetKind() == EShaderBuildPlanKind::LinkedProgram;
		Optional<SnapshotFileSystem> filesystem;
		if (!bLink)
		{
			filesystem.emplace(plan, resource, cancellation);
		}

		ModuleFileSystem moduleFilesystem;
		Result<Slang::ComPtr<slang::ISession>> createdSession = createSession(
			request, bLink ? static_cast<ISlangFileSystem*>(&moduleFilesystem) : static_cast<ISlangFileSystem*>(&*filesystem), resource);
		if (!createdSession)
		{
			return R::Failure(Move(createdSession.GetError()));
		}

		Slang::ComPtr<slang::ISession> session = Move(createdSession).TakeValue();
		Slang::ComPtr<slang::IBlob> diagnostics;
		auto reportBlob = [&](uint32 entry = 0xffffffffu, uint32 target = 0xffffffffu)
		{
			if (diagnostics.get() != nullptr)
			{
				AddDiagnostics(report, {reinterpret_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize()}, request, entry, target);
			}
		};

		slang::IModule* module = nullptr;
		Array<slang::IModule*> moduleRoots(resource);
		Array<slang::IComponentType*> components(resource);
		if (bLink)
		{
			const Span<const ShaderModuleArtifact> artifacts = plan.GetModuleArtifacts();
			if (!moduleRoots.Resize(artifacts.Size()))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			for (uint32 artifactIndex = 0; artifactIndex < artifacts.Size(); ++artifactIndex)
			{
				const ShaderModuleArtifact& artifact = artifacts[artifactIndex];
				const Span<const ShaderModuleRecord> modules = artifact.GetModules();

				for (uint32 moduleIndex = 0; moduleIndex < modules.Size(); ++moduleIndex)
				{
					if (cancellation.IsCancellationRequested())
					{
						return R::Failure(MakeBuildCancelledError());
					}

					const ShaderModuleRecord& record = modules[moduleIndex];
					slang::IModule* restored = nullptr;

					for (SlangInt loaded = 0; loaded < session->getLoadedModuleCount(); ++loaded)
					{
						slang::IModule* candidate = session->getLoadedModule(loaded);
						if (record.Name.View() == StringView(candidate->getName()))
						{
							restored = candidate;
							break;
						}
					}

					if (restored == nullptr)
					{
						const Span<const Byte> bytes = artifact.GetModuleBytes(moduleIndex);
						Slang::ComPtr<slang::IBlob> blob;
						blob.attach(slang_createBlob(bytes.Data(), bytes.Size()));
						if (!blob)
						{
							return R::Failure(MakeBuildAllocationError());
						}

						restored = session->loadModuleFromIRBlob(record.Name.CStr(), record.SourceIdentity.CStr(), blob.get(), diagnostics.writeRef());
						reportBlob();
						if (restored == nullptr)
						{
							return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Captured Slang module IR could not be restored."));
						}

						if (!components.Add(restored))
						{
							return R::Failure(MakeBuildAllocationError());
						}
					}

					if (moduleIndex == artifact.GetRootModuleIndex())
					{
						moduleRoots[artifactIndex] = restored;
					}
				}
			}

			if (!components.Grow(components.Size() + 1))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}
		else
		{
			module = session->loadModuleFromSourceString("RoxellRoot", request.Root.Identity.CStr(), request.Root.Source.CStr(), diagnostics.writeRef());
			reportBlob();
			if (module == nullptr)
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Slang source compilation failed."));
			}

			if (!components.Reserve(2) || !components.Add(module))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		ShaderBuildOutput output(resource);
		shader::ShaderBuilder draft(resource);
		draft.AbiVersion = m_Toolchain.ShaderAbiVersion;

		for (uint32 entryIndex = 0; entryIndex < request.EntryPoints.Size(); ++entryIndex)
		{
			if (bLink)
			{
				module = moduleRoots[plan.GetLinkRequest().EntryPoints[entryIndex].ModuleIndex];
			}

			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			const ShaderEntryPoint& requested = request.EntryPoints[entryIndex];
			Slang::ComPtr<slang::IEntryPoint> entry;
			const SlangResult entryResult = module->findAndCheckEntryPoint(requested.Name.CStr(), stage(requested.Stage), entry.writeRef(), diagnostics.writeRef());
			reportBlob(entryIndex);
			if (SLANG_FAILED(entryResult) || entry.get() == nullptr)
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Slang entry-point checking failed."));
			}

			components.AddRequired(entry.get());
			Slang::ComPtr<slang::IComponentType> composite;
			Slang::ComPtr<slang::IComponentType> linked;
			const SlangResult composition = session->createCompositeComponentType(
				components.Data(), static_cast<SlangInt>(components.Size()), composite.writeRef(), diagnostics.writeRef());
			components.PopBack();
			reportBlob(entryIndex);
			if (SLANG_FAILED(composition))
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Slang program composition failed."));
			}

			Slang::ComPtr<slang::IComponentType> specialized = composite;
			if (bLink)
			{
				Result<Slang::ComPtr<slang::IComponentType>> applied = specializeEntry(
					plan.GetLinkRequest().EntryPoints[entryIndex], moduleRoots.View(), *composite.get(), report, request, resource);
				if (!applied)
				{
					return R::Failure(Move(applied.GetError()));
				}

				specialized = Move(applied).TakeValue();
			}

			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			const SlangResult link = specialized->link(linked.writeRef(), diagnostics.writeRef());
			reportBlob(entryIndex);
			if (SLANG_FAILED(link) || linked->getSpecializationParamCount() != 0)
			{
				return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Slang program linking left unresolved specialization."));
			}

			shader::ShaderEntryPoint published(resource);
			if (!published.Name.Assign(requested.Name))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			published.Stage = requested.Stage;

			for (uint32 targetIndex = 0; targetIndex < request.Targets.Size(); ++targetIndex)
			{
				if (cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				shader::ShaderVariant variant(resource);
				variant.EntryPointIndex = entryIndex;
				variant.Target = request.Targets[targetIndex];
				Slang::ComPtr<slang::IBlob> code;
				const SlangResult generation = linked->getEntryPointCode(0, targetIndex, code.writeRef(), diagnostics.writeRef());
				reportBlob(entryIndex, targetIndex);
				if (SLANG_FAILED(generation) || code.get() == nullptr)
				{
					return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "Slang target code generation or validation failed."));
				}

				if (code->getBufferSize() > 16u * 1024u * 1024u || !variant.Bytecode.ResizeUninitialized(code->getBufferSize()))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				CopyBytes(variant.Bytecode.Data(), code->getBufferPointer(), code->getBufferSize());
				slang::ProgramLayout* layout = linked->getLayout(targetIndex, diagnostics.writeRef());
				reportBlob(entryIndex, targetIndex);
				if (layout == nullptr || layout->getEntryPointCount() != 1 || layout->getTypeParameterCount() != 0)
				{
					return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Slang target reflection is unresolved."));
				}

				slang::EntryPointReflection* entryLayout = layout->getEntryPointByIndex(0);
				const char* emitted = entryLayout->getNameOverride();
				if (emitted == nullptr)
				{
					emitted = entryLayout->getName();
				}

				if (emitted == nullptr || !variant.EmittedEntryPoint.Assign(StringView(emitted)))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				if (requested.Stage == shader::EShaderStage::Compute || requested.Stage == shader::EShaderStage::Mesh || requested.Stage == shader::EShaderStage::Task)
				{
					SlangUInt dimensions[3] = {};
					entryLayout->getComputeThreadGroupSize(3, dimensions);

					for (unsigned axis = 0; axis < 3; ++axis)
					{
						if (dimensions[axis] > 65535 || (targetIndex != 0 && published.ThreadGroupSize[axis] != dimensions[axis]))
						{
							return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Target-dependent thread group dimensions are unsupported."));
						}

						published.ThreadGroupSize[axis] = static_cast<uint32>(dimensions[axis]);
					}
				}

				SlangReflector reflection(variant, resource, *session.get(), targetIndex, cancellation, draft.AbiVersion);
				if (layout->getGlobalConstantBufferSize() != 0)
				{
					return R::Failure(
						MakeBuildError(
						EErrorCode::Unsupported,
						"Implicit global-uniform constant buffers are "
						"unsupported; declare ConstantBuffer<T>."));
				}

				for (unsigned parameterIndex = 0; parameterIndex < layout->getParameterCount(); ++parameterIndex)
				{
					if (variant.Target.Target == shader::EShaderTarget::SpirV)
					{
						Result<bool> native = reflection.NativeParameter(layout->getParameterByIndex(parameterIndex), requested.Stage, draft.AbiVersion);
						if (!native)
						{
							return R::Failure(native.GetError());
						}

						if (native.Value())
						{
							continue;
						}
					}

					Result<void> reflected = reflection.Binding(layout->getParameterByIndex(parameterIndex));
					if (!reflected)
					{
						return R::Failure(reflected.GetError());
					}
				}

				for (unsigned parameterIndex = 0; requested.Stage < shader::EShaderStage::RayGeneration && parameterIndex < entryLayout->getParameterCount(); ++parameterIndex)
				{
					slang::VariableLayoutReflection* parameter = entryLayout->getParameterByIndex(parameterIndex);

					for (unsigned categoryIndex = 0; categoryIndex < parameter->getCategoryCount(); ++categoryIndex)
					{
						slang::ParameterCategory category = parameter->getCategoryByIndex(categoryIndex);
						if (category != slang::ParameterCategory::VaryingInput && category != slang::ParameterCategory::VaryingOutput && category != slang::ParameterCategory::None)
						{
							return R::Failure(MakeBuildError(EErrorCode::Unsupported, "Resource entry parameters require a declared global binding."));
						}
					}
				}

				if (variant.Target.Target == shader::EShaderTarget::Dxil)
				{
					if (requested.Stage == shader::EShaderStage::Mesh)
					{
						Result<void> payload = reflection.MeshPayload(entryLayout);
						if (!payload)
						{
							return R::Failure(Move(payload.GetError()));
						}
					}

					if (requested.Stage >= shader::EShaderStage::RayGeneration)
					{
						Result<void> ray = reflection.RayParameters(entryLayout);
						if (!ray)
						{
							return R::Failure(Move(ray.GetError()));
						}
					}

					Result<uint64> capabilities = requested.Stage >= shader::EShaderStage::RayGeneration
						? Result<uint64>::Success(static_cast<uint64>(shader::EShaderCapability::RayTracing))
						: ReflectDxilCapabilities(variant.Bytecode.View(), resource, draft.AbiVersion);
					if (!capabilities)
					{
						return R::Failure(capabilities.GetError());
					}

					variant.RequiredCapabilities |= capabilities.Value();
				}
				else
				{
					Result<uint64> capabilities = spirvCapabilities(variant.Bytecode.View(), cancellation, draft.AbiVersion);
					if (!capabilities)
					{
						return R::Failure(capabilities.GetError());
					}

					variant.RequiredCapabilities |= capabilities.Value();
				}

				if ((variant.RequiredCapabilities & static_cast<uint64>(shader::EShaderCapability::Atomic)) != 0)
				{
					for (ShaderBinding& binding : variant.Bindings)
					{
						if (binding.Kind == shader::EShaderBindingKind::ReadWriteBuffer || binding.Kind == shader::EShaderBindingKind::StorageTexture)
						{
							binding.Access = shader::EShaderResourceAccess::ReadWriteAtomic;
						}
					}

					for (ShaderResourceUse& use : variant.ResourceUses)
					{
						use.Access = variant.Bindings[use.BindingIndex].Access;
					}
				}

				if (requested.Stage >= shader::EShaderStage::RayGeneration)
				{
					variant.RayInterface.DeclaredLimits = request.RayDeclaredLimits;
				}

				{
					Result<void> metadata = variant.Target.Target == shader::EShaderTarget::SpirV
						? SpirvMetadataReader(resource, cancellation).Reflect(variant, published, draft.AbiVersion)
						: requested.Stage >= shader::EShaderStage::RayGeneration ? ReflectDxilRayMetadata(variant, published, cancellation, draft.AbiVersion)
																				: ReflectDxilMetadata(variant, published, resource, cancellation, draft.AbiVersion);
					if (!metadata)
					{
						return R::Failure(Move(metadata.GetError()));
					}
				}

				if (variant.Target.Target == shader::EShaderTarget::Dxil)
				{
					Result<void> declared = ReflectDxilLocalRoot(variant, published, request, resource, cancellation);
					if (!declared)
					{
						return R::Failure(Move(declared.GetError()));
					}
				}

				if (variant.Target.Target == shader::EShaderTarget::Dxil)
				{
					Result<void> advanced = ReflectDxilBindings(variant, published, cancellation);
					if (!advanced)
					{
						return R::Failure(Move(advanced.GetError()));
					}
				}

				if (!draft.Variants.Add(Move(variant)))
				{
					return R::Failure(MakeBuildAllocationError());
				}
			}

			if (!draft.EntryPoints.Add(Move(published)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		Result<void> finish = FinishBuild(draft, output, plan, resource, cancellation);
		if (!finish)
		{
			return R::Failure(finish.GetError());
		}

		return R::Success(Move(output));
	}

	Result<InterfaceOwner<IShaderCompiler>> CreateSlang(const ShaderCompilerOptions& options, MemoryResource& resource)
	{
		InterfaceOwner<SlangCompiler> owner = InterfaceOwner<SlangCompiler>::Create(resource, options, resource);
		if (!owner)
		{
			return Result<InterfaceOwner<IShaderCompiler>>::Failure(MakeBuildAllocationError());
		}

		Result<void> initialized = owner->Initialize(resource);
		if (!initialized)
		{
			return Result<InterfaceOwner<IShaderCompiler>>::Failure(Move(initialized.GetError()));
		}

		return Result<InterfaceOwner<IShaderCompiler>>::Success(InterfaceOwner<IShaderCompiler>(Move(owner)));
	}
} // namespace rox::shader
