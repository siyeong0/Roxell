set_source_files_properties(Private/D3D12/D3D12IntegerClear.hlsl PROPERTIES HEADER_FILE_ONLY TRUE)

set(ROXELL_RHI_SOURCES
    Public/Device.h
    Public/Rhi.h
    Public/RhiBindingTypes.h
    Public/RhiCommandPreparation.h
    Public/RhiCommandTypes.h
    Public/RhiCoopVecTypes.h
    Public/RhiDeviceTypes.h
    Public/RhiDiagnostics.h
    Public/RhiFormat.h
    Public/RhiInterop.h
    Public/RhiPipelineTypes.h
    Public/RhiPresentationTypes.h
    Public/RhiRayTracingTypes.h
    Public/RhiResourceTypes.h
    Public/RhiTypes.h
    Private/AcquiredImagePlan.cpp
    Private/AcquiredImagePlan.h
    Private/AftermathBackend.cpp
    Private/AftermathBackend.h
    Private/Device.cpp
    Private/DeviceState.h
    Private/DeviceCommands.cpp
    Private/DeviceCommands.h
    Private/DevicePipelines.cpp
    Private/DevicePipelines.h
    Private/DeviceResources.cpp
    Private/DeviceResources.h
    Private/DeviceValidation.cpp
    Private/DeviceValidation.h
    Private/Device_accelStruct.cpp
    Private/Device_bindings.cpp
    Private/Device_buffer.cpp
    Private/Device_cluster.cpp
    Private/Device_commandList.cpp
    Private/Device_computePipeline.cpp
    Private/Device_coopVec.cpp
    Private/Device_descriptorBindings.cpp
    Private/Device_descriptorTable.cpp
    Private/Device_dispatch.cpp
    Private/Device_draw.cpp
    Private/Device_framebuffer.cpp
    Private/Device_graphicsPipeline.cpp
    Private/Device_heap.cpp
    Private/Device_indirect.cpp
    Private/Device_interop.cpp
    Private/Device_opacityMicromap.cpp
    Private/Device_preparation.cpp
    Private/Device_preparedAccelStruct.cpp
    Private/Device_preparedRecording.cpp
    Private/Device_preparedSubmission.cpp
    Private/Device_preparedValidation.cpp
    Private/Device_queries.cpp
    Private/Device_rayDispatch.cpp
    Private/Device_rayPipeline.cpp
    Private/Device_resourceState.cpp
    Private/Device_samplerFeedback.cpp
    Private/Device_shader.cpp
    Private/Device_sparseTexture.cpp
    Private/Device_submission.cpp
    Private/Device_swapChain.cpp
    Private/Device_texture.cpp
    Private/Device_textureClear.cpp
    Private/Device_textureTransfer.cpp
    Private/PreparedBindingCache.cpp
    Private/PreparedBindingCache.h
    Private/PreparedProgramCache.cpp
    Private/PreparedProgramCache.h
    Private/PreparedRecordingGroup.cpp
    Private/PreparedRecordingGroup.h
    Private/RecordedResourceUses.h
    Private/Rhi.cpp
    Private/RhiBindingTypes.cpp
    Private/RhiCommandPreparation.cpp
    Private/RhiCommandTypes.cpp
    Private/RhiDiagnostics.cpp
    Private/RhiFormat.cpp
    Private/RhiInterop.cpp
    Private/RhiPipelineTypes.cpp
    Private/SubmissionCompletion.cpp
    Private/SubmissionCompletion.h
    Private/D3D12/RayLocalRootRecipe.h
    Private/D3D12/RayLocalRootRecipe.cpp
    Private/Vulkan/SpirvDescriptorRequirements.h
    Private/Vulkan/SpirvDescriptorRequirements.cpp
)

add_library(Runtime-RHI STATIC ${ROXELL_RHI_SOURCES})

