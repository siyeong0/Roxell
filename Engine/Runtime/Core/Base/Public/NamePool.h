#pragma once

#include <Name.hpp>
#include <Synchronization.hpp>
#include <Types.h>
#include <HashSet.hpp>
#include <mutex>

namespace rox::core
{
	// Operations are thread-safe; destruction requires quiescent users. Interned storage is never evicted.
	class NamePool final
	{
	public:
		explicit NamePool(MemoryResource& resource) noexcept;
		NamePool(const NamePool&) = delete;
		NamePool& operator=(const NamePool&) = delete;

		Name Intern(StringView text) noexcept;
		Name Find(StringView text) const noexcept;

		void Reserve(rox::core::usize count) noexcept;
		rox::core::usize Size() const noexcept;

		MemoryResource& GetResource() const noexcept { return m_Names.GetResource(); }

	private:
		HashSet<String> m_Names;

		mutable rox::core::Mutex m_Mutex;
	};
}
