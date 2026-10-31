#include "CanonicalJsonWriter.h"
#include <TextPrimitives.hpp>
#include <Vocabulary.hpp>
#include <SystemError.hpp>
#include <StringView.hpp>
#include <Span.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <Numeric.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <SchemaRegistry.h>
#include <SystemMemoryResource.h>
#include <ValueTraversal.h>

#include <nlohmann/json.hpp>

namespace rox::core
{
	namespace
	{
		template <typename T>
		Result<T> failure(EErrorCode code, StringView diagnostic, StringView message, StringView recovery)
		{
			return Result<T>::Failure(code, diagnostic, message, recovery);
		}

		template <typename T>
		Result<T> allocationFailure()
		{
			return failure<T>(EErrorCode::ExternalFailure, "ROX-CORE-JSON-WRITE-ALLOCATION", "Canonical JSON serialization could not allocate its bounded temporary state.", "Free memory, lower the write limits, and retry the operation.");
		}

		Result<void> byteLimitFailure()
		{
			return failure<void>(EErrorCode::Unsupported, "ROX-CORE-JSON-WRITE-BYTE-LIMIT", "The canonical JSON output exceeds the configured byte limit.", "Reduce the value size or raise JsonWriteLimits::MaxBytes intentionally.");
		}

		enum class EPieceKind
		{
			Literal,
			EscapedString,
			Inline
		};

		// Borrowed text points only into immutable inputs or static literals, never into the plan.
		struct Piece final
		{
			EPieceKind Kind = EPieceKind::Inline;
			rox::core::StringView Text;
			rox::core::usize Size = 0;
			char Inline[64]{};
		};

		char escapedCharacter(const unsigned char value) noexcept
		{
			switch (value)
			{
			case '"':
				return '"';
			case '\\':
				return '\\';
			case '\b':
				return 'b';
			case '\f':
				return 'f';
			case '\n':
				return 'n';
			case '\r':
				return 'r';
			case '\t':
				return 't';
			default:
				return 0;
			}
		}

		struct PlanFrame final
		{
			explicit PlanFrame(MemoryResource& resource) noexcept
			    : Fields(resource)
			{
			}

			const Value* Node = nullptr;
			const SchemaDescriptor* Schema = nullptr;

			Array<const SchemaFieldDescriptor*> Fields;
			Value::UnknownFieldMap::ConstIterator Unknown;

			TypeId ObjectType;
			TypeId ElementType;
			rox::core::Span<const SchemaArrayDescriptor> NestedArrays;
			ESchemaValueKind Kind = ESchemaValueKind::Null;
			ESchemaValueKind ElementKind = ESchemaValueKind::Null;

			rox::core::usize Index = 0;

			bool bGeneric = false;
			bool bFirst = true;
			bool bEntered = false;
		};

		class WritePlan final
		{
		public:
			WritePlan(MemoryResource& scratch, const SchemaRegistry& registry, const JsonWriteLimits& limits) noexcept
			    : m_Pieces(scratch)
			    , m_Registry(registry)
			    , m_Limits(limits)
			{
			}

