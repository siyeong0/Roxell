# Dependencies

- RHI uses [Vulkan-Headers](vulkan_headers/DEPENDENCY.md) for its Vulkan backend.
- ShaderCompiler uses [DXC](dxc/DEPENDENCY.md) and [Slang](slang/DEPENDENCY.md).
- Native RHI GPU tests also use DXC.

CMake downloads and hash-verifies the required compiler SDKs. Set
`ROXELL_DXC_ROOT` or `ROXELL_SLANG_ROOT` to use extracted offline packages.
Disable `ROXELL_BUILD_SHADER_COMPILER` to omit compiler SDKs when GPU tests are off.
