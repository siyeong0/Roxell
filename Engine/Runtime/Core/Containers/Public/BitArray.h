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

		[[nodiscard]] rox::core::usize Size() const noexcept;
		[[nodiscard]] rox::core::usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] MemoryResource& GetResource() const noexcept;

		// Failure leaves size and bits unchanged.
		[[nodiscard]] bool Reserve(rox::core::usize minimumBits) noexcept;
		[[nodiscard]] bool Resize(rox::core::usize bits, bool bValue = false) noexcept;
		[[nodiscard]] bool CloneTo(MemoryResource& resource, BitArray* outBits) const noexcept;
		void ReserveRequired(rox::core::usize minimumBits) noexcept;
		void ResizeRequired(rox::core::usize bits, bool bValue = false) noexcept;

		[[nodiscard]] bool Test(const rox::core::usize index) const noexcept
		{
			ASSERT(index < m_Size, "Test index must address a logical bit.");
			return (m_Words[index / BITS_PER_WORD] & (rox::core::uint64{1} << (index % BITS_PER_WORD))) != 0;
		}

		void Set(const rox::core::usize index, const bool bValue = true) noexcept
		{
			ASSERT(index < m_Size, "Set index must address a logical bit.");
			const rox::core::uint64 mask = rox::core::uint64{1} << (index % BITS_PER_WORD);
			if (bValue)
			{
				m_Words[index / BITS_PER_WORD] |= mask;
			}
			else
			{
				m_Words[index / BITS_PER_WORD] &= ~mask;
			}
		}

		void Flip(const rox::core::usize index) noexcept
		{
			ASSERT(index < m_Size, "Flip index must address a logical bit.");
			m_Words.Data()[index / BITS_PER_WORD] ^= rox::core::uint64{1} << (index % BITS_PER_WORD);
		}
		void SetAll() noexcept;
		void ResetAll() noexcept;
		void Clear() noexcept;
		void Reset() noexcept;

		[[nodiscard]] rox::core::usize Count() const noexcept;
		[[nodiscard]] bool Any() const noexcept;
		[[nodiscard]] bool None() const noexcept;
		[[nodiscard]] bool All() const noexcept;

		// An absent bit, including a start beyond the logical end, returns Size().
		[[nodiscard]] rox::core::usize FindFirstSet(rox::core::usize start = 0) const noexcept;
		[[nodiscard]] rox::core::usize FindFirstClear(rox::core::usize start = 0) const noexcept;

		BitArray& operator&=(const BitArray& other) noexcept;
		BitArray& operator|=(const BitArray& other) noexcept;
		BitArray& operator^=(const BitArray& other) noexcept;
		[[nodiscard]] bool operator==(const BitArray& other) const noexcept;

	private:
		[[nodiscard]] static rox::core::usize wordCount(rox::core::usize bits) noexcept;
		void maskTail() noexcept;
		[[nodiscard]] rox::core::usize findFirst(rox::core::usize start, bool bValue) const noexcept;

	private:
		static constexpr rox::core::usize BITS_PER_WORD = 64;
		static constexpr rox::core::usize MAXIMUM_BITS = (rox::core::NumericLimits<rox::core::usize>::max() / BITS_PER_WORD) * BITS_PER_WORD;

		Array<rox::core::uint64> m_Words;
		rox::core::usize m_Size = 0;
	};
}
