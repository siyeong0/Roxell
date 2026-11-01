#pragma once

#include <Rhi.h>
#include <Array.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <gtest/gtest.h>

namespace rox::rhi
{
	class GpuDeviceTest : public testing::Test
	{
	protected:
		void SetUp() override;
		void TearDown() override;
		[[nodiscard]] testing::AssertionResult compileShader(const char* source, const wchar_t* entry, const wchar_t* target, core::Array<core::Byte>& output);

		core::TrackingMemoryResource m_Memory{ core::SystemMemoryResource::Get() };
		core::UniquePtr<Device> m_Device;
		bool m_bPreparedRecording = false;
	};
}
