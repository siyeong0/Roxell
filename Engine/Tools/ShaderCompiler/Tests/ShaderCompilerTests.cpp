#include <ShaderCompiler.h>

#include <FailingMemoryResource.h>
#include <ShaderBuildJob.h>
#include <ShaderBuildService.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		class FileSourceResolver final : public IShaderSourceResolver
		{
		public:
			Result<ShaderSourceSnapshot> Resolve(StringView, StringView path, MemoryResource& resource, CancellationToken cancellation) override
			{
				if (cancellation.IsCancellationRequested())
				{
					return Result<ShaderSourceSnapshot>::Failure(MakeError(EErrorCode::Cancelled, "TEST-SOURCE", "Source load cancelled."));
				}

				const std::filesystem::path file = std::filesystem::path(SHADER_COMPILER_TEST_DATA) / std::string(path.Data(), path.Size());
				std::ifstream stream(file, std::ios::binary);
				if (!stream)
				{
					return Result<ShaderSourceSnapshot>::Failure(MakeError(EErrorCode::NotFound, "TEST-SOURCE", "Source file not found."));
				}

				const std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

				return CreateShaderSourceSnapshot(path, StringView(source.data(), source.size()), resource);
			}
		};

		class CompilerFileTest : public ::testing::TestWithParam<EShaderCompiler>
		{
		};

	} // namespace

	TEST_P(CompilerFileTest, CapturedIncludesSurviveSourceAndCompilerRelease)
	{
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			FileSourceResolver sources;
			Result<InterfaceOwner<IShaderCompiler>> compiler = CreateShaderCompiler(GetParam(), memory);
			ASSERT_TRUE(compiler);
			ShaderBuildPlan plan;
			ShaderBuildReport report(memory);
			{
				ShaderCompileRequest request(memory);
				request.Language = EShaderSourceLanguage::Hlsl;
				Result<ShaderSourceSnapshot> root = sources.Resolve({}, "Compute.hlsl", memory, {});
				ASSERT_TRUE(root);
				request.Root = Move(root).TakeValue();
				ShaderEntryPoint entry(memory);
				entry.Name.AssignRequired("Main");
				ASSERT_TRUE(request.EntryPoints.Add(Move(entry)));
				ASSERT_TRUE(request.Targets.Add(ShaderTargetProfile{}));
				Result<ShaderBuildPlan> captured = compiler.Value()->PrepareBuild(request, sources, report, memory);
				ASSERT_TRUE(captured);
				plan = Move(captured).TakeValue();
			}

			ASSERT_EQ(plan.GetSources().Size(), 2u);
			EXPECT_NE(plan.FindSource("Constants.hlsli"), nullptr);
			EXPECT_EQ(plan.FindSource("Absent.hlsli"), nullptr);
			Shader artifact;
			{
				Result<ShaderBuildOutput> output = compiler.Value()->Compile(plan, report, memory);
				ASSERT_TRUE(output);
				artifact = output.Value().Artifact;
			}

			compiler.Value().Reset();
			plan = {};
			ASSERT_TRUE(artifact.IsValid());
			ASSERT_EQ(artifact.GetVariants().Size(), 1u);
			EXPECT_FALSE(artifact.GetVariants()[0].Bindings.IsEmpty());
		}

		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
	}

	INSTANTIATE_TEST_SUITE_P(Backends, CompilerFileTest, ::testing::Values(EShaderCompiler::Slang, EShaderCompiler::Dxc));

	TEST(ShaderCompilerValidation, RejectsUnknownCompilerAndAbi)
	{
		MemoryResource& memory = SystemMemoryResource::Get();
		const Result<InterfaceOwner<IShaderCompiler>> invalid = CreateShaderCompiler(static_cast<EShaderCompiler>(255), memory);
		ASSERT_FALSE(invalid);
		EXPECT_EQ(invalid.GetError().Code, EErrorCode::InvalidArgument);
		ShaderCompilerOptions options;
		options.ShaderAbiVersion = 0;
		const Result<InterfaceOwner<IShaderCompiler>> abi = CreateShaderCompiler(EShaderCompiler::Slang, options, memory);
		ASSERT_FALSE(abi);
		EXPECT_EQ(abi.GetError().Code, EErrorCode::Unsupported);
	}

	TEST(ShaderCompilerValidation, AllocationFailureReleasesPartialFactory)
	{
		TrackingMemoryResource tracking(SystemMemoryResource::Get());
		{
			FailingMemoryResource memory(tracking, 0);
			EXPECT_FALSE(CreateShaderCompiler(EShaderCompiler::Slang, memory));
			EXPECT_FALSE(CreateShaderCompiler(EShaderCompiler::Dxc, memory));
		}

		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0u);
	}

	TEST(ShaderCompilerValidation, EmptyHandlesSupportOptionalQueries)
	{
		ShaderBuildPlan plan;
		EXPECT_FALSE(plan.IsValid());
		EXPECT_TRUE(plan.GetSources().IsEmpty());
		EXPECT_TRUE(plan.GetDependencies().IsEmpty());
		ShaderBuildJob job;
		EXPECT_FALSE(job.IsValid());
		job.Cancel();
		ShaderBuildResult result;
		EXPECT_FALSE(result.IsValid());
		EXPECT_FALSE(result.HasOutput());
		EXPECT_EQ(result.GetRetainedByteCount(), 0u);
	}

	TEST(ShaderCompilerDeathTest, EmptyPlanCannotExposeRequiredRequest)
	{
		EXPECT_DEATH(
			{
				ShaderBuildPlan plan;
				static_cast<void>(plan.GetRequest());
			},
			".*");
	}

	TEST(ShaderCompilerDeathTest, EmptyPlanCannotEncodeInputs)
	{
		MemoryResource& memory = SystemMemoryResource::Get();
		EXPECT_DEATH(
			{
				ShaderBuildPlan plan;
				static_cast<void>(plan.EncodeInputs(memory, 4096));
			},
			".*");
	}

	TEST(ShaderCompilerDeathTest, EmptyPlanCannotVerifyInputs)
	{
		EXPECT_DEATH(
			{
				ShaderBuildPlan plan;
				static_cast<void>(plan.VerifyInputs({}, 4096));
			},
			".*");
	}

	TEST(ShaderCompilerDeathTest, EmptyPlanCannotCompile)
	{
		MemoryResource& memory = SystemMemoryResource::Get();

		for (EShaderCompiler kind : {EShaderCompiler::Dxc, EShaderCompiler::Slang})
		{
			Result<InterfaceOwner<IShaderCompiler>> compiler = CreateShaderCompiler(kind, memory);
			ASSERT_TRUE(compiler);
			ShaderBuildReport report(memory);
			EXPECT_DEATH(
				{
					ShaderBuildPlan plan;
					static_cast<void>(compiler.Value()->Compile(plan, report, memory));
				},
				".*");
		}
	}

	TEST(ShaderCompilerDeathTest, ServiceStartupRequiresThreadBackend)
	{
		MemoryResource& memory = SystemMemoryResource::Get();
		EXPECT_DEATH(
			{
				ShaderBuildService service(memory);
				static_cast<void>(service.Start({}));
			},
			".*");
	}

	TEST(ShaderCompilerDeathTest, EmptyResultCannotExposeOutput)
	{
		EXPECT_DEATH(
			{
				ShaderBuildResult result;
				static_cast<void>(result.GetOutput());
			},
			".*");
	}
} // namespace rox::shader

namespace rox::shader
{
	TEST(ShaderModuleArtifact, InvalidPublicationDoesNotConsumeArrays)
	{
		using namespace core;
		TrackingMemoryResource memory(SystemMemoryResource::Get());
		{
			Array<ShaderModuleRecord> modules(memory);
			Array<Array<Byte>> bytes(memory);
			Array<ShaderModuleEntryRecord> entries(memory);
			ShaderModuleRecord record(memory);
			record.Name.AssignRequired("Invalid");
			ASSERT_TRUE(modules.Add(Move(record)));
			const Result<ShaderModuleArtifact> result = ShaderModuleArtifact::Create({}, "Invalid", 0, Move(modules), Move(bytes), Move(entries), memory);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.GetError().Code, EErrorCode::InvalidArgument);
			ASSERT_EQ(modules.Size(), 1u);
			EXPECT_EQ(modules[0].Name.View(), "Invalid");
		}

		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0u);
	}
} // namespace rox::shader
