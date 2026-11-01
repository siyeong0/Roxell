#pragma once

#include <Array.hpp>
#include <Types.h>
#include <Result.hpp>
#include <Schema.h>
#include <UniquePtr.hpp>
#include <Value.h>

#include <cstddef>

namespace rox::core
{
	class MemoryResource;

	struct SchemaCanonicalizationLimits final
	{
		static constexpr uint32 HARD_MAXIMUM_DEPTH = 256;

		uint32 MaximumDepth = HARD_MAXIMUM_DEPTH;
		uint64 MaximumValues = 4'194'304;
	};

	class SchemaRegistry final
	{
	public:
		// Bootstrap construction uses SystemMemoryResource; production owners inject their resource.
		SchemaRegistry();
		// The resource allocates every schema node, name byte, field array, and index entry the registry owns. It
		// must outlive the registry and every descriptor view the registry publishes.
		explicit SchemaRegistry(MemoryResource& resource) noexcept;
		~SchemaRegistry();

		// Move construction adopts the source storage and its resource wholesale, so the registry and its nodes
		// always name one allocation domain. A moved-from registry keeps its resource and stays usable.
		SchemaRegistry(SchemaRegistry&& other) noexcept;
		// Assignment is disabled to keep the node owners and lookup storage in one allocation domain.
		SchemaRegistry& operator=(SchemaRegistry&&) = delete;

		SchemaRegistry(const SchemaRegistry&) = delete;
		SchemaRegistry& operator=(const SchemaRegistry&) = delete;

		// Register requires a mutable registry; Canonicalize requires Freeze. Misuse asserts.
		// Invalid descriptors, display metadata and allocation failures leave the registry unchanged.
		[[nodiscard]] Result<void> Register(const SchemaDescriptor& descriptor);
		void Freeze() noexcept;

		[[nodiscard]] bool IsFrozen() const noexcept { return m_bFrozen; }

		[[nodiscard]] const SchemaDescriptor* Find(SchemaId id) const;
		[[nodiscard]] const SchemaDescriptor* Find(TypeId type) const;
		[[nodiscard]] usize GetSchemaCount() const noexcept;
		[[nodiscard]] const SchemaDescriptor& GetSchemaByIndex(usize index) const;

		// Checks constraints after accounting for defaults without modifying the input.
		[[nodiscard]] Result<void> Validate(TypeId type, const Value& value) const;
		[[nodiscard]] Result<Value> Canonicalize(TypeId type, const Value& value) const;
		// The destination resource owns all returned descendants and must outlive every shared result copy.
		[[nodiscard]] Result<Value> Canonicalize(TypeId type, const Value& value, MemoryResource& resource, SchemaCanonicalizationLimits limits = {}) const;

	private:
		struct OwnedSchema;
		struct CanonicalizationContext;

		// The two lookup indexes use StrongId ordering over (High, Low). Lookup is a binary search;
		// registration shifts entries within the reserved schema count. Index entries borrow the
		// separately owned schema nodes, whose addresses remain stable as the indexes grow.
		struct SchemaIndexEntry
		{
			SchemaId Key;
			OwnedSchema* Schema = nullptr;
		};

		struct TypeIndexEntry
		{
			TypeId Key;
			OwnedSchema* Schema = nullptr;
		};

		[[nodiscard]] Result<Value> canonicalizeGenericValue(const Value& value, CanonicalizationContext& context, uint32 depth) const;
		[[nodiscard]] Result<Value> canonicalizeFieldValue(const SchemaFieldDescriptor& field, const Value& value, CanonicalizationContext& context, uint32 depth) const;
		[[nodiscard]] Result<Value> canonicalizeObject(const SchemaDescriptor& descriptor, const Value& value, CanonicalizationContext& context, uint32 depth) const;

		[[nodiscard]] bool validateGenericValueImpl(const Value& value, Error& error) const;
		[[nodiscard]] bool validateArrayField(const SchemaFieldDescriptor& field, const Value& value, Error& error) const;
		[[nodiscard]] bool validateFieldValueImpl(const SchemaFieldDescriptor& field, const Value& value, Error& error) const;
		[[nodiscard]] bool validateObjectImpl(const SchemaDescriptor& descriptor, const Value& value, Error& error) const;
		[[nodiscard]] Result<void> validateFieldValue(const SchemaFieldDescriptor& field, const Value& value) const;
		[[nodiscard]] Result<void> validateObject(const SchemaDescriptor& descriptor, const Value& value) const;

	private:
		MemoryResource* m_Resource = nullptr;
		bool m_bFrozen = false;

		// Registration order is the enumeration contract of GetSchemaByIndex, and the owning indirection keeps
		// every descriptor view Find publishes valid across later registrations.
		Array<UniquePtr<OwnedSchema>> m_Schemas;

		Array<SchemaIndexEntry> m_ByID;
		Array<TypeIndexEntry> m_ByType;
	};
} // namespace rox::core
