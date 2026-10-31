#pragma once

#include <Assertion.h>
#include <TextPrimitives.hpp>
#include <SystemError.hpp>
#include <Types.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <CoreString.hpp>
#include <Utf8.h>

#include <charconv>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// The resource outlives the builder and any taken string. Mutation invalidates content views.
	class StringBuilder final
	{
	public:
		explicit StringBuilder(MemoryResource& resource) noexcept
		    : m_Storage(resource)
		{
		}

		StringBuilder(const StringBuilder& other) noexcept;
		StringBuilder(StringBuilder&& other) noexcept;
		~StringBuilder() = default;
		StringBuilder& operator=(const StringBuilder& other) noexcept;
		StringBuilder& operator=(StringBuilder&& other) noexcept;

		[[nodiscard]] rox::core::usize Size() const noexcept { return m_Storage.IsEmpty() ? 0 : m_Storage.Size() - 1; }
		[[nodiscard]] rox::core::usize Capacity() const noexcept { return m_Storage.Capacity() - 1; }
		[[nodiscard]] bool IsEmpty() const noexcept { return Size() == 0; }

		[[nodiscard]] MemoryResource& GetResource() const noexcept { return m_Storage.GetResource(); }
		[[nodiscard]] StringView View() const noexcept { return {m_Storage.Data(), Size()}; }

		// The first CStr after a mutation writes the reserved terminator and requires exclusive access.
		[[nodiscard]] const char* CStr() const noexcept
		{
			if (m_Storage.IsEmpty())
			{
				return "";
			}
			if (!m_Terminated)
			{
				m_Storage.Data()[Size()] = '\0';
				m_Terminated = true;
			}
			return m_Storage.Data();
		}

		void ReserveRequired(rox::core::usize bytes) noexcept;
		[[nodiscard]] bool Reserve(rox::core::usize bytes) noexcept;

		void Clear() noexcept { m_Storage.Clear(); m_Terminated = true; }
		void Reset() noexcept { m_Storage.Reset(); m_Terminated = true; }

		ROXELL_FORCE_INLINE void AppendRequired(const StringView text) noexcept
		{
			if (text.Size() == 1 && static_cast<unsigned char>(text[0]) < 0x80)
			{
				AppendRequired(text[0]);
				return;
			}
			ASSERT(canAppend(text.Size()), "StringBuilder append must fit the representable byte range.");
			ASSERT(IsValidUtf8(text), "StringBuilder append requires well-formed UTF-8.");
			appendValidated(text);
		}

		ROXELL_FORCE_INLINE void AppendRequired(const char ascii) noexcept
		{
			ASSERT(static_cast<unsigned char>(ascii) < 0x80, "StringBuilder character append requires ASCII.");
			if (!appendAsciiWithinCapacity(ascii))
			{
				ASSERT(canAppend(1), "StringBuilder append must fit the representable byte range.");
				appendValidated(StringView(&ascii, 1));
			}
		}

		// Malformed UTF-8 or exhausted storage leaves the existing text unchanged.
		[[nodiscard]] ROXELL_FORCE_INLINE bool Append(const StringView text) noexcept
		{
			if (text.Size() == 1 && static_cast<unsigned char>(text[0]) < 0x80)
			{
				return Append(text[0]);
			}
			return canAppend(text.Size()) && IsValidUtf8(text) && tryAppendValidated(text);
		}

		[[nodiscard]] ROXELL_FORCE_INLINE bool Append(const char ascii) noexcept
		{
			return static_cast<unsigned char>(ascii) < 0x80 && (appendAsciiWithinCapacity(ascii) || (canAppend(1) && tryAppendValidated(StringView(&ascii, 1))));
		}

		template <typename TText>
		    requires IsSame<RemoveCvref<TText>, String>
		void AppendRequired(const TText& text) noexcept
		{
			ASSERT(canAppend(text.Size()), "StringBuilder append must fit the representable byte range.");
			appendValidated(text.View());
		}

		template <typename TText>
		    requires IsSame<RemoveCvref<TText>, String>
		[[nodiscard]] bool Append(const TText& text) noexcept
		{
			return canAppend(text.Size()) && tryAppendValidated(text.View());
		}

		template <typename TInteger>
		    requires rox::core::IsIntegral<TInteger> && (!rox::core::IsSame<rox::core::RemoveCv<TInteger>, bool>) && (sizeof(TInteger) <= 8)
		[[nodiscard]] bool AppendInteger(const TInteger value) noexcept
		{
			char text[24];
			const rox::core::ToCharsResult converted = rox::core::ToChars(text, text + sizeof(text), value);
			ASSERT(converted.ec == rox::core::EErrorCondition(), "StringBuilder integer rendering must fit 24 bytes.");
			const rox::core::usize size = static_cast<rox::core::usize>(converted.ptr - text);
			if (size == 1)
			{
				return Append(text[0]);
			}
			// ToChars emits only ASCII digits and an optional minus sign.
			return canAppend(size) && tryAppendValidated(StringView(text, size));
		}

		template <typename TInteger>
		    requires rox::core::IsIntegral<TInteger> && (!rox::core::IsSame<rox::core::RemoveCv<TInteger>, bool>) && (sizeof(TInteger) <= 8)
		void AppendIntegerRequired(const TInteger value) noexcept
		{
			char text[24];
			const rox::core::ToCharsResult converted = rox::core::ToChars(text, text + sizeof(text), value);
			ASSERT(converted.ec == rox::core::EErrorCondition(), "StringBuilder integer rendering must fit 24 bytes.");
			const rox::core::usize size = static_cast<rox::core::usize>(converted.ptr - text);
			if (size == 1)
			{
				AppendRequired(text[0]);
				return;
			}
			ASSERT(canAppend(size), "String append must fit the representable byte range.");
			// The generated ASCII is already valid UTF-8.
			appendValidated(StringView(text, size));
		}

		// Transfers the buffer, including reserved empty storage; this builder keeps its resource for reuse.
		[[nodiscard]] String TakeString() noexcept;

	private:
		[[nodiscard]] bool tryAppendValidated(StringView text) noexcept;
		void appendValidated(StringView text) noexcept;
		[[nodiscard]] bool canAppend(rox::core::usize size) const noexcept;
		[[nodiscard]] rox::core::usize growthStorageCapacity(rox::core::usize required) const noexcept;

		[[nodiscard]] ROXELL_FORCE_INLINE bool appendAsciiWithinCapacity(const char ascii) noexcept
		{
			const rox::core::usize storageSize = m_Storage.Size();
			if (storageSize == 0 || storageSize >= m_Storage.Capacity())
			{
				return false;
			}
			const bool resized = m_Storage.ResizeUninitialized(storageSize + 1);
			ASSERT(resized, "Reserved StringBuilder append must not allocate.");
			m_Storage.Data()[storageSize - 1] = ascii;
			m_Terminated = false;
			return true;
		}

	private:
		// The last slot is reserved for C interoperation and is initialized lazily by CStr or TakeString.
		mutable SmallVector<char, String::INLINE_CAPACITY + 1> m_Storage;
		mutable bool m_Terminated = true;
	};
}
