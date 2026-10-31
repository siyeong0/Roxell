#include <Log.h>
#include "DiagnosticThread.h"
#include <ClockPrimitives.hpp>
#include <Synchronization.hpp>
#include <TextPrimitives.hpp>
#include <Vocabulary.hpp>
#include <SystemError.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <Comparison.hpp>
#include <Assertion.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <system_error>

namespace rox::core
{
	namespace
	{
		constexpr rox::core::usize MAXIMUM_LOG_LINE_BYTES = 8192;

		constexpr rox::core::usize MAXIMUM_CATEGORY_BYTES = 64;
		constexpr rox::core::usize MAXIMUM_MESSAGE_BYTES = 512;
		constexpr rox::core::usize MAXIMUM_EXPRESSION_BYTES = 256;
		constexpr rox::core::usize MAXIMUM_FILE_BYTES = 256;
		constexpr rox::core::usize MAXIMUM_FUNCTION_BYTES = 128;
		constexpr rox::core::usize MAXIMUM_JSON_STRUCTURE_BYTES = 512;

		static_assert(MAXIMUM_LOG_LINE_BYTES >= (MAXIMUM_CATEGORY_BYTES + MAXIMUM_MESSAGE_BYTES + MAXIMUM_EXPRESSION_BYTES + MAXIMUM_FILE_BYTES + MAXIMUM_FUNCTION_BYTES) * 6 + MAXIMUM_JSON_STRUCTURE_BYTES);

		rox::core::Atomic<ILogSink*> s_LogSink = nullptr;

		rox::core::Atomic<IFailureDiagnostics*> s_FailureDiagnostics = nullptr;
		rox::core::Atomic<bool> s_bResultFailureTracingEnabled = false;
#if defined(NDEBUG)
		rox::core::Atomic<bool> s_bFailureDiagnosticsEnabled = false;
#else
		rox::core::Atomic<bool> s_bFailureDiagnosticsEnabled = true;
#endif
		thread_local bool s_bCapturingFailure = false;

		rox::core::Atomic<ELogStderrPolicy> s_StderrPolicy = ELogStderrPolicy::Immediate;
		rox::core::Atomic<ELogLevel> s_MinimumLogLevel = ELogLevel::Debug;

		rox::core::Atomic<rox::core::uint64> s_LogSequence = 0;
		thread_local bool s_bHandlingLog = false;

		[[nodiscard]] rox::core::uint64 getTimestampUnixMicroseconds() noexcept
		{
			const rox::core::Microseconds::rep timestamp = rox::core::DurationCast<rox::core::Microseconds>(rox::core::WallClock::now().time_since_epoch()).count();
			return timestamp > 0 ? static_cast<rox::core::uint64>(timestamp) : 1;
		}

		[[nodiscard]] rox::core::usize getScalarByteCount(const rox::core::DiagnosticText text, const rox::core::usize index) noexcept
		{
			const unsigned char lead = static_cast<unsigned char>(text[index]);
			const rox::core::usize count = lead >= 0xc2 && lead <= 0xdf   ? 2
			                          : lead >= 0xe0 && lead <= 0xef ? 3
			                          : lead >= 0xf0 && lead <= 0xf4 ? 4
			                                                         : 0;

			if (count == 0 || count > text.size() - index)
			{
				return 0;
			}

			for (rox::core::usize offset = 1; offset < count; ++offset)
			{
				const unsigned char continuation = static_cast<unsigned char>(text[index + offset]);

				if (continuation < 0x80 || continuation > 0xbf)
				{
					return 0;
				}
			}

			const unsigned char second = static_cast<unsigned char>(text[index + 1]);

			if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second >= 0xa0) ||
			    (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second >= 0x90))
			{
				return 0;
			}

