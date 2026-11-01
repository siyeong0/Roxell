# Platform

Platform provides operating system services used by the engine. Its code builds as the `Runtime-Platform` static library and depends on Core.

| Area | Contents |
| --- | --- |
| Windows and input | Windows, text input, accessibility, and gamepads |
| Files and processes | Filesystems, file watching, processes, and standard streams |
| Execution | Clocks and native thread backends |
| System services | Dynamic libraries, logging, diagnostics, and random bytes |
| Terminal | Interactive terminal sessions |

Link `Runtime-Platform` and include the headers for the services you use. Services and their memory resources must outlive their operations and returned objects.

Windows implementations use the Windows SDK libraries, including ICU. Other platforms currently provide a POSIX thread backend; the remaining services do not have equivalent native implementations here.
