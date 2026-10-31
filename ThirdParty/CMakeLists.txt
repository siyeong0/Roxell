add_library(ThirdParty-NlohmannJson INTERFACE)
target_include_directories(ThirdParty-NlohmannJson SYSTEM INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json/include>
)
