# Roxell Coding Standard

Follow this standard for new Roxell C++ code and update touched code when the change is safe and local.

Prefer readable code, clear ownership, and straightforward control flow. Respect nearby style in narrow edits. Third-party code keeps its own conventions; Roxell wrappers follow this standard.

Follow [Architecture](ARCHITECTURE.md) for ownership and dependencies and [Development](DEVELOPMENT.md) for build and test commands. Module READMEs own detailed API contracts and examples.

## I. Naming

### 1. Follow the naming conventions for each type and variable

| Target | Example | Convention |
| --- | --- | --- |
| Class | `PlayerController` | PascalCase |
| Struct | `MeshData` | PascalCase |
| Interface | `IRenderer` | `I` + PascalCase |
| Enum | `EDirection` | `E` + PascalCase |
| Bit-flag enum | `EVisibilityFlags` | Enum name + `Flags` |
| Public member function | `UpdatePosition()` | PascalCase |
| Protected or private member function | `updatePosition()` | camelCase |
| Local variable or parameter | `vertexCount` | camelCase |
| Class member variable | `m_VertexCount` | `m_` + PascalCase |
| Struct member variable | `VertexCount` | PascalCase |
| Static variable | `s_InstanceCount` | `s_` + PascalCase |
| Boolean variable | `bVisible` | `b` + PascalCase |
| Boolean class member | `m_bVisible` | `m_b` + PascalCase |
| Static boolean variable | `s_bInitialized` | `s_b` + PascalCase |
| Constant or macro constant | `MAX_VERTEX_COUNT` | UPPER_SNAKE_CASE |
| Namespace | `rox::render` | Lowercase API-family name |
| `goto` label | `CLEANUP` | UPPER_SNAKE_CASE |

Use `m_Storage`, `m_Schemas`, and `m_bFrozen`, not `m_storage`, `m_schemas`, or `m_frozen`. Update touched code when the change is local and safe.

An abstract policy base may omit `I` only when an established C++ vocabulary makes the family clearer, as with
`MemoryResource`, and the module contract documents the exception. Concrete family members still use an explicit
suffix such as `SystemMemoryResource`; do not use this exception for service or domain interfaces.

Do not add an interface only to mirror one concrete implementation. Introduce one when a current caller needs
substitution, a second implementation exists, or a documented platform or external-service boundary requires it.
A possible future mock or backend is not sufficient by itself.

### 2. Name functions with a verb and an object

```cpp
void LoadTexture();
void UpdatePosition();
void RemoveActor();
```

For a simple boolean state, prefer `Is`, `Has`, `Can`, or `Should`. If these do not fit, use another third-person singular verb that describes the state.

```cpp
bool IsVisible() const;
bool HasChildren() const;
bool CanRender() const;
bool ShouldUpdate() const;
bool Exists() const;
```

Name value-returning functions after what they return.

```cpp
uint32 GetAge() const;
```

Include `Recursive` in recursive function names.

```cpp
void VisitChildrenRecursive();
```

File-local free functions, including anonymous-namespace functions, use camelCase. Public APIs, interface overrides, and externally visible C-linkage functions follow their API contract.

```cpp
namespace
{
    void processInput();
}
```

### 3. Name variables after the values they store

Avoid meaningless short names except for simple loop variables. Use names that describe the stored data, and avoid unnecessary abbreviations.

```cpp
// Bad
const int32 cnt = mesh.GetVertexCount();
const float32 t = timer.GetDeltaTime();

// Good
const int32 vertexCount = mesh.GetVertexCount();
const float32 deltaTime = timer.GetDeltaTime();
```

Distinguish variables by their roles, even when their types match.

```cpp
const int32 sourceIndex = GetSourceIndex();
const int32 destinationIndex = GetDestinationIndex();

bool bVisible = true;
bool bHasChildren = false;
bool bCanRender = true;
```

```cpp
class Player
{
private:
    static uint32 s_PlayerCount;

private:
    int32 m_Health = 100;
    float32 m_MoveSpeed = 5.0f;
    bool m_bVisible = true;
};
```

Use PascalCase for struct members. Boolean struct members keep the `b` prefix.

```cpp
struct MeshData
{
    uint32 VertexCount = 0;
    uint32 IndexCount = 0;
    bool bHasNormals = false;
};
```

Write established abbreviations in uppercase, as in `orderID` and `m_DepartmentID`.

Use the `p` prefix only when identifying a variable as a pointer is necessary to avoid ambiguity.

### 4. Suffix nullable pointer parameters with OrNull

Append `OrNull` to a pointer parameter name when `nullptr` is a valid argument.

```cpp
void SetTarget(Player* const targetOrNull);
```

A pointer parameter without `OrNull` is non-null by default. Assert that precondition before using it.

```cpp
void RegisterPlayer(Player* const player)
{
    ASSERT(player != nullptr, "Registered player must not be null.");
}
```

## II. Variables and Types

### 5. Use fixed-width engine types

Use the scalar aliases from `Types.h`, such as `int32`, `uint32`, `uint64`, `float32`, and `float64`. Avoid bare `int`, `char`, and `float` for numerical data.

```cpp
int32 vertexCount = 0;
uint64 frameIndex = 0;
float32 deltaTime = 0.0f;
```

Do not use standard fixed-width names such as `uint32_t` in engine code. Core topics also use the engine aliases; standard-library implementations stay in their owning Core topic.

Use character types for text and native types where an external API requires them. Keep standard-library and interoperability details within their permitted boundaries.

