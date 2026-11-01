#include "CanonicalJsonDocument.h"
#include <TextPrimitives.hpp>
#include <TextConversion.hpp>
#include <SystemError.hpp>
#include <StringView.hpp>
#include <Span.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include <Utf8.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <clocale>
#include <cstdlib>
#include <utility>

namespace rox::core
{
	namespace
	{
		enum class EParseFailure : uint8
		{
			None,
			Malformed,
			Allocation
		};

		struct ContainerFrame final
		{
			usize Node;
			bool bFirst = true;
			bool bAfterValue = false;
		};

		class DocumentParser final
		{
		public:
			DocumentParser(const StringView input, CanonicalJsonDocument& document, MemoryResource& resource) noexcept
			    : m_Input(input)
			    , m_Document(document)
			    , m_Stack(resource)
			    , m_Members(resource)
			    , m_NumberBuffer(resource)
			{
			}

			[[nodiscard]] EParseFailure Parse()
			{
				if (m_Input.starts_with("\xef\xbb\xbf"))
				{
					m_Position = 3;
				}

				if (!parseValue(CanonicalJsonNode{}))
				{
					return m_Failure;
				}

				while (!m_Stack.IsEmpty())
				{
					const usize frameIndex = m_Stack.Size() - 1;
					const ContainerFrame frame = m_Stack[frameIndex];
					const bool bObject = m_Document.Nodes[frame.Node].Kind == ESchemaValueKind::Object;
					const char closing = bObject ? '}' : ']';
					skipWhitespace();

					if ((frame.bAfterValue || frame.bFirst) && take(closing))
					{
						m_Stack.PopBack();
						continue;
					}

					if (frame.bAfterValue)
					{
						if (!take(','))
						{
							return EParseFailure::Malformed;
						}

						m_Stack[frameIndex].bAfterValue = false;
						skipWhitespace();
					}
					CanonicalJsonNode child;
					child.Parent = frame.Node;

					if (bObject)
					{
						if (!parseString(child.KeyOffset, child.KeySize))
						{
							return m_Failure;
						}
						skipWhitespace();

						if (!take(':'))
						{
							return EParseFailure::Malformed;
						}
					}

					m_Stack[frameIndex].bFirst = false;
					m_Stack[frameIndex].bAfterValue = true;

					if (!parseValue(child))
					{
						return m_Failure;
					}
				}
				skipWhitespace();
				// The existing codec treats a NUL after the root value as end of input.
				return m_Position == m_Input.size() || m_Input[m_Position] == '\0' ? EParseFailure::None
				                                                                   : EParseFailure::Malformed;
			}

			[[nodiscard]] bool HasDuplicateKeys()
			{
				if (m_Members.Size() < 2)
				{
					return false;
				}

				core::Sort(m_Members.begin(), m_Members.end(),
				          [this](usize left, usize right)
				          {
					          const CanonicalJsonNode& a = m_Document.Nodes[left];
					          const CanonicalJsonNode& b = m_Document.Nodes[right];
					          return a.Parent != b.Parent ? a.Parent < b.Parent : m_Document.GetText(a.KeyOffset, a.KeySize) < m_Document.GetText(b.KeyOffset, b.KeySize);
				          });
				for (usize i = 1; i < m_Members.Size(); ++i)
				{
					const CanonicalJsonNode& a = m_Document.Nodes[m_Members[i - 1]];
					const CanonicalJsonNode& b = m_Document.Nodes[m_Members[i]];

					if (a.Parent == b.Parent && m_Document.GetText(a.KeyOffset, a.KeySize) == m_Document.GetText(b.KeyOffset, b.KeySize))
					{
						return true;
					}
				}

				return false;
			}

		private:
			[[nodiscard]] bool fail(EParseFailure failure) noexcept
			{
				m_Failure = failure;
				return false;
			}

			void skipWhitespace() noexcept
			{
				while (m_Position < m_Input.size())
				{
					const char c = m_Input[m_Position];

					if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
					{
						break;
					}

					++m_Position;
				}
			}

			[[nodiscard]] bool take(char c) noexcept
			{
				if (m_Position == m_Input.size() || m_Input[m_Position] != c)
				{
					return false;
				}

				++m_Position;
				return true;
			}

			[[nodiscard]] static bool isDigit(char c) noexcept
			{
				return c >= '0' && c <= '9';
			}

			[[nodiscard]] bool append(char c) noexcept
			{
				return m_Document.Text.Add(c) || fail(EParseFailure::Allocation);
			}

			[[nodiscard]] bool hex4(uint32& value) noexcept
			{
				value = 0;

				for (uint32 i = 0; i < 4; ++i)
				{
					if (m_Position == m_Input.size())
					{
						return fail(EParseFailure::Malformed);
					}

					const char c = m_Input[m_Position++];
					uint32 digit = 0;

					if (c >= '0' && c <= '9')
					{
						digit = c - '0';
					}
					else if (c >= 'a' && c <= 'f')
					{
						digit = c - 'a' + 10;
					}
					else if (c >= 'A' && c <= 'F')
					{
						digit = c - 'A' + 10;
					}
					else
					{
						return fail(EParseFailure::Malformed);
					}

					value = (value << 4) | digit;
				}

				return true;
			}

