#include <DynamicLibraryOwner.h>
#include <Utility.hpp>

#include <Assertion.h>
#include <Vocabulary.hpp>

namespace rox::os
{
	using namespace core;

	DynamicLibraryOwner::~DynamicLibraryOwner() noexcept
	{
		const Result<void> closed = Close();
		ASSERT(closed, "A dynamic library Close failure must be resolved before owner destruction.");
	}

	DynamicLibraryOwner::DynamicLibraryOwner(DynamicLibraryOwner&& other) noexcept
		: m_Resource(Exchange(other.m_Resource, nullptr))
		, m_Object(Exchange(other.m_Object, nullptr))
		, m_Find(Exchange(other.m_Find, nullptr))
		, m_Close(Exchange(other.m_Close, nullptr))
		, m_Destroy(Exchange(other.m_Destroy, nullptr))
	{
	}

	DynamicLibraryOwner& DynamicLibraryOwner::operator=(DynamicLibraryOwner&& other) noexcept
	{
		if (this != &other)
		{
			const Result<void> closed = Close();
			ASSERT(closed, "A dynamic library Close failure must be resolved before replacement.");
			m_Resource = Exchange(other.m_Resource, nullptr);
			m_Object = Exchange(other.m_Object, nullptr);
			m_Find = Exchange(other.m_Find, nullptr);
			m_Close = Exchange(other.m_Close, nullptr);
			m_Destroy = Exchange(other.m_Destroy, nullptr);
		}

		return *this;
	}

	DynamicLibraryOwner DynamicLibraryOwner::Adopt(MemoryResource& resource, void* object, FindFunction find, CloseFunction close,
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

	Result<DynamicLibrarySymbol> DynamicLibraryOwner::FindSymbol(const StringView name) const noexcept
	{
		ASSERT(m_Object != nullptr, "Symbol lookup requires an open dynamic library owner.");

		if (name.IsEmpty() || name.Size() > 256)
		{
			return Result<DynamicLibrarySymbol>::Failure(EErrorCode::InvalidArgument, "ROX-PLATFORM-LIBRARY-SYMBOL", "Symbol lookup requires a bounded export name.");
		}

		for (const char value : name)
		{
			if (value <= 0 || static_cast<unsigned char>(value) > 127)
			{
				return Result<DynamicLibrarySymbol>::Failure(EErrorCode::InvalidArgument, "ROX-PLATFORM-LIBRARY-SYMBOL", "An export name must be nonempty ASCII with no embedded NUL.");
			}
		}

		return m_Find(m_Object, name);
	}

	Result<void> DynamicLibraryOwner::Close() noexcept
	{
		if (m_Object == nullptr)
		{
			return Result<void>::Success();
		}

		Result<void> closed = m_Close(m_Object);

		if (!closed)
		{
			return closed;
		}
		void* const object = Exchange(m_Object, nullptr);
		MemoryResource* const resource = Exchange(m_Resource, nullptr);
		void (* const destroy)(MemoryResource&, void*) noexcept = Exchange(m_Destroy, nullptr);
		m_Find = nullptr;
		m_Close = nullptr;
		destroy(*resource, object);

		return Result<void>::Success();
	}
} // namespace rox::os
