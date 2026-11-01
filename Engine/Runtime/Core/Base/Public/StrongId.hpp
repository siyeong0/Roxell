#pragma once

#include <Assertion.h>
#include <HashFunction.hpp>
#include <StringView.hpp>
#include <Types.h>
#include <CoreString.hpp>
#include <Hash.h>
#include <Hashing.hpp>
#include <Span.hpp>

#include <compare>
#include <functional>
#include <span>
#include <string_view>

namespace rox::core
{
	template <typename TTag> struct StrongId
	{
		uint64 High = 0;
		uint64 Low = 0;

		[[nodiscard]] static constexpr StrongId Invalid() noexcept { return {}; }

		[[nodiscard]] constexpr bool IsValid() const noexcept { return High != 0 || Low != 0; }

		auto operator<=>(const StrongId&) const = default;
	};

	template <typename TTag> [[nodiscard]] String ToString(StrongId<TTag> id)
	{
		static constexpr char HEX_DIGITS[] = "0123456789abcdef";

		char text[32] = {};

		for (uint32 index = 0; index < 16; ++index)
		{
			const uint32 shift = (15 - index) * 4;
			text[index] = HEX_DIGITS[(id.High >> shift) & 0xf];
			text[index + 16] = HEX_DIGITS[(id.Low >> shift) & 0xf];
		}

		return String(StringView(text, sizeof(text)));
	}

	template <typename TTag> [[nodiscard]] bool TryParseStrongId(StringView text, StrongId<TTag>* outId) noexcept
	{
		ASSERT(outId != nullptr, "Strong ID output must not be null.");

		char normalized[32] = {};
		uint32 normalizedIndex = 0;

		for (const char character : text)
		{
			if (character == '-' || character == '{' || character == '}')
			{
				continue;
			}

			if (normalizedIndex >= 32)
			{
				return false;
			}

			normalized[normalizedIndex++] = character;
		}

		if (normalizedIndex != 32)
		{
			return false;
		}

		const auto hexValue = [](char character) noexcept -> uint8
		{
			if (character >= '0' && character <= '9')
			{
				return static_cast<uint8>(character - '0');
			}

			if (character >= 'a' && character <= 'f')
			{
				return static_cast<uint8>(10 + character - 'a');
			}

			if (character >= 'A' && character <= 'F')
			{
				return static_cast<uint8>(10 + character - 'A');
			}

			return 0xff;
		};

		StrongId<TTag> parsed;

		for (uint32 index = 0; index < 32; ++index)
		{
			const uint8 value = hexValue(normalized[index]);

			if (value == 0xff)
			{
				return false;
			}

			if (index < 16)
			{
				parsed.High = (parsed.High << 4) | value;
			}
			else
			{
				parsed.Low = (parsed.Low << 4) | value;
			}
		}

		*outId = parsed;
		return true;
	}

	template <typename TTag> struct StrongIdHash
	{
		[[nodiscard]] usize operator()(StrongId<TTag> id) const noexcept
		{
			const usize highHash = core::HashFunction<uint64>{}(id.High);
			const usize lowHash = core::HashFunction<uint64>{}(id.Low);
			return highHash ^
			       (lowHash + static_cast<usize>(0x9e3779b97f4a7c15ull) + (highHash << 6) + (highHash >> 2));
		}
	};

	template <typename TTag> struct DefaultHash<StrongId<TTag>>
	{
		[[nodiscard]] uint64 operator()(const StrongId<TTag>& id) const noexcept
		{
			return static_cast<uint64>(StrongIdHash<TTag>{}(id));
		}
	};

	struct CorrelationIdTag;
	struct StrongIdNamespaceTag;
	struct OperationIdTag;

	using CorrelationId = StrongId<CorrelationIdTag>;
	using StrongIdNamespace = StrongId<StrongIdNamespaceTag>;
	using OperationId = StrongId<OperationIdTag>;

	template <typename TTag>
	[[nodiscard]] StrongId<TTag> DeriveStrongId(const StrongIdNamespace idNamespace,
	                                            const Span<const Byte> name) noexcept
	{
		ASSERT(idNamespace.IsValid(), "A deterministic strong ID requires a valid namespace.");

		static constexpr char DOMAIN_SEPARATOR[] = "Roxell.StrongId.v1";
		static constexpr usize DOMAIN_SEPARATOR_SIZE = sizeof(DOMAIN_SEPARATOR) - 1;
		static constexpr usize NAMESPACE_SIZE = 16;
		static constexpr usize NAME_HASH_SIZE = 32;
		static constexpr usize MATERIAL_SIZE = DOMAIN_SEPARATOR_SIZE + NAMESPACE_SIZE + NAME_HASH_SIZE;

		const Hash256 nameHash = ComputeSha256(Span<const Byte>(name.Data(), name.Size()));
		Byte material[MATERIAL_SIZE] = {};
		usize materialOffset = 0;

		for (usize index = 0; index < DOMAIN_SEPARATOR_SIZE; ++index)
		{
			material[materialOffset++] = static_cast<Byte>(static_cast<uint8>(DOMAIN_SEPARATOR[index]));
		}

		const auto appendUint64 = [&material, &materialOffset](const uint64 value) noexcept
		{
			for (uint32 index = 0; index < 8; ++index)
			{
				const uint32 shift = (7 - index) * 8;
				material[materialOffset++] = static_cast<Byte>((value >> shift) & 0xffu);
			}
		};
		appendUint64(idNamespace.High);
		appendUint64(idNamespace.Low);

		for (const uint8 value : nameHash.Bytes)
		{
			material[materialOffset++] = static_cast<Byte>(value);
		}

		ASSERT(materialOffset == MATERIAL_SIZE, "Strong ID derivation material must have the expected size.");

		const Hash256 derivedHash = ComputeSha256(Span<const Byte>(material, MATERIAL_SIZE));
		StrongId<TTag> id;

		for (uint32 index = 0; index < 8; ++index)
		{
			id.High = (id.High << 8) | derivedHash.Bytes[index];
			id.Low = (id.Low << 8) | derivedHash.Bytes[index + 8];
		}

		if (!id.IsValid())
		{
			id.Low = 1;
		}

		return id;
	}

	template <typename TTag>
	[[nodiscard]] bool TryCreateStrongIdFromEntropy(const Span<const Byte> entropy, StrongId<TTag>* outId) noexcept
	{
		ASSERT(outId != nullptr, "Strong ID output must not be null.");

		if (entropy.Size() != 16)
		{
			return false;
		}

		StrongId<TTag> id;

		for (uint32 index = 0; index < 8; ++index)
		{
			id.High = (id.High << 8) | static_cast<uint8>(entropy[index]);
			id.Low = (id.Low << 8) | static_cast<uint8>(entropy[index + 8]);
		}

		if (!id.IsValid())
		{
			return false;
		}

		*outId = id;
		return true;
	}
} // namespace rox::core

namespace std
{
	template <typename TTag> struct hash<rox::core::StrongId<TTag>>
	{
		[[nodiscard]] size_t operator()(rox::core::StrongId<TTag> id) const noexcept
		{
			return rox::core::StrongIdHash<TTag>{}(id);
		}
	};
} // namespace std