if(ROXELL_RHI_BACKEND STREQUAL "D3D12")
    target_sources(Runtime-RHI PRIVATE
        Private/D3D12/D3D12Backend.cpp
        Private/D3D12/D3D12Backend.h
        Private/D3D12/D3D12Backend_accelStruct.cpp
        Private/D3D12/D3D12Backend_bindingCache.cpp
        Private/D3D12/D3D12Backend_buffer.cpp
        Private/D3D12/D3D12Backend_cluster.cpp
        Private/D3D12/D3D12Backend_computePipeline.cpp
        Private/D3D12/D3D12Backend_coopVec.cpp
        Private/D3D12/D3D12Backend_diagnostics.cpp
        Private/D3D12/D3D12Backend_dispatch.cpp
        Private/D3D12/D3D12Backend_draw.cpp
        Private/D3D12/D3D12Backend_externalInterop.cpp
        Private/D3D12/D3D12Backend_framebuffer.cpp
        Private/D3D12/D3D12Backend_graphicsPipeline.cpp
        Private/D3D12/D3D12Backend_heap.cpp
        Private/D3D12/D3D12Backend_opacityMicromap.cpp
        Private/D3D12/D3D12Backend_preparedAccelStruct.cpp
        Private/D3D12/D3D12Backend_preparedRay.cpp
        Private/D3D12/D3D12Backend_preparedRecording.cpp
        Private/D3D12/D3D12Backend_rangeBindings.cpp
        Private/D3D12/D3D12Backend_rayPipeline.cpp
        Private/D3D12/D3D12Backend_samplerFeedback.cpp
        Private/D3D12/D3D12Backend_sparseTexture.cpp
        Private/D3D12/D3D12Backend_stateTracking.cpp
        Private/D3D12/D3D12Backend_swapChain.cpp
        Private/D3D12/D3D12Backend_texture.cpp
        Private/D3D12/D3D12Backend_textureClear.cpp
        Private/D3D12/D3D12Backend_textureTransfer.cpp
        Private/D3D12/D3D12Descriptors.cpp
        Private/D3D12/D3D12Descriptors.h
        Private/D3D12/D3D12IntegerClear.cpp
        Private/D3D12/D3D12IntegerClear.h
        Private/D3D12/D3D12IntegerClear.hlsl
        Private/D3D12/D3D12IntegerClearShaders.h
        Private/D3D12/D3D12Resources.cpp
        Private/D3D12/D3D12Resources.h
        Private/D3D12/D3D12State.cpp
        Private/D3D12/D3D12State.h
        Private/D3D12/D3D12Submission.cpp
        Private/D3D12/D3D12Submission.h
        Private/D3D12/GenerateD3D12IntegerClear.ps1
        Private/D3D12/NvApiSession.cpp
        Private/D3D12/NvApiSession.h
    )
    target_compile_definitions(Runtime-RHI PRIVATE ROXELL_RHI_BACKEND=1)
elseif(ROXELL_RHI_BACKEND STREQUAL "Vulkan")
    target_sources(Runtime-RHI PRIVATE
        Private/Vulkan/VulkanBackend.cpp
        Private/Vulkan/VulkanBackend.h
        Private/Vulkan/VulkanBackend_accelStruct.cpp
        Private/Vulkan/VulkanBackend_bindingCache.cpp
        Private/Vulkan/VulkanBackend_buffer.cpp
        Private/Vulkan/VulkanBackend_cluster.cpp
        Private/Vulkan/VulkanBackend_computePipeline.cpp
        Private/Vulkan/VulkanBackend_coopVec.cpp
        Private/Vulkan/VulkanBackend_creation.cpp
        Private/Vulkan/VulkanBackend_diagnostics.cpp
        Private/Vulkan/VulkanBackend_dispatch.cpp
        Private/Vulkan/VulkanBackend_draw.cpp
        Private/Vulkan/VulkanBackend_externalInterop.cpp
        Private/Vulkan/VulkanBackend_framebuffer.cpp
        Private/Vulkan/VulkanBackend_graphicsPipeline.cpp
        Private/Vulkan/VulkanBackend_heap.cpp
        Private/Vulkan/VulkanBackend_loader.cpp
        Private/Vulkan/VulkanBackend_preparedAccelStruct.cpp
        Private/Vulkan/VulkanBackend_preparedRay.cpp
        Private/Vulkan/VulkanBackend_preparedRecording.cpp
        Private/Vulkan/VulkanBackend_rangeBindings.cpp
        Private/Vulkan/VulkanBackend_rayPipeline.cpp
        Private/Vulkan/VulkanBackend_sparseTexture.cpp
        Private/Vulkan/VulkanBackend_stateTracking.cpp
        Private/Vulkan/VulkanBackend_swapChain.cpp
        Private/Vulkan/VulkanBackend_texture.cpp
        Private/Vulkan/VulkanBackend_textureClear.cpp
        Private/Vulkan/VulkanBackend_textureTransfer.cpp
        Private/Vulkan/VulkanDescriptors.cpp
        Private/Vulkan/VulkanDescriptors.h
        Private/Vulkan/VulkanFormat.cpp
        Private/Vulkan/VulkanFormat.h
        Private/Vulkan/VulkanFunctions.h
        Private/Vulkan/VulkanResources.cpp
        Private/Vulkan/VulkanResources.h
        Private/Vulkan/VulkanState.cpp
        Private/Vulkan/VulkanState.h
        Private/Vulkan/VulkanSubmission.cpp
        Private/Vulkan/VulkanSubmission.h
    )
    target_compile_definitions(Runtime-RHI PRIVATE ROXELL_RHI_BACKEND=2)
elseif(ROXELL_RHI_BACKEND STREQUAL "Null")
    target_sources(Runtime-RHI PRIVATE
        Private/Null/NullBackend.cpp
        Private/Null/NullBackend.h
    )
    target_compile_definitions(Runtime-RHI PRIVATE ROXELL_RHI_BACKEND=0)
endif()

