#include <ByteWriter.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(ByteWriterTests, WritesEachWidthInBothByteOrders)
{
	Byte storage[31]{};
	ByteWriter writer(storage);
	writer.WriteU8(0xab);
	writer.WriteU16LE(0x1234);
	writer.WriteU16BE(0x1234);
	writer.WriteU32LE(0x12345678);
	writer.WriteU32BE(0x12345678);
	writer.WriteU64LE(0x0123456789abcdefULL);
	writer.WriteU64BE(0x0123456789abcdefULL);
	const Byte tail[] = {Byte{0xaa}, Byte{0xbb}};
	writer.WriteBytes(tail);

	constexpr Byte EXPECTED[] =
	{
		Byte{0xab}, Byte{0x34}, Byte{0x12}, Byte{0x12}, Byte{0x34},
		Byte{0x78}, Byte{0x56}, Byte{0x34}, Byte{0x12}, Byte{0x12}, Byte{0x34}, Byte{0x56}, Byte{0x78},
		Byte{0xef}, Byte{0xcd}, Byte{0xab}, Byte{0x89}, Byte{0x67}, Byte{0x45}, Byte{0x23}, Byte{0x01},
		Byte{0x01}, Byte{0x23}, Byte{0x45}, Byte{0x67}, Byte{0x89}, Byte{0xab}, Byte{0xcd}, Byte{0xef},
		Byte{0xaa}, Byte{0xbb}
	};
	EXPECT_EQ(writer.GetOffset(), 31);
	EXPECT_EQ(writer.GetRemaining(), 0);
	for (usize index = 0; index < 31; ++index)
	{
		EXPECT_EQ(storage[index], EXPECTED[index]) << "index=" << index;
	}
}

TEST(ByteWriterTests, EmptyWriteAndOverlappingBytes)
{
	Byte bytes[] = {Byte{1}, Byte{2}, Byte{3}, Byte{4}, Byte{5}, Byte{6}};
	ByteWriter writer(bytes);
	writer.WriteBytes(Span<const Byte>());
	EXPECT_EQ(writer.GetOffset(), 0u);
	writer.WriteU8(0xaa);
	writer.WriteBytes(Span<const Byte>(bytes, 4));
	EXPECT_EQ(writer.GetOffset(), 5u);
	EXPECT_EQ(writer.GetRemaining(), 1u);
	EXPECT_TRUE(writer.CanWrite(1));
	EXPECT_FALSE(writer.CanWrite(2));
	EXPECT_EQ(bytes[0], Byte{0xaa});
	EXPECT_EQ(bytes[1], Byte{0xaa});
	EXPECT_EQ(bytes[2], Byte{2});
	EXPECT_EQ(bytes[3], Byte{3});
	EXPECT_EQ(bytes[4], Byte{4});
	EXPECT_EQ(bytes[5], Byte{6});
}
