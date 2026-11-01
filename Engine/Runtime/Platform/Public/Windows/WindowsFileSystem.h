#pragma once

#include <FileSystem.h>
#include <Cancellation.h>
#include <CoreString.hpp>
#include <Result.hpp>
#include <StringView.hpp>

namespace rox::os
{
	// Paths are UTF-8, absolute or relative to the working directory. The parent directory must exist.
	[[nodiscard]] core::Result<core::Array<core::Byte>> ReadWindowsFile(core::StringView path, core::uint64 maximumBytes, core::MemoryResource& resource);
	[[nodiscard]] core::Result<void> WriteWindowsFileAtomically(core::StringView path, core::Span<const core::Byte> bytes, core::MemoryResource& resource);
	[[nodiscard]] core::Result<core::uint64> GetWindowsFileWriteStamp(core::StringView path, core::MemoryResource& resource);

	// Read-only absolute, weakly-canonical component containment, including equality.
	// Resolves existing link prefixes; missing descendants need not exist. Both inputs
	// must be nonempty UTF-8 without NUL. Does not defend concurrent link replacement.
	[[nodiscard]] core::Result<bool> IsWindowsPathWithinRoot(rox::core::StringView rootPathUtf8, rox::core::StringView candidatePathUtf8);

	// Opens an existing root with normal I/O rights; does not create directories.
	[[nodiscard]] core::Result<FileSystemOwner> OpenWindowsFileSystem(core::MemoryResource& resource, rox::core::StringView rootPathUtf8);

	[[nodiscard]] core::Result<FileSystemOwner> CreateWindowsFileSystem(core::MemoryResource& resource, rox::core::StringView rootPathUtf8);

	// Bounded physical directory inventory. Rejects links/reparse points in the root and descendants.
	// Relative UTF-8 names use '/'; resource owns every returned string. Does not lock concurrent writers.
	[[nodiscard]] core::Result<core::Array<core::String>> ReadWindowsDirectoryFiles(
		core::StringView root,
		core::MemoryResource& memory,
		core::uint32 maximumEntries = 4096,
		core::uint32 maximumDepth = 32,
		core::CancellationToken = {});
}
