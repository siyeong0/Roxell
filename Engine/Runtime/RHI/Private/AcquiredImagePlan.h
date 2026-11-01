#pragma once
#include <Array.hpp>
#include <Result.hpp>
#include <RhiResourceTypes.h>
#include <RhiTypes.h>

namespace rox::rhi
{
	class AcquiredImagePlan final
	{
	public:
		struct Entry final
		{
			TextureHandle Image;
			SwapChainHandle SwapChain;

			core::uint64 OwnerGeneration = 0;
			core::uint64 AcquireGeneration = 0;

			core::uint32 FirstList = 0;
			core::uint32 LastList = 0;
			core::uint32 LastPhase = 0;

			bool bRelease = false;
		};

		explicit AcquiredImagePlan(core::MemoryResource& memory) noexcept;
		~AcquiredImagePlan();
		AcquiredImagePlan(const AcquiredImagePlan&) = delete;
		AcquiredImagePlan& operator=(const AcquiredImagePlan&) = delete;

		void Initialize(core::uint32 listCount) noexcept;
		[[nodiscard]] core::Result<void> Add(
			TextureHandle image,
			SwapChainHandle owner,
			core::uint64 ownerGeneration,
			core::uint64 acquireGeneration,
			core::uint32 list,
			core::uint32 phase,
			ETextureState state);

		[[nodiscard]] bool HasCompleteReleases() const noexcept;
		[[nodiscard]] const Entry* GetListImage(core::uint32 list) const noexcept;
		[[nodiscard]] core::Span<const Entry> GetEntries() const noexcept;

	private:
		core::uint32 m_ListCount = 0;
		core::Array<Entry> m_Entries;
		core::Array<TextureHandle> m_ListImages;
	};
}
