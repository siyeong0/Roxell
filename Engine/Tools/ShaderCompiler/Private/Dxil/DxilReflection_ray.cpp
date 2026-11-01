#include "DxilReflection.h"

#include <Assertion.h>
#include <ByteReader.h>
#include <Types.h>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		Error unsupportedRay()
		{
			return MakeBuildError(EErrorCode::Unsupported, "DXIL ray library metadata is incomplete or outside the exact profile.");
		}

		uint32 word(Span<const Byte> bytes, usize offset) noexcept
		{
			ASSERT(offset <= bytes.Size() && bytes.Size() - offset >= 4, "RDAT word reads have checked bounds.");
			ByteReader reader({bytes.Data() + offset, 4});

			return reader.ReadU32LE();
		}

		StringView string(Span<const Byte> bytes, uint32 offset) noexcept
		{
			if (offset >= bytes.Size())
			{
				return {};
			}

			const char* begin = reinterpret_cast<const char*>(bytes.Data() + offset);

			for (usize count = 0; count < bytes.Size() - offset && count <= 4096; ++count)
			{
				if (begin[count] == '\0')
				{
					return {begin, count};
				}
			}

			return {};
		}

		uint32 shaderKind(shader::EShaderStage stage) noexcept
		{
			switch (stage)
			{
			case shader::EShaderStage::RayGeneration:
				return 7;
			case shader::EShaderStage::Intersection:
				return 8;
			case shader::EShaderStage::AnyHit:
				return 9;
			case shader::EShaderStage::ClosestHit:
				return 10;
			case shader::EShaderStage::Miss:
				return 11;
			case shader::EShaderStage::Callable:
				return 12;
			default:
				return 0xffffffffu;
			}
		}
	} // namespace

	Result<void> ReflectDxilRayMetadata(shader::ShaderVariant& variant, const shader::ShaderEntryPoint& entry, CancellationToken cancellation, [[maybe_unused]] uint32 abi)
	{
		using R = Result<void>;

		const Span<Byte> code = variant.Bytecode.View();
		const uint32 expectedKind = shaderKind(entry.Stage);
		if (expectedKind == 0xffffffffu)
		{
			return R::Failure(unsupportedRay());
		}

		if (code.Size() < 32 || code.Size() > 16u * 1024u * 1024u || word(code, 0) != 0x43425844 || word(code, 24) != code.Size())
		{
			return R::Failure(unsupportedRay());
		}

		const uint32 partCount = word(code, 28);
		if (partCount > 64 || partCount > (code.Size() - 32) / 4)
		{
			return R::Failure(unsupportedRay());
		}

		Span<const Byte> rdat;
		uint64 flags = 0;
		bool bFeaturePart = false;

		for (uint32 index = 0; index < partCount; ++index)
		{
			const uint32 offset = word(code, 32 + usize(index) * 4);
			if (offset < 32 + partCount * 4 || offset > code.Size() || code.Size() - offset < 8)
			{
				return R::Failure(unsupportedRay());
			}

			const uint32 size = word(code, offset + 4);
			if (size > code.Size() - offset - 8)
			{
				return R::Failure(unsupportedRay());
			}

			for (uint32 prior = 0; prior < index; ++prior)
			{
				const uint32 other = word(code, 32 + usize(prior) * 4);
				if (offset < uint64(other) + 8 + word(code, other + 4) && other < uint64(offset) + 8 + size)
				{
					return R::Failure(unsupportedRay());
				}
			}

			if (word(code, offset) == 0x54414452)
			{
				if (!rdat.IsEmpty())
				{
					return R::Failure(unsupportedRay());
				}

				rdat = {code.Data() + offset + 8, size};
			}
			else if (word(code, offset) == 0x30494653)
			{
				if (bFeaturePart || size != 8)
				{
					return R::Failure(unsupportedRay());
				}

				flags = word(code, offset + 8) | (uint64(word(code, offset + 12)) << 32);
				bFeaturePart = true;
			}
		}

		if (rdat.Size() < 8 || word(rdat, 0) != 0x10 || !bFeaturePart)
		{
			return R::Failure(unsupportedRay());
		}

		const uint32 tableCount = word(rdat, 4);
		if (tableCount > 64 || tableCount > (rdat.Size() - 8) / 4)
		{
			return R::Failure(unsupportedRay());
		}

		Span<const Byte> strings;
		Span<const Byte> functions;

		for (uint32 index = 0; index < tableCount; ++index)
		{
			const uint32 offset = word(rdat, 8 + usize(index) * 4);
			if (offset < 8 + tableCount * 4 || offset > rdat.Size() || rdat.Size() - offset < 8)
			{
				return R::Failure(unsupportedRay());
			}

			const uint32 size = word(rdat, offset + 4);
			if (size > rdat.Size() - offset - 8)
			{
				return R::Failure(unsupportedRay());
			}

			for (uint32 prior = 0; prior < index; ++prior)
			{
				const uint32 other = word(rdat, 8 + usize(prior) * 4);
				if (offset < uint64(other) + 8 + word(rdat, other + 4) && other < uint64(offset) + 8 + size)
				{
					return R::Failure(unsupportedRay());
				}
			}

			const uint32 kind = word(rdat, offset);
			if (kind == 1 || kind == 4)
			{
				Span<const Byte>& table = kind == 1 ? strings : functions;
				if (!table.IsEmpty())
				{
					return R::Failure(unsupportedRay());
				}

				table = {rdat.Data() + offset + 8, size};
			}
		}

		if (strings.IsEmpty() || functions.Size() < 8 || word(functions, 0) > 128 || word(functions, 4) != 52 || word(functions, 0) > (functions.Size() - 8) / 52)
		{
			return R::Failure(unsupportedRay());
		}

		uint32 matches = 0;
		uint32 payloadBytes = 0;
		uint32 attributeBytes = 0;

		for (uint32 index = 0; index < word(functions, 0); ++index)
		{
			if (cancellation.IsCancellationRequested())
			{
				return R::Failure(MakeBuildCancelledError());
			}

			const Span<const Byte> record(functions.Data() + 8 + usize(index) * 52, 52);
			if (string(strings, word(record, 4)) != variant.EmittedEntryPoint.View())
			{
				continue;
			}

			if (++matches != 1 || word(record, 16) != expectedKind || word(record, 12) != 0xffffffffu || word(record, 36) != (uint32{1} << expectedKind))
			{
				return R::Failure(unsupportedRay());
			}

			payloadBytes = word(record, 20);
			attributeBytes = word(record, 24);
			flags |= word(record, 28) | (uint64(word(record, 32)) << 32);
		}

		constexpr uint64 SUPPORTED_FLAGS = 0x00000001 | 0x00000002 | 0x00000004 | 0x00000008 | 0x00000020 | 0x00008000 | 0x00040000;
		const uint64 allowed = SUPPORTED_FLAGS | (uint64{0x06000000});
		if (matches != 1 || (flags & ~allowed) != 0)
		{
			return R::Failure(unsupportedRay());
		}

		uint32 representedPayload = 0;
		uint32 representedAttributes = 0;

		for (ShaderRayDataSignature& data : variant.RayInterface.Data)
		{
			const bool bAttributes = data.Kind == shader::EShaderRayDataKind::HitAttributes;
			data.ProvenByteSize = bAttributes ? attributeBytes : payloadBytes;
			if (data.ProvenByteSize == 0 || (bAttributes ? ++representedAttributes : ++representedPayload) != 1)
			{
				return R::Failure(unsupportedRay());
			}

			data.bHasProvenByteSize = true;
		}

		if ((payloadBytes != 0) != (representedPayload != 0) || (attributeBytes != 0) != (representedAttributes != 0))
		{
			return R::Failure(unsupportedRay());
		}

		variant.RequiredCapabilities |= static_cast<uint64>(shader::EShaderCapability::RayTracing);
		if ((flags & 1) != 0)
		{
			variant.RequiredCapabilities |= static_cast<uint64>(shader::EShaderCapability::Float64);
		}

		if ((flags & 0x8000) != 0)
		{
			variant.RequiredCapabilities |= static_cast<uint64>(shader::EShaderCapability::Int64);
		}

		if ((flags & 0x40000) != 0)
		{
			variant.RequiredCapabilities |= static_cast<uint64>(shader::EShaderCapability::Float16);
		}

		variant.Requirements.Features |= variant.RequiredCapabilities;
		variant.bStageIOComplete = true;
		variant.RayInterface.bOutgoingDataComplete = false;

		return R::Success();
	}
} // namespace rox::shader
