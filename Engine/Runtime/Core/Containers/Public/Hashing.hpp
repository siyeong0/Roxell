#pragma once

#include <Assertion.h>
#include <Types.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <CoreString.hpp>
#include <StringView.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace rox::core
{
	// In-memory, non-cryptographic hashing. Never use this vocabulary as a persisted content identity.
	[[nodiscard]] inline rox::core::uint64 HashBytes(const void* data, const rox::core::usize byteCount) noexcept
	{
		ASSERT(data != nullptr || byteCount == 0, "HashBytes requires storage for every input byte.");
		const auto* bytes = static_cast<const unsigned char*>(data);
		rox::core::uint64 hash = 14695981039346656037ull;

		for (rox::core::usize index = 0; index < byteCount; ++index)
		{
			hash = (hash ^ bytes[index]) * 1099511628211ull;
		}

		return hash;
	}

	[[nodiscard]] constexpr rox::core::uint64 HashCombine(const rox::core::uint64 seed, const rox::core::uint64 value) noexcept
	{
		rox::core::uint64 mixed = seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
		mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ull;
		mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebull;
		return mixed ^ (mixed >> 31);
	}

	template <typename TKey> struct DefaultHash
	{
		template <typename TLookup>
		[[nodiscard]] rox::core::uint64 operator()(const TLookup& key) const noexcept
		    requires((rox::core::IsSame<TKey, String> || rox::core::IsSame<TKey, StringView>) && rox::core::IsNothrowConvertible<const TLookup&, StringView>)
		{
			const StringView bytes = TextKeyView(key);
			return HashBytes(bytes.data(), bytes.size());
		}

		[[nodiscard]] rox::core::uint64 operator()(const TKey& key) const noexcept
		{
			if constexpr (rox::core::IsIntegral<TKey> || rox::core::IsEnum<TKey>)
			{
				return HashCombine(0, static_cast<rox::core::uint64>(key));
			}
			else if constexpr (rox::core::IsPointer<TKey>)
			{
				return HashCombine(0, static_cast<rox::core::uint64>(reinterpret_cast<rox::core::uintptr>(key)));
			}
			else if constexpr (rox::core::IsSame<TKey, float>)
			{
				return HashCombine(0, rox::core::BitCast<rox::core::uint32>(key == 0.0f ? 0.0f : key));
			}
			else if constexpr (rox::core::IsSame<TKey, double>)
			{
				return HashCombine(0, rox::core::BitCast<rox::core::uint64>(key == 0.0 ? 0.0 : key));
			}
			else
			{
				static_assert(rox::core::IsSame<TKey, String> || rox::core::IsSame<TKey, StringView>, "Provide a hash functor for this key type.");
				return HashBytes(key.Data(), key.Size());
			}
		}
	};

	template <> struct DefaultHash<std::string_view>
	{
		[[nodiscard]] rox::core::uint64 operator()(const std::string_view key) const noexcept
		{
			return HashBytes(key.data(), key.size());
		}
	};

	template <typename TKey> struct DefaultEqual
	{
		template <typename TLookup>
		[[nodiscard]] bool operator()(const TKey& left, const TLookup& right) const noexcept
		    requires((rox::core::IsSame<TKey, String> || rox::core::IsSame<TKey, StringView> || rox::core::IsSame<TKey, std::string_view>) && rox::core::IsNothrowConvertible<const TLookup&, StringView>)
		{
			return TextKeyView(left) == TextKeyView(right);
		}

		[[nodiscard]] bool operator()(const TKey& left, const TKey& right) const noexcept(noexcept(left == right))
		{
			return left == right;
		}
	};
}
