#include <SchemaCatalogJson.h>
#include <TextPrimitives.hpp>
#include <Vocabulary.hpp>
#include <SystemError.hpp>
#include <StringView.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <SchemaRegistry.h>
#include <Utf8.h>
#include <Value.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>

namespace rox::core
{
	namespace
	{
		enum class EWriteFailure : uint8
		{
			None,
			InvalidInput,
			Limit,
			Memory
		};

		struct WriteContext final
		{
			Array<Byte>& Output;
			const SchemaCatalogJsonLimits& Limits;

			uint64 ValueCount = 0;
			EWriteFailure Failure = EWriteFailure::None;
		};

		[[nodiscard]] Error makeError(const EErrorCode code, const StringView diagnosticID, const StringView message, const StringView recoveryHint)
		{
			return MakeError(code, diagnosticID, message, recoveryHint);
		}

		[[nodiscard]] Result<Array<Byte>> failureResult(const EWriteFailure failure, MemoryResource& resource)
		{
			Array<Byte> unused(resource);

			switch (failure)
			{
			case EWriteFailure::InvalidInput:
				return Result<Array<Byte>>::Failure(makeError(
				    EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-JSON-VALUE",
				    "The schema catalog contains a value that cannot be represented by the current JSON contract.",
				    "Register bounded UTF-8 metadata and finite canonical schema values."));
			case EWriteFailure::Limit:
				return Result<Array<Byte>>::Failure(
				    makeError(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-JSON-LIMIT",
				              "The schema catalog JSON exceeds its configured output limits.",
				              "Reduce the catalog or intentionally select a larger valid limit profile."));
			case EWriteFailure::Memory:
				return Result<Array<Byte>>::Failure(
				    makeError(EErrorCode::ExternalFailure, "ROX-CORE-SCHEMA-CATALOG-JSON-MEMORY",
				              "Core could not allocate the schema catalog JSON.",
				              "Release transient memory and retry with the unchanged frozen registry."));
			case EWriteFailure::None:
				break;

			default:
				break;
			}

			ASSERT(false, "Schema catalog JSON failure mapping requires a concrete failure.");
			return Result<Array<Byte>>::Success(rox::core::Move(unused));
		}

		[[nodiscard]] bool consumeValue(WriteContext* const context, const uint32 depth) noexcept
		{
			ASSERT(context != nullptr, "Schema catalog JSON value counting requires a valid context.");

			if (depth > context->Limits.MaximumDepth || context->ValueCount >= context->Limits.MaximumValues)
			{
				context->Failure = EWriteFailure::Limit;
				return false;
			}

			++context->ValueCount;
			return true;
		}

		[[nodiscard]] bool appendByte(WriteContext* const context, const char value) noexcept
		{
			ASSERT(context != nullptr, "Schema catalog JSON writing requires a valid context.");

			if (context->Output.Size() >= context->Limits.MaximumBytes)
			{
				context->Failure = EWriteFailure::Limit;
				return false;
			}

			if (!context->Output.Add(static_cast<Byte>(static_cast<uint8>(value))))
			{
				context->Failure = EWriteFailure::Memory;
				return false;
			}

			return true;
		}

		[[nodiscard]] bool appendText(WriteContext* const context, const rox::core::StringView text) noexcept
		{
			ASSERT(context != nullptr, "Schema catalog JSON text writing requires a valid context.");

			if (text.size() > context->Limits.MaximumBytes - context->Output.Size())
			{
				context->Failure = EWriteFailure::Limit;
				return false;
			}

			if (!context->Output.Grow(context->Output.Size() + text.size()))
			{
				context->Failure = EWriteFailure::Memory;
				return false;
			}

			for (const char value : text)
			{
				const bool bAdded = context->Output.Add(static_cast<Byte>(static_cast<uint8>(value)));
				ASSERT(bAdded, "Reserved schema catalog JSON storage must accept every text byte.");
			}

			return true;
		}

