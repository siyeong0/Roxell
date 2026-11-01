#include <Value.h>
#include <Vocabulary.hpp>
#include <StringView.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <SystemMemoryResource.h>

#include <utility>
#include <cstring>
#include <algorithm>

namespace rox::core
{
	Result<ValueFieldName> ValueFieldName::Copy(StringView name, MemoryResource& resource) noexcept
	{
		Result<Value> text = Value::CreateString(name, resource);

		if (!text)
		{
			return Result<ValueFieldName>::Failure(text.GetError());
		}

		return Result<ValueFieldName>::Success(ValueFieldName(core::Move(text).TakeValue()));
	}

	bool ValueFieldNameLess::operator()(const ValueFieldName& left, const ValueFieldName& right) const noexcept
	{
		return left.View() < right.View();
	}

	bool ValueFieldNameLess::operator()(const ValueFieldName& left, StringView right) const noexcept
	{
		return left.View() < right;
	}

	bool ValueFieldNameLess::operator()(StringView left, const ValueFieldName& right) const noexcept
	{
		return left < right.View();
	}

	Value::Value(bool bValue) noexcept
	    : m_Storage(bValue)
	{
	}

	Value::Value(int64 value) noexcept
	    : m_Storage(value)
	{
	}

	Value::Value(uint64 value) noexcept
	    : m_Storage(value)
	{
	}

	Value::Value(float64 value) noexcept
	    : m_Storage(value)
	{
	}

	Value::Value(StringView value) noexcept
	    : Value(value, SystemMemoryResource::Get())
	{
	}

	Value::Value(StringView value, MemoryResource& resource) noexcept
	{
		Result<Value> copied = CreateString(value, resource);
		ASSERT(copied, "Value string construction requires sufficient resource memory.");
		*this = core::Move(copied).TakeValue();
	}

	Result<Value> Value::CreateString(StringView value, MemoryResource& resource) noexcept
	{
		Value result;
		TextStorage text;

		if (!value.empty())
		{
			core::Array<char> bytes(resource);

			if (!bytes.Resize(value.size()) || !MakeSharedInto(resource, &text.Owner, core::Move(bytes)))
			{
				return Result<Value>::Failure(EErrorCode::ExternalFailure, "ROX-CORE-VALUE-ALLOCATION", "The Value string could not allocate its resource-owned storage.", "Release memory or reduce input size.");
			}

			core::CopyBytes(text.Owner->Bytes.Data(), value.data(), value.size());
			text.View = {text.Owner->Bytes.Data(), value.size()};
		}

		result.m_Storage = core::Move(text);
		return Result<Value>::Success(core::Move(result));
	}

	Value::Value(StableValueId value) noexcept
	    : m_Storage(value)
	{
	}

	Value::Value(Bytes value) noexcept
	{
		Result<Value> adopted = CreateBytes(core::Move(value));
		ASSERT(adopted, "Value byte adoption requires sufficient resource memory.");
		*this = core::Move(adopted).TakeValue();
	}

	Value::Value(Span<const Byte> bytes, MemoryResource& resource) noexcept
	{
		Result<Value> copied = CreateBytes(bytes, resource);
		ASSERT(copied, "Value byte construction requires sufficient resource memory.");
		*this = core::Move(copied).TakeValue();
	}

	Value::Value(InitializerList<Byte> bytes) noexcept
	    : Value(Span<const Byte>(bytes.begin(), bytes.size()), SystemMemoryResource::Get())
	{
	}

	Result<Value> Value::CreateBytes(Bytes&& bytes) noexcept
	{
		Value result;
		ByteStorage storage;

		if (!bytes.IsEmpty())
		{
			if (!MakeSharedInto(bytes.GetResource(), &storage.Owner, core::Move(bytes)))
			{
				return Result<Value>::Failure(EErrorCode::ExternalFailure, "ROX-CORE-VALUE-ALLOCATION", "The Value bytes could not allocate shared ownership.", "Release memory or reduce input size.");
			}

			storage.View = storage.Owner->View();
		}
		else
		{
			bytes.Reset();
		}

		result.m_Storage = core::Move(storage);
		return Result<Value>::Success(core::Move(result));
	}

	Result<Value> Value::CreateBytes(Span<const Byte> bytes, MemoryResource& resource) noexcept
	{
		Bytes copy(resource);

		if (!copy.Resize(bytes.Size()))
		{
			return Result<Value>::Failure(EErrorCode::ExternalFailure, "ROX-CORE-VALUE-ALLOCATION", "The Value bytes could not allocate their payload.", "Release memory or reduce input size.");
		}

		if (!bytes.IsEmpty())
		{
			core::CopyBytes(copy.Data(), bytes.Data(), bytes.Size());
		}

		return CreateBytes(core::Move(copy));
	}

