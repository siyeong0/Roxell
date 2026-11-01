#include "SchemaRegistryInternal.h"
#include "ValueCloneBounds.h"
#include <Assertion.h>

namespace rox::core
{
	bool SchemaRegistry::CanonicalizationContext::Consume(uint32 depth, uint64 count) noexcept
	{
		if (count == 0)
		{
			return true;
		}

		if (depth > Limits.MaximumDepth || count > Remaining)
		{
			return false;
		}

		Remaining -= count;
		return true;
	}

	Error SchemaRegistry::CanonicalizationContext::EntryError(uint32 depth, uint64 count) const
	{
		ASSERT(count != 0 && (depth > Limits.MaximumDepth || count > Remaining), "Canonical entry errors require an exhausted depth or node budget.");

		if (depth > Limits.MaximumDepth)
		{
			return MakeError(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CANONICAL-DEPTH", "The schema value exceeds its depth limit.", "Reduce nesting or raise the limit within the supported bound.");
		}

		return MakeError(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CANONICAL-COUNT", "The schema value exceeds its node budget.", "Reduce value complexity or increase the explicit budget.");
	}

	Result<void> SchemaRegistry::CanonicalizationContext::CheckInput(const Value& value)
	{
		const Result<void> bounded = ValidateValueCloneBounds(value, {Limits.MaximumDepth, Limits.MaximumValues});
		if (bounded)
		{
			return Result<void>::Success();
		}

		if (bounded.GetError().DiagnosticID == "ROX-CORE-VALUE-WALK-DEPTH")
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CANONICAL-DEPTH", "The schema value exceeds its depth limit.", "Reduce nesting or raise the limit within the supported bound.");
		}

		return Result<void>::Failure(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CANONICAL-COUNT", "The schema value exceeds its node budget.", "Reduce value complexity or increase the explicit budget.");
	}

	SchemaRegistry::OwnedSchema::OwnedSchema(MemoryResource& resource) noexcept
	    : Name(resource)
	    , DisplayName(resource)
	    , Description(resource)
	    , Category(resource)
	    , Icon(resource)
	    , RefKind(resource)
	    , Unit(resource)
	    , FieldNames(resource)
	    , DisplayNames(resource)
	    , Descriptions(resource)
	    , Categories(resource)
	    , Icons(resource)
	    , RefKinds(resource)
	    , Units(resource)
	    , DefaultValues(resource)
	    , MinimumValues(resource)
	    , MaximumValues(resource)
	    , Fields(resource)
	    , Enums(resource)
	    , NestedArrays(resource)
	{
	}

	namespace detail
	{
		SchemaFieldDescriptor NestedArrayField(const SchemaFieldDescriptor& field) noexcept
		{
			ASSERT(!field.NestedArrays.empty(), "A nested array field requires its next descriptor.");

			const SchemaArrayDescriptor& nested = field.NestedArrays.front();
			SchemaFieldDescriptor result;
			result.ValueKind = ESchemaValueKind::Array;
			result.ElementValueKind = nested.ElementValueKind;
			result.ElementType = nested.ElementType;
			result.Constraints.MinimumLength = nested.MinimumLength;
			result.Constraints.MaximumLength = nested.MaximumLength;
			result.EnumEntries = field.EnumEntries;
			result.ElementMaximumLength = field.ElementMaximumLength;
			result.NestedArrays = field.NestedArrays.subspan(1);
			return result;
		}

		StringView ToStringView(const StringView text) noexcept
		{
			return {text.data(), text.size()};
		}

		Result<void> Failure(EErrorCode code, StringView diagnosticId, StringView message, StringView recoveryHint)
		{
			return Result<void>::Failure(code, diagnosticId, message, recoveryHint);
		}

		Result<void> AllocationFailure(const StringView message)
		{
			return Failure(EErrorCode::ExternalFailure, "ROX-CORE-SCHEMA-REGISTRY-ALLOCATION", message, "Retry the registration with a resource that can satisfy the bounded schema storage.");
		}

		bool IsNumericKind(ESchemaValueKind kind) noexcept
		{
			return kind == ESchemaValueKind::SignedInteger || kind == ESchemaValueKind::UnsignedInteger || kind == ESchemaValueKind::FloatingPoint;
		}

		bool HasLength(ESchemaValueKind kind) noexcept
		{
			return kind == ESchemaValueKind::String || kind == ESchemaValueKind::Bytes || kind == ESchemaValueKind::Array || kind == ESchemaValueKind::Object;
		}

		int32 CompareNumeric(const Value& left, const Value& right) noexcept
		{
			ASSERT(left.GetKind() == right.GetKind(), "Numeric comparison requires values with the same schema value kind.");
			ASSERT(IsNumericKind(left.GetKind()), "Numeric comparison requires numeric schema values.");

			switch (left.GetKind())
			{
			case ESchemaValueKind::SignedInteger:
				ASSERT(left.GetSignedInteger() != nullptr && right.GetSignedInteger() != nullptr, "Signed integer Value kind must have signed integer storage.");
				return *left.GetSignedInteger() < *right.GetSignedInteger() ? -1 : (*left.GetSignedInteger() > *right.GetSignedInteger() ? 1 : 0);
			case ESchemaValueKind::UnsignedInteger:
				ASSERT(left.GetUnsignedInteger() != nullptr && right.GetUnsignedInteger() != nullptr, "Unsigned integer Value kind must have unsigned integer storage.");
				return *left.GetUnsignedInteger() < *right.GetUnsignedInteger() ? -1 : (*left.GetUnsignedInteger() > *right.GetUnsignedInteger() ? 1 : 0);
			case ESchemaValueKind::FloatingPoint:
				ASSERT(left.GetFloatingPoint() != nullptr && right.GetFloatingPoint() != nullptr, "Floating-point Value kind must have floating-point storage.");
				return *left.GetFloatingPoint() < *right.GetFloatingPoint() ? -1 : (*left.GetFloatingPoint() > *right.GetFloatingPoint() ? 1 : 0);
			default:
				ASSERT(false, "Numeric comparison must handle every supported numeric value kind.");
			}
		}
	}
}
