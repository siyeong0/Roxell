#pragma once

#include <Result.hpp>
#include <Span.hpp>
#include <Types.h>

#include <Utility.hpp>

namespace rox::os
{
	// Read and backfill bounds are validated at construction. Oversized requests fail rather than being clamped.
	constexpr rox::core::usize DEFAULT_FILE_TAIL_READ_BYTES = 262'144;
	constexpr rox::core::usize MAXIMUM_FILE_TAIL_READ_BYTES = 1'048'576;
	constexpr rox::core::usize DEFAULT_FILE_TAIL_BACKFILL_BYTES = 262'144;

	// Only Replaced establishes that the attached file no longer has its original identity.
	// A failed query is Inconclusive and does not authorize retiring the reader.
	enum class EFileTailIdentity : core::uint8
	{
		// No identity probe requested.
		Unknown,
		// The handle still resolves to the name it was opened under, and no deletion is pending.
		Same,
		// The file was deleted or renamed away from its original path.
		Replaced,
		// Query failed; keep reading and retry the probe.
		Inconclusive
	};

	// Owner-thread only; returned byte views remain valid until the next Read or reader destruction.
	class IFileTailReader
	{
	public:
		virtual ~IFileTailReader() = default;

		// Arms or harvests overlapped I/O without waiting; pending reads return an empty view.
		// Applies a published Replaced identity first. Failures and truncation are terminal.
		[[nodiscard]] virtual core::Result<core::Span<const core::Byte>> Read() = 0;

		// Queries identity on the existing handle without opening the path or starting a worker.
		// The answer is available through ReadIdentity when this call returns.
		virtual void RequestIdentityProbe() = 0;

		// Returns the last probe answer, or Unknown before the first probe.
		[[nodiscard]] virtual EFileTailIdentity ReadIdentity() const noexcept = 0;

		// Starts inside an existing file. Line readers must skip to the first separator only in this case.
		[[nodiscard]] virtual bool BeganMidFile() const noexcept = 0;

		// Remains true until reads reach the file end captured at attachment.
		[[nodiscard]] virtual bool HasPendingBackfill() const noexcept = 0;

		// True while an armed read awaits completion.
		[[nodiscard]] virtual bool IsReadPending() const noexcept = 0;
	};
} // namespace rox::os
