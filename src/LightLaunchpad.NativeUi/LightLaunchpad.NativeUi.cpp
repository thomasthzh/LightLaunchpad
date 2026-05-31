#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <imm.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <cwchar>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef SHIL_EXTRALARGE
#define SHIL_EXTRALARGE 0x2
#endif

#ifndef SHIL_JUMBO
#define SHIL_JUMBO 0x4
#endif

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
constexpr int HotkeyId = 0x4C55;
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
constexpr UINT WmTray = WM_APP + 72;
constexpr wchar_t WindowClassName[] = L"LightLaunchpadNativeUiWindow";
constexpr wchar_t SettingsWindowClassName[] = L"LightLaunchpadNativeSettingsWindow";
constexpr wchar_t TextInputWindowClassName[] = L"LightLaunchpadNativeTextInputWindow";
constexpr wchar_t AppIconResourceName[] = L"IDI_APP";
constexpr int SpotlightCornerRadius = 34;
constexpr int SpotlightEdgeLayers = 10;
constexpr int SpotlightPanelInset = 0;
constexpr int SpotlightContentInset = 68;
constexpr int SpotlightContentBottomInset = 34;
constexpr int SpotlightContentFeatherSize = 44;
constexpr int SpotlightSearchSideInset = 68;
constexpr int SpotlightSearchCornerRadius = 24;
constexpr COLORREF SpotlightTransparentKey = RGB(1, 2, 3);
constexpr BYTE SpotlightGlassAlpha = 218;
constexpr int SpotlightAnimationSteps = 9;
constexpr int SpotlightAnimationOffset = 18;
constexpr int SpotlightCloseAnimationOffset = 12;
constexpr size_t MaxResidentIconCount = 96;
constexpr UINT_PTR DragAnimationTimerId = 0x4C5601;
constexpr int DragTargetSettleMs = 55;
constexpr double DragReflowSmoothing = 0.24;
constexpr UINT_PTR HotkeyEditSubclassId = 0x4C5602;
constexpr UINT_PTR TrayIconRetryTimerId = 0x4C5603;
constexpr UINT_PTR TrayIconPromotionRetryTimerId = 0x4C5604;
constexpr UINT TrayIconRetryMs = 1000;
constexpr UINT TrayIconPromotionRetryMs = 1000;
constexpr int TrayIconPromotionMaxAttempts = 8;
const GUID NativeIID_IImageList = { 0x46eb5926, 0x582e, 0x4017, { 0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50 } };

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
    FolderLabel = 3020,
    HotkeyLabel = 3021,
    IconSizeLabel = 3022,
    DisplayModeLabel = 3023,
    LanguageLabel = 3024,
    SpotlightSizeLabel = 3025,
    AppSpacingLabel = 3026,
    WheelSensitivityLabel = 3027
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
    std::wstring displayNameLower;
    std::wstring searchCandidateText;
    std::wstring searchCandidateLower;
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

HINSTANCE g_instance = nullptr;
HWND g_hwnd = nullptr;
HWND g_settingsHwnd = nullptr;
Settings g_settings;
std::vector<Region> g_regions;
std::vector<LaunchItem> g_items;
std::vector<int> g_filtered;
std::vector<HitTile> g_hits;
std::vector<IconOpaqueBoundsCacheEntry> g_iconBoundsCache;
std::vector<RegionHit> g_regionHits;
std::vector<std::wstring> g_selectedSourcePaths;
std::vector<std::wstring> g_selectedRegionIds;
std::wstring g_searchText;
std::wstring g_searchCompositionText;
std::wstring g_completionBaseText;
std::wstring g_completionCandidateText;
bool g_searchInputActive = false;
int g_selectedFilteredIndex = -1;
int g_scrollOffset = 0;
int g_contentHeight = 0;
std::wstring g_layoutViewMode = L"InlineRegions";
bool g_mouseDown = false;
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
HFONT g_uiFont = nullptr;
bool g_spotlightLayerBasePass = false;
BYTE g_spotlightPaintOpacity = 255;
bool g_spotlightLayerDirty = true;
SpotlightLayerBuffer g_spotlightLayerBuffer;
UINT g_taskbarCreatedMessage = 0;
bool g_trayIconAdded = false;
int g_trayPromotionAttempts = 0;

bool ShowTextInputDialog(const std::wstring& title, const std::wstring& label, const std::wstring& initialValue, std::wstring& result);
void RegisterCurrentHotkey();
void ShowSettingsWindow();
void RebuildFiltered();
void DestroyDirectRenderer();
void RemoveTrayIcon();
bool IsSpotlightBaseLayerPass();
void ActivateSearchInput();
bool InitializeDirectRenderer();
bool IsSpotlightMode();
bool PaintSpotlightLayeredWindow(bool repaint = true);
void ReleaseSpotlightLayerBuffer();
std::wstring BuildPinyinSearchText(const std::wstring& text);
void UpdateSearchIndex(LaunchItem& item);
void RebuildSearchIndex();
void DrawSearchSurface(HDC dc, const RECT& client);
void DrawSearchSurface(ID2D1DCRenderTarget* target, const RECT& client);
int CalculateColumnCount(const RECT& client);
std::wstring NormalizeHotkeyText(const std::wstring& value);
void RequestSearchRepaint(HWND hwnd, bool erase);

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
    return { sideInset, SearchTop(), client.right - sideInset, SearchBottom() };
}

RECT SearchTextRect(const RECT& searchRect)
{
    return { searchRect.left + 22, searchRect.top, searchRect.right - 22, searchRect.bottom };
}

std::wstring SearchCommittedText()
{
    return g_searchText;
}

std::wstring SearchDisplayText()
{
    return SearchCommittedText() + g_searchCompositionText;
}

bool IsSearchCompletionActive()
{
    return !g_completionCandidateText.empty() && g_searchCompositionText.empty();
}

void ClearSearchCompletion()
{
    g_completionBaseText.clear();
    g_completionCandidateText.clear();
}

std::wstring SearchCompletionTail()
{
    if (!IsSearchCompletionActive()) return L"";
    if (StartsWithIgnoreCase(g_completionCandidateText, g_searchText))
    {
        return g_completionCandidateText.substr(g_searchText.size());
    }

    return g_searchText.empty()
        ? g_completionCandidateText
        : L"  " + g_completionCandidateText;
}