			Result<void> PrepareObject(const Value& value, const SchemaDescriptor& schema)
			{
				Array<PlanFrame> frames(m_Pieces.GetResource());
				PlanFrame root(m_Pieces.GetResource());
				root.Node = &value;
				root.Schema = &schema;
				root.Kind = ESchemaValueKind::Object;

				if (!frames.Add(rox::core::Move(root)))
				{
					return allocationFailure<void>();
				}

				while (!frames.IsEmpty())
				{
					PlanFrame& frame = frames[frames.Size() - 1];
					Result<void> result = Result<void>::Success();

					if (!frame.bEntered)
					{
						ASSERT(frame.Node != nullptr && frame.Node->GetKind() == frame.Kind, "Prepared frames must retain the validated storage kind.");

						if (frame.Kind != ESchemaValueKind::Object && frame.Kind != ESchemaValueKind::Array)
						{
							result = prepareScalar(*frame.Node);

							if (!result)
							{
								return result;
							}
							frames.PopBack();
							continue;
						}

						if (frame.Kind == ESchemaValueKind::Object)
						{
							if (frame.ObjectType.IsValid())
							{
								frame.Schema = m_Registry.Find(frame.ObjectType);

								ASSERT(frame.Schema != nullptr, "Validated object types must resolve to a registered schema.");
							}

							const Value::Object* const object = frame.Node->GetObject();
							ASSERT(object != nullptr, "Prepared objects must retain object storage.");

							if (frame.Schema != nullptr)
							{
								result = prepareFields(*object, *frame.Schema, frame.Fields);

								if (!result)
								{
									return result;
								}
							}
							else
							{
								ASSERT(object->Fields.IsEmpty(), "Validated schema-free objects contain only name-keyed fields.");
							}

							frame.Unknown = object->UnknownFields.begin();
						}

						result = literal(frame.Kind == ESchemaValueKind::Object ? "{" : "[");
						if (!result)
						{
							return result;
						}

						frame.bEntered = true;
					}

					PlanFrame child(m_Pieces.GetResource());

					if (frame.Kind == ESchemaValueKind::Array)
					{
						const Value::Array* const array = frame.Node->GetArray();
						ASSERT(array != nullptr, "An entered array frame must retain array storage.");

						if (frame.Index != array->Size())
						{
							child.Node = &(*array)[frame.Index++];
							child.bGeneric = frame.bGeneric || (frame.ElementKind == ESchemaValueKind::Array && frame.NestedArrays.empty());
							child.Kind = child.bGeneric ? child.Node->GetKind() : frame.ElementKind;
							child.ObjectType = child.bGeneric ? TypeId::Invalid() : frame.ElementType;

							if (!frame.NestedArrays.empty())
							{
								child.ElementKind = frame.NestedArrays.front().ElementValueKind;
								child.ElementType = frame.NestedArrays.front().ElementType;
								child.NestedArrays = frame.NestedArrays.subspan(1);
							}

							if (!frame.bFirst)
							{
								result = literal(",");
							}

							frame.bFirst = false;
						}
					}
					else
					{
						const Value::Object* const object = frame.Node->GetObject();
						ASSERT(object != nullptr, "An entered object frame must retain object storage.");

						if (frame.Index != frame.Fields.Size())
						{
							const SchemaFieldDescriptor* const field = frame.Fields[frame.Index++];
							const Value* const found = object->Fields.Find(field->ID);
							child.Node = found != nullptr ? found : field->DefaultValue;
							ASSERT(child.Node != nullptr, "Prepared fields must have a stored value or a default.");
							child.Kind = field->ValueKind;
							child.ObjectType = field->ValueType;
							child.ElementKind = field->ElementValueKind;
							child.ElementType = field->ElementType;
							child.NestedArrays = field->NestedArrays;
							result = key(field->Name, frame.bFirst);
						}
						else if (frame.Unknown != object->UnknownFields.end())
						{
							// FlatMap retains byte ordering while frames move between scratch allocations.
							const Value::UnknownFieldMap::Entry& field = *frame.Unknown++;
							child.Node = &field.Value;
							child.Kind = field.Value.GetKind();
							child.bGeneric = true;
							result = key(field.GetKey().View(), frame.bFirst);
						}
					}

					if (!result)
					{
						return result;
					}

					if (child.Node == nullptr)
					{
						result = literal(frame.Kind == ESchemaValueKind::Object ? "}" : "]");
						if (!result)
						{
							return result;
						}
						frames.PopBack();
						continue;
					}

					if (frames.Size() >= ValueCloneLimits::HARD_MAXIMUM_DEPTH)
					{
						return failure<void>(EErrorCode::Unsupported, "ROX-CORE-JSON-WRITE-DEPTH-LIMIT", "The canonical JSON value exceeds the configured nesting depth.", "Reduce nesting or raise JsonWriteLimits::MaxDepth intentionally.");
					}
					// No references into frames survive growth; all payload views borrow immutable inputs.
					if (!frames.Add(rox::core::Move(child)))
					{
						return allocationFailure<void>();
					}
				}

				return Result<void>::Success();
			}

