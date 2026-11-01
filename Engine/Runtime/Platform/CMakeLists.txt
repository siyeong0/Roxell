set(ROXELL_PLATFORM_SOURCES
    Private/Accessibility.cpp
    Private/AccessibilityInternal.h
    Private/DynamicLibraryOwner.cpp
    Private/FileSystem.cpp
    Private/PlatformThreadBackend.cpp
    Private/StandardStreams.cpp
    Public/Accessibility.h
    Public/DynamicLibraryOwner.h
    Public/FileSystem.h
    Public/IClock.h
    Public/IFileTailReader.h
    Public/IFileWatcher.h
    Public/IGamepadSource.h
    Public/IProcessService.h
    Public/ITerminalSession.h
    Public/IWindowSystem.h
    Public/NativeWindowHandle.h
    Public/PlatformThreadBackend.h
    Public/StandardStreams.h
    Public/WindowTextDocument.h
    Public/WindowTypes.h
)

if(WIN32)
    list(APPEND ROXELL_PLATFORM_SOURCES
        Private/Windows/WindowsAccessibility.cpp
        Private/Windows/WindowsAccessibility.h
        Private/Windows/WindowsClipboardMemory.cpp
        Private/Windows/WindowsClipboardMemory.h
        Private/Windows/WindowsClock.cpp
        Private/Windows/WindowsComApartment.cpp
        Private/Windows/WindowsConsoleCancellation.cpp
        Private/Windows/WindowsDynamicLibrary.cpp
        Private/Windows/WindowsExecutionEnvironment.cpp
        Private/Windows/WindowsFailureDiagnostics.cpp
        Private/Windows/WindowsFileSystem.cpp
        Private/Windows/WindowsFileTailReader.cpp
        Private/Windows/WindowsFileWatcher.cpp
        Private/Windows/WindowsGamepadSource.cpp
        Private/Windows/WindowsLogSink.cpp
        Private/Windows/WindowsPath.cpp
        Private/Windows/WindowsPath.h
        Private/Windows/WindowsPhysicalKeyTable.cpp
        Private/Windows/WindowsPhysicalKeyTable.h
        Private/Windows/WindowsProcessService.cpp
        Private/Windows/WindowsRandom.cpp
        Private/Windows/WindowsStandardStreams.cpp
        Private/Windows/WindowsTerminalSession.cpp
        Private/Windows/WindowsTextInput.cpp
        Private/Windows/WindowsTextInput.h
        Private/Windows/WindowsThreadBackend.cpp
        Private/Windows/WindowsWaitTimer.cpp
        Private/Windows/WindowsWindowSystem.cpp
        Public/Windows/WindowsClock.h
        Public/Windows/WindowsComApartment.h
        Public/Windows/WindowsConsoleCancellation.h
        Public/Windows/WindowsDynamicLibrary.h
        Public/Windows/WindowsExecutionEnvironment.h
        Public/Windows/WindowsFailureDiagnostics.h
        Public/Windows/WindowsFileSystem.h
        Public/Windows/WindowsFileTailReader.h
        Public/Windows/WindowsFileWatcher.h
        Public/Windows/WindowsGamepadSource.h
        Public/Windows/WindowsLogSink.h
        Public/Windows/WindowsProcessService.h
        Public/Windows/WindowsRandom.h
        Public/Windows/WindowsStandardStreams.h
        Public/Windows/WindowsTerminalSession.h
        Public/Windows/WindowsThreadBackend.h
        Public/Windows/WindowsWaitTimer.h
        Public/Windows/WindowsWindowSystem.h
    )
endif()

add_library(Runtime-Platform STATIC ${ROXELL_PLATFORM_SOURCES})
target_include_directories(Runtime-Platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>
)
target_link_libraries(Runtime-Platform PUBLIC Build-Settings Runtime-Core)

if(WIN32)
    target_include_directories(Runtime-Platform PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public/Windows>
    )
    target_link_libraries(Runtime-Platform PRIVATE
        bcrypt
        dbghelp
        icu
        imm32
        ole32
        oleaut32
        psapi
        uiautomationcore
        user32
        uuid
        xinput
    )
endif()

set_property(TARGET Runtime-Platform PROPERTY FOLDER "Engine")
if(COMMAND roxell_configure_ide_directory)
    roxell_configure_ide_directory()
endif()
