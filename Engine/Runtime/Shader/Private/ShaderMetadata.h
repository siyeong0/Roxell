#pragma once

#include <ShaderBuilder.h>

namespace rox::shader
{
	class Reader;
	class Writer;

	core::Result<void> ValidateVariantMetadata(
		const ShaderVariant&,
		const ShaderEntryPoint&,
		core::uint32 abi,
		const ShaderLimits&,
		core::uint64& totalBytes,
		core::uint64& comparisonBudget);

	core::Result<void> CanonicalizeVariantMetadata(ShaderVariant&, core::MemoryResource&, const core::Array<core::uint32>& layoutMap);
	bool MetadataIsCanonical(const ShaderVariant&) noexcept;

	core::Result<void> WriteVariantMetadata( Writer&, const ShaderVariant&, core::MemoryResource&, const core::Array<core::uint32>* layoutMap = nullptr,
		core::uint32 abi = SHADER_ABI_VERSION,
		const core::Array<core::uint32>* bindingMap = nullptr);

	core::Result<void> ReadVariantMetadata( Reader&, ShaderVariant&, const ShaderLimits&, core::uint64& totalStageIO, core::uint64& totalPushConstants,
		core::uint64& totalSpecializationParameters,
		core::uint32 abi,
	    core::uint64& totalPayloadTypes,
		core::uint64& totalPayloadMembers,
		core::uint64& totalRayDataSignatures);

	bool SpecializationValueValid(const ShaderSpecializationParameter&, core::uint64 bits) noexcept;
	core::Result<ShaderSpecializationParameter> CloneSpecializationParameter(const ShaderSpecializationParameter&, core::MemoryResource&);
	bool SpecializationShapeEqual(const ShaderSpecializationParameter&, const ShaderSpecializationParameter&) noexcept;

	bool StageIOSlotEqual(const ShaderStageIO&, const ShaderStageIO&, EShaderTarget) noexcept;
	bool StageIORepresentationEqual(const ShaderStageIO&, const ShaderStageIO&) noexcept;
} // namespace rox::shader
