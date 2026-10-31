#include <SchemaRegistry.h>
#include "SchemaRegistryInternal.h"
#include <Assertion.h>
#include <Utf8.h>
#include <Numeric.hpp>
#include <TextPrimitives.hpp>
#include <Utility.hpp>

namespace rox::core
{
	namespace
	{
		bool fieldFailure(Error& error, StringView name)
		{
			String message(error.Message.GetResource());
			// Keep the primary failure if adding diagnostic context exhausts its resource.
			if (message.Append("Field \"") && message.Append(name) && message.Append("\": ") && message.Append(error.Message.View()))
			{
				error.Message = rox::core::Move(message);
			}

			return false;
		}

		bool elementFailure(Error& error, usize index)
		{
			char digits[24];
			const ToCharsResult converted = ToChars(digits, digits + sizeof(digits), index);
			ASSERT(converted.ec == EErrorCondition{}, "An array index must fit the diagnostic buffer.");
			String message(error.Message.GetResource());
			if (message.Append("Element [") &&
				message.Append({digits, static_cast<usize>(converted.ptr - digits)}) && message.Append("]: ") &&
				message.Append(error.Message.View()))
			{
				error.Message = rox::core::Move(message);
			}

			return false;
		}

		bool storeValidationResult(Result<void> result, Error& error);
		bool setValidationFailure(Error& error, EErrorCode code, StringView diagnosticId, StringView message, StringView recoveryHint);

		[[nodiscard]] bool validateScalarValue(const Value& value, Error& error);

		[[nodiscard]] uint64 getLength(const Value& value, const SchemaDescriptor* objectSchema = nullptr) noexcept;

		[[nodiscard]] bool hasFlag(ESchemaFieldFlags value, ESchemaFieldFlags flag) noexcept;

		bool validateFieldHead(const SchemaFieldDescriptor& field, const Value& value, Error& error);
		bool validateFieldConstraints(const SchemaFieldDescriptor& field, const Value& value, const SchemaDescriptor* objectSchema, Error& error);

		bool storeValidationResult(Result<void> result, Error& error)
		{
			if (result)
			{
				return true;
			}

			error = rox::core::Move(result.GetError());
			return false;
		}

		bool setValidationFailure(Error& error, EErrorCode code, StringView diagnosticId, StringView message, StringView recoveryHint)
		{
			error = MakeError(code, diagnosticId, message, recoveryHint);
			return false;
		}

		[[nodiscard]] bool validateScalarValue(const Value& value, Error& error)
		{
			if (value.GetKind() == ESchemaValueKind::FloatingPoint)
			{
				const float64* const floatingPoint = value.GetFloatingPoint();
				ASSERT(floatingPoint != nullptr, "Floating-point Value kind must have floating-point storage.");

				if (!rox::core::IsFinite(*floatingPoint))
				{
					return setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-NON-FINITE-NUMBER", "Authored values cannot contain NaN or infinity.", "Provide a finite floating-point value.");
				}
			}

			if (value.GetKind() == ESchemaValueKind::String)
			{
				const StringView* const string = value.GetString();
				ASSERT(string != nullptr, "String Value kind must have string storage.");

				if (!IsValidUtf8(detail::ToStringView(*string)))
				{
					return setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-UTF8", "Authored strings must contain well-formed UTF-8.", "Replace invalid, overlong, surrogate, or truncated UTF-8 sequences.");
				}
			}

			return true;
		}

		[[nodiscard]] uint64 getLength(const Value& value, const SchemaDescriptor* objectSchema) noexcept
		{
			ASSERT(detail::HasLength(value.GetKind()), "Length queries require a value kind with defined length.");

			switch (value.GetKind())
			{
			case ESchemaValueKind::String:
				ASSERT(value.GetString() != nullptr, "String Value kind must have string storage.");
				return static_cast<uint64>(value.GetString()->size());
			case ESchemaValueKind::Bytes:
				ASSERT(value.GetBytes() != nullptr, "Byte array Value kind must have byte array storage.");
				return static_cast<uint64>(value.GetBytes()->Size());
			case ESchemaValueKind::Array:
				ASSERT(value.GetArray() != nullptr, "Array Value kind must have array storage.");
				return static_cast<uint64>(value.GetArray()->Size());
			case ESchemaValueKind::Object: {
				ASSERT(value.GetObject() != nullptr, "Object Value kind must have object storage.");
				const Value::Object& object = *value.GetObject();
				uint64 length = static_cast<uint64>(object.Fields.Size()) + object.UnknownFields.Size();
				if (objectSchema != nullptr)
				{
					// Constraints describe the same default-expanded object that the codecs publish.
					for (const SchemaFieldDescriptor& field : objectSchema->Fields)
					{
						if (field.DefaultValue != nullptr && !object.Fields.Contains(field.ID))
						{
							++length;
						}
					}
				}

				return length;
			}
			default:
				ASSERT(false, "Length query must handle every supported sized value kind.");
			}
		}

