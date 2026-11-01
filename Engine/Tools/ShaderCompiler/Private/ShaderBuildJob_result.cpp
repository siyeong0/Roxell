#include <ShaderBuildJob.h>

#include "ShaderBuildState.h"

#include <Assertion.h>

namespace rox::shader
{
	using namespace core;

	ShaderBuildResult ShaderBuildResult::Publish(const SharedPtr<BuildResultStorage>& storage)
	{
		ASSERT(storage && storage->bPublished && storage->Resource, "Result publication requires finalized storage and retained memory.");
		ASSERT(
			storage->bHasOutput ? storage->Output.Artifact.IsValid() : storage->Error.Code != EErrorCode::None,
			"A finalized result contains a valid artifact or an explicit failure.");
		ShaderBuildResult result;
		result.m_Storage = storage;

		return result;
	}

	ShaderBuildResult::ShaderBuildResult() noexcept = default;
	ShaderBuildResult::~ShaderBuildResult() = default;
	ShaderBuildResult::ShaderBuildResult(const ShaderBuildResult&) noexcept = default;
	ShaderBuildResult& ShaderBuildResult::operator=(const ShaderBuildResult&) noexcept = default;
	ShaderBuildResult::ShaderBuildResult(ShaderBuildResult&&) noexcept = default;
	ShaderBuildResult& ShaderBuildResult::operator=(ShaderBuildResult&&) noexcept = default;

	bool ShaderBuildResult::IsValid() const noexcept
	{
		return static_cast<bool>(m_Storage);
	}

	bool ShaderBuildResult::HasOutput() const noexcept
	{
		return m_Storage && m_Storage->bHasOutput;
	}

	const ShaderBuildOutput& ShaderBuildResult::GetOutput() const noexcept
	{
		ASSERT(HasOutput(), "Build output inspection requires successful immutable result.");
		return m_Storage->Output;
	}

	const ShaderBuildReport& ShaderBuildResult::GetReport() const noexcept
	{
		ASSERT(IsValid(), "Report inspection requires immutable result.");
		return m_Storage->Report;
	}

	const Error& ShaderBuildResult::GetError() const noexcept
	{
		ASSERT(IsValid() && !HasOutput(), "Error inspection requires failed immutable result.");
		return m_Storage->Error;
	}

	uint64 ShaderBuildResult::GetRetainedByteCount() const noexcept
	{
		return m_Storage ? m_Storage->RetainedBytes : 0;
	}
} // namespace rox::shader
