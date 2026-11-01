set(shader_test_directory "${CMAKE_CURRENT_LIST_DIR}/Tests")

add_executable(Test-Shader
    "${shader_test_directory}/ShaderTestMain.cpp"
    "${shader_test_directory}/ShaderTestData.h"
    "${shader_test_directory}/ShaderTests.cpp"
    "${shader_test_directory}/ShaderBuilderTests.cpp"
    "${shader_test_directory}/ShaderSelectionTests.cpp"
    "${shader_test_directory}/ShaderOptionsTests.cpp"
    "${shader_test_directory}/ShaderLayoutTests.cpp"
    "${shader_test_directory}/ShaderBufferWriterTests.cpp"
    "${shader_test_directory}/ShaderSerializationTests.cpp"
)
target_link_libraries(Test-Shader PRIVATE Runtime-Shader GTest::gtest)
target_compile_definitions(Test-Shader PRIVATE SHADER_TEST_DATA="${shader_test_directory}/Data")
set_property(TARGET Test-Shader PROPERTY FOLDER "Tests/Shader")
gtest_discover_tests(Test-Shader
    TEST_PREFIX "Roxell.Shader."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 30 LABELS "shader-unit"
)
