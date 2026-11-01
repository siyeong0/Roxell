#include "NvApiSession.h"
#if ROXELL_RHI_WITH_NVAPI
#include <nvapi.h>
#endif

namespace rox::rhi
{
#if ROXELL_RHI_WITH_NVAPI
	using namespace core;
#endif

	NvApiSession::~NvApiSession()
	{
#if ROXELL_RHI_WITH_NVAPI
		if (!m_bActive)
		{
			return;
		}

		SharedRuntime& runtime = getRuntime();
		LockGuard lock(runtime.Mutex);

		if (--runtime.Users == 0)
		{
			(void)NvAPI_Unload();
		}
#endif
	}

	[[nodiscard]] bool NvApiSession::Acquire() noexcept
	{
#if ROXELL_RHI_WITH_NVAPI
		if (m_bActive)
		{
			return true;
		}

		SharedRuntime& runtime = getRuntime();
		LockGuard lock(runtime.Mutex);

		if (runtime.Users == 0 && NvAPI_Initialize() != NVAPI_OK)
		{
			return false;
		}
		++runtime.Users;
		m_bActive = true;

		return true;
#else

		return false;
#endif
	}

#if ROXELL_RHI_WITH_NVAPI
	NvApiSession::SharedRuntime& NvApiSession::getRuntime() noexcept
	{
		static SharedRuntime runtime;

		return runtime;
	}
#endif
}
