#include "GpuDeviceTest.h"
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objidl.h>
#include <dxcapi.h>
#include <wrl/client.h>

namespace rox::rhi
{
	using namespace core;

	void GpuDeviceTest::SetUp()
	{
		DeviceCreateDesc desc;
#if ROXELL_TEST_VULKAN
		desc.Backend = EBackend::Vulkan;
#else
		desc.Backend = EBackend::D3D12;
#endif
		desc.bEnableValidation = ROXELL_TEST_GPU_VALIDATION != 0;
		const Result<void> created = CreateDevice(desc, m_Memory, &m_Device);
		ASSERT_TRUE(created) << created.GetError().DiagnosticID.CStr() << ": " << created.GetError().Message.CStr();
	}

	void GpuDeviceTest::TearDown()
	{
		if (m_Device)
		{
			if (m_bPreparedRecording)
			{
				m_Device->CancelPreparedRecording();
			}
			EXPECT_TRUE(m_Device->WaitIdle(10000));
			m_Device.Reset();
		}
		EXPECT_EQ(m_Memory.GetStatistics().LiveAllocationCount, 0u);
	}

	testing::AssertionResult GpuDeviceTest::compileShader(const char* source, const wchar_t* entry, const wchar_t* target, Array<Byte>& output)
	{
		Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
		if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(compiler.GetAddressOf()))))
		{
			return testing::AssertionFailure() << "DXC initialization failed.";
		}
		const DxcBuffer input{ source, std::strlen(source), DXC_CP_UTF8 };
		const wchar_t* arguments[]{ L"-E", entry, L"-T", target, L"-spirv", L"-fspv-target-env=vulkan1.2" };
		const uint32 count = m_Device->GetBackend() == EBackend::Vulkan ? 6 : 4;
		Microsoft::WRL::ComPtr<IDxcResult> result;
		if (FAILED(compiler->Compile(&input, arguments, count, nullptr, IID_PPV_ARGS(result.GetAddressOf()))))
		{
			return testing::AssertionFailure() << "DXC compilation call failed.";
		}
		HRESULT status = E_FAIL;
		if (FAILED(result->GetStatus(&status)) || FAILED(status))
		{
			Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
			(void)result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errors.GetAddressOf()), nullptr);
			return testing::AssertionFailure() << (errors ? errors->GetStringPointer() : "DXC produced no diagnostic.");
		}
		Microsoft::WRL::ComPtr<IDxcBlob> binary;
		if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(binary.GetAddressOf()), nullptr)) || !binary || !output.ResizeUninitialized(binary->GetBufferSize()))
		{
			return testing::AssertionFailure() << "Could not retrieve compiled shader bytes.";
		}
		std::memcpy(output.Data(), binary->GetBufferPointer(), binary->GetBufferSize());
		return testing::AssertionSuccess();
	}
}
