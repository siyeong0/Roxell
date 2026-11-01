#include "ShaderTestSources.h"
#include "../Private/ShaderCompilerInternal.h"
#include "CapturedSourceCompiler.h"

#include <ByteReader.h>
#include <Cancellation.h>
#include <FailingMemoryResource.h>
#include <ShaderBuilder.h>
#include <ShaderBuildPlan.h>
#include <ShaderLayout.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include "ShaderCompilerTest.h"

namespace
{
	using namespace rox;
	using namespace core;
	using namespace shader;

	bool hasNoContraction(core::Span<const core::Byte> code)
	{
		core::ByteReader reader(code);
		if (!reader.CanRead(20))
		{
			return false;
		}

		(void)reader.ReadBytes(20);

		while (reader.CanRead(4))
		{
			const uint32 instruction = reader.ReadU32LE();
			const uint32 words = instruction >> 16;
			if (words == 0 || !reader.CanRead(core::usize(words - 1) * 4))
			{
				return false;
			}

			const Span<const Byte> operands = reader.ReadBytes(core::usize(words - 1) * 4);
			if ((instruction & 0xffffu) == 71 && words == 3)
			{
				core::ByteReader values(operands);
				(void)values.ReadU32LE();
				if (values.ReadU32LE() == 42)
				{
					return true;
				}
			}
		}

		return false;
	}

	bool testPreciseFloatingPoint(core::MemoryResource& memory)
	{
		for (const shader::EShaderCompiler kind : {shader::EShaderCompiler::Slang, shader::EShaderCompiler::Dxc})
		{
			core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(kind, memory);
			CHECK(test::Succeeded(compiler));
			shader::ShaderCompileRequest request(memory);
			request.Language = shader::EShaderSourceLanguage::Hlsl;
			core::Result<ShaderSourceSnapshot> source = shader::CreateShaderSourceSnapshot(
				"floating/entry.hlsl",
				"[[vk::binding(0,0)]] RWStructuredBuffer<float> Values : register(u0);\n"
				"[numthreads(1,1,1)] void Main(uint3 id : SV_DispatchThreadID) {\n"
				"Values[id.x] = Values[0] * Values[1] + Values[2]; }\n",
				memory);
			CHECK(test::Succeeded(source));
			request.Root = core::Move(source).TakeValue();
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign("Main") && request.EntryPoints.Add(core::Move(entry)));
			shader::ShaderTargetProfile target;
			target.Target = kind == shader::EShaderCompiler::Slang ? shader::EShaderTarget::SpirV : shader::EShaderTarget::Dxil;
			CHECK(request.Targets.Add(target));
			test::MemoryShaderSources resolver;
			shader::ShaderBuildReport report(memory);
			core::Result<ShaderBuildPlan> ordinary = compiler.Value()->PrepareBuild(request, resolver, report, memory);
			CHECK(test::Succeeded(ordinary));
			request.bPreciseFloatingPoint = true;
			core::Result<ShaderBuildPlan> precise = compiler.Value()->PrepareBuild(request, resolver, report, memory);
			CHECK(test::Succeeded(precise));
			CHECK(precise.Value().GetBuildIdentity() != ordinary.Value().GetBuildIdentity());
			CHECK(!precise.Value().HasSameInputs(ordinary.Value()));
			core::Result<core::Array<core::Byte>> proof = precise.Value().EncodeInputs(memory, 1024 * 1024);
			CHECK(test::Succeeded(proof));
			CHECK(!ordinary.Value().VerifyInputs(proof.Value().View(), 1024 * 1024));
			request.bPreciseFloatingPoint = false;
			core::Result<ShaderBuildOutput> built = compiler.Value()->Compile(precise.Value(), report, memory);
			if (!built)
			{
				test::PrintBuildReport(report);
			}

			CHECK(test::Succeeded(built));
			if (kind == shader::EShaderCompiler::Slang)
			{
				CHECK(hasNoContraction(built.Value().Artifact.GetVariants()[0].Bytecode.View()));
				request.Language = shader::EShaderSourceLanguage::Slang;
				request.Purpose = shader::EShaderSourceBuildPurpose::Module;
				request.bPreciseFloatingPoint = true;
				core::Result<ShaderBuildPlan> modulePlan = compiler.Value()->PrepareBuild(request, resolver, report, memory);
				CHECK(test::Succeeded(modulePlan));
				EXPECT_DEATH(static_cast<void>(compiler.Value()->Compile(modulePlan.Value(), report, memory)), ".*");
				core::Result<ShaderModuleArtifact> module = compiler.Value()->CompileModule(modulePlan.Value(), {"Precision"}, report, memory);
				CHECK(test::Succeeded(module));
				shader::ShaderLinkRequest link(memory);
				shader::ShaderLinkEntrySelection selected(memory);
				CHECK(selected.Name.Assign("Main") && link.EntryPoints.Add(core::Move(selected)) && link.Targets.Add(target));
				CHECK(!compiler.Value()->PrepareLink(link, {&module.Value(), 1}, report, memory));
				link.bPreciseFloatingPoint = true;
				core::Result<ShaderBuildPlan> linkedPlan = compiler.Value()->PrepareLink(link, {&module.Value(), 1}, report, memory);
				CHECK(test::Succeeded(linkedPlan));
				EXPECT_DEATH(static_cast<void>(compiler.Value()->Compile(linkedPlan.Value(), report, memory)), ".*");
				link.bPreciseFloatingPoint = false;
				core::Result<ShaderBuildOutput> linked = compiler.Value()->LinkProgram(linkedPlan.Value(), report, memory);
				if (!linked)
					test::PrintBuildReport(report);
				CHECK(test::Succeeded(linked));
				CHECK(hasNoContraction(linked.Value().Artifact.GetVariants()[0].Bytecode.View()));
			}
			else
			{
				core::Result<ShaderBuildOutput> defaultBuild = compiler.Value()->Compile(ordinary.Value(), report, memory);
				CHECK(test::Succeeded(defaultBuild));
				CHECK(defaultBuild.Value().Artifact.GetVariantCodeHash(0) != built.Value().Artifact.GetVariantCodeHash(0));
				request.Targets[0].Target = shader::EShaderTarget::SpirV;
				request.bPreciseFloatingPoint = true;
				core::Result<ShaderBuildPlan> unsupported = compiler.Value()->PrepareBuild(request, resolver, report, memory);
				CHECK(!unsupported && unsupported.GetError().Code == core::EErrorCode::Unsupported);
			}

			CHECK(precise.Value().VerifyInputs(proof.Value().View(), 1024 * 1024));
		}

