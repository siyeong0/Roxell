#include <ByteReader.h>
#include <Types.h>
#include <Assertion.h>

namespace rox::core
{
	uint8 ByteReader::ReadU8() noexcept
	{
		return static_cast<uint8>(readUnsigned(1, true));
	}

	uint16 ByteReader::ReadU16LE() noexcept
	{
		return static_cast<uint16>(readUnsigned(2, true));
	}

	uint16 ByteReader::ReadU16BE() noexcept
	{
		return static_cast<uint16>(readUnsigned(2, false));
	}

	uint32 ByteReader::ReadU32LE() noexcept
	{
		return static_cast<uint32>(readUnsigned(4, true));
	}

	uint32 ByteReader::ReadU32BE() noexcept
	{
		return static_cast<uint32>(readUnsigned(4, false));
	}

	uint64 ByteReader::ReadU64LE() noexcept
	{
		return readUnsigned(8, true);
	}

	uint64 ByteReader::ReadU64BE() noexcept
	{
		return readUnsigned(8, false);
	}

	uint64 ByteReader::readUnsigned(const usize byteCount, const bool bLittleEndian) noexcept
	{
		ASSERT(CanRead(byteCount), "ByteReader requires enough remaining bytes.");

		uint64 value = 0;

		for (usize index = 0; index < byteCount; ++index)
		{
			const usize shift = (bLittleEndian ? index : byteCount - index - 1) * 8;
			value |= static_cast<uint64>(m_Bytes[m_Offset + index]) << shift;
		}

		m_Offset += byteCount;

		return value;
	}

	Span<const Byte> ByteReader::ReadBytes(const usize byteCount) noexcept
	{
		ASSERT(CanRead(byteCount), "ByteReader requires enough remaining bytes.");

		const Byte* data = m_Bytes.Data() == nullptr ? nullptr : m_Bytes.Data() + m_Offset;
		m_Offset += byteCount;

		return {data, byteCount};
	}
}