			return count;
		}

		class BufferWriter final
		{
		public:
			explicit BufferWriter(rox::core::StaticArray<char, MAXIMUM_LOG_LINE_BYTES>& buffer) noexcept
			    : m_Buffer(buffer)
			{
			}

			void Append(const char character) noexcept
			{
				ASSERT(m_Size + 1 < m_Buffer.size(), "The bounded diagnostic buffer must have room for a character and its terminator.");
				m_Buffer[m_Size++] = character;
			}

			void Append(const rox::core::DiagnosticText text) noexcept
			{
				ASSERT(m_Size < m_Buffer.size(), "The bounded diagnostic buffer size must remain valid.");
				ASSERT(text.size() <= m_Buffer.size() - m_Size - 1, "The bounded diagnostic schema and field limits must fit in one log line.");

				if (!text.empty())
				{
					rox::core::CopyBytes(m_Buffer.data() + m_Size, text.data(), text.size());
				}

				m_Size += text.size();
			}

			void Append(const rox::core::uint64 value) noexcept
			{
				char digits[32] = {};
				const rox::core::ToCharsResult result = rox::core::ToChars(digits, digits + sizeof(digits), value);
				ASSERT(result.ec == rox::core::EErrorCondition(), "A 32-byte buffer must encode every unsigned 64-bit integer.");
				Append(rox::core::DiagnosticText(digits, static_cast<rox::core::usize>(result.ptr - digits)));
			}

			void AppendEscaped(const rox::core::DiagnosticText text, const rox::core::usize maximumBytes, bool* const outTruncated) noexcept
			{
				ASSERT(outTruncated != nullptr, "Log escaping requires a valid truncation output.");
				const rox::core::usize byteCount = (rox::core::Min)(text.size(), maximumBytes);
				*outTruncated = *outTruncated || byteCount != text.size();
				constexpr char HEX[] = "0123456789abcdef";

				for (rox::core::usize index = 0; index < byteCount; ++index)
				{
					const unsigned char value = static_cast<unsigned char>(text[index]);

					if (value >= 0x80)
					{
						const rox::core::usize scalarBytes = getScalarByteCount(text, index);

						if (scalarBytes == 0)
						{
							Append('?');
							*outTruncated = true;
						}
						else
						{
							if (scalarBytes > byteCount - index)
							{
								break;
							}

							Append(text.substr(index, scalarBytes));
							index += scalarBytes - 1;
						}

						continue;
					}

					switch (value)
					{
					case '"':
						Append("\\\"");
						break;
					case '\\':
						Append("\\\\");
						break;
					case '\b':
						Append("\\b");
						break;
					case '\f':
						Append("\\f");
						break;
					case '\n':
						Append("\\n");
						break;
					case '\r':
						Append("\\r");
						break;
					case '\t':
						Append("\\t");
						break;
					default:
						if (value < 0x20)
						{
							Append("\\u00");
							Append(HEX[(value >> 4) & 0x0f]);
							Append(HEX[value & 0x0f]);
						}
						else
						{
							Append(static_cast<char>(value));
						}
						break;
					}
				}
			}

			[[nodiscard]] rox::core::DiagnosticText Finish() noexcept
			{
				m_Buffer[m_Size] = '\0';
				return {m_Buffer.data(), m_Size};
			}

		private:
			rox::core::StaticArray<char, MAXIMUM_LOG_LINE_BYTES>& m_Buffer;
			rox::core::usize m_Size = 0;
		};

		[[nodiscard]] const char* getLogLevelName(const ELogLevel level) noexcept
		{
			switch (level)
			{
			case ELogLevel::Trace:
				return "trace";
			case ELogLevel::Debug:
				return "debug";
			case ELogLevel::Information:
				return "information";
			case ELogLevel::Warning:
				return "warning";
			case ELogLevel::Error:
				return "error";
			case ELogLevel::Critical:
				return "critical";
			default:
				ASSERT(false, "Every log level must have a stable JSON spelling.");
			}
		}

		[[nodiscard]] rox::core::DiagnosticText formatLogEvent(LogEvent* const event, rox::core::StaticArray<char, MAXIMUM_LOG_LINE_BYTES>* const buffer) noexcept
		{
			ASSERT(event != nullptr, "Log formatting requires a valid event.");
			ASSERT(buffer != nullptr, "Log formatting requires a valid bounded buffer.");

			BufferWriter writer(*buffer);
			bool bTruncated = false;
			writer.Append("{\"schema\":\"roxell.log.v1\",\"sequence\":");
			writer.Append(event->Sequence);
			writer.Append(",\"timestampUnixMicroseconds\":");
			writer.Append(event->TimestampUnixMicroseconds);
			writer.Append(",\"threadId\":");
			writer.Append(event->ThreadID);
			writer.Append(",\"level\":\"");
			writer.Append(getLogLevelName(event->Record.Level));
			writer.Append("\",\"category\":\"");
			writer.AppendEscaped(event->Record.Category, MAXIMUM_CATEGORY_BYTES, &bTruncated);
			writer.Append("\",\"message\":\"");
			writer.AppendEscaped(event->Record.Message, MAXIMUM_MESSAGE_BYTES, &bTruncated);
			writer.Append("\",\"expression\":\"");
			writer.AppendEscaped(event->Record.Expression, MAXIMUM_EXPRESSION_BYTES, &bTruncated);
			writer.Append("\",\"file\":\"");
			writer.AppendEscaped(event->Record.Source.file_name(), MAXIMUM_FILE_BYTES, &bTruncated);
			writer.Append("\",\"line\":");
			writer.Append(static_cast<rox::core::uint64>(event->Record.Source.line()));
			writer.Append(",\"function\":\"");
			writer.AppendEscaped(event->Record.Source.function_name(), MAXIMUM_FUNCTION_BYTES, &bTruncated);
			writer.Append("\",\"truncated\":");
			writer.Append(bTruncated ? "true" : "false");
			writer.Append("}\n");

			event->bTruncated = bTruncated;
			return writer.Finish();
		}

		void writeEmergencyStderr(const LogRecord& record) noexcept
		{
			std::fprintf(stderr, "Roxell recursive log [%s]: %.*s (%s:%u)\n", getLogLevelName(record.Level),
			             static_cast<int>((rox::core::Min)(record.Message.size(), MAXIMUM_MESSAGE_BYTES)),
			             record.Message.data(), record.Source.file_name(), record.Source.line());
			std::fflush(stderr);
		}
	} // namespace

	bool IsLogEnabled(const ELogLevel level) noexcept
	{
		ASSERT(level >= ELogLevel::Trace && level <= ELogLevel::Critical, "The queried log level must be a declared ELogLevel value.");
		return level >= GetMinimumLogLevel();
	}

	ELogStderrPolicy SetLogStderrPolicy(const ELogStderrPolicy policy) noexcept
	{
		ASSERT(policy >= ELogStderrPolicy::Immediate && policy <= ELogStderrPolicy::Disabled, "The stderr policy must be a declared ELogStderrPolicy value.");
		return s_StderrPolicy.exchange(policy, rox::core::MemoryOrderAcqRel);
	}

	ELogStderrPolicy GetLogStderrPolicy() noexcept
	{
		return s_StderrPolicy.load(rox::core::MemoryOrderAcquire);
	}

	ILogSink* SetLogSink(ILogSink* const sink) noexcept
	{
		return s_LogSink.exchange(sink, rox::core::MemoryOrderAcqRel);
	}

	ELogLevel SetMinimumLogLevel(const ELogLevel level) noexcept
	{
		ASSERT(level >= ELogLevel::Trace && level <= ELogLevel::Critical, "The minimum log level must be a declared ELogLevel value.");
		return s_MinimumLogLevel.exchange(level, rox::core::MemoryOrderAcqRel);
	}

	ELogLevel GetMinimumLogLevel() noexcept
	{
		return s_MinimumLogLevel.load(rox::core::MemoryOrderAcquire);
	}

	IFailureDiagnostics* SetFailureDiagnostics(IFailureDiagnostics* provider) noexcept
	{
		return s_FailureDiagnostics.exchange(provider, rox::core::MemoryOrderAcqRel);
	}

	bool SetFailureDiagnosticsEnabled(bool enabled) noexcept
	{
		return s_bFailureDiagnosticsEnabled.exchange(enabled, rox::core::MemoryOrderAcqRel);
	}

	bool IsFailureDiagnosticsEnabled() noexcept
	{
		return s_bFailureDiagnosticsEnabled.load(rox::core::MemoryOrderAcquire);
	}

	bool SetResultFailureTracingEnabled(bool enabled) noexcept
	{
		return s_bResultFailureTracingEnabled.exchange(enabled, rox::core::MemoryOrderAcqRel);
	}

	bool IsResultFailureTracingEnabled() noexcept
	{
		return s_bResultFailureTracingEnabled.load(rox::core::MemoryOrderAcquire);
	}

	void WriteLog(const LogRecord& record) noexcept
	{
		ASSERT(record.Level >= ELogLevel::Trace && record.Level <= ELogLevel::Critical, "A log record must use a declared ELogLevel value.");
		ASSERT(!record.Category.empty(), "A log record must have a non-empty category.");
		ASSERT(!record.Message.empty(), "A log record must have a non-empty message.");
		ASSERT(record.Source.line() != 0, "A log record must retain its source location.");

		if (!IsLogEnabled(record.Level))
		{
			return;
		}

		if (s_bHandlingLog)
		{
			writeEmergencyStderr(record);
			return;
		}

		s_bHandlingLog = true;

		const rox::core::uint64 sequence = s_LogSequence.fetch_add(1, rox::core::MemoryOrderRelaxed) + 1;
		ASSERT(sequence != 0, "The process-local log event sequence must not wrap.");

		LogEvent event{sequence, getTimestampUnixMicroseconds(), diagnosticThreadId(), record, false};
		rox::core::StaticArray<char, MAXIMUM_LOG_LINE_BYTES> buffer = {};
		const rox::core::DiagnosticText jsonLine = formatLogEvent(&event, &buffer);

		const ELogStderrPolicy policy = GetLogStderrPolicy();
		const bool bCritical = record.Level == ELogLevel::Critical;

		if (bCritical || policy != ELogStderrPolicy::Disabled)
		{
			std::fwrite(jsonLine.data(), 1, jsonLine.size(), stderr);
			if (bCritical || policy == ELogStderrPolicy::Immediate)
			{
				std::fflush(stderr);
			}
		}

		if (ILogSink* const sink = s_LogSink.load(rox::core::MemoryOrderAcquire))
		{
			sink->Write(event, jsonLine);
		}

		s_bHandlingLog = false;

		if (record.Level >= ELogLevel::Error && IsFailureDiagnosticsEnabled() && !s_bCapturingFailure)
		{
			if (IFailureDiagnostics* provider = s_FailureDiagnostics.load(rox::core::MemoryOrderAcquire))
			{
				s_bCapturingFailure = true;
				provider->Capture(event);
				s_bCapturingFailure = false;
			}
		}
	}

	void FlushLog() noexcept
	{
		std::fflush(stderr);

		if (s_bHandlingLog)
		{
			return;
		}

		if (ILogSink* const sink = s_LogSink.load(rox::core::MemoryOrderAcquire))
		{
			s_bHandlingLog = true;
			sink->Flush();
			s_bHandlingLog = false;
		}
	}
} // namespace rox::core
