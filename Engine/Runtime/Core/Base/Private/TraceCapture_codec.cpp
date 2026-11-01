#include <TraceCapture.h>
#include <StringView.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <Numeric.hpp>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include <Utf8.h>
#include <Assertion.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace rox::core
{
	namespace
	{
		class BinaryTraceCaptureWriter final
		{
		public:
			void Bytes(StringView bytes) noexcept
			{
				if (bSuccess)
				{
					bSuccess = Callback(Context, bytes);
				}
			}

			void Integer(uint64 value) noexcept
			{
				char bytes[8];
				for (unsigned int i = 0; i < 8; ++i)
				{
					bytes[i] = static_cast<char>((value >> (i * 8)) & 255);
				}

				Bytes({bytes, 8});
			}

			template <usize N> void Label(const char (&value)[N]) noexcept
			{
				usize length = 0;
				while (length < N && value[length] != 0)
				{
					++length;
				}

				if (length == N || !IsValidUtf8(StringView(value, length)))
				{
					bSuccess = false;
					return;
				}

				Integer(length);
				Bytes({value, length});
			}

		public:
			TraceWriteCallback Callback;
			void* Context;

			bool bSuccess = true;
		};

		class BinaryTraceCaptureReader final
		{
		public:
			bool Integer(uint64& value) noexcept
			{
				if (Bytes.size() - Offset < 8)
				{
					return false;
				}

				value = 0;
				for (unsigned int i = 0; i < 8; ++i)
				{
					value |= static_cast<uint64>(static_cast<unsigned char>(Bytes[Offset++])) << (i * 8);
				}

				return true;
			}

			template <usize N> bool Label(char (&value)[N], uint8* length = nullptr) noexcept
			{
				uint64 count = 0;
				if (!Integer(count) || count >= N || count > Bytes.size() - Offset)
				{
					return false;
				}

				const auto text = Bytes.substr(Offset, static_cast<usize>(count));
				if (text.find('\0') != StringView::npos || !IsValidUtf8(StringView(text.data(), text.size())))
				{
					return false;
				}

				core::Copy(text.begin(), text.end(), value);
				value[count] = 0;
				Offset += static_cast<usize>(count);
				if (length != nullptr)
				{
					*length = static_cast<uint8>(count);
				}

				return true;
			}

		public:
			StringView Bytes;

			usize Offset = 0;
		};
	} // namespace

	bool WriteTraceCapture(const TraceSnapshot& snapshot, TraceWriteCallback writer, void* context) noexcept
	{
		ASSERT(writer != nullptr, "Trace capture requires a writer callback.");

		BinaryTraceCaptureWriter output{writer, context};
		output.Bytes("ROXTRACE");
		output.Integer(1);
		output.Integer(snapshot.CaptureId);
		output.Integer(snapshot.Dropped);
		output.Integer(snapshot.bRecording ? 1 : 0);
		output.Integer(snapshot.Metadata.Width);
		output.Integer(snapshot.Metadata.Height);
		output.Label(snapshot.Metadata.Build);
		output.Label(snapshot.Metadata.Backend);
		output.Label(snapshot.Metadata.Device);
		output.Label(snapshot.Metadata.Quality);
		output.Integer(snapshot.Events.Size());

		for (const auto& event : snapshot.Events)
		{
			if (!output.bSuccess)
			{
				return false;
			}

			output.Integer(static_cast<uint64>(event.Type));
			output.Label(event.Name);
			output.Label(event.Category);
			output.Integer(event.bTruncated ? 1 : 0);
			output.Integer(event.TimestampNanoseconds);
			output.Integer(event.DurationNanoseconds);
			output.Integer(event.ThreadId);
			output.Integer(core::BitCast<uint64>(event.Value));
			output.Integer(event.ResourceId);
			output.Integer(event.Address);
			output.Integer(event.SizeBytes);
			output.Integer(event.CaptureId);
			output.Integer(event.ScopeId);
			output.Integer(event.ParentScopeId);
			output.Integer(event.FrameId);
			output.Integer(event.Queue);
		}

		return output.bSuccess;
	}

	bool ReadTraceCapture(StringView bytes, TraceSnapshot& output, usize maximumEvents) noexcept
	{
		if (!bytes.starts_with("ROXTRACE"))
		{
			return false;
		}

		BinaryTraceCaptureReader input{bytes, 8};
		TraceSnapshot candidate(output.Events.GetResource());
		uint64 version = 0;
		uint64 recording = 0;
		uint64 width = 0;
		uint64 height = 0;
		uint64 count = 0;

		if (!input.Integer(version) || version != 1 || !input.Integer(candidate.CaptureId) ||
		    !input.Integer(candidate.Dropped) || !input.Integer(recording) || recording > 1 ||
		    !input.Integer(width) || width > NumericLimits<uint32>::max() ||
		    !input.Integer(height) || height > NumericLimits<uint32>::max() ||
		    !input.Label(candidate.Metadata.Build) || !input.Label(candidate.Metadata.Backend) ||
		    !input.Label(candidate.Metadata.Device) || !input.Label(candidate.Metadata.Quality) ||
		    !input.Integer(count) || count > maximumEvents || count > (bytes.size() - input.Offset) / 128 ||
		    !candidate.Events.Reserve(static_cast<usize>(count)))
		{
			return false;
		}

		candidate.Metadata.Width = static_cast<uint32>(width);
		candidate.Metadata.Height = static_cast<uint32>(height);
		candidate.bRecording = recording != 0;

		for (uint64 i = 0; i < count; ++i)
		{
			CapturedTraceEvent event;
			uint64 type = 0;
			uint64 truncated = 0;
			uint64 value = 0;
			uint64 resource = 0;
			uint64 address = 0;
			uint64 size = 0;
			uint64 queue = 0;

			if (!input.Integer(type) || type > static_cast<uint64>(ETraceEventType::GpuDuration) ||
			    !input.Label(event.Name, &event.NameBytes) || !input.Label(event.Category, &event.CategoryBytes) ||
			    !input.Integer(truncated) || truncated > 1 || !input.Integer(event.TimestampNanoseconds) ||
			    !input.Integer(event.DurationNanoseconds) || !input.Integer(event.ThreadId) || !input.Integer(value) ||
			    !input.Integer(resource) || resource > NumericLimits<uintptr>::max() ||
			    !input.Integer(address) || address > NumericLimits<uintptr>::max() ||
			    !input.Integer(size) || size > NumericLimits<usize>::max() ||
			    !input.Integer(event.CaptureId) || event.CaptureId == 0 || event.CaptureId != candidate.CaptureId ||
			    !input.Integer(event.ScopeId) || !input.Integer(event.ParentScopeId) ||
			    !input.Integer(event.FrameId) || !input.Integer(queue) || queue > NumericLimits<uint32>::max() ||
			    event.DurationNanoseconds > NumericLimits<uint64>::max() - event.TimestampNanoseconds)
			{
				return false;
			}

			event.Type = static_cast<ETraceEventType>(type);
			event.bTruncated = truncated != 0;
			event.Value = core::BitCast<double>(value);

			if (!core::IsFinite(event.Value) ||
			    (event.Type == ETraceEventType::Scope && (event.ScopeId == 0 || event.ThreadId == 0 || event.ParentScopeId >= event.ScopeId)) ||
			    (event.Type != ETraceEventType::Scope && (event.ScopeId != 0 || event.ParentScopeId != 0)) ||
			    (event.Type != ETraceEventType::Scope && event.Type != ETraceEventType::GpuDuration && event.DurationNanoseconds != 0))
			{
				return false;
			}

			event.ResourceId = static_cast<uintptr>(resource);
			event.Address = static_cast<uintptr>(address);
			event.SizeBytes = static_cast<usize>(size);
			event.Queue = static_cast<uint32>(queue);
			candidate.Events.AddRequired(event);
		}

		if (input.Offset != bytes.size())
		{
			return false;
		}

		Array<TraceScopeStatistics> validation(output.Events.GetResource());
		if (!AnalyzeTraceSnapshot(candidate, validation))
		{
			return false;
		}

		output = core::Move(candidate);
		return true;
	}
}
