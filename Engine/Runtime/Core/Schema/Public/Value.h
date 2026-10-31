#pragma once

#include <Assertion.h>
#include <Vocabulary.hpp>
#include <StringView.hpp>
#include <Types.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <CoreString.hpp>
#include <Result.hpp>
#include <SharedPtr.hpp>
#include <Schema.h>
#include <StrongId.hpp>
#include <FlatMap.hpp>

#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace rox::core
{
	struct StableValueIdTag;
	using StableValueId = StrongId<StableValueIdTag>;
	class ValueFieldName;

	struct ValueFieldNameLess final
	{
		bool operator()(const ValueFieldName& left, const ValueFieldName& right) const noexcept;
		bool operator()(const ValueFieldName& left, rox::core::StringView right) const noexcept;
		bool operator()(rox::core::StringView left, const ValueFieldName& right) const noexcept;
	};

	class Value final
	{
	public:
		using Bytes = rox::core::Array<Byte>;
		using Array = rox::core::Array<Value>;
		using FieldMap = FlatMap<FieldId, Value>;
		using UnknownFieldMap = FlatMap<ValueFieldName, Value, ValueFieldNameLess>;

		struct Object;

	public:
		// Allocating constructors require sufficient memory. Create functions return allocation failures.
		Value() = default;
		Value(const Value&) = default;
		Value& operator=(const Value&) = default;
		Value(Value&& other) noexcept;
		Value& operator=(Value&& other) noexcept;

		explicit Value(bool bValue) noexcept;
		explicit Value(int64 value) noexcept;
		explicit Value(uint64 value) noexcept;
		explicit Value(float64 value) noexcept;

		template <rox::core::usize Size>
		explicit Value(const char (&value)[Size]) noexcept
		    : Value(rox::core::StringView(value, Size - (value[Size - 1] == '\0' ? 1 : 0)))
		{
		}

		template <typename TPointer>
		    requires rox::core::IsPointer<rox::core::RemoveCv<rox::core::RemoveReference<TPointer>>>
		explicit Value(TPointer&&) = delete;
		explicit Value(rox::core::Nullptr) = delete;

		explicit Value(rox::core::StringView value) noexcept;
		Value(rox::core::StringView value, MemoryResource& resource) noexcept;
		[[nodiscard]] static Result<Value> CreateString(rox::core::StringView value, MemoryResource& resource) noexcept;

		explicit Value(StableValueId value) noexcept;

		explicit Value(Bytes value) noexcept;
		Value(Span<const Byte> bytes, MemoryResource& resource) noexcept;
		explicit Value(rox::core::InitializerList<Byte> bytes) noexcept;
		[[nodiscard]] static Result<Value> CreateBytes(Bytes&& bytes) noexcept;
		[[nodiscard]] static Result<Value> CreateBytes(Span<const Byte> bytes, MemoryResource& resource) noexcept;

		explicit Value(Array value) noexcept;
		[[nodiscard]] static Result<Value> CreateArray(Array&& values) noexcept;

		explicit Value(Object value) noexcept;
		Value(Object value, MemoryResource& resource) noexcept;
		[[nodiscard]] static Result<Value> CreateObject(Object&& value, MemoryResource& resource) noexcept;

		[[nodiscard]] ESchemaValueKind GetKind() const noexcept;

		[[nodiscard]] const bool* GetBoolean() const noexcept;
		[[nodiscard]] const int64* GetSignedInteger() const noexcept;
		[[nodiscard]] const uint64* GetUnsignedInteger() const noexcept;
		[[nodiscard]] const float64* GetFloatingPoint() const noexcept;
		[[nodiscard]] const rox::core::StringView* GetString() const noexcept;
		[[nodiscard]] const StableValueId* GetStableId() const noexcept;
		[[nodiscard]] const Span<const Byte>* GetBytes() const noexcept;
		[[nodiscard]] const Array* GetArray() const noexcept;
		[[nodiscard]] const Object* GetObject() const noexcept;

	private:
		struct TextBuffer final
		{
			rox::core::Array<char> Bytes;

			explicit TextBuffer(rox::core::Array<char>&& bytes) noexcept
			    : Bytes(rox::core::Move(bytes))
			{
			}
		};

		struct TextStorage final
		{
			SharedPtr<TextBuffer> Owner;
			rox::core::StringView View;
		};

		struct ArrayOwner;
		using ArrayStorage = SharedPtr<ArrayOwner>;

		struct ByteStorage final
		{
			SharedPtr<Bytes> Owner;
			Span<const Byte> View;
		};

		struct ObjectOwner;
		using ObjectStorage = SharedPtr<ObjectOwner>;

		// Raw text preserves malformed UTF-8 for schema validation. Shared buffers remain immutable.
		using Storage = rox::core::Variant<rox::core::EmptyValue, bool, int64, uint64, float64, TextStorage, StableValueId, ByteStorage, ArrayStorage, ObjectStorage>;

	private:
		Storage m_Storage;
	};

	[[nodiscard]] bool operator==(const Value& left, const Value& right) noexcept;

	// Raw immutable key bytes preserve malformed input for the schema validation boundary.
	class ValueFieldName final
	{
	public:
		ValueFieldName(rox::core::StringView name, MemoryResource& resource) noexcept
		    : m_Text(name, resource)
		{
		}

		[[nodiscard]] static Result<ValueFieldName> Copy(rox::core::StringView name, MemoryResource& resource) noexcept;

		[[nodiscard]] rox::core::StringView View() const noexcept
		{
			ASSERT(m_Text.GetString() != nullptr, "Value field name access requires an unmoved name.");
			return *m_Text.GetString();
		}

		bool operator==(const ValueFieldName& right) const noexcept
		{
			return View() == right.View();
		}

	private:
		explicit ValueFieldName(Value&& text) noexcept
		    : m_Text(rox::core::Move(text))
		{
		}

	private:
		Value m_Text;
	};

	struct Value::Object
	{
		FieldMap Fields;
		UnknownFieldMap UnknownFields;

		explicit Object(MemoryResource& resource) noexcept
		    : Fields(resource)
		    , UnknownFields(resource)
		{
		}

		Object() noexcept
		    : Object(SystemMemoryResource::Get())
		{
		}

		// Maps and names are copied into resource; immutable child Values retain their existing owners.
		[[nodiscard]] Result<Object> Copy(MemoryResource& resource) const noexcept;
	};

	struct Value::ArrayOwner final
	{
		explicit ArrayOwner(Array&& values) noexcept
		    : Data(rox::core::Move(values))
		{
		}

		Array Data;
	};

	struct Value::ObjectOwner final
	{
		explicit ObjectOwner(Object&& value) noexcept
		    : Data(rox::core::Move(value))
		{
		}

		Object Data;
	};
} // namespace rox::core
