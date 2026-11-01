#include <DynamicLibraryOwner.h>
#include <Utility.hpp>

#include <Assertion.h>
#include <Vocabulary.hpp>

namespace rox::os
{
	DynamicLibraryOwner::~DynamicLibraryOwner() noexcept
	{
		const core::Result<void> closed = Close();
		ASSERT(closed, "A dynamic library Close failure must be resolved before owner destruction.");
	}

	DynamicLibraryOwner::DynamicLibraryOwner(DynamicLibraryOwner&& other) noexcept
		: m_Resource(rox::core::Exchange(other.m_Resource, nullptr))
		, m_Object(rox::core::Exchange(other.m_Object, nullptr))
		, m_Find(rox::core::Exchange(other.m_Find, nullptr))
		, m_Close(rox::core::Exchange(other.m_Close, nullptr))
		, m_Destroy(rox::core::Exchange(other.m_Destroy, nullptr))
	{
	}

	DynamicLibraryOwner& DynamicLibraryOwner::operator=(DynamicLibraryOwner&& other) noexcept
	{
		if (this != &other)
		{
			const core::Result<void> closed = Close();
			ASSERT(closed, "A dynamic library Close failure must be resolved before replacement.");
			m_Resource = rox::core::Exchange(other.m_Resource, nullptr);
			m_Object = rox::core::Exchange(other.m_Object, nullptr);
			m_Find = rox::core::Exchange(other.m_Find, nullptr);
			m_Close = rox::core::Exchange(other.m_Close, nullptr);
			m_Destroy = rox::core::Exchange(other.m_Destroy, nullptr);
		}

		return *this;
	}

	DynamicLibraryOwner DynamicLibraryOwner::Adopt(core::MemoryResource& resource, void* object, FindFunction find, CloseFunction close,
		DestroyFunction destroy) noexcept
	{
		ASSERT(object != nullptr && find != nullptr && close != nullptr && destroy != nullptr, "Dynamic library adoption requires one complete backend owner.");
		DynamicLibraryOwner owner;
		owner.m_Resource = &resource;
		owner.m_Object = object;
		owner.m_Find = find;
		owner.m_Close = close;
		owner.m_Destroy = destroy;

		return owner;
	}

	core::Result<DynamicLibrarySymbol> DynamicLibraryOwner::FindSymbol(const core::StringView name) const noexcept
	{
		ASSERT(m_Object != nullptr, "Symbol lookup requires an open dynamic library owner.");

		if (name.IsEmpty() || name.Size() > 256)
		{
			return core::Result<DynamicLibrarySymbol>::Failure(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-LIBRARY-SYMBOL", "Symbol lookup requires a bounded export name.");
		}

		for (const char value : name)
		{
			if (value <= 0 || static_cast<unsigned char>(value) > 127)
			{
				return core::Result<DynamicLibrarySymbol>::Failure(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-LIBRARY-SYMBOL", "An export name must be nonempty ASCII with no embedded NUL.");
			}
		}

		return m_Find(m_Object, name);
	}

	core::Result<void> DynamicLibraryOwner::Close() noexcept
	{
		if (m_Object == nullptr)
		{
			return core::Result<void>::Success();
		}

		core::Result<void> closed = m_Close(m_Object);

		if (!closed)
		{
			return closed;
		}
		void* const object = rox::core::Exchange(m_Object, nullptr);
		core::MemoryResource* const resource = rox::core::Exchange(m_Resource, nullptr);
		void (* const destroy)(core::MemoryResource&, void*) noexcept = rox::core::Exchange(m_Destroy, nullptr);
		m_Find = nullptr;
		m_Close = nullptr;
		destroy(*resource, object);

		return core::Result<void>::Success();
	}
} // namespace rox::os
