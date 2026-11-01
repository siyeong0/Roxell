#include "ShaderBuildInputWriter.h"

#include <Algorithms.hpp>
#include <Assertion.h>
#include <ByteOperations.hpp>
#include <ByteWriter.h>
#include <Comparison.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>

namespace rox::shader
{
	using namespace core;

	ShaderBuildInputWriter::ShaderBuildInputWriter(uint64 maximumBytes, CancellationToken cancellation) noexcept
		: m_MaximumBytes(maximumBytes)
		, m_Cancellation(cancellation)
	{
	}

	ShaderBuildInputWriter::ShaderBuildInputWriter(Span<Byte> output, uint64 maximumBytes, CancellationToken cancellation) noexcept
		: ShaderBuildInputWriter(maximumBytes, cancellation)
	{
		m_Mode = EMode::Write;
		m_Output = output;
	}

	ShaderBuildInputWriter::ShaderBuildInputWriter(Span<const Byte> input, uint64 maximumBytes, CancellationToken cancellation) noexcept
		: ShaderBuildInputWriter(maximumBytes, cancellation)
	{
		m_Mode = EMode::Compare;
		m_Input = input;
	}

	void ShaderBuildInputWriter::U32(uint32 value)
	{
		Byte bytes[4];
		ByteWriter writer(bytes);
		writer.WriteU32LE(value);
		raw(bytes);
	}

	void ShaderBuildInputWriter::U64(uint64 value)
	{
		Byte bytes[8];
		ByteWriter writer(bytes);
		writer.WriteU64LE(value);
		raw(bytes);
	}

	void ShaderBuildInputWriter::Text(StringView value)
	{
		U64(value.Size());
		raw({reinterpret_cast<const Byte*>(value.Data()), value.Size()});
	}

	void ShaderBuildInputWriter::Bytes(Span<const Byte> value)
	{
		U64(value.Size());
		raw(value);
	}

	void ShaderBuildInputWriter::Hash(const Hash256& value)
	{
		raw({reinterpret_cast<const Byte*>(value.Bytes.data()), value.Bytes.size()});
	}

	void ShaderBuildInputWriter::Target(const shader::ShaderTargetProfile& value)
	{
		U32(static_cast<uint32>(value.Target));
		U32(value.ShaderModelMajor);
		U32(value.ShaderModelMinor);
		U32(value.SpirVMajor);
		U32(value.SpirVMinor);
		U32(value.EnvironmentMajor);
		U32(value.EnvironmentMinor);
		U32(value.ProfileVersion);
	}

	bool ShaderBuildInputWriter::IsValid() const noexcept
	{
		return m_Error == EErrorCode::None && !m_Cancellation.IsCancellationRequested();
	}

	uint64 ShaderBuildInputWriter::GetByteCount() const noexcept
	{
		return m_Offset;
	}

	void ShaderBuildInputWriter::raw(Span<const Byte> value)
	{
		if (m_Error != EErrorCode::None)
		{
			return;
		}

		if (m_Cancellation.IsCancellationRequested())
		{
			m_Error = EErrorCode::Cancelled;
			return;
		}

		if (m_Offset > m_MaximumBytes || value.Size() > m_MaximumBytes - m_Offset)
		{
			m_Error = EErrorCode::Unsupported;
			return;
		}

		if (m_Mode == EMode::Compare && (m_Offset > m_Input.Size() || value.Size() > m_Input.Size() - m_Offset))
		{
			m_Error = EErrorCode::InvalidArgument;
			return;
		}

		ASSERT(
			m_Mode != EMode::Write ||
			(m_Offset <= m_Output.Size() && value.Size() <= m_Output.Size() - m_Offset), "A counted shader input record must fit its allocated output.");

		if (m_Mode != EMode::Count)
		{
			usize copiedBytes = 0;

			while (copiedBytes < value.Size())
			{
				if (m_Cancellation.IsCancellationRequested())
				{
					m_Error = EErrorCode::Cancelled;
					return;
				}

				const uint64 byteCount = Min<usize>(4096, value.Size() - copiedBytes);
				const usize offset = static_cast<usize>(m_Offset) + copiedBytes;

				if (m_Mode == EMode::Write)
				{
					CopyBytes(m_Output.Data() + offset, value.Data() + copiedBytes, byteCount);
				}
				else if (CompareBytes(m_Input.Data() + offset, value.Data() + copiedBytes, byteCount) != 0)
				{
					m_Error = EErrorCode::Conflict;
					return;
				}

				copiedBytes += byteCount;
			}
		}

		m_Offset += value.Size();
	}

	Result<void> ShaderBuildInputWriter::Finish() const
	{
		if (m_Cancellation.IsCancellationRequested())
		{
			return Result<void>::Failure(EErrorCode::Cancelled, "ROX-SHADER-INPUT-CANCELLED", "Input verification cancelled.");
		}

		if (m_Error != EErrorCode::None)
		{
			return Result<void>::Failure(m_Error, "ROX-SHADER-INPUT-PROOF", "The shader input record differs, is truncated or exceeds its bound.");
		}

		if (m_Mode == EMode::Compare && m_Offset != m_Input.Size())
		{
			return Result<void>::Failure(EErrorCode::InvalidArgument, "ROX-SHADER-INPUT-TRAILING", "The shader input record has trailing bytes.");
		}

		return Result<void>::Success();
	}
} // namespace rox::shader
