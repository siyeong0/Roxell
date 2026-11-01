#pragma once

#include <Array.hpp>
#include <Types.h>
#include <TypeTraits.hpp>

namespace rox::rhi
{
	class SubmissionCompletion final
	{
	public:
		enum class ELane : core::uint8
		{
			Direct,
			Compute,
			Copy
		};

	private:
		struct Record final
		{
			core::uint64 Serial = 0;
			ELane Lane = ELane::Direct;
		};

		static_assert(core::IsTriviallyCopyable<Record>);

	public:
		explicit SubmissionCompletion(core::MemoryResource& resource) noexcept;

		[[nodiscard]] bool TryReserveOne() noexcept;
		[[nodiscard]] bool TryReserveCredits(core::usize count) noexcept;
		void ReleaseCredit() noexcept;

		void RecordSubmission(core::uint64 serial, ELane lane) noexcept;
		[[nodiscard]] core::uint64 CompletedPrefix(core::uint64 directCompleted, core::uint64 computeCompleted, core::uint64 copyCompleted) const noexcept;
		void Advance(core::uint64 directCompleted, core::uint64 computeCompleted, core::uint64 copyCompleted) noexcept;

	private:
		void compact() noexcept;

		core::Array<Record> m_Records;
		core::usize m_Head = 0;
		core::usize m_Reserved = 0;
		core::uint64 m_CompletedPrefix = 0;
	};
}
