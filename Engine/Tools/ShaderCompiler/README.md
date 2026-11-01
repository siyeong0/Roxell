# ShaderCompiler

Compiles HLSL and Slang into [Shader](../../Runtime/Shader/README.md) assets. Link `Tool-ShaderCompiler`; the public API uses `rox::shader` and depends on Core and Shader. DXC and Slang stay private to the tool.

## Features

- DXIL and SPIR-V compilation with binding, buffer layout, stage interface and workgroup reflection.
- Captured source/include dependencies and reproducible build identities for caching.
- Slang module compilation, linking and entry specialization.
- Asynchronous builds with deduplication, priorities, cancellation and memory limits.

`IShaderCompiler::GetProfiles()` lists the supported language, target and stage combinations. Support differs between DXC and Slang; unsupported combinations return a failure.

## API

| Task | Entry point |
| --- | --- |
| Create a compiler and compile sources | [CreateShaderCompiler, IShaderCompiler](Public/ShaderCompiler.h) |
| Configure sources, entry points and targets; read diagnostics | [ShaderCompileRequest, ShaderBuildReport](Public/ShaderCompilerTypes.h) |
| Capture inputs and validate cached builds | [ShaderBuildPlan](Public/ShaderBuildPlan.h) |
| Compile and link reusable modules | [ShaderModuleArtifact, ShaderLinkRequest](Public/ShaderModuleArtifact.h) |
| Schedule builds and drain workers | [ShaderBuildService](Public/ShaderBuildService.h) |
| Poll, cancel and retain results | [ShaderBuildJob, ShaderBuildResult](Public/ShaderBuildJob.h) |

## Usage

Create the compiler with `CreateShaderCompiler(EShaderCompiler::Slang, memory)` or `EShaderCompiler::Dxc` and check the returned `Result`. Its value owns the compiler.

With an initialized `IShaderCompiler& compiler`, a `ShaderCompileRequest request` and an `IShaderSourceResolver& sources`:

```cpp
ShaderBuildReport report(memory);
Result<ShaderBuildOutput> result = compiler.Compile(request, sources, report, memory);
if (!result)
{
    return Result<void>::Failure(result.GetError());
}

Shader shader = result.Value().Artifact;
```

`request` selects the root source, entry points and target profiles. `sources` resolves includes; `report.Diagnostics` contains compiler messages. The output contains the Shader asset, dependencies and build identity.

SDK headers and runtime libraries contribute to `ShaderToolchainIdentity::SdkIdentity`, so changing the compiler SDK changes the build identity.

For repeated builds, call `PrepareBuild` once and pass the resulting plan to `Compile`. The plan owns the captured source text. Use `HasSameInputs`, or `EncodeInputs` and `VerifyInputs`, to validate cache inputs; the build hash alone is not an equality check.

## Compilation failures

Check the returned `Result` before accessing the output. On failure, `result.GetError()` provides the error code and message. Source errors, missing includes, unsupported targets, allocation failures and cancellation all use this path.

Use `report.Diagnostics` to show compiler errors and warnings. Each entry contains a severity and message, with `SourceIdentity`, `Line` and `Column` when a source location is available. Diagnostics may be empty when compilation cannot start, so always include the returned error in the failure message. Successful builds can also produce warnings.

`report.bTruncated` means some diagnostics were omitted; `report.bAllocationFailure` means diagnostic storage allocation failed. Do not treat an incomplete report as the full compiler output.

## Tests

```powershell
cmake --build .build/windows-ninja --config Release --target Test-ShaderCompiler
ctest --test-dir .build/windows-ninja -C Release -R '^Roxell\.ShaderCompiler\.' --output-on-failure
```

GoogleTest covers real shader files, reflection, captured inputs, module linking, asynchronous builds, allocation failures, lifetime and ASSERT contracts. GPU execution is checked by the separate rendering integration tests.
