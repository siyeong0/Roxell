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
	    : m_Words(core::Move(other.m_Words))
	    , m_Size(core::Exchange(other.m_Size, 0))
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
			m_Words = core::Move(other.m_Words);
			m_Size = core::Exchange(other.m_Size, 0);
		}

		return *this;
	}

	usize BitArray::Size() const noexcept
	{
		return m_Size;
	}

	usize BitArray::Capacity() const noexcept
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

	void BitArray::ReserveRequired(const usize minimumBits) noexcept
	{
		ASSERT(minimumBits <= MAXIMUM_BITS, "BitArray::ReserveRequired bit capacity must fit its representable limit.");
		m_Words.ReserveRequired(wordCount(minimumBits));
	}

	void BitArray::ResizeRequired(const usize bits, const bool bValue) noexcept
	{
		ASSERT(bits <= MAXIMUM_BITS, "BitArray::ResizeRequired bit size must fit its representable limit.");
		if (!Resize(bits, bValue))
		{
			ReportMemoryFailure({EMemoryFailureReason::AllocationFailed, &GetResource(), GetResource().GetName(), {wordCount(bits) * sizeof(uint64), alignof(uint64)}});
		}
	}

	bool BitArray::Reserve(const usize minimumBits) noexcept
	{
		return minimumBits <= MAXIMUM_BITS && m_Words.Reserve(wordCount(minimumBits));
	}

	bool BitArray::Resize(const usize bits, const bool bValue) noexcept
	{
		if (bits > MAXIMUM_BITS)
		{
			return false;
		}
		// True growth writes every new word below, without first zero-initializing it.
		const usize words = wordCount(bits);
		const bool bResized = bValue ? m_Words.ResizeUninitialized(words) : m_Words.Resize(words);
		if (!bResized)
		{
			return false;
		}

		if (bits > m_Size && bValue)
		{
			usize firstWord = m_Size / BITS_PER_WORD;
			const usize firstBit = m_Size % BITS_PER_WORD;

			if (firstBit != 0)
			{
				m_Words[firstWord] |= NumericLimits<uint64>::max() << firstBit;
				++firstWord;
			}

			if (firstWord < words)
			{
				core::SetBytes(m_Words.Data() + firstWord, 0xff,
				                    (words - firstWord) * sizeof(uint64));
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
			word = NumericLimits<uint64>::max();
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

	usize BitArray::Count() const noexcept
	{
		usize count = 0;

		for (const auto word : m_Words)
		{
			count += static_cast<usize>(core::PopCount(word));
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
		const usize fullWords = m_Size / BITS_PER_WORD;
		for (usize index = 0; index < fullWords; ++index)
		{
			if (m_Words[index] != NumericLimits<uint64>::max())
			{
				return false;
			}
		}
		const usize tailBits = m_Size % BITS_PER_WORD;
		return tailBits == 0 || m_Words[fullWords] == (uint64{1} << tailBits) - 1;
	}

	usize BitArray::FindFirstSet(const usize start) const noexcept
	{
		return findFirst(start, true);
	}

	usize BitArray::FindFirstClear(const usize start) const noexcept
	{
		return findFirst(start, false);
	}

	BitArray& BitArray::operator&=(const BitArray& other) noexcept
	{
		ASSERT(m_Size == other.m_Size, "BitArray::operator&= requires equal bit sizes.");

		for (usize index = 0; index < m_Words.Size(); ++index)
		{
			m_Words[index] &= other.m_Words[index];
		}

		return *this;
	}

	BitArray& BitArray::operator|=(const BitArray& other) noexcept
	{
		ASSERT(m_Size == other.m_Size, "BitArray::operator|= requires equal bit sizes.");

		for (usize index = 0; index < m_Words.Size(); ++index)
		{
			m_Words[index] |= other.m_Words[index];
		}

		return *this;
	}

	BitArray& BitArray::operator^=(const BitArray& other) noexcept
	{
		ASSERT(m_Size == other.m_Size, "BitArray::operator^= requires equal bit sizes.");

		for (usize index = 0; index < m_Words.Size(); ++index)
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

		return m_Size == 0 || core::CompareBytes(m_Words.Data(), other.m_Words.Data(), m_Words.Size() * sizeof(uint64)) == 0;
	}

	usize BitArray::wordCount(const usize bits) noexcept
	{
		return bits / BITS_PER_WORD + static_cast<usize>(bits % BITS_PER_WORD != 0);
	}

	void BitArray::maskTail() noexcept
	{
		const usize remainingBits = m_Size % BITS_PER_WORD;

		if (remainingBits != 0)
		{
			m_Words[m_Words.Size() - 1] &= (uint64{1} << remainingBits) - 1;
		}
	}

	usize BitArray::findFirst(const usize start, const bool bValue) const noexcept
	{
		if (start >= m_Size)
		{
			return m_Size;
		}

		usize wordIndex = start / BITS_PER_WORD;
		uint64 allowedBits = NumericLimits<uint64>::max() << (start % BITS_PER_WORD);

		for (; wordIndex < m_Words.Size(); ++wordIndex)
		{
			const uint64 candidates = (bValue ? m_Words[wordIndex] : ~m_Words[wordIndex]) & allowedBits;

			if (candidates != 0)
			{
				const usize found =
				    wordIndex * BITS_PER_WORD + static_cast<usize>(core::CountTrailingZeros(candidates));
				return found < m_Size ? found : m_Size;
			}
			allowedBits = NumericLimits<uint64>::max();
		}

		return m_Size;
	}
}
