add_library(ThirdParty-VulkanHeaders INTERFACE)
target_include_directories(ThirdParty-VulkanHeaders SYSTEM INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
set_property(TARGET ThirdParty-VulkanHeaders PROPERTY FOLDER "External")