### 6. Declare one variable per line

```cpp
// Bad
int32 width = 0, height = 0;

// Good
int32 width = 0;
int32 height = 0;
```

### 7. Avoid Variable Shadowing

Do not redeclare an outer-scope variable name in an inner scope. Choose a name that describes the inner variable's role.

```cpp
const int32 itemCount = GetCount();

for (int32 itemIndex = 0; itemIndex < itemCount; ++itemIndex)
{
    Process(itemIndex);
}
```

### 8. Use const whenever possible

Apply `const` to local variables and function parameters whenever possible. Mark member functions that do not modify the object `const`.

```cpp
const int32 vertexCount = mesh.GetVertexCount();

void SetHealth(const int32 health);
void DrawMesh(const Mesh& mesh);

int32 GetHealth() const;
```

### 9. Use constexpr whenever possible

Use `constexpr` whenever possible. Prefer `constexpr` or `const` constants over `#define` constants.

```cpp
constexpr int32 MAX_PLAYER_COUNT = 4;
constexpr float32 DEFAULT_MOVE_SPEED = 5.0f;
```

If a macro constant is necessary, enclose its value in parentheses.

```cpp
#define NUM_CLASSES (1)
```

### 10. Use nullptr instead of NULL

```cpp
Player* player = nullptr;
```

### 11. Append f to float literals

Append `f` to floating-point literals unless `double` is required.

```cpp
const float32 alpha = 0.5f;
const float32 moveSpeed = 10.0f;
```

### 12. Declare enums with enum class

Use `enum class`. A plain `enum` is allowed only when an external ABI or a narrow technical constraint requires it.

```cpp
enum class EDirection
{
    North,
    South,
    East,
    West
};
```

Specify an underlying type only when a particular size is required, such as for serialization, ABI, or member packing.

```cpp
enum class ESerializedDirection : uint8
{
    North,
    South
};
```

### 13. Use C++ casts instead of C-style casts

Prefer explicit C++ casts such as `static_cast`. Use `reinterpret_cast` only where the required low-level conversion calls for it.

```cpp
const float32 ratio =
    static_cast<float32>(completedCount) / totalCount;
```

Do not use `const_cast`. If mutable access is required, provide an API that returns a mutable object from a mutable source.

### 14. Use auto only when the type is clear

Use `auto` only when it improves readability and the type remains obvious. Do not use it to hide the concrete object being created, including iterator and allocation expressions.

Write the type when a function's return type matters to understanding the code.

```cpp
const TextureHandle texture = CreateTexture();
```

Use `auto` for lambdas.

```cpp
const auto isPositive = [](const int32 value)
{
    return value > 0;
};
```

## III. Classes and Functions

### 15. Use structs for simple data

Structs contain public data members. Use PascalCase for their names, with the `b` prefix for boolean members. Avoid non-trivial member functions except very small construction helpers.

```cpp
struct MeshData
{
    uint32 VertexCount;
    uint32 IndexCount;
};
```

Use a class when state needs encapsulation. Use getters and setters where appropriate.

```cpp
class Employee
{
public:
    const String& GetName() const;
    void SetName(const String& name);

private:
    String m_Name;
};
```

- Math numeric value types are the deliberate exception: vectors, matrices, quaternions, bounds, planes, and frusta may expose value-semantic operations while keeping all state public and free of hidden ownership.
- Keep trivial `constexpr` arithmetic in the header; place normalization, inversion, projection, intersection, and other non-trivial runtime algorithms in the matching `.cpp` file.
- Math numeric templates support `float32` and `float64` with explicit instantiations for runtime algorithms.
- Keep each precision within its scalar type; place vector/matrix `f` and `d` aliases in the declaration-owning header.
- Other floating value templates default to `float32`.

### 16. Keep class declarations in a consistent order

Declare class members in this order:

1. `friend` classes
2. public member functions
3. protected member functions
4. private member functions
5. public member variables
6. protected member variables
7. private member variables

Write an access specifier for every declaration group. Keep functions and variables in separate blocks, even when their access level is the same: repeat `private:`, `protected:`, or `public:` before the member-variable group.

```cpp
class Player
{
public:
    int32 GetHealth() const;
    void SetHealth(const int32 health);

private:
    bool isValidHealth(const int32 health) const;

private:
    int32 m_Health;
};
```

Within each declaration group, order functions and variables for readability and logical flow unless there is an important reason to do otherwise, such as memory layout or alignment, serialization layout, ABI constraints, or language rules. Use blank lines to separate meaningful subgroups, not as decoration. Keep related fields together and separate fields with distinct responsibilities when that improves scanning; do not split every field or disrupt a compact record.

Avoid `friend class` unless it clearly reduces complexity.

Keep validation, state transitions, commit/rollback and resource retirement on the object that owns their invariants.
Do not grant whole-class access for a read-only query or an operation the owner can perform itself. Remove unused
friendships. Prefer an exact member-function friend when only that operation needs privileged collaboration.
Do not replace friends with mutable state getters, unchecked constructors or generic `Access` helpers.
Retain narrow producer/lease, iterator and test-injection relationships when they protect a concrete invariant;
value operators declared as hidden friends are not cross-subsystem access grants.

### 17. Initialize members in the constructor initializer list

Initialize members in the initializer list, one per line and in declaration order.

```cpp
Player::Player(const int32 health, const float32 moveSpeed)
    : m_Health(health)
    , m_MoveSpeed(moveSpeed)
    , m_bVisible(true)
{
}
```

