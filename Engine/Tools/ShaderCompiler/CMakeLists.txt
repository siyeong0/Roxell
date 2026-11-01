add_library(Tool-ShaderCompiler STATIC
    Public/ShaderBuildJob.h
    Public/ShaderBuildPlan.h
    Public/ShaderBuildService.h
    Public/ShaderCompiler.h
    Public/ShaderCompilerTypes.h
    Public/ShaderModuleArtifact.h
    Private/Dxc/DxcCompiler.cpp
    Private/Dxc/DxcCompiler.h
    Private/Dxc/DxcReflector.cpp
    Private/Dxc/DxcReflector.h
    Private/Dxc/DxcSpirvReflector.cpp
    Private/Dxc/DxcSpirvReflector.h
    Private/Dxc/SnapshotIncludeHandler.cpp
    Private/Dxc/SnapshotIncludeHandler.h
    Private/Dxil/DxcPointer.hpp
    Private/Dxil/DxcSdk.h
    Private/Dxil/DxilReflection.cpp
    Private/Dxil/DxilReflection.h
    Private/Dxil/DxilReflection_bindings.cpp
    Private/Dxil/DxilReflection_localRoot.cpp
    Private/Dxil/DxilReflection_ray.cpp
    Private/ShaderBuildInputWriter.cpp
    Private/ShaderBuildInputWriter.h
    Private/ShaderBuildJob.cpp
    Private/ShaderBuildJob_result.cpp
    Private/ShaderBuildPlan.cpp
    Private/ShaderBuildService.cpp
    Private/ShaderBuildService_workers.cpp
    Private/ShaderBuildState.cpp
    Private/ShaderBuildState.h
    Private/ShaderCompiler.cpp
    Private/ShaderCompilerInternal.cpp
    Private/ShaderCompilerInternal.h
    Private/ShaderCompiler_diagnostics.cpp
    Private/ShaderCompiler_source.cpp
    Private/ShaderCompiler_validation.cpp
    Private/ShaderModuleArtifact.cpp
    Private/ShaderResultMemoryResource.cpp
    Private/ShaderResultMemoryResource.h
    Private/Slang/SlangCompiler.cpp
    Private/Slang/SlangCompiler.h
    Private/Slang/SlangCompiler_module.cpp
    Private/Slang/SlangCompiler_session.cpp
    Private/Slang/SlangCompiler_specialization.cpp
    Private/Slang/SlangFileSystem.cpp
    Private/Slang/SlangFileSystem.h
    Private/Slang/SlangReflector.cpp
    Private/Slang/SlangReflector.h
    Private/SourceClosure.cpp
    Private/SourceClosure.h
    Private/Spirv/SpirvMetadataReader.cpp
    Private/Spirv/SpirvMetadataReader.h
)
target_include_directories(Tool-ShaderCompiler PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>)
target_link_libraries(Tool-ShaderCompiler PUBLIC Runtime-Shader Runtime-Core PRIVATE ThirdParty-Slang ThirdParty-Dxc)

set(shader_sdk_identity "Roxell.ShaderSdk.1")
foreach(sdk IN ITEMS ThirdParty-Dxc ThirdParty-Slang)
    get_target_property(sdk_runtime_files ${sdk} ROXELL_RUNTIME_FILES)
    get_target_property(sdk_include_directory ${sdk} INTERFACE_INCLUDE_DIRECTORIES)
    file(GLOB_RECURSE sdk_headers CONFIGURE_DEPENDS
        "${sdk_include_directory}/*.h" "${sdk_include_directory}/*.hpp")
    list(SORT sdk_headers)
    foreach(sdk_file IN LISTS sdk_headers sdk_runtime_files)
        file(SHA256 "${sdk_file}" sdk_file_hash)
        string(APPEND shader_sdk_identity ";${sdk}:${sdk_file_hash}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sdk_file}")
    endforeach()
endforeach()
string(SHA256 ROXELL_SHADER_SDK_SHA256 "${shader_sdk_identity}")

get_target_property(ROXELL_SHADER_DXC_RUNTIME_FILES ThirdParty-Dxc ROXELL_RUNTIME_FILES)
list(GET ROXELL_SHADER_DXC_RUNTIME_FILES 0 ROXELL_SHADER_DXC_LIBRARY_PATH)
file(TO_CMAKE_PATH "${ROXELL_SHADER_DXC_LIBRARY_PATH}" ROXELL_SHADER_DXC_LIBRARY_PATH)
target_compile_definitions(Tool-ShaderCompiler PRIVATE
    ROXELL_SHADER_SDK_SHA256="${ROXELL_SHADER_SDK_SHA256}"
    ROXELL_SHADER_DXC_LIBRARY_PATH="${ROXELL_SHADER_DXC_LIBRARY_PATH}"
)
set_property(TARGET Tool-ShaderCompiler PROPERTY FOLDER "Engine")

get_target_property(shader_compiler_sources Tool-ShaderCompiler SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${shader_compiler_sources})
