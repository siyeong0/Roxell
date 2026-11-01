#include <WindowsWindowSystem.h>
#include <Synchronization.hpp>
#include <TextPrimitives.hpp>
#include <Vocabulary.hpp>
#include <SystemError.hpp>
#include <StringView.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Utility.hpp>
#include "WindowsPhysicalKeyTable.h"
#include "WindowsAccessibility.h"
#include "WindowsTextInput.h"
#include "WindowsClipboardMemory.h"

#include <Array.hpp>
#include <Assertion.h>
#include <Log.h>
#include <UniquePtr.hpp>
#include <ScopeExit.hpp>
#include <Utf8.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef GetObject
#include <windowsx.h>
#include <imm.h>

#ifdef CreateWindow
#undef CreateWindow
#endif

namespace rox::os
{
	namespace
	{
		constexpr core::uint32 PHYSICAL_KEY_STATE_COUNT = 0xE8;
		constexpr core::uint32 POINTER_BUTTON_STATE_COUNT = 5;
		constexpr core::uint32 REPLACEMENT_CHARACTER = 0xFFFD;
		constexpr core::uint32 HIGH_SURROGATE_FIRST = 0xD800;
		constexpr core::uint32 HIGH_SURROGATE_LAST = 0xDBFF;
		constexpr core::uint32 LOW_SURROGATE_FIRST = 0xDC00;
		constexpr core::uint32 LOW_SURROGATE_LAST = 0xDFFF;
		constexpr core::uint32 SUPPLEMENTARY_PLANE_FIRST = 0x10000;
		constexpr core::uint32 UNICODE_SCALAR_LAST = 0x10FFFF;

		enum class EWindowInitializeResult
		{
			Success,
			AllocationFailed,
			ClassFailed
		};

		rox::core::Atomic<core::uint64> s_WindowSystemOwner = 1;

		[[nodiscard]] core::Error makeWindowError(const core::EErrorCode code, const core::StringView diagnosticID, const core::StringView message, const core::StringView recoveryHint)
		{
			ASSERT(code != core::EErrorCode::None, "A window failure requires a non-success error code.");
			ASSERT(!diagnosticID.IsEmpty(), "A window failure requires a diagnostic ID.");
			ASSERT(!message.IsEmpty(), "A window failure requires a message.");

			return core::MakeError(code, diagnosticID, message, recoveryHint);
		}

		[[nodiscard]] bool isHighSurrogate(const core::uint32 codeUnit) noexcept
		{
			return codeUnit >= HIGH_SURROGATE_FIRST && codeUnit <= HIGH_SURROGATE_LAST;
		}

		[[nodiscard]] bool isLowSurrogate(const core::uint32 codeUnit) noexcept
		{
			return codeUnit >= LOW_SURROGATE_FIRST && codeUnit <= LOW_SURROGATE_LAST;
		}

		[[nodiscard]] core::uint32 combineSurrogates(const core::uint32 high, const core::uint32 low) noexcept
		{
			ASSERT(isHighSurrogate(high), "Unicode surrogate composition requires a high surrogate.");
			ASSERT(isLowSurrogate(low), "Unicode surrogate composition requires a low surrogate.");
			const core::uint32 codePoint = SUPPLEMENTARY_PLANE_FIRST + ((high - HIGH_SURROGATE_FIRST) << 10) + (low - LOW_SURROGATE_FIRST);
			ASSERT(codePoint <= UNICODE_SCALAR_LAST, "A composed UTF-16 surrogate pair must produce a Unicode scalar value.");

			return codePoint;
		}

		[[nodiscard]] core::uint32 pointerButtonIndex(const EPointerButton button) noexcept
		{
			const core::uint32 index = static_cast<core::uint32>(button);
			ASSERT(index < POINTER_BUTTON_STATE_COUNT, "A pointer-button state index must be within the admitted button set.");

			return index;
		}

		// Use bounded stack storage to report recoverable failures without allocating.
		// The key-value payload shares the log message bound because Error and LogRecord carry no structured payload.
		constexpr rox::core::usize POINTER_MESSAGE_BUFFER_BYTES = 512;
		constexpr rox::core::usize MAXIMUM_POINTER_SENTENCE_BYTES = 80;
		constexpr core::uint32 CURSOR_HIDE_LOOP_BOUND = 64;
		constexpr core::uint16 RAW_INPUT_MOUSE_USAGE_PAGE = 0x01;
		constexpr core::uint16 RAW_INPUT_MOUSE_USAGE = 0x02;

		// Limit pointer diagnostics to the known sentence table.
		enum class EPointerMessage : core::uint8
		{
			RawInputRegistrationRefused,
			ConfinementChangeRefused,
			ConfinementQueryFailed,
			ConfinementNotOurs,
			CursorHideLoopExhausted,
			ArbiterHandleUnavailable,
			NativeEnter,
			NativeLeave,
			Count
		};

		enum class EPointerOperation : core::uint8
		{
			RegisterRawInputDevices,
			ClipCursor,
			GetClipCursor,
			ShowCursorLoop,
			DuplicateHandle,
			// Not an operation. The two Debug shapes report the outcome of a whole reconcile rather
			// than of one Win32 call, so they omit the key.
			None
		};

		enum class EPointerDirection : core::uint8
		{
			TowardApplied,
			TowardAbsent
		};

		constexpr core::StringView POINTER_MESSAGE_TABLE[] = { "Windows refused the raw-input registration change.",
															   "Windows refused to change the cursor confinement.",
															   "Windows could not report the current cursor confinement.",
															   "The cursor confinement was not ours to release and was left alone.",
															   "The bounded cursor-hide loop could not hide the cursor.",
															   "The arbiter thread handle could not be acquired.",
															   "Relative pointer mode entered natively.",
															   "Relative pointer mode left natively." };
		constexpr core::StringView POINTER_OPERATION_TABLE[] = { "RegisterRawInputDevices", "ClipCursor", "GetClipCursor", "ShowCursorLoop", "DuplicateHandle" };
		constexpr core::StringView POINTER_DIRECTION_TABLE[] = { "TowardApplied", "TowardAbsent" };
		constexpr core::StringView POINTER_CONFINEMENT_TABLE[] = { "Absent", "Current", "Stale" };

		[[nodiscard]] constexpr bool everyPointerSentenceFitsItsBound() noexcept
		{
			for (const core::StringView sentence : POINTER_MESSAGE_TABLE)
			{
				if (sentence.Size() > MAXIMUM_POINTER_SENTENCE_BYTES)
				{
					return false;
				}
			}

			return true;
		}

		static_assert(rox::core::Size(POINTER_MESSAGE_TABLE) == static_cast<rox::core::usize>(EPointerMessage::Count), "Every pointer-message enumerator requires its own sentence literal.");
		static_assert(rox::core::Size(POINTER_OPERATION_TABLE) == static_cast<rox::core::usize>(EPointerOperation::None), "Every reportable pointer operation requires its own enumerator name.");
		static_assert(rox::core::Size(POINTER_DIRECTION_TABLE) == static_cast<rox::core::usize>(EPointerDirection::TowardAbsent) + 1, "Every reconciliation direction requires its own enumerator name.");
		static_assert(rox::core::Size(POINTER_CONFINEMENT_TABLE) == static_cast<rox::core::usize>(EConfinement::Stale) + 1, "Every confinement value requires its own enumerator name.");
		static_assert(everyPointerSentenceFitsItsBound(), "Every pointer-message sentence must fit the proven eighty-byte sentence bound.");

		struct PointerDiagnosticPayload final
		{
		public:
			EPointerMessage Message = EPointerMessage::Count;
			EPointerOperation Operation = EPointerOperation::None;
			EPointerDirection Direction = EPointerDirection::TowardApplied;
			WindowId Window;
			WindowId Claimant;
			core::uint32 NativeError = 0;

			EPointerOperation ContextOperation = EPointerOperation::None;
			core::uint32 ContextNativeError = 0;

			RECT AppliedRect = {};
			RECT CurrentRect = {};

			core::uint32 LoopCallsMade = 0;
			core::uint32 LoopCallsUndone = 0;
			core::int32 LoopDisplayCount = 0;

			WindowId RemainingClaimant;
			WindowId RemainingRegisteredTarget;
			EConfinement RemainingConfinement = EConfinement::Absent;
			core::uint32 RemainingCursorHideCalls = 0;

			bool bHasNativeError = false;
			bool bHasAppliedRect = false;
			bool bHasCurrentRect = false;
			bool bHasLoopResult = false;
		};

		class PointerMessageWriter final
		{
		public:
			explicit PointerMessageWriter(char* const buffer) noexcept
				: m_Buffer(buffer)
			{
				ASSERT(m_Buffer != nullptr, "A pointer-message writer requires its bounded buffer.");
			}

			void Append(const core::StringView text) noexcept
			{
				ASSERT(m_Length + text.Size() <= POINTER_MESSAGE_BUFFER_BYTES, "A pointer diagnostic message must fit the proven message bound.");

				for (const char character : text)
				{
					m_Buffer[m_Length] = character;
					++m_Length;
				}
			}

			void AppendUnsigned(const core::uint64 value) noexcept
			{
				char digits[24] = {};
				const rox::core::ToCharsResult converted = rox::core::ToChars(digits, digits + sizeof(digits), value);
				ASSERT(converted.ec == rox::core::EErrorCondition(), "A bounded decimal rendering of an unsigned value must succeed.");
				Append(core::StringView(digits, static_cast<rox::core::usize>(converted.ptr - digits)));
			}

			void AppendSigned(const core::int64 value) noexcept
			{
				char digits[24] = {};
				const rox::core::ToCharsResult converted = rox::core::ToChars(digits, digits + sizeof(digits), value);
				ASSERT(converted.ec == rox::core::EErrorCondition(), "A bounded decimal rendering of a signed value must succeed.");
				Append(core::StringView(digits, static_cast<rox::core::usize>(converted.ptr - digits)));
			}

			// Include the owner; spell absent identifiers as invalid.
			void AppendWindowId(const WindowId window) noexcept
			{
				if (!window.IsValid())
				{
					Append("invalid");

					return;
				}

				AppendUnsigned(window.Owner);
				Append(":");
				AppendUnsigned(window.Slot);
				Append(":");
				AppendUnsigned(window.Generation);
			}

			void AppendRect(const RECT& rect) noexcept
			{
				AppendSigned(rect.left);
				Append(",");
				AppendSigned(rect.top);
				Append(",");
				AppendSigned(rect.right);
				Append(",");
				AppendSigned(rect.bottom);
			}

			[[nodiscard]] rox::core::usize Length() const noexcept
			{
				return m_Length;
			}

		private:
			char* m_Buffer = nullptr;
			rox::core::usize m_Length = 0;
		};

		// The sentence carries its own terminal period and every tail key carries its own leading
		// space, so nothing is written between them. An absent field is omitted with its key rather
		// than written empty, which is what makes context-operation present mean exactly the failed
		// query preceding a failed release, and loop present mean exactly ShowCursorLoop.
		[[nodiscard]] rox::core::usize encodePointerMessage(const PointerDiagnosticPayload& payload,
			char(&buffer)[POINTER_MESSAGE_BUFFER_BYTES]) noexcept
		{
			ASSERT(payload.Message < EPointerMessage::Count, "A pointer diagnostic sentence must name an entry of the closed sentence table.");
			PointerMessageWriter writer(buffer);
			writer.Append(POINTER_MESSAGE_TABLE[static_cast<rox::core::usize>(payload.Message)]);

			if (payload.Operation != EPointerOperation::None)
			{
				writer.Append(" operation=");
				writer.Append(POINTER_OPERATION_TABLE[static_cast<rox::core::usize>(payload.Operation)]);
			}

			writer.Append(" direction=");
			writer.Append(POINTER_DIRECTION_TABLE[static_cast<rox::core::usize>(payload.Direction)]);
			writer.Append(" window=");
			writer.AppendWindowId(payload.Window);
			writer.Append(" claimant=");
			writer.AppendWindowId(payload.Claimant);

			if (payload.bHasNativeError)
			{
				writer.Append(" error=");
				writer.AppendUnsigned(payload.NativeError);
			}

			if (payload.ContextOperation != EPointerOperation::None)
			{
				writer.Append(" context-operation=");
				writer.Append(POINTER_OPERATION_TABLE[static_cast<rox::core::usize>(payload.ContextOperation)]);
				writer.Append(" context-error=");
				writer.AppendUnsigned(payload.ContextNativeError);
			}

			if (payload.bHasAppliedRect)
			{
				writer.Append(" applied-rect=");
				writer.AppendRect(payload.AppliedRect);
			}

			if (payload.bHasCurrentRect)
			{
				writer.Append(" current-rect=");
				writer.AppendRect(payload.CurrentRect);
			}

			if (payload.bHasLoopResult)
			{
				writer.Append(" loop=");
				writer.AppendUnsigned(payload.LoopCallsMade);
				writer.Append("/");
				writer.AppendUnsigned(payload.LoopCallsUndone);
				writer.Append("/");
				writer.AppendSigned(payload.LoopDisplayCount);
			}

			writer.Append(" remaining=");
			writer.AppendWindowId(payload.RemainingClaimant);
			writer.Append("/");
			writer.AppendWindowId(payload.RemainingRegisteredTarget);
			writer.Append("/");
			writer.Append(POINTER_CONFINEMENT_TABLE[static_cast<rox::core::usize>(payload.RemainingConfinement)]);
			writer.Append("/");
			writer.AppendUnsigned(payload.RemainingCursorHideCalls);

			return writer.Length();
		}

