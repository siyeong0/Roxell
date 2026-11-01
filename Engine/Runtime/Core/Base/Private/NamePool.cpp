#include <NamePool.h>
#include <Synchronization.hpp>
#include <Types.h>
#include <Utility.hpp>
#include <Assertion.h>
#include <Utf8.h>

namespace rox::core
{
	NamePool::NamePool(MemoryResource& resource) noexcept
	    : m_Names(resource)
	{
		ASSERT(resource.GetThreadSafety() == EMemoryResourceThreadSafety::ThreadSafe,
		       "Name pools require a thread-safe resource.");
	}

	Name NamePool::Intern(StringView text) noexcept
	{
		ASSERT(IsValidUtf8(text), "Interned names must be valid UTF-8.");

		if (text.IsEmpty())
		{
			return {};
		}

		core::LockGuard lock(m_Mutex);

		if (const auto* existing = m_Names.Find(text))
		{
			return Name(existing->View());
		}

		String owned(text, m_Names.GetResource());
		// Interned views must survive entry moves and rehash, including short names.
		owned.ReserveRequired(String::INLINE_CAPACITY + 1);
		const auto view = owned.View();
		const bool bInserted = m_Names.InsertRequired(core::Move(owned));
		ASSERT(bInserted, "Name insertion requires an absent interned value.");

		return Name(view);
	}

	Name NamePool::Find(StringView text) const noexcept
	{
		ASSERT(IsValidUtf8(text), "Interned names must be valid UTF-8.");

		if (text.IsEmpty())
		{
			return {};
		}

		core::LockGuard lock(m_Mutex);
		const auto* found = m_Names.Find(text);
		return found == nullptr ? Name{} : Name(found->View());
	}

	void NamePool::Reserve(usize count) noexcept
	{
		core::LockGuard lock(m_Mutex);
		m_Names.ReserveRequired(count);
	}

	usize NamePool::Size() const noexcept
	{
		core::LockGuard lock(m_Mutex);
		return m_Names.Size();
	}
}
