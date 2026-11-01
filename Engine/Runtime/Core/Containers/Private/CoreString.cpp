#include <CoreString.hpp>
#include <StringBuilder.hpp>
#include <Vocabulary.hpp>
#include <ComparisonTypes.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <SystemMemoryResource.h>
#include <Utf8.h>

#include <cstring>
#include <limits>
#include <utility>

namespace
{
	constexpr char LOSSY_TEXT_PLACEHOLDER[] = "<text omitted: not well-formed UTF-8, or storage was exhausted>";
} // namespace

namespace rox::core
{
	String::String(MemoryResource& resource) noexcept
	    : m_Storage(resource)
	{
	}

	String::String(const StringView text) noexcept
	    : String(SystemMemoryResource::Get())
	{
		AssignRequired(text);
	}

	String::String(const StringView text, MemoryResource& resource) noexcept
	    : m_Storage(resource)
	{
		AssignRequired(text);
	}

	String::String(const char* const text, const usize byteSize) noexcept
	    : String(StringView(text, byteSize))
	{
	}

	String::String(const char* const text, const usize byteSize, MemoryResource& resource) noexcept
	    : String(StringView(text, byteSize), resource)
	{
	}

	String::String(const usize count, const char character) noexcept
	    : String(count, character, SystemMemoryResource::Get())
	{
	}

	String::String(const usize count, const char character, MemoryResource& resource) noexcept
	    : m_Storage(resource)
	{
		ASSERT(count < NumericLimits<usize>::max(), "String fill must leave space for a null terminator.");
		ASSERT(static_cast<unsigned char>(character) < 0x80, "String fill construction requires an ASCII character to stay well-formed UTF-8.");

		if (count == 0)
		{
			return;
		}

		m_Storage.ReserveRequired(count + 1);
		const bool bResized = m_Storage.ResizeUninitialized(count + 1);
		ASSERT(bResized, "String storage resize must succeed after mandatory String storage reservation.");

		core::SetBytes(m_Storage.Data(), static_cast<unsigned char>(character), count);
		m_Storage[count] = '\0';
	}

	String& String::operator=(const String& other) noexcept
	{
		if (this != &other)
		{
			AssignRequired(other);
		}

		return *this;
	}

	String::operator StringView() const noexcept
	{
		return View();
	}

	String::operator std::string_view() const noexcept
	{
		return std::string_view(CStr(), Size());
	}

	bool String::Assign(const StringView text) noexcept
	{
		if (text.Size() == NumericLimits<usize>::max() || !IsValidUtf8(text))
		{
			return false;
		}

		return tryAssignValidated(text);
	}

	bool String::tryAssignValidated(const StringView text) noexcept
	{
		if (text.IsEmpty())
		{
			m_Storage.Clear();
			return true;
		}

		if (text.Size() < m_Storage.Capacity())
		{
			if (text.Size() >= m_Storage.Size())
			{
				const bool resized = m_Storage.ResizeUninitialized(text.Size() + 1);
				ASSERT(resized, "Reserved string assignment must not allocate.");
			}

			core::MoveBytes(m_Storage.Data(), text.Data(), text.Size());
			const bool resized = m_Storage.ResizeUninitialized(text.Size() + 1);
			ASSERT(resized, "Reserved string assignment must not allocate.");
			m_Storage[text.Size()] = '\0';

			return true;
		}

		SmallVector<char, INLINE_CAPACITY + 1> replacement(m_Storage.GetResource());
		if (!replacement.ResizeUninitialized(text.Size() + 1))
		{
			return false;
		}

		if (!text.IsEmpty())
		{
			core::CopyBytes(replacement.Data(), text.Data(), text.Size());
		}
		replacement[text.Size()] = '\0';

		m_Storage = core::Move(replacement);
		return true;
	}

	void String::AssignRequired(const StringView text) noexcept
	{
		ASSERT(text.Size() != NumericLimits<usize>::max(), "String assignment must leave space for a null terminator.");
		ASSERT(IsValidUtf8(text), "String assignment requires well-formed UTF-8.");

		assignValidated(text);
	}

