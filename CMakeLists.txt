cmake_minimum_required(VERSION 3.28)

project(Roxell VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_library(Build-Settings INTERFACE)
target_compile_features(Build-Settings INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(Build-Settings INTERFACE
        /W4
        "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
        /permissive-
        /Zc:preprocessor
        /utf-8
        /fp:precise
    )
else()
    target_compile_options(Build-Settings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

include(CTest)

option(ROXELL_ENABLE_PROFILING "Enable profiling instrumentation" ON)

find_package(Threads REQUIRED)

set(ROXELL_RHI_BACKEND "Null" CACHE STRING "RHI backend: Null, D3D12 or Vulkan")
set_property(CACHE ROXELL_RHI_BACKEND PROPERTY STRINGS Null D3D12 Vulkan)
if(NOT ROXELL_RHI_BACKEND MATCHES "^(Null|D3D12|Vulkan)$")
    message(FATAL_ERROR "ROXELL_RHI_BACKEND must be Null, D3D12 or Vulkan")
endif()
if(ROXELL_RHI_BACKEND STREQUAL "D3D12" AND NOT WIN32)
    message(FATAL_ERROR "The D3D12 backend requires Windows")
endif()
option(ROXELL_BUILD_RHI_GPU_TESTS "Build Google Test native RHI integration tests" OFF)

add_subdirectory(ThirdParty)
add_subdirectory(Engine/Runtime/Core)
add_subdirectory(Engine/Runtime/Platform)
add_subdirectory(Engine/Runtime/RHI)
add_subdirectory(Engine/Runtime/Shader)

if(BUILD_TESTING)
    include(Engine/Runtime/Core/Tests.cmake)
    include(Engine/Runtime/Platform/Tests.cmake)
    include(Engine/Runtime/RHI/Tests.cmake)
    include(Engine/Runtime/Shader/Tests.cmake)
endif()
