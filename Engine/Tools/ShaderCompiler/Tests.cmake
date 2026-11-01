set(shader_compiler_test_directory "${CMAKE_CURRENT_LIST_DIR}/Tests")

add_executable(Test-ShaderCompiler
    "${shader_compiler_test_directory}/ShaderCompilerTests.cpp"
    "${shader_compiler_test_directory}/ShaderModuleArtifactTests.cpp"
    "${shader_compiler_test_directory}/CapturedSourceCompiler.cpp"
    "${shader_compiler_test_directory}/CapturedSourceCompiler.h"
    "${shader_compiler_test_directory}/ShaderAdvancedTests.cpp"
    "${shader_compiler_test_directory}/ShaderBuildTests.cpp"
    "${shader_compiler_test_directory}/ShaderCompilerMetadataTests.cpp"
    "${shader_compiler_test_directory}/ShaderCompilerTest.h"
    "${shader_compiler_test_directory}/ShaderTestSources.h"
    "${shader_compiler_test_directory}/ShaderTestSources.cpp"
    "${shader_compiler_test_directory}/ShaderCompilerTestMain.cpp"
    "${shader_compiler_test_directory}/ShaderLocalRootTests.cpp"
    "${shader_compiler_test_directory}/ShaderMeshTests.cpp"
    "${shader_compiler_test_directory}/ShaderStageInterfaceTests.cpp"
)

if(WIN32)
    target_sources(Test-ShaderCompiler PRIVATE
        "${shader_compiler_test_directory}/DxcSpirvTests.cpp"
        "${shader_compiler_test_directory}/ShaderBuildServiceTests.cpp"
        "${shader_compiler_test_directory}/GatedShaderCompiler.cpp"
        "${shader_compiler_test_directory}/GatedShaderCompiler.h"
        "${shader_compiler_test_directory}/ShaderCompilerGate.cpp"
        "${shader_compiler_test_directory}/ShaderCompilerGate.h"
        "${shader_compiler_test_directory}/FailingThreadBackend.cpp"
        "${shader_compiler_test_directory}/FailingThreadBackend.h"
    )
    target_link_libraries(Test-ShaderCompiler PRIVATE Runtime-Platform)
endif()

target_link_libraries(Test-ShaderCompiler PRIVATE Tool-ShaderCompiler GTest::gtest)
target_compile_definitions(Test-ShaderCompiler PRIVATE
    SHADER_COMPILER_TEST_DATA="${shader_compiler_test_directory}/Data"
    SHADER_COMPILER_REPOSITORY="${PROJECT_SOURCE_DIR}"
)
target_include_directories(Test-ShaderCompiler PRIVATE "${CMAKE_CURRENT_LIST_DIR}/Private")
roxell_copy_dxc_runtime(Test-ShaderCompiler)
roxell_copy_slang_runtime(Test-ShaderCompiler)
set_property(TARGET Test-ShaderCompiler PROPERTY FOLDER "Tests/ShaderCompiler")

gtest_discover_tests(Test-ShaderCompiler
    TEST_PREFIX "Roxell.ShaderCompiler."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 120 LABELS "shader-compiler"
)

add_executable(Build-ShaderTestData EXCLUDE_FROM_ALL "${shader_compiler_test_directory}/GenerateShaderTestData.cpp")
target_link_libraries(Build-ShaderTestData PRIVATE Tool-ShaderCompiler)
roxell_copy_slang_runtime(Build-ShaderTestData)
roxell_copy_dxc_runtime(Build-ShaderTestData)
set_property(TARGET Build-ShaderTestData PROPERTY FOLDER "Tests/ShaderCompiler")
