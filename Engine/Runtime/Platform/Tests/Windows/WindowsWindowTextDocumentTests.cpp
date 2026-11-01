#include <WindowsWindowSystem.h>
#include <FailingMemoryResource.h>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <ScopeExit.hpp>
#include <gtest/gtest.h>

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	TEST(WindowsWindowTextDocumentTests, InvalidUtf8SelectionBoundariesPreservePublishedSession)
	{
		core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
		{
			os::WindowsWindowSystem system;
			ASSERT_TRUE(os::WindowsWindowSystem::TryCreate(memory, {}, &system));
			os::WindowDesc desc;
			desc.Title = "Roxell text selection boundary";
			core::Result<os::WindowId> created = system.CreateWindow(desc);
			ASSERT_TRUE(created);
			const os::WindowId window = created.Value();
			core::ScopeExit destroy([&]() noexcept { (void)system.DestroyWindow(window); });
			os::WindowTextDocument document;
			document.Session = 111;
			document.Revision = 1;
			document.Text = "A\xC3\xA9\xF0\x9F\x99\x82Z";
			document.AnchorByte = 7;
			document.CaretByte = 1;
			ASSERT_TRUE(system.PublishTextDocument(window, document));
			const core::uint32 invalidOffsets[] = { 2, 4, 5, 6, 9 };
			for (const core::uint32 offset : invalidOffsets)
			{
				os::WindowTextDocument invalid = document;
				invalid.Session = 222;
				invalid.Revision = 2;
				invalid.CaretByte = offset;
				const core::Result<void> rejected = system.PublishTextDocument(window, invalid);
				ASSERT_FALSE(rejected) << offset;
				EXPECT_EQ(rejected.GetError().Code, core::EErrorCode::ExternalFailure) << offset;
				EXPECT_EQ(rejected.GetError().DiagnosticID, "platform.window.tsf") << offset;
				EXPECT_TRUE(system.SetTextInputState(window, { .Session = 111, .CaretHeight = 20, .bEnabled = true })) << offset;
			}
			document.AnchorByte = 0;
			document.CaretByte = 8;
			EXPECT_TRUE(system.PublishTextDocument(window, document));
			EXPECT_TRUE(system.PublishTextDocument(window, {}));
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(WindowsWindowTextDocumentTests, AllocationFailurePreservesPreviousDocumentSession)
	{
		core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
		{
			core::FailingMemoryResource memory(tracking, 1000);
			os::WindowsWindowSystem system;
			ASSERT_TRUE(os::WindowsWindowSystem::TryCreate(memory, {}, &system));
			os::WindowDesc desc;
			desc.Title = "Roxell text allocation boundary";
			core::Result<os::WindowId> created = system.CreateWindow(desc);
			ASSERT_TRUE(created);
			const os::WindowId window = created.Value();
			core::ScopeExit destroy([&]() noexcept { (void)system.DestroyWindow(window); });
			os::WindowTextDocument document;
			document.Session = 11;
			document.Revision = 1;
			document.Text = "before";
			ASSERT_TRUE(system.PublishTextDocument(window, document));
			memory.Reset(0);
			os::WindowTextDocument replacement = document;
			replacement.Session = 22;
			replacement.Revision = 2;
			replacement.Text = "after";
			const core::Result<void> failed = system.PublishTextDocument(window, replacement);
			ASSERT_FALSE(failed);
			EXPECT_EQ(failed.GetError().Code, core::EErrorCode::ExternalFailure);
			EXPECT_EQ(failed.GetError().DiagnosticID, "platform.window.tsf");
			memory.Reset(1000);
			EXPECT_TRUE(system.SetTextInputState(window, { .Session = 11, .CaretHeight = 20, .bEnabled = true }));
			EXPECT_TRUE(system.PublishTextDocument(window, replacement));
			EXPECT_TRUE(system.SetTextInputState(window, { .Session = 22, .CaretHeight = 20, .bEnabled = true }));
		}
		EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
	}
}