### 18. Always use override when overriding a virtual function

```cpp
class IRenderer
{
public:
    virtual ~IRenderer() = default;

    virtual void Render() = 0;
};

class MeshRenderer : public IRenderer
{
public:
    void Render() override;
};
```

### 19. Use final whenever possible

Prefer `final` when further inheritance or overriding is unnecessary.

```cpp
class MeshRenderer final : public IRenderer
{
public:
    void Render() override;
};
```

To allow inheritance while preventing a particular override, mark that function `final`.

```cpp
class MeshRenderer : public IRenderer
{
public:
    void Render() override final;
};
```

### 20. Prefer reference parameters and use pointers for output parameters

Prefer references when passing objects. Use `const` references for read-only access.

```cpp
void DrawMesh(const Mesh& mesh);
void UpdatePlayer(Player& player);
```

Use pointers and the `out` prefix for output parameters. Output pointers must not be null; assert that precondition.

```cpp
void GetScreenDimension(uint32* const outWidth, uint32* const outHeight)
{
    ASSERT(outWidth != nullptr, "Screen width output must not be null.");
    ASSERT(outHeight != nullptr, "Screen height output must not be null.");

    *outWidth = m_Width;
    *outHeight = m_Height;
}
```

```cpp
uint32 width = 0;
uint32 height = 0;
GetScreenDimension(&width, &height);
```

Also use pointers when `nullptr` is valid, the class stores the argument's address, or the API requires `void*`. Nullable pointer names end in `OrNull`; storing an address does not extend the object's lifetime.

```cpp
void AddMesh(Mesh* const mesh)
{
    ASSERT(mesh != nullptr, "Stored mesh must not be null.");
    m_MeshCollection.Add(mesh);
}
```

Use zero-like defaults such as `nullptr`, `false`, and `0` when defaulting pointer and boolean parameters.

### 21. Use distinct function names for different behavior or lookup keys

Prefer explicit names over ambiguous overload sets.

```cpp
const Anim* GetAnimByIndex(const int32 index) const;
const Anim* GetAnimByName(const String& name) const;
```

Use const and non-const overloads for the same operation.

```cpp
Anim& GetAnim();
const Anim& GetAnim() const;
```

### 22. Use local lambdas for short logic repeated within one scope

Prefer a local lambda when logic is short, used more than once in the same scope, and unlikely to be useful elsewhere.

```cpp
const auto isValidIndex = [vertexCount](const int32 index)
{
    return index >= 0 && index < vertexCount;
};
```

Keep single-use logic at the call site when it remains readable. Extract a function when the logic grows or is needed elsewhere. A longer or templated helper used only in one function can remain file-local when putting a lambda inside the caller would obscure its main flow.

### 23. Make use of RVO

Return objects by value and let the compiler apply RVO or NRVO.

```cpp
MeshData CreateMeshData()
{
    MeshData meshData{};

    BuildVertices(meshData);
    BuildIndices(meshData);

    return meshData;
}
```

Do not add `core::Move` to optimize a return value manually. NRVO is not guaranteed; do not complicate control flow merely to force a single return statement.

Use move constructors and move assignment operators when ownership transfer is part of the type's design.

## IV. Control Flow and Formatting

### 24. Put braces on a new line and do not omit them

Put opening braces on a new line. Use braces even when an `if`, `for`, or `while` body contains only one statement.

```cpp
if (!bInitialized)
{
    return;
}
```

Use the built-in Visual Studio C++ formatter with `Ctrl+K, Ctrl+D`. Use one tab per indentation level and a tab width of four columns. The repository intentionally has no `.clang-format` or C/C++ `.editorconfig` override.

Keep very small inline functions in headers on one line only when that is clearer.

Separate multi-line function definitions and distinct steps within a function with one blank line: entry contracts, validation, preparation, mutation/publication, and the final result. Keep related declarations, assignments, assertions, and short guard clauses together. Group closely related short one-line functions or declarations without blank lines between each; separate unrelated groups. Keep `else`, `catch`, and a `do` loop's `while` attached to their blocks. Do not add a blank line after every statement or every field. Split multiple statements on one line when they hide control flow or mix distinct steps.

Wrap long declarations, conditions, and comments at natural semantic boundaries. Keep related expressions and their qualifiers easy to read; do not wrap mechanically after each parameter or word.

### 25. Attach pointer and reference symbols to the type

```cpp
Player* player = nullptr;
Player& target = GetPlayer();
const Player& source = GetSourcePlayer();
```

### 26. Always include default in switch statements

```cpp
switch (mode)
{
case ERenderMode::DepthOnly:
    RenderDepth();
    break;

case ERenderMode::Shaded:
    RenderShaded();
    break;

default:
    ASSERT(false, "Render mode must be supported.");
    break;
}
```

Use `ASSERT` when `default` must be unreachable. Validate external input before passing it to code that relies on a valid enum value.

### 27. Mark intentional fallthrough with [[fallthrough]]

```cpp
switch (step)
{
case 0:
    Prepare();
    [[fallthrough]];

case 1:
    Execute();
    break;

case 2:
case 3:
    Finish();
    break;

default:
    break;
}
```

Empty case labels that share a body do not need `[[fallthrough]]`.

### 28. Use range-based for loops whenever possible

Prefer range-based loops when an index is unnecessary.

```cpp
for (const Vertex& vertex : vertices)
{
    ProcessVertex(vertex);
}
```

Use a mutable reference when modifying an element.

