#pragma once

#include <Array.hpp>
#include <CoreString.hpp>
#include <InterfaceOwner.hpp>
#include <Result.hpp>
#include <SharedPtr.hpp>
#include <Utility.hpp>

namespace rox::os
{
	struct AccessibilityId
	{
		core::uint64 Owner = 0;
		core::uint64 Slot = 0;
		core::uint64 Generation = 0;
		core::uint64 Item = 0;

		bool IsValid() const noexcept { return Owner && Slot && Generation; }
		friend bool operator==(AccessibilityId, AccessibilityId) noexcept = default;
	};

	struct AccessibilityRect
	{
		core::float64 X = 0;
		core::float64 Y = 0;

		core::float64 Width = 0;
		core::float64 Height = 0;
	};

	enum class EAccessibilityRole
	{
		Group,
		Text,
		Button,
		CheckBox,
		Edit,
		Tree,
		TreeItem
	};

	enum class EAccessibilityAction
	{
		Focus,
		Invoke,
		Toggle,
		SetValue,
		SetSelection,
		Select,
		Expand,
		Collapse,
		Realize,
		ScrollText,
		ScrollTo
	};

	class IAccessibilityTextGeometry
	{
	public:
		virtual ~IAccessibilityTextGeometry() = default;

		// Immutable document bytes; rectangles and points use physical client pixels.
		virtual core::Result<core::Array<AccessibilityRect>> GetBounds(core::uint32 firstByte, core::uint32 lastByte, core::MemoryResource&) const = 0;
		virtual core::Result<core::uint32> HitTest(core::float64 x, core::float64 y) const = 0;
		virtual core::uint32 NextBoundary(core::uint32 byte, bool bForward) const noexcept = 0;
	};

	struct AccessibilityNode
	{
		AccessibilityId Id;
		AccessibilityId Parent;
		EAccessibilityRole Role = EAccessibilityRole::Group;

		core::String Name;
		core::String Value;

		AccessibilityRect Bounds;

		core::uint64 ContentRevision = 0;

		core::uint32 SelectionStart = 0;
		core::uint32 SelectionEnd = 0;

		core::uint32 ToggleState = 0;

		core::float64 VerticalScrollPercent = 0;
		core::float64 VerticalViewSize = 100;

		bool bEnabled = true;
		bool bFocusable = false;
		bool bFocused = false;

		bool bReadOnly = false;
		bool bPassword = false;

		bool bOffscreen = false;

		bool bSelected = false;
		bool bSelectable = false;

		bool bExpandable = false;
		bool bExpanded = false;

		core::InterfaceOwner<IAccessibilityTextGeometry> Geometry;

		explicit AccessibilityNode(core::MemoryResource& memory) noexcept
			: Name(memory)
			, Value(memory)
		{
		}
	};

	struct AccessibilityData
	{
		core::uint64 Publication = 0;
		core::Array<AccessibilityNode> Nodes;

		explicit AccessibilityData(core::Array<AccessibilityNode>&& nodes) noexcept
			: Nodes(rox::core::Move(nodes))
		{
		}
	};

	struct AccessibilityActionState;
	struct WindowsAccessibilityAccess;

	class AccessibilitySnapshot final
	{
	public:
		static core::Result<AccessibilitySnapshot> Create(core::uint64 publication, core::Array<AccessibilityNode>&& nodes);
		core::uint64 GetPublication() const noexcept { return m_Data ? m_Data->Publication : 0; }
		core::Span<const AccessibilityNode> GetNodes() const noexcept
		{
			return m_Data ? m_Data->Nodes.View() : core::Span<const AccessibilityNode>{};
		}

		const AccessibilityNode* Find(AccessibilityId id) const noexcept;

	private:
		core::SharedPtr<AccessibilityData> m_Data;
	};

	struct AccessibilityAction
	{
		AccessibilityId Target;
		core::uint64 Publication = 0;
		EAccessibilityAction Kind = EAccessibilityAction::Focus;
		core::String Value;

		core::uint32 SelectionStart = 0;
		core::uint32 SelectionEnd = 0;

		explicit AccessibilityAction(core::MemoryResource& memory) noexcept
			: Value(memory)
		{
		}
	};

	class AccessibilityRequest final
	{
		friend struct WindowsAccessibilityAccess;
	public:
		AccessibilityRequest() noexcept;
		~AccessibilityRequest();
		AccessibilityRequest(const AccessibilityRequest&) noexcept;
		AccessibilityRequest& operator=(const AccessibilityRequest&) noexcept;
		AccessibilityRequest(AccessibilityRequest&&) noexcept;
		AccessibilityRequest& operator=(AccessibilityRequest&&) noexcept;

		bool IsValid() const noexcept { return static_cast<bool>(m_State); }
		const AccessibilityAction& GetAction() const noexcept;

		// A timed-out or disconnected request cannot begin. The UI owner completes every begun request.
		bool TryBegin() noexcept;
		void Complete(core::EErrorCode error) noexcept;

	private:
		core::SharedPtr<AccessibilityActionState> m_State;
	};
}
