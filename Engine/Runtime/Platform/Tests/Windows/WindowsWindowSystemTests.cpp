#include <FailingMemoryResource.h>
#include <Synchronization.hpp>
#include <ThreadPrimitives.hpp>
#include <ClockPrimitives.hpp>
#include <SystemMemoryResource.h>
#include <TrackingMemoryResource.h>
#include <ScopeExit.hpp>
#include <WindowsWindowSystem.h>
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <imm.h>

#include <cstdlib>
#include <thread>

#ifdef CreateWindow
#undef CreateWindow
#endif

namespace
{
	namespace core = rox::core;
	namespace os = rox::os;

	class WindowsWindowSystemTests : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			os::WindowsWindowSystemOptions options;
			options.MaximumWindows = 1;
			options.MaximumEventsPerPoll = 8;
			ASSERT_TRUE(os::WindowsWindowSystem::TryCreate(m_Tracking, options, &m_System));
		}

		void TearDown() override
		{
			if (m_Window.IsValid())
			{
				EXPECT_TRUE(m_System.DestroyWindow(m_Window));
			}
		}

		void createWindow()
		{
			os::WindowDesc desc;
			desc.Title = "Roxell window test";
			desc.ClientExtent = { 320, 200 };
			desc.bVisible = false;
			core::Result<os::WindowId> result = m_System.CreateWindow(desc);
			ASSERT_TRUE(result);
			m_Window = result.Value();
		}

	protected:
		core::TrackingMemoryResource m_Tracking{ core::SystemMemoryResource::Get() };
		os::WindowsWindowSystem m_System;
		os::WindowId m_Window;
	};

	TEST(WindowsWindowSystemCreationTests, AllocationFailuresLeaveNoLiveAllocations)
	{
		for (core::uint32 accepted = 0; accepted < 4; ++accepted)
		{
			core::TrackingMemoryResource tracking(core::SystemMemoryResource::Get());
			{
				core::FailingMemoryResource failing(tracking, accepted);
				os::WindowsWindowSystem system;
				os::WindowsWindowSystemOptions options;
				options.MaximumWindows = 1;
				options.MaximumEventsPerPoll = 2;
				const core::Result<void> result = os::WindowsWindowSystem::TryCreate(failing, options, &system);
				ASSERT_FALSE(result);
				EXPECT_EQ(result.GetError().Code, core::EErrorCode::ExternalFailure);
				EXPECT_EQ(result.GetError().DiagnosticID, "ROX-PLATFORM-WINDOW-ALLOCATION");
				EXPECT_FALSE(system.IsValid());
			}
			EXPECT_EQ(tracking.GetStatistics().LiveAllocationCount, 0);
		}
	}

	TEST_F(WindowsWindowSystemTests, InvalidDescriptionsDoNotConsumeCapacity)
	{
		os::WindowDesc desc;
		desc.Title = {};
		core::Result<os::WindowId> invalid = m_System.CreateWindow(desc);
		ASSERT_FALSE(invalid);
		EXPECT_EQ(invalid.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(invalid.GetError().DiagnosticID, "ROX-PLATFORM-WINDOW-DESC");
		desc.Title = "invalid extent";
		desc.ClientExtent = {};
		invalid = m_System.CreateWindow(desc);
		ASSERT_FALSE(invalid);
		EXPECT_EQ(invalid.GetError().DiagnosticID, "ROX-PLATFORM-WINDOW-DESC");
		const char invalidUtf8[] = { static_cast<char>(0xC3), static_cast<char>(0x28) };
		desc.Title = core::StringView(invalidUtf8, 2);
		desc.ClientExtent = { 320, 200 };
		invalid = m_System.CreateWindow(desc);
		ASSERT_FALSE(invalid);
		EXPECT_EQ(invalid.GetError().DiagnosticID, "ROX-PLATFORM-WINDOW-DESC");
		ASSERT_NO_FATAL_FAILURE(createWindow());
	}

	TEST_F(WindowsWindowSystemTests, ResizeRejectsEmptyExtentAndPreservesState)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		ASSERT_TRUE(m_System.ResizeWindow(m_Window, { 400, 240 }));
		const core::Result<void> failed = m_System.ResizeWindow(m_Window, {});
		ASSERT_FALSE(failed);
		EXPECT_EQ(failed.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(failed.GetError().DiagnosticID, "ROX-PLATFORM-WINDOW-RESIZE");
		const core::Result<os::WindowState> state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_EQ(state.Value().ClientExtent, (os::WindowExtent{ 400, 240 }));
	}

	TEST_F(WindowsWindowSystemTests, DestroyReleasesNativeWindowAndInvalidatesGeneration)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const os::WindowId oldWindow = m_Window;
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(oldWindow);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		ASSERT_NE(IsWindow(handle), FALSE);
		ASSERT_TRUE(m_System.DestroyWindow(oldWindow));
		m_Window = {};
		EXPECT_EQ(IsWindow(handle), FALSE);
		const core::Result<os::WindowState> state = m_System.GetWindowState(oldWindow);
		ASSERT_FALSE(state);
		EXPECT_EQ(state.GetError().Code, core::EErrorCode::NotFound);
		EXPECT_EQ(state.GetError().DiagnosticID, "ROX-PLATFORM-WINDOW-NOT-FOUND");
		ASSERT_NO_FATAL_FAILURE(createWindow());
		EXPECT_EQ(m_Window.Slot, oldWindow.Slot);
		EXPECT_GT(m_Window.Generation, oldWindow.Generation);
	}

	TEST_F(WindowsWindowSystemTests, OverflowIsReportedOnceAndQueueRecovers)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		for (core::uint32 index = 0; index < 9; ++index)
		{
			SendMessageW(handle, WM_CLOSE, 0, 0);
		}
		const os::WindowEventBatch overflow = m_System.PollEvents();
		EXPECT_TRUE(overflow.bOverflowed);
		ASSERT_EQ(overflow.Events.Size(), 8);
		for (const os::WindowEvent& event : overflow.Events)
		{
			EXPECT_EQ(event.Kind, os::EWindowEventKind::CloseRequested);
		}
		const os::WindowEventBatch recovered = m_System.PollEvents();
		EXPECT_FALSE(recovered.bOverflowed);
		EXPECT_TRUE(recovered.Events.IsEmpty());
	}

	TEST_F(WindowsWindowSystemTests, Utf16InputCombinesPairsAndReplacesUnpairedSurrogates)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		SendMessageW(handle, WM_CHAR, 'A', 0);
		SendMessageW(handle, WM_CHAR, 0xD83D, 0);
		SendMessageW(handle, WM_CHAR, 0xDE00, 0);
		SendMessageW(handle, WM_CHAR, 0xD83D, 0);
		SendMessageW(handle, WM_CHAR, 'B', 0);
		SendMessageW(handle, WM_CHAR, 0xDC00, 0);
		const os::WindowEventBatch batch = m_System.PollEvents();
		const core::uint32 expected[] = { 'A', 0x1F600, 0xFFFD, 'B', 0xFFFD };
		core::uint32 index = 0;
		for (const os::WindowEvent& event : batch.Events)
		{
			if (event.Kind == os::EWindowEventKind::TextInput)
			{
				ASSERT_LT(index, 5);
				EXPECT_EQ(event.Text.CodePoint, expected[index]);
				++index;
			}
		}
		EXPECT_EQ(index, 5);
	}

	TEST_F(WindowsWindowSystemTests, CompositionIsCancelledWhenSessionChanges)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		os::WindowTextInputState state{ .Session = 91, .CaretX = 30, .CaretY = 40, .CaretHeight = 20, .bEnabled = true };
		ASSERT_TRUE(m_System.SetTextInputState(m_Window, state));
		const HIMC context = ImmGetContext(handle);
		ASSERT_NE(context, nullptr);
		CANDIDATEFORM candidate{};
		const bool bQueried = ImmGetCandidateWindow(context, 0, &candidate) != FALSE;
		ImmReleaseContext(handle, context);
		ASSERT_TRUE(bQueried);
		EXPECT_EQ(candidate.ptCurrentPos.x, 30);
		EXPECT_EQ(candidate.ptCurrentPos.y, 60);
		EXPECT_EQ(candidate.rcArea.top, 40);
		EXPECT_EQ(candidate.rcArea.bottom, 60);
		SendMessageW(handle, WM_IME_STARTCOMPOSITION, 0, 0);
		state.Session = 92;
		ASSERT_TRUE(m_System.SetTextInputState(m_Window, state));
		const os::WindowEventBatch events = m_System.PollEvents();
		ASSERT_GE(events.Events.Size(), 2);
		EXPECT_EQ(events.Events[0].Kind, os::EWindowEventKind::CompositionStart);
		EXPECT_EQ(events.Events[1].Kind, os::EWindowEventKind::CompositionCancel);
		EXPECT_EQ(events.Events[1].Composition.Session, 91);
	}

	TEST_F(WindowsWindowSystemTests, ClipboardRoundTripRejectsEmbeddedNullAndOversizedReads)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		if (CountClipboardFormats() != 0)
		{
			GTEST_SKIP() << "Clipboard contains data that this test cannot restore completely.";
		}
		DWORD writtenSequence = 0;
		core::ScopeExit restore([&]() noexcept
		{
			if (writtenSequence != 0 && GetClipboardSequenceNumber() == writtenSequence && OpenClipboard(handle))
			{
				EmptyClipboard();
				CloseClipboard();
			}
		});
		const core::StringView text("\xED\x95\x9C\r\n\xF0\x9F\x99\x82");
		ASSERT_TRUE(m_System.WriteClipboardText(m_Window, text));
		writtenSequence = GetClipboardSequenceNumber();
		core::Result<core::String> read = m_System.ReadClipboardText(m_Window, m_Tracking, 1024);
		ASSERT_TRUE(read);
		EXPECT_EQ(read.Value().View(), text);
		EXPECT_FALSE(m_System.ReadClipboardText(m_Window, m_Tracking, 4));
		EXPECT_FALSE(m_System.WriteClipboardText(m_Window, core::StringView("x\0y", 3)));
		read = m_System.ReadClipboardText(m_Window, m_Tracking, 1024);
		ASSERT_TRUE(read);
		EXPECT_EQ(read.Value().View(), text);
	}

	TEST_F(WindowsWindowSystemTests, TextPublicationStopsBatchBeforeQueuedKey)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		os::WindowTextDocument document;
		document.Session = 111;
		document.Revision = 1;
		document.Text = "abc";
		ASSERT_TRUE(m_System.PublishTextDocument(m_Window, document));
		ASSERT_TRUE(m_System.SetTextInputState(m_Window, { .Session = 111, .CaretHeight = 24, .bEnabled = true }));
		for (core::uint32 index = 0; index < 8; ++index)
		{
			(void)m_System.PollEvents();
		}
		ASSERT_NE(PostMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 20)), FALSE);
		ASSERT_NE(PostMessageW(handle, WM_KEYDOWN, VK_LEFT, 1), FALSE);
		const os::WindowEventBatch batch = m_System.PollEvents();
		bool bPointer = false;
		bool bKey = false;
		for (const os::WindowEvent& event : batch.Events)
		{
			bPointer = bPointer || event.Kind == os::EWindowEventKind::PointerButtonChanged;
			bKey = bKey || event.Kind == os::EWindowEventKind::KeyChanged;
		}
		EXPECT_TRUE(bPointer);
		EXPECT_FALSE(bKey);
		MSG queued{};
		EXPECT_NE(PeekMessageW(&queued, handle, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE), FALSE);
		document.AnchorByte = 2;
		document.CaretByte = 2;
		ASSERT_TRUE(m_System.PublishTextDocument(m_Window, document));
		(void)m_System.PollEvents();
		EXPECT_EQ(PeekMessageW(&queued, handle, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE), FALSE);
		EXPECT_TRUE(m_System.PublishTextDocument(m_Window, {}));
	}

	TEST(WindowsWindowPointerTests, CompetingOwnerFailsWithoutChangingEitherWindow)
	{
		core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
		{
			os::WindowsWindowSystem first;
			os::WindowsWindowSystem second;
			ASSERT_TRUE(os::WindowsWindowSystem::TryCreate(memory, {}, &first));
			ASSERT_TRUE(os::WindowsWindowSystem::TryCreate(memory, {}, &second));
			os::WindowDesc desc;
			desc.Title = "Roxell pointer owner";
			desc.ClientExtent = { 320, 200 };
			core::Result<os::WindowId> firstResult = first.CreateWindow(desc);
			ASSERT_TRUE(firstResult);
			const os::WindowId firstWindow = firstResult.Value();
			core::ScopeExit destroyFirst([&]() noexcept { (void)first.DestroyWindow(firstWindow); });
			core::Result<os::WindowId> secondResult = second.CreateWindow(desc);
			ASSERT_TRUE(secondResult);
			const os::WindowId secondWindow = secondResult.Value();
			core::ScopeExit destroySecond([&]() noexcept { (void)second.DestroyWindow(secondWindow); });
			ASSERT_TRUE(first.SetPointerMode(firstWindow, os::EPointerMode::Relative));
			const core::Result<void> rejected = second.SetPointerMode(secondWindow, os::EPointerMode::Relative);
			ASSERT_FALSE(rejected);
			EXPECT_EQ(rejected.GetError().Code, core::EErrorCode::Conflict);
			EXPECT_EQ(rejected.GetError().DiagnosticID, "ROX-PLATFORM-WINDOW-POINTER-OWNER");
			const core::Result<os::WindowState> firstState = first.GetWindowState(firstWindow);
			const core::Result<os::WindowState> secondState = second.GetWindowState(secondWindow);
			ASSERT_TRUE(firstState);
			ASSERT_TRUE(secondState);
			EXPECT_EQ(firstState.Value().PointerMode, os::EPointerMode::Relative);
			EXPECT_EQ(secondState.Value().PointerMode, os::EPointerMode::Free);
			ASSERT_TRUE(first.SetPointerMode(firstWindow, os::EPointerMode::Free));
			EXPECT_TRUE(second.SetPointerMode(secondWindow, os::EPointerMode::Relative));
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST_F(WindowsWindowSystemTests, NativeMinimizeAndRestorePublishExtentEvents)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		SendMessageW(handle, WM_SIZE, SIZE_MINIMIZED, MAKELPARAM(0, 0));
		const os::WindowEventBatch minimized = m_System.PollEvents();
		bool bMinimized = false;
		for (const os::WindowEvent& event : minimized.Events)
		{
			if (event.Kind == os::EWindowEventKind::Resized)
			{
				bMinimized = event.Resize.bMinimized && event.Resize.ClientExtent == os::WindowExtent{};
			}
		}
		EXPECT_TRUE(bMinimized);
		SendMessageW(handle, WM_SIZE, SIZE_RESTORED, MAKELPARAM(320, 200));
		const os::WindowEventBatch restored = m_System.PollEvents();
		bool bRestored = false;
		for (const os::WindowEvent& event : restored.Events)
		{
			if (event.Kind == os::EWindowEventKind::Resized)
			{
				bRestored = !event.Resize.bMinimized && event.Resize.ClientExtent == os::WindowExtent{ 320, 200 };
			}
		}
		EXPECT_TRUE(bRestored);
	}

	TEST_F(WindowsWindowSystemTests, Utf8TitleIsPublishedToNativeWindow)
	{
		os::WindowDesc desc;
		desc.Title = "Roxell \xEC\xB0\xBD \xEA\xB3\x84\xEC\x95\xBD";
		desc.ClientExtent = { 320, 200 };
		core::Result<os::WindowId> created = m_System.CreateWindow(desc);
		ASSERT_TRUE(created);
		m_Window = created.Value();
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		wchar_t title[64]{};
		ASSERT_GT(GetWindowTextW(handle, title, 64), 0);
		EXPECT_STREQ(title, L"Roxell \uCC3D \uACC4\uC57D");
	}

	TEST_F(WindowsWindowSystemTests, CapacityFailurePreservesExistingWindow)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		os::WindowDesc desc;
		desc.Title = "second window";
		const core::Result<os::WindowId> rejected = m_System.CreateWindow(desc);
		ASSERT_FALSE(rejected);
		EXPECT_EQ(rejected.GetError().Code, core::EErrorCode::Conflict);
		EXPECT_EQ(rejected.GetError().DiagnosticID, "ROX-PLATFORM-WINDOW-CAPACITY");
		const core::Result<os::WindowState> state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_EQ(state.Value().ClientExtent, (os::WindowExtent{ 320, 200 }));
	}

	TEST_F(WindowsWindowSystemTests, VisibilityAndCloseRequestDoNotDestroyWindow)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		ASSERT_TRUE(m_System.SetWindowVisibility(m_Window, true));
		core::Result<os::WindowState> state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_TRUE(state.Value().bVisible);
		ASSERT_TRUE(m_System.SetWindowVisibility(m_Window, false));
		state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_FALSE(state.Value().bVisible);
		(void)m_System.PollEvents();
		SendMessageW(handle, WM_CLOSE, 0, 0);
		const os::WindowEventBatch batch = m_System.PollEvents();
		bool bCloseRequested = false;
		for (const os::WindowEvent& event : batch.Events)
		{
			bCloseRequested = bCloseRequested || event.Kind == os::EWindowEventKind::CloseRequested;
		}
		EXPECT_TRUE(bCloseRequested);
		EXPECT_FALSE(batch.bQuitRequested);
		EXPECT_NE(IsWindow(handle), FALSE);
	}

	TEST_F(WindowsWindowSystemTests, NativeKeysPreservePhysicalIdentityAndRepeat)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		SendMessageW(handle, WM_KEYDOWN, 'A', 0x001E0001);
		SendMessageW(handle, WM_KEYDOWN, 'A', 0x401E0001);
		SendMessageW(handle, WM_KEYUP, 'A', static_cast<LPARAM>(0xC01E0001u));
		const os::WindowEventBatch batch = m_System.PollEvents();
		core::uint32 index = 0;
		for (const os::WindowEvent& event : batch.Events)
		{
			if (event.Kind == os::EWindowEventKind::KeyChanged)
			{
				ASSERT_LT(index, 3);
				EXPECT_EQ(event.Key.Key, os::EPhysicalKey::A);
				EXPECT_EQ(event.Key.bPressed, index != 2);
				EXPECT_EQ(event.Key.bRepeated, index == 1);
				++index;
			}
		}
		EXPECT_EQ(index, 3);
	}

	TEST_F(WindowsWindowSystemTests, FocusLossCancelsHeldKeyAndPointerButton)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		SendMessageW(handle, WM_SETFOCUS, 0, 0);
		SendMessageW(handle, WM_KEYDOWN, 'W', 0x00110001);
		SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(9, 11));
		SendMessageW(handle, WM_KILLFOCUS, 0, 0);
		const os::WindowEventBatch batch = m_System.PollEvents();
		bool bKeyCancelled = false;
		bool bButtonCancelled = false;
		for (const os::WindowEvent& event : batch.Events)
		{
			if (event.Kind == os::EWindowEventKind::KeyChanged && event.Key.Key == os::EPhysicalKey::W && !event.Key.bPressed)
			{
				bKeyCancelled = event.Key.bCancelled;
			}
			if (event.Kind == os::EWindowEventKind::PointerButtonChanged && event.PointerButton.Button == os::EPointerButton::Left &&
				!event.PointerButton.bPressed)
			{
				bButtonCancelled = event.PointerButton.bCancelled;
			}
		}
		EXPECT_TRUE(bKeyCancelled);
		EXPECT_TRUE(bButtonCancelled);
		EXPECT_NE(GetCapture(), handle);
	}

	TEST_F(WindowsWindowSystemTests, PointerMotionReportsClientCoordinatesAndDeltas)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		SendMessageW(handle, WM_MOUSEMOVE, 0, MAKELPARAM(static_cast<WORD>(-5), 7));
		SendMessageW(handle, WM_MOUSEMOVE, 0, MAKELPARAM(3, 10));
		SendMessageW(handle, WM_MOUSELEAVE, 0, 0);
		const os::WindowEventBatch events = m_System.PollEvents();
		core::uint32 moves = 0;
		bool bLeft = false;
		for (const os::WindowEvent& event : events.Events)
		{
			if (event.Kind == os::EWindowEventKind::PointerMoved)
			{
				ASSERT_LT(moves, 2);
				EXPECT_EQ(event.PointerMove.X, moves == 0 ? -5 : 3);
				EXPECT_EQ(event.PointerMove.Y, moves == 0 ? 7 : 10);
				EXPECT_EQ(event.PointerMove.DeltaX, moves == 0 ? 0 : 8);
				EXPECT_EQ(event.PointerMove.DeltaY, moves == 0 ? 0 : 3);
				++moves;
			}
			bLeft = bLeft || event.Kind == os::EWindowEventKind::PointerLeft;
		}
		EXPECT_EQ(moves, 2);
		EXPECT_TRUE(bLeft);
	}

	TEST_F(WindowsWindowSystemTests, NativeWheelCoordinatesAreConvertedToClientSpace)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		POINT point{ -7, 19 };
		ASSERT_NE(ClientToScreen(handle, &point), FALSE);
		SendMessageW(handle, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(static_cast<WORD>(point.x), static_cast<WORD>(point.y)));
		const os::WindowEventBatch events = m_System.PollEvents();
		bool bFound = false;
		for (const os::WindowEvent& event : events.Events)
		{
			if (event.Kind == os::EWindowEventKind::PointerWheel)
			{
				bFound = true;
				EXPECT_TRUE(event.PointerWheel.bHasPosition);
				EXPECT_EQ(event.PointerWheel.Vertical, 1.0f);
				EXPECT_EQ(event.PointerWheel.X, -7);
				EXPECT_EQ(event.PointerWheel.Y, 19);
			}
		}
		EXPECT_TRUE(bFound);
	}

	TEST_F(WindowsWindowSystemTests, FocusLossCancelsActiveComposition)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		ASSERT_TRUE(m_System.SetTextInputState(m_Window, { .Session = 92, .CaretHeight = 20, .bEnabled = true }));
		SendMessageW(handle, WM_IME_STARTCOMPOSITION, 0, 0);
		SendMessageW(handle, WM_KILLFOCUS, 0, 0);
		const os::WindowEventBatch events = m_System.PollEvents();
		bool bCancelled = false;
		for (const os::WindowEvent& event : events.Events)
		{
			bCancelled = bCancelled || (event.Kind == os::EWindowEventKind::CompositionCancel && event.Composition.Session == 92);
		}
		EXPECT_TRUE(bCancelled);
		SendMessageW(handle, WM_IME_CHAR, L'X', 0);
		const os::WindowEventBatch afterFocus = m_System.PollEvents();
		for (const os::WindowEvent& event : afterFocus.Events)
		{
			EXPECT_FALSE(event.Kind == os::EWindowEventKind::TextInput && event.Text.CodePoint == 'X');
		}
	}

	TEST_F(WindowsWindowSystemTests, RelativePointerReconcilesFocusLossAndRestoration)
	{
		RECT previousClip{};
		ASSERT_NE(GetClipCursor(&previousClip), FALSE);
		const int desktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int desktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		if (previousClip.right - previousClip.left < desktopWidth || previousClip.bottom - previousClip.top < desktopHeight)
		{
			GTEST_SKIP() << "Existing cursor confinement cannot be safely disturbed.";
		}
		core::ScopeExit restore([]() noexcept { (void)ClipCursor(nullptr); });
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		SendMessageW(handle, WM_SETFOCUS, 0, 0);
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		core::Result<os::WindowState> state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_TRUE(state.Value().bPointerRelativeActive);
		SendMessageW(handle, WM_KILLFOCUS, 0, 0);
		state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_EQ(state.Value().PointerMode, os::EPointerMode::Relative);
		EXPECT_FALSE(state.Value().bPointerRelativeActive);
		RECT clip{};
		ASSERT_NE(GetClipCursor(&clip), FALSE);
		EXPECT_GE(clip.right - clip.left, desktopWidth);
		EXPECT_GE(clip.bottom - clip.top, desktopHeight);
		SendMessageW(handle, WM_SETFOCUS, 0, 0);
		state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_TRUE(state.Value().bPointerRelativeActive);
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Free));
		state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_FALSE(state.Value().bPointerRelativeActive);
	}

	TEST_F(WindowsWindowSystemTests, CursorHideFailurePreservesCounterAndCanRecover)
	{
		RECT previousClip{};
		ASSERT_NE(GetClipCursor(&previousClip), FALSE);
		if (previousClip.right - previousClip.left < GetSystemMetrics(SM_CXVIRTUALSCREEN) || previousClip.bottom - previousClip.top < GetSystemMetrics(SM_CYVIRTUALSCREEN))
		{
			GTEST_SKIP() << "Existing cursor confinement cannot be safely disturbed.";
		}
		const auto currentCount = []() noexcept
		{
			(void)ShowCursor(TRUE);
			return ShowCursor(FALSE);
		};
		const auto setCount = [&](const int target) noexcept
		{
			for (int attempt = 0; attempt < 512; ++attempt)
			{
				const int current = currentCount();
				if (current == target)
				{
					return true;
				}
				(void)ShowCursor(current < target ? TRUE : FALSE);
			}
			return false;
		};
		const int initial = currentCount();
		ASSERT_NO_FATAL_FAILURE(createWindow());
		core::ScopeExit restore([&]() noexcept
		{
			(void)m_System.SetPointerMode(m_Window, os::EPointerMode::Free);
			(void)setCount(initial);
			(void)ClipCursor(nullptr);
		});
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		SendMessageW(reinterpret_cast<HWND>(native.Value().Window), WM_SETFOCUS, 0, 0);
		ASSERT_TRUE(setCount(100));
		const core::Result<void> rejected = m_System.SetPointerMode(m_Window, os::EPointerMode::Relative);
		ASSERT_FALSE(rejected);
		EXPECT_EQ(rejected.GetError().Code, core::EErrorCode::ExternalFailure);
		EXPECT_EQ(rejected.GetError().DiagnosticID, "ROX-PLATFORM-WINDOW-POINTER-CURSOR");
		EXPECT_EQ(currentCount(), 100);
		core::Result<os::WindowState> state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_FALSE(state.Value().bPointerRelativeActive);
		ASSERT_TRUE(setCount(0));
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_TRUE(state.Value().bPointerRelativeActive);
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Free));
		EXPECT_EQ(currentCount(), 0);
	}

	bool checkPointerThreadGone()
	{
		rox::core::Atomic<bool> bPinned{ false };
		std::thread owner([&]()
		{
			os::WindowsWindowSystem system;
			if (!os::WindowsWindowSystem::TryCreate(core::SystemMemoryResource::Get(), {}, &system))
			{
				return;
			}
			os::WindowDesc desc;
			desc.Title = "Roxell pointer owner thread";
			core::Result<os::WindowId> created = system.CreateWindow(desc);
			if (!created)
			{
				return;
			}
			const os::WindowId window = created.Value();
			core::ScopeExit destroy([&]() noexcept { (void)system.DestroyWindow(window); });
			if (system.SetPointerMode(window, os::EPointerMode::Relative) &&
				system.SetPointerMode(window, os::EPointerMode::Free))
			{
				bPinned.store(true);
			}
		});
		owner.join();
		if (!bPinned.load())
		{
			return false;
		}
		os::WindowsWindowSystem system;
		if (!os::WindowsWindowSystem::TryCreate(core::SystemMemoryResource::Get(), {}, &system))
		{
			return false;
		}
		os::WindowDesc desc;
		desc.Title = "Roxell pointer successor thread";
		core::Result<os::WindowId> created = system.CreateWindow(desc);
		if (!created)
		{
			return false;
		}
		const os::WindowId window = created.Value();
		core::ScopeExit destroy([&]() noexcept { (void)system.DestroyWindow(window); });
		const core::Result<void> rejected = system.SetPointerMode(window, os::EPointerMode::Relative);
		const core::Result<os::WindowState> state = system.GetWindowState(window);
		return !rejected && rejected.GetError().Code == core::EErrorCode::Unsupported &&
			rejected.GetError().DiagnosticID == "ROX-PLATFORM-WINDOW-POINTER-THREAD-GONE" && state &&
			state.Value().PointerMode == os::EPointerMode::Free && !state.Value().bPointerRelativeActive;
	}

	TEST(WindowsWindowPointerTests, OwnerThreadExitPermanentlyRejectsRelativeMode)
	{
		EXPECT_EXIT(ExitProcess(checkPointerThreadGone() ? 0 : 1), ::testing::ExitedWithCode(0), "");
	}

	TEST_F(WindowsWindowSystemTests, RelativePointerClipTracksWindowMoveAndDpiChange)
	{
		RECT previousClip{};
		ASSERT_NE(GetClipCursor(&previousClip), FALSE);
		if (previousClip.right - previousClip.left < GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
			previousClip.bottom - previousClip.top < GetSystemMetrics(SM_CYVIRTUALSCREEN))
		{
			GTEST_SKIP() << "Existing cursor confinement cannot be safely disturbed.";
		}
		ASSERT_NO_FATAL_FAILURE(createWindow());
		core::ScopeExit restore([&]() noexcept
		{
			(void)m_System.SetPointerMode(m_Window, os::EPointerMode::Free);
			(void)ClipCursor(nullptr);
		});
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		SendMessageW(handle, WM_SETFOCUS, 0, 0);
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		const auto expectClientClip = [&]()
		{
			RECT client{};
			ASSERT_NE(GetClientRect(handle, &client), FALSE);
			POINT topLeft{ client.left, client.top };
			POINT bottomRight{ client.right, client.bottom };
			ASSERT_NE(ClientToScreen(handle, &topLeft), FALSE);
			ASSERT_NE(ClientToScreen(handle, &bottomRight), FALSE);
			RECT clip{};
			ASSERT_NE(GetClipCursor(&clip), FALSE);
			EXPECT_EQ(clip.left, topLeft.x);
			EXPECT_EQ(clip.top, topLeft.y);
			EXPECT_EQ(clip.right, bottomRight.x);
			EXPECT_EQ(clip.bottom, bottomRight.y);
		};
		RECT outer{};
		ASSERT_NE(GetWindowRect(handle, &outer), FALSE);
		ASSERT_NE(SetWindowPos(handle, nullptr, outer.left + 37, outer.top + 23, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE), FALSE);
		ASSERT_NO_FATAL_FAILURE(expectClientClip());
		ASSERT_NE(GetWindowRect(handle, &outer), FALSE);
		RECT suggested{ outer.left + 51, outer.top + 19, outer.right + 51, outer.bottom + 19 };
		(void)m_System.PollEvents();
		SendMessageW(handle, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&suggested));
		ASSERT_NO_FATAL_FAILURE(expectClientClip());
		const os::WindowEventBatch batch = m_System.PollEvents();
		bool bDpiChanged = false;
		for (const os::WindowEvent& event : batch.Events)
		{
			if (event.Kind == os::EWindowEventKind::DpiChanged)
			{
				bDpiChanged = true;
				EXPECT_EQ(event.Dpi.DpiX, 144);
				EXPECT_EQ(event.Dpi.DpiY, 144);
			}
		}
		EXPECT_TRUE(bDpiChanged);
	}

	TEST_F(WindowsWindowSystemTests, OtherThreadCannotClaimRelativePointerAndCanStillResize)
	{
		RECT previousClip{};
		ASSERT_NE(GetClipCursor(&previousClip), FALSE);
		if (previousClip.right - previousClip.left < GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
			previousClip.bottom - previousClip.top < GetSystemMetrics(SM_CYVIRTUALSCREEN))
		{
			GTEST_SKIP() << "Existing cursor confinement cannot be safely disturbed.";
		}
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		SendMessageW(handle, WM_SETFOCUS, 0, 0);
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		core::ScopeExit release([&]() noexcept
		{
			(void)m_System.SetPointerMode(m_Window, os::EPointerMode::Free);
			(void)ClipCursor(nullptr);
		});
		rox::core::Atomic<bool> bRejected{ false };
		rox::core::Atomic<bool> bResizeSucceeded{ false };
		std::thread worker([&]()
		{
			os::WindowsWindowSystem system;
			if (!os::WindowsWindowSystem::TryCreate(core::SystemMemoryResource::Get(), {}, &system))
			{
				return;
			}
			os::WindowDesc desc;
			desc.Title = "Roxell other thread";
			core::Result<os::WindowId> created = system.CreateWindow(desc);
			if (!created)
			{
				return;
			}
			const os::WindowId window = created.Value();
			core::ScopeExit destroy([&]() noexcept { (void)system.DestroyWindow(window); });
			const core::Result<void> rejected = system.SetPointerMode(window, os::EPointerMode::Relative);
			bRejected.store(!rejected && rejected.GetError().Code == core::EErrorCode::Unsupported &&
				rejected.GetError().DiagnosticID == "ROX-PLATFORM-WINDOW-POINTER-THREAD");
			if (system.ResizeWindow(window, { 240, 160 }))
			{
				const core::Result<os::WindowState> state = system.GetWindowState(window);
				bResizeSucceeded.store(state && state.Value().ClientExtent == os::WindowExtent{ 240, 160 });
			}
		});
		worker.join();
		EXPECT_TRUE(bRejected.load());
		EXPECT_TRUE(bResizeSucceeded.load());
		const core::Result<os::WindowState> state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_EQ(state.Value().PointerMode, os::EPointerMode::Relative);
		EXPECT_TRUE(state.Value().bPointerRelativeActive);
		RAWINPUTDEVICE devices[16]{};
		UINT count = 16;
		const UINT returned = GetRegisteredRawInputDevices(devices, &count, sizeof(RAWINPUTDEVICE));
		ASSERT_NE(returned, static_cast<UINT>(-1));
		bool bRegistered = false;
		for (UINT index = 0; index < returned; ++index)
		{
			if (devices[index].usUsagePage == 1 && devices[index].usUsage == 2)
			{
				bRegistered = true;
				EXPECT_EQ(devices[index].hwndTarget, handle);
				EXPECT_EQ(devices[index].dwFlags, 0);
			}
		}
		EXPECT_TRUE(bRegistered);
	}

	TEST_F(WindowsWindowSystemTests, AllNativePointerButtonsPublishPressAndRelease)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		struct ButtonCase
		{
			UINT Down;
			UINT Up;
			WPARAM Parameter;
			os::EPointerButton Button;
		};
		const ButtonCase cases[] = {
			{ WM_LBUTTONDOWN, WM_LBUTTONUP, 0, os::EPointerButton::Left },
			{ WM_RBUTTONDOWN, WM_RBUTTONUP, 0, os::EPointerButton::Right },
			{ WM_MBUTTONDOWN, WM_MBUTTONUP, 0, os::EPointerButton::Middle },
			{ WM_XBUTTONDOWN, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), os::EPointerButton::X1 },
			{ WM_XBUTTONDOWN, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), os::EPointerButton::X2 }
		};
		for (const ButtonCase& item : cases)
		{
			SendMessageW(handle, item.Down, item.Parameter, MAKELPARAM(3, 10));
			SendMessageW(handle, item.Up, item.Parameter, MAKELPARAM(3, 10));
			const os::WindowEventBatch events = m_System.PollEvents();
			bool bPressed = false;
			bool bReleased = false;
			for (const os::WindowEvent& event : events.Events)
			{
				if (event.Kind == os::EWindowEventKind::PointerButtonChanged && event.PointerButton.Button == item.Button)
				{
					bPressed = bPressed || event.PointerButton.bPressed;
					bReleased = bReleased || !event.PointerButton.bPressed;
				}
			}
			EXPECT_TRUE(bPressed) << static_cast<core::uint32>(item.Button);
			EXPECT_TRUE(bReleased) << static_cast<core::uint32>(item.Button);
		}
	}

	TEST_F(WindowsWindowSystemTests, HorizontalWheelReportsClientCoordinates)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		POINT point{ 31, 27 };
		ASSERT_NE(ClientToScreen(handle, &point), FALSE);
		SendMessageW(handle, WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), MAKELPARAM(static_cast<WORD>(point.x), static_cast<WORD>(point.y)));
		const os::WindowEventBatch events = m_System.PollEvents();
		bool bFound = false;
		for (const os::WindowEvent& event : events.Events)
		{
			if (event.Kind == os::EWindowEventKind::PointerWheel)
			{
				bFound = true;
				EXPECT_TRUE(event.PointerWheel.bHasPosition);
				EXPECT_EQ(event.PointerWheel.Horizontal, -1.0f);
				EXPECT_EQ(event.PointerWheel.X, 31);
				EXPECT_EQ(event.PointerWheel.Y, 27);
			}
		}
		EXPECT_TRUE(bFound);
	}

	TEST_F(WindowsWindowSystemTests, ImeEndCancelsCompositionAndInvalidSessionIsRejected)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		os::WindowTextInputState state{ .Session = 91, .CaretHeight = 20, .bEnabled = true };
		ASSERT_TRUE(m_System.SetTextInputState(m_Window, state));
		SendMessageW(handle, WM_IME_STARTCOMPOSITION, 0, 0);
		SendMessageW(handle, WM_IME_ENDCOMPOSITION, 0, 0);
		const os::WindowEventBatch events = m_System.PollEvents();
		ASSERT_EQ(events.Events.Size(), 2);
		EXPECT_EQ(events.Events[0].Kind, os::EWindowEventKind::CompositionStart);
		EXPECT_EQ(events.Events[1].Kind, os::EWindowEventKind::CompositionCancel);
		EXPECT_EQ(events.Events[0].Composition.Session, 91);
		EXPECT_EQ(events.Events[1].Composition.Session, 91);
		state.Session = 0;
		const core::Result<void> rejected = m_System.SetTextInputState(m_Window, state);
		ASSERT_FALSE(rejected);
		EXPECT_EQ(rejected.GetError().Code, core::EErrorCode::InvalidArgument);
		EXPECT_EQ(rejected.GetError().DiagnosticID, "platform.window.text");
		SendMessageW(handle, WM_IME_STARTCOMPOSITION, 0, 0);
		const os::WindowEventBatch preserved = m_System.PollEvents();
		ASSERT_FALSE(preserved.Events.IsEmpty());
		EXPECT_EQ(preserved.Events[0].Kind, os::EWindowEventKind::CompositionStart);
		EXPECT_EQ(preserved.Events[0].Composition.Session, 91);
		state.bEnabled = false;
		EXPECT_TRUE(m_System.SetTextInputState(m_Window, state));
	}

	TEST_F(WindowsWindowSystemTests, NativeRelativeMouseInputReportsDeviceCounts)
	{
		RECT previousClip{};
		ASSERT_NE(GetClipCursor(&previousClip), FALSE);
		if (previousClip.right - previousClip.left < GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
			previousClip.bottom - previousClip.top < GetSystemMetrics(SM_CYVIRTUALSCREEN))
		{
			GTEST_SKIP() << "Existing cursor confinement cannot be safely disturbed.";
		}
		POINT previousCursor{};
		ASSERT_NE(GetCursorPos(&previousCursor), FALSE);
		const HWND previousForeground = GetForegroundWindow();
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		core::ScopeExit restore([&]() noexcept
		{
			(void)m_System.SetPointerMode(m_Window, os::EPointerMode::Free);
			(void)ClipCursor(nullptr);
			(void)SetCursorPos(previousCursor.x, previousCursor.y);
			if (previousForeground != nullptr && IsWindow(previousForeground))
			{
				(void)SetForegroundWindow(previousForeground);
			}
		});
		ASSERT_TRUE(m_System.SetWindowVisibility(m_Window, true));
		if (!SetForegroundWindow(handle))
		{
			GTEST_SKIP() << "The desktop did not permit this test window to take foreground focus.";
		}
		(void)SetFocus(handle);
		(void)m_System.PollEvents();
		const core::Result<os::WindowState> state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		if (!state.Value().bFocused)
		{
			GTEST_SKIP() << "The test window did not receive focus required by raw input.";
		}
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		(void)m_System.PollEvents();
		INPUT input{};
		input.type = INPUT_MOUSE;
		input.mi.dx = 7;
		input.mi.dy = -3;
		input.mi.dwFlags = MOUSEEVENTF_MOVE;
		ASSERT_EQ(SendInput(1, &input, sizeof(INPUT)), 1);
		bool bFound = false;
		for (core::uint32 attempt = 0; attempt < 200 && !bFound; ++attempt)
		{
			const os::WindowEventBatch events = m_System.PollEvents();
			for (const os::WindowEvent& event : events.Events)
			{
				if (event.Kind == os::EWindowEventKind::PointerRelativeMoved)
				{
					EXPECT_FALSE(bFound);
					bFound = true;
					EXPECT_EQ(event.PointerRelative.DeltaX, 7);
					EXPECT_EQ(event.PointerRelative.DeltaY, -3);
					EXPECT_EQ(event.PointerRelative.Source, os::EPointerRelativeSource::DeviceCounts);
				}
			}
			if (!bFound)
			{
				Sleep(5);
			}
		}
		EXPECT_TRUE(bFound);
	}

	TEST_F(WindowsWindowSystemTests, RelativePointerLeavesOnMinimizeAndReentersOnRestore)
	{
		RECT previousClip{};
		ASSERT_NE(GetClipCursor(&previousClip), FALSE);
		const int desktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int desktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		if (previousClip.right - previousClip.left < desktopWidth || previousClip.bottom - previousClip.top < desktopHeight)
		{
			GTEST_SKIP() << "Existing cursor confinement cannot be safely disturbed.";
		}
		ASSERT_NO_FATAL_FAILURE(createWindow());
		core::ScopeExit restore([&]() noexcept
		{
			(void)m_System.SetPointerMode(m_Window, os::EPointerMode::Free);
			(void)ClipCursor(nullptr);
		});
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		const auto mouseTarget = []() noexcept -> HWND
		{
			RAWINPUTDEVICE devices[16]{};
			UINT count = 16;
			const UINT returned = GetRegisteredRawInputDevices(devices, &count, sizeof(RAWINPUTDEVICE));
			if (returned == static_cast<UINT>(-1))
			{
				return nullptr;
			}
			for (UINT index = 0; index < returned; ++index)
			{
				if (devices[index].usUsagePage == 1 && devices[index].usUsage == 2)
				{
					return devices[index].hwndTarget;
				}
			}
			return nullptr;
		};
		SendMessageW(handle, WM_SETFOCUS, 0, 0);
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		EXPECT_EQ(mouseTarget(), handle);
		SendMessageW(handle, WM_SIZE, SIZE_MINIMIZED, MAKELPARAM(0, 0));
		core::Result<os::WindowState> state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_EQ(state.Value().PointerMode, os::EPointerMode::Relative);
		EXPECT_FALSE(state.Value().bPointerRelativeActive);
		EXPECT_EQ(mouseTarget(), nullptr);
		SendMessageW(handle, WM_KILLFOCUS, 0, 0);
		SendMessageW(handle, WM_SETFOCUS, 0, 0);
		state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_FALSE(state.Value().bPointerRelativeActive);
		EXPECT_EQ(mouseTarget(), nullptr);
		RECT clip{};
		ASSERT_NE(GetClipCursor(&clip), FALSE);
		EXPECT_GE(clip.right - clip.left, desktopWidth);
		EXPECT_GE(clip.bottom - clip.top, desktopHeight);
		SendMessageW(handle, WM_SIZE, SIZE_RESTORED, MAKELPARAM(320, 200));
		state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		EXPECT_TRUE(state.Value().bPointerRelativeActive);
		EXPECT_EQ(mouseTarget(), handle);
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Free));
		EXPECT_EQ(mouseTarget(), nullptr);
	}

	struct RawPointerSample
	{
		HANDLE Device = nullptr;
		USHORT Flags = 0;
		LONG X = 0;
		LONG Y = 0;
	};

	thread_local WNDPROC s_PreviousWindowProcedure = nullptr;
	thread_local RawPointerSample s_RawSamples[8]{};
	thread_local core::uint32 s_RawSampleCount = 0;

	LRESULT CALLBACK traceRawPointer(HWND window, UINT message, WPARAM parameter, LPARAM data)
	{
		if (message == WM_INPUT && s_RawSampleCount < 8)
		{
			RAWINPUT input{};
			UINT size = sizeof(input);
			if (GetRawInputData(reinterpret_cast<HRAWINPUT>(data), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) && input.header.dwType == RIM_TYPEMOUSE)
			{
				s_RawSamples[s_RawSampleCount++] = { input.header.hDevice, input.data.mouse.usFlags, input.data.mouse.lLastX, input.data.mouse.lLastY };
			}
		}
		return CallWindowProcW(s_PreviousWindowProcedure, window, message, parameter, data);
	}

	TEST_F(WindowsWindowSystemTests, AbsoluteRawSamplesReportDifferencesWithinCoordinateSpace)
	{
		RECT previousClip{};
		ASSERT_NE(GetClipCursor(&previousClip), FALSE);
		if (previousClip.right - previousClip.left < GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
			previousClip.bottom - previousClip.top < GetSystemMetrics(SM_CYVIRTUALSCREEN))
		{
			GTEST_SKIP() << "Existing cursor confinement cannot be safely disturbed.";
		}
		POINT previousCursor{};
		ASSERT_NE(GetCursorPos(&previousCursor), FALSE);
		const HWND previousForeground = GetForegroundWindow();
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		core::ScopeExit restore([&]() noexcept
		{
			if (s_PreviousWindowProcedure != nullptr)
			{
				(void)SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_PreviousWindowProcedure));
				s_PreviousWindowProcedure = nullptr;
			}
			(void)m_System.SetPointerMode(m_Window, os::EPointerMode::Free);
			(void)ClipCursor(nullptr);
			(void)SetCursorPos(previousCursor.x, previousCursor.y);
			if (previousForeground != nullptr && IsWindow(previousForeground))
			{
				(void)SetForegroundWindow(previousForeground);
			}
		});
		ASSERT_TRUE(m_System.SetWindowVisibility(m_Window, true));
		if (!SetForegroundWindow(handle))
		{
			GTEST_SKIP() << "The desktop did not permit foreground focus.";
		}
		(void)SetFocus(handle);
		(void)m_System.PollEvents();
		const core::Result<os::WindowState> state = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(state);
		if (!state.Value().bFocused)
		{
			GTEST_SKIP() << "Raw input requires a focused test window.";
		}
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		(void)m_System.PollEvents();
		s_RawSampleCount = 0;
		s_PreviousWindowProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&traceRawPointer)));
		ASSERT_NE(s_PreviousWindowProcedure, nullptr);
		const auto sendSample = [&](const LONG x, const LONG y, const bool bVirtual, const bool bExpectDelta)
		{
			const core::uint32 previousCount = s_RawSampleCount;
			INPUT input{};
			input.type = INPUT_MOUSE;
			input.mi.dx = x;
			input.mi.dy = y;
			input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | (bVirtual ? MOUSEEVENTF_VIRTUALDESK : 0);
			ASSERT_EQ(SendInput(1, &input, sizeof(INPUT)), 1);
			bool bDelta = false;
			core::int32 deltaX = 0;
			core::int32 deltaY = 0;
			for (core::uint32 attempt = 0; attempt < 200 && s_RawSampleCount == previousCount; ++attempt)
			{
				const os::WindowEventBatch events = m_System.PollEvents();
				for (const os::WindowEvent& event : events.Events)
				{
					if (event.Kind == os::EWindowEventKind::PointerRelativeMoved)
					{
						bDelta = true;
						deltaX = event.PointerRelative.DeltaX;
						deltaY = event.PointerRelative.DeltaY;
						EXPECT_EQ(event.PointerRelative.Source, os::EPointerRelativeSource::NormalizedCoordinates);
					}
				}
				if (s_RawSampleCount == previousCount)
				{
					Sleep(5);
				}
			}
			ASSERT_EQ(s_RawSampleCount, previousCount + 1);
			const RawPointerSample& current = s_RawSamples[previousCount];
			EXPECT_NE(current.Flags & MOUSE_MOVE_ABSOLUTE, 0);
			EXPECT_EQ((current.Flags & MOUSE_VIRTUAL_DESKTOP) != 0, bVirtual);
			EXPECT_EQ(bDelta, bExpectDelta);
			if (bExpectDelta)
			{
				ASSERT_GT(previousCount, 0);
				const RawPointerSample& previous = s_RawSamples[previousCount - 1];
				EXPECT_EQ(current.Device, previous.Device);
				EXPECT_EQ(deltaX, current.X - previous.X);
				EXPECT_EQ(deltaY, current.Y - previous.Y);
			}
		};
		ASSERT_NO_FATAL_FAILURE(sendSample(30000, 20000, false, false));
		ASSERT_NO_FATAL_FAILURE(sendSample(30500, 20250, false, true));
		ASSERT_NO_FATAL_FAILURE(sendSample(31000, 20500, true, false));
		ASSERT_NO_FATAL_FAILURE(sendSample(31250, 20600, true, true));
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Free));
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		ASSERT_NO_FATAL_FAILURE(sendSample(31500, 20700, true, false));
	}

	TEST_F(WindowsWindowSystemTests, ExtendedControlScanCodeReportsRightControl)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		const HWND handle = reinterpret_cast<HWND>(native.Value().Window);
		(void)m_System.PollEvents();
		SendMessageW(handle, WM_KEYDOWN, VK_RCONTROL, 0x011D0001);
		SendMessageW(handle, WM_KEYUP, VK_RCONTROL, static_cast<LPARAM>(0xC11D0001u));
		const os::WindowEventBatch events = m_System.PollEvents();
		core::uint32 count = 0;
		for (const os::WindowEvent& event : events.Events)
		{
			if (event.Kind == os::EWindowEventKind::KeyChanged)
			{
				EXPECT_EQ(event.Key.Key, os::EPhysicalKey::RightControl);
				EXPECT_EQ(event.Key.bPressed, count == 0);
				++count;
			}
		}
		EXPECT_EQ(count, 2);
	}

	TEST_F(WindowsWindowSystemTests, MalformedAndForeignIdsTriggerOwnershipAssertions)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		EXPECT_DEATH((void)m_System.GetWindowState({}), "Window access requires a valid handle owned by this window system");
		os::WindowId foreign = m_Window;
		++foreign.Owner;
		EXPECT_DEATH((void)m_System.GetNativeHandle(foreign), "Window access requires a valid handle owned by this window system");
	}

	TEST_F(WindowsWindowSystemTests, RepeatedPointerModeRequestsAreIdempotent)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		core::ScopeExit release([&]() noexcept { (void)m_System.SetPointerMode(m_Window, os::EPointerMode::Free); });
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Free));
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Free));
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		const core::Result<os::WindowState> requested = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(requested);
		EXPECT_EQ(requested.Value().PointerMode, os::EPointerMode::Relative);
		EXPECT_FALSE(requested.Value().bPointerRelativeActive);
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Free));
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Free));
		const core::Result<os::WindowState> released = m_System.GetWindowState(m_Window);
		ASSERT_TRUE(released);
		EXPECT_EQ(released.Value().PointerMode, os::EPointerMode::Free);
		EXPECT_FALSE(released.Value().bPointerRelativeActive);
	}

	TEST(WindowsWindowSystemCreationTests, ZeroCapacityIsRejectedWithoutAllocations)
	{
		core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
		{
			os::WindowsWindowSystem system;
			os::WindowsWindowSystemOptions options;
			options.MaximumWindows = 0;
			const core::Result<void> rejected = os::WindowsWindowSystem::TryCreate(memory, options, &system);
			ASSERT_FALSE(rejected);
			EXPECT_EQ(rejected.GetError().Code, core::EErrorCode::InvalidArgument);
			EXPECT_EQ(rejected.GetError().DiagnosticID, "ROX-PLATFORM-WINDOW-OPTIONS");
			EXPECT_FALSE(system.IsValid());
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST(WindowsWindowPointerTests, ReleasingOwnerAllowsAnotherFocusedWindowToEnter)
	{
		RECT previousClip{};
		ASSERT_NE(GetClipCursor(&previousClip), FALSE);
		if (previousClip.right - previousClip.left < GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
			previousClip.bottom - previousClip.top < GetSystemMetrics(SM_CYVIRTUALSCREEN))
		{
			GTEST_SKIP() << "Existing cursor confinement cannot be safely disturbed.";
		}
		core::TrackingMemoryResource memory(core::SystemMemoryResource::Get());
		{
			os::WindowsWindowSystem first;
			os::WindowsWindowSystem second;
			ASSERT_TRUE(os::WindowsWindowSystem::TryCreate(memory, {}, &first));
			ASSERT_TRUE(os::WindowsWindowSystem::TryCreate(memory, {}, &second));
			os::WindowDesc desc;
			desc.Title = "Roxell pointer transfer";
			core::Result<os::WindowId> firstResult = first.CreateWindow(desc);
			ASSERT_TRUE(firstResult);
			const os::WindowId firstWindow = firstResult.Value();
			core::ScopeExit destroyFirst([&]() noexcept { (void)first.DestroyWindow(firstWindow); });
			core::Result<os::WindowId> secondResult = second.CreateWindow(desc);
			ASSERT_TRUE(secondResult);
			const os::WindowId secondWindow = secondResult.Value();
			core::ScopeExit destroySecond([&]() noexcept { (void)second.DestroyWindow(secondWindow); });
			core::ScopeExit release([&]() noexcept
			{
				(void)first.SetPointerMode(firstWindow, os::EPointerMode::Free);
				(void)second.SetPointerMode(secondWindow, os::EPointerMode::Free);
				(void)ClipCursor(nullptr);
			});
			const core::Result<os::NativeWindowHandle> firstNative = first.GetNativeHandle(firstWindow);
			const core::Result<os::NativeWindowHandle> secondNative = second.GetNativeHandle(secondWindow);
			ASSERT_TRUE(firstNative);
			ASSERT_TRUE(secondNative);
			SendMessageW(reinterpret_cast<HWND>(firstNative.Value().Window), WM_SETFOCUS, 0, 0);
			ASSERT_TRUE(first.SetPointerMode(firstWindow, os::EPointerMode::Relative));
			const core::Result<void> rejected = second.SetPointerMode(secondWindow, os::EPointerMode::Relative);
			ASSERT_FALSE(rejected);
			EXPECT_EQ(rejected.GetError().Code, core::EErrorCode::Conflict);
			EXPECT_EQ(rejected.GetError().DiagnosticID, "ROX-PLATFORM-WINDOW-POINTER-OWNER");
			ASSERT_TRUE(first.SetPointerMode(firstWindow, os::EPointerMode::Free));
			SendMessageW(reinterpret_cast<HWND>(secondNative.Value().Window), WM_SETFOCUS, 0, 0);
			ASSERT_TRUE(second.SetPointerMode(secondWindow, os::EPointerMode::Relative));
			const core::Result<os::WindowState> state = second.GetWindowState(secondWindow);
			ASSERT_TRUE(state);
			EXPECT_TRUE(state.Value().bPointerRelativeActive);
		}
		EXPECT_EQ(memory.GetStatistics().LiveAllocationCount, 0);
	}

	TEST_F(WindowsWindowSystemTests, OtherThreadRemainsIneligibleAfterOwnerReleasesClaim)
	{
		ASSERT_NO_FATAL_FAILURE(createWindow());
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		core::ScopeExit release([&]() noexcept { (void)m_System.SetPointerMode(m_Window, os::EPointerMode::Free); });
		rox::core::Atomic<bool> bReady{ false };
		rox::core::Atomic<bool> bContinue{ false };
		rox::core::Atomic<bool> bFirstRejected{ false };
		rox::core::Atomic<bool> bSecondRejected{ false };
		std::thread worker([&]()
		{
			core::ScopeExit signal([&]() noexcept { bReady.store(true); });
			os::WindowsWindowSystem system;
			if (!os::WindowsWindowSystem::TryCreate(core::SystemMemoryResource::Get(), {}, &system))
			{
				return;
			}
			os::WindowDesc desc;
			desc.Title = "Roxell thread affinity";
			core::Result<os::WindowId> created = system.CreateWindow(desc);
			if (!created)
			{
				return;
			}
			const os::WindowId window = created.Value();
			core::ScopeExit destroy([&]() noexcept { (void)system.DestroyWindow(window); });
			const core::Result<void> first = system.SetPointerMode(window, os::EPointerMode::Relative);
			bFirstRejected.store(!first && first.GetError().Code == core::EErrorCode::Unsupported && first.GetError().DiagnosticID == "ROX-PLATFORM-WINDOW-POINTER-THREAD");
			bReady.store(true);
			const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
			while (!bContinue.load() && rox::core::MonotonicClock::now() < deadline)
			{
				rox::core::YieldThread();
			}
			if (!bContinue.load())
			{
				return;
			}
			const core::Result<void> second = system.SetPointerMode(window, os::EPointerMode::Relative);
			bSecondRejected.store(!second && second.GetError().Code == core::EErrorCode::Unsupported && second.GetError().DiagnosticID == "ROX-PLATFORM-WINDOW-POINTER-THREAD");
		});
		core::ScopeExit join([&]() noexcept
		{
			bContinue.store(true);
			if (worker.joinable())
			{
				worker.join();
			}
		});
		const rox::core::MonotonicClock::time_point deadline = rox::core::MonotonicClock::now() + rox::core::Seconds(10);
		while (!bReady.load() && rox::core::MonotonicClock::now() < deadline)
		{
			rox::core::YieldThread();
		}
		ASSERT_TRUE(bReady.load());
		EXPECT_TRUE(bFirstRejected.load());
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Free));
		bContinue.store(true);
		worker.join();
		EXPECT_TRUE(bSecondRejected.load());
	}

	TEST_F(WindowsWindowSystemTests, RelativePointerRestoresPriorCursorDisplayCount)
	{
		RECT previousClip{};
		ASSERT_NE(GetClipCursor(&previousClip), FALSE);
		if (previousClip.right - previousClip.left < GetSystemMetrics(SM_CXVIRTUALSCREEN) ||
			previousClip.bottom - previousClip.top < GetSystemMetrics(SM_CYVIRTUALSCREEN))
		{
			GTEST_SKIP() << "Existing cursor confinement cannot be safely disturbed.";
		}
		const auto currentCount = []() noexcept
		{
			(void)ShowCursor(TRUE);
			return ShowCursor(FALSE);
		};
		const auto setCount = [&](const int target) noexcept
		{
			for (int attempt = 0; attempt < 512; ++attempt)
			{
				const int current = currentCount();
				if (current == target)
				{
					return true;
				}
				(void)ShowCursor(current < target ? TRUE : FALSE);
			}
			return false;
		};
		const int original = currentCount();
		ASSERT_NO_FATAL_FAILURE(createWindow());
		core::ScopeExit restore([&]() noexcept
		{
			(void)m_System.SetPointerMode(m_Window, os::EPointerMode::Free);
			(void)setCount(original);
			(void)ClipCursor(nullptr);
		});
		const core::Result<os::NativeWindowHandle> native = m_System.GetNativeHandle(m_Window);
		ASSERT_TRUE(native);
		SendMessageW(reinterpret_cast<HWND>(native.Value().Window), WM_SETFOCUS, 0, 0);
		ASSERT_TRUE(setCount(2));
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Relative));
		EXPECT_LT(currentCount(), 0);
		ASSERT_TRUE(m_System.SetPointerMode(m_Window, os::EPointerMode::Free));
		EXPECT_EQ(currentCount(), 2);
	}
}
