#include "DeviceTestFixture.h"

namespace rox::rhi
{
	using namespace core;

	class TextureDescriptorDeathTest : public DeviceTest, public testing::WithParamInterface<uint32> {};

	TEST_P(TextureDescriptorDeathTest, InvalidDescriptorsAreContractsRatherThanOperationalFailures)
	{
		TextureDesc desc{ 4, 4, EFormat::RGBA8_UNORM, ETextureUsage::CopySource };
		switch (GetParam())
		{
		case 0: desc.Width = 0; break;
		case 1: desc.Height = 0; break;
		case 2: desc.Depth = 0; break;
		case 3: desc.MipLevels = 0; break;
		case 4: desc.ArrayLayers = 0; break;
		case 5: desc.SampleCount = 3; break;
		case 6: desc.Format = EFormat::UNKNOWN; break;
		case 7: desc.Format = EFormat::COUNT; break;
		case 8: desc.Usage = ETextureUsage::None; break;
		case 9: desc.Dimension = static_cast<ETextureDimension>(255); break;
		case 10: desc.Dimension = ETextureDimension::Cube; desc.ArrayLayers = 5; break;
		case 11: desc.Depth = 2; break;
		}
		EXPECT_DEATH((void)m_Device->CreateTexture(desc), "valid texture descriptor");
	}

	INSTANTIATE_TEST_SUITE_P(InvalidDescriptions, TextureDescriptorDeathTest, testing::Range(0u, 12u));

	class BufferDescriptorDeathTest : public DeviceTest, public testing::WithParamInterface<uint32> {};

	TEST_P(BufferDescriptorDeathTest, InvalidDescriptorsAreContractsRatherThanOperationalFailures)
	{
		BufferDesc desc{ 16, EBufferUsage::CopySource };
		switch (GetParam())
		{
		case 0: desc.ByteCount = 0; break;
		case 1: desc.Usage = EBufferUsage::None; break;
		case 2: desc.Memory = static_cast<EBufferMemory>(255); break;
		case 3: desc.Usage = static_cast<EBufferUsage>(0x80000000u); break;
		}
		EXPECT_DEATH((void)m_Device->CreateBuffer(desc), "valid buffer descriptor");
	}

	INSTANTIATE_TEST_SUITE_P(InvalidDescriptions, BufferDescriptorDeathTest, testing::Range(0u, 4u));

	class TextureAllocationTest : public DeviceTest, public testing::WithParamInterface<usize> {};

	TEST_P(TextureAllocationTest, PartialAllocationFailuresReleaseStorageAndAllowRetry)
	{
		const TextureDesc desc{ 4, 4, EFormat::RGBA8_UNORM, ETextureUsage::CopySource | ETextureUsage::CopyDestination };
		m_Memory.Reset(GetParam());
		const Result<TextureHandle> attempted = m_Device->CreateTexture(desc);
		m_Memory.Reset(NumericLimits<usize>::max());
		if (attempted)
		{
			ASSERT_TRUE(m_Device->DestroyTexture(attempted.Value()));
		}
		else
		{
			EXPECT_EQ(attempted.GetError().Code, EErrorCode::ExternalFailure);
		}
		const Result<TextureHandle> retried = m_Device->CreateTexture(desc);
		ASSERT_TRUE(retried);
		EXPECT_TRUE(m_Device->DestroyTexture(retried.Value()));
	}

	INSTANTIATE_TEST_SUITE_P(AllocationBudgets, TextureAllocationTest, testing::Values(usize{0}, usize{1}, usize{2}, usize{3}, usize{4}, usize{8}, usize{16}));

	TEST_F(DeviceTest, UnsupportedNativeIntegrationDoesNotFabricateNativeHandles)
	{
		const Result<NativeDeviceHandles> native = m_Device->GetNativeDeviceHandles();
		ASSERT_FALSE(native);
		EXPECT_EQ(native.GetError().Code, EErrorCode::Unsupported);
		const Result<BufferHandle> external = m_Device->CreateExternalBuffer({ 16, EBufferUsage::CopySource });
		ASSERT_FALSE(external);
		EXPECT_EQ(external.GetError().Code, EErrorCode::Unsupported);
		const Result<ExternalFenceHandle> fence = m_Device->CreateExternalFence();
		ASSERT_FALSE(fence);
		EXPECT_EQ(fence.GetError().Code, EErrorCode::Unsupported);
		const Result<SamplerHandle> sampler = m_Device->CreateSampler({});
		ASSERT_FALSE(sampler);
		EXPECT_EQ(sampler.GetError().Code, EErrorCode::Unsupported);
		EXPECT_TRUE(m_Device->GetCapabilities().Features.bCopy);
		EXPECT_FALSE(m_Device->GetCapabilities().Features.bCompute);
		EXPECT_FALSE(m_Device->GetCapabilities().Features.bGraphics);
	}
}
