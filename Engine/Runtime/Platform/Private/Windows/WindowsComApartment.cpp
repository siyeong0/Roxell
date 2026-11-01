#include <WindowsComApartment.h>

#include <Assertion.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>

namespace rox::os
{
	WindowsComApartment::~WindowsComApartment()
	{
		if (m_OwnerThreadId != 0)
		{
			ASSERT(m_OwnerThreadId == GetCurrentThreadId(), "A COM apartment initialization must be released on its owning thread.");
			CoUninitialize();
		}
	}

	core::Result<void> WindowsComApartment::Initialize()
	{
		ASSERT(m_OwnerThreadId == 0, "An initialized COM apartment owner cannot be initialized again.");
		const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		if (FAILED(result))
		{
			return core::Result<void>::Failure(
				result == RPC_E_CHANGED_MODE ? core::EErrorCode::Conflict : core::EErrorCode::ExternalFailure,
				"ROX-PLATFORM-COM-INITIALIZE", "The calling thread could not initialize a multithreaded COM apartment.",
				"Use an MTA host thread and retain its apartment until dependent device owners are destroyed.");
		}

		m_OwnerThreadId = GetCurrentThreadId();

		return core::Result<void>::Success();
	}

	bool WindowsComApartment::IsInitialized() const noexcept
	{
		return m_OwnerThreadId != 0;
	}
} // namespace rox::os
