#include <StandardStreams.h>
#include <TypeTraits.hpp>
#include <TypeVocabulary.hpp>

#include <gtest/gtest.h>

using namespace rox::core;
using namespace rox::os;

static_assert(rox::core::IsNothrowMoveConstructible<StandardStreamsOwner>);
static_assert(!rox::core::IsCopyConstructible<StandardStreamsOwner>);
static_assert(rox::core::IsNothrowMoveConstructible<StandardReadResult>);
static_assert(rox::core::IsSame<decltype(rox::core::Declval<IStandardStreams&>().ReadSome(4096, rox::core::Declval<MemoryResource&>())), Result<void>>);

TEST(StandardStreamsTests, MovingEmptyOwnerLeavesBothOwnersEmpty)
{
	StandardStreamsOwner source;
	StandardStreamsOwner destination = rox::core::Move(source);
	EXPECT_FALSE(source);
	EXPECT_FALSE(destination);
	EXPECT_EQ(source.Get(), nullptr);
	EXPECT_EQ(destination.GetResource(), nullptr);
}
