#pragma once

#include <Result.hpp>
#include <ShaderProperty.h>
#include <SharedPtr.hpp>

namespace rox::shader
{
	enum class EShaderFormat : core::uint32
	{
		V1 = 1
	};

	enum class EShaderFeature : core::uint64
	{
		None = 0,
		Float16 = 1,
		Float64 = 2,
		Int64 = 4,
		Atomic = 8,
		Geometry = 16,
		Tessellation = 32,
		Mesh = 64,
		RayTracing = 128,
		DrawParameters = 256,
		Subgroup = 512,
		SubgroupQuad = 1024,
		NonUniformIndexing = 2048,
		RuntimeDescriptorArray = 4096,
		PartiallyBoundDescriptors = 8192,
		Int64Atomics = 16384,
		Task = 32768,
		ResourceDescriptorHeap = 65536,
		SamplerDescriptorHeap = 131072,
		UniformBufferArrayDynamicIndexing = 262144,
		SampledImageArrayDynamicIndexing = 524288,
		StorageBufferArrayDynamicIndexing = 1048576,
		StorageImageArrayDynamicIndexing = 2097152,
		DemoteToHelperInvocation = 4194304,
		SampleRateShading = 8388608,
		// DXIL's shader-wide additional typed UAV load format requirement.
		TypedUavLoadAdditionalFormats = 16777216,
		// DXIL minimum arithmetic precision permits full-width execution; it is not native Float16.
		MinimumPrecision = 33554432,
		// SPIR-V ImageGatherExtended requires enabled Vulkan shaderImageGatherExtended.
		ImageGatherExtended = 67108864,
		StorageImageReadWithoutFormat = 134217728,
		StorageImageWriteWithoutFormat = 268435456,
		// Exact SPIR-V compute subgroup operation families, in addition to Subgroup.
		SubgroupVote = 536870912,
		SubgroupArithmetic = 1073741824,
		SubgroupBallot = 2147483648,
		SubgroupShuffle = 4294967296,
		SubgroupShuffleRelative = 8589934592,
		SubgroupClustered = 17179869184,
		// SPIR-V capability49 requires the selected Vulkan extended storage-format contract.
		StorageImageExtendedFormats = 34359738368
	};

	struct ShaderFeatureRequirements
	{
		core::uint32 Version = 1;
		core::uint64 Features = 0;

		core::uint32 MinimumSubgroupSize = 0;
		core::uint32 MaximumSubgroupSize = 0;

		core::uint32 MinimumPushConstantBytes = 0;
		core::uint32 MinimumWorkgroupInvocations = 0;

		auto operator<=>(const ShaderFeatureRequirements&) const = default;
	};

	enum class EShaderCapability : core::uint64
	{
		None = 0,
		Float16 = 1,
		Float64 = 2,
		Int64 = 4,
		Atomic = 8,
		Geometry = 16,
		Tessellation = 32,
		Mesh = 64,
		RayTracing = 128,
		DrawParameters = 256
	};

	enum class EShaderTarget : core::uint32
	{
		Dxil,
		SpirV
	};

	struct ShaderTargetProfile
	{
		EShaderTarget Target = EShaderTarget::Dxil;

		core::uint32 ShaderModelMajor = 6;
		core::uint32 ShaderModelMinor = 6;

		core::uint32 SpirVMajor = 1;
		core::uint32 SpirVMinor = 6;

		core::uint32 EnvironmentMajor = 1;
		core::uint32 EnvironmentMinor = 3;

		core::uint32 ProfileVersion = 1;

		auto operator<=>(const ShaderTargetProfile&) const = default;
	};

	enum class EShaderSpecializationPhase : core::uint32
	{
		BuildTime,
		PipelineTime
	};

	struct ShaderSpecializationParameter
	{
		core::uint32 ID = 0;
		core::String Name;

		EShaderScalar Scalar = EShaderScalar::UnsignedInteger;
		core::uint32 ScalarWidth = 32;
		EShaderSpecializationPhase Phase = EShaderSpecializationPhase::PipelineTime;

		core::uint64 DefaultBits = 0;
		core::uint64 MinimumBits = 0;
		core::uint64 MaximumBits = 0;
		bool bHasDefault = false;
		bool bHasRange = false;

		bool bAffectsLayout = false;
		core::uint32 WorkgroupAxis = NO_STAGE_LOCATION;

		explicit ShaderSpecializationParameter(core::MemoryResource& resource) noexcept
			: Name(resource)
		{
		}

		ShaderSpecializationParameter(ShaderSpecializationParameter&&) noexcept = default;
		ShaderSpecializationParameter& operator=(ShaderSpecializationParameter&&) noexcept = default;

		ShaderSpecializationParameter(const ShaderSpecializationParameter&) = delete;
		ShaderSpecializationParameter& operator=(const ShaderSpecializationParameter&) = delete;
	};

	struct ShaderSpecializationValue
	{
		core::uint32 ID = 0;

		EShaderScalar Scalar = EShaderScalar::UnsignedInteger;
		core::uint32 ScalarWidth = 32;
		core::uint64 Bits = 0;
	};

	class ShaderLayout;
	// Owns canonical values and requirements; native application remains the caller's pipeline
	// responsibility.
	class ShaderPipelineConstants
	{
	private:
		struct Storage;

	public:
		[[nodiscard]] static core::Result<ShaderPipelineConstants> Resolve(const ShaderLayout&, core::Span<const ShaderSpecializationValue>, core::MemoryResource&);

		ShaderPipelineConstants() noexcept;
		~ShaderPipelineConstants();
		ShaderPipelineConstants(const ShaderPipelineConstants&) noexcept;
		ShaderPipelineConstants(ShaderPipelineConstants&&) noexcept;
		ShaderPipelineConstants& operator=(const ShaderPipelineConstants&) noexcept;
		ShaderPipelineConstants& operator=(ShaderPipelineConstants&&) noexcept;

		bool IsValid() const noexcept;
		const ShaderLayout& GetLayout() const noexcept;
		core::Span<const ShaderSpecializationValue> GetValues() const noexcept;
		core::Hash256 GetIdentity() const noexcept;
		core::Span<const core::uint32> GetThreadGroupSize() const noexcept;
		ShaderFeatureRequirements GetFeatureRequirements() const noexcept;

	private:
		core::SharedPtr<Storage> m_Storage;
	};

	// Keywords select existing code. Constants are applied when creating a pipeline.
	class ShaderOptions
	{
	public:
		explicit ShaderOptions(core::MemoryResource& memory)
			: m_Keywords(memory)
			, m_Constants(memory)
		{
		}

		[[nodiscard]] core::Result<void> SetKeyword(core::StringView name, bool bEnabled = true);
		[[nodiscard]] core::Result<void> SetPipelineConstant(ShaderSpecializationValue value);
		[[nodiscard]] core::Result<void> SetPipelineConstant(core::uint32 id, core::uint32 value);
		[[nodiscard]] core::Result<void> SetPipelineConstant(core::uint32 id, core::int32 value);
		[[nodiscard]] core::Result<void> SetPipelineConstant(core::uint32 id, core::float32 value);
		[[nodiscard]] core::Result<void> SetPipelineConstant(core::uint32 id, bool bValue);
		core::Span<const core::String> GetKeywords() const noexcept { return m_Keywords.View(); }
		core::Span<const ShaderSpecializationValue> GetPipelineConstants() const noexcept { return m_Constants.View(); }

	private:
		core::Array<core::String> m_Keywords;

		core::Array<ShaderSpecializationValue> m_Constants;
	};

} // namespace rox::shader
