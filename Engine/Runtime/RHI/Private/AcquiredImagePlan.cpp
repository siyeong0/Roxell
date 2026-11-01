#include "AcquiredImagePlan.h"
#include <Assertion.h>
#include <Utility.hpp>

namespace rox::rhi
{
	using namespace core;

	AcquiredImagePlan::AcquiredImagePlan(MemoryResource& memory) noexcept
		: m_Entries(memory)
		, m_ListImages(memory)
	{
	}

	AcquiredImagePlan::~AcquiredImagePlan() = default;

	void AcquiredImagePlan::Initialize(const uint32 listCount) noexcept
	{
		ASSERT(m_ListCount == 0 && listCount != 0, "An acquisition plan is initialized once before capture.");
		m_ListCount = listCount;
	}

	Result<void> AcquiredImagePlan::Add(
		TextureHandle image,
		SwapChainHandle owner,
		uint64 ownerGeneration,
		uint64 acquireGeneration,
		uint32 list,
		uint32 phase,
		ETextureState state)
	{
		using R = Result<void>;
		const auto reject = [](EErrorCode code)
			{
				Error error;
				error.Code = code;

				return R::Failure(Move(error));
			};
		ASSERT(image.IsValid() && owner.IsValid() && ownerGeneration && acquireGeneration && list < m_ListCount, "Acquisition planning uses the current owned image and a valid list index.");
		if (state != ETextureState::RenderTarget && state != ETextureState::Present)
		{
			return reject(EErrorCode::Unsupported);
		}
		if (m_Entries.IsEmpty() && (!m_Entries.Reserve(m_ListCount) || !m_ListImages.Resize(m_ListCount)))
		{
			return reject(EErrorCode::ExternalFailure);
		}
		if (m_ListImages[list].IsValid() && m_ListImages[list] != image)
		{
			return reject(EErrorCode::Unsupported);
		}
		for (AcquiredImagePlan::Entry& entry : m_Entries)
		{
			if (entry.Image != image)
			{
				continue;
			}
			ASSERT(entry.SwapChain == owner &&
				entry.OwnerGeneration == ownerGeneration &&
				entry.AcquireGeneration == acquireGeneration &&
				!entry.bRelease &&
				list >= entry.LastList &&
				(list != entry.LastList || phase > entry.LastPhase),
				"RHI binding arguments must match their declared resources and ranges.");
			entry.LastList = list;
			entry.LastPhase = phase;
			entry.bRelease = state == ETextureState::Present;
			m_ListImages[list] = image;

			return R::Success();
		}
		const bool added = m_Entries.Add({ image, owner, ownerGeneration, acquireGeneration, list, list, phase, state == ETextureState::Present });
		ASSERT(added, "One acquired image per list bounds preallocated plan entries.");
		m_ListImages[list] = image;

		return R::Success();
	}

	bool AcquiredImagePlan::HasCompleteReleases() const noexcept
	{
		for (const AcquiredImagePlan::Entry& entry : m_Entries)
		{
			if (!entry.bRelease)
			{
				return false;
			}
		}

		return true;
	}

	const AcquiredImagePlan::Entry* AcquiredImagePlan::GetListImage(uint32 list) const noexcept
	{
		ASSERT(list < m_ListCount, "Prepared list identity is validated before acquisition lookup.");
		if (m_Entries.IsEmpty())
		{
			return nullptr;
		}
		for (const AcquiredImagePlan::Entry& entry : m_Entries)
		{
			if (entry.Image == m_ListImages[list])
			{
				return &entry;
			}
		}

		return nullptr;
	}

	Span<const AcquiredImagePlan::Entry> AcquiredImagePlan::GetEntries() const noexcept
	{
		return m_Entries.View();
	}
} // namespace rox::rhi
