# Development

## Requirements

On Windows, install Visual Studio C++ tools, the Windows SDK, CMake 3.28 or later, and Ninja 1.13 or later. Run the commands below from the repository root in an x64 Developer PowerShell.

## Build and test

```powershell
cmake -S . -B .build/windows-ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build .build/windows-ninja
ctest --test-dir .build/windows-ninja --output-on-failure
```

Reuse the build tree for subsequent builds. Set `BUILD_TESTING=OFF` when configuring to omit test targets.

To run a focused test:

```powershell
cmake --build .build/windows-ninja --target Test-CoreMath
ctest --test-dir .build/windows-ninja -R '^Roxell\.Core\.Math\.' --output-on-failure
```

To generate the Visual Studio solution:

```powershell
cmake -S . -B .build/windows-vs -G 'Visual Studio 17 2022' -A x64
```

Open `.build/windows-vs/Roxell.sln`. Regenerate it after source-list changes; do not edit generated files.

## Verification

Test the behavior affected by your changes. Check links and diffs for documentation changes. See [Tests](../Tests/README.md) for the available test targets.

## Git and pull requests

Write PR and commit titles in English using `<type>: <description>`. Start the description with a capitalized verb, describe the change, and omit the final period. Keep commit subjects around 50 characters when practical. Choose one type for the main change:

| Type | Use |
| --- | --- |
| `feat` | New features or public APIs |
| `fix` | Bug fixes |
| `refactor` | Code changes that preserve behavior |
| `build` | Build configuration and dependencies |
| `test` | Test additions or changes |
| `docs` | Documentation |
| `chore` | Other repository maintenance |

For example, use `feat: Add Core memory resources` for a feature PR and `test: Add memory resource tests` for its test commit. Use `add` in the description rather than as a type.

Name branches `<type>/<short-description>` using the same types, lowercase letters, and hyphens. Use one branch per PR. For example:

- `build/core-foundation`
- `feat/core-memory`
- `fix/array-overflow`
- `docs/git-conventions`

Keep each commit focused on one logical change. Add a commit body when the reason or constraints need explanation; separate it from the subject with a blank line and wrap lines around 72 characters.

In the PR description, briefly explain what changed and why. Include relevant checks and their results, and note any tests not run or remaining limitations. Keep simple changes concise.

## Local files

- `.build/` contains reusable build output.
- `.cache/` contains generated content, logs, captures, dumps, and tool caches.
- `.local/` contains authored projects and content; preserve it as source data.

Remove obsolete temporary files and keep useful evidence for unresolved failures. Stop processes using an output before deleting it. Full-memory dumps are opt-in with `ROXELL_FULL_MEMORY_DUMPS=1`. [CleanWorkspace.ps1](../DevKit/Build/CleanWorkspace.ps1) removes only the obsolete trees it lists.