	Value::Value(Array value) noexcept
	{
		Result<Value> adopted = CreateArray(core::Move(value));
		ASSERT(adopted, "Value array adoption requires sufficient resource memory.");
		*this = core::Move(adopted).TakeValue();
	}

	Result<Value> Value::CreateArray(Array&& values) noexcept
	{
		MemoryResource& resource = values.GetResource();
		ArrayStorage owner;

		if (!MakeSharedInto(resource, &owner, core::Move(values)))
		{
			return Result<Value>::Failure(EErrorCode::ExternalFailure, "ROX-CORE-VALUE-ALLOCATION", "The Value array could not allocate shared ownership.", "Release memory or reduce input size.");
		}
		Value result;
		result.m_Storage = core::Move(owner);
		return Result<Value>::Success(core::Move(result));
	}

	Result<Value::Object> Value::Object::Copy(MemoryResource& resource) const noexcept
	{
		Object copy(resource);

		if (!copy.Fields.Reserve(Fields.Size()) || !copy.UnknownFields.Reserve(UnknownFields.Size()))
		{
			return Result<Object>::Failure(EErrorCode::ExternalFailure, "ROX-CORE-VALUE-ALLOCATION", "The editable Value object could not allocate its field maps.", "Release memory or reduce input size.");
		}

		for (const Value::FieldMap::Entry& entry : Fields)
		{
			copy.Fields.InsertRequired(entry.GetKey(), entry.Value);
		}

		for (const Value::UnknownFieldMap::Entry& entry : UnknownFields)
		{
			Result<ValueFieldName> name = ValueFieldName::Copy(entry.GetKey().View(), resource);

			if (!name)
			{
				return Result<Object>::Failure(name.GetError());
			}
			copy.UnknownFields.InsertRequired(core::Move(name).TakeValue(), entry.Value);
		}

		return Result<Object>::Success(core::Move(copy));
	}

	Value::Value(Object value) noexcept
	{
		MemoryResource& resource = value.Fields.GetResource();
		Result<Value> adopted = CreateObject(core::Move(value), resource);
		ASSERT(adopted, "Value object adoption requires sufficient resource memory.");
		*this = core::Move(adopted).TakeValue();
	}

	Value::Value(Object value, MemoryResource& resource) noexcept
	{
		Result<Value> adopted = CreateObject(core::Move(value), resource);
		ASSERT(adopted, "Value object adoption requires sufficient resource memory.");
		*this = core::Move(adopted).TakeValue();
	}

	Result<Value> Value::CreateObject(Object&& value, MemoryResource& resource) noexcept
	{
		ObjectStorage owner;

		if (!MakeSharedInto(resource, &owner, core::Move(value)))
		{
			return Result<Value>::Failure(EErrorCode::ExternalFailure, "ROX-CORE-VALUE-ALLOCATION", "The Value object could not allocate shared ownership.", "Release memory or reduce input size.");
		}
		Value result;
		result.m_Storage = core::Move(owner);
		return Result<Value>::Success(core::Move(result));
	}

	Value::Value(Value&& other) noexcept
	    : m_Storage(core::Move(other.m_Storage))
	{
		other.m_Storage = core::EmptyValue{};
	}

	Value& Value::operator=(Value&& other) noexcept
	{
		if (this != &other)
		{
			m_Storage = core::Move(other.m_Storage);
			other.m_Storage = core::EmptyValue{};
		}

		return *this;
	}

	ESchemaValueKind Value::GetKind() const noexcept
	{
		switch (m_Storage.index())
		{
		case 0:
			return ESchemaValueKind::Null;
		case 1:
			return ESchemaValueKind::Boolean;
		case 2:
			return ESchemaValueKind::SignedInteger;
		case 3:
			return ESchemaValueKind::UnsignedInteger;
		case 4:
			return ESchemaValueKind::FloatingPoint;
		case 5:
			return ESchemaValueKind::String;
		case 6:
			return ESchemaValueKind::StableId;
		case 7:
			return ESchemaValueKind::Bytes;
		case 8:
			return ESchemaValueKind::Array;
		case 9:
			return ESchemaValueKind::Object;
		default:
			ASSERT(false, "Value storage index must identify a supported schema value kind.");
		}
	}

	const bool* Value::GetBoolean() const noexcept
	{
		return core::GetIf<bool>(&m_Storage);
	}

	const int64* Value::GetSignedInteger() const noexcept
	{
		return core::GetIf<int64>(&m_Storage);
	}

	const uint64* Value::GetUnsignedInteger() const noexcept
	{
		return core::GetIf<uint64>(&m_Storage);
	}

	const float64* Value::GetFloatingPoint() const noexcept
	{
		return core::GetIf<float64>(&m_Storage);
	}

	const StringView* Value::GetString() const noexcept
	{
		const TextStorage* text = core::GetIf<TextStorage>(&m_Storage);
		return text != nullptr ? &text->View : nullptr;
	}

