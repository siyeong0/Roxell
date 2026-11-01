#pragma once

#include <ByteReader.h>
#include <ShaderBuilder.h>
#include <Types.h>

namespace rox::shader
{
	class Reader
	{
	public:
		explicit Reader(core::Span<const core::Byte> bytes);
		core::uint32 U32();
		core::uint64 U64();

		template <class E>
		E Enum() { return static_cast<E>(U32()); }

		core::Span<const core::Byte> Raw(core::usize count);
		void Hash(core::Hash256& outHash);
		void Text(core::String& outText, core::uint32 limit);
		bool Count(core::uint32 count, core::uint64& total, core::uint32 maximum, core::usize minimum);
		bool CountLayouts(core::uint32 count, core::uint32 maximum);
		bool CountBindings(core::uint32 count, core::uint32 maximum);
		bool CountFields(core::uint32 count, core::uint32 maximum);

		const core::ByteReader& GetCursor() const noexcept { return m_Cursor; }
		bool IsValid() const noexcept { return m_bValid; }
		bool HasAllocationFailure() const noexcept { return m_bAllocationFailure; }

	private:
		core::ByteReader m_Cursor;

		bool m_bValid = true;
		bool m_bAllocationFailure = false;

		core::uint64 m_TotalLayouts = 0;
		core::uint64 m_TotalBindings = 0;
		core::uint64 m_TotalFields = 0;
	};
} // namespace rox::shader
