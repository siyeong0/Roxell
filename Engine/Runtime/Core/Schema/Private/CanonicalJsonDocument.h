#pragma once

#include <Array.hpp>
#include <StringView.hpp>
#include <Span.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Assertion.h>
#include <Result.hpp>
#include <Schema.h>

#include <limits>
#include <span>
#include <string_view>

namespace rox::core
{
	inline constexpr usize JSON_NO_NODE = NumericLimits<usize>::max();

	struct CanonicalJsonNode final
	{
		ESchemaValueKind Kind = ESchemaValueKind::Null;

		union
		{
			uint64 Unsigned = 0;
			int64 Signed;
			float64 Floating;
			bool Boolean;
		};

		usize Parent = JSON_NO_NODE;
		usize FirstChild = JSON_NO_NODE;
		usize LastChild = JSON_NO_NODE;
		usize NextSibling = JSON_NO_NODE;
		usize ChildCount = 0;

		usize TextOffset = 0;
		usize TextSize = 0;

		usize KeyOffset = 0;
		usize KeySize = 0;
	};

	struct CanonicalJsonDocument final
	{
		explicit CanonicalJsonDocument(MemoryResource& resource) noexcept
		    : Nodes(resource)
		    , Text(resource)
		{
		}

		[[nodiscard]] StringView GetText(usize offset, usize size) const noexcept
		{
			ASSERT(offset <= Text.Size() && size <= Text.Size() - offset, "JSON text must stay inside decoded storage.");
			return size == 0 ? StringView{} : StringView(Text.Data() + offset, size);
		}

		Array<CanonicalJsonNode> Nodes;
		Array<char> Text;
	};

	class CanonicalJsonView final
	{
	public:
		CanonicalJsonView(const CanonicalJsonDocument& document, const usize index) noexcept
		    : m_Document(document)
		    , m_Index(index)
		{
			ASSERT(index < document.Nodes.Size(), "JSON views require a valid node index.");
		}

		[[nodiscard]] ESchemaValueKind GetKind() const noexcept { return node().Kind; }

		[[nodiscard]] bool IsNull() const noexcept { return GetKind() == ESchemaValueKind::Null; }
		[[nodiscard]] bool IsBoolean() const noexcept { return GetKind() == ESchemaValueKind::Boolean; }
		[[nodiscard]] bool IsUnsignedInteger() const noexcept { return GetKind() == ESchemaValueKind::UnsignedInteger; }
		[[nodiscard]] bool IsInteger() const noexcept
		{
			return GetKind() == ESchemaValueKind::SignedInteger || IsUnsignedInteger();
		}

		[[nodiscard]] bool IsFloatingPoint() const noexcept { return GetKind() == ESchemaValueKind::FloatingPoint; }
		[[nodiscard]] bool IsNumber() const noexcept { return IsInteger() || IsFloatingPoint(); }
		[[nodiscard]] bool IsString() const noexcept { return GetKind() == ESchemaValueKind::String; }
		[[nodiscard]] bool IsArray() const noexcept { return GetKind() == ESchemaValueKind::Array; }
		[[nodiscard]] bool IsObject() const noexcept { return GetKind() == ESchemaValueKind::Object; }

		[[nodiscard]] bool GetBoolean() const noexcept
		{
			ASSERT(IsBoolean(), "JSON boolean access requires boolean storage.");
			return node().Boolean;
		}

		[[nodiscard]] uint64 GetUnsignedInteger() const noexcept
		{
			ASSERT(IsUnsignedInteger(), "JSON unsigned access requires unsigned storage.");
			return node().Unsigned;
		}

		[[nodiscard]] int64 GetSignedInteger() const noexcept
		{
			ASSERT(GetKind() == ESchemaValueKind::SignedInteger, "JSON signed access requires signed storage.");
			return node().Signed;
		}

		[[nodiscard]] float64 GetNumber() const noexcept
		{
			ASSERT(IsNumber(), "JSON numeric access requires numeric storage.");
			return IsUnsignedInteger() ? static_cast<float64>(node().Unsigned)
			       : IsInteger()       ? static_cast<float64>(node().Signed)
			                           : node().Floating;
		}

		[[nodiscard]] StringView GetString() const noexcept
		{
			ASSERT(IsString(), "JSON string access requires string storage.");
			return m_Document.GetText(node().TextOffset, node().TextSize);
		}

		[[nodiscard]] StringView GetKey() const noexcept
		{
			return m_Document.GetText(node().KeyOffset, node().KeySize);
		}

		[[nodiscard]] usize Size() const noexcept
		{
			ASSERT(IsArray() || IsObject(), "JSON child count requires a container.");
			return node().ChildCount;
		}

		class Iterator final
		{
		public:
			Iterator(const CanonicalJsonDocument& document, const usize index) noexcept
			    : m_Document(document)
			    , m_Index(index)
			{
			}

			[[nodiscard]] CanonicalJsonView operator*() const noexcept { return {m_Document, m_Index}; }

			Iterator& operator++() noexcept
			{
				m_Index = m_Document.Nodes[m_Index].NextSibling;
				return *this;
			}

			[[nodiscard]] bool operator==(const Iterator& other) const noexcept
			{
				return &m_Document == &other.m_Document && m_Index == other.m_Index;
			}

		private:
			const CanonicalJsonDocument& m_Document;
			usize m_Index;
		};

		[[nodiscard]] Iterator begin() const noexcept { return {m_Document, node().FirstChild}; }
		[[nodiscard]] Iterator end() const noexcept { return {m_Document, JSON_NO_NODE}; }

	private:
		[[nodiscard]] const CanonicalJsonNode& node() const noexcept { return m_Document.Nodes[m_Index]; }

	private:
		const CanonicalJsonDocument& m_Document;
		usize m_Index;
	};

	[[nodiscard]] Result<CanonicalJsonDocument> ParseCanonicalJsonDocument(Span<const Byte> bytes, MemoryResource& resource);
} // namespace rox::core
