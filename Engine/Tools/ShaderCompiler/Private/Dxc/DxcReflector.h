#pragma once

#include "../Dxil/DxcPointer.hpp"
#include "../ShaderCompilerInternal.h"

#include <Cancellation.h>

#if defined(_WIN32)
namespace rox::shader
{
	struct TypeShape
	{
		core::uint64 Extent = 0;
		core::uint64 ElementExtent = 0;
		core::uint64 ArrayStride = 0;
		core::uint64 MatrixStride = 0;
		core::uint32 Alignment = 1;
		core::uint32 ScalarWidth = 0;
		shader::EShaderScalar Scalar = shader::EShaderScalar::None;
	};

	class DxcReflector
	{
	public:
		DxcReflector(shader::ShaderVariant& variant, core::MemoryResource& resource, core::CancellationToken cancellation);
		core::Result<void> Reflect(ID3D12ShaderReflection& reflection, shader::ShaderEntryPoint& entry);

	private:
		core::Result<TypeShape> reflectShapeRecursive(ID3D12ShaderReflectionType* type, bool bConstant, core::uint32 depth);
		core::Result<void> reflectFieldRecursive(ID3D12ShaderReflectionType* type, shader::ShaderDataField& field, core::uint64 reflectedSize, bool bConstant, core::uint32 depth);
		core::Result<core::uint32> reflectStructLayoutRecursive(ID3D12ShaderReflectionType* type, core::uint64 extent, bool bConstant, core::uint32 depth);
		core::Result<core::uint32> reflectBufferLayout(ID3D12ShaderReflectionConstantBuffer* buffer, bool bConstant);

		core::Result<core::uint32> publishLayout(shader::ShaderDataLayout&& layout);

	private:
		shader::ShaderVariant& m_Variant;
		core::MemoryResource& m_Resource;
		core::CancellationToken m_Cancellation;

		core::uint32 m_FieldCount = 0;
	};

	core::Result<core::uint64> ReflectDxilRequirements(ID3D12ShaderReflection& reflection, core::uint32 abi = shader::SHADER_ABI_VERSION);
} // namespace rox::shader
#endif
