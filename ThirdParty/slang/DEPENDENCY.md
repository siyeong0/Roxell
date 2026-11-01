# Slang

ShaderCompiler uses Slang 2026.18 under Apache-2.0 with the LLVM exception.
CMake downloads the official Windows x64 or Linux x64 release and verifies its
SHA-256. The version, archive URL and hash are defined in `CMakeLists.txt`.
The SDK retains its license and bundled third-party notices.

Set `ROXELL_SLANG_ROOT` to an extracted SDK for an offline build.
ShaderCompiler hashes the actual SDK headers and runtime libraries for its
build identity, including when an offline SDK is supplied.
`ThirdParty-Slang` remains private to ShaderCompiler; executable consumers
use `roxell_copy_slang_runtime` to stage its runtime libraries.
