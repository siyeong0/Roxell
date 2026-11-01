#pragma once

#include <Hash.h>
#include <Vocabulary.hpp>
#include <Span.hpp>
#include <Types.h>

namespace rox::core
{
	// Thread confined; input bytes are borrowed only during Update. The state performs no allocations.
	class Sha256
	{
	public:
		Sha256() noexcept;

		void Update(Span<const Byte> data) noexcept;

		// Returns the digest of current input without consuming the state; further updates remain valid.
		[[nodiscard]] Hash256 Finalize() const noexcept;

		void Reset() noexcept;

	private:
		StaticArray<uint32, 8> m_State = {};

		StaticArray<uint8, 64> m_Tail = {};

		uint64 m_TotalBytes = 0;
		usize m_TailBytes = 0;
	};
}
