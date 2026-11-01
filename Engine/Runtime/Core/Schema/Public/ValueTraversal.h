#pragma once

#include <FunctionRef.hpp>
#include <StringView.hpp>
#include <Types.h>
#include <Result.hpp>
#include <Span.hpp>
#include <Value.h>

namespace rox::core
{
	enum class EValuePathKind : uint8
	{
		Field,
		UnknownField,
		ArrayElement
	};

	struct ValuePathSegment final
	{
		EValuePathKind Kind = EValuePathKind::Field;
		FieldId Field;
		StringView UnknownField;
		usize Index = 0;
	};

	enum class EValueVisitControl : uint8
	{
		Continue,
		SkipChildren,
		Stop
	};

	struct ValueTraversalLimits final
	{
		static constexpr uint32 HARD_MAXIMUM_DEPTH = 64;

		uint32 MaximumDepth = HARD_MAXIMUM_DEPTH;
		uint64 MaximumValues = 4'194'304;
	};

	struct ValueTraversalSummary final
	{
		uint64 VisitedCount = 0;
		bool Stopped = false;
	};

	using ValueVisitor = FunctionRef<EValueVisitControl(const Value&, Span<const ValuePathSegment>) noexcept>;

	// Input is immutable for the walk. Paths borrow callback-local storage; callback effects are not rolled back.
	[[nodiscard]] Result<ValueTraversalSummary> WalkValue(const Value& root, ValueVisitor visitor, ValueTraversalLimits limits = {}) noexcept;

	struct ValueCloneLimits final
	{
		static constexpr uint32 HARD_MAXIMUM_DEPTH = 256;

		uint32 MaximumDepth = HARD_MAXIMUM_DEPTH;
		uint64 MaximumValues = 4'194'304;
	};

	// All output owners use resource. The immutable input is unchanged even when allocation fails.
	[[nodiscard]] Result<Value> CloneValue(const Value& source, MemoryResource& resource, ValueCloneLimits limits = {}) noexcept;
}