	void String::assignValidated(const StringView text) noexcept
	{
		if (text.IsEmpty())
		{
			m_Storage.Clear();
			return;
		}

		if (text.Size() < m_Storage.Capacity())
		{
			if (text.Size() >= m_Storage.Size())
			{
				const bool resized = m_Storage.ResizeUninitialized(text.Size() + 1);
				ASSERT(resized, "Reserved string assignment must not allocate.");
			}

			core::MoveBytes(m_Storage.Data(), text.Data(), text.Size());
			const bool resized = m_Storage.ResizeUninitialized(text.Size() + 1);
			ASSERT(resized, "Reserved string assignment must not allocate.");
			m_Storage[text.Size()] = '\0';

			return;
		}

		SmallVector<char, INLINE_CAPACITY + 1> replacement(m_Storage.GetResource());
		replacement.ReserveRequired(text.Size() + 1);
		const bool bResized = replacement.ResizeUninitialized(text.Size() + 1);
		ASSERT(bResized, "String storage resize must succeed after mandatory String storage reservation.");

		if (!text.IsEmpty())
		{
			core::CopyBytes(replacement.Data(), text.Data(), text.Size());
		}
		replacement[text.Size()] = '\0';

		m_Storage = core::Move(replacement);
	}

	bool String::tryAppendView(const StringView text) noexcept
	{
		return canAppend(text.Size()) && IsValidUtf8(text) && tryAppendValidated(text);
	}

	void String::appendView(const StringView text) noexcept
	{
		ASSERT(canAppend(text.Size()), "String append must fit the representable byte range.");
		ASSERT(IsValidUtf8(text), "String append requires well-formed UTF-8.");

		appendValidated(text);
	}

	void String::Truncate(const usize byteSize) noexcept
	{
		ASSERT(byteSize <= Size(), "String truncation must not grow the string.");

		if (byteSize == Size())
		{
			return;
		}

		if (byteSize == 0)
		{
			m_Storage.Clear();
			return;
		}

		ASSERT((static_cast<unsigned char>(m_Storage[byteSize]) & 0xC0) != 0x80, "String truncation must not cut a multi-byte UTF-8 sequence.");

		const bool bResized = m_Storage.ResizeUninitialized(byteSize + 1);
		ASSERT(bResized, "String storage resize must succeed when shrinking String storage.");
		m_Storage[byteSize] = char{};
	}

	bool String::Reserve(const usize byteCapacity) noexcept
	{
		if (byteCapacity == NumericLimits<usize>::max())
		{
			return false;
		}

		return m_Storage.Reserve(byteCapacity + 1);
	}

	void String::ReserveRequired(const usize byteCapacity) noexcept
	{
		ASSERT(byteCapacity != NumericLimits<usize>::max(), "String reservation must leave space for a null terminator.");

		m_Storage.ReserveRequired(byteCapacity + 1);
	}

	void String::Clear() noexcept
	{
		m_Storage.Clear();
	}

	void String::Reset() noexcept
	{
		m_Storage.Reset();
	}

	bool String::tryAppendValidated(const StringView text) noexcept
	{
		if (text.IsEmpty())
		{
			return true;
		}

		const usize oldSize = Size();
		const usize newStorageSize = oldSize + text.Size() + 1;

		if (newStorageSize <= m_Storage.Capacity())
		{
			const bool bResized = m_Storage.ResizeUninitialized(newStorageSize);
			ASSERT(bResized, "String storage resize must succeed within existing String storage capacity.");

			core::MoveBytes(m_Storage.Data() + oldSize, text.Data(), text.Size());
			m_Storage[newStorageSize - 1] = '\0';

			return true;
		}

		SmallVector<char, INLINE_CAPACITY + 1> replacement(m_Storage.GetResource());
		if (!replacement.Reserve(growthStorageCapacity(newStorageSize)))
		{
			return false;
		}

		const bool bResized = replacement.ResizeUninitialized(newStorageSize);
		ASSERT(bResized, "String storage resize must succeed after successful String storage reservation.");

		if (oldSize != 0)
		{
			core::CopyBytes(replacement.Data(), m_Storage.Data(), oldSize);
		}
		core::CopyBytes(replacement.Data() + oldSize, text.Data(), text.Size());
		replacement[newStorageSize - 1] = '\0';

		m_Storage = core::Move(replacement);
		return true;
	}

