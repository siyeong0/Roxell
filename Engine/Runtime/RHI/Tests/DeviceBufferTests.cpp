#include "DeviceTestFixture.h"
#include <cstring>

namespace rox::rhi
{
	using namespace core;

	class BufferTransferTest : public DeviceTest, public testing::WithParamInterface<uint32> {};

	TEST_P(BufferTransferTest, CopiesExactBytesAndPreservesDestinationGuards)
	{
		const uint32 count = GetParam();
		const EBufferUsage usage = EBufferUsage::CopySource | EBufferUsage::CopyDestination;
		const Result<BufferHandle> source = m_Device->CreateBuffer({ 256, usage });
		const Result<BufferHandle> destination = m_Device->CreateBuffer({ 256, usage });
		ASSERT_TRUE(source);
		ASSERT_TRUE(destination);
		Byte input[256];
		Byte guards[256];
		for (uint32 index = 0; index < 256; ++index)
		{
			input[index] = static_cast<Byte>((index * 37u + 11u) & 255u);
			guards[index] = Byte{ 0xa5 };
		}
		ASSERT_TRUE(m_Device->UploadBuffer(source.Value(), 0, input));
		ASSERT_TRUE(m_Device->UploadBuffer(destination.Value(), 0, guards));
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Copy);
		ASSERT_TRUE(list);
		ASSERT_TRUE(m_Device->CopyBuffer(list.Value(), { source.Value(), destination.Value(), 3, 7, count }));
		const Result<void> retained = m_Device->DestroyBuffer(source.Value());
		ASSERT_FALSE(retained);
		EXPECT_EQ(retained.GetError().Code, EErrorCode::Conflict);
		const SubmitOutcome submitted = m_Device->Submit(list.Value());
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		Byte output[256]{};
		ASSERT_TRUE(m_Device->ReadBufferBlocking(destination.Value(), 0, output, submitted.Token, 1000));
		for (uint32 index = 0; index < 256; ++index)
		{
			SCOPED_TRACE(index);
			EXPECT_EQ(output[index], index >= 7 && index < 7 + count ? input[index - 7 + 3] : Byte{ 0xa5 });
		}
		EXPECT_TRUE(m_Device->DestroyBuffer(source.Value()));
		EXPECT_TRUE(m_Device->DestroyBuffer(destination.Value()));
	}

	INSTANTIATE_TEST_SUITE_P(Boundaries, BufferTransferTest, testing::Values(1u, 3u, 4u, 15u, 16u, 63u, 64u, 249u));

	TEST_F(DeviceTest, WriteBufferSnapshotsCallerBytesAndPreservesCommandOrder)
	{
		const Result<BufferHandle> buffer = m_Device->CreateBuffer({ 8, EBufferUsage::CopySource | EBufferUsage::CopyDestination });
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Copy);
		ASSERT_TRUE(buffer);
		ASSERT_TRUE(list);
		Byte first[]{ Byte{ 1 }, Byte{ 2 }, Byte{ 3 }, Byte{ 4 } };
		const Byte second[]{ Byte{ 5 }, Byte{ 6 }, Byte{ 7 }, Byte{ 8 } };
		ASSERT_TRUE(m_Device->WriteBuffer(list.Value(), buffer.Value(), 0, first));
		first[0] = Byte{ 99 };
		ASSERT_TRUE(m_Device->WriteBuffer(list.Value(), buffer.Value(), 4, second));
		const SubmitOutcome submitted = m_Device->Submit(list.Value());
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		Byte output[8]{};
		ASSERT_TRUE(m_Device->ReadBufferBlocking(buffer.Value(), 0, output, submitted.Token, 1000));
		for (uint32 index = 0; index < 8; ++index)
		{
			EXPECT_EQ(output[index], static_cast<Byte>(index + 1));
		}
	}

	TEST_F(DeviceTest, DiscardDoesNotExecuteRecordedWrites)
	{
		const Result<BufferHandle> buffer = m_Device->CreateBuffer({ 4, EBufferUsage::CopySource | EBufferUsage::CopyDestination });
		ASSERT_TRUE(buffer);
		const Byte initial[]{ Byte{ 1 }, Byte{ 2 }, Byte{ 3 }, Byte{ 4 } };
		const Result<SubmissionToken> uploaded = m_Device->UploadBuffer(buffer.Value(), 0, initial);
		ASSERT_TRUE(uploaded);
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Copy);
		ASSERT_TRUE(list);
		ASSERT_TRUE(m_Device->ClearBufferUInt(list.Value(), buffer.Value(), 0xffffffffu));
		m_Device->DiscardCommandList(list.Value());
		Byte output[4]{};
		ASSERT_TRUE(m_Device->ReadBufferBlocking(buffer.Value(), 0, output, uploaded.Value(), 1000));
		EXPECT_EQ(std::memcmp(output, initial, sizeof(output)), 0);
	}

	TEST_F(DeviceTest, MapConflictsAreRecoverableAndUnmapAllowsReuse)
	{
		const Result<BufferHandle> buffer = m_Device->CreateBuffer({ 16, EBufferUsage::CopySource, EBufferMemory::CpuWrite });
		ASSERT_TRUE(buffer);
		const Result<Span<Byte>> mapped = m_Device->MapBuffer(buffer.Value(), 4, 8, 0);
		ASSERT_TRUE(mapped);
		EXPECT_EQ(mapped.Value().Size(), 8u);
		mapped.Value()[0] = Byte{ 17 };
		const Result<Span<Byte>> duplicate = m_Device->MapBuffer(buffer.Value(), 0, 1, 0);
		ASSERT_FALSE(duplicate);
		EXPECT_EQ(duplicate.GetError().Code, EErrorCode::Conflict);
		EXPECT_FALSE(m_Device->DestroyBuffer(buffer.Value()));
		ASSERT_TRUE(m_Device->UnmapBuffer(buffer.Value()));
		const Result<Span<Byte>> remapped = m_Device->MapBuffer(buffer.Value(), 4, 1, 0);
		ASSERT_TRUE(remapped);
		EXPECT_EQ(remapped.Value()[0], Byte{ 17 });
		EXPECT_TRUE(m_Device->UnmapBuffer(buffer.Value()));
		EXPECT_TRUE(m_Device->DestroyBuffer(buffer.Value()));
	}

	TEST_F(DeviceTest, AllocationFailureDoesNotPreventLaterResourceCreation)
	{
		m_Memory.Reset(0);
		const Result<BufferHandle> failed = m_Device->CreateBuffer({ 32, EBufferUsage::CopySource });
		EXPECT_FALSE(failed);
		m_Memory.Reset(NumericLimits<usize>::max());
		const Result<BufferHandle> buffer = m_Device->CreateBuffer({ 32, EBufferUsage::CopySource });
		ASSERT_TRUE(buffer);
		EXPECT_TRUE(m_Device->DestroyBuffer(buffer.Value()));
	}

	class BufferContractDeathTest : public DeviceTest, public testing::WithParamInterface<uint32> {};

	TEST_P(BufferContractDeathTest, RejectsInvalidCopyRangesAndUsage)
	{
		const Result<BufferHandle> source = m_Device->CreateBuffer({ 16, EBufferUsage::CopySource });
		const Result<BufferHandle> target = m_Device->CreateBuffer({ 16, EBufferUsage::CopyDestination });
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Copy);
		ASSERT_TRUE(source);
		ASSERT_TRUE(target);
		ASSERT_TRUE(list);
		BufferCopy copy{ source.Value(), target.Value(), 0, 0, 4 };
		switch (GetParam())
		{
		case 0: copy.ByteCount = 0; break;
		case 1: copy.SourceOffsetBytes = 13; break;
		case 2: copy.DestinationOffsetBytes = 13; break;
		case 3: copy.SourceOffsetBytes = NumericLimits<uint64>::max(); break;
		case 4: copy.ByteCount = NumericLimits<uint64>::max(); break;
		case 5: copy.Source = target.Value(); break;
		case 6: copy.Destination = source.Value(); break;
		}
		EXPECT_DEATH((void)m_Device->CopyBuffer(list.Value(), copy), "Buffer copy");
		m_Device->DiscardCommandList(list.Value());
	}

	INSTANTIATE_TEST_SUITE_P(InvalidCopies, BufferContractDeathTest, testing::Range(0u, 7u));

	class BufferHandleDeathTest : public DeviceTest, public testing::WithParamInterface<uint32> {};

	TEST_P(BufferHandleDeathTest, RejectsEmptyForeignOutOfRangeAndStaleHandles)
	{
		const Result<BufferHandle> buffer = m_Device->CreateBuffer({ 16, EBufferUsage::CopySource });
		ASSERT_TRUE(buffer);
		BufferHandle invalid = buffer.Value();
		switch (GetParam())
		{
		case 0: invalid = {}; break;
		case 1: ++invalid.Owner; break;
		case 2: invalid.Slot = NumericLimits<uint32>::max(); break;
		case 3: ++invalid.Generation; break;
		case 4: invalid.Generation = 0; break;
		}
		EXPECT_DEATH((void)m_Device->DestroyBuffer(invalid), "handle");
		EXPECT_TRUE(m_Device->DestroyBuffer(buffer.Value()));
	}

	INSTANTIATE_TEST_SUITE_P(InvalidHandles, BufferHandleDeathTest, testing::Range(0u, 5u));
}
