#pragma once

#if ROXELL_RHI_WITH_NVAPI
#include <Synchronization.hpp>
#endif

namespace rox::rhi
{
	class NvApiSession final
	{
	public:
		NvApiSession() noexcept = default;
		NvApiSession(const NvApiSession&) = delete;
		NvApiSession& operator=(const NvApiSession&) = delete;
		~NvApiSession();

		[[nodiscard]] bool Acquire() noexcept;

	private:
#if ROXELL_RHI_WITH_NVAPI
		struct SharedRuntime final
		{
			core::Mutex Mutex;
			core::uint32 Users = 0;
		};

		static SharedRuntime& getRuntime() noexcept;

		bool m_bActive = false;
#endif
	};
}
