#pragma once

#include <Array.hpp>
#include <CoreString.hpp>
#include <Hash.h>
#include <Types.h>

namespace rox::shader
{
	inline constexpr core::uint32 SHADER_ABI_VERSION = 1;

	inline constexpr core::uint32 MAXIMUM_DXIL_LOCAL_ROOT_BYTES = 4064;

	inline constexpr core::uint32 NO_PAYLOAD_TYPE = 0xffffffffu;
	inline constexpr core::uint32 NO_DATA_LAYOUT = 0xffffffffu;
	inline constexpr core::uint32 NO_STAGE_LOCATION = 0xffffffffu;

	enum class EShaderScalar : core::uint32
	{
		None,
		Bool,
		SignedInteger,
		UnsignedInteger,
		Float
	};

	enum class EShaderMatrixOrder : core::uint32
	{
		None,
		RowMajor,
		ColumnMajor
	};

	enum class EShaderPropertyType : core::uint32
	{
		Float,
		Int,
		Bool,
		Vector,
		Color,
		Matrix,
		Texture2D,
		TextureCube,
		Buffer,
		Sampler
	};

	// Numeric values use exact 32-bit lanes; matrices are tightly packed row-major.
	// Resource defaults are empty and are supplied by Material or the caller.
	struct ShaderPropertyValue
	{
		EShaderPropertyType Type = EShaderPropertyType::Float;
		core::uint32 Bits[16] = {};

		static ShaderPropertyValue Float(core::float32) noexcept;
		static ShaderPropertyValue Int(core::int32) noexcept;
		static ShaderPropertyValue Bool(bool) noexcept;
		static ShaderPropertyValue Vector(core::float32 x, core::float32 y, core::float32 z, core::float32 w) noexcept;
		static ShaderPropertyValue Color(core::float32 r, core::float32 g, core::float32 b, core::float32 a = 1) noexcept;
	};

	struct ShaderProperty
	{
		core::String Name;
		core::String DisplayName;

		ShaderPropertyValue DefaultValue;

		bool bHasRange = false;
		core::float32 Minimum = 0;
		core::float32 Maximum = 0;

		explicit ShaderProperty(core::MemoryResource& memory)
			: Name(memory)
			, DisplayName(memory)
		{
		}

		core::StringView GetName() const noexcept { return Name; }
		EShaderPropertyType GetType() const noexcept { return DefaultValue.Type; }
		core::Hash256 GetID() const noexcept;
	};

	struct ShaderPropertyHandle
	{
		core::Hash256 ShaderIdentity;
		core::uint32 Index = 0;
	};

} // namespace rox::shader
