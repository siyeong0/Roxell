#pragma once

#include <StrongId.hpp>
#include <Vocabulary.hpp>
#include <StringView.hpp>
#include <Span.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Utility.hpp>

#include <optional>
#include <limits>
#include <bit>
#include <span>
#include <string_view>

namespace rox::core
{
	class Value;

	struct SchemaIdTag;
	struct TypeIdTag;
	struct FieldIdTag;

	using SchemaId = StrongId<SchemaIdTag>;
	using TypeId = StrongId<TypeIdTag>;
	using FieldId = StrongId<FieldIdTag>;

	enum class ESchemaValueKind : uint8
	{
		Null,
		Boolean,
		SignedInteger,
		UnsignedInteger,
		FloatingPoint,
		String,
		StableId,
		Bytes,
		Array,
		Object
	};

	enum class ESchemaFieldFlags : uint32
	{
		None = 0,
		Optional = 1 << 0,
		Replicated = 1 << 1,
		EditorOnly = 1 << 2
	};

	struct SchemaValueConstraints
	{
		const Value* Minimum = nullptr;
		const Value* Maximum = nullptr;

		// Object length includes unknown fields and missing fields supplied by schema defaults.
		core::Optional<uint64> MinimumLength;
		core::Optional<uint64> MaximumLength;
	};

	// How a field or a schema presents itself in a tool. Every member is a hint: nothing here
	// changes value validation, canonicalization, or serialized value bytes.
	enum class ESchemaUiHint : uint8
	{
		None = 0,
		// Three or four float lanes in linear RGB(A).
		Color,
		// A bounded number drawn as a slider.
		Slider,
		// A floating-point angle in degrees.
		Angle,
		// A 0..1 value shown as 0..100 %.
		Percent,
		// A multi-line string.
		Multiline,
		// A file path string.
		Path,
		// A StableId that names an asset; RefKind says which kind.
		AssetRef,
		// A StableId that names a scene entity.
		EntityRef,
		// Four lanes shown as Euler degrees.
		Quaternion,
		Hidden,
		ReadOnly
	};

	struct SchemaDisplayMetadata
	{
		StringView DisplayName;
		StringView Description;
		StringView Category;
		StringView Icon;

		ESchemaUiHint Hint = ESchemaUiHint::None;

		// For AssetRef: a token matched against the asset source type ("mesh", "material").
		StringView RefKind;
		StringView Unit;

		float32 Step = 0.0f;
		int32 Order = 0;
	};

	struct SchemaEnumEntry
	{
		StringView Name;
		uint64 Bits = 0;

		static constexpr SchemaEnumEntry Signed(StringView name, int64 value) noexcept
		{
			return {name, core::BitCast<uint64>(value)};
		}

		static constexpr SchemaEnumEntry Unsigned(StringView name, uint64 value) noexcept
		{
			return {name, value};
		}
	};

	inline constexpr usize MAXIMUM_SCHEMA_ENUM_ENTRIES = 4096;
	inline constexpr usize MAXIMUM_SCHEMA_NESTED_ARRAYS = 64;

	// Each entry describes one array inside the preceding array's elements, outermost first.
	struct SchemaArrayDescriptor
	{
		ESchemaValueKind ElementValueKind = ESchemaValueKind::Null;
		TypeId ElementType;

		uint64 MinimumLength = 0;
		uint64 MaximumLength = NumericLimits<uint64>::max();
	};

	struct SchemaFieldDescriptor
	{
		FieldId ID;
		TypeId ValueType;
		StringView Name;

		ESchemaFieldFlags Flags = ESchemaFieldFlags::None;
		ESchemaValueKind ValueKind = ESchemaValueKind::Null;
		ESchemaValueKind ElementValueKind = ESchemaValueKind::Null;
		TypeId ElementType;

		const Value* DefaultValue = nullptr;

		SchemaValueConstraints Constraints;
		SchemaDisplayMetadata Display;

		Span<const SchemaEnumEntry> EnumEntries;
		Span<const SchemaArrayDescriptor> NestedArrays;

		// Maximum byte length of each innermost String/Bytes element in this array shape.
		core::Optional<uint64> ElementMaximumLength;
	};

	[[nodiscard]] const SchemaEnumEntry* FindSchemaEnumEntry(const SchemaFieldDescriptor& field, uint64 bits) noexcept;
	[[nodiscard]] const SchemaEnumEntry* FindSchemaEnumEntry(const SchemaFieldDescriptor& field,
	                                                         StringView name) noexcept;

	struct SchemaDescriptor
	{
		SchemaId ID;
		TypeId Type;
		uint32 Version = 0;
		StringView Name;

		Span<const SchemaFieldDescriptor> Fields;

		// How tools present the schema as a whole: the human name, the category it is listed under.
		SchemaDisplayMetadata Display;
	};
} // namespace rox::core
