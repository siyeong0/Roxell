#pragma once

#include <Array.hpp>
#include <Cancellation.h>
#include <InterfaceOwner.hpp>
#include <MemoryResource.h>
#include <Result.hpp>
#include <Span.hpp>
#include <Types.h>
#include <Utility.hpp>
#include <Vocabulary.hpp>

namespace rox::os
{
	inline constexpr rox::core::usize DEFAULT_STANDARD_READ_BYTES = 65'536;
	inline constexpr rox::core::usize DEFAULT_STANDARD_WRITE_BYTES = 1'048'576;
	inline constexpr rox::core::usize MAXIMUM_STANDARD_STREAM_BYTES = 67'108'864;

	struct StandardStreamsOptions final
	{
		rox::core::usize MaximumReadBytes = DEFAULT_STANDARD_READ_BYTES;
		rox::core::usize MaximumWriteBytes = DEFAULT_STANDARD_WRITE_BYTES;
	};

	enum class EStandardReadStatus : core::uint8
	{
		Data,
		EndOfStream
	};

	struct StandardReadResult final
	{
		explicit StandardReadResult(core::MemoryResource& resource) noexcept
			: Bytes(resource)
		{
		}

		EStandardReadStatus Status = EStandardReadStatus::Data;
		core::Array<core::Byte> Bytes;
	};

	struct StandardWriteResult final
	{
		rox::core::usize BytesWritten = 0;
	};

	// One owner thread admits, polls, consumes and closes operations; cancellation may be requested elsewhere.
	// Continue polling until pending operations complete, then Take their results before reusing that direction.
	// Workers borrow buffers; only the owner allocates or releases them after native completion is joined.
	// CancellationSource retains its separate thread-safe-resource requirement.
	class IStandardStreams
	{
	public:
		virtual ~IStandardStreams() = default;

		virtual core::Result<void> ReadSome(rox::core::usize maximumBytes, core::MemoryResource& resultResource, core::CancellationToken cancellation = {}) = 0;

		// Copies bytes before returning. After admission, I/O failure/cancellation may leave a
		// prefix at the peer: retire this output stream; do not resend or append another message.
		virtual core::Result<void> Write(core::Span<const core::Byte> bytes, core::CancellationToken cancellation = {}) = 0;

		// Poll failure reports the polling mechanism, not an operation's I/O failure.
		virtual core::Result<void> Poll() = 0;

		// Empty optional means pending. Failure or a populated optional consumes the completion.
		// With no admitted operation Take returns Conflict. TakeRead completion errors use
		// resultResource, which outlives the operation and returned bytes/errors. Admission, polling,
		// no-operation and write errors use the owner resource, which outlives them and this interface.
		virtual core::Result<rox::core::Optional<StandardReadResult>> TakeRead() = 0;
		virtual core::Result<rox::core::Optional<StandardWriteResult>> TakeWrite() = 0;

		virtual void RequestClose() noexcept = 0;

		// False/error retains the owner and every pending buffer/handle. Continue polling; never
		// destroy an unclosed owner. True means all workers joined and duplicated handles closed.
		// Completed results remain available to Take until consumed or the closed owner is reset.
		virtual core::Result<bool> PollClose() = 0;

		[[nodiscard]] virtual bool IsClosed() const noexcept = 0;
	};

	class StandardStreamsOwner final
	{
	public:
		StandardStreamsOwner() noexcept = default;
		~StandardStreamsOwner() noexcept;
		StandardStreamsOwner(const StandardStreamsOwner&) = delete;
		StandardStreamsOwner& operator=(const StandardStreamsOwner&) = delete;
		StandardStreamsOwner(StandardStreamsOwner&& other) noexcept;
		StandardStreamsOwner& operator=(StandardStreamsOwner&& other) noexcept;

		template <typename TStreams, typename... TArguments>
		[[nodiscard]] static StandardStreamsOwner TryCreate(core::MemoryResource& resource, TArguments&&... arguments) noexcept
		{
			StandardStreamsOwner owner;
			owner.m_Owner = core::InterfaceOwner<IStandardStreams>::Create<TStreams>(resource, rox::core::Forward<TArguments>(arguments)...);

			return owner;
		}

		// Reset/destruction of an unclosed service is a lifetime violation, not implicit cancel.
		void Reset() noexcept;
		[[nodiscard]] IStandardStreams* Get() const noexcept;
		[[nodiscard]] core::MemoryResource* GetResource() const noexcept;
		[[nodiscard]] explicit operator bool() const noexcept;
		IStandardStreams* operator->() const noexcept;

	private:
		core::InterfaceOwner<IStandardStreams> m_Owner;
	};
} // namespace rox::os