			[[nodiscard]] bool unicodeEscape() noexcept
			{
				uint32 code = 0;

				if (!hex4(code))
				{
					return false;
				}

				if (code >= 0xd800 && code <= 0xdbff)
				{
					uint32 low = 0;

					if (!take('\\') || !take('u'))
					{
						return fail(EParseFailure::Malformed);
					}

					if (!hex4(low))
					{
						return false;
					}

					if (low < 0xdc00 || low > 0xdfff)
					{
						return fail(EParseFailure::Malformed);
					}

					code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
				}
				else if (code >= 0xdc00 && code <= 0xdfff)
				{
					return fail(EParseFailure::Malformed);
				}

				if (code <= 0x7f)
				{
					return append(static_cast<char>(code));
				}

				if (code <= 0x7ff)
				{
					return append(static_cast<char>(0xc0 | (code >> 6))) &&
					       append(static_cast<char>(0x80 | (code & 0x3f)));
				}

				if (code <= 0xffff)
				{
					return append(static_cast<char>(0xe0 | (code >> 12))) &&
					       append(static_cast<char>(0x80 | ((code >> 6) & 0x3f))) &&
					       append(static_cast<char>(0x80 | (code & 0x3f)));
				}

				return append(static_cast<char>(0xf0 | (code >> 18))) &&
				       append(static_cast<char>(0x80 | ((code >> 12) & 0x3f))) &&
				       append(static_cast<char>(0x80 | ((code >> 6) & 0x3f))) &&
				       append(static_cast<char>(0x80 | (code & 0x3f)));
			}

			[[nodiscard]] bool parseString(usize& offset, usize& size) noexcept
			{
				if (!take('"'))
				{
					return fail(EParseFailure::Malformed);
				}

				offset = m_Document.Text.Size();

				while (m_Position < m_Input.size())
				{
					char c = m_Input[m_Position++];

					if (c == '"')
					{
						size = m_Document.Text.Size() - offset;
						const StringView text = m_Document.GetText(offset, size);
						return IsValidUtf8(StringView(text.data(), text.size())) || fail(EParseFailure::Malformed);
					}

					if (static_cast<unsigned char>(c) < 0x20)
					{
						return fail(EParseFailure::Malformed);
					}

					if (c == '\\')
					{
						if (m_Position == m_Input.size())
						{
							return fail(EParseFailure::Malformed);
						}

						c = m_Input[m_Position++];

						switch (c)
						{
						case 'u':
							if (!unicodeEscape())
							{
								return false;
							}
							continue;
						case '"':
						case '\\':
						case '/':
							break;
						case 'b':
							c = '\b';
							break;
						case 'f':
							c = '\f';
							break;
						case 'n':
							c = '\n';
							break;
						case 'r':
							c = '\r';
							break;
						case 't':
							c = '\t';
							break;
						default:
							return fail(EParseFailure::Malformed);
						}
					}

					if (!append(c))
					{
						return false;
					}
				}

				return fail(EParseFailure::Malformed);
			}

			[[nodiscard]] bool parseNumber(CanonicalJsonNode& node) noexcept
			{
				const usize start = m_Position;
				const bool bNegative = take('-');

				if (m_Position == m_Input.size())
				{
					return fail(EParseFailure::Malformed);
				}

				if (!take('0'))
				{
					if (!isDigit(m_Input[m_Position]))
					{
						return fail(EParseFailure::Malformed);
					}

					while (m_Position < m_Input.size() && isDigit(m_Input[m_Position]))
					{
						++m_Position;
					}
				}
				bool bFloating = false;

				if (take('.'))
				{
					bFloating = true;
					const usize first = m_Position;

					while (m_Position < m_Input.size() && isDigit(m_Input[m_Position]))
					{
						++m_Position;
					}

					if (m_Position == first)
					{
						return fail(EParseFailure::Malformed);
					}
				}

				if (take('e') || take('E'))
				{
					bFloating = true;

					if (!take('+'))
					{
						(void)take('-');
					}

					const usize first = m_Position;

					while (m_Position < m_Input.size() && isDigit(m_Input[m_Position]))
					{
						++m_Position;
					}

					if (m_Position == first)
					{
						return fail(EParseFailure::Malformed);
					}
				}

				const char* first = m_Input.data() + start;
				const char* last = m_Input.data() + m_Position;

				if (!bFloating)
				{
					if (bNegative)
					{
						int64 integer = 0;
						const FromCharsResult result = core::FromChars(first, last, integer);

						if (result.ec == core::EErrorCondition{})
						{
							node.Kind = ESchemaValueKind::SignedInteger;
							node.Signed = integer;
							return true;
						}
					}
					else
					{
						uint64 integer = 0;
						const FromCharsResult result = core::FromChars(first, last, integer);

						if (result.ec == core::EErrorCondition{})
						{
							node.Kind = ESchemaValueKind::UnsignedInteger;
							node.Unsigned = integer;
							return true;
						}
					}
				}

				const usize length = static_cast<usize>(last - first);

				if (length == NumericLimits<usize>::max() || !m_NumberBuffer.ResizeUninitialized(length + 1))
				{
					return fail(EParseFailure::Allocation);
				}

				const std::lconv* locale = core::LocaleConvention();
				const char decimalPoint = locale->decimal_point == nullptr ? '.' : *locale->decimal_point;

				for (usize i = 0; i < length; ++i)
				{
					m_NumberBuffer[i] = first[i] == '.' ? decimalPoint : first[i];
				}

				m_NumberBuffer[length] = '\0';
				char* end = nullptr;
				// Preserve the codec's active rounding mode and subnormal behavior.
				const float64 number = core::ParseDouble(m_NumberBuffer.Data(), &end);

				if (end != m_NumberBuffer.Data() + length || !core::IsFinite(number))
				{
					return fail(EParseFailure::Malformed);
				}

				node.Kind = ESchemaValueKind::FloatingPoint;
				node.Floating = number;
				return true;
			}

