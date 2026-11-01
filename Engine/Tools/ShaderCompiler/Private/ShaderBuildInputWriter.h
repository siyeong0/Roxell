#pragma once

#include <ShaderBuildPlan.h>

namespace rox::shader
{
	class ShaderBuildInputWriter final
	{
	public:
		ShaderBuildInputWriter(core::uint64 maximumBytes, core::CancellationToken cancellation) noexcept;
		ShaderBuildInputWriter(core::Span<core::Byte> output, core::uint64 maximumBytes, core::CancellationToken cancellation) noexcept;
		ShaderBuildInputWriter(core::Span<const core::Byte> input, core::uint64 maximumBytes, core::CancellationToken cancellation) noexcept;
		void U32(core::uint32 value);
		void U64(core::uint64 value);
		void Text(core::StringView value);
		void Bytes(core::Span<const core::Byte> value);
		void Hash(const core::Hash256& value);
		void Target(const shader::ShaderTargetProfile& value);
		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] core::uint64 GetByteCount() const noexcept;
		[[nodiscard]] core::Result<void> Finish() const;

	private:
		enum class EMode
		{
			Count,
			Write,
			Compare
		};

		void raw(core::Span<const core::Byte> value);

	private:
		EMode m_Mode = EMode::Count;
		core::Span<core::Byte> m_Output;
		core::Span<const core::Byte> m_Input;
		core::uint64 m_MaximumBytes;
		core::uint64 m_Offset = 0;
		core::EErrorCode m_Error = core::EErrorCode::None;
		core::CancellationToken m_Cancellation;
	};

} // namespace rox::shader
