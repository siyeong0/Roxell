#pragma once

#include <Assertion.h>
#include <Types.h>
#include <ByteOperations.hpp>
#include <NumericLimits.hpp>
#include <StringView.hpp>
#include <Utf8.h>

#include <cstddef>
#include <cstring>
#include <limits>

namespace rox::core
{
	// Capacity counts UTF-8 payload bytes. Mutation requires exclusive access to the inline storage.
	template <rox::core::usize N> class FixedString final
	{
		static_assert(N < rox::core::NumericLimits<rox::core::usize>::max());

	public:
		FixedString() noexcept = default;
		explicit FixedString(StringView text) noexcept;
		~FixedString() = default;

		FixedString(const FixedString& other) noexcept;
		FixedString(FixedString&& other) noexcept;

		FixedString& operator=(const FixedString& other) noexcept;
		FixedString& operator=(FixedString&& other) noexcept;

		[[nodiscard]] rox::core::usize Size() const noexcept;
		[[nodiscard]] rox::core::usize Capacity() const noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept;
		[[nodiscard]] bool IsFull() const noexcept;

		[[nodiscard]] const char* Data() const noexcept;
		[[nodiscard]] const char* CStr() const noexcept;

		[[nodiscard]] StringView View() const noexcept;
		[[nodiscard]] operator StringView() const noexcept;

		[[nodiscard]] const char* begin() const noexcept;
		[[nodiscard]] const char* end() const noexcept;

		[[nodiscard]] char operator[](rox::core::usize index) const noexcept;

		void AssignRequired(StringView text) noexcept;
		void AppendRequired(StringView text) noexcept;

		void Clear() noexcept;

		// Both operations reject malformed UTF-8 or insufficient capacity without mutation.
		[[nodiscard]] bool Assign(StringView text) noexcept;
		[[nodiscard]] bool Append(StringView text) noexcept;

		void AppendRequired(char ascii) noexcept;

		void Truncate(rox::core::usize bytes) noexcept;

	private:
		void assignValidated(StringView text) noexcept;
		void appendValidated(StringView text) noexcept;

	private:
		char m_Data[N + 1]{};
		rox::core::usize m_Size = 0;
	};
}

namespace rox::core
{
	template <rox::core::usize N>
	FixedString<N>::FixedString(StringView text) noexcept
	{
		AssignRequired(text);
	}

	template <rox::core::usize N>
	FixedString<N>::FixedString(const FixedString& other) noexcept
	{
		assignValidated(other.View());
	}

	template <rox::core::usize N>
	FixedString<N>::FixedString(FixedString&& other) noexcept
	{
		assignValidated(other.View());
		other.Clear();
	}

	template <rox::core::usize N>
	FixedString<N>& FixedString<N>::operator=(const FixedString& other) noexcept
	{
		if (this != &other)
		{
			assignValidated(other.View());
		}

		return *this;
	}

	template <rox::core::usize N>
	FixedString<N>& FixedString<N>::operator=(FixedString&& other) noexcept
	{
		if (this != &other)
		{
			assignValidated(other.View());
			other.Clear();
		}

		return *this;
	}

	template <rox::core::usize N>
	rox::core::usize FixedString<N>::Size() const noexcept
	{
		return m_Size;
	}

	template <rox::core::usize N>
	rox::core::usize FixedString<N>::Capacity() const noexcept
	{
		return N;
	}

	template <rox::core::usize N>
	bool FixedString<N>::IsEmpty() const noexcept
	{
		return m_Size == 0;
	}

	template <rox::core::usize N>
	bool FixedString<N>::IsFull() const noexcept
	{
		return m_Size == N;
	}

	template <rox::core::usize N>
	const char* FixedString<N>::Data() const noexcept
	{
		return m_Data;
	}

	template <rox::core::usize N>
	const char* FixedString<N>::CStr() const noexcept
	{
		return m_Data;
	}

	template <rox::core::usize N>
	StringView FixedString<N>::View() const noexcept
	{
		return {m_Data, m_Size};
	}

	template <rox::core::usize N>
	FixedString<N>::operator StringView() const noexcept
	{
		return View();
	}

	template <rox::core::usize N>
	const char* FixedString<N>::begin() const noexcept
	{
		return m_Data;
	}

	template <rox::core::usize N>
	const char* FixedString<N>::end() const noexcept
	{
		return m_Data + m_Size;
	}

	template <rox::core::usize N>
	char FixedString<N>::operator[](rox::core::usize index) const noexcept
	{
		ASSERT(index < m_Size, "FixedString index must address a payload byte.");
		return m_Data[index];
	}

	template <rox::core::usize N>
	void FixedString<N>::AssignRequired(StringView text) noexcept
	{
		ASSERT(IsValidUtf8(text), "FixedString assignment requires well-formed UTF-8.");
		ASSERT(text.Size() <= N, "FixedString assignment must fit capacity.");
		assignValidated(text);
	}

	template <rox::core::usize N>
	void FixedString<N>::AppendRequired(StringView text) noexcept
	{
		ASSERT(IsValidUtf8(text), "FixedString append requires well-formed UTF-8.");
		ASSERT(text.Size() <= N - m_Size, "FixedString append must fit remaining capacity.");
		appendValidated(text);
	}

	template <rox::core::usize N>
	void FixedString<N>::Clear() noexcept
	{
		m_Size = 0;
		m_Data[0] = '\0';
	}

	template <rox::core::usize N>
	bool FixedString<N>::Assign(StringView text) noexcept
	{
		if (text.Size() > N || !IsValidUtf8(text))
		{
			return false;
		}
		assignValidated(text);
		return true;
	}

	template <rox::core::usize N>
	bool FixedString<N>::Append(StringView text) noexcept
	{
		if (text.Size() > N - m_Size || !IsValidUtf8(text))
		{
			return false;
		}
		appendValidated(text);
		return true;
	}

	template <rox::core::usize N>
	void FixedString<N>::AppendRequired(char ascii) noexcept
	{
		ASSERT(static_cast<unsigned char>(ascii) < 0x80, "FixedString character append requires ASCII.");
		ASSERT(m_Size < N, "FixedString append must fit remaining capacity.");
		m_Data[m_Size++] = ascii;
		m_Data[m_Size] = '\0';
	}

	template <rox::core::usize N>
	void FixedString<N>::Truncate(rox::core::usize bytes) noexcept
	{
		ASSERT(bytes <= m_Size, "FixedString truncation must not grow text.");
		ASSERT(bytes == m_Size || (static_cast<unsigned char>(m_Data[bytes]) & 0xC0) != 0x80, "FixedString truncation requires a UTF-8 scalar boundary.");
		m_Size = bytes;
		m_Data[m_Size] = '\0';
	}

	template <rox::core::usize N>
	void FixedString<N>::assignValidated(StringView text) noexcept
	{
		if (!text.IsEmpty())
		{
			rox::core::MoveBytes(m_Data, text.Data(), text.Size());
		}

		m_Size = text.Size();
		m_Data[m_Size] = '\0';
	}

	template <rox::core::usize N>
	void FixedString<N>::appendValidated(StringView text) noexcept
	{
		if (text.IsEmpty())
		{
			return;
		}

		rox::core::MoveBytes(m_Data + m_Size, text.Data(), text.Size());
		m_Size += text.Size();
		m_Data[m_Size] = '\0';
	}
}
