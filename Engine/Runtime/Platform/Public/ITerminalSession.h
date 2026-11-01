#pragma once

#include <Array.hpp>
#include <Result.hpp>
#include <Span.hpp>
#include <StringView.hpp>

namespace rox::os
{
	struct TerminalSize final
	{
		core::uint32 Columns = 80;
		core::uint32 Rows = 24;
	};

	struct TerminalDesc final
	{
		core::StringView ExecutablePath;
		core::Span<const core::StringView> Arguments;
		core::StringView WorkingDirectoryPath;

		TerminalSize Size;

		core::uint32 MaximumInputBytes = 65'536;
		core::uint32 MaximumOutputBytes = 1'048'576;
		core::uint32 MaximumPollBytes = 65'536;
		core::uint32 MaximumQueuedCommands = 32;
	};

	struct TerminalOutput final
	{
		explicit TerminalOutput(core::MemoryResource& memory) noexcept
			: Bytes(memory)
		{
		}

		core::Array<core::Byte> Bytes;

		bool bExited = false;
		bool bClosing = false;

		// Workers are finished and no output remains queued after this batch.
		bool bClosed = false;

		core::uint32 ExitCode = 0;

		core::Error StreamFailure;
	};

	// Single caller thread; workers own native I/O. The allocation resource outlives the owner.
	class ITerminalSession
	{
	public:
		virtual ~ITerminalSession() = default;

		// Whole-batch admission; exhausted queues return Conflict without consuming bytes.
		virtual core::Result<void> Write(core::Span<const core::Byte> bytes) = 0;
		virtual core::Result<void> Resize(TerminalSize size) = 0;

		// Raw ordered UTF-8/VT bytes; no decoding, dropped bytes or implicit newline translation.
		virtual core::Result<TerminalOutput> Poll(core::MemoryResource& memory) = 0;
		virtual void BeginClose() noexcept = 0;

		// Native workers are finished; Poll may still have final queued output to deliver.
		virtual bool IsClosed() const noexcept = 0;
	};
}
