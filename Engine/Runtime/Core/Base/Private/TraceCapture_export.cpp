#include <TraceCapture.h>
#include <TextPrimitives.hpp>
#include <SystemError.hpp>
#include <StringView.hpp>
#include <Types.h>
#include <Assertion.h>
#include <charconv>
#include <system_error>

namespace rox::core
{
	namespace
	{
		class ChromeTraceWriter final
		{
		public:
			ChromeTraceWriter(TraceWriteCallback writer, void* context) noexcept
			    : m_Writer(writer)
			    , m_Context(context)
			{
			}

			void Text(rox::core::StringView text) noexcept
			{
				if (m_bSuccess)
				{
					m_bSuccess = m_Writer(m_Context, text);
				}
			}

			bool Succeeded() const noexcept { return m_bSuccess; }

			template <typename T> void Number(T value) noexcept
			{
				char buffer[64];
				const auto result = rox::core::ToChars(buffer, buffer + sizeof(buffer), value);
				ASSERT(result.ec == rox::core::EErrorCondition{}, "Trace numeric storage must fit the formatted value.");
				Text({buffer, static_cast<rox::core::usize>(result.ptr - buffer)});
			}

			void Pointer(rox::core::uintptr value) noexcept
			{
				char buffer[2 * sizeof(value)];
				const auto result = rox::core::ToChars(buffer, buffer + sizeof(buffer), value, 16);
				ASSERT(result.ec == rox::core::EErrorCondition{}, "Trace pointer storage must fit its hexadecimal value.");
				Text("\"0x");
				Text({buffer, static_cast<rox::core::usize>(result.ptr - buffer)});
				Text("\"");
			}

			void Microseconds(rox::core::uint64 nanoseconds) noexcept
			{
				Number(nanoseconds / 1000);
				const auto remainder = nanoseconds % 1000;

				if (remainder != 0)
				{
					const char fraction[] = {'.', static_cast<char>('0' + remainder / 100),
					                         static_cast<char>('0' + remainder / 10 % 10),
					                         static_cast<char>('0' + remainder % 10)};
					Text({fraction, sizeof(fraction)});
				}
			}

			void Quoted(rox::core::StringView value) noexcept
			{
				constexpr char hex[] = "0123456789abcdef";
				Text("\"");

				for (const unsigned char character : value)
				{
					if (character == '"' || character == '\\')
					{
						const char escaped[] = {'\\', static_cast<char>(character)};
						Text({escaped, sizeof(escaped)});
					}
					else if (character < 0x20)
					{
						const char escaped[] = {'\\', 'u', '0', '0', hex[character >> 4], hex[character & 15]};
						Text({escaped, sizeof(escaped)});
					}
					else
					{
						const char byte = static_cast<char>(character);
						Text({&byte, 1});
					}
				}
				Text("\"");
			}

		private:
			TraceWriteCallback m_Writer;
			void* m_Context;

			bool m_bSuccess = true;
		};
	} // namespace

	bool TraceCapture::WriteChromeTrace(TraceWriteCallback writer, void* context) const noexcept
	{
		ASSERT(writer != nullptr, "Trace export requires a writer callback.");

		ChromeTraceWriter output(writer, context);
		output.Text("{\"traceEvents\":[");
		bool bFirst = true;

		for (const auto& event : m_Events)
		{
			if (!output.Succeeded())
			{
				return false;
			}

			if (!bFirst)
			{
				output.Text(",");
			}
			bFirst = false;
			output.Text("{\"name\":");
			output.Quoted({event.Name, event.NameBytes});
			output.Text(",\"cat\":");
			output.Quoted({event.Category, event.CategoryBytes});
			output.Text(",\"ph\":");
			output.Quoted(event.Type == ETraceEventType::Scope     ? "X"
			              : (event.Type == ETraceEventType::Counter || event.Type == ETraceEventType::GpuDuration) ? "C"
			                                                       : "i");
			output.Text(",\"pid\":1,\"tid\":");
			output.Number(event.ThreadId);
			output.Text(",\"ts\":");
			output.Microseconds(event.TimestampNanoseconds);

			if (event.Type == ETraceEventType::Scope)
			{
				output.Text(",\"dur\":");
				output.Microseconds(event.DurationNanoseconds);
			}

			if (event.Type == ETraceEventType::Allocation || event.Type == ETraceEventType::Deallocation || event.Type == ETraceEventType::Frame)
			{
				output.Text(",\"s\":\"t\"");
			}

			output.Text(",\"truncated\":");
			output.Text(event.bTruncated ? "true" : "false");
			output.Text(",\"args\":{");

			if (event.Type == ETraceEventType::GpuDuration)
			{
				output.Text("\"durationNanoseconds\":");
				output.Number(event.DurationNanoseconds);
				output.Text(",\"queue\":");
				output.Number(event.Queue);
				output.Text(",\"frameId\":");
				output.Number(event.FrameId);
			}

			if (event.Type == ETraceEventType::Counter)
			{
				output.Text("\"value\":");
				output.Number(event.Value);
			}

			if (event.Type == ETraceEventType::Allocation || event.Type == ETraceEventType::Deallocation)
			{
				output.Text("\"resource\":");
				output.Pointer(event.ResourceId);
				output.Text(",\"address\":");
				output.Pointer(event.Address);
				output.Text(",\"bytes\":");
				output.Number(event.SizeBytes);
			}

			output.Text("}}");
		}

		output.Text("],\"droppedEvents\":");
		output.Number(m_Dropped);
		output.Text("}");
		return output.Succeeded();
	}
}