# Localized MSVC /showIncludes output can leave Ninja without implicit header dependencies.
# Keep the public ABI and private backend contract explicit for this module.
set_source_files_properties(
    Private/Device.cpp Private/Null/NullBackend.cpp Private/D3D12/D3D12Backend.cpp
    Private/Vulkan/VulkanBackend.cpp Private/RhiFormat.cpp Private/Vulkan/VulkanFormat.cpp
    PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/Rhi.h;${CMAKE_CURRENT_SOURCE_DIR}/Public/RhiTypes.h;${CMAKE_CURRENT_SOURCE_DIR}/Public/RhiFormat.h;${CMAKE_CURRENT_SOURCE_DIR}/Public/RhiInterop.h;${CMAKE_CURRENT_SOURCE_DIR}/Public/RhiDiagnostics.h;${CMAKE_CURRENT_SOURCE_DIR}/Private/DeviceState.h;${CMAKE_CURRENT_SOURCE_DIR}/Private/AftermathBackend.h"
)
set_property(SOURCE Private/D3D12/D3D12Backend.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/SubmissionCompletion.h")
set_property(SOURCE Private/Vulkan/VulkanBackend.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/SubmissionCompletion.h")
if(ROXELL_RHI_BACKEND STREQUAL "D3D12")
    target_sources(Runtime-RHI PRIVATE Private/D3D12/DxgiFormat.h Private/D3D12/DxgiFormat.cpp)
endif()
if(BUILD_TESTING)
    target_compile_definitions(Runtime-RHI PRIVATE ROXELL_RHI_CONTRACT_TEST_HOOKS=1)
endif()
target_include_directories(Runtime-RHI PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>)
target_link_libraries(Runtime-RHI PUBLIC Build-Settings Runtime-Core)
if(ROXELL_RHI_BACKEND STREQUAL "Vulkan")
    target_link_libraries(Runtime-RHI PRIVATE ThirdParty-VulkanHeaders)
endif()
set(ROXELL_AFTERMATH_SDK_DIR "" CACHE PATH "Optional local NVIDIA Nsight Aftermath SDK root")
if(ROXELL_AFTERMATH_SDK_DIR)
    if(NOT WIN32 OR NOT EXISTS "${ROXELL_AFTERMATH_SDK_DIR}/include/GFSDK_Aftermath.h" OR
        NOT EXISTS "${ROXELL_AFTERMATH_SDK_DIR}/lib/x64/GFSDK_Aftermath_Lib.x64.dll")
        message(FATAL_ERROR "ROXELL_AFTERMATH_SDK_DIR requires the Windows Nsight Aftermath SDK headers and runtime DLL")
    endif()
    target_include_directories(Runtime-RHI PRIVATE "${ROXELL_AFTERMATH_SDK_DIR}/include")
    target_compile_definitions(Runtime-RHI PRIVATE ROXELL_RHI_WITH_AFTERMATH=1)
else()
    target_compile_definitions(Runtime-RHI PRIVATE ROXELL_RHI_WITH_AFTERMATH=0)
endif()
if(ROXELL_RHI_BACKEND STREQUAL "D3D12")
    target_link_libraries(Runtime-RHI PRIVATE d3d12 dxgi user32)
else()
    target_link_libraries(Runtime-RHI PRIVATE ${CMAKE_DL_LIBS})
endif()
set_property(TARGET Runtime-RHI PROPERTY FOLDER "Engine")

set(ROXELL_NVAPI_SDK_DIR "" CACHE PATH "NVIDIA NVAPI SDK root for D3D12 optional GPU features")
if(ROXELL_NVAPI_SDK_DIR AND ROXELL_RHI_BACKEND STREQUAL "D3D12")
    if(NOT WIN32 OR NOT EXISTS "${ROXELL_NVAPI_SDK_DIR}/nvapi.h" OR
        NOT EXISTS "${ROXELL_NVAPI_SDK_DIR}/amd64/nvapi64.lib")
        message(FATAL_ERROR "ROXELL_NVAPI_SDK_DIR requires the Windows x64 NVAPI SDK")
    endif()
    target_include_directories(Runtime-RHI PRIVATE "${ROXELL_NVAPI_SDK_DIR}")
    target_link_libraries(Runtime-RHI PRIVATE "${ROXELL_NVAPI_SDK_DIR}/amd64/nvapi64.lib")
    target_compile_definitions(Runtime-RHI PRIVATE ROXELL_RHI_WITH_NVAPI=1)
else()
    target_compile_definitions(Runtime-RHI PRIVATE ROXELL_RHI_WITH_NVAPI=0)
endif()
set(ROXELL_D3D12_PREVIEW_SDK_DIR "" CACHE PATH "Optional Agility SDK root exposing Direct3D linear algebra")
if(ROXELL_D3D12_PREVIEW_SDK_DIR AND ROXELL_RHI_BACKEND STREQUAL "D3D12")
    if(NOT WIN32 OR NOT EXISTS "${ROXELL_D3D12_PREVIEW_SDK_DIR}/build/native/include/d3d12.h")
        message(FATAL_ERROR "ROXELL_D3D12_PREVIEW_SDK_DIR requires the extracted Windows Agility preview SDK")
    endif()
    target_include_directories(Runtime-RHI BEFORE PRIVATE "${ROXELL_D3D12_PREVIEW_SDK_DIR}/build/native/include")
endif()

get_target_property(rhi_sources Runtime-RHI SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${rhi_sources})
