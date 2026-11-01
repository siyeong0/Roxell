#include "WindowsTextInput.h"
#include <Synchronization.hpp>
#include <TextPrimitives.hpp>
#include <Algorithms.hpp>
#include <Types.h>
#include <ByteOperations.hpp>
#include <NumericLimits.hpp>
#include <Comparison.hpp>
#include <Utility.hpp>
#include <Array.hpp>
#include <Utf8.h>
#include <msctf.h>
#include <textstor.h>
#include <objbase.h>
#include <olectl.h>

namespace rox::os
{
	using namespace core;

	namespace
	{
		constexpr uint32 MAX_TEXT_BYTES = 1'048'576;
		Error textError(HRESULT result, StringView operation)
		{
			char detail[256];
			FormatText(detail, sizeof(detail), "%.*s (HRESULT 0x%08lx).", static_cast<int>(operation.Size()), operation.Data(), static_cast<unsigned long>(result));

			return MakeError(result == RPC_E_CHANGED_MODE ? EErrorCode::Conflict : EErrorCode::ExternalFailure,
				"platform.window.tsf", StringView(detail, CStringLength(detail)),
				"Use the owning STA UI thread and resynchronize the focused text document.");
		}

		template <class T> void release(T*& object) noexcept
		{
			if (object)
			{
				object->Release();
				object = nullptr;
			}
		}

		bool boundary(const Array<wchar_t>& text, LONG position) noexcept
		{
			return position >= 0 && static_cast<usize>(position) <= text.Size() &&
				(position == 0 || static_cast<usize>(position) == text.Size() || !(text[position] >= 0xdc00 && text[position] <= 0xdfff));
		}

		uint32 bytePosition(const Array<wchar_t>& text, LONG position) noexcept
		{
			if (position == 0)
			{
				return 0;
			}

			return static_cast<uint32>(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.Data(), position, nullptr, 0, nullptr, nullptr));
		}

		LONG acpPosition(const Array<wchar_t>& text, uint32 bytes) noexcept
		{
			LONG first = 0;
			LONG last = static_cast<LONG>(text.Size());

			while (first < last)
			{
				LONG middle = first + (last - first) / 2;

				if (!boundary(text, middle))
				{
					--middle;
				}

				if (bytePosition(text, middle) < bytes)
				{
					first = middle + 1;

					if (!boundary(text, first))
					{
						++first;
					}
				}
				else
				{
					last = middle;
				}
			}

			return first;
		}

