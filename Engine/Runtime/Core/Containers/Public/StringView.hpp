#pragma once

#include <Assertion.h>
#include <CharacterTraits.hpp>
#include <TypeTraits.hpp>
#include <TypeVocabulary.hpp>
#include <ComparisonTypes.hpp>
#include <Types.h>
#include <compare>
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace rox::core
{
	// The caller owns the viewed range. A null pointer is valid only when the size is zero.
	class StringView
	{
	public:
		static constexpr rox::core::usize NPOS = std::string_view::npos;

		constexpr StringView() noexcept = default;

		template <typename TText>
		    requires requires(const TText& text) { text.Data(); text.Size(); } && IsSame<decltype(Declval<const TText&>().Data()), const char*>
		explicit constexpr StringView(const TText& text) noexcept : StringView(text.Data(), text.Size()) {}


		template <typename TText>
		    requires requires(const TText& text) { text.data(); text.size(); } && IsSame<decltype(Declval<const TText&>().data()), const char*>
		constexpr StringView(const TText& text) noexcept
		    : StringView(text.data(), text.size())
		{
		}

		template <typename TPointer>
		    requires rox::core::IsSame<TPointer, const char*> || rox::core::IsSame<TPointer, char*>
		constexpr StringView(const TPointer& text) noexcept
		    : StringView(std::string_view(text))
		{
		}

		static constexpr usize npos = NPOS;
		using value_type = char;
		using const_iterator = const char*;

		[[nodiscard]] constexpr const char* data() const noexcept { return Data(); }
		[[nodiscard]] constexpr usize size() const noexcept { return Size(); }
		[[nodiscard]] constexpr usize length() const noexcept { return Size(); }

		[[nodiscard]] constexpr bool empty() const noexcept { return IsEmpty(); }
		[[nodiscard]] constexpr char front() const noexcept { return (*this)[0]; }
		[[nodiscard]] constexpr char back() const noexcept { return (*this)[Size() - 1]; }
		
		[[nodiscard]] constexpr StringView substr(usize offset, usize count = NPOS) const noexcept { return Substr(offset, count); 
		}
		[[nodiscard]] constexpr usize find(char value, usize start = 0) const noexcept { return Find(value, start); }
		[[nodiscard]] constexpr usize find(StringView value, usize start = 0) const noexcept { return Find(value, start); }

		[[nodiscard]] constexpr bool starts_with(StringView value) const noexcept { return StartsWith(value); }
		[[nodiscard]] constexpr bool starts_with(char value) const noexcept { return !IsEmpty() && front() == value; }
		[[nodiscard]] constexpr bool ends_with(StringView value) const noexcept { return EndsWith(value); }
		[[nodiscard]] constexpr bool ends_with(char value) const noexcept { return !IsEmpty() && back() == value; }

		constexpr void remove_prefix(usize count) noexcept { *this = Substr(count); }
		constexpr void remove_suffix(usize count) noexcept
		{
			ASSERT(count <= Size(), "StringView suffix must fit the viewed range.");
			m_Size -= count;
		}
		[[nodiscard]] constexpr usize rfind(char value, usize start = NPOS) const noexcept
		{
			return static_cast<std::string_view>(*this).rfind(value, start);
		}
		[[nodiscard]] constexpr usize rfind(StringView value, usize start = NPOS) const noexcept
		{
			return static_cast<std::string_view>(*this).rfind(static_cast<std::string_view>(value), start);
		}


		constexpr StringView(const char* data, const rox::core::usize size) noexcept
		    : m_Data(data)
		    , m_Size(size)
		{
			ASSERT(m_Data != nullptr || m_Size == 0, "A non-empty StringView requires a valid data pointer.");
		}

		// Implicit in both directions: the two view types are the same pointer and size, and String's
		// comparison and concatenation operators are same-type only, so no mixed candidate can tie with this.
		constexpr StringView(const std::string_view text) noexcept
		    : m_Data(text.data())
		    , m_Size(text.size())
		{
			ASSERT(m_Data != nullptr || m_Size == 0, "A non-empty StringView requires a valid data pointer.");
		}

		template <rox::core::usize Size>
		constexpr StringView(const char (&text)[Size]) noexcept
		    : m_Data(text)
		    , m_Size(Size != 0 && text[Size - 1] == '\0' ? Size - 1 : Size)
		{
		}

		[[nodiscard]] constexpr const char* Data() const noexcept { return m_Data; }

		[[nodiscard]] constexpr rox::core::usize Size() const noexcept { return m_Size; }

		[[nodiscard]] constexpr bool IsEmpty() const noexcept { return m_Size == 0; }

		[[nodiscard]] constexpr StringView Substr(const rox::core::usize offset,
		                                          const rox::core::usize count = NPOS) const noexcept
		{
			ASSERT(offset <= m_Size, "StringView::Substr offset must fit the viewed range.");
			const rox::core::usize remaining = m_Size - offset;

			StringView result = *this;
			result.m_Data = m_Data == nullptr ? nullptr : m_Data + offset;
			result.m_Size = count < remaining ? count : remaining;
			return result;
		}

		[[nodiscard]] constexpr rox::core::usize Find(const char value, const rox::core::usize start = 0) const noexcept
		{
			return static_cast<std::string_view>(*this).find(value, start);
		}

		[[nodiscard]] constexpr rox::core::usize Find(const StringView needle, const rox::core::usize start = 0) const noexcept
		{
			return static_cast<std::string_view>(*this).find(static_cast<std::string_view>(needle), start);
		}

		[[nodiscard]] constexpr bool Contains(const char value) const noexcept { return Find(value) != NPOS; }

		[[nodiscard]] constexpr bool Contains(const StringView needle) const noexcept { return Find(needle) != NPOS; }

		[[nodiscard]] constexpr bool StartsWith(const StringView prefix) const noexcept
		{
			return prefix.m_Size <= m_Size && (prefix.m_Size == 0 || CharacterTraits<char>::compare(m_Data, prefix.m_Data, prefix.m_Size) == 0);
		}

		[[nodiscard]] constexpr bool EndsWith(const StringView suffix) const noexcept
		{
			return suffix.m_Size <= m_Size && (suffix.m_Size == 0 || CharacterTraits<char>::compare(m_Data + (m_Size - suffix.m_Size), suffix.m_Data, suffix.m_Size) == 0);
		}

		[[nodiscard]] constexpr char operator[](const rox::core::usize index) const noexcept
		{
			ASSERT(index < m_Size, "StringView index must be within the viewed range.");
			return m_Data[index];
		}

		// Conversion borrows the same bytes without allocation; an empty null view becomes a default view.
		[[nodiscard]] constexpr operator std::string_view() const noexcept
		{
			return m_Data == nullptr ? std::string_view() : std::string_view(m_Data, m_Size);
		}

		[[nodiscard]] constexpr const char* begin() const noexcept { return m_Data; }

		[[nodiscard]] constexpr const char* end() const noexcept
		{
			return m_Data == nullptr ? nullptr : m_Data + m_Size;
		}

		[[nodiscard]] constexpr usize find_first_of(StringView value, usize start = 0) const noexcept
		{
			return static_cast<std::string_view>(*this).find_first_of(static_cast<std::string_view>(value), start);
		}
		[[nodiscard]] constexpr usize find_first_of(char value, usize start = 0) const noexcept
		{
			return static_cast<std::string_view>(*this).find_first_of(value, start);
		}

		[[nodiscard]] constexpr usize find_first_not_of(StringView value, usize start = 0) const noexcept
		{
			return static_cast<std::string_view>(*this).find_first_not_of(static_cast<std::string_view>(value), start);
		}
		[[nodiscard]] constexpr usize find_first_not_of(char value, usize start = 0) const noexcept
		{
			return static_cast<std::string_view>(*this).find_first_not_of(value, start);
		}

		[[nodiscard]] constexpr usize find_last_of(StringView value, usize start = NPOS) const noexcept
		{
			return static_cast<std::string_view>(*this).find_last_of(static_cast<std::string_view>(value), start);
		}
		[[nodiscard]] constexpr usize find_last_of(char value, usize start = NPOS) const noexcept
		{
			return static_cast<std::string_view>(*this).find_last_of(value, start);
		}

		[[nodiscard]] constexpr usize find_last_not_of(StringView value, usize start = NPOS) const noexcept
		{
			return static_cast<std::string_view>(*this).find_last_not_of(static_cast<std::string_view>(value), start);
		}
		[[nodiscard]] constexpr usize find_last_not_of(char value, usize start = NPOS) const noexcept
		{
			return static_cast<std::string_view>(*this).find_last_not_of(value, start);
		}

		[[nodiscard]] constexpr usize find_first_of(const char* value, usize start, usize count) const noexcept
		{
			return find_first_of(StringView(value, count), start);
		}

		[[nodiscard]] constexpr usize find_first_not_of(const char* value, usize start, usize count) const noexcept
		{
			return find_first_not_of(StringView(value, count), start);
		}

		[[nodiscard]] constexpr usize find_last_of(const char* value, usize start, usize count) const noexcept
		{
			return find_last_of(StringView(value, count), start);
		}

		[[nodiscard]] constexpr usize find_last_not_of(const char* value, usize start, usize count) const noexcept
		{
			return find_last_not_of(StringView(value, count), start);
		}

	private:
		const char* m_Data = nullptr;
		rox::core::usize m_Size = 0;
	};

	[[nodiscard]] constexpr bool operator==(const StringView left, const StringView right) noexcept
	{
		return static_cast<std::string_view>(left) == static_cast<std::string_view>(right);
	}

	template <typename TText>
	    requires IsSame<RemoveCvref<TText>, std::string_view>
	[[nodiscard]] constexpr bool operator==(StringView left, const TText& right) noexcept
	{
		return left == StringView(right);
	}

	// Bytes order as unsigned values, so the ordering matches Unicode code-point order for well-formed UTF-8.
	[[nodiscard]] constexpr rox::core::StrongOrdering operator<=>(const StringView left, const StringView right) noexcept
	{
		return static_cast<std::string_view>(left) <=> static_cast<std::string_view>(right);
	}

	// Heterogeneous text keys preserve C-string probes; explicit views retain embedded NUL bytes.
	template <typename T> [[nodiscard]] constexpr StringView TextKeyView(const T& text) noexcept
	{
		if constexpr (IsArray<T> && IsSame<RemoveCv<RemoveExtent<T>>, char>)
		{
			return StringView(static_cast<const char*>(text));
		}
		else
		{
			return static_cast<StringView>(text);
		}
	}


} // namespace rox::core
