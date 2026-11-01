#include "WindowsAccessibility.h"

#include "../AccessibilityInternal.h"
#include <Algorithms.hpp>
#include <Comparison.hpp>
#include <Numeric.hpp>
#include <Synchronization.hpp>
#include <SystemMemoryResource.h>
#include <Types.h>
#include <Utf8.h>
#include <Utility.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef GetObject
#include <objbase.h>
#include <UIAutomation.h>

namespace rox::os
{
	namespace
	{
		class AccessibilityProvider;
	}

	struct WindowsAccessibilityState
	{
	public:
		rox::core::Mutex Mutex;
		AccessibilitySnapshot Snapshot;
		core::Array<core::SharedPtr<AccessibilityActionState>> Queue;
		core::Array<AccessibilityProvider*> Providers;

		HWND Window = nullptr;
		DWORD OwnerThread = 0;
		POINT Origin{};

		bool bClosed = false;
		bool bComInitialized = false;

	public:
		explicit WindowsAccessibilityState(HWND window) noexcept
			: Queue(core::SystemMemoryResource::Get())
			, Providers(core::SystemMemoryResource::Get())
			, Window(window)
			, OwnerThread(GetCurrentThreadId())
		{
		}
	};

	namespace
	{
		core::MemoryResource& getAccessibilityResource() noexcept
		{
			return core::SystemMemoryResource::Get();
		}

		HRESULT toNativeStatus(core::EErrorCode error) noexcept
		{
			switch (error)
			{
			case core::EErrorCode::None:
				return S_OK;
			case core::EErrorCode::PermissionDenied:
				return UIA_E_ELEMENTNOTENABLED;
			case core::EErrorCode::InvalidArgument:
				return E_INVALIDARG;
			case core::EErrorCode::Unsupported:
				return UIA_E_NOTSUPPORTED;
			case core::EErrorCode::ExternalFailure:
				return E_OUTOFMEMORY;
			case core::EErrorCode::Timeout:
				return UIA_E_TIMEOUT;
			default:
				return UIA_E_ELEMENTNOTAVAILABLE;
			}
		}

		HRESULT createBstr(core::StringView text, BSTR* output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			const int count =
				text.IsEmpty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Size()), nullptr, 0);

			if (!text.IsEmpty() && !count)
			{
				return E_INVALIDARG;
			}

			*output = SysAllocStringLen(nullptr, static_cast<UINT>(count));

			if (!*output)
			{
				return E_OUTOFMEMORY;
			}

