#pragma once
#include <ComparisonTypes.hpp>
#include <Types.h>
namespace rox::rhi
{
	inline constexpr core::uint32 MAXIMUM_FIXED_BINDING_LAYOUTS = 8;
	inline constexpr core::uint32 MAXIMUM_BINDING_RANGES_PER_LAYOUT = 16;

	enum class EBackend : core::uint8
	{
		Null,
		D3D12,
		Vulkan
	};

	enum class EQueue : core::uint8
	{
		Graphics,
		Compute,
		Copy
	};

	struct NativeQueueAccessHandle final
	{
		core::uint64 Owner = 0;
		core::uint64 Generation = 0;
		EQueue Queue = EQueue::Graphics;

		[[nodiscard]] bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const NativeQueueAccessHandle&) const = default;
	};

	struct BufferHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const BufferHandle&) const = default;
	};

	struct OpacityMicromapHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const OpacityMicromapHandle&) const = default;
	};

	struct AccelStructHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const AccelStructHandle&) const = default;
	};

	struct HeapHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const HeapHandle&) const = default;
	};

	struct TextureHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const TextureHandle&) const = default;
	};

	struct PreparedTextureMappingsHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const PreparedTextureMappingsHandle&) const = default;
	};

	struct SamplerFeedbackHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		[[nodiscard]] constexpr TextureHandle AsStorageTexture() const noexcept { return { Owner, Slot, Generation }; }

		core::StrongOrdering operator<=>(const SamplerFeedbackHandle&) const = default;
	};

	struct StagingTextureHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const StagingTextureHandle&) const = default;
	};

	struct TextureViewHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const TextureViewHandle&) const = default;
	};

	struct BufferViewHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const BufferViewHandle&) const = default;
	};

	struct SamplerHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const SamplerHandle&) const = default;
	};

	struct FramebufferHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const FramebufferHandle&) const = default;
	};

	struct SwapChainHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const SwapChainHandle&) const = default;
	};

	struct CommandListHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const CommandListHandle&) const = default;
	};

	struct ComputePipelineHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const ComputePipelineHandle&) const = default;
	};

	struct ShaderHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const ShaderHandle&) const = default;
	};

	struct ShaderLibraryHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const ShaderLibraryHandle&) const = default;
	};

	struct GraphicsPipelineHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const GraphicsPipelineHandle&) const = default;
	};

	struct InputLayoutHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const InputLayoutHandle&) const = default;
	};

	struct RayTracingPipelineHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const RayTracingPipelineHandle&) const = default;
	};

	struct ShaderTableHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const ShaderTableHandle&) const = default;
	};

	struct BindingSetHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const BindingSetHandle&) const = default;
	};

	struct BindingLayoutHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const BindingLayoutHandle&) const = default;
	};

	struct DescriptorTableHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const DescriptorTableHandle&) const = default;
	};

	struct SubmissionToken final
	{
		core::uint64 Owner = 0;
		EQueue Queue = EQueue::Copy;
		core::uint64 Value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Value != 0; }

		bool operator==(const SubmissionToken&) const = default;
	};

	struct GpuTimerHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const GpuTimerHandle&) const = default;
	};

	struct EventQueryHandle final
	{
		core::uint64 Owner = 0;
		core::uint32 Slot = 0;
		core::uint32 Generation = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return Owner != 0 && Generation != 0; }

		core::StrongOrdering operator<=>(const EventQueryHandle&) const = default;
	};
}
