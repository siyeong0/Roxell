#pragma once

#include <IThreadBackend.h>
#include <ShaderCompiler.h>
#include <ShaderBuildJob.h>

namespace rox::shader
{
	using ShaderWorkerCompilerFactory =
		core::Result<core::InterfaceOwner<IShaderCompiler>> (*)(void* context, EShaderCompiler kind, const ShaderCompilerOptions& options, core::MemoryResource& resource);

	struct ShaderBuildServiceDesc
	{
		EShaderCompiler Compiler = EShaderCompiler::Slang;
		ShaderCompilerOptions CompilerOptions;
		core::IThreadBackend* ThreadBackend = nullptr;
		ShaderWorkerCompilerFactory CompilerFactory = nullptr;
		void* CompilerFactoryContext = nullptr;

		core::uint32 WorkerCount = 2;
		core::uint32 MaximumQueuedJobs = 32;
		core::uint32 MaximumSubscribers = 128;
		core::uint64 MaximumAdmittedSourceBytes = 64u * 1024u * 1024u;
		core::uint64 MaximumRetainedResultBytes = 256u * 1024u * 1024u;
		core::uint64 MaximumResultBytesPerJob = 64u * 1024u * 1024u;
	};

	struct ShaderBuildStatistics
	{
		core::uint32 QueuedJobs = 0;
		core::uint32 RunningJobs = 0;
		core::uint32 Subscribers = 0;
		core::uint64 AdmittedSourceBytes = 0;
		core::uint64 RetainedResultBytes = 0;

		core::uint64 SubmittedSubscribers = 0;
		core::uint64 CompilerExecutions = 0;
		core::uint64 DeduplicatedSubscribers = 0;
		core::uint64 CancelledQueuedSubscribers = 0;
		core::uint64 CancelledRunningSubscribers = 0;

		bool bAdmissionClosed = false;
		bool bDrained = false;
	};

	// Submit/statistics are concurrent. Drain is control-thread-only; destruction requires successful drain.
	// The thread-safe resource outlives all handles. The backend and factory context outlive completed drain.
	class ShaderBuildService final
	{
	public:
		explicit ShaderBuildService(core::MemoryResource& resource) noexcept;
		~ShaderBuildService();
		ShaderBuildService(const ShaderBuildService&) = delete;
		ShaderBuildService& operator=(const ShaderBuildService&) = delete;

		[[nodiscard]] core::Result<void> Start(const ShaderBuildServiceDesc& desc);

		[[nodiscard]] core::Result<ShaderBuildJob> Submit(const ShaderBuildPlan&, const ShaderBuildJobOptions& = {});
		[[nodiscard]] core::Result<void> Drain(core::Milliseconds timeout);
		[[nodiscard]] core::Result<void> CancelAndDrain(core::Milliseconds timeout);
		[[nodiscard]] ShaderBuildStatistics GetStatistics() const noexcept;

	private:
		static core::Error error(core::EErrorCode code, core::StringView message);
		core::Result<void> initialize();
		core::Result<void> drain(core::Milliseconds timeout, bool bCancel);
		void worker() noexcept;
		void deadlines() noexcept;

	private:
		ShaderBuildServiceDesc m_Desc;
		core::MemoryResource& m_Resource;
		core::SharedPtr<BuildServiceState> m_State;
		bool m_bDrained = true;
	};

} // namespace rox::shader
