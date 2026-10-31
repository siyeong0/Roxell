#include <Quaternion.hpp>
#include <Matrix3x3.hpp>
#include <Matrix4x4.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <limits>

using namespace rox::core;
using namespace rox::math;

namespace
{
	bool isNear(const float32 actual, const float32 expected)
	{
		return std::isfinite(actual) && std::abs(actual - expected) <= 1.0e-5f;
	}
}

TEST(QuaternionTests, QuaternionQuarterTurnAndInverseFailure)
{
	const Quaternion<> rotation = Quaternion<>::FromAxisAngle(Vector3f::UnitY(), std::numbers::pi_v<float32> / 2.0f);
	const Vector3f result = rotation.RotateVector(Vector3f::Forward());
	ASSERT_TRUE(isNear(result.X, 1.0f));
	ASSERT_TRUE(isNear(result.Y, 0.0f));
	ASSERT_TRUE(isNear(result.Z, 0.0f));
	Quaternion<> output{1.0f, 2.0f, 3.0f, 4.0f};
	ASSERT_FALSE((Quaternion<>{0.0f, 0.0f, 0.0f, 0.0f}.TryInverse(&output)));
	ASSERT_EQ(output, Quaternion<>(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST(QuaternionTests, NormalizationAndInversePreserveOutputOnFailure)
{
	const Quaternion<float64> original{1.0, 2.0, 3.0, 4.0};
	Quaternion<float64> output = original;
	const Quaternion<float64> zero{0.0, 0.0, 0.0, 0.0};
	EXPECT_FALSE(zero.TryNormalize(&output));
	EXPECT_EQ(output, original);
	EXPECT_FALSE(zero.TryInverse(&output));
	EXPECT_EQ(output, original);
	const Quaternion<float64> invalid{std::numeric_limits<float64>::infinity(), 0.0, 0.0, 1.0};
	EXPECT_FALSE(invalid.IsFinite());
	EXPECT_FALSE(invalid.TryNormalize(&output));
	EXPECT_EQ(output, original);
	EXPECT_FALSE(invalid.TryInverse(&output));
	EXPECT_EQ(output, original);
	ASSERT_TRUE(original.TryNormalize(&output));
	EXPECT_NEAR(output.Length(), 1.0, 1.0e-12);
	EXPECT_DOUBLE_EQ(original.LengthSquared(), 30.0);
	EXPECT_EQ(original.Conjugated(), (Quaternion<float64>{-1.0, -2.0, -3.0, 4.0}));
	ASSERT_TRUE(original.TryInverse(&output));
	const auto identity = original * output;
	EXPECT_NEAR(identity.X, 0.0, 1.0e-12);
	EXPECT_NEAR(identity.Y, 0.0, 1.0e-12);
	EXPECT_NEAR(identity.Z, 0.0, 1.0e-12);
	EXPECT_NEAR(identity.W, 1.0, 1.0e-12);
}

TEST(QuaternionTests, InterpolationAndConversionsRepresentSameRotation)
{
	using Rotation = Quaternion<float64>;
	const Rotation identity = Rotation::Identity();
	const Rotation turn = Rotation::FromAxisAngle(Vector3d::UnitZ(), std::numbers::pi_v<float64> / 2.0);
	const Vector3d expected{0.7071067811865475, 0.7071067811865475, 0.0};
	for (const Rotation& middle : {Rotation::Nlerp(identity, turn, 0.5), Rotation::Slerp(identity, turn, 0.5),
	                               Rotation::RotateTowards(identity, turn, std::numbers::pi_v<float64> / 4.0)})
	{
		const Vector3d rotated = middle.RotateVector(Vector3d::UnitX());
		EXPECT_NEAR(rotated.X, expected.X, 1.0e-12);
		EXPECT_NEAR(rotated.Y, expected.Y, 1.0e-12);
		EXPECT_NEAR(rotated.Z, expected.Z, 1.0e-12);
	}
	EXPECT_NEAR(identity.AngularDistance(turn), std::numbers::pi_v<float64> / 2.0, 1.0e-12);
	const auto axisAngle = turn.ToAxisAngle();
	EXPECT_NEAR(axisAngle.Axis.Z, 1.0, 1.0e-12);
	EXPECT_NEAR(axisAngle.Radians, std::numbers::pi_v<float64> / 2.0, 1.0e-12);
	const auto reconstructed = Rotation::FromRotationMatrix(turn.ToMatrix3x3());
	EXPECT_NEAR(std::abs(reconstructed.Dot(turn)), 1.0, 1.0e-12);
	const auto fromLog = Rotation::RotationExp(turn.RotationLog());
	EXPECT_NEAR(std::abs(fromLog.Dot(turn)), 1.0, 1.0e-12);
	const Vector3d matrixResult = turn.ToMatrix4x4().TransformDirection(Vector3d::UnitX());
	EXPECT_NEAR(matrixResult.X, 0.0, 1.0e-12);
	EXPECT_NEAR(matrixResult.Y, 1.0, 1.0e-12);
}

TEST(QuaternionTests, DirectionAlignmentEulerAndSquadEndpoints)
{
	using Rotation = Quaternion<float64>;
	const Rotation opposite = Rotation::FromToRotation(Vector3d::UnitX(), -Vector3d::UnitX());
	EXPECT_TRUE(opposite.RotateVector(Vector3d::UnitX()).NearlyEquals(-Vector3d::UnitX(), 1.0e-12));
	const Rotation look = Rotation::LookRotation(Vector3d::UnitX(), Vector3d::UnitY());
	EXPECT_TRUE(look.RotateVector(Vector3d::Forward()).NearlyEquals(Vector3d::UnitX(), 1.0e-12));
	const Rotation rotation = Rotation::FromEulerXYZ({0.2, -0.3, 0.4});
	const Rotation reconstructed = Rotation::FromEulerXYZ(rotation.ToEulerXYZ());
	EXPECT_NEAR(std::abs(rotation.Dot(reconstructed)), 1.0, 1.0e-12);
	const Rotation identity = Rotation::Identity();
	const Rotation tangent = Rotation::SquadTangent(identity, identity, identity);
	EXPECT_NEAR(std::abs(tangent.Dot(identity)), 1.0, 1.0e-12);
	EXPECT_NEAR(std::abs(Rotation::Squad(identity, rotation, identity, rotation, 0.0).Dot(identity)), 1.0, 1.0e-12);
	EXPECT_NEAR(std::abs(Rotation::Squad(identity, rotation, identity, rotation, 1.0).Dot(rotation)), 1.0, 1.0e-12);
}

TEST(QuaternionTests, SwingTwistReconstructsRotationAndHandlesHalfTurnSingularity)
{
	using Rotation = Quaternion<float64>;
	const Rotation rotation = Rotation::FromAxisAngle(Vector3d::UnitY(), 0.7) *
	                          Rotation::FromAxisAngle(Vector3d::UnitZ(), 0.4);
	const QuaternionSwingTwist<float64> parts = rotation.DecomposeSwingTwist(Vector3d{0, 0, 5});
	EXPECT_NEAR(std::abs((parts.Swing * parts.Twist).Dot(rotation)), 1.0, 1.0e-12);
	EXPECT_NEAR(parts.Twist.X, 0.0, 1.0e-12);
	EXPECT_NEAR(parts.Twist.Y, 0.0, 1.0e-12);
	EXPECT_TRUE(parts.Twist.RotateVector(Vector3d::UnitZ()).NearlyEquals(Vector3d::UnitZ(), 1.0e-12));
	const Rotation halfTurn{1.0, 0.0, 0.0, 0.0};
	const QuaternionSwingTwist<float64> singular = halfTurn.DecomposeSwingTwist(Vector3d::UnitZ());
	EXPECT_EQ(singular.Twist, Rotation::Identity());
	EXPECT_NEAR(std::abs(singular.Swing.Dot(halfTurn)), 1.0, 1.0e-12);
}

TEST(QuaternionTests, IdentityAndEulerPolesRoundTripRotations)
{
	using Rotation = Quaternion<float64>;
	const auto axisAngle = Rotation::Identity().ToAxisAngle();
	EXPECT_EQ(axisAngle.Axis, Vector3d::UnitX());
	EXPECT_DOUBLE_EQ(axisAngle.Radians, 0.0);
	EXPECT_EQ(Rotation::Identity().RotationLog(), Vector3d::Zero());
	EXPECT_EQ(Rotation::RotationExp(Vector3d::Zero()), Rotation::Identity());
	for (const float64 pitch : {-std::numbers::pi_v<float64> / 2.0, std::numbers::pi_v<float64> / 2.0})
	{
		const Rotation rotation = Rotation::FromEulerXYZ({0.3, pitch, -0.2});
		const Rotation reconstructed = Rotation::FromEulerXYZ(rotation.ToEulerXYZ());
		EXPECT_NEAR(std::abs(rotation.Dot(reconstructed)), 1.0, 1.0e-12);
	}
}
