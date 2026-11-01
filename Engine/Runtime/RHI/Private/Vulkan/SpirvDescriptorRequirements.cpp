#include "SpirvDescriptorRequirements.h"
#include <ByteOperations.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utility.hpp>
#include <Vocabulary.hpp>
namespace rox::rhi
{
	using namespace core;

	Result<void> ValidateSpirvDescriptorIndexing(Span<const Byte> bytecode, const GpuCapabilities& capabilities) noexcept
	{
		using R = Result<void>;
		const auto fail = [](EErrorCode code)
			{
				Error error;
				error.Code = code;

				return R::Failure(Move(error));
			};
		if (bytecode.Size() < 5 * sizeof(uint32) || bytecode.Size() % sizeof(uint32))
		{
			return fail(EErrorCode::InvalidArgument);
		}

		const auto word = [&](usize index)
			{
				uint32 value = 0;
				CopyBytes(&value, bytecode.Data() + index * sizeof(value), sizeof(value));

				return value;
			};
		if (word(0) != 0x07230203u)
		{
			return fail(EErrorCode::InvalidArgument);
		}

		const bool bEnabled[4]{ capabilities.Features.bUniformBufferArrayDynamicIndexing, capabilities.Features.bSampledImageArrayDynamicIndexing,
							   capabilities.Features.bStorageBufferArrayDynamicIndexing, capabilities.Features.bStorageImageArrayDynamicIndexing };
		const uint64 wordCount = bytecode.Size() / sizeof(uint32);
		uint32 requiredSubgroupOperations = 0;
		bool bComputeEntry = false;
		bool bFragmentEntry = false;
		bool bOtherEntry = false;
		for (usize offset = 5; offset < wordCount;)
		{
			const uint32 instruction = word(offset);
			const uint32 length = instruction >> 16;
			const uint32 opcode = instruction & 0xffffu;
			if (!length || length > wordCount - offset)
			{
				return fail(EErrorCode::InvalidArgument);
			}
			if (opcode == 15 && length >= 4)
			{
				bComputeEntry |= word(offset + 1) == 5;
				bFragmentEntry |= word(offset + 1) == 4;
				bOtherEntry |= word(offset + 1) != 5 && word(offset + 1) != 4;
			}
			// OpExecutionModeId LocalSizeId requires an enabled maintenance4 feature.
			if (opcode == 331 && length >= 3 && word(offset + 2) == 38)
			{
				if (length != 6)
				{
					return fail(EErrorCode::InvalidArgument);
				}
				if (!capabilities.Features.bShaderLocalSizeId)
				{
					return fail(EErrorCode::Unsupported);
				}
			}
			if (opcode == 17)
			{
				if (length != 2)
				{
					return fail(EErrorCode::InvalidArgument);
				}
				const uint32 capability = word(offset + 1);
				if (capability >= 61 && capability <= 68)
				{
					requiredSubgroupOperations |= 1u << (capability - 61);
				}
				if (capability >= 28 && capability <= 31 && !bEnabled[capability - 28])
				{
					return fail(EErrorCode::Unsupported);
				}
				if ((capability == 9 && !capabilities.Features.bShaderFloat16) ||
					(capability == 5379 && !capabilities.Features.bShaderDemoteToHelperInvocation) ||
					(capability == 35 && !capabilities.Features.bSampleRateShading) ||
					(capability == 25 && !capabilities.Features.bShaderImageGatherExtended) ||
					(capability == 49 && !capabilities.Features.bShaderStorageImageExtendedFormats) ||
					(capability == 55 && !capabilities.Features.bShaderStorageImageReadWithoutFormat) ||
					(capability == 56 && !capabilities.Features.bShaderStorageImageWriteWithoutFormat))
				{
					return fail(EErrorCode::Unsupported);
				}
			}
			offset += length;
		}
		if (requiredSubgroupOperations != 0)
		{
			if (bOtherEntry || bComputeEntry == bFragmentEntry || (bFragmentEntry && (requiredSubgroupOperations & ~129u) != 0))
			{
				return fail(EErrorCode::Unsupported);
			}
			const uint32 operations = static_cast<uint32>(bComputeEntry ? capabilities.Features.ComputeSubgroupOperations : capabilities.Features.PixelSubgroupOperations);
			if ((requiredSubgroupOperations & ~operations) != 0)
			{
				return fail(EErrorCode::Unsupported);
			}
		}

		return R::Success();
	}
}
