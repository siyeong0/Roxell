#pragma once

#include <gtest/gtest.h>

#include <Result.hpp>
#include <Span.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>

#include <cstring>

#undef CHECK
#define CHECK(...) \
	do \
	{ \
		if (!(__VA_ARGS__)) \
		{ \
			ADD_FAILURE() << #__VA_ARGS__; \
			return false; \
		} \
	} while (false)

namespace rox::test
{
	template <typename T>
	[[nodiscard]] bool Succeeded(const core::Result<T>& result)
	{
		if (result)
		{
			return true;
		}

		const core::Error& error = result.GetError();
		ADD_FAILURE() << error.DiagnosticID.CStr() << ": " << error.Message.CStr();
		return false;
	}

	[[nodiscard]] inline bool EqualBytes(core::Span<const core::Byte> left, core::Span<const core::Byte> right) noexcept
	{
		return left.Size() == right.Size() && (left.IsEmpty() || std::memcmp(left.Data(), right.Data(), left.Size()) == 0);
	}
}
