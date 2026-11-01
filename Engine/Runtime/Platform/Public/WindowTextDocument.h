#pragma once

#include <InterfaceOwner.hpp>
#include <Result.hpp>
#include <SharedPtr.hpp>
#include <StringView.hpp>
#include <Utility.hpp>

namespace rox::os
{
	struct WindowTextRect final
	{
		core::int32 X = 0;
		core::int32 Y = 0;

		core::int32 Width = 0;
		core::int32 Height = 0;

		bool bClipped = false;
	};

	struct WindowTextHit final
	{
		core::uint32 CharacterByte = 0;
		core::uint32 NearestByte = 0;

		bool bInside = false;
	};
	// Immutable retained geometry, queried on the publishing window's owner thread only.
	// Coordinates are physical client pixels; Platform owns conversion to screen coordinates.
	class IWindowTextGeometry
	{
	public:
		virtual ~IWindowTextGeometry() = default;
		virtual core::Result<WindowTextRect> GetExtent(core::uint32 firstByte, core::uint32 lastByte) const = 0;
		virtual core::Result<WindowTextHit> HitTest(core::int32 clientX, core::int32 clientY) const = 0;
	};

	struct WindowTextGeometry final
	{
		explicit WindowTextGeometry(core::InterfaceOwner<IWindowTextGeometry>&& native) noexcept
			: Native(rox::core::Move(native))
		{
		}

		core::InterfaceOwner<IWindowTextGeometry> Native;
	};

	struct WindowTextDocument final
	{
		core::uint64 Session = 0;
		core::uint64 Revision = 0;

		core::StringView Text;

		core::uint32 AnchorByte = 0;
		core::uint32 CaretByte = 0;

		bool bReadOnly = false;
		bool bPassword = false;
		bool bMultiline = false;

		core::SharedPtr<const WindowTextGeometry> Geometry;
	};
}