		[[nodiscard]] bool appendJsonString(WriteContext* const context, const rox::core::StringView text) noexcept
		{
			if (!IsValidUtf8(StringView(text.data(), text.size())))
			{
				context->Failure = EWriteFailure::InvalidInput;
				return false;
			}

			if (!appendByte(context, '"'))
			{
				return false;
			}
			static constexpr char HEX_DIGITS[] = "0123456789abcdef";

			for (const unsigned char value : text)
			{
				switch (value)
				{
				case '"':
					if (!appendText(context, "\\\""))
					{
						return false;
					}
					break;
				case '\\':
					if (!appendText(context, "\\\\"))
					{
						return false;
					}
					break;
				case '\b':
					if (!appendText(context, "\\b"))
					{
						return false;
					}
					break;
				case '\f':
					if (!appendText(context, "\\f"))
					{
						return false;
					}
					break;
				case '\n':
					if (!appendText(context, "\\n"))
					{
						return false;
					}
					break;
				case '\r':
					if (!appendText(context, "\\r"))
					{
						return false;
					}
					break;
				case '\t':
					if (!appendText(context, "\\t"))
					{
						return false;
					}
					break;
				default:
					if (value < 0x20)
					{
						const char escaped[] = {
						    '\\', 'u', '0', '0', HEX_DIGITS[(value >> 4) & 0xf], HEX_DIGITS[value & 0xf]};
						if (!appendText(context, rox::core::StringView(escaped, sizeof(escaped))))
						{
							return false;
						}
					}
					else if (!appendByte(context, static_cast<char>(value)))
					{
						return false;
					}
					break;
				}
			}

			return appendByte(context, '"');
		}

		template <typename TValue>
		[[nodiscard]] bool appendInteger(WriteContext* const context, const TValue value) noexcept
		{
			char text[32] = {};
			const rox::core::ToCharsResult result = rox::core::ToChars(rox::core::Begin(text), rox::core::End(text), value);
			ASSERT(result.ec == rox::core::EErrorCondition{}, "Schema catalog integer formatting must fit its fixed buffer.");
			return appendText(context, rox::core::StringView(text, static_cast<rox::core::usize>(result.ptr - text)));
		}

		[[nodiscard]] bool appendFloat(WriteContext* const context, const float64 value) noexcept
		{
			if (!rox::core::IsFinite(value))
			{
				context->Failure = EWriteFailure::InvalidInput;
				return false;
			}
			char text[64] = {};
			const rox::core::ToCharsResult result = rox::core::ToChars(rox::core::Begin(text), rox::core::End(text), value == 0.0 ? 0.0 : value, rox::core::ECharsFormat::general, rox::core::NumericLimits<float64>::max_digits10);

			if (result.ec != rox::core::EErrorCondition{})
			{
				context->Failure = EWriteFailure::InvalidInput;
				return false;
			}

			return appendText(context, rox::core::StringView(text, static_cast<rox::core::usize>(result.ptr - text)));
		}

		template <typename TTag>
		[[nodiscard]] bool appendStrongId(WriteContext* const context, const StrongId<TTag> id) noexcept
		{
			// Descriptor IDs are validated at registration; value IDs may represent an empty reference.
			static constexpr char HEX_DIGITS[] = "0123456789abcdef";
			char text[32] = {};
			for (uint32 index = 0; index < 16; ++index)
			{
				const uint32 shift = (15 - index) * 4;
				text[index] = HEX_DIGITS[(id.High >> shift) & 0xf];
				text[index + 16] = HEX_DIGITS[(id.Low >> shift) & 0xf];
			}

			return appendJsonString(context, rox::core::StringView(text, sizeof(text)));
		}

		template <typename TTag>
		[[nodiscard]] bool appendNullableStrongId(WriteContext* const context, const StrongId<TTag> id) noexcept
		{
			return id.IsValid() ? appendStrongId(context, id) : appendText(context, "null");
		}

		[[nodiscard]] const char* valueKindName(const ESchemaValueKind kind) noexcept
		{
			switch (kind)
			{
			case ESchemaValueKind::Null:
				return "null";
			case ESchemaValueKind::Boolean:
				return "boolean";
			case ESchemaValueKind::SignedInteger:
				return "signedInteger";
			case ESchemaValueKind::UnsignedInteger:
				return "unsignedInteger";
			case ESchemaValueKind::FloatingPoint:
				return "floatingPoint";
			case ESchemaValueKind::String:
				return "string";
			case ESchemaValueKind::StableId:
				return "stableId";
			case ESchemaValueKind::Bytes:
				return "bytes";
			case ESchemaValueKind::Array:
				return "array";
			case ESchemaValueKind::Object:
				return "object";
			default:
				return nullptr;
			}
		}

		[[nodiscard]] bool appendValue(WriteContext* context, const Value& value, uint32 depth) noexcept;

