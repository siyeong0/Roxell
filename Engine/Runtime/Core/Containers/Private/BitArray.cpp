#include <BitArray.h>
#include <ByteOperations.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include <Assertion.h>

#include <bit>
#include <utility>

namespace rox::core
{
	BitArray::BitArray(MemoryResource& resource) noexcept
	    : m_Words(resource)
	{
	}

	BitArray::BitArray(const BitArray& other) noexcept
	    : m_Words(other.m_Words)
	    , m_Size(other.m_Size)
	{
	}

	BitArray::BitArray(BitArray&& other) noexcept
	    : m_Words(rox::core::Move(other.m_Words))
	    , m_Size(rox::core::Exchange(other.m_Size, 0))
	{
	}

	BitArray& BitArray::operator=(const BitArray& other) noexcept
	{
		if (this != &other)
		{
			m_Words = other.m_Words;
			m_Size = other.m_Size;
		}

		return *this;
	}

	BitArray& BitArray::operator=(BitArray&& other) noexcept
	{
		if (this != &other)
		{
			m_Words = rox::core::Move(other.m_Words);
			m_Size = rox::core::Exchange(other.m_Size, 0);
		}

		return *this;
	}

	rox::core::usize BitArray::Size() const noexcept
	{
		return m_Size;
	}

	rox::core::usize BitArray::Capacity() const noexcept
	{
		return m_Words.Capacity() * BITS_PER_WORD;
	}

	bool BitArray::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	MemoryResource& BitArray::GetResource() const noexcept
	{
		return m_Words.GetResource();
	}

	void BitArray::ReserveRequired(const rox::core::usize minimumBits) noexcept
	{
		ASSERT(minimumBits <= MAXIMUM_BITS, "BitArray::ReserveRequired bit capacity must fit its representable limit.");
		m_Words.ReserveRequired(wordCount(minimumBits));
	}

	void BitArray::ResizeRequired(const rox::core::usize bits, const bool bValue) noexcept
	{
		ASSERT(bits <= MAXIMUM_BITS, "BitArray::ResizeRequired bit size must fit its representable limit.");
		if (!Resize(bits, bValue))
		{
			ReportMemoryFailure({EMemoryFailureReason::AllocationFailed, &GetResource(), GetResource().GetName(), {wordCount(bits) * sizeof(rox::core::uint64), alignof(rox::core::uint64)}});
		}
	}

	bool BitArray::Reserve(const rox::core::usize minimumBits) noexcept
	{
		return minimumBits <= MAXIMUM_BITS && m_Words.Reserve(wordCount(minimumBits));
	}

	bool BitArray::Resize(const rox::core::usize bits, const bool bValue) noexcept
	{
		if (bits > MAXIMUM_BITS)
		{
			return false;
		}
		// True growth writes every new word below, without first zero-initializing it.
		const rox::core::usize words = wordCount(bits);
		const bool bResized = bValue ? m_Words.ResizeUninitialized(words) : m_Words.Resize(words);
		if (!bResized)
		{
			return false;
		}

		if (bits > m_Size && bValue)
		{
			rox::core::usize firstWord = m_Size / BITS_PER_WORD;
			const rox::core::usize firstBit = m_Size % BITS_PER_WORD;

			if (firstBit != 0)
			{
				m_Words[firstWord] |= rox::core::NumericLimits<rox::core::uint64>::max() << firstBit;
				++firstWord;
			}

			if (firstWord < words)
			{
				rox::core::SetBytes(m_Words.Data() + firstWord, 0xff,
				                    (words - firstWord) * sizeof(rox::core::uint64));
			}
		}

		m_Size = bits;
		maskTail();
		return true;
	}

	bool BitArray::CloneTo(MemoryResource& resource, BitArray* const outBits) const noexcept
	{
		ASSERT(outBits != nullptr && &outBits->GetResource() == &resource, "BitArray::CloneTo output must use the requested resource.");
		if (!m_Words.CloneTo(resource, &outBits->m_Words))
		{
			return false;
		}
		outBits->m_Size = m_Size;
		return true;
	}

	void BitArray::SetAll() noexcept
	{
		for (auto& word : m_Words)
		{
			word = rox::core::NumericLimits<rox::core::uint64>::max();
		}
		maskTail();
	}

	void BitArray::ResetAll() noexcept
	{
		for (auto& word : m_Words)
		{
			word = 0;
		}
	}

