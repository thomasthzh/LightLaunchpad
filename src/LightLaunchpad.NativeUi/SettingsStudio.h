#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "Hotwords.h"
#include "InputStats.h"

namespace llp::settings_ui
{

struct SettingsViewModel
{
    std::wstring launchpadFolder;
    std::wstring hotkey = L"Alt+D";
    std::wstring displayMode = L"Spotlight";
    std::wstring language = L"Chinese";
    int spotlightWidth = 740;
    int spotlightHeight = 460;
    int iconSize = 56;
    int appSpacing = 10;
    double wheelSensitivity = 1.0;
    bool startWithWindows = false;
    bool resetSearchOnOpen = true;
    bool showRecentAndFavorites = true;
    bool trackLaunchHistory = true;
    bool reduceMotion = false;
    bool enableEverythingSearch = true;
    bool enableInputStats = true;
    bool enableHotwordsNoPrefix = true;
    bool enableInlineCalculator = true;
    std::vector<llp::hotwords::HotwordEntry> hotwords;
    std::wstring glassPreset = L"Crystal";
    int glassBlurPercent = 6;
    int glassTintPercent = 2;
    int glassRefractionPx = 30;
    int glassRimPercent = 95;
    int spotlightMouseBiasPercent = 72;
    int lastSettingsPage = 0;

    // Live runtime read-only info
    bool darkMode = true;
    bool everythingAvailable = false;
    int catalogItemCount = 0;
    std::wstring dataDirectory;
    std::wstring statusMessage;
    std::wstring versionLabel = L"V1.13";
};

struct SettingsCallbacks
{
    std::function<SettingsViewModel()> readState;
    std::function<bool(const SettingsViewModel&, std::wstring& statusOut)> applySettings;
    std::function<void(const SettingsViewModel&)> previewGlass;
    std::function<bool(HWND owner, std::wstring& selectedFolder)> browseFolder;
    std::function<void()> openLaunchpadFolder;
    std::function<void()> rescanCatalog;
    std::function<int()> importSystemShortcuts;
    std::function<void()> clearLaunchHistory;
    std::function<void()> clearInputStats;
    std::function<void()> openDataDirectory;
    std::function<bool(std::wstring& outPath)> exportDiagnostics;
    std::function<void()> resetDefaults;
    std::function<llp::stats::Snapshot()> readInputStats;
    std::function<void(size_t index, bool enabled)> toggleHotword;
    std::function<void()> openHotwordsFile;
    std::function<void()> reloadHotwords;
    std::function<void()> resetHotwords;
    std::function<void(int page)> onPageChanged;
    std::function<void()> onClosed;
};

HWND ShowSettingsStudio(HWND owner, const SettingsCallbacks& callbacks, int initialPage);
void RefreshSettingsStudio(HWND hwnd);
void CloseSettingsStudio(HWND hwnd);
bool IsSettingsStudioWindow(HWND hwnd);

// Renders the current Settings Studio view to a PNG file for real-screen verification.
bool CaptureSettingsStudioPng(HWND hwnd, const std::wstring& outputPath);

} // namespace llp::settings_ui