			if (count)
			{
				MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.Data(), static_cast<int>(text.Size()), *output, count);
			}

			return S_OK;
		}

		core::uint32 getAdjacentByteOffset(core::StringView text, core::uint32 byte, bool bForward) noexcept
		{
			if (bForward)
			{
				if (byte < text.Size())
				{
					++byte;

					while (byte < text.Size() && (static_cast<unsigned char>(text[byte]) & 0xc0) == 0x80)
					{
						++byte;
					}
				}
			}
			else if (byte)
			{
				--byte;

				while (byte && (static_cast<unsigned char>(text[byte]) & 0xc0) == 0x80)
				{
					--byte;
				}
			}

			return byte;
		}

		HRESULT submitAction(const core::SharedPtr<WindowsAccessibilityState>& state, AccessibilityId id, EAccessibilityAction kind, core::StringView value = {}, core::uint32 first = 0, core::uint32 last = 0, core::uint64 contentRevision = 0)
		{
			core::SharedPtr<AccessibilityActionState> request;

			if (!core::SharedPtr<AccessibilityActionState>::CreateInto(getAccessibilityResource(), &request, getAccessibilityResource()) || !request->Action.Value.Assign(value))
			{
				return E_OUTOFMEMORY;
			}

			{
				rox::core::LockGuard lock(state->Mutex);
				const AccessibilityNode* node = state->Snapshot.Find(id);

				if (state->bClosed || !node || (contentRevision && node->ContentRevision != contentRevision))
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				if (!node->bEnabled)
				{
					return UIA_E_ELEMENTNOTENABLED;
				}

				if ((kind == EAccessibilityAction::SetValue && node->bReadOnly) ||
					(node->bPassword && (kind == EAccessibilityAction::SetValue || kind == EAccessibilityAction::SetSelection ||
						kind == EAccessibilityAction::ScrollText)))
				{
					return E_ACCESSDENIED;
				}

				if (GetCurrentThreadId() == state->OwnerThread && kind != EAccessibilityAction::Invoke)
				{
					return UIA_E_INVALIDOPERATION;
				}

				if (state->Queue.Size() >= 64)
				{
					return UIA_E_TIMEOUT;
				}

				if (kind == EAccessibilityAction::Focus)
				{
					request->SuccessContext = state->Window;
					request->OnSuccess = [](void* window) noexcept
					{
						::SetFocus(static_cast<HWND>(window));
					};
				}

				request->Action.Target = id;
				request->Action.Publication = state->Snapshot.GetPublication();
				request->Action.Kind = kind;
				request->Action.SelectionStart = first;
				request->Action.SelectionEnd = last;

				if (!state->Queue.Add(request))
				{
					return E_OUTOFMEMORY;
				}

				if (kind == EAccessibilityAction::Invoke)
				{
					return S_OK;
				}
			}

			rox::core::UniqueLock lock(request->Mutex);

			while (!request->bComplete)
			{
				if (request->bStarted)
				{
					request->Changed.wait(lock);
				}
				else if (request->Changed.wait_until(lock, request->Deadline) == rox::core::EWaitStatus::timeout && !request->bStarted)
				{
					request->bComplete = true;
					request->Error = core::EErrorCode::Timeout;
				}
			}

			return toNativeStatus(request->Error);
		}

		class AccessibilityRange;
		class AccessibilityProvider final :
			public IRawElementProviderSimple,
			public IRawElementProviderFragment,
			public IRawElementProviderFragmentRoot,
			public IInvokeProvider,
			public IToggleProvider,
			public IValueProvider,
			public ITextProvider,
			public IScrollProvider,
			public ISelectionProvider,
			public ISelectionItemProvider,
			public IExpandCollapseProvider,
			public IVirtualizedItemProvider
		{
		public:
			core::SharedPtr<WindowsAccessibilityState> State;
			AccessibilityId Id;
			rox::core::Atomic<ULONG> References{ 1 };

		public:
			AccessibilityProvider(core::SharedPtr<WindowsAccessibilityState> state, AccessibilityId id) noexcept
				: State(rox::core::Move(state))
				, Id(id)
			{
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = nullptr;

				if (id == __uuidof(IUnknown))
				{
					*output = static_cast<IRawElementProviderSimple*>(this);
				}
				else if (id == __uuidof(IRawElementProviderSimple))
				{
					*output = static_cast<IRawElementProviderSimple*>(this);
				}
				else if (id == __uuidof(IRawElementProviderFragment))
				{
					*output = static_cast<IRawElementProviderFragment*>(this);
				}
				else if (id == __uuidof(IRawElementProviderFragmentRoot))
				{
					*output = static_cast<IRawElementProviderFragmentRoot*>(this);
				}
				else if (id == __uuidof(IInvokeProvider))
				{
					*output = static_cast<IInvokeProvider*>(this);
				}
				else if (id == __uuidof(IToggleProvider))
				{
					*output = static_cast<IToggleProvider*>(this);
				}
				else if (id == __uuidof(IValueProvider))
				{
					*output = static_cast<IValueProvider*>(this);
				}
				else if (id == __uuidof(ITextProvider))
				{
					*output = static_cast<ITextProvider*>(this);
				}
				else if (id == __uuidof(IScrollProvider))
				{
					*output = static_cast<IScrollProvider*>(this);
				}
				else if (id == __uuidof(ISelectionProvider))
				{
					*output = static_cast<ISelectionProvider*>(this);
				}
				else if (id == __uuidof(ISelectionItemProvider))
				{
					*output = static_cast<ISelectionItemProvider*>(this);
				}
				else if (id == __uuidof(IExpandCollapseProvider))
				{
					*output = static_cast<IExpandCollapseProvider*>(this);
				}
				else if (id == __uuidof(IVirtualizedItemProvider))
				{
					*output = static_cast<IVirtualizedItemProvider*>(this);
				}

				if (!*output)
				{
					return E_NOINTERFACE;
				}

				AddRef();

				return S_OK;
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return ++References;
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG remaining = --References;

				if (!remaining)
				{
					core::DestroyObject(getAccessibilityResource(), this);
				}

				return remaining;
			}

			HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_ProviderOwnsSetFocus);

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** output) override;
			HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* output) override;
			HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = nullptr;
				rox::core::LockGuard lock(State->Mutex);

				if (State->bClosed)
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				return Id.IsValid() ? S_OK : UiaHostProviderFromHwnd(State->Window, output);
			}

			HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** output) override;
			HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** output) override;
			HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* output) override;
			HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = nullptr;

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE SetFocus() override
			{
				return submitAction(State, Id, EAccessibilityAction::Focus);
			}

			HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** output) override;
			HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** output) override;
			HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** output) override;
			HRESULT STDMETHODCALLTYPE Invoke() override
			{
				return submitAction(State, Id, EAccessibilityAction::Invoke);
			}

			HRESULT STDMETHODCALLTYPE Toggle() override
			{
				return submitAction(State, Id, EAccessibilityAction::Toggle);
			}

			HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* output) override;
			HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override;
			HRESULT STDMETHODCALLTYPE get_Value(BSTR* output) override;
			HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* output) override;
			HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** output) override;
			HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** output) override;
			HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple*, ITextRangeProvider** output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = nullptr;

				return E_INVALIDARG;
			}

			HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint point, ITextRangeProvider** output) override;
			HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider** output) override;
			HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = SupportedTextSelection_Single;

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = FALSE;

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = FALSE;

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Select() override
			{
				return submitAction(State, Id, EAccessibilityAction::Select);
			}

			HRESULT STDMETHODCALLTYPE AddToSelection() override
			{
				return Select();
			}

			HRESULT STDMETHODCALLTYPE RemoveFromSelection() override
			{
				return UIA_E_INVALIDOPERATION;
			}

			HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* output) override;
			HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** output) override;
			HRESULT STDMETHODCALLTYPE Expand() override
			{
				return submitAction(State, Id, EAccessibilityAction::Expand);
			}

			HRESULT STDMETHODCALLTYPE Collapse() override
			{
				return submitAction(State, Id, EAccessibilityAction::Collapse);
			}

			HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* output) override;
			HRESULT STDMETHODCALLTYPE Realize() override
			{
				return submitAction(State, Id, EAccessibilityAction::Realize);
			}

			HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override
			{
				if (horizontal != ScrollAmount_NoAmount || vertical < ScrollAmount_LargeDecrement || vertical > ScrollAmount_SmallIncrement)
				{
					return E_INVALIDARG;
				}

				double percentage;
				{
					rox::core::LockGuard lock(State->Mutex);
					const AccessibilityNode* node = State->bClosed ? nullptr : State->Snapshot.Find(Id);

					if (!node)
					{
						return UIA_E_ELEMENTNOTAVAILABLE;
					}

					const double step = rox::core::Max(
						0.001, node->VerticalViewSize *
						((vertical == ScrollAmount_LargeDecrement || vertical == ScrollAmount_LargeIncrement) ? 0.9 : 0.1));
					percentage = rox::core::ClampValue(
						node->VerticalScrollPercent + (vertical == ScrollAmount_NoAmount ? 0
							: (vertical == ScrollAmount_LargeDecrement || vertical == ScrollAmount_SmallDecrement)
							? -step
							: step),
						0.0, 100.0);
				}

				return SetScrollPercent(UIA_ScrollPatternNoScroll, percentage);
			}

			HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override
			{
				if (horizontal != UIA_ScrollPatternNoScroll || !rox::core::IsFinite(vertical) || (vertical != UIA_ScrollPatternNoScroll && (vertical < 0 || vertical > 100)))
				{
					return E_INVALIDARG;
				}

				if (vertical == UIA_ScrollPatternNoScroll)
				{
					return S_OK;
				}

				return submitAction(State, Id, EAccessibilityAction::ScrollTo, {}, static_cast<core::uint32>(vertical * 10000));
			}

			HRESULT scrollProperty(double* output, bool bSize)
			{
				if (!output)
				{
					return E_POINTER;
				}

				rox::core::LockGuard lock(State->Mutex);
				const AccessibilityNode* node = State->bClosed ? nullptr : State->Snapshot.Find(Id);

				if (!node)
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				*output = bSize ? node->VerticalViewSize
					: node->VerticalViewSize >= 100 ? UIA_ScrollPatternNoScroll
					: node->VerticalScrollPercent;

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = UIA_ScrollPatternNoScroll;

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* output) override
			{
				return scrollProperty(output, false);
			}

			HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = 100;

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* output) override
			{
				return scrollProperty(output, true);
			}

			HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = FALSE;

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				double size;
				const HRESULT result = scrollProperty(&size, true);

				if (SUCCEEDED(result))
				{
					*output = size < 100;
				}

				return result;
			}

		};

		AccessibilityProvider* findOrCreateProvider(const core::SharedPtr<WindowsAccessibilityState>& state, AccessibilityId id)
		{
			if (state->bClosed || (id.IsValid() && !state->Snapshot.Find(id)))
			{
				return nullptr;
			}

			for (AccessibilityProvider* existing : state->Providers)
			{
				if (existing->Id == id)
				{
					existing->AddRef();

					return existing;
				}
			}

			if (state->Providers.Size() >= 65537)
			{
				return nullptr;
			}

			AccessibilityProvider* created = core::CreateObject<AccessibilityProvider>(getAccessibilityResource(), state, id);

			if (!created)
			{
				return nullptr;
			}

			if (!state->Providers.Add(created))
			{
				created->Release();

				return nullptr;
			}

			created->AddRef();

			return created;
		}

		HRESULT AccessibilityProvider::GetPatternProvider(PATTERNID pattern, IUnknown** output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);

			if (State->bClosed)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			const AccessibilityNode* node = State->Snapshot.Find(Id);

			if (!node)
			{
				return Id.IsValid() ? UIA_E_ELEMENTNOTAVAILABLE : S_OK;
			}

			if (pattern == UIA_InvokePatternId && node->Role == EAccessibilityRole::Button)
			{
				*output = static_cast<IInvokeProvider*>(this);
			}
			else if (pattern == UIA_TogglePatternId && node->Role == EAccessibilityRole::CheckBox)
			{
				*output = static_cast<IToggleProvider*>(this);
			}
			else if (pattern == UIA_ValuePatternId && node->Role == EAccessibilityRole::Edit && !node->bPassword)
			{
				*output = static_cast<IValueProvider*>(this);
			}
			else if (pattern == UIA_TextPatternId && node->Role == EAccessibilityRole::Edit && !node->bPassword)
			{
				*output = static_cast<ITextProvider*>(this);
			}
			else if (pattern == UIA_ScrollPatternId && node->Role == EAccessibilityRole::Tree)
			{
				*output = static_cast<IScrollProvider*>(this);
			}
			else if (pattern == UIA_SelectionPatternId && node->Role == EAccessibilityRole::Tree)
			{
				*output = static_cast<ISelectionProvider*>(this);
			}
			else if (pattern == UIA_SelectionItemPatternId && node->bSelectable)
			{
				*output = static_cast<ISelectionItemProvider*>(this);
			}
			else if (pattern == UIA_ExpandCollapsePatternId && node->bExpandable)
			{
				*output = static_cast<IExpandCollapseProvider*>(this);
			}
			else if (pattern == UIA_VirtualizedItemPatternId && node->Role == EAccessibilityRole::TreeItem)
			{
				*output = static_cast<IVirtualizedItemProvider*>(this);
			}

			if (*output)
			{
				AddRef();
			}

			return S_OK;
		}

		HRESULT AccessibilityProvider::GetPropertyValue(PROPERTYID property, VARIANT* output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			VariantInit(output);
			rox::core::LockGuard lock(State->Mutex);

			if (State->bClosed)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			const AccessibilityNode* node = State->Snapshot.Find(Id);

			if (!node && Id.IsValid())
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			if (property == UIA_ControlTypePropertyId)
			{
				static constexpr int types[]{ UIA_GroupControlTypeId, UIA_TextControlTypeId, UIA_ButtonControlTypeId,  UIA_CheckBoxControlTypeId, UIA_EditControlTypeId,  UIA_TreeControlTypeId, UIA_TreeItemControlTypeId };
				output->vt = VT_I4;
				output->lVal = node ? types[static_cast<unsigned>(node->Role)] : UIA_WindowControlTypeId;

				return S_OK;
			}

			if (property == UIA_NamePropertyId || property == UIA_FrameworkIdPropertyId)
			{
				output->vt = VT_BSTR;

				return createBstr(property == UIA_FrameworkIdPropertyId ? core::StringView("Rui")
					: node ? node->Name.View()
					: core::StringView("Rui window"),
					&output->bstrVal);
			}

			bool bValue = false;

			switch (property)
			{
			case UIA_IsEnabledPropertyId:
				bValue = !node || node->bEnabled;
				break;
			case UIA_IsKeyboardFocusablePropertyId:
				bValue = node && node->bFocusable;
				break;
			case UIA_HasKeyboardFocusPropertyId:
				bValue = node && node->bFocused;
				break;
			case UIA_IsPasswordPropertyId:
				bValue = node && node->bPassword;
				break;
			case UIA_IsOffscreenPropertyId:
				bValue = node && node->bOffscreen;
				break;
			case UIA_IsControlElementPropertyId:
			case UIA_IsContentElementPropertyId:
				bValue = true;
				break;
			default:
				return S_OK;
			}

			output->vt = VT_BOOL;
			output->boolVal = bValue ? VARIANT_TRUE : VARIANT_FALSE;

			return S_OK;
		}

		HRESULT AccessibilityProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);

			if (State->bClosed)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			const AccessibilityNode* node = State->Snapshot.Find(Id);

			if (!node && Id.IsValid())
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			AccessibilityId destination;
			bool bFound = false;

			if (direction == NavigateDirection_Parent && node)
			{
				destination = node->Parent;
				bFound = true;
			}
			else if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)
			{
				for (const AccessibilityNode& child : State->Snapshot.GetNodes())
				{
					if (child.Parent == Id)
					{
						destination = child.Id;
						bFound = true;

						if (direction == NavigateDirection_FirstChild)
						{
							break;
						}
					}
				}
			}
			else if (node && (direction == NavigateDirection_NextSibling || direction == NavigateDirection_PreviousSibling))
			{
				bool bPassed = false;
				AccessibilityId previous;

				for (const AccessibilityNode& sibling : State->Snapshot.GetNodes())
				{
					if (sibling.Parent != node->Parent)
					{
						continue;
					}

					if (sibling.Id == Id)
					{
						if (direction == NavigateDirection_PreviousSibling)
						{
							destination = previous;
							bFound = previous.IsValid();
							break;
						}

						bPassed = true;
					}
					else if (bPassed)
					{
						destination = sibling.Id;
						bFound = true;
						break;
					}

					previous = sibling.Id;
				}
			}

			if (bFound)
			{
				AccessibilityProvider* next = findOrCreateProvider(State, destination);

				if (!next)
				{
					return E_OUTOFMEMORY;
				}

				*output = static_cast<IRawElementProviderFragment*>(next);
			}

			return S_OK;
		}

		HRESULT AccessibilityProvider::GetRuntimeId(SAFEARRAY** output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);

			if (State->bClosed || (Id.IsValid() && !State->Snapshot.Find(Id)))
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			if (!Id.IsValid())
			{
				return S_OK;
			}

			int values[9]{ UiaAppendRuntimeId };
			const core::uint64 fields[]{ Id.Owner, Id.Slot, Id.Generation, Id.Item };

			for (unsigned index = 0; index < 4; ++index)
			{
				values[1 + index * 2] = static_cast<int>(fields[index]);
				values[2 + index * 2] = static_cast<int>(fields[index] >> 32);
			}

			*output = SafeArrayCreateVector(VT_I4, 0, 9);

			if (!*output)
			{
				return E_OUTOFMEMORY;
			}

			for (LONG index = 0; index < 9; ++index)
			{
				SafeArrayPutElement(*output, &index, &values[index]);
			}

			return S_OK;
		}

		HRESULT AccessibilityProvider::get_BoundingRectangle(UiaRect* output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = {};
			rox::core::LockGuard lock(State->Mutex);

			if (State->bClosed)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			const AccessibilityNode* node = State->Snapshot.Find(Id);

			if (!node)
			{
				if (Id.IsValid())
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				RECT rect{};
				GetWindowRect(State->Window, &rect);
				*output = { static_cast<double>(rect.left), static_cast<double>(rect.top), static_cast<double>(rect.right - rect.left), static_cast<double>(rect.bottom - rect.top) };
			}
			else
			{
				*output = { node->Bounds.X + State->Origin.x, node->Bounds.Y + State->Origin.y, node->Bounds.Width, node->Bounds.Height };
			}

			return S_OK;
		}

		HRESULT AccessibilityProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);

			if (State->bClosed)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			AccessibilityProvider* root = findOrCreateProvider(State, {});

			if (!root)
			{
				return E_OUTOFMEMORY;
			}

			*output = static_cast<IRawElementProviderFragmentRoot*>(root);

			return S_OK;
		}

		HRESULT AccessibilityProvider::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);

			if (State->bClosed)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			AccessibilityId selected;

			for (const AccessibilityNode& node : State->Snapshot.GetNodes())
			{
				const AccessibilityRect& rect = node.Bounds;

				if (!node.bOffscreen && x >= rect.X + State->Origin.x && y >= rect.Y + State->Origin.y && x < rect.X + State->Origin.x + rect.Width && y < rect.Y + State->Origin.y + rect.Height)
				{
					selected = node.Id;
				}
			}

			AccessibilityProvider* result = findOrCreateProvider(State, selected);

			if (!result)
			{
				return E_OUTOFMEMORY;
			}

			*output = static_cast<IRawElementProviderFragment*>(result);

			return S_OK;
		}

		HRESULT AccessibilityProvider::GetFocus(IRawElementProviderFragment** output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);

			if (State->bClosed)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			for (const AccessibilityNode& node : State->Snapshot.GetNodes())
			{
				if (node.bFocused)
				{
					AccessibilityProvider* result = findOrCreateProvider(State, node.Id);

					if (!result)
					{
						return E_OUTOFMEMORY;
					}

					*output = static_cast<IRawElementProviderFragment*>(result);
					break;
				}
			}

			return S_OK;
		}

		HRESULT AccessibilityProvider::get_ToggleState(ToggleState* output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			rox::core::LockGuard lock(State->Mutex);
			const AccessibilityNode* node = State->bClosed ? nullptr : State->Snapshot.Find(Id);

			if (!node)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			*output = static_cast<ToggleState>(node->ToggleState);

			return S_OK;
		}

		HRESULT AccessibilityProvider::SetValue(LPCWSTR value)
		{
			if (!value)
			{
				return E_INVALIDARG;
			}

			const size_t length = wcsnlen(value, 1024 * 1024 + 1);

			if (length > 1024 * 1024)
			{
				return E_INVALIDARG;
			}

			const int count = length ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, static_cast<int>(length), nullptr, 0, nullptr, nullptr) : 0;

			if (count > 1024 * 1024 || (length && !count))
			{
				return E_INVALIDARG;
			}

			core::Array<char> utf8(getAccessibilityResource());

			if (!utf8.ResizeUninitialized(static_cast<rox::core::usize>(count)))
			{
				return E_OUTOFMEMORY;
			}

			if (count)
			{
				WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, static_cast<int>(length), utf8.Data(), count, nullptr, nullptr);
			}

			return submitAction(State, Id, EAccessibilityAction::SetValue, { utf8.Data(), utf8.Size() });
		}

		HRESULT AccessibilityProvider::get_Value(BSTR* output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);
			const AccessibilityNode* node = State->bClosed ? nullptr : State->Snapshot.Find(Id);

			if (!node)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			if (node->bPassword)
			{
				return E_ACCESSDENIED;
			}

			return createBstr(node->Value.View(), output);
		}

		HRESULT AccessibilityProvider::get_IsReadOnly(BOOL* output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			rox::core::LockGuard lock(State->Mutex);
			const AccessibilityNode* node = State->bClosed ? nullptr : State->Snapshot.Find(Id);

			if (!node)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			*output = node->bReadOnly || node->bPassword;

			return S_OK;
		}

		HRESULT AccessibilityProvider::get_IsSelected(BOOL* output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			rox::core::LockGuard lock(State->Mutex);
			const AccessibilityNode* node = State->bClosed ? nullptr : State->Snapshot.Find(Id);

			if (!node)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			*output = node->bSelected;

			return S_OK;
		}

		HRESULT AccessibilityProvider::get_SelectionContainer(IRawElementProviderSimple** output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);

			if (State->bClosed)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			AccessibilityId owner = Id;
			owner.Item = 0;
			AccessibilityProvider* result = findOrCreateProvider(State, owner);

			if (!result)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			*output = static_cast<IRawElementProviderSimple*>(result);

			return S_OK;
		}

		HRESULT AccessibilityProvider::get_ExpandCollapseState(ExpandCollapseState* output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			rox::core::LockGuard lock(State->Mutex);
			const AccessibilityNode* node = State->bClosed ? nullptr : State->Snapshot.Find(Id);

			if (!node)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			*output = !node->bExpandable ? ExpandCollapseState_LeafNode
				: node->bExpanded ? ExpandCollapseState_Expanded
				: ExpandCollapseState_Collapsed;

			return S_OK;
		}

	}

	namespace
	{
		class AccessibilityRange final : public ITextRangeProvider
		{
		public:
			core::SharedPtr<WindowsAccessibilityState> State;
			AccessibilityId Id;
			core::uint64 Revision;
			core::uint32 First;
			core::uint32 Last;
			rox::core::Atomic<ULONG> References{ 1 };

		public:
			AccessibilityRange(core::SharedPtr<WindowsAccessibilityState> state, AccessibilityId id, core::uint64 revision, core::uint32 first,
				core::uint32 last) noexcept
				: State(rox::core::Move(state))
				, Id(id)
				, Revision(revision)
				, First(first)
				, Last(last)
			{
			}

			const AccessibilityNode* node() const noexcept
			{
				const AccessibilityNode* current = State->bClosed ? nullptr : State->Snapshot.Find(Id);

				return current && !current->bPassword && current->ContentRevision == Revision && Last <= current->Value.Size() ? current : nullptr;
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = nullptr;

				if (id != __uuidof(IUnknown) && id != __uuidof(ITextRangeProvider))
				{
					return E_NOINTERFACE;
				}

				*output = static_cast<ITextRangeProvider*>(this);
				AddRef();

				return S_OK;
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return ++References;
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG remaining = --References;

				if (!remaining)
				{
					core::DestroyObject(getAccessibilityResource(), this);
				}

				return remaining;
			}

			HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = nullptr;
				rox::core::LockGuard lock(State->Mutex);

				if (!node())
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				*output = core::CreateObject<AccessibilityRange>(getAccessibilityResource(), State, Id, Revision, First, Last);

				return *output ? S_OK : E_OUTOFMEMORY;
			}

			HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* other, BOOL* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = FALSE;
				AccessibilityRange* range = dynamic_cast<AccessibilityRange*>(other);

				if (!range || range->State.Get() != State.Get())
				{
					return S_OK;
				}

				rox::core::LockGuard lock(State->Mutex);

				if (!node() || !range->node())
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				*output = range->Id == Id && range->First == First && range->Last == Last;

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider* other, TextPatternRangeEndpoint target, int* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = 0;
				AccessibilityRange* range = dynamic_cast<AccessibilityRange*>(other);

				if (!range || range->State.Get() != State.Get() || range->Id != Id)
				{
					return E_INVALIDARG;
				}

				rox::core::LockGuard lock(State->Mutex);

				if (!node() || !range->node())
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				*output = static_cast<int>(endpoint == TextPatternRangeEndpoint_Start ? First : Last) - static_cast<int>(target == TextPatternRangeEndpoint_Start ? range->First : range->Last);
				return S_OK;
			}

			core::uint32 boundary(const AccessibilityNode& current, core::uint32 byte, TextUnit unit, bool bForward) const noexcept
			{
				const core::StringView text = current.Value.View();

				if (unit == TextUnit_Character)
				{
					return current.Geometry ? current.Geometry->NextBoundary(byte, bForward) : getAdjacentByteOffset(text, byte, bForward);
				}

				if (unit == TextUnit_Line || unit == TextUnit_Paragraph)
				{
					if (bForward)
					{
						while (byte < text.Size() && text[byte++] != '\n')
						{
						}
					}
					else if (byte)
					{
						--byte;

						while (byte && text[byte - 1] != '\n')
						{
							--byte;
						}
					}

					return byte;
				}

				// Unsupported format/word/page units promote to the supported document unit.
				return bForward ? static_cast<core::uint32>(text.Size()) : 0;
			}

			HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override
			{
				rox::core::LockGuard lock(State->Mutex);
				const AccessibilityNode* current = node();

				if (!current)
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				if (unit == TextUnit_Character)
				{
					if (First == current->Value.Size() && First)
					{
						First = boundary(*current, First, unit, false);
					}

					Last = boundary(*current, First, unit, true);
				}
				else if (unit == TextUnit_Line || unit == TextUnit_Paragraph)
				{
					while (First && current->Value[First - 1] != '\n')
					{
						--First;
					}

					Last = boundary(*current, First, unit, true);
				}
				else
				{
					First = 0;
					Last = static_cast<core::uint32>(current->Value.Size());
				}

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID, VARIANT, BOOL, ITextRangeProvider** output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = nullptr;

				return UIA_E_NOTSUPPORTED;
			}

			HRESULT STDMETHODCALLTYPE FindText(BSTR text, BOOL backward, BOOL ignoreCase, ITextRangeProvider** output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = nullptr;

				if (!text || !SysStringLen(text))
				{
					return E_INVALIDARG;
				}

				rox::core::LockGuard lock(State->Mutex);
				const AccessibilityNode* current = node();

				if (!current)
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				BSTR source = nullptr;
				HRESULT converted = createBstr(current->Value.View().Substr(First, Last - First), &source);

				if (FAILED(converted))
				{
					return converted;
				}

				const int length = static_cast<int>(SysStringLen(source));
				const int needle = static_cast<int>(SysStringLen(text));
				const int found = FindStringOrdinal(backward ? FIND_FROMEND : FIND_FROMSTART, source, length, text, needle, ignoreCase);

				if (found >= 0)
				{
					const int prefix = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, found, nullptr, 0, nullptr, nullptr);
					const int matched =
						WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source + found, needle, nullptr, 0, nullptr, nullptr);

					if (matched > 0)
					{
						*output = core::CreateObject<AccessibilityRange>(getAccessibilityResource(), State, Id, Revision, First + static_cast<core::uint32>(prefix), First + static_cast<core::uint32>(prefix + matched));
					}
				}

				SysFreeString(source);

				return found >= 0 && !*output ? E_OUTOFMEMORY : S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID attribute, VARIANT* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				VariantInit(output);
				rox::core::LockGuard lock(State->Mutex);
				const AccessibilityNode* current = node();

				if (!current)
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				if (attribute == UIA_IsReadOnlyAttributeId)
				{
					output->vt = VT_BOOL;
					output->boolVal = current->bReadOnly ? VARIANT_TRUE : VARIANT_FALSE;

					return S_OK;
				}

				output->vt = VT_UNKNOWN;

				return UiaGetReservedNotSupportedValue(&output->punkVal);
			}

			HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = nullptr;
				rox::core::LockGuard lock(State->Mutex);
				const AccessibilityNode* current = node();

				if (!current)
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				if (!current->Geometry)
				{
					*output = SafeArrayCreateVector(VT_R8, 0, 0);

					return *output ? S_OK : E_OUTOFMEMORY;
				}

				core::Result<core::Array<AccessibilityRect>> rectangles = current->Geometry->GetBounds(First, Last, getAccessibilityResource());

				if (!rectangles)
				{
					return toNativeStatus(rectangles.GetError().Code);
				}

				if (rectangles.Value().Size() > 65536)
				{
					return E_OUTOFMEMORY;
				}

				*output = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(rectangles.Value().Size() * 4));

				if (!*output)
				{
					return E_OUTOFMEMORY;
				}

				LONG index = 0;

				for (const AccessibilityRect& rect : rectangles.Value())
				{
					double values[]{ rect.X + State->Origin.x, rect.Y + State->Origin.y, rect.Width, rect.Height };

					for (core::float64 value : values)
					{
						SafeArrayPutElement(*output, &index, &value);
						++index;
					}
				}

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple** output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = nullptr;
				rox::core::LockGuard lock(State->Mutex);

				if (!node())
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				AccessibilityProvider* result = findOrCreateProvider(State, Id);

				if (!result)
				{
					return E_OUTOFMEMORY;
				}

				*output = static_cast<IRawElementProviderSimple*>(result);

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetText(int maximum, BSTR* output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				*output = nullptr;

				if (maximum < -1)
				{
					return E_INVALIDARG;
				}

				rox::core::LockGuard lock(State->Mutex);
				const AccessibilityNode* current = node();

				if (!current)
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				HRESULT converted = createBstr(current->Value.View().Substr(First, Last - First), output);

				if (FAILED(converted))
				{
					return converted;
				}

				if (maximum >= 0 && static_cast<UINT>(maximum) < SysStringLen(*output))
				{
					if (maximum && (*output)[maximum - 1] >= 0xd800 && (*output)[maximum - 1] <= 0xdbff)
					{
						--maximum;
					}

					BSTR limited = SysAllocStringLen(*output, static_cast<UINT>(maximum));
					SysFreeString(*output);
					*output = limited;

					if (!limited)
					{
						return E_OUTOFMEMORY;
					}
				}

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int* moved) override
			{
				if (!moved)
				{
					return E_POINTER;
				}

				*moved = 0;

				if (unit < TextUnit_Character || unit > TextUnit_Document || count < -1048576 || count > 1048576)
				{
					return E_INVALIDARG;
				}

				rox::core::LockGuard lock(State->Mutex);
				const AccessibilityNode* current = node();

				if (!current)
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				const bool bNonempty = First != Last;
				const bool bForward = count > 0;

				while (*moved != count)
				{
					const core::uint32 next = boundary(*current, First, unit, bForward);

					if (next == First || (bNonempty && next == current->Value.Size()))
					{
						break;
					}

					First = next;
					*moved += bForward ? 1 : -1;
				}

				Last = bNonempty ? boundary(*current, First, unit, true) : First;

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count, int* moved) override
			{
				if (!moved)
				{
					return E_POINTER;
				}

				*moved = 0;

				if (unit < TextUnit_Character || unit > TextUnit_Document || count < -1048576 || count > 1048576)
				{
					return E_INVALIDARG;
				}

				if (endpoint != TextPatternRangeEndpoint_Start && endpoint != TextPatternRangeEndpoint_End)
				{
					return E_INVALIDARG;
				}

				rox::core::LockGuard lock(State->Mutex);
				const AccessibilityNode* current = node();

				if (!current)
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				core::uint32& selected = endpoint == TextPatternRangeEndpoint_Start ? First : Last;

				while (*moved != count)
				{
					const core::uint32 next = boundary(*current, selected, unit, count > 0);

					if (next == selected)
					{
						break;
					}

					selected = next;
					*moved += count > 0 ? 1 : -1;
				}

				if (First > Last)
				{
					if (endpoint == TextPatternRangeEndpoint_Start)
					{
						Last = First;
					}
					else
					{
						First = Last;
					}
				}

				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider* other, TextPatternRangeEndpoint target) override
			{
				if ((endpoint != TextPatternRangeEndpoint_Start && endpoint != TextPatternRangeEndpoint_End) || (target != TextPatternRangeEndpoint_Start && target != TextPatternRangeEndpoint_End))
				{
					return E_INVALIDARG;
				}

				AccessibilityRange* range = dynamic_cast<AccessibilityRange*>(other);

				if (!range || range->State.Get() != State.Get() || range->Id != Id)
				{
					return E_INVALIDARG;
				}

				rox::core::LockGuard lock(State->Mutex);

				if (!node() || !range->node())
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				const core::uint32 value = target == TextPatternRangeEndpoint_Start ? range->First : range->Last;

				if (endpoint == TextPatternRangeEndpoint_Start)
				{
					First = value;

					if (Last < First)
					{
						Last = First;
					}
				}
				else
				{
					Last = value;

					if (First > Last)
					{
						First = Last;
					}
				}

				return S_OK;
			}

			HRESULT action(EAccessibilityAction kind)
			{
				core::uint32 first;
				core::uint32 last;
				{
					rox::core::LockGuard lock(State->Mutex);

					if (!node())
					{
						return UIA_E_ELEMENTNOTAVAILABLE;
					}

					first = First;
					last = Last;
				}

				return submitAction(State, Id, kind, {}, first, last, Revision);
			}

			HRESULT STDMETHODCALLTYPE Select() override
			{
				return action(EAccessibilityAction::SetSelection);
			}

			HRESULT STDMETHODCALLTYPE AddToSelection() override
			{
				return UIA_E_INVALIDOPERATION;
			}

			HRESULT STDMETHODCALLTYPE RemoveFromSelection() override
			{
				return UIA_E_INVALIDOPERATION;
			}

			HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL) override
			{
				return action(EAccessibilityAction::ScrollText);
			}

			HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** output) override
			{
				if (!output)
				{
					return E_POINTER;
				}

				rox::core::LockGuard lock(State->Mutex);

				if (!node())
				{
					return UIA_E_ELEMENTNOTAVAILABLE;
				}

				*output = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);

				return *output ? S_OK : E_OUTOFMEMORY;
			}

		};
		HRESULT createRangeArray(IUnknown* object, SAFEARRAY** output)
		{
			*output = SafeArrayCreateVector(VT_UNKNOWN, 0, object ? 1 : 0);

			if (!*output)
			{
				if (object)
				{
					object->Release();
				}

				return E_OUTOFMEMORY;
			}

			if (object)
			{
				LONG index = 0;
				const HRESULT result = SafeArrayPutElement(*output, &index, object);
				object->Release();

				if (FAILED(result))
				{
					SafeArrayDestroy(*output);
					*output = nullptr;

					return result;
				}
			}

			return S_OK;
		}

		HRESULT AccessibilityProvider::get_DocumentRange(ITextRangeProvider** output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);
			const AccessibilityNode* node = State->bClosed ? nullptr : State->Snapshot.Find(Id);

			if (!node)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			if (node->bPassword)
			{
				return E_ACCESSDENIED;
			}

			*output = core::CreateObject<AccessibilityRange>(getAccessibilityResource(), State, Id, node->ContentRevision, 0u, static_cast<core::uint32>(node->Value.Size()));
			return *output ? S_OK : E_OUTOFMEMORY;
		}

		HRESULT AccessibilityProvider::GetSelection(SAFEARRAY** output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);
			const AccessibilityNode* node = State->bClosed ? nullptr : State->Snapshot.Find(Id);

			if (!node)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			if (node->bPassword)
			{
				return E_ACCESSDENIED;
			}

			if (node->Role == EAccessibilityRole::Edit)
			{
				AccessibilityRange* range = core::CreateObject<AccessibilityRange>(getAccessibilityResource(), State, Id, node->ContentRevision, node->SelectionStart, node->SelectionEnd);

				if (!range)
				{
					return E_OUTOFMEMORY;
				}

				return createRangeArray(range, output);
			}

			for (const AccessibilityNode& item : State->Snapshot.GetNodes())
			{
				if (item.Id.Owner == Id.Owner && item.Id.Slot == Id.Slot && item.Id.Generation == Id.Generation && item.bSelected)
				{
					AccessibilityProvider* selected = findOrCreateProvider(State, item.Id);

					if (!selected)
					{
						return E_OUTOFMEMORY;
					}

					return createRangeArray(static_cast<IRawElementProviderSimple*>(selected), output);
				}
			}

			return createRangeArray(nullptr, output);
		}

		HRESULT AccessibilityProvider::GetVisibleRanges(SAFEARRAY** output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);
			const AccessibilityNode* node = State->bClosed ? nullptr : State->Snapshot.Find(Id);

			if (!node)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			if (node->bPassword)
			{
				return E_ACCESSDENIED;
			}

			if (node->bOffscreen)
			{
				return createRangeArray(nullptr, output);
			}

			core::uint32 first = 0;
			core::uint32 last = static_cast<core::uint32>(node->Value.Size());

			if (node->Geometry)
			{
				core::Result<core::uint32> start = node->Geometry->HitTest(node->Bounds.X, node->Bounds.Y);
				core::Result<core::uint32> end = node->Geometry->HitTest(node->Bounds.X + node->Bounds.Width, node->Bounds.Y + node->Bounds.Height);

				if (start && end)
				{
					first = rox::core::Min(start.Value(), end.Value());
					last = rox::core::Max(start.Value(), end.Value());
				}
			}

			AccessibilityRange* range = core::CreateObject<AccessibilityRange>(getAccessibilityResource(), State, Id, node->ContentRevision, first, last);

			if (!range)
			{
				return E_OUTOFMEMORY;
			}

			return createRangeArray(range, output);
		}

		HRESULT AccessibilityProvider::RangeFromPoint(UiaPoint point, ITextRangeProvider** output)
		{
			if (!output)
			{
				return E_POINTER;
			}

			*output = nullptr;
			rox::core::LockGuard lock(State->Mutex);
			const AccessibilityNode* node = State->bClosed ? nullptr : State->Snapshot.Find(Id);

			if (!node)
			{
				return UIA_E_ELEMENTNOTAVAILABLE;
			}

			if (node->bPassword)
			{
				return E_ACCESSDENIED;
			}

			core::uint32 byte = 0;

			if (node->Geometry)
			{
				core::Result<core::uint32> hit = node->Geometry->HitTest(point.x - State->Origin.x, point.y - State->Origin.y);

				if (!hit)
				{
					return toNativeStatus(hit.GetError().Code);
				}

				byte = hit.Value();
			}

			*output = core::CreateObject<AccessibilityRange>(getAccessibilityResource(), State, Id, node->ContentRevision, byte, byte);

			return *output ? S_OK : E_OUTOFMEMORY;
		}
	}

	WindowsAccessibility::WindowsAccessibility(core::SharedPtr<WindowsAccessibilityState> state) noexcept
		: m_State(rox::core::Move(state))
	{
	}

	WindowsAccessibility::~WindowsAccessibility()
	{
		Close();
	}

	core::Result<core::UniquePtr<WindowsAccessibility>> WindowsAccessibility::Create(void* window)
	{
		using R = core::Result<core::UniquePtr<WindowsAccessibility>>;
		core::SharedPtr<WindowsAccessibilityState> state;

		if (!window || !core::SharedPtr<WindowsAccessibilityState>::CreateInto(getAccessibilityResource(), &state, static_cast<HWND>(window)) || !state->Queue.Reserve(64))
		{
			return R::Failure(core::EErrorCode::ExternalFailure, "Platform.Accessibility", "Accessibility owner allocation failed.");
		}

		const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

		if (FAILED(apartment))
		{
			return R::Failure(core::EErrorCode::ExternalFailure, "Platform.Accessibility", "Accessibility window owner requires a COM STA.");
		}

		state->bComInitialized = true;
		core::UniquePtr<WindowsAccessibility> owner = core::MakeUnique<WindowsAccessibility>(getAccessibilityResource(), state);

		if (!owner)
		{
			state->bComInitialized = false;
			CoUninitialize();
		}

		if (!owner)
		{
			return R::Failure(core::EErrorCode::ExternalFailure, "Platform.Accessibility", "Accessibility owner allocation failed.");
		}

		return R::Success(rox::core::Move(owner));
	}

	core::Result<void> WindowsAccessibility::Publish(const AccessibilitySnapshot& snapshot)
	{
		ASSERT(GetCurrentThreadId() == m_State->OwnerThread, "Accessibility publication requires the window owner thread.");

		if (!snapshot.GetPublication())
		{
			Close();

			return core::Result<void>::Success();
		}

		POINT origin{};
		ClientToScreen(m_State->Window, &origin);
		core::Array<AccessibilityProvider*> removed(getAccessibilityResource());
		AccessibilityProvider* root = nullptr;
		AccessibilityProvider* focus = nullptr;
		bool bStructureChanged = false;
		struct Notification
		{
		public:
			AccessibilityProvider* Provider = nullptr;
			EVENTID Event = 0;
			PROPERTYID Property = 0;
			VARIANT Before{};
			VARIANT After{};
		};
		Notification notifications[128]{};
		unsigned notificationCount = 0;
		{
			rox::core::LockGuard lock(m_State->Mutex);

			if (m_State->bClosed)
			{
				return core::Result<void>::Failure(core::EErrorCode::Cancelled, "Platform.Accessibility", "Accessibility owner is closed.");
			}

			if (!removed.Reserve(m_State->Providers.Size()))
			{
				return core::Result<void>::Failure(core::EErrorCode::ExternalFailure, "Platform.Accessibility",
					"Accessibility retirement allocation failed.");
			}

			const core::Span<const AccessibilityNode> old = m_State->Snapshot.GetNodes();
			const core::Span<const AccessibilityNode> next = snapshot.GetNodes();
			bStructureChanged = old.Size() != next.Size();
			AccessibilityId oldFocus;
			AccessibilityId newFocus;

			for (const AccessibilityNode& node : old)
			{
				if (node.bFocused)
				{
					oldFocus = node.Id;
				}
			}

			for (rox::core::usize index = 0; index < next.Size(); ++index)
			{
				if (next[index].bFocused)
				{
					newFocus = next[index].Id;
				}

				if (!bStructureChanged && (old[index].Id != next[index].Id || old[index].Parent != next[index].Parent))
				{
					bStructureChanged = true;
				}
			}

			if (!bStructureChanged)
			{
				const auto notify = [&](AccessibilityId id, EVENTID event, PROPERTYID property, const AccessibilityNode& before,
					const AccessibilityNode& after)
					{
						if (notificationCount == 128)
						{
							bStructureChanged = true;

							return;
						}

						AccessibilityProvider* object = findOrCreateProvider(m_State, id);

						if (!object)
						{
							return;
						}

						Notification& item = notifications[notificationCount++];
						item.Provider = object;
						item.Event = event;
						item.Property = property;

						if (property == UIA_ValueValuePropertyId)
						{
							item.Before.vt = item.After.vt = VT_BSTR;
							createBstr(before.Value.View(), &item.Before.bstrVal);
							createBstr(after.Value.View(), &item.After.bstrVal);
						}
						else if (property == UIA_ToggleToggleStatePropertyId)
						{
							item.Before.vt = item.After.vt = VT_I4;
							item.Before.lVal = before.ToggleState;
							item.After.lVal = after.ToggleState;
						}
					};

				for (rox::core::usize index = 0; index < next.Size(); ++index)
				{
					const AccessibilityNode& before = old[index];
					const AccessibilityNode& after = next[index];

					if (after.Role == EAccessibilityRole::Edit && !after.bPassword)
					{
						if (before.ContentRevision != after.ContentRevision)
						{
							notify(after.Id, UIA_Text_TextChangedEventId, 0, before, after);
							notify(after.Id, 0, UIA_ValueValuePropertyId, before, after);
						}

						if (before.SelectionStart != after.SelectionStart || before.SelectionEnd != after.SelectionEnd)
						{
							notify(after.Id, UIA_Text_TextSelectionChangedEventId, 0, before, after);
						}
					}

					if (before.ToggleState != after.ToggleState)
					{
						notify(after.Id, 0, UIA_ToggleToggleStatePropertyId, before, after);
					}

					if (before.bSelected != after.bSelected && after.bSelected)
					{
						notify(after.Id, UIA_SelectionItem_ElementSelectedEventId, 0, before, after);
					}
				}
			}

			m_State->Snapshot = snapshot;
			m_State->Origin = origin;

			for (rox::core::usize index = m_State->Providers.Size(); index; --index)
			{
				AccessibilityProvider* candidate = m_State->Providers[index - 1];

				if (candidate->Id.IsValid() && !snapshot.Find(candidate->Id))
				{
					removed.AddRequired(candidate);
					m_State->Providers.RemoveAt(index - 1);
				}
			}

			if (bStructureChanged)
			{
				root = findOrCreateProvider(m_State, {});
			}

			if (newFocus.IsValid() && newFocus != oldFocus)
			{
				focus = findOrCreateProvider(m_State, newFocus);
			}
		}

		for (AccessibilityProvider* candidate : removed)
		{
			UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(candidate));
			candidate->Release();
		}

		for (unsigned index = 0; index < notificationCount; ++index)
		{
			Notification& item = notifications[index];

			if (item.Event)
			{
				UiaRaiseAutomationEvent(item.Provider, item.Event);
			}
			else
			{
				UiaRaiseAutomationPropertyChangedEvent(item.Provider, item.Property, item.Before, item.After);
			}

			VariantClear(&item.Before);
			VariantClear(&item.After);
			item.Provider->Release();
		}

		if (root)
		{
			UiaRaiseStructureChangedEvent(root, StructureChangeType_ChildrenInvalidated, nullptr, 0);
			root->Release();
		}

		if (focus)
		{
			UiaRaiseAutomationEvent(focus, UIA_AutomationFocusChangedEventId);
			focus->Release();
		}

		return core::Result<void>::Success();
	}

	AccessibilityRequest WindowsAccessibility::Poll()
	{
		ASSERT(GetCurrentThreadId() == m_State->OwnerThread, "Accessibility actions require the window owner thread.");
		rox::core::LockGuard lock(m_State->Mutex);

		if (m_State->bClosed || m_State->Queue.IsEmpty())
		{
			return {};
		}

		AccessibilityRequest request = WindowsAccessibilityAccess::Make(rox::core::Move(m_State->Queue[0]));
		m_State->Queue.RemoveAt(0);

		return request;
	}

	core::int64 WindowsAccessibility::ReturnProvider(core::uint64 word, core::int64 data)
	{
		AccessibilityProvider* root;
		{
			rox::core::LockGuard lock(m_State->Mutex);
			root = findOrCreateProvider(m_State, {});
		}

		if (!root)
		{
			return 0;
		}

		const core::int64 result = UiaReturnRawElementProvider(m_State->Window, static_cast<WPARAM>(word), static_cast<LPARAM>(data), root);
		root->Release();

		return result;
	}

	void WindowsAccessibility::Close() noexcept
	{
		if (!m_State)
		{
			return;
		}

		ASSERT(GetCurrentThreadId() == m_State->OwnerThread, "Accessibility closure requires the window owner thread.");
		core::Array<AccessibilityProvider*> providers(getAccessibilityResource());
		{
			rox::core::LockGuard lock(m_State->Mutex);

			if (m_State->bClosed)
			{
				return;
			}

			m_State->bClosed = true;
			// Geometry may retain application allocator storage. Drop it under the query lock,
			// on the UI owner, before application retirement. External COM objects retain only this empty system owner.
			m_State->Snapshot = {};

			for (core::SharedPtr<AccessibilityActionState>& action : m_State->Queue)
			{
				WindowsAccessibilityAccess::Make(action).Complete(core::EErrorCode::Cancelled);
			}

			m_State->Queue.Clear();
			providers = rox::core::Move(m_State->Providers);
		}

		for (AccessibilityProvider* candidate : providers)
		{
			UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(candidate));
			candidate->Release();
		}

		if (m_State->bComInitialized)
		{
			m_State->bComInitialized = false;
			CoUninitialize();
		}
	}
}