	void BitArray::Clear() noexcept
	{
		m_Words.Clear();
		m_Size = 0;
	}

	void BitArray::Reset() noexcept
	{
		m_Words.Reset();
		m_Size = 0;
	}

	rox::core::usize BitArray::Count() const noexcept
	{
		rox::core::usize count = 0;

		for (const auto word : m_Words)
		{
			count += static_cast<rox::core::usize>(rox::core::PopCount(word));
		}

		return count;
	}

	bool BitArray::Any() const noexcept
	{
		for (const auto word : m_Words)
		{
			if (word != 0)
			{
				return true;
			}
		}

		return false;
	}

	bool BitArray::None() const noexcept
	{
		return !Any();
	}

	bool BitArray::All() const noexcept
	{
		const rox::core::usize fullWords = m_Size / BITS_PER_WORD;
		for (rox::core::usize index = 0; index < fullWords; ++index)
		{
			if (m_Words[index] != rox::core::NumericLimits<rox::core::uint64>::max())
			{
				return false;
			}
		}
		const rox::core::usize tailBits = m_Size % BITS_PER_WORD;
		return tailBits == 0 || m_Words[fullWords] == (rox::core::uint64{1} << tailBits) - 1;
	}

	rox::core::usize BitArray::FindFirstSet(const rox::core::usize start) const noexcept
	{
		return findFirst(start, true);
	}

	rox::core::usize BitArray::FindFirstClear(const rox::core::usize start) const noexcept
	{
		return findFirst(start, false);
	}

	BitArray& BitArray::operator&=(const BitArray& other) noexcept
	{
		ASSERT(m_Size == other.m_Size, "BitArray::operator&= requires equal bit sizes.");

		for (rox::core::usize index = 0; index < m_Words.Size(); ++index)
		{
			m_Words[index] &= other.m_Words[index];
		}

		return *this;
	}

	BitArray& BitArray::operator|=(const BitArray& other) noexcept
	{
		ASSERT(m_Size == other.m_Size, "BitArray::operator|= requires equal bit sizes.");

		for (rox::core::usize index = 0; index < m_Words.Size(); ++index)
		{
			m_Words[index] |= other.m_Words[index];
		}

		return *this;
	}

	BitArray& BitArray::operator^=(const BitArray& other) noexcept
	{
		ASSERT(m_Size == other.m_Size, "BitArray::operator^= requires equal bit sizes.");

		for (rox::core::usize index = 0; index < m_Words.Size(); ++index)
		{
			m_Words[index] ^= other.m_Words[index];
		}

		return *this;
	}

	bool BitArray::operator==(const BitArray& other) const noexcept
	{
		if (m_Size != other.m_Size)
		{
			return false;
		}

		return m_Size == 0 || rox::core::CompareBytes(m_Words.Data(), other.m_Words.Data(), m_Words.Size() * sizeof(rox::core::uint64)) == 0;
	}

	rox::core::usize BitArray::wordCount(const rox::core::usize bits) noexcept
	{
		return bits / BITS_PER_WORD + static_cast<rox::core::usize>(bits % BITS_PER_WORD != 0);
	}

	void BitArray::maskTail() noexcept
	{
		const rox::core::usize remainingBits = m_Size % BITS_PER_WORD;

		if (remainingBits != 0)
		{
			m_Words[m_Words.Size() - 1] &= (rox::core::uint64{1} << remainingBits) - 1;
		}
	}

	rox::core::usize BitArray::findFirst(const rox::core::usize start, const bool bValue) const noexcept
	{
		if (start >= m_Size)
		{
			return m_Size;
		}

		rox::core::usize wordIndex = start / BITS_PER_WORD;
		rox::core::uint64 allowedBits = rox::core::NumericLimits<rox::core::uint64>::max() << (start % BITS_PER_WORD);

		for (; wordIndex < m_Words.Size(); ++wordIndex)
		{
			const rox::core::uint64 candidates = (bValue ? m_Words[wordIndex] : ~m_Words[wordIndex]) & allowedBits;

			if (candidates != 0)
			{
				const rox::core::usize found =
				    wordIndex * BITS_PER_WORD + static_cast<rox::core::usize>(rox::core::CountTrailingZeros(candidates));
				return found < m_Size ? found : m_Size;
			}
			allowedBits = rox::core::NumericLimits<rox::core::uint64>::max();
		}

		return m_Size;
	}
}
