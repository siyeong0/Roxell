#include "ShaderTestData.h"

namespace rox::shader::test
{
	using namespace core;

	TEST_F(ShaderTest, BuilderCloneIsIndependentAndCreateDoesNotConsumeIt)
	{
		Result<Shader> original = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(original);
		Result<ShaderBuilder> draft = ShaderBuilder::FromShader(original.Value(), m_Memory);
		ASSERT_TRUE(draft);
		ASSERT_TRUE(draft.Value().Name.Assign("Changed"));
		Result<Shader> changed = Shader::Create(draft.Value(), m_Memory);
		ASSERT_TRUE(changed);
		EXPECT_EQ(original.Value().GetName(), "Compute");
		EXPECT_EQ(draft.Value().Name, "Changed");
		EXPECT_NE(original.Value().GetIdentity(), changed.Value().GetIdentity());
		EXPECT_EQ(original.Value().GetVariantCodeHash(0), changed.Value().GetVariantCodeHash(0));
		Result<Shader> consumed = Move(draft).TakeValue().Build(m_Memory);
		ASSERT_TRUE(consumed);
		EXPECT_EQ(consumed.Value().GetIdentity(), changed.Value().GetIdentity());
	}

	class InvalidBuilderTest : public ShaderTest, public testing::WithParamInterface<int>
	{
	};

	TEST_P(InvalidBuilderTest, RejectsMalformedAuthoredOrCompiledRecords)
	{
		Result<Shader> original = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(original);
		Result<ShaderBuilder> draft = ShaderBuilder::FromShader(original.Value(), m_Memory);
		ASSERT_TRUE(draft);
		switch (GetParam())
		{
		case 0:
			draft.Value().Variants[0].EntryPointIndex = 999;
			break;
		case 1:
			draft.Value().AbiVersion = 999;
			break;
		case 2:
			draft.Value().EntryPoints[0].Name.AssignRequired(draft.Value().EntryPoints[1].Name);
			break;
		case 3:
			draft.Value().Kernels[0].Choices[0].Entries[0].AssignRequired("Missing");
			break;
		case 4:
			draft.Value().Variants[0].Bytecode.Clear();
			break;
		case 5:
			draft.Value().Variants[0].Bindings[0].Name.AssignRequired("");
			break;
		default:
			FAIL();
		}
		Result<void> checked = draft.Value().Validate();
		ASSERT_FALSE(checked);
		EXPECT_STREQ(checked.GetError().DiagnosticID.CStr(), "ROX-SHADER-ARTIFACT");
		EXPECT_FALSE(Shader::Create(draft.Value(), m_Memory));
		EXPECT_TRUE(original.Value().Save(m_Memory));
	}

	INSTANTIATE_TEST_SUITE_P(InvalidRecords, InvalidBuilderTest, testing::Range(0, 6));

	TEST_F(ShaderTest, EntryAndVariantLimitsAcceptExactCountsAndRejectOneLess)
	{
		Result<Shader> loaded = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(loaded);
		Result<ShaderBuilder> draft = ShaderBuilder::FromShader(loaded.Value(), m_Memory);
		ASSERT_TRUE(draft);
		ShaderLimits limits;
		limits.MaximumEntryPoints = 2;
		limits.MaximumVariants = 4;
		EXPECT_TRUE(draft.Value().Validate(limits));
		limits.MaximumEntryPoints = 1;
		expectError(draft.Value().Validate(limits), EErrorCode::InvalidArgument);
		limits.MaximumEntryPoints = 2;
		limits.MaximumVariants = 3;
		expectError(draft.Value().Validate(limits), EErrorCode::InvalidArgument);
	}

	TEST_F(ShaderTest, CreateAllocationSweepPreservesReusableBuilder)
	{
		Result<Shader> loaded = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(loaded);
		Result<ShaderBuilder> draft = ShaderBuilder::FromShader(loaded.Value(), m_Memory);
		ASSERT_TRUE(draft);
		bool bSucceeded = false;
		for (usize count = 0; count < 1024; ++count)
		{
			FailingMemoryResource failing(m_Memory, count);
			Result<Shader> result = Shader::Create(draft.Value(), failing);
			if (result)
			{
				EXPECT_EQ(result.Value().GetIdentity(), loaded.Value().GetIdentity());
				bSucceeded = true;
				break;
			}
			expectError(result, EErrorCode::ExternalFailure);
		}
		EXPECT_TRUE(bSucceeded);
		EXPECT_TRUE(draft.Value().Validate());
	}

	TEST_F(ShaderTest, DuplicatePropertiesAndInvalidPropertyMappingsAreRejected)
	{
		Result<Shader> asset = loadFixture("Surface", m_Memory);
		ASSERT_TRUE(asset);
		Result<ShaderBuilder> draft = ShaderBuilder::FromShader(asset.Value(), m_Memory);
		ASSERT_TRUE(draft);
		draft.Value().PropertyBindings[0].Binding.AssignRequired("MissingTexture");
		expectError(draft.Value().Validate(), EErrorCode::InvalidArgument);
		draft.Value().PropertyBindings[0].Binding.AssignRequired("Albedo");
		ASSERT_TRUE(draft.Value().Validate());
		ShaderProperty duplicate(m_Memory);
		duplicate.Name.AssignRequired("Albedo");
		duplicate.DefaultValue.Type = EShaderPropertyType::Texture2D;
		ASSERT_TRUE(draft.Value().AddProperty(Move(duplicate)));
		expectError(draft.Value().Validate(), EErrorCode::InvalidArgument);
		EXPECT_EQ(asset.Value().GetProperties().Size(), 1u);
	}
	TEST_F(ShaderTest, ConsumingBuildReleasesMixedResourcesOnEveryPublicationFailure)
	{
		Result<Shader> fixture = loadFixture("Surface", m_Memory);
		ASSERT_TRUE(fixture);
		bool bSucceeded = false;
		for (usize count = 0; count < 1024; ++count)
		{
			TrackingMemoryResource records(static_cast<MemoryResource&>(m_Memory));
			TrackingMemoryResource names(static_cast<MemoryResource&>(m_Memory));
			FailingMemoryResource publication(m_Memory, count);
			{
				Result<ShaderBuilder> draft = ShaderBuilder::FromShader(fixture.Value(), records);
				ASSERT_TRUE(draft);
				ShaderProperty property(names);
				property.Name.AssignRequired("Albedo");
				property.DisplayName.AssignRequired("Albedo texture property allocated by a separate resource");
				property.DefaultValue.Type = EShaderPropertyType::Texture2D;
				draft.Value().Properties.Clear();
				ASSERT_TRUE(draft.Value().AddProperty(Move(property)));
				ASSERT_TRUE(draft.Value().Validate());
				Result<Shader> built = Move(draft).TakeValue().Build(publication);
				if (built)
				{
					Shader retained = built.Value();
					built.Value() = {};
					EXPECT_EQ(retained.GetProperties()[0].Name, "Albedo");
					EXPECT_GT(records.GetStatistics().LiveAllocationCount, 0u);
					EXPECT_GT(names.GetStatistics().LiveAllocationCount, 0u);
					bSucceeded = true;
				}
				else
				{
					expectError(built, EErrorCode::ExternalFailure);
				}
			}
			EXPECT_EQ(records.GetStatistics().LiveAllocationCount, 0u);
			EXPECT_EQ(names.GetStatistics().LiveAllocationCount, 0u);
			if (bSucceeded)
			{
				break;
			}
		}
		EXPECT_TRUE(bSucceeded);
	}

}