void CommitSearchCompletion()
{
    if (!IsSearchCompletionActive()) return;
    g_searchText = g_completionCandidateText;
    ClearSearchCompletion();
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

bool IsLaunchablePath(const std::wstring& path)
{
    return EndsWithIgnoreCase(path, L".lnk")
        || EndsWithIgnoreCase(path, L".url")
        || EndsWithIgnoreCase(path, L".exe")
        || EndsWithIgnoreCase(path, L".com");
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

std::string ReadFileUtf8(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 8 * 1024 * 1024)
    {
        CloseHandle(file);
        return {};
    }

    std::string content(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &read, nullptr);
    CloseHandle(file);
    content.resize(read);
    return content;
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

int HexValue(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return 0;
}

std::wstring ParseJsonStringAt(const std::string& text, size_t quote)
{
    if (quote == std::string::npos || quote >= text.size() || text[quote] != '"') return {};

    std::wstring result;
    std::string utf8;
    for (size_t i = quote + 1; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (ch == '"')
        {
            if (!utf8.empty())
            {
                result += Utf8ToWide(utf8);
            }
            return result;
        }

        if (ch != '\\')
        {
            utf8.push_back(ch);
            continue;
        }

        if (!utf8.empty())
        {
            result += Utf8ToWide(utf8);
            utf8.clear();
        }
        if (++i >= text.size()) break;
        const char escaped = text[i];
        switch (escaped)
        {
        case '"': result += L'"'; break;
        case '\\': result += L'\\'; break;
        case '/': result += L'/'; break;
        case 'b': result += L'\b'; break;
        case 'f': result += L'\f'; break;
        case 'n': result += L'\n'; break;
        case 'r': result += L'\r'; break;
        case 't': result += L'\t'; break;
        case 'u':
            if (i + 4 < text.size())
            {
                wchar_t code = static_cast<wchar_t>(
                    (HexValue(text[i + 1]) << 12)
                    | (HexValue(text[i + 2]) << 8)
                    | (HexValue(text[i + 3]) << 4)
                    | HexValue(text[i + 4]));
                result += code;
                i += 4;
            }
            break;
        default:
            result += static_cast<wchar_t>(escaped);
            break;
        }
    }

    if (!utf8.empty())
    {
        result += Utf8ToWide(utf8);
    }
    return result;
}

size_t FindValueStart(const std::string& text, const char* key, size_t begin, size_t end)
{
    std::string pattern = "\"";
    pattern += key;
    pattern += "\"";
    const auto found = text.find(pattern, begin);
    if (found == std::string::npos || found >= end) return std::string::npos;
    const auto colon = text.find(':', found + pattern.size());
    if (colon == std::string::npos || colon >= end) return std::string::npos;
    return colon + 1;
}

std::wstring FindJsonStringValue(const std::string& text, const char* key, size_t begin = 0, size_t end = std::string::npos)
{
    if (end == std::string::npos) end = text.size();
    const auto valueStart = FindValueStart(text, key, begin, end);
    if (valueStart == std::string::npos) return {};
    const auto quote = text.find('"', valueStart);
    if (quote == std::string::npos || quote >= end) return {};
    return ParseJsonStringAt(text, quote);
}

int FindJsonNumberValue(const std::string& text, const char* key, int fallback, size_t begin = 0, size_t end = std::string::npos)
{
    if (end == std::string::npos) end = text.size();
    const auto valueStart = FindValueStart(text, key, begin, end);
    if (valueStart == std::string::npos) return fallback;
    int value = fallback;
    if (std::sscanf(text.c_str() + valueStart, "%d", &value) == 1)
    {
        return value;
    }
    return fallback;
}

double FindJsonDoubleValue(const std::string& text, const char* key, double fallback, size_t begin = 0, size_t end = std::string::npos)
{
    if (end == std::string::npos) end = text.size();
    const auto valueStart = FindValueStart(text, key, begin, end);
    if (valueStart == std::string::npos) return fallback;
    char* parseEnd = nullptr;
    const double value = std::strtod(text.c_str() + valueStart, &parseEnd);
    return parseEnd && parseEnd != text.c_str() + valueStart ? value : fallback;
}

bool FindJsonBoolValue(const std::string& text, const char* key, bool fallback, size_t begin = 0, size_t end = std::string::npos)
{
    if (end == std::string::npos) end = text.size();
    const auto valueStart = FindValueStart(text, key, begin, end);
    if (valueStart == std::string::npos) return fallback;
    if (text.compare(valueStart, 4, "true") == 0) return true;
    if (text.compare(valueStart, 5, "false") == 0) return false;
    return fallback;
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

std::wstring GetLightLaunchpadDataPath()
{
    const auto appData = GetAppDataPath();
    return appData.empty() ? L"" : JoinPath(appData, L"LightLaunchpad");
}

std::wstring GetLayoutPath()
{
    const auto dataPath = GetLightLaunchpadDataPath();
    return dataPath.empty() ? L"" : JoinPath(dataPath, L"layout.json");
}

std::wstring GetSettingsPath()
{
    const auto dataPath = GetLightLaunchpadDataPath();
    return dataPath.empty() ? L"" : JoinPath(dataPath, L"settings.json");
}

bool WriteFileUtf8(const std::wstring& path, const std::string& content)
{
    if (path.empty()) return false;
    wchar_t directory[MAX_PATH] = L"";
    CopyText(directory, ARRAYSIZE(directory), path.c_str());
    PathRemoveFileSpecW(directory);
    if (directory[0] != L'\0') CreateDirectoryW(directory, nullptr);

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

    return MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

void LoadSettings()
{
    const auto appData = GetAppDataPath();
    const auto profile = GetUserProfilePath();
    g_settings.launchpadFolder = profile.empty() ? L"Launchpad" : JoinPath(profile, L"Launchpad");
    if (appData.empty()) return;

    const auto json = ReadFileUtf8(JoinPath(JoinPath(appData, L"LightLaunchpad"), L"settings.json"));
    if (json.empty()) return;

    const auto folder = FindJsonStringValue(json, "LaunchpadFolder");
    if (!folder.empty()) g_settings.launchpadFolder = folder;
    const auto hotkey = FindJsonStringValue(json, "Hotkey");
    if (!hotkey.empty()) g_settings.hotkey = NormalizeHotkeyText(hotkey);
    const auto displayMode = FindJsonStringValue(json, "DisplayMode");
    if (!displayMode.empty()) g_settings.displayMode = displayMode;
    const auto language = FindJsonStringValue(json, "Language");
    if (!language.empty()) g_settings.language = language;
    const auto iconSize = FindJsonStringValue(json, "IconSize");
    if (ContainsIgnoreCase(iconSize, L"Large")) g_settings.iconSize = 72;
    if (ContainsIgnoreCase(iconSize, L"Small")) g_settings.iconSize = 42;
    g_settings.spotlightWidth = NormalizeSpotlightWidth(FindJsonNumberValue(json, "SpotlightWidth", g_settings.spotlightWidth));
    g_settings.spotlightHeight = NormalizeSpotlightHeight(FindJsonNumberValue(json, "SpotlightHeight", g_settings.spotlightHeight));
    g_settings.appSpacing = NormalizeAppSpacing(FindJsonNumberValue(json, "AppSpacing", g_settings.appSpacing));
    g_settings.wheelSensitivity = NormalizeWheelSensitivity(FindJsonDoubleValue(json, "WheelSensitivity", g_settings.wheelSensitivity));
    g_settings.startWithWindows = FindJsonBoolValue(json, "StartWithWindows", g_settings.startWithWindows);
    g_settings.resetSearchOnOpen = FindJsonBoolValue(json, "ResetSearchOnOpen", g_settings.resetSearchOnOpen);
}

std::wstring IconSizeName()
{
    if (g_settings.iconSize <= 44) return L"Small";
    if (g_settings.iconSize >= 64) return L"Large";
    return L"Medium";
}

bool SaveSettings()
{
    std::string json;
    json += "{\r\n";
    json += "  \"LaunchpadFolder\": \"" + EscapeJson(g_settings.launchpadFolder) + "\",\r\n";
    json += "  \"Hotkey\": \"" + EscapeJson(g_settings.hotkey) + "\",\r\n";
    json += std::string("  \"StartWithWindows\": ") + (g_settings.startWithWindows ? "true" : "false") + ",\r\n";
    json += "  \"IconSize\": \"" + EscapeJson(IconSizeName()) + "\",\r\n";
    json += "  \"ViewMode\": \"" + EscapeJson(g_layoutViewMode) + "\",\r\n";
    json += "  \"IconQuality\": \"High\",\r\n";
    json += "  \"DisplayMode\": \"" + EscapeJson(g_settings.displayMode) + "\",\r\n";
    json += "  \"Language\": \"" + EscapeJson(g_settings.language) + "\",\r\n";
    json += "  \"SpotlightWidth\": " + std::to_string(g_settings.spotlightWidth) + ",\r\n";
    json += "  \"SpotlightHeight\": " + std::to_string(g_settings.spotlightHeight) + ",\r\n";
    json += "  \"AppSpacing\": " + std::to_string(g_settings.appSpacing) + ",\r\n";
    json += "  \"WheelSensitivity\": " + WideToUtf8(FormatDouble(g_settings.wheelSensitivity)) + ",\r\n";
    json += std::string("  \"ResetSearchOnOpen\": ") + (g_settings.resetSearchOnOpen ? "true" : "false") + "\r\n";
    json += "}\r\n";
    return WriteFileUtf8(GetSettingsPath(), json);
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
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool ContainsPath(const std::vector<std::wstring>& paths, const std::wstring& sourcePath)
{
    return std::any_of(paths.begin(), paths.end(), [&](const std::wstring& path) {
        return SamePath(path, sourcePath);
    });
}

void SetSingleSelection(const std::wstring& sourcePath)
{
    g_selectedSourcePaths.clear();
    if (!sourcePath.empty())
    {
        g_selectedSourcePaths.push_back(sourcePath);
    }
}

void ToggleSelection(const std::wstring& sourcePath)
{
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
    g_selectedRegionIds.clear();
    if (!regionId.empty())
    {
        g_selectedRegionIds.push_back(regionId);
    }
}

void ToggleRegionSelection(const std::wstring& regionId)
{
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

void LoadLayout()
{
    g_regions.clear();
    g_items.clear();
    const auto appData = GetAppDataPath();
    if (appData.empty())
    {
        AddDefaultRegion();
        return;
    }

    const auto layoutPath = GetLayoutPath();
    const auto json = ReadFileUtf8(layoutPath);
    if (json.empty())
    {
        AddDefaultRegion();
        return;
    }

    const auto viewMode = FindJsonStringValue(json, "ViewMode");
    if (!viewMode.empty()) g_layoutViewMode = viewMode;

    size_t pos = 0;
    while ((pos = json.find("\"Id\"", pos)) != std::string::npos)
    {
        const auto objectEnd = json.find('}', pos);
        if (objectEnd == std::string::npos) break;
        auto id = FindJsonStringValue(json, "Id", pos, objectEnd);
        auto name = FindJsonStringValue(json, "Name", pos, objectEnd);
        int order = FindJsonNumberValue(json, "Order", static_cast<int>(g_regions.size()), pos, objectEnd);
        if (!id.empty() && !name.empty())
        {
            g_regions.push_back({ id, name, order });
        }
        pos = objectEnd + 1;
    }
    AddDefaultRegion();

    pos = 0;
    while ((pos = json.find("\"SourcePath\"", pos)) != std::string::npos)
    {
        const auto objectEnd = json.find('}', pos);
        if (objectEnd == std::string::npos) break;
        auto sourcePath = FindJsonStringValue(json, "SourcePath", pos, objectEnd);
        auto displayName = FindJsonStringValue(json, "DisplayName", pos, objectEnd);
        auto regionId = FindJsonStringValue(json, "RegionId", pos, objectEnd);
        int order = FindJsonNumberValue(json, "Order", static_cast<int>(g_items.size()), pos, objectEnd);

        if (!sourcePath.empty() && GetFileAttributesW(sourcePath.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            if (displayName.empty()) displayName = GetFileStem(sourcePath);
            if (regionId.empty() || !RegionExists(regionId)) regionId = L"uncategorized";
            g_items.push_back({ sourcePath, displayName, regionId, order, nullptr });
        }
        pos = objectEnd + 1;
    }
}

void AddMissingLaunchpadFolderItems()
{
    CreateDirectoryW(g_settings.launchpadFolder.c_str(), nullptr);
    std::wstring search = JoinPath(g_settings.launchpadFolder, L"*");
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(search.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;

    do
    {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring path = JoinPath(g_settings.launchpadFolder, data.cFileName);
        if (!IsLaunchablePath(path)) continue;
        const bool exists = std::any_of(g_items.begin(), g_items.end(), [&](const LaunchItem& item) {
            return SamePath(item.sourcePath, path);
        });
        if (!exists)
        {
            g_items.push_back({ path, GetFileStem(path), L"uncategorized", static_cast<int>(g_items.size()), nullptr });
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
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
    std::string json;
    json += "{\r\n";
    json += "  \"ViewMode\": \"" + EscapeJson(g_layoutViewMode) + "\",\r\n";
    json += "  \"Regions\": [\r\n";
    const auto regions = OrderedRegions();
    for (size_t i = 0; i < regions.size(); ++i)
    {
        const auto& region = regions[i];
        json += "    {\r\n";
        json += "      \"Id\": \"" + EscapeJson(region.id) + "\",\r\n";
        json += "      \"Name\": \"" + EscapeJson(region.name) + "\",\r\n";
        json += "      \"Order\": " + std::to_string(region.order) + "\r\n";
        json += "    }";
        json += i + 1 < regions.size() ? ",\r\n" : "\r\n";
    }
    json += "  ],\r\n";
    json += "  \"Items\": [\r\n";
    for (size_t i = 0; i < g_items.size(); ++i)
    {
        const auto& item = g_items[i];
        json += "    {\r\n";
        json += "      \"SourcePath\": \"" + EscapeJson(item.sourcePath) + "\",\r\n";
        json += "      \"DisplayName\": \"" + EscapeJson(item.displayName) + "\",\r\n";
        json += "      \"RegionId\": \"" + EscapeJson(item.regionId) + "\",\r\n";
        json += "      \"Order\": " + std::to_string(item.order) + "\r\n";
        json += "    }";
        json += i + 1 < g_items.size() ? ",\r\n" : "\r\n";
    }
    json += "  ]\r\n";
    json += "}\r\n";
    return WriteFileUtf8(GetLayoutPath(), json);
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

std::wstring FullPath(const std::wstring& path)
{
    if (path.empty()) return L"";
    const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (required == 0) return path;

    std::vector<wchar_t> buffer(static_cast<size_t>(required) + 1, L'\0');
    const DWORD written = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (written == 0 || written >= buffer.size()) return path;

    std::wstring fullPath(buffer.data(), written);
    while (fullPath.size() > 3 && (fullPath.back() == L'\\' || fullPath.back() == L'/'))
    {
        fullPath.pop_back();
    }
    return fullPath;
}

bool IsPathInsideLaunchpadFolder(const std::wstring& sourcePath)
{
    std::wstring root = FullPath(g_settings.launchpadFolder);
    const std::wstring fullPath = FullPath(sourcePath);
    if (root.empty() || fullPath.empty()) return false;
    if (root.back() != L'\\' && root.back() != L'/')
    {
        root.push_back(L'\\');
    }
    if (fullPath.size() <= root.size()) return false;
    return CompareStringOrdinal(fullPath.c_str(), static_cast<int>(root.size()), root.c_str(), static_cast<int>(root.size()), TRUE) == CSTR_EQUAL;
}

bool DeleteLaunchpadItemFile(const std::wstring& sourcePath)
{
    if (!IsPathInsideLaunchpadFolder(sourcePath)) return true;
    const DWORD attributes = GetFileAttributesW(sourcePath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return true;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    return DeleteFileW(sourcePath.c_str()) != FALSE;
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

int DeleteItemsFromLaunchpad(const std::vector<std::wstring>& sourcePaths)
{
    int failed = 0;
    for (const auto& sourcePath : sourcePaths)
    {
        if (!DeleteLaunchpadItemFile(sourcePath))
        {
            failed++;
        }
    }
    RemoveItemsFromLayout(sourcePaths);
    return failed;
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
    UpdateSearchIndex(*found);
    ClearSearchCompletion();
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

std::vector<HitTile> BuildContentHitTiles()
{
    std::vector<HitTile> hits;
    if (!g_hwnd) return hits;

    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int tileSize = TileSize();
    const int gap = TileGap();
    const int left = 44;
    const int columns = CalculateColumnCount(client);
    int column = 0;
    int x = left;
    int y = 114;
    std::wstring activeRegion;
    const bool groupByRegion = g_searchText.empty();

    hits.reserve(g_filtered.size());
    for (int filteredIndex = 0; filteredIndex < static_cast<int>(g_filtered.size()); ++filteredIndex)
    {
        const auto& item = g_items[g_filtered[filteredIndex]];
        if (groupByRegion && activeRegion != item.regionId)
        {
            if (column != 0)
            {
                y += tileSize + gap;
                column = 0;
                x = left;
            }
            activeRegion = item.regionId;
            y += 38;
        }

        hits.push_back({ { x, y, x + tileSize, y + tileSize }, filteredIndex });
        x += tileSize + gap;
        column++;
        if (column >= columns)
        {
            column = 0;
            x = left;
            y += tileSize + gap;
        }
    }

    return hits;
}

void BeginSelectionBox(HWND hwnd, POINT point, bool additive)
{
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

void ReloadData()
{
    const int previousIconSize = g_settings.iconSize;
    std::vector<LaunchItem> previousItems = std::move(g_items);
    LoadSettings();
    LoadLayout();
    AddMissingLaunchpadFolderItems();
    SortItems();
    RebuildSearchIndex();

    if (previousIconSize == g_settings.iconSize)
    {
        for (auto& item : g_items)
        {
            auto found = std::find_if(previousItems.begin(), previousItems.end(), [&](const LaunchItem& previous) {
                return previous.icon && SamePath(previous.sourcePath, item.sourcePath);
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

    for (auto& item : previousItems)
    {
        ReleaseItemIcon(item);
    }

    if (previousIconSize != g_settings.iconSize)
    {
        ReleaseAllResidentIcons();
    }
    else
    {
        g_iconBoundsCache.erase(
            std::remove_if(g_iconBoundsCache.begin(), g_iconBoundsCache.end(), [](const IconOpaqueBoundsCacheEntry& entry) {
                return !entry.icon || std::none_of(g_items.begin(), g_items.end(), [&](const LaunchItem& item) {
                    return item.icon == entry.icon;
                });
            }),
            g_iconBoundsCache.end());
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
}

bool IsCjkCharacter(wchar_t ch)
{
    return (ch >= 0x4E00 && ch <= 0x9FFF)
        || (ch >= 0x3400 && ch <= 0x4DBF);
}

std::wstring KnownPinyinForChar(wchar_t ch)
{
    switch (ch)
    {
    case L'爱': return L"ai";
    case L'百': return L"bai";
    case L'宝': return L"bao";
    case L'编': return L"bian";
    case L'哔': return L"bi";
    case L'便': return L"bian";
    case L'表': return L"biao";
    case L'博': return L"bo";
    case L'播': return L"bo";
    case L'财': return L"cai";
    case L'程': return L"cheng";
    case L'词': return L"ci";
    case L'达': return L"da";
    case L'大': return L"da";
    case L'店': return L"dian";
    case L'东': return L"dong";
    case L'抖': return L"dou";
    case L'钉': return L"ding";
    case L'度': return L"du";
    case L'多': return L"duo";
    case L'飞': return L"fei";
    case L'付': return L"fu";
    case L'蜂': return L"feng";
    case L'狗': return L"gou";
    case L'管': return L"guan";
    case L'光': return L"guang";
    case L'广': return L"guang";
    case L'果': return L"guo";
    case L'盒': return L"he";
    case L'红': return L"hong";
    case L'狐': return L"hu";
    case L'乎': return L"hu";
    case L'虎': return L"hu";
    case L'花': return L"hua";
    case L'画': return L"hua";
    case L'会': return L"hui";
    case L'火': return L"huo";
    case L'机': return L"ji";
    case L'辑': return L"ji";
    case L'家': return L"jia";
    case L'加': return L"jia";
    case L'剪': return L"jian";
    case L'记': return L"ji";
    case L'件': return L"jian";
    case L'金': return L"jin";
    case L'京': return L"jing";
    case L'快': return L"kuai";
    case L'酷': return L"ku";
    case L'克': return L"ke";
    case L'夸': return L"kua";
    case L'控': return L"kong";
    case L'览': return L"lan";
    case L'狸': return L"li";
    case L'哩': return L"li";
    case L'理': return L"li";
    case L'雷': return L"lei";
    case L'乐': return L"le";
    case L'联': return L"lian";
    case L'聊': return L"liao";
    case L'流': return L"liu";
    case L'龙': return L"long";
    case L'猫': return L"mao";
    case L'美': return L"mei";
    case L'梦': return L"meng";
    case L'米': return L"mi";
    case L'密': return L"mi";
    case L'民': return L"min";
    case L'墨': return L"mo";
    case L'目': return L"mu";
    case L'脑': return L"nao";
    case L'能': return L"neng";
    case L'盘': return L"pan";
    case L'片': return L"pian";
    case L'频': return L"pin";
    case L'拼': return L"pin";
    case L'屏': return L"ping";
    case L'企': return L"qi";
    case L'奇': return L"qi";
    case L'器': return L"qi";
    case L'签': return L"qian";
    case L'全': return L"quan";
    case L'软': return L"ruan";
    case L'绒': return L"rong";
    case L'森': return L"sen";
    case L'商': return L"shang";
    case L'书': return L"shu";
    case L'输': return L"shu";
    case L'搜': return L"sou";
    case L'速': return L"su";
    case L'台': return L"tai";
    case L'淘': return L"tao";
    case L'腾': return L"teng";
    case L'天': return L"tian";
    case L'条': return L"tiao";
    case L'图': return L"tu";
    case L'团': return L"tuan";
    case L'网': return L"wang";
    case L'微': return L"wei";
    case L'文': return L"wen";
    case L'无': return L"wu";
    case L'息': return L"xi";
    case L'侠': return L"xia";
    case L'下': return L"xia";
    case L'小': return L"xiao";
    case L'星': return L"xing";
    case L'型': return L"xing";
    case L'信': return L"xin";
    case L'行': return L"xing";
    case L'讯': return L"xun";
    case L'迅': return L"xun";
    case L'牙': return L"ya";
    case L'压': return L"ya";
    case L'音': return L"yin";
    case L'影': return L"ying";
    case L'易': return L"yi";
    case L'艺': return L"yi";
    case L'银': return L"yin";
    case L'有': return L"you";
    case L'游': return L"you";
    case L'鱼': return L"yu";
    case L'语': return L"yu";
    case L'云': return L"yun";
    case L'业': return L"ye";
    case L'支': return L"zhi";
    case L'知': return L"zhi";
    case L'中': return L"zhong";
    case L'助': return L"zhu";
    case L'桌': return L"zhuo";
    case L'字': return L"zi";
    default: return {};
    }
}

wchar_t GbkPinyinInitial(wchar_t ch)
{
    char bytes[2] = {};
    const int count = WideCharToMultiByte(936, 0, &ch, 1, bytes, 2, nullptr, nullptr);
    if (count != 2) return 0;

    const int high = static_cast<unsigned char>(bytes[0]);
    const int low = static_cast<unsigned char>(bytes[1]);
    const int code = (high - 160) * 100 + (low - 160);
    static const int ranges[] = {
        1601, 1637, 1833, 2078, 2274, 2302, 2433, 2594, 2787, 3106, 3212, 3472,
        3635, 3722, 3730, 3858, 4027, 4086, 4390, 4558, 4684, 4925, 5249, 5590
    };
    static const wchar_t letters[] = L"ABCDEFGHJKLMNOPQRSTWXYZ";
    for (int i = 0; i < ARRAYSIZE(ranges) - 1; ++i)
    {
        if (code >= ranges[i] && code < ranges[i + 1])
        {
            return static_cast<wchar_t>(std::towlower(letters[i]));
        }
    }

    return 0;
}

std::wstring BuildPinyinSearchText(const std::wstring& text)
{
    std::wstring full;
    std::wstring initials;
    for (wchar_t ch : text)
    {
        if (!IsCjkCharacter(ch)) continue;

        const std::wstring known = KnownPinyinForChar(ch);
        if (!known.empty())
        {
            full += known;
            initials.push_back(known.front());
            continue;
        }

        const wchar_t initial = GbkPinyinInitial(ch);
        if (initial)
        {
            full.push_back(initial);
            initials.push_back(initial);
        }
    }

    if (full.empty() && initials.empty()) return {};
    return full + L" " + initials;
}

std::wstring BuildSearchCandidateText(const LaunchItem& item)
{
    const std::wstring displayPinyin = BuildPinyinSearchText(item.displayName);
    const std::wstring stemPinyin = BuildPinyinSearchText(GetFileStem(item.sourcePath));
    return item.displayName
        + L" " + GetFileStem(item.sourcePath)
        + L" " + displayPinyin
        + L" " + stemPinyin
        + L" " + item.sourcePath;
}

void UpdateSearchIndex(LaunchItem& item)
{
    item.displayNameLower = ToLower(item.displayName);
    item.searchCandidateText = BuildSearchCandidateText(item);
    item.searchCandidateLower = ToLower(item.searchCandidateText);
}

void RebuildSearchIndex()
{
    for (auto& item : g_items)
    {
        UpdateSearchIndex(item);
    }
}

std::vector<std::wstring> SearchTokens(const std::wstring& query)
{
    std::vector<std::wstring> tokens;
    std::wstring current;
    for (wchar_t ch : Trim(query))
    {
        if (std::iswspace(ch))
        {
            if (!current.empty())
            {
                tokens.push_back(current);
                current.clear();
            }
        }
        else
        {
            current.push_back(ch);
        }
    }
    if (!current.empty())
    {
        tokens.push_back(current);
    }
    return tokens;
}

int BoundaryMatchIndex(const std::wstring& text, const std::wstring& token)
{
    if (token.empty()) return 0;
    const auto lowerText = ToLower(text);
    const auto lowerToken = ToLower(token);
    size_t position = lowerText.find(lowerToken);
    while (position != std::wstring::npos)
    {
        if (position == 0)
        {
            return 0;
        }
        const wchar_t previous = lowerText[position - 1];
        if (std::iswspace(previous) || previous == L'-' || previous == L'_' || previous == L'.' || previous == L'\\' || previous == L'/')
        {
            return static_cast<int>(position);
        }
        position = lowerText.find(lowerToken, position + 1);
    }
    return -1;
}

int BoundaryMatchIndexLower(const std::wstring& lowerText, const std::wstring& lowerToken)
{
    if (lowerToken.empty()) return 0;
    size_t position = lowerText.find(lowerToken);
    while (position != std::wstring::npos)
    {
        if (position == 0)
        {
            return 0;
        }
        const wchar_t previous = lowerText[position - 1];
        if (std::iswspace(previous) || previous == L'-' || previous == L'_' || previous == L'.' || previous == L'\\' || previous == L'/')
        {
            return static_cast<int>(position);
        }
        position = lowerText.find(lowerToken, position + 1);
    }
    return -1;
}

int FuzzyMatchScore(const std::wstring& text, const std::wstring& token)
{
    if (token.empty()) return 0;
    const auto lowerText = ToLower(text);
    const auto lowerToken = ToLower(token);
    int score = 0;
    int lastMatched = -1;
    size_t searchFrom = 0;
    for (wchar_t ch : lowerToken)
    {
        const size_t found = lowerText.find(ch, searchFrom);
        if (found == std::wstring::npos)
        {
            return 1000000;
        }
        if (lastMatched >= 0)
        {
            score += static_cast<int>(found) - lastMatched - 1;
        }
        else
        {
            score += static_cast<int>(found);
        }
        lastMatched = static_cast<int>(found);
        searchFrom = found + 1;
    }
    return score + static_cast<int>(lowerText.size() - lowerToken.size());
}

int FuzzyMatchScoreLower(const std::wstring& lowerText, const std::wstring& lowerToken)
{
    if (lowerToken.empty()) return 0;
    int score = 0;
    int lastMatched = -1;
    size_t searchFrom = 0;
    for (wchar_t ch : lowerToken)
    {
        const size_t found = lowerText.find(ch, searchFrom);
        if (found == std::wstring::npos)
        {
            return 1000000;
        }
        if (lastMatched >= 0)
        {
            score += static_cast<int>(found) - lastMatched - 1;
        }
        else
        {
            score += static_cast<int>(found);
        }
        lastMatched = static_cast<int>(found);
        searchFrom = found + 1;
    }
    return score + static_cast<int>(lowerText.size() - lowerToken.size());
}

int SearchScore(const LaunchItem& item, const std::wstring& query)
{
    const auto tokens = SearchTokens(query);
    if (tokens.empty()) return 0;

    const auto& display = item.displayNameLower;
    const auto& candidate = item.searchCandidateLower;
    int total = 0;
    for (const auto& rawToken : tokens)
    {
        const auto token = ToLower(rawToken);
        int tokenScore = 1000000;
        if (display == token)
        {
            tokenScore = 0;
        }
        else if (display.rfind(token, 0) == 0)
        {
            tokenScore = 5 + static_cast<int>(display.size() - token.size());
        }
        else
        {
            const int boundary = BoundaryMatchIndexLower(display, token);
            if (boundary >= 0)
            {
                tokenScore = 20 + boundary;
            }
            else
            {
                const size_t displayContains = display.find(token);
                const size_t candidateContains = candidate.find(token);
                if (displayContains != std::wstring::npos)
                {
                    tokenScore = 45 + static_cast<int>(displayContains);
                }
                else if (candidateContains != std::wstring::npos)
                {
                    tokenScore = 80 + static_cast<int>(candidateContains);
                }
                else
                {
                    const int displayFuzzy = FuzzyMatchScoreLower(display, token);
                    const int candidateFuzzy = FuzzyMatchScoreLower(candidate, token);
                    tokenScore = std::min(120 + displayFuzzy, 220 + candidateFuzzy);
                }
            }
        }

        if (tokenScore >= 1000000)
        {
            return 1000000;
        }
        total += tokenScore;
    }

    return total;
}

void RebuildFiltered()
{
    g_filtered.clear();
    g_filtered.reserve(g_items.size());
    g_filterScores.assign(g_items.size(), 0);
    for (int i = 0; i < static_cast<int>(g_items.size()); ++i)
    {
        const int score = g_searchText.empty() ? 0 : SearchScore(g_items[i], g_searchText);
        if (g_searchText.empty() || score < 1000000)
        {
            g_filterScores[i] = score;
            g_filtered.push_back(i);
        }
    }
    if (!g_searchText.empty())
    {
        std::stable_sort(g_filtered.begin(), g_filtered.end(), [&](int left, int right) {
            const int leftScore = g_filterScores[left];
            const int rightScore = g_filterScores[right];
            if (leftScore != rightScore) return leftScore < rightScore;
            return CompareStringOrdinal(g_items[left].displayName.c_str(), -1, g_items[right].displayName.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
    }
    if (g_selectedFilteredIndex >= static_cast<int>(g_filtered.size()))
    {
        g_selectedFilteredIndex = static_cast<int>(g_filtered.size()) - 1;
    }
    if (g_selectedFilteredIndex < 0 && !g_filtered.empty())
    {
        g_selectedFilteredIndex = 0;
    }
}

HICON LoadShellImageListIcon(const std::wstring& path, int imageListSize)
{
    SHFILEINFOW info = {};
    if (!SHGetFileInfoW(
        path.c_str(),
        0,
        &info,
        sizeof(info),
        SHGFI_SYSICONINDEX | SHGFI_LARGEICON))
    {
        return nullptr;
    }

    IImageList* imageList = nullptr;
    HRESULT hr = SHGetImageList(imageListSize, NativeIID_IImageList, reinterpret_cast<void**>(&imageList));
    if (FAILED(hr) || !imageList)
    {
        return nullptr;
    }

    HICON icon = nullptr;
    hr = imageList->GetIcon(info.iIcon, ILD_TRANSPARENT, &icon);
    imageList->Release();
    return SUCCEEDED(hr) ? icon : nullptr;
}

bool ResolveShortcutIconPath(const std::wstring& sourcePath, std::wstring& iconPath, int& iconIndex)
{
    iconPath = sourcePath;
    iconIndex = 0;
    if (!EndsWithIgnoreCase(sourcePath, L".lnk"))
    {
        return true;
    }

    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (FAILED(hr) || !link) return false;

    IPersistFile* file = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (SUCCEEDED(hr) && file)
    {
        hr = file->Load(sourcePath.c_str(), STGM_READ);
        if (SUCCEEDED(hr))
        {
            wchar_t resolvedIcon[MAX_PATH] = L"";
            int resolvedIndex = 0;
            if (SUCCEEDED(link->GetIconLocation(resolvedIcon, ARRAYSIZE(resolvedIcon), &resolvedIndex)) && resolvedIcon[0] != L'\0')
            {
                iconPath = resolvedIcon;
                iconIndex = resolvedIndex;
            }
            else
            {
                wchar_t target[MAX_PATH] = L"";
                if (SUCCEEDED(link->GetPath(target, ARRAYSIZE(target), nullptr, SLGP_UNCPRIORITY)) && target[0] != L'\0')
                {
                    iconPath = target;
                    iconIndex = 0;
                }
            }
        }
        file->Release();
    }

    link->Release();
    return !iconPath.empty();
}

HICON LoadExactSizedIcon(const std::wstring& sourcePath, int size)
{
    std::wstring iconPath;
    int iconIndex = 0;
    ResolveShortcutIconPath(sourcePath, iconPath, iconIndex);
    if (iconPath.empty()) iconPath = sourcePath;

    HICON icon = nullptr;
    UINT extracted = PrivateExtractIconsW(
        iconPath.c_str(),
        iconIndex,
        size,
        size,
        &icon,
        nullptr,
        1,
        0);
    return extracted > 0 ? icon : nullptr;
}

void LoadItemIcon(LaunchItem& item)
{
    if (item.icon)
    {
        item.iconLastUsed = ++g_iconUseCounter;
        return;
    }
    item.icon = LoadExactSizedIcon(item.sourcePath, g_settings.iconSize);
    if (item.icon)
    {
        item.iconLastUsed = ++g_iconUseCounter;
        return;
    }
    item.icon = LoadShellImageListIcon(item.sourcePath, SHIL_JUMBO);
    if (!item.icon) item.icon = LoadShellImageListIcon(item.sourcePath, SHIL_EXTRALARGE);
    if (item.icon)
    {
        item.iconLastUsed = ++g_iconUseCounter;
        return;
    }

    SHFILEINFOW info = {};
    if (SHGetFileInfoW(
        item.sourcePath.c_str(),
        0,
        &info,
        sizeof(info),
        SHGFI_ICON | SHGFI_LARGEICON))
    {
        item.icon = info.hIcon;
    }
    if (item.icon)
    {
        item.iconLastUsed = ++g_iconUseCounter;
    }
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

enum class TrayPromotionResult
{
    NotFound,
    AlreadyPromoted,
    Promoted
};

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

bool RegistryDwordEquals(HKEY key, const wchar_t* name, DWORD expected)
{
    DWORD type = 0;
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes) == ERROR_SUCCESS
        && type == REG_DWORD
        && bytes == sizeof(value)
        && value == expected;
}

TrayPromotionResult PromoteTrayIconVisibility()
{
    const std::wstring executablePath = CurrentExecutablePath();
    if (executablePath.empty())
    {
        return TrayPromotionResult::NotFound;
    }

    HKEY settingsKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\NotifyIconSettings", 0, KEY_READ, &settingsKey) != ERROR_SUCCESS)
    {
        return TrayPromotionResult::NotFound;
    }

    bool found = false;
    bool promoted = false;
    for (DWORD index = 0;; ++index)
    {
        wchar_t subkeyName[256] = L"";
        DWORD subkeyNameLength = ARRAYSIZE(subkeyName);
        const LONG enumResult = RegEnumKeyExW(settingsKey, index, subkeyName, &subkeyNameLength, nullptr, nullptr, nullptr, nullptr);
        if (enumResult == ERROR_NO_MORE_ITEMS)
        {
            break;
        }
        if (enumResult != ERROR_SUCCESS)
        {
            continue;
        }

        HKEY iconKey = nullptr;
        if (RegOpenKeyExW(settingsKey, subkeyName, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &iconKey) != ERROR_SUCCESS)
        {
            continue;
        }

        std::wstring registeredPath;
        if (ReadRegistryString(iconKey, L"ExecutablePath", registeredPath)
            && CompareStringOrdinal(registeredPath.c_str(), -1, executablePath.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            found = true;
            if (!RegistryDwordEquals(iconKey, L"IsPromoted", 1))
            {
                const DWORD value = 1;
                if (RegSetValueExW(iconKey, L"IsPromoted", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS)
                {
                    promoted = true;
                }
            }
        }

        RegCloseKey(iconKey);
    }

    RegCloseKey(settingsKey);
    if (!found)
    {
        return TrayPromotionResult::NotFound;
    }

    return promoted ? TrayPromotionResult::Promoted : TrayPromotionResult::AlreadyPromoted;
}

bool AddTrayIcon(bool refreshAfterPromotion = true)
{
    NOTIFYICONDATAW nid = {};
    PopulateTrayIconData(nid);
    g_trayIconAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (g_trayIconAdded)
    {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        KillTimer(g_hwnd, TrayIconRetryTimerId);
        const TrayPromotionResult promotionResult = PromoteTrayIconVisibility();
        if (promotionResult == TrayPromotionResult::NotFound)
        {
            g_trayPromotionAttempts = 0;
            SetTimer(g_hwnd, TrayIconPromotionRetryTimerId, TrayIconPromotionRetryMs, nullptr);
        }
        else
        {
            g_trayPromotionAttempts = 0;
            KillTimer(g_hwnd, TrayIconPromotionRetryTimerId);
            if (promotionResult == TrayPromotionResult::Promoted && refreshAfterPromotion)
            {
                RemoveTrayIcon();
                return AddTrayIcon(false);
            }
        }
        return true;
    }

    if (g_hwnd)
    {
        SetTimer(g_hwnd, TrayIconRetryTimerId, TrayIconRetryMs, nullptr);
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
    if (g_hwnd)
    {
        KillTimer(g_hwnd, TrayIconPromotionRetryTimerId);
    }
    g_trayPromotionAttempts = 0;
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

void ResolveHotkey(UINT& modifiers, UINT& key)
{
    const auto hotkeyText = NormalizeHotkeyText(g_settings.hotkey);
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

void RegisterCurrentHotkey()
{
    if (!g_hwnd) return;
    UnregisterHotKey(g_hwnd, HotkeyId);
    UINT modifiers = 0;
    UINT key = 0;
    ResolveHotkey(modifiers, key);
    RegisterHotKey(g_hwnd, HotkeyId, modifiers, key);
}

void ActivateSearchInput()
{
    g_searchInputActive = true;
    if (g_hwnd)
    {
        SetFocus(g_hwnd);
    }
}

void ResetSearchForOpen()
{
    if (!g_settings.resetSearchOnOpen)
    {
        return;
    }

    g_searchText.clear();
    g_searchCompositionText.clear();
    ClearSearchCompletion();
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

void UpdateSearchComposition(HWND hwnd, LPARAM compositionFlags)
{
    ActivateSearchInput();

    if (compositionFlags & GCS_RESULTSTR)
    {
        CommitSearchCompletion();
        const std::wstring result = ReadImeCompositionString(hwnd, GCS_RESULTSTR);
        if (!result.empty())
        {
            g_searchText += result;
            g_scrollOffset = 0;
            RebuildFiltered();
        }

        g_searchCompositionText.clear();
        ClearSearchCompletion();
        RequestSearchRepaint(hwnd, true);
        return;
    }

    if (compositionFlags & GCS_COMPSTR)
    {
        g_searchCompositionText = ReadImeCompositionString(hwnd, GCS_COMPSTR);
        ClearSearchCompletion();
        RequestSearchRepaint(hwnd, false);
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
        LoadItemIcon(g_items[g_filtered[hit.itemIndex]]);
        loaded++;
        if (loaded >= preloadLimit) break;
    }
}

void ShowSpotlightWithAnimation()
{
    RECT finalRect = {};
    GetWindowRect(g_hwnd, &finalRect);
    const int width = finalRect.right - finalRect.left;
    const int height = finalRect.bottom - finalRect.top;

    SetSpotlightLayeredOpacity(0);
    SetWindowPos(
        g_hwnd,
        HWND_TOPMOST,
        finalRect.left,
        finalRect.top - SpotlightAnimationOffset,
        width,
        height,
        SWP_NOACTIVATE);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    RedrawWindow(g_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);

    for (int step = 1; step <= SpotlightAnimationSteps; ++step)
    {
        const double progress = step / static_cast<double>(SpotlightAnimationSteps);
        const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
        const BYTE alpha = static_cast<BYTE>(std::round(SpotlightGlassAlpha * eased));
        const int y = finalRect.top - static_cast<int>(std::round((1.0 - eased) * SpotlightAnimationOffset));
        SetWindowPos(g_hwnd, HWND_TOPMOST, finalRect.left, y, width, height, SWP_NOACTIVATE);
        SetSpotlightLayeredOpacity(alpha);
        Sleep(8);
    }

    SetWindowPos(g_hwnd, HWND_TOPMOST, finalRect.left, finalRect.top, width, height, SWP_NOACTIVATE);
    SetSpotlightLayeredOpacity(SpotlightGlassAlpha);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void HideSpotlightWithAnimation()
{
    if (!g_hwnd || !IsWindowVisible(g_hwnd))
    {
        return;
    }

    RECT startRect = {};
    GetWindowRect(g_hwnd, &startRect);
    const int width = startRect.right - startRect.left;
    const int height = startRect.bottom - startRect.top;

    for (int step = 1; step <= SpotlightAnimationSteps; ++step)
    {
        const double progress = step / static_cast<double>(SpotlightAnimationSteps);
        const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
        const BYTE alpha = static_cast<BYTE>(std::round(SpotlightGlassAlpha * (1.0 - eased)));
        const int y = startRect.top - static_cast<int>(std::round(eased * SpotlightCloseAnimationOffset));
        SetWindowPos(g_hwnd, HWND_TOPMOST, startRect.left, y, width, height, SWP_NOACTIVATE);
        SetSpotlightLayeredOpacity(alpha);
        Sleep(7);
    }

    ShowWindow(g_hwnd, SW_HIDE);
    SetWindowPos(g_hwnd, HWND_TOPMOST, startRect.left, startRect.top, width, height, SWP_NOACTIVATE);
    SetSpotlightLayeredOpacity(SpotlightGlassAlpha);
}

void ShowNativeUi()
{
    ReloadData();
    ResetSearchForOpen();
    RebuildFiltered();
    PositionWindow();
    g_searchInputActive = true;
    PrewarmNativeUiForShow();
    if (IsSpotlightMode())
    {
        ShowSpotlightWithAnimation();
    }
    else
    {
        SetSpotlightLayeredOpacity(255);
        ShowWindow(g_hwnd, SW_SHOW);
    }
    SetForegroundWindow(g_hwnd);
    ActivateSearchInput();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void HideNativeUi()
{
    g_searchInputActive = false;
    if (IsSpotlightMode())
    {
        HideSpotlightWithAnimation();
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
    ShellExecuteW(g_hwnd, L"open", item.sourcePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    HideNativeUi();
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
    if (!g_searchText.empty()) return {};

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

void BeginDrag(HWND hwnd, POINT point)
{
    if (g_dragFilteredIndex < 0 || g_dragFilteredIndex >= static_cast<int>(g_filtered.size())) return;
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
    if (g_pendingRegionDragId.empty()) return;
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
        && g_searchText.empty()
        && !g_dragSourcePaths.empty();
}

bool ShouldTrackDragAvoidancePreview()
{
    return g_dragActive
        && g_dragMode == DragMode::Items
        && g_searchText.empty()
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

void RenderSpotlightGlassDirect2D(ID2D1DCRenderTarget* target, const RECT& client)
{
    target->Clear(D2DColor(SpotlightTransparentKey));

    ID2D1GradientStopCollection* glassStops = nullptr;
    ID2D1LinearGradientBrush* glassBrush = nullptr;
    D2D1_GRADIENT_STOP stops[] = {
        { 0.0f, D2DColorAlpha(RGB(82, 100, 118), 0.66f) },
        { 0.35f, D2DColorAlpha(RGB(50, 64, 82), 0.70f) },
        { 0.72f, D2DColorAlpha(RGB(26, 38, 58), 0.76f) },
        { 1.0f, D2DColorAlpha(RGB(16, 26, 42), 0.80f) }
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
        { 0.0f, D2DColorAlpha(RGB(128, 154, 178), 0.16f) },
        { 0.58f, D2DColorAlpha(RGB(70, 96, 124), 0.052f) },
        { 1.0f, D2DColorAlpha(RGB(6, 14, 26), 0.28f) }
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

    ID2D1GradientStopCollection* sheenStops = nullptr;
    ID2D1LinearGradientBrush* sheenBrush = nullptr;
    D2D1_GRADIENT_STOP sheen[] = {
        { 0.0f, D2DColorAlpha(RGB(238, 248, 255), 0.13f) },
        { 0.24f, D2DColorAlpha(RGB(196, 220, 240), 0.055f) },
        { 1.0f, D2DColorAlpha(RGB(255, 255, 255), 0.0f) }
    };
    if (SUCCEEDED(target->CreateGradientStopCollection(sheen, ARRAYSIZE(sheen), &sheenStops)) && sheenStops)
    {
        const auto props = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(client.left), static_cast<float>(client.top)),
            D2D1::Point2F(static_cast<float>(client.left), static_cast<float>(client.bottom)));
        target->CreateLinearGradientBrush(props, sheenStops, &sheenBrush);
    }
    if (sheenBrush)
    {
        target->FillRoundedRectangle(panel, sheenBrush);
    }

    SafeRelease(sheenBrush);
    SafeRelease(sheenStops);
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
    const int layers = spotlight ? SpotlightEdgeLayers : 5;
    for (int inset = 0; inset < layers; ++inset)
    {
        const int shade = spotlight ? 126 - inset * 5 : 92 + inset * 18;
        const COLORREF color = spotlight
            ? RGB(std::max(80, shade), std::max(92, shade + 8), std::max(110, shade + 18))
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

void DrawSearchGlassSurface(ID2D1DCRenderTarget* target, const RECT& rect, bool active)
{
    const auto rounded = D2D1::RoundedRect(D2DRect(rect), static_cast<float>(SpotlightSearchCornerRadius), static_cast<float>(SpotlightSearchCornerRadius));

    ID2D1GradientStopCollection* fillStops = nullptr;
    ID2D1LinearGradientBrush* fillBrush = nullptr;
    D2D1_GRADIENT_STOP stops[] = {
        { 0.0f, D2DColorAlpha(RGB(124, 148, 168), active ? 0.38f : 0.32f) },
        { 0.52f, D2DColorAlpha(RGB(62, 80, 100), active ? 0.34f : 0.28f) },
        { 1.0f, D2DColorAlpha(RGB(22, 32, 48), active ? 0.42f : 0.34f) }
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
        { 0.0f, D2DColorAlpha(active ? RGB(126, 196, 255) : RGB(210, 230, 246), active ? 0.14f : 0.06f) },
        { 0.62f, D2DColorAlpha(RGB(116, 154, 188), active ? 0.04f : 0.02f) },
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

    ID2D1SolidColorBrush* strokeBrush = nullptr;
    ID2D1SolidColorBrush* innerBrush = nullptr;
    target->CreateSolidColorBrush(D2DColorAlpha(active ? RGB(164, 216, 255) : RGB(214, 234, 246), active ? 0.48f : 0.22f), &strokeBrush);
    target->CreateSolidColorBrush(D2DColorAlpha(RGB(255, 255, 255), active ? 0.10f : 0.06f), &innerBrush);
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
    HBRUSH fill = CreateSolidBrush(active ? RGB(68, 84, 102) : RGB(52, 66, 82));
    HPEN pen = CreatePen(PS_SOLID, 1, active ? RGB(146, 206, 255) : RGB(126, 154, 180));
    HGDIOBJ oldBrush = SelectObject(dc, fill);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, SpotlightSearchCornerRadius * 2, SpotlightSearchCornerRadius * 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(fill);
}

void DrawAppTileSurface(ID2D1DCRenderTarget* target, const RECT& rect, bool selected)
{
    if (!selected) return;
    DrawRoundedRectDirect(target, rect, RGB(42, 82, 118), RGB(130, 190, 255), 12.0f);
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

void DrawAppTileSurface(HDC dc, const RECT& rect, bool selected)
{
    if (!selected) return;
    DrawRoundedRect(dc, rect, RGB(42, 82, 118), RGB(130, 190, 255), 12);
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

RECT SearchCompletionHighlightRect(const RECT& textRect, int prefixWidth)
{
    const int left = std::clamp(textRect.left + prefixWidth, textRect.left, textRect.right - 8);
    return { left, textRect.top - 2, textRect.right, textRect.bottom - 5 };
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
    target->CreateSolidColorBrush(D2DColor(RGB(245, 250, 255)), &brush);
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
    HBRUSH brush = CreateSolidBrush(RGB(245, 250, 255));
    FillRect(dc, &caret, brush);
    DeleteObject(brush);
}

void DrawSearchSurface(ID2D1DCRenderTarget* target, const RECT& client)
{
    const RECT searchRect = SearchRect(client);
    const RECT textRect = SearchTextRect(searchRect);
    DrawSearchGlassSurface(target, searchRect, g_searchInputActive);
    if (IsSpotlightBaseLayerPass()) return;
    DrawTextDirect(
        target,
        g_searchTextFormat,
        SearchDisplayText(),
        textRect,
        g_searchText.empty() ? RGB(158, 172, 188) : RGB(236, 244, 252),
        DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
        DWRITE_WORD_WRAPPING_NO_WRAP);

    const std::wstring tail = SearchCompletionTail();
    if (!tail.empty())
    {
        const int prefixWidth = static_cast<int>(std::ceil(MeasureTextWidthDirect(g_searchTextFormat, SearchDisplayText())));
        RECT highlightRect = SearchCompletionHighlightRect(textRect, prefixWidth);
        DrawRoundedRectDirect(target, highlightRect, RGB(58, 140, 255), RGB(58, 140, 255), 7.0f);
        RECT tailText = { highlightRect.left + 8, textRect.top, highlightRect.right - 8, textRect.bottom };
        DrawTextDirect(
            target,
            g_searchTextFormat,
            tail,
            tailText,
            RGB(255, 255, 255),
            DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
            DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    DrawSearchCaret(target, textRect, static_cast<int>(std::ceil(MeasureTextWidthDirect(g_searchTextFormat, SearchDisplayText()))));
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
    DrawTextClipped(dc, SearchDisplayText(), textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, g_searchText.empty() ? RGB(158, 172, 188) : RGB(236, 244, 252));
    const std::wstring tail = SearchCompletionTail();
    if (!tail.empty())
    {
        const int prefixWidth = MeasureTextWidthGdi(dc, SearchDisplayText());
        RECT highlightRect = SearchCompletionHighlightRect(textRect, prefixWidth);
        DrawRoundedRect(dc, highlightRect, RGB(58, 140, 255), RGB(58, 140, 255), 7);
        RECT tailText = { highlightRect.left + 8, textRect.top, highlightRect.right - 8, textRect.bottom };
        DrawTextClipped(dc, tail, tailText, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, RGB(255, 255, 255));
    }
    DrawSearchCaret(dc, textRect, MeasureTextWidthGdi(dc, SearchDisplayText()));
    SelectObject(dc, oldFont);
    DeleteObject(searchFont);
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
    metrics.groupByRegion = g_searchText.empty();
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
}

void RenderFrameGdi(HDC dc, const RECT& client)
{
    DrawGlassBackground(dc, client);
    DrawSearchSurface(dc, client);
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

    int column = 0;
    int x = metrics.left;
    int y = metrics.initialY;
    std::wstring activeRegion;
    int contentBottom = y;

    if (g_filtered.empty())
    {
        RECT emptyRect = { metrics.left, y + 60, metrics.right, y + 120 };
        if (!baseLayerOnly)
        {
            DrawTextDirect(target, g_headerTextFormat, L"No matching apps", emptyRect, RGB(210, 220, 235), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        g_contentHeight = client.bottom;
    }
    else
    {
        for (int filteredIndex = 0; filteredIndex < static_cast<int>(g_filtered.size()); ++filteredIndex)
        {
            auto& item = g_items[g_filtered[filteredIndex]];
            if (metrics.groupByRegion && activeRegion != item.regionId)
            {
                if (column != 0)
                {
                    y += metrics.tileSize + metrics.gap;
                    column = 0;
                    x = metrics.left;
                }
                activeRegion = item.regionId;
                RECT headerRect = { metrics.left + 8, y, metrics.right, y + 28 };
                if (IsRegionSelected(activeRegion))
                {
                    RECT selectedRect = { metrics.left, y - 4, metrics.right, y + 32 };
                    DrawRoundedRectDirect(target, selectedRect, RGB(30, 66, 96), RGB(118, 178, 238), 10.0f);
                }
                if (!baseLayerOnly)
                {
                    DrawTextDirect(target, g_headerTextFormat, RegionName(activeRegion), headerRect, RGB(220, 232, 248), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
                }
                g_regionHits.push_back({ { metrics.left, y, metrics.right, y + 38 }, { metrics.left, y, metrics.right, y + 38 }, activeRegion });
                y += 38;
            }

            RECT tileRect = { x, y, x + metrics.tileSize, y + metrics.tileSize };
            RECT visualTileRect = tileRect;
            bool hideTileForDrag = false;
            if (ShouldHideTileForDragPreview(item))
            {
                hideTileForDrag = true;
            }
            else
            {
                ApplyDragAvoidanceOffset(visualTileRect, item, metrics.tileSize, metrics.gap, metrics.columns);
            }

            if (!hideTileForDrag && !g_regionHits.empty() && CompareStringOrdinal(g_regionHits.back().regionId.c_str(), -1, item.regionId.c_str(), -1, TRUE) == CSTR_EQUAL)
            {
                g_regionHits.back().bounds.bottom = std::max(g_regionHits.back().bounds.bottom, visualTileRect.bottom + metrics.gap);
            }
            if (!hideTileForDrag && visualTileRect.bottom >= ContentClipTop() && visualTileRect.top <= client.bottom)
            {
                const bool selected = filteredIndex == g_selectedFilteredIndex || IsSelected(item.sourcePath);
                DrawAppTileSurface(target, visualTileRect, selected);
                if (!baseLayerOnly)
                {
                    DrawIconBackplateDirect(target, visualTileRect, selected);
                    LoadItemIcon(item);
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
                g_hits.push_back({ visualTileRect, filteredIndex });
            }

            x += metrics.tileSize + metrics.gap;
            column++;
            if (column >= metrics.columns)
            {
                column = 0;
                x = metrics.left;
                y += metrics.tileSize + metrics.gap;
            }
            contentBottom = std::max(contentBottom, static_cast<int>(visualTileRect.bottom) + metrics.gap + g_scrollOffset);
        }

        g_contentHeight = contentBottom + 44;
    }

    target->PopAxisAlignedClip();
    DrawSoftWindowEdge(target, client);

    const HRESULT drawn = target->EndDraw();
    if (FAILED(drawn))
    {
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

    int column = 0;
    int x = metrics.left;
    int y = metrics.initialY;
    std::wstring activeRegion;
    int contentBottom = y;

    SelectObject(dc, headerFont);
    if (g_filtered.empty())
    {
        RECT emptyRect = { metrics.left, y + 60, metrics.right, y + 120 };
        if (!baseLayerOnly)
        {
            DrawTextClipped(dc, L"No matching apps", emptyRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER, RGB(210, 220, 235));
        }
        g_contentHeight = client.bottom;
    }
    else
    {
        for (int filteredIndex = 0; filteredIndex < static_cast<int>(g_filtered.size()); ++filteredIndex)
        {
            auto& item = g_items[g_filtered[filteredIndex]];
            if (metrics.groupByRegion && activeRegion != item.regionId)
            {
                if (column != 0)
                {
                    y += metrics.tileSize + metrics.gap;
                    column = 0;
                    x = metrics.left;
                }
                activeRegion = item.regionId;
                RECT headerRect = { metrics.left + 8, y, metrics.right, y + 28 };
                if (IsRegionSelected(activeRegion))
                {
                    RECT selectedRect = { metrics.left, y - 4, metrics.right, y + 32 };
                    DrawRoundedRect(dc, selectedRect, RGB(30, 66, 96), RGB(118, 178, 238), 10);
                }
                if (!baseLayerOnly)
                {
                    DrawTextClipped(dc, RegionName(activeRegion), headerRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, RGB(220, 232, 248));
                }
                g_regionHits.push_back({ { metrics.left, y, metrics.right, y + 38 }, { metrics.left, y, metrics.right, y + 38 }, activeRegion });
                y += 38;
            }

            RECT tileRect = { x, y, x + metrics.tileSize, y + metrics.tileSize };
            RECT visualTileRect = tileRect;
            bool hideTileForDrag = false;
            if (ShouldHideTileForDragPreview(item))
            {
                hideTileForDrag = true;
            }
            else
            {
                ApplyDragAvoidanceOffset(visualTileRect, item, metrics.tileSize, metrics.gap, metrics.columns);
            }

            if (!hideTileForDrag && !g_regionHits.empty() && CompareStringOrdinal(g_regionHits.back().regionId.c_str(), -1, item.regionId.c_str(), -1, TRUE) == CSTR_EQUAL)
            {
                g_regionHits.back().bounds.bottom = std::max(g_regionHits.back().bounds.bottom, visualTileRect.bottom + metrics.gap);
            }
            if (!hideTileForDrag && visualTileRect.bottom >= ContentClipTop() && visualTileRect.top <= client.bottom)
            {
                const bool selected = filteredIndex == g_selectedFilteredIndex || IsSelected(item.sourcePath);
                DrawAppTileSurface(dc, visualTileRect, selected);
                if (!baseLayerOnly)
                {
                    LoadItemIcon(item);
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
                g_hits.push_back({ visualTileRect, filteredIndex });
            }

            x += metrics.tileSize + metrics.gap;
            column++;
            if (column >= metrics.columns)
            {
                column = 0;
                x = metrics.left;
                y += metrics.tileSize + metrics.gap;
            }
            contentBottom = std::max(contentBottom, static_cast<int>(visualTileRect.bottom) + metrics.gap + g_scrollOffset);
        }

        g_contentHeight = contentBottom + 44;
    }

    if (!baseLayerOnly)
    {
        DrawDragFeedback(dc);
        DrawSelectionBox(dc);
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(contentClip);
    RestoreDC(dc, savedDc);
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
            const double contentCoverage = contentPixel ? SpotlightContentCoverageForPixel(y, contentClip) : 1.0;
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
    const int gap = TileGap();
    const int left = IsSpotlightMode() ? SpotlightContentInset : 44;
    const int right = client.right - left;
    return std::max(1, (right - left + gap) / (TileSize() + gap));
}

bool EstimateTileBoundsForFilteredIndex(int targetFilteredIndex, RECT& bounds)
{
    if (targetFilteredIndex < 0 || targetFilteredIndex >= static_cast<int>(g_filtered.size())) return false;

    RECT client = {};
    GetClientRect(g_hwnd, &client);
    const int tileSize = TileSize();
    const int gap = TileGap();
    const int columns = CalculateColumnCount(client);
    const int left = 44;
    int column = 0;
    int x = left;
    int logicalY = 114;
    std::wstring activeRegion;
    const bool groupByRegion = g_searchText.empty();

    for (int filteredIndex = 0; filteredIndex <= targetFilteredIndex; ++filteredIndex)
    {
        const auto& item = g_items[g_filtered[filteredIndex]];
        if (groupByRegion && activeRegion != item.regionId)
        {
            if (column != 0)
            {
                logicalY += tileSize + gap;
                column = 0;
                x = left;
            }
            activeRegion = item.regionId;
            logicalY += 38;
        }

        if (filteredIndex == targetFilteredIndex)
        {
            bounds = { x, logicalY - g_scrollOffset, x + tileSize, logicalY + tileSize - g_scrollOffset };
            return true;
        }

        x += tileSize + gap;
        column++;
        if (column >= columns)
        {
            column = 0;
            x = left;
            logicalY += tileSize + gap;
        }
    }

    return false;
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

void SelectFilteredIndex(int index)
{
    if (g_filtered.empty()) return;
    g_selectedFilteredIndex = std::clamp(index, 0, static_cast<int>(g_filtered.size()) - 1);
    const auto& item = g_items[g_filtered[g_selectedFilteredIndex]];
    SetSingleSelection(item.sourcePath);
    if (IsSearchCompletionActive())
    {
        g_completionCandidateText = item.displayName;
        g_searchText = g_completionCandidateText;
    }
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
    SelectFilteredIndex(g_selectedFilteredIndex + delta * CalculateColumnCount(client));
}

void CompleteSearchFromSelection(HWND hwnd, bool reverse)
{
    if (g_filtered.empty())
    {
        ClearSearchCompletion();
        return;
    }
    if (g_selectedFilteredIndex < 0)
    {
        g_selectedFilteredIndex = 0;
    }
    if (!IsSearchCompletionActive())
    {
        g_completionBaseText = g_searchText;
    }
    else
    {
        const int count = static_cast<int>(g_filtered.size());
        const int delta = reverse ? -1 : 1;
        g_selectedFilteredIndex = (g_selectedFilteredIndex + delta + count) % count;
    }

    const std::wstring selectedPath = g_items[g_filtered[g_selectedFilteredIndex]].sourcePath;
    g_completionCandidateText = g_items[g_filtered[g_selectedFilteredIndex]].displayName;
    g_searchText = g_completionCandidateText;
    SetSingleSelection(selectedPath);
    ActivateSearchInput();
    ScrollSelectedIntoView();
    RequestSearchRepaint(hwnd, false);
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

void ReadIconSizeFromSettingsWindow(HWND hwnd)
{
    const int index = static_cast<int>(SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::IconSize), CB_GETCURSEL, 0, 0));
    g_settings.iconSize = index == 0 ? 42 : (index == 2 ? 72 : 56);
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
    ApplyLanguageToSettingsWindow(hwnd);
    SetIconSizeSelection(hwnd);
}

void ReadSettingsWindow(HWND hwnd)
{
    g_settings.launchpadFolder = Trim(GetControlText(hwnd, ControlId(SettingsControlId::LaunchpadFolder)));
    g_settings.hotkey = NormalizeHotkeyText(GetControlText(hwnd, ControlId(SettingsControlId::Hotkey)));
    if (g_settings.launchpadFolder.empty())
    {
        const auto profile = GetUserProfilePath();
        g_settings.launchpadFolder = profile.empty() ? L"Launchpad" : JoinPath(profile, L"Launchpad");
    }
    if (g_settings.hotkey.empty()) g_settings.hotkey = L"Alt+D";

    const int displayIndex = static_cast<int>(SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::DisplayMode), CB_GETCURSEL, 0, 0));
    const int languageIndex = static_cast<int>(SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::Language), CB_GETCURSEL, 0, 0));
    g_settings.displayMode = displayIndex == 1 ? L"Spotlight" : L"Launchpad";
    g_settings.language = languageIndex == 1 ? L"Chinese" : L"English";
    ReadIconSizeFromSettingsWindow(hwnd);
    g_settings.spotlightWidth = NormalizeSpotlightWidth(_wtoi(GetControlText(hwnd, ControlId(SettingsControlId::SpotlightWidth)).c_str()));
    g_settings.spotlightHeight = NormalizeSpotlightHeight(_wtoi(GetControlText(hwnd, ControlId(SettingsControlId::SpotlightHeight)).c_str()));
    g_settings.appSpacing = NormalizeAppSpacing(_wtoi(GetControlText(hwnd, ControlId(SettingsControlId::AppSpacing)).c_str()));
    g_settings.wheelSensitivity = NormalizeWheelSensitivity(std::wcstod(GetControlText(hwnd, ControlId(SettingsControlId::WheelSensitivity)).c_str(), nullptr));
    g_settings.startWithWindows = SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::StartWithWindows), BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.resetSearchOnOpen = SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::ResetSearchOnOpen), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SetStartWithWindows(bool enabled)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        return;
    }

    if (enabled)
    {
        wchar_t exe[MAX_PATH] = L"";
        if (GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe)) > 0)
        {
            std::wstring command = L"\"" + std::wstring(exe) + L"\"";
            RegSetValueExW(key, L"LightLaunchpad", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
    }
    else
    {
        RegDeleteValueW(key, L"LightLaunchpad");
    }

    RegCloseKey(key);
}

void OpenLaunchpadFolder()
{
    CreateDirectoryW(g_settings.launchpadFolder.c_str(), nullptr);
    ShellExecuteW(g_hwnd, L"open", g_settings.launchpadFolder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring UniqueLaunchpadDestination(const std::wstring& sourcePath)
{
    const wchar_t* fileName = PathFindFileNameW(sourcePath.c_str());
    std::wstring name = fileName && *fileName ? fileName : L"Imported.lnk";
    std::wstring stem = name;
    std::wstring extension;
    const auto dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos)
    {
        extension = stem.substr(dot);
        stem.resize(dot);
    }

    std::wstring destination = JoinPath(g_settings.launchpadFolder, name.c_str());
    int index = 2;
    while (GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        const std::wstring candidate = stem + L" (" + std::to_wstring(index++) + L")" + extension;
        destination = JoinPath(g_settings.launchpadFolder, candidate.c_str());
    }
    return destination;
}

bool CopyLaunchableIntoLaunchpad(const std::wstring& sourcePath)
{
    if (!IsLaunchablePath(sourcePath)) return false;
    CreateDirectoryW(g_settings.launchpadFolder.c_str(), nullptr);
    const auto destination = UniqueLaunchpadDestination(sourcePath);
    if (SamePath(sourcePath, destination)) return false;
    return CopyFileW(sourcePath.c_str(), destination.c_str(), TRUE) != FALSE;
}

void RefreshAfterImport()
{
    ReloadData();
    SortItems();
    SaveLayout();
    RebuildFiltered();
    InvalidateRect(g_hwnd, nullptr, TRUE);
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

    int imported = 0;
    const wchar_t* cursor = buffer.data();
    std::wstring first = cursor;
    cursor += first.size() + 1;
    if (*cursor == L'\0')
    {
        imported += CopyLaunchableIntoLaunchpad(first) ? 1 : 0;
    }
    else
    {
        const std::wstring directory = first;
        while (*cursor)
        {
            const std::wstring path = JoinPath(directory, cursor);
            imported += CopyLaunchableIntoLaunchpad(path) ? 1 : 0;
            cursor += lstrlenW(cursor) + 1;
        }
    }

    if (imported > 0) RefreshAfterImport();
}

void ImportLaunchableTree(const std::wstring& root, int& imported)
{
    if (root.empty()) return;
    std::wstring search = JoinPath(root, L"*");
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(search.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;

    do
    {
        if (lstrcmpW(data.cFileName, L".") == 0 || lstrcmpW(data.cFileName, L"..") == 0) continue;
        const std::wstring path = JoinPath(root, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            ImportLaunchableTree(path, imported);
        }
        else
        {
            imported += CopyLaunchableIntoLaunchpad(path) ? 1 : 0;
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
}

void ImportStartMenuApps()
{
    int imported = 0;
    const auto appData = GetAppDataPath();
    const auto programData = GetProgramDataPath();
    if (!appData.empty())
    {
        ImportLaunchableTree(JoinPath(appData, L"Microsoft\\Windows\\Start Menu\\Programs"), imported);
    }
    if (!programData.empty())
    {
        ImportLaunchableTree(JoinPath(programData, L"Microsoft\\Windows\\Start Menu\\Programs"), imported);
    }
    if (imported > 0) RefreshAfterImport();
}

void ApplySettingsFromWindow(HWND hwnd)
{
    ReadSettingsWindow(hwnd);
    SaveSettings();
    SetStartWithWindows(g_settings.startWithWindows);
    RegisterCurrentHotkey();
    ReloadData();
    RebuildFiltered();
    PositionWindow();
    PopulateSettingsWindow(hwnd);
    InvalidateRect(g_hwnd, nullptr, TRUE);
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
        y += 48;
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
            ApplySettingsFromWindow(hwnd);
            DestroyWindow(hwnd);
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
        ShowWindow(g_settingsHwnd, SW_SHOW);
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
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        SettingsWindowClassName,
        Text(L"LightLaunchpad Settings", L"LightLaunchpad 设置"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        560,
        540,
        g_hwnd,
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

std::wstring ExplorerSelectArguments(const std::wstring& sourcePath)
{
    return L"/select,\"" + sourcePath + L"\"";
}

void LaunchItemBySourcePath(const std::wstring& sourcePath)
{
    auto found = std::find_if(g_items.begin(), g_items.end(), [&](const LaunchItem& item) {
        return SamePath(item.sourcePath, sourcePath);
    });
    if (found != g_items.end())
    {
        ShellExecuteW(g_hwnd, L"open", found->sourcePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        HideNativeUi();
    }
}

void OpenItemLocation(const std::wstring& sourcePath)
{
    const auto arguments = ExplorerSelectArguments(sourcePath);
    ShellExecuteW(g_hwnd, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

void ShowItemContextMenu(POINT point, int filteredIndex)
{
    if (filteredIndex < 0 || filteredIndex >= static_cast<int>(g_filtered.size())) return;
    const auto& item = g_items[g_filtered[filteredIndex]];
    if (!IsSelected(item.sourcePath))
    {
        SetSingleSelection(item.sourcePath);
    }
    g_contextItemPaths = ResolveDragSourcePaths(item);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, ItemLaunchCommand, Text(L"Launch", L"启动"));
    AppendMenuW(menu, g_contextItemPaths.size() > 1 ? MF_STRING | MF_GRAYED : MF_STRING, ItemRenameCommand, Text(L"Rename", L"重命名"));
    AppendMenuW(menu, MF_STRING, ItemOpenLocationCommand, Text(L"Open file location", L"打开文件位置"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ItemRemoveCommand, g_contextItemPaths.size() > 1 ? Text(L"Delete selected icons", L"删除已选图标") : Text(L"Delete icon", L"删除图标"));
    ClientToScreen(g_hwnd, &point);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
}

void ShowRegionContextMenu(POINT point, const std::wstring& regionId)
{
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
    AppendMenuW(menu, MF_STRING, WorkspaceCreateRegionCommand, Text(L"Create region", L"新建 region"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, WorkspaceImportFilesCommand, Text(L"Import files", L"导入文件"));
    AppendMenuW(menu, MF_STRING, WorkspaceImportStartMenuCommand, Text(L"Import Start Menu apps", L"导入开始菜单应用"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, WorkspaceSelectAllCommand, Text(L"Select all", L"全选"));
    AppendMenuW(menu, MF_STRING, WorkspaceClearSelectionCommand, Text(L"Clear selection", L"清除选择"));
    ClientToScreen(g_hwnd, &point);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == g_taskbarCreatedMessage && g_taskbarCreatedMessage != 0)
    {
        ReaddTrayIcon();
        return 0;
    }

    switch (message)
    {
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
        if (wParam == HotkeyId) ToggleNativeUi();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == TrayOpenCommand) ShowNativeUi();
        if (LOWORD(wParam) == TrayOpenFolderCommand) OpenLaunchpadFolder();
        if (LOWORD(wParam) == TrayRefreshCommand) { ReloadData(); RebuildFiltered(); InvalidateRect(hwnd, nullptr, TRUE); }
        if (LOWORD(wParam) == TraySettingsCommand) ShowSettingsWindow();
        if (LOWORD(wParam) == TrayExitCommand) DestroyWindow(hwnd);
        if (LOWORD(wParam) == ItemLaunchCommand && !g_contextItemPaths.empty()) LaunchItemBySourcePath(g_contextItemPaths.front());
        if (LOWORD(wParam) == ItemRenameCommand && g_contextItemPaths.size() == 1) RenameItem(g_contextItemPaths.front());
        if (LOWORD(wParam) == ItemOpenLocationCommand && !g_contextItemPaths.empty()) OpenItemLocation(g_contextItemPaths.front());
        if (LOWORD(wParam) == ItemRemoveCommand && !g_contextItemPaths.empty())
        {
            const int failed = DeleteItemsFromLaunchpad(g_contextItemPaths);
            SortItems();
            SaveLayout();
            RebuildFiltered();
            InvalidateRect(hwnd, nullptr, TRUE);
            if (failed > 0)
            {
                MessageBoxW(hwnd, Text(L"Some icons could not be deleted from disk.", L"部分图标无法从磁盘删除。"), Text(L"Delete icon", L"删除图标"), MB_ICONWARNING | MB_OK);
            }
        }
        if (LOWORD(wParam) == RegionRenameCommand && !g_contextRegionId.empty())
        {
            RenameRegion(g_contextRegionId);
        }
        if (LOWORD(wParam) == RegionDeleteCommand && !g_contextRegionId.empty())
        {
            DeleteRegionAndMoveItems(g_contextRegionId);
            SortItems();
            SaveLayout();
            RebuildFiltered();
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        if (LOWORD(wParam) == RegionRenameSelectedCommand && !g_contextRegionIds.empty())
        {
            RenameSelectedRegions(g_contextRegionIds);
        }
        if (LOWORD(wParam) == RegionDeleteSelectedCommand && !g_contextRegionIds.empty())
        {
            DeleteSelectedRegions(g_contextRegionIds);
        }
        if (LOWORD(wParam) == WorkspaceCreateRegionCommand) CreateRegion();
        if (LOWORD(wParam) == WorkspaceImportFilesCommand) ImportLaunchableFiles();
        if (LOWORD(wParam) == WorkspaceImportStartMenuCommand) ImportStartMenuApps();
        if (LOWORD(wParam) == WorkspaceSelectAllCommand) SelectAllItems();
        if (LOWORD(wParam) == WorkspaceClearSelectionCommand) ClearAllSelection();
        return 0;
    case WmTray:
        HandleTrayNotification(lParam);
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
        if (wParam == TrayIconPromotionRetryTimerId)
        {
            const TrayPromotionResult promotionResult = PromoteTrayIconVisibility();
            if (promotionResult == TrayPromotionResult::Promoted)
            {
                g_trayPromotionAttempts = 0;
                KillTimer(hwnd, TrayIconPromotionRetryTimerId);
                ReaddTrayIcon();
                return 0;
            }
            if (promotionResult == TrayPromotionResult::AlreadyPromoted
                || ++g_trayPromotionAttempts >= TrayIconPromotionMaxAttempts)
            {
                g_trayPromotionAttempts = 0;
                KillTimer(hwnd, TrayIconPromotionRetryTimerId);
            }
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
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        ResetDragState();
        for (const auto& hit : g_hits)
        {
            if (PtInRect(&hit.bounds, point))
            {
                g_selectedFilteredIndex = hit.itemIndex;
                if (hit.itemIndex >= 0 && hit.itemIndex < static_cast<int>(g_filtered.size()))
                {
                    const auto& item = g_items[g_filtered[hit.itemIndex]];
                    if (wParam & MK_CONTROL)
                    {
                        ToggleSelection(item.sourcePath);
                    }
                    else if (!IsSelected(item.sourcePath))
                    {
                        SetSingleSelection(item.sourcePath);
                    }
                }
                g_mouseDown = g_searchText.empty();
                g_dragFilteredIndex = hit.itemIndex;
                g_dragStart = point;
                g_dragCurrent = point;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        for (const auto& regionHit : g_regionHits)
        {
            if (PtInRect(&regionHit.headerBounds, point))
            {
                if (wParam & MK_CONTROL)
                {
                    ToggleRegionSelection(regionHit.regionId);
                }
                else if (!IsRegionSelected(regionHit.regionId))
                {
                    SetSingleRegionSelection(regionHit.regionId);
                }
                g_mouseDown = g_searchText.empty();
                g_pendingRegionDragId = regionHit.regionId;
                g_dragStart = point;
                g_dragCurrent = point;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        if (point.y >= ContentClipTop())
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
        if (g_selectionBoxActive)
        {
            UpdateSelectionBox(point);
            return 0;
        }
        if (g_mouseDown && !g_dragActive && HasExceededDragThreshold(point))
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
        if (g_dragActive)
        {
            UpdateDrag(point);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
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
        ResetDragState();
        return 0;
    case WM_RBUTTONUP:
    {
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
        if (wParam == VK_ESCAPE)
        {
            if (g_selectionBoxActive) CancelSelectionBox(hwnd);
            else if (g_dragActive) CancelDrag(hwnd);
            else HideNativeUi();
            return 0;
        }
        if (wParam == L'A' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            SelectAllItems();
            return 0;
        }
        if (wParam == VK_TAB)
        {
            CompleteSearchFromSelection(hwnd, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return 0;
        }
        if (wParam == VK_RETURN) { LaunchItemAtFilteredIndex(g_selectedFilteredIndex); return 0; }
        if (wParam == VK_RIGHT)
        {
            MoveSelectionHorizontal(1);
            return 0;
        }
        if (wParam == VK_LEFT)
        {
            MoveSelectionHorizontal(-1);
            return 0;
        }
        if (wParam == VK_DOWN)
        {
            MoveSelectionVertical(1);
            return 0;
        }
        if (wParam == VK_UP)
        {
            MoveSelectionVertical(-1);
            return 0;
        }
        return 0;
    case WM_IME_COMPOSITION:
        UpdateSearchComposition(hwnd, lParam);
        return 0;
    case WM_IME_ENDCOMPOSITION:
        g_searchCompositionText.clear();
        RequestSearchRepaint(hwnd, false);
        return 0;
    case WM_IME_CHAR:
        ActivateSearchInput();
        CommitSearchCompletion();
        g_searchCompositionText.clear();
        if (wParam >= 32)
        {
            g_searchText.push_back(static_cast<wchar_t>(wParam));
            g_scrollOffset = 0;
            RebuildFiltered();
        }
        ClearSearchCompletion();
        RequestSearchRepaint(hwnd, true);
        return 0;
    case WM_CHAR:
        ActivateSearchInput();
        if (wParam == VK_TAB)
        {
            return 0;
        }
        CommitSearchCompletion();
        g_searchCompositionText.clear();
        if (wParam == VK_BACK)
        {
            if (!g_searchText.empty()) g_searchText.pop_back();
        }
        else if (wParam >= 32)
        {
            g_searchText.push_back(static_cast<wchar_t>(wParam));
        }
        g_scrollOffset = 0;
        RebuildFiltered();
        ClearSearchCompletion();
        RequestSearchRepaint(hwnd, true);
        return 0;
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
        KillTimer(hwnd, DragAnimationTimerId);
        RemoveTrayIcon();
        UnregisterHotKey(hwnd, HotkeyId);
        ReleaseSpotlightLayerBuffer();
        DestroyDirectRenderer();
        if (g_uiFont)
        {
            DeleteObject(g_uiFont);
            g_uiFont = nullptr;
        }
        ReleaseAllResidentIcons();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    g_instance = instance;
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    const HRESULT coInitializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(coInitializeResult);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"LightLaunchpad.NativeUi.SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (comInitialized) CoUninitialize();
        return 0;
    }

    LoadSettings();
    ReloadData();
    RebuildFiltered();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = WndProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadEmbeddedAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    wc.hIconSm = LoadEmbeddedAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wc.lpszClassName = WindowClassName;
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);

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
        CloseHandle(mutex);
        if (comInitialized) CoUninitialize();
        return 1;
    }

    RegisterCurrentHotkey();
    AddTrayIcon();

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
