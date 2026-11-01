#include <SchemaRegistry.h>
#include <Assertion.h>
#include "SchemaRegistryInternal.h"
#include "ValueCloneBounds.h"
#include <SystemMemoryResource.h>
#include <Utf8.h>
#include <StringView.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <Numeric.hpp>
#include <Utility.hpp>
#include <algorithm>

namespace rox::core
{
	namespace
	{
		template <typename TValue>
		[[nodiscard]] bool contains(const Array<TValue>& values, const TValue& value) noexcept
		{
			return core::Find(values.begin(), values.end(), value) != values.end();
		}

		[[nodiscard]] Result<void> cyclicSchemaFailure();

		[[nodiscard]] bool appendOwnedText(Array<String>& texts, MemoryResource& resource, const StringView text) noexcept;
		[[nodiscard]] bool appendOwnedValue(Array<UniquePtr<Value>>& values, MemoryResource& resource, const Value* const value);

		[[nodiscard]] bool isValueKindValid(ESchemaValueKind kind) noexcept;
		[[nodiscard]] Result<void> validateConstraintDescriptor(const SchemaFieldDescriptor& field);
		[[nodiscard]] bool hasOnlyKnownFlags(ESchemaFieldFlags value) noexcept;
		[[nodiscard]] Result<void> validateDisplayMetadata(const SchemaDisplayMetadata& display)
		{
			const StringView texts[] = {display.DisplayName, display.Description, display.Category, display.Icon, display.RefKind, display.Unit};
			for (const StringView text : texts)
			{
				if (!IsValidUtf8(text))
				{
					return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-METADATA-UTF8", "Schema display metadata must contain well-formed UTF-8.", "Replace invalid UTF-8 in the display metadata.");
				}
			}
			if (display.Hint > ESchemaUiHint::ReadOnly || !core::IsFinite(display.Step))
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-METADATA", "Schema display metadata requires a known UI hint and a finite step.", "Use a declared ESchemaUiHint and a finite step value.");
			}

			return Result<void>::Success();
		}

		template <typename TEntry, typename TKey>
		[[nodiscard]] usize lowerBoundIndex(const Array<TEntry>& entries, const TKey key) noexcept
		{
			usize low = 0;
			usize high = entries.Size();

			while (low < high)
			{
				const usize middle = low + ((high - low) / 2);

				if (entries[middle].Key < key)
				{
					low = middle + 1;
				}
				else
				{
					high = middle;
				}
			}

			return low;
		}

		template <typename TEntry>
		void insertIndexEntry(Array<TEntry>& entries, const TEntry& entry, const usize position) noexcept
		{
			ASSERT(position <= entries.Size(), "A sorted index insertion position must be within the index.");
			ASSERT(entries.Size() < entries.Capacity(), "Sorted index storage must be reserved before an entry is published.");
			entries.AddRequired(entry);

			for (usize index = entries.Size() - 1; index > position; --index)
			{
				entries[index] = entries[index - 1];
			}
			entries[position] = entry;
		}

		[[nodiscard]] Result<void> cyclicSchemaFailure()
		{
			return detail::Failure(EErrorCode::Conflict, "ROX-CORE-SCHEMA-BY-VALUE-CYCLE", "A schema contains a cyclic by-value type dependency.", "Break the cycle with an ID reference or another non-owning representation.");
		}

		[[nodiscard]] bool appendOwnedText(Array<String>& texts, MemoryResource& resource, const StringView text) noexcept
		{
			String owned(resource);

			if (!owned.Assign(detail::ToStringView(text)))
			{
				return false;
			}

			ASSERT(texts.Size() < texts.Capacity(), "Owned schema text storage must be reserved before it is filled.");
			texts.AddRequired(core::Move(owned));
			return true;
		}

		[[nodiscard]] bool appendOwnedValue(Array<UniquePtr<Value>>& values, MemoryResource& resource, const Value* const value)
		{
			UniquePtr<Value> owned;

			if (value != nullptr)
			{
				Result<Value> copied = CloneValue(*value, resource);

				if (!copied)
				{
					return false;
				}

				owned = MakeUnique<Value>(resource, core::Move(copied).TakeValue());

				if (!owned)
				{
					return false;
				}
			}

			ASSERT(values.Size() < values.Capacity(), "Owned schema value storage must be reserved before it is filled.");
			values.AddRequired(core::Move(owned));
			return true;
		}

