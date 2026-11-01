#pragma once

#include <Result.hpp>
#include <Types.h>

namespace rox::os
{
	// One successful MTA initialization on the calling thread. Destroy COM-dependent owners first.
	class WindowsComApartment final
	{
	public:
		WindowsComApartment() noexcept = default;
		~WindowsComApartment();

		WindowsComApartment(const WindowsComApartment&) = delete;
		WindowsComApartment& operator=(const WindowsComApartment&) = delete;
		WindowsComApartment(WindowsComApartment&&) = delete;
		WindowsComApartment& operator=(WindowsComApartment&&) = delete;

		// Success, including an existing MTA, owns one matching uninitialization. Failure owns none.
		[[nodiscard]] core::Result<void> Initialize();
		[[nodiscard]] bool IsInitialized() const noexcept;

	private:
		core::uint32 m_OwnerThreadId = 0;
	};
} // namespace rox::os
