# DirectXShaderCompiler

Version 1.9.2602.24 from [Microsoft DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2602.24).

CMake downloads the platform archive when ShaderCompiler or native GPU tests are enabled, verifies its pinned SHA-256 and extracts it in the build tree. Subsequent configurations reuse it. Original license files remain in the extracted package. The dependency supplies shader compilation and runtime libraries for offline compilation and GPU tests.

For an offline build, set `ROXELL_DXC_ROOT` to an already extracted package of this version. This explicit override checks required files but does not verify the archive hash. Disable `ROXELL_BUILD_SHADER_COMPILER` and GPU tests for a runtime-only build without DXC.
