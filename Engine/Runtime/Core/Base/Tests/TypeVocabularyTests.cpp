#include <TypeVocabulary.hpp>
#include <Types.h>
#include <TypeTraits.hpp>

using namespace rox::core;

static_assert(IntegralConstant<int32, 3>::value == 3);
static_assert(TrueType::value && !FalseType::value);
static_assert(IsSame<Decay<const int32&>, int32>);
static_assert(IsSame<Conditional<true, int32, float32>, int32>);
static_assert(Extent<int32[2][3], 0> == 2 && Extent<int32[2][3], 1> == 3);
static_assert(Rank<int32[2][3]> == 2);
static_assert(IsSame<decltype(Declval<int32>()), int32&&>);
