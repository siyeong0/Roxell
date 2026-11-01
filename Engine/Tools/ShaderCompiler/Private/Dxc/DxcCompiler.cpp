#include "DxcCompiler.h"

#include "../Dxil/DxilReflection.h"
#include "DxcReflector.h"
#include "DxcSpirvReflector.h"
#include "SnapshotIncludeHandler.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <ByteOperations.hpp>
#include <Cancellation.h>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utility.hpp>

#if defined(_WIN32)
namespace rox::shader
{
	using namespace core;

	namespace
	{
		Result<Array<wchar_t>> wide(StringView text, MemoryResource& resource)
		{
			using R = Result<Array<wchar_t>>;

			if (text.Size() > 16384)
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "DXC argument text exceeds its bound."));
			}

			const int size = text.IsEmpty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Size()), nullptr, 0);
			if (!text.IsEmpty() && size == 0)
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "DXC argument text is invalid UTF-8."));
			}

			Array<wchar_t> result(resource);
			if (!result.Resize(static_cast<usize>(size) + 1))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			if (size != 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Size()), result.Data(), size) != size)
			{
				return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "DXC argument text conversion failed."));
			}

			result[static_cast<usize>(size)] = 0;

			return R::Success(Move(result));
		}
	} // namespace

	DxcCompiler::DxcCompiler(const ShaderCompilerOptions& options, MemoryResource& resource) noexcept
		: m_Toolchain(resource)
	{
		m_Toolchain.ShaderAbiVersion = options.ShaderAbiVersion;
		m_Toolchain.AdapterVersion = 3;
	}

	Result<void> DxcCompiler::Initialize()
	{
		using R = Result<void>;

		if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(m_Utils.Put()))) || FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(m_Compiler.Put()))))
		{
			return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "Pinned DXC initialization failed."));
		}

		DxcPointer<IDxcVersionInfo> version;
		UINT32 major = 0;
		UINT32 minor = 0;
		UINT32 flags = 0;
		if (FAILED(m_Compiler->QueryInterface(IID_PPV_ARGS(version.Put()))) || FAILED(version->GetVersion(&major, &minor)) || FAILED(version->GetFlags(&flags)))
		{
			return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "DXC did not provide its actual compiler version."));
		}

		char text[128]{};
		const int length = FormatText(text, sizeof(text), "DXC-%u.%u-flags-%u-adapter-%u", major, minor, flags, m_Toolchain.AdapterVersion);
		if (length <= 0 || static_cast<usize>(length) >= sizeof(text) || !m_Toolchain.CompilerVersion.Assign({text, static_cast<usize>(length)}))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		m_Toolchain.Compiler = EShaderCompiler::Dxc;
		if (!m_Toolchain.SdkIdentity.Assign(ROXELL_SHADER_SDK_SHA256))
		{
			return R::Failure(MakeBuildAllocationError());
		}

		return R::Success();
	}

	Span<const ShaderCompilerProfile> DxcCompiler::GetProfiles() const noexcept
	{
		static constexpr ShaderCompilerProfile spirv = []
		{
			ShaderCompilerProfile profile = CompilerProfile(EShaderSourceLanguage::Hlsl, shader::EShaderTarget::SpirV);
			profile.StageMask = uint64{1} << static_cast<uint32>(shader::EShaderStage::Compute);

			return profile;
		}();
		static constexpr ShaderCompilerProfile profiles[]{CompilerProfile(EShaderSourceLanguage::Hlsl, shader::EShaderTarget::Dxil), spirv};

		return {profiles, Size(profiles)};
	}

	const ShaderToolchainIdentity& DxcCompiler::GetToolchainIdentity() const noexcept
	{
		return m_Toolchain;
	}

	ShaderCompilerCapabilities DxcCompiler::GetCapabilities() const noexcept
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

	Result<ShaderBuildOutput> DxcCompiler::Compile(const ShaderBuildPlan& plan, ShaderBuildReport& report, MemoryResource& resource, CancellationToken cancellation)
	{
		using R = Result<ShaderBuildOutput>;

		report.Diagnostics.Clear();
		report.bTruncated = false;
		report.bAllocationFailure = false;
		ASSERT(plan.IsValid() && plan.GetKind() == EShaderBuildPlanKind::SourceProgram, "DXC compilation requires a source-program plan.");

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
		ShaderBuildOutput output(resource);
		shader::ShaderBuilder draft(resource);
		draft.AbiVersion = m_Toolchain.ShaderAbiVersion;

		for (uint32 entryIndex = 0; entryIndex < request.EntryPoints.Size(); ++entryIndex)
		{
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			const ShaderEntryPoint& requested = request.EntryPoints[entryIndex];
			shader::ShaderEntryPoint entry(resource);
			entry.Stage = requested.Stage;
			if (!entry.Name.Assign(requested.Name))
			{
				return R::Failure(MakeBuildAllocationError());
			}

			for (uint32 targetIndex = 0; targetIndex < request.Targets.Size(); ++targetIndex)
			{
				const bool bSpirV = request.Targets[targetIndex].Target == shader::EShaderTarget::SpirV;
				if (bSpirV && (requested.Stage != shader::EShaderStage::Compute || request.DebugInfo != EShaderDebugInfo::None))
				{
					return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXC SPIR-V supports the bounded compute profile without debug instrumentation."));
				}

				Array<Array<wchar_t>> storage(resource);
				Array<const wchar_t*> arguments(resource);
				auto argument = [&](StringView value) -> bool
				{
					Result<Array<wchar_t>> converted = wide(value, resource);
					if (!converted)
					{
						return false;
					}

					if (!storage.Add(Move(converted).TakeValue()))
					{
						return false;
					}

					return arguments.Add(storage.Back().Data());
				};

				const StringView profile = requested.Stage == shader::EShaderStage::Compute ? "cs_6_6" : requested.Stage == shader::EShaderStage::Vertex ? "vs_6_6" : "ps_6_6";
				if (!argument(request.Root.Identity) ||
					!argument("-E") ||
					!argument(requested.Name) ||
					!argument("-T") ||
					!argument(profile) ||
					!argument("-HV") ||
					!argument("2021") ||
					!argument(request.MatrixOrder == shader::EShaderMatrixOrder::RowMajor ? "-Zpr" : "-Zpc"))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				if (bSpirV && (!argument("-spirv") || !argument("-fspv-target-env=vulkan1.3") || !argument("-fvk-use-gl-layout")))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				if (!argument(
						request.Optimization == EShaderOptimization::None		   ? "-Od"
							: request.Optimization == EShaderOptimization::Default ? "-O1"
							: request.Optimization == EShaderOptimization::High	   ? "-O2"
																				: "-O3"))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				if (request.DebugInfo == EShaderDebugInfo::Standard && (!argument("-Zi") || !argument("-Qembed_debug")))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				if (request.bPreciseFloatingPoint && !argument("-Gis"))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				if (request.bWarningsAsErrors && !argument("-WX"))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				for (const String& root : request.IncludeRoots)
				{
					if (!argument("-I") || !argument(root))
					{
						return R::Failure(MakeBuildAllocationError());
					}
				}

				for (const ShaderDefine& define : request.Defines)
				{
					String value(resource);
					if (!value.Assign(define.Name) || !value.Append("=") || !value.Append(define.Value) || !argument("-D") || !argument(value))
					{
						return R::Failure(MakeBuildAllocationError());
					}
				}

				const DxcBuffer source{request.Root.Source.Data(), request.Root.Source.Size(), DXC_CP_UTF8};
				SnapshotIncludeHandler includes(plan, *m_Utils.Get(), resource, cancellation);
				DxcPointer<IDxcResult> compiled;
				Result<void> preparedIncludes = includes.Initialize();
				if (!preparedIncludes)
				{
					return R::Failure(preparedIncludes.GetError());
				}

				const HRESULT call = m_Compiler->Compile(&source, arguments.Data(), static_cast<UINT32>(arguments.Size()), &includes, IID_PPV_ARGS(compiled.Put()));
				if (cancellation.IsCancellationRequested())
				{
					return R::Failure(MakeBuildCancelledError());
				}

				if (includes.HasAllocationFailure())
				{
					return R::Failure(MakeBuildAllocationError());
				}

				if (includes.HasAmbiguousRequest())
				{
					return R::Failure(MakeBuildError(EErrorCode::Unsupported, "DXC cannot distinguish requesting sources for an ambiguous native include path."));
				}

				if (FAILED(call) || compiled.Get() == nullptr)
				{
					return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "DXC compile call failed without a result."));
				}

				DxcPointer<IDxcBlobUtf8> diagnostics;
				(void)compiled->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(diagnostics.Put()), nullptr);
				if (diagnostics.Get() != nullptr && diagnostics->GetStringLength() != 0)
				{
					AddDiagnostics(report, {diagnostics->GetStringPointer(), diagnostics->GetStringLength()}, request, entryIndex, targetIndex);
				}

				HRESULT status = E_FAIL;
				if (FAILED(compiled->GetStatus(&status)))
				{
					return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "DXC did not report its compile status."));
				}

				if (FAILED(status))
				{
					return R::Failure(MakeBuildError(EErrorCode::InvalidArgument, "DXC HLSL compilation failed."));
				}

				if (bSpirV)
				{
					DxcPointer<IDxcBlob> nativeObject;
					if (FAILED(compiled->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(nativeObject.Put()), nullptr)) ||
						nativeObject.Get() == nullptr ||
						!nativeObject->GetBufferSize() ||
						nativeObject->GetBufferSize() > 16u * 1024u * 1024u)
					{
						return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "DXC did not produce bounded SPIR-V code."));
					}

					shader::ShaderVariant nativeVariant(resource);
					nativeVariant.Target = request.Targets[targetIndex];
					nativeVariant.EntryPointIndex = entryIndex;
					if (!nativeVariant.EmittedEntryPoint.Assign(requested.Name) || !nativeVariant.Bytecode.Resize(nativeObject->GetBufferSize()))
					{
						return R::Failure(MakeBuildAllocationError());
					}

					CopyBytes(nativeVariant.Bytecode.Data(), nativeObject->GetBufferPointer(), nativeObject->GetBufferSize());
					Result<void> reflectedNative = reflectDxcSpirv(nativeVariant, entry, resource, cancellation);
					if (!reflectedNative)
					{
						return R::Failure(reflectedNative.GetError());
					}

					if (!draft.Variants.Add(Move(nativeVariant)))
					{
						return R::Failure(MakeBuildAllocationError());
					}

					continue;
				}

				DxcPointer<IDxcBlob> object;
				DxcPointer<IDxcBlob> reflectionBytes;
				if (FAILED(compiled->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(object.Put()), nullptr)) ||
					object.Get() == nullptr ||
					object->GetBufferSize() == 0 ||
					object->GetBufferSize() > 16u * 1024u * 1024u ||
					FAILED(compiled->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(reflectionBytes.Put()), nullptr)) ||
					reflectionBytes.Get() == nullptr)
				{
					return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "DXC did not produce bounded code and reflection outputs."));
				}

				const DxcBuffer reflectionBuffer{reflectionBytes->GetBufferPointer(), reflectionBytes->GetBufferSize(), 0};
				DxcPointer<ID3D12ShaderReflection> reflection;
				if (FAILED(m_Utils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(reflection.Put()))) || reflection.Get() == nullptr)
				{
					return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "DXC shader reflection initialization failed."));
				}

				shader::ShaderVariant variant(resource);
				variant.Target = request.Targets[targetIndex];
				variant.EntryPointIndex = entryIndex;
				if (!variant.EmittedEntryPoint.Assign(requested.Name) || !variant.Bytecode.Resize(object->GetBufferSize()))
				{
					return R::Failure(MakeBuildAllocationError());
				}

				CopyBytes(variant.Bytecode.Data(), object->GetBufferPointer(), object->GetBufferSize());
				DxcReflector reflector(variant, resource, cancellation);
				Result<void> reflected = reflector.Reflect(*reflection.Get(), entry);
				if (!reflected)
				{
					return R::Failure(reflected.GetError());
				}

				{

					Result<void> captured = ReflectDxilMetadata(variant, entry, resource, cancellation);
					if (!captured)
					{
						return R::Failure(Move(captured.GetError()));
					}
				}

				Result<void> advanced = ReflectDxilBindings(variant, entry, cancellation);
				if (!advanced)
				{
					return R::Failure(Move(advanced.GetError()));
				}

				if (!draft.Variants.Add(Move(variant)))
				{
					return R::Failure(MakeBuildAllocationError());
				}
			}

			if (!draft.EntryPoints.Add(Move(entry)))
			{
				return R::Failure(MakeBuildAllocationError());
			}
		}

		Result<void> finished = FinishBuild(draft, output, plan, resource, cancellation);
		if (!finished)
		{
			return R::Failure(finished.GetError());
		}

		return R::Success(Move(output));
	}

	Result<uint64> ReflectDxilCapabilities(Span<const Byte> bytecode, MemoryResource&, uint32 abi)
	{
		using R = Result<uint64>;

		DxcPointer<IDxcUtils> utils;
		if (bytecode.Size() == 0 || bytecode.Size() > 16u * 1024u * 1024u || FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(utils.Put()))))
		{
			return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "DXC capability reflection initialization failed."));
		}

		const DxcBuffer buffer{bytecode.Data(), bytecode.Size(), 0};
		DxcPointer<ID3D12ShaderReflection> reflection;
		if (FAILED(utils->CreateReflection(&buffer, IID_PPV_ARGS(reflection.Put()))) || reflection.Get() == nullptr)
		{
			return R::Failure(MakeBuildError(EErrorCode::ExternalFailure, "DXC capability reflection could not read emitted DXIL."));
		}

		return ReflectDxilRequirements(*reflection.Get(), abi);
	}

	Result<InterfaceOwner<IShaderCompiler>> CreateDxc(const ShaderCompilerOptions& options, MemoryResource& resource)
	{
		using R = Result<InterfaceOwner<IShaderCompiler>>;

		InterfaceOwner<DxcCompiler> compiler = InterfaceOwner<DxcCompiler>::Create(resource, options, resource);
		if (!compiler)
		{
			return R::Failure(MakeBuildAllocationError());
		}

		Result<void> initialized = compiler->Initialize();
		if (!initialized)
		{
			return R::Failure(initialized.GetError());
		}

		return R::Success(InterfaceOwner<IShaderCompiler>(Move(compiler)));
	}
} // namespace rox::shader
#else
namespace rox::shader
{
	using namespace core;

	Result<uint64> ReflectDxilCapabilities(Span<const Byte>, MemoryResource&, uint32)
	{
		return Result<uint64>::Failure(MakeBuildError(EErrorCode::Unsupported, "DXC instruction capability reflection currently supports Windows only."));
	}

	Result<InterfaceOwner<IShaderCompiler>> CreateDxc(const ShaderCompilerOptions&, MemoryResource&)
	{
		return Result<InterfaceOwner<IShaderCompiler>>::Failure(
			MakeBuildError(EErrorCode::Unsupported, "The DXC compatibility reflection adapter currently supports Windows only."));
	}
} // namespace rox::shader
#endif
