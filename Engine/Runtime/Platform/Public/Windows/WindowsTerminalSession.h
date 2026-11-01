#pragma once

#include <ITerminalSession.h>
#include <InterfaceOwner.hpp>

namespace rox::os
{
	[[nodiscard]] core::Result<core::InterfaceOwner<ITerminalSession>> CreateWindowsTerminalSession(const TerminalDesc& desc, core::MemoryResource& memory);

	struct WindowsTerminalUnicodeVersion final
	{
		core::uint32 Major = 0;
		core::uint32 Minor = 0;
		core::uint32 Update = 0;
		core::uint32 Revision = 0;
	};

	[[nodiscard]] WindowsTerminalUnicodeVersion GetWindowsTerminalUnicodeVersion() noexcept;
	// OS ICU Unicode properties: W/F=2, Mn/Me/default-ignorable=0, other scalars=1.
	// Ambiguous width is narrow. Grapheme/emoji sequences require a higher-level policy.
	[[nodiscard]] core::uint32 WindowsTerminalCellWidth(core::uint32 scalar) noexcept;
}
