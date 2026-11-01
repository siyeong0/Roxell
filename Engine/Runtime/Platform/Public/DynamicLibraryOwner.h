#pragma once

#include <MemoryResource.h>
#include <Result.hpp>
#include <StringView.hpp>

namespace rox::os
{
	using DynamicLibrarySymbol = void (*)();

	// Borrowed symbols remain valid only until a successful Close. Callers drain their users first.
	class DynamicLibraryOwner final
	{
	public:
		using FindFunction = core::Result<DynamicLibrarySymbol>(*)(void*, core::StringView) noexcept;
		using CloseFunction = core::Result<void>(*)(void*) noexcept;
		using DestroyFunction = void (*)(core::MemoryResource&, void*) noexcept;

		DynamicLibraryOwner() noexcept = default;
		~DynamicLibraryOwner() noexcept;
		DynamicLibraryOwner(const DynamicLibraryOwner&) = delete;
		DynamicLibraryOwner& operator=(const DynamicLibraryOwner&) = delete;
		DynamicLibraryOwner(DynamicLibraryOwner&& other) noexcept;
		DynamicLibraryOwner& operator=(DynamicLibraryOwner&& other) noexcept;

		// Backend adoption transfers one complete allocation and its exact destruction operation.
		[[nodiscard]] static DynamicLibraryOwner Adopt(core::MemoryResource& resource, void* object, FindFunction find, CloseFunction close, DestroyFunction destroy) noexcept;

		// Requires an open owner. Invalid export names and native lookup failures remain recoverable.
		[[nodiscard]] core::Result<DynamicLibrarySymbol> FindSymbol(core::StringView name) const noexcept;

		// Failure retains the complete owner. Retry before destruction or move assignment over it.
		[[nodiscard]] core::Result<void> Close() noexcept;

		[[nodiscard]] explicit operator bool() const noexcept { return m_Object != nullptr; }

	private:
		core::MemoryResource* m_Resource = nullptr;
		void* m_Object = nullptr;

		FindFunction m_Find = nullptr;
		CloseFunction m_Close = nullptr;
		DestroyFunction m_Destroy = nullptr;
	};
} // namespace rox::os
