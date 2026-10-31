#include "ValueCloneBounds.h"

namespace rox::core
{
	namespace
	{
		enum class ECloneBound
		{
			Valid,
			Depth,
			Count
		};

		ECloneBound checkBounds(const Value& value, uint32 depth, ValueCloneLimits limits, uint64& remaining) noexcept
		{
			if (depth > limits.MaximumDepth)
			{
				return ECloneBound::Depth;
			}

			if (remaining == 0)
			{
				return ECloneBound::Count;
			}

			--remaining;

			if (const Value::Array* array = value.GetArray())
			{
				for (const Value& child : *array)
				{
					const ECloneBound result = checkBounds(child, depth + 1, limits, remaining);

					if (result != ECloneBound::Valid)
					{
						return result;
					}
				}
			}

			if (const Value::Object* object = value.GetObject())
			{
				for (const Value::FieldMap::Entry& entry : object->Fields)
				{
					const ECloneBound result = checkBounds(entry.Value, depth + 1, limits, remaining);

					if (result != ECloneBound::Valid)
					{
						return result;
					}
				}

				for (const Value::UnknownFieldMap::Entry& entry : object->UnknownFields)
				{
					const ECloneBound result = checkBounds(entry.Value, depth + 1, limits, remaining);

					if (result != ECloneBound::Valid)
					{
						return result;
					}
				}
			}

			return ECloneBound::Valid;
		}
	}

	Result<void> ValidateValueCloneBounds(const Value& source, ValueCloneLimits limits) noexcept
	{
		ASSERT(limits.MaximumDepth <= ValueCloneLimits::HARD_MAXIMUM_DEPTH,
		       "Value clone depth must fit bounded stack storage.");
		ASSERT(limits.MaximumValues > 0, "Value cloning requires a positive node budget.");
		uint64 remaining = limits.MaximumValues;
		const ECloneBound bounded = checkBounds(source, 0, limits, remaining);

		if (bounded == ECloneBound::Depth)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "ROX-CORE-VALUE-WALK-DEPTH", "Value cloning exceeded its depth limit.", "Reduce nesting or increase the supported depth limit.");
		}

		if (bounded == ECloneBound::Count)
		{
			return Result<void>::Failure(EErrorCode::Unsupported, "ROX-CORE-VALUE-WALK-COUNT", "Value cloning exceeded its node budget.", "Reduce input size or increase the node budget.");
		}

		return Result<void>::Success();
	}
}
