#include <SchemaCatalog.h>
#include <Vocabulary.hpp>
#include <SystemError.hpp>
#include <StringView.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Utility.hpp>
#include <Assertion.h>
#include <ByteReader.h>
#include <ByteWriter.h>
#include <SystemMemoryResource.h>
#include <Utf8.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>

namespace rox::core
{
	namespace
	{
		constexpr uint64 HEADER_SIZE = 48;
		constexpr uint32 ENDIAN_MARKER = 0x01020304;
		constexpr uint16 VALUE_PRESENT = 1u << 0;
		constexpr uint16 MINIMUM_PRESENT = 1u << 1;
		constexpr uint16 MAXIMUM_PRESENT = 1u << 2;
		constexpr uint16 MINIMUM_LENGTH_PRESENT = 1u << 3;
		constexpr uint16 MAXIMUM_LENGTH_PRESENT = 1u << 4;
		constexpr uint16 ENUM_ENTRIES_PRESENT = 1u << 5;
		constexpr uint16 NESTED_ARRAYS_PRESENT = 1u << 6;
		constexpr uint16 ELEMENT_MAXIMUM_LENGTH_PRESENT = 1u << 7;
		constexpr uint16 KNOWN_PRESENCE_BITS = VALUE_PRESENT | MINIMUM_PRESENT | MAXIMUM_PRESENT |
		                                       MINIMUM_LENGTH_PRESENT | MAXIMUM_LENGTH_PRESENT | ENUM_ENTRIES_PRESENT |
		                                       NESTED_ARRAYS_PRESENT | ELEMENT_MAXIMUM_LENGTH_PRESENT;
		constexpr char MAGIC[] = {'C', 'S', 'C', 'A'};

		template <typename TValue>
		Result<TValue> failure(EErrorCode code, const StringView diagnosticID, const StringView message, const StringView recoveryHint)
		{
			return Result<TValue>::Failure(code, diagnosticID, message, recoveryHint);
		}

		Result<void> invalidLimits()
		{
			return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-LIMITS", "The schema catalog limits are outside the supported profile.", "Use non-zero limits no larger than the published hard maxima.");
		}

		template <typename TValue>
		Result<TValue> allocationFailure()
		{
			return failure<TValue>(EErrorCode::ExternalFailure, "ROX-CORE-SCHEMA-CATALOG-ALLOCATION", "Schema catalog processing could not allocate its bounded storage.", "Free memory, lower the catalog limits, and retry the operation.");
		}

		bool addBytes(const uint64 amount, const uint64 maximum, uint64* const total) noexcept
		{
			ASSERT(total != nullptr, "Schema catalog byte accounting requires a valid total.");

			if (*total > maximum || amount > maximum - *total)
			{
				return false;
			}
			*total += amount;
			return true;
		}

		bool unsignedStringLess(const rox::core::StringView left, const rox::core::StringView right) noexcept
		{
			return rox::core::LexicographicalCompare(left.begin(), left.end(), right.begin(), right.end(),
				[](const char leftCharacter, const char rightCharacter)
				{
					return static_cast<unsigned char>(leftCharacter) < static_cast<unsigned char>(rightCharacter);
				});
		}

		class CatalogByteWriter final
		{
		public:
			explicit CatalogByteWriter(Span<Byte> bytes) noexcept
			    : m_Writer(bytes)
			{
			}

			void WriteByte(const uint8 value) noexcept
			{
				m_Writer.WriteU8(value);
			}

			void WriteUint16(const uint16 value) noexcept
			{
				m_Writer.WriteU16LE(value);
			}

			void WriteUint32(const uint32 value) noexcept
			{
				m_Writer.WriteU32LE(value);
			}

			void WriteUint64(const uint64 value) noexcept
			{
				m_Writer.WriteU64LE(value);
			}

			template <typename TTag>
			void WriteID(const StrongId<TTag> id) noexcept
			{
				WriteUint64(id.High);
				WriteUint64(id.Low);
			}

			void WriteString(const rox::core::StringView text) noexcept
			{
				WriteUint64(static_cast<uint64>(text.size()));
				m_Writer.WriteBytes({reinterpret_cast<const Byte*>(text.data()), text.size()});
			}

			[[nodiscard]] uint64 GetOffset() const noexcept
			{
				return static_cast<uint64>(m_Writer.GetOffset());
			}

		private:
			rox::core::ByteWriter m_Writer;
		};

		class CatalogByteReader final
		{
		public:
			explicit CatalogByteReader(const Span<const Byte> bytes) noexcept
			    : m_Reader(bytes)
			{
			}

			[[nodiscard]] bool ReadByte(uint8* const outValue) noexcept
			{
				return m_Reader.TryReadU8(outValue);
			}

			[[nodiscard]] bool ReadUint16(uint16* const outValue) noexcept
			{
				return m_Reader.TryReadU16LE(outValue);
			}

			[[nodiscard]] bool ReadUint32(uint32* const outValue) noexcept
			{
				return m_Reader.TryReadU32LE(outValue);
			}

			[[nodiscard]] bool ReadUint64(uint64* const outValue) noexcept
			{
				return m_Reader.TryReadU64LE(outValue);
			}

			template <typename TTag>
			[[nodiscard]] bool ReadID(StrongId<TTag>* const outID) noexcept
			{
				ASSERT(outID != nullptr, "Schema catalog ID decoding requires a valid output value.");
				return ReadUint64(&outID->High) && ReadUint64(&outID->Low);
			}

			[[nodiscard]] bool ReadString(rox::core::StringView* const outText, const uint64 maximumBytes) noexcept
			{
				ASSERT(outText != nullptr, "Schema catalog string decoding requires a valid output view.");
				uint64 size = 0;

				if (!ReadUint64(&size) || size > maximumBytes || size > GetRemaining())
				{
					return false;
				}

				const Span<const Byte> text = m_Reader.ReadBytes(static_cast<rox::core::usize>(size));
				*outText = rox::core::StringView(reinterpret_cast<const char*>(text.Data()), text.Size());
				return true;
			}

			[[nodiscard]] uint64 GetOffset() const noexcept
			{
				return static_cast<uint64>(m_Reader.GetOffset());
			}

			[[nodiscard]] uint64 GetRemaining() const noexcept
			{
				return static_cast<uint64>(m_Reader.GetRemaining());
			}

			[[nodiscard]] bool IsAtEnd() const noexcept
			{
				return m_Reader.GetRemaining() == 0;
			}

		private:
			rox::core::ByteReader m_Reader;
		};

		struct ValueContext final
		{
			const SchemaCatalogLimits& Limits;
			uint64 Values = 0;
		};

		Result<void> consumeValue(ValueContext* const context, const uint32 depth)
		{
			ASSERT(context != nullptr, "Schema catalog value accounting requires a valid context.");

			if (depth == 0 || depth > context->Limits.MaximumDepth)
			{
				return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-VALUE-DEPTH", "A schema catalog value exceeds the configured recursion depth.", "Flatten the schema default or raise MaximumDepth intentionally.");
			}

			if (context->Values >= context->Limits.MaximumValues)
			{
				return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-VALUE-COUNT", "A schema catalog exceeds the configured value count.", "Reduce schema defaults or raise MaximumValues intentionally.");
			}

			++context->Values;
			return Result<void>::Success();
		}

		Result<void> measureString(const rox::core::StringView text, const SchemaCatalogLimits& limits, uint64* const total)
		{
			if (text.size() > limits.MaximumStringBytes || !addBytes(8, limits.MaximumBytes, total) || !addBytes(static_cast<uint64>(text.size()), limits.MaximumBytes, total))
			{
				return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-STRING-LIMIT", "A schema catalog string exceeds its configured byte limit.", "Shorten schema metadata or raise the explicit string and catalog limits.");
			}

			return Result<void>::Success();
		}