			[[nodiscard]] bool parseValue(CanonicalJsonNode node)
			{
				skipWhitespace();

				if (m_Position == m_Input.size())
				{
					return fail(EParseFailure::Malformed);
				}

				const char c = m_Input[m_Position];

				if (c == '"')
				{
					node.Kind = ESchemaValueKind::String;

					if (!parseString(node.TextOffset, node.TextSize))
					{
						return false;
					}
				}
				else if (c == '{' || c == '[')
				{
					node.Kind = c == '{' ? ESchemaValueKind::Object : ESchemaValueKind::Array;
					++m_Position;
				}
				else if (c == '-' || isDigit(c))
				{
					if (!parseNumber(node))
					{
						return false;
					}
				}
				else if (m_Input.substr(m_Position).starts_with("true"))
				{
					node.Kind = ESchemaValueKind::Boolean;
					node.Boolean = true;
					m_Position += 4;
				}
				else if (m_Input.substr(m_Position).starts_with("false"))
				{
					node.Kind = ESchemaValueKind::Boolean;
					node.Boolean = false;
					m_Position += 5;
				}
				else if (m_Input.substr(m_Position).starts_with("null"))
				{
					m_Position += 4;
				}
				else
				{
					return fail(EParseFailure::Malformed);
				}

				const usize index = m_Document.Nodes.Size();

				if (!m_Document.Nodes.Add(node))
				{
					return fail(EParseFailure::Allocation);
				}

				if (node.Parent != JSON_NO_NODE)
				{
					CanonicalJsonNode& parent = m_Document.Nodes[node.Parent];

					if (parent.Kind == ESchemaValueKind::Object && !m_Members.Add(index))
					{
						return fail(EParseFailure::Allocation);
					}

					if (parent.FirstChild == JSON_NO_NODE)
					{
						parent.FirstChild = index;
					}
					else
					{
						m_Document.Nodes[parent.LastChild].NextSibling = index;
					}

					parent.LastChild = index;
					++parent.ChildCount;
				}

				if ((node.Kind == ESchemaValueKind::Object || node.Kind == ESchemaValueKind::Array) &&
				    !m_Stack.Add(ContainerFrame{index}))
				{
					return fail(EParseFailure::Allocation);
				}

				return true;
			}

		private:
			StringView m_Input;
			CanonicalJsonDocument& m_Document;

			Array<ContainerFrame> m_Stack;
			Array<usize> m_Members;
			Array<char> m_NumberBuffer;

			usize m_Position = 0;
			EParseFailure m_Failure = EParseFailure::None;
		};
	} // namespace

	Result<CanonicalJsonDocument> ParseCanonicalJsonDocument(Span<const Byte> bytes, MemoryResource& resource)
	{
		CanonicalJsonDocument document(resource);
		DocumentParser parser({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, document, resource);
		const EParseFailure result = parser.Parse();

		if (result == EParseFailure::Allocation)
		{
			return Result<CanonicalJsonDocument>::Failure(
			    EErrorCode::ExternalFailure, "ROX-CORE-JSON-READ-ALLOCATION",
			    "Canonical JSON reading could not allocate its temporary state.",
			    "Free memory, lower the read limits, and retry the operation.");
		}

		if (result == EParseFailure::Malformed)
		{
			return Result<CanonicalJsonDocument>::Failure(EErrorCode::InvalidArgument, "ROX-CORE-JSON-MALFORMED",
			                                              "The byte sequence is not valid strict JSON.",
			                                              "Repair the malformed JSON before loading it.");
		}

		if (parser.HasDuplicateKeys())
		{
			return Result<CanonicalJsonDocument>::Failure(
			    EErrorCode::InvalidArgument, "ROX-CORE-JSON-DUPLICATE-KEY",
			    "The JSON document contains a duplicate object key.",
			    "Keep exactly one value for each object key before loading the document.");
		}

		return Result<CanonicalJsonDocument>::Success(core::Move(document));
	}
} // namespace rox::core
