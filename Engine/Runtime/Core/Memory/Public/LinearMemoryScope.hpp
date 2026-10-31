#pragma once

#include <LinearMemoryResource.h>

namespace rox::core
{
	// Declare the scope before temporary owners so their destructors run before storage is reclaimed.
	class LinearMemoryScope final
	{
	public:
		explicit LinearMemoryScope(LinearMemoryResource& resource) noexcept
		    : m_Resource(resource)
		    , m_Marker(resource.Mark())
		{
		}

		~LinearMemoryScope() { m_Resource.Rewind(m_Marker); }

		LinearMemoryScope(const LinearMemoryScope&) = delete;
		LinearMemoryScope& operator=(const LinearMemoryScope&) = delete;
		LinearMemoryScope(LinearMemoryScope&&) = delete;
		LinearMemoryScope& operator=(LinearMemoryScope&&) = delete;

	private:
		LinearMemoryResource& m_Resource;
		LinearMemoryResource::Marker m_Marker;
	};
} // namespace rox::core
