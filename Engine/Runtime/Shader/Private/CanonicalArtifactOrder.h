#pragma once

#include <ShaderBuilder.h>

namespace rox::shader
{
	struct CanonicalVariantOrder
	{
		core::Array<core::uint32> Layouts;
		core::Array<core::uint32> LayoutMap;

		core::Array<core::uint32> Bindings;
		core::Array<core::uint32> BindingMap;

		core::Array<core::uint32> Uses;

		explicit CanonicalVariantOrder(core::MemoryResource& resource) noexcept
			: Layouts(resource)
			, LayoutMap(resource)
			, Bindings(resource)
			, BindingMap(resource)
			, Uses(resource)
		{
		}
	};

	// Initialize from a validated artifact; indices describe its original record order.
	class CanonicalArtifactOrder
	{
	public:
		explicit CanonicalArtifactOrder(core::MemoryResource& resource) noexcept;
		core::Result<void> Initialize(const ShaderBuilder& artifact, core::CancellationToken cancellation);

		const core::Array<core::uint32>& GetEntries() const noexcept { return m_Entries; }
		const core::Array<core::uint32>& GetEntryMap() const noexcept { return m_EntryMap; }
		const core::Array<core::uint32>& GetVariants() const noexcept { return m_Variants; }
		const CanonicalVariantOrder& GetVariantOrder(core::uint32 index) const noexcept { return m_VariantOrders[index]; }

		bool IsCanonical(const ShaderBuilder& artifact) const noexcept;

	private:
		core::Array<core::uint32> m_Entries;
		core::Array<core::uint32> m_EntryMap;

		core::Array<core::uint32> m_Variants;
		core::Array<CanonicalVariantOrder> m_VariantOrders;
	};
} // namespace rox::shader
