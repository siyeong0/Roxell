#pragma once

#include "../DeviceCommands.h"
#include <Array.hpp>

namespace rox::rhi
{
	class RayLocalRootRecipe final
	{
	public:
		explicit RayLocalRootRecipe(core::MemoryResource&) noexcept;

		[[nodiscard]] static core::Result<RayLocalRootRecipe> Create(
			core::Span<const BindingLayoutHandle>,
			core::Span<const RayLocalRootArgumentDesc>,
			core::Span<const NativeBindingLayoutDesc>,
			core::MemoryResource&);

		[[nodiscard]] core::Span<const BindingLayoutHandle> GetLayouts() const noexcept;
		[[nodiscard]] core::Span<const NativeRayLocalRootArgument> GetArguments() const noexcept;
		[[nodiscard]] core::uint32 GetByteSize() const noexcept;

	private:
		core::Array<BindingLayoutHandle> m_Layouts;
		core::Array<NativeRayLocalRootArgument> m_Arguments;
		core::uint32 m_ByteSize = 0;
	};
}
