#pragma once

#include "../ShaderCompilerInternal.h"

namespace rox::shader
{
	struct SpirvDecoration
	{
		core::uint32 Location = shader::NO_STAGE_LOCATION;
		core::uint32 Component = 0;
		core::uint32 Builtin = 0xffffffffu;
		core::uint32 SpecID = 0xffffffffu;
		core::uint32 ByteOffset = 0xffffffffu;
		core::uint32 ArrayStride = 0;
		core::uint32 DescriptorSet = 0xffffffffu;
		core::uint32 DescriptorBinding = 0xffffffffu;
		bool bBlock = false;
		bool bHasSpecID = false;
		bool bHasByteOffset = false;
		bool bHasArrayStride = false;
		bool bHasComponent = false;
		bool bPerPrimitive = false;
		bool bFlat = false;
		bool bNoPerspective = false;
		bool bCentroid = false;
		bool bSample = false;
		bool bUnsupported = false;
	};

	struct SpirvMetadataNode
	{
		core::uint32 Definition = 0;
		core::uint32 NameOffset = 0;
		SpirvDecoration Decoration;
	};

	struct SpirvMemberDecoration
	{
		core::uint32 Type = 0;
		core::uint32 Member = 0;
		core::uint32 NameOffset = 0;
		core::uint32 Kind = 0xffffffffu;
		SpirvDecoration Decoration;
	};

	class SpirvMetadataReader final
	{
	public:
		SpirvMetadataReader(core::MemoryResource&, core::CancellationToken);
		core::Result<void> Reflect(shader::ShaderVariant&, shader::ShaderEntryPoint&, core::uint32 abi = shader::SHADER_ABI_VERSION);

	private:
		core::uint32 word(core::uint32 offset) const noexcept;
		core::StringView text(core::uint32 offset, core::uint32 end) const noexcept;
		bool decorate(SpirvDecoration&, core::uint32 kind, core::uint32 value);
		core::Result<void> indexInstructions(const shader::ShaderVariant&, const shader::ShaderEntryPoint&);
		core::Result<void> reflectType(shader::ShaderVariant&, core::uint32 type, SpirvDecoration, core::StringView name, bool bInput, core::uint32 depth);
		core::Result<void> reflectSpecializations(shader::ShaderVariant&, shader::ShaderEntryPoint&);
		core::Result<void> reflectRay(shader::ShaderVariant&);
		core::Result<void> verifyDescriptorCounts(shader::ShaderVariant&);
		core::Result<core::uint32> reflectPayloadTypeRecursive(core::Array<shader::ShaderPayloadType>&, core::uint32 id, core::uint32 depth);
		bool verifyPushLayout(const shader::ShaderVariant&, core::uint32 type, core::uint32 layout, core::uint32 depth, bool bRecord = false);
		bool verifyPushField(const shader::ShaderVariant&, core::uint32 type, const shader::ShaderDataField&, core::uint32 depth, bool bRecord = false);

	private:
		core::MemoryResource& m_Resource;
		core::CancellationToken m_Cancellation;

		core::Span<const core::Byte> m_Code;
		core::Array<SpirvMetadataNode> m_Nodes;
		core::Array<SpirvMemberDecoration> m_Members;
		core::Array<core::uint32> m_Interfaces;

		shader::EShaderStage m_Stage = shader::EShaderStage::Compute;
		shader::ShaderMeshOutput m_MeshOutput;
		bool m_bTopology = false;

		core::uint32 m_PayloadIDs[128]{};
		core::uint8 m_PayloadStates[128]{};
		core::uint32 m_PayloadTypeIndices[128]{};
		core::uint32 m_PayloadIDCount = 0;

		core::uint32 m_WordCount = 0;
		core::uint32 m_EntryID = 0;

		bool m_bWorkgroupVerified = false;
		bool m_bDepthReplacing = false;
		core::uint32 m_ConservativeDepthMode = 0;
		core::uint32 m_WorkgroupIDs[3]{};

		core::uint64 m_TraversalBudget = 65536;
		core::uint64 m_AdditionalFeatures = 0;
	};

} // namespace rox::shader
