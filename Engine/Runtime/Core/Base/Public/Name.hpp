#pragma once

#include <CoreString.hpp>
#include <StringView.hpp>
#include <Types.h>
#include <Hashing.hpp>
#include <compare>

namespace rox::core
{
	class NamePool;

	// The originating pool outlives every use of this name or its view. Hashes are not persisted identities.
	class Name final
	{
		friend class NamePool;

	public:
		constexpr Name() noexcept = default;

		StringView View() const noexcept { return m_Text; }
		bool IsEmpty() const noexcept { return m_Text.IsEmpty(); }
		uint64 GetHash() const noexcept { return m_Hash; }

		bool operator==(Name right) const noexcept
		{
			return m_Hash == right.m_Hash && m_Text.Size() == right.m_Text.Size() &&
			       (m_Text.Data() == right.m_Text.Data() || m_Text == right.m_Text);
		}

		auto operator<=>(Name right) const noexcept
		{
			return static_cast<StringView>(m_Text) <=> static_cast<StringView>(right.m_Text);
		}

	private:
		explicit Name(StringView text) noexcept
		    : m_Text(text)
		    , m_Hash(HashBytes(text.Data(), text.Size()))
		{
		}

	private:
		StringView m_Text;

		uint64 m_Hash = 0;
	};

	template <> struct DefaultHash<Name>
	{
		uint64 operator()(Name name) const noexcept { return name.GetHash(); }
	};

}
