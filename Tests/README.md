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
