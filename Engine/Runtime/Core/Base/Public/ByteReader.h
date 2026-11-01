#pragma once

#include <Span.hpp>
#include <Types.h>

#include <cstddef>

namespace rox::core
{
	// Storage remains alive and unchanged during reads. Returned byte ranges borrow that storage.
	class ByteReader final
	{
	public:
		explicit ByteReader(Span<const Byte> bytes) noexcept
		    : m_Bytes(bytes)
		{
		}

		ByteReader(const ByteReader&) noexcept = default;
		ByteReader(ByteReader&&) noexcept = default;
		ByteReader& operator=(const ByteReader&) noexcept = default;
		ByteReader& operator=(ByteReader&&) noexcept = default;

		[[nodiscard]] usize GetOffset() const noexcept { return m_Offset; }
		[[nodiscard]] usize GetRemaining() const noexcept { return m_Bytes.Size() - m_Offset; }
		[[nodiscard]] bool CanRead(usize byteCount) const noexcept { return byteCount <= GetRemaining(); }

		[[nodiscard]] uint8 ReadU8() noexcept;
		[[nodiscard]] uint16 ReadU16LE() noexcept;
		[[nodiscard]] uint16 ReadU16BE() noexcept;
		[[nodiscard]] uint32 ReadU32LE() noexcept;
		[[nodiscard]] uint32 ReadU32BE() noexcept;
		[[nodiscard]] uint64 ReadU64LE() noexcept;
		[[nodiscard]] uint64 ReadU64BE() noexcept;
		[[nodiscard]] Span<const Byte> ReadBytes(usize byteCount) noexcept;

		// Checked reads leave the cursor and output unchanged when input is truncated.
		// Output pointers must be non-null. Returned byte spans borrow the input storage.
		[[nodiscard]] bool TryReadU8(uint8* output) noexcept { return tryRead(output, 1, &ByteReader::ReadU8); }
		[[nodiscard]] bool TryReadU16LE(uint16* output) noexcept { return tryRead(output, 2, &ByteReader::ReadU16LE); }
		[[nodiscard]] bool TryReadU16BE(uint16* output) noexcept { return tryRead(output, 2, &ByteReader::ReadU16BE); }
		[[nodiscard]] bool TryReadU32LE(uint32* output) noexcept { return tryRead(output, 4, &ByteReader::ReadU32LE); }
		[[nodiscard]] bool TryReadU32BE(uint32* output) noexcept { return tryRead(output, 4, &ByteReader::ReadU32BE); }
		[[nodiscard]] bool TryReadU64LE(uint64* output) noexcept { return tryRead(output, 8, &ByteReader::ReadU64LE); }
		[[nodiscard]] bool TryReadU64BE(uint64* output) noexcept { return tryRead(output, 8, &ByteReader::ReadU64BE); }
		[[nodiscard]] bool TryReadBytes(usize byteCount, Span<const Byte>* output) noexcept
		{
			ASSERT(output != nullptr, "ByteReader requires a valid output.");

			if (!CanRead(byteCount))
			{
				return false;
			}

			*output = ReadBytes(byteCount);
			return true;
		}

	private:
		template <typename T> bool tryRead(T* output, usize byteCount, T (ByteReader::*read)() noexcept) noexcept
		{
			ASSERT(output != nullptr, "ByteReader requires a valid output.");

			if (!CanRead(byteCount))
			{
				return false;
			}

			*output = (this->*read)();
			return true;
		}

		uint64 readUnsigned(usize byteCount, bool bLittleEndian) noexcept;

	private:
		Span<const Byte> m_Bytes;
		usize m_Offset = 0;
	};
}
