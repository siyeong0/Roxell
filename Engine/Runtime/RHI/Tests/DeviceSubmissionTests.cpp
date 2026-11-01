#include "DeviceTestFixture.h"
#include <thread>

namespace rox::rhi
{
	using namespace core;

	// Existing RHI fault injection is available only in contract-test builds.
	void FailNextSubmissionReservationForTesting() noexcept;
	void FailNextAcceptedSubmitForTesting() noexcept;

	TEST_F(DeviceTest, BusySubmissionRetainsTheExactListForRetry)
	{
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Copy);
		ASSERT_TRUE(list);
		FailNextSubmissionReservationForTesting();
		const SubmitOutcome busy = m_Device->Submit(list.Value());
		EXPECT_EQ(busy.Status, ESubmitStatus::Busy);
		const SubmitOutcome submitted = m_Device->Submit(list.Value());
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		const Result<bool> complete = m_Device->IsSubmissionComplete(submitted.Token);
		ASSERT_TRUE(complete);
		EXPECT_TRUE(complete.Value());
		EXPECT_TRUE(m_Device->WaitForSubmission(submitted.Token, 0));
	}

	TEST_F(DeviceTest, AcceptedFailureClosesAdmissionAndReleasesRetentionAfterDrain)
	{
		const Result<BufferHandle> buffer = m_Device->CreateBuffer({ 16, EBufferUsage::CopyDestination });
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Copy);
		const Result<CommandListHandle> pending = m_Device->CreateCommandList(EQueue::Copy);
		ASSERT_TRUE(buffer);
		ASSERT_TRUE(list);
		ASSERT_TRUE(pending);
		ASSERT_TRUE(m_Device->ClearBufferUInt(list.Value(), buffer.Value(), 17));
		FailNextAcceptedSubmitForTesting();
		EXPECT_EQ(m_Device->Submit(list.Value()).Status, ESubmitStatus::Failed);
		EXPECT_FALSE(m_Device->DestroyBuffer(buffer.Value()));
		EXPECT_EQ(m_Device->Submit(pending.Value()).Status, ESubmitStatus::Failed);
		ASSERT_TRUE(m_Device->WaitIdle(1000));
		EXPECT_TRUE(m_Device->DestroyBuffer(buffer.Value()));
	}

	TEST_F(DeviceTest, ParallelListsRecordOnWorkersAndSubmitAfterJoin)
	{
		const Result<BufferHandle> source = m_Device->CreateBuffer({ 16, EBufferUsage::CopySource | EBufferUsage::CopyDestination });
		const Result<BufferHandle> destination = m_Device->CreateBuffer({ 16, EBufferUsage::CopySource | EBufferUsage::CopyDestination });
		const Result<CommandListHandle> first = m_Device->CreateCommandList(EQueue::Copy);
		const Result<CommandListHandle> second = m_Device->CreateCommandList(EQueue::Copy);
		ASSERT_TRUE(source);
		ASSERT_TRUE(destination);
		ASSERT_TRUE(first);
		ASSERT_TRUE(second);
		const Byte input[]{ Byte{ 1 }, Byte{ 2 }, Byte{ 3 }, Byte{ 4 }, Byte{ 5 }, Byte{ 6 }, Byte{ 7 }, Byte{ 8 } };
		ASSERT_TRUE(m_Device->UploadBuffer(source.Value(), 0, input));
		ASSERT_TRUE(m_Device->BeginParallelRecording());
		bool bFirst = false;
		bool bSecond = false;
		std::thread workerA([&] { bFirst = static_cast<bool>(m_Device->CopyBuffer(first.Value(), { source.Value(), destination.Value(), 0, 0, 4 })); });
		std::thread workerB([&] { bSecond = static_cast<bool>(m_Device->CopyBuffer(second.Value(), { source.Value(), destination.Value(), 4, 4, 4 })); });
		workerA.join();
		workerB.join();
		const Result<ParallelRecordingStatistics> ended = m_Device->EndParallelRecording();
		ASSERT_TRUE(ended);
		ASSERT_TRUE(bFirst);
		ASSERT_TRUE(bSecond);
		ASSERT_EQ(m_Device->Submit(first.Value()).Status, ESubmitStatus::Submitted);
		const SubmitOutcome submitted = m_Device->Submit(second.Value());
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		Byte output[8]{};
		ASSERT_TRUE(m_Device->ReadBufferBlocking(destination.Value(), 0, output, submitted.Token, 1000));
		for (uint32 index = 0; index < 8; ++index)
		{
			EXPECT_EQ(output[index], input[index]);
		}
	}

	class SubmissionTokenDeathTest : public DeviceTest, public testing::WithParamInterface<uint32> {};

	TEST_P(SubmissionTokenDeathTest, TokensMustIdentifyPreviouslySubmittedWorkOnTheirOwnQueue)
	{
		const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Copy);
		ASSERT_TRUE(list);
		const SubmitOutcome submitted = m_Device->Submit(list.Value());
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		SubmissionToken invalid = submitted.Token;
		switch (GetParam())
		{
		case 0: invalid = {}; break;
		case 1: ++invalid.Owner; break;
		case 2: invalid.Queue = EQueue::Graphics; break;
		case 3: invalid.Value += 4; break;
		}
		EXPECT_DEATH((void)m_Device->IsSubmissionComplete(invalid), "previously submitted token");
		EXPECT_DEATH((void)m_Device->WaitForSubmission(invalid, 0), "previously submitted token");
	}

	INSTANTIATE_TEST_SUITE_P(InvalidIdentity, SubmissionTokenDeathTest, testing::Range(0u, 4u));
}
