#include <TypeTraits.hpp>
#include <Types.h>

using namespace rox::core;

namespace
{
	struct Base
	{
	};
	struct Derived : Base
	{
	};
	enum class ETest : uint8
	{
		Value
	};
}

static_assert(IsIntegral<int32> && !IsIntegral<float32>);
static_assert(IsFloatingPoint<float64> && !IsFloatingPoint<int32>);
static_assert(IsArray<int32[2]> && !IsArray<int32>);
static_assert(IsBaseOf<Base, Derived> && DerivedFrom<Derived, Base>);
static_assert(IsConst<const int32> && IsReference<int32&>);
static_assert(IsPointer<AddPointer<int32>>);
static_assert(IsSame<RemoveCvref<const int32&>, int32>);
static_assert(IsSame<RemoveExtent<int32[2]>, int32>);
static_assert(IsSame<MakeUnsigned<int32>, uint32>);
static_assert(IsSame<UnderlyingType<ETest>, uint8>);
static_assert(ConvertibleTo<int32, float64> && !ConvertibleTo<void*, int32*>);
