#include <MathScalar.hpp>
#include <Matrix3x3.hpp>
#include <Vector3.hpp>

#include <type_traits>
#include <utility>

using namespace rox::core;
using namespace rox::math;

static_assert(MathScalar<float32>);
static_assert(MathScalar<float64>);
static_assert(!MathScalar<int32>);
static_assert(!MathScalar<uint64>);
static_assert(!MathScalar<bool>);
static_assert(!MathScalar<long double>);
static_assert(!MathScalar<const float32>);
static_assert(!MathScalar<float64&>);

static_assert(std::is_same_v<decltype(std::declval<Vector3f>().LengthSquared()), float32>);
static_assert(std::is_same_v<decltype(std::declval<Vector3d>().LengthSquared()), float64>);
static_assert(std::is_same_v<decltype(std::declval<Matrix3x3f>().Determinant()), float32>);
static_assert(std::is_same_v<decltype(std::declval<Matrix3x3d>().Determinant()), float64>);
