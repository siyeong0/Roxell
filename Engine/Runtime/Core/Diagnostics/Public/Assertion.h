#pragma once

#include <source_location>
#include <TextPrimitives.hpp>
#include <SourceLocation.h>
#include <TypeTraits.hpp>
#include <string_view>
#include <type_traits>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace rox::core
{
	struct AssertionFailure
	{
		DiagnosticText Expression;
		DiagnosticText Message;
		SourceLocation Source;
	};

	using AssertionFailureHandler = void (*)(const AssertionFailure& failure) noexcept;

	// The observer is local to this binary image and runs after the assertion log is flushed.
	AssertionFailureHandler SetAssertionFailureHandler(AssertionFailureHandler handler) noexcept;

	void ReportAssertionFailure(const AssertionFailure& failure) noexcept;
	[[noreturn]] void TerminateAfterAssertion() noexcept;
} // namespace rox::core

#if defined(ASSERT)
#error "ASSERT is already defined. Remove the conflicting external macro before including Roxell Assertion.h."
#endif

#if defined(_MSC_VER)
#define ROXELL_INTERNAL_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__)
#define ROXELL_INTERNAL_DEBUG_BREAK() __builtin_debugtrap()
#elif defined(__GNUC__)
#define ROXELL_INTERNAL_DEBUG_BREAK() __builtin_trap()
#else
#define ROXELL_INTERNAL_DEBUG_BREAK() ::rox::core::TerminateAfterAssertion()
#endif

// Always active: conditions must be side-effect-free and messages must be English literals.
// Keep the break here so the debugger stops at the failing call site.
#define ASSERT(condition, message)                                                                                     \
	do                                                                                                                 \
	{                                                                                                                  \
		static_assert(                                                                                                 \
		    ::rox::core::IsArray<::rox::core::RemoveReference<decltype(message)>> &&                                         \
		        ::rox::core::IsSame<::rox::core::RemoveExtent<::rox::core::RemoveReference<decltype(message)>>, const char>,    \
		    "ASSERT message must be a narrow English string literal.");                                                \
		static_assert(sizeof(message) > 1, "ASSERT message must not be empty.");                                       \
		if (!(condition))                                                                                              \
		{                                                                                                              \
			constexpr ::rox::core::AssertionFailure roxellInternalFailure{#condition, message,                         \
			                                                              rox::core::SourceLocation::current()};            \
			::rox::core::ReportAssertionFailure(roxellInternalFailure);                                                \
			ROXELL_INTERNAL_DEBUG_BREAK();                                                                             \
			::rox::core::TerminateAfterAssertion();                                                                    \
		}                                                                                                              \
	} while (false)
