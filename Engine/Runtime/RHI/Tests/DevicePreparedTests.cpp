#include "DeviceTestFixture.h"

namespace rox::rhi
{
	using namespace core;

	class PreparedDeviceTest : public DeviceTest
	{
	protected:
		void SetUp() override
		{
			DeviceTest::SetUp();
			ASSERT_FALSE(HasFatalFailure());
			const Result<BufferHandle> source = m_Device->CreateBuffer({ 16, EBufferUsage::CopySource | EBufferUsage::CopyDestination });
			const Result<BufferHandle> destination = m_Device->CreateBuffer({ 16, EBufferUsage::CopySource | EBufferUsage::CopyDestination });
			const Result<CommandListHandle> list = m_Device->CreateCommandList(EQueue::Copy);
			ASSERT_TRUE(source);
			ASSERT_TRUE(destination);
			ASSERT_TRUE(list);
			m_Source = source.Value();
			m_Destination = destination.Value();
			m_List = list.Value();
			m_Accesses[0] = { m_Source, { 0, 16 }, EBufferState::CopySource };
			m_Accesses[1] = { m_Destination, { 0, 16 }, EBufferState::CopyDestination };
			m_Phase.Buffers = m_Accesses;
			m_Phase.Capacity.BufferCopies = 1;
			m_Preparation.List = m_List;
			m_Preparation.Phases = { &m_Phase, 1 };
		}

		Result<void> beginRecording()
		{
			Result<void> result = m_Device->BeginPreparedRecording({ &m_List, 1 });
			m_bPreparedRecording = static_cast<bool>(result);
			return result;
		}

		BufferHandle m_Source;
		BufferHandle m_Destination;
		CommandListHandle m_List;

		PreparedBufferAccessDesc m_Accesses[2];
		CommandAccessPhaseDesc m_Phase;
		CommandListPreparationDesc m_Preparation;
	};

	TEST_F(PreparedDeviceTest, RecordedCopiesNeedNoAllocationAndUseCapturedDeclarations)
	{
		const Byte input[]{ Byte{ 11 }, Byte{ 22 }, Byte{ 33 }, Byte{ 44 } };
		ASSERT_TRUE(m_Device->UploadBuffer(m_Source, 0, input));
		ASSERT_TRUE(m_Device->PrepareCommandLists({ &m_Preparation, 1 }));
		m_Accesses[0].Buffer = {};
		m_Phase.Capacity.BufferCopies = 0;
		m_Memory.Reset(0);
		ASSERT_TRUE(beginRecording());
		ASSERT_TRUE(m_Device->BeginPreparedAccessPhase(m_List, 0));
		ASSERT_TRUE(m_Device->CopyBuffer(m_List, { m_Source, m_Destination, 0, 8, 4 }));
		ASSERT_TRUE(m_Device->FinishPreparedCommandList(m_List));
		const Result<ParallelRecordingStatistics> ended = m_Device->EndPreparedRecording();
		m_bPreparedRecording = false;
		ASSERT_TRUE(ended);
		const SubmitOutcome submitted = m_Device->SubmitPrepared(m_List);
		ASSERT_EQ(submitted.Status, ESubmitStatus::Submitted);
		EXPECT_EQ(m_Memory.GetAllocationAttemptCount(), 0u);
		m_Memory.Reset(NumericLimits<usize>::max());
		Byte output[4]{};
		ASSERT_TRUE(m_Device->ReadBufferBlocking(m_Destination, 8, output, submitted.Token, 1000));
		for (uint32 index = 0; index < 4; ++index)
		{
			EXPECT_EQ(output[index], input[index]);
		}
	}

	TEST_F(PreparedDeviceTest, PreparationAllocationFailureCanRetryTheSameList)
	{
		m_Memory.Reset(0);
		EXPECT_FALSE(m_Device->PrepareCommandLists({ &m_Preparation, 1 }));
		m_Memory.Reset(NumericLimits<usize>::max());
		ASSERT_TRUE(m_Device->PrepareCommandLists({ &m_Preparation, 1 }));
		m_Device->DiscardCommandList(m_List);
		EXPECT_TRUE(m_Device->DestroyBuffer(m_Source));
		EXPECT_TRUE(m_Device->DestroyBuffer(m_Destination));
	}

	TEST_F(PreparedDeviceTest, CancelClosesRecordingButRetainsResourcesUntilDiscard)
	{
		ASSERT_TRUE(m_Device->PrepareCommandLists({ &m_Preparation, 1 }));
		ASSERT_TRUE(beginRecording());
		ASSERT_TRUE(m_Device->BeginPreparedAccessPhase(m_List, 0));
		ASSERT_TRUE(m_Device->CopyBuffer(m_List, { m_Source, m_Destination, 0, 0, 4 }));
		m_Device->CancelPreparedRecording();
		m_bPreparedRecording = false;
		EXPECT_FALSE(m_Device->DestroyBuffer(m_Source));
		m_Device->DiscardCommandList(m_List);
		EXPECT_TRUE(m_Device->DestroyBuffer(m_Source));
		EXPECT_TRUE(m_Device->DestroyBuffer(m_Destination));
		const Result<CommandListHandle> replacement = m_Device->CreateCommandList(EQueue::Copy);
		ASSERT_TRUE(replacement);
		m_Device->DiscardCommandList(replacement.Value());
	}

