#pragma once

#ifdef _WIN32
#include <RhiFormat.h>
#include <dxgiformat.h>

namespace rox::rhi
{
	struct DxgiFormatMapping final
	{
		EFormat AbstractFormat;
		DXGI_FORMAT ResourceFormat;
		DXGI_FORMAT SrvFormat;
		DXGI_FORMAT RtvFormat;
	};

	[[nodiscard]] const DxgiFormatMapping& GetDxgiFormatMapping(EFormat format) noexcept;
}
#endif
