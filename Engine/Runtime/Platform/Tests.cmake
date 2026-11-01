if(WIN32)
    set(platform_test_directory "${CMAKE_CURRENT_LIST_DIR}/Tests")
    add_library(Test-PlatformLibrary SHARED "${platform_test_directory}/Windows/WindowsDynamicLibrary_fixture.cpp")
    target_link_libraries(Test-PlatformLibrary PRIVATE Build-Settings)
    add_library(Test-PlatformLibraryDependency SHARED "${platform_test_directory}/Windows/WindowsDynamicLibrary_dependencyFixture.cpp")
    target_link_libraries(Test-PlatformLibraryDependency PRIVATE Build-Settings)
    add_library(Test-PlatformLibraryDependent SHARED "${platform_test_directory}/Windows/WindowsDynamicLibrary_dependentFixture.cpp")
    target_link_libraries(Test-PlatformLibraryDependent PRIVATE Build-Settings Test-PlatformLibraryDependency)
    add_executable(Test-ConsoleCancellationChild "${platform_test_directory}/Windows/ConsoleCancellationTestChild.cpp")
    target_link_libraries(Test-ConsoleCancellationChild PRIVATE Runtime-Platform)
    add_executable(Test-PlatformProcessChild "${platform_test_directory}/Windows/ProcessTestChild.cpp")
    target_link_libraries(Test-PlatformProcessChild PRIVATE Runtime-Platform)
    add_executable(Test-PlatformTerminalFixture "${platform_test_directory}/Windows/WindowsTerminalFixture.cpp")
    target_link_libraries(Test-PlatformTerminalFixture PRIVATE Build-Settings)
    add_executable(Test-Platform
        "${platform_test_directory}/AccessibilityTests.cpp"
        "${platform_test_directory}/Accessibility_geometryTests.cpp"
        "${platform_test_directory}/Windows/WindowsAccessibilityTests.cpp"
        "${platform_test_directory}/Windows/WindowsAccessibility_geometryTests.cpp"
        "${platform_test_directory}/ClockTests.cpp"
        "${platform_test_directory}/DynamicLibraryOwnerTests.cpp"
        "${platform_test_directory}/GamepadSourceTests.cpp"
        "${platform_test_directory}/StandardStreamsTests.cpp"
        "${platform_test_directory}/Windows/WindowsClockTests.cpp"
        "${platform_test_directory}/Windows/WindowsComApartmentTests.cpp"
        "${platform_test_directory}/Windows/WindowsConsoleCancellationTests.cpp"
        "${platform_test_directory}/Windows/WindowsConsoleCancellation_signalTests.cpp"
        "${platform_test_directory}/Windows/WindowsDynamicLibraryTests.cpp"
        "${platform_test_directory}/Windows/WindowsDynamicLibrary_dependencyTests.cpp"
        "${platform_test_directory}/Windows/WindowsExecutionEnvironmentTests.cpp"
        "${platform_test_directory}/Windows/WindowsExecutionEnvironment_roundingTests.cpp"
        "${platform_test_directory}/Windows/WindowsFailureDiagnosticsTests.cpp"
        "${platform_test_directory}/Windows/WindowsFileSystemTests.cpp"
        "${platform_test_directory}/Windows/WindowsFileTailReaderTests.cpp"
        "${platform_test_directory}/Windows/WindowsFileWatcherTests.cpp"
        "${platform_test_directory}/Windows/WindowsFileWatcher_changesTests.cpp"
        "${platform_test_directory}/Windows/WindowsFileWatcher_handleTests.cpp"
        "${platform_test_directory}/Windows/WindowsGamepadSourceTests.cpp"
        "${platform_test_directory}/Windows/WindowsLogSinkTests.cpp"
        "${platform_test_directory}/Windows/WindowsPhysicalKeyTests.cpp"
        "${platform_test_directory}/Windows/WindowsProcessServiceTests.cpp"
        "${platform_test_directory}/Windows/WindowsProcessService_argumentsTests.cpp"
        "${platform_test_directory}/Windows/WindowsProcessService_lifecycleTests.cpp"
        "${platform_test_directory}/Windows/WindowsProcessService_imageTests.cpp"
        "${platform_test_directory}/Windows/WindowsProcessService_handleTests.cpp"
        "${platform_test_directory}/Windows/WindowsRandomTests.cpp"
        "${platform_test_directory}/Windows/WindowsStandardStreamsTests.cpp"
        "${platform_test_directory}/Windows/WindowsStandardStreams_nativeTests.cpp"
        "${platform_test_directory}/Windows/WindowsTerminalSessionTests.cpp"
        "${platform_test_directory}/Windows/WindowsTerminalSession_failureTests.cpp"
        "${platform_test_directory}/Windows/WindowsTerminalSession_ioTests.cpp"
        "${platform_test_directory}/Windows/WindowsThreadBackendTests.cpp"
        "${platform_test_directory}/Windows/WindowsWaitTimerTests.cpp"
        "${platform_test_directory}/Windows/WindowsWindowSystemTests.cpp"
        "${platform_test_directory}/Windows/WindowsWindowTextDocumentTests.cpp"
    )
    target_link_libraries(Test-Platform PRIVATE Runtime-Platform GTest::gtest_main)
    add_dependencies(Test-Platform Test-ConsoleCancellationChild Test-PlatformProcessChild Test-PlatformTerminalFixture Test-PlatformLibrary Test-PlatformLibraryDependency Test-PlatformLibraryDependent)
    target_compile_definitions(Test-Platform PRIVATE
        ROXELL_TEST_CACHE="${PROJECT_SOURCE_DIR}/.cache"
        ROXELL_TEST_MODULE_C="$<TARGET_FILE:Test-PlatformLibrary>"
        ROXELL_TEST_MODULE_CPP="$<TARGET_FILE:Test-PlatformLibraryDependency>"
        ROXELL_TEST_MODULE_DEPENDENT="$<TARGET_FILE:Test-PlatformLibraryDependent>"
    )
    set_property(TARGET Test-Platform PROPERTY FOLDER "Tests/Platform")
    set_property(TARGET Test-ConsoleCancellationChild PROPERTY FOLDER "Tests/Platform")
    set_property(TARGET Test-PlatformProcessChild PROPERTY FOLDER "Tests/Platform")
    set_property(TARGET Test-PlatformTerminalFixture PROPERTY FOLDER "Tests/Platform")
    gtest_discover_tests(Test-Platform
        TEST_PREFIX "Roxell.Platform."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 120 RESOURCE_LOCK "roxell-desktop-input"
    )
endif()