	class PreparedSelectorDeathTest : public PreparedDeviceTest, public testing::WithParamInterface<uint32> {};

	TEST_P(PreparedSelectorDeathTest, EmptySlotsCannotProduceTypedReferences)
	{
		ASSERT_TRUE(m_Device->PrepareCommandLists({ &m_Preparation, 1 }));
		switch (GetParam())
		{
		case 0: EXPECT_DEATH((void)m_Device->GetPreparedBindings(m_List, 0, 0), "selectors"); break;
		case 1: EXPECT_DEATH((void)m_Device->GetPreparedAttachment(m_List, 0, 0), "selectors"); break;
		case 2: EXPECT_DEATH((void)m_Device->GetPreparedTextureBufferCopy(m_List, 0, 0), "selectors"); break;
		case 3: EXPECT_DEATH((void)m_Device->GetPreparedResolve(m_List, 0, 0), "selectors"); break;
		case 4: EXPECT_DEATH((void)m_Device->GetPreparedIndirect(m_List, 0, 0), "selectors"); break;
		case 5: EXPECT_DEATH((void)m_Device->GetPreparedAccelStructBuild(m_List, 0, 0), "selectors"); break;
		case 6: EXPECT_DEATH((void)m_Device->GetPreparedAccelStructCompactionQuery(m_List, 0, 0), "selectors"); break;
		case 7: EXPECT_DEATH((void)m_Device->GetPreparedAccelStructCompact(m_List, 0, 0), "selectors"); break;
		}
		m_Device->DiscardCommandList(m_List);
	}

	INSTANTIATE_TEST_SUITE_P(TypedReferences, PreparedSelectorDeathTest, testing::Range(0u, 8u));

	class PreparedRecordingDeathTest : public PreparedDeviceTest, public testing::WithParamInterface<uint32> {};

	TEST_P(PreparedRecordingDeathTest, RejectsPhaseOrderCapacityAndAccessViolations)
	{
		m_Accesses[0].Range = { 0, 8 };
		ASSERT_TRUE(m_Device->PrepareCommandLists({ &m_Preparation, 1 }));
		ASSERT_TRUE(beginRecording());
		ASSERT_TRUE(m_Device->BeginPreparedAccessPhase(m_List, 0));
		switch (GetParam())
		{
		case 0:
			EXPECT_DEATH((void)m_Device->BeginPreparedAccessPhase(m_List, 0), "exactly once");
			break;
		case 1:
			EXPECT_DEATH((void)m_Device->BeginPreparedAccessPhase(m_List, 1), "exactly once");
			break;
		case 2:
			EXPECT_DEATH((void)m_Device->CopyBuffer(m_List, { m_Source, m_Destination, 8, 0, 4 }), "admitted input");
			break;
		case 3:
			ASSERT_TRUE(m_Device->CopyBuffer(m_List, { m_Source, m_Destination, 0, 0, 4 }));
			EXPECT_DEATH((void)m_Device->CopyBuffer(m_List, { m_Source, m_Destination, 0, 0, 4 }), "admitted input");
			break;
		case 4:
			EXPECT_DEATH((void)m_Device->EndPreparedRecording(), "every healthy list");
			break;
		}
	}

	INSTANTIATE_TEST_SUITE_P(RecordingContracts, PreparedRecordingDeathTest, testing::Range(0u, 5u));

	void FailPreparedSubmitAfterForTesting(uint32 acceptedCount, bool bRejectBeforeAcceptance) noexcept;

	class PreparedSubmitFailureTest : public PreparedDeviceTest, public testing::WithParamInterface<bool> {};

	TEST_P(PreparedSubmitFailureTest, FailureConsumesInputAndDrainingReleasesCapturedResources)
	{
		ASSERT_TRUE(m_Device->PrepareCommandLists({ &m_Preparation, 1 }));
		ASSERT_TRUE(beginRecording());
		ASSERT_TRUE(m_Device->BeginPreparedAccessPhase(m_List, 0));
		ASSERT_TRUE(m_Device->CopyBuffer(m_List, { m_Source, m_Destination, 0, 0, 4 }));
		ASSERT_TRUE(m_Device->FinishPreparedCommandList(m_List));
		const Result<ParallelRecordingStatistics> ended = m_Device->EndPreparedRecording();
		m_bPreparedRecording = false;
		ASSERT_TRUE(ended);
		FailPreparedSubmitAfterForTesting(0, GetParam());
		const SubmitOutcome failed = m_Device->SubmitPrepared(m_List);
		EXPECT_EQ(failed.Status, ESubmitStatus::Failed);
		ASSERT_TRUE(m_Device->WaitIdle(1000));
		EXPECT_TRUE(m_Device->DestroyBuffer(m_Source));
		EXPECT_TRUE(m_Device->DestroyBuffer(m_Destination));
	}

	INSTANTIATE_TEST_SUITE_P(NativeAcceptance, PreparedSubmitFailureTest, testing::Bool());
}
