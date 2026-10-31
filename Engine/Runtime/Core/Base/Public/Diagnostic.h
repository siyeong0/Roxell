#pragma once

#include <CoreString.hpp>

namespace rox::core
{
	enum class EDiagnosticSeverity
	{
		Information,
		Warning,
		Error,
		Fatal
	};

	// A reportable record with the same text-ownership rules as Error. Build the text fields with
	// MakeLossyString when any part of it originates outside the process.
	struct Diagnostic
	{
		String DiagnosticID;
		EDiagnosticSeverity Severity = EDiagnosticSeverity::Information;

		String Module;

		String Message;
		String RecoveryHint;
	};
} // namespace rox::core
