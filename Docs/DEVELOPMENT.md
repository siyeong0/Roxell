# Development

## Requirements

On Windows, install Visual Studio C++ tools, the Windows SDK, CMake 3.28 or later, Ninja 1.13 or later, Python 3.11 or later, and Node.js. Run the commands below from the repository root in an x64 Developer PowerShell.

Install the Python test dependencies once:

```powershell
python -m pip install -r DevKit/Automation/requirements-test.txt
```

## Build and test

```powershell
& ./DevKit/bootstrap.ps1 build
& ./DevKit/bootstrap.ps1 test
```

The build uses `.build/windows-ninja` and configures it if needed. The test command builds first, then runs CTest. Reuse this build tree. Pass `-Configuration Release` or `-Parallel <jobs>` when needed.

To run a focused test:

```powershell
& ./DevKit/bootstrap.ps1 test -Target Test-CoreContracts -TestFilter '^Roxell\.Core$'
```

To generate the Visual Studio solution:

```powershell
& ./DevKit/bootstrap.ps1 configure -Generator 'Visual Studio 17 2022'
```

Open `.build/windows-vs/Roxell.sln`. Regenerate it after source-list changes; do not edit generated files.

Release builds enable supported first-party interprocedural optimization by default (`ROXELL_ENABLE_RELEASE_IPO`); sanitized builds skip it. Record the configuration, IPO setting, and worker count when comparing performance.

## Verification

Test the behavior affected by your changes. Run `python DevKit/Architecture/validate_modules.py` for API, manifest, or source-policy changes, and build `Test-ContractHeaders` for public-header changes. Use a reusable ASan build for ownership changes and check affected backends. Skipped tests are not verified. Check links and diffs for documentation changes. See [Tests](../Tests/README.md) for test design.

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
