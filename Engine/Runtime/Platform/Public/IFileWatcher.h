#pragma once

#include <Result.hpp>
#include <Span.hpp>
#include <StringView.hpp>
#include <Types.h>

#include <Utility.hpp>

namespace rox::os
{
	// Invalid construction bounds fail; exceeding a runtime buffer bound requests a rescan instead.
	constexpr core::uint32 DEFAULT_MAXIMUM_FILE_CHANGE_COUNT = 4'096;
	constexpr core::uint32 MAXIMUM_FILE_CHANGE_COUNT = 65'536;
	constexpr rox::core::usize DEFAULT_FILE_WATCH_BUFFER_BYTES = 65'536;
	constexpr rox::core::usize MAXIMUM_FILE_WATCH_BUFFER_BYTES = 1'048'576;

	// Ordinal order is the secondary sort key of a batch, so the enumerators are ordered deliberately.
	enum class EFileChangeKind : core::uint8
	{
		Created,
		Modified,
		Deleted,
		Renamed
	};

	// Native change records provide no timestamp.
	struct FileChange final
	{
		// Root relative, UTF-8, with forward slashes. For Renamed this is the destination path.
		core::StringView RelativePath;

		// Populated only for Renamed, where it is the source path. Empty for every other kind.
		core::StringView OldRelativePath;
		EFileChangeKind Kind = EFileChangeKind::Created;
	};

	// Batch and path views expire on the next Poll or watcher destruction.
	struct FileChangeBatch final
	{
		core::Span<const FileChange> Changes;

		// The batch is incomplete: the caller rescans the watched tree. Watching continues regardless.
		bool bRescanRequired = false;
	};

	// Recursive watch with owner-thread-only creation, polling and destruction.
	class IFileWatcher
	{
	public:
		virtual ~IFileWatcher() = default;

		// Returns immediately with changes sorted by path bytes and kind, deduplicated within the batch.
		// Failure is terminal; incomplete batches request a rescan while the watch continues.
		[[nodiscard]] virtual core::Result<FileChangeBatch> Poll() = 0;
	};
} // namespace rox::os
