#pragma once

#include <Span.hpp>
#include <Types.h>

#include <cstddef>

namespace rox::core
{
	// The caller retains storage and exclusive access to the bytes written through this cursor.
	class ByteWriter final
	{
	public:
		explicit ByteWriter(Span<Byte> bytes) noexcept
		    : m_Bytes(bytes)
		{
		}

		ByteWriter(const ByteWriter&) noexcept = default;
		ByteWriter(ByteWriter&&) noexcept = default;
		ByteWriter& operator=(const ByteWriter&) noexcept = default;
		ByteWriter& operator=(ByteWriter&&) noexcept = default;

		[[nodiscard]] rox::core::usize GetOffset() const noexcept { return m_Offset; }
		[[nodiscard]] rox::core::usize GetRemaining() const noexcept { return m_Bytes.Size() - m_Offset; }
		[[nodiscard]] bool CanWrite(rox::core::usize byteCount) const noexcept { return byteCount <= GetRemaining(); }

		void WriteU8(uint8 value) noexcept;
		void WriteU16LE(uint16 value) noexcept;
		void WriteU16BE(uint16 value) noexcept;
		void WriteU32LE(uint32 value) noexcept;
		void WriteU32BE(uint32 value) noexcept;
		void WriteU64LE(uint64 value) noexcept;
		void WriteU64BE(uint64 value) noexcept;
		void WriteBytes(Span<const Byte> bytes) noexcept;

	private:
		void writeUnsigned(uint64 value, rox::core::usize byteCount, bool bLittleEndian) noexcept;

	private:
		Span<Byte> m_Bytes;
		rox::core::usize m_Offset = 0;
	};
}
