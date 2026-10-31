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

## Local files

- `.build/` contains reusable build output.
- `.cache/` contains generated content, logs, captures, dumps, and tool caches.
- `.local/` contains authored projects and content; preserve it as source data.

Remove obsolete temporary files and keep useful evidence for unresolved failures. Stop processes using an output before deleting it. Full-memory dumps are opt-in with `ROXELL_FULL_MEMORY_DUMPS=1`. [CleanWorkspace.ps1](../DevKit/Build/CleanWorkspace.ps1) removes only the obsolete trees it lists.
