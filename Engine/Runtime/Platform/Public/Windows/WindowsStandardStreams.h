#pragma once

#include <Cancellation.h>
#include <StandardStreams.h>
#include <Types.h>
#include <Vocabulary.hpp>

namespace rox::os
{
	class WindowsStandardStreams final : public IStandardStreams
	{
	public:
		WindowsStandardStreams() noexcept = default;
		~WindowsStandardStreams() noexcept override;
		WindowsStandardStreams(const WindowsStandardStreams&) = delete;
		WindowsStandardStreams& operator=(const WindowsStandardStreams&) = delete;

		// Duplicates current-process stdin/stdout without inheriting or closing original handles.
		// Only byte-mode pipes are supported; console, files, message/nonblocking pipes are refused.
		[[nodiscard]] static core::Result<StandardStreamsOwner> Create(core::MemoryResource& resource, StandardStreamsOptions options = {});

		core::Result<void> ReadSome(rox::core::usize maximumBytes, core::MemoryResource& resultResource, core::CancellationToken cancellation = {}) override;
		core::Result<void> Write(core::Span<const core::Byte> bytes, core::CancellationToken cancellation = {}) override;

		core::Result<void> Poll() override;
		core::Result<rox::core::Optional<StandardReadResult>> TakeRead() override;
		core::Result<rox::core::Optional<StandardWriteResult>> TakeWrite() override;

		void RequestClose() noexcept override;
		core::Result<bool> PollClose() override;

		[[nodiscard]] bool IsClosed() const noexcept override;

	private:
		class State;

		core::MemoryResource* m_Resource = nullptr;
		State* m_State = nullptr;
	};
} // namespace rox::os