		struct CursorHideLoopResult final
		{
		public:
			core::uint32 CallsMade = 0;
			core::uint32 CallsUndone = 0;
			core::int32 DisplayCount = 0;
			bool bHidden = false;
		};

		// A bounded attempt rolls back its own ShowCursor calls on failure.
		// The counter is shared, so an unbounded retry could hang under interference.
		[[nodiscard]] CursorHideLoopResult hideCursorAllOrNothing() noexcept
		{
			CursorHideLoopResult result;

			while (result.CallsMade < CURSOR_HIDE_LOOP_BOUND)
			{
				result.DisplayCount = ShowCursor(FALSE);
				++result.CallsMade;

				if (result.DisplayCount < 0)
				{
					result.bHidden = true;

					return result;
				}
			}
			while (result.CallsUndone < result.CallsMade)
			{
				result.DisplayCount = ShowCursor(TRUE);
				++result.CallsUndone;
			}

			return result;
		}

		[[nodiscard]] bool isSameRect(const RECT& left, const RECT& right) noexcept
		{
			return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
		}
	} // namespace

	class WindowsWindowSystem::State final
	{
	public:
		struct WindowRecord final
		{
		public:
			State* Owner = nullptr;
			HWND NativeWindow = nullptr;

			core::UniquePtr<WindowsAccessibility> Accessibility;
			core::InterfaceOwner<WindowsTextInput> TextInput;
			bool bTextServiceKeyHandled = false;

			WindowId ID;
			WindowState StateSnapshot;

			core::uint32 PendingHighSurrogate = 0;
			core::uint64 TextSession = 0;
			bool bTextInputEnabled = false;
			bool bManagedTextInput = false;
			bool bCompositionActive = false;

			core::int32 LastPointerX = 0;
			core::int32 LastPointerY = 0;

			// Absolute pointer differencing tracks one device and coordinate space.
			HANDLE LastAbsoluteDevice = nullptr;
			core::int32 LastAbsoluteX = 0;
			core::int32 LastAbsoluteY = 0;

			core::uint32 Generation = 1;

			bool Keys[PHYSICAL_KEY_STATE_COUNT] = {};
			bool PointerButtons[POINTER_BUTTON_STATE_COUNT] = {};

			bool bReserved = false;
			bool bAlive = false;
			bool bGenerationExhausted = false;

			bool bHasPointerPosition = false;
			bool bTrackingPointerLeave = false;

			bool bHasAbsoluteSample = false;
			bool bLastAbsoluteVirtualDesktop = false;

			// One flag and two callers. Both WM_DPICHANGED and native destruction synchronously
			// deliver messages back into this window procedure, and in both cases a reconcile running
			// inside the call would act on state the system is still changing. While it is set no
			// trigger reconciles and no trigger writes the arbiter.
			bool bSuppressPointerReconcile = false;
		};

		// The process-global relative-pointer arbiter. Raw-mouse registration is process scoped,
		// cursor confinement is desktop scoped, and the cursor display count is input-queue scoped,
		// and not one of the three is a property of a window. Multiple window-system instances are
		// permitted, so an instance-scoped guard would guard nothing.
		struct PointerArbiter final
		{
		public:
			// Held for every record read and every record write, and never held across a Win32 call.
			rox::core::Mutex Mutex;

			// The reservation held by a live window; invalid otherwise.
			WindowId Claimant;

			// The instance responsible for this record; null only when the record is clear.
			State* Owner = nullptr;

			// The actual raw-input target, which a failed removal can leave naming a window the
			// claimant is not. A boolean here would force one of two wrong readings.
			WindowId RegisteredTarget;
			EConfinement Confinement = EConfinement::Absent;

			// Meaningless while Confinement is Absent.
			RECT AppliedClipRect = {};

			// An outstanding ShowCursor(FALSE) contribution on the arbiter queue. It does NOT mean
			// the cursor is hidden: another writer on the same queue can raise the counter and Win32
			// offers no ownership query for it.
			core::uint32 CursorHideCalls = 0;

			// The last two fields are the only ones that outlive a clear record. A thread id names
			// its thread only until that thread terminates, so a stored id alone would admit a later
			// thread that inherited the number. The handle is the identity and the liveness; the id
			// is a cached comparison that is sound only while the handle reports running.
			HANDLE ArbiterThread = nullptr;
			core::uint32 ArbiterThreadId = 0;
		};

		State(core::MemoryResource& resource, const WindowsWindowSystemOptions options) noexcept
			: m_Resource(resource)
			, m_Options(options)
			, m_Records(resource)
			, m_PendingEvents(resource)
			, m_PublishedEvents(resource)
		{
			m_Owner = s_WindowSystemOwner.fetch_add(1, rox::core::MemoryOrderRelaxed);
			ASSERT(m_Owner != 0 && m_Owner != rox::core::NumericLimits<core::uint64>::max(), "Window-system owner identity space must not be exhausted.");
			m_OwnerThread = GetCurrentThreadId();
			ASSERT(m_OwnerThread != 0, "A Windows window system requires a valid owner-thread ID.");
		}

		~State() noexcept
		{
			shutdown();
		}

		[[nodiscard]] EWindowInitializeResult Initialize() noexcept
		{
			assertOwnerThread();

			if (!m_Records.Resize(m_Options.MaximumWindows) || !m_PendingEvents.Reserve(m_Options.MaximumEventsPerPoll) || !m_PublishedEvents.Reserve(m_Options.MaximumEventsPerPoll))
			{
				return EWindowInitializeResult::AllocationFailed;
			}

			const int classNameLength = swprintf_s(m_ClassName, L"RoxellWindow_%016llX", static_cast<unsigned long long>(m_Owner));
			ASSERT(classNameLength > 0 && static_cast<rox::core::usize>(classNameLength) < sizeof(m_ClassName) / sizeof(m_ClassName[0]), "A Windows window class name must fit its bounded storage.");

			m_Instance = GetModuleHandleW(nullptr);

			if (m_Instance == nullptr)
			{
				return EWindowInitializeResult::ClassFailed;
			}

			WNDCLASSEXW windowClass = {};
			windowClass.cbSize = sizeof(windowClass);
			windowClass.style = CS_HREDRAW | CS_VREDRAW;
			windowClass.lpfnWndProc = &State::windowProcedure;
			windowClass.hInstance = m_Instance;
			windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
			windowClass.lpszClassName = m_ClassName;
			m_Class = RegisterClassExW(&windowClass);

			return m_Class != 0 ? EWindowInitializeResult::Success : EWindowInitializeResult::ClassFailed;
		}

		[[nodiscard]] core::Result<WindowId> CreateWindow(const WindowDesc& desc)
		{
			assertOwnerThread();
			core::Result<void> validated = validateDesc(desc);

			if (!validated)
			{
				return core::Result<WindowId>::Failure(validated.GetError());
			}

			WindowRecord* const record = reserveRecord();

			if (record == nullptr)
			{
				return core::Result<WindowId>::Failure(makeWindowError(
					core::EErrorCode::Conflict, "ROX-PLATFORM-WINDOW-CAPACITY", "Every configured Windows window slot is occupied.",
					"Destroy an existing window or increase the reviewed window-system bound."));
			}

			wchar_t title[MAXIMUM_WINDOW_TITLE_UTF8_BYTE_COUNT + 1] = {};
			const int titleLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, desc.Title.Data(), static_cast<int>(desc.Title.Size()), title, static_cast<int>(MAXIMUM_WINDOW_TITLE_UTF8_BYTE_COUNT));
			ASSERT(titleLength > 0 && static_cast<rox::core::usize>(titleLength) <= MAXIMUM_WINDOW_TITLE_UTF8_BYTE_COUNT, "A validated bounded UTF-8 window title must convert to bounded UTF-16.");
			title[titleLength] = L'\0';

			DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

			if (desc.bResizable)
			{
				style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
			}

			const DWORD extendedStyle = WS_EX_APPWINDOW;
			RECT outerRect = { 0, 0, static_cast<LONG>(desc.ClientExtent.Width), static_cast<LONG>(desc.ClientExtent.Height) };

			if (AdjustWindowRectExForDpi(&outerRect, style, FALSE, extendedStyle, USER_DEFAULT_SCREEN_DPI) == FALSE)
			{
				releaseReservedRecord(*record);

				return core::Result<WindowId>::Failure(makeWindowError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-CREATE",
					"Windows could not calculate the requested client-area window size.",
					"Retry with a supported window size on the owner thread."));
			}

			const DPI_AWARENESS_CONTEXT previousDpiContext = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

