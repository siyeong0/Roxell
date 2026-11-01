#pragma once

#include <Array.hpp>
#include <Cancellation.h>
#include <ComparisonTypes.hpp>
#include <Result.hpp>
#include <Span.hpp>
#include <StringView.hpp>
#include <Task.hpp>
#include <Types.h>

#include <Utility.hpp>
#include <Vocabulary.hpp>

namespace rox::os
{
	constexpr core::uint32 DEFAULT_MAXIMUM_PROCESS_COUNT = 8;
	constexpr core::uint32 MAXIMUM_PROCESS_COUNT = 64;
	constexpr rox::core::usize DEFAULT_MAXIMUM_CAPTURED_PROCESS_OUTPUT_BYTES = 1'048'576;
	constexpr rox::core::usize MAXIMUM_CAPTURED_PROCESS_OUTPUT_BYTES = 67'108'864;
	constexpr rox::core::usize MAXIMUM_PROCESS_ARGUMENT_COUNT = 256;

	struct ProcessHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const ProcessHandle&) const = default;
	};

	struct ProcessLaunchDesc final
	{
		core::StringView ExecutablePath;
		core::Span<const core::StringView> Arguments;
		core::StringView WorkingDirectoryPath;

		rox::core::usize MaximumCapturedOutputBytesPerStream = DEFAULT_MAXIMUM_CAPTURED_PROCESS_OUTPUT_BYTES;
		bool bHidden = true;
	};

	struct ProcessOutput final
	{
		explicit ProcessOutput(core::MemoryResource& resource) noexcept
			: Bytes(resource)
		{
		}

		core::Array<core::Byte> Bytes;

		bool bTruncated = false;
		bool bReadFailed = false;
		bool bAllocationFailed = false;
	};

	struct ProcessExit final
	{
		explicit ProcessExit(core::MemoryResource& resource) noexcept
			: StandardOutput(resource)
			, StandardError(resource)
		{
		}

		core::uint32 ExitCode = 0;
		bool bTerminationRequested = false;

		ProcessOutput StandardOutput;
		ProcessOutput StandardError;
	};

	class IProcessService
	{
	public:
		virtual ~IProcessService() = default;

		virtual core::Task<ProcessHandle> Launch(const ProcessLaunchDesc& desc, core::CancellationToken cancellation) = 0;
		virtual core::Task<ProcessExit> Wait(ProcessHandle process, core::MemoryResource& resultResource, core::CancellationToken cancellation) = 0;
		virtual core::Result<void> RequestTermination(ProcessHandle process) = 0;
		virtual core::Result<void> Release(ProcessHandle process) = 0;
	};
} // namespace rox::os
