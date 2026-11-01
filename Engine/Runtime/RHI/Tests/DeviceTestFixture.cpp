#include "DeviceTestFixture.h"

namespace rox::rhi
{
	using namespace core;

	void DeviceTest::SetUp()
	{
		ASSERT_TRUE(CreateDevice(EBackend::Null, m_Memory, &m_Device));
	}

	void DeviceTest::TearDown()
	{
		m_Memory.Reset(NumericLimits<usize>::max());
		if (m_Device)
		{
			if (m_bPreparedRecording)
			{
				m_Device->CancelPreparedRecording();
			}
			EXPECT_TRUE(m_Device->WaitIdle(1000));
			m_Device.Reset();
		}
		EXPECT_EQ(m_Tracking.GetStatistics().LiveAllocationCount, 0u);
	}
}