		[[nodiscard]] bool hasFlag(ESchemaFieldFlags value, ESchemaFieldFlags flag) noexcept
		{
			return (static_cast<uint32>(value) & static_cast<uint32>(flag)) != 0;
		}

		bool validateFieldHead(const SchemaFieldDescriptor& field, const Value& value, Error& error)
		{
			if (value.GetKind() != field.ValueKind)
			{
				return setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-FIELD-TYPE-MISMATCH", "A field value does not match its schema value kind.", "Convert the field to the value kind declared by its descriptor.");
			}

			if (field.ValueKind != ESchemaValueKind::Array && field.ValueKind != ESchemaValueKind::Object)
			{
				if (!validateScalarValue(value, error))
				{
					return false;
				}

				if (!field.EnumEntries.empty() && !storeValidationResult(ValidateSchemaEnumValue(field, value), error))
				{
					return false;
				}
			}

			return true;
		}

		bool validateFieldConstraints(const SchemaFieldDescriptor& field, const Value& value,
									  const SchemaDescriptor* objectSchema, Error& error)
		{
			if (field.Constraints.Minimum != nullptr && detail::CompareNumeric(value, *field.Constraints.Minimum) < 0)
			{
				return setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-BELOW-MINIMUM", "A numeric field is below its declared minimum.", "Increase the field value to the allowed range.");
			}

			if (field.Constraints.Maximum != nullptr && detail::CompareNumeric(value, *field.Constraints.Maximum) > 0)
			{
				return setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-ABOVE-MAXIMUM", "A numeric field is above its declared maximum.", "Decrease the field value to the allowed range.");
			}

			if (!field.Constraints.MinimumLength.has_value() && !field.Constraints.MaximumLength.has_value())
			{
				return true;
			}

			const uint64 length = getLength(value, objectSchema);
			if (field.Constraints.MinimumLength.has_value() && length < *field.Constraints.MinimumLength)
			{
				return setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-BELOW-MINIMUM-LENGTH", "A field is shorter than its declared minimum length.", "Provide enough values or UTF-8 bytes.");
			}

			if (field.Constraints.MaximumLength.has_value() && length > *field.Constraints.MaximumLength)
			{
				return setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-ABOVE-MAXIMUM-LENGTH", "A field is longer than its declared maximum length.", "Remove values or UTF-8 bytes.");
			}

			return true;
		}
	} // namespace

	Result<void> SchemaRegistry::Validate(TypeId type, const Value& value) const
	{
		const SchemaDescriptor* const descriptor = Find(type);

		if (descriptor == nullptr)
		{
			return detail::Failure(EErrorCode::NotFound, "ROX-CORE-SCHEMA-TYPE-NOT-FOUND", "The requested value type is not registered.", "Register the schema before validating values of this type.");
		}

		const SchemaCanonicalizationLimits limits;
		CanonicalizationContext context{*m_Resource, limits, limits.MaximumValues};
		Result<void> bounded = context.CheckInput(value);

		if (!bounded)
		{
			return bounded;
		}

		return validateObject(*descriptor, value);
	}

