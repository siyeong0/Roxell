#pragma once

#include <Rhi.h>
#include <FailingMemoryResource.h>
#include <NumericLimits.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	class DeviceTest : public testing::Test
	{
	protected:
		void SetUp() override;
		void TearDown() override;

		core::TrackingMemoryResource m_Tracking{ core::SystemMemoryResource::Get() };
		core::FailingMemoryResource m_Memory{ m_Tracking, core::NumericLimits<core::usize>::max() };
		core::UniquePtr<Device> m_Device;
		bool m_bPreparedRecording = false;
	};
}