		[[nodiscard]] bool appendValueObject(WriteContext* const context, const Value::Object& object, const uint32 depth) noexcept
		{
			if (!appendText(context, "{\"fields\":{"))
			{
				return false;
			}
			bool bFirst = true;

			for (const Value::FieldMap::Entry& entry : object.Fields)
			{
				const FieldId& fieldID = entry.GetKey();
				const Value& fieldValue = entry.Value;

				if ((!bFirst && !appendByte(context, ',')) || !appendStrongId(context, fieldID) || !appendByte(context, ':') || !appendValue(context, fieldValue, depth + 1))
				{
					return false;
				}

				bFirst = false;
			}

			if (!appendText(context, "},\"unknownFields\":{"))
			{
				return false;
			}

			bFirst = true;

			for (const Value::UnknownFieldMap::Entry& entry : object.UnknownFields)
			{
				const StringView name = entry.GetKey().View();
				const Value& fieldValue = entry.Value;

				if ((!bFirst && !appendByte(context, ',')) || !appendJsonString(context, name) || !appendByte(context, ':') || !appendValue(context, fieldValue, depth + 1))
				{
					return false;
				}

				bFirst = false;
			}

			return appendText(context, "}}");
		}

		[[nodiscard]] bool appendValue(WriteContext* const context, const Value& value, const uint32 depth) noexcept
		{
			if (!consumeValue(context, depth))
			{
				return false;
			}

			switch (value.GetKind())
			{
			case ESchemaValueKind::Null:
				return appendText(context, "null");
			case ESchemaValueKind::Boolean:
				ASSERT(value.GetBoolean() != nullptr, "Boolean schema Value must retain Boolean storage.");
				return appendText(context, *value.GetBoolean() ? "true" : "false");
			case ESchemaValueKind::SignedInteger:
				ASSERT(value.GetSignedInteger() != nullptr, "Signed schema Value must retain signed storage.");
				return appendInteger(context, *value.GetSignedInteger());
			case ESchemaValueKind::UnsignedInteger:
				ASSERT(value.GetUnsignedInteger() != nullptr, "Unsigned schema Value must retain unsigned storage.");
				return appendInteger(context, *value.GetUnsignedInteger());
			case ESchemaValueKind::FloatingPoint:
				ASSERT(value.GetFloatingPoint() != nullptr, "Floating schema Value must retain floating storage.");
				return appendFloat(context, *value.GetFloatingPoint());
			case ESchemaValueKind::String:
			{
				const StringView* const text = value.GetString();
				ASSERT(text != nullptr, "String schema Value must retain string storage.");
				return appendJsonString(context, *text);
			}
			case ESchemaValueKind::StableId:
			{
				const StableValueId* const id = value.GetStableId();
				ASSERT(id != nullptr, "Stable-ID schema Value must retain stable-ID storage.");
				return appendStrongId(context, *id);
			}
			case ESchemaValueKind::Bytes:
			{
				const Span<const Byte>* const bytes = value.GetBytes();
				ASSERT(bytes != nullptr, "Bytes schema Value must retain byte storage.");

				if (!appendByte(context, '['))
				{
					return false;
				}

				for (rox::core::usize index = 0; index < bytes->Size(); ++index)
				{
					if ((index != 0 && !appendByte(context, ',')) ||
					    !appendInteger(context, rox::core::ToInteger<uint8>((*bytes)[index])))
					{
						return false;
					}
				}

				return appendByte(context, ']');
			}
			case ESchemaValueKind::Array:
			{
				const Value::Array* const values = value.GetArray();
				ASSERT(values != nullptr, "Array schema Value must retain array storage.");

				if (!appendByte(context, '['))
				{
					return false;
				}

				for (rox::core::usize index = 0; index < values->Size(); ++index)
				{
					if ((index != 0 && !appendByte(context, ',')) || !appendValue(context, (*values)[index], depth + 1))
					{
						return false;
					}
				}

				return appendByte(context, ']');
			}
			case ESchemaValueKind::Object:
			{
				const Value::Object* const object = value.GetObject();
				ASSERT(object != nullptr, "Object schema Value must retain object storage.");
				return appendValueObject(context, *object, depth);
			}
			default:
				context->Failure = EWriteFailure::InvalidInput;
				return false;
			}
		}

