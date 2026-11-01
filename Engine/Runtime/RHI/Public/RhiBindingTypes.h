#pragma once
#include <RhiFormat.h>
#include <RhiResourceTypes.h>
#include <RhiTypes.h>
#include <Span.hpp>
#include <Types.h>
#include <Vocabulary.hpp>
namespace rox::rhi
{
	struct DescriptorHeapBinding final
	{
		DescriptorTableHandle Table;
		core::Span<const core::uint32> UsedEntries;
	};

	enum class EDescriptorValueKind : core::uint8
	{
		Invalid,
		Buffer,
		BufferView,
		SampledTexture,
		SampledView,
		StorageTexture,
		StorageView,
		Sampler,
		AccelerationStructure
	};

	// Only factories can set the active kind and payload.
	class DescriptorValue final
	{
	public:
		DescriptorValue() noexcept = default;
		[[nodiscard]] static DescriptorValue Buffer(BufferHandle buffer, BufferRange range = {}) noexcept;
		[[nodiscard]] static DescriptorValue BufferView(BufferViewHandle view) noexcept;
		[[nodiscard]] static DescriptorValue SampledTexture(TextureHandle texture) noexcept;
		[[nodiscard]] static DescriptorValue SampledView(TextureViewHandle view) noexcept;
		[[nodiscard]] static DescriptorValue StorageTexture(TextureHandle texture) noexcept;
		[[nodiscard]] static DescriptorValue StorageView(TextureViewHandle view) noexcept;
		[[nodiscard]] static DescriptorValue Sampler(SamplerHandle sampler) noexcept;
		[[nodiscard]] static DescriptorValue AccelerationStructure(AccelStructHandle accelerationStructure) noexcept;

		[[nodiscard]] EDescriptorValueKind GetKind() const noexcept { return m_Kind; }
		[[nodiscard]] BufferHandle GetBuffer() const noexcept;
		[[nodiscard]] BufferRange GetBufferRange() const noexcept;
		[[nodiscard]] BufferViewHandle GetBufferView() const noexcept;
		[[nodiscard]] TextureHandle GetTexture() const noexcept;
		[[nodiscard]] TextureViewHandle GetTextureView() const noexcept;
		[[nodiscard]] SamplerHandle GetSampler() const noexcept;
		[[nodiscard]] AccelStructHandle GetAccelerationStructure() const noexcept;

	private:

		struct BufferPayload final
		{
			BufferHandle Handle;
			BufferRange Range;
		};

		EDescriptorValueKind m_Kind = EDescriptorValueKind::Invalid; core::Variant<core::EmptyValue, BufferPayload, BufferViewHandle, TextureHandle, TextureViewHandle, SamplerHandle, AccelStructHandle> m_Payload;
	};

	struct BindingEntry final
	{
		core::uint32 RangeIndex = 0;
		core::uint32 ArrayElement = 0;
		DescriptorValue Value;
	};

	struct BindingTable final
	{
		core::uint32 RangeIndex = 0;
		DescriptorTableHandle Table;
		core::Span<const core::uint32> UsedEntries;
	};

	struct CommandBindings final
	{
		core::Span<const BindingSetHandle> Sets; core::Span<const DescriptorHeapBinding> DeviceHeaps;
	};

	struct BindingSetDesc final
	{
		BindingLayoutHandle Layout;
		core::Span<const BindingEntry> Entries;
		core::Span<const BindingTable> Tables;
	};

	enum class EDescriptorType : core::uint8
	{
		SampledTexture,
		StorageTexture,
		UniformBuffer,
		StorageBuffer,
		Sampler,
		ReadOnlyBuffer,
		AccelerationStructure
	};

	enum class EBindingLayoutKind : core::uint8
	{
		Compute,
		Graphics,
		RayTracing
	};

	enum class EShaderVisibilityFlags : core::uint32
	{
		Compute = 1,
		Vertex = 2,
		Pixel = 4,
		ComputeVertexPixel = 7,
		Mesh = 8,
		Task = 16,
		RayGeneration = 32,
		Miss = 64,
		ClosestHit = 128,
		AnyHit = 256,
		Intersection = 512,
		Callable = 1024,
		Geometry = 2048,
		Hull = 4096,
		Domain = 8192,
		GraphicsRangeStages = 14366,
		RayTracing = 2016,
		AllRangeStages = 16383
	};

	enum class EBindingRangeSource : core::uint8
	{
		Fixed,
		DescriptorTable
	};

	enum class EBufferBindingMode : core::uint8
	{
		DescriptorTable,
		RootDescriptor
	};

	struct BindingRangeDesc final
	{
		EDescriptorType Type = EDescriptorType::StorageBuffer;
		core::uint32 Binding = 0;
		core::uint32 DescriptorCount = 1;

		EFormat BufferFormat = EFormat::UNKNOWN;

		EShaderVisibilityFlags Visibility = EShaderVisibilityFlags::ComputeVertexPixel;
		core::uint32 StructureStrideBytes = 0;

		EBindingRangeSource Source = EBindingRangeSource::Fixed;
		EBufferBindingMode BufferMode = EBufferBindingMode::DescriptorTable;

		const SamplerDesc* ImmutableSamplerOrNull = nullptr;
	};

	struct BindingLayoutDesc final
	{
		EBindingLayoutKind Kind = EBindingLayoutKind::Compute;
		core::uint32 Space = 0;
		core::Span<const BindingRangeDesc> Ranges;
	};

	struct DescriptorTableDesc final
	{
		EDescriptorType Type = EDescriptorType::SampledTexture;
		core::uint32 Capacity = 0;

		EFormat BufferFormat = EFormat::UNKNOWN;
		core::uint32 StructureStrideBytes = 0;
	};

	struct DescriptorTableWrite final
	{
		core::uint32 Index = 0;
		DescriptorValue Value;
	};
}
