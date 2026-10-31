#include <ValueTraversal.h>
#include "ValueCloneBounds.h"
#include <Utility.hpp>
#include <Vocabulary.hpp>
#include <Types.h>
#include <array>

namespace rox::core
{
	namespace
	{
		enum class EWalkOutcome
		{
			Continue,
			Stop,
			DepthLimit,
			CountLimit
		};

		struct WalkState final
		{
			ValueVisitor Visitor;
			ValueTraversalLimits Limits;

			ValueTraversalSummary Summary;
			rox::core::StaticArray<ValuePathSegment, ValueTraversalLimits::HARD_MAXIMUM_DEPTH> Path;

			EWalkOutcome Visit(const Value& value, uint32 depth) noexcept
			{
				if (Summary.VisitedCount == Limits.MaximumValues)
				{
					return EWalkOutcome::CountLimit;
				}

				++Summary.VisitedCount;
				const EValueVisitControl control = Visitor(value, {Path.data(), depth});
				if (control == EValueVisitControl::Stop)
				{
					return EWalkOutcome::Stop;
				}

				if (control == EValueVisitControl::SkipChildren)
				{
					return EWalkOutcome::Continue;
				}

				ASSERT(control == EValueVisitControl::Continue, "Value visitor returned an invalid control value.");

				if (const Value::Array* array = value.GetArray())
				{
					for (rox::core::usize index = 0; index < array->Size(); ++index)
					{
						if (depth == Limits.MaximumDepth)
						{
							return EWalkOutcome::DepthLimit;
						}
						Path[depth] = {EValuePathKind::ArrayElement, {}, {}, index};
						const EWalkOutcome outcome = Visit((*array)[index], depth + 1);

						if (outcome != EWalkOutcome::Continue)
						{
							return outcome;
						}
					}
				}

				if (const Value::Object* object = value.GetObject())
				{
					for (const Value::FieldMap::Entry& entry : object->Fields)
					{
						const FieldId& field = entry.GetKey();
						const Value& child = entry.Value;

						if (depth == Limits.MaximumDepth)
						{
							return EWalkOutcome::DepthLimit;
						}
						Path[depth] = {EValuePathKind::Field, field, {}, 0};
						const EWalkOutcome outcome = Visit(child, depth + 1);

						if (outcome != EWalkOutcome::Continue)
						{
							return outcome;
						}
					}

					for (const Value::UnknownFieldMap::Entry& entry : object->UnknownFields)
					{
						const StringView& name = entry.GetKey().View();
						const Value& child = entry.Value;

						if (depth == Limits.MaximumDepth)
						{
							return EWalkOutcome::DepthLimit;
						}
						Path[depth] = {EValuePathKind::UnknownField, {}, name, 0};
						const EWalkOutcome outcome = Visit(child, depth + 1);

						if (outcome != EWalkOutcome::Continue)
						{
							return outcome;
						}
					}
				}

				return EWalkOutcome::Continue;
			}
		};

		Result<Value> allocationFailure()
		{
			return Result<Value>::Failure(EErrorCode::ExternalFailure, "ROX-CORE-VALUE-ALLOCATION", "Value cloning could not allocate its destination storage.", "Release memory or reduce input size.");
		}

		Result<Value> copyValidated(const Value& source, MemoryResource& resource)
		{
			if (const StringView* text = source.GetString())
			{
				return Value::CreateString(*text, resource);
			}

			if (const Span<const Byte>* bytes = source.GetBytes())
			{
				return Value::CreateBytes(*bytes, resource);
			}

			if (const Value::Array* array = source.GetArray())
			{
				Value::Array output(resource);

				if (!output.Reserve(array->Size()))
				{
					return allocationFailure();
				}

				for (const Value& child : *array)
				{
					Result<Value> copied = copyValidated(child, resource);

					if (!copied)
					{
						return copied;
					}
					output.AddRequired(rox::core::Move(copied).TakeValue());
				}

				return Value::CreateArray(rox::core::Move(output));
			}

			if (const Value::Object* object = source.GetObject())
			{
				Value::Object output(resource);

				if (!output.Fields.Reserve(object->Fields.Size()) ||
				    !output.UnknownFields.Reserve(object->UnknownFields.Size()))
				{
					return allocationFailure();
				}

				for (const Value::FieldMap::Entry& entry : object->Fields)
				{
					Result<Value> copied = copyValidated(entry.Value, resource);

					if (!copied)
					{
						return copied;
					}
					output.Fields.InsertRequired(entry.GetKey(), rox::core::Move(copied).TakeValue());
				}

				for (const Value::UnknownFieldMap::Entry& entry : object->UnknownFields)
				{
					Result<ValueFieldName> name = ValueFieldName::Copy(entry.GetKey().View(), resource);

					if (!name)
					{
						return Result<Value>::Failure(name.GetError());
					}

					Result<Value> copied = copyValidated(entry.Value, resource);

					if (!copied)
					{
						return copied;
					}
					output.UnknownFields.InsertRequired(rox::core::Move(name).TakeValue(), rox::core::Move(copied).TakeValue());
				}

				return Value::CreateObject(rox::core::Move(output), resource);
			}

			return Result<Value>::Success(source);
		}
	} // namespace

	Result<ValueTraversalSummary> WalkValue(const Value& root, ValueVisitor visitor, ValueTraversalLimits limits) noexcept
	{
		ASSERT(limits.MaximumDepth <= ValueTraversalLimits::HARD_MAXIMUM_DEPTH, "Value traversal depth must fit bounded stack storage.");
		ASSERT(limits.MaximumValues > 0, "Value traversal requires a positive node budget.");

		WalkState state{visitor, limits, {}, {}};
		const EWalkOutcome outcome = state.Visit(root, 0);

		if (outcome == EWalkOutcome::DepthLimit)
		{
			return Result<ValueTraversalSummary>::Failure(EErrorCode::Unsupported, "ROX-CORE-VALUE-WALK-DEPTH", "Value traversal exceeded its depth limit.", "Reduce nesting or increase the depth limit within the supported bound.");
		}

		if (outcome == EWalkOutcome::CountLimit)
		{
			return Result<ValueTraversalSummary>::Failure(EErrorCode::Unsupported, "ROX-CORE-VALUE-WALK-COUNT", "Value traversal exceeded its node budget.", "Reduce the visited subtree or increase the node budget.");
		}

		state.Summary.Stopped = outcome == EWalkOutcome::Stop;
		return Result<ValueTraversalSummary>::Success(state.Summary);
	}

	Result<Value> CloneValue(const Value& source, MemoryResource& resource, ValueCloneLimits limits) noexcept
	{
		const Result<void> bounded = ValidateValueCloneBounds(source, limits);

		if (!bounded)
		{
			return Result<Value>::Failure(bounded.GetError());
		}

		return copyValidated(source, resource);
	}
}
