#include <CanonicalJsonCodec.h>
#include <TextPrimitives.hpp>
#include <Vocabulary.hpp>
#include <SystemError.hpp>
#include <StringView.hpp>
#include <Span.hpp>
#include <Types.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include "CanonicalJsonDocument.h"
#include "CanonicalJsonWriter.h"

#include <Assertion.h>
#include <SchemaRegistry.h>
#include <SystemMemoryResource.h>
#include <ValueTraversal.h>
#include <optional>

#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <cstddef>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace rox::core
{
	namespace
	{
		enum class EJsonDefaultLimit
		{
			None,
			Depth,
			Count
		};

		EJsonDefaultLimit ConsumeJsonDefaultBudget(const Value& value, uint32 depth, uint32 maximumDepth, uint64& remaining) noexcept
		{
			if (value.GetArray() || value.GetObject() || value.GetBytes())
			{
				++depth;
			}

			if (depth > maximumDepth)
			{
				return EJsonDefaultLimit::Depth;
			}

			if (remaining == 0)
			{
				return EJsonDefaultLimit::Count;
			}

			--remaining;

			if (const Span<const Byte>* bytes = value.GetBytes())
			{
				if (bytes->Size() > remaining)
				{
					return EJsonDefaultLimit::Count;
				}

				remaining -= bytes->Size();
			}

			if (const Value::Array* array = value.GetArray())
			{
				for (const Value& child : *array)
				{
					const EJsonDefaultLimit result = ConsumeJsonDefaultBudget(child, depth, maximumDepth, remaining);

					if (result != EJsonDefaultLimit::None)
					{
						return result;
					}
				}
			}

			if (const Value::Object* object = value.GetObject())
			{
				for (const Value::FieldMap::Entry& entry : object->Fields)
				{
					const EJsonDefaultLimit result = ConsumeJsonDefaultBudget(entry.Value, depth, maximumDepth, remaining);

					if (result != EJsonDefaultLimit::None)
					{
						return result;
					}
				}

				for (const Value::UnknownFieldMap::Entry& entry : object->UnknownFields)
				{
					const EJsonDefaultLimit result = ConsumeJsonDefaultBudget(entry.Value, depth, maximumDepth, remaining);

					if (result != EJsonDefaultLimit::None)
					{
						return result;
					}
				}
			}

			return EJsonDefaultLimit::None;
		}

		struct ReadContext
		{
			const JsonReadLimits& Limits;
			const SchemaRegistry& Registry;
			MemoryResource& Resource;

			uint64 ValueCount = 0;
		};

		struct WriteLimitContext
		{
			const JsonWriteLimits& Limits;

			uint64 ValueCount = 0;
			uint64 MinimumBytes = 0;
		};

		template <typename TValue>
		[[nodiscard]] Result<TValue> failure(EErrorCode code, StringView diagnosticId, StringView message, StringView recoveryHint)
		{
			return Result<TValue>::Failure(code, diagnosticId, message, recoveryHint);
		}

		template <typename TValue>
		[[nodiscard]] Result<TValue> failure(const Error& error)
		{
			return Result<TValue>::Failure(error);
		}

		Result<Value> readAllocationFailure()
		{
			return failure<Value>(EErrorCode::ExternalFailure, "ROX-CORE-JSON-READ-ALLOCATION",
			                      "Canonical JSON reading could not allocate its temporary state.",
			                      "Free memory, lower the read limits, and retry the operation.");
		}

		Result<Value> normalizeReadAllocation(Result<Value> result)
		{
			if (!result && (result.GetError().DiagnosticID == "ROX-CORE-VALUE-ALLOCATION" || result.GetError().DiagnosticID == "ROX-CORE-SCHEMA-REGISTRY-ALLOCATION"))
			{
				return readAllocationFailure();
			}

			return result;
		}

		[[nodiscard]] bool hasFlag(ESchemaFieldFlags value, ESchemaFieldFlags flag) noexcept
		{
			return (static_cast<uint32>(value) & static_cast<uint32>(flag)) != 0;
		}

		[[nodiscard]] Result<void> consumeValue(ReadContext* context)
		{
			ASSERT(context != nullptr, "Canonical JSON value counting requires a valid read context.");

			if (context->ValueCount >= context->Limits.MaxValues)
			{
				return failure<void>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-VALUE-LIMIT", "The JSON document exceeds the configured value count limit.", "Reduce the document size or raise JsonReadLimits::MaxValues intentionally.");
			}

			++context->ValueCount;
			return Result<void>::Success();
		}

		[[nodiscard]] Result<void> checkLexicalDepth(rox::core::Span<const Byte> bytes, const JsonReadLimits& limits)
		{
			uint32 depth = 0;
			bool bInString = false;
			bool bEscaped = false;

			for (const Byte byte : bytes)
			{
				const char character = static_cast<char>(rox::core::ToInteger<uint8>(byte));

				if (bInString)
				{
					if (bEscaped)
					{
						bEscaped = false;
					}
					else if (character == '\\')
					{
						bEscaped = true;
					}
					else if (character == '"')
					{
						bInString = false;
					}
					continue;
				}

				if (character == '"')
				{
					bInString = true;
				}
				else if (character == '{' || character == '[')
				{
					++depth;

					if (depth > limits.MaxDepth)
					{
						return failure<void>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-DEPTH-LIMIT",
						                     "The JSON document exceeds the configured nesting depth.",
						                     "Reduce nesting or raise JsonReadLimits::MaxDepth intentionally.");
					}
				}
				else if (character == '}' || character == ']')
				{
					if (depth == 0)
					{
						return failure<void>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-MALFORMED",
						                     "The JSON document contains an unmatched closing delimiter.",
						                     "Repair the malformed JSON before loading it.");
					}

					--depth;
				}
			}

			if (bInString || depth != 0)
			{
				return failure<void>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-TRUNCATED",
				                     "The JSON document ends inside a string, object, or array.",
				                     "Provide the complete JSON byte sequence.");
			}

			return Result<void>::Success();
		}

		[[nodiscard]] Result<void> addMinimumBytes(WriteLimitContext* const context, const uint64 count)
		{
			ASSERT(context != nullptr, "Canonical JSON write byte counting requires a valid context.");

			if (count > context->Limits.MaxBytes - context->MinimumBytes)
			{
				return failure<void>(EErrorCode::Unsupported, "ROX-CORE-JSON-WRITE-BYTE-LIMIT", "The canonical JSON output exceeds the configured byte limit.", "Reduce the value size or raise JsonWriteLimits::MaxBytes intentionally.");
			}

			context->MinimumBytes += count;
			return Result<void>::Success();
		}

		[[nodiscard]] Result<void> consumeWriteValues(WriteLimitContext* const context, const uint64 count)
		{
			ASSERT(context != nullptr, "Canonical JSON write value counting requires a valid context.");

			if (count > context->Limits.MaxValues - context->ValueCount)
			{
				return failure<void>(EErrorCode::Unsupported, "ROX-CORE-JSON-WRITE-VALUE-LIMIT", "The canonical JSON value exceeds the configured value count limit.", "Reduce the value count or raise JsonWriteLimits::MaxValues intentionally.");
			}

			context->ValueCount += count;
			return Result<void>::Success();
		}

		[[nodiscard]] Result<void> countEscapedString(const rox::core::StringView text, WriteLimitContext* const context)
		{
			Result<void> result = addMinimumBytes(context, 2);

			if (!result)
			{
				return result;
			}

			for (const unsigned char character : text)
			{
				const uint64 encodedBytes = character == '"' || character == '\\' || character == '\b' ||
				                                    character == '\f' || character == '\n' || character == '\r' ||
				                                    character == '\t'
				                                ? 2
				                            : character < 0x20 ? 6
				                                               : 1;
				result = addMinimumBytes(context, encodedBytes);

				if (!result)
				{
					return result;
				}
			}

			return Result<void>::Success();
		}

		[[nodiscard]] Result<void> checkWriteValue(const Value& value, const uint32 depth, WriteLimitContext* const context)
		{
			ASSERT(context != nullptr, "Canonical JSON write validation requires a valid context.");

			struct Frame final
			{
				const Value* Node = nullptr;
				rox::core::usize ArrayIndex = 0;
				Value::FieldMap::ConstIterator Known;
				Value::UnknownFieldMap::ConstIterator Unknown;
				bool bEntered = false;
			};

			rox::core::StaticArray<Frame, ValueCloneLimits::HARD_MAXIMUM_DEPTH> frames{};
			rox::core::usize frameCount = 1;
			frames[0].Node = &value;
			Result<void> result = Result<void>::Success();

			while (frameCount != 0)
			{
				Frame& frame = frames[frameCount - 1];
				const Value& current = *frame.Node;

				if (!frame.bEntered)
				{
					if (depth + frameCount - 1 > context->Limits.MaxDepth)
					{
						return failure<void>(EErrorCode::Unsupported, "ROX-CORE-JSON-WRITE-DEPTH-LIMIT",
						                     "The canonical JSON value exceeds the configured nesting depth.",
						                     "Reduce nesting or raise JsonWriteLimits::MaxDepth intentionally.");
					}

					result = consumeWriteValues(context, 1);

					if (!result)
					{
						return result;
					}

					switch (current.GetKind())
					{
					case ESchemaValueKind::Null:
						result = addMinimumBytes(context, 4);
						break;
					case ESchemaValueKind::Boolean:
						ASSERT(current.GetBoolean() != nullptr, "Boolean Value kind must have boolean storage.");
						result = addMinimumBytes(context, *current.GetBoolean() ? 4 : 5);
						break;
					case ESchemaValueKind::SignedInteger:
					case ESchemaValueKind::UnsignedInteger:
					case ESchemaValueKind::FloatingPoint:
						result = addMinimumBytes(context, 1);
						break;
					case ESchemaValueKind::String:
						ASSERT(current.GetString() != nullptr, "String Value kind must have string storage.");
						result = countEscapedString(*current.GetString(), context);
						break;
					case ESchemaValueKind::StableId:
						result = addMinimumBytes(context, 34);
						break;
					case ESchemaValueKind::Bytes:
					{
						const Span<const Byte>* const bytes = current.GetBytes();
						ASSERT(bytes != nullptr, "Byte array Value kind must have byte array storage.");
						result = consumeWriteValues(context, static_cast<uint64>(bytes->Size()));

						if (!result)
						{
							return result;
						}

						result = addMinimumBytes(context, 2 + (bytes->IsEmpty() ? 0 : bytes->Size() - 1));

						if (!result)
						{
							return result;
						}

						for (const Byte byte : *bytes)
						{
							const uint8 valueByte = rox::core::ToInteger<uint8>(byte);
							result = addMinimumBytes(context, valueByte >= 100 ? 3 : valueByte >= 10 ? 2 : 1);

							if (!result)
							{
								return result;
							}
						}
						break;
					}
					case ESchemaValueKind::Array:
					{
						const Value::Array* const array = current.GetArray();
						ASSERT(array != nullptr, "Array Value kind must have array storage.");
						result = addMinimumBytes(context, 2 + (array->IsEmpty() ? 0 : array->Size() - 1));
						break;
					}
					case ESchemaValueKind::Object:
					{
						const Value::Object* const object = current.GetObject();
						ASSERT(object != nullptr, "Object Value kind must have object storage.");
						const uint64 fieldCount = static_cast<uint64>(object->Fields.Size()) +
						                          static_cast<uint64>(object->UnknownFields.Size());
						result = addMinimumBytes(context, 2 + (fieldCount == 0 ? 0 : fieldCount - 1));
						frame.Known = object->Fields.begin();
						frame.Unknown = object->UnknownFields.begin();
						break;
					}
					default:
						ASSERT(false, "Canonical JSON write limits must handle every Value storage alternative.");
					}

					if (!result)
					{
						return result;
					}

					frame.bEntered = true;
				}

				const Value* child = nullptr;

				if (const Value::Array* array = current.GetArray())
				{
					if (frame.ArrayIndex < array->Size())
					{
						child = &(*array)[frame.ArrayIndex++];
					}
				}
				else if (const Value::Object* object = current.GetObject())
				{
					if (frame.Known != object->Fields.end())
					{
						child = &frame.Known->Value;
						++frame.Known;
					}
					else if (frame.Unknown != object->UnknownFields.end())
					{
						result = countEscapedString(frame.Unknown->GetKey().View(), context);

						if (result)
						{
							result = addMinimumBytes(context, 1);
						}

						if (!result)
						{
							return result;
						}

						child = &frame.Unknown->Value;
						++frame.Unknown;
					}
				}

				if (child == nullptr)
				{
					--frameCount;
					continue;
				}

				if (frameCount == frames.size())
				{
					return failure<void>(EErrorCode::Unsupported, "ROX-CORE-JSON-WRITE-DEPTH-LIMIT",
					                     "The canonical JSON value exceeds the configured nesting depth.",
					                     "Reduce nesting or raise JsonWriteLimits::MaxDepth intentionally.");
				}
				frames[frameCount++] = Frame{.Node = child};
			}

			return Result<void>::Success();
		}

		[[nodiscard]] Result<void> validateWriteLimits(const JsonWriteLimits& limits)
		{
			constexpr uint32 MAXIMUM_SUPPORTED_WRITE_DEPTH = 256;

			if (limits.MaxBytes == 0 || limits.MaxDepth == 0 || limits.MaxValues == 0 ||
			    limits.MaxDepth > MAXIMUM_SUPPORTED_WRITE_DEPTH)
			{
				return failure<void>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-WRITE-LIMITS",
				                     "Canonical JSON write limits are zero or exceed the supported nesting depth.",
				                     "Provide non-zero limits with a maximum nesting depth of 256.");
			}

			return Result<void>::Success();
		}

		[[nodiscard]] Result<void> checkWriteLimits(const Value& value, const JsonWriteLimits& limits)
		{
			const Result<void> limitsResult = validateWriteLimits(limits);

			if (!limitsResult)
			{
				return limitsResult;
			}
			WriteLimitContext context{limits};
			return checkWriteValue(value, 1, &context);
		}

		[[nodiscard]] const SchemaFieldDescriptor* findFieldByName(const SchemaDescriptor& schema, rox::core::StringView name) noexcept
		{
			for (const SchemaFieldDescriptor& field : schema.Fields)
			{
				if (field.Name == name)
				{
					return &field;
				}
			}

			return nullptr;
		}

		[[nodiscard]] Result<void> checkSchemaWriteLimits(const Value& value, const SchemaDescriptor& schema, const SchemaRegistry& registry, const JsonWriteLimits& limits)
		{
			const Result<void> limitsResult = validateWriteLimits(limits);

			if (!limitsResult)
			{
				return limitsResult;
			}

			struct Frame final
			{
				const Value* Node = nullptr;
				const SchemaDescriptor* Schema = nullptr;
				TypeId ObjectType;
				TypeId ElementType;
				rox::core::Span<const SchemaArrayDescriptor> NestedArrays;
				ESchemaValueKind Kind = ESchemaValueKind::Null;
				ESchemaValueKind ElementKind = ESchemaValueKind::Null;
				rox::core::usize Index = 0;
				Value::UnknownFieldMap::ConstIterator Unknown;
				uint64 Emitted = 0;
				bool bGeneric = false;
				bool bEntered = false;
			};

			rox::core::StaticArray<Frame, ValueCloneLimits::HARD_MAXIMUM_DEPTH> frames{};
			frames[0].Node = &value;
			frames[0].Schema = &schema;
			frames[0].Kind = ESchemaValueKind::Object;
			rox::core::usize count = 1;
			WriteLimitContext context{limits};
			while (count != 0)
			{
				Frame& frame = frames[count - 1];
				const uint32 depth = static_cast<uint32>(count);
				Result<void> result = Result<void>::Success();

				if (!frame.bEntered)
				{
					ASSERT(frame.Node != nullptr, "Write traversal frames must retain an immutable value.");
					ASSERT(frame.bGeneric || frame.Node->GetKind() == frame.Kind, "A schema-validated value must retain its declared storage kind.");

					if (frame.bGeneric || (frame.Kind != ESchemaValueKind::Array && frame.Kind != ESchemaValueKind::Object))
					{
						result = checkWriteValue(*frame.Node, depth, &context);

						if (!result)
						{
							return result;
						}

						--count;
						continue;
					}

					if (frame.Kind == ESchemaValueKind::Object && frame.ObjectType.IsValid())
					{
						frame.Schema = registry.Find(frame.ObjectType);

						if (frame.Schema == nullptr)
						{
							return failure<void>(
							    EErrorCode::NotFound, "ROX-CORE-JSON-NESTED-SCHEMA-NOT-FOUND",
							    "A nested object schema is not present in the registry.",
							    "Register the complete schema dependency set before writing the document.");
						}
					}

					if (depth > limits.MaxDepth)
					{
						return failure<void>(EErrorCode::Unsupported, "ROX-CORE-JSON-WRITE-DEPTH-LIMIT",
						                     "The canonical JSON value exceeds the configured nesting depth.",
						                     "Reduce nesting or raise JsonWriteLimits::MaxDepth intentionally.");
					}

					result = consumeWriteValues(&context, 1);

					if (frame.Kind == ESchemaValueKind::Array)
					{
						const Value::Array* const array = frame.Node->GetArray();
						ASSERT(array != nullptr, "A schema-validated array must retain array storage.");

						if (result)
						{
							result = addMinimumBytes(&context, 2 + (array->IsEmpty() ? 0 : array->Size() - 1));
						}
					}
					else
					{
						const Value::Object* const object = frame.Node->GetObject();
						ASSERT(object != nullptr, "A schema-validated object must retain object storage.");
						ASSERT(frame.Schema != nullptr || object->Fields.IsEmpty(), "A validated open object cannot contain schema-bound field IDs.");
						frame.Unknown = object->UnknownFields.begin();

						if (result)
						{
							result = addMinimumBytes(&context, 1);
						}
					}

					if (!result)
					{
						return result;
					}

					frame.bEntered = true;
				}

				Frame child;

				if (frame.Kind == ESchemaValueKind::Array)
				{
					const Value::Array* const array = frame.Node->GetArray();
					ASSERT(array != nullptr, "An entered array frame must retain array storage.");

					if (frame.Index == array->Size())
					{
						--count;
						continue;
					}

					child.Node = &(*array)[frame.Index++];
					child.Kind = frame.ElementKind;
					child.ObjectType = frame.ElementType;
					child.bGeneric = frame.ElementKind == ESchemaValueKind::Array && frame.NestedArrays.empty();

					if (!frame.NestedArrays.empty())
					{
						child.ElementKind = frame.NestedArrays.front().ElementValueKind;
						child.ElementType = frame.NestedArrays.front().ElementType;
						child.NestedArrays = frame.NestedArrays.subspan(1);
					}
				}
				else
				{
					const Value::Object* const object = frame.Node->GetObject();
					ASSERT(object != nullptr, "An entered object frame must retain object storage.");
					rox::core::StringView name;

					if (frame.Schema != nullptr)
					{
						while (frame.Index != frame.Schema->Fields.size())
						{
							const SchemaFieldDescriptor& field = frame.Schema->Fields[frame.Index++];
							ASSERT(!object->UnknownFields.Contains(field.Name), "A schema-validated object cannot duplicate a known field by name.");
							const Value* const found = object->Fields.Find(field.ID);
							child.Node = found != nullptr ? found : field.DefaultValue;

							if (child.Node == nullptr)
							{
								ASSERT(hasFlag(field.Flags, ESchemaFieldFlags::Optional), "A schema-validated object cannot omit a required field without a default.");
								continue;
							}

							name = field.Name;
							child.Kind = field.ValueKind;
							child.ObjectType = field.ValueType;
							child.ElementKind = field.ElementValueKind;
							child.ElementType = field.ElementType;
							child.NestedArrays = field.NestedArrays;
							break;
						}
					}

					if (child.Node == nullptr && frame.Unknown != object->UnknownFields.end())
					{
						const Value::UnknownFieldMap::Entry& field = *frame.Unknown++;
						ASSERT(frame.Schema == nullptr || findFieldByName(*frame.Schema, field.GetKey().View()) == nullptr, "A schema-validated unknown field cannot duplicate a known field name.");
						name = field.GetKey().View();
						child.Node = &field.Value;
						child.bGeneric = true;
					}

					if (child.Node == nullptr)
					{
						result = addMinimumBytes(&context, 1);

						if (!result)
						{
							return result;
						}

						--count;
						continue;
					}

					if (frame.Emitted != 0)
					{
						result = addMinimumBytes(&context, 1);
					}

					if (result)
					{
						result = countEscapedString(name, &context);
					}

					if (result)
					{
						result = addMinimumBytes(&context, 1);
					}

					if (!result)
					{
						return result;
					}

					++frame.Emitted;
				}

				if (count == frames.size())
				{
					return failure<void>(EErrorCode::Unsupported, "ROX-CORE-JSON-WRITE-DEPTH-LIMIT",
					                     "The canonical JSON value exceeds the configured nesting depth.",
					                     "Reduce nesting or raise JsonWriteLimits::MaxDepth intentionally.");
				}
				frames[count++] = child;
			}

			return Result<void>::Success();
		}

		template <typename Integer>
		[[nodiscard]] Result<Value> readIntegerText(rox::core::StringView text)
		{
			const StringView digits = !text.empty() && text.front() == '-' ? text.substr(1) : text;
			bool canonical = !digits.empty() && (digits.size() == 1 || digits.front() != '0') && text != "-0";
			for (const char digit : digits)
			{
				canonical = canonical && digit >= '0' && digit <= '9';
			}
			Integer value{};
			const FromCharsResult parsed = canonical ? rox::core::FromChars(text.data(), text.data() + text.size(), value) : rox::core::FromCharsResult{text.data(), rox::core::EErrorCondition::invalid_argument};
			if (!canonical || parsed.ec != rox::core::EErrorCondition{} || parsed.ptr != text.data() + text.size())
				return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-INTEGER-TEXT", "An integer string must be canonical decimal text within the declared signed or unsigned range.", "Use decimal digits without whitespace, a plus sign, leading zeroes, fraction, or exponent.");
			return Result<Value>::Success(Value(value));
		}

		[[nodiscard]] Result<Value> readTypedScalar(const CanonicalJsonView& json, const ESchemaValueKind kind, ReadContext* const context)
		{
			switch (kind)
			{
			case ESchemaValueKind::Null:
				if (json.IsNull())
				{
					return Result<Value>::Success(Value());
				}
				break;
			case ESchemaValueKind::Boolean:
				if (json.IsBoolean())
				{
					return Result<Value>::Success(Value(json.GetBoolean()));
				}
				break;
			case ESchemaValueKind::SignedInteger:
				if (json.IsString())
				{
					return readIntegerText<int64>(json.GetString());
				}
				if (json.IsUnsignedInteger())
				{
					const uint64 value = json.GetUnsignedInteger();

					if (value <= static_cast<uint64>(rox::core::NumericLimits<int64>::max()))
					{
						return Result<Value>::Success(Value(static_cast<int64>(value)));
					}
				}
				else if (json.IsInteger())
				{
					return Result<Value>::Success(Value(json.GetSignedInteger()));
				}
				break;
			case ESchemaValueKind::UnsignedInteger:
				if (json.IsString())
				{
					return readIntegerText<uint64>(json.GetString());
				}
				if (json.IsUnsignedInteger())
				{
					return Result<Value>::Success(Value(json.GetUnsignedInteger()));
				}

				if (json.IsInteger())
				{
					const int64 value = json.GetSignedInteger();

					if (value >= 0)
					{
						return Result<Value>::Success(Value(static_cast<uint64>(value)));
					}
				}
				break;
			case ESchemaValueKind::FloatingPoint:
				if (json.IsNumber())
				{
					const float64 value = json.GetNumber();

					if (rox::core::IsFinite(value))
					{
						return Result<Value>::Success(Value(value == 0.0 ? 0.0 : value));
					}
				}
				break;
			case ESchemaValueKind::String:
				if (json.IsString())
				{
					return Value::CreateString(json.GetString(), context->Resource);
				}
				break;
			case ESchemaValueKind::StableId:
				if (json.IsString())
				{
					StableValueId id;
					const rox::core::StringView text = json.GetString();

					if (TryParseStrongId(text, &id))
					{
						return Result<Value>::Success(Value(id));
					}
				}
				break;
			case ESchemaValueKind::Bytes:
				if (json.IsArray())
				{
					Value::Bytes bytes(context->Resource);

					if (!bytes.Reserve(json.Size()))
					{
						return readAllocationFailure();
					}

					for (const CanonicalJsonView& element : json)
					{
						const Result<void> byteCountResult = consumeValue(context);

						if (!byteCountResult)
						{
							return failure<Value>(byteCountResult.GetError());
						}

						uint64 byteValue = 0;

						if (element.IsUnsignedInteger())
						{
							byteValue = element.GetUnsignedInteger();
						}
						else if (element.IsInteger() && element.GetSignedInteger() >= 0)
						{
							byteValue = static_cast<uint64>(element.GetSignedInteger());
						}
						else
						{
							break;
						}

						if (byteValue > 255)
						{
							break;
						}
						bytes.AddRequired(static_cast<Byte>(static_cast<uint8>(byteValue)));
					}

					if (bytes.Size() == json.Size())
					{
						return Value::CreateBytes(rox::core::Move(bytes));
					}
				}
				break;
			case ESchemaValueKind::Array:
			case ESchemaValueKind::Object:
				break;
			default:
				ASSERT(false, "Schema-bound JSON reading must handle every validated schema value kind.");
			}

			return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-VALUE-TYPE",
			                      "A JSON value does not match its schema value kind.",
			                      "Correct the authored value or select the matching schema.");
		}

		[[nodiscard]] Result<Value> readGenericScalar(const CanonicalJsonView& json, ReadContext* const context)
		{
			if (json.IsNull())
			{
				return Result<Value>::Success(Value());
			}

			if (json.IsBoolean())
			{
				return Result<Value>::Success(Value(json.GetBoolean()));
			}

			if (json.IsUnsignedInteger())
			{
				return Result<Value>::Success(Value(json.GetUnsignedInteger()));
			}

			if (json.IsInteger())
			{
				const int64 value = json.GetSignedInteger();

				if (value >= 0)
				{
					return Result<Value>::Success(Value(static_cast<uint64>(value)));
				}

				return Result<Value>::Success(Value(value));
			}

			if (json.IsFloatingPoint())
			{
				const float64 value = json.GetNumber();

				if (!rox::core::IsFinite(value))
				{
					return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-NON-FINITE",
					                      "Authored JSON cannot contain NaN or infinity.", "Provide a finite number.");
				}

				return Result<Value>::Success(Value(value == 0.0 ? 0.0 : value));
			}

			if (json.IsString())
			{
				return Value::CreateString(json.GetString(), context->Resource);
			}

			return failure<Value>(EErrorCode::Unsupported, "ROX-CORE-JSON-UNSUPPORTED-VALUE",
			                      "The JSON parser produced an unsupported value category.",
			                      "Use standard authored JSON values.");
		}

		[[nodiscard]] Result<void> readDefaults(Value::Object& object, const SchemaDescriptor& schema,
		                                        const uint32 depth, ReadContext* const context)
		{
			for (const SchemaFieldDescriptor& field : schema.Fields)
			{
				if (object.Fields.Contains(field.ID))
				{
					continue;
				}

				if (field.DefaultValue != nullptr)
				{
					uint64 remaining = context->Limits.MaxValues - context->ValueCount;
					const EJsonDefaultLimit limit =
					    ConsumeJsonDefaultBudget(*field.DefaultValue, depth, context->Limits.MaxDepth, remaining);

					if (limit != EJsonDefaultLimit::None)
					{
						return failure<void>(EErrorCode::InvalidArgument,
						                     limit == EJsonDefaultLimit::Depth ? "ROX-CORE-JSON-DEPTH-LIMIT"
						                                                       : "ROX-CORE-JSON-VALUE-LIMIT",
						                     "A schema default exceeds the configured JSON read budget.",
						                     "Reduce the default or increase read limits.");
					}

					context->ValueCount = context->Limits.MaxValues - remaining;
					Result<Value> copied = CloneValue(*field.DefaultValue, context->Resource);

					if (!copied)
					{
						return failure<void>(copied.GetError());
					}
					object.Fields.InsertRequired(field.ID, rox::core::Move(copied).TakeValue());
				}
				else if (!hasFlag(field.Flags, ESchemaFieldFlags::Optional))
				{
					return failure<void>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-MISSING-FIELD",
					                     "A required schema field is missing from the JSON object.",
					                     "Add the required field or define a schema default.");
				}
			}

			return Result<void>::Success();
		}

		[[nodiscard]] Result<Value> readSchemaObject(const CanonicalJsonView& json, const SchemaDescriptor& schema,
		                                             ReadContext* const context)
		{
			ASSERT(context != nullptr && json.IsObject(), "Schema reading requires an object and a valid context.");

			struct Frame final
			{
				explicit Frame(const CanonicalJsonView& json) noexcept
				    : Json(json)
				{
				}

				CanonicalJsonView Json;
				rox::core::Optional<CanonicalJsonView::Iterator> Next;

				const SchemaDescriptor* Schema = nullptr;
				TypeId ObjectType;
				TypeId ElementType;
				rox::core::Span<const SchemaArrayDescriptor> NestedArrays;
				ESchemaValueKind Kind = ESchemaValueKind::Null;
				ESchemaValueKind ElementKind = ESchemaValueKind::Null;

				rox::core::Optional<Value::Array> Array;
				rox::core::Optional<Value::Object> Object;

				uint32 Depth = 1;
				bool bGeneric = false;
				bool bEntered = false;
			};

			// The read limit counts containers; a final scalar may add one Value level.
			rox::core::StaticArray<rox::core::Optional<Frame>, ValueCloneLimits::HARD_MAXIMUM_DEPTH + 1> frames{};
			Frame& root = frames[0].emplace(json);
			root.Schema = &schema;
			root.Kind = ESchemaValueKind::Object;
			rox::core::usize count = 1;

			while (count != 0)
			{
				Frame& frame = *frames[count - 1];
				rox::core::Optional<Value> completed;

				if (!frame.bEntered)
				{
					if (count != 1)
					{
						const Result<void> consumed = consumeValue(context);

						if (!consumed)
						{
							return failure<Value>(consumed.GetError());
						}
					}

					if (frame.Kind == ESchemaValueKind::Array && frame.Json.IsArray())
					{
						frame.Array.emplace(context->Resource);

						if (!frame.Array->Reserve(frame.Json.Size()))
						{
							return readAllocationFailure();
						}
						frame.Next.emplace(frame.Json.begin());
					}
					else if (frame.Kind == ESchemaValueKind::Object && frame.Json.IsObject())
					{
						if (frame.ObjectType.IsValid())
						{
							frame.Schema = context->Registry.Find(frame.ObjectType);

							if (frame.Schema == nullptr)
							{
								return failure<Value>(
								    EErrorCode::NotFound, "ROX-CORE-JSON-NESTED-SCHEMA-NOT-FOUND",
								    "A nested object schema is not present in the registry.",
								    "Register the complete schema dependency set before reading the document.");
							}
						}
						frame.Object.emplace(context->Resource);

						if (!frame.Object->UnknownFields.Reserve(frame.Json.Size()) ||
						    (frame.Schema != nullptr && !frame.Object->Fields.Reserve(frame.Schema->Fields.size())))
						{
							return readAllocationFailure();
						}
						frame.Next.emplace(frame.Json.begin());
					}
					else
					{
						Result<Value> scalar = frame.bGeneric ? readGenericScalar(frame.Json, context)
						                             : readTypedScalar(frame.Json, frame.Kind, context);

						if (!scalar)
						{
							return failure<Value>(scalar.GetError());
						}
						completed.emplace(rox::core::Move(scalar).TakeValue());
					}

					frame.bEntered = true;
				}

				if (!completed && frame.Next && *frame.Next != frame.Json.end())
				{
					const CanonicalJsonView childJson = **frame.Next;
					++*frame.Next;
					ASSERT(count < frames.size(), "Lexically bounded JSON cannot exceed the read frame capacity.");
					Frame& child = frames[count++].emplace(childJson);
					child.Depth = frame.Depth + (childJson.IsArray() || childJson.IsObject());

					if (frame.Array)
					{
						child.bGeneric = frame.bGeneric ||
						                 (frame.ElementKind == ESchemaValueKind::Array && frame.NestedArrays.empty());
						child.Kind = child.bGeneric ? childJson.GetKind() : frame.ElementKind;
						child.ObjectType = child.bGeneric ? TypeId::Invalid() : frame.ElementType;

						if (!frame.NestedArrays.empty())
						{
							child.ElementKind = frame.NestedArrays.front().ElementValueKind;
							child.ElementType = frame.NestedArrays.front().ElementType;
							child.NestedArrays = frame.NestedArrays.subspan(1);
						}
					}
					else
					{
						ASSERT(frame.Object.has_value(), "A non-array container frame must own an object builder.");
						const SchemaFieldDescriptor* const field =
						    frame.Schema != nullptr ? findFieldByName(*frame.Schema, childJson.GetKey()) : nullptr;
						child.bGeneric = field == nullptr;
						child.Kind = field != nullptr ? field->ValueKind : childJson.GetKind();

						if (field != nullptr)
						{
							child.ObjectType = field->ValueType;
							child.ElementKind = field->ElementValueKind;
							child.ElementType = field->ElementType;
							child.NestedArrays = field->NestedArrays;
						}
					}
					continue;
				}

				if (!completed)
				{
					if (frame.Object && frame.Schema != nullptr)
					{
						const Result<void> defaults = readDefaults(*frame.Object, *frame.Schema, frame.Depth, context);

						if (!defaults)
						{
							return failure<Value>(defaults.GetError());
						}
					}

					ASSERT(frame.Array || frame.Object, "An unfinished frame must retain a container builder.");
					Result<Value> finished = frame.Array ? Value::CreateArray(rox::core::Move(*frame.Array))
					                            : Value::CreateObject(rox::core::Move(*frame.Object), context->Resource);

					if (!finished)
					{
						return failure<Value>(finished.GetError());
					}
					completed.emplace(rox::core::Move(finished).TakeValue());
				}

				const rox::core::StringView childName = frame.Json.GetKey();
				frames[--count].reset();

				if (count == 0)
				{
					return Result<Value>::Success(rox::core::Move(*completed));
				}
				Frame& parent = *frames[count - 1];

				if (parent.Array)
				{
					if (!parent.bGeneric && completed->GetKind() != parent.ElementKind)
					{
						return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-ELEMENT-TYPE",
						                      "An array element does not match its schema value kind.",
						                      "Correct the array element type in the authored document.");
					}

					parent.Array->AddRequired(rox::core::Move(*completed));
				}
				else
				{
					ASSERT(parent.Object.has_value(), "A parent frame must retain its container builder.");
					const SchemaFieldDescriptor* const field =
					    parent.Schema != nullptr ? findFieldByName(*parent.Schema, childName) : nullptr;

					if (field != nullptr)
					{
						parent.Object->Fields.InsertRequired(field->ID, rox::core::Move(*completed));
					}
					else
					{
						Result<ValueFieldName> copiedName = ValueFieldName::Copy(childName, context->Resource);

						if (!copiedName)
						{
							return readAllocationFailure();
						}

						parent.Object->UnknownFields.InsertRequired(rox::core::Move(copiedName).TakeValue(), rox::core::Move(*completed));
					}
				}
			}

			ASSERT(false, "Root JSON conversion must return a completed value or a typed error.");
		}

		[[nodiscard]] Result<Value> readDocument(rox::core::Span<const Byte> bytes, const SchemaDescriptor& schema,
		                                         const SchemaRegistry& registry, const JsonReadLimits& limits,
		                                         MemoryResource& resource)
		{
			if (limits.MaxDepth > ValueCloneLimits::HARD_MAXIMUM_DEPTH)
			{
				return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-READ-LIMITS",
				                      "JSON read nesting exceeds the supported maximum.",
				                      "Use a maximum depth of 256 or less.");
			}

			if (static_cast<uint64>(bytes.size()) > limits.MaxBytes)
			{
				return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-BYTE-LIMIT",
				                      "The JSON document exceeds the configured byte limit.",
				                      "Reduce the document size or raise JsonReadLimits::MaxBytes intentionally.");
			}

			if (bytes.empty())
			{
				return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-EMPTY",
				                      "An empty byte sequence is not a JSON document.",
				                      "Provide a complete JSON object.");
			}

			const Result<void> depthResult = checkLexicalDepth(bytes, limits);

			if (!depthResult)
			{
				return failure<Value>(depthResult.GetError());
			}

			Result<CanonicalJsonDocument> parsed = ParseCanonicalJsonDocument(bytes, SystemMemoryResource::Get());

			if (!parsed)
			{
				return failure<Value>(parsed.GetError());
			}

			const CanonicalJsonView json(parsed.Value(), 0);

			if (!json.IsObject())
			{
				return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-JSON-EXPECTED-OBJECT",
				                      "A schema-bound JSON root must be an object.",
				                      "Wrap schema fields in a JSON object.");
			}
			ReadContext context{limits, registry, resource};
			const Result<void> rootCountResult = consumeValue(&context);

			if (!rootCountResult)
			{
				return failure<Value>(rootCountResult.GetError());
			}

			Result<Value> valueResult = readSchemaObject(json, schema, &context);

			if (!valueResult)
			{
				return valueResult;
			}

			const Result<void> validationResult = registry.Validate(schema.Type, valueResult.Value());

			if (!validationResult)
			{
				return failure<Value>(validationResult.GetError());
			}

			return valueResult;
		}

		[[nodiscard]] Result<Array<Byte>> writeDocument(const Value& value, const SchemaDescriptor& schema,
		                                                const SchemaRegistry& registry, MemoryResource& resource,
		                                                const JsonWriteLimits& limits)
		{
			const Result<void> limitResult = checkWriteLimits(value, limits);

			if (!limitResult)
			{
				return failure<Array<Byte>>(limitResult.GetError());
			}

			const Result<void> validationResult = registry.Validate(schema.Type, value);

			if (!validationResult)
			{
				return failure<Array<Byte>>(validationResult.GetError());
			}

			const Result<void> schemaLimitResult = checkSchemaWriteLimits(value, schema, registry, limits);

			if (!schemaLimitResult)
			{
				return failure<Array<Byte>>(schemaLimitResult.GetError());
			}

			ASSERT(value.GetObject() != nullptr,
			       "Successful root schema validation must prove object storage is available.");

			return WriteCanonicalJsonBytes(value, schema, registry, resource, limits);
		}
	} // namespace

	Result<Value> ReadCanonicalJson(rox::core::Span<const Byte> bytes, const SchemaDescriptor& schema,
	                                const JsonReadLimits& limits)
	{
		return ReadCanonicalJson(bytes, schema, SystemMemoryResource::Get(), limits);
	}

	Result<Value> ReadCanonicalJson(rox::core::Span<const Byte> bytes, const SchemaDescriptor& schema,
	                                MemoryResource& resource, const JsonReadLimits& limits)
	{
		try
		{
			// Temporary metadata uses bootstrap storage; decoded values use the caller resource.
			SchemaRegistry registry(SystemMemoryResource::Get());
			const Result<void> registrationResult = registry.Register(schema);

			if (!registrationResult)
			{
				if (registrationResult.GetError().DiagnosticID == "ROX-CORE-SCHEMA-REGISTRY-ALLOCATION")
				{
					return readAllocationFailure();
				}

				return failure<Value>(registrationResult.GetError());
			}

			registry.Freeze();

			const SchemaDescriptor* const storedSchema = registry.Find(schema.Type);
			ASSERT(storedSchema != nullptr,
			       "Successful schema registration must make its type available for canonical JSON reading.");
			return normalizeReadAllocation(readDocument(bytes, *storedSchema, registry, limits, resource));
		}
		catch (const rox::core::AllocationException&)
		{
			return failure<Value>(EErrorCode::ExternalFailure, "ROX-CORE-JSON-READ-ALLOCATION",
			                      "Canonical JSON reading could not allocate its temporary state.",
			                      "Free memory, lower the read limits, and retry the operation.");
		}
	}

	Result<Value> ReadCanonicalJson(rox::core::Span<const Byte> bytes, TypeId rootType, const SchemaRegistry& registry,
	                                const JsonReadLimits& limits)
	{
		return ReadCanonicalJson(bytes, rootType, registry, SystemMemoryResource::Get(), limits);
	}

	Result<Value> ReadCanonicalJson(rox::core::Span<const Byte> bytes, TypeId rootType, const SchemaRegistry& registry,
	                                MemoryResource& resource, const JsonReadLimits& limits)
	{
		try
		{
			ASSERT(registry.IsFrozen(), "Registry-aware canonical JSON reads require a frozen schema registry.");

			const SchemaDescriptor* const schema = registry.Find(rootType);

			if (schema == nullptr)
			{
				return failure<Value>(EErrorCode::NotFound, "ROX-CORE-JSON-ROOT-SCHEMA-NOT-FOUND",
				                      "The canonical JSON root type is not registered.",
				                      "Register the root schema before reading.");
			}

			return normalizeReadAllocation(readDocument(bytes, *schema, registry, limits, resource));
		}
		catch (const rox::core::AllocationException&)
		{
			return failure<Value>(EErrorCode::ExternalFailure, "ROX-CORE-JSON-READ-ALLOCATION",
			                      "Canonical JSON reading could not allocate its temporary state.",
			                      "Free memory, lower the read limits, and retry the operation.");
		}
	}

	Result<Array<Byte>> WriteCanonicalJson(const Value& value, const SchemaDescriptor& schema, MemoryResource& resource,
	                                       const JsonWriteLimits& limits)
	{
		const Result<void> limitResult = validateWriteLimits(limits);

		if (!limitResult)
		{
			return failure<Array<Byte>>(limitResult.GetError());
		}
		try
		{
			// The call-scoped temporary registry names the bootstrap resource rather than the caller output
			// resource: the caller resource pays for exactly the returned Array<Byte> and nothing else, which
			// SchemaContracts.cpp case 87 pins as a single allocation attempt.
			SchemaRegistry registry(SystemMemoryResource::Get());
			const Result<void> registrationResult = registry.Register(schema);

			if (!registrationResult)
			{
				return failure<Array<Byte>>(registrationResult.GetError());
			}

			registry.Freeze();

			const SchemaDescriptor* const storedSchema = registry.Find(schema.Type);
			ASSERT(storedSchema != nullptr,
			       "Successful schema registration must make its type available for canonical JSON writing.");
			return writeDocument(value, *storedSchema, registry, resource, limits);
		}
		catch (const rox::core::AllocationException&)
		{
			return failure<Array<Byte>>(EErrorCode::ExternalFailure, "ROX-CORE-JSON-WRITE-ALLOCATION",
			                            "Canonical JSON serialization could not allocate its bounded temporary state.",
			                            "Free memory, lower the write limits, and retry the operation.");
		}
	}

	Result<Array<Byte>> WriteCanonicalJson(const Value& value, TypeId rootType, const SchemaRegistry& registry,
	                                       MemoryResource& resource, const JsonWriteLimits& limits)
	{
		ASSERT(registry.IsFrozen(), "Registry-aware canonical JSON writes require a frozen schema registry.");

		const SchemaDescriptor* const schema = registry.Find(rootType);

		if (schema == nullptr)
		{
			return failure<Array<Byte>>(EErrorCode::NotFound, "ROX-CORE-JSON-ROOT-SCHEMA-NOT-FOUND",
			                            "The canonical JSON root type is not registered.",
			                            "Register the root schema before writing.");
		}

		return writeDocument(value, *schema, registry, resource, limits);
	}
} // namespace rox::core
