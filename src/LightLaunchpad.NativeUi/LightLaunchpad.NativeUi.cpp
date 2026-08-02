#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <imm.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include "LightLaunchpad.BuildIdentity.generated.h"
#include "LightLaunchpad.Version.h"
#include "Catalog.h"
#include "InteractionState.h"
#include "Persistence.h"
#include "SceneGeometry.h"
#include "SearchAccessibility.h"
#include "SearchIndex.h"
#include "ShellBroker.h"
#include "TextInput.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <cwchar>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif

#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3
#endif

#ifndef DWMSBT_NONE
#define DWMSBT_NONE 1
#endif

namespace
{
using Json = nlohmann::json;

constexpr int HotkeyId = 0x4C55;
constexpr int CandidateHotkeyId = 0x4C56;
constexpr int TrayOpenCommand = 1001;
constexpr int TrayRefreshCommand = 1002;
constexpr int TrayExitCommand = 1003;
constexpr int TraySettingsCommand = 1004;
constexpr int TrayOpenFolderCommand = 1005;
constexpr int ItemLaunchCommand = 2001;
constexpr int ItemOpenLocationCommand = 2002;
constexpr int ItemRemoveCommand = 2003;
constexpr int ItemRenameCommand = 2004;
constexpr int RegionDeleteCommand = 2101;
constexpr int RegionRenameCommand = 2102;
constexpr int RegionRenameSelectedCommand = 2103;
constexpr int RegionDeleteSelectedCommand = 2104;
constexpr int WorkspaceCreateRegionCommand = 2201;
constexpr int WorkspaceSelectAllCommand = 2202;
constexpr int WorkspaceClearSelectionCommand = 2203;
constexpr int WorkspaceImportFilesCommand = 2204;
constexpr int WorkspaceImportStartMenuCommand = 2205;
constexpr int WorkspaceToggleEditCommand = 2206;
constexpr UINT WmTray = WM_APP + 72;
constexpr UINT WmActivateInstance = WM_APP + 73;
constexpr UINT WmShellBrokerResult = WM_APP + 74;
constexpr wchar_t WindowClassName[] = LLP_WINDOW_CLASS_NAME_WIDE;
constexpr wchar_t SettingsWindowClassName[] = L"LightLaunchpadNativeSettingsWindow";
constexpr wchar_t TextInputWindowClassName[] = L"LightLaunchpadNativeTextInputWindow";
constexpr wchar_t AppIconResourceName[] = L"IDI_APP";
constexpr int SpotlightCornerRadius = 34;
constexpr int SpotlightEdgeLayers = 10;
constexpr int SpotlightGlassRimLayers = 9;
constexpr int SpotlightPanelInset = 0;
constexpr int SpotlightContentInset = 68;
constexpr int SpotlightContentBottomInset = 72;
constexpr int SpotlightContentFeatherSize = 44;
constexpr int SpotlightSearchSideInset = 68;
constexpr int SpotlightSearchCornerRadius = 24;
constexpr COLORREF SpotlightTransparentKey = RGB(1, 2, 3);
constexpr BYTE SpotlightGlassAlpha = 200;
constexpr size_t MaxResidentIconCount = 96;
constexpr UINT_PTR DragAnimationTimerId = 0x4C5601;
constexpr int DragTargetSettleMs = 55;
constexpr double DragReflowSmoothing = 0.24;
constexpr UINT_PTR HotkeyEditSubclassId = 0x4C5602;
constexpr UINT_PTR TrayIconRetryTimerId = 0x4C5603;
constexpr UINT TrayIconRetryMs = 1000;
constexpr int TrayIconRetryMaxAttempts = 8;
constexpr DWORD ActivationIpcTimeoutMs = 2000;
constexpr size_t MaxStartupLogBytes = 256 * 1024;
constexpr ULONG_PTR ActivationCopyDataId = 0x4C4C5001;
enum AccentState
{
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

struct AccentPolicy
{
    int accentState = ACCENT_DISABLED;
    int accentFlags = 0;
    DWORD gradientColor = 0;
    int animationId = 0;
};

struct WindowCompositionAttribData
{
    int attribute = 0;
    void* data = nullptr;
    SIZE_T dataSize = 0;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttribData*);

enum class SettingsControlId : int
{
    LaunchpadFolder = 3001,
    Hotkey = 3002,
    IconSize = 3003,
    DisplayMode = 3004,
    Language = 3005,
    SpotlightWidth = 3006,
    SpotlightHeight = 3007,
    AppSpacing = 3008,
    WheelSensitivity = 3009,
    StartWithWindows = 3010,
    Save = 3011,
    Apply = 3012,
    Cancel = 3013,
    ResetSearchOnOpen = 3014,
    DiagnosticsSummary = 3015,
    FolderLabel = 3020,
    HotkeyLabel = 3021,
    IconSizeLabel = 3022,
    DisplayModeLabel = 3023,
    LanguageLabel = 3024,
    SpotlightSizeLabel = 3025,
    AppSpacingLabel = 3026,
    WheelSensitivityLabel = 3027,
    DiagnosticsLabel = 3028
};

int ControlId(SettingsControlId id)
{
    return static_cast<int>(id);
}

struct Settings
{
    std::wstring launchpadFolder;
    std::wstring hotkey = L"Alt+D";
    std::wstring displayMode = L"Launchpad";
    std::wstring language = L"English";
    int spotlightWidth = 980;
    int spotlightHeight = 720;
    int iconSize = 56;
    int appSpacing = 8;
    double wheelSensitivity = 1.0;
    bool startWithWindows = false;
    bool resetSearchOnOpen = true;
    bool showRecentAndFavorites = true;
    bool trackLaunchHistory = true;
};

struct Region
{
    std::wstring id;
    std::wstring name;
    int order = 0;
};

struct LaunchItem
{
    std::wstring sourcePath;
    std::wstring displayName;
    std::wstring regionId;
    int order = 0;
    HICON icon = nullptr;
    uint64_t iconLastUsed = 0;
    std::wstring stableId;
    std::vector<std::wstring> aliases;
    std::wstring subtitle;
    bool iconRequestPending = false;
};

struct HitTile
{
    RECT bounds;
    int itemIndex = -1;
};

struct IconOpaqueBoundsCacheEntry
{
    HICON icon = nullptr;
    bool hasBounds = false;
    RECT bounds = {};
    int width = 0;
    int height = 0;
};

struct RegionHit
{
    RECT bounds;
    RECT headerBounds;
    std::wstring regionId;
};

struct DropTarget
{
    std::wstring regionId;
    int order = 0;
    bool valid = false;
};

struct RegionDropTarget
{
    std::wstring regionId;
    bool insertAfter = false;
    bool valid = false;
};

struct DragVisualOffset
{
    std::wstring sourcePath;
    double dx = 0.0;
    double dy = 0.0;
};

enum class DragMode
{
    None,
    Items,
    Region
};

enum class TileVisualState
{
    Normal,
    Hover,
    KeyboardFocus,
    Selected,
    MultiSelected,
    Pressed,
    DragSource,
    DropTarget
};

struct SpotlightLayerBuffer
{
    int width = 0;
    int height = 0;
    HDC baseDc = nullptr;
    HDC fullDc = nullptr;
    HBITMAP baseBitmap = nullptr;
    HBITMAP fullBitmap = nullptr;
    HGDIOBJ oldBaseBitmap = nullptr;
    HGDIOBJ oldFullBitmap = nullptr;
    void* baseBits = nullptr;
    void* fullBits = nullptr;
};

struct SpotlightBackdropCapture
{
    int width = 0;
    int height = 0;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    void* bits = nullptr;
};

HINSTANCE g_instance = nullptr;
HWND g_hwnd = nullptr;
HWND g_settingsHwnd = nullptr;
Settings g_settings;
std::vector<Region> g_regions;
std::vector<LaunchItem> g_items;
std::vector<LaunchItem> g_missingLayoutItems;
std::vector<int> g_filtered;
std::vector<HitTile> g_hits;
std::vector<IconOpaqueBoundsCacheEntry> g_iconBoundsCache;
std::vector<RegionHit> g_regionHits;
std::vector<std::wstring> g_selectedSourcePaths;
std::vector<std::wstring> g_selectedRegionIds;
llp::input::TextBuffer g_searchInput;
llp::accessibility::SearchAccessibility g_searchAccessibility;
llp::interaction::SpotlightState g_spotlightState;
llp::interaction::LaunchpadState g_launchpadState;
bool g_searchInputActive = false;
bool g_searchPointerSelecting = false;
int g_searchTextOffset = 0;
int g_selectedFilteredIndex = -1;
int g_scrollOffset = 0;
int g_contentHeight = 0;
std::wstring g_layoutViewMode = L"InlineRegions";
bool g_mouseDown = false;
int g_hoveredFilteredIndex = -1;
int g_pressedFilteredIndex = -1;
bool g_dragActive = false;
DragMode g_dragMode = DragMode::None;
int g_dragFilteredIndex = -1;
std::wstring g_pendingRegionDragId;
std::wstring g_regionDragId;
std::vector<std::wstring> g_dragSourcePaths;
POINT g_dragStart = {};
POINT g_dragCurrent = {};
DropTarget g_dropTarget;
DropTarget g_pendingDropTarget;
DWORD g_pendingDropTargetSince = 0;
RegionDropTarget g_regionDropTarget;
bool g_selectionBoxActive = false;
bool g_selectionBoxAdditive = false;
POINT g_selectionBoxStart = {};
POINT g_selectionBoxCurrent = {};
std::vector<DragVisualOffset> g_dragVisualOffsets;
std::vector<std::wstring> g_contextItemPaths;
std::wstring g_contextRegionId;
std::vector<std::wstring> g_contextRegionIds;
int g_modalDialogDepth = 0;
uint64_t g_iconUseCounter = 0;
std::vector<int> g_filterScores;
ID2D1Factory* g_d2dFactory = nullptr;
ID2D1DCRenderTarget* g_d2dDcRenderTarget = nullptr;
IDWriteFactory* g_dwriteFactory = nullptr;
IDWriteTextFormat* g_searchTextFormat = nullptr;
IDWriteTextFormat* g_tileTextFormat = nullptr;
IDWriteTextFormat* g_headerTextFormat = nullptr;
ID2D1Bitmap* g_spotlightBackdropD2dBitmap = nullptr;
HFONT g_uiFont = nullptr;
bool g_spotlightLayerBasePass = false;
BYTE g_spotlightPaintOpacity = 255;
bool g_spotlightLayerDirty = true;
SpotlightLayerBuffer g_spotlightLayerBuffer;
SpotlightBackdropCapture g_spotlightBackdrop;
UINT g_taskbarCreatedMessage = 0;
bool g_trayIconAdded = false;
bool g_hotkeyRegistered = false;
int g_activeHotkeyId = HotkeyId;
bool g_diagnosticsUiMode = false;
std::wstring g_textInputProbePath;
bool g_searchAccessibilityReady = false;
bool g_imeCandidatePositionApplied = false;
bool g_textInputProbeWindowWasVisible = false;
POINT g_lastImeCandidatePoint = {};
ULONGLONG g_processStartTick = 0;
ULONGLONG g_settingsLoadedTick = 0;
ULONGLONG g_shellReadyTick = 0;
ULONGLONG g_catalogStartTick = 0;
bool g_catalogStarted = false;
int g_trayAddAttempts = 0;
int g_trayRetryAttempts = 0;
int g_taskbarRecoveryCount = 0;
std::wstring g_startupLogPath;
std::wstring g_startupProbePath;
std::wstring g_settingsStatus = L"No settings changes in this session.";
bool g_settingsRecoveredFromBackup = false;
bool g_settingsMigrated = false;
std::wstring g_settingsPersistenceError;
bool g_layoutRecoveredFromBackup = false;
bool g_layoutMigrated = false;
std::wstring g_layoutPersistenceError;
std::wstring g_layoutQuarantinePath;
std::wstring g_searchHistoryStatus = L"Search history has not loaded yet.";
bool g_searchHistoryWritable = true;
bool g_searchHistoryLoaded = false;
llp::shell::ShellBroker g_shellBroker;
llp::search::Index g_searchIndex;
llp::search::History g_searchHistory;
uint64_t g_catalogGeneration = 0;
uint64_t g_requestedCatalogGeneration = 0;
bool g_catalogRefreshPending = false;
std::wstring g_catalogFolderIdentity;
std::wstring g_catalogStatus = L"Catalog has not loaded yet.";
DWORD g_catalogBrokerThreadId = 0;

struct CatalogBrokerProbeState
{
    bool active = false;
    int stage = 0;
    DWORD delayMilliseconds = 0;
    std::wstring outputPath;
    ULONGLONG inputPostedTick = 0;
    uint64_t inputResponseMilliseconds = 0;
    size_t cachedItemCountBeforeRefresh = 0;
    size_t cachedItemCountDuringRefresh = 0;
    std::wstring cachedStableIdBeforeRefresh;
    uint64_t refreshMilliseconds = 0;
    DWORD brokerThreadId = 0;
    std::wstring apartment;
    bool shutdownPersistenceQueued = false;
    bool shutdownHistoryPersistenceQueued = false;
};

struct ShellImportProbeState
{
    bool active = false;
    int stage = 0;
    int repeat = 2;
    int firstImported = 0;
    int secondImported = 0;
    int firstSkippedReparsePoints = 0;
    std::wstring outputPath;
    std::wstring root;
    std::wstring destination;
    DWORD brokerThreadId = 0;
    std::wstring apartment;
};

struct SpotlightFlowProbeState
{
    bool active = false;
    bool readyWritten = false;
    std::wstring outputPath;
    std::wstring readyPath;
    int hotkeyMessages = 0;
    int searchEnterLaunches = 0;
    int resultEnterLaunches = 0;
    int successfulLaunches = 0;
    int searchFocusedOnOpen = 0;
    std::vector<int> selectedIndexOnOpen;
    std::vector<uint64_t> inputReadyMilliseconds;
    std::vector<std::wstring> requestedPaths;
    std::vector<std::wstring> focusedResultPaths;
};

struct LaunchpadFlowProbeState
{
    bool active = false;
    bool readyWritten = false;
    bool blockedBrowseDragRecorded = false;
    std::wstring outputPath;
    std::wstring readyPath;
    int hotkeyMessages = 0;
    int modeTransitions = 0;
    int browsePresses = 0;
    int browseClickLaunches = 0;
    int editSelectionClicks = 0;
    int maxSelectedItems = 0;
    int marqueeBegins = 0;
    int itemDragBegins = 0;
    int regionDragBegins = 0;
    int blockedBrowseDrags = 0;
    int successfulLaunches = 0;
    int lastPointerX = -1;
    int lastPointerY = -1;
    int lastHitCount = 0;
    int lastMatchedIndex = -1;
    std::vector<std::wstring> requestedPaths;
};

CatalogBrokerProbeState g_catalogBrokerProbe;
ShellImportProbeState g_shellImportProbe;
SpotlightFlowProbeState g_spotlightFlowProbe;
LaunchpadFlowProbeState g_launchpadFlowProbe;

bool ShowTextInputDialog(const std::wstring& title, const std::wstring& label, const std::wstring& initialValue, std::wstring& result);
bool RegisterCurrentHotkey();
bool SaveLayout();
void RequestCatalogRefresh(DWORD testDelayMilliseconds = 0);
void QueueItemIcon(LaunchItem& item);
void ShowSettingsWindow();
void ShowWorkspaceContextMenu(POINT point);
void RebuildFiltered();
void DestroyDirectRenderer();
void RemoveTrayIcon();
bool IsSpotlightBaseLayerPass();
void ActivateSearchInput();
bool InitializeDirectRenderer();
bool IsSpotlightMode();
bool PaintSpotlightLayeredWindow(bool repaint = true);
void ReleaseSpotlightLayerBuffer();
void ReleaseSpotlightBackdrop();
bool CaptureSpotlightBackdrop(const RECT& windowRect);
ID2D1Bitmap* GetSpotlightBackdropD2dBitmap(ID2D1DCRenderTarget* target);
void RebuildSearchIndex();
void DrawSearchSurface(HDC dc, const RECT& client);
void DrawSearchSurface(ID2D1DCRenderTarget* target, const RECT& client);
int CalculateColumnCount(const RECT& client);
std::wstring NormalizeHotkeyText(const std::wstring& value);
void RequestSearchRepaint(HWND hwnd, bool erase);
void ResetDragState();
void UpdateImeWindowPosition(HWND hwnd);

template <typename T>
void SafeRelease(T*& value)
{
    if (value)
    {
        value->Release();
        value = nullptr;
    }
}

void CopyText(wchar_t* dest, size_t count, const wchar_t* source)
{
    if (count == 0) return;
    lstrcpynW(dest, source ? source : L"", static_cast<int>(count));
}

std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool ContainsIgnoreCase(const std::wstring& text, const std::wstring& query)
{
    return ToLower(text).find(ToLower(query)) != std::wstring::npos;
}

bool StartsWithIgnoreCase(const std::wstring& text, const std::wstring& prefix)
{
    if (prefix.size() > text.size()) return false;
    return CompareStringOrdinal(text.c_str(), static_cast<int>(prefix.size()), prefix.c_str(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

std::wstring Trim(std::wstring value)
{
    while (!value.empty() && std::iswspace(value.front())) value.erase(value.begin());
    while (!value.empty() && std::iswspace(value.back())) value.pop_back();
    return value;
}

bool IsChineseLanguage(const std::wstring& language)
{
    return ContainsIgnoreCase(language, L"Chinese") || ContainsIgnoreCase(language, L"中文") || ContainsIgnoreCase(language, L"zh");
}

const wchar_t* Text(const wchar_t* english, const wchar_t* chinese)
{
    return IsChineseLanguage(g_settings.language) ? chinese : english;
}

std::wstring FormatDouble(double value)
{
    wchar_t buffer[32] = {};
    std::swprintf(buffer, ARRAYSIZE(buffer), L"%.2f", value);
    std::wstring text = buffer;
    while (text.find(L'.') != std::wstring::npos && !text.empty() && text.back() == L'0') text.pop_back();
    if (!text.empty() && text.back() == L'.') text.pop_back();
    return text;
}

int SearchTop()
{
    return 34;
}

int SearchBottom()
{
    return 84;
}

int ContentClipTop()
{
    return SearchBottom() + 16;
}

RECT ContentClipRect(const RECT& client)
{
    const int sideInset = IsSpotlightMode() ? SpotlightContentInset : 0;
    const int bottomInset = IsSpotlightMode() ? SpotlightContentBottomInset : 0;
    return {
        static_cast<LONG>(sideInset),
        static_cast<LONG>(ContentClipTop()),
        std::max<LONG>(static_cast<LONG>(sideInset), client.right - static_cast<LONG>(sideInset)),
        std::max<LONG>(static_cast<LONG>(ContentClipTop()), client.bottom - static_cast<LONG>(bottomInset))
    };
}

RECT SearchRect(const RECT& client)
{
    const int sideInset = IsSpotlightMode() ? SpotlightSearchSideInset : 48;
    const int right = IsSpotlightMode() ? client.right - sideInset : client.right - 148;
    return { sideInset, SearchTop(), std::max<LONG>(sideInset, right), SearchBottom() };
}

RECT SearchTextRect(const RECT& searchRect)
{
    return { searchRect.left + 22, searchRect.top, searchRect.right - 22, searchRect.bottom };
}

RECT SpotlightAuxiliaryRect(const RECT& client)
{
    return { client.right - 76, client.bottom - 60, client.right - 28, client.bottom - 20 };
}

RECT LaunchpadEditRect(const RECT& client)
{
    return { client.right - 132, SearchTop(), client.right - 48, SearchBottom() };
}

std::wstring SearchCommittedText()
{
    return g_searchInput.Text();
}

std::wstring SearchDisplayText()
{
    return g_searchInput.DisplayText();
}

int TileSize()
{
    return std::max(112, g_settings.iconSize + 72);
}

int TileGap()
{
    return std::clamp(g_settings.appSpacing * 2, 0, 56);
}

int WheelScrollStep()
{
    return static_cast<int>(std::round(96.0 * g_settings.wheelSensitivity));
}

bool EndsWithIgnoreCase(const std::wstring& value, const wchar_t* suffix)
{
    const auto suffixLength = lstrlenW(suffix);
    if (static_cast<int>(value.size()) < suffixLength) return false;
    return CompareStringOrdinal(
        value.c_str() + value.size() - suffixLength,
        suffixLength,
        suffix,
        suffixLength,
        TRUE) == CSTR_EQUAL;
}

std::wstring GetFileStem(const std::wstring& path)
{
    const wchar_t* fileName = PathFindFileNameW(path.c_str());
    std::wstring stem = fileName ? fileName : path;
    const auto dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos)
    {
        stem.resize(dot);
    }
    return stem;
}

std::wstring GetAppDataPath()
{
    wchar_t appData[MAX_PATH] = L"";
    DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return L"";
    return appData;
}

std::wstring GetUserProfilePath()
{
    wchar_t profile[MAX_PATH] = L"";
    DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return L"";
    return profile;
}

std::wstring GetProgramDataPath()
{
    wchar_t programData[MAX_PATH] = L"";
    DWORD length = GetEnvironmentVariableW(L"PROGRAMDATA", programData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    return programData;
}

std::wstring JoinPath(const std::wstring& left, const wchar_t* right)
{
    std::wstring result = left;
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/')
    {
        result += L"\\";
    }
    result += right;
    return result;
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), wide.data(), count);
    return wide;
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string utf8(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), utf8.data(), count, nullptr, nullptr);
    return utf8;
}

int NormalizeSpotlightWidth(int value)
{
    return value <= 0 ? 980 : std::clamp(value, 520, 1600);
}

int NormalizeSpotlightHeight(int value)
{
    return value <= 0 ? 720 : std::clamp(value, 420, 1000);
}

int NormalizeAppSpacing(int value)
{
    return std::clamp(value, 0, 28);
}

double NormalizeWheelSensitivity(double value)
{
    if (!std::isfinite(value) || value <= 0) return 1.0;
    return std::clamp(value, 0.5, 3.0);
}

std::string EscapeJson(const std::wstring& value)
{
    std::string result;
    for (wchar_t ch : value)
    {
        switch (ch)
        {
        case L'"': result += "\\\""; break;
        case L'\\': result += "\\\\"; break;
        case L'\b': result += "\\b"; break;
        case L'\f': result += "\\f"; break;
        case L'\n': result += "\\n"; break;
        case L'\r': result += "\\r"; break;
        case L'\t': result += "\\t"; break;
        default:
            if (ch < 0x20)
            {
                char buffer[8] = {};
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(ch));
                result += buffer;
            }
            else
            {
                result += WideToUtf8(std::wstring(1, ch));
            }
            break;
        }
    }
    return result;
}

void AppendPersistenceError(std::wstring& target, const std::wstring& message)
{
    if (message.empty()) return;
    if (target.empty())
    {
        target = message;
    }
    else if (target.find(message) == std::wstring::npos)
    {
        target += L" " + message;
    }
}

bool TryGetJsonInt(const Json& value, int& result)
{
    if (value.is_number_unsigned())
    {
        const auto number = value.get<uint64_t>();
        if (number > static_cast<uint64_t>(std::numeric_limits<int>::max())) return false;
        result = static_cast<int>(number);
        return true;
    }
    if (!value.is_number_integer()) return false;
    const auto number = value.get<int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) return false;
    result = static_cast<int>(number);
    return true;
}

std::wstring GetLightLaunchpadDataPath()
{
    const auto appData = GetAppDataPath();
    return appData.empty() ? L"" : JoinPath(appData, L"LightLaunchpad");
}

std::wstring GetLayoutPath()
{
    const auto dataPath = GetLightLaunchpadDataPath();
    return dataPath.empty() ? L"" : llp::persistence::ScopedLayoutPath(dataPath, g_settings.launchpadFolder);
}

std::wstring GetSettingsPath()
{
    const auto dataPath = GetLightLaunchpadDataPath();
    return dataPath.empty() ? L"" : JoinPath(dataPath, L"settings.json");
}

std::wstring GetSearchHistoryPath()
{
    const auto dataPath = GetLightLaunchpadDataPath();
    return dataPath.empty() ? L"" : JoinPath(dataPath, L"search-history.json");
}

bool EnsureParentDirectory(const std::wstring& path)
{
    if (path.empty()) return false;
    wchar_t directory[MAX_PATH] = L"";
    CopyText(directory, ARRAYSIZE(directory), path.c_str());
    if (!PathRemoveFileSpecW(directory) || directory[0] == L'\0') return true;
    const int result = SHCreateDirectoryExW(nullptr, directory, nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

bool WriteFileUtf8(const std::wstring& path, const std::string& content)
{
    if (path.empty()) return false;
    if (!EnsureParentDirectory(path)) return false;

    const auto temporaryPath = path + L".tmp";
    HANDLE file = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    const bool ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr)
        && written == content.size();
    CloseHandle(file);
    if (!ok)
    {
        DeleteFileW(temporaryPath.c_str());
        return false;
    }

    if (MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
    {
        DeleteFileW(temporaryPath.c_str());
        return false;
    }
    return true;
}

void LoadSettings()
{
    const auto profile = GetUserProfilePath();
    Settings loaded;
    loaded.launchpadFolder = profile.empty() ? L"Launchpad" : JoinPath(profile, L"Launchpad");
    g_settingsRecoveredFromBackup = false;
    g_settingsMigrated = false;
    g_settingsPersistenceError.clear();

    const auto loadResult = llp::persistence::LoadJsonWithRecovery(GetSettingsPath());
    g_settingsRecoveredFromBackup = loadResult.recoveredFromBackup;
    g_settingsPersistenceError = loadResult.error;
    if (!loadResult.document.is_object())
    {
        g_settings = loaded;
        return;
    }

    const Json& document = loadResult.document;
    int schemaVersion = 1;
    if (document.contains("SchemaVersion") && !TryGetJsonInt(document["SchemaVersion"], schemaVersion))
    {
        g_settingsPersistenceError = L"Settings SchemaVersion must be a supported integer.";
        g_settings = loaded;
        return;
    }
    if (schemaVersion < 1 || schemaVersion > llp::persistence::CurrentSchemaVersion)
    {
        g_settingsPersistenceError = L"Unsupported settings schema version " + std::to_wstring(schemaVersion) + L".";
        g_settings = loaded;
        return;
    }
    g_settingsMigrated = schemaVersion < llp::persistence::CurrentSchemaVersion;

    auto readString = [&](const char* key, const std::wstring& fallback) {
        if (!document.contains(key)) return fallback;
        if (!document[key].is_string())
        {
            AppendPersistenceError(g_settingsPersistenceError, L"Settings field " + llp::persistence::Utf8ToWide(key) + L" must be a string.");
            return fallback;
        }
        const auto value = llp::persistence::Utf8ToWide(document[key].get<std::string>());
        if (value.empty())
        {
            AppendPersistenceError(g_settingsPersistenceError, L"Settings field " + llp::persistence::Utf8ToWide(key) + L" must not be empty.");
            return fallback;
        }
        return value;
    };
    auto readInt = [&](const char* key, int fallback) {
        if (!document.contains(key)) return fallback;
        int value = fallback;
        if (!TryGetJsonInt(document[key], value))
        {
            AppendPersistenceError(g_settingsPersistenceError, L"Settings field " + llp::persistence::Utf8ToWide(key) + L" must be an in-range integer.");
            return fallback;
        }
        return value;
    };
    auto readDouble = [&](const char* key, double fallback) {
        if (!document.contains(key)) return fallback;
        if (!document[key].is_number())
        {
            AppendPersistenceError(g_settingsPersistenceError, L"Settings field " + llp::persistence::Utf8ToWide(key) + L" must be a number.");
            return fallback;
        }
        return document[key].get<double>();
    };
    auto readBool = [&](const char* key, bool fallback) {
        if (!document.contains(key)) return fallback;
        if (!document[key].is_boolean())
        {
            AppendPersistenceError(g_settingsPersistenceError, L"Settings field " + llp::persistence::Utf8ToWide(key) + L" must be a boolean.");
            return fallback;
        }
        return document[key].get<bool>();
    };

    loaded.launchpadFolder = readString("LaunchpadFolder", loaded.launchpadFolder);
    loaded.hotkey = NormalizeHotkeyText(readString("Hotkey", loaded.hotkey));
    loaded.displayMode = readString("DisplayMode", loaded.displayMode);
    loaded.language = readString("Language", loaded.language);
    const auto iconSize = readString("IconSize", L"Medium");
    if (ContainsIgnoreCase(iconSize, L"Large")) loaded.iconSize = 72;
    else if (ContainsIgnoreCase(iconSize, L"Small")) loaded.iconSize = 42;
    else loaded.iconSize = 56;
    loaded.spotlightWidth = NormalizeSpotlightWidth(readInt("SpotlightWidth", loaded.spotlightWidth));
    loaded.spotlightHeight = NormalizeSpotlightHeight(readInt("SpotlightHeight", loaded.spotlightHeight));
    loaded.appSpacing = NormalizeAppSpacing(readInt("AppSpacing", loaded.appSpacing));
    loaded.wheelSensitivity = NormalizeWheelSensitivity(readDouble("WheelSensitivity", loaded.wheelSensitivity));
    loaded.startWithWindows = readBool("StartWithWindows", loaded.startWithWindows);
    loaded.resetSearchOnOpen = readBool("ResetSearchOnOpen", loaded.resetSearchOnOpen);
    loaded.showRecentAndFavorites = readBool("ShowRecentAndFavorites", loaded.showRecentAndFavorites);
    loaded.trackLaunchHistory = readBool("TrackLaunchHistory", loaded.trackLaunchHistory);
    g_settings = loaded;
    g_searchHistory.SetTrackingEnabled(g_settings.trackLaunchHistory);
}

std::wstring IconSizeName()
{
    if (g_settings.iconSize <= 44) return L"Small";
    if (g_settings.iconSize >= 64) return L"Large";
    return L"Medium";
}

bool SaveSettings(const Settings& settings)
{
    const std::wstring iconSizeName = settings.iconSize <= 44 ? L"Small" : (settings.iconSize >= 64 ? L"Large" : L"Medium");
    Json document = {
        { "SchemaVersion", llp::persistence::CurrentSchemaVersion },
        { "LaunchpadFolder", llp::persistence::WideToUtf8(settings.launchpadFolder) },
        { "Hotkey", llp::persistence::WideToUtf8(settings.hotkey) },
        { "StartWithWindows", settings.startWithWindows },
        { "IconSize", llp::persistence::WideToUtf8(iconSizeName) },
        { "ViewMode", llp::persistence::WideToUtf8(g_layoutViewMode) },
        { "IconQuality", "High" },
        { "DisplayMode", llp::persistence::WideToUtf8(settings.displayMode) },
        { "Language", llp::persistence::WideToUtf8(settings.language) },
        { "SpotlightWidth", settings.spotlightWidth },
        { "SpotlightHeight", settings.spotlightHeight },
        { "AppSpacing", settings.appSpacing },
        { "WheelSensitivity", settings.wheelSensitivity },
        { "ResetSearchOnOpen", settings.resetSearchOnOpen },
        { "ShowRecentAndFavorites", settings.showRecentAndFavorites },
        { "TrackLaunchHistory", settings.trackLaunchHistory }
    };
    std::wstring error;
    const bool saved = llp::persistence::WriteJsonAtomicWithBackup(GetSettingsPath(), document, error);
    if (!saved) g_settingsPersistenceError = error;
    return saved;
}

bool RegionExists(const std::wstring& id)
{
    return std::any_of(g_regions.begin(), g_regions.end(), [&](const Region& region) {
        return CompareStringOrdinal(region.id.c_str(), -1, id.c_str(), -1, TRUE) == CSTR_EQUAL;
    });
}

std::wstring RegionName(const std::wstring& id)
{
    auto found = std::find_if(g_regions.begin(), g_regions.end(), [&](const Region& region) {
        return CompareStringOrdinal(region.id.c_str(), -1, id.c_str(), -1, TRUE) == CSTR_EQUAL;
    });
    return found == g_regions.end() ? id : found->name;
}

int RegionOrder(const std::wstring& id)
{
    auto found = std::find_if(g_regions.begin(), g_regions.end(), [&](const Region& region) {
        return CompareStringOrdinal(region.id.c_str(), -1, id.c_str(), -1, TRUE) == CSTR_EQUAL;
    });
    return found == g_regions.end() ? 9999 : found->order;
}

bool SamePath(const std::wstring& left, const std::wstring& right)
{
    const auto normalizedLeft = llp::persistence::NormalizeFolderIdentity(left);
    const auto normalizedRight = llp::persistence::NormalizeFolderIdentity(right);
    return CompareStringOrdinal(normalizedLeft.c_str(), -1, normalizedRight.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool ContainsPath(const std::vector<std::wstring>& paths, const std::wstring& sourcePath)
{
    return std::any_of(paths.begin(), paths.end(), [&](const std::wstring& path) {
        return SamePath(path, sourcePath);
    });
}

void SetSingleSelection(const std::wstring& sourcePath)
{
    if (!IsSpotlightMode() && !g_launchpadState.AllowsSelection()) return;
    g_selectedSourcePaths.clear();
    if (!sourcePath.empty())
    {
        g_selectedSourcePaths.push_back(sourcePath);
    }
}

void ToggleSelection(const std::wstring& sourcePath)
{
    if (!IsSpotlightMode() && !g_launchpadState.AllowsSelection()) return;
    auto found = std::find_if(g_selectedSourcePaths.begin(), g_selectedSourcePaths.end(), [&](const std::wstring& path) {
        return SamePath(path, sourcePath);
    });
    if (found == g_selectedSourcePaths.end())
    {
        g_selectedSourcePaths.push_back(sourcePath);
    }
    else
    {
        g_selectedSourcePaths.erase(found);
    }
}

bool IsSelected(const std::wstring& sourcePath)
{
    return ContainsPath(g_selectedSourcePaths, sourcePath);
}

bool IsUncategorizedRegion(const std::wstring& regionId)
{
    return CompareStringOrdinal(regionId.c_str(), -1, L"uncategorized", -1, TRUE) == CSTR_EQUAL;
}

bool ContainsRegionId(const std::vector<std::wstring>& regionIds, const std::wstring& regionId)
{
    return std::any_of(regionIds.begin(), regionIds.end(), [&](const std::wstring& id) {
        return CompareStringOrdinal(id.c_str(), -1, regionId.c_str(), -1, TRUE) == CSTR_EQUAL;
    });
}

void SetSingleRegionSelection(const std::wstring& regionId)
{
    if (!g_launchpadState.AllowsSelection()) return;
    g_selectedRegionIds.clear();
    if (!regionId.empty())
    {
        g_selectedRegionIds.push_back(regionId);
    }
}

void ToggleRegionSelection(const std::wstring& regionId)
{
    if (!g_launchpadState.AllowsSelection()) return;
    auto found = std::find_if(g_selectedRegionIds.begin(), g_selectedRegionIds.end(), [&](const std::wstring& id) {
        return CompareStringOrdinal(id.c_str(), -1, regionId.c_str(), -1, TRUE) == CSTR_EQUAL;
    });
    if (found == g_selectedRegionIds.end())
    {
        g_selectedRegionIds.push_back(regionId);
    }
    else
    {
        g_selectedRegionIds.erase(found);
    }
}

bool IsRegionSelected(const std::wstring& regionId)
{
    return ContainsRegionId(g_selectedRegionIds, regionId);
}

std::vector<std::wstring> ResolveRegionContextIds(const std::wstring& primaryRegionId)
{
    if (IsRegionSelected(primaryRegionId) && !g_selectedRegionIds.empty())
    {
        return g_selectedRegionIds;
    }

    return { primaryRegionId };
}

std::vector<std::wstring> ResolveDragSourcePaths(const LaunchItem& primary)
{
    if (IsSelected(primary.sourcePath) && !g_selectedSourcePaths.empty())
    {
        return g_selectedSourcePaths;
    }

    return { primary.sourcePath };
}

void AddDefaultRegion()
{
    if (!RegionExists(L"uncategorized"))
    {
        g_regions.push_back({ L"uncategorized", L"Uncategorized", 0 });
    }
}

void SortItems()
{
    std::sort(g_items.begin(), g_items.end(), [](const LaunchItem& left, const LaunchItem& right) {
        const int leftRegion = RegionOrder(left.regionId);
        const int rightRegion = RegionOrder(right.regionId);
        if (leftRegion != rightRegion) return leftRegion < rightRegion;
        if (left.order != right.order) return left.order < right.order;
        return CompareStringOrdinal(left.displayName.c_str(), -1, right.displayName.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
}

std::vector<Region> OrderedRegions()
{
    auto regions = g_regions;
    std::sort(regions.begin(), regions.end(), [](const Region& left, const Region& right) {
        if (left.order != right.order) return left.order < right.order;
        return CompareStringOrdinal(left.name.c_str(), -1, right.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return regions;
}

int CountItemsInRegion(const std::wstring& regionId, const std::vector<std::wstring>& excludeSourcePaths)
{
    int count = 0;
    for (const auto& item : g_items)
    {
        if (ContainsPath(excludeSourcePaths, item.sourcePath)) continue;
        if (CompareStringOrdinal(item.regionId.c_str(), -1, regionId.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            count++;
        }
    }
    return count;
}

int CountItemsInRegion(const std::wstring& regionId, const std::wstring& excludeSourcePath = L"")
{
    return excludeSourcePath.empty()
        ? CountItemsInRegion(regionId, std::vector<std::wstring>{})
        : CountItemsInRegion(regionId, std::vector<std::wstring>{ excludeSourcePath });
}

int InsertionOrderForTargetItem(const LaunchItem& targetItem, bool insertAfter, const std::vector<std::wstring>& movingSourcePaths)
{
    int order = 0;
    for (const auto& item : g_items)
    {
        if (CompareStringOrdinal(item.regionId.c_str(), -1, targetItem.regionId.c_str(), -1, TRUE) != CSTR_EQUAL)
        {
            continue;
        }
        if (ContainsPath(movingSourcePaths, item.sourcePath))
        {
            continue;
        }
        if (SamePath(item.sourcePath, targetItem.sourcePath))
        {
            return order + (insertAfter ? 1 : 0);
        }
        order++;
    }

    return order;
}

void RenumberItemsByRegion()
{
    for (const auto& region : OrderedRegions())
    {
        int order = 0;
        for (auto& item : g_items)
        {
            if (CompareStringOrdinal(item.regionId.c_str(), -1, region.id.c_str(), -1, TRUE) == CSTR_EQUAL)
            {
                item.order = order++;
            }
        }
    }
}

bool SaveLayout()
{
    RenumberItemsByRegion();
    Json document = {
        { "SchemaVersion", llp::persistence::CurrentSchemaVersion },
        { "FolderIdentity", llp::persistence::WideToUtf8(llp::persistence::NormalizeFolderIdentity(g_settings.launchpadFolder)) },
        { "FolderPath", llp::persistence::WideToUtf8(g_settings.launchpadFolder) },
        { "ViewMode", llp::persistence::WideToUtf8(g_layoutViewMode) },
        { "Regions", Json::array() },
        { "Items", Json::array() }
    };
    const auto regions = OrderedRegions();
    for (const auto& region : regions)
    {
        document["Regions"].push_back({
            { "Id", llp::persistence::WideToUtf8(region.id) },
            { "Name", llp::persistence::WideToUtf8(region.name) },
            { "Order", region.order }
        });
    }
    auto appendItem = [&](const LaunchItem& item) {
        document["Items"].push_back({
            { "SourcePath", llp::persistence::WideToUtf8(item.sourcePath) },
            { "DisplayName", llp::persistence::WideToUtf8(item.displayName) },
            { "RegionId", llp::persistence::WideToUtf8(item.regionId) },
            { "Order", item.order }
        });
    };
    for (const auto& item : g_items) appendItem(item);
    for (const auto& item : g_missingLayoutItems)
    {
        const bool alreadyVisible = std::any_of(g_items.begin(), g_items.end(), [&](const LaunchItem& visible) {
            return SamePath(visible.sourcePath, item.sourcePath);
        });
        if (!alreadyVisible) appendItem(item);
    }
    if (g_hwnd && g_shellBroker.IsRunning())
    {
        const auto requestId = g_shellBroker.RequestPersistJson(GetLayoutPath(), document.dump());
        if (requestId == 0) g_layoutPersistenceError = L"The layout write could not be queued on the background broker.";
        return requestId != 0;
    }
    std::wstring error;
    const bool saved = llp::persistence::WriteJsonAtomicWithBackup(GetLayoutPath(), document, error);
    if (!saved) g_layoutPersistenceError = error;
    return saved;
}

bool SaveSearchHistory()
{
    if (!g_searchHistoryWritable)
    {
        g_searchHistoryStatus = L"Search history is read-only because its file could not be loaded safely; the existing file was preserved.";
        return false;
    }
    if (!g_shellBroker.IsRunning())
    {
        g_searchHistoryStatus = L"Shell broker is not running; search history was not queued.";
        return false;
    }
    const auto requestId = g_shellBroker.RequestPersistJson(GetSearchHistoryPath(), g_searchHistory.SerializeUtf8());
    g_searchHistoryStatus = requestId != 0
        ? L"Search history save queued on Shell STA."
        : L"Search history save queue failed.";
    return requestId != 0;
}

uint64_t CurrentUnixMilliseconds()
{
    FILETIME fileTime = {};
    GetSystemTimeAsFileTime(&fileTime);
    ULARGE_INTEGER ticks = {};
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    constexpr uint64_t WindowsToUnixEpochTicks = 116444736000000000ULL;
    return ticks.QuadPart <= WindowsToUnixEpochTicks ? 0 : (ticks.QuadPart - WindowsToUnixEpochTicks) / 10000ULL;
}

void MoveItemsToTarget(const std::vector<std::wstring>& sourcePaths, const DropTarget& target)
{
    if (!target.valid || !RegionExists(target.regionId)) return;
    if (sourcePaths.empty()) return;

    std::vector<LaunchItem> movingItems;
    std::vector<LaunchItem> remaining;
    remaining.reserve(g_items.size());
    for (const auto& item : g_items)
    {
        if (ContainsPath(sourcePaths, item.sourcePath))
        {
            auto moving = item;
            moving.regionId = target.regionId;
            movingItems.push_back(moving);
        }
        else
        {
            remaining.push_back(item);
        }
    }

    if (movingItems.empty()) return;

    std::vector<LaunchItem> reordered;
    reordered.reserve(g_items.size());
    for (const auto& region : OrderedRegions())
    {
        std::vector<LaunchItem> regionItems;
        for (const auto& item : remaining)
        {
            if (CompareStringOrdinal(item.regionId.c_str(), -1, region.id.c_str(), -1, TRUE) == CSTR_EQUAL)
            {
                regionItems.push_back(item);
            }
        }

        if (CompareStringOrdinal(region.id.c_str(), -1, target.regionId.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            const int insertAt = std::clamp(target.order, 0, static_cast<int>(regionItems.size()));
            regionItems.insert(regionItems.begin() + insertAt, movingItems.begin(), movingItems.end());
        }

        reordered.insert(reordered.end(), regionItems.begin(), regionItems.end());
    }

    g_items = std::move(reordered);
    RenumberItemsByRegion();
}

void MoveItemToTarget(const std::wstring& sourcePath, const DropTarget& target)
{
    MoveItemsToTarget(std::vector<std::wstring>{ sourcePath }, target);
}

void ForgetIconBounds(HICON icon)
{
    if (!icon) return;
    g_iconBoundsCache.erase(
        std::remove_if(g_iconBoundsCache.begin(), g_iconBoundsCache.end(), [&](const IconOpaqueBoundsCacheEntry& entry) {
            return entry.icon == icon;
        }),
        g_iconBoundsCache.end());
}

void ReleaseItemIcon(LaunchItem& item)
{
    if (!item.icon) return;
    ForgetIconBounds(item.icon);
    DestroyIcon(item.icon);
    item.icon = nullptr;
    item.iconLastUsed = 0;
}

void ReleaseAllResidentIcons()
{
    for (auto& item : g_items)
    {
        ReleaseItemIcon(item);
    }
    g_iconBoundsCache.clear();
}

void TrimResidentIcons(size_t targetCount = MaxResidentIconCount)
{
    size_t residentCount = 0;
    for (const auto& item : g_items)
    {
        if (item.icon) residentCount++;
    }
    if (residentCount <= targetCount) return;

    struct Candidate
    {
        size_t index = 0;
        uint64_t lastUsed = 0;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(residentCount);
    for (size_t i = 0; i < g_items.size(); ++i)
    {
        if (g_items[i].icon)
        {
            candidates.push_back({ i, g_items[i].iconLastUsed });
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return left.lastUsed < right.lastUsed;
    });

    size_t toRelease = residentCount - targetCount;
    for (const auto& candidate : candidates)
    {
        if (toRelease == 0) break;
        ReleaseItemIcon(g_items[candidate.index]);
        toRelease--;
    }
}

void DestroyDirectRenderer();

void TrimHiddenFootprint()
{
    ReleaseAllResidentIcons();
    ReleaseSpotlightLayerBuffer();
    ReleaseSpotlightBackdrop();
    DestroyDirectRenderer();
    CoFreeUnusedLibrariesEx(0, 0);
    HeapCompact(GetProcessHeap(), 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void MoveRegionToTarget(const std::wstring& sourceRegionId, const RegionDropTarget& target)
{
    if (sourceRegionId.empty() || !target.valid || !RegionExists(sourceRegionId) || !RegionExists(target.regionId)) return;
    if (CompareStringOrdinal(sourceRegionId.c_str(), -1, target.regionId.c_str(), -1, TRUE) == CSTR_EQUAL) return;

    auto regions = OrderedRegions();
    Region moving = {};
    bool found = false;
    std::vector<Region> remaining;
    remaining.reserve(regions.size());
    for (const auto& region : regions)
    {
        if (CompareStringOrdinal(region.id.c_str(), -1, sourceRegionId.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            moving = region;
            found = true;
        }
        else
        {
            remaining.push_back(region);
        }
    }

    if (!found) return;

    auto insert = std::find_if(remaining.begin(), remaining.end(), [&](const Region& region) {
        return CompareStringOrdinal(region.id.c_str(), -1, target.regionId.c_str(), -1, TRUE) == CSTR_EQUAL;
    });
    if (insert == remaining.end()) return;
    if (target.insertAfter) ++insert;
    remaining.insert(insert, moving);

    for (int i = 0; i < static_cast<int>(remaining.size()); ++i)
    {
        remaining[i].order = i;
    }
    g_regions = std::move(remaining);
}

void RemoveItemsFromLayout(const std::vector<std::wstring>& sourcePaths)
{
    if (sourcePaths.empty()) return;
    std::vector<LaunchItem> remaining;
    remaining.reserve(g_items.size());
    for (auto& item : g_items)
    {
        if (ContainsPath(sourcePaths, item.sourcePath))
        {
            ReleaseItemIcon(item);
            continue;
        }

        remaining.push_back(item);
    }

    g_items = std::move(remaining);
    for (const auto& sourcePath : sourcePaths)
    {
        auto selected = std::find_if(g_selectedSourcePaths.begin(), g_selectedSourcePaths.end(), [&](const std::wstring& path) {
            return SamePath(path, sourcePath);
        });
        if (selected != g_selectedSourcePaths.end())
        {
            g_selectedSourcePaths.erase(selected);
        }
    }
    RenumberItemsByRegion();
}

void DeleteRegionAndMoveItems(const std::wstring& regionId)
{
    if (regionId.empty() || IsUncategorizedRegion(regionId))
    {
        return;
    }

    for (auto& item : g_items)
    {
        if (CompareStringOrdinal(item.regionId.c_str(), -1, regionId.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            item.regionId = L"uncategorized";
        }
    }

    g_regions.erase(
        std::remove_if(g_regions.begin(), g_regions.end(), [&](const Region& region) {
            return CompareStringOrdinal(region.id.c_str(), -1, regionId.c_str(), -1, TRUE) == CSTR_EQUAL;
        }),
        g_regions.end());
    AddDefaultRegion();
    auto regions = OrderedRegions();
    for (int i = 0; i < static_cast<int>(regions.size()); ++i)
    {
        regions[i].order = i;
    }
    g_regions = std::move(regions);
    RenumberItemsByRegion();
}

std::wstring CreateRegionId()
{
    std::wstring id = L"region-" + std::to_wstring(GetTickCount64());
    int suffix = 2;
    while (RegionExists(id))
    {
        id = L"region-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(suffix++);
    }
    return id;
}

void CreateRegion()
{
    std::wstring name;
    if (!ShowTextInputDialog(Text(L"Create region", L"新建 region"), Text(L"Region name:", L"Region 名称:"), L"", name)) return;
    name = Trim(name);
    if (name.empty()) return;

    int maxOrder = -1;
    for (const auto& region : g_regions)
    {
        maxOrder = std::max(maxOrder, region.order);
    }

    g_regions.push_back({ CreateRegionId(), name, maxOrder + 1 });
    SaveLayout();
    RebuildFiltered();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void RenameItem(const std::wstring& sourcePath)
{
    auto found = std::find_if(g_items.begin(), g_items.end(), [&](const LaunchItem& item) {
        return SamePath(item.sourcePath, sourcePath);
    });
    if (found == g_items.end()) return;

    std::wstring name;
    if (!ShowTextInputDialog(Text(L"Rename app", L"重命名 APP"), Text(L"Display name:", L"显示名称:"), found->displayName, name)) return;
    name = Trim(name);
    if (name.empty()) return;

    found->displayName = name;
    RebuildSearchIndex();
    SaveLayout();
    RebuildFiltered();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void RenameRegion(const std::wstring& regionId)
{
    auto found = std::find_if(g_regions.begin(), g_regions.end(), [&](const Region& region) {
        return CompareStringOrdinal(region.id.c_str(), -1, regionId.c_str(), -1, TRUE) == CSTR_EQUAL;
    });
    if (found == g_regions.end()) return;

    std::wstring name;
    if (!ShowTextInputDialog(Text(L"Rename region", L"重命名 region"), Text(L"Region name:", L"Region 名称:"), found->name, name)) return;
    name = Trim(name);
    if (name.empty()) return;

    found->name = name;
    SaveLayout();
    RebuildFiltered();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void RenameSelectedRegions(const std::vector<std::wstring>& regionIds)
{
    if (regionIds.empty()) return;
    std::wstring name;
    if (!ShowTextInputDialog(Text(L"Rename selected regions", L"批量重命名 region"), Text(L"Region name:", L"Region 名称:"), RegionName(regionIds.front()), name)) return;
    name = Trim(name);
    if (name.empty()) return;

    for (auto& region : g_regions)
    {
        if (ContainsRegionId(regionIds, region.id))
        {
            region.name = name;
        }
    }
    SaveLayout();
    RebuildFiltered();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void DeleteSelectedRegions(const std::vector<std::wstring>& regionIds)
{
    for (const auto& regionId : regionIds)
    {
        if (!IsUncategorizedRegion(regionId))
        {
            DeleteRegionAndMoveItems(regionId);
        }
    }
    g_selectedRegionIds.erase(
        std::remove_if(g_selectedRegionIds.begin(), g_selectedRegionIds.end(), [&](const std::wstring& regionId) {
            return !RegionExists(regionId) || IsUncategorizedRegion(regionId);
        }),
        g_selectedRegionIds.end());
    SortItems();
    SaveLayout();
    RebuildFiltered();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void SelectAllItems()
{
    if (!g_launchpadState.AllowsSelection()) return;
    g_selectedSourcePaths.clear();
    for (int filteredIndex : g_filtered)
    {
        if (filteredIndex >= 0 && filteredIndex < static_cast<int>(g_items.size()))
        {
            g_selectedSourcePaths.push_back(g_items[filteredIndex].sourcePath);
        }
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ClearAllSelection()
{
    g_selectedSourcePaths.clear();
    g_selectedRegionIds.clear();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

RECT NormalizeRect(POINT start, POINT current)
{
    RECT rect = {
        std::min(start.x, current.x),
        std::min(start.y, current.y),
        std::max(start.x, current.x),
        std::max(start.y, current.y)
    };
    return rect;
}

bool RectsIntersect(const RECT& left, const RECT& right)
{
    RECT intersection = {};
    return IntersectRect(&intersection, &left, &right) != FALSE
        && intersection.right > intersection.left
        && intersection.bottom > intersection.top;
}

POINT SelectionContentPointFromClient(POINT point)
{
    point.y += g_scrollOffset;
    return point;
}

RECT SelectionViewportRect()
{
    POINT start = g_selectionBoxStart;
    POINT current = g_selectionBoxCurrent;
    start.y -= g_scrollOffset;
    current.y -= g_scrollOffset;
    return NormalizeRect(start, current);
}

RECT WinRect(const llp::geometry::Rect& rect)
{
    return { rect.left, rect.top, rect.right, rect.bottom };
}

llp::geometry::Scene BuildCurrentSceneGeometry(const RECT& client)
{
    llp::geometry::LayoutRequest request;
    request.clientWidth = static_cast<int>(client.right - client.left);
    request.tileSize = TileSize();
    request.gap = TileGap();
    request.left = IsSpotlightMode() ? SpotlightContentInset : 44;
    request.initialContentTop = 114;
    request.regionHeaderHeight = 38;
    request.scrollOffset = g_scrollOffset;
    request.groupByRegion = SearchCommittedText().empty();
    request.items.reserve(g_filtered.size());
    std::vector<std::wstring> shownRegionIds;
    for (int filteredIndex = 0; filteredIndex < static_cast<int>(g_filtered.size()); ++filteredIndex)
    {
        const int itemIndex = g_filtered[filteredIndex];
        if (itemIndex < 0 || itemIndex >= static_cast<int>(g_items.size())) continue;
        const auto& item = g_items[itemIndex];
        request.items.push_back({ filteredIndex, item.regionId });
        if (std::none_of(shownRegionIds.begin(), shownRegionIds.end(), [&](const std::wstring& id) {
            return CompareStringOrdinal(id.c_str(), -1, item.regionId.c_str(), -1, TRUE) == CSTR_EQUAL;
        }))
        {
            shownRegionIds.push_back(item.regionId);
        }
    }
    if (request.groupByRegion && !IsSpotlightMode())
    {
        for (const auto& region : OrderedRegions())
        {
            if (IsUncategorizedRegion(region.id)) continue;
            const bool shown = std::any_of(shownRegionIds.begin(), shownRegionIds.end(), [&](const std::wstring& id) {
                return CompareStringOrdinal(id.c_str(), -1, region.id.c_str(), -1, TRUE) == CSTR_EQUAL;
            });
            if (!shown) request.emptyRegionIds.push_back(region.id);
        }
    }
    return llp::geometry::BuildScene(request);
}

std::vector<HitTile> BuildContentHitTiles()
{
    std::vector<HitTile> hits;
    if (!g_hwnd) return hits;

    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const auto scene = BuildCurrentSceneGeometry(client);
    hits.reserve(scene.items.size());
    for (const auto& item : scene.items) hits.push_back({ WinRect(item.contentBounds), item.filteredIndex });

    return hits;
}

void BeginSelectionBox(HWND hwnd, POINT point, bool additive)
{
    if (!g_launchpadState.AllowsMarquee()) return;
    if (g_launchpadFlowProbe.active) g_launchpadFlowProbe.marqueeBegins++;
    g_selectionBoxActive = true;
    g_selectionBoxAdditive = additive;
    g_selectionBoxStart = SelectionContentPointFromClient(point);
    g_selectionBoxCurrent = g_selectionBoxStart;
    g_mouseDown = false;
    g_dragActive = false;
    g_dragMode = DragMode::None;
    g_dragFilteredIndex = -1;
    g_pendingRegionDragId.clear();
    if (!additive)
    {
        ClearAllSelection();
        g_selectedFilteredIndex = -1;
    }
    SetCapture(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void UpdateSelectionBox(POINT point)
{
    if (!g_selectionBoxActive) return;
    g_selectionBoxCurrent = SelectionContentPointFromClient(point);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void CompleteSelectionBox(HWND hwnd)
{
    if (!g_selectionBoxActive) return;

    const RECT selectionRect = NormalizeRect(g_selectionBoxStart, g_selectionBoxCurrent);
    int firstSelected = -1;
    for (const auto& hit : BuildContentHitTiles())
    {
        if (!RectsIntersect(selectionRect, hit.bounds)) continue;
        if (hit.itemIndex < 0 || hit.itemIndex >= static_cast<int>(g_filtered.size())) continue;
        const auto& item = g_items[g_filtered[hit.itemIndex]];
        if (!ContainsPath(g_selectedSourcePaths, item.sourcePath))
        {
            g_selectedSourcePaths.push_back(item.sourcePath);
        }
        if (firstSelected < 0)
        {
            firstSelected = hit.itemIndex;
        }
    }

    if (firstSelected >= 0)
    {
        g_selectedFilteredIndex = firstSelected;
    }

    g_selectionBoxActive = false;
    if (GetCapture() == hwnd)
    {
        ReleaseCapture();
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void CancelSelectionBox(HWND hwnd)
{
    if (!g_selectionBoxActive) return;
    g_selectionBoxActive = false;
    if (GetCapture() == hwnd)
    {
        ReleaseCapture();
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

LaunchItem LaunchItemFromCatalog(const llp::catalog::Item& source)
{
    LaunchItem item = { source.sourcePath, source.displayName, source.regionId, source.order, nullptr };
    item.stableId = source.stableId;
    item.aliases = source.aliases;
    item.subtitle = source.subtitle;
    return item;
}

void ApplyCatalogSnapshot(
    const llp::catalog::Snapshot& snapshot,
    const llp::search::History* history = nullptr,
    bool historyWritable = true)
{
    if (snapshot.generation < g_requestedCatalogGeneration) return;
    std::wstring focusedStableId;
    if (!IsSpotlightMode() && g_selectedFilteredIndex >= 0 && g_selectedFilteredIndex < static_cast<int>(g_filtered.size()))
    {
        const int focusedItemIndex = g_filtered[g_selectedFilteredIndex];
        if (focusedItemIndex >= 0 && focusedItemIndex < static_cast<int>(g_items.size())) focusedStableId = g_items[focusedItemIndex].stableId;
    }
    const int previousIconSize = g_settings.iconSize;
    std::vector<LaunchItem> previousItems = std::move(g_items);

    g_regions.clear();
    g_regions.reserve(snapshot.regions.size());
    for (const auto& region : snapshot.regions)
    {
        g_regions.push_back({ region.id, region.name, region.order });
    }
    g_items.clear();
    g_items.reserve(snapshot.items.size());
    for (const auto& source : snapshot.items) g_items.push_back(LaunchItemFromCatalog(source));
    g_missingLayoutItems.clear();
    g_missingLayoutItems.reserve(snapshot.missingItems.size());
    for (const auto& source : snapshot.missingItems) g_missingLayoutItems.push_back(LaunchItemFromCatalog(source));

    if (previousIconSize == g_settings.iconSize)
    {
        for (auto& item : g_items)
        {
            auto found = std::find_if(previousItems.begin(), previousItems.end(), [&](const LaunchItem& previous) {
                return previous.icon && previous.stableId == item.stableId;
            });
            if (found != previousItems.end())
            {
                item.icon = found->icon;
                item.iconLastUsed = found->iconLastUsed;
                found->icon = nullptr;
                found->iconLastUsed = 0;
            }
        }
    }
    for (auto& item : previousItems) ReleaseItemIcon(item);

    g_layoutViewMode = snapshot.viewMode;
    g_layoutRecoveredFromBackup = snapshot.recoveredFromBackup;
    g_layoutMigrated = snapshot.migratedLegacy;
    g_layoutQuarantinePath = snapshot.quarantinePath;
    g_layoutPersistenceError = snapshot.error;
    g_catalogGeneration = snapshot.generation;
    g_catalogFolderIdentity = snapshot.folderIdentity;
    g_catalogRefreshPending = false;
    if (history && !g_searchHistoryLoaded)
    {
        g_searchHistory = *history;
        g_searchHistoryWritable = historyWritable;
        g_searchHistoryLoaded = true;
        g_searchHistoryStatus = snapshot.error.empty()
            ? L"Local usage history loaded with catalog snapshot."
            : (historyWritable
                ? L"History loaded with catalog diagnostics; see Catalog status."
                : L"Search history is read-only; its existing file was preserved.");
    }
    g_searchHistory.SetTrackingEnabled(g_settings.trackLaunchHistory);
    g_catalogStatus = snapshot.error.empty()
        ? L"Catalog snapshot published in " + std::to_wstring(snapshot.loadMilliseconds) + L" ms."
        : L"Catalog snapshot published with diagnostics: " + snapshot.error;

    RebuildSearchIndex();
    RebuildFiltered();
    if (!IsSpotlightMode() && !focusedStableId.empty())
    {
        for (int filteredIndex = 0; filteredIndex < static_cast<int>(g_filtered.size()); ++filteredIndex)
        {
            if (g_items[g_filtered[filteredIndex]].stableId == focusedStableId)
            {
                g_selectedFilteredIndex = filteredIndex;
                break;
            }
        }
    }
    g_selectedSourcePaths.erase(
        std::remove_if(g_selectedSourcePaths.begin(), g_selectedSourcePaths.end(), [](const std::wstring& sourcePath) {
            return std::none_of(g_items.begin(), g_items.end(), [&](const LaunchItem& item) {
                return SamePath(item.sourcePath, sourcePath);
            });
        }),
        g_selectedSourcePaths.end());
    g_selectedRegionIds.erase(
        std::remove_if(g_selectedRegionIds.begin(), g_selectedRegionIds.end(), [](const std::wstring& regionId) {
            return !RegionExists(regionId);
        }),
        g_selectedRegionIds.end());
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, TRUE);
}

void RequestCatalogRefresh(DWORD testDelayMilliseconds)
{
    if (!g_shellBroker.IsRunning())
    {
        g_catalogStatus = L"Shell broker is not running; catalog refresh was not queued.";
        return;
    }
    g_catalogStarted = true;
    if (g_catalogStartTick == 0) g_catalogStartTick = GetTickCount64();
    const auto requestedIdentity = llp::persistence::NormalizeFolderIdentity(g_settings.launchpadFolder);
    if (!g_catalogFolderIdentity.empty()
        && CompareStringOrdinal(g_catalogFolderIdentity.c_str(), -1, requestedIdentity.c_str(), -1, TRUE) != CSTR_EQUAL)
    {
        ReleaseAllResidentIcons();
        g_items.clear();
        g_missingLayoutItems.clear();
        g_regions.clear();
        AddDefaultRegion();
        RebuildSearchIndex();
        RebuildFiltered();
    }
    const uint64_t generation = ++g_requestedCatalogGeneration;
    const auto requestId = g_shellBroker.RequestCatalog(
        GetLightLaunchpadDataPath(),
        g_settings.launchpadFolder,
        generation,
        testDelayMilliseconds);
    g_catalogRefreshPending = requestId != 0;
    g_catalogStatus = requestId != 0 ? L"Catalog refresh queued on Shell STA." : L"Catalog refresh queue failed.";
}

void ReloadData()
{
    RequestCatalogRefresh();
}

void QueueItemIcon(LaunchItem& item)
{
    if (item.icon)
    {
        item.iconLastUsed = ++g_iconUseCounter;
        return;
    }
    if (item.iconRequestPending || !g_shellBroker.IsRunning()) return;
    if (item.stableId.empty()) item.stableId = llp::catalog::StableItemId(item.sourcePath);
    item.iconRequestPending = g_shellBroker.RequestIcon(item.stableId, item.sourcePath, g_settings.iconSize) != 0;
}

void RebuildSearchIndex()
{
    std::vector<llp::search::Item> items;
    items.reserve(g_items.size());
    for (const auto& item : g_items)
    {
        llp::search::Item indexed;
        indexed.stableId = item.stableId;
        indexed.displayName = item.displayName;
        indexed.aliases = item.aliases;
        indexed.subtitle = item.subtitle;
        indexed.sourcePath = item.sourcePath;
        items.push_back(std::move(indexed));
    }
    g_searchHistory.SetTrackingEnabled(g_settings.trackLaunchHistory);
    g_searchIndex.Rebuild(items, g_searchHistory);
}

void RebuildFiltered()
{
    std::wstring focusedStableId;
    if (!IsSpotlightMode() && g_selectedFilteredIndex >= 0 && g_selectedFilteredIndex < static_cast<int>(g_filtered.size()))
    {
        const int itemIndex = g_filtered[g_selectedFilteredIndex];
        if (itemIndex >= 0 && itemIndex < static_cast<int>(g_items.size())) focusedStableId = g_items[itemIndex].stableId;
    }
    g_filtered.clear();
    g_filtered.reserve(g_items.size());
    g_filterScores.assign(g_items.size(), 0);
    const std::wstring query = SearchCommittedText();
    if (query.empty() && !IsSpotlightMode())
    {
        for (int itemIndex = 0; itemIndex < static_cast<int>(g_items.size()); ++itemIndex) g_filtered.push_back(itemIndex);
    }
    else
    {
        llp::search::QueryOptions options;
        options.spotlight = IsSpotlightMode();
        options.showRecentAndFavorites = g_settings.showRecentAndFavorites;
        options.emptyLimit = 10;
        const auto results = g_searchIndex.Query(query, options);
        for (const auto& result : results)
        {
            auto found = std::find_if(g_items.begin(), g_items.end(), [&](const LaunchItem& item) {
                return item.stableId == result.stableId;
            });
            if (found == g_items.end()) continue;
            const int itemIndex = static_cast<int>(found - g_items.begin());
            g_filterScores[itemIndex] = result.score;
            g_filtered.push_back(itemIndex);
        }
    }

    if (IsSpotlightMode())
    {
        std::vector<std::wstring> stableIds;
        stableIds.reserve(g_filtered.size());
        for (const int itemIndex : g_filtered)
        {
            stableIds.push_back(itemIndex >= 0 && itemIndex < static_cast<int>(g_items.size())
                ? g_items[itemIndex].stableId
                : std::wstring{});
        }
        g_spotlightState.ResultsChanged(stableIds);
        g_selectedFilteredIndex = g_spotlightState.FocusedResultIndex();
        return;
    }

    g_selectedFilteredIndex = -1;
    if (!focusedStableId.empty())
    {
        for (int filteredIndex = 0; filteredIndex < static_cast<int>(g_filtered.size()); ++filteredIndex)
        {
            if (g_items[g_filtered[filteredIndex]].stableId == focusedStableId)
            {
                g_selectedFilteredIndex = filteredIndex;
                break;
            }
        }
    }
    if (g_selectedFilteredIndex < 0 && !g_filtered.empty()) g_selectedFilteredIndex = 0;
}

HICON LoadEmbeddedAppIcon(int width = 0, int height = 0)
{
    HICON icon = reinterpret_cast<HICON>(
        LoadImageW(
            g_instance,
            AppIconResourceName,
            IMAGE_ICON,
            width,
            height,
            LR_DEFAULTSIZE | LR_SHARED));
    if (icon)
    {
        return icon;
    }

    return LoadIconW(nullptr, IDI_APPLICATION);
}

HICON LoadTrayIcon()
{
    return LoadEmbeddedAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
}

void PopulateTrayIconData(NOTIFYICONDATAW& nid)
{
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WmTray;
    nid.hIcon = LoadTrayIcon();
    CopyText(nid.szTip, ARRAYSIZE(nid.szTip), L"LightLaunchpad Native UI");
}

std::wstring CurrentExecutablePath()
{
    wchar_t exe[MAX_PATH] = L"";
    const DWORD length = GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
    if (length == 0 || length >= ARRAYSIZE(exe))
    {
        return {};
    }

    return exe;
}

bool ReadRegistryString(HKEY key, const wchar_t* name, std::wstring& value)
{
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS
        || (type != REG_SZ && type != REG_EXPAND_SZ)
        || bytes == 0)
    {
        return false;
    }

    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &bytes) != ERROR_SUCCESS)
    {
        return false;
    }

    while (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
    value = buffer;
    return true;
}

std::wstring StableInstallPath()
{
    wchar_t localAppData[MAX_PATH] = L"";
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, ARRAYSIZE(localAppData));
    if (length == 0 || length >= ARRAYSIZE(localAppData))
    {
        return {};
    }

    return JoinPath(localAppData, L"Programs\\LightLaunchpad\\LightLaunchpad.NativeUi.exe");
}

std::wstring StartupCommand()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return L"not configured";
    }

    std::wstring command;
    const bool found = ReadRegistryString(key, L"LightLaunchpad", command);
    RegCloseKey(key);
    return found ? command : L"not configured";
}

bool IsCurrentExecutableAtStableInstallPath()
{
    const auto executablePath = CurrentExecutablePath();
    const auto stablePath = StableInstallPath();
    return !executablePath.empty()
        && !stablePath.empty()
        && CompareStringOrdinal(executablePath.c_str(), -1, stablePath.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring RendererDiagnostics()
{
    if (!g_hwnd || (!g_d2dFactory && !IsWindowVisible(g_hwnd))) return L"not initialized";
    return g_d2dFactory ? L"Direct2D" : L"GDI fallback";
}

std::wstring DpiDiagnostics()
{
    UINT dpi = 0;
    if (g_hwnd)
    {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (getDpiForWindow)
        {
            dpi = getDpiForWindow(g_hwnd);
        }
    }

    return dpi > 0
        ? L"PerMonitorV2 / " + std::to_wstring(dpi) + L" DPI"
        : L"PerMonitorV2 (manifest; window not initialized)";
}

std::wstring MonitorDiagnostics()
{
    if (!g_hwnd) return L"not initialized";
    const HMONITOR monitor = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info))
    {
        return L"unavailable";
    }

    return info.szDevice;
}

std::wstring HotkeyDiagnostics()
{
    if (!g_hwnd) return L"not initialized";
    if (g_diagnosticsUiMode) return L"disabled in diagnostics UI";
    return g_hotkeyRegistered ? L"registered: " + g_settings.hotkey : L"registration failed: " + g_settings.hotkey;
}

std::wstring BuildId()
{
    return std::wstring(LLP_BUILD_CHANNEL_WIDE) + L":" + LLP_BUILD_COMMIT_WIDE;
}

std::string BuildDiagnosticsJson()
{
    const auto executablePath = CurrentExecutablePath();
    std::string json = "{\r\n";
    json += "  \"productName\": \"LightLaunchpad\",\r\n";
    json += "  \"version\": \"" + EscapeJson(LLP_BUILD_VERSION_WIDE) + "\",\r\n";
    json += "  \"commit\": \"" + EscapeJson(LLP_BUILD_COMMIT_WIDE) + "\",\r\n";
    json += "  \"channel\": \"" + EscapeJson(LLP_BUILD_CHANNEL_WIDE) + "\",\r\n";
    json += "  \"buildId\": \"" + EscapeJson(BuildId()) + "\",\r\n";
    json += "  \"singletonName\": \"" + EscapeJson(LLP_SINGLETON_NAME_WIDE) + "\",\r\n";
    json += "  \"windowClass\": \"" + EscapeJson(LLP_WINDOW_CLASS_NAME_WIDE) + "\",\r\n";
    json += "  \"executablePath\": \"" + EscapeJson(executablePath) + "\",\r\n";
    json += "  \"stableInstallPath\": \"" + EscapeJson(StableInstallPath()) + "\",\r\n";
    json += std::string("  \"isStableInstall\": ") + (IsCurrentExecutableAtStableInstallPath() ? "true" : "false") + ",\r\n";
    json += "  \"renderer\": \"" + EscapeJson(RendererDiagnostics()) + "\",\r\n";
    json += "  \"dpiAwareness\": \"" + EscapeJson(DpiDiagnostics()) + "\",\r\n";
    json += "  \"monitor\": \"" + EscapeJson(MonitorDiagnostics()) + "\",\r\n";
    json += "  \"startupPath\": \"" + EscapeJson(StartupCommand()) + "\",\r\n";
    json += "  \"hotkeyStatus\": \"" + EscapeJson(HotkeyDiagnostics()) + "\"\r\n";
    json += "}\r\n";
    return json;
}

std::string BuildPersistenceProbeJson(bool settingsSaved, bool layoutSaved)
{
    Json document;
    document["settings"] = {
        { "schemaVersion", llp::persistence::CurrentSchemaVersion },
        { "launchpadFolder", llp::persistence::WideToUtf8(g_settings.launchpadFolder) },
        { "hotkey", llp::persistence::WideToUtf8(g_settings.hotkey) },
        { "spotlightWidth", g_settings.spotlightWidth },
        { "migrated", g_settingsMigrated },
        { "recoveredFromBackup", g_settingsRecoveredFromBackup },
        { "error", llp::persistence::WideToUtf8(g_settingsPersistenceError) },
        { "saveSucceeded", settingsSaved }
    };
    document["layout"] = {
        { "schemaVersion", llp::persistence::CurrentSchemaVersion },
        { "path", llp::persistence::WideToUtf8(GetLayoutPath()) },
        { "folderIdentity", llp::persistence::WideToUtf8(llp::persistence::NormalizeFolderIdentity(g_settings.launchpadFolder)) },
        { "migratedLegacy", g_layoutMigrated },
        { "recoveredFromBackup", g_layoutRecoveredFromBackup },
        { "quarantinePath", llp::persistence::WideToUtf8(g_layoutQuarantinePath) },
        { "error", llp::persistence::WideToUtf8(g_layoutPersistenceError) },
        { "saveSucceeded", layoutSaved },
        { "regions", Json::array() },
        { "items", Json::array() },
        { "missingItems", Json::array() }
    };
    for (const auto& region : OrderedRegions())
    {
        document["layout"]["regions"].push_back({
            { "id", llp::persistence::WideToUtf8(region.id) },
            { "name", llp::persistence::WideToUtf8(region.name) },
            { "order", region.order }
        });
    }
    auto appendProbeItem = [](Json& target, const LaunchItem& item) {
        target.push_back({
            { "sourcePath", llp::persistence::WideToUtf8(item.sourcePath) },
            { "displayName", llp::persistence::WideToUtf8(item.displayName) },
            { "regionId", llp::persistence::WideToUtf8(item.regionId) },
            { "order", item.order }
        });
    };
    for (const auto& item : g_items) appendProbeItem(document["layout"]["items"], item);
    for (const auto& item : g_missingLayoutItems) appendProbeItem(document["layout"]["missingItems"], item);
    return document.dump(2) + "\n";
}

int RunPersistenceProbe(const std::wstring& outputPath, bool resave)
{
    LoadSettings();
    g_requestedCatalogGeneration = 1;
    ApplyCatalogSnapshot(llp::catalog::LoadSnapshot(GetLightLaunchpadDataPath(), g_settings.launchpadFolder, 1));
    const bool settingsSaved = !resave || SaveSettings(g_settings);
    const bool layoutSaved = !resave || SaveLayout();
    const bool probeSaved = WriteFileUtf8(outputPath, BuildPersistenceProbeJson(settingsSaved, layoutSaved));
    return settingsSaved && layoutSaved && probeSaved ? 0 : 2;
}

std::wstring BuildDiagnosticsSummary()
{
    std::wstring summary;
    summary += L"Version: " LLP_BUILD_VERSION_WIDE L"\r\n";
    summary += L"Commit: " LLP_BUILD_COMMIT_WIDE L"\r\n";
    summary += L"Channel: " LLP_BUILD_CHANNEL_WIDE L"\r\n";
    summary += L"Build ID: " + BuildId() + L"\r\n";
    summary += L"IPC class: " LLP_WINDOW_CLASS_NAME_WIDE L"\r\n";
    summary += L"Executable: " + CurrentExecutablePath() + L"\r\n";
    summary += L"Stable install: " + StableInstallPath() + L"\r\n";
    summary += std::wstring(L"Running stable: ") + (IsCurrentExecutableAtStableInstallPath() ? L"yes" : L"no") + L"\r\n";
    summary += L"Renderer: " + RendererDiagnostics() + L"\r\n";
    summary += L"DPI: " + DpiDiagnostics() + L"\r\n";
    summary += L"Monitor: " + MonitorDiagnostics() + L"\r\n";
    summary += L"Startup: " + StartupCommand() + L"\r\n";
    summary += L"Hotkey: " + HotkeyDiagnostics() + L"\r\n";
    summary += L"Settings status: " + g_settingsStatus + L"\r\n";
    summary += L"Catalog: " + g_catalogStatus + L"\r\n";
    summary += L"Catalog generation: " + std::to_wstring(g_catalogGeneration) + L"\r\n";
    summary += L"Recent/favorites visible: " + std::wstring(g_settings.showRecentAndFavorites ? L"yes" : L"no") + L"\r\n";
    summary += L"Launch history tracking: " + std::wstring(g_settings.trackLaunchHistory ? L"enabled" : L"disabled") + L"\r\n";
    summary += L"Search history: " + g_searchHistoryStatus + L"\r\n";
    summary += L"Shell broker thread: " + (g_catalogBrokerThreadId == 0 ? L"not published" : std::to_wstring(g_catalogBrokerThreadId));
    return summary;
}

std::wstring CommandLineValue(const wchar_t* name)
{
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments)
    {
        return {};
    }

    std::wstring outputPath;
    for (int index = 1; index + 1 < argumentCount; ++index)
    {
        if (lstrcmpiW(arguments[index], name) == 0)
        {
            outputPath = arguments[index + 1];
            break;
        }
    }
    LocalFree(arguments);
    return outputPath;
}

std::wstring DiagnosticsFileFromCommandLine()
{
    return CommandLineValue(L"--diagnostics-file");
}

bool CommandLineHasArgument(const wchar_t* expected)
{
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments)
    {
        return false;
    }

    bool found = false;
    for (int index = 1; index < argumentCount; ++index)
    {
        if (lstrcmpiW(arguments[index], expected) == 0)
        {
            found = true;
            break;
        }
    }
    LocalFree(arguments);
    return found;
}

int CommandLineIntValue(const wchar_t* name, int fallback, int minimum, int maximum)
{
    const auto value = CommandLineValue(name);
    if (value.empty()) return fallback;
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(value.c_str(), &end, 10);
    if (!end || *end != L'\0') return fallback;
    return std::clamp(static_cast<int>(parsed), minimum, maximum);
}

std::string BuildCatalogBrokerProbeJson()
{
    Json document = {
        { "brokerApartment", llp::persistence::WideToUtf8(g_catalogBrokerProbe.apartment) },
        { "uiThreadId", GetCurrentThreadId() },
        { "brokerThreadId", g_catalogBrokerProbe.brokerThreadId },
        { "cachedItemCountBeforeRefresh", g_catalogBrokerProbe.cachedItemCountBeforeRefresh },
        { "cachedItemCountDuringRefresh", g_catalogBrokerProbe.cachedItemCountDuringRefresh },
        { "cachedStableIdBeforeRefresh", llp::persistence::WideToUtf8(g_catalogBrokerProbe.cachedStableIdBeforeRefresh) },
        { "cachedStableIdAfterRefresh", g_items.empty() ? "" : llp::persistence::WideToUtf8(g_items.front().stableId) },
        { "inputEcho", llp::persistence::WideToUtf8(SearchCommittedText()) },
        { "inputResponseMs", g_catalogBrokerProbe.inputResponseMilliseconds },
        { "refreshMs", g_catalogBrokerProbe.refreshMilliseconds },
        { "catalogGeneration", g_catalogGeneration },
        { "shutdownPersistenceQueued", g_catalogBrokerProbe.shutdownPersistenceQueued },
        { "shutdownHistoryPersistenceQueued", g_catalogBrokerProbe.shutdownHistoryPersistenceQueued }
    };
    return document.dump(2) + "\n";
}

std::string BuildTextInputProbeJson()
{
    Json document = {
        { "text", llp::persistence::WideToUtf8(g_searchInput.Text()) },
        { "displayText", llp::persistence::WideToUtf8(g_searchInput.DisplayText()) },
        { "caret", g_searchInput.Caret() },
        { "anchor", g_searchInput.Anchor() },
        { "selectionStart", g_searchInput.SelectionStart() },
        { "selectionEnd", g_searchInput.SelectionEnd() },
        { "composing", g_searchInput.IsComposing() },
        { "candidatePositionApplied", g_imeCandidatePositionApplied },
        { "candidateX", g_lastImeCandidatePoint.x },
        { "candidateY", g_lastImeCandidatePoint.y },
        { "accessibilityReady", g_searchAccessibilityReady },
        { "windowVisible", g_textInputProbeWindowWasVisible }
    };
    return document.dump(2) + "\n";
}

std::string BuildSpotlightFlowProbeJson()
{
    Json requestedPaths = Json::array();
    for (const auto& path : g_spotlightFlowProbe.requestedPaths) requestedPaths.push_back(llp::persistence::WideToUtf8(path));
    Json focusedResultPaths = Json::array();
    for (const auto& path : g_spotlightFlowProbe.focusedResultPaths) focusedResultPaths.push_back(llp::persistence::WideToUtf8(path));
    Json document = {
        { "configuredHotkey", llp::persistence::WideToUtf8(g_settings.hotkey) },
        { "hotkeyMessages", g_spotlightFlowProbe.hotkeyMessages },
        { "searchFocusedOnOpen", g_spotlightFlowProbe.searchFocusedOnOpen },
        { "selectedIndexOnOpen", g_spotlightFlowProbe.selectedIndexOnOpen },
        { "inputReadyMs", g_spotlightFlowProbe.inputReadyMilliseconds },
        { "searchEnterLaunches", g_spotlightFlowProbe.searchEnterLaunches },
        { "resultEnterLaunches", g_spotlightFlowProbe.resultEnterLaunches },
        { "successfulLaunches", g_spotlightFlowProbe.successfulLaunches },
        { "requestedPaths", requestedPaths },
        { "focusedResultPaths", focusedResultPaths },
        { "finalFocus", g_spotlightState.Focus() == llp::interaction::FocusArea::Search
            ? "search"
            : (g_spotlightState.Focus() == llp::interaction::FocusArea::Results ? "results" : "auxiliary") },
        { "finalFocusedIndex", g_spotlightState.FocusedResultIndex() }
    };
    return document.dump(2) + "\n";
}

std::string BuildLaunchpadFlowProbeJson()
{
    Json requestedPaths = Json::array();
    for (const auto& path : g_launchpadFlowProbe.requestedPaths) requestedPaths.push_back(llp::persistence::WideToUtf8(path));
    Json document = {
        { "configuredHotkey", llp::persistence::WideToUtf8(g_settings.hotkey) },
        { "hotkeyMessages", g_launchpadFlowProbe.hotkeyMessages },
        { "modeTransitions", g_launchpadFlowProbe.modeTransitions },
        { "browsePresses", g_launchpadFlowProbe.browsePresses },
        { "browseClickLaunches", g_launchpadFlowProbe.browseClickLaunches },
        { "editSelectionClicks", g_launchpadFlowProbe.editSelectionClicks },
        { "maxSelectedItems", g_launchpadFlowProbe.maxSelectedItems },
        { "marqueeBegins", g_launchpadFlowProbe.marqueeBegins },
        { "itemDragBegins", g_launchpadFlowProbe.itemDragBegins },
        { "regionDragBegins", g_launchpadFlowProbe.regionDragBegins },
        { "blockedBrowseDrags", g_launchpadFlowProbe.blockedBrowseDrags },
        { "successfulLaunches", g_launchpadFlowProbe.successfulLaunches },
        { "requestedPaths", requestedPaths },
        { "finalMode", g_launchpadState.IsEditing() ? "edit" : "browse" },
        { "finalSelectedItems", g_selectedSourcePaths.size() }
    };
    return document.dump(2) + "\n";
}

bool WriteSpotlightFlowReadyState()
{
    if (!g_spotlightFlowProbe.active || g_spotlightFlowProbe.readyPath.empty()) return false;
    std::string focus = "search";
    if (g_spotlightState.Focus() == llp::interaction::FocusArea::Results) focus = "results";
    else if (g_spotlightState.Focus() == llp::interaction::FocusArea::Auxiliary) focus = "auxiliary";
    std::wstring focusedResultPath;
    if (g_selectedFilteredIndex >= 0 && g_selectedFilteredIndex < static_cast<int>(g_filtered.size()))
    {
        const int itemIndex = g_filtered[g_selectedFilteredIndex];
        if (itemIndex >= 0 && itemIndex < static_cast<int>(g_items.size())) focusedResultPath = g_items[itemIndex].sourcePath;
    }
    const Json ready = {
        { "itemCount", g_items.size() },
        { "filteredCount", g_filtered.size() },
        { "displayMode", llp::persistence::WideToUtf8(g_settings.displayMode) },
        { "catalogGeneration", g_catalogGeneration },
        { "focus", focus },
        { "focusedResultPath", llp::persistence::WideToUtf8(focusedResultPath) }
    };
    return WriteFileUtf8(g_spotlightFlowProbe.readyPath, ready.dump(2) + "\n");
}

bool WriteLaunchpadFlowReadyState()
{
    if (!g_launchpadFlowProbe.active || g_launchpadFlowProbe.readyPath.empty()) return false;
    const Json ready = {
        { "itemCount", g_items.size() },
        { "filteredCount", g_filtered.size() },
        { "displayMode", llp::persistence::WideToUtf8(g_settings.displayMode) },
        { "catalogGeneration", g_catalogGeneration },
        { "lastPointerX", g_launchpadFlowProbe.lastPointerX },
        { "lastPointerY", g_launchpadFlowProbe.lastPointerY },
        { "lastHitCount", g_launchpadFlowProbe.lastHitCount },
        { "lastMatchedIndex", g_launchpadFlowProbe.lastMatchedIndex },
        { "browsePresses", g_launchpadFlowProbe.browsePresses },
        { "browseClickLaunches", g_launchpadFlowProbe.browseClickLaunches },
        { "requestedPaths", g_launchpadFlowProbe.requestedPaths.size() }
    };
    return WriteFileUtf8(g_launchpadFlowProbe.readyPath, ready.dump(2) + "\n");
}

std::string BuildShellImportProbeJson()
{
    Json document = {
        { "brokerApartment", llp::persistence::WideToUtf8(g_shellImportProbe.apartment) },
        { "brokerThreadId", g_shellImportProbe.brokerThreadId },
        { "firstImported", g_shellImportProbe.firstImported },
        { "secondImported", g_shellImportProbe.secondImported },
        { "skippedReparsePoints", g_shellImportProbe.firstSkippedReparsePoints }
    };
    return document.dump(2) + "\n";
}

void HandleShellBrokerResult(LPARAM lParam)
{
    std::unique_ptr<llp::shell::Result> result(reinterpret_cast<llp::shell::Result*>(lParam));
    if (!result) return;
    g_catalogBrokerThreadId = result->brokerThreadId;
    switch (result->kind)
    {
    case llp::shell::ResultKind::Catalog:
        if (g_catalogBrokerProbe.active)
        {
            g_catalogBrokerProbe.brokerThreadId = result->brokerThreadId;
            g_catalogBrokerProbe.apartment = result->apartment;
            if (g_catalogBrokerProbe.stage == 0)
            {
                if (!result->snapshot) break;
                ApplyCatalogSnapshot(*result->snapshot, result->history.get(), result->historyWritable);
                g_catalogBrokerProbe.cachedItemCountBeforeRefresh = g_items.size();
                if (!g_items.empty()) g_catalogBrokerProbe.cachedStableIdBeforeRefresh = g_items.front().stableId;
                g_catalogBrokerProbe.stage = 1;
                PostMessageW(g_hwnd, WM_HOTKEY, static_cast<WPARAM>(g_activeHotkeyId), 0);
            }
            else
            {
                g_catalogBrokerProbe.refreshMilliseconds = result->elapsedMilliseconds;
                if (!result->snapshot) break;
                ApplyCatalogSnapshot(*result->snapshot, result->history.get(), result->historyWritable);
                if (!g_items.empty()) g_items.front().displayName = L"Shutdown Flush Sentinel";
                g_catalogBrokerProbe.shutdownPersistenceQueued = SaveLayout();
                g_catalogBrokerProbe.shutdownHistoryPersistenceQueued = SaveSearchHistory();
                const bool saved = WriteFileUtf8(g_catalogBrokerProbe.outputPath, BuildCatalogBrokerProbeJson());
                g_catalogBrokerProbe.active = false;
                DestroyWindow(g_hwnd);
                if (!saved) PostQuitMessage(2);
            }
        }
        else
        {
            if (result->snapshot)
            {
                ApplyCatalogSnapshot(*result->snapshot, result->history.get(), result->historyWritable);
                if (g_spotlightFlowProbe.active) g_spotlightFlowProbe.readyWritten = WriteSpotlightFlowReadyState();
                if (g_launchpadFlowProbe.active) g_launchpadFlowProbe.readyWritten = WriteLaunchpadFlowReadyState();
            }
        }
        break;
    case llp::shell::ResultKind::Icon:
    {
        auto found = std::find_if(g_items.begin(), g_items.end(), [&](const LaunchItem& item) {
            return item.stableId == result->stableId;
        });
        if (found != g_items.end())
        {
            found->iconRequestPending = false;
            if (result->icon && result->iconSize == g_settings.iconSize)
            {
                ReleaseItemIcon(*found);
                found->icon = result->icon;
                found->iconLastUsed = ++g_iconUseCounter;
                result->icon = nullptr;
                InvalidateRect(g_hwnd, nullptr, FALSE);
            }
            else if (result->iconSize != g_settings.iconSize)
            {
                QueueItemIcon(*found);
            }
        }
        if (result->icon) DestroyIcon(result->icon);
        break;
    }
    case llp::shell::ResultKind::ImportTree:
        if (g_shellImportProbe.active)
        {
            g_shellImportProbe.brokerThreadId = result->brokerThreadId;
            g_shellImportProbe.apartment = result->apartment;
            if (g_shellImportProbe.stage == 0)
            {
                g_shellImportProbe.firstImported = result->imported;
                g_shellImportProbe.firstSkippedReparsePoints = result->skippedReparsePoints;
                g_shellImportProbe.stage = 1;
                g_shellBroker.RequestImportTree(g_shellImportProbe.root, g_shellImportProbe.destination);
            }
            else
            {
                g_shellImportProbe.secondImported = result->imported;
                const bool saved = WriteFileUtf8(g_shellImportProbe.outputPath, BuildShellImportProbeJson());
                g_shellImportProbe.active = false;
                DestroyWindow(g_hwnd);
                if (!saved) PostQuitMessage(2);
            }
        }
        else
        {
            RequestCatalogRefresh();
            if (!result->succeeded && !result->error.empty())
            {
                MessageBoxW(g_hwnd, result->error.c_str(), L"LightLaunchpad import", MB_ICONWARNING | MB_OK);
            }
        }
        break;
    case llp::shell::ResultKind::Recycle:
        if (!result->affectedPaths.empty())
        {
            RemoveItemsFromLayout(result->affectedPaths);
            g_missingLayoutItems.erase(
                std::remove_if(g_missingLayoutItems.begin(), g_missingLayoutItems.end(), [&](const LaunchItem& item) {
                    return ContainsPath(result->affectedPaths, item.sourcePath);
                }),
                g_missingLayoutItems.end());
            SortItems();
            SaveLayout();
            RebuildFiltered();
            InvalidateRect(g_hwnd, nullptr, TRUE);
        }
        RequestCatalogRefresh();
        if (!result->succeeded && !result->error.empty())
        {
            MessageBoxW(g_hwnd, result->error.c_str(), L"LightLaunchpad Shell operation", MB_ICONWARNING | MB_OK);
        }
        break;
    case llp::shell::ResultKind::Launch:
        if (g_spotlightFlowProbe.active && result->succeeded) g_spotlightFlowProbe.successfulLaunches++;
        if (g_launchpadFlowProbe.active && result->succeeded) g_launchpadFlowProbe.successfulLaunches++;
        if (result->succeeded && !result->stableId.empty() && g_settings.trackLaunchHistory)
        {
            g_searchHistory.RecordLaunch(result->stableId, CurrentUnixMilliseconds());
            RebuildSearchIndex();
            RebuildFiltered();
            SaveSearchHistory();
        }
        if (!result->succeeded && !result->error.empty())
        {
            MessageBoxW(g_hwnd, result->error.c_str(), L"LightLaunchpad Shell operation", MB_ICONWARNING | MB_OK);
        }
        break;
    case llp::shell::ResultKind::OpenFolder:
    case llp::shell::ResultKind::OpenLocation:
        if (!result->succeeded && !result->error.empty())
        {
            MessageBoxW(g_hwnd, result->error.c_str(), L"LightLaunchpad Shell operation", MB_ICONWARNING | MB_OK);
        }
        break;
    case llp::shell::ResultKind::Persistence:
        if (CompareStringOrdinal(result->persistencePath.c_str(), -1, GetSearchHistoryPath().c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            g_searchHistoryStatus = result->succeeded ? L"Search history saved atomically." : result->error;
            if (!result->succeeded)
            {
                MessageBoxW(g_hwnd, result->error.c_str(), L"LightLaunchpad search history", MB_ICONWARNING | MB_OK);
            }
            break;
        }
        if (result->succeeded)
        {
            g_layoutPersistenceError.clear();
        }
        else
        {
            g_layoutPersistenceError = result->error;
            MessageBoxW(g_hwnd, result->error.c_str(), L"LightLaunchpad layout save", MB_ICONWARNING | MB_OK);
        }
        break;
    }
}

std::wstring DefaultStartupLogPath()
{
    wchar_t localAppData[MAX_PATH] = L"";
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, ARRAYSIZE(localAppData));
    if (length == 0 || length >= ARRAYSIZE(localAppData)) return {};
    return JoinPath(JoinPath(JoinPath(localAppData, L"LightLaunchpad"), L"logs"), L"apphost.log");
}

void AppendStartupLog(const wchar_t* stage, bool succeeded, const std::wstring& detail = {})
{
    if (g_startupLogPath.empty()) return;
    const ULONGLONG elapsed = g_processStartTick == 0 ? 0 : GetTickCount64() - g_processStartTick;
    std::string line = "{\"elapsedMs\":" + std::to_string(elapsed)
        + ",\"stage\":\"" + EscapeJson(stage)
        + "\",\"success\":" + (succeeded ? "true" : "false")
        + ",\"detail\":\"" + EscapeJson(detail) + "\"}\r\n";
    if (line.size() > MaxStartupLogBytes) line.resize(MaxStartupLogBytes);
    if (!EnsureParentDirectory(g_startupLogPath)) return;

    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    ULONGLONG existingSize = 0;
    if (GetFileAttributesExW(g_startupLogPath.c_str(), GetFileExInfoStandard, &attributes))
    {
        existingSize = (static_cast<ULONGLONG>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
    }
    const bool resetLog = existingSize + line.size() > MaxStartupLogBytes;
    HANDLE file = CreateFileW(
        g_startupLogPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        resetLog ? CREATE_ALWAYS : OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    if (!resetLog) SetFilePointer(file, 0, nullptr, FILE_END);
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
}

std::string BuildStartupProbeJson()
{
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG shellReadyMs = g_shellReadyTick == 0 ? now - g_processStartTick : g_shellReadyTick - g_processStartTick;
    const ULONGLONG settingsLoadedMs = g_settingsLoadedTick == 0 ? 0 : g_settingsLoadedTick - g_processStartTick;
    const bool catalogStartedBeforeShell = g_catalogStarted
        && g_catalogStartTick != 0
        && (g_shellReadyTick == 0 || g_catalogStartTick <= g_shellReadyTick);
    std::string json = "{\r\n";
    json += "  \"processRole\": \"primary\",\r\n";
    json += std::string("  \"windowCreated\": ") + (g_hwnd ? "true" : "false") + ",\r\n";
    json += std::string("  \"catalogStartedBeforeShell\": ") + (catalogStartedBeforeShell ? "true" : "false") + ",\r\n";
    json += "  \"settingsLoadedMs\": " + std::to_string(settingsLoadedMs) + ",\r\n";
    json += "  \"shellReadyMs\": " + std::to_string(shellReadyMs) + ",\r\n";
    json += "  \"trayAddAttempts\": " + std::to_string(g_trayAddAttempts) + ",\r\n";
    json += std::string("  \"trayAdded\": ") + (g_trayIconAdded ? "true" : "false") + ",\r\n";
    json += std::string("  \"hotkeyRegistered\": ") + (g_hotkeyRegistered ? "true" : "false") + ",\r\n";
    json += "  \"taskbarRecoveryCount\": " + std::to_string(g_taskbarRecoveryCount) + ",\r\n";
    json += "  \"logPath\": \"" + EscapeJson(g_startupLogPath) + "\"\r\n";
    json += "}\r\n";
    return json;
}

enum class SecondaryActivationResult
{
    Activated,
    VersionOrChannelMismatch,
    Failed
};

bool WriteActivationReport(const std::wstring& path, const wchar_t* status, ULONGLONG elapsedMs)
{
    if (path.empty()) return true;
    std::string json = "{\r\n";
    json += "  \"status\": \"" + EscapeJson(status) + "\",\r\n";
    json += "  \"requestBuildId\": \"" + EscapeJson(BuildId()) + "\",\r\n";
    json += "  \"channel\": \"" + EscapeJson(LLP_BUILD_CHANNEL_WIDE) + "\",\r\n";
    json += "  \"windowClass\": \"" + EscapeJson(LLP_WINDOW_CLASS_NAME_WIDE) + "\",\r\n";
    json += "  \"elapsedMs\": " + std::to_string(elapsedMs) + "\r\n";
    json += "}\r\n";
    return WriteFileUtf8(path, json);
}

SecondaryActivationResult ActivatePrimaryInstance(const std::wstring& reportPath)
{
    const ULONGLONG start = GetTickCount64();
    HWND target = nullptr;
    while (GetTickCount64() - start < ActivationIpcTimeoutMs)
    {
        target = FindWindowW(LLP_WINDOW_CLASS_NAME_WIDE, nullptr);
        if (target) break;
        Sleep(25);
    }

    if (!target)
    {
        const auto elapsed = GetTickCount64() - start;
        WriteActivationReport(reportPath, L"activation-failed", elapsed);
        AppendStartupLog(L"secondary-activation", false, L"primary window not found");
        return SecondaryActivationResult::Failed;
    }

    const std::wstring payload = BuildId();
    COPYDATASTRUCT copyData = {};
    copyData.dwData = ActivationCopyDataId;
    copyData.cbData = static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t));
    copyData.lpData = const_cast<wchar_t*>(payload.c_str());
    DWORD_PTR response = 0;
    const LRESULT delivered = SendMessageTimeoutW(
        target,
        WM_COPYDATA,
        0,
        reinterpret_cast<LPARAM>(&copyData),
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        ActivationIpcTimeoutMs,
        &response);
    const auto elapsed = GetTickCount64() - start;
    if (!delivered || (response != 1 && response != 2))
    {
        WriteActivationReport(reportPath, L"activation-failed", elapsed);
        AppendStartupLog(L"secondary-activation", false, L"IPC timed out or was rejected");
        return SecondaryActivationResult::Failed;
    }

    if (response == 2)
    {
        WriteActivationReport(reportPath, L"version-or-channel-mismatch", elapsed);
        AppendStartupLog(L"secondary-activation", true, L"version or channel mismatch reported");
        return SecondaryActivationResult::VersionOrChannelMismatch;
    }

    WriteActivationReport(reportPath, L"activated", elapsed);
    AppendStartupLog(L"secondary-activation", true, L"existing instance activated");
    return SecondaryActivationResult::Activated;
}

bool AddTrayIcon()
{
    ++g_trayAddAttempts;
    ++g_trayRetryAttempts;
    NOTIFYICONDATAW nid = {};
    PopulateTrayIconData(nid);
    g_trayIconAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (g_trayIconAdded)
    {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        KillTimer(g_hwnd, TrayIconRetryTimerId);
        AppendStartupLog(L"tray-added", true, L"attempt=" + std::to_wstring(g_trayRetryAttempts));
        return true;
    }

    AppendStartupLog(L"tray-add", false, L"attempt=" + std::to_wstring(g_trayRetryAttempts));
    if (g_hwnd && g_trayRetryAttempts < TrayIconRetryMaxAttempts)
    {
        SetTimer(g_hwnd, TrayIconRetryTimerId, TrayIconRetryMs, nullptr);
    }
    else if (g_hwnd)
    {
        KillTimer(g_hwnd, TrayIconRetryTimerId);
        AppendStartupLog(L"tray-retry-exhausted", false, std::to_wstring(TrayIconRetryMaxAttempts));
    }
    return false;
}

void RemoveTrayIcon()
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayIconAdded = false;
}

void ReaddTrayIcon()
{
    RemoveTrayIcon();
    AddTrayIcon();
}

std::wstring JoinHotkeyParts(const std::vector<std::wstring>& parts)
{
    std::wstring value;
    for (const auto& part : parts)
    {
        if (!value.empty()) value += L"+";
        value += part;
    }
    return value;
}

std::wstring HotkeyKeyName(WPARAM virtualKey)
{
    if (virtualKey >= L'A' && virtualKey <= L'Z')
    {
        return std::wstring(1, static_cast<wchar_t>(virtualKey));
    }

    if (virtualKey >= L'0' && virtualKey <= L'9')
    {
        return std::wstring(1, static_cast<wchar_t>(virtualKey));
    }

    return {};
}

std::wstring NormalizeHotkeyText(const std::wstring& value)
{
    const std::wstring trimmed = Trim(value);
    const std::wstring lowered = ToLower(trimmed);
    std::vector<std::wstring> parts;
    if (lowered.find(L"ctrl") != std::wstring::npos || lowered.find(L"control") != std::wstring::npos) parts.push_back(L"Ctrl");
    if (lowered.find(L"alt") != std::wstring::npos) parts.push_back(L"Alt");
    if (lowered.find(L"shift") != std::wstring::npos) parts.push_back(L"Shift");
    if (lowered.find(L"win") != std::wstring::npos || lowered.find(L"windows") != std::wstring::npos) parts.push_back(L"Win");

    const auto plus = trimmed.find_last_of(L'+');
    const std::wstring keyText = Trim(plus == std::wstring::npos ? trimmed : trimmed.substr(plus + 1));
    if (keyText.size() == 1)
    {
        const wchar_t key = static_cast<wchar_t>(std::towupper(keyText[0]));
        if ((key >= L'A' && key <= L'Z') || (key >= L'0' && key <= L'9'))
        {
            parts.push_back(std::wstring(1, key));
        }
    }

    if (parts.empty() || parts.back().size() != 1)
    {
        return L"Alt+D";
    }

    if (parts.size() == 1)
    {
        parts.insert(parts.begin(), L"Alt");
    }

    return JoinHotkeyParts(parts);
}

std::wstring FormatHotkeyFromKeyPress(WPARAM virtualKey)
{
    const std::wstring keyName = HotkeyKeyName(virtualKey);
    if (keyName.empty())
    {
        return {};
    }

    std::vector<std::wstring> parts;
    if (GetKeyState(VK_CONTROL) & 0x8000) parts.push_back(L"Ctrl");
    if (GetKeyState(VK_MENU) & 0x8000) parts.push_back(L"Alt");
    if (GetKeyState(VK_SHIFT) & 0x8000) parts.push_back(L"Shift");
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) parts.push_back(L"Win");
    if (parts.empty())
    {
        return {};
    }

    parts.push_back(keyName);
    return JoinHotkeyParts(parts);
}

void ResolveHotkey(const std::wstring& hotkey, UINT& modifiers, UINT& key)
{
    const auto hotkeyText = NormalizeHotkeyText(hotkey);
    auto normalized = ToLower(hotkeyText);
    modifiers = 0;
    key = L'D';
    if (normalized.find(L"alt") != std::wstring::npos) modifiers |= MOD_ALT;
    if (normalized.find(L"ctrl") != std::wstring::npos || normalized.find(L"control") != std::wstring::npos) modifiers |= MOD_CONTROL;
    if (normalized.find(L"shift") != std::wstring::npos) modifiers |= MOD_SHIFT;
    if (normalized.find(L"win") != std::wstring::npos || normalized.find(L"windows") != std::wstring::npos) modifiers |= MOD_WIN;
    const auto plus = hotkeyText.find_last_of(L'+');
    wchar_t candidate = plus != std::wstring::npos && plus + 1 < hotkeyText.size()
        ? static_cast<wchar_t>(std::towupper(hotkeyText[plus + 1]))
        : L'D';
    if ((candidate >= L'A' && candidate <= L'Z') || (candidate >= L'0' && candidate <= L'9'))
    {
        key = candidate;
    }
    if (modifiers == 0) modifiers = MOD_ALT;
}

bool RegisterCurrentHotkey()
{
    g_hotkeyRegistered = false;
    g_activeHotkeyId = HotkeyId;
    if (!g_hwnd) return false;
    UINT modifiers = 0;
    UINT key = 0;
    ResolveHotkey(g_settings.hotkey, modifiers, key);
    modifiers |= MOD_NOREPEAT;
    g_hotkeyRegistered = RegisterHotKey(g_hwnd, g_activeHotkeyId, modifiers, key) != FALSE;
    if (!g_hotkeyRegistered)
    {
        g_settingsStatus = L"Hotkey registration failed at startup for " + g_settings.hotkey
            + L" (Windows error " + std::to_wstring(GetLastError()) + L").";
    }
    return g_hotkeyRegistered;
}

void ActivateSearchInput()
{
    const bool gainedFocus = !g_searchInputActive || GetFocus() != g_hwnd;
    if (IsSpotlightMode())
    {
        g_spotlightState.FocusSearch();
        g_selectedFilteredIndex = -1;
    }
    else g_selectedFilteredIndex = -1;
    g_searchInputActive = true;
    if (g_hwnd)
    {
        SetFocus(g_hwnd);
        UpdateImeWindowPosition(g_hwnd);
    }
    if (gainedFocus) g_searchAccessibility.NotifyFocusChanged();
}

void ResetSearchForOpen()
{
    if (!g_settings.resetSearchOnOpen)
    {
        return;
    }

    g_searchInput.Reset(L"");
    g_scrollOffset = 0;
    g_selectedFilteredIndex = -1;
}

std::wstring ReadImeCompositionString(HWND hwnd, DWORD index)
{
    HIMC context = ImmGetContext(hwnd);
    if (!context)
    {
        return {};
    }

    const LONG byteCount = ImmGetCompositionStringW(context, index, nullptr, 0);
    if (byteCount <= 0)
    {
        ImmReleaseContext(hwnd, context);
        return {};
    }

    std::wstring text(static_cast<size_t>(byteCount / sizeof(wchar_t)), L'\0');
    ImmGetCompositionStringW(context, index, text.data(), byteCount);
    ImmReleaseContext(hwnd, context);
    return text;
}

LONG ReadImeCompositionCursor(HWND hwnd)
{
    HIMC context = ImmGetContext(hwnd);
    if (!context) return 0;
    const LONG cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
    ImmReleaseContext(hwnd, context);
    return std::max<LONG>(0, cursor);
}

void UpdateSearchComposition(HWND hwnd, LPARAM compositionFlags)
{
    ActivateSearchInput();

    if (compositionFlags & GCS_RESULTSTR)
    {
        const std::wstring result = ReadImeCompositionString(hwnd, GCS_RESULTSTR);
        if (!g_searchInput.IsComposing()) g_searchInput.BeginComposition();
        if (g_searchInput.CommitComposition(result))
        {
            g_scrollOffset = 0;
            RebuildFiltered();
        }
        RequestSearchRepaint(hwnd, true);
        g_searchAccessibility.NotifyTextChanged();
        g_searchAccessibility.NotifySelectionChanged();
        return;
    }

    if (compositionFlags & GCS_COMPSTR)
    {
        const auto composition = ReadImeCompositionString(hwnd, GCS_COMPSTR);
        g_searchInput.UpdateComposition(composition, static_cast<size_t>(ReadImeCompositionCursor(hwnd)));
        RebuildFiltered();
        RequestSearchRepaint(hwnd, false);
        g_searchAccessibility.NotifyTextChanged();
        g_searchAccessibility.NotifySelectionChanged();
    }
}

bool IsSpotlightMode()
{
    return ContainsIgnoreCase(g_settings.displayMode, L"Spotlight");
}

bool ApplyDwmRoundedCorners()
{
    if (!g_hwnd) return false;
    const DWORD cornerPreference = IsSpotlightMode() ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
    return SUCCEEDED(DwmSetWindowAttribute(
        g_hwnd,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &cornerPreference,
        sizeof(cornerPreference)));
}

void SetSpotlightLayeredOpacity(BYTE alpha)
{
    if (!g_hwnd) return;

    LONG_PTR style = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    if (IsSpotlightMode())
    {
        if ((style & WS_EX_LAYERED) == 0)
        {
            SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED);
        }
        g_spotlightPaintOpacity = SpotlightGlassAlpha > 0
            ? static_cast<BYTE>(std::clamp(static_cast<int>(std::round(alpha * 255.0 / SpotlightGlassAlpha)), 0, 255))
            : alpha;
        if (IsWindowVisible(g_hwnd))
        {
            PaintSpotlightLayeredWindow(false);
        }
        return;
    }

    if (style & WS_EX_LAYERED)
    {
        SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
    }
}

bool IsSpotlightBaseLayerPass()
{
    return g_spotlightLayerBasePass;
}

void ApplyGlassBackdrop()
{
    if (!g_hwnd) return;

    const bool spotlight = IsSpotlightMode();
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto setWindowCompositionAttribute = user32
        ? reinterpret_cast<SetWindowCompositionAttributeFn>(GetProcAddress(user32, "SetWindowCompositionAttribute"))
        : nullptr;
    if (setWindowCompositionAttribute)
    {
        AccentPolicy policy = {};
        policy.accentState = spotlight ? ACCENT_DISABLED : ACCENT_ENABLE_ACRYLICBLURBEHIND;
        policy.accentFlags = spotlight ? 0 : 2;
        policy.gradientColor = spotlight ? 0 : 0xC4282520;
        WindowCompositionAttribData data = {};
        data.attribute = 19;
        data.data = &policy;
        data.dataSize = sizeof(policy);
        setWindowCompositionAttribute(g_hwnd, &data);
    }

    DWM_BLURBEHIND blur = {};
    blur.dwFlags = DWM_BB_ENABLE;
    blur.fEnable = spotlight ? FALSE : TRUE;
    DwmEnableBlurBehindWindow(g_hwnd, &blur);

    const DWORD backdrop = spotlight ? DWMSBT_NONE : DWMSBT_TRANSIENTWINDOW;
    DwmSetWindowAttribute(g_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    const DWORD borderColor = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
    ApplyDwmRoundedCorners();
    SetSpotlightLayeredOpacity(spotlight ? SpotlightGlassAlpha : 255);
}

void ApplySpotlightWindowRegion(int width, int height)
{
    if (!g_hwnd) return;

    ApplyDwmRoundedCorners();
    // Keep the real window rectangular, then make the glass exterior transparent with a color key.
    SetWindowRgn(g_hwnd, nullptr, TRUE);

    RECT client = { 0, 0, std::max(1, width), std::max(1, height) };
    InvalidateRect(g_hwnd, &client, FALSE);
}


void BoxBlurHorizontalPass(DWORD* src, DWORD* dst, int width, int height, int radius)
{
    if (radius <= 0) return;
    const int diameter = radius * 2 + 1;
    for (int y = 0; y < height; ++y)
    {
        const int rowOffset = y * width;
        uint64_t r = 0, g = 0, b = 0, a = 0;
        int count = 0;
        for (int x = -radius; x <= radius; ++x)
        {
            const int clamped = std::clamp(x, 0, width - 1);
            const DWORD pixel = src[rowOffset + clamped];
            b += (pixel >> 0) & 0xFF;
            g += (pixel >> 8) & 0xFF;
            r += (pixel >> 16) & 0xFF;
            a += (pixel >> 24) & 0xFF;
            count++;
        }
        for (int x = 0; x < width; ++x)
        {
            dst[rowOffset + x] = static_cast<DWORD>((a / count) << 24 | (r / count) << 16 | (g / count) << 8 | (b / count));
            const int left = std::max(0, x - radius);
            const int right = std::min(width - 1, x + radius + 1);
            if (right < width)
            {
                const DWORD addPixel = src[rowOffset + right];
                b += (addPixel >> 0) & 0xFF;
                g += (addPixel >> 8) & 0xFF;
                r += (addPixel >> 16) & 0xFF;
                a += (addPixel >> 24) & 0xFF;
                count++;
            }
            if (x - radius - 1 >= 0)
            {
                const DWORD subPixel = src[rowOffset + left - 1];
                b -= (subPixel >> 0) & 0xFF;
                g -= (subPixel >> 8) & 0xFF;
                r -= (subPixel >> 16) & 0xFF;
                a -= (subPixel >> 24) & 0xFF;
                count--;
            }
        }
    }
}

void BoxBlurVerticalPass(DWORD* src, DWORD* dst, int width, int height, int radius)
{
    if (radius <= 0) return;
    for (int x = 0; x < width; ++x)
    {
        uint64_t r = 0, g = 0, b = 0, a = 0;
        int count = 0;
        for (int y = -radius; y <= radius; ++y)
        {
            const int clamped = std::clamp(y, 0, height - 1);
            const DWORD pixel = src[clamped * width + x];
            b += (pixel >> 0) & 0xFF;
            g += (pixel >> 8) & 0xFF;
            r += (pixel >> 16) & 0xFF;
            a += (pixel >> 24) & 0xFF;
            count++;
        }
        for (int y = 0; y < height; ++y)
        {
            dst[y * width + x] = static_cast<DWORD>((a / count) << 24 | (r / count) << 16 | (g / count) << 8 | (b / count));
            const int top = std::max(0, y - radius);
            const int bottom = std::min(height - 1, y + radius + 1);
            if (bottom < height)
            {
                const DWORD addPixel = src[bottom * width + x];
                b += (addPixel >> 0) & 0xFF;
                g += (addPixel >> 8) & 0xFF;
                r += (addPixel >> 16) & 0xFF;
                a += (addPixel >> 24) & 0xFF;
                count++;
            }
            if (y - radius - 1 >= 0)
            {
                const DWORD subPixel = src[(top - 1) * width + x];
                b -= (subPixel >> 0) & 0xFF;
                g -= (subPixel >> 8) & 0xFF;
                r -= (subPixel >> 16) & 0xFF;
                a -= (subPixel >> 24) & 0xFF;
                count--;
            }
        }
    }
}

void ApplyBoxBlurToBackdrop(int width, int height, int radius)
{
    if (!g_spotlightBackdrop.bits || radius <= 0 || width <= 0 || height <= 0) return;
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<DWORD> temp(count);
    DWORD* pixels = reinterpret_cast<DWORD*>(g_spotlightBackdrop.bits);
    BoxBlurHorizontalPass(pixels, temp.data(), width, height, radius);
    BoxBlurVerticalPass(temp.data(), pixels, width, height, radius);
    BoxBlurHorizontalPass(pixels, temp.data(), width, height, radius);
    BoxBlurVerticalPass(temp.data(), pixels, width, height, radius);
    BoxBlurHorizontalPass(pixels, temp.data(), width, height, radius);
    BoxBlurVerticalPass(temp.data(), pixels, width, height, radius);
}

void ReleaseSpotlightBackdrop()
{
    SafeRelease(g_spotlightBackdropD2dBitmap);
    if (g_spotlightBackdrop.dc && g_spotlightBackdrop.oldBitmap)
    {
        SelectObject(g_spotlightBackdrop.dc, g_spotlightBackdrop.oldBitmap);
    }
    if (g_spotlightBackdrop.bitmap) DeleteObject(g_spotlightBackdrop.bitmap);
    if (g_spotlightBackdrop.dc) DeleteDC(g_spotlightBackdrop.dc);
    g_spotlightBackdrop = {};
    g_spotlightLayerDirty = true;
}

bool CaptureSpotlightBackdrop(const RECT& windowRect)
{
    ReleaseSpotlightBackdrop();

    const int width = std::max(1, static_cast<int>(windowRect.right - windowRect.left));
    const int height = std::max(1, static_cast<int>(windowRect.bottom - windowRect.top));

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) return false;

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC captureDc = CreateCompatibleDC(screenDc);
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!captureDc || !bitmap || !bits)
    {
        if (bitmap) DeleteObject(bitmap);
        if (captureDc) DeleteDC(captureDc);
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(captureDc, bitmap);
    const BOOL copied = BitBlt(
        captureDc,
        0,
        0,
        width,
        height,
        screenDc,
        windowRect.left,
        windowRect.top,
        SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, screenDc);

    if (!copied)
    {
        SelectObject(captureDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(captureDc);
        return false;
    }

    g_spotlightBackdrop.width = width;
    g_spotlightBackdrop.height = height;
    g_spotlightBackdrop.dc = captureDc;
    g_spotlightBackdrop.bitmap = bitmap;
    g_spotlightBackdrop.oldBitmap = oldBitmap;
    g_spotlightBackdrop.bits = bits;
    ApplyBoxBlurToBackdrop(width, height, 12);
    g_spotlightLayerDirty = true;
    return true;
}

void PositionWindow()
{
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const bool spotlight = IsSpotlightMode();
    if (spotlight)
    {
        const int workWidth = static_cast<int>(work.right - work.left);
        const int workHeight = static_cast<int>(work.bottom - work.top);
        const int width = std::clamp(g_settings.spotlightWidth, 520, workWidth);
        const int height = std::clamp(g_settings.spotlightHeight, 420, workHeight);
        ApplySpotlightWindowRegion(width, height);
        SetWindowPos(
            g_hwnd,
            HWND_TOPMOST,
            work.left + ((work.right - work.left) - width) / 2,
            work.top + ((work.bottom - work.top) - height) / 2,
            width,
            height,
            SWP_NOACTIVATE);
        ApplyGlassBackdrop();
    }
    else
    {
        const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        ApplySpotlightWindowRegion(width, height);
        SetWindowPos(
            g_hwnd,
            HWND_TOPMOST,
            GetSystemMetrics(SM_XVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN),
            width,
            height,
            SWP_NOACTIVATE);
        ApplyGlassBackdrop();
    }
}

void PrewarmNativeUiForShow()
{
    InitializeDirectRenderer();
    if (!g_hwnd || g_filtered.empty()) return;

    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int preloadTop = g_scrollOffset + ContentClipTop() - TileSize();
    const int preloadBottom = g_scrollOffset + client.bottom + TileSize();
    int loaded = 0;
    const int preloadLimit = std::max(48, CalculateColumnCount(client) * 5);
    for (const auto& hit : BuildContentHitTiles())
    {
        if (hit.itemIndex < 0 || hit.itemIndex >= static_cast<int>(g_filtered.size())) continue;
        if (hit.bounds.bottom < preloadTop || hit.bounds.top > preloadBottom) continue;
        QueueItemIcon(g_items[g_filtered[hit.itemIndex]]);
        loaded++;
        if (loaded >= preloadLimit) break;
    }
}

void ShowSpotlightImmediately()
{
    if (!g_hwnd) return;
    // LLP-017 will restore motion with a compositor-driven timeline. Keep this
    // path immediate until then so opening Spotlight never blocks the UI thread.
    SetSpotlightLayeredOpacity(SpotlightGlassAlpha);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void HideSpotlightImmediately()
{
    if (!g_hwnd || !IsWindowVisible(g_hwnd))
    {
        return;
    }

    ShowWindow(g_hwnd, SW_HIDE);
    SetSpotlightLayeredOpacity(SpotlightGlassAlpha);
}

void ShowNativeUi()
{
    if (!g_catalogRefreshPending)
    {
        const DWORD probeDelay = g_catalogBrokerProbe.active && g_catalogBrokerProbe.stage == 1
            ? g_catalogBrokerProbe.delayMilliseconds
            : 0;
        RequestCatalogRefresh(probeDelay);
    }
    if (IsSpotlightMode()) g_spotlightState.Open();
    else
    {
        g_launchpadState.Open();
        ResetDragState();
        ClearAllSelection();
    }
    ResetSearchForOpen();
    RebuildFiltered();
    PositionWindow();
    if (IsSpotlightMode())
    {
        RECT windowRect = {};
        GetWindowRect(g_hwnd, &windowRect);
        CaptureSpotlightBackdrop(windowRect);
    }
    else
    {
        ReleaseSpotlightBackdrop();
    }
    g_searchInputActive = true;
    PrewarmNativeUiForShow();
    if (IsSpotlightMode())
    {
        ShowSpotlightImmediately();
    }
    else
    {
        SetSpotlightLayeredOpacity(255);
        ShowWindow(g_hwnd, SW_SHOW);
    }
    SetForegroundWindow(g_hwnd);
    ActivateSearchInput();
    InvalidateRect(g_hwnd, nullptr, TRUE);
    if (g_catalogBrokerProbe.active
        && g_catalogBrokerProbe.stage == 1
        && g_catalogBrokerProbe.inputPostedTick == 0)
    {
        g_catalogBrokerProbe.inputPostedTick = GetTickCount64();
        PostMessageW(g_hwnd, WM_CHAR, L'x', 0);
    }
}

void HideNativeUi()
{
    g_searchInputActive = false;
    g_searchInput.CancelComposition();
    g_searchPointerSelecting = false;
    if (GetCapture() == g_hwnd) ReleaseCapture();
    if (IsSpotlightMode())
    {
        HideSpotlightImmediately();
    }
    else
    {
        ShowWindow(g_hwnd, SW_HIDE);
    }
    TrimHiddenFootprint();
}

void ToggleNativeUi()
{
    if (IsWindowVisible(g_hwnd))
    {
        HideNativeUi();
    }
    else
    {
        ShowNativeUi();
    }
}

void LaunchItemAtFilteredIndex(int filteredIndex)
{
    if (filteredIndex < 0 || filteredIndex >= static_cast<int>(g_filtered.size())) return;
    auto& item = g_items[g_filtered[filteredIndex]];
    if (g_shellBroker.RequestLaunch(item.sourcePath) != 0)
    {
        if (g_spotlightFlowProbe.active) g_spotlightFlowProbe.requestedPaths.push_back(item.sourcePath);
        if (g_launchpadFlowProbe.active) g_launchpadFlowProbe.requestedPaths.push_back(item.sourcePath);
        HideNativeUi();
    }
    else MessageBoxW(g_hwnd, L"The Shell broker is unavailable; the item was not launched.", L"LightLaunchpad launch", MB_ICONWARNING | MB_OK);
}

bool HasExceededDragThreshold(POINT point)
{
    return std::abs(point.x - g_dragStart.x) >= GetSystemMetrics(SM_CXDRAG)
        || std::abs(point.y - g_dragStart.y) >= GetSystemMetrics(SM_CYDRAG);
}

struct RegionDropCandidate
{
    RECT bounds;
    const LaunchItem* item = nullptr;
};

DropTarget InsertionOrderFromRegionHits(const std::wstring& regionId, POINT point)
{
    std::vector<RegionDropCandidate> candidates;
    for (const auto& hit : g_hits)
    {
        if (hit.itemIndex < 0 || hit.itemIndex >= static_cast<int>(g_filtered.size())) continue;
        const auto& item = g_items[g_filtered[hit.itemIndex]];
        if (CompareStringOrdinal(item.regionId.c_str(), -1, regionId.c_str(), -1, TRUE) != CSTR_EQUAL) continue;
        if (ContainsPath(g_dragSourcePaths, item.sourcePath)) continue;
        candidates.push_back({ hit.bounds, &item });
    }

    if (candidates.empty())
    {
        return { regionId, CountItemsInRegion(regionId, g_dragSourcePaths), true };
    }

    std::sort(candidates.begin(), candidates.end(), [](const RegionDropCandidate& left, const RegionDropCandidate& right) {
        const int leftMid = left.bounds.top + (left.bounds.bottom - left.bounds.top) / 2;
        const int rightMid = right.bounds.top + (right.bounds.bottom - right.bounds.top) / 2;
        if (std::abs(leftMid - rightMid) > 8) return leftMid < rightMid;
        return left.bounds.left < right.bounds.left;
    });

    const int rowSlack = std::max(8, TileGap() / 2);
    int lastSameRow = -1;
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i)
    {
        const auto& candidate = candidates[i];
        if (point.y < candidate.bounds.top - rowSlack)
        {
            if (lastSameRow >= 0)
            {
                return {
                    regionId,
                    InsertionOrderForTargetItem(*candidates[lastSameRow].item, true, g_dragSourcePaths),
                    true
                };
            }

            return {
                regionId,
                InsertionOrderForTargetItem(*candidate.item, false, g_dragSourcePaths),
                true
            };
        }

        const bool sameRow = point.y >= candidate.bounds.top - rowSlack
            && point.y <= candidate.bounds.bottom + rowSlack;
        if (sameRow)
        {
            lastSameRow = i;
            const int midpoint = candidate.bounds.left + (candidate.bounds.right - candidate.bounds.left) / 2;
            if (point.x < midpoint)
            {
                return {
                    regionId,
                    InsertionOrderForTargetItem(*candidate.item, false, g_dragSourcePaths),
                    true
                };
            }
        }
    }

    return {
        regionId,
        InsertionOrderForTargetItem(*candidates.back().item, true, g_dragSourcePaths),
        true
    };
}

DropTarget FindDropTargetWithinRegion(const std::wstring& regionId, POINT point)
{
    return InsertionOrderFromRegionHits(regionId, point);
}

DropTarget FindDropTarget(POINT point)
{
    if (!SearchDisplayText().empty()) return {};

    for (const auto& hit : g_hits)
    {
        if (!PtInRect(&hit.bounds, point)) continue;
        if (hit.itemIndex < 0 || hit.itemIndex >= static_cast<int>(g_filtered.size())) continue;
        const auto& item = g_items[g_filtered[hit.itemIndex]];
        const int midpoint = hit.bounds.left + (hit.bounds.right - hit.bounds.left) / 2;
        return { item.regionId, InsertionOrderForTargetItem(item, point.x >= midpoint, g_dragSourcePaths), true };
    }

    for (const auto& regionHit : g_regionHits)
    {
        if (PtInRect(&regionHit.bounds, point))
        {
            return FindDropTargetWithinRegion(regionHit.regionId, point);
        }
    }

    if (!g_regionHits.empty())
    {
        const auto* target = &g_regionHits.front();
        for (const auto& regionHit : g_regionHits)
        {
            if (point.y >= regionHit.bounds.top)
            {
                target = &regionHit;
            }
        }
        return FindDropTargetWithinRegion(target->regionId, point);
    }

    return {};
}

bool SameDropTarget(const DropTarget& left, const DropTarget& right)
{
    if (left.valid != right.valid) return false;
    if (!left.valid) return true;
    return left.order == right.order
        && CompareStringOrdinal(left.regionId.c_str(), -1, right.regionId.c_str(), -1, TRUE) == CSTR_EQUAL;
}

void UpdateStableDropTarget(POINT point)
{
    const DropTarget candidate = FindDropTarget(point);
    if (!g_dropTarget.valid || SameDropTarget(candidate, g_dropTarget))
    {
        g_dropTarget = candidate;
        g_pendingDropTarget = {};
        g_pendingDropTargetSince = 0;
        return;
    }

    const DWORD now = GetTickCount();
    if (!SameDropTarget(candidate, g_pendingDropTarget))
    {
        g_pendingDropTarget = candidate;
        g_pendingDropTargetSince = now;
        return;
    }

    if (now - g_pendingDropTargetSince >= DragTargetSettleMs)
    {
        g_dropTarget = candidate;
        g_pendingDropTarget = {};
        g_pendingDropTargetSince = 0;
    }
}

RegionDropTarget FindRegionDropTarget(POINT point)
{
    for (const auto& regionHit : g_regionHits)
    {
        if (!PtInRect(&regionHit.bounds, point)) continue;
        const int midpoint = regionHit.headerBounds.top + (regionHit.headerBounds.bottom - regionHit.headerBounds.top) / 2;
        return { regionHit.regionId, point.y >= midpoint, true };
    }

    if (!g_regionHits.empty())
    {
        const auto* target = &g_regionHits.front();
        for (const auto& regionHit : g_regionHits)
        {
            if (point.y >= regionHit.bounds.top)
            {
                target = &regionHit;
            }
        }
        return { target->regionId, true, true };
    }

    return {};
}

void ResetDragState()
{
    g_mouseDown = false;
    g_pressedFilteredIndex = -1;
    g_dragActive = false;
    g_dragMode = DragMode::None;
    g_dragFilteredIndex = -1;
    g_pendingRegionDragId.clear();
    g_regionDragId.clear();
    g_dragSourcePaths.clear();
    g_dropTarget = {};
    g_pendingDropTarget = {};
    g_pendingDropTargetSince = 0;
    g_regionDropTarget = {};
    g_dragVisualOffsets.clear();
}

void SetLaunchpadEditing(bool editing)
{
    if (IsSpotlightMode()) return;
    const bool changed = g_launchpadState.IsEditing() != editing;
    KillTimer(g_hwnd, DragAnimationTimerId);
    if (g_selectionBoxActive)
    {
        g_selectionBoxActive = false;
        if (GetCapture() == g_hwnd) ReleaseCapture();
    }
    ResetDragState();
    ClearAllSelection();
    if (editing) g_launchpadState.EnterEdit();
    else g_launchpadState.ExitEdit();
    if (changed && g_launchpadFlowProbe.active) g_launchpadFlowProbe.modeTransitions++;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void BeginDrag(HWND hwnd, POINT point)
{
    if (!g_launchpadState.AllowsItemDrag()) return;
    if (g_dragFilteredIndex < 0 || g_dragFilteredIndex >= static_cast<int>(g_filtered.size())) return;
    if (g_launchpadFlowProbe.active) g_launchpadFlowProbe.itemDragBegins++;
    g_dragActive = true;
    g_dragMode = DragMode::Items;
    g_dragCurrent = point;
    g_dragSourcePaths = ResolveDragSourcePaths(g_items[g_filtered[g_dragFilteredIndex]]);
    g_dropTarget = FindDropTarget(point);
    g_pendingDropTarget = {};
    g_pendingDropTargetSince = 0;
    g_dragVisualOffsets.clear();
    SetTimer(hwnd, DragAnimationTimerId, 16, nullptr);
    SetCapture(hwnd);
}

void BeginRegionDrag(HWND hwnd, POINT point)
{
    if (!g_launchpadState.AllowsRegionDrag()) return;
    if (g_pendingRegionDragId.empty()) return;
    if (g_launchpadFlowProbe.active) g_launchpadFlowProbe.regionDragBegins++;
    g_dragActive = true;
    g_dragMode = DragMode::Region;
    g_dragCurrent = point;
    g_regionDragId = g_pendingRegionDragId;
    g_regionDropTarget = FindRegionDropTarget(point);
    SetTimer(hwnd, DragAnimationTimerId, 16, nullptr);
    SetCapture(hwnd);
}

void UpdateDrag(POINT point)
{
    if (!g_dragActive) return;
    g_dragCurrent = point;
    if (g_dragMode == DragMode::Region)
    {
        g_regionDropTarget = FindRegionDropTarget(point);
    }
    else
    {
        UpdateStableDropTarget(point);
    }
}

void CompleteDrag(HWND hwnd)
{
    const std::vector<std::wstring> movedPaths = g_dragSourcePaths;
    if (g_dragActive && g_dropTarget.valid && !g_dragSourcePaths.empty())
    {
        MoveItemsToTarget(g_dragSourcePaths, g_dropTarget);
        SortItems();
        SaveLayout();
        RebuildFiltered();
        g_selectedSourcePaths = movedPaths;
        g_selectedRegionIds.clear();
        g_selectedFilteredIndex = -1;
        for (int i = 0; i < static_cast<int>(g_filtered.size()); ++i)
        {
            if (ContainsPath(movedPaths, g_items[g_filtered[i]].sourcePath))
            {
                g_selectedFilteredIndex = i;
                break;
            }
        }
    }

    KillTimer(hwnd, DragAnimationTimerId);
    if (GetCapture() == hwnd)
    {
        ReleaseCapture();
    }
    ResetDragState();
    InvalidateRect(hwnd, nullptr, TRUE);
}

void CompleteRegionDrag(HWND hwnd)
{
    if (g_dragActive && g_regionDropTarget.valid && !g_regionDragId.empty())
    {
        MoveRegionToTarget(g_regionDragId, g_regionDropTarget);
        SortItems();
        SaveLayout();
        RebuildFiltered();
    }

    KillTimer(hwnd, DragAnimationTimerId);
    if (GetCapture() == hwnd)
    {
        ReleaseCapture();
    }
    ResetDragState();
    InvalidateRect(hwnd, nullptr, TRUE);
}

void CancelDrag(HWND hwnd)
{
    KillTimer(hwnd, DragAnimationTimerId);
    if (GetCapture() == hwnd)
    {
        ReleaseCapture();
    }
    ResetDragState();
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool ShouldUseDragAvoidancePreview()
{
    return g_dragActive
        && g_dragMode == DragMode::Items
        && g_dropTarget.valid
        && SearchDisplayText().empty()
        && !g_dragSourcePaths.empty();
}

bool ShouldTrackDragAvoidancePreview()
{
    return g_dragActive
        && g_dragMode == DragMode::Items
        && SearchDisplayText().empty()
        && !g_dragSourcePaths.empty();
}

bool ShouldHideTileForDragPreview(const LaunchItem& item)
{
    return ShouldUseDragAvoidancePreview() && ContainsPath(g_dragSourcePaths, item.sourcePath);
}

int ItemSlotInRegion(const LaunchItem& item)
{
    int slot = 0;
    for (const auto& candidate : g_items)
    {
        if (CompareStringOrdinal(candidate.regionId.c_str(), -1, item.regionId.c_str(), -1, TRUE) != CSTR_EQUAL) continue;
        if (candidate.order < item.order)
        {
            slot++;
        }
    }
    return slot;
}

int CountNonDraggedItemsBefore(const std::wstring& regionId, int order)
{
    int count = 0;
    for (const auto& item : g_items)
    {
        if (CompareStringOrdinal(item.regionId.c_str(), -1, regionId.c_str(), -1, TRUE) != CSTR_EQUAL) continue;
        if (ContainsPath(g_dragSourcePaths, item.sourcePath)) continue;
        if (item.order < order)
        {
            count++;
        }
    }
    return count;
}

int DragPreviewSlotForItem(const LaunchItem& item)
{
    int previewSlot = CountNonDraggedItemsBefore(item.regionId, item.order);
    if (CompareStringOrdinal(item.regionId.c_str(), -1, g_dropTarget.regionId.c_str(), -1, TRUE) == CSTR_EQUAL
        && previewSlot >= g_dropTarget.order)
    {
        previewSlot += static_cast<int>(g_dragSourcePaths.size());
    }
    return previewSlot;
}

DragVisualOffset& VisualOffsetForItem(const std::wstring& sourcePath)
{
    auto found = std::find_if(g_dragVisualOffsets.begin(), g_dragVisualOffsets.end(), [&](const DragVisualOffset& offset) {
        return SamePath(offset.sourcePath, sourcePath);
    });
    if (found != g_dragVisualOffsets.end())
    {
        return *found;
    }

    g_dragVisualOffsets.push_back({ sourcePath, 0.0, 0.0 });
    return g_dragVisualOffsets.back();
}

bool IsDropHintItem(const LaunchItem& item)
{
    if (!ShouldUseDragAvoidancePreview()) return false;
    if (ContainsPath(g_dragSourcePaths, item.sourcePath)) return false;
    if (CompareStringOrdinal(item.regionId.c_str(), -1, g_dropTarget.regionId.c_str(), -1, TRUE) != CSTR_EQUAL) return false;

    const int remainingCount = CountItemsInRegion(item.regionId, g_dragSourcePaths);
    if (remainingCount <= 0) return false;
    const int slot = CountNonDraggedItemsBefore(item.regionId, item.order);
    const int hintSlot = std::clamp(g_dropTarget.order, 0, remainingCount - 1);
    return slot == hintSlot;
}

BYTE DragIconAlphaForItem(const LaunchItem& item)
{
    (void)item;
    return 255;
}

void ApplyDragAvoidanceOffset(RECT& visualTileRect, const LaunchItem& item, int tileSize, int gap, int columns)
{
    if (!ShouldTrackDragAvoidancePreview()) return;
    if (ContainsPath(g_dragSourcePaths, item.sourcePath)) return;

    int targetDx = 0;
    int targetDy = 0;
    if (ShouldUseDragAvoidancePreview())
    {
        const int currentSlot = ItemSlotInRegion(item);
        const int previewSlot = DragPreviewSlotForItem(item);
        const int stride = tileSize + gap;
        const int currentColumn = currentSlot % columns;
        const int currentRow = currentSlot / columns;
        const int previewColumn = previewSlot % columns;
        const int previewRow = previewSlot / columns;
        targetDx = (previewColumn - currentColumn) * stride;
        targetDy = (previewRow - currentRow) * stride;
    }

    auto& offset = VisualOffsetForItem(item.sourcePath);
    offset.dx += (targetDx - offset.dx) * DragReflowSmoothing;
    offset.dy += (targetDy - offset.dy) * DragReflowSmoothing;
    if (std::abs(targetDx - offset.dx) < 0.45) offset.dx = targetDx;
    if (std::abs(targetDy - offset.dy) < 0.45) offset.dy = targetDy;
    OffsetRect(&visualTileRect, static_cast<int>(std::round(offset.dx)), static_cast<int>(std::round(offset.dy)));
}

void DrawRoundedRect(HDC dc, const RECT& rect, COLORREF fill, COLORREF stroke, int radius)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, stroke);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawTextClipped(HDC dc, const std::wstring& text, RECT rect, UINT format, COLORREF color)
{
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
}

bool InitializeDirectRenderer()
{
    if (g_d2dFactory && g_dwriteFactory && g_searchTextFormat && g_tileTextFormat && g_headerTextFormat)
    {
        return true;
    }

    if (!g_d2dFactory && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2dFactory)))
    {
        return false;
    }

    if (!g_dwriteFactory && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_dwriteFactory))))
    {
        return false;
    }

    if (!g_searchTextFormat)
    {
        g_dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 26.0f, L"", &g_searchTextFormat);
    }
    if (!g_tileTextFormat)
    {
        g_dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"", &g_tileTextFormat);
    }
    if (!g_headerTextFormat)
    {
        g_dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"", &g_headerTextFormat);
    }

    return g_searchTextFormat && g_tileTextFormat && g_headerTextFormat;
}

void DestroyDirectRenderer()
{
    SafeRelease(g_spotlightBackdropD2dBitmap);
    SafeRelease(g_d2dDcRenderTarget);
    SafeRelease(g_searchTextFormat);
    SafeRelease(g_tileTextFormat);
    SafeRelease(g_headerTextFormat);
    SafeRelease(g_dwriteFactory);
    SafeRelease(g_d2dFactory);
}

D2D1_COLOR_F D2DColor(COLORREF color)
{
    return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f, 1.0f);
}

D2D1_COLOR_F D2DColorAlpha(COLORREF color, float alpha)
{
    return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f, alpha);
}

D2D1_RECT_F D2DRect(const RECT& rect)
{
    return D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.right), static_cast<float>(rect.bottom));
}

D2D1_ROUNDED_RECT SpotlightRoundedRect(const RECT& client, float inset)
{
    const float radius = std::max(0.0f, static_cast<float>(SpotlightCornerRadius) - inset);
    return D2D1::RoundedRect(
        D2D1::RectF(
            static_cast<float>(client.left) + inset,
            static_cast<float>(client.top) + inset,
            static_cast<float>(client.right) - inset,
            static_cast<float>(client.bottom) - inset),
        radius,
        radius);
}

bool IsPointInsideRoundedRect(POINT point, const RECT& rect, int radius)
{
    if (!PtInRect(&rect, point)) return false;
    const int r = std::max(1, radius);
    const int left = rect.left;
    const int right = rect.right;
    const int top = rect.top;
    const int bottom = rect.bottom;

    if ((point.x >= left + r && point.x < right - r) || (point.y >= top + r && point.y < bottom - r))
    {
        return true;
    }

    const int cx = point.x < left + r ? left + r : right - r - 1;
    const int cy = point.y < top + r ? top + r : bottom - r - 1;
    const int dx = point.x - cx;
    const int dy = point.y - cy;
    return dx * dx + dy * dy <= r * r;
}

double RoundedRectCoverage(int x, int y, const RECT& rect, int radius)
{
    const double width = static_cast<double>(rect.right - rect.left);
    const double height = static_cast<double>(rect.bottom - rect.top);
    if (width <= 0.0 || height <= 0.0) return 0.0;

    const double r = std::max(1.0, std::min<double>(radius, std::min(width, height) * 0.5));
    const double centerX = (static_cast<double>(rect.left) + static_cast<double>(rect.right)) * 0.5;
    const double centerY = (static_cast<double>(rect.top) + static_cast<double>(rect.bottom)) * 0.5;
    const double halfW = width * 0.5;
    const double halfH = height * 0.5;
    const double px = static_cast<double>(x) + 0.5;
    const double py = static_cast<double>(y) + 0.5;
    const double qx = std::abs(px - centerX) - (halfW - r);
    const double qy = std::abs(py - centerY) - (halfH - r);
    const double outsideX = std::max(qx, 0.0);
    const double outsideY = std::max(qy, 0.0);
    const double outsideDistance = std::sqrt(outsideX * outsideX + outsideY * outsideY);
    const double insideDistance = std::min(std::max(qx, qy), 0.0);
    const double signedDistance = outsideDistance + insideDistance - r;
    return std::clamp(0.5 - signedDistance, 0.0, 1.0);
}

double SmoothStep(double start, double end, double value)
{
    if (start == end) return value >= end ? 1.0 : 0.0;
    const double t = std::clamp((value - start) / (end - start), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double ContentFeatherCoverage(int y, const RECT& contentClip)
{
    const double py = static_cast<double>(y) + 0.5;
    const double feather = static_cast<double>(SpotlightContentFeatherSize);
    const double top = SmoothStep(static_cast<double>(contentClip.top), static_cast<double>(contentClip.top) + feather, py);
    const double bottom = 1.0 - SmoothStep(static_cast<double>(contentClip.bottom) - feather, static_cast<double>(contentClip.bottom), py);
    return std::clamp(std::min(top, bottom), 0.0, 1.0);
}

double SpotlightContentCoverageForPixel(int y, const RECT& contentClip)
{
    if (y < contentClip.top)
    {
        return 1.0;
    }

    return ContentFeatherCoverage(y, contentClip);
}

bool IsPointInsideSpotlightGlass(POINT point)
{
    if (!g_hwnd || !IsSpotlightMode()) return true;
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    return IsPointInsideRoundedRect(point, client, SpotlightCornerRadius);
}

void DrawLiquidGlassSpecularSheen(ID2D1DCRenderTarget* target, const D2D1_ROUNDED_RECT& panel, const RECT& client)
{
    ID2D1GradientStopCollection* sheenStops = nullptr;
    ID2D1LinearGradientBrush* sheenBrush = nullptr;
    D2D1_GRADIENT_STOP stops[] = {
        { 0.0f, D2DColorAlpha(RGB(246, 252, 255), 0.28f) },
        { 0.16f, D2DColorAlpha(RGB(176, 218, 255), 0.11f) },
        { 0.36f, D2DColorAlpha(RGB(255, 255, 255), 0.0f) },
        { 0.68f, D2DColorAlpha(RGB(150, 194, 232), 0.065f) },
        { 1.0f, D2DColorAlpha(RGB(255, 255, 255), 0.0f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(stops, ARRAYSIZE(stops), &sheenStops)) && sheenStops)
    {
        const float width = static_cast<float>(client.right - client.left);
        const float height = static_cast<float>(client.bottom - client.top);
        const auto props = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(client.left) + width * 0.03f, static_cast<float>(client.top) - height * 0.06f),
            D2D1::Point2F(static_cast<float>(client.right) - width * 0.10f, static_cast<float>(client.top) + height * 0.62f));
        target->CreateLinearGradientBrush(props, sheenStops, &sheenBrush);
    }

    if (sheenBrush)
    {
        target->FillRoundedRectangle(panel, sheenBrush);
    }

    SafeRelease(sheenBrush);
    SafeRelease(sheenStops);
}

void DrawLiquidGlassInnerShadow(ID2D1DCRenderTarget* target, const D2D1_ROUNDED_RECT& panel, const RECT& client)
{
    ID2D1GradientStopCollection* shadowStops = nullptr;
    ID2D1LinearGradientBrush* shadowBrush = nullptr;
    D2D1_GRADIENT_STOP stops[] = {
        { 0.0f, D2DColorAlpha(RGB(3, 8, 18), 0.0f) },
        { 0.56f, D2DColorAlpha(RGB(3, 8, 18), 0.0f) },
        { 1.0f, D2DColorAlpha(RGB(3, 8, 18), 0.24f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(stops, ARRAYSIZE(stops), &shadowStops)) && shadowStops)
    {
        const auto props = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(client.left), static_cast<float>(client.top)),
            D2D1::Point2F(static_cast<float>(client.left), static_cast<float>(client.bottom)));
        target->CreateLinearGradientBrush(props, shadowStops, &shadowBrush);
    }
    if (shadowBrush)
    {
        target->FillRoundedRectangle(panel, shadowBrush);
    }

    for (int inset = 1; inset <= 5; ++inset)
    {
        ID2D1SolidColorBrush* strokeBrush = nullptr;
        const float alpha = 0.105f - inset * 0.014f;
        target->CreateSolidColorBrush(D2DColorAlpha(RGB(2, 7, 16), std::max(0.025f, alpha)), &strokeBrush);
        if (strokeBrush)
        {
            const float edgeInset = static_cast<float>(inset) + 0.5f;
            target->DrawRoundedRectangle(
                SpotlightRoundedRect(client, edgeInset),
                strokeBrush,
                1.0f);
        }
        SafeRelease(strokeBrush);
    }

    SafeRelease(shadowBrush);
    SafeRelease(shadowStops);
}

void DrawLiquidGlassRimLight(ID2D1DCRenderTarget* target, const RECT& client)
{
    for (int inset = 0; inset < SpotlightGlassRimLayers; ++inset)
    {
        ID2D1SolidColorBrush* brush = nullptr;
        const COLORREF edgeColor = inset == 0
            ? RGB(250, 255, 255)
            : (inset < 3 ? RGB(150, 208, 246) : RGB(54, 90, 128));
        const float alpha = inset == 0
            ? 0.34f
            : (inset < 3 ? std::max(0.10f, 0.20f - inset * 0.035f) : std::max(0.025f, 0.10f - inset * 0.008f));
        target->CreateSolidColorBrush(D2DColorAlpha(edgeColor, alpha), &brush);
        if (brush)
        {
            const float pixelAlignedInset = static_cast<float>(SpotlightPanelInset + inset) + 0.5f;
            target->DrawRoundedRectangle(SpotlightRoundedRect(client, pixelAlignedInset), brush, 1.0f);
        }
        SafeRelease(brush);
    }
}

void RenderSpotlightGlassDirect2D(ID2D1DCRenderTarget* target, const RECT& client)
{
    target->Clear(D2DColor(SpotlightTransparentKey));

    ID2D1Bitmap* backdrop = GetSpotlightBackdropD2dBitmap(target);
    if (backdrop)
    {
        const auto destRect = D2D1::RectF(static_cast<float>(client.left), static_cast<float>(client.top), static_cast<float>(client.right), static_cast<float>(client.bottom));
        target->DrawBitmap(backdrop, &destRect, 0.52f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    ID2D1GradientStopCollection* glassStops = nullptr;
    ID2D1LinearGradientBrush* glassBrush = nullptr;
    D2D1_GRADIENT_STOP stops[] = {
        { 0.0f, D2DColorAlpha(RGB(76, 94, 116), 0.38f) },
        { 0.32f, D2DColorAlpha(RGB(42, 58, 80), 0.44f) },
        { 0.72f, D2DColorAlpha(RGB(18, 32, 54), 0.53f) },
        { 1.0f, D2DColorAlpha(RGB(7, 17, 32), 0.60f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(stops, ARRAYSIZE(stops), &glassStops)) && glassStops)
    {
        const auto props = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(client.left), static_cast<float>(client.top)),
            D2D1::Point2F(static_cast<float>(client.right), static_cast<float>(client.bottom)));
        target->CreateLinearGradientBrush(props, glassStops, &glassBrush);
    }

    const auto panel = SpotlightRoundedRect(client, 0.0f);
    if (glassBrush)
    {
        target->FillRoundedRectangle(panel, glassBrush);
    }

    ID2D1GradientStopCollection* depthStops = nullptr;
    ID2D1RadialGradientBrush* depthBrush = nullptr;
    D2D1_GRADIENT_STOP depth[] = {
        { 0.0f, D2DColorAlpha(RGB(112, 154, 188), 0.18f) },
        { 0.46f, D2DColorAlpha(RGB(62, 102, 136), 0.060f) },
        { 1.0f, D2DColorAlpha(RGB(4, 12, 26), 0.30f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(depth, ARRAYSIZE(depth), &depthStops)) && depthStops)
    {
        const auto props = D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(client.left) + (client.right - client.left) * 0.36f, static_cast<float>(client.top) + (client.bottom - client.top) * 0.18f),
            D2D1::Point2F(0.0f, 0.0f),
            static_cast<float>(client.right - client.left) * 0.82f,
            static_cast<float>(client.bottom - client.top) * 0.78f);
        target->CreateRadialGradientBrush(props, depthStops, &depthBrush);
    }
    if (depthBrush)
    {
        target->FillRoundedRectangle(panel, depthBrush);
    }

    ID2D1GradientStopCollection* ambientStops = nullptr;
    ID2D1RadialGradientBrush* ambientBrush = nullptr;
    D2D1_GRADIENT_STOP ambient[] = {
        { 0.0f, D2DColorAlpha(RGB(88, 142, 180), 0.12f) },
        { 0.42f, D2DColorAlpha(RGB(56, 104, 146), 0.042f) },
        { 1.0f, D2DColorAlpha(RGB(255, 255, 255), 0.0f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(ambient, ARRAYSIZE(ambient), &ambientStops)) && ambientStops)
    {
        const auto props = D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(client.left) + (client.right - client.left) * 0.70f, static_cast<float>(client.top) + (client.bottom - client.top) * 0.28f),
            D2D1::Point2F(0.0f, 0.0f),
            static_cast<float>(client.right - client.left) * 0.62f,
            static_cast<float>(client.bottom - client.top) * 0.58f);
        target->CreateRadialGradientBrush(props, ambientStops, &ambientBrush);
    }
    if (ambientBrush)
    {
        target->FillRoundedRectangle(panel, ambientBrush);
    }

    DrawLiquidGlassSpecularSheen(target, panel, client);
    DrawLiquidGlassInnerShadow(target, panel, client);

    SafeRelease(ambientBrush);
    SafeRelease(ambientStops);
    SafeRelease(depthBrush);
    SafeRelease(depthStops);
    SafeRelease(glassBrush);
    SafeRelease(glassStops);
}

void RenderLaunchpadBackdropDirect2D(ID2D1DCRenderTarget* target, const RECT& client)
{
    target->Clear(D2DColor(RGB(24, 29, 38)));

    ID2D1SolidColorBrush* baseBrush = nullptr;
    ID2D1SolidColorBrush* topHighlightBrush = nullptr;
    target->CreateSolidColorBrush(D2DColorAlpha(RGB(38, 46, 58), 0.72f), &baseBrush);
    target->CreateSolidColorBrush(D2DColorAlpha(RGB(255, 255, 255), 0.08f), &topHighlightBrush);
    if (baseBrush) target->FillRectangle(D2DRect(client), baseBrush);
    if (topHighlightBrush)
    {
        const LONG highlightHeight = std::max<LONG>(80, (client.bottom - client.top) / 5);
        RECT topGlow = { client.left, client.top, client.right, client.top + highlightHeight };
        target->FillRectangle(D2DRect(topGlow), topHighlightBrush);
    }
    SafeRelease(baseBrush);
    SafeRelease(topHighlightBrush);
}

void DrawGlassBackground(ID2D1DCRenderTarget* target, const RECT& client)
{
    if (IsSpotlightMode())
    {
        RenderSpotlightGlassDirect2D(target, client);
        return;
    }

    RenderLaunchpadBackdropDirect2D(target, client);
}

void RenderSpotlightGlassGdiSpecular(HDC dc, const RECT& client)
{
    const int height = client.bottom - client.top;
    RECT band = {
        client.left,
        client.top,
        client.right,
        client.top + std::max(58, height * 34 / 100)
    };

    TRIVERTEX vertices[2] = {};
    vertices[0].x = band.left;
    vertices[0].y = band.top;
    vertices[0].Red = 116 << 8;
    vertices[0].Green = 144 << 8;
    vertices[0].Blue = 166 << 8;
    vertices[0].Alpha = 0xFFFF;

    vertices[1].x = band.right;
    vertices[1].y = band.bottom;
    vertices[1].Red = 36 << 8;
    vertices[1].Green = 52 << 8;
    vertices[1].Blue = 74 << 8;
    vertices[1].Alpha = 0xFFFF;

    GRADIENT_RECT upper = { 0, 1 };
    GradientFill(dc, vertices, 2, &upper, 1, GRADIENT_FILL_RECT_V);

    HPEN topGlint = CreatePen(PS_SOLID, 1, RGB(218, 238, 250));
    HGDIOBJ oldPen = SelectObject(dc, topGlint);
    MoveToEx(dc, client.left + SpotlightCornerRadius, client.top + 1, nullptr);
    LineTo(dc, client.right - SpotlightCornerRadius, client.top + 1);
    SelectObject(dc, oldPen);
    DeleteObject(topGlint);
}

void RenderSpotlightGlassGdi(HDC dc, const RECT& client)
{
    const int saved = SaveDC(dc);
    HBRUSH transparent = CreateSolidBrush(SpotlightTransparentKey);
    FillRect(dc, &client, transparent);
    DeleteObject(transparent);

    const int radius = std::max(1, SpotlightCornerRadius * 2);
    HRGN clip = CreateRoundRectRgn(client.left, client.top, client.right + 1, client.bottom + 1, radius, radius);
    if (clip)
    {
        SelectClipRgn(dc, clip);
    }

    TRIVERTEX vertices[3] = {};
    vertices[0].x = client.left;
    vertices[0].y = client.top;
    vertices[0].Red = 82 << 8;
    vertices[0].Green = 100 << 8;
    vertices[0].Blue = 118 << 8;
    vertices[0].Alpha = 0xFFFF;

    vertices[1].x = client.right;
    vertices[1].y = client.top + ((client.bottom - client.top) * 46 / 100);
    vertices[1].Red = 48 << 8;
    vertices[1].Green = 62 << 8;
    vertices[1].Blue = 80 << 8;
    vertices[1].Alpha = 0xFFFF;

    vertices[2].x = client.right;
    vertices[2].y = client.bottom;
    vertices[2].Red = 16 << 8;
    vertices[2].Green = 26 << 8;
    vertices[2].Blue = 42 << 8;
    vertices[2].Alpha = 0xFFFF;

    GRADIENT_RECT upper = { 0, 1 };
    GRADIENT_RECT lower = { 1, 2 };
    GradientFill(dc, vertices, 3, &upper, 1, GRADIENT_FILL_RECT_V);
    GradientFill(dc, vertices, 3, &lower, 1, GRADIENT_FILL_RECT_V);
    RenderSpotlightGlassGdiSpecular(dc, client);

    if (clip)
    {
        DeleteObject(clip);
    }
    RestoreDC(dc, saved);
}

void DrawGlassBackground(HDC dc, const RECT& client)
{
    const bool spotlight = IsSpotlightMode();
    if (spotlight)
    {
        RenderSpotlightGlassGdi(dc, client);
        return;
    }

    HBRUSH background = CreateSolidBrush(spotlight ? RGB(44, 52, 64) : RGB(24, 29, 38));
    FillRect(dc, &client, background);
    DeleteObject(background);

    const LONG highlightHeight = std::max<LONG>(80, (client.bottom - client.top) / 5);
    RECT topGlow = { client.left, client.top, client.right, client.top + highlightHeight };
    HBRUSH glow = CreateSolidBrush(spotlight ? RGB(72, 82, 96) : RGB(38, 46, 58));
    FillRect(dc, &topGlow, glow);
    DeleteObject(glow);
}

void DrawSoftWindowEdge(ID2D1DCRenderTarget* target, const RECT& client)
{
    const bool spotlight = IsSpotlightMode();
    if (spotlight)
    {
        DrawLiquidGlassRimLight(target, client);
        return;
    }

    const int layers = spotlight ? SpotlightEdgeLayers : 5;
    for (int inset = 0; inset < layers; ++inset)
    {
        ID2D1SolidColorBrush* brush = nullptr;
        const float alpha = spotlight
            ? std::max(0.010f, 0.070f - inset * 0.0055f)
            : std::max(0.04f, 0.22f - inset * 0.035f);
        const COLORREF edgeColor = spotlight
            ? (inset < 3 ? RGB(214, 234, 248) : RGB(112, 150, 184))
            : (inset < 2 ? RGB(238, 248, 255) : RGB(130, 178, 220));
        target->CreateSolidColorBrush(D2DColorAlpha(edgeColor, std::max(0.04f, alpha)), &brush);
        if (brush)
        {
            const float pixelAlignedInset = spotlight
                ? static_cast<float>(SpotlightPanelInset + inset) + 0.5f
                : static_cast<float>(inset) + 0.5f;
            const auto rounded = spotlight
                ? SpotlightRoundedRect(client, pixelAlignedInset)
                : D2D1::RoundedRect(
                    D2D1::RectF(
                        static_cast<float>(client.left) + pixelAlignedInset,
                        static_cast<float>(client.top) + pixelAlignedInset,
                        static_cast<float>(client.right) - pixelAlignedInset,
                        static_cast<float>(client.bottom) - pixelAlignedInset),
                    0.0f,
                    0.0f);
            target->DrawRoundedRectangle(rounded, brush, 1.0f);
        }
        SafeRelease(brush);
    }
}

void DrawSoftWindowEdge(HDC dc, const RECT& client)
{
    const bool spotlight = IsSpotlightMode();
    const int layers = spotlight ? SpotlightGlassRimLayers : 5;
    for (int inset = 0; inset < layers; ++inset)
    {
        const int shade = spotlight ? 152 - inset * 8 : 92 + inset * 18;
        const COLORREF color = spotlight
            ? (inset < 2
                ? RGB(std::max(120, shade + 28), std::max(148, shade + 34), std::max(166, shade + 42))
                : RGB(std::max(44, shade - 52), std::max(72, shade - 24), std::max(98, shade + 6)))
            : RGB(shade, shade + 18, shade + 38);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        const int edgeInset = spotlight ? SpotlightPanelInset + inset : inset;
        const int radius = spotlight ? std::max(1, (SpotlightCornerRadius - edgeInset) * 2) : 0;
        RoundRect(dc, client.left + edgeInset, client.top + edgeInset, client.right - edgeInset, client.bottom - edgeInset, radius, radius);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
}

void DrawRoundedRectDirect(ID2D1DCRenderTarget* target, const RECT& rect, COLORREF fill, COLORREF stroke, float radius)
{
    ID2D1SolidColorBrush* fillBrush = nullptr;
    ID2D1SolidColorBrush* strokeBrush = nullptr;
    target->CreateSolidColorBrush(D2DColor(fill), &fillBrush);
    target->CreateSolidColorBrush(D2DColor(stroke), &strokeBrush);
    const auto rounded = D2D1::RoundedRect(D2DRect(rect), radius, radius);
    if (fillBrush) target->FillRoundedRectangle(rounded, fillBrush);
    if (strokeBrush) target->DrawRoundedRectangle(rounded, strokeBrush, 1.0f);
    SafeRelease(fillBrush);
    SafeRelease(strokeBrush);
}

D2D1_RECT_F SpotlightBackdropSourceRectForLens(const RECT& rect, float magnification, float shiftX, float shiftY)
{
    const float dstWidth = static_cast<float>(std::max(1L, rect.right - rect.left));
    const float dstHeight = static_cast<float>(std::max(1L, rect.bottom - rect.top));
    const float sourceWidth = std::max(1.0f, std::min(static_cast<float>(g_spotlightBackdrop.width), dstWidth / std::max(1.0f, magnification)));
    const float sourceHeight = std::max(1.0f, std::min(static_cast<float>(g_spotlightBackdrop.height), dstHeight / std::max(1.0f, magnification)));
    const float centerX = (static_cast<float>(rect.left + rect.right) * 0.5f) + shiftX;
    const float centerY = (static_cast<float>(rect.top + rect.bottom) * 0.5f) + shiftY;
    const float maxLeft = std::max(0.0f, static_cast<float>(g_spotlightBackdrop.width) - sourceWidth);
    const float maxTop = std::max(0.0f, static_cast<float>(g_spotlightBackdrop.height) - sourceHeight);
    const float left = std::clamp(centerX - sourceWidth * 0.5f, 0.0f, maxLeft);
    const float top = std::clamp(centerY - sourceHeight * 0.5f, 0.0f, maxTop);
    return D2D1::RectF(left, top, left + sourceWidth, top + sourceHeight);
}

RECT SpotlightBackdropSourceRectForLensGdi(const RECT& rect, float magnification, int shiftX, int shiftY)
{
    const int dstWidth = std::max<LONG>(1, rect.right - rect.left);
    const int dstHeight = std::max<LONG>(1, rect.bottom - rect.top);
    const int sourceWidth = std::max(1, std::min(g_spotlightBackdrop.width, static_cast<int>(std::round(dstWidth / std::max(1.0f, magnification)))));
    const int sourceHeight = std::max(1, std::min(g_spotlightBackdrop.height, static_cast<int>(std::round(dstHeight / std::max(1.0f, magnification)))));
    const int centerX = ((rect.left + rect.right) / 2) + shiftX;
    const int centerY = ((rect.top + rect.bottom) / 2) + shiftY;
    const int maxLeft = std::max(0, g_spotlightBackdrop.width - sourceWidth);
    const int maxTop = std::max(0, g_spotlightBackdrop.height - sourceHeight);
    const int left = std::clamp(centerX - (sourceWidth / 2), 0, maxLeft);
    const int top = std::clamp(centerY - (sourceHeight / 2), 0, maxTop);
    return { left, top, left + sourceWidth, top + sourceHeight };
}

D2D1_RECT_F SpotlightBackdropAlignedSourceRect(const RECT& rect, float offsetX = 0.0f, float offsetY = 0.0f)
{
    const float width = static_cast<float>(std::max(1L, rect.right - rect.left));
    const float height = static_cast<float>(std::max(1L, rect.bottom - rect.top));
    const float maxLeft = std::max(0.0f, static_cast<float>(g_spotlightBackdrop.width) - width);
    const float maxTop = std::max(0.0f, static_cast<float>(g_spotlightBackdrop.height) - height);
    const float left = std::clamp(static_cast<float>(rect.left) + offsetX, 0.0f, maxLeft);
    const float top = std::clamp(static_cast<float>(rect.top) + offsetY, 0.0f, maxTop);
    return D2D1::RectF(left, top, left + width, top + height);
}

RECT SpotlightBackdropAlignedSourceRectGdi(const RECT& rect, int offsetX = 0, int offsetY = 0)
{
    const int width = std::max<LONG>(1, rect.right - rect.left);
    const int height = std::max<LONG>(1, rect.bottom - rect.top);
    const int maxLeft = std::max(0, g_spotlightBackdrop.width - width);
    const int maxTop = std::max(0, g_spotlightBackdrop.height - height);
    const int left = std::clamp(static_cast<int>(rect.left) + offsetX, 0, maxLeft);
    const int top = std::clamp(static_cast<int>(rect.top) + offsetY, 0, maxTop);
    return { left, top, left + width, top + height };
}

ID2D1Bitmap* GetSpotlightBackdropD2dBitmap(ID2D1DCRenderTarget* target)
{
    if (!target || !g_spotlightBackdrop.bits || g_spotlightBackdrop.width <= 0 || g_spotlightBackdrop.height <= 0)
    {
        return nullptr;
    }

    if (g_spotlightBackdropD2dBitmap)
    {
        return g_spotlightBackdropD2dBitmap;
    }

    const auto bitmapProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    const UINT32 pitch = static_cast<UINT32>(g_spotlightBackdrop.width * sizeof(DWORD));
    target->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(g_spotlightBackdrop.width), static_cast<UINT32>(g_spotlightBackdrop.height)),
        g_spotlightBackdrop.bits,
        pitch,
        bitmapProps,
        &g_spotlightBackdropD2dBitmap);
    return g_spotlightBackdropD2dBitmap;
}

void DrawRoundedBackdropBitmap(ID2D1DCRenderTarget* target, ID2D1Bitmap* bitmap, const D2D1_ROUNDED_RECT& rounded, const RECT& dest, const D2D1_RECT_F& source, float opacity)
{
    if (!target || !bitmap || !g_d2dFactory) return;

    ID2D1RoundedRectangleGeometry* mask = nullptr;
    ID2D1Layer* layer = nullptr;
    if (SUCCEEDED(g_d2dFactory->CreateRoundedRectangleGeometry(rounded, &mask))
        && SUCCEEDED(target->CreateLayer(nullptr, &layer))
        && mask
        && layer)
    {
        target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask), layer);
        const auto destRect = D2DRect(dest);
        target->DrawBitmap(bitmap, &destRect, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        target->PopLayer();
    }

    SafeRelease(layer);
    SafeRelease(mask);
}

void DrawAlignedSearchBackdrop(ID2D1DCRenderTarget* target, const D2D1_ROUNDED_RECT& rounded, const RECT& rect, bool active)
{
    ID2D1Bitmap* bitmap = GetSpotlightBackdropD2dBitmap(target);
    if (!bitmap) return;

    DrawRoundedBackdropBitmap(
        target,
        bitmap,
        rounded,
        rect,
        SpotlightBackdropAlignedSourceRect(rect),
        active ? 0.035f : 0.020f);
}

void DrawAlignedSearchBackdrop(HDC dc, const RECT& rect, bool active)
{
    if (!dc || !g_spotlightBackdrop.dc || g_spotlightBackdrop.width <= 0 || g_spotlightBackdrop.height <= 0)
    {
        return;
    }

    const int saved = SaveDC(dc);
    HRGN clip = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, SpotlightSearchCornerRadius * 2, SpotlightSearchCornerRadius * 2);
    if (clip)
    {
        SelectClipRgn(dc, clip);
    }

    const RECT source = SpotlightBackdropAlignedSourceRectGdi(rect);
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, static_cast<BYTE>(active ? 9 : 5), 0 };
    AlphaBlend(
        dc,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        g_spotlightBackdrop.dc,
        source.left,
        source.top,
        source.right - source.left,
        source.bottom - source.top,
        blend);

    if (clip)
    {
        DeleteObject(clip);
    }
    RestoreDC(dc, saved);
}

void DrawSearchLiquidMaterialWash(ID2D1DCRenderTarget* target, const D2D1_ROUNDED_RECT& rounded, const RECT& rect, bool active)
{
    ID2D1GradientStopCollection* washStops = nullptr;
    ID2D1LinearGradientBrush* washBrush = nullptr;
    D2D1_GRADIENT_STOP stops[] = {
        { 0.0f, D2DColorAlpha(RGB(246, 252, 255), active ? 0.46f : 0.34f) },
        { 0.36f, D2DColorAlpha(RGB(202, 234, 255), active ? 0.32f : 0.22f) },
        { 1.0f, D2DColorAlpha(RGB(148, 214, 255), active ? 0.24f : 0.16f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(stops, ARRAYSIZE(stops), &washStops)) && washStops)
    {
        const auto props = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)),
            D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.bottom)));
        target->CreateLinearGradientBrush(props, washStops, &washBrush);
    }
    if (washBrush)
    {
        target->FillRoundedRectangle(rounded, washBrush);
    }

    ID2D1GradientStopCollection* bloomStops = nullptr;
    ID2D1RadialGradientBrush* bloomBrush = nullptr;
    D2D1_GRADIENT_STOP bloom[] = {
        { 0.0f, D2DColorAlpha(RGB(255, 255, 255), active ? 0.18f : 0.12f) },
        { 0.50f, D2DColorAlpha(RGB(190, 230, 255), active ? 0.070f : 0.040f) },
        { 1.0f, D2DColorAlpha(RGB(255, 255, 255), 0.0f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(bloom, ARRAYSIZE(bloom), &bloomStops)) && bloomStops)
    {
        const auto props = D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(rect.left) + (rect.right - rect.left) * 0.18f, static_cast<float>(rect.top) + (rect.bottom - rect.top) * 0.26f),
            D2D1::Point2F(0.0f, 0.0f),
            static_cast<float>(rect.right - rect.left) * 0.54f,
            static_cast<float>(rect.bottom - rect.top) * 1.18f);
        target->CreateRadialGradientBrush(props, bloomStops, &bloomBrush);
    }
    if (bloomBrush)
    {
        target->FillRoundedRectangle(rounded, bloomBrush);
    }

    SafeRelease(bloomBrush);
    SafeRelease(bloomStops);
    SafeRelease(washBrush);
    SafeRelease(washStops);
}

void DrawSearchLiquidMaterialWash(HDC dc, const RECT& rect, bool active)
{
    HDC washDc = CreateCompatibleDC(dc);
    if (!washDc) return;

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = std::max<LONG>(1, rect.right - rect.left);
    info.bmiHeader.biHeight = -std::max<LONG>(1, rect.bottom - rect.top);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP washBitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!washBitmap || !bits)
    {
        if (washBitmap) DeleteObject(washBitmap);
        DeleteDC(washDc);
        return;
    }

    HGDIOBJ oldBitmap = SelectObject(washDc, washBitmap);
    RECT local = { 0, 0, info.bmiHeader.biWidth, -info.bmiHeader.biHeight };
    HBRUSH brush = CreateSolidBrush(active ? RGB(222, 242, 255) : RGB(210, 232, 248));
    FillRect(washDc, &local, brush);
    DeleteObject(brush);

    const int saved = SaveDC(dc);
    HRGN clip = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, SpotlightSearchCornerRadius * 2, SpotlightSearchCornerRadius * 2);
    if (clip)
    {
        SelectClipRgn(dc, clip);
    }
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, static_cast<BYTE>(active ? 78 : 56), 0 };
    AlphaBlend(dc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, washDc, 0, 0, local.right, local.bottom, blend);
    if (clip)
    {
        DeleteObject(clip);
    }
    RestoreDC(dc, saved);

    SelectObject(washDc, oldBitmap);
    DeleteObject(washBitmap);
    DeleteDC(washDc);
}

void DrawSearchEdgeRefraction(ID2D1DCRenderTarget* target, const D2D1_ROUNDED_RECT& rounded, const RECT& rect, bool active)
{
    ID2D1Bitmap* bitmap = GetSpotlightBackdropD2dBitmap(target);
    if (!bitmap || !g_d2dFactory) return;

    const int rectHeight = static_cast<int>(rect.bottom - rect.top);
    const int edge = std::max(12, std::min(20, rectHeight / 3));
    const float opacity = active ? 0.18f : 0.12f;
    const float sideShift = active ? 2.0f : 1.0f;

    ID2D1RoundedRectangleGeometry* mask = nullptr;
    ID2D1Layer* layer = nullptr;
    if (SUCCEEDED(g_d2dFactory->CreateRoundedRectangleGeometry(rounded, &mask))
        && SUCCEEDED(target->CreateLayer(nullptr, &layer))
        && mask
        && layer)
    {
        target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask), layer);

        const RECT leftBand = { rect.left, rect.top, rect.left + edge, rect.bottom };
        const RECT rightBand = { rect.right - edge, rect.top, rect.right, rect.bottom };

        auto drawBand = [&](const RECT& band, float offsetX, float offsetY, float bandOpacity) {
            if (band.right <= band.left || band.bottom <= band.top) return;
            const auto source = SpotlightBackdropAlignedSourceRect(band, offsetX, offsetY);
            const auto destRect = D2DRect(band);
            target->DrawBitmap(bitmap, &destRect, bandOpacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        };

        drawBand(leftBand, -sideShift, 0.0f, opacity * 0.76f);
        drawBand(rightBand, sideShift, 0.0f, opacity * 0.76f);

        target->PopLayer();
    }

    SafeRelease(layer);
    SafeRelease(mask);
}

void DrawSearchEdgeRefraction(HDC dc, const RECT& rect, bool active)
{
    if (!dc || !g_spotlightBackdrop.dc || g_spotlightBackdrop.width <= 0 || g_spotlightBackdrop.height <= 0)
    {
        return;
    }

    const int saved = SaveDC(dc);
    HRGN clip = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, SpotlightSearchCornerRadius * 2, SpotlightSearchCornerRadius * 2);
    if (clip)
    {
        SelectClipRgn(dc, clip);
    }

    const int rectHeight = static_cast<int>(rect.bottom - rect.top);
    const int edge = std::max(12, std::min(20, rectHeight / 3));
    const int sideShift = active ? 2 : 1;
    const BYTE alpha = static_cast<BYTE>(active ? 44 : 30);
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, alpha, 0 };
    const RECT bands[] = {
        { rect.left, rect.top, rect.left + edge, rect.bottom },
        { rect.right - edge, rect.top, rect.right, rect.bottom }
    };
    const POINT offsets[] = {
        { -sideShift, 0 },
        { sideShift, 0 }
    };

    for (int i = 0; i < 2; ++i)
    {
        const RECT& band = bands[i];
        if (band.right <= band.left || band.bottom <= band.top) continue;
        const RECT source = SpotlightBackdropAlignedSourceRectGdi(band, offsets[i].x, offsets[i].y);
        AlphaBlend(
            dc,
            band.left,
            band.top,
            band.right - band.left,
            band.bottom - band.top,
            g_spotlightBackdrop.dc,
            source.left,
            source.top,
            source.right - source.left,
            source.bottom - source.top,
            blend);
    }

    if (clip)
    {
        DeleteObject(clip);
    }
    RestoreDC(dc, saved);
}

void DrawSearchHorizontalGlassRim(ID2D1DCRenderTarget* target, const D2D1_ROUNDED_RECT& rounded, const RECT& rect, bool active)
{
    if (!target || !g_d2dFactory) return;

    ID2D1RoundedRectangleGeometry* mask = nullptr;
    ID2D1Layer* layer = nullptr;
    if (SUCCEEDED(g_d2dFactory->CreateRoundedRectangleGeometry(rounded, &mask))
        && SUCCEEDED(target->CreateLayer(nullptr, &layer))
        && mask
        && layer)
    {
        const float rim = active ? 8.0f : 6.0f;
        target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask), layer);

        ID2D1GradientStopCollection* topStops = nullptr;
        ID2D1LinearGradientBrush* topBrush = nullptr;
        D2D1_GRADIENT_STOP top[] = {
            { 0.0f, D2DColorAlpha(RGB(255, 255, 255), active ? 0.52f : 0.36f) },
            { 0.55f, D2DColorAlpha(RGB(190, 232, 255), active ? 0.18f : 0.10f) },
            { 1.0f, D2DColorAlpha(RGB(255, 255, 255), 0.0f) }
        };
        if (SUCCEEDED(target->CreateGradientStopCollection(top, ARRAYSIZE(top), &topStops)) && topStops)
        {
            const auto props = D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)),
                D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top) + rim));
            target->CreateLinearGradientBrush(props, topStops, &topBrush);
        }
        if (topBrush)
        {
            target->FillRectangle(
                D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.right), static_cast<float>(rect.top) + rim),
                topBrush);
        }

        ID2D1GradientStopCollection* bottomStops = nullptr;
        ID2D1LinearGradientBrush* bottomBrush = nullptr;
        D2D1_GRADIENT_STOP bottom[] = {
            { 0.0f, D2DColorAlpha(RGB(118, 186, 232), 0.0f) },
            { 0.45f, D2DColorAlpha(RGB(138, 204, 246), active ? 0.12f : 0.070f) },
            { 1.0f, D2DColorAlpha(RGB(232, 250, 255), active ? 0.28f : 0.18f) }
        };
        if (SUCCEEDED(target->CreateGradientStopCollection(bottom, ARRAYSIZE(bottom), &bottomStops)) && bottomStops)
        {
            const auto props = D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.bottom) - rim),
                D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.bottom)));
            target->CreateLinearGradientBrush(props, bottomStops, &bottomBrush);
        }
        if (bottomBrush)
        {
            target->FillRectangle(
                D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.bottom) - rim, static_cast<float>(rect.right), static_cast<float>(rect.bottom)),
                bottomBrush);
        }

        target->PopLayer();

        SafeRelease(bottomBrush);
        SafeRelease(bottomStops);
        SafeRelease(topBrush);
        SafeRelease(topStops);
    }

    SafeRelease(layer);
    SafeRelease(mask);
}

void DrawSearchHorizontalGlassRim(HDC dc, const RECT& rect, bool active)
{
    const int saved = SaveDC(dc);
    HRGN clip = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, SpotlightSearchCornerRadius * 2, SpotlightSearchCornerRadius * 2);
    if (clip)
    {
        SelectClipRgn(dc, clip);
    }

    HPEN topPen = CreatePen(PS_SOLID, active ? 3 : 2, active ? RGB(246, 254, 255) : RGB(220, 242, 252));
    HPEN bottomPen = CreatePen(PS_SOLID, active ? 2 : 1, active ? RGB(166, 226, 255) : RGB(132, 190, 226));
    HGDIOBJ oldPen = SelectObject(dc, topPen);
    MoveToEx(dc, rect.left + SpotlightSearchCornerRadius, rect.top + 2, nullptr);
    LineTo(dc, rect.right - SpotlightSearchCornerRadius, rect.top + 2);
    SelectObject(dc, bottomPen);
    MoveToEx(dc, rect.left + SpotlightSearchCornerRadius, rect.bottom - 2, nullptr);
    LineTo(dc, rect.right - SpotlightSearchCornerRadius, rect.bottom - 2);
    SelectObject(dc, oldPen);
    DeleteObject(bottomPen);
    DeleteObject(topPen);

    if (clip)
    {
        DeleteObject(clip);
    }
    RestoreDC(dc, saved);
}

void DrawSearchFocusGlow(ID2D1DCRenderTarget* target, const RECT& rect, bool active)
{
    ID2D1SolidColorBrush* haloBrush = nullptr;
    ID2D1SolidColorBrush* innerBrush = nullptr;
    const float activeBoost = active ? 1.0f : 0.58f;
    target->CreateSolidColorBrush(D2DColorAlpha(RGB(126, 210, 255), 0.18f * activeBoost), &haloBrush);
    target->CreateSolidColorBrush(D2DColorAlpha(RGB(244, 252, 255), 0.10f * activeBoost), &innerBrush);
    if (haloBrush)
    {
        const auto halo = D2D1::RoundedRect(
            D2D1::RectF(static_cast<float>(rect.left) - 1.5f, static_cast<float>(rect.top) - 1.5f, static_cast<float>(rect.right) + 1.5f, static_cast<float>(rect.bottom) + 1.5f),
            static_cast<float>(SpotlightSearchCornerRadius + 2),
            static_cast<float>(SpotlightSearchCornerRadius + 2));
        target->DrawRoundedRectangle(halo, haloBrush, active ? 3.0f : 2.0f);
    }
    if (innerBrush)
    {
        const auto inner = D2D1::RoundedRect(
            D2D1::RectF(static_cast<float>(rect.left) + 3.0f, static_cast<float>(rect.top) + 3.0f, static_cast<float>(rect.right) - 3.0f, static_cast<float>(rect.bottom) - 3.0f),
            static_cast<float>(SpotlightSearchCornerRadius - 3),
            static_cast<float>(SpotlightSearchCornerRadius - 3));
        target->DrawRoundedRectangle(inner, innerBrush, 1.0f);
    }
    SafeRelease(innerBrush);
    SafeRelease(haloBrush);
}

void DrawSearchFocusGlow(HDC dc, const RECT& rect, bool active)
{
    HPEN haloPen = CreatePen(PS_SOLID, active ? 3 : 2, active ? RGB(126, 210, 255) : RGB(94, 144, 184));
    HGDIOBJ oldPen = SelectObject(dc, haloPen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc, rect.left - 1, rect.top - 1, rect.right + 1, rect.bottom + 1, (SpotlightSearchCornerRadius + 2) * 2, (SpotlightSearchCornerRadius + 2) * 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(haloPen);
}

void DrawLiquidGlassFocusLens(ID2D1DCRenderTarget* target, const D2D1_ROUNDED_RECT& rounded, const RECT& rect, float magnification, float opacity, float shiftX, float shiftY)
{
    ID2D1Bitmap* bitmap = GetSpotlightBackdropD2dBitmap(target);
    if (!bitmap)
    {
        return;
    }

    ID2D1RoundedRectangleGeometry* mask = nullptr;
    ID2D1Layer* layer = nullptr;
    if (g_d2dFactory
        && SUCCEEDED(g_d2dFactory->CreateRoundedRectangleGeometry(rounded, &mask))
        && SUCCEEDED(target->CreateLayer(nullptr, &layer))
        && mask
        && layer)
    {
        const auto sourceRect = SpotlightBackdropSourceRectForLens(rect, magnification, shiftX, shiftY);
        const auto destRect = D2DRect(rect);
        target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask), layer);
        target->DrawBitmap(bitmap, &destRect, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &sourceRect);
        target->PopLayer();
    }

    SafeRelease(layer);
    SafeRelease(mask);
}

void DrawLiquidGlassFocusLens(HDC dc, const RECT& rect, int radius, float magnification, int shiftX, int shiftY)
{
    if (!dc || !g_spotlightBackdrop.dc || g_spotlightBackdrop.width <= 0 || g_spotlightBackdrop.height <= 0)
    {
        return;
    }

    const int saved = SaveDC(dc);
    HRGN clip = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1, radius * 2, radius * 2);
    if (clip)
    {
        SelectClipRgn(dc, clip);
    }

    const RECT source = SpotlightBackdropSourceRectForLensGdi(rect, magnification, shiftX, shiftY);
    const int previousMode = SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(
        dc,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        g_spotlightBackdrop.dc,
        source.left,
        source.top,
        source.right - source.left,
        source.bottom - source.top,
        SRCCOPY);
    SetStretchBltMode(dc, previousMode);

    if (clip)
    {
        DeleteObject(clip);
    }
    RestoreDC(dc, saved);
}

void DrawSearchFocusLens(ID2D1DCRenderTarget* target, const D2D1_ROUNDED_RECT& rounded, const RECT& rect, bool active)
{
    DrawAlignedSearchBackdrop(target, rounded, rect, active);
    DrawSearchEdgeRefraction(target, rounded, rect, active);
}

void DrawSearchFocusLens(HDC dc, const RECT& rect, bool active)
{
    DrawAlignedSearchBackdrop(dc, rect, active);
    DrawSearchEdgeRefraction(dc, rect, active);
}

void DrawFocusedTileLens(ID2D1DCRenderTarget* target, const D2D1_ROUNDED_RECT& rounded, const RECT& rect)
{
    DrawLiquidGlassFocusLens(target, rounded, rect, 1.0f, 0.30f, 0.0f, 0.0f);
}

void DrawFocusedTileLens(HDC dc, const RECT& rect)
{
    DrawLiquidGlassFocusLens(dc, rect, 14, 1.0f, 0, 0);
}

void DrawSearchLiquidSpecular(ID2D1DCRenderTarget* target, const D2D1_ROUNDED_RECT& rounded, const RECT& rect, bool active)
{
    ID2D1GradientStopCollection* sheenStops = nullptr;
    ID2D1LinearGradientBrush* sheenBrush = nullptr;
    D2D1_GRADIENT_STOP sheen[] = {
        { 0.0f, D2DColorAlpha(RGB(246, 252, 255), active ? 0.18f : 0.12f) },
        { 0.24f, D2DColorAlpha(RGB(164, 216, 255), active ? 0.080f : 0.050f) },
        { 0.48f, D2DColorAlpha(RGB(255, 255, 255), 0.0f) },
        { 1.0f, D2DColorAlpha(RGB(255, 255, 255), 0.0f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(sheen, ARRAYSIZE(sheen), &sheenStops)) && sheenStops)
    {
        const auto props = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)),
            D2D1::Point2F(static_cast<float>(rect.right), static_cast<float>(rect.bottom)));
        target->CreateLinearGradientBrush(props, sheenStops, &sheenBrush);
    }
    if (sheenBrush)
    {
        target->FillRoundedRectangle(rounded, sheenBrush);
    }

    ID2D1GradientStopCollection* shadowStops = nullptr;
    ID2D1LinearGradientBrush* shadowBrush = nullptr;
    D2D1_GRADIENT_STOP shadow[] = {
        { 0.0f, D2DColorAlpha(RGB(1, 5, 12), 0.0f) },
        { 0.60f, D2DColorAlpha(RGB(1, 5, 12), 0.0f) },
        { 1.0f, D2DColorAlpha(RGB(1, 5, 12), active ? 0.14f : 0.10f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(shadow, ARRAYSIZE(shadow), &shadowStops)) && shadowStops)
    {
        const auto props = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)),
            D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.bottom)));
        target->CreateLinearGradientBrush(props, shadowStops, &shadowBrush);
    }
    if (shadowBrush)
    {
        target->FillRoundedRectangle(rounded, shadowBrush);
    }

    SafeRelease(shadowBrush);
    SafeRelease(shadowStops);
    SafeRelease(sheenBrush);
    SafeRelease(sheenStops);
}

void DrawSearchGlassSurface(ID2D1DCRenderTarget* target, const RECT& rect, bool active)
{
    const auto rounded = D2D1::RoundedRect(D2DRect(rect), static_cast<float>(SpotlightSearchCornerRadius), static_cast<float>(SpotlightSearchCornerRadius));

    DrawSearchFocusLens(target, rounded, rect, active);
    DrawSearchLiquidMaterialWash(target, rounded, rect, active);
    DrawSearchHorizontalGlassRim(target, rounded, rect, active);

    ID2D1GradientStopCollection* fillStops = nullptr;
    ID2D1LinearGradientBrush* fillBrush = nullptr;
    D2D1_GRADIENT_STOP stops[] = {
        { 0.0f, D2DColorAlpha(RGB(250, 254, 255), active ? 0.18f : 0.12f) },
        { 0.46f, D2DColorAlpha(RGB(194, 230, 255), active ? 0.12f : 0.075f) },
        { 1.0f, D2DColorAlpha(RGB(76, 144, 196), active ? 0.13f : 0.085f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(stops, ARRAYSIZE(stops), &fillStops)) && fillStops)
    {
        const auto props = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)),
            D2D1::Point2F(static_cast<float>(rect.right), static_cast<float>(rect.bottom)));
        target->CreateLinearGradientBrush(props, fillStops, &fillBrush);
    }

    if (fillBrush)
    {
        target->FillRoundedRectangle(rounded, fillBrush);
    }

    ID2D1GradientStopCollection* glowStops = nullptr;
    ID2D1RadialGradientBrush* glowBrush = nullptr;
    D2D1_GRADIENT_STOP glow[] = {
        { 0.0f, D2DColorAlpha(active ? RGB(204, 238, 255) : RGB(220, 238, 250), active ? 0.18f : 0.08f) },
        { 0.62f, D2DColorAlpha(RGB(126, 190, 232), active ? 0.055f : 0.026f) },
        { 1.0f, D2DColorAlpha(RGB(255, 255, 255), 0.0f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(glow, ARRAYSIZE(glow), &glowStops)) && glowStops)
    {
        const auto props = D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(rect.left) + (rect.right - rect.left) * 0.20f, static_cast<float>(rect.top) + (rect.bottom - rect.top) * 0.28f),
            D2D1::Point2F(0.0f, 0.0f),
            static_cast<float>(rect.right - rect.left) * 0.54f,
            static_cast<float>(rect.bottom - rect.top) * 1.12f);
        target->CreateRadialGradientBrush(props, glowStops, &glowBrush);
    }
    if (glowBrush)
    {
        target->FillRoundedRectangle(rounded, glowBrush);
    }

    DrawSearchLiquidSpecular(target, rounded, rect, active);
    DrawSearchFocusGlow(target, rect, active);

    ID2D1SolidColorBrush* strokeBrush = nullptr;
    ID2D1SolidColorBrush* innerBrush = nullptr;
    target->CreateSolidColorBrush(D2DColorAlpha(active ? RGB(224, 248, 255) : RGB(196, 232, 252), active ? 0.74f : 0.42f), &strokeBrush);
    target->CreateSolidColorBrush(D2DColorAlpha(RGB(255, 255, 255), active ? 0.22f : 0.13f), &innerBrush);
    if (strokeBrush)
    {
        target->DrawRoundedRectangle(rounded, strokeBrush, active ? 1.20f : 0.90f);
    }
    if (innerBrush)
    {
        target->DrawRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(static_cast<float>(rect.left) + 2.0f, static_cast<float>(rect.top) + 2.0f, static_cast<float>(rect.right) - 2.0f, static_cast<float>(rect.bottom) - 2.0f),
            static_cast<float>(SpotlightSearchCornerRadius - 2),
            static_cast<float>(SpotlightSearchCornerRadius - 2)), innerBrush, 1.0f);
    }
    SafeRelease(innerBrush);
    SafeRelease(strokeBrush);
    SafeRelease(glowBrush);
    SafeRelease(glowStops);
    SafeRelease(fillBrush);
    SafeRelease(fillStops);
}

void DrawSearchGlassSurface(HDC dc, const RECT& rect, bool active)
{
    DrawSearchFocusLens(dc, rect, active);
    DrawSearchLiquidMaterialWash(dc, rect, active);
    DrawSearchHorizontalGlassRim(dc, rect, active);

    HBRUSH fill = CreateSolidBrush(active ? RGB(170, 214, 242) : RGB(136, 178, 210));
    HPEN pen = CreatePen(PS_SOLID, 1, active ? RGB(222, 248, 255) : RGB(180, 224, 248));
    HGDIOBJ oldBrush = SelectObject(dc, fill);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, SpotlightSearchCornerRadius * 2, SpotlightSearchCornerRadius * 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(fill);
    DrawSearchFocusGlow(dc, rect, active);
}

void DrawAppTileSurface(ID2D1DCRenderTarget* target, const RECT& rect, TileVisualState state)
{
    if (state == TileVisualState::Normal) return;
    if (state != TileVisualState::KeyboardFocus)
    {
        COLORREF fill = RGB(28, 45, 64);
        COLORREF rim = RGB(82, 108, 136);
        if (state == TileVisualState::Selected) { fill = RGB(34, 82, 112); rim = RGB(112, 185, 226); }
        else if (state == TileVisualState::MultiSelected) { fill = RGB(45, 72, 116); rim = RGB(150, 174, 238); }
        else if (state == TileVisualState::Pressed) { fill = RGB(22, 54, 82); rim = RGB(154, 210, 244); }
        else if (state == TileVisualState::DragSource) { fill = RGB(70, 62, 46); rim = RGB(210, 180, 116); }
        else if (state == TileVisualState::DropTarget) { fill = RGB(36, 78, 68); rim = RGB(116, 202, 174); }
        DrawRoundedRectDirect(target, rect, fill, rim, 14.0f);
        if (state == TileVisualState::MultiSelected)
        {
            RECT inner = { rect.left + 3, rect.top + 3, rect.right - 3, rect.bottom - 3 };
            DrawRoundedRectDirect(target, inner, fill, RGB(205, 218, 255), 11.0f);
        }
        return;
    }

    const auto rounded = D2D1::RoundedRect(D2DRect(rect), 14.0f, 14.0f);
    DrawFocusedTileLens(target, rounded, rect);

    ID2D1GradientStopCollection* fillStops = nullptr;
    ID2D1LinearGradientBrush* fillBrush = nullptr;
    D2D1_GRADIENT_STOP stops[] = {
        { 0.0f, D2DColorAlpha(RGB(132, 174, 210), 0.22f) },
        { 0.46f, D2DColorAlpha(RGB(42, 86, 124), 0.28f) },
        { 1.0f, D2DColorAlpha(RGB(8, 22, 42), 0.38f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(stops, ARRAYSIZE(stops), &fillStops)) && fillStops)
    {
        const auto props = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)),
            D2D1::Point2F(static_cast<float>(rect.right), static_cast<float>(rect.bottom)));
        target->CreateLinearGradientBrush(props, fillStops, &fillBrush);
    }
    if (fillBrush)
    {
        target->FillRoundedRectangle(rounded, fillBrush);
    }

    ID2D1SolidColorBrush* rimBrush = nullptr;
    ID2D1SolidColorBrush* innerBrush = nullptr;
    target->CreateSolidColorBrush(D2DColorAlpha(RGB(172, 226, 255), 0.58f), &rimBrush);
    target->CreateSolidColorBrush(D2DColorAlpha(RGB(255, 255, 255), 0.12f), &innerBrush);
    if (rimBrush)
    {
        target->DrawRoundedRectangle(rounded, rimBrush, 1.35f);
    }
    if (innerBrush)
    {
        target->DrawRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(static_cast<float>(rect.left) + 2.0f, static_cast<float>(rect.top) + 2.0f, static_cast<float>(rect.right) - 2.0f, static_cast<float>(rect.bottom) - 2.0f),
            12.0f,
            12.0f), innerBrush, 1.0f);
    }
    SafeRelease(innerBrush);
    SafeRelease(rimBrush);
    SafeRelease(fillBrush);
    SafeRelease(fillStops);
}

void DrawIconBackplateDirect(ID2D1DCRenderTarget* target, const RECT& rect, bool selected)
{
    if (IsSpotlightBaseLayerPass()) return;

    const float tileWidth = static_cast<float>(rect.right - rect.left);
    const float icon = static_cast<float>(g_settings.iconSize);
    const D2D1_POINT_2F center = D2D1::Point2F(
        static_cast<float>(rect.left) + tileWidth * 0.5f,
        static_cast<float>(rect.top) + 14.0f + icon * 0.54f);

    ID2D1GradientStopCollection* shadowStops = nullptr;
    ID2D1RadialGradientBrush* shadowBrush = nullptr;
    D2D1_GRADIENT_STOP shadow[] = {
        { 0.0f, D2DColorAlpha(RGB(4, 8, 14), selected ? 0.22f : 0.15f) },
        { 0.55f, D2DColorAlpha(RGB(4, 8, 14), selected ? 0.10f : 0.060f) },
        { 1.0f, D2DColorAlpha(RGB(4, 8, 14), 0.0f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(shadow, ARRAYSIZE(shadow), &shadowStops)) && shadowStops)
    {
        const auto props = D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(center.x, center.y + icon * 0.16f),
            D2D1::Point2F(0.0f, 0.0f),
            icon * 0.78f,
            icon * 0.64f);
        target->CreateRadialGradientBrush(props, shadowStops, &shadowBrush);
    }
    if (shadowBrush)
    {
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x, center.y + icon * 0.16f), icon * 0.78f, icon * 0.64f), shadowBrush);
    }

    ID2D1GradientStopCollection* glowStops = nullptr;
    ID2D1RadialGradientBrush* glowBrush = nullptr;
    D2D1_GRADIENT_STOP glow[] = {
        { 0.0f, D2DColorAlpha(RGB(196, 226, 248), selected ? 0.18f : 0.105f) },
        { 0.42f, D2DColorAlpha(RGB(126, 170, 206), selected ? 0.080f : 0.040f) },
        { 1.0f, D2DColorAlpha(RGB(255, 255, 255), 0.0f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(glow, ARRAYSIZE(glow), &glowStops)) && glowStops)
    {
        const auto props = D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(center.x - icon * 0.10f, center.y - icon * 0.12f),
            D2D1::Point2F(0.0f, 0.0f),
            icon * 0.72f,
            icon * 0.56f);
        target->CreateRadialGradientBrush(props, glowStops, &glowBrush);
    }
    if (glowBrush)
    {
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x - icon * 0.10f, center.y - icon * 0.12f), icon * 0.72f, icon * 0.56f), glowBrush);
    }

    SafeRelease(glowBrush);
    SafeRelease(glowStops);
    SafeRelease(shadowBrush);
    SafeRelease(shadowStops);
}

void DrawAppTileSurface(HDC dc, const RECT& rect, TileVisualState state)
{
    if (state == TileVisualState::Normal) return;
    if (state == TileVisualState::KeyboardFocus)
    {
        DrawFocusedTileLens(dc, rect);
        DrawRoundedRect(dc, rect, RGB(42, 82, 118), RGB(130, 190, 255), 12);
        return;
    }
    COLORREF fill = RGB(28, 45, 64);
    COLORREF rim = RGB(82, 108, 136);
    if (state == TileVisualState::Selected) { fill = RGB(34, 82, 112); rim = RGB(112, 185, 226); }
    else if (state == TileVisualState::MultiSelected) { fill = RGB(45, 72, 116); rim = RGB(150, 174, 238); }
    else if (state == TileVisualState::Pressed) { fill = RGB(22, 54, 82); rim = RGB(154, 210, 244); }
    else if (state == TileVisualState::DragSource) { fill = RGB(70, 62, 46); rim = RGB(210, 180, 116); }
    else if (state == TileVisualState::DropTarget) { fill = RGB(36, 78, 68); rim = RGB(116, 202, 174); }
    DrawRoundedRect(dc, rect, fill, rim, 12);
    if (state == TileVisualState::MultiSelected)
    {
        RECT inner = { rect.left + 3, rect.top + 3, rect.right - 3, rect.bottom - 3 };
        DrawRoundedRect(dc, inner, fill, RGB(205, 218, 255), 10);
    }
}

void DrawTextDirect(
    ID2D1DCRenderTarget* target,
    IDWriteTextFormat* format,
    const std::wstring& text,
    const RECT& rect,
    COLORREF color,
    DWRITE_TEXT_ALIGNMENT alignment,
    DWRITE_PARAGRAPH_ALIGNMENT paragraph,
    DWRITE_WORD_WRAPPING wrapping)
{
    ID2D1SolidColorBrush* brush = nullptr;
    target->CreateSolidColorBrush(D2DColor(color), &brush);
    if (!brush) return;
    format->SetTextAlignment(alignment);
    format->SetParagraphAlignment(paragraph);
    format->SetWordWrapping(wrapping);
    target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, D2DRect(rect), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    SafeRelease(brush);
}

float MeasureTextWidthDirect(IDWriteTextFormat* format, const std::wstring& text)
{
    if (!g_dwriteFactory || !format || text.empty()) return 0.0f;

    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = g_dwriteFactory->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        format,
        4096.0f,
        64.0f,
        &layout);
    if (FAILED(hr) || !layout) return 0.0f;

    DWRITE_TEXT_METRICS metrics = {};
    layout->GetMetrics(&metrics);
    SafeRelease(layout);
    return metrics.widthIncludingTrailingWhitespace;
}

int MeasureTextWidthGdi(HDC dc, const std::wstring& text)
{
    if (text.empty()) return 0;
    SIZE size = {};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    return size.cx;
}

void UpdateSearchTextOffset(int caretWidth, int totalWidth, int visibleWidth)
{
    if (totalWidth <= visibleWidth)
    {
        g_searchTextOffset = 0;
        return;
    }
    const int margin = 6;
    if (caretWidth - g_searchTextOffset > visibleWidth - margin)
        g_searchTextOffset = caretWidth - visibleWidth + margin;
    else if (caretWidth - g_searchTextOffset < margin)
        g_searchTextOffset = std::max(0, caretWidth - margin);
    g_searchTextOffset = std::clamp(g_searchTextOffset, 0, std::max(0, totalWidth - visibleWidth + margin));
}

TileVisualState ResolveTileVisualState(const LaunchItem& item, int filteredIndex)
{
    if (g_dragActive && ContainsPath(g_dragSourcePaths, item.sourcePath)) return TileVisualState::DragSource;
    if (IsDropHintItem(item)) return TileVisualState::DropTarget;
    if (filteredIndex == g_pressedFilteredIndex) return TileVisualState::Pressed;
    if (!IsSpotlightMode() && g_launchpadState.IsEditing() && IsSelected(item.sourcePath))
    {
        return g_selectedSourcePaths.size() > 1 ? TileVisualState::MultiSelected : TileVisualState::Selected;
    }
    if (filteredIndex == g_selectedFilteredIndex
        && (IsSpotlightMode() ? g_spotlightState.Focus() == llp::interaction::FocusArea::Results : !g_searchInputActive))
    {
        return TileVisualState::KeyboardFocus;
    }
    if (filteredIndex == g_hoveredFilteredIndex) return TileVisualState::Hover;
    return TileVisualState::Normal;
}

RECT SearchCaretRect(const RECT& textRect, int prefixWidth)
{
    const int x = std::clamp(textRect.left + prefixWidth + 2, textRect.left + 1, textRect.right - 2);
    return { x, textRect.top + 15, x + 2, textRect.bottom - 15 };
}

void DrawSearchCaret(ID2D1DCRenderTarget* target, const RECT& textRect, int prefixWidth)
{
    if (!g_searchInputActive) return;
    ID2D1SolidColorBrush* brush = nullptr;
    target->CreateSolidColorBrush(D2DColor(RGB(34, 54, 72)), &brush);
    if (brush)
    {
        target->FillRectangle(D2DRect(SearchCaretRect(textRect, prefixWidth)), brush);
    }
    SafeRelease(brush);
}

void DrawSearchCaret(HDC dc, const RECT& textRect, int prefixWidth)
{
    if (!g_searchInputActive) return;
    const RECT caret = SearchCaretRect(textRect, prefixWidth);
    HBRUSH brush = CreateSolidBrush(RGB(34, 54, 72));
    FillRect(dc, &caret, brush);
    DeleteObject(brush);
}

void DrawSearchSurface(ID2D1DCRenderTarget* target, const RECT& client)
{
    const RECT searchRect = SearchRect(client);
    const RECT textRect = SearchTextRect(searchRect);
    DrawSearchGlassSurface(target, searchRect, g_searchInputActive);
    if (IsSpotlightBaseLayerPass()) return;
    const std::wstring displayText = SearchDisplayText();
    const size_t displayCaret = std::min(g_searchInput.DisplayCaret(), displayText.size());
    const int caretWidth = static_cast<int>(std::ceil(MeasureTextWidthDirect(g_searchTextFormat, displayText.substr(0, displayCaret))));
    const int totalWidth = static_cast<int>(std::ceil(MeasureTextWidthDirect(g_searchTextFormat, displayText)));
    const int visibleWidth = std::max(1L, textRect.right - textRect.left - 4);
    UpdateSearchTextOffset(caretWidth, totalWidth, visibleWidth);
    RECT drawRect = { textRect.left - g_searchTextOffset, textRect.top, textRect.left - g_searchTextOffset + std::max(visibleWidth, totalWidth + 8), textRect.bottom };
    target->PushAxisAlignedClip(D2DRect(textRect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (g_searchInput.HasSelection() && !g_searchInput.IsComposing())
    {
        const int selectionStart = static_cast<int>(std::ceil(MeasureTextWidthDirect(g_searchTextFormat, displayText.substr(0, g_searchInput.SelectionStart()))));
        const int selectionEnd = static_cast<int>(std::ceil(MeasureTextWidthDirect(g_searchTextFormat, displayText.substr(0, g_searchInput.SelectionEnd()))));
        RECT selectionRect = { drawRect.left + selectionStart, textRect.top + 8, drawRect.left + selectionEnd, textRect.bottom - 8 };
        ID2D1SolidColorBrush* selectionBrush = nullptr;
        target->CreateSolidColorBrush(D2DColorAlpha(RGB(58, 140, 255), 0.28f), &selectionBrush);
        if (selectionBrush) target->FillRectangle(D2DRect(selectionRect), selectionBrush);
        SafeRelease(selectionBrush);
    }
    DrawTextDirect(
        target,
        g_searchTextFormat,
        displayText,
        drawRect,
        displayText.empty() ? RGB(110, 132, 150) : RGB(34, 54, 72),
        DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
        DWRITE_WORD_WRAPPING_NO_WRAP);
    if (g_searchInput.IsComposing() && g_searchInput.CompositionEnd() > g_searchInput.CompositionStart())
    {
        const int compositionStart = static_cast<int>(std::ceil(MeasureTextWidthDirect(g_searchTextFormat, displayText.substr(0, g_searchInput.CompositionStart()))));
        const int compositionEnd = static_cast<int>(std::ceil(MeasureTextWidthDirect(g_searchTextFormat, displayText.substr(0, g_searchInput.CompositionEnd()))));
        ID2D1SolidColorBrush* compositionBrush = nullptr;
        target->CreateSolidColorBrush(D2DColor(RGB(58, 120, 210)), &compositionBrush);
        if (compositionBrush)
        {
            target->DrawLine(
                D2D1::Point2F(static_cast<float>(drawRect.left + compositionStart), static_cast<float>(textRect.bottom - 10)),
                D2D1::Point2F(static_cast<float>(drawRect.left + compositionEnd), static_cast<float>(textRect.bottom - 10)),
                compositionBrush,
                1.5f);
        }
        SafeRelease(compositionBrush);
    }
    DrawSearchCaret(target, textRect, caretWidth - g_searchTextOffset);
    target->PopAxisAlignedClip();
}

void DrawSearchSurface(HDC dc, const RECT& client)
{
    HFONT searchFont = CreateFontW(26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    const RECT searchRect = SearchRect(client);
    const RECT textRect = SearchTextRect(searchRect);
    DrawSearchGlassSurface(dc, searchRect, g_searchInputActive);
    if (IsSpotlightBaseLayerPass())
    {
        DeleteObject(searchFont);
        return;
    }
    HGDIOBJ oldFont = SelectObject(dc, searchFont);
    const std::wstring displayText = SearchDisplayText();
    const size_t displayCaret = std::min(g_searchInput.DisplayCaret(), displayText.size());
    const int caretWidth = MeasureTextWidthGdi(dc, displayText.substr(0, displayCaret));
    const int totalWidth = MeasureTextWidthGdi(dc, displayText);
    const int visibleWidth = std::max(1L, textRect.right - textRect.left - 4);
    UpdateSearchTextOffset(caretWidth, totalWidth, visibleWidth);
    RECT drawRect = { textRect.left - g_searchTextOffset, textRect.top, textRect.left - g_searchTextOffset + std::max(visibleWidth, totalWidth + 8), textRect.bottom };
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, textRect.left, textRect.top, textRect.right, textRect.bottom);
    if (g_searchInput.HasSelection() && !g_searchInput.IsComposing())
    {
        const int selectionStart = MeasureTextWidthGdi(dc, displayText.substr(0, g_searchInput.SelectionStart()));
        const int selectionEnd = MeasureTextWidthGdi(dc, displayText.substr(0, g_searchInput.SelectionEnd()));
        RECT selectionRect = { drawRect.left + selectionStart, textRect.top + 8, drawRect.left + selectionEnd, textRect.bottom - 8 };
        HBRUSH selectionBrush = CreateSolidBrush(RGB(188, 218, 250));
        FillRect(dc, &selectionRect, selectionBrush);
        DeleteObject(selectionBrush);
    }
    DrawTextClipped(dc, displayText, drawRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER, displayText.empty() ? RGB(110, 132, 150) : RGB(34, 54, 72));
    if (g_searchInput.IsComposing() && g_searchInput.CompositionEnd() > g_searchInput.CompositionStart())
    {
        const int compositionStart = MeasureTextWidthGdi(dc, displayText.substr(0, g_searchInput.CompositionStart()));
        const int compositionEnd = MeasureTextWidthGdi(dc, displayText.substr(0, g_searchInput.CompositionEnd()));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(58, 120, 210));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, drawRect.left + compositionStart, textRect.bottom - 10, nullptr);
        LineTo(dc, drawRect.left + compositionEnd, textRect.bottom - 10);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    DrawSearchCaret(dc, textRect, caretWidth - g_searchTextOffset);
    RestoreDC(dc, savedDc);
    SelectObject(dc, oldFont);
    DeleteObject(searchFont);
}

void DrawSpotlightAuxiliary(ID2D1DCRenderTarget* target, const RECT& client)
{
    if (!IsSpotlightMode() || IsSpotlightBaseLayerPass()) return;
    const bool focused = g_spotlightState.Focus() == llp::interaction::FocusArea::Auxiliary;
    const RECT rect = SpotlightAuxiliaryRect(client);
    DrawRoundedRectDirect(
        target,
        rect,
        focused ? RGB(110, 174, 232) : RGB(70, 91, 116),
        focused ? RGB(224, 243, 255) : RGB(138, 164, 190),
        18.0f);
    DrawTextDirect(
        target,
        g_tileTextFormat,
        L"...",
        rect,
        focused ? RGB(18, 48, 74) : RGB(232, 241, 250),
        DWRITE_TEXT_ALIGNMENT_CENTER,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
        DWRITE_WORD_WRAPPING_NO_WRAP);
}

void DrawSpotlightAuxiliary(HDC dc, const RECT& client)
{
    if (!IsSpotlightMode() || IsSpotlightBaseLayerPass()) return;
    const bool focused = g_spotlightState.Focus() == llp::interaction::FocusArea::Auxiliary;
    const RECT rect = SpotlightAuxiliaryRect(client);
    DrawRoundedRect(
        dc,
        rect,
        focused ? RGB(110, 174, 232) : RGB(70, 91, 116),
        focused ? RGB(224, 243, 255) : RGB(138, 164, 190),
        18);
    HFONT font = CreateFontW(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    DrawTextClipped(dc, L"...", rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER, focused ? RGB(18, 48, 74) : RGB(232, 241, 250));
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void DrawLaunchpadModeControl(ID2D1DCRenderTarget* target, const RECT& client)
{
    if (IsSpotlightMode() || IsSpotlightBaseLayerPass()) return;
    const bool editing = g_launchpadState.IsEditing();
    const RECT rect = LaunchpadEditRect(client);
    DrawRoundedRectDirect(
        target,
        rect,
        editing ? RGB(86, 158, 224) : RGB(39, 57, 78),
        editing ? RGB(220, 241, 255) : RGB(128, 151, 178),
        18.0f);
    DrawTextDirect(
        target,
        g_tileTextFormat,
        editing ? L"Done" : L"Edit",
        rect,
        editing ? RGB(18, 48, 74) : RGB(232, 241, 250),
        DWRITE_TEXT_ALIGNMENT_CENTER,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
        DWRITE_WORD_WRAPPING_NO_WRAP);
}

void DrawLaunchpadModeControl(HDC dc, const RECT& client)
{
    if (IsSpotlightMode() || IsSpotlightBaseLayerPass()) return;
    const bool editing = g_launchpadState.IsEditing();
    const RECT rect = LaunchpadEditRect(client);
    DrawRoundedRect(
        dc,
        rect,
        editing ? RGB(86, 158, 224) : RGB(39, 57, 78),
        editing ? RGB(220, 241, 255) : RGB(128, 151, 178),
        18);
    HFONT font = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    DrawTextClipped(dc, editing ? L"Done" : L"Edit", rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER, editing ? RGB(18, 48, 74) : RGB(232, 241, 250));
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

struct PendingIconDraw
{
    HICON icon = nullptr;
    int x = 0;
    int y = 0;
    int size = 0;
    BYTE alpha = 255;
};

bool GetIconOpaqueBounds(HICON icon, RECT& bounds, int& width, int& height)
{
    ICONINFO iconInfo = {};
    if (!icon || !GetIconInfo(icon, &iconInfo)) return false;

    bool foundBounds = false;
    BITMAP bitmap = {};
    HBITMAP colorBitmap = iconInfo.hbmColor;
    if (colorBitmap && GetObject(colorBitmap, sizeof(bitmap), &bitmap) == sizeof(bitmap))
    {
        width = bitmap.bmWidth;
        height = bitmap.bmHeight;
        if (width > 0 && height > 0)
        {
            BITMAPINFO info = {};
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;

            std::vector<DWORD> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
            HDC scanDc = CreateCompatibleDC(nullptr);
            if (scanDc && GetDIBits(scanDc, colorBitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS) == height)
            {
                int minX = width;
                int minY = height;
                int maxX = -1;
                int maxY = -1;
                for (int y = 0; y < height; ++y)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        const DWORD pixel = pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
                        const BYTE alpha = static_cast<BYTE>((pixel >> 24) & 0xFF);
                        if (alpha <= 12) continue;
                        minX = std::min(minX, x);
                        minY = std::min(minY, y);
                        maxX = std::max(maxX, x);
                        maxY = std::max(maxY, y);
                    }
                }

                if (maxX >= minX && maxY >= minY)
                {
                    bounds.left = std::max(0, minX - 2);
                    bounds.top = std::max(0, minY - 2);
                    bounds.right = std::min(width, maxX + 3);
                    bounds.bottom = std::min(height, maxY + 3);
                    foundBounds = true;
                }
            }
            if (scanDc) DeleteDC(scanDc);
        }
    }

    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
    return foundBounds;
}

bool GetCachedIconOpaqueBounds(HICON icon, RECT& bounds, int& width, int& height)
{
    auto found = std::find_if(g_iconBoundsCache.begin(), g_iconBoundsCache.end(), [&](const IconOpaqueBoundsCacheEntry& entry) {
        return entry.icon == icon;
    });
    if (found != g_iconBoundsCache.end())
    {
        bounds = found->bounds;
        width = found->width;
        height = found->height;
        return found->hasBounds;
    }

    const bool hasBounds = GetIconOpaqueBounds(icon, bounds, width, height);
    g_iconBoundsCache.push_back({ icon, hasBounds, bounds, width, height });
    return hasBounds;
}

bool DrawIconWithAlpha(HDC dc, int x, int y, HICON icon, int size, BYTE alpha)
{
    if (alpha == 255)
    {
        return DrawIconEx(dc, x, y, icon, size, size, 0, nullptr, DI_NORMAL) != FALSE;
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC iconDc = CreateCompatibleDC(dc);
    bool drawn = false;
    if (bitmap && iconDc && bits)
    {
        std::memset(bits, 0, static_cast<size_t>(size) * static_cast<size_t>(size) * sizeof(DWORD));
        HGDIOBJ oldBitmap = SelectObject(iconDc, bitmap);
        DrawIconEx(iconDc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
        drawn = AlphaBlend(dc, x, y, size, size, iconDc, 0, 0, size, size, blend) != FALSE;
        SelectObject(iconDc, oldBitmap);
    }

    if (bitmap) DeleteObject(bitmap);
    if (iconDc) DeleteDC(iconDc);
    return drawn;
}

void DrawFittedIcon(HDC dc, int x, int y, HICON icon, int size, BYTE alpha = 255)
{
    RECT opaqueBounds = {};
    int sourceWidth = 0;
    int sourceHeight = 0;
    if (!GetCachedIconOpaqueBounds(icon, opaqueBounds, sourceWidth, sourceHeight))
    {
        DrawIconWithAlpha(dc, x, y, icon, size, alpha);
        return;
    }

    const int cropWidth = opaqueBounds.right - opaqueBounds.left;
    const int cropHeight = opaqueBounds.bottom - opaqueBounds.top;
    if (cropWidth <= 0 || cropHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
    {
        DrawIconWithAlpha(dc, x, y, icon, size, alpha);
        return;
    }

    const bool hasMeaningfulPadding = cropWidth < static_cast<int>(sourceWidth * 0.86)
        || cropHeight < static_cast<int>(sourceHeight * 0.86);
    if (!hasMeaningfulPadding)
    {
        DrawIconWithAlpha(dc, x, y, icon, size, alpha);
        return;
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = sourceWidth;
    info.bmiHeader.biHeight = -sourceHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC sourceDc = CreateCompatibleDC(dc);
    bool drawn = false;
    if (bitmap && sourceDc && bits)
    {
        std::memset(bits, 0, static_cast<size_t>(sourceWidth) * static_cast<size_t>(sourceHeight) * sizeof(DWORD));
        HGDIOBJ oldBitmap = SelectObject(sourceDc, bitmap);
        DrawIconEx(sourceDc, 0, 0, icon, sourceWidth, sourceHeight, 0, nullptr, DI_NORMAL);

        const double scale = std::min(size / static_cast<double>(cropWidth), size / static_cast<double>(cropHeight));
        const int targetWidth = std::max(1, static_cast<int>(std::round(cropWidth * scale)));
        const int targetHeight = std::max(1, static_cast<int>(std::round(cropHeight * scale)));
        const int targetX = x + (size - targetWidth) / 2;
        const int targetY = y + (size - targetHeight) / 2;
        const int previousMode = SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
        drawn = AlphaBlend(
            dc,
            targetX,
            targetY,
            targetWidth,
            targetHeight,
            sourceDc,
            opaqueBounds.left,
            opaqueBounds.top,
            cropWidth,
            cropHeight,
            blend) != FALSE;
        SetStretchBltMode(dc, previousMode);
        SelectObject(sourceDc, oldBitmap);
    }

    if (bitmap) DeleteObject(bitmap);
    if (sourceDc) DeleteDC(sourceDc);

    if (!drawn)
    {
        DrawIconWithAlpha(dc, x, y, icon, size, alpha);
    }
}

void DrawDragFeedback(HDC dc)
{
    if (!g_dragActive) return;

    if (g_dragMode == DragMode::Region && g_regionDropTarget.valid)
    {
        for (const auto& regionHit : g_regionHits)
        {
            if (CompareStringOrdinal(regionHit.regionId.c_str(), -1, g_regionDropTarget.regionId.c_str(), -1, TRUE) != CSTR_EQUAL) continue;
            const int y = g_regionDropTarget.insertAfter ? regionHit.headerBounds.bottom + 3 : regionHit.headerBounds.top - 3;
            RECT marker = { regionHit.headerBounds.left, y, regionHit.headerBounds.right, y + 6 };
            DrawRoundedRect(dc, marker, RGB(78, 88, 102), RGB(142, 154, 170), 4);
            break;
        }

        RECT ghost = { g_dragCurrent.x + 16, g_dragCurrent.y + 16, g_dragCurrent.x + 180, g_dragCurrent.y + 58 };
        DrawRoundedRect(dc, ghost, RGB(42, 56, 76), RGB(120, 185, 255), 12);
        RECT text = { ghost.left + 12, ghost.top + 4, ghost.right - 12, ghost.bottom - 4 };
        DrawTextClipped(dc, RegionName(g_regionDragId), text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, RGB(242, 248, 255));
        return;
    }

    if (!g_dropTarget.valid) return;

    RECT ghost = { g_dragCurrent.x + 16, g_dragCurrent.y + 16, g_dragCurrent.x + 162, g_dragCurrent.y + 58 };
    DrawRoundedRect(dc, ghost, RGB(42, 52, 66), RGB(132, 148, 168), 12);
    if (g_dragFilteredIndex >= 0 && g_dragFilteredIndex < static_cast<int>(g_filtered.size()))
    {
        RECT text = { ghost.left + 12, ghost.top + 4, ghost.right - 12, ghost.bottom - 4 };
        const auto label = g_dragSourcePaths.size() > 1
            ? std::to_wstring(g_dragSourcePaths.size()) + L" apps"
            : g_items[g_filtered[g_dragFilteredIndex]].displayName;
        DrawTextClipped(dc, label, text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, RGB(242, 248, 255));
    }
}

void DrawSubtleSelectionOutline(HDC dc, const RECT& rect);

void DrawSelectionBox(HDC dc)
{
    if (!g_selectionBoxActive) return;

    RECT rect = SelectionViewportRect();
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width < 2 || height < 2) return;

    DrawSubtleSelectionOutline(dc, rect);
}

void DrawSubtleSelectionOutline(HDC dc, const RECT& rect)
{
    HPEN outerPen = CreatePen(PS_SOLID, 1, RGB(74, 88, 104));
    HPEN innerPen = CreatePen(PS_SOLID, 1, RGB(150, 166, 182));
    HGDIOBJ oldPen = SelectObject(dc, outerPen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, innerPen);
    Rectangle(dc, rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(innerPen);
    DeleteObject(outerPen);
}

struct RenderLayoutMetrics
{
    int tileSize = 0;
    int gap = 0;
    int left = 0;
    int right = 0;
    int columns = 1;
    int initialY = 0;
    RECT contentClip = {};
    bool groupByRegion = true;
};

RenderLayoutMetrics CreateRenderLayoutMetrics(const RECT& client)
{
    RenderLayoutMetrics metrics = {};
    metrics.tileSize = TileSize();
    metrics.gap = TileGap();
    metrics.left = IsSpotlightMode() ? SpotlightContentInset : 44;
    metrics.right = client.right - metrics.left;
    metrics.columns = CalculateColumnCount(client);
    metrics.initialY = 114 - g_scrollOffset;
    metrics.contentClip = ContentClipRect(client);
    metrics.groupByRegion = SearchCommittedText().empty();
    return metrics;
}

void ResetRenderHitState()
{
    g_hits.clear();
    g_regionHits.clear();
}

void RenderFrameDirect2D(ID2D1DCRenderTarget* target, const RECT& client)
{
    DrawGlassBackground(target, client);
    DrawSearchSurface(target, client);
    DrawLaunchpadModeControl(target, client);
}

void RenderFrameGdi(HDC dc, const RECT& client)
{
    DrawGlassBackground(dc, client);
    DrawSearchSurface(dc, client);
    DrawLaunchpadModeControl(dc, client);
}

void RenderDeferredIconOverlay(HDC dc, const RECT& client, const std::vector<PendingIconDraw>& pendingIcons)
{
    int savedDc = SaveDC(dc);
    const RECT contentClip = ContentClipRect(client);
    HRGN clip = CreateRectRgn(contentClip.left, contentClip.top, contentClip.right, contentClip.bottom);
    SelectClipRgn(dc, clip);
    for (const auto& icon : pendingIcons)
    {
        DrawFittedIcon(dc, icon.x, icon.y, icon.icon, icon.size, icon.alpha);
    }
    DrawDragFeedback(dc);
    DrawSelectionBox(dc);
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
    RestoreDC(dc, savedDc);
}

bool PaintContentDirect2D(HDC dc, const RECT& client)
{
    if (!InitializeDirectRenderer()) return false;

    if (!g_d2dDcRenderTarget)
    {
        auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (FAILED(g_d2dFactory->CreateDCRenderTarget(&props, &g_d2dDcRenderTarget)) || !g_d2dDcRenderTarget)
        {
            return false;
        }
    }

    ID2D1DCRenderTarget* target = g_d2dDcRenderTarget;
    if (FAILED(target->BindDC(dc, &client)))
    {
        return false;
    }

    ResetRenderHitState();
    std::vector<PendingIconDraw> pendingIcons;
    const RenderLayoutMetrics metrics = CreateRenderLayoutMetrics(client);
    const bool baseLayerOnly = IsSpotlightBaseLayerPass();

    target->BeginDraw();
    RenderFrameDirect2D(target, client);

    target->PushAxisAlignedClip(D2DRect(metrics.contentClip), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const auto scene = BuildCurrentSceneGeometry(client);
    g_contentHeight = scene.contentHeight;
    for (const auto& region : scene.regions)
    {
        const RECT bounds = WinRect(region.viewportBounds);
        if (IsRegionSelected(region.regionId))
        {
            RECT selectedRect = { bounds.left, bounds.top - 4, bounds.right, bounds.top + 32 };
            DrawRoundedRectDirect(target, selectedRect, RGB(30, 66, 96), RGB(118, 178, 238), 10.0f);
        }
        if (!baseLayerOnly)
        {
            RECT headerRect = { bounds.left + 8, bounds.top, bounds.right, bounds.top + 28 };
            DrawTextDirect(target, g_headerTextFormat, RegionName(region.regionId), headerRect, RGB(220, 232, 248), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        g_regionHits.push_back({ bounds, bounds, region.regionId });
    }
    if (scene.items.empty() && scene.regions.empty() && !baseLayerOnly)
    {
        RECT emptyRect = { metrics.left, metrics.initialY + 60, metrics.right, metrics.initialY + 120 };
        const bool initialSpotlight = IsSpotlightMode() && SearchCommittedText().empty();
        DrawTextDirect(
            target,
            g_headerTextFormat,
            initialSpotlight ? L"Type to search" : L"No matching apps",
            emptyRect,
            initialSpotlight ? RGB(138, 164, 190) : RGB(210, 220, 235),
            DWRITE_TEXT_ALIGNMENT_CENTER,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
            DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    for (const auto& slot : scene.items)
    {
        if (slot.filteredIndex < 0 || slot.filteredIndex >= static_cast<int>(g_filtered.size())) continue;
        auto& item = g_items[g_filtered[slot.filteredIndex]];
        RECT visualTileRect = WinRect(slot.viewportBounds);
        const bool hideTileForDrag = ShouldHideTileForDragPreview(item);
        if (!hideTileForDrag) ApplyDragAvoidanceOffset(visualTileRect, item, metrics.tileSize, metrics.gap, metrics.columns);
        auto regionHit = std::find_if(g_regionHits.begin(), g_regionHits.end(), [&](const RegionHit& hit) {
            return CompareStringOrdinal(hit.regionId.c_str(), -1, item.regionId.c_str(), -1, TRUE) == CSTR_EQUAL;
        });
        if (!hideTileForDrag && regionHit != g_regionHits.end()) regionHit->bounds.bottom = std::max(regionHit->bounds.bottom, visualTileRect.bottom + metrics.gap);
        if (!hideTileForDrag && visualTileRect.bottom >= ContentClipTop() && visualTileRect.top <= client.bottom)
        {
            const TileVisualState visualState = ResolveTileVisualState(item, slot.filteredIndex);
            const bool emphasizedIcon = visualState != TileVisualState::Normal && visualState != TileVisualState::Hover;
            DrawAppTileSurface(target, visualTileRect, visualState);
            if (!baseLayerOnly)
            {
                DrawIconBackplateDirect(target, visualTileRect, emphasizedIcon);
                QueueItemIcon(item);
                if (item.icon)
                {
                    const int icon = g_settings.iconSize;
                    const int iconX = visualTileRect.left + (metrics.tileSize - icon) / 2;
                    const int iconY = visualTileRect.top + 14;
                    pendingIcons.push_back({ item.icon, iconX, iconY, icon, DragIconAlphaForItem(item) });
                }
                RECT labelRect = { visualTileRect.left + 8, visualTileRect.top + g_settings.iconSize + 24, visualTileRect.right - 8, visualTileRect.bottom - 8 };
                const COLORREF labelColor = IsDropHintItem(item) ? RGB(168, 184, 202) : RGB(238, 244, 252);
                DrawTextDirect(target, g_tileTextFormat, item.displayName, labelRect, labelColor, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_WRAP);
            }
            g_hits.push_back({ visualTileRect, slot.filteredIndex });
        }
        g_contentHeight = std::max(g_contentHeight, static_cast<int>(visualTileRect.bottom) + g_scrollOffset + metrics.gap + 44);
    }

    target->PopAxisAlignedClip();
    DrawSpotlightAuxiliary(target, client);
    DrawSoftWindowEdge(target, client);

    const HRESULT drawn = target->EndDraw();
    if (FAILED(drawn))
    {
        SafeRelease(g_spotlightBackdropD2dBitmap);
        SafeRelease(g_d2dDcRenderTarget);
        return false;
    }

    if (!baseLayerOnly)
    {
        RenderDeferredIconOverlay(dc, client, pendingIcons);
    }
    TrimResidentIcons();
    return true;
}

void PaintContent(HDC dc, const RECT& client)
{
    ResetRenderHitState();
    const RenderLayoutMetrics metrics = CreateRenderLayoutMetrics(client);
    const bool baseLayerOnly = IsSpotlightBaseLayerPass();
    RenderFrameGdi(dc, client);

    HFONT tileFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT headerFont = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    const int savedDc = SaveDC(dc);
    HRGN contentClip = CreateRectRgn(metrics.contentClip.left, metrics.contentClip.top, metrics.contentClip.right, metrics.contentClip.bottom);
    SelectClipRgn(dc, contentClip);

    SelectObject(dc, headerFont);
    const auto scene = BuildCurrentSceneGeometry(client);
    g_contentHeight = scene.contentHeight;
    for (const auto& region : scene.regions)
    {
        const RECT bounds = WinRect(region.viewportBounds);
        if (IsRegionSelected(region.regionId))
        {
            RECT selectedRect = { bounds.left, bounds.top - 4, bounds.right, bounds.top + 32 };
            DrawRoundedRect(dc, selectedRect, RGB(30, 66, 96), RGB(118, 178, 238), 10);
        }
        if (!baseLayerOnly)
        {
            RECT headerRect = { bounds.left + 8, bounds.top, bounds.right, bounds.top + 28 };
            DrawTextClipped(dc, RegionName(region.regionId), headerRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, RGB(220, 232, 248));
        }
        g_regionHits.push_back({ bounds, bounds, region.regionId });
    }
    if (scene.items.empty() && scene.regions.empty() && !baseLayerOnly)
    {
        RECT emptyRect = { metrics.left, metrics.initialY + 60, metrics.right, metrics.initialY + 120 };
        const bool initialSpotlight = IsSpotlightMode() && SearchCommittedText().empty();
        DrawTextClipped(
            dc,
            initialSpotlight ? L"Type to search" : L"No matching apps",
            emptyRect,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER,
            initialSpotlight ? RGB(138, 164, 190) : RGB(210, 220, 235));
    }
    for (const auto& slot : scene.items)
    {
        if (slot.filteredIndex < 0 || slot.filteredIndex >= static_cast<int>(g_filtered.size())) continue;
        auto& item = g_items[g_filtered[slot.filteredIndex]];
        RECT visualTileRect = WinRect(slot.viewportBounds);
        const bool hideTileForDrag = ShouldHideTileForDragPreview(item);
        if (!hideTileForDrag) ApplyDragAvoidanceOffset(visualTileRect, item, metrics.tileSize, metrics.gap, metrics.columns);
        auto regionHit = std::find_if(g_regionHits.begin(), g_regionHits.end(), [&](const RegionHit& hit) {
            return CompareStringOrdinal(hit.regionId.c_str(), -1, item.regionId.c_str(), -1, TRUE) == CSTR_EQUAL;
        });
        if (!hideTileForDrag && regionHit != g_regionHits.end()) regionHit->bounds.bottom = std::max(regionHit->bounds.bottom, visualTileRect.bottom + metrics.gap);
        if (!hideTileForDrag && visualTileRect.bottom >= ContentClipTop() && visualTileRect.top <= client.bottom)
        {
            const TileVisualState visualState = ResolveTileVisualState(item, slot.filteredIndex);
            DrawAppTileSurface(dc, visualTileRect, visualState);
            if (!baseLayerOnly)
            {
                QueueItemIcon(item);
                if (item.icon)
                {
                    const int icon = g_settings.iconSize;
                    const int iconX = visualTileRect.left + (metrics.tileSize - icon) / 2;
                    const int iconY = visualTileRect.top + 14;
                    DrawFittedIcon(dc, iconX, iconY, item.icon, icon, DragIconAlphaForItem(item));
                }
                SelectObject(dc, tileFont);
                RECT labelRect = { visualTileRect.left + 8, visualTileRect.top + g_settings.iconSize + 24, visualTileRect.right - 8, visualTileRect.bottom - 8 };
                const COLORREF labelColor = IsDropHintItem(item) ? RGB(168, 184, 202) : RGB(238, 244, 252);
                DrawTextClipped(dc, item.displayName, labelRect, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS, labelColor);
            }
            g_hits.push_back({ visualTileRect, slot.filteredIndex });
        }
        g_contentHeight = std::max(g_contentHeight, static_cast<int>(visualTileRect.bottom) + g_scrollOffset + metrics.gap + 44);
    }

    if (!baseLayerOnly)
    {
        DrawDragFeedback(dc);
        DrawSelectionBox(dc);
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(contentClip);
    RestoreDC(dc, savedDc);
    DrawSpotlightAuxiliary(dc, client);
    DrawSoftWindowEdge(dc, client);
    TrimResidentIcons();

    DeleteObject(tileFont);
    DeleteObject(headerFont);
}

void ReleaseSpotlightLayerBuffer()
{
    if (g_spotlightLayerBuffer.baseDc && g_spotlightLayerBuffer.oldBaseBitmap)
    {
        SelectObject(g_spotlightLayerBuffer.baseDc, g_spotlightLayerBuffer.oldBaseBitmap);
    }
    if (g_spotlightLayerBuffer.fullDc && g_spotlightLayerBuffer.oldFullBitmap)
    {
        SelectObject(g_spotlightLayerBuffer.fullDc, g_spotlightLayerBuffer.oldFullBitmap);
    }
    if (g_spotlightLayerBuffer.baseBitmap) DeleteObject(g_spotlightLayerBuffer.baseBitmap);
    if (g_spotlightLayerBuffer.fullBitmap) DeleteObject(g_spotlightLayerBuffer.fullBitmap);
    if (g_spotlightLayerBuffer.baseDc) DeleteDC(g_spotlightLayerBuffer.baseDc);
    if (g_spotlightLayerBuffer.fullDc) DeleteDC(g_spotlightLayerBuffer.fullDc);
    g_spotlightLayerBuffer = {};
    g_spotlightLayerDirty = true;
}

bool EnsureSpotlightLayerBuffer(int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    if (g_spotlightLayerBuffer.baseDc
        && g_spotlightLayerBuffer.fullDc
        && g_spotlightLayerBuffer.baseBitmap
        && g_spotlightLayerBuffer.fullBitmap
        && g_spotlightLayerBuffer.baseBits
        && g_spotlightLayerBuffer.fullBits
        && g_spotlightLayerBuffer.width == width
        && g_spotlightLayerBuffer.height == height)
    {
        return true;
    }

    ReleaseSpotlightLayerBuffer();

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) return false;

    g_spotlightLayerBuffer.baseDc = CreateCompatibleDC(screenDc);
    g_spotlightLayerBuffer.fullDc = CreateCompatibleDC(screenDc);
    g_spotlightLayerBuffer.baseBitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &g_spotlightLayerBuffer.baseBits, nullptr, 0);
    g_spotlightLayerBuffer.fullBitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &g_spotlightLayerBuffer.fullBits, nullptr, 0);
    ReleaseDC(nullptr, screenDc);

    if (!g_spotlightLayerBuffer.baseDc
        || !g_spotlightLayerBuffer.fullDc
        || !g_spotlightLayerBuffer.baseBitmap
        || !g_spotlightLayerBuffer.fullBitmap
        || !g_spotlightLayerBuffer.baseBits
        || !g_spotlightLayerBuffer.fullBits)
    {
        ReleaseSpotlightLayerBuffer();
        return false;
    }

    g_spotlightLayerBuffer.oldBaseBitmap = SelectObject(g_spotlightLayerBuffer.baseDc, g_spotlightLayerBuffer.baseBitmap);
    g_spotlightLayerBuffer.oldFullBitmap = SelectObject(g_spotlightLayerBuffer.fullDc, g_spotlightLayerBuffer.fullBitmap);
    g_spotlightLayerBuffer.width = width;
    g_spotlightLayerBuffer.height = height;
    g_spotlightLayerDirty = true;
    return true;
}

bool PixelDiffers(DWORD left, DWORD right)
{
    const int lb = static_cast<int>(left & 0xFF);
    const int lg = static_cast<int>((left >> 8) & 0xFF);
    const int lr = static_cast<int>((left >> 16) & 0xFF);
    const int rb = static_cast<int>(right & 0xFF);
    const int rg = static_cast<int>((right >> 8) & 0xFF);
    const int rr = static_cast<int>((right >> 16) & 0xFF);
    return std::abs(lb - rb) + std::abs(lg - rg) + std::abs(lr - rr) > 12;
}

DWORD PremultiplyPixel(DWORD pixel, BYTE alpha)
{
    if (alpha == 0) return 0;
    if (alpha == 255) return (pixel & 0x00FFFFFF) | 0xFF000000;

    const BYTE b = static_cast<BYTE>(((pixel & 0xFF) * alpha + 127) / 255);
    const BYTE g = static_cast<BYTE>(((((pixel >> 8) & 0xFF) * alpha + 127) / 255));
    const BYTE r = static_cast<BYTE>(((((pixel >> 16) & 0xFF) * alpha + 127) / 255));
    return (static_cast<DWORD>(alpha) << 24)
        | (static_cast<DWORD>(r) << 16)
        | (static_cast<DWORD>(g) << 8)
        | b;
}

DWORD BlendPixel(DWORD basePixel, DWORD contentPixel, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    const int bb = static_cast<int>(basePixel & 0xFF);
    const int bg = static_cast<int>((basePixel >> 8) & 0xFF);
    const int br = static_cast<int>((basePixel >> 16) & 0xFF);
    const int cb = static_cast<int>(contentPixel & 0xFF);
    const int cg = static_cast<int>((contentPixel >> 8) & 0xFF);
    const int cr = static_cast<int>((contentPixel >> 16) & 0xFF);
    const BYTE b = static_cast<BYTE>(std::clamp(static_cast<int>(std::round(bb + (cb - bb) * amount)), 0, 255));
    const BYTE g = static_cast<BYTE>(std::clamp(static_cast<int>(std::round(bg + (cg - bg) * amount)), 0, 255));
    const BYTE r = static_cast<BYTE>(std::clamp(static_cast<int>(std::round(br + (cr - br) * amount)), 0, 255));
    return (static_cast<DWORD>(r) << 16) | (static_cast<DWORD>(g) << 8) | b;
}

void ApplySpotlightPerPixelAlpha(DWORD* basePixels, DWORD* fullPixels, int width, int height)
{
    if (!basePixels || !fullPixels || width <= 0 || height <= 0) return;
    RECT client = { 0, 0, width, height };
    const RECT contentClip = ContentClipRect(client);
    const RECT auxiliaryRect = SpotlightAuxiliaryRect(client);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const double coverage = RoundedRectCoverage(x, y, client, SpotlightCornerRadius);
            if (coverage <= 0.0)
            {
                fullPixels[index] = 0;
                continue;
            }

            const bool contentPixel = PixelDiffers(basePixels[index], fullPixels[index]);
            const POINT point = { x, y };
            const bool persistentControlPixel = PtInRect(&auxiliaryRect, point) != FALSE;
            const double contentCoverage = contentPixel
                ? (persistentControlPixel ? 1.0 : SpotlightContentCoverageForPixel(y, contentClip))
                : 1.0;
            if (contentPixel && contentCoverage < 1.0)
            {
                fullPixels[index] = BlendPixel(basePixels[index], fullPixels[index], contentCoverage);
            }
            const BYTE targetAlpha = contentPixel
                ? static_cast<BYTE>(std::round(SpotlightGlassAlpha + (255 - SpotlightGlassAlpha) * contentCoverage))
                : SpotlightGlassAlpha;
            const BYTE alpha = static_cast<BYTE>(std::clamp(static_cast<int>(std::round(targetAlpha * coverage)), 0, 255));
            fullPixels[index] = PremultiplyPixel(fullPixels[index], alpha);
        }
    }
}

bool PaintIntoDib(HDC dc, const RECT& client, bool glassBaseOnly)
{
    g_spotlightLayerBasePass = glassBaseOnly;
    const bool drawn = PaintContentDirect2D(dc, client);
    if (!drawn)
    {
        PaintContent(dc, client);
    }
    g_spotlightLayerBasePass = false;
    return true;
}

bool PaintSpotlightLayeredWindow(bool repaint)
{
    if (!g_hwnd || !IsSpotlightMode()) return false;

    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int width = std::max(1, static_cast<int>(client.right - client.left));
    const int height = std::max(1, static_cast<int>(client.bottom - client.top));
    if (!EnsureSpotlightLayerBuffer(width, height)) return false;

    if (repaint || g_spotlightLayerDirty)
    {
        const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(DWORD);
        std::memset(g_spotlightLayerBuffer.baseBits, 0, bytes);
        std::memset(g_spotlightLayerBuffer.fullBits, 0, bytes);

        PaintIntoDib(g_spotlightLayerBuffer.baseDc, client, true);
        PaintIntoDib(g_spotlightLayerBuffer.fullDc, client, false);
        ApplySpotlightPerPixelAlpha(
            reinterpret_cast<DWORD*>(g_spotlightLayerBuffer.baseBits),
            reinterpret_cast<DWORD*>(g_spotlightLayerBuffer.fullBits),
            width,
            height);
        g_spotlightLayerDirty = false;
    }

    RECT windowRect = {};
    GetWindowRect(g_hwnd, &windowRect);
    POINT dst = { windowRect.left, windowRect.top };
    POINT src = { 0, 0 };
    SIZE size = { width, height };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, g_spotlightPaintOpacity, AC_SRC_ALPHA };
    HDC screenDc = GetDC(nullptr);
    if (!screenDc) return false;
    const BOOL updated = UpdateLayeredWindow(g_hwnd, screenDc, &dst, &size, g_spotlightLayerBuffer.fullDc, &src, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screenDc);
    return updated != FALSE;
}

void RequestSearchRepaint(HWND hwnd, bool erase)
{
    if (IsSpotlightMode() && IsWindowVisible(hwnd))
    {
        g_spotlightLayerDirty = true;
        PaintSpotlightLayeredWindow(true);
        return;
    }

    InvalidateRect(hwnd, nullptr, erase ? TRUE : FALSE);
}

void PaintWindow(HWND hwnd)
{
    if (IsSpotlightMode())
    {
        PAINTSTRUCT ps = {};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        PaintSpotlightLayeredWindow();
        return;
    }

    PAINTSTRUCT ps = {};
    HDC screenDc = BeginPaint(hwnd, &ps);
    RECT client = {};
    GetClientRect(hwnd, &client);

    HDC memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, client.right - client.left, client.bottom - client.top);
    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);

    if (!PaintContentDirect2D(memoryDc, client))
    {
        PaintContent(memoryDc, client);
    }
    BitBlt(screenDc, 0, 0, client.right - client.left, client.bottom - client.top, memoryDc, 0, 0, SRCCOPY);

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    EndPaint(hwnd, &ps);
}

void ClampScroll()
{
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int clientHeight = static_cast<int>(client.bottom - client.top);
    const int maxScroll = std::max(0, g_contentHeight - clientHeight + 24);
    g_scrollOffset = std::clamp(g_scrollOffset, 0, maxScroll);
}

int CalculateColumnCount(const RECT& client)
{
    return BuildCurrentSceneGeometry(client).columns;
}

bool EstimateTileBoundsForFilteredIndex(int targetFilteredIndex, RECT& bounds)
{
    if (targetFilteredIndex < 0 || targetFilteredIndex >= static_cast<int>(g_filtered.size())) return false;

    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const auto scene = BuildCurrentSceneGeometry(client);
    const auto* item = scene.FindItem(targetFilteredIndex);
    if (!item) return false;
    bounds = WinRect(item->viewportBounds);
    return true;
}

void ScrollSelectedIntoView()
{
    RECT bounds = {};
    if (!EstimateTileBoundsForFilteredIndex(g_selectedFilteredIndex, bounds)) return;

    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int topLimit = ContentClipTop();
    const int bottomLimit = client.bottom - 24;
    if (bounds.top < topLimit)
    {
        g_scrollOffset -= topLimit - bounds.top;
    }
    else if (bounds.bottom > bottomLimit)
    {
        g_scrollOffset += bounds.bottom - bottomLimit;
    }
    ClampScroll();
}

void ApplySpotlightInteractionState(bool scrollFocusedResult)
{
    if (!IsSpotlightMode()) return;
    g_selectedFilteredIndex = g_spotlightState.FocusedResultIndex();
    if (g_spotlightFlowProbe.active
        && g_selectedFilteredIndex >= 0
        && g_selectedFilteredIndex < static_cast<int>(g_filtered.size()))
    {
        g_spotlightFlowProbe.focusedResultPaths.push_back(g_items[g_filtered[g_selectedFilteredIndex]].sourcePath);
    }
    g_searchInputActive = g_spotlightState.Focus() == llp::interaction::FocusArea::Search;
    if (scrollFocusedResult && g_selectedFilteredIndex >= 0) ScrollSelectedIntoView();
    if (g_searchInputActive) UpdateImeWindowPosition(g_hwnd);
    g_searchAccessibility.NotifyFocusChanged();
    RequestSearchRepaint(g_hwnd, false);
}

bool EnterSpotlightResults()
{
    if (!IsSpotlightMode() || !g_spotlightState.EnterResults()) return false;
    ApplySpotlightInteractionState(true);
    return true;
}

bool MoveSpotlightResult(llp::interaction::Direction direction)
{
    if (!IsSpotlightMode()) return false;
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    if (!g_spotlightState.MoveResult(direction, CalculateColumnCount(client))) return false;
    ApplySpotlightInteractionState(true);
    return true;
}

void SelectFilteredIndex(int index)
{
    if (g_filtered.empty()) return;
    g_selectedFilteredIndex = std::clamp(index, 0, static_cast<int>(g_filtered.size()) - 1);
    if (IsSpotlightMode())
    {
        g_spotlightState.FocusResult(g_selectedFilteredIndex);
        ApplySpotlightInteractionState(true);
        return;
    }
    g_searchInputActive = false;
    g_searchAccessibility.NotifyFocusChanged();
    const auto& item = g_items[g_filtered[g_selectedFilteredIndex]];
    if (g_launchpadState.IsEditing()) SetSingleSelection(item.sourcePath);
    else ClearAllSelection();
    ScrollSelectedIntoView();
    RequestSearchRepaint(g_hwnd, false);
}

void MoveSelectionHorizontal(int delta)
{
    if (g_filtered.empty()) return;
    SelectFilteredIndex(g_selectedFilteredIndex + delta);
}

void MoveSelectionVertical(int delta)
{
    if (g_filtered.empty()) return;
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int columns = CalculateColumnCount(client);
    if (!IsSpotlightMode() && delta < 0 && g_selectedFilteredIndex >= 0 && g_selectedFilteredIndex < columns)
    {
        ActivateSearchInput();
        RequestSearchRepaint(g_hwnd, false);
        return;
    }
    SelectFilteredIndex(g_selectedFilteredIndex + delta * columns);
}

std::wstring GetControlText(HWND hwnd, int controlId, int maxChars = 2048)
{
    std::wstring value(static_cast<size_t>(maxChars), L'\0');
    const int length = GetDlgItemTextW(hwnd, controlId, value.data(), maxChars);
    value.resize(std::max(0, length));
    return value;
}

void SetControlText(HWND hwnd, SettingsControlId id, const std::wstring& value)
{
    SetDlgItemTextW(hwnd, ControlId(id), value.c_str());
}

HFONT UiFont()
{
    if (!g_uiFont)
    {
        g_uiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }
    return g_uiFont;
}

HWND ApplyControlFont(HWND control)
{
    if (control)
    {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);
        SetWindowTheme(control, L"Explorer", nullptr);
    }
    return control;
}

LRESULT CALLBACK HotkeyEditSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR data)
{
    (void)subclassId;
    (void)data;

    switch (message)
    {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        const std::wstring hotkey = FormatHotkeyFromKeyPress(wParam);
        if (!hotkey.empty())
        {
            SetWindowTextW(hwnd, hotkey.c_str());
            SendMessageW(hwnd, EM_SETSEL, hotkey.size(), hotkey.size());
            return 0;
        }
        break;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, HotkeyEditSubclassProc, HotkeyEditSubclassId);
        break;
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

HWND CreateSettingsLabel(HWND parent, SettingsControlId id, int x, int y)
{
    return ApplyControlFont(CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, x, y + 5, 150, 24, parent, reinterpret_cast<HMENU>(ControlId(id)), g_instance, nullptr));
}

HWND CreateSettingsEdit(HWND parent, SettingsControlId id, int x, int y, int width)
{
    HWND edit = ApplyControlFont(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x, y, width, 26, parent, reinterpret_cast<HMENU>(ControlId(id)), g_instance, nullptr));
    if (id == SettingsControlId::Hotkey && edit)
    {
        SetWindowSubclass(edit, HotkeyEditSubclassProc, HotkeyEditSubclassId, 0);
    }
    return edit;
}

HWND CreateSettingsCombo(HWND parent, SettingsControlId id, int x, int y, int width)
{
    return ApplyControlFont(CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, x, y, width, 140, parent, reinterpret_cast<HMENU>(ControlId(id)), g_instance, nullptr));
}

void ResetDisplayModeCombo(HWND hwnd, const std::wstring& language)
{
    HWND combo = GetDlgItem(hwnd, ControlId(SettingsControlId::DisplayMode));
    const int current = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(IsChineseLanguage(language) ? L"启动台" : L"Launchpad"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(IsChineseLanguage(language) ? L"聚焦" : L"Spotlight"));
    SendMessageW(combo, CB_SETCURSEL, current == 1 ? 1 : 0, 0);
}

void ResetLanguageCombo(HWND hwnd)
{
    HWND combo = GetDlgItem(hwnd, ControlId(SettingsControlId::Language));
    const int current = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"中文"));
    SendMessageW(combo, CB_SETCURSEL, current == 1 ? 1 : 0, 0);
}

void ResetIconSizeCombo(HWND hwnd, const std::wstring& language)
{
    HWND combo = GetDlgItem(hwnd, ControlId(SettingsControlId::IconSize));
    const int current = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(IsChineseLanguage(language) ? L"小" : L"Small"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(IsChineseLanguage(language) ? L"中" : L"Medium"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(IsChineseLanguage(language) ? L"大" : L"Large"));
    SendMessageW(combo, CB_SETCURSEL, current >= 0 ? current : 1, 0);
}

void SetIconSizeSelection(HWND hwnd)
{
    const int index = g_settings.iconSize <= 44
        ? 0
        : (g_settings.iconSize >= 64 ? 2 : 1);
    SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::IconSize), CB_SETCURSEL, index, 0);
}

std::wstring SettingsWindowLanguage(HWND hwnd)
{
    HWND combo = GetDlgItem(hwnd, ControlId(SettingsControlId::Language));
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    return index == 1 ? L"Chinese" : L"English";
}

const wchar_t* SettingsText(HWND hwnd, const wchar_t* english, const wchar_t* chinese)
{
    return IsChineseLanguage(SettingsWindowLanguage(hwnd)) ? chinese : english;
}

void ApplyLanguageToSettingsWindow(HWND hwnd)
{
    const auto language = SettingsWindowLanguage(hwnd);
    SetWindowTextW(hwnd, IsChineseLanguage(language) ? L"LightLaunchpad 设置" : L"LightLaunchpad Settings");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::FolderLabel), IsChineseLanguage(language) ? L"启动台文件夹" : L"Launchpad folder");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::HotkeyLabel), IsChineseLanguage(language) ? L"快捷键" : L"Hotkey");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::IconSizeLabel), IsChineseLanguage(language) ? L"APP 大小" : L"App size");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::DisplayModeLabel), IsChineseLanguage(language) ? L"打开方式" : L"Open mode");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::LanguageLabel), IsChineseLanguage(language) ? L"语言" : L"Language");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::SpotlightSizeLabel), IsChineseLanguage(language) ? L"聚焦窗口大小" : L"Spotlight size");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::AppSpacingLabel), IsChineseLanguage(language) ? L"APP 间距" : L"App spacing");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::WheelSensitivityLabel), IsChineseLanguage(language) ? L"滚轮灵敏度" : L"Wheel sensitivity");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::StartWithWindows), IsChineseLanguage(language) ? L"随 Windows 启动" : L"Start with Windows");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::ResetSearchOnOpen), IsChineseLanguage(language) ? L"打开时清空搜索栏" : L"Reset search on open");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::DiagnosticsLabel), IsChineseLanguage(language) ? L"关于与诊断" : L"About & Diagnostics");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::Save), IsChineseLanguage(language) ? L"保存" : L"Save");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::Apply), IsChineseLanguage(language) ? L"应用" : L"Apply");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::Cancel), IsChineseLanguage(language) ? L"取消" : L"Cancel");
    ResetIconSizeCombo(hwnd, language);
    ResetDisplayModeCombo(hwnd, language);
}

void PopulateSettingsWindow(HWND hwnd)
{
    SetControlText(hwnd, SettingsControlId::LaunchpadFolder, g_settings.launchpadFolder);
    SetControlText(hwnd, SettingsControlId::Hotkey, g_settings.hotkey);
    SetControlText(hwnd, SettingsControlId::SpotlightWidth, std::to_wstring(g_settings.spotlightWidth));
    SetControlText(hwnd, SettingsControlId::SpotlightHeight, std::to_wstring(g_settings.spotlightHeight));
    SetControlText(hwnd, SettingsControlId::AppSpacing, std::to_wstring(g_settings.appSpacing));
    SetControlText(hwnd, SettingsControlId::WheelSensitivity, FormatDouble(g_settings.wheelSensitivity));
    SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::DisplayMode), CB_SETCURSEL, IsSpotlightMode() ? 1 : 0, 0);
    SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::Language), CB_SETCURSEL, IsChineseLanguage(g_settings.language) ? 1 : 0, 0);
    SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::StartWithWindows), BM_SETCHECK, g_settings.startWithWindows ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::ResetSearchOnOpen), BM_SETCHECK, g_settings.resetSearchOnOpen ? BST_CHECKED : BST_UNCHECKED, 0);
    SetControlText(hwnd, SettingsControlId::DiagnosticsSummary, BuildDiagnosticsSummary());
    ApplyLanguageToSettingsWindow(hwnd);
    SetIconSizeSelection(hwnd);
}

Settings ReadSettingsWindowCandidate(HWND hwnd)
{
    Settings candidate = g_settings;
    candidate.launchpadFolder = Trim(GetControlText(hwnd, ControlId(SettingsControlId::LaunchpadFolder)));
    candidate.hotkey = NormalizeHotkeyText(GetControlText(hwnd, ControlId(SettingsControlId::Hotkey)));
    if (candidate.launchpadFolder.empty())
    {
        const auto profile = GetUserProfilePath();
        candidate.launchpadFolder = profile.empty() ? L"Launchpad" : JoinPath(profile, L"Launchpad");
    }
    if (candidate.hotkey.empty()) candidate.hotkey = L"Alt+D";

    const int displayIndex = static_cast<int>(SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::DisplayMode), CB_GETCURSEL, 0, 0));
    const int languageIndex = static_cast<int>(SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::Language), CB_GETCURSEL, 0, 0));
    const int iconSizeIndex = static_cast<int>(SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::IconSize), CB_GETCURSEL, 0, 0));
    candidate.displayMode = displayIndex == 1 ? L"Spotlight" : L"Launchpad";
    candidate.language = languageIndex == 1 ? L"Chinese" : L"English";
    candidate.iconSize = iconSizeIndex == 0 ? 42 : (iconSizeIndex == 2 ? 72 : 56);
    candidate.spotlightWidth = NormalizeSpotlightWidth(_wtoi(GetControlText(hwnd, ControlId(SettingsControlId::SpotlightWidth)).c_str()));
    candidate.spotlightHeight = NormalizeSpotlightHeight(_wtoi(GetControlText(hwnd, ControlId(SettingsControlId::SpotlightHeight)).c_str()));
    candidate.appSpacing = NormalizeAppSpacing(_wtoi(GetControlText(hwnd, ControlId(SettingsControlId::AppSpacing)).c_str()));
    candidate.wheelSensitivity = NormalizeWheelSensitivity(std::wcstod(GetControlText(hwnd, ControlId(SettingsControlId::WheelSensitivity)).c_str(), nullptr));
    candidate.startWithWindows = SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::StartWithWindows), BM_GETCHECK, 0, 0) == BST_CHECKED;
    candidate.resetSearchOnOpen = SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::ResetSearchOnOpen), BM_GETCHECK, 0, 0) == BST_CHECKED;
    return candidate;
}

struct StartupRegistrationSnapshot
{
    bool valid = false;
    bool exists = false;
    DWORD type = REG_SZ;
    std::wstring value;
};

std::wstring StartupRegistrySubkey()
{
    const auto testSubkey = CommandLineValue(L"--startup-registry-subkey");
    if (CompareStringOrdinal(LLP_BUILD_CHANNEL_WIDE, -1, L"development", -1, TRUE) == CSTR_EQUAL
        && StartsWithIgnoreCase(testSubkey, L"Software\\LightLaunchpad\\Tests\\"))
    {
        return testSubkey;
    }
    return L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}

bool CaptureStartupRegistration(StartupRegistrationSnapshot& snapshot)
{
    snapshot = {};
    HKEY key = nullptr;
    const auto subkey = StartupRegistrySubkey();
    const LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (openResult == ERROR_FILE_NOT_FOUND)
    {
        snapshot.valid = true;
        return true;
    }
    if (openResult != ERROR_SUCCESS) return false;

    DWORD type = 0;
    DWORD bytes = 0;
    const LONG sizeResult = RegQueryValueExW(key, L"LightLaunchpad", nullptr, &type, nullptr, &bytes);
    if (sizeResult == ERROR_FILE_NOT_FOUND)
    {
        snapshot.valid = true;
        RegCloseKey(key);
        return true;
    }
    if (sizeResult != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t))
    {
        RegCloseKey(key);
        return false;
    }

    std::vector<wchar_t> value((bytes / sizeof(wchar_t)) + 1, L'\0');
    const LONG readResult = RegQueryValueExW(key, L"LightLaunchpad", nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    if (readResult != ERROR_SUCCESS) return false;
    snapshot.valid = true;
    snapshot.exists = true;
    snapshot.type = type;
    snapshot.value = value.data();
    return true;
}

bool WriteStartupRegistrationValue(bool exists, DWORD type, const std::wstring& value)
{
    HKEY key = nullptr;
    const auto subkey = StartupRegistrySubkey();
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    LONG result = ERROR_SUCCESS;
    if (exists)
    {
        result = RegSetValueExW(key, L"LightLaunchpad", 0, type, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        result = RegDeleteValueW(key, L"LightLaunchpad");
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }

    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool SetStartWithWindows(bool enabled)
{
    if (!enabled) return WriteStartupRegistrationValue(false, REG_SZ, {});
    wchar_t exe[MAX_PATH] = L"";
    if (GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe)) == 0) return false;
    const std::wstring command = L"\"" + std::wstring(exe) + L"\"";
    return WriteStartupRegistrationValue(true, REG_SZ, command);
}

bool RestoreStartupRegistration(const StartupRegistrationSnapshot& snapshot)
{
    return snapshot.valid && WriteStartupRegistrationValue(snapshot.exists, snapshot.type, snapshot.value);
}

void OpenLaunchpadFolder()
{
    if (g_shellBroker.RequestOpenFolder(g_settings.launchpadFolder) == 0)
    {
        MessageBoxW(g_hwnd, L"The Shell broker is unavailable; the launchpad folder was not opened.", L"LightLaunchpad", MB_ICONWARNING | MB_OK);
    }
}

void ImportLaunchableFiles()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"Launchable files (*.lnk;*.url;*.exe;*.com)\0*.lnk;*.url;*.exe;*.com\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = Text(L"Import launchable files", L"导入可启动文件");
    if (!GetOpenFileNameW(&ofn)) return;

    std::vector<std::wstring> sources;
    const wchar_t* cursor = buffer.data();
    std::wstring first = cursor;
    cursor += first.size() + 1;
    if (*cursor == L'\0')
    {
        sources.push_back(first);
    }
    else
    {
        const std::wstring directory = first;
        while (*cursor)
        {
            const std::wstring path = JoinPath(directory, cursor);
            sources.push_back(path);
            cursor += lstrlenW(cursor) + 1;
        }
    }

    if (!sources.empty() && g_shellBroker.RequestImportPaths(sources, g_settings.launchpadFolder) == 0)
    {
        MessageBoxW(g_hwnd, L"The Shell broker is unavailable; no files were imported.", L"LightLaunchpad import", MB_ICONWARNING | MB_OK);
    }
}

void ImportStartMenuApps()
{
    const auto appData = GetAppDataPath();
    const auto programData = GetProgramDataPath();
    int queued = 0;
    if (!appData.empty())
    {
        queued += g_shellBroker.RequestImportTree(JoinPath(appData, L"Microsoft\\Windows\\Start Menu\\Programs"), g_settings.launchpadFolder) != 0 ? 1 : 0;
    }
    if (!programData.empty())
    {
        queued += g_shellBroker.RequestImportTree(JoinPath(programData, L"Microsoft\\Windows\\Start Menu\\Programs"), g_settings.launchpadFolder) != 0 ? 1 : 0;
    }
    if (queued == 0)
    {
        MessageBoxW(g_hwnd, L"The Shell broker is unavailable; Start Menu import was not queued.", L"LightLaunchpad import", MB_ICONWARNING | MB_OK);
    }
}

bool SameHotkey(const std::wstring& left, const std::wstring& right)
{
    const auto normalizedLeft = NormalizeHotkeyText(left);
    const auto normalizedRight = NormalizeHotkeyText(right);
    return CompareStringOrdinal(normalizedLeft.c_str(), -1, normalizedRight.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool ReserveCandidateHotkey(const Settings& candidate, int& reservedId, std::wstring& failure)
{
    reservedId = 0;
    failure.clear();
    if (SameHotkey(candidate.hotkey, g_settings.hotkey) && g_hotkeyRegistered) return true;

    const int candidateId = g_activeHotkeyId == HotkeyId ? CandidateHotkeyId : HotkeyId;
    UnregisterHotKey(g_hwnd, candidateId);
    UINT modifiers = 0;
    UINT key = 0;
    ResolveHotkey(candidate.hotkey, modifiers, key);
    modifiers |= MOD_NOREPEAT;
    if (!RegisterHotKey(g_hwnd, candidateId, modifiers, key))
    {
        const DWORD error = GetLastError();
        failure = L"Hotkey conflict for " + candidate.hotkey
            + L" (Windows error " + std::to_wstring(error) + L"); kept prior setting " + g_settings.hotkey + L" and its registration state.";
        return false;
    }

    reservedId = candidateId;
    return true;
}

void ReleaseReservedHotkey(int reservedId)
{
    if (reservedId != 0 && g_hwnd) UnregisterHotKey(g_hwnd, reservedId);
}

bool ApplySettingsFromWindow(HWND hwnd)
{
    const Settings previous = g_settings;
    const Settings candidate = ReadSettingsWindowCandidate(hwnd);
    int reservedHotkeyId = 0;
    std::wstring hotkeyFailure;
    if (!ReserveCandidateHotkey(candidate, reservedHotkeyId, hotkeyFailure))
    {
        g_settingsStatus = hotkeyFailure;
        AppendStartupLog(L"settings-apply", false, g_settingsStatus);
        PopulateSettingsWindow(hwnd);
        return false;
    }

    const bool startupChanged = candidate.startWithWindows != previous.startWithWindows;
    StartupRegistrationSnapshot startupSnapshot;
    if (startupChanged)
    {
        if (!CaptureStartupRegistration(startupSnapshot) || !SetStartWithWindows(candidate.startWithWindows))
        {
            ReleaseReservedHotkey(reservedHotkeyId);
            g_settingsStatus = L"Startup registration failed; prior settings and hotkey were kept.";
            AppendStartupLog(L"settings-apply", false, g_settingsStatus);
            PopulateSettingsWindow(hwnd);
            return false;
        }
    }

    if (!SaveSettings(candidate))
    {
        const bool startupRestored = !startupChanged || RestoreStartupRegistration(startupSnapshot);
        ReleaseReservedHotkey(reservedHotkeyId);
        g_settingsStatus = startupRestored
            ? L"Settings write failed; prior values and hotkey were kept."
            : L"Settings write failed and startup rollback also failed; prior in-memory values and hotkey were kept.";
        AppendStartupLog(L"settings-apply", false, g_settingsStatus);
        PopulateSettingsWindow(hwnd);
        return false;
    }

    if (reservedHotkeyId != 0)
    {
        UnregisterHotKey(g_hwnd, g_activeHotkeyId);
        g_activeHotkeyId = reservedHotkeyId;
        g_hotkeyRegistered = true;
    }
    g_settings = candidate;
    g_settingsStatus = L"Settings saved. Active hotkey: " + g_settings.hotkey + L".";
    AppendStartupLog(L"settings-apply", true, g_settingsStatus);
    ReloadData();
    RebuildFiltered();
    PositionWindow();
    PopulateSettingsWindow(hwnd);
    InvalidateRect(g_hwnd, nullptr, TRUE);
    return true;
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        const int labelX = 24;
        const int inputX = 184;
        const int inputW = 332;
        int y = 24;
        CreateSettingsLabel(hwnd, SettingsControlId::FolderLabel, labelX, y);
        CreateSettingsEdit(hwnd, SettingsControlId::LaunchpadFolder, inputX, y, inputW);
        y += 38;
        CreateSettingsLabel(hwnd, SettingsControlId::HotkeyLabel, labelX, y);
        CreateSettingsEdit(hwnd, SettingsControlId::Hotkey, inputX, y, inputW);
        y += 38;
        CreateSettingsLabel(hwnd, SettingsControlId::IconSizeLabel, labelX, y);
        CreateSettingsCombo(hwnd, SettingsControlId::IconSize, inputX, y, inputW);
        y += 38;
        CreateSettingsLabel(hwnd, SettingsControlId::DisplayModeLabel, labelX, y);
        CreateSettingsCombo(hwnd, SettingsControlId::DisplayMode, inputX, y, inputW);
        y += 38;
        CreateSettingsLabel(hwnd, SettingsControlId::LanguageLabel, labelX, y);
        CreateSettingsCombo(hwnd, SettingsControlId::Language, inputX, y, inputW);
        ResetLanguageCombo(hwnd);
        y += 38;
        CreateSettingsLabel(hwnd, SettingsControlId::SpotlightSizeLabel, labelX, y);
        CreateSettingsEdit(hwnd, SettingsControlId::SpotlightWidth, inputX, y, 96);
        ApplyControlFont(CreateWindowW(L"STATIC", L"x", WS_CHILD | WS_VISIBLE | SS_CENTER, inputX + 106, y + 5, 24, 24, hwnd, nullptr, g_instance, nullptr));
        CreateSettingsEdit(hwnd, SettingsControlId::SpotlightHeight, inputX + 140, y, 96);
        y += 38;
        CreateSettingsLabel(hwnd, SettingsControlId::AppSpacingLabel, labelX, y);
        CreateSettingsEdit(hwnd, SettingsControlId::AppSpacing, inputX, y, 96);
        y += 38;
        CreateSettingsLabel(hwnd, SettingsControlId::WheelSensitivityLabel, labelX, y);
        CreateSettingsEdit(hwnd, SettingsControlId::WheelSensitivity, inputX, y, 96);
        y += 38;
        ApplyControlFont(CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, inputX, y, inputW, 24, hwnd, reinterpret_cast<HMENU>(ControlId(SettingsControlId::StartWithWindows)), g_instance, nullptr));
        y += 32;
        ApplyControlFont(CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, inputX, y, inputW, 24, hwnd, reinterpret_cast<HMENU>(ControlId(SettingsControlId::ResetSearchOnOpen)), g_instance, nullptr));
        y += 34;
        CreateSettingsLabel(hwnd, SettingsControlId::DiagnosticsLabel, labelX, y);
        y += 24;
        ApplyControlFont(CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            labelX,
            y,
            492,
            168,
            hwnd,
            reinterpret_cast<HMENU>(ControlId(SettingsControlId::DiagnosticsSummary)),
            g_instance,
            nullptr));
        y += 184;
        ApplyControlFont(CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, inputX + 48, y, 88, 30, hwnd, reinterpret_cast<HMENU>(ControlId(SettingsControlId::Save)), g_instance, nullptr));
        ApplyControlFont(CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, inputX + 146, y, 88, 30, hwnd, reinterpret_cast<HMENU>(ControlId(SettingsControlId::Apply)), g_instance, nullptr));
        ApplyControlFont(CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, inputX + 244, y, 88, 30, hwnd, reinterpret_cast<HMENU>(ControlId(SettingsControlId::Cancel)), g_instance, nullptr));
        PopulateSettingsWindow(hwnd);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ControlId(SettingsControlId::Language) && HIWORD(wParam) == CBN_SELCHANGE)
        {
            ApplyLanguageToSettingsWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == ControlId(SettingsControlId::Save))
        {
            if (ApplySettingsFromWindow(hwnd)) DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == ControlId(SettingsControlId::Apply))
        {
            ApplySettingsFromWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == ControlId(SettingsControlId::Cancel))
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_settingsHwnd == hwnd) g_settingsHwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ShowSettingsWindow()
{
    if (g_settingsHwnd)
    {
        ShowWindow(g_settingsHwnd, IsIconic(g_settingsHwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(g_settingsHwnd);
        return;
    }

    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.hInstance = g_instance;
        wc.lpfnWndProc = SettingsWndProc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = SettingsWindowClassName;
        RegisterClassExW(&wc);
        registered = true;
    }

    g_settingsHwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | (g_diagnosticsUiMode ? 0 : WS_EX_TOOLWINDOW),
        SettingsWindowClassName,
        Text(L"LightLaunchpad Settings", L"LightLaunchpad 设置"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        560,
        730,
        g_diagnosticsUiMode ? nullptr : g_hwnd,
        nullptr,
        g_instance,
        nullptr);
    if (g_settingsHwnd)
    {
        ShowWindow(g_settingsHwnd, SW_SHOW);
        SetForegroundWindow(g_settingsHwnd);
    }
}

struct TextInputState
{
    std::wstring title;
    std::wstring label;
    std::wstring value;
    bool accepted = false;
    HWND edit = nullptr;
};

LRESULT CALLBACK TextInputWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<TextInputState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message)
    {
    case WM_CREATE:
    {
        state = reinterpret_cast<TextInputState*>(reinterpret_cast<LPCREATESTRUCTW>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        SetWindowTextW(hwnd, state->title.c_str());
        ApplyControlFont(CreateWindowW(L"STATIC", state->label.c_str(), WS_CHILD | WS_VISIBLE, 18, 18, 340, 24, hwnd, nullptr, g_instance, nullptr));
        state->edit = ApplyControlFont(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->value.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 18, 48, 340, 26, hwnd, reinterpret_cast<HMENU>(1), g_instance, nullptr));
        ApplyControlFont(CreateWindowW(L"BUTTON", Text(L"OK", L"确定"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 166, 90, 88, 30, hwnd, reinterpret_cast<HMENU>(IDOK), g_instance, nullptr));
        ApplyControlFont(CreateWindowW(L"BUTTON", Text(L"Cancel", L"取消"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 270, 90, 88, 30, hwnd, reinterpret_cast<HMENU>(IDCANCEL), g_instance, nullptr));
        SendMessageW(state->edit, EM_SETSEL, 0, -1);
        SetFocus(state->edit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK && state)
        {
            state->value = GetControlText(hwnd, 1, 512);
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool ShowTextInputDialog(const std::wstring& title, const std::wstring& label, const std::wstring& initialValue, std::wstring& result)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.hInstance = g_instance;
        wc.lpfnWndProc = TextInputWndProc;
        wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = TextInputWindowClassName;
        RegisterClassExW(&wc);
        registered = true;
    }

    TextInputState state{ title, label, initialValue };
    ++g_modalDialogDepth;
    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        TextInputWindowClassName,
        title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        394,
        176,
        g_hwnd,
        nullptr,
        g_instance,
        &state);
    if (!dialog)
    {
        --g_modalDialogDepth;
        return false;
    }

    EnableWindow(g_hwnd, FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetForegroundWindow(dialog);

    MSG msg = {};
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(dialog, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(g_hwnd, TRUE);
    SetForegroundWindow(g_hwnd);
    --g_modalDialogDepth;
    if (state.accepted)
    {
        result = Trim(state.value);
        return !result.empty();
    }
    return false;
}

void ShowTrayMenu()
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, TrayOpenCommand, Text(L"Open launchpad", L"打开启动台"));
    AppendMenuW(menu, MF_STRING, TrayOpenFolderCommand, Text(L"Open folder", L"打开文件夹"));
    AppendMenuW(menu, MF_STRING, TrayRefreshCommand, Text(L"Refresh", L"刷新"));
    AppendMenuW(menu, MF_STRING, TraySettingsCommand, Text(L"Settings", L"设置"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayExitCommand, Text(L"Exit", L"退出"));
    POINT point = {};
    GetCursorPos(&point);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
}

void HandleTrayNotification(LPARAM lParam)
{
    const UINT event = LOWORD(lParam);
    if (event == NIN_SELECT || event == WM_LBUTTONUP)
    {
        ToggleNativeUi();
        return;
    }

    if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP)
    {
        ShowTrayMenu();
    }
}

void LaunchItemBySourcePath(const std::wstring& sourcePath)
{
    auto found = std::find_if(g_items.begin(), g_items.end(), [&](const LaunchItem& item) {
        return SamePath(item.sourcePath, sourcePath);
    });
    if (found != g_items.end())
    {
        if (g_shellBroker.RequestLaunch(found->sourcePath) != 0) HideNativeUi();
        else MessageBoxW(g_hwnd, L"The Shell broker is unavailable; the item was not launched.", L"LightLaunchpad launch", MB_ICONWARNING | MB_OK);
    }
}

void OpenItemLocation(const std::wstring& sourcePath)
{
    if (g_shellBroker.RequestOpenLocation(sourcePath) == 0)
    {
        MessageBoxW(g_hwnd, L"The Shell broker is unavailable; the item location was not opened.", L"LightLaunchpad", MB_ICONWARNING | MB_OK);
    }
}

void ShowItemContextMenu(POINT point, int filteredIndex)
{
    if (filteredIndex < 0 || filteredIndex >= static_cast<int>(g_filtered.size())) return;
    const auto& item = g_items[g_filtered[filteredIndex]];
    if (g_launchpadState.IsEditing() && !IsSelected(item.sourcePath))
    {
        SetSingleSelection(item.sourcePath);
    }
    g_contextItemPaths = g_launchpadState.IsEditing() ? ResolveDragSourcePaths(item) : std::vector<std::wstring>{ item.sourcePath };

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, ItemLaunchCommand, Text(L"Launch", L"启动"));
    AppendMenuW(menu, MF_STRING, ItemOpenLocationCommand, Text(L"Open file location", L"打开文件位置"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    if (g_launchpadState.IsEditing())
    {
        AppendMenuW(menu, g_contextItemPaths.size() > 1 ? MF_STRING | MF_GRAYED : MF_STRING, ItemRenameCommand, Text(L"Rename", L"重命名"));
        AppendMenuW(menu, MF_STRING, ItemRemoveCommand, g_contextItemPaths.size() > 1 ? Text(L"Delete selected icons", L"删除已选图标") : Text(L"Delete icon", L"删除图标"));
    }
    else AppendMenuW(menu, MF_STRING, WorkspaceToggleEditCommand, Text(L"Edit apps and regions", L"编辑应用与区域"));
    ClientToScreen(g_hwnd, &point);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
}

void ShowRegionContextMenu(POINT point, const std::wstring& regionId)
{
    if (!g_launchpadState.IsEditing())
    {
        ShowWorkspaceContextMenu(point);
        return;
    }
    g_contextRegionId = regionId;
    if (!IsRegionSelected(regionId))
    {
        SetSingleRegionSelection(regionId);
    }
    g_contextRegionIds = ResolveRegionContextIds(regionId);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const UINT deleteFlags = IsUncategorizedRegion(regionId)
        ? MF_STRING | MF_GRAYED
        : MF_STRING;
    const bool canDeleteAny = std::any_of(g_contextRegionIds.begin(), g_contextRegionIds.end(), [](const std::wstring& id) {
        return !IsUncategorizedRegion(id);
    });
    AppendMenuW(menu, MF_STRING, RegionRenameCommand, Text(L"Rename region", L"重命名 region"));
    AppendMenuW(menu, deleteFlags, RegionDeleteCommand, Text(L"Delete region", L"删除 region"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, g_contextRegionIds.empty() ? MF_STRING | MF_GRAYED : MF_STRING, RegionRenameSelectedCommand, Text(L"Rename selected regions", L"重命名已选 region"));
    AppendMenuW(menu, canDeleteAny ? MF_STRING : MF_STRING | MF_GRAYED, RegionDeleteSelectedCommand, Text(L"Delete selected regions", L"删除已选 region"));
    ClientToScreen(g_hwnd, &point);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
}

void ShowWorkspaceContextMenu(POINT point)
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, WorkspaceToggleEditCommand, g_launchpadState.IsEditing() ? Text(L"Done editing", L"完成编辑") : Text(L"Edit apps and regions", L"编辑应用与区域"));
    if (g_launchpadState.IsEditing())
    {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, WorkspaceCreateRegionCommand, Text(L"Create region", L"新建 region"));
        AppendMenuW(menu, MF_STRING, WorkspaceImportFilesCommand, Text(L"Import files", L"导入文件"));
        AppendMenuW(menu, MF_STRING, WorkspaceImportStartMenuCommand, Text(L"Import Start Menu apps", L"导入开始菜单应用"));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, WorkspaceSelectAllCommand, Text(L"Select all", L"全选"));
        AppendMenuW(menu, MF_STRING, WorkspaceClearSelectionCommand, Text(L"Clear selection", L"清除选择"));
    }
    ClientToScreen(g_hwnd, &point);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
}

HFONT CreateSearchInputFont()
{
    return CreateFontW(26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

int MeasureSearchPrefixGdi(HWND hwnd, const std::wstring& text, size_t position)
{
    HDC dc = GetDC(hwnd);
    if (!dc) return 0;
    HFONT font = CreateSearchInputFont();
    HGDIOBJ oldFont = SelectObject(dc, font);
    const int width = MeasureTextWidthGdi(dc, text.substr(0, std::min(position, text.size())));
    SelectObject(dc, oldFont);
    DeleteObject(font);
    ReleaseDC(hwnd, dc);
    return width;
}

size_t SearchTextPositionFromClientX(HWND hwnd, int clientX)
{
    RECT client = {};
    GetClientRect(hwnd, &client);
    const RECT textRect = SearchTextRect(SearchRect(client));
    const std::wstring text = SearchCommittedText();
    const int contentX = clientX - textRect.left + g_searchTextOffset;
    int previousWidth = 0;
    for (size_t position = 1; position <= text.size(); ++position)
    {
        const unsigned int current = static_cast<unsigned short>(text[position - 1]);
        if (current >= 0xD800 && current <= 0xDBFF && position < text.size()) continue;
        const int width = MeasureSearchPrefixGdi(hwnd, text, position);
        if (contentX < previousWidth + (width - previousWidth) / 2) return position - 1;
        previousWidth = width;
    }
    return text.size();
}

POINT SearchCaretClientPoint(HWND hwnd)
{
    RECT client = {};
    GetClientRect(hwnd, &client);
    const RECT textRect = SearchTextRect(SearchRect(client));
    const std::wstring displayText = SearchDisplayText();
    const int caretWidth = MeasureSearchPrefixGdi(hwnd, displayText, g_searchInput.DisplayCaret());
    const int totalWidth = MeasureSearchPrefixGdi(hwnd, displayText, displayText.size());
    UpdateSearchTextOffset(caretWidth, totalWidth, std::max(1L, textRect.right - textRect.left - 4));
    return {
        std::clamp(textRect.left + caretWidth - g_searchTextOffset, textRect.left + 1, textRect.right - 2),
        textRect.bottom - 8
    };
}

void UpdateImeWindowPosition(HWND hwnd)
{
    if (!hwnd) return;
    HIMC context = ImmGetContext(hwnd);
    if (!context) return;
    const POINT caret = SearchCaretClientPoint(hwnd);
    COMPOSITIONFORM composition = {};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = caret;
    CANDIDATEFORM candidate = {};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = caret;
    const bool compositionApplied = ImmSetCompositionWindow(context, &composition) != FALSE;
    const bool candidateApplied = ImmSetCandidateWindow(context, &candidate) != FALSE;
    g_lastImeCandidatePoint = caret;
    g_imeCandidatePositionApplied = compositionApplied && candidateApplied;
    ImmReleaseContext(hwnd, context);
}

std::wstring ReadClipboardText(HWND hwnd)
{
    if (!OpenClipboard(hwnd)) return {};
    std::wstring result;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data)
    {
        const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));
        if (text)
        {
            result = text;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    std::wstring sanitized;
    sanitized.reserve(result.size());
    bool previousSpace = false;
    for (wchar_t character : result)
    {
        if (character == L'\r' || character == L'\n' || character == L'\t')
        {
            if (!previousSpace && !sanitized.empty()) sanitized.push_back(L' ');
            previousSpace = true;
        }
        else if (character >= 32)
        {
            sanitized.push_back(character);
            previousSpace = character == L' ';
        }
    }
    return sanitized;
}

bool WriteClipboardText(HWND hwnd, const std::wstring& text)
{
    if (text.empty() || !OpenClipboard(hwnd)) return false;
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!data)
    {
        CloseClipboard();
        return false;
    }
    void* destination = GlobalLock(data);
    if (!destination)
    {
        GlobalFree(data);
        CloseClipboard();
        return false;
    }
    std::memcpy(destination, text.c_str(), bytes);
    GlobalUnlock(data);
    bool succeeded = false;
    if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data))
    {
        succeeded = true;
        data = nullptr;
    }
    if (data) GlobalFree(data);
    CloseClipboard();
    return succeeded;
}

void SearchTextChanged(HWND hwnd, bool erase)
{
    g_scrollOffset = 0;
    RebuildFiltered();
    UpdateImeWindowPosition(hwnd);
    RequestSearchRepaint(hwnd, erase);
    g_searchAccessibility.NotifyTextChanged();
    g_searchAccessibility.NotifySelectionChanged();
}

void SearchCaretChanged(HWND hwnd)
{
    UpdateImeWindowPosition(hwnd);
    RequestSearchRepaint(hwnd, false);
    g_searchAccessibility.NotifySelectionChanged();
}

void AccessibilityFocus(void*)
{
    ActivateSearchInput();
}

bool AccessibilitySearchHasFocus(void*)
{
    return IsSpotlightMode()
        ? g_spotlightState.Focus() == llp::interaction::FocusArea::Search
        : g_searchInputActive;
}

void AccessibilityTextChanged(void*)
{
    SearchTextChanged(g_hwnd, true);
}

void AccessibilitySelectionChanged(void*)
{
    SearchCaretChanged(g_hwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == g_taskbarCreatedMessage && g_taskbarCreatedMessage != 0)
    {
        if (!g_diagnosticsUiMode)
        {
            ++g_taskbarRecoveryCount;
            g_trayRetryAttempts = 0;
            AppendStartupLog(L"taskbar-created", true, L"restoring tray icon");
            ReaddTrayIcon();
            if (!g_startupProbePath.empty())
            {
                WriteFileUtf8(g_startupProbePath, BuildStartupProbeJson());
            }
        }
        return 0;
    }

    switch (message)
    {
    case WM_GETOBJECT:
        return g_searchAccessibility.HandleGetObject(wParam, lParam);
    case WM_COPYDATA:
    {
        const auto* copyData = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (!copyData
            || copyData->dwData != ActivationCopyDataId
            || !copyData->lpData
            || copyData->cbData < sizeof(wchar_t)
            || copyData->cbData > 2048
            || copyData->cbData % sizeof(wchar_t) != 0)
        {
            return 0;
        }
        const auto* requestData = static_cast<const wchar_t*>(copyData->lpData);
        const size_t requestLength = copyData->cbData / sizeof(wchar_t);
        std::wstring requestBuildId(requestData, requestData + requestLength);
        while (!requestBuildId.empty() && requestBuildId.back() == L'\0') requestBuildId.pop_back();
        const bool mismatch = CompareStringOrdinal(requestBuildId.c_str(), -1, BuildId().c_str(), -1, FALSE) != CSTR_EQUAL;
        AppendStartupLog(
            L"activation-request",
            true,
            mismatch ? L"version or channel mismatch" : L"matching build");
        PostMessageW(hwnd, WmActivateInstance, mismatch ? 1 : 0, 0);
        return mismatch ? 2 : 1;
    }
    case WmActivateInstance:
    {
        if (g_diagnosticsUiMode)
        {
            ShowSettingsWindow();
        }
        else
        {
            ShowNativeUi();
        }
        if (wParam != 0 && g_trayIconAdded)
        {
            NOTIFYICONDATAW nid = {};
            PopulateTrayIconData(nid);
            nid.uFlags = NIF_INFO;
            CopyText(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), L"LightLaunchpad build mismatch");
            CopyText(nid.szInfo, ARRAYSIZE(nid.szInfo), L"Activated the running instance, but the new executable has a different version or channel.");
            nid.dwInfoFlags = NIIF_WARNING;
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        }
        return 0;
    }
    case WM_NCHITTEST:
    {
        if (IsSpotlightMode())
        {
            POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &point);
            if (!IsPointInsideSpotlightGlass(point))
            {
                return HTTRANSPARENT;
            }
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    case WM_HOTKEY:
        if (wParam == static_cast<WPARAM>(g_activeHotkeyId))
        {
            const ULONGLONG started = GetTickCount64();
            ToggleNativeUi();
            if (g_launchpadFlowProbe.active && IsWindowVisible(hwnd)) g_launchpadFlowProbe.hotkeyMessages++;
            if (g_spotlightFlowProbe.active && IsWindowVisible(hwnd))
            {
                g_spotlightFlowProbe.hotkeyMessages++;
                g_spotlightFlowProbe.inputReadyMilliseconds.push_back(GetTickCount64() - started);
                g_spotlightFlowProbe.selectedIndexOnOpen.push_back(g_selectedFilteredIndex);
                if (g_spotlightState.Focus() == llp::interaction::FocusArea::Search) g_spotlightFlowProbe.searchFocusedOnOpen++;
            }
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == TrayOpenCommand) ShowNativeUi();
        if (LOWORD(wParam) == TrayOpenFolderCommand) OpenLaunchpadFolder();
        if (LOWORD(wParam) == TrayRefreshCommand) { ReloadData(); RebuildFiltered(); InvalidateRect(hwnd, nullptr, TRUE); }
        if (LOWORD(wParam) == TraySettingsCommand) ShowSettingsWindow();
        if (LOWORD(wParam) == TrayExitCommand) DestroyWindow(hwnd);
        if (LOWORD(wParam) == ItemLaunchCommand && !g_contextItemPaths.empty()) LaunchItemBySourcePath(g_contextItemPaths.front());
        if (LOWORD(wParam) == WorkspaceToggleEditCommand && !IsSpotlightMode()) SetLaunchpadEditing(!g_launchpadState.IsEditing());
        if (LOWORD(wParam) == ItemRenameCommand && g_launchpadState.IsEditing() && g_contextItemPaths.size() == 1) RenameItem(g_contextItemPaths.front());
        if (LOWORD(wParam) == ItemOpenLocationCommand && !g_contextItemPaths.empty()) OpenItemLocation(g_contextItemPaths.front());
        if (LOWORD(wParam) == ItemRemoveCommand && g_launchpadState.IsEditing() && !g_contextItemPaths.empty())
        {
            if (g_shellBroker.RequestRecycle(g_settings.launchpadFolder, g_contextItemPaths) == 0)
            {
                MessageBoxW(hwnd, Text(L"The Shell broker is unavailable; no icons were deleted.", L"Shell 后台不可用，未删除任何图标。"), Text(L"Delete icon", L"删除图标"), MB_ICONWARNING | MB_OK);
            }
        }
        if (LOWORD(wParam) == RegionRenameCommand && g_launchpadState.IsEditing() && !g_contextRegionId.empty())
        {
            RenameRegion(g_contextRegionId);
        }
        if (LOWORD(wParam) == RegionDeleteCommand && g_launchpadState.IsEditing() && !g_contextRegionId.empty())
        {
            DeleteRegionAndMoveItems(g_contextRegionId);
            SortItems();
            SaveLayout();
            RebuildFiltered();
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        if (LOWORD(wParam) == RegionRenameSelectedCommand && g_launchpadState.IsEditing() && !g_contextRegionIds.empty())
        {
            RenameSelectedRegions(g_contextRegionIds);
        }
        if (LOWORD(wParam) == RegionDeleteSelectedCommand && g_launchpadState.IsEditing() && !g_contextRegionIds.empty())
        {
            DeleteSelectedRegions(g_contextRegionIds);
        }
        if (LOWORD(wParam) == WorkspaceCreateRegionCommand && g_launchpadState.IsEditing()) CreateRegion();
        if (LOWORD(wParam) == WorkspaceImportFilesCommand && g_launchpadState.IsEditing()) ImportLaunchableFiles();
        if (LOWORD(wParam) == WorkspaceImportStartMenuCommand && g_launchpadState.IsEditing()) ImportStartMenuApps();
        if (LOWORD(wParam) == WorkspaceSelectAllCommand && g_launchpadState.IsEditing()) SelectAllItems();
        if (LOWORD(wParam) == WorkspaceClearSelectionCommand && g_launchpadState.IsEditing()) ClearAllSelection();
        return 0;
    case WmTray:
        HandleTrayNotification(lParam);
        return 0;
    case WmShellBrokerResult:
        HandleShellBrokerResult(lParam);
        return 0;
    case WM_PAINT:
        PaintWindow(hwnd);
        return 0;
    case WM_TIMER:
        if (wParam == TrayIconRetryTimerId)
        {
            AddTrayIcon();
            return 0;
        }
        if (wParam == DragAnimationTimerId && g_dragActive)
        {
            if (g_dragMode == DragMode::Items)
            {
                UpdateStableDropTarget(g_dragCurrent);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_MOUSEWHEEL:
        g_scrollOffset -= static_cast<int>(std::round((GET_WHEEL_DELTA_WPARAM(wParam) / static_cast<double>(WHEEL_DELTA)) * WheelScrollStep()));
        ClampScroll();
        if (g_selectionBoxActive)
        {
            POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &point);
            UpdateSelectionBox(point);
            return 0;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (g_launchpadFlowProbe.active)
        {
            g_launchpadFlowProbe.lastPointerX = point.x;
            g_launchpadFlowProbe.lastPointerY = point.y;
            g_launchpadFlowProbe.lastHitCount = static_cast<int>(g_hits.size());
            g_launchpadFlowProbe.lastMatchedIndex = -1;
        }
        if (IsSpotlightMode() && !IsPointInsideSpotlightGlass(point))
        {
            HideNativeUi();
            return 0;
        }
        RECT client = {};
        GetClientRect(hwnd, &client);
        RECT searchRect = SearchRect(client);
        if (PtInRect(&searchRect, point))
        {
            ActivateSearchInput();
            if (g_searchInput.IsComposing())
            {
                HIMC context = ImmGetContext(hwnd);
                if (context)
                {
                    ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                    ImmReleaseContext(hwnd, context);
                }
                g_searchInput.CancelComposition();
                RebuildFiltered();
            }
            g_searchInput.SetCaret(SearchTextPositionFromClientX(hwnd, point.x), (wParam & MK_SHIFT) != 0);
            g_searchPointerSelecting = true;
            SetCapture(hwnd);
            SearchCaretChanged(hwnd);
            return 0;
        }
        if (!IsSpotlightMode())
        {
            const RECT editRect = LaunchpadEditRect(client);
            if (PtInRect(&editRect, point))
            {
                SetLaunchpadEditing(!g_launchpadState.IsEditing());
                return 0;
            }
        }
        if (IsSpotlightMode())
        {
            const RECT auxiliaryRect = SpotlightAuxiliaryRect(client);
            if (PtInRect(&auxiliaryRect, point))
            {
                g_spotlightState.FocusAuxiliary();
                ApplySpotlightInteractionState(false);
                HideNativeUi();
                ShowSettingsWindow();
                return 0;
            }
        }
        ResetDragState();
        for (const auto& hit : g_hits)
        {
            if (PtInRect(&hit.bounds, point))
            {
                if (g_launchpadFlowProbe.active) g_launchpadFlowProbe.lastMatchedIndex = hit.itemIndex;
                if (IsSpotlightMode())
                {
                    LaunchItemAtFilteredIndex(hit.itemIndex);
                    return 0;
                }
                g_selectedFilteredIndex = hit.itemIndex;
                g_searchInputActive = false;
                g_searchAccessibility.NotifyFocusChanged();
                const auto action = g_launchpadState.ClickItem((wParam & MK_CONTROL) != 0);
                if (action == llp::interaction::LaunchpadClickAction::LaunchItem)
                {
                    if (g_launchpadFlowProbe.active)
                    {
                        g_launchpadFlowProbe.browsePresses++;
                        g_launchpadFlowProbe.blockedBrowseDragRecorded = false;
                        WriteLaunchpadFlowReadyState();
                    }
                    ClearAllSelection();
                    g_pressedFilteredIndex = hit.itemIndex;
                    g_mouseDown = true;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (hit.itemIndex >= 0 && hit.itemIndex < static_cast<int>(g_filtered.size()))
                {
                    const auto& item = g_items[g_filtered[hit.itemIndex]];
                    if (action == llp::interaction::LaunchpadClickAction::ToggleItemSelection)
                    {
                        ToggleSelection(item.sourcePath);
                    }
                    else if (!IsSelected(item.sourcePath))
                    {
                        SetSingleSelection(item.sourcePath);
                    }
                    if (g_launchpadFlowProbe.active)
                    {
                        g_launchpadFlowProbe.editSelectionClicks++;
                        g_launchpadFlowProbe.maxSelectedItems = std::max(g_launchpadFlowProbe.maxSelectedItems, static_cast<int>(g_selectedSourcePaths.size()));
                    }
                }
                g_mouseDown = SearchDisplayText().empty();
                g_dragFilteredIndex = hit.itemIndex;
                g_dragStart = point;
                g_dragCurrent = point;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        if (g_launchpadFlowProbe.active) WriteLaunchpadFlowReadyState();
        if (IsSpotlightMode()) return 0;
        if (g_launchpadState.AllowsSelection()) for (const auto& regionHit : g_regionHits)
        {
            if (PtInRect(&regionHit.headerBounds, point))
            {
                g_searchInputActive = false;
                g_searchAccessibility.NotifyFocusChanged();
                if (wParam & MK_CONTROL)
                {
                    ToggleRegionSelection(regionHit.regionId);
                }
                else if (!IsRegionSelected(regionHit.regionId))
                {
                    SetSingleRegionSelection(regionHit.regionId);
                }
                g_mouseDown = SearchDisplayText().empty();
                g_pendingRegionDragId = regionHit.regionId;
                g_dragStart = point;
                g_dragCurrent = point;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        if (g_launchpadState.AllowsMarquee() && point.y >= ContentClipTop())
        {
            g_selectionBoxAdditive = (wParam & MK_CONTROL) != 0;
            BeginSelectionBox(hwnd, point, g_selectionBoxAdditive);
            return 0;
        }
        ClearAllSelection();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int hovered = -1;
        for (const auto& hit : g_hits)
        {
            if (PtInRect(&hit.bounds, point))
            {
                hovered = hit.itemIndex;
                break;
            }
        }
        if (hovered != g_hoveredFilteredIndex)
        {
            g_hoveredFilteredIndex = hovered;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tracking = { sizeof(tracking), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tracking);
        if (g_searchPointerSelecting)
        {
            g_searchInput.SetCaret(SearchTextPositionFromClientX(hwnd, point.x), true);
            SearchCaretChanged(hwnd);
            return 0;
        }
        if (g_selectionBoxActive)
        {
            UpdateSelectionBox(point);
            return 0;
        }
        if (g_launchpadState.IsEditing() && g_mouseDown && !g_dragActive && HasExceededDragThreshold(point))
        {
            if (g_dragFilteredIndex >= 0)
            {
                BeginDrag(hwnd, point);
            }
            else if (!g_pendingRegionDragId.empty())
            {
                BeginRegionDrag(hwnd, point);
            }
        }
        else if (!IsSpotlightMode() && !g_launchpadState.IsEditing() && g_mouseDown && HasExceededDragThreshold(point)
            && g_launchpadFlowProbe.active && !g_launchpadFlowProbe.blockedBrowseDragRecorded)
        {
            g_launchpadFlowProbe.blockedBrowseDragRecorded = true;
            g_launchpadFlowProbe.blockedBrowseDrags++;
        }
        if (g_dragActive)
        {
            UpdateDrag(point);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hoveredFilteredIndex != -1)
        {
            g_hoveredFilteredIndex = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_searchPointerSelecting)
        {
            g_searchPointerSelecting = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            SearchCaretChanged(hwnd);
            return 0;
        }
        if (g_selectionBoxActive)
        {
            CompleteSelectionBox(hwnd);
            return 0;
        }
        if (g_dragActive)
        {
            if (g_dragMode == DragMode::Region)
            {
                CompleteRegionDrag(hwnd);
            }
            else
            {
                CompleteDrag(hwnd);
            }
            return 0;
        }
        if (!IsSpotlightMode() && !g_launchpadState.IsEditing() && g_pressedFilteredIndex >= 0)
        {
            POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int pressed = g_pressedFilteredIndex;
            bool activate = false;
            for (const auto& hit : g_hits)
            {
                if (hit.itemIndex == pressed && PtInRect(&hit.bounds, point))
                {
                    activate = true;
                    break;
                }
            }
            ResetDragState();
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            if (activate)
            {
                if (g_launchpadFlowProbe.active) g_launchpadFlowProbe.browseClickLaunches++;
                LaunchItemAtFilteredIndex(pressed);
            }
            return 0;
        }
        ResetDragState();
        return 0;
    case WM_RBUTTONUP:
    {
        if (IsSpotlightMode()) return 0;
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        for (const auto& hit : g_hits)
        {
            if (PtInRect(&hit.bounds, point))
            {
                ShowItemContextMenu(point, hit.itemIndex);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        for (const auto& regionHit : g_regionHits)
        {
            if (PtInRect(&regionHit.headerBounds, point))
            {
                ShowRegionContextMenu(point, regionHit.regionId);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        ShowWorkspaceContextMenu(point);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
    {
        if (!IsSpotlightMode() && g_launchpadState.IsEditing()) return 0;
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        for (const auto& hit : g_hits)
        {
            if (PtInRect(&hit.bounds, point))
            {
                LaunchItemAtFilteredIndex(hit.itemIndex);
                return 0;
            }
        }
        return 0;
    }
    case WM_KEYDOWN:
    {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (g_searchInput.IsComposing())
        {
            if (wParam == VK_ESCAPE)
            {
                HIMC context = ImmGetContext(hwnd);
                if (context)
                {
                    ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                    ImmReleaseContext(hwnd, context);
                }
                g_searchInput.CancelComposition();
                SearchTextChanged(hwnd, false);
                return 0;
            }
            if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN
                || wParam == VK_HOME || wParam == VK_END || wParam == VK_DELETE || wParam == VK_BACK
                || wParam == VK_RETURN || wParam == VK_TAB)
            {
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
        }
        if (wParam == VK_ESCAPE)
        {
            if (g_selectionBoxActive) CancelSelectionBox(hwnd);
            else if (g_dragActive) CancelDrag(hwnd);
            else if (IsSpotlightMode())
            {
                const auto action = g_spotlightState.Escape(SearchCommittedText().empty());
                if (action == llp::interaction::EscapeAction::FocusSearch)
                {
                    ApplySpotlightInteractionState(false);
                }
                else if (action == llp::interaction::EscapeAction::ClearQuery)
                {
                    g_searchInput.Reset(L"");
                    SearchTextChanged(hwnd, true);
                }
                else
                {
                    HideNativeUi();
                }
            }
            else if (g_launchpadState.IsEditing()) SetLaunchpadEditing(false);
            else HideNativeUi();
            return 0;
        }
        if (!IsSpotlightMode() && control && wParam == L'E')
        {
            SetLaunchpadEditing(!g_launchpadState.IsEditing());
            return 0;
        }
        if (IsSpotlightMode() && wParam == VK_TAB)
        {
            g_spotlightState.Tab(shift, true);
            ApplySpotlightInteractionState(true);
            return 0;
        }
        const bool searchFocused = IsSpotlightMode()
            ? g_spotlightState.Focus() == llp::interaction::FocusArea::Search
            : g_searchInputActive;
        if (searchFocused && control && wParam == L'A')
        {
            g_searchInput.SelectAll();
            SearchCaretChanged(hwnd);
            return 0;
        }
        if (searchFocused && control && wParam == L'C')
        {
            WriteClipboardText(hwnd, g_searchInput.SelectedText());
            return 0;
        }
        if (searchFocused && control && wParam == L'X')
        {
            const std::wstring selected = g_searchInput.SelectedText();
            if (WriteClipboardText(hwnd, selected) && g_searchInput.CutSelection()) SearchTextChanged(hwnd, true);
            return 0;
        }
        if (searchFocused && control && wParam == L'V')
        {
            const std::wstring pasted = ReadClipboardText(hwnd);
            if (!pasted.empty() && g_searchInput.InsertText(pasted)) SearchTextChanged(hwnd, true);
            return 0;
        }
        if (searchFocused && control && wParam == L'Z')
        {
            if ((shift ? g_searchInput.Redo() : g_searchInput.Undo())) SearchTextChanged(hwnd, true);
            return 0;
        }
        if (searchFocused && control && wParam == L'Y')
        {
            if (g_searchInput.Redo()) SearchTextChanged(hwnd, true);
            return 0;
        }
        if (wParam == VK_TAB)
        {
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            if (IsSpotlightMode())
            {
                const auto action = g_spotlightState.Enter();
                if (action == llp::interaction::EnterAction::LaunchFirstResult)
                {
                    if (g_spotlightFlowProbe.active) g_spotlightFlowProbe.searchEnterLaunches++;
                    LaunchItemAtFilteredIndex(0);
                }
                else if (action == llp::interaction::EnterAction::LaunchFocusedResult)
                {
                    if (g_spotlightFlowProbe.active) g_spotlightFlowProbe.resultEnterLaunches++;
                    LaunchItemAtFilteredIndex(g_spotlightState.FocusedResultIndex());
                }
                else if (action == llp::interaction::EnterAction::OpenAuxiliary)
                {
                    HideNativeUi();
                    ShowSettingsWindow();
                }
            }
            else if (g_launchpadState.EnterOnFocusedItem() == llp::interaction::LaunchpadEnterAction::LaunchFocusedItem)
            {
                LaunchItemAtFilteredIndex(g_searchInputActive ? 0 : g_selectedFilteredIndex);
            }
            return 0;
        }
        if (wParam == VK_RIGHT)
        {
            if (IsSpotlightMode() && !searchFocused) MoveSpotlightResult(llp::interaction::Direction::Right);
            else if (!IsSpotlightMode() && !searchFocused) MoveSelectionHorizontal(1);
            else
            {
                g_searchInput.MoveCaret(control ? llp::input::Move::WordRight : llp::input::Move::Right, shift);
                SearchCaretChanged(hwnd);
            }
            return 0;
        }
        if (wParam == VK_LEFT)
        {
            if (IsSpotlightMode() && !searchFocused) MoveSpotlightResult(llp::interaction::Direction::Left);
            else if (!IsSpotlightMode() && !searchFocused) MoveSelectionHorizontal(-1);
            else
            {
                g_searchInput.MoveCaret(control ? llp::input::Move::WordLeft : llp::input::Move::Left, shift);
                SearchCaretChanged(hwnd);
            }
            return 0;
        }
        if (wParam == VK_HOME)
        {
            if (IsSpotlightMode() && !searchFocused && !g_filtered.empty())
            {
                g_spotlightState.FocusResult(0);
                ApplySpotlightInteractionState(true);
            }
            else if (!IsSpotlightMode() && !searchFocused && !g_filtered.empty()) SelectFilteredIndex(0);
            else
            {
                g_searchInput.MoveCaret(llp::input::Move::Home, shift);
                SearchCaretChanged(hwnd);
            }
            return 0;
        }
        if (wParam == VK_END)
        {
            if (IsSpotlightMode() && !searchFocused && !g_filtered.empty())
            {
                g_spotlightState.FocusResult(static_cast<int>(g_filtered.size()) - 1);
                ApplySpotlightInteractionState(true);
            }
            else if (!IsSpotlightMode() && !searchFocused && !g_filtered.empty()) SelectFilteredIndex(static_cast<int>(g_filtered.size()) - 1);
            else
            {
                g_searchInput.MoveCaret(llp::input::Move::End, shift);
                SearchCaretChanged(hwnd);
            }
            return 0;
        }
        if (wParam == VK_DELETE)
        {
            if (searchFocused && g_searchInput.DeleteForward(control)) SearchTextChanged(hwnd, true);
            return 0;
        }
        if (wParam == VK_DOWN)
        {
            if (IsSpotlightMode())
            {
                if (searchFocused || g_spotlightState.FocusedResultIndex() < 0) EnterSpotlightResults();
                else MoveSpotlightResult(llp::interaction::Direction::Down);
            }
            else if (searchFocused) SelectFilteredIndex(0);
            else MoveSelectionVertical(1);
            return 0;
        }
        if (wParam == VK_UP)
        {
            if (IsSpotlightMode()) MoveSpotlightResult(llp::interaction::Direction::Up);
            else MoveSelectionVertical(-1);
            return 0;
        }
        return 0;
    }
    case WM_IME_STARTCOMPOSITION:
        ActivateSearchInput();
        g_searchInput.BeginComposition();
        UpdateImeWindowPosition(hwnd);
        RequestSearchRepaint(hwnd, false);
        return 0;
    case WM_IME_COMPOSITION:
        UpdateSearchComposition(hwnd, lParam);
        UpdateImeWindowPosition(hwnd);
        return 0;
    case WM_IME_ENDCOMPOSITION:
        if (g_searchInput.IsComposing())
        {
            g_searchInput.CancelComposition();
            SearchTextChanged(hwnd, false);
        }
        return 0;
    case WM_IME_CHAR:
        ActivateSearchInput();
        if (wParam >= 32 && g_searchInput.HandleLegacyImeCharacter(static_cast<wchar_t>(wParam)))
        {
            SearchTextChanged(hwnd, true);
        }
        return 0;
    case WM_CHAR:
    {
        if (wParam == VK_TAB || wParam == VK_RETURN)
        {
            return 0;
        }
        ActivateSearchInput();
        if (g_searchInput.IsComposing()) return 0;
        bool changed = false;
        if (wParam == VK_BACK)
        {
            changed = g_searchInput.Backspace((GetKeyState(VK_CONTROL) & 0x8000) != 0);
        }
        else if (wParam >= 32)
        {
            changed = g_searchInput.InsertText(std::wstring(1, static_cast<wchar_t>(wParam)));
        }
        if (g_catalogBrokerProbe.active
            && g_catalogBrokerProbe.stage == 1
            && g_catalogBrokerProbe.inputResponseMilliseconds == 0)
        {
            g_catalogBrokerProbe.inputResponseMilliseconds = GetTickCount64() - g_catalogBrokerProbe.inputPostedTick;
            g_catalogBrokerProbe.cachedItemCountDuringRefresh = g_items.size();
        }
        if (changed) SearchTextChanged(hwnd, true);
        return 0;
    }
    case WM_SIZE:
        ClampScroll();
        g_spotlightLayerDirty = true;
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && IsSpotlightMode() && IsWindowVisible(hwnd) && !g_settingsHwnd && g_modalDialogDepth == 0)
        {
            HideNativeUi();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != hwnd) g_searchPointerSelecting = false;
        if (reinterpret_cast<HWND>(lParam) != hwnd && g_selectionBoxActive)
        {
            CancelSelectionBox(hwnd);
        }
        if (reinterpret_cast<HWND>(lParam) != hwnd && g_dragActive)
        {
            KillTimer(hwnd, DragAnimationTimerId);
            ResetDragState();
        }
        return 0;
    case WM_DESTROY:
    {
        KillTimer(hwnd, DragAnimationTimerId);
        if (!g_textInputProbePath.empty()) WriteFileUtf8(g_textInputProbePath, BuildTextInputProbeJson());
        if (g_spotlightFlowProbe.active) WriteFileUtf8(g_spotlightFlowProbe.outputPath, BuildSpotlightFlowProbeJson());
        if (g_launchpadFlowProbe.active) WriteFileUtf8(g_launchpadFlowProbe.outputPath, BuildLaunchpadFlowProbeJson());
        g_searchAccessibility.Shutdown();
        g_shellBroker.Stop();
        MSG pendingBrokerResult = {};
        while (PeekMessageW(&pendingBrokerResult, hwnd, WmShellBrokerResult, WmShellBrokerResult, PM_REMOVE))
        {
            auto* pending = reinterpret_cast<llp::shell::Result*>(pendingBrokerResult.lParam);
            if (pending)
            {
                if (pending->icon) DestroyIcon(pending->icon);
                delete pending;
            }
        }
        RemoveTrayIcon();
        UnregisterHotKey(hwnd, g_activeHotkeyId);
        ReleaseSpotlightLayerBuffer();
        ReleaseSpotlightBackdrop();
        DestroyDirectRenderer();
        if (g_uiFont)
        {
            DeleteObject(g_uiFont);
            g_uiFont = nullptr;
        }
        ReleaseAllResidentIcons();
        PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    g_processStartTick = GetTickCount64();
    g_instance = instance;
    g_startupLogPath = CommandLineValue(L"--diagnostics-log-file");
    if (g_startupLogPath.empty()) g_startupLogPath = DefaultStartupLogPath();
    AppendStartupLog(L"process-start", true, BuildId());
    const auto diagnosticsFile = DiagnosticsFileFromCommandLine();
    if (!diagnosticsFile.empty())
    {
        return WriteFileUtf8(diagnosticsFile, BuildDiagnosticsJson()) ? 0 : 2;
    }
    const auto persistenceProbeFile = CommandLineValue(L"--persistence-probe-file");
    if (!persistenceProbeFile.empty())
    {
        return RunPersistenceProbe(persistenceProbeFile, CommandLineHasArgument(L"--persistence-resave"));
    }
    g_catalogBrokerProbe.outputPath = CommandLineValue(L"--catalog-broker-probe-file");
    g_catalogBrokerProbe.active = !g_catalogBrokerProbe.outputPath.empty();
    g_catalogBrokerProbe.delayMilliseconds = static_cast<DWORD>(CommandLineIntValue(L"--catalog-broker-delay-ms", 1200, 0, 10000));
    g_shellImportProbe.outputPath = CommandLineValue(L"--shell-import-probe-file");
    g_shellImportProbe.active = !g_shellImportProbe.outputPath.empty();
    g_shellImportProbe.root = CommandLineValue(L"--shell-import-root");
    g_shellImportProbe.destination = CommandLineValue(L"--shell-import-destination");
    g_shellImportProbe.repeat = CommandLineIntValue(L"--shell-import-repeat", 2, 2, 2);
    g_textInputProbePath = CommandLineValue(L"--text-input-probe-file");
    const bool textInputProbeActive = !g_textInputProbePath.empty();
    g_spotlightFlowProbe.outputPath = CommandLineValue(L"--spotlight-flow-probe-file");
    g_spotlightFlowProbe.readyPath = CommandLineValue(L"--spotlight-flow-ready-file");
    g_spotlightFlowProbe.active = !g_spotlightFlowProbe.outputPath.empty();
    g_launchpadFlowProbe.outputPath = CommandLineValue(L"--launchpad-flow-probe-file");
    g_launchpadFlowProbe.readyPath = CommandLineValue(L"--launchpad-flow-ready-file");
    g_launchpadFlowProbe.active = !g_launchpadFlowProbe.outputPath.empty();
    const int activeBehaviorProbes = (g_catalogBrokerProbe.active ? 1 : 0)
        + (g_shellImportProbe.active ? 1 : 0)
        + (textInputProbeActive ? 1 : 0)
        + (g_spotlightFlowProbe.active ? 1 : 0)
        + (g_launchpadFlowProbe.active ? 1 : 0);
    if (activeBehaviorProbes > 1) return 2;
    if (g_shellImportProbe.active && (g_shellImportProbe.root.empty() || g_shellImportProbe.destination.empty())) return 2;
    if (g_spotlightFlowProbe.active && g_spotlightFlowProbe.readyPath.empty()) return 2;
    if (g_launchpadFlowProbe.active && g_launchpadFlowProbe.readyPath.empty()) return 2;
    g_diagnosticsUiMode = CommandLineHasArgument(L"--diagnostics-ui");
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    const HRESULT coInitializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(coInitializeResult);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, LLP_SINGLETON_NAME_WIDE);
    if (!mutex)
    {
        AppendStartupLog(L"singleton-mutex", false, L"CreateMutex failed: " + std::to_wstring(GetLastError()));
        const auto reportPath = CommandLineValue(L"--activation-report-file");
        WriteActivationReport(reportPath, L"mutex-failed", GetTickCount64() - g_processStartTick);
        if (reportPath.empty())
        {
            MessageBoxW(nullptr, L"LightLaunchpad could not establish its single-instance channel.", L"LightLaunchpad startup failed", MB_ICONERROR | MB_OK);
        }
        if (comInitialized) CoUninitialize();
        return 4;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        AppendStartupLog(L"singleton-secondary", true, L"activating existing instance");
        const auto reportPath = CommandLineValue(L"--activation-report-file");
        const auto activationResult = ActivatePrimaryInstance(reportPath);
        CloseHandle(mutex);
        if (comInitialized) CoUninitialize();
        if (activationResult == SecondaryActivationResult::VersionOrChannelMismatch)
        {
            if (reportPath.empty())
            {
                MessageBoxW(nullptr, L"The running LightLaunchpad instance was activated, but this executable has a different version or channel.", L"LightLaunchpad build mismatch", MB_ICONWARNING | MB_OK);
            }
            return 3;
        }
        if (activationResult == SecondaryActivationResult::Failed)
        {
            if (reportPath.empty())
            {
                MessageBoxW(nullptr, L"Another LightLaunchpad process is running, but it could not be activated. Check Diagnostics for the running executable and version.", L"LightLaunchpad activation failed", MB_ICONERROR | MB_OK);
            }
            return 4;
        }
        return 0;
    }
    AppendStartupLog(L"singleton-primary", true, LLP_SINGLETON_NAME_WIDE);

    LoadSettings();
    g_settingsLoadedTick = GetTickCount64();
    AppendStartupLog(L"settings-loaded", true, g_settings.launchpadFolder);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = WndProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadEmbeddedAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    wc.hIconSm = LoadEmbeddedAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wc.lpszClassName = WindowClassName;
    wc.style = CS_DBLCLKS;
    if (!RegisterClassExW(&wc))
    {
        AppendStartupLog(L"window-class", false, L"RegisterClassEx failed: " + std::to_wstring(GetLastError()));
        CloseHandle(mutex);
        if (comInitialized) CoUninitialize();
        return 1;
    }

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WindowClassName,
        L"LightLaunchpad Native UI",
        WS_POPUP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        980,
        720,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!g_hwnd)
    {
        AppendStartupLog(L"window-created", false, L"CreateWindowEx failed: " + std::to_wstring(GetLastError()));
        CloseHandle(mutex);
        if (comInitialized) CoUninitialize();
        return 1;
    }
    AppendStartupLog(L"window-created", true, LLP_WINDOW_CLASS_NAME_WIDE);
    llp::accessibility::Callbacks accessibilityCallbacks;
    accessibilityCallbacks.focus = AccessibilityFocus;
    accessibilityCallbacks.hasFocus = AccessibilitySearchHasFocus;
    accessibilityCallbacks.textChanged = AccessibilityTextChanged;
    accessibilityCallbacks.selectionChanged = AccessibilitySelectionChanged;
    g_searchAccessibilityReady = g_searchAccessibility.Initialize(g_hwnd, &g_searchInput, accessibilityCallbacks);
    AppendStartupLog(L"search-accessibility", g_searchAccessibilityReady, g_searchAccessibilityReady ? L"UIA Value/Text providers ready" : L"provider initialization failed");

    const bool shellBrokerStarted = g_shellBroker.Start(g_hwnd, WmShellBrokerResult);
    AppendStartupLog(L"shell-broker", shellBrokerStarted, shellBrokerStarted ? L"dedicated STA queued" : L"thread creation failed");

    if (!g_diagnosticsUiMode && !g_catalogBrokerProbe.active && !g_shellImportProbe.active && !textInputProbeActive && !g_spotlightFlowProbe.active && !g_launchpadFlowProbe.active)
    {
        RegisterCurrentHotkey();
        AppendStartupLog(L"hotkey-registered", g_hotkeyRegistered, g_settings.hotkey);
        AddTrayIcon();
    }
    else if (g_diagnosticsUiMode)
    {
        ShowSettingsWindow();
    }

    g_shellReadyTick = GetTickCount64();
    AppendStartupLog(L"shell-ready", true, std::to_wstring(g_shellReadyTick - g_processStartTick) + L" ms");

    g_startupProbePath = CommandLineValue(L"--startup-probe-file");
    if (CommandLineHasArgument(L"--simulate-taskbar-created") && g_taskbarCreatedMessage != 0)
    {
        SendMessageW(g_hwnd, g_taskbarCreatedMessage, 0, 0);
    }

    bool startupProbeWritten = true;
    if (!g_startupProbePath.empty())
    {
        startupProbeWritten = WriteFileUtf8(g_startupProbePath, BuildStartupProbeJson());
        AppendStartupLog(L"startup-probe", startupProbeWritten, g_startupProbePath);
    }

    if (CommandLineHasArgument(L"--exit-after-startup-probe"))
    {
        DestroyWindow(g_hwnd);
        CloseHandle(mutex);
        if (comInitialized) CoUninitialize();
        return startupProbeWritten ? 0 : 2;
    }

    if (shellBrokerStarted)
    {
        if (g_catalogBrokerProbe.active)
        {
            RequestCatalogRefresh();
        }
        else if (g_shellImportProbe.active)
        {
            g_shellBroker.RequestImportTree(g_shellImportProbe.root, g_shellImportProbe.destination);
        }
        else if (!g_diagnosticsUiMode)
        {
            RequestCatalogRefresh();
        }
    }
    else if (g_catalogBrokerProbe.active || g_shellImportProbe.active || g_spotlightFlowProbe.active || g_launchpadFlowProbe.active)
    {
        DestroyWindow(g_hwnd);
        CloseHandle(mutex);
        if (comInitialized) CoUninitialize();
        return 2;
    }

    if (textInputProbeActive)
    {
        ShowNativeUi();
        g_textInputProbeWindowWasVisible = g_hwnd && IsWindowVisible(g_hwnd) != FALSE;
    }

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(mutex);
    if (comInitialized) CoUninitialize();
    return static_cast<int>(msg.wParam);
}
