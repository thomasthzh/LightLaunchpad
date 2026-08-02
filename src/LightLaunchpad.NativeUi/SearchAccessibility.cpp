#include "SearchAccessibility.h"

#include <uiautomationclient.h>
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <new>
#include <oleauto.h>
#include <string>

namespace llp::accessibility
{
namespace
{
class SearchTextRange;

SAFEARRAY* EmptyUnknownArray()
{
    return SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
}

}

class SearchAccessibility::Provider final : public IRawElementProviderSimple, public IValueProvider, public ITextProvider
{
public:
    Provider(HWND hwnd, input::TextBuffer* buffer, const Callbacks& callbacks)
        : hwnd_(hwnd), buffer_(buffer), callbacks_(callbacks)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (interfaceId == IID_IUnknown || interfaceId == IID_IRawElementProviderSimple)
            *object = static_cast<IRawElementProviderSimple*>(this);
        else if (interfaceId == IID_IValueProvider)
            *object = static_cast<IValueProvider*>(this);
        else if (interfaceId == IID_ITextProvider)
            *object = static_cast<ITextProvider*>(this);
        if (!*object) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++references_;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* result) override
    {
        if (!result) return E_POINTER;
        *result = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID patternId, IUnknown** result) override
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (patternId == UIA_ValuePatternId) *result = static_cast<IValueProvider*>(this);
        else if (patternId == UIA_TextPatternId) *result = static_cast<ITextProvider*>(this);
        if (*result) (*result)->AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId, VARIANT* result) override
    {
        if (!result) return E_POINTER;
        VariantInit(result);
        switch (propertyId)
        {
        case UIA_ControlTypePropertyId:
            result->vt = VT_I4;
            result->lVal = UIA_EditControlTypeId;
            break;
        case UIA_NamePropertyId:
            result->vt = VT_BSTR;
            result->bstrVal = SysAllocString(L"Search applications");
            break;
        case UIA_AutomationIdPropertyId:
            result->vt = VT_BSTR;
            result->bstrVal = SysAllocString(L"LightLaunchpad.Search");
            break;
        case UIA_ClassNamePropertyId:
            result->vt = VT_BSTR;
            result->bstrVal = SysAllocString(L"LightLaunchpadSearch");
            break;
        case UIA_FrameworkIdPropertyId:
            result->vt = VT_BSTR;
            result->bstrVal = SysAllocString(L"Win32");
            break;
        case UIA_HelpTextPropertyId:
            result->vt = VT_BSTR;
            result->bstrVal = SysAllocString(L"Type an application name, pinyin, or initials.");
            break;
        case UIA_IsKeyboardFocusablePropertyId:
        case UIA_IsEnabledPropertyId:
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            result->vt = VT_BOOL;
            result->boolVal = VARIANT_TRUE;
            break;
        case UIA_HasKeyboardFocusPropertyId:
            result->vt = VT_BOOL;
            result->boolVal = callbacks_.hasFocus
                ? (callbacks_.hasFocus(callbacks_.context) ? VARIANT_TRUE : VARIANT_FALSE)
                : (GetFocus() == hwnd_ ? VARIANT_TRUE : VARIANT_FALSE);
            break;
        case UIA_IsPasswordPropertyId:
            result->vt = VT_BOOL;
            result->boolVal = VARIANT_FALSE;
            break;
        case UIA_NativeWindowHandlePropertyId:
            result->vt = VT_I4;
            result->lVal = static_cast<LONG>(reinterpret_cast<LONG_PTR>(hwnd_));
            break;
        default:
            break;
        }
        if ((result->vt == VT_BSTR) && !result->bstrVal) return E_OUTOFMEMORY;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) override
    {
        if (!result) return E_POINTER;
        return UiaHostProviderFromHwnd(hwnd_, result);
    }

    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override
    {
        if (!value || !buffer_) return E_INVALIDARG;
        const std::wstring candidate = value;
        if (candidate.size() > input::TextBuffer::MaximumLength) return E_INVALIDARG;
        Focus();
        if (candidate != buffer_->Text())
        {
            buffer_->SetText(candidate);
            TextChanged();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Value(BSTR* result) override
    {
        if (!result || !buffer_) return E_POINTER;
        *result = SysAllocStringLen(buffer_->Text().data(), static_cast<UINT>(buffer_->Text().size()));
        return *result || buffer_->Text().empty() ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT STDMETHODCALLTYPE get_IsReadOnly(WINBOOL* result) override
    {
        if (!result) return E_POINTER;
        *result = FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** result) override;
    HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** result) override;
    HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple*, ITextRangeProvider** result) override
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint, ITextRangeProvider** result) override;
    HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider** result) override;
    HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection* result) override
    {
        if (!result) return E_POINTER;
        *result = SupportedTextSelection_Single;
        return S_OK;
    }

    HWND Window() const { return hwnd_; }
    input::TextBuffer* Buffer() const { return buffer_; }
    void Focus()
    {
        if (callbacks_.focus) callbacks_.focus(callbacks_.context);
        else SetFocus(hwnd_);
    }
    void TextChanged()
    {
        if (callbacks_.textChanged) callbacks_.textChanged(callbacks_.context);
    }
    void SelectionChanged()
    {
        if (callbacks_.selectionChanged) callbacks_.selectionChanged(callbacks_.context);
    }

