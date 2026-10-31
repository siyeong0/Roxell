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

add_subdirectory(ThirdParty)
add_subdirectory(Engine/Runtime/Core)

if(BUILD_TESTING)
    include(Engine/Runtime/Core/Tests.cmake)
endif()