```cpp
for (Vertex& vertex : vertices)
{
    UpdateVertex(vertex);
}
```

### 29. Use static_assert for compile-time checks

Use `static_assert` for conditions known at compile time.

```cpp
constexpr uint32 BUFFER_ALIGNMENT = 16;

static_assert(BUFFER_ALIGNMENT > 0 && (BUFFER_ALIGNMENT & (BUFFER_ALIGNMENT - 1)) == 0, "Buffer alignment must be a nonzero power of two.");
```

### 30. Explain intent and constraints in comments

Use clear names and focused functions. Add comments for intent or context the code cannot explain.

Write comments, warnings, assertion messages, logs, diagnostics, and user-facing developer text in English. Phrase comments as concise, natural sentences and wrap them at sensible boundaries. Describe ownership, lifetime, failure behavior, or a non-obvious constraint where the reader needs it; remove comments that only repeat the code.

#### Comment requirements

A comment is a note to the next reader of *this* code. It is good when all of the following hold:

1. **It explains why, not what.** The code already says what it does. A comment states the intent, the invariant, the
   constraint, or the non-obvious consequence that the code cannot say: "integer frame index — a float accumulator
   drifts between 30 Hz and 60 Hz sessions", not "advance the position by speed times sample rate".
2. **It is self-contained.** The reader must understand it without opening any other file. Everything the comment
   relies on is stated in the comment itself or visible in the code next to it.
3. **It names code, not documents.** The only references a comment may carry are identifiers that exist in the
   source (`SceneCommandTable.h`, `PhysicsSync::WriteBack`, `ROX-CONTENT-LAUNCH-UNRESOLVED`) and stable external
   standards (the glTF specification, IEEE 754, an OS API by name). Architecture decision records, plans, cards,
   ledgers, roadmaps, review rounds, and reviewers are *not* references: those documents move, get archived, or
   disappear, and a comment that needs one is unreadable the day it does.
4. **It has no history.** How the code came to be — what it replaced, which packet added it, who found the bug,
   when a decision was reversed — belongs in version control and the design documents, not in the source. A
   comment describes the code as it is now.
5. **It is as short as the fact.** One or two lines is the norm; three to five is for a genuinely subtle rule.
   A comment longer than eight lines is a signal that either the code should be restructured so the explanation
   is unnecessary, or the explanation is design prose that belongs in the owning module document.
6. **It sits where the reader needs it.** Contracts (preconditions, ownership, thread affinity, lifetime of
   returned views, failure classes) go on the declaration. Algorithmic notes go on the statement they explain.
   Nothing is repeated at both.
7. **It stays true.** A comment that restates a number, a count, or an ordering the code also encodes will rot;
   prefer a `static_assert`, a named constant, or a contract test over a comment for anything checkable.

#### Forbidden in comments

- References to `ADR-*`, section marks (`§`, "section 4.2"), roadmap or ledger sections, plan or card identifiers
  (`AU3b`, `PHYS-004b`, `E2b`, ...), `plans/` paths, review rounds ("round 2", "Codex", "spec review"), dates, and
  people. The validator counts these per file (`comment policy` ratchet) and the count may only go down.