		Result<void> measureValue(const Value& value, ValueContext* const context, const uint32 depth,
		                          uint64* const total)
		{
			const Result<void> consumed = consumeValue(context, depth);

			if (!consumed)
			{
				return consumed;
			}

			if (!addBytes(1, context->Limits.MaximumBytes, total))
			{
				return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "A schema catalog exceeds MaximumBytes.", "Reduce schema data or raise MaximumBytes intentionally.");
			}

			switch (value.GetKind())
			{
			case ESchemaValueKind::Null:
				return Result<void>::Success();
			case ESchemaValueKind::Boolean:
				if (!addBytes(1, context->Limits.MaximumBytes, total))
				{
					return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "A schema catalog exceeds MaximumBytes.", "Reduce schema data or raise MaximumBytes intentionally.");
				}

				return Result<void>::Success();
			case ESchemaValueKind::SignedInteger:
			case ESchemaValueKind::UnsignedInteger:
			case ESchemaValueKind::FloatingPoint:
				if (value.GetKind() == ESchemaValueKind::FloatingPoint && !rox::core::IsFinite(*value.GetFloatingPoint()))
				{
					return failure<void>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-NON-FINITE", "A schema catalog contains a non-finite floating-point value.", "Use only finite schema defaults and constraints.");
				}

				if (!addBytes(8, context->Limits.MaximumBytes, total))
				{
					return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "A schema catalog exceeds MaximumBytes.", "Reduce schema data or raise MaximumBytes intentionally.");
				}