	bool SchemaRegistry::validateGenericValueImpl(const Value& value, Error& error) const
	{
		switch (value.GetKind())
		{
		case ESchemaValueKind::Null:
		case ESchemaValueKind::Boolean:
		case ESchemaValueKind::UnsignedInteger:
			return true;
		case ESchemaValueKind::SignedInteger:
			ASSERT(value.GetSignedInteger() != nullptr, "Signed integer Value kind must have signed integer storage.");

			if (*value.GetSignedInteger() >= 0)
			{
				return setValidationFailure(error, EErrorCode::InvalidArgument,
											"ROX-CORE-SCHEMA-GENERIC-SIGNED-INTEGER",
											"A non-negative signed integer loses its signed kind in schema-free JSON.",
											"Use an unsigned integer or bind the field to a schema.");
			}

			return true;
		case ESchemaValueKind::FloatingPoint:
		case ESchemaValueKind::String:
			return validateScalarValue(value, error);
		case ESchemaValueKind::StableId:
		case ESchemaValueKind::Bytes:
			return setValidationFailure(
				error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-GENERIC-TYPE-LOSS",
				"Stable IDs and byte arrays lose their kind in schema-free JSON.",
				"Bind the field to a schema or store an explicitly JSON-native string or array.");
		case ESchemaValueKind::Array:
			ASSERT(value.GetArray() != nullptr, "Array Value kind must have array storage.");

			for (usize index = 0; index < value.GetArray()->Size(); ++index)
			{
				if (!validateGenericValueImpl((*value.GetArray())[index], error))
				{
					return elementFailure(error, index);
				}
			}

			return true;
		case ESchemaValueKind::Object:
		{
			ASSERT(value.GetObject() != nullptr, "Object Value kind must have object storage.");
			const Value::Object& object = *value.GetObject();

			if (!object.Fields.IsEmpty())
			{
				return setValidationFailure(
					error, EErrorCode::Conflict, "ROX-CORE-SCHEMA-UNMAPPED-FIELD-ID",
					"An open object contains FieldId-keyed values without a schema for their names.",
					"Provide a nested schema or keep open-object fields in the name-keyed map.");
			}

			for (const Value::UnknownFieldMap::Entry& field : object.UnknownFields)
			{
				if (!IsValidUtf8(detail::ToStringView(field.GetKey().View())))
				{
					return setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-UTF8-KEY",
												"Authored object keys must contain well-formed UTF-8.",
												"Replace invalid UTF-8 in the object key.");
				}

				if (!validateGenericValueImpl(field.Value, error))
				{
					return fieldFailure(error, field.GetKey().View());
				}
			}

			return true;
		}
		default:
			ASSERT(false, "Generic schema validation must handle every Value storage alternative.");
		}
	}

	Result<void> SchemaRegistry::validateFieldValue(const SchemaFieldDescriptor& field, const Value& value) const
	{
		Error error;

		if (!validateFieldValueImpl(field, value, error))
		{
			fieldFailure(error, field.Name);
			return Result<void>::Failure(rox::core::Move(error));
		}

		return Result<void>::Success();
	}

	bool SchemaRegistry::validateFieldValueImpl(const SchemaFieldDescriptor& field, const Value& value,
												Error& error) const
	{
		if (!validateFieldHead(field, value, error))
		{
			return false;
		}

		const SchemaDescriptor* objectSchema = nullptr;
		if (field.ValueKind == ESchemaValueKind::Object)
		{
			if (field.ValueType.IsValid())
			{
				objectSchema = Find(field.ValueType);
				ASSERT(objectSchema != nullptr, "Registered object fields must reference a registered value type.");

				if (!validateObjectImpl(*objectSchema, value, error))
				{
					return false;
				}
			}
			else
			{
				if (!validateGenericValueImpl(value, error))
				{
					return false;
				}
			}
		}

		if (field.ValueKind == ESchemaValueKind::Array && !validateArrayField(field, value, error))
		{
			return false;
		}

		return validateFieldConstraints(field, value, objectSchema, error);
	}

	bool SchemaRegistry::validateArrayField(const SchemaFieldDescriptor& field, const Value& value, Error& error) const
	{
		ASSERT(value.GetArray() != nullptr, "A value matching an array field kind must have array storage.");
		const SchemaDescriptor* elementDescriptor = nullptr;

		if (field.ElementType.IsValid())
		{
			elementDescriptor = Find(field.ElementType);
			ASSERT(elementDescriptor != nullptr, "Registered array fields must reference a registered element type.");
		}

		for (usize index = 0; index < value.GetArray()->Size(); ++index)
		{
			const Value& element = (*value.GetArray())[index];
			const bool bValidElement = [&]() -> bool
			{
				if (element.GetKind() != field.ElementValueKind)
				{
					return setValidationFailure(error, EErrorCode::InvalidArgument,
												"ROX-CORE-SCHEMA-ELEMENT-TYPE-MISMATCH",
												"An array element does not match its declared value kind.",
												"Convert every element to the array's declared element value kind.");
				}

				if (field.NestedArrays.empty() && field.ElementMaximumLength.has_value() && getLength(element) > *field.ElementMaximumLength)
				{
					return setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-ELEMENT-LENGTH",
												"An array element exceeds its maximum byte length.",
												"Reduce the element byte length.");
				}

				if (field.NestedArrays.empty() && !field.EnumEntries.empty() && !storeValidationResult(ValidateSchemaEnumValue(field, element), error))
				{
					return false;
				}

				if (!field.NestedArrays.empty())
				{
					if (!validateFieldValueImpl(detail::NestedArrayField(field), element, error))
					{
						return false;
					}
				}
				else if (elementDescriptor != nullptr)
				{
					if (!validateObjectImpl(*elementDescriptor, element, error))
					{
						return false;
					}
				}
				else if (field.ElementValueKind == ESchemaValueKind::Array || field.ElementValueKind == ESchemaValueKind::Object)
				{
					if (!validateGenericValueImpl(element, error))
					{
						return false;
					}
				}
				else
				{
					if (!validateScalarValue(element, error))
					{
						return false;
					}
				}

				return true;
			}();

			if (!bValidElement)
			{
				return elementFailure(error, index);
			}
		}

		return true;
	}

	Result<void> SchemaRegistry::validateObject(const SchemaDescriptor& descriptor, const Value& value) const
	{
		Error error;

		if (!validateObjectImpl(descriptor, value, error))
		{
			return Result<void>::Failure(rox::core::Move(error));
		}

		return Result<void>::Success();
	}

	bool SchemaRegistry::validateObjectImpl(const SchemaDescriptor& descriptor, const Value& value, Error& error) const
	{
		const Value::Object* const object = value.GetObject();

		if (object == nullptr)
		{
			return setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-EXPECTED-OBJECT",
										"A schema-bound value must be an object.",
										"Provide an object value for the requested type.");
		}

		for (const SchemaFieldDescriptor& field : descriptor.Fields)
		{
			if (object->UnknownFields.Contains(field.Name))
			{
				setValidationFailure(
					error, EErrorCode::Conflict, "ROX-CORE-SCHEMA-AMBIGUOUS-FIELD",
					"A known schema field is also present in the unknown-field set.",
					"Keep known fields in the FieldId map and future fields in the name-keyed unknown set.");
				return fieldFailure(error, field.Name);
			}

			const Value* const found = object->Fields.Find(field.ID);

			if (found == nullptr)
			{
				if (!hasFlag(field.Flags, ESchemaFieldFlags::Optional) && field.DefaultValue == nullptr)
				{
					setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-MISSING-FIELD", "A required schema field is missing.", "Provide the required field before validation or serialization.");
					return fieldFailure(error, field.Name);
				}
				continue;
			}

			if (!validateFieldValueImpl(field, *found, error))
			{
				return fieldFailure(error, field.Name);
			}
		}

		for (const Value::FieldMap::Entry& field : object->Fields)
		{
			bool bKnown = false;

			for (const SchemaFieldDescriptor& descriptorField : descriptor.Fields)
			{
				if (descriptorField.ID == field.GetKey())
				{
					bKnown = true;
					break;
				}
			}

			if (!bKnown)
			{
				return setValidationFailure(
					error, EErrorCode::Conflict, "ROX-CORE-SCHEMA-UNKNOWN-FIELD-ID",
					"A value contains an unknown FieldId without its original JSON name.",
					"Store future fields in Value::Object::UnknownFields so they can round-trip by name.");
			}
		}

		for (const Value::UnknownFieldMap::Entry& field : object->UnknownFields)
		{
			if (!IsValidUtf8(detail::ToStringView(field.GetKey().View())))
			{
				return setValidationFailure(error, EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-UTF8-KEY", "Authored object keys must contain well-formed UTF-8.", "Replace invalid UTF-8 in the object key.");
			}

			if (!validateGenericValueImpl(field.Value, error))
			{
				return fieldFailure(error, field.GetKey().View());
			}
		}

		return true;
	}
} // namespace rox::core