		[[nodiscard]] bool appendOptionalValue(WriteContext* const context, const Value* const value, const uint32 depth) noexcept
		{
			return value != nullptr ? appendValue(context, *value, depth) : appendText(context, "null");
		}

		[[nodiscard]] bool appendOptionalLength(WriteContext* const context, const rox::core::Optional<uint64>& value) noexcept
		{
			return value.has_value() ? appendInteger(context, *value) : appendText(context, "null");
		}

		[[nodiscard]] bool appendFlag(WriteContext* const context, bool& bHasPrevious, const ESchemaFieldFlags flag,
		    const char* const name, const ESchemaFieldFlags value) noexcept
		{
			if ((static_cast<uint32>(value) & static_cast<uint32>(flag)) == 0)
			{
				return true;
			}

			if ((bHasPrevious && !appendByte(context, ',')) || !appendJsonString(context, name))
			{
				return false;
			}

			bHasPrevious = true;
			return true;
		}

		[[nodiscard]] bool appendFlags(WriteContext* const context, const ESchemaFieldFlags flags) noexcept
		{
			if (!appendByte(context, '['))
			{
				return false;
			}

			bool bHasPrevious = false;

			if (!appendFlag(context, bHasPrevious, ESchemaFieldFlags::Optional, "optional", flags) ||
			    !appendFlag(context, bHasPrevious, ESchemaFieldFlags::Replicated, "replicated", flags) ||
			    !appendFlag(context, bHasPrevious, ESchemaFieldFlags::EditorOnly, "editorOnly", flags))
			{
				return false;
			}

			return appendByte(context, ']');
		}

		[[nodiscard]] bool appendEnumEntries(WriteContext* context, const SchemaFieldDescriptor& field) noexcept
		{
			if (field.EnumEntries.empty())
			{
				return true;
			}

			if (!appendText(context, ",\"enumEntries\":["))
			{
				return false;
			}

			bool first = true;

			for (const SchemaEnumEntry& entry : field.EnumEntries)
			{
				if (!consumeValue(context, 1))
				{
					return false;
				}

				if (!first && !appendByte(context, ','))
				{
					return false;
				}

				first = false;
				char bits[20];
				const ToCharsResult converted = rox::core::ToChars(bits, bits + sizeof(bits), entry.Bits);
				ASSERT(converted.ec == rox::core::EErrorCondition{}, "Enum bits must fit the uint64 decimal buffer.");
				if (!appendText(context, "{\"name\":") ||
					!appendJsonString(context, entry.Name) ||
				    !appendText(context, ",\"bits\":") ||
				    !appendJsonString(context, {bits, static_cast<rox::core::usize>(converted.ptr - bits)}) ||
				    !appendByte(context, '}'))
				{
					return false;
				}
			}

			return appendByte(context, ']');
		}

		[[nodiscard]] bool appendNestedArrays(WriteContext* context, const SchemaFieldDescriptor& field) noexcept
		{
			if (field.NestedArrays.empty())
			{
				return true;
			}

			if (!appendText(context, ",\"nestedArrays\":["))
			{
				return false;
			}
			bool bFirst = true;

			for (const SchemaArrayDescriptor& level : field.NestedArrays)
			{
				if (!consumeValue(context, 1) || (!bFirst && !appendByte(context, ',')))
				{
					return false;
				}

				bFirst = false;

				if (!appendText(context, "{\"elementValueKind\":") ||
				    !appendJsonString(context, valueKindName(level.ElementValueKind)) ||
				    !appendText(context, ",\"elementType\":") ||
					!appendNullableStrongId(context, level.ElementType) ||
				    !appendText(context, ",\"minimumLength\":") ||
					!appendInteger(context, level.MinimumLength) ||
				    !appendText(context, ",\"maximumLength\":") ||
					!appendInteger(context, level.MaximumLength) ||
				    !appendByte(context, '}'))
				{
					return false;
				}
			}

			return appendByte(context, ']');
		}

		[[nodiscard]] bool hasDisplayMetadata(const SchemaDisplayMetadata& display) noexcept
		{
			return !display.DisplayName.empty() ||
				!display.Description.empty() ||
				!display.Category.empty() ||
			    !display.Icon.empty() ||
				display.Hint != ESchemaUiHint::None ||
				!display.RefKind.empty() ||
			    !display.Unit.empty() ||
				display.Step != 0.0f ||
				display.Order != 0;
		}

