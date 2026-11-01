#include <ShaderProperty.h>

#include <ByteOperations.hpp>

namespace rox::shader
{
	using namespace core;

	ShaderPropertyValue ShaderPropertyValue::Float(float32 bValue) noexcept
	{
		ShaderPropertyValue result;
		CopyBytes(result.Bits, &bValue, 4);
		return result;
	}

	ShaderPropertyValue ShaderPropertyValue::Int(int32 bValue) noexcept
	{
		ShaderPropertyValue result;
		result.Type = EShaderPropertyType::Int;
		result.Bits[0] = static_cast<uint32>(bValue);
		return result;
	}

	ShaderPropertyValue ShaderPropertyValue::Bool(bool bValue) noexcept
	{
		ShaderPropertyValue result;
		result.Type = EShaderPropertyType::Bool;
		result.Bits[0] = bValue ? 1 : 0;
		return result;
	}

	ShaderPropertyValue ShaderPropertyValue::Vector(float32 x, float32 y, float32 z, float32 w) noexcept
	{
		ShaderPropertyValue result;
		result.Type = EShaderPropertyType::Vector;
		const float32 lanes[] = {x, y, z, w};
		CopyBytes(result.Bits, lanes, sizeof(lanes));
		return result;
	}

	ShaderPropertyValue ShaderPropertyValue::Color(float32 r, float32 g, float32 b, float32 a) noexcept
	{
		ShaderPropertyValue result = Vector(r, g, b, a);
		result.Type = EShaderPropertyType::Color;
		return result;
	}

	Hash256 ShaderProperty::GetID() const noexcept
	{
		return ComputeSha256({reinterpret_cast<const Byte*>(Name.Data()), Name.Size()});
	}
} // namespace rox::shader
