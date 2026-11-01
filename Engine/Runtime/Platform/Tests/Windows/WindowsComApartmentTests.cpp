#include <WindowsComApartment.h>
#include <gtest/gtest.h>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>

using namespace rox::core;
using namespace rox::os;

namespace
{
	HRESULT apartmentType(APTTYPE* const type)
	{
		APTTYPEQUALIFIER qualifier{};
		return CoGetApartmentType(type, &qualifier);
	}
}

TEST(WindowsComApartmentTests, NestedOwnersBalanceInitialization)
{
	std::thread worker([]()
	{
		APTTYPE type{};
		APTTYPEQUALIFIER initialQualifier{};
		const HRESULT initial = CoGetApartmentType(&type, &initialQualifier);
		ASSERT_TRUE(initial == CO_E_NOTINITIALIZED || (initial == S_OK && initialQualifier == APTTYPEQUALIFIER_IMPLICIT_MTA));
		{
			WindowsComApartment first;
			EXPECT_FALSE(first.IsInitialized());
			ASSERT_TRUE(first.Initialize());
			EXPECT_TRUE(first.IsInitialized());
			ASSERT_EQ(apartmentType(&type), S_OK);
			EXPECT_EQ(type, APTTYPE_MTA);
			{
				WindowsComApartment second;
				ASSERT_TRUE(second.Initialize());
			}
			EXPECT_EQ(apartmentType(&type), S_OK);
			EXPECT_EQ(type, APTTYPE_MTA);
		}
		APTTYPEQUALIFIER finalQualifier{};
		const HRESULT finalState = CoGetApartmentType(&type, &finalQualifier);
		EXPECT_TRUE(finalState == CO_E_NOTINITIALIZED ||
			(finalState == S_OK && type == APTTYPE_MTA && finalQualifier == APTTYPEQUALIFIER_IMPLICIT_MTA));
	});
	worker.join();
}

TEST(WindowsComApartmentTests, ConflictingApartmentRetainsStateAndCanRetry)
{
	std::thread worker([]()
	{
		ASSERT_TRUE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
		WindowsComApartment owner;
		const Result<void> conflict = owner.Initialize();
		EXPECT_FALSE(conflict);
		if (!conflict)
		{
			EXPECT_EQ(conflict.GetError().Code, EErrorCode::Conflict);
		}
		EXPECT_FALSE(owner.IsInitialized());
		APTTYPE type{};
		EXPECT_EQ(apartmentType(&type), S_OK);
		EXPECT_TRUE(type == APTTYPE_STA || type == APTTYPE_MAINSTA);
		CoUninitialize();
		ASSERT_TRUE(owner.Initialize());
		EXPECT_TRUE(owner.IsInitialized());
		EXPECT_EQ(apartmentType(&type), S_OK);
		EXPECT_EQ(type, APTTYPE_MTA);
	});
	worker.join();
}