		bool copy(const Array<wchar_t>& source, Array<wchar_t>& destination) noexcept
		{
			if (!destination.Resize(source.Size()))
			{
				return false;
			}

			if (!source.IsEmpty())
			{
				CopyBytes(destination.Data(), source.Data(), source.Size() * sizeof(wchar_t));
			}

			return true;
		}
	}

	class WindowsTextInput::State final : public ITextStoreACP, public ITfContextOwnerCompositionSink
	{
	public:
		State(MemoryResource& memory, HWND window, WindowId id, Emit emit, void* context) noexcept
			: m_Memory(memory)
			, m_Text(memory)
			, m_LockBefore(memory)
			, m_CompositionBefore(memory)
			, m_Window(window)
			, m_Id(id)
			, m_Callback(emit)
			, m_Context(context)
			, m_Thread(GetCurrentThreadId())
		{
		}

		~State() noexcept
		{
			Close();
		}

		HRESULT Initialize()
		{
			HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

			if (FAILED(result))
			{
				return result;
			}

			m_bApartment = true;
			result = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, reinterpret_cast<void**>(&m_Manager));

			if (FAILED(result))
			{
				return result;
			}

			result = m_Manager->Activate(&m_Client);

			if (FAILED(result))
			{
				return result;
			}

			m_bActivated = true;
			result = m_Manager->CreateDocumentMgr(&m_Document);

			if (FAILED(result))
			{
				return result;
			}

			result = m_Document->CreateContext(m_Client, 0, static_cast<ITextStoreACP*>(this), &m_NativeContext, &m_EditCookie);

			if (FAILED(result))
			{
				return result;
			}

			result = m_Document->Push(m_NativeContext);

			if (FAILED(result))
			{
				return result;
			}

			m_bPushed = true;

			return m_Manager->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&m_Keys));
		}

		HRESULT Publish(const WindowTextDocument& document)
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (m_bClosed)
			{
				return TF_E_DISCONNECTED;
			}

			if (m_Lock != 0)
			{
				return TS_E_NOLOCK;
			}

			if (document.Session == 0 ||
				document.Revision == 0 ||
				document.bPassword ||
				document.Text.Size() > MAX_TEXT_BYTES ||
				!IsValidUtf8(document.Text) ||
				document.Text.Contains('\0') ||
				document.AnchorByte > document.Text.Size() ||
				document.CaretByte > document.Text.Size())
			{
				return E_INVALIDARG;
			}

			const auto scalarBoundary = [&](uint32 value)
				{
					return value == document.Text.Size() || (static_cast<unsigned char>(document.Text[value]) & 0xc0) != 0x80;
				};

			if (!scalarBoundary(document.AnchorByte) || !scalarBoundary(document.CaretByte))
			{
				return E_INVALIDARG;
			}

			bool bTextChanged = false;
			bool bSelectionChanged = false;
			const bool bStatusChanged = m_bReadOnly != document.bReadOnly;
			const LONG oldEnd = static_cast<LONG>(m_Text.Size());

			if (m_Session != document.Session || m_Revision != document.Revision || m_bFailed)
			{
				const int units = document.Text.IsEmpty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, document.Text.Data(), static_cast<int>(document.Text.Size()), nullptr, 0);
				Array<wchar_t> next(m_Memory);

				if (!next.Resize(units))
				{
					return E_OUTOFMEMORY;
				}

				if (units != 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, document.Text.Data(),
					static_cast<int>(document.Text.Size()), next.Data(), units) != units)
				{
					return E_INVALIDARG;
				}

				Cancel();
				m_Text = Move(next);
				m_Session = document.Session;
				m_Revision = document.Revision;
				m_bFailed = false;
				bTextChanged = true;
			}

			m_bReadOnly = document.bReadOnly;
			m_bPassword = document.bPassword;
			m_bMultiline = document.bMultiline;
			m_Geometry = document.Geometry;

			if (!m_bComposing)
			{
				const LONG anchor = acpPosition(m_Text, document.AnchorByte);
				const LONG caret = acpPosition(m_Text, document.CaretByte);
				const TS_SELECTION_ACP selection{Min(anchor, caret), Max(anchor, caret), {anchor > caret ? TS_AE_START : TS_AE_END, FALSE} };
				bSelectionChanged = m_Selection.acpStart != selection.acpStart || m_Selection.acpEnd != selection.acpEnd || m_Selection.style.ase != selection.style.ase;
				m_Selection = selection;
			}

			if (bTextChanged && m_Sink && (m_SinkMask & TS_AS_TEXT_CHANGE))
			{
				TS_TEXTCHANGE change{ 0, oldEnd, static_cast<LONG>(m_Text.Size()) };
				m_Sink->OnTextChange(0, &change);
			}

			if (bSelectionChanged && m_Sink && (m_SinkMask & TS_AS_SEL_CHANGE))
			{
				m_Sink->OnSelectionChange();
			}

			if (bStatusChanged && m_Sink && (m_SinkMask & TS_AS_STATUS_CHANGE))
			{
				m_Sink->OnStatusChange(TS_SD_READONLY);
			}

			if (m_Sink && (m_SinkMask & TS_AS_LAYOUT_CHANGE))
			{
				m_Sink->OnLayoutChange(TS_LC_CHANGE, 1);
			}

			return S_OK;
		}

		Result<void> SetInputState(const WindowTextInputState& state)
		{
			ASSERT(m_Thread == GetCurrentThreadId(), "TSF state requires its owning thread.");

			if (m_bClosed)
			{
				return Result<void>::Failure(textError(TF_E_DISCONNECTED, "TSF owner is closed."));
			}

			if (state.bEnabled && state.Session != m_Session)
			{
				return Result<void>::Failure(textError(E_INVALIDARG, "TSF activation requires its published document session."));
			}

			if (state.bCancelComposition || m_Session != state.Session || !state.bEnabled)
			{
				Cancel();
			}

			const bool bChanged = m_bEnabled != state.bEnabled;
			m_bEnabled = state.bEnabled;

			if (bChanged && m_Sink && (m_SinkMask & TS_AS_STATUS_CHANGE))
			{
				m_Sink->OnStatusChange(TS_SD_READONLY);
			}

			Focus(state.bEnabled && GetFocus() == m_Window);

			return Result<void>::Success();
		}

		void SetWindowFocus(bool bFocused) noexcept
		{
			Focus(bFocused && m_bEnabled);
		}

		void Focus(bool bFocused) noexcept
		{
			if (!isOwnerThread() || m_bClosed || !m_Manager)
			{
				return;
			}

			ITfDocumentMgr* previous = nullptr;
			m_Manager->AssociateFocus(m_Window, bFocused ? m_Document : nullptr, &previous);
			release(previous);

			if (bFocused)
			{
				m_Manager->SetFocus(m_Document);
			}
			else
			{
				Cancel();
			}
		}

		void Cancel() noexcept
		{
			if (!m_bComposing)
			{
				return;
			}

			m_bCancelling = true;
			ITfContextOwnerCompositionServices* services = nullptr;

			if (m_NativeContext && SUCCEEDED(m_NativeContext->QueryInterface(IID_ITfContextOwnerCompositionServices, reinterpret_cast<void**>(&services))))
			{
				services->TerminateComposition(nullptr);
				release(services);
			}

			if (m_bComposing)
			{
				endComposition();
			}

			m_bCancelling = false;
		}

		void Close() noexcept
		{
			if (m_bClosed)
			{
				return;
			}

			ASSERT(isOwnerThread(), "TSF owners must close on the publishing STA thread.");
			Focus(false);
			m_bClosed = true;
			m_bEnabled = false;
			m_Callback = nullptr;
			m_Context = nullptr;
			m_Geometry.Reset();
			m_PendingLock = 0;

			if (m_Document && m_bPushed)
			{
				m_Document->Pop(TF_POPF_ALL);
				m_bPushed = false;
			}

			release(m_NativeContext);
			release(m_Document);
			release(m_Keys);
			release(m_Sink);

			if (m_Manager && m_bActivated)
			{
				m_Manager->Deactivate();
				m_bActivated = false;
			}

			release(m_Manager);

			if (m_bApartment)
			{
				CoUninitialize();
				m_bApartment = false;
			}
		}

		void Pump() noexcept
		{
			if (isOwnerThread() && !m_bClosed && m_Lock == 0 && m_PendingLock != 0 && m_Sink)
			{
				const DWORD flags = m_PendingLock;
				m_PendingLock = 0;
				HRESULT ignored = S_OK;
				RequestLock(flags, &ignored);
			}
		}

		bool Filter(const MSG& message) noexcept
		{
			if (!isOwnerThread() || !m_bEnabled || m_bClosed || message.hwnd != m_Window || GetFocus() != m_Window || !m_Keys)
			{
				return false;
			}

			BOOL eaten = FALSE;

			if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN)
			{
				if (SUCCEEDED(m_Keys->TestKeyDown(message.wParam, message.lParam, &eaten)) && eaten)
				{
					m_Keys->KeyDown(message.wParam, message.lParam, &eaten);
				}
			}
			else if (message.message == WM_KEYUP || message.message == WM_SYSKEYUP)
			{
				if (SUCCEEDED(m_Keys->TestKeyUp(message.wParam, message.lParam, &eaten)) && eaten)
				{
					m_Keys->KeyUp(message.wParam, message.lParam, &eaten);
				}
			}

			return eaten != FALSE;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override
		{
			if (!result)
			{
				return E_POINTER;
			}

			*result = nullptr;

			if (iid == IID_IUnknown || iid == IID_ITextStoreACP)
			{
				*result = static_cast<ITextStoreACP*>(this);
			}
			else if (iid == IID_ITfContextOwnerCompositionSink)
			{
				*result = static_cast<ITfContextOwnerCompositionSink*>(this);
			}
			else
			{
				return E_NOINTERFACE;
			}

			AddRef();

			return S_OK;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++m_References;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG remaining = --m_References;

			if (!remaining)
			{
				DestroyObject(m_Memory, this);
			}

			return remaining;
		}

		HRESULT STDMETHODCALLTYPE AdviseSink(REFIID iid, IUnknown* unknown, DWORD mask) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (!unknown || iid != IID_ITextStoreACPSink)
			{
				return E_INVALIDARG;
			}

			ITextStoreACPSink* candidate = nullptr;
			const HRESULT result = unknown->QueryInterface(iid, reinterpret_cast<void**>(&candidate));

			if (FAILED(result))
			{
				return result;
			}

			if (m_Sink && m_Sink != candidate)
			{
				candidate->Release();

				return CONNECT_E_ADVISELIMIT;
			}

			release(m_Sink);
			m_Sink = candidate;
			m_SinkMask = mask;

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE UnadviseSink(IUnknown* unknown) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (!m_Sink || !unknown)
			{
				return CONNECT_E_NOCONNECTION;
			}

			IUnknown* identity = nullptr;
			IUnknown* expected = nullptr;
			unknown->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity));
			m_Sink->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&expected));
			const bool bMatches = identity == expected;
			release(identity);
			release(expected);

			if (!bMatches)
			{
				return CONNECT_E_NOCONNECTION;
			}

			release(m_Sink);

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE RequestLock(DWORD flags, HRESULT* session) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (!session)
			{
				return E_POINTER;
			}

			if (m_bClosed || !m_Sink)
			{
				*session = TF_E_DISCONNECTED;

				return S_OK;
			}

			if ((flags & TS_LF_READ) == 0)
			{
				return E_INVALIDARG;
			}

			if (m_Lock != 0)
			{
				if (flags & TS_LF_SYNC)
				{
					*session = TS_E_SYNCHRONOUS;
				}
				else
				{
					m_PendingLock |= flags & TS_LF_READWRITE;
					*session = TS_S_ASYNC;
				}

				return S_OK;
			}

			if ((flags & TS_LF_READWRITE) == TS_LF_READWRITE && !copy(m_Text, m_LockBefore))
			{
				*session = E_OUTOFMEMORY;

				return S_OK;
			}

			m_Lock = flags & TS_LF_READWRITE;
			m_bCommittedInLock = false;
			m_LockSelection = m_Selection;
			m_Sink->AddRef();
			ITextStoreACPSink* callback = m_Sink;
			*session = callback->OnLockGranted(m_Lock);
			callback->Release();
			const bool bWrote = m_Lock == TS_LF_READWRITE;
			m_Lock = 0;

			if (bWrote && !m_bCommittedInLock)
			{
				if (m_bComposing)
				{
					emitDifference(EWindowEventKind::CompositionUpdate, m_CompositionBefore, false);
				}
				else
				{
					emitDifference(EWindowEventKind::TextEdit, m_LockBefore, true);
				}
			}

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetStatus(TS_STATUS* status) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (!status)
			{
				return E_POINTER;
			}

			*status = { static_cast<DWORD>(m_bReadOnly || !m_bEnabled ? TS_SD_READONLY : 0), TS_SS_NOHIDDENTEXT };

			return m_bClosed ? TF_E_DISCONNECTED : S_OK;
		}

		HRESULT STDMETHODCALLTYPE QueryInsert(LONG first, LONG last, ULONG count, LONG* start, LONG* end) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (!start || !end)
			{
				return E_POINTER;
			}

			if (!isValidRange(first, last) || count > MAX_TEXT_BYTES || m_Text.Size() - (last - first) + count > MAX_TEXT_BYTES)
			{
				return TS_E_INVALIDPOS;
			}

			*start = first;
			*end = first + static_cast<LONG>(count);

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* selection, ULONG* fetched) override
		{
			const HRESULT result = checkReadLock();

			if (FAILED(result))
			{
				return result;
			}

			if (!selection || !fetched || count == 0)
			{
				return E_INVALIDARG;
			}

			*fetched = 0;

			if (index != 0 && index != TS_DEFAULT_SELECTION)
			{
				return E_INVALIDARG;
			}

			*selection = m_Selection;
			*fetched = 1;

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetSelection(ULONG count, const TS_SELECTION_ACP* selection) override
		{
			const HRESULT result = checkWriteLock();

			if (FAILED(result))
			{
				return result;
			}

			if (count != 1 || !selection || !isValidRange(selection->acpStart, selection->acpEnd))
			{
				return TS_E_INVALIDPOS;
			}

			m_Selection = *selection;

			if (!m_bComposing)
			{
				WindowEvent event;
				event.Kind = EWindowEventKind::TextSelection;
				fillEventIdentity(event);
				event.Composition.StartByte = bytePosition(m_Text, m_Selection.acpStart);
				event.Composition.EraseBytes = bytePosition(m_Text, m_Selection.acpEnd) - event.Composition.StartByte;
				emitEvent(event);
			}

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetText(LONG first, LONG last, WCHAR* plain, ULONG plainMaximum, ULONG* plainCount, TS_RUNINFO* runs, ULONG runMaximum, ULONG* runCount, LONG* next) override
		{
			const HRESULT result = checkReadLock();

			if (FAILED(result))
			{
				return result;
			}

			if (!plainCount || !runCount || !next || (plainMaximum && !plain) || (runMaximum && !runs))
			{
				return E_POINTER;
			}

			if (last == -1)
			{
				last = static_cast<LONG>(m_Text.Size());
			}

			if (first < 0 || first > last || static_cast<usize>(last) > m_Text.Size())
			{
				return TS_E_INVALIDPOS;
			}

			const ULONG available = static_cast<ULONG>(last - first);
			const unsigned long count = plainMaximum ? Min<ULONG>(available, plainMaximum) : (runMaximum ? available : 0);
			*plainCount = plainMaximum ? count : 0;
			*runCount = 0;
			*next = first + static_cast<LONG>(count);

			if (plainMaximum && count)
			{
				CopyBytes(plain, m_Text.Data() + first, count * sizeof(wchar_t));
			}

			if (runMaximum && count)
			{
				runs[0] = { count, TS_RT_PLAIN };
				*runCount = 1;
			}

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetText(DWORD, LONG first, LONG last, const WCHAR* text, ULONG count, TS_TEXTCHANGE* change) override
		{
			const HRESULT result = checkWriteLock();

			if (FAILED(result))
			{
				return result;
			}

			if (!change || (count && !text))
			{
				return E_POINTER;
			}

			if (!isValidRange(first, last) || count > WindowCompositionEvent::MAX_BYTES || m_Text.Size() - (last - first) + count > MAX_TEXT_BYTES)
			{
				return TS_E_INVALIDPOS;
			}

			const int bytes = count ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, static_cast<int>(count), nullptr, 0, nullptr, nullptr) : 0;

			if ((count && bytes == 0) || bytes > static_cast<int>(WindowCompositionEvent::MAX_BYTES))
			{
				return E_INVALIDARG;
			}

			if (!m_bMultiline && count != 0 && (Find(text, text + count, L'\r') != text + count || Find(text, text + count, L'\n') != text + count))
			{
				return E_INVALIDARG;
			}

			if (bytePosition(m_Text, static_cast<LONG>(m_Text.Size())) - (bytePosition(m_Text, last) - bytePosition(m_Text, first)) + bytes > MAX_TEXT_BYTES)
			{
				return E_INVALIDARG;
			}

			if (m_bComposing && (first < m_CompositionFirst || last > m_CompositionLast))
			{
				return TS_E_INVALIDPOS;
			}

			Array<wchar_t> replacement(m_Memory);

			if (!replacement.Reserve(Max<usize>(m_CompositionBefore.Size(), m_Text.Size() - (last - first) + count)))
			{
				return E_OUTOFMEMORY;
			}

			if (!replacement.Resize(m_Text.Size() - (last - first) + count))
			{
				return E_OUTOFMEMORY;
			}

			if (first)
			{
				CopyBytes(replacement.Data(), m_Text.Data(), first * sizeof(wchar_t));
			}

			if (count)
			{
				CopyBytes(replacement.Data() + first, text, count * sizeof(wchar_t));
			}

			const usize tail = m_Text.Size() - last;

			if (tail)
			{
				CopyBytes(replacement.Data() + first + count, m_Text.Data() + last, tail * sizeof(wchar_t));
			}

			m_Text = Move(replacement);
			*change = { first, last, first + static_cast<LONG>(count) };
			m_Selection = { change->acpNewEnd, change->acpNewEnd, {TS_AE_END, FALSE} };

			if (m_bComposing)
			{
				m_CompositionLast += static_cast<LONG>(count) - (last - first);
			}

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE InsertTextAtSelection(DWORD flags, const WCHAR* text, ULONG count, LONG* first, LONG* last, TS_TEXTCHANGE* change) override
		{
			const HRESULT result = checkWriteLock();

			if (FAILED(result))
			{
				return result;
			}

			if (flags & TS_IAS_QUERYONLY)
			{
				return QueryInsert(m_Selection.acpStart, m_Selection.acpEnd, count, first, last);
			}

			TS_TEXTCHANGE changed{};
			const HRESULT inserted = SetText(0, m_Selection.acpStart, m_Selection.acpEnd, text, count, &changed);

			if (FAILED(inserted))
			{
				return inserted;
			}

			if (first)
			{
				*first = changed.acpStart;
			}

			if (last)
			{
				*last = changed.acpNewEnd;
			}

			if (change)
			{
				*change = changed;
			}

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetFormattedText(LONG, LONG, IDataObject** object) override
		{
			if (object)
			{
				*object = nullptr;
			}

			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE GetEmbedded(LONG, REFGUID, REFIID, IUnknown** object) override
		{
			if (object)
			{
				*object = nullptr;
			}

			return TS_E_NOOBJECT;
		}

		HRESULT STDMETHODCALLTYPE QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* insertable) override
		{
			if (!insertable)
			{
				return E_POINTER;
			}

			*insertable = FALSE;

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override
		{
			return TS_E_NOOBJECT;
		}

		HRESULT STDMETHODCALLTYPE InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override
		{
			return TS_E_NOOBJECT;
		}

		HRESULT STDMETHODCALLTYPE RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*) override
		{
			return isOwnerThread() ? S_OK : RPC_E_WRONG_THREAD;
		}

		HRESULT STDMETHODCALLTYPE RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override
		{
			return isOwnerThread() ? S_OK : RPC_E_WRONG_THREAD;
		}

		HRESULT STDMETHODCALLTYPE RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override
		{
			return isOwnerThread() ? S_OK : RPC_E_WRONG_THREAD;
		}

		HRESULT STDMETHODCALLTYPE FindNextAttrTransition(LONG, LONG halt, ULONG, const TS_ATTRID*, DWORD, LONG* next, BOOL* found, LONG* offset) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (!next || !found || !offset)
			{
				return E_POINTER;
			}

			*next = halt;
			*found = FALSE;
			*offset = 0;

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* fetched) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (!fetched)
			{
				return E_POINTER;
			}

			*fetched = 0;

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetEndACP(LONG* end) override
		{
			const HRESULT result = checkReadLock();

			if (FAILED(result))
			{
				return result;
			}

			if (!end)
			{
				return E_POINTER;
			}

			*end = static_cast<LONG>(m_Text.Size());

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetActiveView(TsViewCookie* view) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (!view)
			{
				return E_POINTER;
			}

			*view = 1;

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetACPFromPoint(TsViewCookie view, const POINT* point, DWORD flags, LONG* acp) override
		{
			const HRESULT result = checkReadLock();

			if (FAILED(result))
			{
				return result;
			}

			if (view != 1 || !point || !acp)
			{
				return E_INVALIDARG;
			}

			if (!m_Geometry)
			{
				return TS_E_NOLAYOUT;
			}

			if (flags & ~(GXFPF_NEAREST | GXFPF_ROUND_NEAREST))
			{
				return E_INVALIDARG;
			}

			POINT client = *point;

			if (!ScreenToClient(m_Window, &client))
			{
				return E_FAIL;
			}

			Result<WindowTextHit> hit = m_Geometry->Native->HitTest(client.x, client.y);

			if (!hit)
			{
				return TS_E_NOLAYOUT;
			}

			if (!hit.Value().bInside && !(flags & GXFPF_NEAREST))
			{
				return TS_E_INVALIDPOINT;
			}

			const uint32 byte = (!hit.Value().bInside || (flags & GXFPF_ROUND_NEAREST)) ? hit.Value().NearestByte : hit.Value().CharacterByte; *acp = acpPosition(m_Text, byte);

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetTextExt(TsViewCookie view, LONG first, LONG last, RECT* rect, BOOL* clipped) override
		{
			const HRESULT result = checkReadLock();

			if (FAILED(result))
			{
				return result;
			}

			if (view != 1 || !rect || !clipped || !isValidRange(first, last))
			{
				return E_INVALIDARG;
			}

			if (!m_Geometry)
			{
				return TS_E_NOLAYOUT;
			}

			Result<WindowTextRect> extent = m_Geometry->Native->GetExtent(bytePosition(m_Text, first), bytePosition(m_Text, last));

			if (!extent)
			{
				return TS_E_NOLAYOUT;
			}

			POINT origin{ extent.Value().X, extent.Value().Y };

			if (!ClientToScreen(m_Window, &origin))
			{
				return E_FAIL;
			}

			*rect = { origin.x, origin.y, origin.x + extent.Value().Width, origin.y + extent.Value().Height };
			*clipped = extent.Value().bClipped;

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetScreenExt(TsViewCookie view, RECT* rect) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (view != 1 || !rect)
			{
				return E_INVALIDARG;
			}

			if (!GetClientRect(m_Window, rect))
			{
				return E_FAIL;
			}

			POINT origin{ 0, 0 };

			if (!ClientToScreen(m_Window, &origin))
			{
				return E_FAIL;
			}

			OffsetRect(rect, origin.x, origin.y);

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetWnd(TsViewCookie view, HWND* window) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (view != 1 || !window)
			{
				return E_INVALIDARG;
			}

			*window = m_Window;

			return m_bClosed ? TF_E_DISCONNECTED : S_OK;
		}

		HRESULT STDMETHODCALLTYPE OnStartComposition(ITfCompositionView* composition, BOOL* accepted) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (!accepted)
			{
				return E_POINTER;
			}

			*accepted = FALSE;

			if (m_bComposing || !m_bEnabled || m_bReadOnly || m_bPassword || m_bClosed)
			{
				return S_OK;
			}

			ITfRange* rangeObject = nullptr;
			ITfRangeACP* acp = nullptr;
			LONG start = 0;
			LONG length = 0;

			if (!composition)
			{
				return E_INVALIDARG;
			}

			const HRESULT gotRange = composition->GetRange(&rangeObject);

			if (FAILED(gotRange))
			{
				return gotRange;
			}

			const HRESULT gotAcp = rangeObject->QueryInterface(IID_ITfRangeACP, reinterpret_cast<void**>(&acp));

			if (FAILED(gotAcp))
			{
				release(rangeObject);

				return gotAcp;
			}

			const HRESULT queried = acp->GetExtent(&start, &length);
			release(acp);
			release(rangeObject);

			if (FAILED(queried))
			{
				return queried;
			}

			if (start < 0 || length < 0 || static_cast<usize>(start) > m_Text.Size() || static_cast<usize>(length) > m_Text.Size() - start || !isValidRange(start, start + length))
			{
				return TS_E_INVALIDPOS;
			}

			if (!copy(m_Lock ? m_LockBefore : m_Text, m_CompositionBefore))
			{
				return E_OUTOFMEMORY;
			}

			m_CompositionFirst = start;
			m_CompositionLast = start + length;
			m_bComposing = true;
			*accepted = TRUE;
			const bool bChangedInLock = m_Lock && (m_LockBefore.Size() != m_Text.Size() || (!m_Text.IsEmpty() && CompareBytes(m_LockBefore.Data(), m_Text.Data(), m_Text.Size() * sizeof(wchar_t)) != 0));
			m_CompositionSelection = bChangedInLock ? m_LockSelection : TS_SELECTION_ACP{ start, start + length, {TS_AE_END, FALSE} };
			WindowEvent event;
			event.Kind = EWindowEventKind::CompositionStart;
			fillEventIdentity(event);
			event.Composition.StartByte = bytePosition(m_CompositionBefore, m_CompositionSelection.acpStart);
			event.Composition.EraseBytes = bytePosition(m_CompositionBefore, m_CompositionSelection.acpEnd) - event.Composition.StartByte;
			emitEvent(event);

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE OnUpdateComposition(ITfCompositionView*, ITfRange* next) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (!next || !m_bComposing)
			{
				return S_OK;
			}

			ITfRangeACP* acp = nullptr;
			const HRESULT queried = next->QueryInterface(IID_ITfRangeACP, reinterpret_cast<void**>(&acp));

			if (FAILED(queried))
			{
				return queried;
			}

			LONG start = 0;
			LONG count = 0;
			const HRESULT result = acp->GetExtent(&start, &count);
			release(acp);

			if (FAILED(result))
			{
				return result;
			}

			if (start < 0 || count < 0 || static_cast<usize>(start) > m_Text.Size() || static_cast<usize>(count) > m_Text.Size() - start || !isValidRange(start, start + count))
			{
				return TS_E_INVALIDPOS;
			}

			m_CompositionFirst = start;
			m_CompositionLast = start + count;

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE OnEndComposition(ITfCompositionView*) override
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (m_bComposing)
			{
				endComposition();
			}

			return S_OK;
		}

	private:
		bool isOwnerThread() const noexcept
		{
			return m_Thread == GetCurrentThreadId();
		}

		bool isValidRange(LONG first, LONG last) const noexcept
		{
			return first <= last && boundary(m_Text, first) && boundary(m_Text, last);
		}

		HRESULT checkReadLock() const noexcept
		{
			if (!isOwnerThread())
			{
				return RPC_E_WRONG_THREAD;
			}

			if (m_bClosed)
			{
				return TF_E_DISCONNECTED;
			}

			return m_Lock & TS_LF_READ ? S_OK : TS_E_NOLOCK;
		}

		HRESULT checkWriteLock() const noexcept
		{
			const HRESULT result = checkReadLock();

			if (FAILED(result))
			{
				return result;
			}

			if (m_bReadOnly || !m_bEnabled)
			{
				return TS_E_READONLY;
			}

			return m_Lock == TS_LF_READWRITE ? S_OK : TS_E_NOLOCK;
		}

		void fillEventIdentity(WindowEvent& event) noexcept
		{
			event.Window = m_Id;
			event.Composition.Session = m_Session;
			event.Composition.ExpectedRevision = m_Revision;
			event.Composition.bExplicitRange = true;
			event.Composition.SelectionAnchorByte = bytePosition(m_Text, m_Selection.style.ase == TS_AE_START ? m_Selection.acpEnd : m_Selection.acpStart);
			event.Composition.SelectionCaretByte = bytePosition(m_Text, m_Selection.style.ase == TS_AE_START ? m_Selection.acpStart : m_Selection.acpEnd);
		}

		void emitEvent(const WindowEvent& event) noexcept
		{
			if (m_Callback && !m_bClosed)
			{
				m_Callback(m_Context, event);
			}
		}

		void emitDifference(EWindowEventKind kind, const Array<wchar_t>& before, bool bCommit) noexcept
		{
			LONG first = 0;
			const LONG oldSize = static_cast<LONG>(before.Size());
			const LONG newSize = static_cast<LONG>(m_Text.Size());

			while (first < Min(oldSize, newSize) && before[first] == m_Text[first])
			{
				++first;
			}
			while (first > 0 && (!boundary(before, first) || !boundary(m_Text, first)))
			{
				--first;
			}

			LONG oldEnd = oldSize;
			LONG newEnd = newSize;

			while (oldEnd > first && newEnd > first && before[oldEnd - 1] == m_Text[newEnd - 1])
			{
				--oldEnd;
				--newEnd;
			}
			while (!boundary(before, oldEnd) || !boundary(m_Text, newEnd))
			{
				++oldEnd;
				++newEnd;
			}

			if (first == oldEnd && first == newEnd && kind == EWindowEventKind::TextEdit)
			{
				return;
			}

			WindowEvent event;
			event.Kind = kind;
			fillEventIdentity(event);
			event.Composition.StartByte = bytePosition(before, first);
			event.Composition.EraseBytes = bytePosition(before, oldEnd) - event.Composition.StartByte;
			const int count = newEnd - first;
			const int bytes = count ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, m_Text.Data() + first, count, nullptr, 0, nullptr, nullptr) : 0;

			if (bytes > static_cast<int>(WindowCompositionEvent::MAX_BYTES) || (count && bytes == 0) || m_Revision == NumericLimits<uint64>::max())
			{
				event.Composition.ByteCount = WindowCompositionEvent::MAX_BYTES + 1;
				emitEvent(event);
				m_bEnabled = false;
				m_bFailed = true;

				return;
			}

			event.Composition.ByteCount = static_cast<uint32>(bytes);

			if (bytes)
			{
				WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, m_Text.Data() + first, count, event.Composition.Utf8, bytes, nullptr, nullptr);
			}

			event.Composition.CaretByte = bytePosition(m_Text, ClampValue<LONG>(m_Selection.acpEnd, first, newEnd)) - bytePosition(m_Text, first);
			emitEvent(event);

			if (bCommit && (first != oldEnd || first != newEnd))
			{
				++m_Revision;
			}
		}

		void endComposition() noexcept
		{
			if (m_bCancelling)
			{
				WindowEvent event;
				event.Kind = EWindowEventKind::CompositionCancel;
				fillEventIdentity(event);
				emitEvent(event);
				const bool bRestored = copy(m_CompositionBefore, m_Text);
				ASSERT(bRestored, "Composition rollback reuses its admitted capacity.");
				m_Selection = m_CompositionSelection;
				m_bCommittedInLock = true;
			}
			else
			{
				const bool bSame = m_CompositionBefore.Size() == m_Text.Size() && (m_Text.IsEmpty() || CompareBytes(m_CompositionBefore.Data(), m_Text.Data(), m_Text.Size() * sizeof(wchar_t)) == 0);

				if (bSame)
				{
					WindowEvent event;
					event.Kind = EWindowEventKind::CompositionCancel;
					fillEventIdentity(event);
					emitEvent(event);
				}
				else
				{
					emitDifference(EWindowEventKind::CompositionCommit, m_CompositionBefore, true);
				}

				m_bCommittedInLock = true;
			}

			m_bComposing = false;
			m_CompositionBefore.Clear();
		}

	private:
		MemoryResource& m_Memory;
		Array<wchar_t> m_Text;
		Array<wchar_t> m_LockBefore;
		Array<wchar_t> m_CompositionBefore;
		SharedPtr<const WindowTextGeometry> m_Geometry;

		HWND m_Window;
		WindowId m_Id;
		Emit m_Callback;
		void* m_Context;
		DWORD m_Thread;
		Atomic<ULONG> m_References{ 1 };

		ITfThreadMgr* m_Manager = nullptr;
		ITfDocumentMgr* m_Document = nullptr;
		ITfContext* m_NativeContext = nullptr;
		ITfKeystrokeMgr* m_Keys = nullptr;
		ITextStoreACPSink* m_Sink = nullptr;
		TfClientId m_Client = TF_CLIENTID_NULL;
		TfEditCookie m_EditCookie = 0;

		TS_SELECTION_ACP m_Selection{ 0, 0, {TS_AE_END, FALSE} };
		TS_SELECTION_ACP m_LockSelection{ 0, 0, {TS_AE_END, FALSE} };
		TS_SELECTION_ACP m_CompositionSelection{ 0, 0, {TS_AE_END, FALSE} };
		DWORD m_SinkMask = 0;
		DWORD m_Lock = 0;
		DWORD m_PendingLock = 0;
		uint64 m_Session = 0;
		uint64 m_Revision = 0;
		LONG m_CompositionFirst = 0;
		LONG m_CompositionLast = 0;
		bool m_bApartment = false;
		bool m_bActivated = false;
		bool m_bPushed = false;
		bool m_bClosed = false;

		bool m_bEnabled = false;
		bool m_bReadOnly = false;
		bool m_bPassword = false;
		bool m_bMultiline = false;
		bool m_bComposing = false;
		bool m_bCancelling = false;
		bool m_bCommittedInLock = false;
		bool m_bFailed = false;

	};

	WindowsTextInput::WindowsTextInput(MemoryResource& memory, HWND window, WindowId id, Emit emit, void* context) noexcept
		: m_Memory(memory)
		, m_Window(window)
		, m_Id(id)
		, m_Emit(emit)
		, m_Context(context)
	{
	}

	WindowsTextInput::~WindowsTextInput() noexcept
	{
		Close();

		if (m_State)
		{
			m_State->Release();
			m_State = nullptr;
		}
	}

	Result<void> WindowsTextInput::Initialize()
	{
		ASSERT(!m_State, "TSF owner initializes once.");
		m_State = CreateObject<State>(m_Memory, m_Memory, m_Window, m_Id, m_Emit, m_Context);

		if (!m_State)
		{
			return Result<void>::Failure(textError(E_OUTOFMEMORY, "TSF text store allocation failed."));
		}

		const HRESULT result = m_State->Initialize();

		if (FAILED(result))
		{
			Close();

			return Result<void>::Failure(textError(result, "TSF thread manager/context initialization failed."));
		}

		return Result<void>::Success();
	}

	Result<void> WindowsTextInput::Publish(const WindowTextDocument& document)
	{
		const HRESULT result = m_State ? m_State->Publish(document) : TF_E_DISCONNECTED;

		return SUCCEEDED(result) ? Result<void>::Success() : Result<void>::Failure(textError(result, "TSF document publication failed."));
	}

	Result<void> WindowsTextInput::SetState(const WindowTextInputState& state)
	{
		if (!m_State)
		{
			return Result<void>::Failure(textError(TF_E_DISCONNECTED, "TSF owner is disconnected."));
		}

		return m_State->SetInputState(state);
	}

	bool WindowsTextInput::FilterKey(const MSG& message) noexcept
	{
		return m_State && m_State->Filter(message);
	}

	void WindowsTextInput::Pump() noexcept
	{
		if (m_State)
		{
			m_State->Pump();
		}
	}

	void WindowsTextInput::Focus(bool bFocused) noexcept
	{
		if (m_State)
		{
			m_State->SetWindowFocus(bFocused);
		}
	}

	void WindowsTextInput::Close() noexcept
	{
		if (m_State)
		{
			m_State->Close();
		}
	}

}
