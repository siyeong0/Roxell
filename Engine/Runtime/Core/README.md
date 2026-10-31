# Core

Core provides platform-neutral primitives used by the engine. Its code builds as the `Runtime-Core` static library.

| Directory | Contents |
| --- | --- |
| `Base` | Types, results, IDs, byte streams, hashing, and capture data |
| `Algorithm` | Generic algorithms |
| `Math` | Scalar math, vectors, matrices, geometry, and random sampling |
| `Diagnostics` | Assertions, logging, and tracing |
| `Memory` | Memory resources and ownership types |
| `Containers` | Strings, arrays, maps, sets, and other containers |
| `Async` | Tasks, cancellation, queues, and job scheduling |
| `Schema` | Structured values, schemas, and JSON serialization |

Link `Runtime-Core` and include the headers for the types you use. `Public/Core.h` is an optional convenience header.

Core uses an explicit memory resource for owned allocations. The resource must outlive the objects and allocations it owns. Native platform services are provided by platform-specific code.

JSON serialization uses the bundled nlohmann/json library. Tests are not included yet.