	void String::appendValidated(const StringView text) noexcept
	{
		if (text.IsEmpty())
		{
			return;
		}

		const usize oldSize = Size();
		const usize newStorageSize = oldSize + text.Size() + 1;

		if (newStorageSize <= m_Storage.Capacity())
		{
			const bool bResized = m_Storage.ResizeUninitialized(newStorageSize);
			ASSERT(bResized, "String storage resize must succeed within existing String storage capacity.");

			core::MoveBytes(m_Storage.Data() + oldSize, text.Data(), text.Size());
			m_Storage[newStorageSize - 1] = '\0';

			return;
		}

		SmallVector<char, INLINE_CAPACITY + 1> replacement(m_Storage.GetResource());
		replacement.ReserveRequired(growthStorageCapacity(newStorageSize));
		const bool bResized = replacement.ResizeUninitialized(newStorageSize);
		ASSERT(bResized, "String storage resize must succeed after mandatory String storage reservation.");

		if (oldSize != 0)
		{
			core::CopyBytes(replacement.Data(), m_Storage.Data(), oldSize);
		}
		core::CopyBytes(replacement.Data() + oldSize, text.Data(), text.Size());
		replacement[newStorageSize - 1] = '\0';

		m_Storage = core::Move(replacement);
	}

	bool String::canAppend(const usize byteCount) const noexcept
	{
		return byteCount <= NumericLimits<usize>::max() - Size() - 1;
	}

	usize String::growthStorageCapacity(const usize requiredStorageSize) const noexcept
	{
		ASSERT(requiredStorageSize != 0, "String growth requires storage for a null terminator.");

		if (requiredStorageSize <= m_Storage.Capacity())
		{
			return m_Storage.Capacity();
		}

		usize capacity = m_Storage.Capacity() == 0 ? 4 : m_Storage.Capacity() + (m_Storage.Capacity() / 2) + 1;
		if (capacity < m_Storage.Capacity() || capacity < requiredStorageSize)
		{
			capacity = requiredStorageSize;
		}

		return capacity;
	}

	bool operator==(const String& left, const String& right) noexcept
	{
		return left.View() == right.View();
	}

	core::StrongOrdering operator<=>(const String& left, const String& right) noexcept
	{
		return left.View() <=> right.View();
	}

	String& String::operator+=(const StringView text) noexcept
	{
		AppendRequired(text);
		return *this;
	}

	String operator+(const String& left, const String& right) noexcept
	{
		ASSERT(right.Size() < NumericLimits<usize>::max() - left.Size(), "String concatenation must leave space for a null terminator.");

		String result(left.GetResource());
		result.ReserveRequired(left.Size() + right.Size());
		result.AppendRequired(left);
		result.AppendRequired(right);

		return result;
	}

	String Concat(const InitializerList<StringView> parts) noexcept
	{
		return Concat(parts, SystemMemoryResource::Get());
	}

	String Concat(const InitializerList<StringView> parts, MemoryResource& resource) noexcept
	{
		usize totalSize = 0;
		for (const StringView part : parts)
		{
			ASSERT(part.Size() < NumericLimits<usize>::max() - totalSize, "String concatenation must leave space for a null terminator.");
			totalSize += part.Size();
		}

		String result(resource);
		result.ReserveRequired(totalSize);

		for (const StringView part : parts)
		{
			result.AppendRequired(part);
		}

		return result;
	}

	String MakeLossyString(const StringView text) noexcept
	{
		return MakeLossyString(text, SystemMemoryResource::Get());
	}

	String MakeLossyString(const StringView text, MemoryResource& resource) noexcept
	{
		String result(resource);
		if (result.Assign(text))
		{
			return result;
		}

		// Assign leaves the destination untouched when it fails, so the placeholder attempt starts clean.
		(void)result.Assign(StringView(LOSSY_TEXT_PLACEHOLDER));
		return result;
	}

	StringBuilder::StringBuilder(const StringBuilder& other) noexcept
	    : m_Storage(other.GetResource())
	{
		appendValidated(other.View());
	}

	StringBuilder::StringBuilder(StringBuilder&& other) noexcept
	    : m_Storage(other.GetResource())
	{
		(void)other.CStr();
		m_Storage = core::Move(other.m_Storage);
	}

