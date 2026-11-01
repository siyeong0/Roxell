#include <ShaderCompiler.h>

#include <FailingMemoryResource.h>
#include <LinearMemoryResource.h>
#include <NumericLimits.hpp>
#include <SystemMemoryResource.h>
#include <TextPrimitives.hpp>
#include <TrackingMemoryResource.h>

#include <gtest/gtest.h>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		class NoIncludes final : public IShaderSourceResolver
		{
		public:
			Result<ShaderSourceSnapshot> Resolve(StringView, StringView, MemoryResource&, CancellationToken) override
			{
				return Result<ShaderSourceSnapshot>::Failure(EErrorCode::NotFound, "Test.Source", "No includes are provided.");
			}
		};

		class ModulePublicationTest : public ::testing::Test
		{
		protected:
			void SetUp() override
			{
				Result<InterfaceOwner<IShaderCompiler>> compiler = CreateShaderCompiler(EShaderCompiler::Slang, m_Memory);
				ASSERT_TRUE(compiler);
				ShaderCompileRequest request(m_Memory);
				request.Purpose = EShaderSourceBuildPurpose::Module;
				request.Limits.MaximumModules = 2;
				request.Limits.MaximumModuleBytes = 4;
				request.Limits.MaximumAggregateModuleBytes = 4;
				Result<ShaderSourceSnapshot> source = CreateShaderSourceSnapshot("entry.slang", "[numthreads(1,1,1)] void Main() {}", m_Memory);
				ASSERT_TRUE(source);
				request.Root = Move(source).TakeValue();
				ASSERT_TRUE(request.Targets.Add(ShaderTargetProfile{}));
				NoIncludes resolver;
				ShaderBuildReport report(m_Memory);
				Result<ShaderBuildPlan> prepared = compiler.Value()->PrepareBuild(request, resolver, report, m_Memory);
				ASSERT_TRUE(prepared);
				m_Plan = Move(prepared).TakeValue();

				ShaderModuleRecord module(m_Memory);
				module.Name.AssignRequired("Module");
				module.SourceIdentity.AssignRequired("entry.slang");
				module.ByteCount = 4;
				ASSERT_TRUE(m_Modules.Add(Move(module)));
				Array<Byte> code(m_Memory);
				ASSERT_TRUE(code.Resize(4));
				ASSERT_TRUE(m_Bytes.Add(Move(code)));
				ShaderModuleEntryRecord entry(m_Memory);
				entry.Name.AssignRequired("Main");
				ShaderModuleSpecializationParameter parameter(m_Memory);
				parameter.Name.AssignRequired("T");
				ASSERT_TRUE(entry.SpecializationParameters.Add(Move(parameter)));
				ASSERT_TRUE(m_Entries.Add(Move(entry)));
			}

			void TearDown() override
			{
				m_Memory.Reset(NumericLimits<usize>::max());
				m_Entries.Reset();
				m_Bytes.Reset();
				m_Modules.Reset();
				m_Plan = {};
				EXPECT_EQ(m_Tracking.GetStatistics().LiveAllocationCount, 0u);
			}

			Result<ShaderModuleArtifact> Publish(uint32 root = 0)
			{
				return ShaderModuleArtifact::Create(m_Plan, "Module", root, Move(m_Modules), Move(m_Bytes), Move(m_Entries), m_Memory);
			}

			TrackingMemoryResource m_Tracking{SystemMemoryResource::Get()};
			FailingMemoryResource m_Memory{m_Tracking, NumericLimits<usize>::max()};
			alignas(16) Byte m_ArenaBytes[16384];
			LinearMemoryResource m_Arena{m_ArenaBytes, sizeof(m_ArenaBytes)};

			ShaderBuildPlan m_Plan;
			Array<ShaderModuleRecord> m_Modules{m_Memory};
			Array<Array<Byte>> m_Bytes{m_Memory};
			Array<ShaderModuleEntryRecord> m_Entries{m_Memory};
		};

		enum class EInvalidPublication
		{
			RootIndex,
			ByteCount,
			EmptyBytes,
			ModuleByteLimit,
			AggregateByteLimit,
			SourceIdentity,
			ModuleName,
			EntryIndex,
			EntryName,
			EntryStage,
			ParameterKind,
			ParameterName,
			DuplicateParameter,
			ParameterLimit,
			ForeignBytes,
			ForeignModuleStrings,
			ForeignEntryStorage,
			ForeignParameterName
		};

		class InvalidModulePublicationTest : public ModulePublicationTest, public ::testing::WithParamInterface<EInvalidPublication>
		{
		};

	} // namespace

	TEST_F(ModulePublicationTest, PublicationRetainsStorageAfterProducerRelease)
	{
		const Byte* bytes = m_Bytes[0].Data();
		Result<ShaderModuleArtifact> result = Publish();
		ASSERT_TRUE(result);
		EXPECT_TRUE(m_Modules.IsEmpty());
		EXPECT_TRUE(m_Bytes.IsEmpty());
		EXPECT_TRUE(m_Entries.IsEmpty());
		m_Plan = {};
		ShaderModuleArtifact retained = result.Value();
		result.Value() = {};
		EXPECT_TRUE(retained.IsValid());
		EXPECT_TRUE(retained.CanShareAcrossThreads());
		EXPECT_EQ(retained.GetModuleBytes(0).Data(), bytes);
		EXPECT_EQ(retained.GetModuleBytes(0).Size(), 4u);
		EXPECT_EQ(retained.GetEntryPoints()[0].SpecializationParameters[0].Name.View(), "T");
	}

	TEST_F(ModulePublicationTest, AllocationFailurePreservesInputsAndAllowsRetry)
	{
		const Byte* bytes = m_Bytes[0].Data();
		const uint64 allocations = m_Tracking.GetStatistics().LiveAllocationCount;
		m_Memory.Reset(0);
		Result<ShaderModuleArtifact> failed = Publish();
		ASSERT_FALSE(failed);
		EXPECT_EQ(failed.GetError().Code, EErrorCode::ExternalFailure);
		EXPECT_EQ(m_Bytes[0].Data(), bytes);
		EXPECT_EQ(m_Modules.Size(), 1u);
		EXPECT_EQ(m_Entries.Size(), 1u);
		EXPECT_EQ(m_Tracking.GetStatistics().LiveAllocationCount, allocations);
		m_Memory.Reset(NumericLimits<usize>::max());
		EXPECT_TRUE(Publish());
	}

	TEST_F(ModulePublicationTest, SpecializationLimitIsInclusive)
	{
		m_Entries[0].SpecializationParameters.Clear();

		for (uint32 index = 0; index < MAXIMUM_SHADER_LINK_ARGUMENTS; ++index)
		{
			char name[16];
			ToCharsResult converted = ToChars(name, name + sizeof(name), index);
			ShaderModuleSpecializationParameter parameter(m_Memory);
			parameter.Name.AssignRequired(StringView(name, static_cast<usize>(converted.ptr - name)));
			ASSERT_TRUE(m_Entries[0].SpecializationParameters.Add(Move(parameter)));
		}

		Result<ShaderModuleArtifact> result = Publish();
		ASSERT_TRUE(result);
		EXPECT_EQ(result.Value().GetEntryPoints()[0].SpecializationParameters.Size(), MAXIMUM_SHADER_LINK_ARGUMENTS);
	}

	TEST_F(ModulePublicationTest, ForeignOuterArrayIsNotConsumed)
	{
		Array<ShaderModuleRecord> modules(m_Arena);
		ASSERT_TRUE(modules.Add(Move(m_Modules[0])));
		const ShaderModuleRecord* original = modules.Data();
		Result<ShaderModuleArtifact> result = ShaderModuleArtifact::Create(m_Plan, "Module", 0, Move(modules), Move(m_Bytes), Move(m_Entries), m_Memory);
		ASSERT_FALSE(result);
		EXPECT_EQ(modules.Data(), original);
		EXPECT_EQ(modules[0].Name.View(), "Module");
		EXPECT_EQ(m_Bytes.Size(), 1u);
		EXPECT_EQ(m_Entries.Size(), 1u);
	}

	TEST_P(InvalidModulePublicationTest, RejectionPreservesAllInputStorage)
	{
		uint32 root = 0;

		switch (GetParam())
		{
		case EInvalidPublication::RootIndex:
			root = 1;
			break;
		case EInvalidPublication::ByteCount:
			m_Modules[0].ByteCount = 3;
			break;
		case EInvalidPublication::EmptyBytes:
			m_Bytes[0].Clear();
			m_Modules[0].ByteCount = 0;
			break;
		case EInvalidPublication::ModuleByteLimit:
			ASSERT_TRUE(m_Bytes[0].Resize(5));
			m_Modules[0].ByteCount = 5;
			break;
		case EInvalidPublication::AggregateByteLimit:
		{
			ShaderModuleRecord module(m_Memory);
			module.Name.AssignRequired("Second");
			module.SourceIdentity.AssignRequired("entry.slang");
			module.ByteCount = 1;
			ASSERT_TRUE(m_Modules.Add(Move(module)));
			Array<Byte> code(m_Memory);
			ASSERT_TRUE(code.Resize(1));
			ASSERT_TRUE(m_Bytes.Add(Move(code)));
			break;
		}

		case EInvalidPublication::SourceIdentity:
			m_Modules[0].SourceIdentity.AssignRequired("missing.slang");
			break;
		case EInvalidPublication::ModuleName:
			m_Modules[0].Name.Clear();
			break;
		case EInvalidPublication::EntryIndex:
			m_Entries[0].ModuleIndex = 1;
			break;
		case EInvalidPublication::EntryName:
			m_Entries[0].Name.Clear();
			break;
		case EInvalidPublication::EntryStage:
			m_Entries[0].Stage = static_cast<EShaderStage>(255);
			break;
		case EInvalidPublication::ParameterKind:
			m_Entries[0].SpecializationParameters[0].Kind = static_cast<EShaderLinkArgumentKind>(255);
			break;
		case EInvalidPublication::ParameterName:
			m_Entries[0].SpecializationParameters[0].Name.Clear();
			break;
		case EInvalidPublication::DuplicateParameter:
		case EInvalidPublication::ParameterLimit:
		{
			const uint32 count = GetParam() == EInvalidPublication::ParameterLimit ? MAXIMUM_SHADER_LINK_ARGUMENTS : 1;

			for (uint32 index = 0; index < count; ++index)
			{
				ShaderModuleSpecializationParameter parameter(m_Memory);
				parameter.Name.AssignRequired("T");
				ASSERT_TRUE(m_Entries[0].SpecializationParameters.Add(Move(parameter)));
			}

			break;
		}

		case EInvalidPublication::ForeignBytes:
		{
			m_Bytes.Reset();
			Array<Byte> code(m_Arena);
			ASSERT_TRUE(code.Resize(4));
			ASSERT_TRUE(m_Bytes.Add(Move(code)));
			break;
		}

		case EInvalidPublication::ForeignModuleStrings:
		{
			m_Modules.Clear();
			ShaderModuleRecord module(m_Arena);
			module.Name.AssignRequired("Module");
			module.SourceIdentity.AssignRequired("entry.slang");
			module.ByteCount = 4;
			ASSERT_TRUE(m_Modules.Add(Move(module)));
			break;
		}

		case EInvalidPublication::ForeignEntryStorage:
		{
			m_Entries.Clear();
			ShaderModuleEntryRecord entry(m_Arena);
			entry.Name.AssignRequired("Main");
			ASSERT_TRUE(m_Entries.Add(Move(entry)));
			break;
		}

		case EInvalidPublication::ForeignParameterName:
		{
			m_Entries[0].SpecializationParameters.Clear();
			ShaderModuleSpecializationParameter parameter(m_Arena);
			parameter.Name.AssignRequired("T");
			ASSERT_TRUE(m_Entries[0].SpecializationParameters.Add(Move(parameter)));
			break;
		}
		}

		const ShaderModuleRecord* modules = m_Modules.Data();
		const Array<Byte>* byteArrays = m_Bytes.Data();
		const ShaderModuleEntryRecord* entries = m_Entries.Data();
		const Byte* bytes = m_Bytes[0].Data();
		const usize moduleCount = m_Modules.Size();
		const usize entryCount = m_Entries.Size();
		const uint64 allocations = m_Tracking.GetStatistics().LiveAllocationCount;
		Result<ShaderModuleArtifact> result = Publish(root);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.GetError().Code, EErrorCode::InvalidArgument);
		EXPECT_EQ(m_Modules.Data(), modules);
		EXPECT_EQ(m_Bytes.Data(), byteArrays);
		EXPECT_EQ(m_Entries.Data(), entries);
		EXPECT_EQ(m_Bytes[0].Data(), bytes);
		EXPECT_EQ(m_Modules.Size(), moduleCount);
		EXPECT_EQ(m_Entries.Size(), entryCount);
		EXPECT_EQ(m_Tracking.GetStatistics().LiveAllocationCount, allocations);
	}

	INSTANTIATE_TEST_SUITE_P(
		Records,
		InvalidModulePublicationTest,
		::testing::Values(
			EInvalidPublication::RootIndex,
			EInvalidPublication::ByteCount,
			EInvalidPublication::EmptyBytes,
			EInvalidPublication::ModuleByteLimit,
			EInvalidPublication::AggregateByteLimit,
			EInvalidPublication::SourceIdentity,
			EInvalidPublication::ModuleName,
			EInvalidPublication::EntryIndex,
			EInvalidPublication::EntryName,
			EInvalidPublication::EntryStage,
			EInvalidPublication::ParameterKind,
			EInvalidPublication::ParameterName,
			EInvalidPublication::DuplicateParameter,
			EInvalidPublication::ParameterLimit,
			EInvalidPublication::ForeignBytes,
			EInvalidPublication::ForeignModuleStrings,
			EInvalidPublication::ForeignEntryStorage,
			EInvalidPublication::ForeignParameterName));
} // namespace rox::shader
