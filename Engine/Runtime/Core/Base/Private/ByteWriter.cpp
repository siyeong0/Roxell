#include <ByteWriter.h>
#include <Types.h>
#include <ByteOperations.hpp>
#include <Assertion.h>

#include <cstring>

namespace rox::core
{
	void ByteWriter::WriteU8(const uint8 value) noexcept
	{
		writeUnsigned(value, 1, true);
	}

	void ByteWriter::WriteU16LE(const uint16 value) noexcept
	{
		writeUnsigned(value, 2, true);
	}

	void ByteWriter::WriteU16BE(const uint16 value) noexcept
	{
		writeUnsigned(value, 2, false);
	}

	void ByteWriter::WriteU32LE(const uint32 value) noexcept
	{
		writeUnsigned(value, 4, true);
	}

	void ByteWriter::WriteU32BE(const uint32 value) noexcept
	{
		writeUnsigned(value, 4, false);
	}

	void ByteWriter::WriteU64LE(const uint64 value) noexcept
	{
		writeUnsigned(value, 8, true);
	}

	void ByteWriter::WriteU64BE(const uint64 value) noexcept
	{
		writeUnsigned(value, 8, false);
	}

	void ByteWriter::writeUnsigned(const uint64 value, const rox::core::usize byteCount, const bool bLittleEndian) noexcept
	{
		ASSERT(CanWrite(byteCount), "ByteWriter requires enough remaining bytes.");

		for (rox::core::usize index = 0; index < byteCount; ++index)
		{
			const rox::core::usize shift = (bLittleEndian ? index : byteCount - index - 1) * 8;
			m_Bytes[m_Offset + index] = static_cast<Byte>(static_cast<uint8>(value >> shift));
		}

		m_Offset += byteCount;
	}

	void ByteWriter::WriteBytes(const Span<const Byte> bytes) noexcept
	{
		ASSERT(CanWrite(bytes.Size()), "ByteWriter requires enough remaining bytes.");

		if (!bytes.IsEmpty())
		{
			rox::core::MoveBytes(m_Bytes.Data() + m_Offset, bytes.Data(), bytes.Size());
			m_Offset += bytes.Size();
		}
	}
}