	StringBuilder& StringBuilder::operator=(const StringBuilder& other) noexcept
	{
		if (this != &other)
		{
			Clear();
			appendValidated(other.View());
		}

		return *this;
	}

	StringBuilder& StringBuilder::operator=(StringBuilder&& other) noexcept
	{
		if (this != &other)
		{
			(void)other.CStr();
			m_Storage = core::Move(other.m_Storage);
			m_Terminated = true;
		}

		return *this;
	}

	void StringBuilder::ReserveRequired(const usize bytes) noexcept
	{
		ASSERT(bytes != NumericLimits<usize>::max(), "StringBuilder reservation must leave space for a null terminator.");

		if (bytes + 1 > m_Storage.Capacity())
		{
			(void)CStr();
		}
		m_Storage.ReserveRequired(bytes + 1);
	}

	bool StringBuilder::Reserve(const usize bytes) noexcept
	{
		if (bytes == NumericLimits<usize>::max())
		{
			return false;
		}

		if (bytes + 1 > m_Storage.Capacity())
		{
			(void)CStr();
		}
		return m_Storage.Reserve(bytes + 1);
	}

	bool StringBuilder::tryAppendValidated(const StringView text) noexcept
	{
		if (text.IsEmpty())
		{
			return true;
		}

		const usize oldSize = Size();
		const usize required = oldSize + text.Size() + 1;

		if (required <= m_Storage.Capacity())
		{
			const bool resized = m_Storage.ResizeUninitialized(required);
			ASSERT(resized, "Reserved StringBuilder append must not allocate.");

			core::MoveBytes(m_Storage.Data() + oldSize, text.Data(), text.Size());
		}
		else
		{
			SmallVector<char, String::INLINE_CAPACITY + 1> replacement(GetResource());
			if (!replacement.Reserve(growthStorageCapacity(required)))
			{
				return false;
			}

			const bool resized = replacement.ResizeUninitialized(required);
			ASSERT(resized, "StringBuilder resize must succeed after successful reservation.");

			if (oldSize != 0)
			{
				core::CopyBytes(replacement.Data(), m_Storage.Data(), oldSize);
			}
			core::CopyBytes(replacement.Data() + oldSize, text.Data(), text.Size());
			replacement.Data()[required - 1] = '\0';

			m_Storage = core::Move(replacement);
		}

		m_Terminated = false;
		return true;
	}

	void StringBuilder::appendValidated(const StringView text) noexcept
	{
		if (text.IsEmpty())
		{
			return;
		}

		const usize oldSize = Size();
		const usize required = oldSize + text.Size() + 1;

		if (required > m_Storage.Capacity())
		{
			// Keep an aliased source alive until it has been copied into the replacement.
			SmallVector<char, String::INLINE_CAPACITY + 1> replacement(GetResource());
			replacement.ReserveRequired(growthStorageCapacity(required));
			const bool resized = replacement.ResizeUninitialized(required);
			ASSERT(resized, "StringBuilder resize must succeed after mandatory reservation.");

			if (oldSize != 0)
			{
				core::CopyBytes(replacement.Data(), m_Storage.Data(), oldSize);
			}
			core::CopyBytes(replacement.Data() + oldSize, text.Data(), text.Size());
			replacement.Data()[required - 1] = '\0';

			m_Storage = core::Move(replacement);
			m_Terminated = false;
			return;
		}

		const bool resized = m_Storage.ResizeUninitialized(required);
		ASSERT(resized, "Reserved StringBuilder append must not allocate.");

		core::MoveBytes(m_Storage.Data() + oldSize, text.Data(), text.Size());
		m_Terminated = false;
	}

	bool StringBuilder::canAppend(const usize size) const noexcept
	{
		return size <= NumericLimits<usize>::max() - Size() - 1;
	}

	usize StringBuilder::growthStorageCapacity(const usize required) const noexcept
	{
		const usize current = m_Storage.Capacity();
		const usize grown = current + current / 2 + 1;
		return grown < current || grown < required ? required : grown;
	}

	String StringBuilder::TakeString() noexcept
	{
		(void)CStr();
		String result(GetResource());
		result.m_Storage = core::Move(m_Storage);
		m_Terminated = true;

		return result;
	}
} // namespace rox::core
