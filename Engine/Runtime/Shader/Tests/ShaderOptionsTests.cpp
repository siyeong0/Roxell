#include "ShaderTestData.h"

namespace rox::shader::test
{
	using namespace core;

	TEST_F(ShaderTest, KeywordsAreUniqueAndCanBeRemovedWithoutReorderingOthers)
	{
		ShaderOptions options(m_Memory);
		ASSERT_TRUE(options.SetKeyword("FAST"));
		ASSERT_TRUE(options.SetKeyword("FAST"));
		ASSERT_TRUE(options.SetKeyword("SECOND"));
		EXPECT_EQ(options.GetKeywords().Size(), 2u);
		ASSERT_TRUE(options.SetKeyword("FAST", false));
		ASSERT_TRUE(options.SetKeyword("MISSING", false));
		ASSERT_EQ(options.GetKeywords().Size(), 1u);
		EXPECT_EQ(options.GetKeywords()[0], "SECOND");
	}

	TEST_F(ShaderTest, KeywordsRejectEmptyEmbeddedNullAndMalformedUtf8)
	{
		ShaderOptions options(m_Memory);
		expectError(options.SetKeyword(""), EErrorCode::InvalidArgument);
		const char embedded[] = {'a', 0, 'b'};
		expectError(options.SetKeyword({embedded, 3}), EErrorCode::InvalidArgument);
		const char invalid[] = "\xc0\xaf";
		expectError(options.SetKeyword({invalid, 2}), EErrorCode::InvalidArgument);
		EXPECT_TRUE(options.GetKeywords().IsEmpty());
	}

	TEST_F(ShaderTest, KeywordCapacityAccepts64AndRejectedAdditionPreservesContents)
	{
		ShaderOptions options(m_Memory);
		for (uint32 i = 0; i < 64; ++i)
		{
			const std::string key = "KEY_" + std::to_string(i);
			ASSERT_TRUE(options.SetKeyword({key.data(), key.size()}));
		}
		expectError(options.SetKeyword("OVERFLOW"), EErrorCode::InvalidArgument);
		EXPECT_EQ(options.GetKeywords().Size(), 64u);
		ASSERT_TRUE(options.SetKeyword("KEY_0", false));
		EXPECT_TRUE(options.SetKeyword("REPLACEMENT"));
	}

	TEST_F(ShaderTest, TypedConstantsPreserveRawBitsAndReplaceById)
	{
		ShaderOptions options(m_Memory);
		ASSERT_TRUE(options.SetPipelineConstant(1, int32{-1}));
		ASSERT_TRUE(options.SetPipelineConstant(2, float32{-0.0f}));
		ASSERT_TRUE(options.SetPipelineConstant(3, true));
		EXPECT_EQ(options.GetPipelineConstants()[0].Bits, 0xffffffffu);
		EXPECT_EQ(options.GetPipelineConstants()[1].Bits, 0x80000000u);
		EXPECT_EQ(options.GetPipelineConstants()[2].Bits, 1u);
		ASSERT_TRUE(options.SetPipelineConstant(1, uint32{42}));
		EXPECT_EQ(options.GetPipelineConstants().Size(), 3u);
		EXPECT_EQ(options.GetPipelineConstants()[0].Scalar, EShaderScalar::UnsignedInteger);
		EXPECT_EQ(options.GetPipelineConstants()[0].Bits, 42u);
	}

	TEST_F(ShaderTest, ConstantsCapacityAllowsReplacingAtTheLimit)
	{
		ShaderOptions options(m_Memory);
		for (uint32 i = 0; i < 1024; ++i)
		{
			ASSERT_TRUE(options.SetPipelineConstant(i, i));
		}
		expectError(options.SetPipelineConstant(1024, uint32{0}), EErrorCode::InvalidArgument);
		EXPECT_TRUE(options.SetPipelineConstant(0, uint32{999}));
		EXPECT_EQ(options.GetPipelineConstants().Size(), 1024u);
	}

	TEST_F(ShaderTest, FailedKeywordAllocationCanBeRetried)
	{
		FailingMemoryResource failing(m_Memory, 0);
		ShaderOptions options(failing);
		expectError(options.SetKeyword("FAST"), EErrorCode::ExternalFailure);
		EXPECT_TRUE(options.GetKeywords().IsEmpty());
		failing.Reset(100);
		ASSERT_TRUE(options.SetKeyword("FAST"));
		EXPECT_EQ(options.GetKeywords().Size(), 1u);
	}

