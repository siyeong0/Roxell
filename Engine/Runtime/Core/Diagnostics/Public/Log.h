#pragma once

#include <cstdint>
#include <TextPrimitives.hpp>
#include <SourceLocation.h>
#include <Types.h>
#include <source_location>
#include <string_view>

namespace rox::core
{
	enum class ELogLevel
	{
		Trace,
		Debug,
		Information,
		Warning,
		Error,
		Critical
	};

	// Controls ordinary stderr output; Critical and recursive diagnostics always flush immediately.
	enum class ELogStderrPolicy
	{
		Immediate,
		Buffered,
		Disabled
	};

	struct LogRecord
	{
		ELogLevel Level = ELogLevel::Information;

		DiagnosticText Category;
		DiagnosticText Message;
		DiagnosticText Expression;

		SourceLocation Source;
	};

	struct LogEvent
	{
		uint64 Sequence = 0;
		uint64 TimestampUnixMicroseconds = 0;
		uint64 ThreadID = 0;

		LogRecord Record;

		// True if serialization truncates a field or replaces malformed UTF-8 bytes.
		bool bTruncated = false;
	};

	// The provider is borrowed. Install or remove it only when logging is quiescent; Capture may run concurrently.
	class IFailureDiagnostics
	{
	public:
		virtual ~IFailureDiagnostics() = default;
		virtual void Capture(const LogEvent& event) noexcept = 0;
	};

	IFailureDiagnostics* SetFailureDiagnostics(IFailureDiagnostics* provider) noexcept;
	bool SetFailureDiagnosticsEnabled(bool enabled) noexcept;
	[[nodiscard]] bool IsFailureDiagnosticsEnabled() noexcept;

	// Result creation and propagation tracing is disabled by default.
	bool SetResultFailureTracingEnabled(bool enabled) noexcept;
	[[nodiscard]] bool IsResultFailureTracingEnabled() noexcept;

	class ILogSink
	{
	public:
		virtual ~ILogSink() = default;

		// Write and Flush may overlap. Event data and the null-terminated JSON line are borrowed for this call.
		// Sequence reflects creation order, not callback arrival order.
		virtual void Write(const LogEvent& event, DiagnosticText jsonLine) noexcept = 0;
		virtual void Flush() noexcept = 0;
	};

	// The borrowed sink must outlive callbacks. Replace it only when logging and flushing are quiescent.
	ILogSink* SetLogSink(ILogSink* sink) noexcept;

	ELogLevel SetMinimumLogLevel(ELogLevel level) noexcept;
	[[nodiscard]] ELogLevel GetMinimumLogLevel() noexcept;
	[[nodiscard]] bool IsLogEnabled(ELogLevel level) noexcept;

	// Buffered skips per-record flushing; Disabled skips ordinary stderr output. Flush before switching if needed.
	ELogStderrPolicy SetLogStderrPolicy(ELogStderrPolicy policy) noexcept;
	[[nodiscard]] ELogStderrPolicy GetLogStderrPolicy() noexcept;

	// JSONL output is bounded; invalid UTF-8 is replaced and the borrowed record is unchanged.
	void WriteLog(const LogRecord& record) noexcept;
	void FlushLog() noexcept;
} // namespace rox::core

// Filtered macros do not evaluate category or message. Each macro is a single statement.
#define ROX_LOG_IMPL(level, category, message) \
	do \
	{ \
		if (::rox::core::IsLogEnabled(level)) \
		{ \
			::rox::core::WriteLog({level, category, message, {}, ::rox::core::SourceLocation::current()}); \
		} \
	} while (false)

#define LOG_TRACE(category, message) ROX_LOG_IMPL(::rox::core::ELogLevel::Trace, category, message)
#define LOG_DEBUG(category, message) ROX_LOG_IMPL(::rox::core::ELogLevel::Debug, category, message)
#define LOG_INFO(category, message) ROX_LOG_IMPL(::rox::core::ELogLevel::Information, category, message)
#define LOG_WARNING(category, message) ROX_LOG_IMPL(::rox::core::ELogLevel::Warning, category, message)
#define LOG_ERROR(category, message) ROX_LOG_IMPL(::rox::core::ELogLevel::Error, category, message)
#define LOG_CRITICAL(category, message) ROX_LOG_IMPL(::rox::core::ELogLevel::Critical, category, message)
