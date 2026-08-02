#pragma once

#include "TextInput.h"

#include <windows.h>
#include <string>

namespace llp::accessibility
{
struct Callbacks
{
    void* context = nullptr;
    void (*focus)(void* context) = nullptr;
    bool (*hasFocus)(void* context) = nullptr;
    void (*textChanged)(void* context) = nullptr;
    void (*selectionChanged)(void* context) = nullptr;
};

class SearchAccessibility
{
public:
    class Provider;

    SearchAccessibility() = default;
    ~SearchAccessibility();
    SearchAccessibility(const SearchAccessibility&) = delete;
    SearchAccessibility& operator=(const SearchAccessibility&) = delete;

    bool Initialize(HWND hwnd, input::TextBuffer* buffer, const Callbacks& callbacks);
    void Shutdown();
    LRESULT HandleGetObject(WPARAM wParam, LPARAM lParam) const;
    void NotifyTextChanged() const;
    void NotifySelectionChanged() const;
    void NotifyFocusChanged() const;
    void NotifyStatus(const std::wstring& text, bool warning) const;

private:
    HWND hwnd_ = nullptr;
    Provider* provider_ = nullptr;
};
}
