#pragma once

#include "../ShaderCompilerInternal.h"

#include <Cancellation.h>

#include <slang.h>

namespace rox::shader
{
	class SlangReflector
	{
	public:
		SlangReflector(
			shader::ShaderVariant& variant,
			core::MemoryResource& resource,
			slang::ISession& session,
			SlangInt target,
			core::CancellationToken cancellation,
			core::uint32 abi = shader::SHADER_ABI_VERSION);
		core::Result<void> Binding(slang::VariableLayoutReflection* variable);
		core::Result<void> RayParameters(slang::EntryPointReflection* entry);
		core::Result<void> MeshPayload(slang::EntryPointReflection* entry);
		core::Result<bool> NativeParameter(slang::VariableLayoutReflection*, shader::EShaderStage, core::uint32 abi);

	private:
		core::Result<core::uint32> reflectPayloadTypeRecursive(core::Array<shader::ShaderPayloadType>& types, slang::TypeReflection* type, core::uint32 depth);
		core::Result<void> reflectFieldRecursive(slang::TypeLayoutReflection* type, shader::ShaderDataField& field, core::uint32 depth);
		core::Result<core::uint32> reflectLayoutRecursive(slang::TypeLayoutReflection* type, core::uint32 depth = 0);

	private:
		shader::ShaderVariant& m_Variant;
		core::MemoryResource& m_Resource;
		slang::ISession& m_Session;
		SlangInt m_Target;
		core::CancellationToken m_Cancellation;
	};

} // namespace rox::shader
