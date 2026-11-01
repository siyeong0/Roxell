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
		static constexpr bool TEXT_KEY = IsSame<TKey, String> || IsSame<TKey, StringView>;

	public:
		template <typename TLeft, typename TRight>
		[[nodiscard]] bool operator()(const TLeft& left, const TRight& right) const noexcept
		    requires((TEXT_KEY && IsNothrowConvertible<const TLeft&, StringView> && IsNothrowConvertible<const TRight&, StringView>) ||
		             (!TEXT_KEY && IsNothrowInvocableR<bool, core::Less<>, const TLeft&, const TRight&>))
		{
			if constexpr (TEXT_KEY)
			{
				// Borrow text directly so literal probes never construct an owning String.
				return TextKeyView(left) < TextKeyView(right);
			}
			else
			{
				return core::Less<>{}(left, right);
			}
		}
	};
}
