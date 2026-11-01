#include "SchemaEnum.h"
#include <StringView.hpp>
#include <Types.h>
#include <Utility.hpp>
#include <Utf8.h>
#include <Value.h>

namespace rox::core
{
	Result<void> ValidateSchemaEnumDescriptor(const SchemaFieldDescriptor& field)
	{
		if (field.EnumEntries.empty())
		{
			return Result<void>::Success();
		}

		ESchemaValueKind kind = field.ValueKind;
		if (!field.NestedArrays.empty())
		{
			kind = field.NestedArrays.back().ElementValueKind;
		}
		else if (field.ValueKind == ESchemaValueKind::Array)
		{
			kind = field.ElementValueKind;
		}

		bool valid = field.EnumEntries.size() <= MAXIMUM_SCHEMA_ENUM_ENTRIES && (kind == ESchemaValueKind::SignedInteger || kind == ESchemaValueKind::UnsignedInteger);

		for (usize index = 0; valid && index < field.EnumEntries.size(); ++index)
		{
			const SchemaEnumEntry& entry = field.EnumEntries[index];
			valid = !entry.Name.empty() && IsValidUtf8(StringView(entry.Name));

			for (usize earlier = 0; valid && earlier < index; ++earlier)
			{
				valid = entry.Name != field.EnumEntries[earlier].Name && entry.Bits != field.EnumEntries[earlier].Bits;
			}
		}

		if (!valid)
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-ENUM-DESCRIPTOR", "Enum metadata requires bounded unique UTF-8 names and integer values.", "Use unique names and values on integer fields or integer array elements.");
		}

		return Result<void>::Success();
	}

	Result<void> ValidateSchemaEnumValue(const SchemaFieldDescriptor& field, const Value& value)
	{
		if (field.EnumEntries.empty())
		{
			return Result<void>::Success();
		}

		ASSERT(value.GetSignedInteger() != nullptr || value.GetUnsignedInteger() != nullptr, "Enum validation requires a type-checked integer value.");
		const uint64 bits = value.GetSignedInteger() != nullptr ? core::BitCast<uint64>(*value.GetSignedInteger()) : *value.GetUnsignedInteger();

		if (FindSchemaEnumEntry(field, bits) == nullptr)
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-ENUM-VALUE", "An integer value is absent from the field's enum entries.", "Choose a declared enum value.");
		}

		return Result<void>::Success();
	}

	Result<UniquePtr<OwnedSchemaEnum>> CopySchemaEnum(const SchemaFieldDescriptor& field, MemoryResource& resource)
	{
		using CopyResult = Result<UniquePtr<OwnedSchemaEnum>>;

		if (field.EnumEntries.empty())
		{
			return CopyResult::Success({});
		}

		CopyResult (*const failure)() = []() -> CopyResult
		{
			return CopyResult::Failure(EErrorCode::ExternalFailure, "ROX-CORE-SCHEMA-REGISTRY-ALLOCATION", "The schema registry could not copy enum metadata.", "Release memory and retry registration.");
		};
		UniquePtr<OwnedSchemaEnum> owned = MakeUnique<OwnedSchemaEnum>(resource, resource);

		if (!owned || !owned->Names.Reserve(field.EnumEntries.size()) || !owned->Entries.Reserve(field.EnumEntries.size()))
		{
			return failure();
		}

		for (const SchemaEnumEntry& entry : field.EnumEntries)
		{
			String name(resource);

			if (!name.Assign(StringView(entry.Name)))
			{
				return failure();
			}

			owned->Names.AddRequired(core::Move(name));
			owned->Entries.AddRequired(SchemaEnumEntry{owned->Names[owned->Names.Size() - 1], entry.Bits});
		}

		return CopyResult::Success(core::Move(owned));
	}
}
