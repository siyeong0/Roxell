#include <Sha256.h>
#include <StringView.hpp>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(Sha256Tests, Sha256PublishedVectorsAndIncrementalState)
{
	const auto bytes = [](const char* text, const usize length)
	{
		return Span<const Byte>(reinterpret_cast<const Byte*>(text), length);
	};
	Sha256 hash;
	ASSERT_EQ(ToString(hash.Finalize()), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	hash.Update(bytes("a", 1));
	ASSERT_EQ(ToString(hash.Finalize()), "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb");
	hash.Update(bytes("bc", 2));
	ASSERT_EQ(ToString(hash.Finalize()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	ASSERT_EQ(ToString(hash.Finalize()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	hash.Reset();
	ASSERT_EQ(ToString(hash.Finalize()), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256Tests, DigestBlockBoundariesAcrossEverySplit)
{
	Byte bytes[65];
	for (usize index = 0; index < 65; ++index)
	{
		bytes[index] = static_cast<Byte>(index);
	}
	struct Vector
	{
		usize Size;
		StringView Digest;
	};
	constexpr Vector VECTORS[] =
	{
		{55, "463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59"},
		{56, "da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562"},
		{63, "29af2686fd53374a36b0846694cc342177e428d1647515f078784d69cdb9e488"},
		{64, "fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"},
		{65, "4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781"},
	};
	for (const Vector& vector : VECTORS)
	{
		for (usize split = 0; split <= vector.Size; ++split)
		{
			Sha256 hash;
			hash.Update({bytes, split});
			hash.Update({bytes + split, vector.Size - split});
			ASSERT_TRUE(ToString(hash.Finalize()) == vector.Digest) << "length=" << vector.Size << " split=" << split;
		}
	}
}
