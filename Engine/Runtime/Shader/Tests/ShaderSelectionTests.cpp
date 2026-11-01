#include "ShaderTestData.h"

namespace rox::shader::test
{
	using namespace core;

	class ShaderSelectionTest : public ShaderTest, public testing::WithParamInterface<EShaderTarget>
	{
	};

	TEST_P(ShaderSelectionTest, ComputeSelectsExactEntryAndResolvedGroupSize)
	{
		Result<Shader> loaded = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(loaded);
		ShaderOptions options(m_Memory);
		Result<ShaderKernelSelection> ordinary = loaded.Value().SelectKernel("Simulate", profile(GetParam()), options, m_Memory);
		ASSERT_TRUE(ordinary);
		EXPECT_EQ(ordinary.Value().GetThreadGroupSize()[0], 8u);
		EXPECT_EQ(ordinary.Value().GetThreadGroupSize()[1], 2u);
		EXPECT_EQ(ordinary.Value().GetThreadGroupSize()[2], 1u);
		ASSERT_TRUE(options.SetKeyword("FAST"));
		Result<ShaderKernelSelection> fast = loaded.Value().SelectKernel("Simulate", profile(GetParam()), options, m_Memory);
		ASSERT_TRUE(fast);
		EXPECT_NE(ordinary.Value().GetCodeHash(), fast.Value().GetCodeHash());
		const ShaderEntrySelection& stage = fast.Value().GetStages()[0];
		EXPECT_EQ(stage.Artifact.GetEntryPoints()[stage.Artifact.GetVariants()[stage.VariantIndex].EntryPointIndex].Name, "Fast");
	}

	TEST_P(ShaderSelectionTest, SurfaceSelectsLinkedVertexAndPixelStages)
	{
		Result<Shader> loaded = loadFixture("Surface", m_Memory);
		ASSERT_TRUE(loaded);
		ShaderOptions options(m_Memory);
		Result<ShaderPassSelection> selected = loaded.Value().SelectPass("Surface", profile(GetParam()), options, m_Memory);
		ASSERT_TRUE(selected);
		EXPECT_EQ(selected.Value().GetStages().Size(), 2u);
		EXPECT_EQ(selected.Value().GetLayout().GetStageIOStatus(), EShaderStageIOStatus::Verified);
		EXPECT_EQ(selected.Value().GetLayout().GetBindings().Size(), 2u);
	}

	TEST_P(ShaderSelectionTest, SelectionRetainsProducerAfterOwnerRelease)
	{
		ShaderKernelSelection selected;
		{
			Result<Shader> loaded = loadFixture("Compute", m_Memory);
			ASSERT_TRUE(loaded);
			ShaderOptions options(m_Memory);
			Result<ShaderKernelSelection> result = loaded.Value().SelectKernel("Simulate", profile(GetParam()), options, m_Memory);
			ASSERT_TRUE(result);
			selected = Move(result).TakeValue();
		}
		EXPECT_EQ(selected.GetStages()[0].Artifact.GetName(), "Compute");
		EXPECT_EQ(selected.GetLayout().GetBindings().Size(), 3u);
	}

	TEST_P(ShaderSelectionTest, UnknownKeywordsConstantsAndRecipeKindsFail)
	{
		Result<Shader> loaded = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(loaded);
		ShaderOptions keywords(m_Memory);
		ASSERT_TRUE(keywords.SetKeyword("UNKNOWN"));
		expectError(loaded.Value().SelectKernel("Simulate", profile(GetParam()), keywords, m_Memory), EErrorCode::InvalidArgument);
		ShaderOptions constants(m_Memory);
		ASSERT_TRUE(constants.SetPipelineConstant(99, uint32{1}));
		expectError(loaded.Value().SelectKernel("Simulate", profile(GetParam()), constants, m_Memory), EErrorCode::InvalidArgument);
		ShaderOptions empty(m_Memory);
		expectError(loaded.Value().SelectPass("Simulate", profile(GetParam()), empty, m_Memory), EErrorCode::NotFound);
	}

	TEST_P(ShaderSelectionTest, SelectionAllocationFailureDoesNotInvalidateAsset)
	{
		Result<Shader> loaded = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(loaded);
		ShaderOptions options(m_Memory);
		bool bSucceeded = false;
		for (usize count = 0; count < 256; ++count)
		{
			FailingMemoryResource failing(m_Memory, count);
			Result<ShaderKernelSelection> selected = loaded.Value().SelectKernel("Simulate", profile(GetParam()), options, failing);
			if (selected)
			{
				bSucceeded = true;
				break;
			}
			expectError(selected, EErrorCode::ExternalFailure);
			EXPECT_TRUE(loaded.Value().IsValid());
		}
		EXPECT_TRUE(bSucceeded);
	}

	INSTANTIATE_TEST_SUITE_P(Targets, ShaderSelectionTest, testing::Values(EShaderTarget::Dxil, EShaderTarget::SpirV));
}