		[[nodiscard]] bool isValueKindValid(ESchemaValueKind kind) noexcept
		{
			switch (kind)
			{
			case ESchemaValueKind::Null:
			case ESchemaValueKind::Boolean:
			case ESchemaValueKind::SignedInteger:
			case ESchemaValueKind::UnsignedInteger:
			case ESchemaValueKind::FloatingPoint:
			case ESchemaValueKind::String:
			case ESchemaValueKind::StableId:
			case ESchemaValueKind::Bytes:
			case ESchemaValueKind::Array:
			case ESchemaValueKind::Object:
				return true;
			default:
				return false;
			}
		}

		[[nodiscard]] Result<void> validateConstraintDescriptor(const SchemaFieldDescriptor& field)
		{
			const Value* const minimum = field.Constraints.Minimum;
			const Value* const maximum = field.Constraints.Maximum;

			if ((minimum != nullptr || maximum != nullptr) && !detail::IsNumericKind(field.ValueKind))
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-NUMERIC-CONSTRAINT", "Minimum and maximum constraints require a numeric field.", "Remove the numeric bounds or use a numeric value kind.");
			}

			if ((minimum != nullptr && minimum->GetKind() != field.ValueKind) || (maximum != nullptr && maximum->GetKind() != field.ValueKind))
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CONSTRAINT-TYPE-MISMATCH", "A numeric constraint does not match the field value kind.", "Express each bound using the field's exact numeric value kind.");
			}

			const float64* minimumFloatingPoint = nullptr;

			if (minimum != nullptr && minimum->GetKind() == ESchemaValueKind::FloatingPoint)
			{
				minimumFloatingPoint = minimum->GetFloatingPoint();
				ASSERT(minimumFloatingPoint != nullptr, "Floating-point minimum constraint must have floating-point storage.");
			}

			const float64* maximumFloatingPoint = nullptr;

			if (maximum != nullptr && maximum->GetKind() == ESchemaValueKind::FloatingPoint)
			{
				maximumFloatingPoint = maximum->GetFloatingPoint();
				ASSERT(maximumFloatingPoint != nullptr, "Floating-point maximum constraint must have floating-point storage.");
			}

			if ((minimumFloatingPoint != nullptr && !core::IsFinite(*minimumFloatingPoint)) || (maximumFloatingPoint != nullptr && !core::IsFinite(*maximumFloatingPoint)))
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-NON-FINITE-CONSTRAINT", "Numeric constraints cannot contain NaN or infinity.", "Provide finite numeric bounds.");
			}

			if (minimum != nullptr && maximum != nullptr && detail::CompareNumeric(*minimum, *maximum) > 0)
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVERTED-NUMERIC-RANGE", "A field minimum is greater than its maximum.", "Correct the numeric constraint range.");
			}

			if ((field.Constraints.MinimumLength.has_value() || field.Constraints.MaximumLength.has_value()) && !detail::HasLength(field.ValueKind))
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-LENGTH-CONSTRAINT", "Length constraints require a string, bytes, array, or object field.", "Remove the length bounds or use a value kind with a length.");
			}

			if (field.ElementMaximumLength.has_value())
			{
				const ESchemaValueKind elementKind = field.NestedArrays.empty() ? field.ElementValueKind : field.NestedArrays.back().ElementValueKind;

				if (field.ValueKind != ESchemaValueKind::Array || (elementKind != ESchemaValueKind::String && elementKind != ESchemaValueKind::Bytes))
				{
					return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-ELEMENT-LENGTH-CONSTRAINT", "Element byte limits require an array with innermost String or Bytes elements.", "Remove the element limit or use a bounded string/byte array shape.");
				}
			}

			if (field.Constraints.MinimumLength.has_value() && field.Constraints.MaximumLength.has_value() && *field.Constraints.MinimumLength > *field.Constraints.MaximumLength)
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVERTED-LENGTH-RANGE", "A field minimum length is greater than its maximum length.", "Correct the length constraint range.");
			}

			return Result<void>::Success();
		}

		[[nodiscard]] bool hasOnlyKnownFlags(ESchemaFieldFlags value) noexcept
		{
			constexpr uint32 KNOWN_FLAGS = static_cast<uint32>(ESchemaFieldFlags::Optional) |
			                               static_cast<uint32>(ESchemaFieldFlags::Replicated) |
			                               static_cast<uint32>(ESchemaFieldFlags::EditorOnly);
			return (static_cast<uint32>(value) & ~KNOWN_FLAGS) == 0;
		}
	} // namespace

	Result<void> SchemaRegistry::Register(const SchemaDescriptor& descriptor)
	{
		ASSERT(m_Resource != nullptr, "A schema registry must retain the resource that owns its storage.");
		ASSERT(!m_bFrozen, "Registration requires a mutable registry before Freeze.");

		if (!descriptor.ID.IsValid() || !descriptor.Type.IsValid())
		{
			return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-ID", "A schema and its value type must have valid stable IDs.", "Assign non-zero SchemaId and TypeId values.");
		}

		if (descriptor.Version == 0 || descriptor.Name.empty() || !IsValidUtf8(detail::ToStringView(descriptor.Name)))
		{
			return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-DESCRIPTOR", "A schema must have a non-zero version and a non-empty UTF-8 name.", "Provide an explicit schema version and a well-formed UTF-8 stable name.");
		}

		if (Find(descriptor.ID) != nullptr || Find(descriptor.Type) != nullptr)
		{
			return detail::Failure(EErrorCode::Conflict, "ROX-CORE-SCHEMA-DUPLICATE", "A schema with the same SchemaId or TypeId is already registered.", "Use a unique stable ID or reuse the existing descriptor.");
		}

		const Result<void> displayResult = validateDisplayMetadata(descriptor.Display);
		if (!displayResult)
		{
			return displayResult;
		}

		// Bounded by the declared field count, checked linearly exactly as validateObject already checks the
		// descriptor field set. Reserved once so no later step in this loop can allocate.
		Array<FieldId> fieldIds(*m_Resource);
		Array<StringView> fieldNames(*m_Resource);

		if (!fieldIds.Reserve(descriptor.Fields.size()) || !fieldNames.Reserve(descriptor.Fields.size()))
		{
			return detail::AllocationFailure("The schema registry could not allocate its field validation scratch.");
		}

		for (const SchemaFieldDescriptor& field : descriptor.Fields)
		{
			if (!field.ID.IsValid() || field.Name.empty() || !IsValidUtf8(detail::ToStringView(field.Name)) || !isValueKindValid(field.ValueKind) || !isValueKindValid(field.ElementValueKind))
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-FIELD", "A schema field has an invalid ID, name, or value kind.", "Provide a stable field ID, a non-empty JSON name, and a supported value kind.");
			}

			const Result<void> fieldDisplayResult = validateDisplayMetadata(field.Display);
			if (!fieldDisplayResult)
			{
				return fieldDisplayResult;
			}

			if (!hasOnlyKnownFlags(field.Flags))
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-UNKNOWN-FLAGS", "A schema field contains an unknown flag bit.", "Use only flags defined by ESchemaFieldFlags for this schema version.");
			}

			if (field.ValueKind != ESchemaValueKind::Array && (field.ElementValueKind != ESchemaValueKind::Null || field.ElementType.IsValid()))
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-UNUSED-ELEMENT-TYPE", "Only array fields may declare an element value kind or element type.", "Clear the element descriptor fields or change the value kind to Array.");
			}

			if (contains(fieldIds, field.ID) || contains(fieldNames, detail::ToStringView(field.Name)))
			{
				return detail::Failure(EErrorCode::Conflict, "ROX-CORE-SCHEMA-DUPLICATE-FIELD", "A schema contains duplicate field IDs or JSON names.", "Give every field a unique stable ID and JSON name.");
			}
			fieldIds.AddRequired(field.ID);
			fieldNames.AddRequired(detail::ToStringView(field.Name));

			if (field.ValueType.IsValid() && field.ValueKind != ESchemaValueKind::Object)
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-VALUE-TYPE", "Only object fields may reference another schema value type.", "Clear ValueType or change the field value kind to Object.");
			}

			if (field.ElementType.IsValid() && (field.ValueKind != ESchemaValueKind::Array || field.ElementValueKind != ESchemaValueKind::Object))
			{
				return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-INVALID-ELEMENT-TYPE", "Only arrays of objects may reference an element schema type.", "Clear ElementType or declare Object as the array element value kind.");
			}

			if (!field.NestedArrays.empty())
			{
				if (field.ValueKind != ESchemaValueKind::Array || field.ElementValueKind != ESchemaValueKind::Array || field.NestedArrays.size() > MAXIMUM_SCHEMA_NESTED_ARRAYS)
				{
					return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-NESTED-ARRAY", "Nested array metadata requires a bounded array-of-arrays field.",  "Use an outermost-first array descriptor chain within the supported depth.");
				}

				for (usize level = 0; level < field.NestedArrays.size(); ++level)
				{
					const SchemaArrayDescriptor& nested = field.NestedArrays[level];

					if (!isValueKindValid(nested.ElementValueKind) || nested.MinimumLength > nested.MaximumLength ||
					    (nested.ElementType.IsValid() && nested.ElementValueKind != ESchemaValueKind::Object) ||
					    (level + 1 < field.NestedArrays.size() && nested.ElementValueKind != ESchemaValueKind::Array))
					{
						return detail::Failure(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-NESTED-ARRAY",
						               "Nested array metadata contains an invalid kind, length, type, or chain edge.",
						               "Give each array ordered bounds and a consistent element descriptor.");
					}
				}
			}

			const TypeId dependencies[] = {
			    field.ValueKind == ESchemaValueKind::Object ? field.ValueType : TypeId::Invalid(),
			    field.ValueKind == ESchemaValueKind::Array && field.ElementValueKind == ESchemaValueKind::Object ? field.ElementType : TypeId::Invalid(),
			    field.NestedArrays.empty() ? TypeId::Invalid() : field.NestedArrays.back().ElementType};

			for (const TypeId dependency : dependencies)
			{
				if (!dependency.IsValid())
				{
					continue;
				}

				if (dependency == descriptor.Type)
				{
					return cyclicSchemaFailure();
				}

				if (Find(dependency) == nullptr)
				{
					return detail::Failure(EErrorCode::NotFound, "ROX-CORE-SCHEMA-UNKNOWN-VALUE-TYPE", "A schema field references a value type that is not registered.", "Register referenced value schemas before dependent schemas.");
				}
			}

			const Result<void> enumResult = ValidateSchemaEnumDescriptor(field);

			if (!enumResult)
			{
				return enumResult;
			}

			for (const Value* input : {field.DefaultValue, field.Constraints.Minimum, field.Constraints.Maximum})
			{
				if (input == nullptr)
				{
					continue;
				}

				const Result<void> bounded = ValidateValueCloneBounds(*input);

				if (!bounded)
				{
					return Result<void>::Failure(bounded.GetError());
				}
			}

			const Result<void> constraintsResult = validateConstraintDescriptor(field);

			if (!constraintsResult)
			{
				return constraintsResult;
			}

			if (field.DefaultValue != nullptr)
			{
				const Result<void> defaultResult = validateFieldValue(field, *field.DefaultValue);

				if (!defaultResult)
				{
					return defaultResult;
				}
			}
		}

		// Nothing below this point mutates registry state until every allocation has succeeded, so a failure
		// returns with no partial registration: GetSchemaCount does not move and neither index gains an entry.
		UniquePtr<OwnedSchema> owned = MakeUnique<OwnedSchema>(*m_Resource, *m_Resource);

		if (!owned)
		{
			return detail::AllocationFailure("The schema registry could not allocate its owned schema node.");
		}

		if (!owned->Name.Assign(StringView(descriptor.Name)) ||
		    !owned->DisplayName.Assign(detail::ToStringView(descriptor.Display.DisplayName)) ||
		    !owned->Description.Assign(detail::ToStringView(descriptor.Display.Description)) ||
		    !owned->Category.Assign(detail::ToStringView(descriptor.Display.Category)) ||
		    !owned->Icon.Assign(detail::ToStringView(descriptor.Display.Icon)) ||
		    !owned->RefKind.Assign(detail::ToStringView(descriptor.Display.RefKind)) ||
		    !owned->Unit.Assign(detail::ToStringView(descriptor.Display.Unit)))
		{
			return detail::AllocationFailure("The schema registry could not allocate the owned schema name.");
		}

		const usize fieldCount = descriptor.Fields.size();

		if (!owned->FieldNames.Reserve(fieldCount) || !owned->DisplayNames.Reserve(fieldCount) ||
		    !owned->Descriptions.Reserve(fieldCount) || !owned->Categories.Reserve(fieldCount) ||
		    !owned->Icons.Reserve(fieldCount) || !owned->RefKinds.Reserve(fieldCount) ||
		    !owned->Units.Reserve(fieldCount) || !owned->DefaultValues.Reserve(fieldCount) ||
		    !owned->MinimumValues.Reserve(fieldCount) || !owned->MaximumValues.Reserve(fieldCount) ||
		    !owned->Fields.Reserve(fieldCount))
		{
			return detail::AllocationFailure("The schema registry could not reserve its owned field storage.");
		}

		for (const SchemaFieldDescriptor& field : descriptor.Fields)
		{
			if (!appendOwnedText(owned->FieldNames, *m_Resource, field.Name) ||
			    !appendOwnedText(owned->DisplayNames, *m_Resource, field.Display.DisplayName) ||
			    !appendOwnedText(owned->Descriptions, *m_Resource, field.Display.Description) ||
			    !appendOwnedText(owned->Categories, *m_Resource, field.Display.Category) ||
			    !appendOwnedText(owned->Icons, *m_Resource, field.Display.Icon) ||
			    !appendOwnedText(owned->RefKinds, *m_Resource, field.Display.RefKind) ||
			    !appendOwnedText(owned->Units, *m_Resource, field.Display.Unit))
			{
				return detail::AllocationFailure("The schema registry could not allocate an owned field text buffer.");
			}

			if (!appendOwnedValue(owned->DefaultValues, *m_Resource, field.DefaultValue) ||
			    !appendOwnedValue(owned->MinimumValues, *m_Resource, field.Constraints.Minimum) ||
			    !appendOwnedValue(owned->MaximumValues, *m_Resource, field.Constraints.Maximum))
			{
				return detail::AllocationFailure("The schema registry could not allocate an owned field value copy.");
			}

			ASSERT(owned->Fields.Size() < owned->Fields.Capacity(), "Owned schema field storage must be reserved before it is filled.");
			owned->Fields.AddRequired(SchemaFieldDescriptor(field));

			if (!field.NestedArrays.empty())
			{
				Array<SchemaArrayDescriptor> nested(*m_Resource);

				if (!nested.Reserve(field.NestedArrays.size()))
				{
					return detail::AllocationFailure("The schema registry could not copy nested array metadata.");
				}

				for (const SchemaArrayDescriptor& level : field.NestedArrays)
				{
					nested.AddRequired(level);
				}

				if (!owned->NestedArrays.Add(core::Move(nested)))
				{
					return detail::AllocationFailure("The schema registry could not retain nested array metadata.");
				}

				const Array<SchemaArrayDescriptor>& stored = owned->NestedArrays.Back();
				owned->Fields.Back().NestedArrays = {stored.Data(), stored.Size()};
			}

			Result<UniquePtr<OwnedSchemaEnum>> copiedEnum = CopySchemaEnum(field, *m_Resource);

			if (!copiedEnum)
			{
				return Result<void>::Failure(copiedEnum.GetError());
			}

			if (copiedEnum.Value())
			{
				if (!owned->Enums.Add(core::Move(copiedEnum).TakeValue()))
				{
					return detail::AllocationFailure("The schema registry could not reserve its owned field storage.");
				}

				const Array<SchemaEnumEntry>& entries = owned->Enums[owned->Enums.Size() - 1]->Entries;
				owned->Fields[owned->Fields.Size() - 1].EnumEntries = {entries.Data(), entries.Size()};
			}
		}

		for (usize index = 0; index < owned->Fields.Size(); ++index)
		{
			ASSERT(index < owned->FieldNames.Size() && index < owned->DisplayNames.Size() &&
			           index < owned->Descriptions.Size() && index < owned->Categories.Size() &&
			           index < owned->DefaultValues.Size() && index < owned->MinimumValues.Size() &&
			           index < owned->MaximumValues.Size(),
			       "Owned schema companion arrays must match the validated field count.");

			SchemaFieldDescriptor& field = owned->Fields[index];
			field.Name = owned->FieldNames[index];
			field.Display.DisplayName = owned->DisplayNames[index];
			field.Display.Description = owned->Descriptions[index];
			field.Display.Category = owned->Categories[index];
			field.Display.Icon = owned->Icons[index];
			field.Display.RefKind = owned->RefKinds[index];
			field.Display.Unit = owned->Units[index];
			field.DefaultValue = owned->DefaultValues[index].Get();
			field.Constraints.Minimum = owned->MinimumValues[index].Get();
			field.Constraints.Maximum = owned->MaximumValues[index].Get();
		}

		owned->View = descriptor;
		owned->View.Name = owned->Name;
		owned->View.Display.DisplayName = owned->DisplayName;
		owned->View.Display.Description = owned->Description;
		owned->View.Display.Category = owned->Category;
		owned->View.Display.Icon = owned->Icon;
		owned->View.Display.RefKind = owned->RefKind;
		owned->View.Display.Unit = owned->Unit;
		owned->View.Fields = {owned->Fields.Data(), owned->Fields.Size()};

		OwnedSchema* const stored = owned.Get();
		ASSERT(stored != nullptr, "Validated schema ownership must produce a valid stored schema.");
		const usize publishedCount = m_Schemas.Size() + 1;

		if (!m_Schemas.Reserve(publishedCount) || !m_ByID.Reserve(publishedCount) || !m_ByType.Reserve(publishedCount))
		{
			return detail::AllocationFailure("The schema registry could not reserve its schema lookup indexes.");
		}

		// Publication starts here and nothing below can allocate or fail, so the registry never carries a schema
		// its indexes do not resolve, or an index entry with no schema behind it.
		const usize idPosition = lowerBoundIndex(m_ByID, stored->View.ID);
		const usize typePosition = lowerBoundIndex(m_ByType, stored->View.Type);
		m_Schemas.AddRequired(core::Move(owned));
		insertIndexEntry(m_ByID, SchemaIndexEntry{stored->View.ID, stored}, idPosition);
		insertIndexEntry(m_ByType, TypeIndexEntry{stored->View.Type, stored}, typePosition);
		ASSERT(m_Schemas.Size() == m_ByID.Size() && m_Schemas.Size() == m_ByType.Size(), "Validated unique schema IDs and types must be inserted exactly once.");
		return Result<void>::Success();
	}

	void SchemaRegistry::Freeze() noexcept
	{
		ASSERT(m_Schemas.Size() == m_ByID.Size() && m_Schemas.Size() == m_ByType.Size(), "Schema registry indexes must match owned schema storage before freezing.");
		m_bFrozen = true;
	}

	const SchemaDescriptor* SchemaRegistry::Find(SchemaId id) const
	{
		const usize position = lowerBoundIndex(m_ByID, id);

		if (position == m_ByID.Size() || !(m_ByID[position].Key == id))
		{
			return nullptr;
		}

		ASSERT(m_ByID[position].Schema != nullptr, "Schema ID index must reference valid owned storage.");
		return &m_ByID[position].Schema->View;
	}

	const SchemaDescriptor* SchemaRegistry::Find(TypeId type) const
	{
		const usize position = lowerBoundIndex(m_ByType, type);

		if (position == m_ByType.Size() || !(m_ByType[position].Key == type))
		{
			return nullptr;
		}

		ASSERT(m_ByType[position].Schema != nullptr, "Schema type index must reference valid owned storage.");
		return &m_ByType[position].Schema->View;
	}

	usize SchemaRegistry::GetSchemaCount() const noexcept
	{
		return m_Schemas.Size();
	}

	const SchemaDescriptor& SchemaRegistry::GetSchemaByIndex(const usize index) const
	{
		ASSERT(m_bFrozen, "Schema enumeration requires a frozen registry.");
		ASSERT(index < m_Schemas.Size(), "Schema enumeration index must be within the frozen registry.");
		ASSERT(m_Schemas[index], "Frozen schema storage must contain a valid descriptor.");
		return m_Schemas[index]->View;
	}

	SchemaRegistry::SchemaRegistry()
	    : SchemaRegistry(SystemMemoryResource::Get())
	{
	}

	SchemaRegistry::SchemaRegistry(MemoryResource& resource) noexcept
	    : m_Resource(&resource)
	    , m_Schemas(resource)
	    , m_ByID(resource)
	    , m_ByType(resource)
	{
	}

	SchemaRegistry::~SchemaRegistry() = default;

	SchemaRegistry::SchemaRegistry(SchemaRegistry&& other) noexcept
	    : m_Resource(other.m_Resource)
	    , m_bFrozen(core::Exchange(other.m_bFrozen, false))
	    , m_Schemas(core::Move(other.m_Schemas))
	    , m_ByID(core::Move(other.m_ByID))
	    , m_ByType(core::Move(other.m_ByType))
	{
		other.m_Schemas.Reset();
		other.m_ByID.Reset();
		other.m_ByType.Reset();
	}
} // namespace rox::core
