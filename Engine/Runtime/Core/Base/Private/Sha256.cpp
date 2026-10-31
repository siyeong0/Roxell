#include <Sha256.h>
#include <Vocabulary.hpp>
#include <Span.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include "Sha256_blocks.h"

#include <Assertion.h>
#include <algorithm>
#include <limits>

namespace rox::core
{
	Sha256::Sha256() noexcept
	{
		Reset();
	}

	void Sha256::Reset() noexcept
	{
		m_State = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
				   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
		m_Tail = {};
		m_TotalBytes = 0;
		m_TailBytes = 0;
	}

	void Sha256::Update(rox::core::Span<const Byte> data) noexcept
	{
		constexpr uint64 MAXIMUM_BYTES = rox::core::NumericLimits<uint64>::max() / 8;
		ASSERT(data.size() <= MAXIMUM_BYTES - m_TotalBytes, "SHA-256 input must fit its 64-bit bit length.");

		m_TotalBytes += static_cast<uint64>(data.size());
		const uint8* bytes = reinterpret_cast<const uint8*>(data.data());
		rox::core::usize offset = 0;

		if (m_TailBytes != 0 && !data.empty())
		{
			const rox::core::usize count = rox::core::Min(data.size(), m_Tail.size() - m_TailBytes);
			rox::core::CopyN(bytes, count, m_Tail.data() + m_TailBytes);
			m_TailBytes += count;
			offset += count;

			if (m_TailBytes == m_Tail.size())
			{
				ProcessBlocks(m_Tail.data(), 1, m_State);
				m_TailBytes = 0;
			}
		}

		const rox::core::usize blocks = (data.size() - offset) / m_Tail.size();
		if (blocks != 0)
		{
			ProcessBlocks(bytes + offset, blocks, m_State);
			offset += blocks * m_Tail.size();
		}

		const rox::core::usize remaining = data.size() - offset;
		if (remaining != 0)
		{
			rox::core::CopyN(bytes + offset, remaining, m_Tail.data());
			m_TailBytes = remaining;
		}
	}

	Hash256 Sha256::Finalize() const noexcept
	{
		rox::core::StaticArray<uint32, 8> state = m_State;
		rox::core::StaticArray<uint8, 128> tail = {};
		rox::core::CopyN(m_Tail.data(), m_TailBytes, tail.data());
		tail[m_TailBytes] = 0x80;
		const rox::core::usize tailBytes = m_TailBytes < 56 ? 64 : 128;
		const uint64 bitLength = m_TotalBytes * 8;

		for (uint32 index = 0; index < 8; ++index)
		{
			tail[tailBytes - 1 - index] = static_cast<uint8>(bitLength >> (index * 8));
		}

		ProcessBlocks(tail.data(), tailBytes / 64, state);

		Hash256 hash;

		for (uint32 index = 0; index < 8; ++index)
		{
			hash.Bytes[index * 4] = static_cast<uint8>(state[index] >> 24);
			hash.Bytes[index * 4 + 1] = static_cast<uint8>(state[index] >> 16);
			hash.Bytes[index * 4 + 2] = static_cast<uint8>(state[index] >> 8);
			hash.Bytes[index * 4 + 3] = static_cast<uint8>(state[index]);
		}

		return hash;
	}
} // namespace rox::core
