#include <RhiBindingTypes.h>

#include <Assertion.h>

namespace rox::rhi
{
	using namespace core;

	DescriptorValue DescriptorValue::Buffer(const BufferHandle buffer, const BufferRange range) noexcept
	{
		DescriptorValue value;
		value.m_Kind = EDescriptorValueKind::Buffer;
		value.m_Payload = BufferPayload{ buffer, range };
		return value;
	}

	DescriptorValue DescriptorValue::BufferView(const BufferViewHandle view) noexcept
	{
		DescriptorValue value;
		value.m_Kind = EDescriptorValueKind::BufferView;
		value.m_Payload = view;
		return value;
	}

	DescriptorValue DescriptorValue::SampledTexture(const TextureHandle texture) noexcept
	{
		DescriptorValue value;
		value.m_Kind = EDescriptorValueKind::SampledTexture;
		value.m_Payload = texture;
		return value;
	}

	DescriptorValue DescriptorValue::SampledView(const TextureViewHandle view) noexcept
	{
		DescriptorValue value;
		value.m_Kind = EDescriptorValueKind::SampledView;
		value.m_Payload = view;
		return value;
	}

	DescriptorValue DescriptorValue::StorageTexture(const TextureHandle texture) noexcept
	{
		DescriptorValue value;
		value.m_Kind = EDescriptorValueKind::StorageTexture;
		value.m_Payload = texture;
		return value;
	}

	DescriptorValue DescriptorValue::StorageView(const TextureViewHandle view) noexcept
	{
		DescriptorValue value;
		value.m_Kind = EDescriptorValueKind::StorageView;
		value.m_Payload = view;
		return value;
	}

	DescriptorValue DescriptorValue::Sampler(const SamplerHandle sampler) noexcept
	{
		DescriptorValue value;
		value.m_Kind = EDescriptorValueKind::Sampler;
		value.m_Payload = sampler;
		return value;
	}

	DescriptorValue DescriptorValue::AccelerationStructure(const AccelStructHandle accelerationStructure) noexcept
	{
		DescriptorValue value;
		value.m_Kind = EDescriptorValueKind::AccelerationStructure;
		value.m_Payload = accelerationStructure;
		return value;
	}

	BufferHandle DescriptorValue::GetBuffer() const noexcept
	{
		ASSERT(m_Kind == EDescriptorValueKind::Buffer, "Descriptor value is not a buffer.");
		return Get<BufferPayload>(m_Payload).Handle;
	}

	BufferRange DescriptorValue::GetBufferRange() const noexcept
	{
		ASSERT(m_Kind == EDescriptorValueKind::Buffer, "Descriptor value is not a buffer.");
		return Get<BufferPayload>(m_Payload).Range;
	}

	BufferViewHandle DescriptorValue::GetBufferView() const noexcept
	{
		ASSERT(m_Kind == EDescriptorValueKind::BufferView, "Descriptor value is not a buffer view.");
		return Get<BufferViewHandle>(m_Payload);
	}

	TextureHandle DescriptorValue::GetTexture() const noexcept
	{
		ASSERT(m_Kind == EDescriptorValueKind::SampledTexture || m_Kind == EDescriptorValueKind::StorageTexture, "Descriptor value is not a texture.");
		return Get<TextureHandle>(m_Payload);
	}

	TextureViewHandle DescriptorValue::GetTextureView() const noexcept
	{
		ASSERT(m_Kind == EDescriptorValueKind::SampledView || m_Kind == EDescriptorValueKind::StorageView, "Descriptor value is not a texture view.");
		return Get<TextureViewHandle>(m_Payload);
	}

	SamplerHandle DescriptorValue::GetSampler() const noexcept
	{
		ASSERT(m_Kind == EDescriptorValueKind::Sampler, "Descriptor value is not a sampler.");
		return Get<SamplerHandle>(m_Payload);
	}

	AccelStructHandle DescriptorValue::GetAccelerationStructure() const noexcept
	{
		ASSERT(m_Kind == EDescriptorValueKind::AccelerationStructure, "Descriptor value is not an acceleration structure.");
		return Get<AccelStructHandle>(m_Payload);
	}
} // namespace rox::rhi