		[[nodiscard]] const char* uiHintName(ESchemaUiHint hint) noexcept
		{
			switch (hint)
			{
			case ESchemaUiHint::None:
				return "none";
			case ESchemaUiHint::Color:
				return "color";
			case ESchemaUiHint::Slider:
				return "slider";
			case ESchemaUiHint::Angle:
				return "angle";
			case ESchemaUiHint::Percent:
				return "percent";
			case ESchemaUiHint::Multiline:
				return "multiline";
			case ESchemaUiHint::Path:
				return "path";
			case ESchemaUiHint::AssetRef:
				return "assetRef";
			case ESchemaUiHint::EntityRef:
				return "entityRef";
			case ESchemaUiHint::Quaternion:
				return "quaternion";
			case ESchemaUiHint::Hidden:
				return "hidden";
			case ESchemaUiHint::ReadOnly:
				return "readOnly";
			default:
				return nullptr;
			}
		}

		[[nodiscard]] bool appendOptionalDisplayText(WriteContext* const context, StringView key, StringView value) noexcept
		{
			return value.empty() || (consumeValue(context, 1) && appendText(context, key) && appendJsonString(context, value));
		}

		[[nodiscard]] bool appendDisplay(WriteContext* context, const SchemaDisplayMetadata& display) noexcept
		{
			const char* const hint = uiHintName(display.Hint);
			ASSERT(hint != nullptr, "Registered display metadata must contain a known UI hint.");

			// Omit absent additions so existing metadata-free catalog bytes stay exact.
			return appendText(context, "{\"name\":") &&
			       appendJsonString(context, display.DisplayName) &&
			       appendText(context, ",\"description\":") &&
			       appendJsonString(context, display.Description) &&
			       appendText(context, ",\"category\":") &&
			       appendJsonString(context, display.Category) &&
			       appendOptionalDisplayText(context, ",\"icon\":", display.Icon) &&
			       (display.Hint == ESchemaUiHint::None || appendOptionalDisplayText(context, ",\"hint\":", hint)) &&
			       appendOptionalDisplayText(context, ",\"refKind\":", display.RefKind) &&
			       appendOptionalDisplayText(context, ",\"unit\":", display.Unit) &&
			       (display.Step == 0.0f ||
			        (consumeValue(context, 1) && appendText(context, ",\"step\":") && appendFloat(context, display.Step))) &&
			       (display.Order == 0 ||
			        (consumeValue(context, 1) && appendText(context, ",\"order\":") && appendInteger(context, display.Order))) &&
			       appendByte(context, '}');
		}

		[[nodiscard]] bool appendField(WriteContext* const context, const SchemaFieldDescriptor& field) noexcept
		{
			const char* const valueKind = valueKindName(field.ValueKind);
			const char* const elementKind = valueKindName(field.ElementValueKind);

			if (valueKind == nullptr || elementKind == nullptr)
			{
				context->Failure = EWriteFailure::InvalidInput;
				return false;
			}

			return appendText(context, "{\"fieldId\":") && appendStrongId(context, field.ID) &&
			       appendText(context, ",\"name\":") && appendJsonString(context, field.Name) &&
			       appendText(context, ",\"valueType\":") && appendNullableStrongId(context, field.ValueType) &&
			       appendText(context, ",\"valueKind\":") && appendJsonString(context, valueKind) &&
			       appendText(context, ",\"elementValueKind\":") && appendJsonString(context, elementKind) &&
			       appendText(context, ",\"elementType\":") && appendNullableStrongId(context, field.ElementType) &&
			       appendText(context, ",\"flags\":") && appendFlags(context, field.Flags) &&
			       appendText(context, ",\"hasDefault\":") &&
			       appendText(context, field.DefaultValue != nullptr ? "true" : "false") &&
			       appendText(context, ",\"defaultValue\":") && appendOptionalValue(context, field.DefaultValue, 1) &&
			       appendText(context, ",\"constraints\":{\"minimum\":") &&
			       appendOptionalValue(context, field.Constraints.Minimum, 1) && appendText(context, ",\"maximum\":") &&
			       appendOptionalValue(context, field.Constraints.Maximum, 1) &&
			       appendText(context, ",\"minimumLength\":") &&
			       appendOptionalLength(context, field.Constraints.MinimumLength) &&
			       appendText(context, ",\"maximumLength\":") &&
			       appendOptionalLength(context, field.Constraints.MaximumLength) &&
			       appendText(context, "},\"display\":") && appendDisplay(context, field.Display) &&
			       appendEnumEntries(context, field) && appendNestedArrays(context, field) &&
			       (!field.ElementMaximumLength.has_value() || (appendText(context, ",\"elementMaximumLength\":") &&
			       appendOptionalLength(context, field.ElementMaximumLength))) &&
			       appendByte(context, '}');
		}

