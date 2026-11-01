#include <RhiBindingTypes.h>
#include <RhiCommandPreparation.h>
#include <RhiPipelineTypes.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	using namespace core;

	TEST(RhiBindingTypes, BufferValuePreservesRangeAndIdentityWhenCopied)
	{
		const BufferHandle handle{ 19, 7, 3 };
		const DescriptorValue source = DescriptorValue::Buffer(handle, { 256, 1024 });
		DescriptorValue copy = source;
		EXPECT_EQ(copy.GetKind(), EDescriptorValueKind::Buffer);
		EXPECT_EQ(copy.GetBuffer(), handle);
		EXPECT_EQ(copy.GetBufferRange().ByteOffset, 256u);
		EXPECT_EQ(copy.GetBufferRange().ByteCount, 1024u);
		copy = DescriptorValue::Sampler({ 20, 8, 4 });
		EXPECT_EQ(copy.GetKind(), EDescriptorValueKind::Sampler);
		EXPECT_EQ(copy.GetSampler(), (SamplerHandle{ 20, 8, 4 }));
		EXPECT_EQ(source.GetBuffer(), handle);
	}

	TEST(RhiBindingTypes, TextureAndViewValuesKeepSampledAndStorageKindsDistinct)
	{
		const TextureHandle texture{ 9, 2, 1 };
		const TextureViewHandle view{ 9, 4, 1 };
		const DescriptorValue sampled = DescriptorValue::SampledTexture(texture);
		const DescriptorValue storage = DescriptorValue::StorageTexture(texture);
		EXPECT_EQ(sampled.GetKind(), EDescriptorValueKind::SampledTexture);
		EXPECT_EQ(storage.GetKind(), EDescriptorValueKind::StorageTexture);
		EXPECT_EQ(sampled.GetTexture(), texture);
		EXPECT_EQ(storage.GetTexture(), texture);
		EXPECT_EQ(DescriptorValue::SampledView(view).GetTextureView(), view);
		EXPECT_EQ(DescriptorValue::StorageView(view).GetTextureView(), view);
		EXPECT_EQ(DescriptorValue::BufferView({ 9, 8, 1 }).GetBufferView(), (BufferViewHandle{ 9, 8, 1 }));
		EXPECT_EQ(DescriptorValue::AccelerationStructure({ 9, 11, 1 }).GetAccelerationStructure(), (AccelStructHandle{ 9, 11, 1 }));
	}

	class DescriptorValueDeathTest : public testing::TestWithParam<uint32> {};

	TEST_P(DescriptorValueDeathTest, EmptyValuesRejectEveryPayloadAccessor)
	{
		const DescriptorValue empty;
		EXPECT_EQ(empty.GetKind(), EDescriptorValueKind::Invalid);
		switch (GetParam())
		{
		case 0: EXPECT_DEATH((void)empty.GetBuffer(), "not a buffer"); break;
		case 1: EXPECT_DEATH((void)empty.GetBufferRange(), "not a buffer"); break;
		case 2: EXPECT_DEATH((void)empty.GetBufferView(), "not a buffer view"); break;
		case 3: EXPECT_DEATH((void)empty.GetTexture(), "not a texture"); break;
		case 4: EXPECT_DEATH((void)empty.GetTextureView(), "not a texture view"); break;
		case 5: EXPECT_DEATH((void)empty.GetSampler(), "not a sampler"); break;
		case 6: EXPECT_DEATH((void)empty.GetAccelerationStructure(), "not an acceleration structure"); break;
		}
	}

	INSTANTIATE_TEST_SUITE_P(Payloads, DescriptorValueDeathTest, testing::Range(0u, 7u));

	TEST(RhiPipelineTypes, InlineShaderInputBorrowsBytesAndEntryName)
	{
		const Byte bytes[]{ Byte{ 1 }, Byte{ 2 } };
		const char name[] = "Kernel";
		const ShaderInput code = ShaderInput::Code(bytes, name);
		EXPECT_EQ(code.GetKind(), ShaderInput::EKind::Code);
		EXPECT_EQ(code.GetCode().Data(), bytes);
		EXPECT_EQ(code.GetCode().Size(), 2u);
		EXPECT_EQ(code.GetEntryPoint(), name);
		const ShaderHandle handle{ 7, 9, 2 };
		const ShaderInput shader = ShaderInput::Shader(handle);
		EXPECT_EQ(shader.GetKind(), ShaderInput::EKind::Shader);
		EXPECT_EQ(shader.GetShader(), handle);
		EXPECT_TRUE(shader.GetCode().IsEmpty());
	}

	TEST(RhiCommandPreparation, BindingFactoriesSelectExactlyOnePipelineFamily)
	{
		const ComputePipelineHandle compute{ 2, 4, 6 };
		const GraphicsPipelineHandle graphics{ 3, 5, 7 };
		const ShaderTableHandle ray{ 4, 6, 8 };
		EXPECT_EQ(Get<ComputePipelineHandle>(PreparedBindingDesc::Compute(compute).Pipeline), compute);
		EXPECT_EQ(Get<GraphicsPipelineHandle>(PreparedBindingDesc::Graphics(graphics).Pipeline), graphics);
		EXPECT_EQ(Get<ShaderTableHandle>(PreparedBindingDesc::Ray(ray).Pipeline), ray);
	}
}
