# Tests

Core unit tests use GoogleTest. Test sources live in each Core component's
`Tests` directory, and their targets are defined in `Engine/Runtime/Core/Tests.cmake`.

From an x64 Developer PowerShell, configure and run the tests:

```powershell
cmake -S . -B .build/windows-ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build .build/windows-ninja
ctest --test-dir .build/windows-ninja --output-on-failure
```

To run one component, build its target and filter the test names:

```powershell
cmake --build .build/windows-ninja --target Test-CoreMath
ctest --test-dir .build/windows-ninja -R '^Roxell\.Core\.Math\.' --output-on-failure
```

Set `BUILD_TESTING=OFF` when configuring to omit test targets.

## Platform unit tests

Platform unit tests use GoogleTest and are defined in
`Engine/Runtime/Platform/Tests.cmake`. They exercise Windows services using
independent OS fixtures, including failure, cancellation, and resource cleanup.

```powershell
cmake --build .build/windows-ninja --target Test-Platform
ctest --test-dir .build/windows-ninja -R '^Roxell\.Platform\.' --output-on-failure
```

Desktop input, clipboard, and symbolic-link cases may skip when their required
Windows environment is unavailable. A skipped case is not verified.

## Platform API test coverage inventory

This inventory maps each Platform public header to behavior asserted by the new
GoogleTest sources. A source entry indicates an assertion exists, not that the test
passed on every machine. Device, desktop, clipboard and link-dependent cases can be
skipped; check the actual test results. The table also identifies remaining
behavior not covered by the registered Platform suite.

