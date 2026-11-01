#pragma once

#include <ShaderBuilder.h>

namespace rox::shader
{
	class ShaderPayloadCanonicalizer final
	{
	public:
		ShaderPayloadCanonicalizer(const ShaderTaskPayloadSignature&, core::MemoryResource&) noexcept;
		ShaderPayloadCanonicalizer(core::Span<const ShaderPayloadType>, core::uint32 root, core::MemoryResource&) noexcept;

		core::Result<void> ValidateStructure(const ShaderLimits&);
		core::uint32 GetMinimumScalarBytes() const noexcept;

		core::Result<void> CanonicalizeStructure(core::Array<ShaderPayloadType>&, core::uint32&);
		static bool IsCanonicalStructure(core::Span<const ShaderPayloadType>, core::uint32) noexcept;

		core::Result<void> Validate(const ShaderLimits&);

		core::Result<ShaderTaskPayloadSignature> Canonicalize();
		static bool IsCanonical(const ShaderTaskPayloadSignature&) noexcept;

		static bool Equal(const ShaderTaskPayloadSignature&, const ShaderTaskPayloadSignature&) noexcept;

	private:
		core::Result<void> validateTypeRecursive(core::uint32 index, core::uint32 maximumDepth);

		core::Result<core::uint32> canonicalTypeRecursive(core::uint32 index);

		static bool typesEqual(const ShaderPayloadType&, const ShaderPayloadType&) noexcept;

	private:
		const ShaderTaskPayloadSignature* m_TaskSignature = nullptr;
		core::Span<const ShaderPayloadType> m_Types;
		core::uint32 m_RootType = NO_PAYLOAD_TYPE;

		core::Array<ShaderPayloadType> m_CanonicalTypes;

		core::uint32 m_Components[128]{};
		core::uint32 m_Depths[128]{};
		core::uint32 m_Map[128]{};
		core::uint8 m_States[128]{};
	};
} // namespace rox::shader
