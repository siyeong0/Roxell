#pragma once

#include <ClockPrimitives.hpp>
#include <ShaderCompilerTypes.h>
#include <SharedPtr.hpp>

namespace rox::shader
{
	enum class EShaderBuildJobState
	{
		Queued,
		Running,
		Succeeded,
		Failed,
		Cancelled
	};

	enum class EShaderBuildPriority
	{
		Low,
		Normal,
		High
	};

	struct ShaderBuildJobOptions
	{
		EShaderBuildPriority Priority = EShaderBuildPriority::Normal;
		bool bHasDeadline = false;
		core::MonotonicClock::time_point Deadline;
	};

	struct BuildResultStorage;
	struct BuildServiceState;

	// Immutable views borrow this owner. Copied Shader handles retain their allocation lease independently.
	class ShaderBuildResult final
	{
	public:
		// Publishes only a finalized result; the source record remains immutable afterward.
		[[nodiscard]] static ShaderBuildResult Publish(const core::SharedPtr<BuildResultStorage>& storage);

		ShaderBuildResult() noexcept;
		~ShaderBuildResult();
		ShaderBuildResult(const ShaderBuildResult&) noexcept;
		ShaderBuildResult& operator=(const ShaderBuildResult&) noexcept;
		ShaderBuildResult(ShaderBuildResult&&) noexcept;
		ShaderBuildResult& operator=(ShaderBuildResult&&) noexcept;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] bool HasOutput() const noexcept;
		[[nodiscard]] const ShaderBuildOutput& GetOutput() const noexcept;
		[[nodiscard]] const ShaderBuildReport& GetReport() const noexcept;
		[[nodiscard]] const core::Error& GetError() const noexcept;
		[[nodiscard]] core::uint64 GetRetainedByteCount() const noexcept;

	private:
		core::SharedPtr<BuildResultStorage> m_Storage;
	};

	struct BuildSubscriber;
	struct BuildExecution;

	struct ShaderBuildJobSnapshot
	{
		EShaderBuildJobState State = EShaderBuildJobState::Cancelled;
		ShaderBuildResult Result;
	};

	// Copies share one subscriber. The resource outlives all jobs, snapshots and retained results.
	class ShaderBuildJob final
	{
	public:
		// The producer holds the service mutex and reserves subscriber capacity before attachment.
		[[nodiscard]] static core::Result<ShaderBuildJob> Attach(
			const core::SharedPtr<BuildServiceState>& state,
			const core::SharedPtr<BuildExecution>& execution,
			const ShaderBuildJobOptions& options,
			core::MemoryResource& resource);

		ShaderBuildJob() noexcept;
		~ShaderBuildJob();
		ShaderBuildJob(const ShaderBuildJob&) noexcept;
		ShaderBuildJob& operator=(const ShaderBuildJob&) noexcept;
		ShaderBuildJob(ShaderBuildJob&&) noexcept;
		ShaderBuildJob& operator=(ShaderBuildJob&&) noexcept;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] ShaderBuildJobSnapshot Poll() const noexcept;
		void Cancel() const noexcept;

	private:
		core::SharedPtr<BuildSubscriber> m_Storage;
	};

} // namespace rox::shader