				return Result<void>::Success();
			case ESchemaValueKind::String:
				return measureString(*value.GetString(), context->Limits, total);
			case ESchemaValueKind::StableId:
				if (!addBytes(16, context->Limits.MaximumBytes, total))
				{
					return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "A schema catalog exceeds MaximumBytes.", "Reduce schema data or raise MaximumBytes intentionally.");
				}

				return Result<void>::Success();
			case ESchemaValueKind::Bytes:
				if (!addBytes(8, context->Limits.MaximumBytes, total) || !addBytes(static_cast<uint64>(value.GetBytes()->Size()), context->Limits.MaximumBytes, total))
				{
					return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "A schema catalog exceeds MaximumBytes.", "Reduce schema data or raise MaximumBytes intentionally.");
				}

				return Result<void>::Success();
			case ESchemaValueKind::Array:
				if (!addBytes(8, context->Limits.MaximumBytes, total))
				{
					return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "A schema catalog exceeds MaximumBytes.", "Reduce schema data or raise MaximumBytes intentionally.");
				}

				for (const Value& element : *value.GetArray())
				{
					const Result<void> measured = measureValue(element, context, depth + 1, total);
					if (!measured)
					{
						return measured;
					}
				}

				return Result<void>::Success();
			case ESchemaValueKind::Object:
			{
				const Value::Object& object = *value.GetObject();

				if (!addBytes(16, context->Limits.MaximumBytes, total))
				{
					return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "A schema catalog exceeds MaximumBytes.", "Reduce schema data or raise MaximumBytes intentionally.");
				}

				for (const Value::FieldMap::Entry& entry : object.Fields)
				{
					const FieldId& field = entry.GetKey();
					const Value& fieldValue = entry.Value;
					(void)field;

					if (!addBytes(16, context->Limits.MaximumBytes, total))
					{
						return failure<void>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "A schema catalog exceeds MaximumBytes.", "Reduce schema data or raise MaximumBytes intentionally.");
					}

					const Result<void> measured = measureValue(fieldValue, context, depth + 1, total);

					if (!measured)
					{
						return measured;
					}
				}

				for (const Value::UnknownFieldMap::Entry& entry : object.UnknownFields)
				{
					const rox::core::StringView& name = entry.GetKey().View();
					const Value& fieldValue = entry.Value;
					const Result<void> nameResult = measureString(name, context->Limits, total);

					if (!nameResult)
					{
						return nameResult;
					}

					const Result<void> measured = measureValue(fieldValue, context, depth + 1, total);

					if (!measured)
					{
						return measured;
					}
				}

				return Result<void>::Success();
			}
			default:
				ASSERT(false, "Schema catalog measurement requires a documented schema value kind.");
			}
		}

		struct RankedSchema
		{
			const SchemaDescriptor* Schema;
			rox::core::usize Pass = 0;
		};

		RankedSchema* findRankedSchema(Array<RankedSchema>& schemas, const TypeId type) noexcept
		{
			auto* found = rox::core::LowerBound(schemas.begin(), schemas.end(), type,
				[](const RankedSchema& candidate, TypeId key) noexcept
				{
					return candidate.Schema->Type < key;
				});
			ASSERT(found != schemas.end() && found->Schema->Type == type, "A frozen registry must contain every dependency.");

			return found;
		}

		Result<Array<const SchemaDescriptor*>> buildSchemaOrder(const SchemaRegistry& registry, MemoryResource& scratchResource, const SchemaCatalogLimits& limits)
		{
			const rox::core::usize count = registry.GetSchemaCount();

			if (count > limits.MaximumSchemas)
			{
				return failure<Array<const SchemaDescriptor*>>(
				    EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-SCHEMA-LIMIT",
				    "A schema catalog exceeds MaximumSchemas.",
				    "Reduce the published registry or raise MaximumSchemas intentionally.");
			}

			Array<RankedSchema> sorted(scratchResource);
			Array<const SchemaDescriptor*> ordered(scratchResource);

			if (!sorted.Reserve(count) || !ordered.Reserve(count))
			{
				return allocationFailure<Array<const SchemaDescriptor*>>();
			}

			for (rox::core::usize index = 0; index < count; ++index)
			{
				sorted.AddRequired({&registry.GetSchemaByIndex(index), 0});
			}

			rox::core::Sort(sorted.begin(), sorted.end(),
				[](const RankedSchema& left, const RankedSchema& right)
				{
					return left.Schema->Type < right.Schema->Type;
				});
			// Registration is topological. A dependency later in TypeId order requires the next scan pass.
			// Ranking by that pass preserves the canonical repeated-scan order without repeated scans.
			for (rox::core::usize index = 0; index < count; ++index)
			{
				const SchemaDescriptor& schema = registry.GetSchemaByIndex(index);
				RankedSchema& ranked = *findRankedSchema(sorted, schema.Type);

				for (const SchemaFieldDescriptor& field : schema.Fields)
				{
					const TypeId dependencies[] = {
					    field.ValueKind == ESchemaValueKind::Object ? field.ValueType : TypeId::Invalid(),
					    field.ValueKind == ESchemaValueKind::Array && field.ElementValueKind == ESchemaValueKind::Object
					        ? field.ElementType
					        : TypeId::Invalid(),
					    field.NestedArrays.empty() ? TypeId::Invalid() : field.NestedArrays.back().ElementType};
					for (const TypeId dependency : dependencies)
					{
						if (!dependency.IsValid())
						{
							continue;
						}

						const RankedSchema& predecessor = *findRankedSchema(sorted, dependency);
						ranked.Pass = rox::core::Max(ranked.Pass, predecessor.Pass + (schema.Type < dependency ? 1U : 0U));
					}
				}
			}

			rox::core::Sort(sorted.begin(), sorted.end(),
				[](const RankedSchema& left, const RankedSchema& right)
				{
					return left.Pass != right.Pass ? left.Pass < right.Pass : left.Schema->Type < right.Schema->Type;
				});
			for (const RankedSchema& entry : sorted)
			{
				ordered.AddRequired(entry.Schema);
			}

			return Result<Array<const SchemaDescriptor*>>::Success(rox::core::Move(ordered));
		}

		Result<Array<const SchemaFieldDescriptor*>> buildFieldOrder(const SchemaDescriptor& schema, MemoryResource& scratchResource)
		{
			Array<const SchemaFieldDescriptor*> fields(scratchResource);

			if (!fields.Reserve(schema.Fields.size()))
			{
				return allocationFailure<Array<const SchemaFieldDescriptor*>>();
			}

			for (const SchemaFieldDescriptor& field : schema.Fields)
			{
				fields.AddRequired(&field);
			}

			rox::core::Sort(fields.begin(), fields.end(),
				[](const SchemaFieldDescriptor* const left, const SchemaFieldDescriptor* const right)
				{
					return left->ID < right->ID;
				});
			return Result<Array<const SchemaFieldDescriptor*>>::Success(rox::core::Move(fields));
		}

		Result<uint64> measureCatalog(const Array<const SchemaDescriptor*>& schemas, MemoryResource& scratchResource, const SchemaCatalogLimits& limits, uint64* const outFieldCount)
		{
			ASSERT(outFieldCount != nullptr, "Schema catalog measurement requires a valid field-count output.");
			uint64 total = HEADER_SIZE;
			uint64 fieldCount = 0;
			ValueContext valueContext{limits};
			for (const SchemaDescriptor* const schema : schemas)
			{
				ASSERT(schema != nullptr, "Schema catalog ordering must contain valid descriptors.");

				if (schema->Fields.size() > limits.MaximumFields - fieldCount || !addBytes(48, limits.MaximumBytes, &total))
				{
					return failure<uint64>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-FIELD-LIMIT", "A schema catalog exceeds its field or byte limit.", "Reduce the published registry or raise the explicit limits.");
				}

				fieldCount += static_cast<uint64>(schema->Fields.size());
				const Result<void> schemaName = measureString(schema->Name, limits, &total);

				if (!schemaName)
				{
					return Result<uint64>::Failure(schemaName.GetError());
				}

				Result<Array<const SchemaFieldDescriptor*>> fieldsResult = buildFieldOrder(*schema, scratchResource);

				if (!fieldsResult)
				{
					return Result<uint64>::Failure(fieldsResult.GetError());
				}

				for (const SchemaFieldDescriptor* const field : fieldsResult.Value())
				{
					if (!addBytes(56, limits.MaximumBytes, &total))
					{
						return failure<uint64>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "A schema catalog exceeds MaximumBytes.", "Reduce schema data or raise MaximumBytes intentionally.");
					}

					const rox::core::StringView strings[] = {field->Name, field->Display.DisplayName, field->Display.Description, field->Display.Category};
					for (const rox::core::StringView text : strings)
					{
						const Result<void> measured = measureString(text, limits, &total);
						if (!measured)
						{
							return Result<uint64>::Failure(measured.GetError());
						}
					}

					if (!field->NestedArrays.empty())
					{
						if (!addBytes(8 + 40 * field->NestedArrays.size(), limits.MaximumBytes, &total))
						{
							return failure<uint64>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "Nested array metadata exceeds the catalog byte limit.", "Reduce metadata or increase the limit.");
						}

						for (rox::core::usize level = 0; level < field->NestedArrays.size(); ++level)
						{
							const Result<void> consumed = consumeValue(&valueContext, 1);

							if (!consumed)
							{
								return Result<uint64>::Failure(consumed.GetError());
							}
						}
					}

					if (!field->EnumEntries.empty())
					{
						if (!addBytes(8 + 8 * field->EnumEntries.size(), limits.MaximumBytes, &total))
						{
							return failure<uint64>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "Enum metadata exceeds the catalog byte limit.", "Reduce metadata or increase the limit.");
						}

						for (const SchemaEnumEntry& entry : field->EnumEntries)
						{
							const Result<void> consumed = consumeValue(&valueContext, 1);
							if (!consumed)
							{
								return Result<uint64>::Failure(consumed.GetError());
							}

							const Result<void> measured = measureString(entry.Name, limits, &total);
							if (!measured)
							{
								return Result<uint64>::Failure(measured.GetError());
							}
						}
					}

					if ((field->ElementMaximumLength.has_value() && !addBytes(8, limits.MaximumBytes, &total)) ||
					    (field->Constraints.MinimumLength.has_value() && !addBytes(8, limits.MaximumBytes, &total)) ||
					    (field->Constraints.MaximumLength.has_value() && !addBytes(8, limits.MaximumBytes, &total)))
					{
						return failure<uint64>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "A schema catalog exceeds MaximumBytes.", "Reduce schema data or raise MaximumBytes intentionally.");
					}

					const Value* const values[] = {field->DefaultValue, field->Constraints.Minimum, field->Constraints.Maximum};
					for (const Value* const value : values)
					{
						if (value != nullptr)
						{
							const Result<void> measured = measureValue(*value, &valueContext, 1, &total);

							if (!measured)
							{
								return Result<uint64>::Failure(measured.GetError());
							}
						}
					}
				}
			}
			*outFieldCount = fieldCount;
			return Result<uint64>::Success(total);
		}

		Result<void> writeValue(const Value& value, CatalogByteWriter* const writer, MemoryResource& scratchResource)
		{
			ASSERT(writer != nullptr, "Schema catalog value encoding requires a valid writer.");
			writer->WriteByte(static_cast<uint8>(value.GetKind()));

			switch (value.GetKind())
			{
			case ESchemaValueKind::Null:
				break;
			case ESchemaValueKind::Boolean:
				writer->WriteByte(*value.GetBoolean() ? 1 : 0);
				break;
			case ESchemaValueKind::SignedInteger:
				writer->WriteUint64(rox::core::BitCast<uint64>(*value.GetSignedInteger()));
				break;
			case ESchemaValueKind::UnsignedInteger:
				writer->WriteUint64(*value.GetUnsignedInteger());
				break;
			case ESchemaValueKind::FloatingPoint:
			{
				const float64 source = *value.GetFloatingPoint();
				const float64 canonical = source == 0.0 ? 0.0 : source;
				writer->WriteUint64(rox::core::BitCast<uint64>(canonical));
				break;
			}
			case ESchemaValueKind::String:
				writer->WriteString(*value.GetString());
				break;
			case ESchemaValueKind::StableId:
				writer->WriteID(*value.GetStableId());
				break;
			case ESchemaValueKind::Bytes:
				writer->WriteUint64(static_cast<uint64>(value.GetBytes()->Size()));

				for (const Byte byte : *value.GetBytes())
				{
					writer->WriteByte(static_cast<uint8>(byte));
				}
				break;
			case ESchemaValueKind::Array:
				writer->WriteUint64(static_cast<uint64>(value.GetArray()->Size()));

				for (const Value& element : *value.GetArray())
				{
					const Result<void> result = writeValue(element, writer, scratchResource);

					if (!result)
					{
						return result;
					}
				}
				break;
			case ESchemaValueKind::Object:
			{
				const Value::Object& object = *value.GetObject();
				writer->WriteUint64(static_cast<uint64>(object.Fields.Size()));

				for (const Value::FieldMap::Entry& entry : object.Fields)
				{
					const FieldId& field = entry.GetKey();
					const Value& fieldValue = entry.Value;
					writer->WriteID(field);
					const Result<void> result = writeValue(fieldValue, writer, scratchResource);

					if (!result)
					{
						return result;
					}
				}

				writer->WriteUint64(static_cast<uint64>(object.UnknownFields.Size()));

				for (const Value::UnknownFieldMap::Entry& field : object.UnknownFields)
				{
					writer->WriteString(field.GetKey().View());
					const Result<void> result = writeValue(field.Value, writer, scratchResource);

					if (!result)
					{
						return result;
					}
				}
				break;
			}
			default:
				ASSERT(false, "Schema catalog encoding requires a documented schema value kind.");
			}

			return Result<void>::Success();
		}

		Result<Array<Byte>> writeCatalog(const SchemaRegistry& registry, MemoryResource& outputResource,
		                                 MemoryResource& scratchResource, const SchemaCatalogLimits& limits)
		{
			if (!limits.IsValidProfile())
			{
				return Result<Array<Byte>>::Failure(invalidLimits().GetError());
			}

			ASSERT(registry.IsFrozen(), "A schema catalog can be written only from a frozen registry.");

			Result<Array<const SchemaDescriptor*>> schemasResult = buildSchemaOrder(registry, scratchResource, limits);

			if (!schemasResult)
			{
				return Result<Array<Byte>>::Failure(schemasResult.GetError());
			}

			uint64 fieldCount = 0;
			Result<uint64> measuredResult = measureCatalog(schemasResult.Value(), scratchResource, limits, &fieldCount);

			if (!measuredResult)
			{
				return Result<Array<Byte>>::Failure(measuredResult.GetError());
			}

			if (measuredResult.Value() > static_cast<uint64>(rox::core::NumericLimits<rox::core::usize>::max()))
			{
				return failure<Array<Byte>>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "The measured schema catalog cannot be represented by this process.", "Use a smaller catalog or a platform with a sufficient address space.");
			}

			Array<Byte> output(outputResource);

			if (!output.Resize(static_cast<rox::core::usize>(measuredResult.Value())))
			{
				return allocationFailure<Array<Byte>>();
			}

			CatalogByteWriter writer(output.View());

			for (const char character : MAGIC)
			{
				writer.WriteByte(static_cast<uint8>(character));
			}

			writer.WriteUint16(static_cast<uint16>(SCHEMA_CATALOG_FORMAT_VERSION));
			writer.WriteUint16(static_cast<uint16>(HEADER_SIZE));
			writer.WriteUint32(ENDIAN_MARKER);
			writer.WriteUint32(0);
			writer.WriteUint64(static_cast<uint64>(schemasResult.Value().Size()));
			writer.WriteUint64(fieldCount);
			writer.WriteUint64(measuredResult.Value() - HEADER_SIZE);
			writer.WriteUint64(0);

			for (const SchemaDescriptor* const schema : schemasResult.Value())
			{
				Result<Array<const SchemaFieldDescriptor*>> fieldsResult = buildFieldOrder(*schema, scratchResource);

				if (!fieldsResult)
				{
					return Result<Array<Byte>>::Failure(fieldsResult.GetError());
				}

				writer.WriteID(schema->ID);
				writer.WriteID(schema->Type);
				writer.WriteUint32(schema->Version);
				writer.WriteUint32(0);
				writer.WriteString(schema->Name);
				writer.WriteUint64(static_cast<uint64>(fieldsResult.Value().Size()));

				for (const SchemaFieldDescriptor* const field : fieldsResult.Value())
				{
					uint16 presence = 0;
					presence |= field->DefaultValue != nullptr ? VALUE_PRESENT : 0;
					presence |= field->Constraints.Minimum != nullptr ? MINIMUM_PRESENT : 0;
					presence |= field->Constraints.Maximum != nullptr ? MAXIMUM_PRESENT : 0;
					presence |= field->Constraints.MinimumLength.has_value() ? MINIMUM_LENGTH_PRESENT : 0;
					presence |= field->Constraints.MaximumLength.has_value() ? MAXIMUM_LENGTH_PRESENT : 0;
					presence |= !field->EnumEntries.empty() ? ENUM_ENTRIES_PRESENT : 0;
					presence |= !field->NestedArrays.empty() ? NESTED_ARRAYS_PRESENT : 0;
					presence |= field->ElementMaximumLength.has_value() ? ELEMENT_MAXIMUM_LENGTH_PRESENT : 0;
					writer.WriteID(field->ID);
					writer.WriteID(field->ValueType);
					writer.WriteID(field->ElementType);
					writer.WriteUint32(static_cast<uint32>(field->Flags));
					writer.WriteByte(static_cast<uint8>(field->ValueKind));
					writer.WriteByte(static_cast<uint8>(field->ElementValueKind));
					writer.WriteUint16(presence);
					writer.WriteString(field->Name);
					writer.WriteString(field->Display.DisplayName);
					writer.WriteString(field->Display.Description);
					writer.WriteString(field->Display.Category);

					if (!field->EnumEntries.empty())
					{
						writer.WriteUint64(field->EnumEntries.size());

						for (const SchemaEnumEntry& entry : field->EnumEntries)
						{
							writer.WriteUint64(entry.Bits);
							writer.WriteString(entry.Name);
						}
					}

					if (!field->NestedArrays.empty())
					{
						writer.WriteUint64(field->NestedArrays.size());

						for (const SchemaArrayDescriptor& nested : field->NestedArrays)
						{
							writer.WriteUint64(static_cast<uint64>(nested.ElementValueKind));
							writer.WriteID(nested.ElementType);
							writer.WriteUint64(nested.MinimumLength);
							writer.WriteUint64(nested.MaximumLength);
						}
					}

					if (field->Constraints.MinimumLength.has_value())
					{
						writer.WriteUint64(*field->Constraints.MinimumLength);
					}

					if (field->Constraints.MaximumLength.has_value())
					{
						writer.WriteUint64(*field->Constraints.MaximumLength);
					}

					if (field->ElementMaximumLength.has_value())
					{
						writer.WriteUint64(*field->ElementMaximumLength);
					}

					const Value* const values[] = {field->DefaultValue, field->Constraints.Minimum,
					                               field->Constraints.Maximum};
					for (const Value* const value : values)
					{
						if (value != nullptr)
						{
							const Result<void> result = writeValue(*value, &writer, scratchResource);

							if (!result)
							{
								return Result<Array<Byte>>::Failure(result.GetError());
							}
						}
					}
				}
			}

			ASSERT(writer.GetOffset() == measuredResult.Value(), "Schema catalog encoding must write exactly the measured byte count.");
			return Result<Array<Byte>>::Success(rox::core::Move(output));
		}

		Result<Value> decodeValue(CatalogByteReader* const reader, ValueContext* const context, const uint32 depth, MemoryResource& resource)
		{
			ASSERT(reader != nullptr, "Schema catalog value decoding requires a valid reader.");
			const Result<void> consumed = consumeValue(context, depth);

			if (!consumed)
			{
				return Result<Value>::Failure(consumed.GetError());
			}

			uint8 tag = 0;

			if (!reader->ReadByte(&tag) || tag > static_cast<uint8>(ESchemaValueKind::Object))
			{
				return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-VALUE-KIND", "A schema catalog value has a truncated or unknown kind.", "Replace the catalog with bytes produced by the current codec.");
			}

			const ESchemaValueKind kind = static_cast<ESchemaValueKind>(tag);

			switch (kind)
			{
			case ESchemaValueKind::Null:
				return Result<Value>::Success(Value());
			case ESchemaValueKind::Boolean:
			{
				uint8 value = 0;

				if (!reader->ReadByte(&value) || value > 1)
				{
					return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-BOOLEAN", "A schema catalog boolean is truncated or non-canonical.", "Encode booleans as exactly zero or one.");
				}

				return Result<Value>::Success(Value(value != 0));
			}
			case ESchemaValueKind::SignedInteger:
			{
				uint64 bits = 0;

				if (!reader->ReadUint64(&bits))
				{
					return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-VALUE-TRUNCATED", "A schema catalog integer is truncated.", "Replace the catalog with complete bytes.");
				}

				return Result<Value>::Success(Value(rox::core::BitCast<int64>(bits)));
			}
			case ESchemaValueKind::UnsignedInteger:
			{
				uint64 value = 0;

				if (!reader->ReadUint64(&value))
				{
					return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-VALUE-TRUNCATED", "A schema catalog integer is truncated.", "Replace the catalog with complete bytes.");
				}

				return Result<Value>::Success(Value(value));
			}
			case ESchemaValueKind::FloatingPoint:
			{
				uint64 bits = 0;

				if (!reader->ReadUint64(&bits))
				{
					return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-VALUE-TRUNCATED", "A schema catalog floating-point value is truncated.", "Replace the catalog with complete bytes.");
				}

				const float64 value = rox::core::BitCast<float64>(bits);

				if (!rox::core::IsFinite(value) || (value == 0.0 && rox::core::SignBit(value)))
				{
					return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-FLOAT-NON-CANONICAL", "A schema catalog floating-point value is non-finite or negative zero.", "Encode finite values and canonicalize both signed zeros to positive zero.");
				}

				return Result<Value>::Success(Value(value));
			}
			case ESchemaValueKind::String:
			{
				rox::core::StringView value;

				if (!reader->ReadString(&value, context->Limits.MaximumStringBytes) ||
				    !IsValidUtf8({value.data(), value.size()}))
				{
					return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-STRING", "A schema catalog string is truncated, over limit, or invalid UTF-8.", "Replace it with bounded well-formed UTF-8.");
				}

				Result<Value> copied = Value::CreateString(value, resource);

				if (!copied)
				{
					return allocationFailure<Value>();
				}

				return copied;
			}
			case ESchemaValueKind::StableId:
			{
				StableValueId value;

				if (!reader->ReadID(&value))
				{
					return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-VALUE-TRUNCATED", "A schema catalog stable ID is truncated.", "Replace the catalog with complete bytes.");
				}

				return Result<Value>::Success(Value(value));
			}
			case ESchemaValueKind::Bytes:
			{
				uint64 count = 0;

				if (!reader->ReadUint64(&count) || count > reader->GetRemaining() ||
				    count > static_cast<uint64>(rox::core::NumericLimits<rox::core::usize>::max()))
				{
					return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-VALUE-TRUNCATED", "A schema catalog byte value exceeds the remaining input.", "Replace the catalog with complete bounded bytes.");
				}

				Value::Bytes value(resource);

				if (!value.Reserve(static_cast<rox::core::usize>(count)))
				{
					return allocationFailure<Value>();
				}

				for (uint64 index = 0; index < count; ++index)
				{
					uint8 byte = 0;
					const bool bRead = reader->ReadByte(&byte);
					ASSERT(bRead, "A validated schema catalog byte count must fit the remaining input.");
					value.AddRequired(static_cast<Byte>(byte));
				}

				Result<Value> adopted = Value::CreateBytes(rox::core::Move(value));

				if (!adopted)
				{
					return allocationFailure<Value>();
				}

				return adopted;
			}
			case ESchemaValueKind::Array:
			{
				uint64 count = 0;

				if (!reader->ReadUint64(&count) || count > reader->GetRemaining() ||
				    count > context->Limits.MaximumValues - context->Values ||
				    count > static_cast<uint64>(rox::core::NumericLimits<rox::core::usize>::max()))
				{
					return failure<Value>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-VALUE-COUNT", "A schema catalog array count is invalid or exceeds its limit.", "Reduce the array or raise MaximumValues intentionally.");
				}

				Value::Array values(resource);

				if (!values.Reserve(static_cast<rox::core::usize>(count)))
				{
					return allocationFailure<Value>();
				}

				for (uint64 index = 0; index < count; ++index)
				{
					Result<Value> value = decodeValue(reader, context, depth + 1, resource);

					if (!value)
					{
						return value;
					}

					values.AddRequired(rox::core::Move(value).TakeValue());
				}

				Result<Value> adopted = Value::CreateArray(rox::core::Move(values));

				if (!adopted)
				{
					return allocationFailure<Value>();
				}

				return adopted;
			}
			case ESchemaValueKind::Object:
			{
				Value::Object object(resource);
				uint64 knownCount = 0;

				if (!reader->ReadUint64(&knownCount) || knownCount > reader->GetRemaining() / 17 ||
				    knownCount > context->Limits.MaximumValues - context->Values)
				{
					return failure<Value>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-VALUE-COUNT", "A schema catalog object field count is invalid or exceeds its limit.", "Reduce the object or raise MaximumValues intentionally.");
				}

				if (!object.Fields.Reserve(static_cast<rox::core::usize>(knownCount)))
				{
					return allocationFailure<Value>();
				}

				FieldId previousField;

				for (uint64 index = 0; index < knownCount; ++index)
				{
					FieldId field;

					if (!reader->ReadID(&field) || !field.IsValid() || (index != 0 && !(previousField < field)))
					{
						return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-OBJECT-ORDER", "Schema catalog object fields must have unique ascending IDs.", "Rebuild the catalog with the current deterministic codec.");
					}

					Result<Value> value = decodeValue(reader, context, depth + 1, resource);

					if (!value)
					{
						return value;
					}

					const bool bInserted = object.Fields.InsertRequired(field, rox::core::Move(value).TakeValue()).bInserted;
					ASSERT(bInserted, "Strict ascending schema catalog object fields must insert exactly once.");
					previousField = field;
				}

				uint64 unknownCount = 0;

				if (!reader->ReadUint64(&unknownCount) || unknownCount > reader->GetRemaining() / 9 ||
				    unknownCount > context->Limits.MaximumValues - context->Values)
				{
					return failure<Value>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-VALUE-COUNT", "A schema catalog unknown-field count is invalid or exceeds its limit.", "Reduce the object or raise MaximumValues intentionally.");
				}

				if (!object.UnknownFields.Reserve(static_cast<rox::core::usize>(unknownCount)))
				{
					return allocationFailure<Value>();
				}

				rox::core::StringView previousName;

				for (uint64 index = 0; index < unknownCount; ++index)
				{
					rox::core::StringView name;

					if (!reader->ReadString(&name, context->Limits.MaximumStringBytes) || name.empty() ||
					    !IsValidUtf8({name.data(), name.size()}) ||
					    (index != 0 && !unsignedStringLess(previousName, name)))
					{
						return failure<Value>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-OBJECT-ORDER", "Schema catalog unknown fields must have unique ascending UTF-8 names.", "Rebuild the catalog with the current deterministic codec.");
					}

					Result<Value> value = decodeValue(reader, context, depth + 1, resource);

					if (!value)
					{
						return value;
					}

					Result<ValueFieldName> copiedName = ValueFieldName::Copy(name, object.UnknownFields.GetResource());

					if (!copiedName)
					{
						return allocationFailure<Value>();
					}

					const bool bInserted = object.UnknownFields.InsertRequired(rox::core::Move(copiedName).TakeValue(), rox::core::Move(value).TakeValue()).bInserted;
					ASSERT(bInserted, "Strict ascending schema catalog unknown fields must insert exactly once.");
					previousName = name;
				}

				Result<Value> adopted = Value::CreateObject(rox::core::Move(object), resource);

				if (!adopted)
				{
					return allocationFailure<Value>();
				}

				return adopted;
			}
			default:
				ASSERT(false, "Schema catalog decoding requires a documented schema value kind.");
			}
		}

		bool equalBytes(const Span<const Byte> left, const Span<const Byte> right) noexcept
		{
			return left.Size() == right.Size() && (left.IsEmpty() || rox::core::CompareBytes(left.Data(), right.Data(), left.Size()) == 0);
		}

		Result<SchemaRegistry> readCatalog(const Span<const Byte> bytes, MemoryResource& outputResource,MemoryResource& scratchResource, const SchemaCatalogLimits& limits)
		{
			if (!limits.IsValidProfile())
			{
				return Result<SchemaRegistry>::Failure(invalidLimits().GetError());
			}

			if (bytes.Size() < HEADER_SIZE)
			{
				return failure<SchemaRegistry>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-HEADER", "The schema catalog is smaller than its fixed header.", "Provide a complete 48-byte current header and body.");
			}

			if (bytes.Size() > limits.MaximumBytes)
			{
				return failure<SchemaRegistry>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-BYTE-LIMIT", "The schema catalog size is outside the configured byte range.", "Provide a complete bounded current catalog.");
			}

			CatalogByteReader reader(bytes);

			for (const char character : MAGIC)
			{
				uint8 byte = 0;

				if (!reader.ReadByte(&byte) || byte != static_cast<uint8>(character))
				{
					return failure<SchemaRegistry>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-MAGIC", "The input is not a current Roxell schema catalog.", "Provide bytes beginning with the CSCA magic.");
				}
			}

			uint16 version = 0;
			uint16 headerSize = 0;
			uint32 endian = 0;
			uint32 catalogFlags = 0;
			uint64 schemaCount = 0;
			uint64 totalFieldCount = 0;
			uint64 bodySize = 0;
			uint64 reserved = 0;

			if (!reader.ReadUint16(&version) || !reader.ReadUint16(&headerSize) || !reader.ReadUint32(&endian) ||
			    !reader.ReadUint32(&catalogFlags) || !reader.ReadUint64(&schemaCount) ||
			    !reader.ReadUint64(&totalFieldCount) || !reader.ReadUint64(&bodySize) || !reader.ReadUint64(&reserved))
			{
				return failure<SchemaRegistry>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-HEADER", "The schema catalog header is truncated.", "Provide a complete 48-byte current header.");
			}

			if (version != SCHEMA_CATALOG_FORMAT_VERSION || headerSize != HEADER_SIZE || endian != ENDIAN_MARKER ||
			    catalogFlags != 0 || reserved != 0)
			{
				return failure<SchemaRegistry>(
				    EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-VERSION",
				    "The schema catalog version, endian marker, flags, or reserved field is unsupported.",
				    "Rebuild the catalog with the current CSCA v1 codec.");
			}

			if (bodySize != static_cast<uint64>(bytes.Size()) - HEADER_SIZE)
			{
				return failure<SchemaRegistry>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-SIZE", "The schema catalog body size does not match the complete input.", "Replace the truncated or trailing-byte catalog.");
			}

			if (schemaCount > limits.MaximumSchemas || totalFieldCount > limits.MaximumFields ||
			    schemaCount > static_cast<uint64>(rox::core::NumericLimits<rox::core::usize>::max()) ||
			    totalFieldCount > static_cast<uint64>(rox::core::NumericLimits<rox::core::usize>::max()) ||
			    (schemaCount == 0 && totalFieldCount != 0))
			{
				return failure<SchemaRegistry>(EErrorCode::Unsupported, "ROX-CORE-SCHEMA-CATALOG-COUNT", "The schema or field count is invalid or exceeds its configured limit.", "Publish a smaller valid registry or raise the explicit limits.");
			}

			// Returned metadata owns its storage independently of the call-local scratch arena.
			SchemaRegistry registry(outputResource);
			uint64 decodedFieldCount = 0;
			ValueContext valueContext{limits};
			for (uint64 schemaIndex = 0; schemaIndex < schemaCount; ++schemaIndex)
			{
				SchemaDescriptor schema;
				uint32 schemaReserved = 0;
				uint64 fieldCount = 0;

				if (!reader.ReadID(&schema.ID) || !reader.ReadID(&schema.Type) || !reader.ReadUint32(&schema.Version) ||
				    !reader.ReadUint32(&schemaReserved) ||
				    !reader.ReadString(&schema.Name, limits.MaximumStringBytes) || !reader.ReadUint64(&fieldCount))
				{
					return failure<SchemaRegistry>(EErrorCode::InvalidArgument,
					                               "ROX-CORE-SCHEMA-CATALOG-SCHEMA-TRUNCATED",
					                               "A schema catalog descriptor is truncated or over its string limit.",
					                               "Replace the catalog with complete bounded bytes.");
				}

				if (schemaReserved != 0 || fieldCount > totalFieldCount - decodedFieldCount || fieldCount > static_cast<uint64>(rox::core::NumericLimits<rox::core::usize>::max()))
				{
					return failure<SchemaRegistry>(
					    EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-SCHEMA-RECORD",
					    "A schema record has non-zero reserved data or an invalid field count.",
					    "Rebuild the catalog with the current deterministic codec.");
				}

				decodedFieldCount += fieldCount;

				Array<SchemaFieldDescriptor> fields(scratchResource);
				Array<Array<SchemaEnumEntry>> enumStorage(scratchResource);
				Array<Array<SchemaArrayDescriptor>> nestedStorage(scratchResource);
				Array<Value> defaultValues(scratchResource);
				Array<Value> minimumValues(scratchResource);
				Array<Value> maximumValues(scratchResource);
				const rox::core::usize fieldSize = static_cast<rox::core::usize>(fieldCount);

				if (!fields.Resize(fieldSize) || !defaultValues.Resize(fieldSize) ||
				    !minimumValues.Resize(fieldSize) || !maximumValues.Resize(fieldSize) ||
				    !enumStorage.Reserve(fieldSize) || !nestedStorage.Reserve(fieldSize))
				{
					return allocationFailure<SchemaRegistry>();
				}

				for (rox::core::usize fieldIndex = 0; fieldIndex < fieldSize; ++fieldIndex)
				{
					SchemaFieldDescriptor& field = fields[fieldIndex];
					uint32 flags = 0;
					uint8 valueKind = 0;
					uint8 elementKind = 0;
					uint16 presence = 0;

					if (!reader.ReadID(&field.ID) || !reader.ReadID(&field.ValueType) ||
					    !reader.ReadID(&field.ElementType) || !reader.ReadUint32(&flags) ||
					    !reader.ReadByte(&valueKind) || !reader.ReadByte(&elementKind) ||
					    !reader.ReadUint16(&presence) || !reader.ReadString(&field.Name, limits.MaximumStringBytes) ||
					    !reader.ReadString(&field.Display.DisplayName, limits.MaximumStringBytes) ||
					    !reader.ReadString(&field.Display.Description, limits.MaximumStringBytes) ||
					    !reader.ReadString(&field.Display.Category, limits.MaximumStringBytes))
					{
						return failure<SchemaRegistry>(EErrorCode::InvalidArgument,
						                               "ROX-CORE-SCHEMA-CATALOG-FIELD-TRUNCATED",
						                               "A schema catalog field is truncated or over its string limit.",
						                               "Replace the catalog with complete bounded bytes.");
					}

					if ((presence & ~KNOWN_PRESENCE_BITS) != 0 ||
					    valueKind > static_cast<uint8>(ESchemaValueKind::Object) ||
					    elementKind > static_cast<uint8>(ESchemaValueKind::Object))
					{
						return failure<SchemaRegistry>(EErrorCode::Unsupported,
						                               "ROX-CORE-SCHEMA-CATALOG-FIELD-METADATA",
						                               "A schema field has unknown presence bits or value kinds.",
						                               "Rebuild the catalog with the current CSCA v1 codec.");
					}

					field.Flags = static_cast<ESchemaFieldFlags>(flags);
					field.ValueKind = static_cast<ESchemaValueKind>(valueKind);
					field.ElementValueKind = static_cast<ESchemaValueKind>(elementKind);
					enumStorage.EmplaceRequired(scratchResource);

					if ((presence & ENUM_ENTRIES_PRESENT) != 0)
					{
						uint64 count = 0;

						if (!reader.ReadUint64(&count) || count == 0 || count > MAXIMUM_SCHEMA_ENUM_ENTRIES || count > limits.MaximumValues - valueContext.Values || count > reader.GetRemaining() / 16)
						{
							return failure<SchemaRegistry>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-ENUM",
							                               "Enum metadata is truncated or exceeds its entry limits.",
							                               "Supply a complete bounded catalog.");
						}

						valueContext.Values += count;
						Array<SchemaEnumEntry>& entries = enumStorage[fieldIndex];

						if (!entries.Resize(static_cast<rox::core::usize>(count)))
						{
							return allocationFailure<SchemaRegistry>();
						}

						for (SchemaEnumEntry& entry : entries)
						{
							if (!reader.ReadUint64(&entry.Bits) || !reader.ReadString(&entry.Name, limits.MaximumStringBytes))
							{
								return failure<SchemaRegistry>(
								    EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-ENUM",
								    "An enum entry is truncated or exceeds its string limit.",
								    "Supply complete bounded enum metadata.");
							}
						}

						field.EnumEntries = {entries.Data(), entries.Size()};
					}

					nestedStorage.EmplaceRequired(scratchResource);

					if ((presence & NESTED_ARRAYS_PRESENT) != 0)
					{
						uint64 count = 0;

						if (!reader.ReadUint64(&count) || count == 0 || count > MAXIMUM_SCHEMA_NESTED_ARRAYS || count > limits.MaximumValues - valueContext.Values || count > reader.GetRemaining() / 40)
						{
							return failure<SchemaRegistry>(EErrorCode::InvalidArgument,
							                               "ROX-CORE-SCHEMA-CATALOG-NESTED-ARRAY",
							                               "Nested array metadata is truncated or exceeds its limits.",
							                               "Supply a complete bounded catalog.");
						}

						valueContext.Values += count;
						Array<SchemaArrayDescriptor>& nested = nestedStorage[fieldIndex];

						if (!nested.Resize(static_cast<rox::core::usize>(count)))
						{
							return allocationFailure<SchemaRegistry>();
						}

						for (SchemaArrayDescriptor& level : nested)
						{
							uint64 kind = 0;

							if (!reader.ReadUint64(&kind) || kind > static_cast<uint64>(ESchemaValueKind::Object) ||
							    !reader.ReadID(&level.ElementType) || !reader.ReadUint64(&level.MinimumLength) ||
							    !reader.ReadUint64(&level.MaximumLength))
							{
								return failure<SchemaRegistry>(EErrorCode::InvalidArgument,
								                               "ROX-CORE-SCHEMA-CATALOG-NESTED-ARRAY",
								                               "A nested array descriptor is truncated or invalid.",
								                               "Supply complete typed array metadata.");
							}

							level.ElementValueKind = static_cast<ESchemaValueKind>(kind);
						}

						field.NestedArrays = {nested.Data(), nested.Size()};
					}

					if ((presence & MINIMUM_LENGTH_PRESENT) != 0)
					{
						uint64 value = 0;

						if (!reader.ReadUint64(&value))
						{
							return failure<SchemaRegistry>(EErrorCode::InvalidArgument,
							                               "ROX-CORE-SCHEMA-CATALOG-FIELD-TRUNCATED",
							                               "A schema field minimum length is truncated.",
							                               "Replace the catalog with complete bytes.");
						}

						field.Constraints.MinimumLength = value;
					}

					if ((presence & MAXIMUM_LENGTH_PRESENT) != 0)
					{
						uint64 value = 0;

						if (!reader.ReadUint64(&value))
						{
							return failure<SchemaRegistry>(EErrorCode::InvalidArgument,
							                               "ROX-CORE-SCHEMA-CATALOG-FIELD-TRUNCATED",
							                               "A schema field maximum length is truncated.",
							                               "Replace the catalog with complete bytes.");
						}

						field.Constraints.MaximumLength = value;
					}

					if ((presence & ELEMENT_MAXIMUM_LENGTH_PRESENT) != 0)
					{
						uint64 value = 0;

						if (!reader.ReadUint64(&value))
						{
							return failure<SchemaRegistry>(EErrorCode::InvalidArgument,
							                               "ROX-CORE-SCHEMA-CATALOG-TRUNCATED",
							                               "An element maximum length is truncated.",
							                               "Provide a complete current schema catalog.");
						}

						field.ElementMaximumLength = value;
					}

					Value* const values[] = {&defaultValues[fieldIndex], &minimumValues[fieldIndex], &maximumValues[fieldIndex]};
					const uint16 bits[] = {VALUE_PRESENT, MINIMUM_PRESENT, MAXIMUM_PRESENT};
					for (rox::core::usize valueIndex = 0; valueIndex < 3; ++valueIndex)
					{
						if ((presence & bits[valueIndex]) == 0)
						{
							continue;
						}

						Result<Value> value = decodeValue(&reader, &valueContext, 1, scratchResource);

						if (!value)
						{
							return Result<SchemaRegistry>::Failure(value.GetError());
						}
						*values[valueIndex] = rox::core::Move(value).TakeValue();
					}

					field.DefaultValue = (presence & VALUE_PRESENT) != 0 ? values[0] : nullptr;
					field.Constraints.Minimum = (presence & MINIMUM_PRESENT) != 0 ? values[1] : nullptr;
					field.Constraints.Maximum = (presence & MAXIMUM_PRESENT) != 0 ? values[2] : nullptr;
				}

				schema.Fields = {fields.Data(), fields.Size()};
				const Result<void> registered = registry.Register(schema);

				if (!registered)
				{
					return Result<SchemaRegistry>::Failure(registered.GetError());
				}
			}

			if (decodedFieldCount != totalFieldCount || !reader.IsAtEnd())
			{
				return failure<SchemaRegistry>(
				    EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-SIZE",
				    "The schema catalog field count or final byte position does not match its header.",
				    "Replace the truncated or trailing-byte catalog.");
			}

			registry.Freeze();

			Result<Array<Byte>> canonical = writeCatalog(registry, scratchResource, scratchResource, limits);

			if (!canonical)
			{
				return Result<SchemaRegistry>::Failure(canonical.GetError());
			}

			if (!equalBytes(bytes, canonical.Value().View()))
			{
				return failure<SchemaRegistry>(
				    EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-CATALOG-NON-CANONICAL",
				    "The schema catalog is valid data but not in its unique canonical encoding.",
				    "Rebuild it with the current deterministic codec.");
			}

			return Result<SchemaRegistry>::Success(rox::core::Move(registry));
		}
	} // namespace

	Result<Array<Byte>> WriteSchemaCatalog(const SchemaRegistry& registry, MemoryResource& outputResource, MemoryResource& scratchResource, const SchemaCatalogLimits& limits)
	{
		try
		{
			return writeCatalog(registry, outputResource, scratchResource, limits);
		}
		catch (const rox::core::AllocationException&)
		{
			return allocationFailure<Array<Byte>>();
		}
	}

	Result<SchemaRegistry> ReadSchemaCatalog(const Span<const Byte> bytes, MemoryResource& scratchResource, const SchemaCatalogLimits& limits)
	{
		return ReadSchemaCatalog(bytes, SystemMemoryResource::Get(), scratchResource, limits);
	}

	Result<SchemaRegistry> ReadSchemaCatalog(const Span<const Byte> bytes, MemoryResource& outputResource,
	                                         MemoryResource& scratchResource, const SchemaCatalogLimits& limits)
	{
		try
		{
			return readCatalog(bytes, outputResource, scratchResource, limits);
		}
		catch (const rox::core::AllocationException&)
		{
			return allocationFailure<SchemaRegistry>();
		}
	}

	namespace
	{
		constexpr uint64 TRANSFER_MAGIC = 0x3154465343585255ull;
		template <typename T>
		Result<T> invalidTransfer()
		{
			return failure<T>(EErrorCode::InvalidArgument, "ROX-CORE-SCHEMA-TRANSFER", "The descriptor transfer metadata is malformed or exceeds its bounds.", "Use an exact bounded descriptor transfer.");
		}

		bool measureDisplay(const SchemaDisplayMetadata& display, const SchemaCatalogLimits& limits, uint64& total)
		{
			ASSERT(display.Hint <= ESchemaUiHint::ReadOnly && rox::core::IsFinite(display.Step), "Registered display metadata must contain a known hint and finite step.");
			if (!addBytes(9, limits.MaximumBytes, &total))
			{
				return false;
			}

			const rox::core::StringView texts[] = {display.DisplayName, display.Description, display.Category, display.Icon, display.RefKind, display.Unit};
			for (const rox::core::StringView text : texts)
			{
				ASSERT(IsValidUtf8(text), "Registered display text must contain well-formed UTF-8.");
				if (!measureString(text, limits, &total))
				{
					return false;
				}
			}

			return true;
		}

		void writeDisplay(CatalogByteWriter& writer, const SchemaDisplayMetadata& display)
		{
			writer.WriteString(display.DisplayName);
			writer.WriteString(display.Description);
			writer.WriteString(display.Category);
			writer.WriteString(display.Icon);
			writer.WriteString(display.RefKind);
			writer.WriteString(display.Unit);
			writer.WriteByte(static_cast<uint8>(display.Hint));
			writer.WriteUint32(rox::core::BitCast<uint32>(display.Step));
			writer.WriteUint32(rox::core::BitCast<uint32>(display.Order));
		}

		bool readDisplay(CatalogByteReader& reader, SchemaDisplayMetadata& display, const SchemaCatalogLimits& limits)
		{
			rox::core::StringView* texts[] = {&display.DisplayName, &display.Description, &display.Category, &display.Icon, &display.RefKind, &display.Unit};
			for (rox::core::StringView* const text : texts)
			{
				if (!reader.ReadString(text, limits.MaximumStringBytes) || !IsValidUtf8({text->data(), text->size()}))
				{
					return false;
				}
			}

			uint8 hint = 0;
			uint32 step = 0;
			uint32 order = 0;
			if (!reader.ReadByte(&hint) || hint > static_cast<uint8>(ESchemaUiHint::ReadOnly) || !reader.ReadUint32(&step) || !reader.ReadUint32(&order))
			{
				return false;
			}

			display.Hint = static_cast<ESchemaUiHint>(hint);
			display.Step = rox::core::BitCast<float32>(step);
			display.Order = rox::core::BitCast<int32>(order);

			return rox::core::IsFinite(display.Step);
		}
	}

	Result<Array<Byte>> WriteSchemaCatalogTransfer(const SchemaRegistry& registry, MemoryResource& output, MemoryResource& scratch, const SchemaCatalogLimits& limits)
	{
		try
		{
			Result<Array<Byte>> binary = WriteSchemaCatalog(registry, scratch, scratch, limits);
			if (!binary)
			{
				return Result<Array<Byte>>::Failure(binary.GetError());
			}

			Result<Array<const SchemaDescriptor*>> ordered = buildSchemaOrder(registry, scratch, limits);
			if (!ordered)
			{
				return Result<Array<Byte>>::Failure(ordered.GetError());
			}

			uint64 total = 24;
			if (!addBytes(binary.Value().Size(), limits.MaximumBytes, &total))
			{
				return invalidTransfer<Array<Byte>>();
			}

			for (const SchemaDescriptor* schema : ordered.Value())
			{
				if (!addBytes(40, limits.MaximumBytes, &total) || !measureDisplay(schema->Display, limits, total))
				{
					return invalidTransfer<Array<Byte>>();
				}

				for (const SchemaFieldDescriptor& field : schema->Fields)
				{
					if (!addBytes(16, limits.MaximumBytes, &total) || !measureDisplay(field.Display, limits, total))
					{
						return invalidTransfer<Array<Byte>>();
					}
				}
			}

			Array<Byte> result(output);
			if (!result.Resize(static_cast<rox::core::usize>(total)))
			{
				return allocationFailure<Array<Byte>>();
			}

			CatalogByteWriter header(result.View());
			header.WriteUint64(TRANSFER_MAGIC);
			header.WriteUint64(binary.Value().Size());
			rox::core::Copy(binary.Value().begin(), binary.Value().end(), result.begin() + 16);

			CatalogByteWriter writer(result.View().Subspan(16 + binary.Value().Size()));
			writer.WriteUint64(ordered.Value().Size());
			for (const SchemaDescriptor* schema : ordered.Value())
			{
				writer.WriteID(schema->ID);
				writer.WriteID(schema->Type);
				writer.WriteUint64(schema->Fields.size());
				writeDisplay(writer, schema->Display);

				Result<Array<const SchemaFieldDescriptor*>> fields = buildFieldOrder(*schema, scratch);
				if (!fields)
				{
					return Result<Array<Byte>>::Failure(fields.GetError());
				}

				for (const SchemaFieldDescriptor* field : fields.Value())
				{
					writer.WriteID(field->ID);
					writeDisplay(writer, field->Display);
				}
			}

			ASSERT(writer.GetOffset() + 16 + binary.Value().Size() == total, "Transfer measurement must match its complete metadata bytes.");
			return Result<Array<Byte>>::Success(rox::core::Move(result));
		}
		catch (const rox::core::AllocationException&)
		{
			return allocationFailure<Array<Byte>>();
		}
	}

	Result<SchemaRegistry> ReadSchemaCatalogTransfer(Span<const Byte> bytes, MemoryResource& output,
	    MemoryResource& scratch, const SchemaCatalogLimits& limits)
	{
		try
		{
			if (!limits.IsValidProfile() || bytes.Size() > limits.MaximumBytes)
			{
				return invalidTransfer<SchemaRegistry>();
			}

			CatalogByteReader header(bytes);
			uint64 magic = 0;
			uint64 length = 0;
			if (!header.ReadUint64(&magic) || magic != TRANSFER_MAGIC || !header.ReadUint64(&length) || length < HEADER_SIZE || length > header.GetRemaining())
			{
				return invalidTransfer<SchemaRegistry>();
			}

			Result<SchemaRegistry> binary = ReadSchemaCatalog(bytes.Subspan(16, static_cast<rox::core::usize>(length)), scratch, scratch, limits);
			if (!binary)
			{
				return Result<SchemaRegistry>::Failure(binary.GetError());
			}

			Result<Array<const SchemaDescriptor*>> ordered = buildSchemaOrder(binary.Value(), scratch, limits);
			if (!ordered)
			{
				return Result<SchemaRegistry>::Failure(ordered.GetError());
			}

			CatalogByteReader reader(bytes.Subspan(16 + static_cast<rox::core::usize>(length)));
			uint64 count = 0;
			if (!reader.ReadUint64(&count) || count != ordered.Value().Size())
			{
				return invalidTransfer<SchemaRegistry>();
			}

			SchemaRegistry result(output);
			for (const SchemaDescriptor* base : ordered.Value())
			{
				SchemaId id;
				TypeId type;
				uint64 countFields = 0;
				if (!reader.ReadID(&id) || id != base->ID || !reader.ReadID(&type) || type != base->Type || !reader.ReadUint64(&countFields) || countFields != base->Fields.size())
				{
					return invalidTransfer<SchemaRegistry>();
				}

				SchemaDescriptor descriptor = *base;
				if (!readDisplay(reader, descriptor.Display, limits))
				{
					return invalidTransfer<SchemaRegistry>();
				}

				Result<Array<const SchemaFieldDescriptor*>> orderedFields = buildFieldOrder(*base, scratch);
				if (!orderedFields)
				{
					return Result<SchemaRegistry>::Failure(orderedFields.GetError());
				}

				Array<SchemaFieldDescriptor> fields(scratch);
				if (!fields.Reserve(base->Fields.size()))
				{
					return allocationFailure<SchemaRegistry>();
				}

				for (const SchemaFieldDescriptor* original : orderedFields.Value())
				{
					FieldId fieldId;
					SchemaFieldDescriptor field = *original;
					if (!reader.ReadID(&fieldId) || fieldId != field.ID || !readDisplay(reader, field.Display, limits) ||
					    field.Display.DisplayName != original->Display.DisplayName || field.Display.Description != original->Display.Description ||
					    field.Display.Category != original->Display.Category)
					{
						return invalidTransfer<SchemaRegistry>();
					}
					fields.AddRequired(field);
				}

				descriptor.Fields = {fields.Data(), fields.Size()};
				const Result<void> registered = result.Register(descriptor);
				if (!registered)
				{
					return Result<SchemaRegistry>::Failure(registered.GetError());
				}
			}

			if (!reader.IsAtEnd())
			{
				return invalidTransfer<SchemaRegistry>();
			}

			result.Freeze();
			return Result<SchemaRegistry>::Success(rox::core::Move(result));
		}
		catch (const rox::core::AllocationException&)
		{
			return allocationFailure<SchemaRegistry>();
		}
	}
} // namespace rox::core
