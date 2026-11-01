#pragma once

#include <functional>
#include <TypeVocabulary.hpp>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <type_traits>
#include <utility>

namespace rox::core
{
	// Release cancels invocation while retaining captures until destruction. The callback cannot reenter its guard.
	template <typename TFunction> class ScopeExit final
	{
		static_assert(IsNothrowInvocable<TFunction&>);
		static_assert(IsNothrowMoveConstructible<TFunction>);
		static_assert(IsNothrowDestructible<TFunction>);

	public:
		template <typename TCallable>
		explicit ScopeExit(TCallable&& callable) noexcept
		    requires IsNothrowConstructible<TFunction, TCallable&&>
		    : m_Function(core::Forward<TCallable>(callable))
		{
		}

		ScopeExit(ScopeExit&& other) noexcept
		    : m_Function(core::Move(other.m_Function))
		    , m_bActive(core::Exchange(other.m_bActive, false))
		{
		}

		ScopeExit(const ScopeExit&) = delete;
		ScopeExit& operator=(const ScopeExit&) = delete;
		ScopeExit& operator=(ScopeExit&&) = delete;

		~ScopeExit() noexcept
		{
			if (m_bActive)
			{
				core::Invoke(m_Function);
			}
		}

		void Release() noexcept { m_bActive = false; }
		bool IsActive() const noexcept { return m_bActive; }

	private:
		TFunction m_Function;

		bool m_bActive = true;
	};

	template <typename TCallable> ScopeExit(TCallable&&) -> ScopeExit<core::Decay<TCallable>>;
}
