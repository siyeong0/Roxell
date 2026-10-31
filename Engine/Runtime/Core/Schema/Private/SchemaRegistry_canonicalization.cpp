#include <SchemaRegistry.h>
#include <Assertion.h>
#include "SchemaRegistryInternal.h"
#include <SystemMemoryResource.h>
#include <Utility.hpp>

namespace rox::core
{
	namespace
	{
		bool isInlineValueKind(ESchemaValueKind kind) noexcept
		{
			switch (kind)
			{
			case ESchemaValueKind::Null:
			case ESchemaValueKind::Boolean:
			case ESchemaValueKind::SignedInteger:
			case ESchemaValueKind::UnsignedInteger:
			case ESchemaValueKind::FloatingPoint:
			case ESchemaValueKind::StableId:
				return true;
			default:
				return false;
			}
		}
	}

	Result<Value> SchemaRegistry::Canonicalize(TypeId type, const Value& value) const
	{
		return Canonicalize(type, value, SystemMemoryResource::Get());
	}

	Result<Value> SchemaRegistry::Canonicalize(TypeId type, const Value& value, MemoryResource& resource, SchemaCanonicalizationLimits limits) const
	{
		ASSERT(limits.MaximumDepth <= SchemaCanonicalizationLimits::HARD_MAXIMUM_DEPTH, "Schema canonicalization depth must fit the supported bound.");
		ASSERT(limits.MaximumValues > 0, "Schema canonicalization requires a positive node budget.");
		ASSERT(m_bFrozen, "Schema-bound values can be canonicalized only with a frozen registry.");

		const SchemaDescriptor* const descriptor = Find(type);

		if (descriptor == nullptr)
		{
			return Result<Value>::Failure(EErrorCode::NotFound, "ROX-CORE-SCHEMA-TYPE-NOT-FOUND", "The requested value type is not registered.", "Register the schema before canonicalizing values of this type.");
		}

		CanonicalizationContext context{resource, limits, limits.MaximumValues};
		Result<void> bounded = context.CheckInput(value);

		if (!bounded)
		{
			return Result<Value>::Failure(bounded.GetError());
		}

		context.Remaining = limits.MaximumValues;
		const Result<void> validation = validateObject(*descriptor, value);

		if (!validation)
		{
			return Result<Value>::Failure(validation.GetError());
		}

		ASSERT(value.GetObject() != nullptr, "Successful schema object validation must prove object storage is available.");

		Result<Value> canonical = canonicalizeObject(*descriptor, value, context, 0);

		if (!canonical)
		{
			if (canonical.GetError().DiagnosticID == "ROX-CORE-VALUE-ALLOCATION")
			{
				return Result<Value>::Failure( detail::AllocationFailure("Schema canonicalization could not allocate destination storage.").GetError());
			}

			return canonical;
		}

		const Result<void> canonicalValidation = validateObject(*descriptor, canonical.Value());
		ASSERT(canonicalValidation, "A value produced by schema canonicalization must satisfy the same validated schema.");
		return canonical;
	}

	Result<Value> SchemaRegistry::canonicalizeGenericValue(const Value& value, CanonicalizationContext& context,
	                                                       uint32 depth) const
	{
		if (!context.Consume(depth))
		{
			return Result<Value>::Failure(context.EntryError(depth));
		}

		MemoryResource& resource = context.Resource;

		switch (value.GetKind())
		{
		case ESchemaValueKind::FloatingPoint:
			ASSERT(value.GetFloatingPoint() != nullptr, "Floating-point Value kind must have floating-point storage during canonicalization.");
			return Result<Value>::Success(Value(*value.GetFloatingPoint() == 0.0 ? 0.0 : *value.GetFloatingPoint()));
		case ESchemaValueKind::Array:
		{
			ASSERT(value.GetArray() != nullptr, "Array Value kind must have array storage during canonicalization.");
			Value::Array elements(resource);

			if (!elements.Reserve(value.GetArray()->Size()))
			{
				return Result<Value>::Failure(detail::AllocationFailure("Schema canonicalization could not allocate array storage.").GetError());
			}

			for (const Value& element : *value.GetArray())
			{
				Result<Value> canonicalElement = canonicalizeGenericValue(element, context, depth + 1);
				if (!canonicalElement)
				{
					return canonicalElement;
				}
				elements.AddRequired(rox::core::Move(canonicalElement).TakeValue());
			}

			return Value::CreateArray(rox::core::Move(elements));
		}
		case ESchemaValueKind::Object:
		{
			ASSERT(value.GetObject() != nullptr, "Object Value kind must have object storage during canonicalization.");
			Value::Object object(resource);

			if (!object.UnknownFields.Reserve(value.GetObject()->UnknownFields.Size()))
			{
				return Result<Value>::Failure(detail::AllocationFailure("Schema canonicalization could not allocate object fields.").GetError());
			}

			for (const Value::UnknownFieldMap::Entry& entry : value.GetObject()->UnknownFields)
			{
				const StringView& name = entry.GetKey().View();
				const Value& fieldValue = entry.Value;
				Result<Value> canonicalField = canonicalizeGenericValue(fieldValue, context, depth + 1);
				if (!canonicalField)
				{
					return canonicalField;
				}

				Result<ValueFieldName> copiedName = ValueFieldName::Copy(name, object.UnknownFields.GetResource());
				if (!copiedName)
				{
					return Result<Value>::Failure(copiedName.GetError());
				}
				object.UnknownFields.InsertRequired(rox::core::Move(copiedName).TakeValue(), rox::core::Move(canonicalField).TakeValue());
			}

			return Value::CreateObject(rox::core::Move(object), resource);
		}
		case ESchemaValueKind::String:
			return Value::CreateString(*value.GetString(), resource);
		case ESchemaValueKind::Bytes:
			return Value::CreateBytes(*value.GetBytes(), resource);
		default:
			return Result<Value>::Success(value);
		}
	}