		return true;
	}

	bool testCompilationAndDependencies(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> created = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(created));
		core::InterfaceOwner<shader::IShaderCompiler> compiler = std::move(created).TakeValue();
		CHECK(compiler->GetCapabilities().bDxil && compiler->GetCapabilities().bSpirV);
		shader::ShaderCompileRequest request(memory);
		CHECK(test::MakeComputeRequest(request, memory, shader::EShaderTarget::Dxil, true));
		shader::ShaderTargetProfile spirv = request.Targets[0];
		spirv.Target = shader::EShaderTarget::SpirV;
		CHECK(request.Targets.Add(spirv));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> output = compiler->Compile(request, resolver, report, memory);

		if (!output)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(output));
		CHECK(output.Value().Artifact.GetEntryPoints().Size() == 2);
		CHECK(output.Value().Artifact.GetVariants().Size() == 4);
		CHECK(resolver.Calls >= 2 && resolver.bSawNestedInclude);
		CHECK(output.Value().Dependencies.Size() >= 3);
		CHECK(output.Value().Artifact.IsValid());

		for (const shader::ShaderEntryPoint& entry : output.Value().Artifact.GetEntryPoints())
		{
			CHECK(entry.Stage == shader::EShaderStage::Compute);
			CHECK(entry.ThreadGroupSize[0] == 8 && entry.ThreadGroupSize[1] == 1 && entry.ThreadGroupSize[2] == 1);
		}

		for (const shader::ShaderVariant& variant : output.Value().Artifact.GetVariants())
		{
			CHECK(variant.Bindings.Size() == 1 && variant.ResourceUses.Size() == 1);
			CHECK(variant.Bindings[0].Name == "Output");
			CHECK(variant.Bindings[0].Kind == shader::EShaderBindingKind::ReadWriteBuffer);
			CHECK(variant.Bindings[0].Access == shader::EShaderResourceAccess::ReadWrite);
			CHECK(!variant.Bytecode.IsEmpty());
		}

		core::Result<ShaderBuildOutput> repeat = compiler->Compile(request, resolver, report, memory);
		CHECK(test::Succeeded(repeat));
		CHECK(repeat.Value().BuildIdentity == output.Value().BuildIdentity);
		const core::StringView originalOffset = resolver.OffsetSource;
		resolver.OffsetSource = "static const uint Offset = 11;\n";
		core::Result<ShaderBuildOutput> changed = compiler->Compile(request, resolver, report, memory);
		CHECK(test::Succeeded(changed));
		CHECK(changed.Value().BuildIdentity != output.Value().BuildIdentity);
		CHECK(!test::EqualBytes(changed.Value().Artifact.GetVariants()[0].Bytecode.View(), output.Value().Artifact.GetVariants()[0].Bytecode.View()));
		resolver.OffsetSource = originalOffset;
		core::Result<ShaderBuildOutput> restored = compiler->Compile(request, resolver, report, memory);
		CHECK(test::Succeeded(restored));
		CHECK(restored.Value().BuildIdentity == output.Value().BuildIdentity);
		CHECK(test::EqualBytes(restored.Value().Artifact.GetVariants()[0].Bytecode.View(), output.Value().Artifact.GetVariants()[0].Bytecode.View()));
		resolver.OffsetSource = "static const uint Offset = 11;\n";
		shader::ShaderDefine define(memory);
		CHECK(define.Name.Assign("SCALE") && define.Value.Assign("5"));
		CHECK(request.Defines.Add(std::move(define)));
		core::Result<ShaderBuildOutput> specialized = compiler->Compile(request, resolver, report, memory);
		CHECK(test::Succeeded(specialized));
		CHECK(specialized.Value().BuildIdentity != changed.Value().BuildIdentity);

		return true;
	}

	bool testPreparedBuildOwnership(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderBuildPlan plan;
		core::Hash256 baselineBuild;
		core::Hash256 baselineCode;
		core::TrackingMemoryResource temporary(memory);
		core::CancellationSource preparationCancellation(memory);
		{
			shader::ShaderCompileRequest request(temporary);
			CHECK(test::MakeComputeRequest(request, temporary, shader::EShaderTarget::SpirV));
			test::MemoryShaderSources resolver;
			shader::ShaderBuildReport report(memory);
			core::Result<ShaderBuildOutput> baseline = compiler.Value()->Compile(request, resolver, report, memory);
			CHECK(test::Succeeded(baseline));
			baselineBuild = baseline.Value().BuildIdentity;
			baselineCode = baseline.Value().Artifact.GetVariantCodeHash(0);
			core::Result<ShaderBuildPlan> prepared = compiler.Value()->PrepareBuild(request, resolver, report, memory, preparationCancellation.GetToken());
			CHECK(test::Succeeded(prepared));
			plan = std::move(prepared).TakeValue();
			CHECK(plan.IsValid() && plan.GetBuildIdentity() == baselineBuild);
			CHECK(plan.GetSources().Size() >= 3 && plan.GetDependencies().Size() >= 3);
			CHECK(plan.GetToolchainIdentity() == compiler.Value()->GetToolchainIdentity());
			const core::uint32 callsBefore = resolver.Calls;
			resolver.OffsetSource = "static const uint Offset = 123;\n";
			CHECK(request.Root.Source.Assign("this no longer contains a shader"));
			preparationCancellation.RequestCancellation();
			core::Result<ShaderBuildOutput> compiled = compiler.Value()->Compile(plan, report, memory);
			CHECK(test::Succeeded(compiled) && resolver.Calls == callsBefore);
			CHECK(compiled.Value().BuildIdentity == baselineBuild);
			CHECK(compiled.Value().Artifact.GetVariantCodeHash(0) == baselineCode);
		}

		CHECK(temporary.GetStatistics().LiveAllocationCount == 0);
		compiler.Value().Reset();
		core::Result<core::InterfaceOwner<IShaderCompiler>> otherCompiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(otherCompiler));
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> compiled = otherCompiler.Value()->Compile(plan, report, memory);
		CHECK(test::Succeeded(compiled) && compiled.Value().BuildIdentity == baselineBuild);
		CHECK(compiled.Value().Artifact.GetVariantCodeHash(0) == baselineCode);
		core::Result<core::InterfaceOwner<IShaderCompiler>> incompatibleCompiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Dxc, memory);
		CHECK(test::Succeeded(incompatibleCompiler));
		CHECK(!incompatibleCompiler.Value()->Compile(plan, report, memory));
		core::CancellationSource cancelled(memory);
		cancelled.RequestCancellation();
		core::Result<ShaderBuildOutput> rejected = otherCompiler.Value()->Compile(plan, report, memory, cancelled.GetToken());
		CHECK(!rejected && rejected.GetError().Code == core::EErrorCode::Cancelled);

		return true;
	}

	bool testAdvertisedCompilerProfiles(core::MemoryResource& memory)
	{
		for (const shader::EShaderCompiler kind : {shader::EShaderCompiler::Slang, shader::EShaderCompiler::Dxc})
		{
			core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(kind, memory);
			CHECK(test::Succeeded(compiler));
			const core::Span<const ShaderCompilerProfile> profiles = compiler.Value()->GetProfiles();
			CHECK(!profiles.IsEmpty());

			for (const shader::ShaderCompilerProfile& profile : profiles)
			{
				CHECK(profile.ProfileVersion == 1 && profile.ShaderAbiVersion == 1);
				CHECK(profile.bModuleCompilation == profile.bLinking);
				CHECK(!profile.bSpecialization || profile.Target.Target == shader::EShaderTarget::SpirV);
				if ((profile.StageMask & (core::uint64{1} << static_cast<core::uint32>(shader::EShaderStage::Compute))) == 0)
				{
					continue;
				}

				shader::ShaderCompileRequest request(memory);
				CHECK(test::MakeComputeRequest(request, memory, profile.Target.Target));
				request.Language = profile.Language;
				request.LanguageVersion = profile.LanguageVersion;
				request.EngineProfileVersion = profile.EngineProfileVersion;
				request.Targets[0] = profile.Target;
				test::MemoryShaderSources resolver;
				shader::ShaderBuildReport report(memory);
				core::Result<ShaderBuildPlan> plan = compiler.Value()->PrepareBuild(request, resolver, report, memory);
				CHECK(test::Succeeded(plan));
				core::Result<ShaderBuildOutput> output = compiler.Value()->Compile(plan.Value(), report, memory);
				if (!output)
				{
					test::PrintBuildReport(report);
				}

				CHECK(test::Succeeded(output));
				CHECK(output.Value().BuildIdentity == plan.Value().GetBuildIdentity());
			}
		}

		return true;
	}

	bool testBuildPreparationFailures(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(compiler));
		shader::ShaderCompileRequest request(memory);
		CHECK(test::MakeComputeRequest(request, memory, shader::EShaderTarget::SpirV));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::CancellationSource cancel(memory);
		cancel.RequestCancellation();
		core::Result<ShaderBuildPlan> cancelled = compiler.Value()->PrepareBuild(request, resolver, report, memory, cancel.GetToken());
		CHECK(!cancelled && cancelled.GetError().Code == core::EErrorCode::Cancelled && resolver.Calls == 0);
		core::FailingMemoryResource exhausted(memory, 0);
		CHECK(!compiler.Value()->PrepareBuild(request, resolver, report, exhausted));
		core::Result<ShaderBuildPlan> original = compiler.Value()->PrepareBuild(request, resolver, report, memory);
		CHECK(test::Succeeded(original));
		request.Limits.MaximumDiagnostics = 1;
		request.Limits.MaximumDiagnosticBytes = 128;
		core::Result<ShaderBuildPlan> smallerReport = compiler.Value()->PrepareBuild(request, resolver, report, memory);
		CHECK(test::Succeeded(smallerReport));
		CHECK(smallerReport.Value().GetBuildIdentity() == original.Value().GetBuildIdentity());
		CHECK(smallerReport.Value().GetRequest().Limits.MaximumDiagnostics == 1);
		const core::uint32 priorCalls = resolver.Calls;
		request.Targets[0].EnvironmentMinor += 100;
		core::Result<ShaderBuildPlan> unsupported = compiler.Value()->PrepareBuild(request, resolver, report, memory);
		CHECK(!unsupported && resolver.Calls == priorCalls);

		return true;
	}

	bool testDiagnosticSourceLocations(core::MemoryResource& memory)
	{
		constexpr core::StringView firstBlock = "error[E30015]: undefined identifier\r\n"
												" --> material-function-91-2-1.slang:1:8\r\n"
												"  |\r\n"
												"1 | return MissingSourceMapValue;\r\n"
												"  |        ^^^^^^^^^^^^^^^^^^^^^ undefined identifier 'MissingSourceMapValue'.\r\n"
												"--'";
		constexpr core::StringView secondBlock = "warning[W100]: unused declaration\n"
												" --> material-node-2.slang:42:3\n"
												"   |\n"
												"42 | float unused;\n"
												"--'\n"
												"compiler summary without a location\n"
												"shaders/old.slang(7,9): error X3000: old format\n"
												"shaders/colon.slang:11:4: warning W2000: colon format\n";
		shader::ShaderCompileRequest request(memory);
		shader::ShaderBuildReport report(memory);
		shader::AddDiagnostics(report, firstBlock, request, 2, 3);
		shader::AddDiagnostics(report, secondBlock, request, 4, 5);
		CHECK(!report.bTruncated && !report.bAllocationFailure && report.Diagnostics.Size() == 5);
		const shader::ShaderDiagnostic& first = report.Diagnostics[0];
		CHECK(first.Severity == shader::EShaderDiagnosticSeverity::Error && first.CompilerCode == "E30015");
		CHECK(first.SourceIdentity == "material-function-91-2-1.slang" && first.Line == 1 && first.Column == 8);
		CHECK(first.EntryPointIndex == 2 && first.TargetIndex == 3);
		CHECK(first.Message.View() == firstBlock && first.Message.View().Contains("MissingSourceMapValue"));
		const shader::ShaderDiagnostic& second = report.Diagnostics[1];
		CHECK(second.Severity == shader::EShaderDiagnosticSeverity::Warning && second.CompilerCode == "W100");
		CHECK(second.SourceIdentity == "material-node-2.slang" && second.Line == 42 && second.Column == 3);
		CHECK(second.EntryPointIndex == 4 && second.TargetIndex == 5);
		CHECK(report.Diagnostics[2].Severity == shader::EShaderDiagnosticSeverity::Note);
		CHECK(report.Diagnostics[2].SourceIdentity.IsEmpty() && report.Diagnostics[2].Line == 0);
		CHECK(
			report.Diagnostics[3].SourceIdentity == "shaders/old.slang" && report.Diagnostics[3].Line == 7 && report.Diagnostics[3].Column == 9 &&
			report.Diagnostics[3].CompilerCode == "X3000");
		CHECK(
			report.Diagnostics[4].SourceIdentity == "shaders/colon.slang" && report.Diagnostics[4].Line == 11 && report.Diagnostics[4].Column == 4 &&
			report.Diagnostics[4].CompilerCode == "W2000");

		for (const core::StringView invalidArrow : {core::StringView(" --> ../escape.slang:1:8"), core::StringView(" --> shader.slang:0:8"),
				core::StringView(" --> shader.slang:1:0"), core::StringView(" --> shader.slang:1:8trailing"), core::StringView(" --> shader.slang:4294967296:8")})
		{
			core::String text(memory);
			CHECK(text.Assign("error[E30015]: undefined identifier\n") && text.Append(invalidArrow));
			shader::ShaderBuildReport invalid(memory);
			shader::AddDiagnostics(invalid, text.View(), request);
			CHECK(invalid.Diagnostics.Size() == 2 && !invalid.bTruncated);
			CHECK(invalid.Diagnostics[0].SourceIdentity.IsEmpty() && invalid.Diagnostics[0].Line == 0);
			CHECK(invalid.Diagnostics[1].SourceIdentity.IsEmpty() && invalid.Diagnostics[1].Line == 0);
		}

		request.Limits.MaximumDiagnostics = 1;
		request.Limits.MaximumDiagnosticBytes = static_cast<core::uint32>(firstBlock.Size());
		shader::ShaderBuildReport exact(memory);
		shader::AddDiagnostics(exact, firstBlock, request);
		CHECK(exact.Diagnostics.Size() == 1 && !exact.bTruncated);
		shader::AddDiagnostics(exact, secondBlock, request);
		CHECK(exact.Diagnostics.Size() == 1 && exact.bTruncated);
		--request.Limits.MaximumDiagnosticBytes;
		shader::ShaderBuildReport bounded(memory);
		shader::AddDiagnostics(bounded, firstBlock, request);
		CHECK(bounded.Diagnostics.IsEmpty() && bounded.bTruncated && !bounded.bAllocationFailure);
		++request.Limits.MaximumDiagnosticBytes;
		core::FailingMemoryResource exhausted(memory, 0);
		shader::ShaderBuildReport failed(exhausted);
		shader::AddDiagnostics(failed, firstBlock, request);
		CHECK(failed.bAllocationFailure && failed.bTruncated && failed.Diagnostics.IsEmpty());

		return true;
	}

	bool testDiagnosticsAndCancellation(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> created = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(created));
		core::InterfaceOwner<shader::IShaderCompiler> compiler = std::move(created).TakeValue();
		shader::ShaderCompileRequest request(memory);
		CHECK(test::MakeComputeRequest(request, memory, shader::EShaderTarget::SpirV));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::CancellationSource cancellation(memory);
		cancellation.RequestCancellation();
		core::Result<ShaderBuildOutput> cancelled = compiler->Compile(request, resolver, report, memory, cancellation.GetToken());
		CHECK(!cancelled && cancelled.GetError().Code == core::EErrorCode::Cancelled);
		CHECK(resolver.Calls == 0);
		core::CancellationSource duringResolve(memory);
		resolver.CancelOnResolve = &duringResolve;
		core::Result<ShaderBuildOutput> interrupted = compiler->Compile(request, resolver, report, memory, duringResolve.GetToken());
		CHECK(!interrupted && interrupted.GetError().Code == core::EErrorCode::Cancelled);
		resolver.CancelOnResolve = nullptr;
		core::Result<ShaderSourceSnapshot> invalid = shader::CreateShaderSourceSnapshot(
			"kernels/broken.slang", "[numthreads(8,1,1)] void ComputeMain() { uint x = UndefinedSymbol; }", memory);
		CHECK(test::Succeeded(invalid));
		request.Root = std::move(invalid).TakeValue();
		core::Result<ShaderBuildOutput> rejected = compiler->Compile(request, resolver, report, memory);
		CHECK(!rejected && !report.Diagnostics.IsEmpty());
		CHECK(!report.Diagnostics[0].Message.IsEmpty());
		bool hasCompilerCode = false;

		for (const shader::ShaderDiagnostic& diagnostic : report.Diagnostics)
		{
			hasCompilerCode |= !diagnostic.CompilerCode.IsEmpty();
		}

		CHECK(hasCompilerCode);

		return true;
	}

	bool testGraphicsEntries(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> created = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(created));
		core::InterfaceOwner<shader::IShaderCompiler> compiler = std::move(created).TakeValue();
		shader::ShaderCompileRequest request(memory);
		core::Result<ShaderSourceSnapshot> root = shader::CreateShaderSourceSnapshot(
			"graphics/triangle.slang",
			"struct Params { float4 Color; };\n"
			"[[vk::binding(0,0)]] ConstantBuffer<Params> Parameters : register(b0);\n"
			"float4 VSMain(uint id : SV_VertexID) : SV_Position { return "
			"float4(float(id)+Parameters.Color.x,0,0,1); }\n"
			"float4 PSMain() : SV_Target0 { return Parameters.Color; }\n",
			memory);
		CHECK(test::Succeeded(root));
		request.Root = std::move(root).TakeValue();
		shader::ShaderEntryPoint vertex(memory);
		shader::ShaderEntryPoint pixel(memory);
		CHECK(vertex.Name.Assign("VSMain") && pixel.Name.Assign("PSMain"));
		vertex.Stage = shader::EShaderStage::Vertex;
		pixel.Stage = shader::EShaderStage::Pixel;
		CHECK(request.EntryPoints.Add(std::move(vertex)) && request.EntryPoints.Add(std::move(pixel)));
		shader::ShaderTargetProfile dxil;
		shader::ShaderTargetProfile spirv;
		spirv.Target = shader::EShaderTarget::SpirV;
		CHECK(request.Targets.Add(dxil) && request.Targets.Add(spirv));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> output = compiler->Compile(request, resolver, report, memory);

		if (!output)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(output));
		CHECK(output.Value().Artifact.GetVariants().Size() == 4);

		for (const shader::ShaderEntryPoint& entry : output.Value().Artifact.GetEntryPoints())
		{
			CHECK(entry.ThreadGroupSize[0] == 0);
		}

		for (const shader::ShaderTargetProfile& target : request.Targets)
		{
			core::Result<core::uint32> vertexVariant = output.Value().Artifact.FindVariant({"VSMain", target});
			core::Result<core::uint32> pixelVariant = output.Value().Artifact.FindVariant({"PSMain", target});
			CHECK(vertexVariant && pixelVariant);
			shader::ShaderEntrySelection selections[]{{output.Value().Artifact, vertexVariant.Value()}, {output.Value().Artifact, pixelVariant.Value()}};
			core::Result<ShaderLayout> programInterface = shader::ShaderLayout::Create(selections, memory);
			CHECK(test::Succeeded(programInterface));
			CHECK(programInterface.Value().GetStageIOStatus() == shader::EShaderStageIOStatus::Verified);
			CHECK(programInterface.Value().GetSelections().Size() == 2 && programInterface.Value().GetBindings().Size() == 1);
			CHECK(programInterface.Value().GetBindings()[0].StageMask == 3);
			std::swap(selections[0], selections[1]);
			core::Result<ShaderLayout> reordered = shader::ShaderLayout::Create(selections, memory);
			CHECK(reordered && reordered.Value().GetPhysicalLayoutHash() == programInterface.Value().GetPhysicalLayoutHash());
			CHECK(reordered.Value().GetLayoutHash() == programInterface.Value().GetLayoutHash());
			CHECK(reordered.Value().GetSelectedCodeHash() == programInterface.Value().GetSelectedCodeHash());
		}

		return true;
	}

	bool testReflectedHostLayout(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> created = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(created));
		core::InterfaceOwner<shader::IShaderCompiler> compiler = std::move(created).TakeValue();
		shader::ShaderCompileRequest request(memory);
		core::Result<ShaderSourceSnapshot> root = shader::CreateShaderSourceSnapshot(
			"layout/parameters.slang",
			"#if 0\n#include \"disabled-feature.slang\"\n#endif\n"
			"struct Params { float4 Color; uint Count; float Padding0; float Padding1; float Padding2; };\n"
			"[[vk::binding(1,0)]] ConstantBuffer<Params> Parameters : register(b0);\n"
			"[[vk::binding(0,0)]] RWStructuredBuffer<uint> Output : register(u0);\n"
			"[numthreads(1,1,1)] void ComputeMain() { Output[0] = Parameters.Count + "
			"uint(Parameters.Color.x); }\n",
			memory);
		CHECK(test::Succeeded(root));
		request.Root = std::move(root).TakeValue();
		shader::ShaderEntryPoint entry(memory);
		CHECK(entry.Name.Assign("ComputeMain"));
		CHECK(request.EntryPoints.Add(std::move(entry)));
		shader::ShaderTargetProfile dxil;
		shader::ShaderTargetProfile spirv;
		spirv.Target = shader::EShaderTarget::SpirV;
		CHECK(request.Targets.Add(dxil) && request.Targets.Add(spirv));
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> built = compiler->Compile(request, resolver, report, memory);

		if (!built)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(built));

		for (const shader::ShaderVariant& variant : built.Value().Artifact.GetVariants())
		{
			const shader::ShaderDataLayout* layout = nullptr;

			for (const shader::ShaderBinding& binding : variant.Bindings)
			{
				if (binding.Kind == shader::EShaderBindingKind::ConstantBuffer)
				{
					CHECK(binding.DataLayout != shader::NO_DATA_LAYOUT && binding.DataLayout < variant.Layouts.Size());
					layout = &variant.Layouts[binding.DataLayout];
				}
			}

			CHECK(layout != nullptr && layout->ByteSize == 32 && layout->Identity.IsValid());
			bool color = false;
			bool count = false;

			for (const shader::ShaderDataField& field : layout->Fields)
			{
				if (field.Name == "Color")
				{
					color = field.ByteOffset == 0 && field.ByteSize == 16;
				}

				if (field.Name == "Count")
				{
					count = field.ByteOffset == 16 && field.ByteSize == 4;
				}
			}

			CHECK(color && count);
		}

		return true;
	}

	bool testDxcCompatibility(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> created = shader::CreateShaderCompiler(shader::EShaderCompiler::Dxc, memory);
		CHECK(test::Succeeded(created));
		core::InterfaceOwner<shader::IShaderCompiler> compiler = std::move(created).TakeValue();
		shader::ShaderCompileRequest request(memory);
		CHECK(test::MakeComputeRequest(request, memory, shader::EShaderTarget::Dxil));
		request.Language = shader::EShaderSourceLanguage::Hlsl;
		test::MemoryShaderSources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> compiled = compiler->Compile(request, resolver, report, memory);

		if (!compiled)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(compiled));
		CHECK(compiled.Value().Artifact.GetVariants()[0].Bindings.Size() == 1);
		CHECK(compiled.Value().Artifact.GetEntryPoints()[0].ThreadGroupSize[0] == 8);
		request.Targets[0].Target = shader::EShaderTarget::SpirV;
		core::Result<ShaderBuildOutput> spirv = compiler->Compile(request, resolver, report, memory);
		if (!spirv)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(spirv));
		const shader::Shader& artifact = spirv.Value().Artifact;
		CHECK(artifact.GetVariants().Size() == 1 && artifact.GetEntryPoints().Size() == 1);
		const shader::ShaderVariant& variant = artifact.GetVariants()[0];
		const shader::ShaderEntryPoint& entry = artifact.GetEntryPoints()[0];
		CHECK(variant.Target.Target == shader::EShaderTarget::SpirV && variant.EmittedEntryPoint == "ComputeMain");
		CHECK(variant.bStageIOComplete && variant.Requirements.Features == 0 && variant.RequiredCapabilities == 0);
		CHECK(entry.Stage == shader::EShaderStage::Compute && entry.ThreadGroupSize[0] == 8 && entry.ThreadGroupSize[1] == 1 && entry.ThreadGroupSize[2] == 1);
		CHECK(variant.Requirements.MinimumWorkgroupInvocations == 8 && variant.Bindings.Size() == 1);
		const shader::ShaderBinding& binding = variant.Bindings[0];
		CHECK(
			binding.Name == "Output" && binding.Kind == shader::EShaderBindingKind::ReadWriteBuffer && binding.Access == shader::EShaderResourceAccess::ReadWrite &&
			binding.RegisterNamespace == shader::EShaderRegisterClass::Unified && binding.Space == 0 && binding.Binding == 0);
		CHECK(binding.DataLayout < variant.Layouts.Size());
		const shader::ShaderDataLayout& layout = variant.Layouts[binding.DataLayout];
		CHECK(layout.ByteSize == 4 && layout.ElementStride == 4 && layout.Fields.Size() == 1);
		CHECK(
			layout.Fields[0].Name == "$element" && layout.Fields[0].Scalar == shader::EShaderScalar::UnsignedInteger && layout.Fields[0].ScalarWidth == 32 &&
			layout.Fields[0].VectorWidth == 1);
		CHECK(variant.Bytecode.Size() >= 20 && variant.Bytecode.Size() % 4 == 0);
		core::uint32 magic = 0;
		std::memcpy(&magic, variant.Bytecode.Data(), sizeof(magic));
		CHECK(magic == 0x07230203);
		CHECK(artifact.GetVariantCodeHash(0) != compiled.Value().Artifact.GetVariantCodeHash(0));
		CHECK(spirv.Value().BuildIdentity != compiled.Value().BuildIdentity);

		return true;
	}

	class OverlaySources final : public shader::IShaderSourceResolver
	{
	public:
		bool Swap = false;

		core::Result<shader::ShaderSourceSnapshot> Resolve(core::StringView, core::StringView path, core::MemoryResource& memory, core::CancellationToken) override
		{
			const bool a = path == "a.slang" || path == "overlay/a.slang";
			const bool b = path == "b.slang" || path == "overlay/b.slang";

			if (!a && !b)
			{
				return core::Result<shader::ShaderSourceSnapshot>::Failure(core::EErrorCode::NotFound, "Test.Include", "Unknown overlay include.");
			}

			return (a != Swap) ? shader::CreateShaderSourceSnapshot("shared/x.slang", "#define CHOICE 1\n", memory)
							: shader::CreateShaderSourceSnapshot("shared/y.slang", "#define CHOICE 2\n", memory);
		}
	};

	bool testIncludeOverlayIdentity(core::MemoryResource& memory)
	{
		core::Result<core::InterfaceOwner<IShaderCompiler>> created = shader::CreateShaderCompiler(shader::EShaderCompiler::Slang, memory);
		CHECK(test::Succeeded(created));
		shader::ShaderCompileRequest request(memory);
		core::Result<ShaderSourceSnapshot> root = shader::CreateShaderSourceSnapshot(
			"overlay/main.slang",
			"#include \"a.slang\"\nstatic const uint First = CHOICE;\n#undef CHOICE\n#include \"b.slang\"\n"
			"[[vk::binding(0,0)]] RWStructuredBuffer<uint> Output : register(u0);\n"
			"[numthreads(1,1,1)] void ComputeMain() { Output[0] = First * 10 + CHOICE; }\n",
			memory);
		CHECK(root);
		request.Root = std::move(root).TakeValue();
		shader::ShaderEntryPoint entry(memory);
		CHECK(entry.Name.Assign("ComputeMain") && request.EntryPoints.Add(std::move(entry)));
		shader::ShaderTargetProfile target;
		target.Target = shader::EShaderTarget::SpirV;
		CHECK(request.Targets.Add(target));
		OverlaySources resolver;
		shader::ShaderBuildReport report(memory);
		core::Result<ShaderBuildOutput> first = created.Value()->Compile(request, resolver, report, memory);

		if (!first)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(first));
		resolver.Swap = true;
		core::Result<ShaderBuildOutput> second = created.Value()->Compile(request, resolver, report, memory);

		if (!second)
		{
			test::PrintBuildReport(report);
		}

		CHECK(test::Succeeded(second));
		CHECK(first.Value().Dependencies.Size() == second.Value().Dependencies.Size());
		CHECK(first.Value().BuildIdentity != second.Value().BuildIdentity);

		return true;
	}

	class NestedAliasSources final : public shader::IShaderSourceResolver
	{
	public:
		bool CanonicalRequester = false;

		core::Result<shader::ShaderSourceSnapshot> Resolve(core::StringView requester, core::StringView path, core::MemoryResource& memory, core::CancellationToken) override
		{
			if (path == "entry.slang" || path == "root/entry.slang")
			{
				return shader::CreateShaderSourceSnapshot("shared/sub/entry.slang", "#include \"value.slang\"\n", memory);
			}

			if (path == "value.slang" || path == "shared/sub/value.slang")
			{
				CanonicalRequester |= requester == "shared/sub/entry.slang";

				return shader::CreateShaderSourceSnapshot("shared/sub/value.slang", "uint Value() { return 17; }\n", memory);
			}

			return core::Result<shader::ShaderSourceSnapshot>::Failure(core::EErrorCode::NotFound, "Test.NestedAlias", "Unknown nested alias include.");
		}
	};

	bool testCapturedSourceBackend(core::MemoryResource& memory)
	{
		for (shader::EShaderCompiler kind : {shader::EShaderCompiler::Slang, shader::EShaderCompiler::Dxc})
		{
			core::Result<core::InterfaceOwner<IShaderCompiler>> native = shader::CreateShaderCompiler(kind, memory);
			CHECK(test::Succeeded(native));
			test::CapturedSourceCompiler compiler(*native.Value());
			core::FailingMemoryResource planMemory(memory, static_cast<core::usize>(-1));
			shader::ShaderBuildPlan plan;
			shader::ShaderBuildReport report(memory);
			{
				shader::ShaderCompileRequest request(memory);
				request.Language = kind == shader::EShaderCompiler::Dxc ? shader::EShaderSourceLanguage::Hlsl : shader::EShaderSourceLanguage::Slang;
				core::Result<ShaderSourceSnapshot> root = shader::CreateShaderSourceSnapshot(
					"root/main.slang",
					"#include \"entry.slang\"\n#if 0\n#include \"missing.slang\"\n#endif\n"
					"RWStructuredBuffer<uint> Output : register(u0);\n"
					"[numthreads(1,1,1)] void ComputeMain() { Output[0] = Value(); }\n",
					memory);
				CHECK(test::Succeeded(root));
				request.Root = std::move(root).TakeValue();
				shader::ShaderEntryPoint entry(memory);
				CHECK(entry.Name.Assign("ComputeMain") && request.EntryPoints.Add(std::move(entry)));
				CHECK(request.Targets.Add(shader::ShaderTargetProfile{}));
				NestedAliasSources resolver;
				core::Result<ShaderBuildPlan> prepared = compiler.PrepareBuild(request, resolver, report, planMemory);
				CHECK(test::Succeeded(prepared) && resolver.CanonicalRequester);
				plan = std::move(prepared).TakeValue();
			}

			const core::Hash256 identity = plan.GetBuildIdentity();
			core::Result<core::Array<core::Byte>> bytes = plan.EncodeInputs(memory, 1024 * 1024);
			CHECK(test::Succeeded(bytes));
			planMemory.Reset(0);
			const ShaderSourceSnapshot* redirected = plan.FindSource("root/entry.slang");
			CHECK(redirected != nullptr && redirected->Identity == "shared/sub/entry.slang");
			const ShaderIncludeResolution nested = plan.ResolveInclude("root/entry.slang", "value.slang");
			CHECK(nested.Status == shader::EShaderIncludeResolution::Resolved && nested.Source != nullptr);
			CHECK(nested.Source->Identity == "shared/sub/value.slang");
			const ShaderIncludeResolution missing = plan.ResolveInclude("root/main.slang", "missing.slang");
			CHECK(missing.Status == shader::EShaderIncludeResolution::Missing && missing.Source == nullptr);
			const ShaderIncludeResolution uncaptured = plan.ResolveInclude("root/main.slang", "value.slang");
			CHECK(uncaptured.Status == shader::EShaderIncludeResolution::Uncaptured && uncaptured.Source == nullptr);
			CHECK(plan.FindSource("unknown.slang") == nullptr);
			CHECK(plan.GetSourceAliasCount() >= 3 && plan.GetSourceIncludeCount() == 3);
			CHECK(plan.VerifyInputs(bytes.Value().View(), 1024 * 1024));
			CHECK(plan.GetBuildIdentity() == identity);
			CHECK(planMemory.GetAllocationAttemptCount() == 0);

			core::Result<shader::ShaderBuildOutput> compiled = compiler.Compile(plan, report, memory);
			if (!compiled)
			{
				test::PrintBuildReport(report);
			}

			CHECK(test::Succeeded(compiled) && compiled.Value().Artifact.IsValid());
			CHECK(compiled.Value().BuildIdentity == identity);
			CHECK(planMemory.GetAllocationAttemptCount() == 0);
		}

		return true;
	}

	bool testNestedAliasIncludes(core::MemoryResource& memory)
	{
		for (shader::EShaderCompiler kind : {shader::EShaderCompiler::Slang, shader::EShaderCompiler::Dxc})
		{
			core::Result<core::InterfaceOwner<IShaderCompiler>> compiler = shader::CreateShaderCompiler(kind, memory);
			CHECK(compiler);
			shader::ShaderCompileRequest request(memory);
			request.Language = kind == shader::EShaderCompiler::Dxc ? shader::EShaderSourceLanguage::Hlsl : shader::EShaderSourceLanguage::Slang;
			core::Result<ShaderSourceSnapshot> root = shader::CreateShaderSourceSnapshot(
				"root/main.slang",
				"#include \"entry.slang\"\nRWStructuredBuffer<uint> Output : register(u0);\n"
				"[numthreads(1,1,1)] void ComputeMain() { Output[0] = Value(); }\n",
				memory);
			CHECK(root);
			request.Root = std::move(root).TakeValue();
			shader::ShaderEntryPoint entry(memory);
			CHECK(entry.Name.Assign("ComputeMain") && request.EntryPoints.Add(std::move(entry)));
			CHECK(request.Targets.Add(shader::ShaderTargetProfile{}));
			NestedAliasSources resolver;
			shader::ShaderBuildReport report(memory);
			core::Result<ShaderBuildOutput> built = compiler.Value()->Compile(request, resolver, report, memory);

			if (!built)
			{
				test::PrintBuildReport(report);
			}

			CHECK(test::Succeeded(built) && resolver.CanonicalRequester);
		}

		return true;
	}
} // namespace

TEST(ShaderBuild, PreciseFloatingPoint)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testPreciseFloatingPoint(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, CompilationAndDependencies)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testCompilationAndDependencies(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, PreparedBuildOwnership)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testPreparedBuildOwnership(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, AdvertisedCompilerProfiles)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testAdvertisedCompilerProfiles(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, BuildPreparationFailures)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testBuildPreparationFailures(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, DiagnosticSourceLocations)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testDiagnosticSourceLocations(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, DiagnosticsAndCancellation)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testDiagnosticsAndCancellation(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, GraphicsEntries)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testGraphicsEntries(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, ReflectedHostLayout)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testReflectedHostLayout(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, DxcCompatibility)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testDxcCompatibility(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, IncludeOverlayIdentity)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testIncludeOverlayIdentity(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, CapturedSourceBackend)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testCapturedSourceBackend(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}

TEST(ShaderBuild, NestedAliasIncludes)
{
	using namespace rox;
	using namespace core;
	using namespace shader;
	core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());

	EXPECT_TRUE(testNestedAliasIncludes(memory));
	EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
}
