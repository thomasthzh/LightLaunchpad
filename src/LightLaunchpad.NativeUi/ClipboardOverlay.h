#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "ClipboardHistory.h"

namespace llp::clipboard_ui
{

struct OverlayCallbacks
{
    std::function<llp::clipboard::ClipboardStore*()> getStore;
    std::function<void()> onStoreMutated;
    std::function<bool()> isDarkMode;
    std::function<bool()> isChinese;
    std::function<int()> getMouseBiasPercent;
};

void ToggleClipboardOverlay(HWND owner, const OverlayCallbacks& callbacks);
void HideClipboardOverlay();
bool IsClipboardOverlayVisible();

// Renders the Clipboard Overlay to a PNG file for real-screen verification.
bool CaptureClipboardOverlayPng(HWND owner, const OverlayCallbacks& callbacks, const std::wstring& outputPath);

} // namespace llp::clipboard_ui
