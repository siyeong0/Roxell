# Shader

Shader turns compiled shader assets into validated graphics or compute selections: code, resource layouts, pipeline constants and device requirements. It also packs CPU values into reflected buffer layouts. Link `Runtime-Shader`; its only dependency is Core.

## Core features and APIs

| Feature | Main APIs | When to use |
| --- | --- | --- |
| Immutable assets | [`Shader`](Public/Shader.h): `Load`, `Save`, `GetIdentity`, `GetEntryPoints`, `GetVariants` | Load validated compiled assets, inspect their code/metadata and serialize them for storage. |
| Asset authoring | [`ShaderBuilder`](Public/ShaderBuilder.h): `FromShader`, `AddProperty`, `AddPass`, `AddKernel`, `AddPropertyBinding`, `Validate`, `Build` | Combine compiled records with explicit properties and graphics/compute recipes. |
| Pass/kernel selection | `Shader::SelectPass`, `SelectKernel`; [`ShaderOptions`](Public/ShaderOptions.h): `SetKeyword`, `SetPipelineConstant` | Select an exact target and keyword combination, validate linkage and resolve pipeline constants. |
| Material properties | [`ShaderProperty`](Public/ShaderProperty.h), `Shader::FindProperty`, `GetProperty`, `ShaderLayout::FindPropertyBindings` | Define typed defaults and ranges, then locate each property's resource binding or numeric field in selected code. |
| Program interfaces | [`ShaderLayout`](Public/ShaderLayout.h): `Create`, `GetBindings`, `GetPushConstants`, `GetFeatureRequirements` | Compose entry selections directly or inspect the layout produced by pass/kernel selection. |
| Compatibility and caching | `CompareShaderLayouts`, `GetPhysicalLayoutHash`, `GetLayoutHash`, `GetSelectedCodeHash` | Distinguish binding/layout changes from code changes and choose appropriate cache keys. |
| CPU buffer packing | [`ShaderBufferWriter`](Public/ShaderBufferWriter.h): `WriteValues`, `Create`, `WriteStruct` | Pack numeric values or reusable host structures into the shader's reflected memory layout. |

A pass describes graphics stages and render state; a kernel describes a compute entry. Their selections retain code, layouts, resolved constants and device requirements. Graphics attachment formats, vertex input and sample counts remain caller inputs. `ShaderPipelineConstants::Resolve` is also available when composing a layout directly. Selection never compiles code or issues GPU commands.

Properties are authored explicitly rather than exposing every reflected field. Numeric defaults support float, int, bool, vector, color and matrix values; resource properties have empty defaults. Each public property needs a physical mapping, but a particular selection may omit it. Property handles belong to the complete asset identity.

Layouts represent resource bindings, stage I/O, push constants, specialization, Mesh/Task payloads and Ray/local-record contracts. Structural compatibility can be `Identical`, `BindingCompatible`, `RepackRequired` or `Incompatible`; it does not establish semantic equivalence or authorize GPU resource reuse. Hashes are cache keys, not substitutes for compatibility checks or native device admission.

`WriteValues` handles complete top-level numeric values with temporary storage. For arrays, nested records, scalar64 or matrix layout conversion, create a packing plan from an explicit host layout and call `WriteStruct`. Reusing the plan performs no allocation or name lookup. Failed writes preserve the destination; successful writes zero padding and preserve bytes beyond `GetDestinationByteSize()`.

## Usage

Load an asset with `Shader::Load`, then select an authored pass or kernel for an exact target and keyword combination. This example uses a loaded `Shader` named `asset` with a `Simulate` kernel and a `FAST` choice. Error handling is omitted to show the selection flow; check both results before accessing `Value()`:

```cpp
shader::ShaderOptions options(memory);
const core::Result<void> enabled = options.SetKeyword("FAST");

core::Result<shader::ShaderKernelSelection> selected = asset.SelectKernel("Simulate", target, options, memory);

// Inputs for GPU program and pipeline creation.
const shader::ShaderLayout& layout = selected.Value().GetLayout();
const shader::ShaderPipelineConstants& constants = selected.Value().GetPipelineConstants();
```

Use `SelectPass` for the equivalent graphics flow and `GetRenderState()` to retrieve its authored render state. The GPU consumer uses the selected layout and constants to create programs and pipelines.

## Ownership

Assets and selections are shared owners; inspection views borrow their storage. A selection keeps its asset alive. Allocation resources must outlive all owners using them. `Create` clones a builder; `Build` consumes it and preserves retained allocation resources. Invalid input and operational failures return `Result`; invalid inspection contracts assert.

## Tests

[Tests.cmake](Tests.cmake) registers `Test-Shader`: GoogleTest coverage for selection, serialization, linkage, packing, boundary cases, allocation failures and ownership. Tests load [compiled HLSL fixtures](Tests/Data/README.md) without invoking a compiler or GPU. 

```powershell
cmake --build .build/windows-ninja --config Release --target Test-Shader
ctest --test-dir .build/windows-ninja -C Release -R '^Roxell\.Shader\.' --output-on-failure
```
