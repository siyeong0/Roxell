add_library(ThirdParty-NlohmannJson INTERFACE)
target_include_directories(ThirdParty-NlohmannJson SYSTEM INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json/include>
)

if(ROXELL_RHI_BACKEND STREQUAL "Vulkan")
    add_subdirectory(vulkan_headers)
endif()
if(ROXELL_BUILD_SHADER_COMPILER OR (BUILD_TESTING AND ROXELL_BUILD_RHI_GPU_TESTS))
    add_subdirectory(dxc)
endif()

if(ROXELL_BUILD_SHADER_COMPILER)
    add_subdirectory(slang)
endif()
