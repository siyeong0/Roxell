#pragma once

#include <Array.hpp>
#include <Assertion.h>
#include <Types.h>
#include <NumericLimits.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace rox::core
{
	// The resource outlives storage. Mutation requires exclusive access; tail padding is always zero.
	class BitArray final
	{
	public:
		explicit BitArray(MemoryResource& resource) noexcept;
		BitArray(const BitArray& other) noexcept;
		BitArray(BitArray&& other) noexcept;
		~BitArray() = default;
		BitArray& operator=(const BitArray& other) noexcept;
		BitArray& operator=(BitArray&& other) noexcept;

		[[nodiscard]] usize Size() const noexcept;
		[[nodiscard]] usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		// Failure leaves size and bits unchanged.
		[[nodiscard]] bool Reserve(usize minimumBits) noexcept;
		[[nodiscard]] bool Resize(usize bits, bool bValue = false) noexcept;
		[[nodiscard]] bool CloneTo(MemoryResource& resource, BitArray* outBits) const noexcept;
		void ReserveRequired(usize minimumBits) noexcept;
		void ResizeRequired(usize bits, bool bValue = false) noexcept;

		[[nodiscard]] bool Test(const usize index) const noexcept
		{
			ASSERT(index < m_Size, "Test index must address a logical bit.");
			return (m_Words[index / BITS_PER_WORD] & (uint64{1} << (index % BITS_PER_WORD))) != 0;
		}

		void Set(const usize index, const bool bValue = true) noexcept
		{
			ASSERT(index < m_Size, "Set index must address a logical bit.");
			const uint64 mask = uint64{1} << (index % BITS_PER_WORD);
			if (bValue)
			{
				m_Words[index / BITS_PER_WORD] |= mask;
			}
			else
			{
				m_Words[index / BITS_PER_WORD] &= ~mask;
			}
		}

		void Flip(const usize index) noexcept
		{
			ASSERT(index < m_Size, "Flip index must address a logical bit.");
			m_Words.Data()[index / BITS_PER_WORD] ^= uint64{1} << (index % BITS_PER_WORD);
		}
		void SetAll() noexcept;
		void ResetAll() noexcept;
		void Clear() noexcept;
		void Reset() noexcept;

		[[nodiscard]] usize Count() const noexcept;
		[[nodiscard]] bool Any() const noexcept;
		[[nodiscard]] bool None() const noexcept;
		[[nodiscard]] bool All() const noexcept;

		// An absent bit, including a start beyond the logical end, returns Size().
		[[nodiscard]] usize FindFirstSet(usize start = 0) const noexcept;
		[[nodiscard]] usize FindFirstClear(usize start = 0) const noexcept;

		BitArray& operator&=(const BitArray& other) noexcept;
		BitArray& operator|=(const BitArray& other) noexcept;
		BitArray& operator^=(const BitArray& other) noexcept;
		[[nodiscard]] bool operator==(const BitArray& other) const noexcept;

	private:
		[[nodiscard]] static usize wordCount(usize bits) noexcept;
		void maskTail() noexcept;
		[[nodiscard]] usize findFirst(usize start, bool bValue) const noexcept;

	private:
		static constexpr usize BITS_PER_WORD = 64;
		static constexpr usize MAXIMUM_BITS = (NumericLimits<usize>::max() / BITS_PER_WORD) * BITS_PER_WORD;

		Array<uint64> m_Words;
		usize m_Size = 0;
	};
}