			Result<Array<Byte>> Emit(MemoryResource& outputResource) const
			{
				Array<Byte> output(outputResource);

				if (!output.ResizeUninitialized(m_ByteCount))
				{
					return failure<Array<Byte>>(
					    EErrorCode::ExternalFailure, "ROX-CORE-JSON-WRITE-ALLOCATION",
					    "Canonical JSON output allocation failed within the configured byte limit.",
					    "Free memory, lower the write limits, and retry the operation.");
				}
				char* cursor = reinterpret_cast<char*>(output.Data());

				for (const Piece& piece : m_Pieces)
				{
					if (piece.Kind == EPieceKind::EscapedString)
					{
						*cursor++ = '"';

						for (const unsigned char character : piece.Text)
						{
							const char escaped = escapedCharacter(character);

							if (escaped != 0)
							{
								*cursor++ = '\\';
								*cursor++ = escaped;
							}
							else if (character < 0x20)
							{
								constexpr char HEX[] = "0123456789abcdef";
								*cursor++ = '\\';
								*cursor++ = 'u';
								*cursor++ = '0';
								*cursor++ = '0';
								*cursor++ = HEX[character >> 4];
								*cursor++ = HEX[character & 15];
							}
							else
							{
								*cursor++ = static_cast<char>(character);
							}
						}
						*cursor++ = '"';
					}
					else
					{
						const char* const text = piece.Kind == EPieceKind::Inline ? piece.Inline : piece.Text.data();
						rox::core::CopyBytes(cursor, text, piece.Size);
						cursor += piece.Size;
					}
				}

				ASSERT(cursor == reinterpret_cast<char*>(output.Data()) + m_ByteCount, "Canonical JSON emission must match its prepared exact byte count.");
				return Result<Array<Byte>>::Success(rox::core::Move(output));
			}

		private:
			Result<void> append(Piece piece)
			{
				rox::core::usize next = 0;

				if (!AddMemorySize(m_ByteCount, piece.Size, &next) || static_cast<uint64>(next) > m_Limits.MaxBytes)
				{
					return byteLimitFailure();
				}

				if (!m_Pieces.Add(rox::core::Move(piece)))
				{
					return allocationFailure<void>();
				}

				m_ByteCount = next;
				return Result<void>::Success();
			}

			Result<void> literal(const rox::core::StringView text)
			{
				Piece piece;
				piece.Kind = EPieceKind::Literal;
				piece.Text = text;
				piece.Size = text.size();
				return append(rox::core::Move(piece));
			}

			Result<void> string(const rox::core::StringView text)
			{
				Piece piece;
				piece.Kind = EPieceKind::EscapedString;
				piece.Text = text;
				piece.Size = 2;

				for (const unsigned char character : text)
				{
					const rox::core::usize bytes = escapedCharacter(character) != 0 ? 2 : character < 0x20 ? 6 : 1;

					if (!AddMemorySize(piece.Size, bytes, &piece.Size))
					{
						return byteLimitFailure();
					}
				}

				return append(rox::core::Move(piece));
			}

			Result<void> key(const rox::core::StringView name, bool& bFirst)
			{
				if (!bFirst)
				{
					Result<void> comma = literal(",");
					if (!comma)
					{
						return comma;
					}
				}

				bFirst = false;

				Result<void> result = string(name);
				if (!result)
				{
					return result;
				}

				return literal(":");
			}

			template <typename T>
			Result<void> integer(const T value)
			{
				Piece piece;
				const ToCharsResult converted = rox::core::ToChars(piece.Inline, piece.Inline + sizeof(piece.Inline), value);
				ASSERT(converted.ec == rox::core::EErrorCondition{}, "The integer buffer must hold every 64-bit decimal value.");
				piece.Size = static_cast<rox::core::usize>(converted.ptr - piece.Inline);
				return append(rox::core::Move(piece));
			}

			Result<void> floating(const float64 value)
			{
				ASSERT(rox::core::IsFinite(value), "Validated JSON numbers must be finite.");
				Piece piece;
				// Only the pinned formatter is used; no library DOM, lexer or serializer owner is created.
				const char* const end = nlohmann::detail::to_chars(piece.Inline, piece.Inline + sizeof(piece.Inline), value == 0.0 ? 0.0 : value);
				piece.Size = static_cast<rox::core::usize>(end - piece.Inline);
				return append(rox::core::Move(piece));
			}

