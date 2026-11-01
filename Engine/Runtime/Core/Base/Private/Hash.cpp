#include <Hash.h>
#include <StringView.hpp>
#include <Span.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <Sha256.h>

#include <Assertion.h>

#include <algorithm>
#include <bit>
#include <functional>

namespace rox::core
{
	namespace
	{
		uint8 hexValue(char character) noexcept
		{
			if (character >= '0' && character <= '9')
			{
				return static_cast<uint8>(character - '0');
			}

			if (character >= 'a' && character <= 'f')
			{
				return static_cast<uint8>(10 + character - 'a');
			}

			if (character >= 'A' && character <= 'F')
			{
				return static_cast<uint8>(10 + character - 'A');
			}

			return 0xff;
		}
	} // namespace

	bool Hash256::IsValid() const noexcept
	{
		return core::AnyOf(Bytes.begin(), Bytes.end(),
		                   [](uint8 value)
		                   {
			                   return value != 0;
		                   });
	}

	Hash256 ComputeSha256(Span<const Byte> data) noexcept
	{
		Sha256 state;
		state.Update(data);
		return state.Finalize();
	}

	String ToString(const Hash256& hash)
	{
		static constexpr char HEX_DIGITS[] = "0123456789abcdef";
		char text[64] = {};

		for (usize index = 0; index < hash.Bytes.size(); ++index)
		{
			text[index * 2] = HEX_DIGITS[hash.Bytes[index] >> 4];
			text[index * 2 + 1] = HEX_DIGITS[hash.Bytes[index] & 0x0f];
		}

		return String(StringView(text, sizeof(text)));
	}

	bool TryParseHash256(StringView text, Hash256* outHash) noexcept
	{
		ASSERT(outHash != nullptr, "Hash output must not be null.");

		if (text.size() != 64)
		{
			return false;
		}

		Hash256 parsed;

		for (usize index = 0; index < parsed.Bytes.size(); ++index)
		{
			const uint8 high = hexValue(text[index * 2]);
			const uint8 low = hexValue(text[index * 2 + 1]);

			if (high == 0xff || low == 0xff)
			{
				return false;
			}

			parsed.Bytes[index] = static_cast<uint8>((high << 4) | low);
		}

		*outHash = parsed;

		return true;
	}

	usize Hash256Hash::operator()(const Hash256& hash) const noexcept
	{
		usize result = 0;

		for (const uint8 value : hash.Bytes)
		{
			result ^= static_cast<usize>(value) + static_cast<usize>(0x9e3779b97f4a7c15ull) +
			          (result << 6) + (result >> 2);
		}

		return result;
	}
} // namespace rox::core
