#include <ByteReader.h>
#include <ByteWriter.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(ByteReaderTests, ByteOrderAndCursorBoundaries)
{
	constexpr Byte EXPECTED[] = {Byte{0x12}, Byte{0x34}, Byte{0x78}, Byte{0x56}, Byte{0x34}, Byte{0x12}};
	Byte storage[8] = {Byte{0xcc}, {}, {}, {}, {}, {}, {}, Byte{0xcc}};
	ByteWriter writer({storage + 1, 6});
	writer.WriteU16BE(0x1234);
	writer.WriteU32LE(0x12345678);
	ASSERT_EQ(writer.GetOffset(), 6);
	ASSERT_EQ(writer.GetRemaining(), 0);
	ASSERT_TRUE(writer.CanWrite(0));
	ASSERT_FALSE(writer.CanWrite(1));
	ASSERT_EQ(storage[0], Byte{0xcc});
	ASSERT_EQ(storage[7], Byte{0xcc});
	for (usize index = 0; index < 6; ++index)
	{
		ASSERT_EQ(storage[index + 1], EXPECTED[index]);
	}

	ByteReader reader(EXPECTED);
	ASSERT_EQ(reader.ReadU16BE(), 0x1234);
	ASSERT_EQ(reader.ReadU32LE(), 0x12345678u);
	uint16 unchanged = 0xabcd;
	ASSERT_FALSE(reader.TryReadU16BE(&unchanged));
	ASSERT_EQ(unchanged, 0xabcd);
	ASSERT_EQ(reader.GetOffset(), 6);
	ASSERT_TRUE(reader.CanRead(0));
	ASSERT_FALSE(reader.CanRead(1));
}

TEST(ByteReaderTests, ShortReadDoesNotConsumeOrOverwrite)
{
	constexpr Byte BYTES[] = {Byte{0x01}, Byte{0x02}, Byte{0x03}};
	ByteReader reader(BYTES);
	uint32 value = 0xdeadbeef;
	ASSERT_FALSE(reader.TryReadU32LE(&value));
	ASSERT_EQ(value, 0xdeadbeef);
	ASSERT_EQ(reader.GetOffset(), 0);
	ASSERT_EQ(reader.ReadU16BE(), 0x0102);
	ASSERT_EQ(reader.ReadU8(), 0x03);
}

TEST(ByteReaderTests, CheckedReadsDecodeAllWidthsAndOrders)
{
	constexpr Byte BYTES[] = {
		Byte{0xab}, Byte{0x34}, Byte{0x12}, Byte{0x12}, Byte{0x34},
		Byte{0x78}, Byte{0x56}, Byte{0x34}, Byte{0x12}, Byte{0x12}, Byte{0x34}, Byte{0x56}, Byte{0x78},
		Byte{0xef}, Byte{0xcd}, Byte{0xab}, Byte{0x89}, Byte{0x67}, Byte{0x45}, Byte{0x23}, Byte{0x01},
		Byte{0x01}, Byte{0x23}, Byte{0x45}, Byte{0x67}, Byte{0x89}, Byte{0xab}, Byte{0xcd}, Byte{0xef}
	};
	ByteReader reader(BYTES);
	uint8 value8 = 0;
	uint16 value16 = 0;
	uint32 value32 = 0;
	uint64 value64 = 0;
	ASSERT_TRUE(reader.TryReadU8(&value8));
	EXPECT_EQ(value8, 0xab);
	ASSERT_TRUE(reader.TryReadU16LE(&value16));
	EXPECT_EQ(value16, 0x1234);
	ASSERT_TRUE(reader.TryReadU16BE(&value16));
	EXPECT_EQ(value16, 0x1234);
	ASSERT_TRUE(reader.TryReadU32LE(&value32));
	EXPECT_EQ(value32, 0x12345678u);
	ASSERT_TRUE(reader.TryReadU32BE(&value32));
	EXPECT_EQ(value32, 0x12345678u);
	ASSERT_TRUE(reader.TryReadU64LE(&value64));
	EXPECT_EQ(value64, 0x0123456789abcdefULL);
	ASSERT_TRUE(reader.TryReadU64BE(&value64));
	EXPECT_EQ(value64, 0x0123456789abcdefULL);
	EXPECT_EQ(reader.GetOffset(), sizeof(BYTES));
	EXPECT_EQ(reader.GetRemaining(), 0u);
	EXPECT_FALSE(reader.TryReadU8(&value8));
	EXPECT_EQ(value8, 0xab);
}

TEST(ByteReaderTests, TruncatedReadsPreserveCursorAndOutputs)
{
	constexpr Byte BYTES[] = {Byte{1}, Byte{2}, Byte{3}};
	ByteReader reader(BYTES);
	EXPECT_EQ(reader.ReadU8(), 1);
	uint32 value32 = 0xdeadbeef;
	uint64 value64 = 0x0123456789abcdefULL;
	EXPECT_FALSE(reader.TryReadU32LE(&value32));
	EXPECT_FALSE(reader.TryReadU32BE(&value32));
	EXPECT_FALSE(reader.TryReadU64LE(&value64));
	EXPECT_FALSE(reader.TryReadU64BE(&value64));
	EXPECT_EQ(value32, 0xdeadbeefu);
	EXPECT_EQ(value64, 0x0123456789abcdefULL);
	Span<const Byte> output(BYTES, 1);
	EXPECT_FALSE(reader.TryReadBytes(3, &output));
	EXPECT_EQ(output.Data(), BYTES);
	EXPECT_EQ(output.Size(), 1u);
	EXPECT_EQ(reader.GetOffset(), 1u);
	ASSERT_TRUE(reader.TryReadBytes(2, &output));
	EXPECT_EQ(output.Data(), BYTES + 1);
	EXPECT_EQ(output.Size(), 2u);
	EXPECT_EQ(output[1], Byte{3});
	ASSERT_TRUE(reader.TryReadBytes(0, &output));
	EXPECT_EQ(output.Data(), BYTES + 3);
	EXPECT_TRUE(output.IsEmpty());
	uint16 value16 = 0xabcd;
	EXPECT_FALSE(reader.TryReadU16LE(&value16));
	EXPECT_FALSE(reader.TryReadU16BE(&value16));
	EXPECT_EQ(value16, 0xabcd);
	EXPECT_EQ(reader.GetOffset(), 3u);
}