			Result<void> stableId(const StableValueId value)
			{
				Piece piece;
				constexpr char HEX[] = "0123456789abcdef";
				piece.Inline[0] = '"';

				for (uint32 index = 0; index < 16; ++index)
				{
					const uint32 shift = (15 - index) * 4;
					piece.Inline[index + 1] = HEX[(value.High >> shift) & 15];
					piece.Inline[index + 17] = HEX[(value.Low >> shift) & 15];
				}
				piece.Inline[33] = '"';
				piece.Size = 34;
				return append(rox::core::Move(piece));
			}

			Result<void> prepareFields(const Value::Object& object, const SchemaDescriptor& schema,
			                           Array<const SchemaFieldDescriptor*>& fields)
			{
				for (const SchemaFieldDescriptor& field : schema.Fields)
				{
					ASSERT(!object.UnknownFields.Contains(field.Name), "Validated objects cannot duplicate known fields in the unknown-field map.");

					if (!object.Fields.Contains(field.ID) && field.DefaultValue == nullptr)
					{
						ASSERT((static_cast<uint32>(field.Flags) & static_cast<uint32>(ESchemaFieldFlags::Optional)) != 0, "Validated required fields must have a stored value or a default.");
						continue;
					}
					if (!fields.Add(&field))
					{
						return allocationFailure<void>();
					}
				}

				rox::core::Sort(fields.begin(), fields.end(),
				          [](const SchemaFieldDescriptor* left, const SchemaFieldDescriptor* right)
				          {
					          return left->ID < right->ID;
				          });
				return Result<void>::Success();
			}

			Result<void> prepareScalar(const Value& value)
			{
				switch (value.GetKind())
				{
				case ESchemaValueKind::Null:
					return literal("null");
				case ESchemaValueKind::Boolean:
					return literal(*value.GetBoolean() ? "true" : "false");
				case ESchemaValueKind::SignedInteger:
					return integer(*value.GetSignedInteger());
				case ESchemaValueKind::UnsignedInteger:
					return integer(*value.GetUnsignedInteger());
				case ESchemaValueKind::FloatingPoint:
					return floating(*value.GetFloatingPoint());
				case ESchemaValueKind::String:
					return string(*value.GetString());
				case ESchemaValueKind::StableId:
					return stableId(*value.GetStableId());
				case ESchemaValueKind::Bytes:
				{
					Result<void> result = literal("[");

					if (!result)
					{
						return result;
					}
					bool bFirst = true;

					for (const Byte byte : *value.GetBytes())
					{
						if (!bFirst)
						{
							result = literal(",");

							if (!result)
							{
								return result;
							}
						}

						bFirst = false;
						result = integer(rox::core::ToInteger<uint8>(byte));

						if (!result)
						{
							return result;
						}
					}

					return literal("]");
				}
				default:
					ASSERT(false, "Scalar preparation requires a non-container value kind.");
				}
			}

		private:
			Array<Piece> m_Pieces;

			const SchemaRegistry& m_Registry;
			const JsonWriteLimits& m_Limits;

			rox::core::usize m_ByteCount = 0;
		};
	} // namespace

	Result<Array<Byte>> WriteCanonicalJsonBytes(const Value& value, const SchemaDescriptor& schema, const SchemaRegistry& registry, MemoryResource& outputResource, const JsonWriteLimits& limits)
	{
		return WriteCanonicalJsonBytes(value, schema, registry, outputResource, limits, SystemMemoryResource::Get());
	}

	Result<Array<Byte>> WriteCanonicalJsonBytes(const Value& value, const SchemaDescriptor& schema, const SchemaRegistry& registry, MemoryResource& outputResource, const JsonWriteLimits& limits, MemoryResource& scratchResource)
	{
		WritePlan plan(scratchResource, registry, limits);
		const Result<void> prepared = plan.PrepareObject(value, schema);

		if (!prepared)
		{
			return Result<Array<Byte>>::Failure(prepared.GetError());
		}

		return plan.Emit(outputResource);
	}
} // namespace rox::core
