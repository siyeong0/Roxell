#pragma once

#include <IFileTailReader.h>
#include <MemoryResource.h>
#include <StringView.hpp>
#include <Types.h>

namespace rox::os
{
	struct WindowsFileTailReaderOptions final
	{
		rox::core::usize MaximumBytesPerRead = DEFAULT_FILE_TAIL_READ_BYTES;
		rox::core::usize BackfillBytes = DEFAULT_FILE_TAIL_BACKFILL_BYTES;
	};

	// TryCreate reports refusal without allocating an Error; Create provides owned diagnostics.
	enum class EFileTailCreateResult : core::uint8
	{
		Created,
		// File is absent; retry creation.
		Missing,
		AccessDenied,
		// Empty, non-UTF-8, over-long, or containing a null character.
		InvalidPath,
		InvalidOptions,
		NativeFailure,
		Allocation
	};

	// Owner-thread only; creation, reading, probing, moving and destruction use the same thread.
	// Identity queries use the retained handle and do not start a worker.
	class WindowsFileTailReader final : public IFileTailReader
	{
	public:
		WindowsFileTailReader() noexcept = default;
		~WindowsFileTailReader() noexcept override;

		WindowsFileTailReader(const WindowsFileTailReader&) = delete;
		WindowsFileTailReader& operator=(const WindowsFileTailReader&) = delete;
		WindowsFileTailReader(WindowsFileTailReader&& other) noexcept;
		WindowsFileTailReader& operator=(WindowsFileTailReader&& other) noexcept;

		// Validates and opens before allocating state; refusals do not allocate diagnostics.
		// outReader changes only on Created.
		[[nodiscard]] static EFileTailCreateResult TryCreate(core::MemoryResource& resource, core::StringView pathUtf8, WindowsFileTailReaderOptions options, WindowsFileTailReader* outReader) noexcept;

		// Returns owned diagnostics on refusal.
		[[nodiscard]] static core::Result<WindowsFileTailReader> Create(core::MemoryResource& resource, core::StringView pathUtf8, WindowsFileTailReaderOptions options = {});

		[[nodiscard]] core::Result<core::Span<const core::Byte>> Read() override;
		void RequestIdentityProbe() override;
		[[nodiscard]] EFileTailIdentity ReadIdentity() const noexcept override;

		// Applies Replaced on the owner thread; Read also applies it before reading.
		void ApplyPublishedIdentity();

		[[nodiscard]] bool BeganMidFile() const noexcept override;
		[[nodiscard]] bool HasPendingBackfill() const noexcept override;
		[[nodiscard]] bool IsReadPending() const noexcept override;

		[[nodiscard]] bool IsValid() const noexcept;

		// True when the read limit left unread bytes. The next Read continues.
		[[nodiscard]] bool IsBacklogged() const noexcept;

	private:
		// Hides native handles and overlapped I/O state.
		class State;

		WindowsFileTailReader(core::MemoryResource& resource, State* state) noexcept;

	private:
		core::MemoryResource* m_Resource = nullptr;
		State* m_State = nullptr;
	};

	// Returns the same diagnostic ID as Create without allocating an Error.
	[[nodiscard]] core::StringView GetFileTailCreateDiagnosticID(EFileTailCreateResult result) noexcept;
} // namespace rox::os
