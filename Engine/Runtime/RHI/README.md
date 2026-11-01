# RHI

RHI provides GPU resources, command recording, submission and lifetime validation. Link `Runtime-RHI`; its public dependency is Core. Public headers contain no graphics SDK types.

## Structure

RHI follows a Reverse OOP design: the public `Device` defines concrete operations, while private backend implementations provide the native GPU behavior. The build selects one backend and links its implementation directly; backends do not inherit a public device interface, and RHI calls use neither virtual dispatch nor a runtime function-pointer table. `Device` owns validation, preparation and resource lifetimes; its internal state stores backend data and synchronization.

| Area | Responsibility |
| --- | --- |
| `Public/Rhi.h`, `Public/Device.h` | Device creation and concrete, nonvirtual operations |
| Public type headers | Resources, bindings, pipelines, commands, preparation and presentation |
| `Private/Device_*.cpp` | Validation, recording, caching and lifetime management |
| `Private/DeviceState.h` | Internal device storage and synchronization |
| `Private/D3D12`, `Private/Vulkan`, `Private/Null` | Directly linked backend implementations |
| `Tests`, `Tests.cmake` | Google Test CPU unit tests and Null device contracts |

`ROXELL_RHI_BACKEND=D3D12|Vulkan|Null` selects one backend at configure time. Requesting an uncompiled backend returns `Unsupported`. D3D12 requires Windows; Vulkan uses Vulkan-Headers and the native loader. Optional NVAPI, Aftermath and Agility SDK paths are configured in [CMakeLists.txt](CMakeLists.txt).

## Usage and ownership

Include `Rhi.h` for device creation and operations. Include the relevant type headers for declarations. Implementation files use `using namespace core;` inside their implementation namespace; headers retain explicit `core::` names.

```cpp
using namespace rox;

core::UniquePtr<rhi::Device> device;
const core::Result<void> created = rhi::CreateDevice(backend, memoryResource, &device);
if (!created)
{
    return created;
}
```

The output owner must be empty. Its memory resource outlives the device and returned owners. Drain submitted GPU work before releasing the device.

Ordinary and prepared commands share execution arguments and names. Prepared overloads accept typed references; preparation retains resources until execution retirement or discard. Ruda owns preparation/submission lifetime and RenderGraph owns execution planning.

Caller contract violations assert: wrong owner or generation, invalid ranges, incompatible bindings and call order. Optional empty handles and normal query misses remain valid where documented. Allocation failures, unsupported features and operational GPU errors remain fallible. A recording failure leaves the list owned by the caller for explicit discard.

`SubmitOutcome` distinguishes `Busy` (input retained), `Submitted` (input consumed with a token) and `Failed` (input consumed; uncertain native work retained internally). Device destruction does not implicitly wait for the GPU.

## Tests

[Tests.cmake](Tests.cmake) registers Google Test executables with `PRE_TEST` discovery. Building them does not execute tests.

- `Test-RHI`: 40 backend-independent cases; 133 cases including device tests in a Null configuration. Coverage includes resources, transfers, shader ownership, preparation, cache eviction, recording, submission, allocation failure and ASSERT contracts.
- `Test-RHIGpu`: 20 native cases per selected backend, enabled with `ROXELL_BUILD_RHI_GPU_TESTS=ON`. Requires Windows, D3D12 or Vulkan, and DXC. Tests compare readback values for ordinary/prepared compute, indirect dispatch, draw/indexed draw, texture transfers, integer clears, depth-only rendering and MSAA resolve; resource tests check descriptor, view, heap and external ownership.

GPU tests serialize through the `roxell-gpu` CTest resource lock. Validation is required by default (`ROXELL_RHI_GPU_TEST_VALIDATION=ON`); unavailable optional GPU capabilities are explicitly skipped. Null tests can also run in an AddressSanitizer build.

See [test coverage and execution](Tests/README.md) for configuration, filters and limits.

## Native dependencies

The default backend is Null. Select a native backend with `-DROXELL_RHI_BACKEND=D3D12` or `-DROXELL_RHI_BACKEND=Vulkan`. Vulkan headers and their licenses are included in `ThirdParty/vulkan_headers`.

With `BUILD_TESTING=ON` and `ROXELL_BUILD_RHI_GPU_TESTS=ON`, CMake downloads the pinned DXC package, verifies its SHA-256 and reuses it from the build tree. For offline builds, set `ROXELL_DXC_ROOT` to an extracted DXC 1.9.2602.24 package.

Set `BUILD_TESTING=OFF` to omit tests. Runtime-only and Null builds do not require DXC.