	const StableValueId* Value::GetStableId() const noexcept
	{
		return core::GetIf<StableValueId>(&m_Storage);
	}

	const Span<const Byte>* Value::GetBytes() const noexcept
	{
		const ByteStorage* bytes = core::GetIf<ByteStorage>(&m_Storage);
		return bytes != nullptr ? &bytes->View : nullptr;
	}

	const Value::Array* Value::GetArray() const noexcept
	{
		const ArrayStorage* const storage = core::GetIf<ArrayStorage>(&m_Storage);
		ASSERT(storage == nullptr || static_cast<bool>(*storage), "Array Value storage must own a valid array.");
		return storage != nullptr ? &(*storage)->Data : nullptr;
	}

	const Value::Object* Value::GetObject() const noexcept
	{
		const ObjectStorage* const storage = core::GetIf<ObjectStorage>(&m_Storage);
		ASSERT(storage == nullptr || static_cast<bool>(*storage), "Object Value storage must own a valid object.");
		return storage != nullptr ? &(*storage)->Data : nullptr;
	}

	bool operator==(const Value& left, const Value& right) noexcept
	{
		if (left.GetKind() != right.GetKind())
		{
			return false;
		}

		switch (left.GetKind())
		{
		case ESchemaValueKind::Null:
			return true;
		case ESchemaValueKind::Boolean:
			ASSERT(left.GetBoolean() != nullptr && right.GetBoolean() != nullptr, "Boolean Value kind must have boolean storage.");
			return *left.GetBoolean() == *right.GetBoolean();
		case ESchemaValueKind::SignedInteger:
			ASSERT(left.GetSignedInteger() != nullptr && right.GetSignedInteger() != nullptr, "Signed integer Value kind must have signed integer storage.");
			return *left.GetSignedInteger() == *right.GetSignedInteger();
		case ESchemaValueKind::UnsignedInteger:
			ASSERT(left.GetUnsignedInteger() != nullptr && right.GetUnsignedInteger() != nullptr, "Unsigned integer Value kind must have unsigned integer storage.");
			return *left.GetUnsignedInteger() == *right.GetUnsignedInteger();
		case ESchemaValueKind::FloatingPoint:
			ASSERT(left.GetFloatingPoint() != nullptr && right.GetFloatingPoint() != nullptr, "Floating-point Value kind must have floating-point storage.");
			return *left.GetFloatingPoint() == *right.GetFloatingPoint();
		case ESchemaValueKind::String:
			ASSERT(left.GetString() != nullptr && right.GetString() != nullptr, "String Value kind must have string storage.");
			return *left.GetString() == *right.GetString();
		case ESchemaValueKind::StableId:
			ASSERT(left.GetStableId() != nullptr && right.GetStableId() != nullptr, "Stable ID Value kind must have stable ID storage.");
			return *left.GetStableId() == *right.GetStableId();
		case ESchemaValueKind::Bytes:
			ASSERT(left.GetBytes() != nullptr && right.GetBytes() != nullptr, "Byte array Value kind must have byte array storage.");
			return left.GetBytes()->Size() == right.GetBytes()->Size() && core::Equal(left.GetBytes()->begin(), left.GetBytes()->end(), right.GetBytes()->begin());
		case ESchemaValueKind::Array:
			ASSERT(left.GetArray() != nullptr && right.GetArray() != nullptr, "Array Value kind must have array storage.");
			return left.GetArray()->Size() == right.GetArray()->Size() && core::Equal(left.GetArray()->begin(), left.GetArray()->end(), right.GetArray()->begin());
		case ESchemaValueKind::Object:
			ASSERT(left.GetObject() != nullptr && right.GetObject() != nullptr, "Object Value kind must have object storage.");

			if (left.GetObject()->Fields.Size() != right.GetObject()->Fields.Size() || left.GetObject()->UnknownFields.Size() != right.GetObject()->UnknownFields.Size())
			{
				return false;
			}

			for (usize index = 0; index < left.GetObject()->Fields.Size(); ++index)
			{
				const Value::FieldMap::Entry& a = left.GetObject()->Fields.At(index);
				const Value::FieldMap::Entry& b = right.GetObject()->Fields.At(index);

				if (a.GetKey() != b.GetKey() || !operator==(a.Value, b.Value))
				{
					return false;
				}
			}

			for (usize index = 0; index < left.GetObject()->UnknownFields.Size(); ++index)
			{
				const Value::UnknownFieldMap::Entry& a = left.GetObject()->UnknownFields.At(index);
				const Value::UnknownFieldMap::Entry& b = right.GetObject()->UnknownFields.At(index);

				if (a.GetKey() != b.GetKey() || !operator==(a.Value, b.Value))
				{
					return false;
				}
			}

			return true;
		default:
			ASSERT(false, "Value equality must handle every supported schema value kind.");
		}
	}
} // namespace rox::core
