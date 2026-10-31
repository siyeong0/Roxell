#include <FailingMemoryResource.h>
#include <SchemaRegistry.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <utility>

using namespace rox::core;

namespace
{
	constexpr TypeId TYPE{22, 1};
	constexpr FieldId FIELD{33, 1};

	SchemaFieldDescriptor makeField() noexcept
	{
		SchemaFieldDescriptor field;
		field.ID = FIELD;
		field.Name = "value";
		field.ValueKind = ESchemaValueKind::FloatingPoint;
		return field;
	}

	Value makeObject(float64 number)
	{
		Value::Object object;
		object.Fields.InsertRequired(FIELD, Value(number));
		return Value(std::move(object));
	}
}

TEST(SchemaRegistryTests, CanonicalLimitsAndAllocation)
{
	const SchemaFieldDescriptor field = makeField();
	const SchemaDescriptor schema{{11, 1}, TYPE, 1, "Limits", {&field, 1}};
	SchemaRegistry registry;
	ASSERT_TRUE(registry.Register(schema));
	registry.Freeze();
	const Value input = makeObject(1.5);
	TrackingMemoryResource tracked(SystemMemoryResource::Get());
	{
		const Result<Value> depth = registry.Canonicalize(TYPE, input, tracked, {0, 2});
		ASSERT_TRUE(!depth && depth.GetError().DiagnosticID == "ROX-CORE-SCHEMA-CANONICAL-DEPTH");
		const Result<Value> count = registry.Canonicalize(TYPE, input, tracked, {1, 1});
		ASSERT_TRUE(!count && count.GetError().DiagnosticID == "ROX-CORE-SCHEMA-CANONICAL-COUNT");
		ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 0);
		FailingMemoryResource failing(tracked, 0);
		const Result<Value> refused = registry.Canonicalize(TYPE, input, failing, {1, 2});
		ASSERT_TRUE(!refused && refused.GetError().Code == EErrorCode::ExternalFailure);
		ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 0);
		const Result<Value> success = registry.Canonicalize(TYPE, input, tracked, {1, 2});
		ASSERT_TRUE(success && success.Value() == input);
	}
	ASSERT_TRUE(tracked.GetStatistics().LiveAllocationCount == 0);
	ASSERT_TRUE(*input.GetObject()->Fields.Get(FIELD).GetFloatingPoint() == 1.5);
}

TEST(SchemaRegistryTests, InvalidValuePrecedesAllocation)
{
	const SchemaFieldDescriptor field = makeField();
	const SchemaDescriptor schema{{11, 1}, TYPE, 1, "Finite", {&field, 1}};
	SchemaRegistry registry;
	ASSERT_TRUE(registry.Register(schema));
	registry.Freeze();
	const Value invalid = makeObject(std::numeric_limits<float64>::infinity());
	FailingMemoryResource refusing(SystemMemoryResource::Get(), 0);
	const Result<void> validation = registry.Validate(TYPE, invalid);
	const Result<Value> canonical = registry.Canonicalize(TYPE, invalid, refusing);
	ASSERT_TRUE(!validation && !canonical);
	ASSERT_TRUE(validation.GetError().DiagnosticID == "ROX-CORE-SCHEMA-NON-FINITE-NUMBER");
	ASSERT_TRUE(canonical.GetError().DiagnosticID == validation.GetError().DiagnosticID);
	ASSERT_TRUE(refusing.GetAllocationAttemptCount() == 0);
}

TEST(SchemaRegistryTests, InvalidDescriptorPreservesRegistry)
{
	SchemaRegistry registry;
	const SchemaFieldDescriptor field = makeField();
	const SchemaDescriptor valid{{11, 1}, TYPE, 1, "Valid", {&field, 1}};
	ASSERT_TRUE(registry.Register(valid));
	const SchemaDescriptor* const original = registry.Find(TYPE);
	const std::array duplicateFields{field, field};
	const SchemaDescriptor duplicate{{11, 2}, {22, 2}, 1, "Duplicate", duplicateFields};
	ASSERT_FALSE(registry.Register(duplicate));
	ASSERT_TRUE(registry.GetSchemaCount() == 1 && registry.Find(TYPE) == original);
	ASSERT_TRUE(registry.Find(TypeId{22, 2}) == nullptr);
}
