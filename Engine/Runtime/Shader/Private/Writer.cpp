#include "Writer.h"

#include <Assertion.h>
#include <ByteOperations.hpp>
#include <ByteWriter.h>
#include <TextPrimitives.hpp>
#include <Types.h>

namespace rox::shader
{
	using namespace core;

	Writer::Writer(MemoryResource& resource, bool bHashOnly)
		: m_Bytes(resource)
		, m_bHashOnly(bHashOnly)
	{
	}

	void Writer::Raw(const void* source, usize count)
	{
		ASSERT(count == 0 || source != nullptr, "Nonempty shader wire writes require source bytes.");
		if (!m_bValid || count > 64u * 1024u * 1024u - m_ByteCount)
		{
			m_bValid = false;
			return;
		}
		m_ByteCount += count;
		if (m_bHashOnly)
		{
			m_Hash.Update({static_cast<const Byte*>(source), count});
			return;
		}
		if (!m_Bytes.Grow(m_ByteCount))
		{
			m_bValid = false;
			return;
		}

		const usize previousSize = m_Bytes.Size();
		if (!m_Bytes.ResizeUninitialized(previousSize + count))
		{
			m_bValid = false;
			return;
		}
		if (count != 0)
		{
			CopyBytes(m_Bytes.Data() + previousSize, source, count);
		}
	}

	Hash256 Writer::GetHash() const noexcept
	{
		return m_bHashOnly ? m_Hash.Finalize() : ComputeSha256({m_Bytes.Data(), m_Bytes.Size()});
	}

	void Writer::U32(uint32 value)
	{
		Byte bytes[4];
		ByteWriter writer({bytes, 4});
		writer.WriteU32LE(value);
		Raw(bytes, 4);
	}

	void Writer::U64(uint64 value)
	{
		Byte bytes[8];
		ByteWriter writer({bytes, 8});
		writer.WriteU64LE(value);
		Raw(bytes, 8);
	}

	void Writer::Text(StringView text)
	{
		U32(static_cast<uint32>(text.Size()));
		Raw(text.Data(), text.Size());
	}

	void Writer::Hash(const Hash256& hash)
	{
		Raw(hash.Bytes.data(), hash.Bytes.size());
	}
} // namespace rox::shader
