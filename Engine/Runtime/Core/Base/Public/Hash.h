#pragma once

#include <CoreString.hpp>
#include <Vocabulary.hpp>
#include <StringView.hpp>
#include <Span.hpp>
#include <Types.h>
#include <Hashing.hpp>

#include <array>
#include <compare>
#include <functional>
#include <span>
#include <string_view>

namespace rox::core
{
	struct Hash256
	{
		StaticArray<uint8, 32> Bytes = {};

		[[nodiscard]] bool IsValid() const noexcept;

		bool operator==(const Hash256&) const noexcept = default;
		auto operator<=>(const Hash256&) const = default;
	};

	[[nodiscard]] Hash256 ComputeSha256(Span<const Byte> data) noexcept;

	[[nodiscard]] String ToString(const Hash256& hash);

	[[nodiscard]] bool TryParseHash256(StringView text, Hash256* outHash) noexcept;

	struct Hash256Hash
	{
		[[nodiscard]] usize operator()(const Hash256& hash) const noexcept;
	};

	template <> struct DefaultHash<Hash256>
	{
		[[nodiscard]] uint64 operator()(const Hash256& hash) const noexcept
		{
			return static_cast<uint64>(Hash256Hash{}(hash));
		}
	};

} // namespace rox::core

namespace std
{
	template <> struct hash<rox::core::Hash256>
	{
		[[nodiscard]] size_t operator()(const rox::core::Hash256& hash) const noexcept
		{
			return rox::core::Hash256Hash{}(hash);
		}
	};
} // namespace std
