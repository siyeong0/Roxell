#include <FailingMemoryResource.h>
#include <SchemaRegistry.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <array>
#include <gtest/gtest.h>
#include <string>
#include <utility>

using namespace rox::core;

namespace
{
	constexpr SchemaId SCHEMA{1, 1};
	constexpr TypeId TYPE{2, 1};
	constexpr FieldId FIELD{3, 1};

	SchemaFieldDescriptor makeField() noexcept
	{
		SchemaFieldDescriptor field;
		field.ID = FIELD;
		field.Name = "count";
		field.ValueKind = ESchemaValueKind::UnsignedInteger;
		return field;
	}

	SchemaDescriptor makeSchema(Span<const SchemaFieldDescriptor> fields) noexcept
	{
		return {SCHEMA, TYPE, 1, "Example", fields};
	}

	Value objectWith(Value value)
	{
		Value::Object object;
		object.Fields.InsertRequired(FIELD, std::move(value));
		return Value(std::move(object));
	}
}

TEST(SchemaRegistryTests, RegistrationOwnershipAndRejection)
{
	TrackingMemoryResource memory(SystemMemoryResource::Get());
	{
		SchemaRegistry registry(memory);
		std::string name = "count";
		SchemaFieldDescriptor field = makeField();
		field.Name = name;
		ASSERT_TRUE(registry.Register(makeSchema({&field, 1})));
		const SchemaDescriptor* const stored = registry.Find(TYPE);
		ASSERT_TRUE(stored != nullptr && registry.Find(SCHEMA) == stored);
		name.assign("other");
		ASSERT_TRUE(stored->Fields[0].Name == "count");
		ASSERT_FALSE(registry.Register(makeSchema({&field, 1})));
		ASSERT_TRUE(registry.GetSchemaCount() == 1 && registry.Find(TYPE) == stored);
		SchemaFieldDescriptor invalid = makeField();
		invalid.Name = "bad\xff";
		const SchemaDescriptor invalidSchema{{1, 2}, {2, 2}, 1, "Invalid", {&invalid, 1}};
		ASSERT_FALSE(registry.Register(invalidSchema));
		ASSERT_TRUE(registry.GetSchemaCount() == 1 && registry.Find(TypeId{2, 2}) == nullptr);
		registry.Freeze();
		registry.Freeze();
		ASSERT_TRUE(registry.IsFrozen() && registry.GetSchemaByIndex(0).ID == SCHEMA);
	}
	ASSERT_TRUE(memory.GetStatistics().LiveAllocationCount == 0);
}

TEST(SchemaRegistryTests, RegistrationAllocationFailure)
{
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	{
		SchemaRegistry registry(failing);
		const SchemaFieldDescriptor field = makeField();
		ASSERT_FALSE(registry.Register(makeSchema({&field, 1})));
		ASSERT_TRUE(registry.GetSchemaCount() == 0 && registry.Find(TYPE) == nullptr);
		failing.Reset(100);
		ASSERT_TRUE(registry.Register(makeSchema({&field, 1})));
		ASSERT_TRUE(registry.GetSchemaCount() == 1 && registry.Find(TYPE) != nullptr);
	}
	ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 0);
}

TEST(SchemaRegistryTests, ValidationAndDefaults)
{
	Value minimum(uint64{2});
	Value maximum(uint64{5});
	Value defaultValue(uint64{3});
	SchemaFieldDescriptor field = makeField();
	field.Constraints.Minimum = &minimum;
	field.Constraints.Maximum = &maximum;
	field.DefaultValue = &defaultValue;
	SchemaRegistry registry;
	ASSERT_TRUE(registry.Register(makeSchema({&field, 1})));
	registry.Freeze();
	ASSERT_TRUE(registry.Validate(TYPE, objectWith(Value(uint64{2}))));
	ASSERT_TRUE(registry.Validate(TYPE, objectWith(Value(uint64{5}))));
	ASSERT_FALSE(registry.Validate(TYPE, objectWith(Value(uint64{1}))));
	ASSERT_FALSE(registry.Validate(TYPE, objectWith(Value(uint64{6}))));
	const Result<void> wrongType = registry.Validate(TYPE, objectWith(Value(true)));
	ASSERT_TRUE(!wrongType && wrongType.GetError().DiagnosticID == "ROX-CORE-SCHEMA-FIELD-TYPE-MISMATCH");
	ASSERT_FALSE(registry.Validate(TypeId{9, 9}, objectWith(Value(uint64{3}))));
	Value empty(Value::Object{});
	ASSERT_TRUE(registry.Validate(TYPE, empty));
	Result<Value> canonical = registry.Canonicalize(TYPE, empty);
	ASSERT_TRUE(canonical && canonical.Value().GetObject() != nullptr);
	const Value* const defaultField = canonical.Value().GetObject()->Fields.Find(FIELD);
	ASSERT_TRUE(defaultField != nullptr && defaultField->GetUnsignedInteger() != nullptr);
	ASSERT_TRUE(*defaultField->GetUnsignedInteger() == 3);
	ASSERT_TRUE(empty.GetObject()->Fields.IsEmpty());
}

