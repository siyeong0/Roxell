#if ROXELL_TEST_GPU
#include "GpuDeviceTest.h"
#else
#include "DeviceTestFixture.h"
#endif

namespace rox::rhi
{
	using namespace core;

#if ROXELL_TEST_GPU
	using TransferDeviceBase = GpuDeviceTest;
#else
	using TransferDeviceBase = DeviceTest;
#endif

	class TextureTransferTest : public TransferDeviceBase, public testing::WithParamInterface<bool> {};

	TEST_P(TextureTransferTest, SelectedMipLayerRoundTripsWithPaddedRowsAndNonzeroOffsets)
	{
		const bool bPrepared = GetParam();
		TextureDesc desc{ 8, 8, EFormat::RGBA8_UNORM, ETextureUsage::CopySource | ETextureUsage::CopyDestination };
		desc.MipLevels = 2;
		desc.ArrayLayers = 2;
		const Result<TextureHandle> texture = m_Device->CreateTexture(desc);
		ASSERT_TRUE(texture);
		const TextureSubresource subresource{ 1, 1 };
		const Result<TextureBufferFootprint> footprint = m_Device->GetTextureBufferFootprint(texture.Value(), subresource);
		ASSERT_TRUE(footprint);
		ASSERT_EQ(footprint.Value().Width, 4u);
		ASSERT_EQ(footprint.Value().Height, 4u);
		constexpr uint64 OFFSET = 512;
		const uint64 byteCount = footprint.Value().ByteCount + OFFSET;
		const Result<BufferHandle> upload = m_Device->CreateBuffer({ byteCount, EBufferUsage::CopySource, EBufferMemory::CpuWrite });
		const Result<BufferHandle> readback = m_Device->CreateBuffer({ byteCount, EBufferUsage::CopyDestination, EBufferMemory::CpuRead });
		ASSERT_TRUE(upload);
		ASSERT_TRUE(readback);
		const Result<Span<Byte>> mapped = m_Device->MapBuffer(upload.Value(), 0, byteCount, 10000);
		ASSERT_TRUE(mapped);
		for (uint32 row = 0; row < 4; ++row)
		{
			for (uint32 byte = 0; byte < 16; ++byte)
			{
				mapped.Value()[OFFSET + row * footprint.Value().RowPitchBytes + byte] = static_cast<Byte>(row * 16 + byte);
			}
		}
		ASSERT_TRUE(m_Device->UnmapBuffer(upload.Value()));
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Copy);
		ASSERT_TRUE(list);
		const TextureBufferCopy input{ upload.Value(), texture.Value(), subresource, OFFSET };
		const TextureBufferCopy output{ readback.Value(), texture.Value(), subresource, OFFSET };
		if (bPrepared)
		{
			const PreparedBufferAccessDesc buffers[]{
				{ upload.Value(), { OFFSET, footprint.Value().ByteCount }, EBufferState::CopySource },
				{ readback.Value(), { OFFSET, footprint.Value().ByteCount }, EBufferState::CopyDestination }
			};
			const PreparedTextureAccessDesc textures[]{
				{ texture.Value(), 1, 1, 1, 1, ETextureAspect::Color, ETextureState::CopyDestination },
				{ texture.Value(), 1, 1, 1, 1, ETextureAspect::Color, ETextureState::CopySource }
			};
			PreparedTextureBufferCopyDesc copies[]{ { input, ETextureBufferCopyDirection::BufferToTexture }, { output, ETextureBufferCopyDirection::TextureToBuffer } };
			CommandAccessPhaseDesc phases[2];
			for (uint32 phase = 0; phase < 2; ++phase)
			{
				phases[phase].Buffers = { &buffers[phase], 1 };
				phases[phase].Textures = { &textures[phase], 1 };
				phases[phase].TextureBufferCopies = { &copies[phase], 1 };
				phases[phase].Capacity.TextureCopies = 1;
			}
			const CommandListPreparationDesc preparation{ list.Value(), phases };
			ASSERT_TRUE(m_Device->PrepareCommandLists({ &preparation, 1 }));
			copies[0].Copy.BufferOffsetBytes = 0;
			copies[1].Copy.BufferOffsetBytes = 0;
			ASSERT_TRUE(m_Device->BeginPreparedRecording({ &list.Value(), 1 }));
			m_bPreparedRecording = true;
			ASSERT_TRUE(m_Device->BeginPreparedAccessPhase(list.Value(), 0));
			const PreparedTextureBufferCopyRef uploadRef = m_Device->GetPreparedTextureBufferCopy(list.Value(), 0, 0);
			EXPECT_DEATH((void)m_Device->CopyTextureToBuffer(list.Value(), uploadRef), "declared transfer direction");
			ASSERT_TRUE(m_Device->CopyBufferToTexture(list.Value(), uploadRef));
			ASSERT_TRUE(m_Device->BeginPreparedAccessPhase(list.Value(), 1));
			const PreparedTextureBufferCopyRef readbackRef = m_Device->GetPreparedTextureBufferCopy(list.Value(), 1, 0);
			EXPECT_DEATH((void)m_Device->CopyBufferToTexture(list.Value(), readbackRef), "declared transfer direction");
			ASSERT_TRUE(m_Device->CopyTextureToBuffer(list.Value(), readbackRef));
			ASSERT_TRUE(m_Device->FinishPreparedCommandList(list.Value()));
			const Result<ParallelRecordingStatistics> ended = m_Device->EndPreparedRecording();
			m_bPreparedRecording = false;
			ASSERT_TRUE(ended);
		}
		else
		{
			ASSERT_TRUE(m_Device->CopyBufferToTexture(list.Value(), input));
			ASSERT_TRUE(m_Device->CopyTextureToBuffer(list.Value(), output));
		}
		const SubmitOutcome submitted = bPrepared ? m_Device->SubmitPrepared(list.Value()) : m_Device->Submit(list.Value());
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		ASSERT_TRUE(m_Device->WaitForSubmission(submitted.Token, 10000));
		const Result<Span<Byte>> result = m_Device->MapBuffer(readback.Value(), 0, byteCount, 10000);
		ASSERT_TRUE(result);
		for (uint32 row = 0; row < 4; ++row)
		{
			for (uint32 byte = 0; byte < 16; ++byte)
			{
				SCOPED_TRACE(row * 16 + byte);
				EXPECT_EQ(result.Value()[OFFSET + row * footprint.Value().RowPitchBytes + byte], static_cast<Byte>(row * 16 + byte));
			}
		}
		EXPECT_TRUE(m_Device->UnmapBuffer(readback.Value()));
	}

	INSTANTIATE_TEST_SUITE_P(CommandPaths, TextureTransferTest, testing::Bool());
}
