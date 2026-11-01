set(rhi_test_directory "${CMAKE_CURRENT_LIST_DIR}/Tests")
add_executable(Test-RHI
    "${rhi_test_directory}/RhiTestMain.cpp"
    "${rhi_test_directory}/RhiFormatTests.cpp"
    "${rhi_test_directory}/RhiCommandTypesTests.cpp"
    "${rhi_test_directory}/RhiBindingTypesTests.cpp"
    "${rhi_test_directory}/RhiValidationTests.cpp"
    "${rhi_test_directory}/SubmissionCompletionTests.cpp"
    "${rhi_test_directory}/RhiDiagnosticsTests.cpp"
)
if(ROXELL_RHI_BACKEND STREQUAL "Null")
    target_sources(Test-RHI PRIVATE
        "${rhi_test_directory}/DeviceTests.cpp"
        "${rhi_test_directory}/DeviceTestFixture.cpp"
        "${rhi_test_directory}/DeviceTestFixture.h"
        "${rhi_test_directory}/DeviceBufferTests.cpp"
        "${rhi_test_directory}/DeviceValidationTests.cpp"
        "${rhi_test_directory}/DevicePreparedTests.cpp"
        "${rhi_test_directory}/DeviceSubmissionTests.cpp"
        "${rhi_test_directory}/DeviceTextureTests.cpp"
        "${rhi_test_directory}/DeviceTextureTransferTests.cpp"
        "${rhi_test_directory}/DeviceStateTests.cpp"
        "${rhi_test_directory}/DeviceShaderTests.cpp"
        "${rhi_test_directory}/DeviceLifetimeTests.cpp"
    )
endif()
target_link_libraries(Test-RHI PRIVATE Runtime-RHI GTest::gtest)
target_include_directories(Test-RHI PRIVATE "${CMAKE_CURRENT_LIST_DIR}/Private")
set_property(TARGET Test-RHI PROPERTY FOLDER "Tests/RHI")
gtest_discover_tests(Test-RHI
    TEST_PREFIX "Roxell.RHI."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 30 LABELS "rhi-unit"
)

option(ROXELL_BUILD_RHI_GPU_TESTS "Build Google Test native RHI integration tests" OFF)
option(ROXELL_RHI_GPU_TEST_VALIDATION "Require native validation when running RHI GPU tests" ON)
if(ROXELL_BUILD_RHI_GPU_TESTS)
    if(ROXELL_RHI_BACKEND STREQUAL "Null" OR NOT WIN32 OR NOT TARGET ThirdParty-Dxc)
        message(FATAL_ERROR "RHI GPU tests require a Windows D3D12/Vulkan configuration with DXC")
    endif()
    add_executable(Test-RHIGpu
        "${rhi_test_directory}/RhiTestMain.cpp"
        "${rhi_test_directory}/GpuDeviceTest.h"
        "${rhi_test_directory}/GpuDeviceTest.cpp"
        "${rhi_test_directory}/GpuComputeTests.cpp"
        "${rhi_test_directory}/GpuGraphicsTests.cpp"
        "${rhi_test_directory}/GpuResourceTests.cpp"
        "${rhi_test_directory}/DeviceTextureTransferTests.cpp"
    )
    target_link_libraries(Test-RHIGpu PRIVATE Runtime-RHI ThirdParty-Dxc GTest::gtest)
    target_compile_definitions(Test-RHIGpu PRIVATE
        ROXELL_TEST_GPU=1
        ROXELL_TEST_VULKAN=$<STREQUAL:${ROXELL_RHI_BACKEND},Vulkan>
        ROXELL_TEST_GPU_VALIDATION=$<BOOL:${ROXELL_RHI_GPU_TEST_VALIDATION}>
    )
    roxell_copy_dxc_runtime(Test-RHIGpu)
    set_property(TARGET Test-RHIGpu PROPERTY FOLDER "Tests/RHI")
    gtest_discover_tests(Test-RHIGpu
        TEST_PREFIX "Roxell.RHI.Gpu.${ROXELL_RHI_BACKEND}."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 120 LABELS "rhi-gpu" RESOURCE_LOCK "roxell-gpu"
    )
endif()
