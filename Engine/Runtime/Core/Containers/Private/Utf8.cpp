#include <Utf8.h>
#include <Types.h>
#include <ByteOperations.hpp>
#include <cstddef>
#include <cstdint>

namespace rox::core
{
	namespace
	{
		// Keep the scalar loop independent of the ASCII fast path's code size.
#if defined(_MSC_VER)
		__declspec(noinline)
#elif defined(__clang__) || defined(__GNUC__)
		__attribute__((noinline))
#endif
		bool validateScalarUtf8(const StringView text) noexcept
		{
			if (text.IsEmpty())
			{
				return true;
			}
			const unsigned char* current = reinterpret_cast<const unsigned char*>(text.Data());
			const unsigned char* const end = current + text.Size();
			while (current != end)
			{
				const uint8 lead = *current++;

				if (lead <= 0x7F)
				{
					const unsigned char* ascii = current - 1;
					while (static_cast<usize>(end - ascii) >= sizeof(uint64))
					{
						uint64 word;
						core::CopyBytes(&word, ascii, sizeof(word));
						if ((word & 0x8080808080808080ull) != 0)
						{
							break;
						}
						ascii += sizeof(word);
						current = ascii;
					}
					continue;
				}

				if (lead < 0xC2)
				{
					return false;
				}
				if (lead <= 0xDF)
				{
					if (current == end || (*current & 0xC0) != 0x80)
					{
						return false;
					}
					++current;
				}
				else if (lead <= 0xEF)
				{
					if (end - current < 2)
					{
						return false;
					}
					const uint8 second = current[0];
					// The first continuation excludes overlong forms and surrogate scalars.
					if ((second & 0xC0) != 0x80 ||
					    (lead == 0xE0 && second < 0xA0) ||
					    (lead == 0xED && second >= 0xA0) ||
					    (current[1] & 0xC0) != 0x80)
					{
						return false;
					}
					current += 2;
				}
				else
				{
					if (lead > 0xF4 || end - current < 3)
					{
						return false;
					}
					const uint8 second = current[0];
					// These bounds restrict four-byte sequences to U+10000 through U+10FFFF.
					if ((second & 0xC0) != 0x80 ||
					    (lead == 0xF0 && second < 0x90) ||
					    (lead == 0xF4 && second >= 0x90) ||
					    (current[1] & 0xC0) != 0x80 ||
					    (current[2] & 0xC0) != 0x80)
					{
						return false;
					}
					current += 3;
				}
			}

			return true;
		}
	}

	bool IsValidUtf8(const StringView text) noexcept
	{
		usize index = 0;
		while (text.Size() - index >= sizeof(uint64))
		{
			uint64 word;
			core::CopyBytes(&word, text.Data() + index, sizeof(word));
			if ((word & 0x8080808080808080ull) != 0)
			{
				break;
			}
			index += sizeof(word);
		}
		return index == text.Size() || validateScalarUtf8(text.Substr(index));
	}
} // namespace rox::core
