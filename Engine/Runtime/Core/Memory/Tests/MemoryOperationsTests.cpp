#include <MemoryOperations.hpp>
#include <Types.h>

#include <gtest/gtest.h>

using namespace rox::core;

namespace
{
	class LifetimeProbe final
	{
	public:
		LifetimeProbe(int32& destroyed, const int32 value) noexcept
			: m_Destroyed(destroyed)
			, m_Value(value)
		{
		}

		LifetimeProbe(LifetimeProbe&& other) noexcept
			: m_Destroyed(other.m_Destroyed)
			, m_Value(other.m_Value)
		{
		}

		~LifetimeProbe() noexcept
		{
			++m_Destroyed;
		}

		int32 GetValue() const noexcept
		{
			return m_Value;
		}

	private:
		int32& m_Destroyed;
		int32 m_Value;
	};
}

TEST(MemoryOperationsTests, RelocationConstructsDestinationAndDestroysSource)
{
	alignas(LifetimeProbe) Byte sourceStorage[sizeof(LifetimeProbe)];
	alignas(LifetimeProbe) Byte destinationStorage[sizeof(LifetimeProbe)];
	int32 destroyed = 0;
	LifetimeProbe* const source = reinterpret_cast<LifetimeProbe*>(sourceStorage);
	LifetimeProbe* const destination = reinterpret_cast<LifetimeProbe*>(destinationStorage);
	ConstructAt(source, destroyed, 42);
	RelocateAt(destination, source);
	EXPECT_EQ(destroyed, 1);
	EXPECT_EQ(destination->GetValue(), 42);
	DestroyAt(destination);
	EXPECT_EQ(destroyed, 2);
}

TEST(MemoryOperationsTests, ConstructCopyFillAndDestroyRanges)
{
	alignas(int32) Byte sourceStorage[sizeof(int32) * 3];
	alignas(int32) Byte copyStorage[sizeof(int32) * 3];
	int32* const source = reinterpret_cast<int32*>(sourceStorage);
	int32* const copy = reinterpret_cast<int32*>(copyStorage);
	ValueConstructN(source, 3);
	EXPECT_EQ(source[0], 0);
	EXPECT_EQ(source[1], 0);
	EXPECT_EQ(source[2], 0);
	source[0] = 2;
	source[1] = 4;
	source[2] = 6;
	UninitializedCopyN(source, 3, copy);
	EXPECT_EQ(copy[0], 2);
	EXPECT_EQ(copy[1], 4);
	EXPECT_EQ(copy[2], 6);
	DestroyN(copy, 3);
	UninitializedFillN(copy, 3, 9);
	EXPECT_EQ(*Launder(copy), 9);
	EXPECT_EQ(copy[1], 9);
	EXPECT_EQ(copy[2], 9);
	DestroyN(copy, 3);
	DestroyN(source, 3);
}
