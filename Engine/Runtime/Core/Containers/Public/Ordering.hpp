#pragma once

#include <CoreString.hpp>
#include <ComparisonTypes.hpp>
#include <TypeTraits.hpp>
#include <StringView.hpp>

#include <functional>
#include <string_view>
#include <type_traits>

namespace rox::core
{
	// Comparisons must keep a strict weak order; default floating keys must exclude NaN.
	template <typename TKey> struct DefaultLess
	{
	private:
		static constexpr bool TEXT_KEY = rox::core::IsSame<TKey, String> || rox::core::IsSame<TKey, StringView>;

	public:
		template <typename TLeft, typename TRight>
		[[nodiscard]] bool operator()(const TLeft& left, const TRight& right) const noexcept
		    requires((TEXT_KEY && rox::core::IsNothrowConvertible<const TLeft&, StringView> && rox::core::IsNothrowConvertible<const TRight&, StringView>) ||
		             (!TEXT_KEY && rox::core::IsNothrowInvocableR<bool, rox::core::Less<>, const TLeft&, const TRight&>))
		{
			if constexpr (TEXT_KEY)
			{
				// Borrow text directly so literal probes never construct an owning String.
				return TextKeyView(left) < TextKeyView(right);
			}
			else
			{
				return rox::core::Less<>{}(left, right);
			}
		}
	};
}