		[[nodiscard]] bool appendSchema(WriteContext* const context, const SchemaDescriptor& schema, MemoryResource& resource) noexcept
		{
			Array<const SchemaFieldDescriptor*> fields(resource);

			if (!fields.Reserve(schema.Fields.size()))
			{
				context->Failure = EWriteFailure::Memory;
				return false;
			}

			for (const SchemaFieldDescriptor& field : schema.Fields)
			{
				const bool bAdded = fields.Add(&field);
				ASSERT(bAdded, "Reserved schema catalog field order must accept every field.");
			}

			rox::core::Sort(fields.begin(), fields.end(),
			          [](const SchemaFieldDescriptor* const left, const SchemaFieldDescriptor* const right) noexcept
			          {
				          ASSERT(left != nullptr && right != nullptr, "Schema catalog field ordering requires valid descriptors.");
				          return left->ID < right->ID;
			          });

			if (!appendText(context, "{\"schemaId\":") || !appendStrongId(context, schema.ID) ||
			    !appendText(context, ",\"typeId\":") || !appendStrongId(context, schema.Type) ||
			    !appendText(context, ",\"version\":") || !appendInteger(context, schema.Version) ||
			    !appendText(context, ",\"name\":") || !appendJsonString(context, schema.Name) ||
			    !appendText(context, ",\"fields\":["))
			{
				return false;
			}

			for (rox::core::usize index = 0; index < fields.Size(); ++index)
			{
				if ((index != 0 && !appendByte(context, ',')) || !appendField(context, *fields[index]))
				{
					return false;
				}
			}

			return appendByte(context, ']') && (!hasDisplayMetadata(schema.Display) || (consumeValue(context, 1) && appendText(context, ",\"display\":") && appendDisplay(context, schema.Display))) && appendByte(context, '}');
		}
	} // namespace

	Result<Array<Byte>> WriteSchemaCatalogJson(const SchemaRegistry& registry, MemoryResource& resource, const SchemaCatalogJsonLimits& limits)
	{
		if (!limits.IsValidProfile())
		{
			return Result<Array<Byte>>::Failure(makeError(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-JSON-LIMITS", "The schema catalog JSON limit profile is invalid.", "Use nonzero limits at or below the documented hard maxima."));
		}

		ASSERT(registry.IsFrozen(), "Schema catalog JSON requires a frozen registry.");

		Array<const SchemaDescriptor*> schemas(resource);

		if (!schemas.Reserve(registry.GetSchemaCount()))
		{
			return failureResult(EWriteFailure::Memory, resource);
		}

		for (rox::core::usize index = 0; index < registry.GetSchemaCount(); ++index)
		{
			const bool bAdded = schemas.Add(&registry.GetSchemaByIndex(index));
			ASSERT(bAdded, "Reserved schema catalog ordering storage must accept every schema.");
		}

		rox::core::Sort(schemas.begin(), schemas.end(),
		          [](const SchemaDescriptor* const left, const SchemaDescriptor* const right) noexcept
		          {
			          ASSERT(left != nullptr && right != nullptr, "Schema catalog ordering requires valid descriptors.");
			          return left->ID < right->ID;
		          });

		Array<Byte> output(resource);
		WriteContext context{output, limits};
		if (!appendText(&context, "{\"format\":\"roxell.schema-catalog\",\"version\":1,\"schemas\":["))
		{
			return failureResult(context.Failure, resource);
		}

		for (rox::core::usize index = 0; index < schemas.Size(); ++index)
		{
			if ((index != 0 && !appendByte(&context, ',')) || !appendSchema(&context, *schemas[index], resource))
			{
				return failureResult(context.Failure, resource);
			}
		}

		if (!appendText(&context, "]}"))
		{
			return failureResult(context.Failure, resource);
		}

		ASSERT(context.Failure == EWriteFailure::None && output.Size() <= limits.MaximumBytes, "A successful schema catalog JSON write must satisfy its limits.");
		return Result<Array<Byte>>::Success(rox::core::Move(output));
	}
} // namespace rox::core
