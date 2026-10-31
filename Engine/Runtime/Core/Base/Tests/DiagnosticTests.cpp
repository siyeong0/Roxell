#include <Diagnostic.h>

#include <gtest/gtest.h>

using namespace rox::core;

TEST(DiagnosticTests, DefaultRecordContainsNoTextAndIsInformational)
{
	const Diagnostic diagnostic{};
	EXPECT_EQ(diagnostic.Severity, EDiagnosticSeverity::Information);
	EXPECT_TRUE(diagnostic.DiagnosticID.IsEmpty());
	EXPECT_TRUE(diagnostic.Module.IsEmpty());
	EXPECT_TRUE(diagnostic.Message.IsEmpty());
	EXPECT_TRUE(diagnostic.RecoveryHint.IsEmpty());
}
