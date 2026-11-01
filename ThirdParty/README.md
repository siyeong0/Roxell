
RHI uses vendored [Vulkan-Headers](vulkan_headers/DEPENDENCY.md) for the Vulkan backend. [DXC](dxc/DEPENDENCY.md) is downloaded and hash-verified by CMake only for native GPU tests. Neither is required by the default Null configuration.
