#pragma once

#include <ComparisonTypes.hpp>
#include <TypeTraits.hpp>
#include <Types.h>

#include <Vocabulary.hpp>

namespace rox::os
{
	enum class ENativeWindowHandleKind : core::uint8
	{
		Unknown,
		Win32,
		X11,
		Wayland
	};

	struct NativeWindowHandle final
	{
		ENativeWindowHandleKind Kind = ENativeWindowHandleKind::Unknown;
		core::uintptr Connection = 0;
		core::uintptr Window = 0;

		[[nodiscard]] static constexpr NativeWindowHandle FromWin32(const core::uintptr window) noexcept
		{
			return { ENativeWindowHandleKind::Win32, 0, window };
		}

		[[nodiscard]] static constexpr NativeWindowHandle FromX11(const core::uintptr display, const core::uintptr window) noexcept
		{
			return { ENativeWindowHandleKind::X11, display, window };
		}

		[[nodiscard]] static constexpr NativeWindowHandle FromWayland(const core::uintptr display, const core::uintptr surface) noexcept
		{
			return { ENativeWindowHandleKind::Wayland, display, surface };
		}

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			switch (Kind)
			{
			case ENativeWindowHandleKind::Win32:
				return Connection == 0 && Window != 0;
			case ENativeWindowHandleKind::X11:
			case ENativeWindowHandleKind::Wayland:
				return Connection != 0 && Window != 0;
			default:
				return false;
			}
		}

		core::StrongOrdering operator<=>(const NativeWindowHandle&) const = default;
	};

	static_assert(rox::core::IsStandardLayout<NativeWindowHandle>);
	static_assert(rox::core::IsTriviallyCopyable<NativeWindowHandle>);
} // namespace rox::os
