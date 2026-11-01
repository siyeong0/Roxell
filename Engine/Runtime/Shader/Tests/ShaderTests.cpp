#include "ShaderTestData.h"

namespace rox::shader::test
{
	using namespace core;

	TEST_F(ShaderTest, EmptyOwnerRejectsSaveAndIdentityComputation)
	{
		Shader empty;
		EXPECT_FALSE(empty.IsValid());
		expectError(empty.Save(m_Memory), EErrorCode::InvalidArgument);
	}

	TEST_F(ShaderTest, RealComputeAssetRetainsBothTargetsAndEntryPoints)
	{
		Result<Shader> loaded = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(loaded);
		EXPECT_EQ(loaded.Value().GetName(), "Compute");
		EXPECT_EQ(loaded.Value().GetEntryPoints().Size(), 2u);
		EXPECT_EQ(loaded.Value().GetVariants().Size(), 4u);
		for (EShaderTarget target : {EShaderTarget::Dxil, EShaderTarget::SpirV})
		{
			Result<uint32> index = loaded.Value().FindVariant({"Main", profile(target)});
			ASSERT_TRUE(index);
			const ShaderVariant& variant = loaded.Value().GetVariants()[index.Value()];
			EXPECT_GT(variant.Bytecode.Size(), 32u);
			EXPECT_EQ(variant.Bindings.Size(), 3u);
		}
	}

	TEST_F(ShaderTest, CopyAndMoveKeepDataAfterOriginalAndInputBytesAreReleased)
	{
		Shader retained;
		Hash256 identity;
		{
			Result<Array<Byte>> bytes = readFixture("Compute", m_Memory);
			ASSERT_TRUE(bytes);
			Result<Shader> loaded = Shader::Load(bytes.Value().View(), m_Memory);
			ASSERT_TRUE(loaded);
			identity = loaded.Value().GetIdentity();
			retained = loaded.Value();
		}
		Shader moved = Move(retained);
		EXPECT_FALSE(retained.IsValid());
		EXPECT_EQ(moved.GetIdentity(), identity);
		EXPECT_EQ(moved.GetName(), "Compute");
		EXPECT_TRUE(moved.Save(m_Memory));
	}

	TEST_F(ShaderTest, ExactVariantLookupDoesNotFallbackToAnotherProfile)
	{
		Result<Shader> loaded = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(loaded);
		ShaderTargetProfile unknown;
		unknown.ShaderModelMinor = 5;
		expectError(loaded.Value().FindVariant({"Main", unknown}), EErrorCode::NotFound);
		expectError(loaded.Value().FindVariant({"Missing", {}}), EErrorCode::NotFound);
		expectError(loaded.Value().FindKernel("Missing"), EErrorCode::NotFound);
		expectError(loaded.Value().FindProperty("Missing"), EErrorCode::NotFound);
	}

	TEST_F(ShaderTest, InvalidInspectionAsserts)
	{
		EXPECT_DEATH({ Shader empty; (void)empty.GetVariants(); }, "Shader artifact must be valid");
		Result<Shader> loaded = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(loaded);
		EXPECT_DEATH((void)loaded.Value().GetVariantCodeHash(1000), "valid variant index");
	}

	TEST_F(ShaderTest, PropertyHandlesRejectForeignIdentityAndOutOfRangeIndex)
	{
		Result<Shader> loaded = loadFixture("Compute", m_Memory);
		ASSERT_TRUE(loaded);
		expectError(loaded.Value().GetProperty({{}, 0}), EErrorCode::InvalidArgument);
		expectError(loaded.Value().GetProperty({loaded.Value().GetIdentity(), 999}), EErrorCode::InvalidArgument);
	}

	TEST_F(ShaderTest, RealSurfacePropertyResolvesThroughSelectedPixelBindings)
	{
		Result<Shader> asset = loadFixture("Surface", m_Memory);
		ASSERT_TRUE(asset);
		Result<ShaderPropertyHandle> handle = asset.Value().FindProperty("Albedo");
		ASSERT_TRUE(handle);
		Result<const ShaderProperty*> property = asset.Value().GetProperty(handle.Value());
		ASSERT_TRUE(property);
		EXPECT_EQ(property.Value()->GetType(), EShaderPropertyType::Texture2D);
		for (EShaderTarget target : {EShaderTarget::Dxil, EShaderTarget::SpirV})
		{
			ShaderOptions options(m_Memory);
			Result<ShaderPassSelection> selected = asset.Value().SelectPass("Surface", profile(target), options, m_Memory);
			ASSERT_TRUE(selected);
			Result<Array<ShaderPropertyLocation>> locations = selected.Value().GetLayout().FindPropertyBindings("Albedo", m_Memory);
			ASSERT_TRUE(locations);
			ASSERT_EQ(locations.Value().Size(), 1u);
			const ShaderPropertyLocation& location = locations.Value()[0];
			const ShaderEntrySelection& entry = selected.Value().GetStages()[location.SelectionIndex];
			EXPECT_EQ(entry.Artifact.GetVariants()[entry.VariantIndex].Bindings[location.BindingIndex].Name, "Albedo");
			EXPECT_EQ(location.FieldIndex, NO_DATA_LAYOUT);
			expectError(selected.Value().GetLayout().FindPropertyBindings("Missing", m_Memory), EErrorCode::NotFound);
		}
	}
}