| Public header | New GoogleTest coverage and remaining gaps |
| --- | --- |
| `Accessibility.h` | `AccessibilityTests.cpp` and `Accessibility_geometryTests.cpp` check snapshot lookup, duplicate IDs and geometry ownership. `WindowsAccessibilityTests.cpp` and `WindowsAccessibility_geometryTests.cpp` exercise native UI Automation actions and text geometry. |
| `DynamicLibraryOwner.h` | `WindowsDynamicLibraryTests.cpp` checks symbol lookup, missing exports, move, close, repeated close and allocation cleanup. `DynamicLibraryOwnerTests.cpp` checks adopted callback ownership, failed close/retry and invalid symbol names. |
| `FileSystem.h` | `WindowsFileSystemTests.cpp` checks existence, both read overloads, limits, allocation failure, atomic create/replace, blocked replacement preservation and cleanup, removal, lock conflicts, moves and ownership. The conflict, post-release child acquisition and selected error message/hint contracts are asserted. |
| `IClock.h` | `ClockTests.cpp` checks value comparisons; `WindowsClockTests.cpp` checks both clock operations and concurrent monotonicity. |
| `IFileTailReader.h` | `WindowsFileTailReaderTests.cpp` checks read, backfill, pending reads, identity probes, truncation, replacement and retirement. |
| `IFileWatcher.h` | `WindowsFileWatcherTests.cpp` and `WindowsFileWatcher_changesTests.cpp` cover changes, rename, overflow/rescan, invalid roots, ownership and pending-watch destruction. |
| `IGamepadSource.h` | `GamepadSourceTests.cpp` checks button and axis validity; `WindowsGamepadSourceTests.cpp` checks polling and move. Actual controller state transitions require a device and are not verified. |
| `IProcessService.h` | `WindowsProcessServiceTests.cpp`, `WindowsProcessService_argumentsTests.cpp` and `WindowsProcessService_lifecycleTests.cpp` cover launch, arguments, output limits, cancellation, termination, child tree cleanup, allocation failure, wait, release and move. |
| `ITerminalSession.h` | `WindowsTerminalSessionTests.cpp`, `WindowsTerminalSession_failureTests.cpp` and `WindowsTerminalSession_ioTests.cpp` cover child output and exit, creation bounds, input, resize, backfill, allocation failure and close with queued I/O. Additional terminal parser and interactive device cases remain unverified. |
| `IWindowSystem.h` | `WindowsWindowSystemTests.cpp` checks window lifetime, visibility, resizing, pointer ownership, native input/events, text input and clipboard. Some input paths and actual desktop availability remain environment-dependent. Clipboard coverage may skip to preserve existing clipboard contents. |
| `NativeWindowHandle.h` | Window tests obtain a native Windows handle. X11/Wayland constructors and validity boundaries lack new behavior tests. |
| `PlatformThreadBackend.h` | `WindowsThreadBackendTests.cpp` checks native start, join, identity, synchronization, handle release and invalid-entry, empty-token and self-join assertions. The platform selector and POSIX backend are not exercised by these cases. |
| `StandardStreams.h` | `WindowsStandardStreamsTests.cpp` and `WindowsStandardStreams_nativeTests.cpp` check redirected input/output, native handle types, overlapped pipes, read failure, bounds, allocation failure, cancellation, EOF, ownership and pending-I/O shutdown. |
| `WindowTextDocument.h` | `WindowsWindowTextDocumentTests.cpp` covers UTF-8 selection boundaries and allocation failure; window and UI Automation tests cover publication, composition and text geometry. Native text edits and additional selection interactions remain unverified. |
| `WindowTypes.h` | Window tests cover IDs, extents, events, pointer mode and text; `WindowsPhysicalKeyTests.cpp` checks physical-key mapping. Enum validity and event variants are not exhaustively covered. |
| `Windows/WindowsClock.h` | `WindowsClockTests.cpp` checks creation, monotonicity, concurrent reads and UTC epoch units. |
| `Windows/WindowsComApartment.h` | `WindowsComApartmentTests.cpp` checks nested owners and conflicting apartment initialization/retry. |
| `Windows/WindowsConsoleCancellation.h` | `WindowsConsoleCancellationTests.cpp` checks invalid registration, conflicting subscriptions, cleanup and reacquisition. `WindowsConsoleCancellation_signalTests.cpp` checks CTRL_BREAK delivery in an isolated console. |
| `Windows/WindowsDynamicLibrary.h` | `WindowsDynamicLibraryTests.cpp` and `WindowsDynamicLibrary_dependencyTests.cpp` check system and dedicated DLL loading, missing symbols, Unicode paths, dependency search, unloading and allocation failure. |
| `Windows/WindowsExecutionEnvironment.h` | `WindowsExecutionEnvironmentTests.cpp`, `WindowsExecutionEnvironment_roundingTests.cpp` and process child tests check stable collection, floating-point control state, rounding and cross-process fingerprints. |
| `Windows/WindowsFailureDiagnostics.h` | `WindowsFailureDiagnosticsTests.cpp` checks concurrent captures, bounded dumps, disabled capture, unwritable dump reporting and result diagnostic identity. |
| `Windows/WindowsFileSystem.h` | `WindowsFileSystemTests.cpp` checks standalone file read/write/stamp, containment, open/create, inventory limits, cancellation, link refusal and allocation cleanup. Link tests may skip when Windows cannot create symbolic links. |
| `Windows/WindowsFileTailReader.h` | `WindowsFileTailReaderTests.cpp` checks creation and failure, reading, backfill, pending I/O, identity transitions, move and handle release. |
| `Windows/WindowsFileWatcher.h` | `WindowsFileWatcherTests.cpp`, `WindowsFileWatcher_changesTests.cpp` and `WindowsFileWatcher_handleTests.cpp` check creation, allocation failure, changes, overflow/rescan, moves and pending watch retirement. |
| `Windows/WindowsGamepadSource.h` | `WindowsGamepadSourceTests.cpp` checks repeated slot polling and move; physical controller behavior remains device-dependent. |
| `Windows/WindowsLogSink.h` | `WindowsLogSinkTests.cpp` checks open, append, flush, close, path validation and link rejection; forced write failure remains untested. |
| `Windows/WindowsProcessService.h` | `WindowsProcessServiceTests.cpp`, `WindowsProcessService_argumentsTests.cpp`, `WindowsProcessService_lifecycleTests.cpp` and `WindowsProcessService_imageTests.cpp` cover creation, arguments, hash, output limits, cancellation, termination, release and child cleanup. |
| `Windows/WindowsRandom.h` | `WindowsRandomTests.cpp` checks empty requests and subspan boundary preservation; provider failure is not injected. |
| `Windows/WindowsStandardStreams.h` | `WindowsStandardStreamsTests.cpp` and `WindowsStandardStreams_nativeTests.cpp` exercise redirected and overlapped pipes, unsupported handles, cancellation, native failures, allocation failure, ownership and shutdown. |
| `Windows/WindowsTerminalSession.h` | `WindowsTerminalSessionTests.cpp`, `WindowsTerminalSession_failureTests.cpp` and `WindowsTerminalSession_ioTests.cpp` cover creation failures, Unicode widths, child input/output, resize and pending close; additional parser and interactive device cases remain unverified. |
| `Windows/WindowsThreadBackend.h` | `WindowsThreadBackendTests.cpp` checks start, join, identity, native handle retirement and fatal ownership boundaries. Native start-failure injection is not covered. |
| `Windows/WindowsWaitTimer.h` | `WindowsWaitTimerTests.cpp` checks zero/repeated waits and moved ownership. Native timer creation/wait failure is not injected. |
| `Windows/WindowsWindowSystem.h` | `WindowsWindowSystemTests.cpp` checks creation, allocation failures, lifetime, window state, events, pointer and text/clipboard behavior. Some input paths and desktop-dependent cases remain unverified. |

## RHI tests

RHI uses GoogleTest for common types, Null device contracts and optional native GPU readback tests. See [RHI test coverage and execution](../Engine/Runtime/RHI/Tests/README.md).

## ShaderCompiler tests

ShaderCompiler uses real HLSL and Slang files to test compilation, reflection,
module linking, specialization, asynchronous scheduling and resource lifetime.

```powershell
cmake --build .build/windows-ninja --target Test-ShaderCompiler
ctest --test-dir .build/windows-ninja -R '^Roxell\.ShaderCompiler\.' --output-on-failure
```