			if (previousDpiContext == nullptr)
			{
				releaseReservedRecord(*record);

				return core::Result<WindowId>::Failure(
					makeWindowError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-CREATE",
						"Windows could not enter per-monitor-v2 DPI awareness for window creation.",
						"Run on a supported Windows desktop and retry creation."));
			}

			const HWND nativeWindow = CreateWindowExW(extendedStyle, m_ClassName, title, style, CW_USEDEFAULT, CW_USEDEFAULT, outerRect.right - outerRect.left, outerRect.bottom - outerRect.top, nullptr, nullptr, m_Instance, record);
			const DPI_AWARENESS_CONTEXT restoredDpiContext = SetThreadDpiAwarenessContext(previousDpiContext);
			ASSERT(restoredDpiContext != nullptr, "A temporary Windows thread DPI-awareness context must restore successfully.");

			if (nativeWindow == nullptr)
			{
				releaseReservedRecord(*record);
				LOG_ERROR("Platform.Window", "Windows failed to create a top-level window.");

				return core::Result<WindowId>::Failure(
					makeWindowError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-CREATE",
						"Windows failed to create the requested top-level window.",
						"Check desktop availability and retry with a supported descriptor."));
			}

			ASSERT(record->NativeWindow == nativeWindow, "WM_NCCREATE must attach the exact native window to its stable record.");
			record->bAlive = true;
			updateState(*record);
			++m_LiveWindowCount;

			if (m_LiveWindowCount == 1)
			{
				LOG_INFO("Platform.Window", "The first Windows top-level window was created.");
			}

			if (desc.bVisible)
			{
				core::Result<void> visible = SetWindowVisibility(record->ID, true);

				if (!visible)
				{
					core::Result<void> destroyed = DestroyWindow(record->ID);
					ASSERT(destroyed.HasValue(), "A newly created window must roll back after visibility publication fails.");

					return core::Result<WindowId>::Failure(visible.GetError());
				}
			}

			return core::Result<WindowId>::Success(record->ID);
		}

		[[nodiscard]] core::Result<void> DestroyWindow(const WindowId window)
		{
			assertOwnerThread();
			core::Result<WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<void>::Failure(resolved.GetError());
			}

			WindowRecord& record = *resolved.Value();
			ASSERT(record.NativeWindow != nullptr, "A live window record must retain its native window until explicit destruction.");
			const WindowId destroyed = record.ID;
			core::Result<void> reconciled = core::Result<void>::Success();

			if (isPointerClaimant(destroyed))
			{
				record.StateSnapshot.PointerMode = EPointerMode::Free;
				reconciled = reconcilePointer(record);
				clearPointerRecordIfAbsent(destroyed);
			}

			// DestroyWindow can deliver WM_KILLFOCUS reentrantly. Suppress nested reconciliation
			// during that call and restore the flag on both success and failure.
			ASSERT(!record.bSuppressPointerReconcile, "Native destruction must not begin while pointer reconciliation is already suppressed.");
			record.bSuppressPointerReconcile = true;
			const BOOL bDestroyed = ::DestroyWindow(record.NativeWindow);
			record.bSuppressPointerReconcile = false;

			if (bDestroyed == FALSE)
			{
				LOG_ERROR("Platform.Window", "Windows failed to destroy an owned top-level window.");

				return core::Result<void>::Failure(makeWindowError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-DESTROY",
					"Windows failed to destroy the owned top-level window.",
					"Release native consumers and retry destruction on the owner thread."));
			}

			if (!reconciled)
			{
				releasePointerClaimToSystem(destroyed);
			}

			ASSERT(record.NativeWindow == nullptr, "WM_NCDESTROY must clear the native handle before DestroyWindow returns.");
			record.bAlive = false;
			record.bReserved = false;
			record.Owner = nullptr;
			record.ID = {};
			record.PendingHighSurrogate = 0;
			record.bHasPointerPosition = false;
			ASSERT(m_LiveWindowCount > 0, "Destroying a live window requires a positive live-window count.");
			--m_LiveWindowCount;
			advanceGeneration(record);

			if (m_LiveWindowCount == 0)
			{
				LOG_INFO("Platform.Window", "The last Windows top-level window was destroyed.");
			}

			// A native destruction failure would have won the return above; when destruction
			// succeeded and the reconcile did not, the reconcile failure is what the caller needs.
			return reconciled;
		}

		[[nodiscard]] core::Result<void> SetWindowVisibility(const WindowId window, const bool bVisible)
		{
			assertOwnerThread();
			core::Result<WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<void>::Failure(resolved.GetError());
			}

			WindowRecord& record = *resolved.Value();
			ShowWindow(record.NativeWindow, bVisible ? SW_SHOW : SW_HIDE);

			if (bVisible)
			{
				UpdateWindow(record.NativeWindow);
			}

			record.StateSnapshot.bVisible = IsWindowVisible(record.NativeWindow) != FALSE;

			if (record.StateSnapshot.bVisible != bVisible)
			{
				return core::Result<void>::Failure(makeWindowError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-VISIBILITY",
					"Windows did not apply the requested top-level window visibility.",
					"Retry visibility after restoring the interactive desktop."));
			}

			return core::Result<void>::Success();
		}

		[[nodiscard]] core::Result<void> ResizeWindow(const WindowId window, const WindowExtent clientExtent)
		{
			assertOwnerThread();

			if (!isValidExtent(clientExtent))
			{
				return core::Result<void>::Failure(makeWindowError(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-WINDOW-RESIZE",
					"The requested window client extent is outside the supported bound.",
					"Use nonzero dimensions no greater than 16,384 pixels."));
			}

			core::Result<WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<void>::Failure(resolved.GetError());
			}

			WindowRecord& record = *resolved.Value();
			const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(record.NativeWindow, GWL_STYLE));
			const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(record.NativeWindow, GWL_EXSTYLE));
			RECT outerRect = { 0, 0, static_cast<LONG>(clientExtent.Width), static_cast<LONG>(clientExtent.Height) };
			const UINT dpi = GetDpiForWindow(record.NativeWindow);

			if (AdjustWindowRectExForDpi(&outerRect, style, FALSE, extendedStyle, dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi) == FALSE ||
				SetWindowPos(record.NativeWindow, nullptr, 0, 0, outerRect.right - outerRect.left, outerRect.bottom - outerRect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) == FALSE)
			{
				LOG_ERROR("Platform.Window", "Windows failed to resize an owned top-level window.");

				return core::Result<void>::Failure(makeWindowError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-RESIZE",
					"Windows failed to resize the owned top-level window.",
					"Restore the window and retry a supported client extent."));
			}

			updateState(record);

			return core::Result<void>::Success();
		}

		[[nodiscard]] core::Result<void> SetPointerMode(const WindowId window, const EPointerMode mode)
		{
			assertOwnerThread();
			core::Result<WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<void>::Failure(resolved.GetError());
			}

			WindowRecord& record = *resolved.Value();

			if (mode == EPointerMode::Relative)
			{
				core::Result<void> admitted = admitRelativeClaim(record);

				if (!admitted)
				{
					return admitted;
				}

				record.StateSnapshot.PointerMode = EPointerMode::Relative;

				return reconcilePointer(record);
			}

			record.StateSnapshot.PointerMode = EPointerMode::Free;

			if (!isPointerClaimant(record.ID))
			{
				return core::Result<void>::Success();
			}

			// Idempotence is stated over the arbiter record and never over the requested value, so
			// this still reconciles a residue the window owns.
			core::Result<void> reconciled = reconcilePointer(record);
			clearPointerRecordIfAbsent(record.ID);

			return reconciled;
		}

		[[nodiscard]] core::Result<core::String> ReadClipboardText(const WindowId window, core::MemoryResource& memory, core::uint32 maximumBytes)
		{
			assertOwnerThread();
			core::Result<WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<core::String>::Failure(resolved.GetError());
			}

			if (maximumBytes == 0 || maximumBytes > 1024 * 1024)
			{
				return core::Result<core::String>::Failure(core::EErrorCode::InvalidArgument, "platform.window.clipboard", "Clipboard reads are bounded to one MiB.");
			}

			if (!OpenClipboard(resolved.Value()->NativeWindow))
			{
				return core::Result<core::String>::Failure(clipboardError("OpenClipboard", GetLastError()));
			}

			core::ScopeExit close([]() noexcept
			{
				CloseClipboard();
			});

			if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
			{
				return core::Result<core::String>::Failure(core::EErrorCode::NotFound, "platform.window.clipboard", "Clipboard has no Unicode text.");
			}

			const HANDLE handle = GetClipboardData(CF_UNICODETEXT);

			if (!handle)
			{
				return core::Result<core::String>::Failure(clipboardError("GetClipboardData", GetLastError()));
			}

			const SIZE_T bytes = GlobalSize(handle);

			if (bytes < sizeof(wchar_t))
			{
				return core::Result<core::String>::Failure(core::EErrorCode::InvalidArgument, "platform.window.clipboard", "Clipboard Unicode extent is invalid.");
			}

			const wchar_t* source = static_cast<const wchar_t*>(GlobalLock(handle));

			if (!source)
			{
				return core::Result<core::String>::Failure(clipboardError("GlobalLock", GetLastError()));
			}

			core::ScopeExit unlock([&]() noexcept
			{
				GlobalUnlock(handle);
			});
			const core::uint64 bound = rox::core::Min<SIZE_T>(bytes / sizeof(wchar_t), static_cast<SIZE_T>(maximumBytes) + 1);
			SIZE_T count = 0;

			while (count < bound && source[count] != 0)
			{
				++count;
			}

			if (count == bound)
			{
				return core::Result<core::String>::Failure(core::EErrorCode::InvalidArgument, "platform.window.clipboard", "Clipboard text exceeds its read bound or lacks a terminator.");
			}

			core::String result(memory);

			if (count == 0)
			{
				return core::Result<core::String>::Success(rox::core::Move(result));
			}

			const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, static_cast<int>(count), nullptr, 0, nullptr, nullptr);

			if (required <= 0)
			{
				return core::Result<core::String>::Failure(clipboardError("Clipboard UTF-16 conversion", GetLastError()));
			}

			if (static_cast<core::uint32>(required) > maximumBytes)
			{
				return core::Result<core::String>::Failure(core::EErrorCode::InvalidArgument, "platform.window.clipboard", "Clipboard text exceeds its UTF-8 byte bound.");
			}

			core::Array<char> encoded(memory);

			if (!encoded.ResizeUninitialized(required))
			{
				return core::Result<core::String>::Failure(core::EErrorCode::ExternalFailure, "platform.window.clipboard", "Clipboard text allocation failed.");
			}

			if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, static_cast<int>(count), encoded.Data(), required, nullptr, nullptr) != required)
			{
				return core::Result<core::String>::Failure(clipboardError("Clipboard UTF-16 conversion", GetLastError()));
			}

			if (!result.Assign({ encoded.Data(), encoded.Size() }))
			{
				return core::Result<core::String>::Failure(core::EErrorCode::ExternalFailure, "platform.window.clipboard", "Clipboard text copy failed.");
			}

			return core::Result<core::String>::Success(rox::core::Move(result));
		}

		[[nodiscard]] core::Result<void> WriteClipboardText(const WindowId window, core::StringView text)
		{
			assertOwnerThread();
			core::Result<WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<void>::Failure(resolved.GetError());
			}

			if (text.Size() > 1024 * 1024 || !core::IsValidUtf8(text) || text.Contains('\0'))
			{
				return core::Result<void>::Failure(core::EErrorCode::InvalidArgument, "platform.window.clipboard", "Clipboard writes require bounded UTF-8 without embedded nulls.");
			}

			const int count = text.IsEmpty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Size()), nullptr, 0);

			if (count <= 0 && !text.IsEmpty())
			{
				return core::Result<void>::Failure(clipboardError("Clipboard UTF-8 conversion", GetLastError()));
			}

			core::Result<WindowsClipboardMemory> allocation = WindowsClipboardMemory::Allocate(static_cast<core::uint32>((static_cast<SIZE_T>(count) + 1) * sizeof(wchar_t)));

			if (!allocation)
			{
				return core::Result<void>::Failure(allocation.GetError());
			}

			core::Result<void*> locked = allocation.Value().Lock();

			if (!locked)
			{
				return core::Result<void>::Failure(locked.GetError());
			}

			wchar_t* destination = static_cast<wchar_t*>(locked.Value());
			const bool bEncoded = count == 0 || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Size()), destination, count) == count;
			const DWORD conversionError = bEncoded ? ERROR_SUCCESS : GetLastError();
			destination[count] = 0;
			allocation.Value().Unlock();

			if (!bEncoded)
			{
				return core::Result<void>::Failure(clipboardError("Clipboard UTF-8 conversion", conversionError));
			}

			if (!OpenClipboard(resolved.Value()->NativeWindow))
			{
				return core::Result<void>::Failure(clipboardError("OpenClipboard", GetLastError()));
			}

			core::ScopeExit close([]() noexcept
			{
				CloseClipboard();
			});

			if (!EmptyClipboard())
			{
				return core::Result<void>::Failure(clipboardError("EmptyClipboard", GetLastError()));
			}

			if (!SetClipboardData(CF_UNICODETEXT, allocation.Value().Get()))
			{
				return core::Result<void>::Failure(clipboardError("SetClipboardData", GetLastError()));
			}

			allocation.Value().ReleaseToWindows();

			return core::Result<void>::Success();
		}

		core::Result<void> SetAccessibilitySnapshot(WindowId window, const AccessibilitySnapshot& snapshot)
		{
			assertOwnerThread();
			core::Result<WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<void>::Failure(resolved.GetError());
			}

			WindowRecord& record = *resolved.Value();

			if (!snapshot.GetPublication())
			{
				if (record.Accessibility)
				{
					record.Accessibility->Close();
					record.Accessibility.Reset();
				}

				return core::Result<void>::Success();
			}

			if (!record.Accessibility)
			{
				core::Result<core::UniquePtr<WindowsAccessibility>> owner = WindowsAccessibility::Create(record.NativeWindow);

				if (!owner)
				{
					return core::Result<void>::Failure(owner.GetError());
				}

				record.Accessibility = rox::core::Move(owner).TakeValue();
			}

			return record.Accessibility->Publish(snapshot);
		}

		core::Result<AccessibilityRequest> PollAccessibilityAction(WindowId window)
		{
			assertOwnerThread();
			core::Result<WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<AccessibilityRequest>::Failure(resolved.GetError());
			}

			return core::Result<AccessibilityRequest>::Success(resolved.Value()->Accessibility ? resolved.Value()->Accessibility->Poll() : AccessibilityRequest{});
		}

		core::Result<void> PublishTextDocument(WindowId window, const WindowTextDocument& document)
		{
			assertOwnerThread();
			core::Result<WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<void>::Failure(resolved.GetError());
			}

			WindowRecord& record = *resolved.Value();

			if (document.Session == 0)
			{
				if (record.TextInput)
				{
					record.TextInput->Close();
					record.TextInput.Reset();
				}

				return core::Result<void>::Success();
			}

			if (!record.TextInput)
			{
				core::InterfaceOwner<WindowsTextInput> owner = core::InterfaceOwner<WindowsTextInput>::Create(
					m_Resource, m_Resource, record.NativeWindow, record.ID,
					+[](void* context, const WindowEvent& event) noexcept
					{
						WindowRecord& target = *static_cast<WindowRecord*>(context);

						if (event.Composition.ByteCount > WindowCompositionEvent::MAX_BYTES)
						{
							target.Owner->m_bPendingOverflowed = true;
						}
						else
						{
							target.Owner->appendEvent(target, event);
						}
					},
					&record);

				if (!owner)
				{
					return core::Result<void>::Failure(core::EErrorCode::ExternalFailure, "platform.window.tsf", "TSF owner allocation failed.");
				}

				core::Result<void> initialized = owner->Initialize();

				if (!initialized)
				{
					return initialized;
				}

				record.TextInput = rox::core::Move(owner);
			}

			return record.TextInput->Publish(document);
		}

		[[nodiscard]] core::Result<void> SetTextInputState(const WindowId window, const WindowTextInputState& state)
		{
			assertOwnerThread();
			core::Result<WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<void>::Failure(resolved.GetError());
			}

			WindowRecord& record = *resolved.Value();

			if (state.bEnabled && (state.Session == 0 || state.CaretHeight <= 0 || state.CaretHeight > MAXIMUM_WINDOW_EXTENT_DIMENSION ||
				state.CaretX < -static_cast<core::int32>(MAXIMUM_WINDOW_EXTENT_DIMENSION) ||
				state.CaretX > static_cast<core::int32>(MAXIMUM_WINDOW_EXTENT_DIMENSION) ||
				state.CaretY < -static_cast<core::int32>(MAXIMUM_WINDOW_EXTENT_DIMENSION) ||
				state.CaretY > static_cast<core::int32>(MAXIMUM_WINDOW_EXTENT_DIMENSION)))
			{
				return core::Result<void>::Failure(core::EErrorCode::InvalidArgument, "platform.window.text", "Native text input requires a session and bounded physical caret geometry.");
			}

			if (record.TextInput)
			{
				if (record.bCompositionActive)
				{
					compositionEvent(record, EWindowEventKind::CompositionCancel);
					record.bCompositionActive = false;
				}

				if (!ImmAssociateContextEx(record.NativeWindow, nullptr, 0))
				{
					return core::Result<void>::Failure(core::EErrorCode::ExternalFailure, "platform.window.tsf", "Windows could not disable duplicate IMM32 routing.");
				}

				record.bManagedTextInput = true;
				record.bTextInputEnabled = state.bEnabled;
				record.TextSession = state.Session;

				return record.TextInput->SetState(state);
			}

			if (!record.bManagedTextInput && !state.bEnabled)
			{
				return core::Result<void>::Success();
			}

			const bool bChange = !record.bManagedTextInput || record.bTextInputEnabled != state.bEnabled || record.TextSession != state.Session;

			if ((bChange || state.bCancelComposition) && record.bCompositionActive)
			{
				compositionEvent(record, EWindowEventKind::CompositionCancel);
				record.bCompositionActive = false;
				const HIMC context = ImmGetContext(record.NativeWindow);

				if (context)
				{
					ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
					ImmReleaseContext(record.NativeWindow, context);
				}
			}

			if (bChange)
			{
				if (!ImmAssociateContextEx(record.NativeWindow, nullptr, state.bEnabled ? IACE_DEFAULT : 0))
				{
					return core::Result<void>::Failure(core::EErrorCode::ExternalFailure, "platform.window.text", "Windows could not associate the requested IMM32 context.");
				}

				record.bManagedTextInput = true;
				record.bTextInputEnabled = state.bEnabled;
				record.TextSession = state.Session;
			}

			if (!state.bEnabled)
			{
				return core::Result<void>::Success();
			}

			const HIMC context = ImmGetContext(record.NativeWindow);

			if (!context)
			{
				return core::Result<void>::Failure(core::EErrorCode::Unsupported, "platform.window.text", "No IMM32 text input context is available.");
			}

			COMPOSITIONFORM composition{ CFS_POINT, {state.CaretX, state.CaretY + state.CaretHeight}, {} };
			CANDIDATEFORM candidate{ 0,
									CFS_EXCLUDE,
									{state.CaretX, state.CaretY + state.CaretHeight},
									{state.CaretX, state.CaretY, state.CaretX + 1, state.CaretY + state.CaretHeight} };
			const bool bPositioned = ImmSetCompositionWindow(context, &composition) && ImmSetCandidateWindow(context, &candidate);
			ImmReleaseContext(record.NativeWindow, context);

			if (!bPositioned)
			{
				return core::Result<void>::Failure(core::EErrorCode::ExternalFailure, "platform.window.text", "Windows could not position the IMM32 composition/candidate windows.");
			}

			return core::Result<void>::Success();
		}

		[[nodiscard]] core::Result<WindowState> GetWindowState(const WindowId window) const
		{
			assertOwnerThread();
			core::Result<const WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<WindowState>::Failure(resolved.GetError());
			}

			return core::Result<WindowState>::Success(resolved.Value()->StateSnapshot);
		}

		[[nodiscard]] core::Result<NativeWindowHandle> GetNativeHandle(const WindowId window) const
		{
			assertOwnerThread();
			core::Result<const WindowRecord*> resolved = resolveRecord(window);

			if (!resolved)
			{
				return core::Result<NativeWindowHandle>::Failure(resolved.GetError());
			}

			const WindowRecord& record = *resolved.Value();
			ASSERT(record.NativeWindow != nullptr, "A live window record must retain a native handle for borrowing.");

			return core::Result<NativeWindowHandle>::Success(NativeWindowHandle::FromWin32(reinterpret_cast<core::uintptr>(record.NativeWindow)));
		}

		[[nodiscard]] WindowEventBatch PollEvents()
		{
			assertOwnerThread();
			m_PublishedEvents.Clear();
			const bool bTextPublicationBoundary = rox::core::AnyOf(m_Records.begin(), m_Records.end(), [](const WindowRecord& record) { return static_cast<bool>(record.TextInput); });

			// Grant deferred text locks only while the last consumer publication is
			// current, before admitting a pointer/navigation message which can move it.
			if (m_PendingEvents.IsEmpty())
			{
				for (WindowRecord& record : m_Records)
				{
					if (record.TextInput)
					{
						record.TextInput->Pump();
					}
				}
			}

			MSG message = {};

			// A queued pointer/navigation edit must reach the UI and its retained text
			// publication before a later native message can acquire a TSF edit lock.
			while ((!bTextPublicationBoundary || m_PendingEvents.IsEmpty()) && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
			{
				if (message.message == WM_QUIT)
				{
					m_bPendingQuitRequested = true;
					continue;
				}

				WindowRecord* textRecord = nullptr;

				for (WindowRecord& candidate : m_Records)
				{
					if (candidate.NativeWindow == message.hwnd && candidate.TextInput)
					{
						textRecord = &candidate;
						break;
					}
				}

				const bool bTextHandled = textRecord && textRecord->TextInput->FilterKey(message);

				if (textRecord)
				{
					textRecord->bTextServiceKeyHandled = bTextHandled;
				}

				if (!bTextHandled)
				{
					TranslateMessage(&message);
				}

				DispatchMessageW(&message);

				if (textRecord)
				{
					textRecord->bTextServiceKeyHandled = false;
				}
			}

			rox::core::Swap(m_PendingEvents, m_PublishedEvents);
			const bool bOverflowed = m_bPendingOverflowed;
			const bool bQuitRequested = m_bPendingQuitRequested;
			m_bPendingOverflowed = false;
			m_bPendingQuitRequested = false;
			ASSERT(m_PendingEvents.IsEmpty(), "The recycled pending-event buffer must be empty after publication.");

			return { m_PublishedEvents.View(), bOverflowed, bQuitRequested };
		}

	private:
		// Check thread liveness before its cached ID: Windows may reuse IDs after termination.
		// Reject invalid threads before ownership checks because they cannot legally acquire the arbiter.
		[[nodiscard]] core::Result<void> admitRelativeClaim(const WindowRecord& record)
		{
			rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);

			if (s_PointerArbiter.ArbiterThread != nullptr)
			{
				const DWORD waitResult = WaitForSingleObject(s_PointerArbiter.ArbiterThread, 0);
				ASSERT(waitResult == WAIT_OBJECT_0 || waitResult == WAIT_TIMEOUT, "A privately duplicated arbiter thread handle must answer a zero-timeout wait.");

				if (waitResult == WAIT_OBJECT_0)
				{
					return core::Result<void>::Failure(
						makeWindowError(core::EErrorCode::Unsupported, "ROX-PLATFORM-WINDOW-POINTER-THREAD-GONE",
							"The input thread that owned relative pointer mode has terminated.",
							"Relative pointer mode is over for this process; start a new process to use it."));
				}

				if (GetCurrentThreadId() != s_PointerArbiter.ArbiterThreadId)
				{
					return core::Result<void>::Failure(
						makeWindowError(core::EErrorCode::Unsupported, "ROX-PLATFORM-WINDOW-POINTER-THREAD",
							"Relative pointer mode is pinned to another input thread of this process.",
							"Request relative pointer mode from the thread that first entered it."));
				}
			}

			const bool bHeldByAnotherClaimant = s_PointerArbiter.Claimant.IsValid() && s_PointerArbiter.Claimant != record.ID;
			const bool bHeldByAnotherSystem = s_PointerArbiter.Owner != nullptr && s_PointerArbiter.Owner != this;

			if (bHeldByAnotherClaimant || bHeldByAnotherSystem)
			{
				return core::Result<void>::Failure(
					makeWindowError(core::EErrorCode::Conflict, "ROX-PLATFORM-WINDOW-POINTER-OWNER",
						"Another window or window system holds the relative pointer arbiter.",
						"Free or destroy the holding window, or wait for its window system to shut down."));
			}

			// Acquisition is itself a Win32 call and it runs FIRST, before any record field is
			// written and before any pointer effect is attempted. On failure nothing is mutated and
			// the arbiter is left unpinned, so the next admitted claim may still pin it.
			if (s_PointerArbiter.ArbiterThread == nullptr)
			{
				HANDLE duplicated = nullptr;

				if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &duplicated,
					THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, 0) == FALSE)
				{
					PointerDiagnosticPayload payload;
					payload.Message = EPointerMessage::ArbiterHandleUnavailable;
					payload.Operation = EPointerOperation::DuplicateHandle;
					payload.Direction = EPointerDirection::TowardApplied;
					payload.NativeError = static_cast<core::uint32>(GetLastError());
					payload.bHasNativeError = true;

					return reportPointerFailureLocked(payload, record.ID, core::EErrorCode::ExternalFailure,
						"ROX-PLATFORM-WINDOW-POINTER-ARBITER",
						"Request relative pointer mode again; nothing was pinned and nothing was applied.");
				}

				s_PointerArbiter.ArbiterThread = duplicated;
				s_PointerArbiter.ArbiterThreadId = GetCurrentThreadId();
			}

			ASSERT(s_PointerArbiter.ArbiterThread != nullptr, "The arbiter thread handle must already exist when an arbiter owner is written.");
			ASSERT(m_OwnerThread == s_PointerArbiter.ArbiterThreadId, "An arbiter owner must have the arbiter thread as its own creator thread.");

			// A claim change reclassifies a current rectangle as stale, because it belongs to a
			// window that is gone.
			if (s_PointerArbiter.Claimant != record.ID && s_PointerArbiter.Confinement == EConfinement::Current)
			{
				s_PointerArbiter.Confinement = EConfinement::Stale;
			}

			s_PointerArbiter.Claimant = record.ID;
			s_PointerArbiter.Owner = this;

			return core::Result<void>::Success();
		}

		// The one reconciliation algorithm. Every trigger performs it; no trigger performs a special
		// enter, a special leave, or a rollback.
		[[nodiscard]] core::Result<void> reconcilePointer(WindowRecord& record)
		{
			ASSERT(!record.bSuppressPointerReconcile, "Pointer reconciliation must not run while a native call of this window suppresses it.");
			assertArbiterThread();
			const bool bDesiredApplied = record.StateSnapshot.PointerMode == EPointerMode::Relative && record.StateSnapshot.bFocused && !record.StateSnapshot.bMinimized;
			const bool bWasActive = record.StateSnapshot.bPointerRelativeActive;
			core::Result<void> outcome = bDesiredApplied ? reconcileTowardApplied(record) : reconcileTowardAbsent(record.ID);
			record.StateSnapshot.bPointerRelativeActive = isPointerRelativeActive(record.ID);
			{
				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				assertPointerRecordConsistentLocked();
			}

			if (bWasActive != record.StateSnapshot.bPointerRelativeActive)
			{
				// The absolute state is cleared on every native enter and every native leave, so a
				// delta never spans a focus change.
				record.LastAbsoluteDevice = nullptr;
				record.LastAbsoluteX = 0;
				record.LastAbsoluteY = 0;
				record.bHasAbsoluteSample = false;
				record.bLastAbsoluteVirtualDesktop = false;
				logPointerTransition(record.ID, record.StateSnapshot.bPointerRelativeActive);
			}

			return outcome;
		}

		// Toward applied the order is registration, confinement, cursor. That keeps raw reports
		// discarded until every entry effect has succeeded. The sequence stops at the first failing
		// effect, the record keeps exactly what succeeded, and nothing already applied is undone.
		[[nodiscard]] core::Result<void> reconcileTowardApplied(WindowRecord& record)
		{
			WindowId registeredTarget;
			{
				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				registeredTarget = s_PointerArbiter.RegisteredTarget;
			}

			if (registeredTarget != record.ID)
			{
				RAWINPUTDEVICE device = {};
				device.usUsagePage = RAW_INPUT_MOUSE_USAGE_PAGE;
				device.usUsage = RAW_INPUT_MOUSE_USAGE;
				device.dwFlags = 0;
				device.hwndTarget = record.NativeWindow;

				if (RegisterRawInputDevices(&device, 1, sizeof(device)) == FALSE)
				{
					PointerDiagnosticPayload payload;
					payload.Message = EPointerMessage::RawInputRegistrationRefused;
					payload.Operation = EPointerOperation::RegisterRawInputDevices;
					payload.Direction = EPointerDirection::TowardApplied;
					payload.NativeError = static_cast<core::uint32>(GetLastError());
					payload.bHasNativeError = true;

					return reportPointerFailure(payload, record.ID, core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-RAW-INPUT", "Request relative pointer mode again on the arbiter thread.");
				}

				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				s_PointerArbiter.RegisteredTarget = record.ID;
				ASSERT(s_PointerArbiter.RegisteredTarget == record.ID, "The arbiter must record the raw-input target the last call actually applied.");
			}

			EConfinement confinement = EConfinement::Absent;
			{
				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				confinement = s_PointerArbiter.Confinement;
			}

			if (confinement != EConfinement::Current)
			{
				const RECT clipRect = clientScreenRect(record);

				if (ClipCursor(&clipRect) == FALSE)
				{
					PointerDiagnosticPayload payload;
					payload.Message = EPointerMessage::ConfinementChangeRefused;
					payload.Operation = EPointerOperation::ClipCursor;
					payload.Direction = EPointerDirection::TowardApplied;
					payload.NativeError = static_cast<core::uint32>(GetLastError());
					payload.bHasNativeError = true;
					payload.AppliedRect = clipRect;
					payload.bHasAppliedRect = true;

					return reportPointerFailure(payload, record.ID, core::EErrorCode::ExternalFailure,
						"ROX-PLATFORM-WINDOW-POINTER-CONFINE",
						"Request relative pointer mode again once the desktop admits a cursor clip.");
				}

				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				s_PointerArbiter.Confinement = EConfinement::Current;
				s_PointerArbiter.AppliedClipRect = clipRect;
				ASSERT(s_PointerArbiter.Confinement == EConfinement::Current, "The arbiter must record the confinement the last call actually applied.");
			}

			core::uint32 cursorHideCalls = 0;
			{
				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				cursorHideCalls = s_PointerArbiter.CursorHideCalls;
			}

			if (cursorHideCalls == 0)
			{
				const CursorHideLoopResult loop = hideCursorAllOrNothing();

				if (!loop.bHidden)
				{
					ASSERT(loop.CallsUndone == loop.CallsMade, "An exhausted cursor-hide loop must undo exactly the calls it made.");
					PointerDiagnosticPayload payload;
					payload.Message = EPointerMessage::CursorHideLoopExhausted;
					payload.Operation = EPointerOperation::ShowCursorLoop;
					payload.Direction = EPointerDirection::TowardApplied;
					payload.LoopCallsMade = loop.CallsMade;
					payload.LoopCallsUndone = loop.CallsUndone;
					payload.LoopDisplayCount = loop.DisplayCount;
					payload.bHasLoopResult = true;

					return reportPointerFailure(payload, record.ID, core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-POINTER-CURSOR", "Lower the cursor display count of this input queue and request the mode again.");
				}

				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				s_PointerArbiter.CursorHideCalls = loop.CallsMade;
				ASSERT(s_PointerArbiter.CursorHideCalls > 0, "A hidden cursor must record the hide contribution this packet owes.");
			}

			return core::Result<void>::Success();
		}

		// Toward absent the order is the reverse, which makes the derived active answer false the
		// moment a leave begins. It needs no window record: RIDEV_REMOVE takes a null target and the
		// clip release names no window, which is what lets the owning shutdown route run it.
		[[nodiscard]] core::Result<void> reconcileTowardAbsent(const WindowId window)
		{
			core::uint32 cursorHideCalls = 0;
			{
				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				cursorHideCalls = s_PointerArbiter.CursorHideCalls;
			}

			if (cursorHideCalls > 0)
			{
				// No thread test, because one input queue owes the whole debt, and no failure
				// channel, because ShowCursor(TRUE) has none.
				for (core::uint32 call = 0; call < cursorHideCalls; ++call)
				{
					(void)ShowCursor(TRUE);
				}

				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				s_PointerArbiter.CursorHideCalls = 0;
			}

			EConfinement confinement = EConfinement::Absent;
			RECT appliedRect = {};
			{
				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				confinement = s_PointerArbiter.Confinement;
				appliedRect = s_PointerArbiter.AppliedClipRect;
				ASSERT(confinement == EConfinement::Absent || s_PointerArbiter.Claimant == window || s_PointerArbiter.Owner == this, "A confinement release requires the arbiter to name this window or this system.");
			}

			if (confinement != EConfinement::Absent)
			{
				core::Result<void> released = releaseConfinement(window, appliedRect);

				if (!released)
				{
					return released;
				}
			}

			WindowId registeredTarget;
			{
				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				registeredTarget = s_PointerArbiter.RegisteredTarget;
			}

			if (registeredTarget.IsValid())
			{
				ASSERT(registeredTarget.Owner == m_Owner, "A raw-input removal requires the recorded target to belong to this system.");
				RAWINPUTDEVICE device = {};
				device.usUsagePage = RAW_INPUT_MOUSE_USAGE_PAGE;
				device.usUsage = RAW_INPUT_MOUSE_USAGE;
				device.dwFlags = RIDEV_REMOVE;
				device.hwndTarget = nullptr;

				if (RegisterRawInputDevices(&device, 1, sizeof(device)) == FALSE)
				{
					PointerDiagnosticPayload payload;
					payload.Message = EPointerMessage::RawInputRegistrationRefused;
					payload.Operation = EPointerOperation::RegisterRawInputDevices;
					payload.Direction = EPointerDirection::TowardAbsent;
					payload.NativeError = static_cast<core::uint32>(GetLastError());
					payload.bHasNativeError = true;

					return reportPointerFailure(payload, window, core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-RAW-INPUT", "Free the pointer mode of the claiming window again on the arbiter thread.");
				}

				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				s_PointerArbiter.RegisteredTarget = {};
				ASSERT(!s_PointerArbiter.RegisteredTarget.IsValid(),
					"The arbiter must record the raw-input removal the last call actually applied.");
			}

			return core::Result<void>::Success();
		}

		// Clipping has no native owner query. On query failure, attempt release to avoid trapping the cursor.
		[[nodiscard]] core::Result<void> releaseConfinement(const WindowId window, const RECT& appliedRect)
		{
			RECT currentRect = {};

			if (GetClipCursor(&currentRect) != FALSE)
			{
				if (isSameRect(currentRect, appliedRect))
				{
					if (ClipCursor(nullptr) == FALSE)
					{
						PointerDiagnosticPayload payload;
						payload.Message = EPointerMessage::ConfinementChangeRefused;
						payload.Operation = EPointerOperation::ClipCursor;
						payload.Direction = EPointerDirection::TowardAbsent;
						payload.NativeError = static_cast<core::uint32>(GetLastError());
						payload.bHasNativeError = true;
						payload.AppliedRect = appliedRect;
						payload.bHasAppliedRect = true;
						payload.CurrentRect = currentRect;
						payload.bHasCurrentRect = true;

						return reportPointerFailure(payload, window, core::EErrorCode::ExternalFailure,
							"ROX-PLATFORM-WINDOW-POINTER-CONFINE",
							"Free the pointer mode of the claiming window again on the arbiter thread.");
					}

					recordConfinementAbsent();

					return core::Result<void>::Success();
				}

				// The rectangle we applied is demonstrably not current, so there is nothing of ours
				// to release. It is one Warning and a recorded Absent, not a confinement failure.
				recordConfinementAbsent();
				PointerDiagnosticPayload payload;
				payload.Message = EPointerMessage::ConfinementNotOurs;
				payload.Operation = EPointerOperation::GetClipCursor;
				payload.Direction = EPointerDirection::TowardAbsent;
				payload.AppliedRect = appliedRect;
				payload.bHasAppliedRect = true;
				payload.CurrentRect = currentRect;
				payload.bHasCurrentRect = true;
				reportPointerWarning(payload, window);

				return core::Result<void>::Success();
			}

			const core::uint32 queryError = static_cast<core::uint32>(GetLastError());

			if (ClipCursor(nullptr) != FALSE)
			{
				recordConfinementAbsent();
				PointerDiagnosticPayload payload;
				payload.Message = EPointerMessage::ConfinementQueryFailed;
				payload.Operation = EPointerOperation::GetClipCursor;
				payload.Direction = EPointerDirection::TowardAbsent;
				payload.NativeError = queryError;
				payload.bHasNativeError = true;
				payload.AppliedRect = appliedRect;
				payload.bHasAppliedRect = true;
				reportPointerWarning(payload, window);

				return core::Result<void>::Success();
			}

			// The release error wins the primary slot, because it is the one that left native state
			// applied; the advisory query error rides beside it in the context pair.
			PointerDiagnosticPayload payload;
			payload.Message = EPointerMessage::ConfinementChangeRefused;
			payload.Operation = EPointerOperation::ClipCursor;
			payload.Direction = EPointerDirection::TowardAbsent;
			payload.NativeError = static_cast<core::uint32>(GetLastError());
			payload.bHasNativeError = true;
			payload.ContextOperation = EPointerOperation::GetClipCursor;
			payload.ContextNativeError = queryError;
			payload.AppliedRect = appliedRect;
			payload.bHasAppliedRect = true;

			return reportPointerFailure(payload, window, core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-POINTER-CONFINE", "Free the pointer mode of the claiming window again on the arbiter thread.");
		}

		void recordConfinementAbsent() noexcept
		{
			rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
			s_PointerArbiter.Confinement = EConfinement::Absent;
		}

		[[nodiscard]] static RECT clientScreenRect(const WindowRecord& record) noexcept
		{
			ASSERT(record.NativeWindow != nullptr, "A cursor confinement rectangle requires a live native window.");
			RECT clientRect = {};
			const BOOL bClientRect = GetClientRect(record.NativeWindow, &clientRect);
			ASSERT(bClientRect != FALSE, "A live Windows window must provide its client rectangle.");
			POINT topLeft = { clientRect.left, clientRect.top };
			POINT bottomRight = { clientRect.right, clientRect.bottom };
			const BOOL bTopLeft = ClientToScreen(record.NativeWindow, &topLeft);
			const BOOL bBottomRight = ClientToScreen(record.NativeWindow, &bottomRight);
			ASSERT(bTopLeft != FALSE && bBottomRight != FALSE, "A live Windows window must map its client rectangle into screen coordinates.");

			return RECT{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
		}

		[[nodiscard]] static bool isPointerClaimant(const WindowId window) noexcept
		{
			rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);

			return s_PointerArbiter.Claimant == window;
		}

		[[nodiscard]] static bool isPointerRelativeActive(const WindowId window) noexcept
		{
			rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);

			return s_PointerArbiter.Claimant == window && s_PointerArbiter.RegisteredTarget == window && s_PointerArbiter.Confinement == EConfinement::Current && s_PointerArbiter.CursorHideCalls > 0;
		}

		// Rows 9 and 10 of the trigger table: a move never marks an absent state stale, and a
		// non-claimant window never marks anything.
		[[nodiscard]] static bool markPointerConfinementStale(const WindowId window) noexcept
		{
			rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);

			if (s_PointerArbiter.Claimant != window || s_PointerArbiter.Confinement != EConfinement::Current)
			{
				return false;
			}

			s_PointerArbiter.Confinement = EConfinement::Stale;

			return true;
		}

		void clearPointerRecordIfAbsent(const WindowId window) noexcept
		{
			rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);

			if (s_PointerArbiter.Claimant == window && isPointerRecordEmptyLocked())
			{
				s_PointerArbiter.Claimant = {};
				s_PointerArbiter.Owner = nullptr;
				s_PointerArbiter.AppliedClipRect = {};
			}

			assertPointerRecordConsistentLocked();
		}

		// The claim passes to the system rather than being orphaned: detachment would leave residue
		// with no live object able to reconcile it.
		void releasePointerClaimToSystem(const WindowId window) noexcept
		{
			rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);

			if (s_PointerArbiter.Claimant == window)
			{
				s_PointerArbiter.Claimant = {};
			}

			assertPointerRecordConsistentLocked();
		}

		[[nodiscard]] static bool isPointerRecordEmptyLocked() noexcept
		{
			return !s_PointerArbiter.RegisteredTarget.IsValid() && s_PointerArbiter.Confinement == EConfinement::Absent &&
				s_PointerArbiter.CursorHideCalls == 0;
		}

		// An invariant about the record and not about the world: a cleared record can coexist with
		// abandoned native residue, which is why that state is labelled abandonment.
		static void assertPointerRecordConsistentLocked() noexcept
		{
			const bool bEmpty = !s_PointerArbiter.Claimant.IsValid() && isPointerRecordEmptyLocked();
			ASSERT((s_PointerArbiter.Owner == nullptr) == bEmpty, "The arbiter owner must be null exactly when every recorded field is at its empty value.");
		}

		void assertArbiterThread() const noexcept
		{
			HANDLE arbiterThread = nullptr;
			core::uint32 arbiterThreadId = 0;
			{
				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
				arbiterThread = s_PointerArbiter.ArbiterThread;
				arbiterThreadId = s_PointerArbiter.ArbiterThreadId;
			}

			ASSERT(arbiterThread != nullptr, "Pointer reconciliation requires a pinned arbiter thread.");
			ASSERT(GetCurrentThreadId() == arbiterThreadId, "Pointer reconciliation must run on the pinned arbiter thread.");
			const DWORD waitResult = WaitForSingleObject(arbiterThread, 0);
			ASSERT(waitResult == WAIT_TIMEOUT, "The pinned arbiter thread must still be running inside its own reconcile.");
		}

		[[nodiscard]] rox::core::usize fillAndEncodePointerPayloadLocked(PointerDiagnosticPayload& payload, const WindowId window, char(&buffer)[POINTER_MESSAGE_BUFFER_BYTES]) const noexcept
		{
			payload.Window = window;
			payload.Claimant = s_PointerArbiter.Claimant;
			payload.RemainingClaimant = s_PointerArbiter.Claimant;
			payload.RemainingRegisteredTarget = s_PointerArbiter.RegisteredTarget;
			payload.RemainingConfinement = s_PointerArbiter.Confinement;
			payload.RemainingCursorHideCalls = s_PointerArbiter.CursorHideCalls;

			return encodePointerMessage(payload, buffer);
		}

		[[nodiscard]] rox::core::usize fillAndEncodePointerPayload(PointerDiagnosticPayload& payload, const WindowId window, char(&buffer)[POINTER_MESSAGE_BUFFER_BYTES]) const noexcept
		{
			rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);

			return fillAndEncodePointerPayloadLocked(payload, window, buffer);
		}

		[[nodiscard]] core::Result<void> reportPointerFailure(PointerDiagnosticPayload& payload, const WindowId window, const core::EErrorCode code, const core::StringView diagnosticID, const core::StringView recoveryHint) const
		{
			char buffer[POINTER_MESSAGE_BUFFER_BYTES] = {};
			const rox::core::usize length = fillAndEncodePointerPayload(payload, window, buffer);
			LOG_ERROR("Platform.Window", rox::core::StringView(buffer, length));

			return core::Result<void>::Failure(makeWindowError(code, diagnosticID, core::StringView(buffer, length), recoveryHint));
		}

		[[nodiscard]] core::Result<void> reportPointerFailureLocked(PointerDiagnosticPayload& payload, const WindowId window, const core::EErrorCode code, const core::StringView diagnosticID, const core::StringView recoveryHint) const
		{
			char buffer[POINTER_MESSAGE_BUFFER_BYTES] = {};
			const rox::core::usize length = fillAndEncodePointerPayloadLocked(payload, window, buffer);
			LOG_ERROR("Platform.Window", rox::core::StringView(buffer, length));

			return core::Result<void>::Failure(makeWindowError(code, diagnosticID, core::StringView(buffer, length), recoveryHint));
		}

		void reportPointerWarning(PointerDiagnosticPayload& payload, const WindowId window) const noexcept
		{
			char buffer[POINTER_MESSAGE_BUFFER_BYTES] = {};
			const rox::core::usize length = fillAndEncodePointerPayload(payload, window, buffer);
			LOG_WARNING("Platform.Window", rox::core::StringView(buffer, length));
		}

		void logPointerTransition(const WindowId window, const bool bEntered) const noexcept
		{
			PointerDiagnosticPayload payload;
			payload.Message = bEntered ? EPointerMessage::NativeEnter : EPointerMessage::NativeLeave;
			payload.Direction = bEntered ? EPointerDirection::TowardApplied : EPointerDirection::TowardAbsent;
			char buffer[POINTER_MESSAGE_BUFFER_BYTES] = {};
			const rox::core::usize length = fillAndEncodePointerPayload(payload, window, buffer);
			LOG_DEBUG("Platform.Window", rox::core::StringView(buffer, length));
		}

		// A message trigger has no Result channel. Every stopping state is already one Error record
		// on Platform.Window, and the state audit names the trigger that converges each of them.
		void reconcilePointerFromMessage(WindowRecord& record) noexcept
		{
			core::Result<void> reconciled = reconcilePointer(record);
			(void)reconciled;
		}

		[[nodiscard]] static LRESULT CALLBACK windowProcedure(const HWND window, const UINT message, const WPARAM word,
			const LPARAM data) noexcept
		{
			WindowRecord* record = reinterpret_cast<WindowRecord*>(GetWindowLongPtrW(window, GWLP_USERDATA));

			if (message == WM_NCCREATE)
			{
				const CREATESTRUCTW* const create = reinterpret_cast<const CREATESTRUCTW*>(data);
				record = static_cast<WindowRecord*>(create->lpCreateParams);
				ASSERT(record != nullptr && record->Owner != nullptr && record->bReserved,
					"WM_NCCREATE requires a stable reserved window record.");
				record->NativeWindow = window;
				SetLastError(ERROR_SUCCESS);
				const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(record));
				ASSERT(previous != 0 || GetLastError() == ERROR_SUCCESS, "WM_NCCREATE must attach its stable window record.");

				return DefWindowProcW(window, message, word, data);
			}

			if (record == nullptr)
			{
				return DefWindowProcW(window, message, word, data);
			}

			ASSERT(record->Owner != nullptr && record->NativeWindow == window, "A Windows message must resolve to its exact stable record.");
			return record->Owner->handleMessage(*record, message, word, data);
		}

		[[nodiscard]] LRESULT handleMessage(WindowRecord& record, const UINT message, const WPARAM word, const LPARAM data) noexcept
		{
			switch (message)
			{
			case WM_KEYDOWN:
			case WM_KEYUP:
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
			{
				const EPhysicalKey key = PhysicalKeyFromScanCode(word, data);

				if (key != EPhysicalKey::Unknown)
				{
					const bool bPressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
					const bool bRepeated = bPressed && (data & 0x40000000) != 0;
					const core::uint32 keyIndex = static_cast<core::uint32>(key);
					ASSERT(keyIndex < PHYSICAL_KEY_STATE_COUNT, "A mapped physical key must fit the tracked key-state table.");
					record.Keys[keyIndex] = bPressed;
					WindowEvent event;
					event.Window = record.ID;
					event.Kind = EWindowEventKind::KeyChanged;
					event.Key = { key, bPressed, bRepeated };
					event.Key.bTextServiceHandled = record.bTextServiceKeyHandled;
					appendEvent(record, event);
				}

				if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) && !record.bTextServiceKeyHandled)
				{
					return DefWindowProcW(record.NativeWindow, message, word, data);
				}

				return 0;
			}

			case WM_IME_SETCONTEXT:
				return DefWindowProcW(record.NativeWindow, message, word,
					record.bTextInputEnabled ? (data & ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW)) : data);
			case WM_IME_STARTCOMPOSITION:
				if (record.TextInput)
				{
					return 0;
				}

				if (!record.bTextInputEnabled)
				{
					return DefWindowProcW(record.NativeWindow, message, word, data);
				}

				record.bCompositionActive = true;
				compositionEvent(record, EWindowEventKind::CompositionStart);

				return 0;
			case WM_IME_COMPOSITION:
			{
				if (record.TextInput)
				{
					return 0;
				}

				if (!record.bTextInputEnabled)
				{
					return DefWindowProcW(record.NativeWindow, message, word, data);
				}

				const HIMC context = ImmGetContext(record.NativeWindow);

				if (!context)
				{
					m_bPendingOverflowed = true;

					return 0;
				}

				if ((data & GCS_RESULTSTR) != 0 && record.bCompositionActive)
				{
					readComposition(record, context, GCS_RESULTSTR, EWindowEventKind::CompositionCommit);
					record.bCompositionActive = false;
				}

				if ((data & (GCS_COMPSTR | GCS_CURSORPOS)) != 0)
				{
					if (!record.bCompositionActive)
					{
						record.bCompositionActive = true;
						compositionEvent(record, EWindowEventKind::CompositionStart);
					}

					readComposition(record, context, GCS_COMPSTR, EWindowEventKind::CompositionUpdate);
				}

				if (data == 0 && record.bCompositionActive)
				{
					compositionEvent(record, EWindowEventKind::CompositionCancel);
					record.bCompositionActive = false;
				}

				ImmReleaseContext(record.NativeWindow, context);

				return 0;
			}

			case WM_IME_ENDCOMPOSITION:
				if (record.TextInput)
				{
					return 0;
				}

				if (!record.bTextInputEnabled)
				{
					return DefWindowProcW(record.NativeWindow, message, word, data);
				}

				if (record.bCompositionActive)
				{
					compositionEvent(record, EWindowEventKind::CompositionCancel);
					record.bCompositionActive = false;
				}

				return 0;
			case WM_IME_CHAR:
				if (record.bManagedTextInput)
				{
					return 0;
				}

				return DefWindowProcW(record.NativeWindow, message, word, data);
			case WM_CHAR:
				handleUtf16CodeUnit(record, static_cast<core::uint32>(word));

				return 0;

			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
			case WM_XBUTTONDOWN:
			case WM_XBUTTONUP:
				handlePointerButton(record, message, word, data);

				return message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ? TRUE : 0;

			case WM_MOUSEMOVE:
				if (!record.bTrackingPointerLeave)
				{
					TRACKMOUSEEVENT tracking{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, record.NativeWindow, 0 };
					record.bTrackingPointerLeave = TrackMouseEvent(&tracking) != FALSE;
				}

				handlePointerMove(record, data);

				return 0;

			case WM_MOUSELEAVE:
			{
				record.bTrackingPointerLeave = false;
				record.bHasPointerPosition = false;
				WindowEvent event;
				event.Window = record.ID;
				event.Kind = EWindowEventKind::PointerLeft;
				appendEvent(record, event);

				return 0;
			}

			case WM_INPUT:
				handleRawInput(record, data);
				// DefWindowProc must process every foreground WM_INPUT message, including discarded input, to release native storage.
				return DefWindowProcW(record.NativeWindow, message, word, data);

			case WM_MOUSEWHEEL:
			case WM_MOUSEHWHEEL:
			{
				WindowEvent event;
				event.Window = record.ID;
				event.Kind = EWindowEventKind::PointerWheel;
				POINT position{ GET_X_LPARAM(data), GET_Y_LPARAM(data) };

				if (ScreenToClient(record.NativeWindow, &position))
				{
					event.PointerWheel.X = position.x;
					event.PointerWheel.Y = position.y;
					event.PointerWheel.bHasPosition = true;
				}

				const core::float32 delta =
					static_cast<core::float32>(GET_WHEEL_DELTA_WPARAM(word)) / static_cast<core::float32>(WHEEL_DELTA);

				if (message == WM_MOUSEWHEEL)
				{
					event.PointerWheel.Vertical = delta;
				}
				else
				{
					event.PointerWheel.Horizontal = delta;
				}

				appendEvent(record, event);

				return 0;
			}

			case WM_SETFOCUS:
				if (record.TextInput)
				{
					record.TextInput->Focus(true);
				}

				record.StateSnapshot.bFocused = true;
				record.bHasPointerPosition = false;

				if (!record.bSuppressPointerReconcile && !record.StateSnapshot.bMinimized && isPointerClaimant(record.ID))
				{
					reconcilePointerFromMessage(record);
				}

				appendFocusEvent(record, true);

				return 0;

			case WM_KILLFOCUS:
				if (record.TextInput)
				{
					record.TextInput->Focus(false);
				}

				// Focus-loss cleanup still runs when native destruction suppresses pointer reconciliation.
				record.StateSnapshot.bFocused = false;

				if (record.bCompositionActive)
				{
					compositionEvent(record, EWindowEventKind::CompositionCancel);
					record.bCompositionActive = false;
					const HIMC context = ImmGetContext(record.NativeWindow);

					if (context)
					{
						ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
						ImmReleaseContext(record.NativeWindow, context);
					}
				}

				if (!record.bSuppressPointerReconcile && isPointerClaimant(record.ID))
				{
					reconcilePointerFromMessage(record);
				}

				releasePressedInput(record, true);

				if (GetCapture() == record.NativeWindow)
				{
					const BOOL bReleased = ReleaseCapture();
					ASSERT(bReleased != FALSE, "Focus loss must release an owned pointer capture.");
				}

				record.bHasPointerPosition = false;
				flushPendingSurrogate(record);
				appendFocusEvent(record, false);

				return 0;

			case WM_CAPTURECHANGED:
				if (reinterpret_cast<HWND>(data) != record.NativeWindow)
				{
					releasePressedInput(record, false);
				}

				return 0;

			case WM_DPICHANGED:
			{
				record.StateSnapshot.DpiX = LOWORD(word);
				record.StateSnapshot.DpiY = HIWORD(word);
				WindowEvent event;
				event.Window = record.ID;
				event.Kind = EWindowEventKind::DpiChanged;
				event.Dpi = { record.StateSnapshot.DpiX, record.StateSnapshot.DpiY };
				appendEvent(record, event);
				const RECT* const suggested = reinterpret_cast<const RECT*>(data);
				ASSERT(suggested != nullptr, "WM_DPICHANGED requires a suggested native rectangle.");
				// SetWindowPos synchronously delivers WM_WINDOWPOSCHANGED, WM_SIZE, and WM_MOVE, so a
				// move-triggered reconcile would run inside this arm against a rectangle the system
				// is still moving. The arm suppresses, repositions, clears, and only then reconciles.
				record.bSuppressPointerReconcile = true;
				const BOOL bPositioned = SetWindowPos(record.NativeWindow, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
				record.bSuppressPointerReconcile = false;
				ASSERT(bPositioned != FALSE, "A valid WM_DPICHANGED suggested rectangle must apply to its live window.");

				if (markPointerConfinementStale(record.ID))
				{
					reconcilePointerFromMessage(record);
				}

				return 0;
			}

			case WM_MOVE:
				if (!record.bSuppressPointerReconcile && markPointerConfinementStale(record.ID))
				{
					reconcilePointerFromMessage(record);
				}

				return DefWindowProcW(record.NativeWindow, message, word, data);

			case WM_SIZE:
			{
				const bool bWasMinimized = record.StateSnapshot.bMinimized;
				const bool bMinimized = word == SIZE_MINIMIZED;
				record.StateSnapshot.bMinimized = bMinimized;
				record.StateSnapshot.ClientExtent = bMinimized ? WindowExtent{} : WindowExtent{ LOWORD(data), HIWORD(data) };
				WindowEvent event;
				event.Window = record.ID;
				event.Kind = EWindowEventKind::Resized;
				event.Resize = { record.StateSnapshot.ClientExtent, record.StateSnapshot.bMinimized };
				appendEvent(record, event);

				if (record.bSuppressPointerReconcile || !isPointerClaimant(record.ID))
				{
					return 0;
				}

				if (bMinimized)
				{
					reconcilePointerFromMessage(record);
				}
				else if (bWasMinimized)
				{
					if (record.StateSnapshot.bFocused)
					{
						reconcilePointerFromMessage(record);
					}
				}
				else if (markPointerConfinementStale(record.ID))
				{
					reconcilePointerFromMessage(record);
				}

				return 0;
			}

			case WM_CLOSE:
			{
				WindowEvent event;
				event.Window = record.ID;
				event.Kind = EWindowEventKind::CloseRequested;
				appendEvent(record, event);
				LOG_INFO("Platform.Window", "A Windows top-level window requested close.");

				return 0;
			}

			case WM_PAINT:
			{
				PAINTSTRUCT paint = {};
				BeginPaint(record.NativeWindow, &paint);
				EndPaint(record.NativeWindow, &paint);

				return 0;
			}

			case WM_GETOBJECT:
				if (static_cast<LONG>(data) == -25 && record.Accessibility)
				{
					return static_cast<LRESULT>(record.Accessibility->ReturnProvider(word, data));
				}

				return DefWindowProcW(record.NativeWindow, message, word, data);

			case WM_NCDESTROY:
			{
				if (record.TextInput)
				{
					record.TextInput->Close();
					record.TextInput.Reset();
				}

				if (record.Accessibility)
				{
					record.Accessibility->Close();
					record.Accessibility.Reset();
				}

				const HWND nativeWindow = record.NativeWindow;
				SetWindowLongPtrW(nativeWindow, GWLP_USERDATA, 0);
				const LRESULT result = DefWindowProcW(nativeWindow, message, word, data);
				record.NativeWindow = nullptr;

				return result;
			}

			default:
				return DefWindowProcW(record.NativeWindow, message, word, data);
			}
		}

		void handleUtf16CodeUnit(WindowRecord& record, const core::uint32 codeUnit) noexcept
		{
			ASSERT(codeUnit <= 0xFFFF, "WM_CHAR must provide one UTF-16 code unit.");

			if (isHighSurrogate(codeUnit))
			{
				flushPendingSurrogate(record);
				record.PendingHighSurrogate = codeUnit;

				return;
			}

			if (isLowSurrogate(codeUnit))
			{
				if (record.PendingHighSurrogate == 0)
				{
					appendTextEvent(record, REPLACEMENT_CHARACTER);

					return;
				}

				appendTextEvent(record, combineSurrogates(record.PendingHighSurrogate, codeUnit));
				record.PendingHighSurrogate = 0;

				return;
			}

			flushPendingSurrogate(record);
			appendTextEvent(record, codeUnit);
		}

		void flushPendingSurrogate(WindowRecord& record) noexcept
		{
			if (record.PendingHighSurrogate != 0)
			{
				appendTextEvent(record, REPLACEMENT_CHARACTER);
				record.PendingHighSurrogate = 0;
			}
		}

		void appendTextEvent(WindowRecord& record, const core::uint32 codePoint) noexcept
		{
			ASSERT(codePoint <= UNICODE_SCALAR_LAST && !isHighSurrogate(codePoint) && !isLowSurrogate(codePoint),
				"A text-input event requires a Unicode scalar value.");
			WindowEvent event;
			event.Window = record.ID;
			event.Kind = EWindowEventKind::TextInput;
			event.Text.CodePoint = codePoint;
			appendEvent(record, event);
		}

		void handlePointerButton(WindowRecord& record, const UINT message, const WPARAM word, const LPARAM data) noexcept
		{
			EPointerButton button = EPointerButton::Left;
			bool bPressed = false;

			switch (message)
			{
			case WM_LBUTTONDOWN:
				button = EPointerButton::Left;
				bPressed = true;
				break;
			case WM_LBUTTONUP:
				button = EPointerButton::Left;
				break;
			case WM_RBUTTONDOWN:
				button = EPointerButton::Right;
				bPressed = true;
				break;
			case WM_RBUTTONUP:
				button = EPointerButton::Right;
				break;
			case WM_MBUTTONDOWN:
				button = EPointerButton::Middle;
				bPressed = true;
				break;
			case WM_MBUTTONUP:
				button = EPointerButton::Middle;
				break;
			case WM_XBUTTONDOWN:
				button = GET_XBUTTON_WPARAM(word) == XBUTTON1 ? EPointerButton::X1 : EPointerButton::X2;
				bPressed = true;
				break;
			case WM_XBUTTONUP:
				button = GET_XBUTTON_WPARAM(word) == XBUTTON1 ? EPointerButton::X1 : EPointerButton::X2;
				break;
			default:
				ASSERT(false, "Pointer-button translation requires an admitted Windows message.");
			}

			record.PointerButtons[pointerButtonIndex(button)] = bPressed;

			if (bPressed)
			{
				SetCapture(record.NativeWindow);
			}
			else if (!hasPressedPointerButton(record) && GetCapture() == record.NativeWindow)
			{
				const BOOL bReleased = ReleaseCapture();
				ASSERT(bReleased != FALSE, "An owned pointer capture must release successfully.");
			}

			WindowEvent event;
			event.Window = record.ID;
			event.Kind = EWindowEventKind::PointerButtonChanged;
			event.PointerButton = { button, GET_X_LPARAM(data), GET_Y_LPARAM(data), bPressed };
			appendEvent(record, event);
		}

		void handlePointerMove(WindowRecord& record, const LPARAM data) noexcept
		{
			const core::int32 x = GET_X_LPARAM(data);
			const core::int32 y = GET_Y_LPARAM(data);
			const core::int32 deltaX = record.bHasPointerPosition ? x - record.LastPointerX : 0;
			const core::int32 deltaY = record.bHasPointerPosition ? y - record.LastPointerY : 0;
			record.LastPointerX = x;
			record.LastPointerY = y;
			record.bHasPointerPosition = true;

			WindowEvent event;
			event.Window = record.ID;
			event.Kind = EWindowEventKind::PointerMoved;
			event.PointerMove = { x, y, deltaX, deltaY };
			appendEvent(record, event);
		}

		// No coalescing: two raw reports in one pump produce two events. Accumulation belongs to the
		// consumer, because a backend that summed them would be making a tick-attribution decision
		// inside a module that knows nothing about ticks.
		void handleRawInput(WindowRecord& record, const LPARAM data) noexcept
		{
			RAWINPUT rawInput = {};
			UINT rawInputSize = sizeof(rawInput);
			const UINT copied =
				GetRawInputData(reinterpret_cast<HRAWINPUT>(data), RID_INPUT, &rawInput, &rawInputSize, sizeof(RAWINPUTHEADER));

			if (copied == static_cast<UINT>(-1) || rawInput.header.dwType != RIM_TYPEMOUSE)
			{
				return;
			}

			const RAWMOUSE& mouse = rawInput.data.mouse;
			core::int32 deltaX = 0;
			core::int32 deltaY = 0;
			EPointerRelativeSource source = EPointerRelativeSource::DeviceCounts;

			if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
			{
				source = EPointerRelativeSource::NormalizedCoordinates;
				const bool bVirtualDesktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
				const core::int32 x = static_cast<core::int32>(mouse.lLastX);
				const core::int32 y = static_cast<core::int32>(mouse.lLastY);
				// A difference across two devices or two coordinate spaces is not a motion, so such
				// a report replaces the state and emits nothing.
				const bool bContinuesOneDevice = record.bHasAbsoluteSample && record.LastAbsoluteDevice == rawInput.header.hDevice &&
					record.bLastAbsoluteVirtualDesktop == bVirtualDesktop;
				deltaX = bContinuesOneDevice ? x - record.LastAbsoluteX : 0;
				deltaY = bContinuesOneDevice ? y - record.LastAbsoluteY : 0;
				record.LastAbsoluteDevice = rawInput.header.hDevice;
				record.LastAbsoluteX = x;
				record.LastAbsoluteY = y;
				record.bLastAbsoluteVirtualDesktop = bVirtualDesktop;
				record.bHasAbsoluteSample = true;

				if (!bContinuesOneDevice)
				{
					return;
				}
			}
			else
			{
				deltaX = static_cast<core::int32>(mouse.lLastX);
				deltaY = static_cast<core::int32>(mouse.lLastY);
			}

			if (deltaX == 0 && deltaY == 0)
			{
				return;
			}

			// Emission is gated on the derived active answer and not on the registration bit: a
			// failed removal leaves the process registered while the arbiter no longer names this
			// window, which is precisely the traffic to discard.
			if (!record.StateSnapshot.bPointerRelativeActive)
			{
				return;
			}

			WindowEvent event;
			event.Window = record.ID;
			event.Kind = EWindowEventKind::PointerRelativeMoved;
			event.PointerRelative = { deltaX, deltaY, source };
			appendEvent(record, event);
		}

		void appendFocusEvent(WindowRecord& record, const bool bFocused) noexcept
		{
			WindowEvent event;
			event.Window = record.ID;
			event.Kind = EWindowEventKind::FocusChanged;
			event.Focus.bFocused = bFocused;
			appendEvent(record, event);
		}

		void releasePressedInput(WindowRecord& record, const bool bReleaseKeys) noexcept
		{
			if (bReleaseKeys)
			{
				for (core::uint32 keyIndex = 0; keyIndex < PHYSICAL_KEY_STATE_COUNT; ++keyIndex)
				{
					if (!record.Keys[keyIndex])
					{
						continue;
					}

					record.Keys[keyIndex] = false;
					WindowEvent event;
					event.Window = record.ID;
					event.Kind = EWindowEventKind::KeyChanged;
					event.Key = { static_cast<EPhysicalKey>(keyIndex), false, false, true };
					appendEvent(record, event);
				}
			}

			for (core::uint32 buttonIndex = 0; buttonIndex < POINTER_BUTTON_STATE_COUNT; ++buttonIndex)
			{
				if (!record.PointerButtons[buttonIndex])
				{
					continue;
				}

				record.PointerButtons[buttonIndex] = false;
				WindowEvent event;
				event.Window = record.ID;
				event.Kind = EWindowEventKind::PointerButtonChanged;
				event.PointerButton = { static_cast<EPointerButton>(buttonIndex), record.LastPointerX, record.LastPointerY, false, true };
				appendEvent(record, event);
			}
		}

		[[nodiscard]] static bool hasPressedPointerButton(const WindowRecord& record) noexcept
		{
			for (const bool bPressed : record.PointerButtons)
			{
				if (bPressed)
				{
					return true;
				}
			}

			return false;
		}

		core::Error clipboardError(const char* operation, DWORD code) const noexcept
		{
			char buffer[192]{};
			const int size = rox::core::FormatText(buffer, sizeof(buffer), "%s failed (Win32 error %lu).", operation, static_cast<unsigned long>(code));

			return core::MakeError(code == ERROR_ACCESS_DENIED ? core::EErrorCode::Conflict : core::EErrorCode::ExternalFailure, "platform.window.clipboard", { buffer, static_cast<rox::core::usize>(rox::core::Max(0, size)) });
		}

		void compositionEvent(const WindowRecord& record, EWindowEventKind kind) noexcept
		{
			WindowEvent event;
			event.Window = record.ID;
			event.Kind = kind;
			event.Composition.Session = record.TextSession;
			appendEvent(record, event);
		}

		void readComposition(WindowRecord& record, HIMC context, DWORD query, EWindowEventKind kind) noexcept
		{
			const LONG bytes = ImmGetCompositionStringW(context, query, nullptr, 0);

			if (bytes < 0 || bytes % sizeof(wchar_t) != 0 || bytes > WindowCompositionEvent::MAX_BYTES)
			{
				m_bPendingOverflowed = true;

				return;
			}

			wchar_t utf16[WindowCompositionEvent::MAX_BYTES / sizeof(wchar_t)]{};

			if (bytes != 0 && ImmGetCompositionStringW(context, query, utf16, bytes) != bytes)
			{
				m_bPendingOverflowed = true;

				return;
			}

			const int units = bytes / sizeof(wchar_t);
			const int required = units == 0 ? 0 : WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16, units, nullptr, 0, nullptr, nullptr);

			if ((units != 0 && required == 0) || required > WindowCompositionEvent::MAX_BYTES)
			{
				m_bPendingOverflowed = true;

				return;
			}

			WindowEvent event;
			event.Window = record.ID;
			event.Kind = kind;
			event.Composition.Session = record.TextSession;
			event.Composition.ByteCount = required;

			if (required != 0 && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16, units, event.Composition.Utf8, required, nullptr, nullptr) != required)
			{
				m_bPendingOverflowed = true;

				return;
			}

			LONG cursor = query == GCS_RESULTSTR ? units : ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
			cursor = rox::core::Max<LONG>(0, rox::core::Min<LONG>(cursor, units));

			if (cursor > 0 && cursor < units && isHighSurrogate(utf16[cursor - 1]) && isLowSurrogate(utf16[cursor]))
			{
				--cursor;
			}

			event.Composition.CaretByte = cursor == 0 ? 0 : WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16, cursor, nullptr, 0, nullptr, nullptr);
			appendEvent(record, event);
		}

		void appendEvent(const WindowRecord& record, const WindowEvent& event) noexcept
		{
			if (!record.bAlive)
			{
				return;
			}

			ASSERT(event.Window == record.ID && event.Window.IsValid(), "A window event must identify its live source record.");
			ASSERT(m_PendingEvents.Capacity() >= m_Options.MaximumEventsPerPoll, "The pending window-event buffer must retain its configured capacity.");

			if (m_PendingEvents.Size() >= m_Options.MaximumEventsPerPoll)
			{
				if (!m_bPendingOverflowed)
				{
					m_bPendingOverflowed = true;
					LOG_WARNING("Platform.Window", "The bounded Windows event buffer overflowed.");
				}

				return;
			}

			const bool bAdded = m_PendingEvents.Add(event);
			ASSERT(bAdded, "A preallocated Windows event buffer must append within its configured bound.");
		}

		[[nodiscard]] core::Result<void> validateDesc(const WindowDesc& desc) const
		{
			if (desc.Title.IsEmpty() || desc.Title.Size() > MAXIMUM_WINDOW_TITLE_UTF8_BYTE_COUNT || !isValidExtent(desc.ClientExtent))
			{
				return core::Result<void>::Failure(
					makeWindowError(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-WINDOW-DESC",
						"The window descriptor has an empty or oversized title or invalid client extent.",
						"Use a non-empty bounded UTF-8 title and dimensions from one through 16,384."));
			}

			for (const char character : desc.Title)
			{
				if (character == '\0')
				{
					return core::Result<void>::Failure(makeWindowError(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-WINDOW-DESC",
						"The window title contains an embedded null character.",
						"Remove embedded null characters from the UTF-8 title."));
				}
			}

			if (!core::IsValidUtf8(desc.Title))
			{
				return core::Result<void>::Failure(makeWindowError(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-WINDOW-DESC",
					"The window title is not well-formed UTF-8.",
					"Encode the title as valid UTF-8 before creating the window."));
			}

			return core::Result<void>::Success();
		}

		[[nodiscard]] static bool isValidExtent(const WindowExtent extent) noexcept
		{
			return extent.Width > 0 && extent.Height > 0 && extent.Width <= MAXIMUM_WINDOW_EXTENT_DIMENSION && extent.Height <= MAXIMUM_WINDOW_EXTENT_DIMENSION;
		}

		[[nodiscard]] WindowRecord* reserveRecord() noexcept
		{
			for (core::uint32 slotIndex = 0; slotIndex < m_Records.Size(); ++slotIndex)
			{
				WindowRecord& record = m_Records[slotIndex];

				if (record.bReserved || record.bGenerationExhausted)
				{
					continue;
				}

				record.Owner = this;
				record.NativeWindow = nullptr;
				record.ID = { m_Owner, slotIndex, record.Generation };
				record.StateSnapshot = {};
				record.PendingHighSurrogate = 0;
				record.LastPointerX = 0;
				record.LastPointerY = 0;

				for (bool& bPressed : record.Keys)
				{
					bPressed = false;
				}

				for (bool& bPressed : record.PointerButtons)
				{
					bPressed = false;
				}

				record.bReserved = true;
				record.bAlive = false;
				record.bHasPointerPosition = false;
				record.bSuppressPointerReconcile = false;
				record.LastAbsoluteDevice = nullptr;
				record.LastAbsoluteX = 0;
				record.LastAbsoluteY = 0;
				record.bHasAbsoluteSample = false;
				record.bLastAbsoluteVirtualDesktop = false;

				return &record;
			}

			return nullptr;
		}

		void releaseReservedRecord(WindowRecord& record) noexcept
		{
			ASSERT(record.bReserved && !record.bAlive, "Rolling back window creation requires a reserved unpublished record.");
			record.Owner = nullptr;
			record.NativeWindow = nullptr;
			record.ID = {};
			record.bReserved = false;
		}

		void advanceGeneration(WindowRecord& record) noexcept
		{
			if (record.Generation == rox::core::NumericLimits<core::uint32>::max())
			{
				record.bGenerationExhausted = true;

				return;
			}

			++record.Generation;
			ASSERT(record.Generation != 0, "A window slot generation must remain nonzero.");
		}

		[[nodiscard]] core::Result<WindowRecord*> resolveRecord(const WindowId window)
		{
			ASSERT(window.IsValid() && window.Owner == m_Owner && window.Slot < m_Records.Size(), "Window access requires a valid handle owned by this window system.");

			WindowRecord& record = m_Records[window.Slot];

			if (!record.bAlive || record.Generation != window.Generation)
			{
				return core::Result<WindowRecord*>::Failure(makeWindowError(core::EErrorCode::NotFound, "ROX-PLATFORM-WINDOW-NOT-FOUND",
					"The window ID no longer identifies a live window.",
					"Create a new window and use its current generation."));
			}

			ASSERT(record.bReserved && record.Owner == this && record.NativeWindow != nullptr, "A live window slot must retain its stable owner and native handle.");
			return core::Result<WindowRecord*>::Success(&record);
		}

		[[nodiscard]] core::Result<const WindowRecord*> resolveRecord(const WindowId window) const
		{
			ASSERT(window.IsValid() && window.Owner == m_Owner && window.Slot < m_Records.Size(), "Window access requires a valid handle owned by this window system.");

			const WindowRecord& record = m_Records[window.Slot];

			if (!record.bAlive || record.Generation != window.Generation)
			{
				return core::Result<const WindowRecord*>::Failure(makeWindowError(
					core::EErrorCode::NotFound, "ROX-PLATFORM-WINDOW-NOT-FOUND", "The window ID no longer identifies a live window.",
					"Create a new window and use its current generation."));
			}

			ASSERT(record.bReserved && record.Owner == this && record.NativeWindow != nullptr, "A live window slot must retain its stable owner and native handle.");
			return core::Result<const WindowRecord*>::Success(&record);
		}

		void updateState(WindowRecord& record) const noexcept
		{
			ASSERT(record.NativeWindow != nullptr, "Updating window state requires a live native window.");
			RECT clientRect = {};
			const BOOL bClientRect = GetClientRect(record.NativeWindow, &clientRect);
			ASSERT(bClientRect != FALSE, "A live Windows window must provide its client rectangle.");
			record.StateSnapshot.bMinimized = IsIconic(record.NativeWindow) != FALSE;
			record.StateSnapshot.ClientExtent = record.StateSnapshot.bMinimized
				? WindowExtent{}
				: WindowExtent{ static_cast<core::uint32>(clientRect.right - clientRect.left),
							   static_cast<core::uint32>(clientRect.bottom - clientRect.top) };
			const UINT dpi = GetDpiForWindow(record.NativeWindow);
			record.StateSnapshot.DpiX = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
			record.StateSnapshot.DpiY = record.StateSnapshot.DpiX;
			record.StateSnapshot.bFocused = GetFocus() == record.NativeWindow;
			record.StateSnapshot.bVisible = IsWindowVisible(record.NativeWindow) != FALSE;
		}

		void assertOwnerThread() const noexcept
		{
			ASSERT(GetCurrentThreadId() == m_OwnerThread, "Windows window-system operations must run on the creator thread.");
		}

		// Only the owning instance releases the arbiter. Clear its record even if reconciliation fails
		// so destruction cannot leave a pointer to the retired instance.
		void reconcileOwnedPointerRecordOnShutdown() noexcept
		{
			WindowId claimant;
			{
				rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);

				if (s_PointerArbiter.Owner != this)
				{
					return;
				}

				claimant = s_PointerArbiter.Claimant;
			}

			assertArbiterThread();
			core::Result<void> reconciled = reconcileTowardAbsent(claimant);

			if (!reconciled)
			{
				LOG_WARNING("Platform.Window", "The owning window system abandoned unreleased relative pointer state at shutdown.");
			}

			rox::core::LockGuard<rox::core::Mutex> guard(s_PointerArbiter.Mutex);
			s_PointerArbiter.Claimant = {};
			s_PointerArbiter.Owner = nullptr;
			s_PointerArbiter.RegisteredTarget = {};
			s_PointerArbiter.Confinement = EConfinement::Absent;
			s_PointerArbiter.AppliedClipRect = {};
			s_PointerArbiter.CursorHideCalls = 0;
			assertPointerRecordConsistentLocked();
		}

		void shutdown() noexcept
		{
			assertOwnerThread();
			ASSERT(m_LiveWindowCount == 0, "Every Windows window must be destroyed explicitly before its window system.");

			for (const WindowRecord& record : m_Records)
			{
				ASSERT(!record.bAlive && record.NativeWindow == nullptr,
					"Window-system shutdown requires every native window to be released.");
			}

			reconcileOwnedPointerRecordOnShutdown();

			if (m_Class != 0)
			{
				const BOOL bUnregistered = UnregisterClassW(m_ClassName, m_Instance);
				ASSERT(bUnregistered != FALSE, "A service-unique Windows class must unregister after all windows are destroyed.");
				m_Class = 0;
			}

			m_Instance = nullptr;
		}

	private:
		core::MemoryResource& m_Resource;
		WindowsWindowSystemOptions m_Options;
		core::Array<WindowRecord> m_Records;
		core::Array<WindowEvent> m_PendingEvents;
		core::Array<WindowEvent> m_PublishedEvents;
		core::uint64 m_Owner = 0;
		core::uint32 m_OwnerThread = 0;
		core::uint32 m_LiveWindowCount = 0;
		HINSTANCE m_Instance = nullptr;
		ATOM m_Class = 0;
		wchar_t m_ClassName[64] = {};
		bool m_bPendingOverflowed = false;
		bool m_bPendingQuitRequested = false;
		// One object for the whole process, because every effect behind it is process, desktop, or
		// input-queue scoped and a second instance on a second thread would otherwise own a second
		// copy of state Windows keeps only one of.
		inline static PointerArbiter s_PointerArbiter;
	};

	WindowsWindowSystem::~WindowsWindowSystem() noexcept
	{
		if (m_State != nullptr)
		{
			core::DestroyObject(*m_Resource, m_State);
			m_State = nullptr;
			m_Resource = nullptr;
		}
	}

	core::Result<void> WindowsWindowSystem::TryCreate(core::MemoryResource& resource, const WindowsWindowSystemOptions options,
		WindowsWindowSystem* const outSystem)
	{
		ASSERT(outSystem != nullptr, "Windows window-system creation requires an output object.");
		ASSERT(!outSystem->IsValid(), "Windows window-system creation requires an empty output object.");
		ASSERT(resource.GetThreadSafety() == core::EMemoryResourceThreadSafety::ThreadSafe, "Windows window systems require a thread-safe memory resource.");

		if (options.MaximumWindows == 0 || options.MaximumWindows > MAXIMUM_WINDOW_COUNT || options.MaximumEventsPerPoll == 0 || options.MaximumEventsPerPoll > MAXIMUM_WINDOW_EVENT_COUNT)
		{
			return core::Result<void>::Failure(makeWindowError(core::EErrorCode::InvalidArgument, "ROX-PLATFORM-WINDOW-OPTIONS",
				"The Windows window-system capacities are outside their supported bounds.",
				"Select one through sixty-four windows and one through 65,536 events."));
		}

		State* const state = core::CreateObject<State>(resource, resource, options);

		if (state == nullptr)
		{
			return core::Result<void>::Failure(makeWindowError(
				core::EErrorCode::ExternalFailure,
				"ROX-PLATFORM-WINDOW-ALLOCATION",
				"Memory allocation for the Windows window system failed.",
				"Release bounded service memory and retry creation."));
		}

		const EWindowInitializeResult initializeResult = state->Initialize();

		if (initializeResult != EWindowInitializeResult::Success)
		{
			core::DestroyObject(resource, state);

			if (initializeResult == EWindowInitializeResult::AllocationFailed)
			{
				return core::Result<void>::Failure(makeWindowError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-ALLOCATION",
					"Memory allocation for Windows window records or event buffers failed.",
					"Release bounded service memory and retry creation."));
			}

			return core::Result<void>::Failure(makeWindowError(core::EErrorCode::ExternalFailure, "ROX-PLATFORM-WINDOW-CLASS",
				"Windows window-system storage or class registration failed.",
				"Release bounded memory, verify desktop availability, and retry creation."));
		}

		outSystem->m_Resource = &resource;
		outSystem->m_State = state;
		outSystem->assertInvariant();
		LOG_INFO("Platform.Window", "The Windows window system started.");

		return core::Result<void>::Success();
	}

	core::Result<WindowId> WindowsWindowSystem::CreateWindow(const WindowDesc& desc)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Window creation requires a valid Windows window system.");

		return m_State->CreateWindow(desc);
	}

	core::Result<void> WindowsWindowSystem::DestroyWindow(const WindowId window)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Window destruction requires a valid Windows window system.");

		return m_State->DestroyWindow(window);
	}

	core::Result<void> WindowsWindowSystem::SetWindowVisibility(const WindowId window, const bool bVisible)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Window visibility requires a valid Windows window system.");

		return m_State->SetWindowVisibility(window, bVisible);
	}

	core::Result<void> WindowsWindowSystem::ResizeWindow(const WindowId window, const WindowExtent clientExtent)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Window resize requires a valid Windows window system.");

		return m_State->ResizeWindow(window, clientExtent);
	}

	core::Result<core::String> WindowsWindowSystem::ReadClipboardText(const WindowId window, core::MemoryResource& memory, core::uint32 maximumBytes)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Clipboard reads require a valid Windows window system.");

		return m_State->ReadClipboardText(window, memory, maximumBytes);
	}

	core::Result<void> WindowsWindowSystem::WriteClipboardText(const WindowId window, core::StringView text)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Clipboard writes require a valid Windows window system.");

		return m_State->WriteClipboardText(window, text);
	}

	core::Result<void> WindowsWindowSystem::SetTextInputState(const WindowId window, const WindowTextInputState& state)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Native text input requires a valid Windows window system.");

		return m_State->SetTextInputState(window, state);
	}

	core::Result<void> WindowsWindowSystem::PublishTextDocument(WindowId window, const WindowTextDocument& document)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Text publication requires a valid Windows window system.");

		return m_State->PublishTextDocument(window, document);
	}

	core::Result<void> WindowsWindowSystem::SetPointerMode(const WindowId window, const EPointerMode mode)
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Pointer-mode selection requires a valid Windows window system.");

		return m_State->SetPointerMode(window, mode);
	}

	core::Result<WindowState> WindowsWindowSystem::GetWindowState(const WindowId window) const
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Window-state access requires a valid Windows window system.");

		return m_State->GetWindowState(window);
	}

	core::Result<NativeWindowHandle> WindowsWindowSystem::GetNativeHandle(const WindowId window) const
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Native-window borrowing requires a valid Windows window system.");

		return m_State->GetNativeHandle(window);
	}

	WindowEventBatch WindowsWindowSystem::PollEvents()
	{
		assertInvariant();
		ASSERT(m_State != nullptr, "Event polling requires a valid Windows window system.");

		return m_State->PollEvents();
	}

	bool WindowsWindowSystem::IsValid() const noexcept
	{
		assertInvariant();

		return m_State != nullptr;
	}

	void WindowsWindowSystem::assertInvariant() const noexcept
	{
		ASSERT((m_Resource == nullptr) == (m_State == nullptr), "A Windows window system must own both its resource and state or neither.");
	}
} // namespace rox::os

namespace rox::os
{
	core::Result<void> WindowsWindowSystem::SetAccessibilitySnapshot(WindowId window, const AccessibilitySnapshot& snapshot)
	{
		assertInvariant();
		ASSERT(m_State, "Accessibility requires a valid window system.");

		return m_State->SetAccessibilitySnapshot(window, snapshot);
	}

	core::Result<AccessibilityRequest> WindowsWindowSystem::PollAccessibilityAction(WindowId window)
	{
		assertInvariant();
		ASSERT(m_State, "Accessibility requires a valid window system.");

		return m_State->PollAccessibilityAction(window);
	}
}