	class PipelineConstantsTest : public ShaderTest, public testing::WithParamInterface<int>
	{
	};

	TEST_P(PipelineConstantsTest, ResolutionChecksDefaultsTypesRangesAndDuplicateIds)
	{
		Result<Shader> loaded = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(loaded);
		Result<ShaderBuilder> draft = ShaderBuilder::FromShader(loaded.Value(), m_Memory);
		ASSERT_TRUE(draft);
		Result<uint32> index = loaded.Value().FindVariant({"Main", {}});
		ASSERT_TRUE(index);
		ShaderSpecializationParameter parameter(m_Memory);
		parameter.Name.AssignRequired("Count");
		parameter.ID = 7;
		parameter.bHasDefault = true;
		parameter.DefaultBits = 4;
		parameter.bHasRange = true;
		parameter.MinimumBits = 1;
		parameter.MaximumBits = 8;
		if (GetParam() >= 8 && GetParam() <= 11)
		{
			parameter.Scalar = EShaderScalar::Float;
			parameter.bHasRange = false;
			parameter.DefaultBits = 0;
			parameter.MinimumBits = 0;
			parameter.MaximumBits = 0;
		}
		if (GetParam() == 12)
		{
			parameter.bHasDefault = false;
			parameter.DefaultBits = 0;
		}
		if (GetParam() == 13)
		{
			parameter.Phase = EShaderSpecializationPhase::BuildTime;
		}
		if (GetParam() == 14)
		{
			parameter.bAffectsLayout = true;
		}
		draft.Value().Variants[index.Value()].SpecializationParameters.AddRequired(Move(parameter));
		Result<Shader> asset = Move(draft).TakeValue().Build(m_Memory);
		ASSERT_TRUE(asset) << asset.GetError().Message.CStr();
		Result<uint32> selected = asset.Value().FindVariant({"Main", {}});
		ASSERT_TRUE(selected);
		ShaderEntrySelection entry{asset.Value(), selected.Value()};
		Result<ShaderLayout> layout = ShaderLayout::Create({&entry, 1}, m_Memory);
		if (GetParam() >= 13)
		{
			expectError(layout, EErrorCode::Unsupported);
			return;
		}
		ASSERT_TRUE(layout);
		ShaderSpecializationValue values[2] = {{7, EShaderScalar::UnsignedInteger, 32, 8}, {7, EShaderScalar::UnsignedInteger, 32, 2}};
		usize count = 1;
		switch (GetParam())
		{
		case 0:
			count = 0;
			break;
		case 1:
			break;
		case 2:
			values[0].Bits = 0;
			break;
		case 3:
			values[0].Bits = 9;
			break;
		case 4:
			values[0].Scalar = EShaderScalar::SignedInteger;
			break;
		case 5:
			values[0].ID = 8;
			break;
		case 6:
			count = 2;
			break;
		case 7:
			values[0].ScalarWidth = 64;
			break;
		case 8:
			values[0] = {7, EShaderScalar::Float, 32, 0x7fc00000};
			break;
		case 9:
			values[0] = {7, EShaderScalar::Float, 32, 0x7f800000};
			break;
		case 10:
			values[0] = {7, EShaderScalar::Float, 32, 0xff800000};
			break;
		case 11:
			values[0] = {7, EShaderScalar::Float, 32, 0x80000000};
			break;
		case 12:
			count = 0;
			break;
		case 13:
		case 14:
			break;
		default:
			FAIL();
		}
		Result<ShaderPipelineConstants> result = ShaderPipelineConstants::Resolve(layout.Value(), {values, count}, m_Memory);
		if (GetParam() <= 1 || GetParam() == 11)
		{
			ASSERT_TRUE(result);
			EXPECT_EQ(result.Value().GetValues()[0].Bits, GetParam() == 0 ? 4u : GetParam() == 11 ? 0x80000000u : 8u);
		}
		else
		{
			expectError(result, EErrorCode::InvalidArgument);
		}
	}

	INSTANTIATE_TEST_SUITE_P(Values, PipelineConstantsTest, testing::Range(0, 15));
}
