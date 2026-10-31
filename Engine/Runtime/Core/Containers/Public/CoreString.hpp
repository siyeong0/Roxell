#pragma once

#include <SmallVector.hpp>
#include <CompilerAttributes.h>
#include <TextPrimitives.hpp>
#include <Vocabulary.hpp>
#include <ComparisonTypes.hpp>
#include <SystemError.hpp>
#include <Types.h>
#include <TypeTraits.hpp>
#include <SystemMemoryResource.h>
#include <StringView.hpp>

#include <charconv>
#include <compare>
#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace rox::core
{
	// The resource must outlive the string. Default construction uses the bootstrap System resource.
	// Every successful mutation invalidates views of its contents. Moving inline text changes its address.
	class String
	{
		friend class StringBuilder;

	public:
		static constexpr rox::core::usize INLINE_CAPACITY = 31;

		String() noexcept
		    : m_Storage(SystemMemoryResource::Get())
		{
		}

		explicit String(MemoryResource& resource) noexcept;
		explicit String(StringView text) noexcept;
		String(StringView text, MemoryResource& resource) noexcept;

		// Pointer and length, mirroring StringView. Byte-buffer boundaries hand text over in this shape.
		String(const char* text, rox::core::usize byteSize) noexcept;
		String(const char* text, rox::core::usize byteSize, MemoryResource& resource) noexcept;

		// Fill. The character must be ASCII: repeating a continuation or lead byte would not be valid UTF-8.
		String(rox::core::usize count, char character) noexcept;
		String(rox::core::usize count, char character, MemoryResource& resource) noexcept;

		// Character arrays use the bootstrap resource and the same UTF-8 precondition as explicit views.
		template <rox::core::usize Size>
		String(const char (&text)[Size]) noexcept
		    : String(StringView(text))
		{
		}

		String(const String& other) noexcept = default;
		String(String&& other) noexcept = default;
		~String() = default;

		String& operator=(const String& other) noexcept;
		String& operator=(String&& other) noexcept = default;

		[[nodiscard]] const char* Data() const noexcept
		{
			return CStr();
		}
		[[nodiscard]] const char* CStr() const noexcept
		{
			ASSERT(m_Storage.IsEmpty() || m_Storage.Data()[m_Storage.Size() - 1] == '\0',
			       "String storage must remain null-terminated.");
			return m_Storage.IsEmpty() ? EMPTY_STRING : m_Storage.Data();
		}
		[[nodiscard]] rox::core::usize Size() const noexcept
		{
			return m_Storage.IsEmpty() ? 0 : m_Storage.Size() - 1;
		}
		[[nodiscard]] rox::core::usize Capacity() const noexcept
		{
			return m_Storage.Capacity() == 0 ? 0 : m_Storage.Capacity() - 1;
		}
		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return Size() == 0;
		}
		[[nodiscard]] MemoryResource& GetResource() const noexcept
		{
			return m_Storage.GetResource();
		}
		[[nodiscard]] StringView View() const noexcept
		{
			return {CStr(), Size()};
		}

		[[nodiscard]] char operator[](const rox::core::usize index) const noexcept
		{
			ASSERT(index < Size(), "String index must be within the stored text.");
			return CStr()[index];
		}

		// The range excludes the null terminator, so it matches Size() and StringView iteration.
		[[nodiscard]] const char* begin() const noexcept
		{
			return CStr();
		}
		[[nodiscard]] const char* end() const noexcept
		{
			return CStr() + Size();
		}

		// Both conversions are borrows of the current contents, so they are invalidated by the next mutation.
		[[nodiscard]] operator StringView() const noexcept;
		[[nodiscard]] operator std::string_view() const noexcept;

		[[nodiscard]] bool Assign(StringView text) noexcept;
		void AssignRequired(StringView text) noexcept;
		// Only an owning String carries a previously established UTF-8 invariant. Keep these exact-type
		// templates so literals continue to select StringView without competing implicit conversions.
		template <typename TText>
		    requires IsSame<RemoveCvref<TText>, String>
		[[nodiscard]] bool Assign(const TText& text) noexcept { return tryAssignValidated(text.View()); }

		template <typename TText>
		    requires IsSame<RemoveCvref<TText>, String>
		void AssignRequired(const TText& text) noexcept { assignValidated(text.View()); }

		template <typename TText>
		    requires IsSame<RemoveCvref<TText>, String>
		[[nodiscard]] bool Append(const TText& text) noexcept
		{
			return canAppend(text.Size()) && tryAppendValidated(text.View());
		}

		template <typename TText>
		    requires IsSame<RemoveCvref<TText>, String>
		void AppendRequired(const TText& text) noexcept
		{
			ASSERT(canAppend(text.Size()), "String append must fit the representable byte range.");
			appendValidated(text.View());
		}

		[[nodiscard]] ROXELL_FORCE_INLINE bool Append(const StringView text) noexcept
		{
			if (text.Size() == 1 && static_cast<unsigned char>(text[0]) < 0x80)
			{
				return Append(text[0]);
			}
			return tryAppendView(text);
		}

		ROXELL_FORCE_INLINE void AppendRequired(const StringView text) noexcept
		{
			if (text.Size() == 1 && static_cast<unsigned char>(text[0]) < 0x80)
			{
				AppendRequired(text[0]);
				return;
			}
			appendView(text);
		}

		// Single character, ASCII only for the same reason as the fill constructor.
		[[nodiscard]] ROXELL_FORCE_INLINE bool Append(const char character) noexcept
		{
			if (static_cast<unsigned char>(character) >= 0x80)
			{
				return false;
			}

			return appendAsciiWithinCapacity(character) ||
			       (canAppend(1) && tryAppendValidated(StringView(&character, 1)));
		}

		ROXELL_FORCE_INLINE void AppendRequired(const char character) noexcept
		{
			ASSERT(static_cast<unsigned char>(character) < 0x80,
			       "String character append requires ASCII to stay well-formed UTF-8.");
			if (!appendAsciiWithinCapacity(character))
			{
				ASSERT(canAppend(1), "String append must fit the representable byte range.");
				appendValidated(StringView(&character, 1));
			}
		}

		// Spelling for AppendRequired: same resource, same assertion on malformed UTF-8 or exhausted storage.
		String& operator+=(StringView text) noexcept;
		template <typename TText>
		    requires IsSame<RemoveCvref<TText>, String>
		String& operator+=(const TText& text) noexcept
		{
			AppendRequired(text);
			return *this;
		}
		// Shortens to byteSize. The cut must not land inside a multi-byte sequence, so the invariant holds.
		void Truncate(rox::core::usize byteSize) noexcept;

		[[nodiscard]] bool Reserve(rox::core::usize byteCapacity) noexcept;
		void ReserveRequired(rox::core::usize byteCapacity) noexcept;
		void Clear() noexcept;
		void Reset() noexcept;

	private:
		[[nodiscard]] bool tryAssignValidated(StringView text) noexcept;
		void assignValidated(StringView text) noexcept;
		[[nodiscard]] bool tryAppendView(StringView text) noexcept;
		void appendView(StringView text) noexcept;

		// Inline capacity always leaves room for the first byte and its terminator.
		[[nodiscard]] ROXELL_FORCE_INLINE bool appendAsciiWithinCapacity(const char character) noexcept
		{
			if (m_Storage.IsEmpty())
			{
				m_Storage.EmplaceRequired(character);
				m_Storage.EmplaceRequired('\0');
				return true;
			}

			if (m_Storage.Size() == m_Storage.Capacity())
			{
				return false;
			}

			m_Storage.Data()[m_Storage.Size() - 1] = character;
			m_Storage.EmplaceRequired('\0');
			return true;
		}

		[[nodiscard]] bool tryAppendValidated(StringView text) noexcept;
		void appendValidated(StringView text) noexcept;
		[[nodiscard]] bool canAppend(rox::core::usize byteCount) const noexcept;
		[[nodiscard]] rox::core::usize growthStorageCapacity(rox::core::usize requiredStorageSize) const noexcept;

	private:
		static constexpr char EMPTY_STRING[] = "";
		SmallVector<char, INLINE_CAPACITY + 1> m_Storage;
	};

	// Deliberately only the same-type overloads. Mixed String/StringView ones look convenient, but combined
	// with the literal constructor above every literal comparison becomes ambiguous: the literal can reach a
	// StringView parameter and a String parameter through one conversion each. String converts to StringView
	// implicitly, so the pair below still covers String/StringView and heterogeneous map lookup.
	[[nodiscard]] bool operator==(const String& left, const String& right) noexcept;

	template <typename TText>
	    requires IsSame<RemoveCvref<TText>, std::string_view>
	[[nodiscard]] bool operator==(const String& left, const TText& right) noexcept
	{
		return left.View() == StringView(right);
	}
	[[nodiscard]] rox::core::StrongOrdering operator<=>(const String& left, const String& right) noexcept;

	// Same-type only, for the reason given above the comparisons: mixed String/StringView overloads make
	// every `string + "literal"` ambiguous. A StringView operand is spelled String(view) or goes through
	// Concat, which is the better tool for more than two parts anyway.
	[[nodiscard]] String operator+(const String& left, const String& right) noexcept;

	// Decimal rendering. Lives here rather than beside the Hash256 and StrongId overloads because Containers
	// is below Base, and this needs nothing from Base.
	template <typename TInteger>
	    requires rox::core::IsIntegral<TInteger> && (!rox::core::IsSame<rox::core::RemoveCv<TInteger>, bool>)
	[[nodiscard]] String ToString(const TInteger value) noexcept
	{
		char text[24] = {};
		const rox::core::ToCharsResult converted = rox::core::ToChars(text, text + sizeof(text), value);
		ASSERT(converted.ec == rox::core::EErrorCondition(), "A 64-bit integer must render into 24 bytes.");
		return String(StringView(text, static_cast<rox::core::usize>(converted.ptr - text)));
	}

	// Joins the parts in order. Every part must be well-formed UTF-8, so this is for assembled text such as
	// paths and messages, not for arbitrary input; use MakeLossyString plus Append for that.
	[[nodiscard]] String Concat(rox::core::InitializerList<StringView> parts) noexcept;
	[[nodiscard]] String Concat(rox::core::InitializerList<StringView> parts, MemoryResource& resource) noexcept;

	// Diagnostic text can carry bytes from untrusted input and is produced on failure paths, so neither
	// malformed UTF-8 nor an exhausted resource may abort. Returns the text when it is well-formed UTF-8 and
	// storage succeeds, a fixed placeholder when it is not, and an empty string when even that cannot be stored.
	[[nodiscard]] String MakeLossyString(StringView text) noexcept;
	[[nodiscard]] String MakeLossyString(StringView text, MemoryResource& resource) noexcept;
} // namespace rox::core