	Result<Value> SchemaRegistry::canonicalizeFieldValue(const SchemaFieldDescriptor& field, const Value& value,
	                                                     CanonicalizationContext& context, uint32 depth) const
	{
		ASSERT(value.GetKind() == field.ValueKind, "Canonical field values must already match their validated schema kind.");

		if (field.ValueKind == ESchemaValueKind::Object)
		{
			if (field.ValueType.IsValid())
			{
				const SchemaDescriptor* const descriptor = Find(field.ValueType);
				ASSERT(descriptor != nullptr, "Validated object fields must reference a registered schema descriptor.");
				return canonicalizeObject(*descriptor, value, context, depth);
			}

			return canonicalizeGenericValue(value, context, depth);
		}

		if (field.ValueKind == ESchemaValueKind::Array)
		{
			if (!context.Consume(depth))
			{
				return Result<Value>::Failure(context.EntryError(depth));
			}

			MemoryResource& resource = context.Resource;
			ASSERT(value.GetArray() != nullptr, "Validated array fields must have array storage during canonicalization.");
			const SchemaDescriptor* const elementDescriptor = field.ElementType.IsValid() ? Find(field.ElementType) : nullptr;
			ASSERT(!field.ElementType.IsValid() || elementDescriptor != nullptr, "Validated object-array fields must reference a registered element schema.");
			Value::Array elements(resource);

			if (!elements.Reserve(value.GetArray()->Size()))
			{
				return Result<Value>::Failure(detail::AllocationFailure("Schema canonicalization could not allocate array storage.").GetError());
			}

			if (isInlineValueKind(field.ElementValueKind))
			{
				const Value::Array& source = *value.GetArray();

				if (!context.Consume(depth + 1, source.Size()))
				{
					return Result<Value>::Failure(context.EntryError(depth + 1, source.Size()));
				}
				// The preceding schema validation proves homogeneous inline values. No child
				// owns storage, so the reserved destination can copy them without recursive Results.
				elements.AppendRequired(source.View());

				if (field.ElementValueKind == ESchemaValueKind::FloatingPoint)
				{
					for (Value& element : elements)
					{
						const float64* number = element.GetFloatingPoint();
						ASSERT(number != nullptr, "Validated floating-point arrays contain only floating-point values.");

						if (*number == 0.0)
						{
							element = Value(0.0);
						}
					}
				}
			}
			else
			{
				for (const Value& element : *value.GetArray())
				{
					Result<Value> canonicalElement = !field.NestedArrays.empty() ? canonicalizeFieldValue(detail::NestedArrayField(field), element, context, depth + 1)
					    : elementDescriptor != nullptr
					        ? canonicalizeObject(*elementDescriptor, element, context, depth + 1)
					        : canonicalizeGenericValue(element, context, depth + 1);

					if (!canonicalElement)
					{
						return canonicalElement;
					}
					elements.AddRequired(rox::core::Move(canonicalElement).TakeValue());
				}
			}

			return Value::CreateArray(rox::core::Move(elements));
		}

		return canonicalizeGenericValue(value, context, depth);
	}

	Result<Value> SchemaRegistry::canonicalizeObject(const SchemaDescriptor& descriptor, const Value& value, CanonicalizationContext& context, uint32 depth) const
	{
		if (!context.Consume(depth))
		{
			return Result<Value>::Failure(context.EntryError(depth));
		}

		MemoryResource& resource = context.Resource;
		const Value::Object* const sourceObject = value.GetObject();
		ASSERT(sourceObject != nullptr, "Canonical schema objects must have valid object storage.");
		const Value::Object& source = *sourceObject;
		Value::Object object(resource);

		if (!object.Fields.Reserve(descriptor.Fields.size()) || !object.UnknownFields.Reserve(source.UnknownFields.Size()))
		{
			return Result<Value>::Failure(detail::AllocationFailure("Schema canonicalization could not allocate object fields.").GetError());
		}

		for (const SchemaFieldDescriptor& field : descriptor.Fields)
		{
			const Value* const found = source.Fields.Find(field.ID);
			const Value* const sourceValue = found != nullptr ? found : field.DefaultValue;

			if (sourceValue == nullptr)
			{
				continue;
			}

			Result<Value> canonicalField = canonicalizeFieldValue(field, *sourceValue, context, depth + 1);

			if (!canonicalField)
			{
				return canonicalField;
			}
			object.Fields.InsertRequired(field.ID, rox::core::Move(canonicalField).TakeValue());
		}

		for (const Value::UnknownFieldMap::Entry& entry : source.UnknownFields)
		{
			const StringView& name = entry.GetKey().View();
			const Value& fieldValue = entry.Value;
			Result<Value> canonicalField = canonicalizeGenericValue(fieldValue, context, depth + 1);

			if (!canonicalField)
			{
				return canonicalField;
			}

			Result<ValueFieldName> copiedName = ValueFieldName::Copy(name, object.UnknownFields.GetResource());

			if (!copiedName)
			{
				return Result<Value>::Failure(copiedName.GetError());
			}
			object.UnknownFields.InsertRequired(rox::core::Move(copiedName).TakeValue(), rox::core::Move(canonicalField).TakeValue());
		}

		return Value::CreateObject(rox::core::Move(object), resource);
	}
} // namespace rox::core
