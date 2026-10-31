#pragma once

#include <cstdint>
#include <Types.h>

namespace rox::core
{
	struct ThreadId final
	{
		rox::core::uint64 Value = 0;
		[[nodiscard]] bool operator==(const ThreadId&) const noexcept = default;
	};

	// One owner retains this token until Join. Copying it does not duplicate native ownership.
	struct NativeThread final
	{
		void* Handle = nullptr;
		ThreadId Id;
	};

	// The entry and its fields stay at the same address and immutable until Join returns.
	struct NativeThreadEntry final
	{
		void (*Function)(void*) noexcept = nullptr;
		void* Context = nullptr;
	};

	enum class EThreadStartStatus
	{
		Started,
		NativeFailure
	};

	struct ThreadStartResult final
	{
		EThreadStartStatus Status = EThreadStartStatus::NativeFailure;
		rox::core::uint32 NativeError = 0;
	};

	struct NativeThreadStartResult final
	{
		NativeThread Thread;
		ThreadStartResult Result;
	};

	class IThreadBackend
	{
	public:
		virtual ~IThreadBackend() = default;

		// Success owns a distinct thread, which may finish before Start returns. Refusal never invokes the entry.
		// Borrows addressable entry storage for native APIs; neither backend nor caller may change it until Join.
		[[nodiscard]] virtual NativeThreadStartResult Start(NativeThreadEntry& entry) noexcept = 0;

		// Requires an owned token from this backend. Waits for termination, publishes writes and closes ownership.
		virtual void Join(NativeThread thread) noexcept = 0;

		// Identifiers may be reused after termination; they are not persistent identities.
		[[nodiscard]] virtual ThreadId CurrentThreadId() const noexcept = 0;
	};
}
