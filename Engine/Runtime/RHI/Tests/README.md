# RHI tests

The suite uses Google Test assertions and subprocess death tests. Production RHI, the selected backend and Core memory resources are used directly. There is no mock GPU or alternate RHI implementation.

## Coverage

Case counts include parameter combinations: 40 backend-independent cases, 133 cases with the Null device tests, and 20 GPU cases per native backend.

| Area | Scenarios | Sources |
| --- | --- | --- |
| Formats and data | Metadata, copy/view compatibility, depth/stencil planes, compressed mip tails, overflow-safe ranges, integer/float clear limits, NaN, sampler limits, typed descriptor payloads | `RhiFormatTests.cpp`, `RhiValidationTests.cpp`, `RhiBindingTypesTests.cpp`, `RhiCommandTypesTests.cpp` |
| Device and resources | Creation allocation sweep, output ownership, unavailable backend/features, invalid buffer/texture descriptors, allocation retry, mapping conflicts, empty/foreign/stale handles | `DeviceTests.cpp`, `DeviceBufferTests.cpp`, `DeviceValidationTests.cpp`, `DeviceLifetimeTests.cpp` |
| Copies and staging | Exact bytes and guards, nonaligned sizes, caller-data snapshots, discard without execution, 1D/3D textures, mip/layer selection, padded rows, staging mappings and retained ownership | `DeviceBufferTests.cpp`, `DeviceTextureTests.cpp`, `DeviceTextureTransferTests.cpp` |
| Shader ownership | Shader stages, library retention, missing entry/stage, invalid shader queries, unsupported Null ray pipeline | `DeviceShaderTests.cpp` |
| State and queries | Initial/permanent state, conflicting recorded uses, invalid call order, event queries, generation reuse | `DeviceStateTests.cpp` |
| Preparation | Manifest capture, failed preparation retry, cancellation/discard, all eight reference selectors, phase order, access ranges, capacity, zero recording/submission allocations, cache reuse/eviction with live executions | `DevicePreparedTests.cpp`, `DeviceLifetimeTests.cpp`, `DeviceTextureTransferTests.cpp` |
| Submission | Busy retry, accepted failure, prepared failure before/after acceptance, retirement after drain, parallel recording/join, invalid tokens, out-of-order queue completion and reservation failure | `DeviceSubmissionTests.cpp`, `DevicePreparedTests.cpp`, `SubmissionCompletionTests.cpp` |
| GPU compute | Ordinary/prepared direct and indirect dispatch, identical expected integers, output guard, push constants, wrong push size and zero groups | `GpuComputeTests.cpp` |
| GPU raster | Ordinary/prepared draw and indexed draw, pixel readback, 16/32-bit indices, byte offsets, FirstIndex and negative BaseVertex | `GpuGraphicsTests.cpp` |
| GPU resources | Heap placement/lifetime, texture view retention, immutable binding entries, descriptor copy/clear/resize ownership, integer clear precision, depth-only clear and MSAA resolve | `GpuResourceTests.cpp` |
| GPU transfers | Ordinary/prepared bidirectional transfer, captured descriptions, direction ASSERT, padded rows, nonzero buffer offset and mip/layer selection | `DeviceTextureTransferTests.cpp` |
| Diagnostics | Invalid budgets and missing required callbacks | `RhiDiagnosticsTests.cpp` |

The common fixture resets failure injection resources before cleanup, drains the device and checks that tracked Core allocations are released. Prepared fixtures cancel an unfinished recording interval during cleanup. Death tests match assertion diagnostics rather than accepting any process crash. Windows error dialogs are disabled in the test entry point.

Native optional capabilities are checked before use. Missing device creation, compiler support or explicitly requested validation layers fail the test; they are not reported as successful skips.

## Build and execution

Use the repository's existing build profiles and trees. Configure Null for CPU device tests; D3D12/Vulkan builds include only the 40 backend-independent CPU cases in `Test-RHI`.

```powershell
cmake --build .build/windows-ninja --config Release --target Test-RHI
ctest --test-dir .build/windows-ninja -C Release -L unit -R '^Roxell\.RHI\.' --output-on-failure
```

For focused iteration, run the built executable with a Google Test filter such as `--gtest_filter=*Prepared*` or `--gtest_filter=*Buffer*`. CTest discovers individual cases, including parameter combinations, at test time. The cache eviction case intentionally exceeds the cache budget while keeping two executions alive.

Native tests require `ROXELL_BUILD_RHI_GPU_TESTS=ON` with a Windows D3D12/Vulkan backend and the repository DXC dependency. They run without an editor window.

```powershell
cmake --build .build/windows-ninja --config Release --target Test-RHIGpu
ctest --test-dir .build/windows-ninja -C Release -L gpu -R '^Roxell\.RHI\.Gpu\.' --output-on-failure
```

`ROXELL_RHI_GPU_TEST_VALIDATION=ON` requests the native validation layer. Use `OFF` only for a deliberately separate run without validation; that run does not satisfy the validation-layer gate. Configure `VK_LAYER_PATH` in the test process environment when using a local Vulkan layer installation.

For ASan, configure a separate compiler sanitizer build and run `Test-RHI` with the Null backend. Instrument GoogleTest with the same sanitizer settings as RHI and Core. The tests use the same source and contracts; sanitizer builds do not require a separate test framework.

## Verification limits

The suite is not a complete GPU conformance suite. Native ray tracing/AS, mesh/task shaders, sparse mapping, external-process interop, native callbacks, swapchain acquisition/presentation, optional vendor SDKs and shading-rate execution require additional hardware/platform workloads. The suite currently covers only a subset of their input/ownership rejection paths. Consumer integration tests belong to their owning modules.

Bistro frame timing and image quality remain acceptance benchmarks, not unit tests. Use repeated hardware runs with fixed inputs and explicit CPU/GPU p95 limits for performance acceptance.
