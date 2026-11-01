#include "AccessibilityInternal.h"
#include <ClockPrimitives.hpp>
#include <Synchronization.hpp>
#include <Vocabulary.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <Numeric.hpp>
#include <Utility.hpp>
#include <SystemMemoryResource.h>
#include <Utf8.h>

namespace rox::os
{
	AccessibilityRequest::AccessibilityRequest() noexcept = default;
	AccessibilityRequest::~AccessibilityRequest() = default;
	AccessibilityRequest::AccessibilityRequest(const AccessibilityRequest&) noexcept = default;
	AccessibilityRequest& AccessibilityRequest::operator=(const AccessibilityRequest&) noexcept = default;
	AccessibilityRequest::AccessibilityRequest(AccessibilityRequest&&) noexcept = default;
	AccessibilityRequest& AccessibilityRequest::operator=(AccessibilityRequest&&) noexcept = default;
	core::Result<AccessibilitySnapshot> AccessibilitySnapshot::Create(core::uint64 publication, core::Array<AccessibilityNode>&& nodes)
	{
		using R = core::Result<AccessibilitySnapshot>;
		core::uint64 bytes = 0;
		struct IndexedId
		{
			AccessibilityId Id;
			rox::core::usize Index;
		};

		core::Array<IndexedId> identities(core::SystemMemoryResource::Get());

		if (!publication || nodes.Size() > 65536)
		{
			return R::Failure(core::EErrorCode::InvalidArgument, "Platform.Accessibility", "Invalid accessibility publication or node count.");
		}

		if (!identities.Reserve(nodes.Size()))
		{
			return R::Failure(core::EErrorCode::ExternalFailure, "Platform.Accessibility", "Accessibility index allocation failed.");
		}
		for (rox::core::usize index = 0; index < nodes.Size(); ++index)
		{
			const AccessibilityNode& node = nodes[index];
			identities.AddRequired({ node.Id, index });
			bytes += sizeof(node) + node.Name.Size() + node.Value.Size();

			if (!node.Id.IsValid() ||
			static_cast<unsigned>(node.Role) > static_cast<unsigned>(EAccessibilityRole::TreeItem) ||
				!core::IsValidUtf8(node.Name.View()) ||
				!core::IsValidUtf8(node.Value.View()) ||
				bytes > 8 * 1024 * 1024 ||
				!rox::core::IsFinite(node.Bounds.X) ||
				!rox::core::IsFinite(node.Bounds.Y) ||
				!rox::core::IsFinite(node.Bounds.Width) ||
				!rox::core::IsFinite(node.Bounds.Height) ||
				node.Bounds.Width < 0 || node.Bounds.Height < 0 ||
				rox::core::Abs(node.Bounds.X) > 1e9 ||
				rox::core::Abs(node.Bounds.Y) > 1e9 ||
				node.Bounds.Width > 1e9 ||
				node.Bounds.Height > 1e9 ||
				!rox::core::IsFinite(node.VerticalScrollPercent) ||
				!rox::core::IsFinite(node.VerticalViewSize) ||
				node.VerticalScrollPercent < 0 ||
				node.VerticalScrollPercent > 100 ||
				node.VerticalViewSize < 0 ||
				node.VerticalViewSize > 100 ||
				node.SelectionStart > node.SelectionEnd ||
				node.SelectionEnd > node.Value.Size() ||
				node.ToggleState > 2 ||
				(node.SelectionStart < node.Value.Size() && (static_cast<unsigned char>(node.Value[node.SelectionStart]) & 0xc0) == 0x80) ||
				(node.SelectionEnd < node.Value.Size() && (static_cast<unsigned char>(node.Value[node.SelectionEnd]) & 0xc0) == 0x80) ||
				(node.bPassword && (!node.Value.IsEmpty() || node.Geometry || node.SelectionStart || node.SelectionEnd)))
			{
				return R::Failure(core::EErrorCode::InvalidArgument, "Platform.Accessibility", "Invalid, over-budget or password-bearing accessibility node.");
			}
		}

		const auto key = [](AccessibilityId id) noexcept
		{
			return rox::core::Tuple{ id.Owner, id.Slot, id.Generation, id.Item };
		};
		rox::core::Sort(identities.begin(), identities.end(), [&](const IndexedId& first, const IndexedId& second) noexcept
		{
			return key(first.Id) < key(second.Id);
		});
		core::uint32 roots = 0;

		for (rox::core::usize index = 0; index < identities.Size(); ++index)
		{
			const IndexedId& item = identities[index];
			const AccessibilityId parent = nodes[item.Index].Parent;

			if (index && identities[index - 1].Id == item.Id)
			{
				return R::Failure(core::EErrorCode::InvalidArgument, "Platform.Accessibility", "Duplicate accessibility identity.");
			}

			if (!parent.IsValid())
			{
				if (parent != AccessibilityId{} || ++roots > 1)
				{
					return R::Failure(core::EErrorCode::InvalidArgument, "Platform.Accessibility", "Accessibility tree requires one valid root.");
				}
				continue;
			}

			const IndexedId* const found = rox::core::LowerBound(identities.begin(), identities.end(), parent,
				[&](const IndexedId& candidate, AccessibilityId value) noexcept
				{
					return key(candidate.Id) < key(value);
				});

			if (found == identities.end() || found->Id != parent || found->Index >= item.Index)
			{
				return R::Failure(core::EErrorCode::InvalidArgument, "Platform.Accessibility", "Accessibility parents must precede children without cycles.");
			}
		}

		AccessibilitySnapshot result;

		if (!core::SharedPtr<AccessibilityData>::CreateInto(core::SystemMemoryResource::Get(), &result.m_Data, rox::core::Move(nodes)))
		{
			return R::Failure(core::EErrorCode::ExternalFailure, "Platform.Accessibility", "Accessibility publication allocation failed.");
		}
		result.m_Data->Publication = publication;

		return R::Success(rox::core::Move(result));
	}

	const AccessibilityNode* AccessibilitySnapshot::Find(AccessibilityId id) const noexcept
	{
		for (const AccessibilityNode& node : GetNodes())
		{
			if (node.Id == id)
			{
				return &node;
			}
		}

		return nullptr;
	}

	const AccessibilityAction& AccessibilityRequest::GetAction() const noexcept
	{
		ASSERT(m_State, "Accessibility request must be valid.");

		return m_State->Action;
	}

	bool AccessibilityRequest::TryBegin() noexcept
	{
		if (!m_State)
		{
			return false;
		}

		rox::core::LockGuard lock(m_State->Mutex);

		if (m_State->bComplete || m_State->bStarted)
		{
			return false;
		}

		if (rox::core::MonotonicClock::now() >= m_State->Deadline)
		{
			m_State->Error = core::EErrorCode::Timeout;
			m_State->bComplete = true;
			m_State->Changed.notify_all();

			return false;
		}
		m_State->bStarted = true;

		return true;
	}

	void AccessibilityRequest::Complete(core::EErrorCode error) noexcept
	{
		if (!m_State)
		{
			return;
		}

		rox::core::LockGuard lock(m_State->Mutex);

		if (!m_State->bComplete)
		{
			if (error == core::EErrorCode::None && m_State->bStarted && m_State->OnSuccess)
			{
				m_State->OnSuccess(m_State->SuccessContext);
			}
			m_State->Error = error;
			m_State->bComplete = true;
			m_State->Changed.notify_all();
		}
	}
}
