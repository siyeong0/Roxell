#include "SubmissionCompletion.h"

#include <ByteOperations.hpp>
#include <NumericLimits.hpp>
#include <Assertion.h>
#include <TextPrimitives.hpp>

namespace rox::rhi
{
	using namespace core;

	SubmissionCompletion::SubmissionCompletion(MemoryResource& resource) noexcept
		: m_Records(resource)
	{
	}

	[[nodiscard]] bool SubmissionCompletion::TryReserveOne() noexcept
	{
		if (m_Head != 0 && m_Head >= m_Records.Size() - m_Head)
		{
			compact();
		}

		if (m_Reserved == NumericLimits<usize>::max() || m_Records.Size() > NumericLimits<usize>::max() - m_Reserved - 1)
		{
			return false;
		}

		return m_Records.Reserve(m_Records.Size() + m_Reserved + 1);
	}

	[[nodiscard]] bool SubmissionCompletion::TryReserveCredits(usize count) noexcept
	{
		if (m_Head != 0)
		{
			compact();
		}
		if (count > NumericLimits<usize>::max() - m_Reserved ||
			m_Records.Size() > NumericLimits<usize>::max() - m_Reserved - count ||
			!m_Records.Reserve(m_Records.Size() + m_Reserved + count))
		{
			return false;
		}
		m_Reserved += count;

		return true;
	}

	void SubmissionCompletion::ReleaseCredit() noexcept
	{
		ASSERT(m_Reserved != 0, "A prepared completion credit is released once.");
		--m_Reserved;
	}

	void SubmissionCompletion::RecordSubmission(uint64 serial, ELane lane) noexcept
	{
		ASSERT(lane <= ELane::Copy, "The submission lane must be valid.");
		ASSERT(m_Records.Size() < m_Records.Capacity(), "A submission record must be reserved before GPU submission.");
		const uint64 previous = m_Records.IsEmpty() ? m_CompletedPrefix : m_Records.Back().Serial;
		ASSERT(serial == previous + 1, "Submission serials must be recorded in issue order.");
		m_Records.AddRequired({ serial, lane });
	}

	[[nodiscard]] uint64 SubmissionCompletion::CompletedPrefix(uint64 directCompleted, uint64 computeCompleted, uint64 copyCompleted) const noexcept
	{
		const uint64 completed[3]{ directCompleted, computeCompleted, copyCompleted };
		uint64 prefix = m_CompletedPrefix;

		for (usize index = m_Head; index < m_Records.Size(); ++index)
		{
			const Record& record = m_Records[index];

			if (record.Serial > completed[static_cast<uint8>(record.Lane)])
			{
				break;
			}
			prefix = record.Serial;
		}

		return prefix;
	}

	void SubmissionCompletion::Advance(uint64 directCompleted, uint64 computeCompleted, uint64 copyCompleted) noexcept
	{
		const uint64 completed[3]{ directCompleted, computeCompleted, copyCompleted };
		while (m_Head < m_Records.Size())
		{
			const Record& record = m_Records[m_Head];

			if (record.Serial > completed[static_cast<uint8>(record.Lane)])
			{
				break;
			}

			m_CompletedPrefix = record.Serial;
			++m_Head;
		}

		if (m_Head == m_Records.Size())
		{
			m_Records.Clear();
			m_Head = 0;
		}
		else if (m_Head >= 1024 && m_Head >= m_Records.Size() - m_Head)
		{
			compact();
		}
	}

	void SubmissionCompletion::compact() noexcept
	{
		const usize remaining = m_Records.Size() - m_Head;
		MoveBytes(m_Records.Data(), m_Records.Data() + m_Head, remaining * sizeof(Record));
		const bool bResized = m_Records.Resize(remaining);
		ASSERT(bResized, "Shrinking submission records cannot fail.");
		m_Head = 0;
	}
}
