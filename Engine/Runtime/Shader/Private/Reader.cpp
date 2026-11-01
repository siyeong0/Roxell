#include "Reader.h"

#include <Assertion.h>
#include <ByteOperations.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utf8.h>

namespace rox::shader
{
	using namespace core;

	Reader::Reader(Span<const Byte> bytes)
		: m_Cursor(bytes)
	{
	}

	uint32 Reader::U32()
	{
		if (!m_Cursor.CanRead(4))
		{
			m_bValid = false;
			return 0;
		}

		return m_Cursor.ReadU32LE();
	}

	uint64 Reader::U64()
	{
		if (!m_Cursor.CanRead(8))
		{
			m_bValid = false;
			return 0;
		}

		return m_Cursor.ReadU64LE();
	}

	Span<const Byte> Reader::Raw(usize count)
	{
		if (!m_Cursor.CanRead(count))
		{
			m_bValid = false;
			return {};
		}

		return m_Cursor.ReadBytes(count);
	}

	void Reader::Hash(Hash256& outHash)
	{
		Span<const Byte> bytes = Raw(32);
		if (bytes.Size() == 32)
		{
			CopyBytes(outHash.Bytes.data(), bytes.Data(), 32);
		}
	}

	void Reader::Text(String& outText, uint32 limit)
	{
		uint32 count = U32();
		if (!m_bValid || count > limit || !m_Cursor.CanRead(count))
		{
			m_bValid = false;
			return;
		}

		Span<const Byte> bytes = Raw(count);
		StringView text(reinterpret_cast<const char*>(bytes.Data()), count);
		if (!IsValidUtf8(text))
		{
			m_bValid = false;
			return;
		}
		if (!outText.Assign(text))
		{
			m_bValid = false;
			m_bAllocationFailure = true;
		}
	}

	bool Reader::Count(uint32 count, uint64& total, uint32 maximum, usize minimum)
	{
		ASSERT(minimum != 0, "Shader wire record counts require a nonzero minimum encoded extent.");
		if (!m_bValid || count > maximum || count > maximum - total || count > m_Cursor.GetRemaining() / minimum)
		{
			m_bValid = false;
			return false;
		}
		total += count;
		return true;
	}

	bool Reader::CountLayouts(uint32 count, uint32 maximum)
	{
		return Count(count, m_TotalLayouts, maximum, 56);
	}

	bool Reader::CountBindings(uint32 count, uint32 maximum)
	{
		return Count(count, m_TotalBindings, maximum, 45);
	}

	bool Reader::CountFields(uint32 count, uint32 maximum)
	{
		return Count(count, m_TotalFields, maximum, 69);
	}
} // namespace rox::shader
