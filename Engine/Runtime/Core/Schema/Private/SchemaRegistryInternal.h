#pragma once

#include "SchemaEnum.h"
#include <SchemaRegistry.h>
#include <StringView.hpp>

namespace rox::core
{
	struct SchemaRegistry::CanonicalizationContext
	{
		MemoryResource& Resource;
		SchemaCanonicalizationLimits Limits;
		uint64 Remaining;

		[[nodiscard]] bool Consume(uint32 depth, uint64 count = 1) noexcept;
		[[nodiscard]] Error EntryError(uint32 depth, uint64 count = 1) const;
		Result<void> CheckInput(const Value& value);
	};

	struct SchemaRegistry::OwnedSchema
	{
		explicit OwnedSchema(MemoryResource& resource) noexcept;

		String Name;
		String DisplayName;
		String Description;
		String Category;
		String Icon;
		String RefKind;
		String Unit;

		Array<String> FieldNames;
		Array<String> DisplayNames;
		Array<String> Descriptions;
		Array<String> Categories;
		Array<String> Icons;
		Array<String> RefKinds;
		Array<String> Units;

		Array<UniquePtr<Value>> DefaultValues;
		Array<UniquePtr<Value>> MinimumValues;
		Array<UniquePtr<Value>> MaximumValues;

		Array<SchemaFieldDescriptor> Fields;
		Array<UniquePtr<OwnedSchemaEnum>> Enums;
		Array<Array<SchemaArrayDescriptor>> NestedArrays;

		SchemaDescriptor View;
	};

	namespace detail
	{
		[[nodiscard]] SchemaFieldDescriptor NestedArrayField(const SchemaFieldDescriptor& field) noexcept;
		[[nodiscard]] StringView ToStringView(const rox::core::StringView text) noexcept;

		[[nodiscard]] Result<void> Failure(EErrorCode code, StringView diagnosticId, StringView message, StringView recoveryHint);
		[[nodiscard]] Result<void> AllocationFailure(const StringView message);

		[[nodiscard]] bool IsNumericKind(ESchemaValueKind kind) noexcept;
		[[nodiscard]] bool HasLength(ESchemaValueKind kind) noexcept;
		[[nodiscard]] int32 CompareNumeric(const Value& left, const Value& right) noexcept;
	}
}
