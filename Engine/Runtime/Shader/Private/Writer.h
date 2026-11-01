#pragma once

#include <Sha256.h>
#include <ShaderBuilder.h>
#include <Types.h>

namespace rox::shader
{
	class Writer
	{
	public:
		explicit Writer(core::MemoryResource& resource, bool bHashOnly = false);
		void Raw(const void* source, core::usize count);
		void U32(core::uint32 value);
		void U64(core::uint64 value);

		template <class E>
		void Enum(E value) { U32(static_cast<core::uint32>(value)); }

		void Text(core::StringView text);
		void Hash(const core::Hash256& hash);

		core::Array<core::Byte>& GetBytes() noexcept { return m_Bytes; }

		core::Hash256 GetHash() const noexcept;

		core::usize GetByteCount() const noexcept { return m_ByteCount; }
		bool IsValid() const noexcept { return m_bValid; }
		void MarkInvalid() noexcept { m_bValid = false; }

	private:
		core::Array<core::Byte> m_Bytes;
		core::Sha256 m_Hash;
		core::usize m_ByteCount = 0;

		bool m_bHashOnly = false;
		bool m_bValid = true;
	};
} // namespace rox::shader
