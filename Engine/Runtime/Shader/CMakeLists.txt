set(ROXELL_SHADER_SOURCES
    Public/Shader.h
    Public/ShaderBufferWriter.h
    Public/ShaderBuilder.h
    Public/ShaderKernel.h
    Public/ShaderLayout.h
    Public/ShaderOptions.h
    Public/ShaderPass.h
    Public/ShaderProperty.h

    Private/CanonicalArtifactOrder.cpp
    Private/CanonicalArtifactOrder.h
    Private/Reader.cpp
    Private/Reader.h
    Private/Shader.cpp
    Private/Shader_selection.cpp
    Private/ShaderBufferWriter.cpp
    Private/ShaderBuilder.cpp
    Private/ShaderDescription.cpp
    Private/ShaderDescription.h
    Private/ShaderLayout.cpp
    Private/ShaderLocalRootMetadata.cpp
    Private/ShaderLocalRootMetadata.h
    Private/ShaderMetadata.cpp
    Private/ShaderMetadata.h
    Private/ShaderOptions.cpp
    Private/ShaderPayloadCanonicalizer.cpp
    Private/ShaderPayloadCanonicalizer.h
    Private/ShaderProperty.cpp
    Private/ShaderRayMetadata.cpp
    Private/ShaderRayMetadata.h
    Private/ShaderSerialization.cpp
    Private/ShaderSerialization.h
    Private/ShaderWire.hpp
    Private/Writer.cpp
    Private/Writer.h
)

add_library(Runtime-Shader STATIC ${ROXELL_SHADER_SOURCES})

target_include_directories(Runtime-Shader PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>)
target_link_libraries(Runtime-Shader PUBLIC Runtime-Core)
set_property(TARGET Runtime-Shader PROPERTY FOLDER "Engine")

get_target_property(shader_sources Runtime-Shader SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${shader_sources})