private:
    std::atomic<ULONG> references_{ 1 };
    HWND hwnd_ = nullptr;
    input::TextBuffer* buffer_ = nullptr;
    Callbacks callbacks_;
};

namespace
{
class SearchTextRange final : public ITextRangeProvider
{
public:
    SearchTextRange(SearchAccessibility::Provider* provider, size_t start, size_t end)
        : provider_(provider), start_(start), end_(end)
    {
        if (provider_) provider_->AddRef();
        Normalize();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (interfaceId == IID_IUnknown || interfaceId == IID_ITextRangeProvider) *object = static_cast<ITextRangeProvider*>(this);
        if (!*object) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** result) override
    {
        if (!result) return E_POINTER;
        *result = new (std::nothrow) SearchTextRange(provider_, start_, end_);
        return *result ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* range, WINBOOL* result) override
    {
        if (!result) return E_POINTER;
        auto* other = dynamic_cast<SearchTextRange*>(range);
        *result = other && other->provider_ == provider_ && other->start_ == start_ && other->end_ == end_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider* target, TextPatternRangeEndpoint targetEndpoint, int* result) override
    {
        if (!result) return E_POINTER;
        auto* other = dynamic_cast<SearchTextRange*>(target);
        if (!other || other->provider_ != provider_) return E_INVALIDARG;
        const size_t left = endpoint == TextPatternRangeEndpoint_Start ? start_ : end_;
        const size_t right = targetEndpoint == TextPatternRangeEndpoint_Start ? other->start_ : other->end_;
        *result = left < right ? -1 : (left > right ? 1 : 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override
    {
        Normalize();
        const size_t length = Length();
        if (unit == TextUnit_Character && length > 0)
        {
            if (start_ >= length) start_ = length - 1;
            end_ = std::min(length, start_ + 1);
        }
        else if (unit >= TextUnit_Line)
        {
            start_ = 0;
            end_ = length;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID, VARIANT, WINBOOL, ITextRangeProvider** result) override
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindText(BSTR text, WINBOOL backward, WINBOOL ignoreCase, ITextRangeProvider** result) override
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (!text || !provider_ || !provider_->Buffer()) return S_OK;
        std::wstring haystack = provider_->Buffer()->Text().substr(start_, end_ - start_);
        std::wstring needle = text;
        if (ignoreCase)
        {
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
            std::transform(needle.begin(), needle.end(), needle.begin(), [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
        }
        const size_t found = backward ? haystack.rfind(needle) : haystack.find(needle);
        if (found != std::wstring::npos) *result = new (std::nothrow) SearchTextRange(provider_, start_ + found, start_ + found + needle.size());
        return (found == std::wstring::npos || *result) ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID, VARIANT* result) override
    {
        if (!result) return E_POINTER;
        VariantInit(result);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** result) override
    {
        if (!result) return E_POINTER;
        *result = SafeArrayCreateVector(VT_R8, 0, 0);
        return *result ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple** result) override
    {
        if (!result || !provider_) return E_POINTER;
        *result = static_cast<IRawElementProviderSimple*>(provider_);
        (*result)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetText(int maxLength, BSTR* result) override
    {
        if (!result || !provider_ || !provider_->Buffer()) return E_POINTER;
        Normalize();
        size_t count = end_ - start_;
        if (maxLength >= 0) count = std::min(count, static_cast<size_t>(maxLength));
        *result = SysAllocStringLen(provider_->Buffer()->Text().data() + start_, static_cast<UINT>(count));
        return *result || count == 0 ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int* moved) override
    {
        if (!moved) return E_POINTER;
        if (unit != TextUnit_Character) { *moved = 0; return S_OK; }
        const int current = static_cast<int>(start_);
        const int target = std::clamp(current + count, 0, static_cast<int>(Length()));
        start_ = end_ = static_cast<size_t>(target);
        *moved = target - current;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count, int* moved) override
    {
        if (!moved) return E_POINTER;
        if (unit != TextUnit_Character) { *moved = 0; return S_OK; }
        size_t& value = endpoint == TextPatternRangeEndpoint_Start ? start_ : end_;
        const int current = static_cast<int>(value);
        const int target = std::clamp(current + count, 0, static_cast<int>(Length()));
        value = static_cast<size_t>(target);
        if (start_ > end_)
        {
            if (endpoint == TextPatternRangeEndpoint_Start) end_ = start_;
            else start_ = end_;
        }
        *moved = target - current;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider* target, TextPatternRangeEndpoint targetEndpoint) override
    {
        auto* other = dynamic_cast<SearchTextRange*>(target);
        if (!other || other->provider_ != provider_) return E_INVALIDARG;
        const size_t value = targetEndpoint == TextPatternRangeEndpoint_Start ? other->start_ : other->end_;
        if (endpoint == TextPatternRangeEndpoint_Start) start_ = std::min(value, end_);
        else end_ = std::max(value, start_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Select() override
    {
        if (!provider_ || !provider_->Buffer()) return UIA_E_ELEMENTNOTAVAILABLE;
        Normalize();
        provider_->Buffer()->SetSelection(start_, end_);
        provider_->Focus();
        provider_->SelectionChanged();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE ScrollIntoView(WINBOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** result) override
    {
        if (!result) return E_POINTER;
        *result = EmptyUnknownArray();
        return *result ? S_OK : E_OUTOFMEMORY;
    }

private:
    ~SearchTextRange()
    {
        if (provider_) provider_->Release();
    }
    size_t Length() const { return provider_ && provider_->Buffer() ? provider_->Buffer()->Text().size() : 0; }
    void Normalize()
    {
        const size_t length = Length();
        start_ = std::min(start_, length);
        end_ = std::min(end_, length);
        if (start_ > end_) std::swap(start_, end_);
    }
    std::atomic<ULONG> references_{ 1 };
    SearchAccessibility::Provider* provider_ = nullptr;
    size_t start_ = 0;
    size_t end_ = 0;
};

HRESULT PutSingleRange(SearchTextRange* range, SAFEARRAY** result)
{
    if (!result) return E_POINTER;
    if (!range)
    {
        *result = nullptr;
        return E_OUTOFMEMORY;
    }
    *result = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
    if (!*result)
    {
        if (range) range->Release();
        return E_OUTOFMEMORY;
    }
    LONG index = 0;
    const HRESULT status = SafeArrayPutElement(*result, &index, static_cast<IUnknown*>(range));
    if (range) range->Release();
    if (FAILED(status))
    {
        SafeArrayDestroy(*result);
        *result = nullptr;
    }
    return status;
}
}

HRESULT SearchAccessibility::Provider::GetSelection(SAFEARRAY** result)
{
    if (!buffer_) return UIA_E_ELEMENTNOTAVAILABLE;
    return PutSingleRange(new (std::nothrow) SearchTextRange(this, buffer_->SelectionStart(), buffer_->SelectionEnd()), result);
}

HRESULT SearchAccessibility::Provider::GetVisibleRanges(SAFEARRAY** result)
{
    if (!buffer_) return UIA_E_ELEMENTNOTAVAILABLE;
    return PutSingleRange(new (std::nothrow) SearchTextRange(this, 0, buffer_->Text().size()), result);
}

HRESULT SearchAccessibility::Provider::RangeFromPoint(UiaPoint, ITextRangeProvider** result)
{
    if (!result || !buffer_) return E_POINTER;
    *result = new (std::nothrow) SearchTextRange(this, buffer_->Caret(), buffer_->Caret());
    return *result ? S_OK : E_OUTOFMEMORY;
}

HRESULT SearchAccessibility::Provider::get_DocumentRange(ITextRangeProvider** result)
{
    if (!result || !buffer_) return E_POINTER;
    *result = new (std::nothrow) SearchTextRange(this, 0, buffer_->Text().size());
    return *result ? S_OK : E_OUTOFMEMORY;
}

SearchAccessibility::~SearchAccessibility()
{
    Shutdown();
}

bool SearchAccessibility::Initialize(HWND hwnd, input::TextBuffer* buffer, const Callbacks& callbacks)
{
    Shutdown();
    if (!hwnd || !buffer) return false;
    hwnd_ = hwnd;
    provider_ = new (std::nothrow) Provider(hwnd, buffer, callbacks);
    return provider_ != nullptr;
}

void SearchAccessibility::Shutdown()
{
    if (provider_) provider_->Release();
    provider_ = nullptr;
    hwnd_ = nullptr;
}

LRESULT SearchAccessibility::HandleGetObject(WPARAM wParam, LPARAM lParam) const
{
    if (!provider_ || lParam != UiaRootObjectId) return 0;
    return UiaReturnRawElementProvider(hwnd_, wParam, lParam, static_cast<IRawElementProviderSimple*>(provider_));
}

void SearchAccessibility::NotifyTextChanged() const
{
    if (provider_) UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider_), UIA_Text_TextChangedEventId);
}

void SearchAccessibility::NotifySelectionChanged() const
{
    if (provider_) UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider_), UIA_Text_TextSelectionChangedEventId);
}

void SearchAccessibility::NotifyFocusChanged() const
{
    if (provider_) UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider_), UIA_AutomationFocusChangedEventId);
}

void SearchAccessibility::NotifyStatus(const std::wstring& text, bool warning) const
{
    if (!provider_ || text.empty()) return;
    BSTR displayText = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
    BSTR activityId = SysAllocString(L"LightLaunchpad.Organization");
    if (!displayText || !activityId)
    {
        SysFreeString(activityId);
        SysFreeString(displayText);
        return;
    }
    UiaRaiseNotificationEvent(
        static_cast<IRawElementProviderSimple*>(provider_),
        warning ? NotificationKind_ActionAborted : NotificationKind_ActionCompleted,
        NotificationProcessing_ImportantMostRecent,
        displayText,
        activityId);
    SysFreeString(activityId);
    SysFreeString(displayText);
    if (hwnd_) NotifyWinEvent(EVENT_SYSTEM_ALERT, hwnd_, OBJID_CLIENT, CHILDID_SELF);
}
}