TEST(SchemaRegistryTests, EnumMembership)
{
	const std::array entries{SchemaEnumEntry::Unsigned("Off", 0), SchemaEnumEntry::Unsigned("On", 7)};
	SchemaFieldDescriptor field = makeField();
	field.EnumEntries = entries;
	SchemaRegistry registry;
	ASSERT_TRUE(registry.Register(makeSchema({&field, 1})));
	registry.Freeze();
	const SchemaDescriptor* const descriptor = registry.Find(TYPE);
	ASSERT_TRUE(descriptor != nullptr && descriptor->Fields.size() == 1);
	const SchemaFieldDescriptor& stored = descriptor->Fields[0];
	const SchemaEnumEntry* const on = FindSchemaEnumEntry(stored, "On");
	const SchemaEnumEntry* const off = FindSchemaEnumEntry(stored, uint64{0});
	ASSERT_TRUE(on != nullptr && on->Bits == 7);
	ASSERT_TRUE(off != nullptr && off->Name == "Off");
	ASSERT_TRUE(FindSchemaEnumEntry(stored, "Unknown") == nullptr);
	ASSERT_TRUE(FindSchemaEnumEntry(stored, uint64{1}) == nullptr);
	ASSERT_TRUE(registry.Validate(TYPE, objectWith(Value(uint64{7}))));
	const Result<void> invalid = registry.Validate(TYPE, objectWith(Value(uint64{1})));
	ASSERT_TRUE(!invalid && invalid.GetError().DiagnosticID == "ROX-CORE-SCHEMA-ENUM-VALUE");
}

TEST(SchemaRegistryTests, RegistrationOrderStableViewsAndMove)
{
	SchemaRegistry registry;
	const SchemaFieldDescriptor field = makeField();
	ASSERT_TRUE(registry.Register(makeSchema({&field, 1})));
	const SchemaDescriptor* const first = registry.Find(TYPE);
	const SchemaDescriptor second{{1, 2}, {2, 2}, 1, "Second", {}};
	ASSERT_TRUE(registry.Register(second));
	EXPECT_EQ(registry.Find(TYPE), first);
	registry.Freeze();
	EXPECT_EQ(registry.GetSchemaByIndex(0).ID, SCHEMA);
	EXPECT_EQ(registry.GetSchemaByIndex(1).ID, second.ID);
	SchemaRegistry moved(std::move(registry));
	EXPECT_EQ(moved.Find(TYPE), first);
	EXPECT_EQ(moved.GetSchemaCount(), 2);
	EXPECT_EQ(registry.GetSchemaCount(), 0);
	const SchemaDescriptor third{{1, 3}, {2, 3}, 1, "Third", {}};
	EXPECT_TRUE(registry.Register(third));
	EXPECT_NE(registry.Find(third.Type), nullptr);
}

TEST(SchemaRegistryTests, InvalidDescriptorsLeaveExistingRegistrationUnchanged)
{
	SchemaRegistry registry;
	const SchemaFieldDescriptor field = makeField();
	ASSERT_TRUE(registry.Register(makeSchema({&field, 1})));
	const SchemaDescriptor* const existing = registry.Find(TYPE);
	SchemaFieldDescriptor duplicateFields[] = {field, field};
	const SchemaDescriptor duplicate{{1, 2}, {2, 2}, 1, "Duplicate", duplicateFields};
	const Result<void> duplicateResult = registry.Register(duplicate);
	ASSERT_FALSE(duplicateResult);
	EXPECT_EQ(duplicateResult.GetError().DiagnosticID, "ROX-CORE-SCHEMA-DUPLICATE-FIELD");
	Value minimum(uint64{5});
	Value maximum(uint64{2});
	SchemaFieldDescriptor inverted = field;
	inverted.Constraints.Minimum = &minimum;
	inverted.Constraints.Maximum = &maximum;
	const SchemaDescriptor invalid{{1, 3}, {2, 3}, 1, "Invalid", {&inverted, 1}};
	const Result<void> invertedResult = registry.Register(invalid);
	ASSERT_FALSE(invertedResult);
	EXPECT_EQ(invertedResult.GetError().DiagnosticID, "ROX-CORE-SCHEMA-INVERTED-NUMERIC-RANGE");
	EXPECT_EQ(registry.GetSchemaCount(), 1);
	EXPECT_EQ(registry.Find(TYPE), existing);
	EXPECT_EQ(registry.Find(duplicate.Type), nullptr);
	EXPECT_EQ(registry.Find(invalid.Type), nullptr);
}

TEST(SchemaRegistryTests, CanonicalizationFailurePreservesInputAndCanRetry)
{
	SchemaRegistry registry;
	const SchemaFieldDescriptor field = makeField();
	ASSERT_TRUE(registry.Register(makeSchema({&field, 1})));
	registry.Freeze();
	const Value input = objectWith(Value(uint64{42}));
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	FailingMemoryResource failing(tracked, 0);
	const Result<Value> failed = registry.Canonicalize(TYPE, input, failing);
	EXPECT_FALSE(failed);
	EXPECT_EQ(tracked.GetStatistics().LiveAllocationCount, 0);
	ASSERT_NE(input.GetObject(), nullptr);
	ASSERT_NE(input.GetObject()->Fields.Find(FIELD), nullptr);
	EXPECT_EQ(*input.GetObject()->Fields.Find(FIELD)->GetUnsignedInteger(), 42);
	const Result<Value> retried = registry.Canonicalize(TYPE, input, tracked);
	ASSERT_TRUE(retried);
	EXPECT_TRUE(retried.Value() == input);
}
