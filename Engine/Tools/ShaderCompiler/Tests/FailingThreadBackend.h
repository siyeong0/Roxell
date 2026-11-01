#pragma once

#include <IThreadBackend.h>
#include <Types.h>

namespace rox::test
{
	class FailingThreadBackend final : public core::IThreadBackend
	{
	public:
		FailingThreadBackend(core::IThreadBackend& backend, core::uint32 failureIndex) noexcept;
		[[nodiscard]] core::NativeThreadStartResult Start(core::NativeThreadEntry& entry) noexcept override;
		void Join(core::NativeThread thread) noexcept override;
		[[nodiscard]] core::ThreadId CurrentThreadId() const noexcept override;
		[[nodiscard]] core::uint32 GetJoinedCount() const noexcept;

	private:
		core::IThreadBackend& m_Backend;
		core::uint32 m_FailureIndex;
		core::uint32 m_StartCount = 0;
		core::uint32 m_JoinedCount = 0;
	};

} // namespace rox::test
