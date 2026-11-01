# Architecture

Roxell is organized around runtime modules, feature packages, and tools. Modules define their own contracts, packages provide features, and applications assemble them.

## Project structure

| Directory | Purpose |
| --- | --- |
| `Engine/Runtime` | Shared runtime systems |
| `Engine/Packages` | Feature code and runtime/editor integration |
| `Engine/Tools` | Authoring, asset processing, and build services |
| `Engine/Editor`, `Engine/Agents`, `Engine/SDK` | Editor UI, agent integration, and clients |
| `Engine/Apps` | Application entry points and feature assembly |
| `DevKit` | Development tools |

## Dependencies

Dependencies must be explicit and free of cycles, including private dependencies. Runtime features must not depend on editor services. Generic services must not depend on UI or application-specific feature composition.

Core does not depend on engine domains or the operating system. Platform and backend adapters own native resources. World owns ECS storage, execution hosts manage scheduling and lifetime, and features own their schemas and integrations.

## Modules

Group code by responsibility. A module normally has one library, with `Public`, `Private`, and `Tests` directories and an optional `Shaders` directory. Keep module tests with their code and cross-module tests in [Tests](../Tests/README.md).

Manifests and CMake must agree on exports and dependencies. Each module README describes its contracts and usage.

## Data and lifetime

Authoring is the single writer for authored changes. The editor, CLI, and AI clients use the same revision, approval, and commit contracts. Cooking produces validated runtime data, which games can use without the editor.

Preserve stable IDs, schema versions, ordering, and serialized formats. Validate external input before publishing it. Allocators and borrowed storage must outlive their users; cancellation alone does not mean work or resource retirement is complete.