- Narrative ("previously", "this replaces", "was moved from", "X found that", "deliberately not ... because a
  reviewer ...").
- Probe and mutation narratives in production code. A contract test may carry one line per probe naming the
  mutation and the assertion it reddens; nothing else about the review process belongs in the tree.
- `TODO` with an owner or ticket that will disappear. Write `TODO: <the missing behaviour>` and nothing more.

#### Examples

```cpp
// Bad — history, a document reference, and a restated count:
// ADR-104 section 11.2 (round 2 finding 3). Replaces the modulo ring AUDIO-002 shipped; the
// table has 32 slots because MAXIMUM_AUDIO_VOICES is 32.
VoiceSlot m_Voices[MAXIMUM_AUDIO_VOICES];

// Good — the invariant the code cannot say:
// A slot is free at tick t when t >= EndTick; EndTick 0 means never taken, so the zeroed table is empty.
VoiceSlot m_Voices[MAXIMUM_AUDIO_VOICES];
```

```cpp
// Bad — a contract that sends the reader elsewhere:
// See ADR-091 section 2.5.1 for why a ContentMountTable is not the parameter.

// Good — the contract, stated:
// Takes one manifest rather than a mount table: provenance must be provable against exactly the
// manifest that published the descriptor, and a mount table resolves across several.
```

## V. Files and Modules

### 31. Keep independently owned classes in their own files

An independently owned class with its own responsibility, lifetime, invariants, or test surface uses one `ClassName.h` and `ClassName.cpp` pair.

Pure interfaces, templates, concepts, and simple constexpr or value types may be header-only. Do not create an empty `.cpp` merely to satisfy the pair convention.

Avoid headers that contain only includes. Include declaration-owning headers directly; splitting a class is not a reason to keep the old file as a forwarding header. Retain an aggregate entry point only when it has a documented module or package use case, such as the existing Core convenience headers. Do not add new aggregate headers for private implementation groups.

A small file-local helper class may live in its owner's `.cpp` when it has no independently owned lifetime or reuse requirement; keep its invariants and validation local to that helper. Closely coupled small structs, enums, and request/result types may live with their primary API. Retain a local reader or writer when it makes state, error handling, or format-specific operations clearer; name it for its actual format or role. Do not combine helpers merely because both read or write data.

Prefer the smallest set of abstractions needed by current callers. Remove a redundant specialization when the generic implementation preserves its behavior and guarantees. Reconsider unused public utilities rather than inventing a caller; check external consumers and source/ABI compatibility before removing an export.

### 32. Match file names to their primary class

File names must match the primary class name, including capitalization.

```cpp
class PlayerAnimation;

// PlayerAnimation.h
// PlayerAnimation.cpp
```

When several files implement one class, use `ClassName_topicName.cpp`. Preserve the class name exactly, separate it from the topic with one underscore, and write the short topic name in lower camelCase. Do not use additional underscores between topic words.

```cpp
// RenderWorld_Load.cpp
// Device_CommandList.cpp
// Device_PreparedRecording.cpp
```

Choose a separate public header only when the API has an independently useful responsibility or lifetime; otherwise keep related declarations together and split implementation by topic. Update source lists, manifests, includes, tests, and documentation when renaming files.

For reverse-OOP or backend-specific implementations, use topic/backend suffixes that make the relationship explicit.

```cpp
// Renderer.h
// Renderer.cpp
// Renderer_Gl.h
// Renderer_Gl.cpp
```

### 33. Use header extensions consistently and start headers with #pragma once

Use `.hpp` for headers containing substantial C++ implementation, including templates, containers, and inline utilities, even when some implementation remains in a `.cpp`. Use `.h` for declaration-oriented APIs.

Prefer ordinary `.cpp` files. Use `.inl` only when an included definition is necessary.

Start every header with `#pragma once`. Leave a blank line between the include block and the code body.

### 34. Include declaration-owning headers in a consistent order

Include exported target headers by their target-relative name in angle brackets. Use quotes only for private headers in the same target. Keep the directly corresponding header first in a source file, then separate third-party or standard-library groups with a blank line.

```cpp
#include <AnimationInfo.h>

#include <Array.hpp>
#include <algorithm>
```

When possible, sort includes alphabetically inside each group.

Run include audits when editing public headers or changing include relationships.

### 35. Use forward declarations without adding unnecessary indirection

Prefer forward declarations only when the API naturally stores or accepts a type by pointer or reference. Do not introduce PImpl, heap allocation, or another indirection solely to reduce includes. If include cost is a measured problem, first move implementation-only includes to the existing `.cpp` file.

- Avoid PImpl by default.
- Do not add a one-pointer `Implementation` or `Impl` facade merely to hide private members.
- Use direct private members because they keep ownership, lifetime, debugging, and code navigation visible.
- PImpl is allowed only when a documented constraint requires it, such as preserving an already-stable binary ABI or isolating a very large or platform/SDK header that cannot be contained in a private source file.
- Record that reason next to the declaration or in the module document.
- An incomplete private record stored in a collection for stable addresses is not PImpl when the owning class still exposes and directly operates on its own state.

### 36. Keep exported headers in their owning module

Place exported headers directly under `Engine/<Group>/<Module>/Public` by default. Do not repeat product or module names in paths such as `Public/Roxell/Core`.

- Core is the deliberate exception because it contains eight stable responsibility groups: `Algorithm`, `Memory`, `Containers`, `Diagnostics`, `Base`, `Async`, `Schema`, and `Math`.
- They live under `Engine/Runtime/Core/<Package>/Public` and `Private`.
- Keep these folders flat; the dependency checker identifies low-level Base and Math headers explicitly.
- `Engine/Runtime/Core/Public/Core.h` remains the module-wide umbrella.
- Do not create a package for a single type, an unimplemented future feature, or a temporary file grouping.

- Core groups compile into the single `Runtime-Core` library.
- Math exposes `rox::math`; other Core groups use `rox::core`.
- Keep direct includes such as `#include <Result.hpp>`, `#include <Task.hpp>`, and `#include <Vector3.hpp>`.
- Public header names must be unique across exported roots.
- Current module manifests and CMake define dependencies; directory grouping alone does not create a new namespace or module.

### 37. Keep package include dependencies explicit and acyclic

- Every internal package layout must document an acyclic include direction across the package targets.
- Core uses Base primitive headers as its bottom layer.
- Math scalar primitives and Algorithm may depend only on Base primitive headers.
- Diagnostics may use these primitive layers; Memory may additionally use Diagnostics; Containers may additionally use Memory.
- Higher Base utilities may use Containers, Memory and Diagnostics; Async and Schema may use these services; Math geometry may use Base and Diagnostics.
- These header ownership rules are defined in `DevKit/Architecture/core_layout.py` and enforced by the validator and include audit.
- Package files must not include the module umbrella `Core.h`.

Name the optional Math package umbrella `CoreMath.hpp`, not `Math.h`. On Windows' case-insensitive file systems, a project `Math.h` can shadow the CRT `<math.h>` header.

Use `CoreMemory.h` and `CoreString.hpp` rather than public project headers named `Memory.h` or `String.h`; those generic names can shadow C/C++ runtime headers on case-insensitive file systems.

### 38. Keep namespaces within their API family

Namespaces use lowercase API-family names under `rox` and stop at those two levels. Related modules may share a namespace without sharing a library, dependencies or resource ownership. Keep implementation types in their owning API family and use anonymous namespaces for translation-unit-local helpers; do not introduce `detail` namespaces. Generated package identities may use a deeper namespace to isolate independently selected providers.

```cpp
namespace rox::asset
{
}
```

Use `rox::<family>` as the normal namespace boundary. Directory groupings do not create namespaces automatically. Namespace sharing does not change module ownership or permit additional dependencies; directory groups such as Runtime and Packages do not introduce a shared domain namespace.

## VI. Memory and Ownership

### 39. Prefer unique ownership and document shared lifetimes

- Use Core `UniquePtr` for new first-party engine objects with one dynamic owner and preserve the allocating `MemoryResource`.
- Do not introduce shared ownership by default.
- When multiple objects genuinely share one lifetime, such as a cancellation state, keep that use local, structurally prevent ownership cycles, and use exact-type Core `SharedPtr` only after the owner contract establishes why value, borrowing, or unique ownership is insufficient.
- `SharedPtr` does not provide weak, aliasing, custom-deleter, array, or polymorphic-conversion facilities; needing one requires a separate design review.
- Standard smart pointers currently recorded by the policy baseline are ownership debt, not precedent for new exported APIs or owning fields.

Moving `MemoryBlock`, `UniquePtr`, or `SharedPtr` transfers the resource together with the owned allocation. `Array` and `String`
assignment preserve the destination resource; a cross-resource move reallocates through that destination resource.
Copying a `SharedPtr` is allocation-free and retains the same resource. Different owner objects may be copied and
released concurrently; the same owner object and its pointee require external synchronization. The resource must
outlive the last owner and be thread-safe when the last release may run on another thread. Recoverable
`MakeSharedInto` failure leaves its output owner unchanged.

### 40. Choose Core storage and pass its resource explicitly

- Use a fixed-size value or non-owning `Span` when the maximum size is known and ownership is unnecessary.
- Use Core `Array` or `String` for new dynamic engine-owned storage.
- If the size can be estimated, reserve it in advance.
- Do not add a small-buffer optimization, hash container, deque, pool, or another specialized container before an accepted caller and profile justify its invariants.

Default-constructed `Array` and `String` use `SystemMemoryResource` only as a bootstrap convenience. A production
subsystem with accounting, budgets, or bounded lifetime injects its owning resource explicitly. Shared resource
decorators serialize exact accounting. Profile realistic contention and worker-local alternatives before using
them on a shared hot path; throughput measurements do not establish fairness or bounded acquisition latency.

### 41. Keep standard-library use within the Core boundary

- Engine code outside Core must not refer to `std`, include standard-library headers, or own standard containers.
- Use Core `String`, `StringView`, `Array`, `Span`, `FlatMap`, `FlatSet` and ownership types with an explicit resource.
- Core implementations also reuse Core primitives; only their primitive, allocation, diagnostic and interoperability boundaries call the standard library directly.

Do not hide new standard-library ownership behind `using namespace std`, a namespace alias for `std`, or an owning
STL type alias. The transition validator is lexical rather than a C++ type-system proof, so review still verifies
ownership expressed through an existing alias.

`DevKit/Architecture/memory_policy_baseline.json` records the exact occurrence count for each file, policy
category, and forbidden token/API. The module validator rejects new raw allocation, a new token, or an increase
of an existing token; swapping one debt kind for another cannot preserve a passing total. When debt is removed,
the same change must ratchet that exact baseline entry down.

- Base primitives supply atomics, mutexes, traits, numeric limits and source locations; Algorithm owns generic range operations and Math primitives own numerical functions.
- Tests may use standard containers as an independent oracle; third-party sources retain their own representation.
- C/SDK adapters keep native resources in their owning backend and convert data to Core before returning to engine code.
- Public ABI boundaries never transfer allocator-dependent standard objects. Shared C/C++ ABI headers use C-compatible Core scalar aliases rather than C++ namespaces; preserve underlying types, layout, and calling conventions and verify both C and C++ consumers.
- The architecture validator rejects standard namespace references and standard includes outside Core without a migration baseline.

### 42. Allocate and release memory through its owning backend

Core.Memory and Core.Containers are the engine-owned allocation boundary. Changes require failure, alignment, lifetime, concurrency, platform, and sanitizer verification appropriate to their contracts.

- Outside `Engine/Runtime/Core/Memory`, new or materially changed first-party engine code must not directly use global `new`/`delete` or `malloc`/`calloc`/`realloc`/`free`.
- A documented Platform memory backend is the only additional first-party boundary allowed to call OS/SDK page and pressure primitives.
- Other code uses an explicit `MemoryResource`, Core ownership type, or Core container.
- The concrete backend that allocates a block also owns its matching deallocation path; memory must not cross a CRT, DLL, process, SDK, or resource boundary with an implicit free contract.

### 43. Validate allocation sizes, alignment, and lifetime

- Every allocation request has an explicit byte count, power-of-two alignment, and resource.
- A bounded `MemoryFailure` carries the failure reason, resource identity, and request without allocating.
- Zero-byte allocation returns `nullptr`; freeing `nullptr` is a no-op.
- The same resource and the original size/alignment contract must be used to release a block, and the resource must outlive every allocation it owns.
- Check multiplication, addition, alignment rounding, capacity growth, and pointer-range arithmetic for overflow before allocating or copying.
- Never silently fall back to another resource or retry indefinitely.

### 44. Name recoverable and mandatory allocation APIs explicitly

- Use ordinary action names and explicit return contracts for recoverable operations.
- Memory uses `Allocate` / `Create` for nullable results and `CreateInto` for transactional output replacement; mandatory variants have an explicit `Required` suffix.
- Reserve `Try` for a meaningful distinction such as nonblocking acquisition or competing completion, not merely because a function can fail.
- Existing container `Try...` APIs retain their documented contracts until their own API migration.
- Mandatory allocation may use the allocation-free fail-fast boundary after recording bounded diagnostics; it must not allocate recursively or perform unbounded cache trimming in an out-of-memory path.
- Resource thread safety is explicit: a thread-confined arena must not be shared, while a resource documented as concurrent must synchronize its own accounting.

### 45. Use Core operations before raw byte operations

Prefer Core container operations and Core algorithms over byte operations. Use raw `memset`, `memcpy`, or `memmove` only inside the Memory/Containers implementation or a narrow interop/codec boundary, with validated ranges and trivially copyable data. Construct, move, assign, and destroy non-trivial objects through their language operations.

### 46. Add specialized allocation strategies only after profiling

- Do not override global `operator new/delete` or build a general-purpose replacement heap as part of unrelated feature work.
- A complete global override would require every scalar/array, sized/unsized, aligned/unaligned, and nothrow form and would affect CRT/DLL and sanitizer behavior.
- Add size classes, slab/pool allocators, thread caches, small-string/small-vector optimization, huge pages, or global interception only after a repeatable profile and a separate accepted decision.

### 47. Keep native memory APIs in Platform backends

- Core owns platform-neutral allocation/resource contracts.
- Platform owns virtual-page reservation/commit and memory-pressure adapters, then injects them through the narrow Core seam.
- Core public headers must not expose Win32, POSIX, Android, Apple, Xbox, or PlayStation SDK types or assume a fixed page size.
- A pressure callback only publishes bounded state; actual trimming occurs later at an owning safe point.

## VII. Assertions, Errors, and Diagnostics

### 48. Use ASSERT for runtime contracts in every build

- Include `<Assertion.h>` and use exactly `ASSERT(condition, "English message")`.
- Do not use standard C `assert`, alternate assertion macros, a direct debugger-break intrinsic, or a local fatal macro.
- The exported header is deliberately named `Assertion.h`, not `Assert.h`: on case-insensitive Windows include paths, `Assert.h` shadows the C runtime `<assert.h>` used by `<cassert>` and third-party headers.

- `ASSERT` is active in every build configuration.
- Contract violations are programmer errors: a function's preconditions, its promised postconditions, intermediate algorithm invariants, ownership and lifetime rules, thread/phase/reentrancy rules, and unreachable control flow all use `ASSERT`.
- The ability to return an error, keep running, or manufacture a fallback does not make a contract violation an operational failure.
- Neither a `Result` return type nor a `Try` name weakens a precondition.

For the distinction between contracts and error handling, see [C++ Core Guidelines I.6, I.8 and E.3](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#i6-prefer-expects-for-expressing-preconditions). Roxell expresses all three runtime contract categories with its always-enabled `ASSERT`.

### 49. Assert preconditions, intermediate invariants, and postconditions

1. **Before the call:** assert required receiver/collaborator initialization, nonoptional pointers, valid device/graph/scope-local handles, contained ranges, valid enum values, declared access and compatible typed arguments. Assert required call order, phase, thread affinity and non-reentrancy before dependent work. A stale internal handle or a mismatched prepared reference is a bug even when returning `NotFound`, `Conflict`, or `InvalidArgument` would be easy.
2. **During the call:** assert facts established by successful validation, reservation, admission or preparation. A lookup that must find a previously registered object, a pre-reserved insertion that cannot allocate, a verified VM index, or a balanced recording transition must not silently fail, skip work or substitute a value.
3. **After successful work:** assert the promised usable owner, complete publication, valid output identity/range, correct phase, and balanced ownership/counters before publishing or returning the result. Check a logical guarantee once where its owner establishes it; do not repeat tautologies or conditions already enforced by types.

### 50. Separate contract violations from operational failures

**Operational outcomes:** keep `Result`, an explicit status or a predicate only when the call satisfies its contract and the outcome still belongs to normal execution. Examples include allocation/admission pressure, cancellation, device/OS/I/O failure, unavailable capabilities, an optional lookup, and explicitly supported polling/backpressure. The caller must have a meaningful response such as retry, cancellation, rejection/reporting of external input, rollback or selecting another supported path. An error code added only to propagate an impossible state is not such a response.

**External input boundaries:** parsing and explicit validation APIs reject malformed authored documents, files, scripts, command-line and IPC/network input without asserting on that data. Validate before turning external values into trusted indices, handles, descriptors or commands. Downstream code then asserts the established contract. Being a public C++ method, taking a descriptor, or receiving a number does not by itself make an API an external-input boundary.

- Split mixed guards.
- Assert the caller/implementation obligations first, then handle the actual runtime outcome separately.
- For example, an empty or foreign recorder and an out-of-phase command are contract violations; a valid prepared native submission can still encounter device loss.
- A bounded queue may legitimately be full, while exceeding a command budget that preparation already admitted is a recording bug.

- State queries, optional lookups and documented idempotent cleanup can accept empty state as part of their contract.
- Pending asynchronous work may return Pending or Busy when the operation explicitly supports polling or retry.
- These are explicit API behaviors, not blanket exceptions for all stale handles, descriptors, lifecycle states or `Try` methods.
- Internal callbacks and same-process adapters obey the same contracts as ordinary methods; do not label an impossible callback result a recoverable composition fault.

### 51. Place assertions at the contract boundary and keep conditions side-effect-free

Put precondition assertions at entry, before dependent reads or mutations; acquire a required synchronization guard before inspecting its protected invariant. Put postconditions immediately after the successful operation that establishes them, and intermediate assertions where the algorithm relies on them. Conditions must be side-effect-free. Evaluate allocation, insertion, increment, I/O, lock acquisition, polling and other work separately, then assert its saved result. Messages are non-empty English narrow string literals describing the required invariant.

```cpp
ASSERT(recording.IsValid(), "Commands require a live recording owner.");
ASSERT(phase == recording.GetPhase(), "Commands use the active prepared phase.");

const bool bInserted = reservedValues.Add(value);
ASSERT(bInserted, "Pre-reserved value storage accepts every admitted value.");

const auto created = device.CreateBuffer(descriptor);
if (!created)
{
    return Result<Buffer>::Failure(created.GetError());
}
ASSERT(created.Value().IsValid(), "Successful buffer creation returns a usable owner.");
```

Remove the fallback/return branch for a converted contract violation; do not leave both checks for the same impossible state. Assertion failure writes and flushes its structured record, notifies an installed read-only observer, breaks at the macro call site, then terminates. Assertions diagnose defects during development and remain enabled in Release; they are not a crash-recovery policy. Prefer `static_assert` for conditions known at compile time.

### 52. Verify fatal contracts in child processes

- Test changed contracts in child processes and verify the exact fatal diagnostic.
- Replace tests that treated programmer misuse as ordinary rejection; keep separate tests proving that valid calls can still handle operational failures and malformed external input.
- Also exercise the successful postcondition and relevant cancellation/retirement paths.
- A dedicated test child may exit immediately only to inspect the fatal report before the debugger break; keep that exception bound to an exact path, function, call expression and occurrence in the diagnostics policy.

### 53. Return operational failures with Result

- Use `Result<T>` by default: `Failure(code, diagnosticID, message, recoveryHint)` for a new operational failure and `Failure(error)` to propagate one.
- The optional hint describes a meaningful recovery.
- Diagnostic IDs remain stable semantic identifiers; use the automatically captured source location for file, line and function instead of encoding a function name into the ID.
- Do not replace external input validation with assertions.
- A documented domain outcome may replace Result when its states directly express the caller's required actions and simplify the complete call site. Define input consumption, retry, completion and cleanup for every state. Do not nest Result in that outcome or encode operational failure as Result success.
- When an owner handles a terminal failure internally and returns only an action/status, it must retain required lifetime/completion facts and report the diagnostic once. Logging alone is not recovery; callers must not continue normal work when the outcome forbids it. Keep detailed Result errors where the caller can recover or select another supported operation.

### 54. Use Core logging macros and stable categories

Include `<Log.h>` and use the `LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`, and `LOG_CRITICAL` macros with a stable category and an English message. Categories identify a subsystem or component, such as `Content.PNG` or `RuntimeHost`; do not put entity IDs, paths, or other per-event data in the category.

- `Trace`: high-volume execution detail that is normally filtered.
- `Debug`: developer-oriented state useful during diagnosis.
- `Information`: process lifecycle and significant successful milestones.
- `Warning`: an unusual condition was handled and execution can continue.
- `Error`: an operation failed but the process remains able to report or recover.
- `Critical`: the process cannot safely continue or an assertion is being reported.

### 55. Log meaningful transitions and report each failure once

Logging does not change control flow. `LOG_ERROR` and `LOG_CRITICAL` do not assert, and `ASSERT` already emits a Critical `Assert` record, so do not use a log call as a substitute for a required invariant or duplicate the same assertion record without additional context.

- Log lifecycle boundaries and diagnostically meaningful transitions: host and subsystem startup/shutdown, selected backend or mode, successful publication or connection milestones, retry/recovery decisions, and failures at the layer that has enough context to decide their outcome.
- A lower layer that returns a recoverable `Result` normally does not also log the same failure; the owning operation boundary records it once.
- Avoid function-entry noise, successful per-item/per-frame logs, and duplicate propagation logs.

### 56. Write bounded structured diagnostics under .cache

- Engine and host diagnostics never write to stdout.
- Core writes one bounded `roxell.log.v1` JSON object per line to stderr and to the installed borrowed sink.
- Each record includes a process-local sequence, nonzero Unix-microsecond timestamp, and process-local thread ID so a person or AI can order and correlate records without allocation-heavy context.
- Application composition installs `HostDiagnosticsSession`, which writes `.cache/logs/<process>.jsonl`; the Windows sink also mirrors bounded lines to the attached debugger.
- AI debugging must inspect that JSONL artifact before guessing at a crash cause.
- Do not log secrets, credentials, raw user content, unbounded payloads, or per-frame noise above the appropriate level.
- Flush explicitly before an intentional fatal boundary and during orderly host shutdown.

### 57. Keep stack and dump capture bounded

- Use configurable stack and dump capture through the shared diagnostics provider.
- Keep captures under `.cache`, bound repeated captures, and enable full-memory dumps only for a specific investigation.
- Expected-failure tests need not create dumps unless capture is the behavior under test.
- Retain the diagnostic identity and matching symbols needed to investigate an unresolved failure.

## VIII. Verification and Application

### 58. Fix compiler warnings and run the relevant checks

Fix all compiler warnings. Verify changed C++ contracts with relevant behavior, failure and public-header tests. Update manifests and CMake together; lower policy baselines only when the corresponding debt is removed. See [Development](DEVELOPMENT.md) for commands.

### 59. Apply the standard to new and changed code

Apply this standard to new and changed Roxell code. Keep broad style cleanup within scope and separate from behavior changes.
