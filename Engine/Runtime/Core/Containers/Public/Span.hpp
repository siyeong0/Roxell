#pragma once

#include <Assertion.h>
#include <Algorithms.hpp>
#include <TypeVocabulary.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <TypeTraits.hpp>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <span>
#include <iterator>

namespace rox::core
{
	// The caller owns the viewed range and must keep it alive for the lifetime of the span.
	// Owning ranges must be lvalues; temporary std::span and Span views remain supported.
	template <typename TValue> 
	class Span
	{
	public:
		[[nodiscard]] constexpr TValue& At(usize index) const noexcept { return (*this)[index]; }

		[[nodiscard]] constexpr TValue& Front() const noexcept
		{
			ASSERT(m_Size != 0, "Span::Front requires a non-empty range.");
			return (*this)[0];
		}

		[[nodiscard]] constexpr TValue& Back() const noexcept
		{
			ASSERT(m_Size != 0, "Span::Back requires a non-empty range.");
			return (*this)[m_Size - 1];
		}

		using ElementType = TValue;
		using SizeType = usize;

		constexpr Span() noexcept = default;

		template <typename TRange>
		    requires requires(TRange& range) { std::data(range); core::Size(range); } && IsConvertible<RemovePointer<decltype(std::data(core::Declval<TRange&>()))> (*)[], TValue (*)[]>
		constexpr Span(TRange& range) noexcept
		    : Span(std::data(range), core::Size(range))
		{
		}

		template <typename TOther, usize Extent>
		    requires IsConvertible<TOther (*)[], TValue (*)[]>
		constexpr Span(std::span<TOther, Extent> range) noexcept : Span(range.data(), range.size()) {}

		using element_type = TValue;
		using value_type = RemoveCv<TValue>;
		using iterator = TValue*;

		[[nodiscard]] constexpr TValue* data() const noexcept { return Data(); }
		[[nodiscard]] constexpr usize size() const noexcept { return Size(); }
		[[nodiscard]] constexpr usize size_bytes() const noexcept { return SizeBytes(); }

		[[nodiscard]] constexpr bool empty() const noexcept { return IsEmpty(); }
		
		[[nodiscard]] constexpr TValue& front() const noexcept { return Front(); }
		[[nodiscard]] constexpr TValue& back() const noexcept { return Back(); }

		[[nodiscard]] constexpr Span subspan(usize offset) const noexcept { return Subspan(offset); }
		[[nodiscard]] constexpr Span subspan(usize offset, usize count) const noexcept { return Subspan(offset, count); }

		[[nodiscard]] constexpr Span first(usize count) const noexcept { return First(count); }
		[[nodiscard]] constexpr Span last(usize count) const noexcept { return Last(count); }

		[[nodiscard]] constexpr operator std::span<TValue>() const noexcept { return {Data(), Size()}; }


		constexpr Span(TValue* data, const usize size) noexcept
		    : m_Data(data)
		    , m_Size(size)
		{
			ASSERT(m_Data != nullptr || m_Size == 0, "A non-empty Span requires a valid data pointer.");
		}

		template <usize Size>
		constexpr Span(TValue (&values)[Size]) noexcept
		    : m_Data(values)
		    , m_Size(Size)
		{
		}

		template <typename TOther>
		    requires IsConvertible<TOther (*)[], TValue (*)[]>
		constexpr Span(const Span<TOther>& other) noexcept
		    : m_Data(other.Data())
		    , m_Size(other.Size())
		{
		}

		[[nodiscard]] constexpr TValue* Data() const noexcept { return m_Data; }

		[[nodiscard]] constexpr usize Size() const noexcept { return m_Size; }

		[[nodiscard]] constexpr bool IsEmpty() const noexcept { return m_Size == 0; }

		[[nodiscard]] constexpr Span Subspan(const usize offset) const noexcept
		{
			ASSERT(offset <= m_Size, "Span::Subspan offset must fit the viewed range.");
			Span result = *this;
			result.m_Data = m_Data == nullptr ? nullptr : m_Data + offset;
			result.m_Size = m_Size - offset;
			return result;
		}

		[[nodiscard]] constexpr Span Subspan(const usize offset, const usize count) const noexcept
		{
			ASSERT(offset <= m_Size, "Span::Subspan offset must fit the viewed range.");
			ASSERT(count <= m_Size - offset, "Span::Subspan count must fit the remaining viewed range.");

			Span result = *this;
			result.m_Data = m_Data == nullptr ? nullptr : m_Data + offset;
			result.m_Size = count;
			return result;
		}

		[[nodiscard]] constexpr Span First(const usize count) const noexcept
		{
			ASSERT(count <= m_Size, "Span::First count must fit the viewed range.");
			Span result = *this;
			result.m_Size = count;
			return result;
		}

		[[nodiscard]] constexpr Span Last(const usize count) const noexcept
		{
			ASSERT(count <= m_Size, "Span::Last count must fit the viewed range.");
			Span result = *this;
			result.m_Data = m_Data == nullptr ? nullptr : m_Data + (m_Size - count);
			result.m_Size = count;
			return result;
		}

		[[nodiscard]] constexpr usize SizeBytes() const noexcept
		{
			ASSERT(m_Size <= NumericLimits<usize>::max() / sizeof(TValue), "Span::SizeBytes byte count must fit size_t.");
			return m_Size * sizeof(TValue);
		}

		[[nodiscard]] constexpr TValue& operator[](const usize index) const noexcept
		{
			ASSERT(index < m_Size, "Span index must be within the viewed range.");
			return m_Data[index];
		}

		[[nodiscard]] constexpr TValue* begin() const noexcept { return m_Data; }
		[[nodiscard]] constexpr TValue* end() const noexcept { return m_Data == nullptr ? nullptr : m_Data + m_Size; }

	private:
		TValue* m_Data = nullptr;
		usize m_Size = 0;
	};
	template <typename TRange> Span(TRange&) -> Span<std::remove_pointer_t<decltype(std::data(std::declval<TRange&>()))>>;
	template <typename T> Span(T*, usize) -> Span<T>;
	template <typename T, usize N> Span(T (&)[N]) -> Span<T>;
	template <typename T> [[nodiscard]] Span<const Byte> AsBytes(Span<T> values) noexcept
	{
		return {reinterpret_cast<const Byte*>(values.Data()), values.SizeBytes()};
	}
} // namespace rox::core
