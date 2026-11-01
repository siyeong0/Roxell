# Shader test data

These fixtures are compiled from the adjacent HLSL files through the real ShaderBuild Slang adapter. Each asset contains DXIL (shader model 6.6) and SPIR-V (1.6, Vulkan 1.3) variants. Requests use HLSL and the default compiler options; Vulkan binding attributes prevent register-class collisions. Compiled files are inputs to CPU-only tests, not generated during test execution.

- Compute: two entries, a constant buffer, read-only input and writable output. The authored Simulate kernel has default and FAST choices.
- Surface: vertex/pixel linkage, sampled texture and sampler; includes an authored Surface pass and Albedo property mappings for both targets.
- Parameters: scalar/vector, fixed array, row-major matrix and nested constant-buffer records. Both targets have a 112-byte Settings layout. Independent packing expectations live in ShaderBufferWriterTests.cpp.

Regenerate explicitly from an x64 developer shell using a build configured with ShaderBuild and contract tests enabled:

```powershell
cmake --build .build/rhi-direct-call --config Release --target Build-ShaderTestData
.build/rhi-direct-call/Tests/Release/Build-ShaderTestData.exe Engine/Runtime/Shader/Tests/Data
```

The generator links Tool-ShaderBuild and its compiler runtimes. Test-Shader links only Runtime-Shader and GoogleTest. Review source, reflection, golden bytes and hashes together when regenerating. Never regenerate fixtures as part of CTest or to accept an unexplained test failure. Fixture validity does not establish native GPU execution correctness.

## Recorded inputs

- DirectXShaderCompiler: 1.9.2602.24
- Slang: 2026.18

| File | SHA-256 |
| --- | --- |
| `Sources/Compute.hlsl` | `4d98ee06c7b2f45f2319e7c50a803589c8a1057746a54824e152d63d5723e782` |
| `Sources/Parameters.hlsl` | `4a85af014db0aeb36d07911665146ce33d47dc0e90ad4e9de48ec2b2396f5a81` |
| `Sources/Surface.hlsl` | `604cab9138fefdb6f7576d310fcd7888185dcbdaa3810bb1d7b845f565405e7a` |
| `Compiled/Compute.shader` | `76a72c860667c5b38b9b399e1589b542483e0ada108e2c50f19314abbcea78b6` |
| `Compiled/Parameters.shader` | `8fba887c2981a65b1b232164c11164f65da4116da39262e9153bc4efd47e8c63` |
| `Compiled/Surface.shader` | `a4c527c48ca56c7c5cfde222e48258b0f861959420861149b27fcdd24b4239a0` |
