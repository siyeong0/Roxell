#include <StandardStreams.h>
#include <Utility.hpp>

namespace rox::os
{
	StandardStreamsOwner::~StandardStreamsOwner() noexcept
	{
		Reset();
	}

	StandardStreamsOwner::StandardStreamsOwner(StandardStreamsOwner&& other) noexcept
		: m_Owner(rox::core::Move(other.m_Owner))
	{
		if (m_Owner)
		{
			(void)m_Owner->IsClosed();
		}
	}

	StandardStreamsOwner& StandardStreamsOwner::operator=(StandardStreamsOwner&& other) noexcept
	{
		if (this != &other)
		{
			Reset();

			if (other.m_Owner)
			{
				(void)other.m_Owner->IsClosed();
			}

			m_Owner = rox::core::Move(other.m_Owner);
		}

		return *this;
	}

	void StandardStreamsOwner::Reset() noexcept
	{
		ASSERT(!m_Owner || m_Owner->IsClosed(), "Standard streams require completed close before owner destruction.");
		m_Owner.Reset();
	}

	IStandardStreams* StandardStreamsOwner::Get() const noexcept
	{
		return m_Owner.Get();
	}

	core::MemoryResource* StandardStreamsOwner::GetResource() const noexcept
	{
		return m_Owner.GetResource();
	}

	StandardStreamsOwner::operator bool() const noexcept
	{
		return static_cast<bool>(m_Owner);
	}

	IStandardStreams* StandardStreamsOwner::operator->() const noexcept
	{
		return m_Owner.operator->();
	}
} // namespace rox::os
