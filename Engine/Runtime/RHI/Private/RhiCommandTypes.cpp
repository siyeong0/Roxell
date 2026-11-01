#include <RhiCommandTypes.h>

#include <Assertion.h>

namespace rox::rhi
{
	using namespace core;

	ClearColor::ClearColor(const float32 r, const float32 g, const float32 b, const float32 a) noexcept
		: m_Float{ r, g, b, a }
	{
	}

	ClearColor ClearColor::Float(const float32 r, const float32 g, const float32 b, const float32 a) noexcept
	{
		return ClearColor(r, g, b, a);
	}

	ClearColor ClearColor::Uint(const uint32 r, const uint32 g, const uint32 b, const uint32 a) noexcept
	{
		ClearColor color;
		color.m_Type = EClearColorType::Uint;
		color.m_Uint[0] = r;
		color.m_Uint[1] = g;
		color.m_Uint[2] = b;
		color.m_Uint[3] = a;
		return color;
	}

	ClearColor ClearColor::Sint(const int32 r, const int32 g, const int32 b, const int32 a) noexcept
	{
		ClearColor color;
		color.m_Type = EClearColorType::Sint;
		color.m_Sint[0] = r;
		color.m_Sint[1] = g;
		color.m_Sint[2] = b;
		color.m_Sint[3] = a;
		return color;
	}

	const float32* ClearColor::GetFloat() const noexcept
	{
		ASSERT(m_Type == EClearColorType::Float, "Clear color contains floating-point values.");
		return m_Float;
	}

	const uint32* ClearColor::GetUint() const noexcept
	{
		ASSERT(m_Type == EClearColorType::Uint, "Clear color contains unsigned integer values.");
		return m_Uint;
	}

	const int32* ClearColor::GetSint() const noexcept
	{
		ASSERT(m_Type == EClearColorType::Sint, "Clear color contains signed integer values.");
		return m_Sint;
	}
}
