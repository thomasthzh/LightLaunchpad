#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
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
const GUID NativeIID_IImageList = { 0x46eb5926, 0x582e, 0x4017, { 0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50 } };

enum class SettingsControlId : int
{
    LaunchpadFolder = 3001,
    Hotkey = 3002,
    DisplayMode = 3003,
    Language = 3004,
    SpotlightWidth = 3005,
    SpotlightHeight = 3006,
    AppSpacing = 3007,
    WheelSensitivity = 3008,
    StartWithWindows = 3009,
    Save = 3010,
    Cancel = 3011,
    FolderLabel = 3020,
    HotkeyLabel = 3021,
    DisplayModeLabel = 3022,
    LanguageLabel = 3023,
    SpotlightSizeLabel = 3024,
    AppSpacingLabel = 3025,
    WheelSensitivityLabel = 3026
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
};

struct HitTile
{
    RECT bounds;
    int itemIndex = -1;
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

enum class DragMode
{
    None,
    Items,
    Region
};

HINSTANCE g_instance = nullptr;
HWND g_hwnd = nullptr;
HWND g_settingsHwnd = nullptr;
Settings g_settings;
std::vector<Region> g_regions;
std::vector<LaunchItem> g_items;
std::vector<int> g_filtered;
std::vector<HitTile> g_hits;
std::vector<RegionHit> g_regionHits;
std::vector<std::wstring> g_selectedSourcePaths;
std::vector<std::wstring> g_selectedRegionIds;
std::wstring g_searchText;
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
RegionDropTarget g_regionDropTarget;
std::vector<std::wstring> g_contextItemPaths;
std::wstring g_contextRegionId;
std::vector<std::wstring> g_contextRegionIds;
int g_modalDialogDepth = 0;
ID2D1Factory* g_d2dFactory = nullptr;
IDWriteFactory* g_dwriteFactory = nullptr;
IDWriteTextFormat* g_searchTextFormat = nullptr;
IDWriteTextFormat* g_tileTextFormat = nullptr;
IDWriteTextFormat* g_headerTextFormat = nullptr;

bool ShowTextInputDialog(const std::wstring& title, const std::wstring& label, const std::wstring& initialValue, std::wstring& result);
void RegisterCurrentHotkey();
void ShowSettingsWindow();
void RebuildFiltered();
void DestroyDirectRenderer();

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
    if (!hotkey.empty()) g_settings.hotkey = hotkey;
    const auto displayMode = FindJsonStringValue(json, "DisplayMode");
    if (!displayMode.empty()) g_settings.displayMode = displayMode;
    const auto language = FindJsonStringValue(json, "Language");
    if (!language.empty()) g_settings.language = language;
    const auto iconSize = FindJsonStringValue(json, "IconSize");
    if (ContainsIgnoreCase(iconSize, L"Large")) g_settings.iconSize = 64;
    if (ContainsIgnoreCase(iconSize, L"Small")) g_settings.iconSize = 42;
    g_settings.spotlightWidth = NormalizeSpotlightWidth(FindJsonNumberValue(json, "SpotlightWidth", g_settings.spotlightWidth));
    g_settings.spotlightHeight = NormalizeSpotlightHeight(FindJsonNumberValue(json, "SpotlightHeight", g_settings.spotlightHeight));
    g_settings.appSpacing = NormalizeAppSpacing(FindJsonNumberValue(json, "AppSpacing", g_settings.appSpacing));
    g_settings.wheelSensitivity = NormalizeWheelSensitivity(FindJsonDoubleValue(json, "WheelSensitivity", g_settings.wheelSensitivity));
    g_settings.startWithWindows = FindJsonBoolValue(json, "StartWithWindows", g_settings.startWithWindows);
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
    json += "  \"WheelSensitivity\": " + WideToUtf8(FormatDouble(g_settings.wheelSensitivity)) + "\r\n";
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
            if (item.icon) DestroyIcon(item.icon);
            item.icon = nullptr;
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

void ReloadData()
{
    for (auto& item : g_items)
    {
        if (item.icon) DestroyIcon(item.icon);
        item.icon = nullptr;
    }
    LoadSettings();
    LoadLayout();
    AddMissingLaunchpadFolderItems();
    SortItems();
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

void RebuildFiltered()
{
    g_filtered.clear();
    for (int i = 0; i < static_cast<int>(g_items.size()); ++i)
    {
        if (g_searchText.empty() || ContainsIgnoreCase(g_items[i].displayName, g_searchText))
        {
            g_filtered.push_back(i);
        }
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

void LoadItemIcon(LaunchItem& item)
{
    if (item.icon) return;
    item.icon = LoadShellImageListIcon(item.sourcePath, SHIL_JUMBO);
    if (!item.icon) item.icon = LoadShellImageListIcon(item.sourcePath, SHIL_EXTRALARGE);
    if (item.icon) return;

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
}

HICON LoadTrayIcon()
{
    return LoadIconW(nullptr, IDI_APPLICATION);
}

void AddTrayIcon()
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WmTray;
    nid.hIcon = LoadTrayIcon();
    CopyText(nid.szTip, ARRAYSIZE(nid.szTip), L"LightLaunchpad Native UI");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon()
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ResolveHotkey(UINT& modifiers, UINT& key)
{
    auto normalized = ToLower(g_settings.hotkey);
    modifiers = 0;
    key = L'D';
    if (normalized.find(L"alt") != std::wstring::npos) modifiers |= MOD_ALT;
    if (normalized.find(L"ctrl") != std::wstring::npos || normalized.find(L"control") != std::wstring::npos) modifiers |= MOD_CONTROL;
    if (normalized.find(L"shift") != std::wstring::npos) modifiers |= MOD_SHIFT;
    if (normalized.find(L"win") != std::wstring::npos || normalized.find(L"windows") != std::wstring::npos) modifiers |= MOD_WIN;
    const auto plus = g_settings.hotkey.find_last_of(L'+');
    wchar_t candidate = plus != std::wstring::npos && plus + 1 < g_settings.hotkey.size()
        ? static_cast<wchar_t>(std::towupper(g_settings.hotkey[plus + 1]))
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

bool IsSpotlightMode()
{
    return ContainsIgnoreCase(g_settings.displayMode, L"Spotlight");
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
        SetWindowPos(
            g_hwnd,
            HWND_TOPMOST,
            work.left + ((work.right - work.left) - width) / 2,
            work.top + ((work.bottom - work.top) - height) / 2,
            width,
            height,
            SWP_NOACTIVATE);
    }
    else
    {
        SetWindowPos(
            g_hwnd,
            HWND_TOPMOST,
            GetSystemMetrics(SM_XVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_CXVIRTUALSCREEN),
            GetSystemMetrics(SM_CYVIRTUALSCREEN),
            SWP_NOACTIVATE);
    }
}

void ShowNativeUi()
{
    ReloadData();
    RebuildFiltered();
    PositionWindow();
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void HideNativeUi()
{
    ShowWindow(g_hwnd, SW_HIDE);
    DestroyDirectRenderer();
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
            return { regionHit.regionId, CountItemsInRegion(regionHit.regionId, g_dragSourcePaths), true };
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
        return { target->regionId, CountItemsInRegion(target->regionId, g_dragSourcePaths), true };
    }

    return {};
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
    g_regionDropTarget = {};
}

void BeginDrag(HWND hwnd, POINT point)
{
    if (g_dragFilteredIndex < 0 || g_dragFilteredIndex >= static_cast<int>(g_filtered.size())) return;
    g_dragActive = true;
    g_dragMode = DragMode::Items;
    g_dragCurrent = point;
    g_dragSourcePaths = ResolveDragSourcePaths(g_items[g_filtered[g_dragFilteredIndex]]);
    g_dropTarget = FindDropTarget(point);
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
        g_dropTarget = FindDropTarget(point);
    }
}

void CompleteDrag(HWND hwnd)
{
    if (g_dragActive && g_dropTarget.valid && !g_dragSourcePaths.empty())
    {
        MoveItemsToTarget(g_dragSourcePaths, g_dropTarget);
        SortItems();
        SaveLayout();
        RebuildFiltered();
    }

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

    if (GetCapture() == hwnd)
    {
        ReleaseCapture();
    }
    ResetDragState();
    InvalidateRect(hwnd, nullptr, TRUE);
}

void CancelDrag(HWND hwnd)
{
    if (GetCapture() == hwnd)
    {
        ReleaseCapture();
    }
    ResetDragState();
    InvalidateRect(hwnd, nullptr, FALSE);
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

D2D1_RECT_F D2DRect(const RECT& rect)
{
    return D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.right), static_cast<float>(rect.bottom));
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

struct PendingIconDraw
{
    HICON icon = nullptr;
    int x = 0;
    int y = 0;
    int size = 0;
};

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
            DrawRoundedRect(dc, marker, RGB(80, 190, 255), RGB(160, 225, 255), 4);
            break;
        }

        RECT ghost = { g_dragCurrent.x + 16, g_dragCurrent.y + 16, g_dragCurrent.x + 180, g_dragCurrent.y + 58 };
        DrawRoundedRect(dc, ghost, RGB(42, 56, 76), RGB(120, 185, 255), 12);
        RECT text = { ghost.left + 12, ghost.top + 4, ghost.right - 12, ghost.bottom - 4 };
        DrawTextClipped(dc, RegionName(g_regionDragId), text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, RGB(242, 248, 255));
        return;
    }

    if (!g_dropTarget.valid) return;

    RECT marker = {};
    bool hasMarker = false;
    for (const auto& hit : g_hits)
    {
        if (hit.itemIndex < 0 || hit.itemIndex >= static_cast<int>(g_filtered.size())) continue;
        const auto& item = g_items[g_filtered[hit.itemIndex]];
        if (CompareStringOrdinal(item.regionId.c_str(), -1, g_dropTarget.regionId.c_str(), -1, TRUE) != CSTR_EQUAL) continue;
        if (item.order >= g_dropTarget.order)
        {
            marker = { hit.bounds.left - 3, hit.bounds.top + 8, hit.bounds.left + 3, hit.bounds.bottom - 8 };
            hasMarker = true;
            break;
        }

        marker = { hit.bounds.right + 4, hit.bounds.top + 8, hit.bounds.right + 10, hit.bounds.bottom - 8 };
        hasMarker = true;
    }

    if (!hasMarker)
    {
        for (const auto& regionHit : g_regionHits)
        {
            if (CompareStringOrdinal(regionHit.regionId.c_str(), -1, g_dropTarget.regionId.c_str(), -1, TRUE) == CSTR_EQUAL)
            {
                marker = { regionHit.bounds.left + 10, regionHit.bounds.top + 34, regionHit.bounds.right - 10, regionHit.bounds.top + 40 };
                hasMarker = true;
                break;
            }
        }
    }

    if (hasMarker)
    {
        DrawRoundedRect(dc, marker, RGB(80, 190, 255), RGB(160, 225, 255), 4);
    }

    RECT ghost = { g_dragCurrent.x + 16, g_dragCurrent.y + 16, g_dragCurrent.x + 162, g_dragCurrent.y + 58 };
    DrawRoundedRect(dc, ghost, RGB(42, 56, 76), RGB(120, 185, 255), 12);
    if (g_dragFilteredIndex >= 0 && g_dragFilteredIndex < static_cast<int>(g_filtered.size()))
    {
        RECT text = { ghost.left + 12, ghost.top + 4, ghost.right - 12, ghost.bottom - 4 };
        const auto label = g_dragSourcePaths.size() > 1
            ? std::to_wstring(g_dragSourcePaths.size()) + L" apps"
            : g_items[g_filtered[g_dragFilteredIndex]].displayName;
        DrawTextClipped(dc, label, text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, RGB(242, 248, 255));
    }
}

bool PaintContentDirect2D(HDC dc, const RECT& client)
{
    if (!InitializeDirectRenderer()) return false;

    ID2D1DCRenderTarget* target = nullptr;
    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    if (FAILED(g_d2dFactory->CreateDCRenderTarget(&props, &target)) || !target)
    {
        return false;
    }

    if (FAILED(target->BindDC(dc, &client)))
    {
        SafeRelease(target);
        return false;
    }

    g_hits.clear();
    g_regionHits.clear();
    std::vector<PendingIconDraw> pendingIcons;

    target->BeginDraw();
    target->Clear(D2DColor(RGB(18, 22, 30)));

    RECT searchRect = { 48, 34, client.right - 48, 84 };
    DrawRoundedRectDirect(target, searchRect, RGB(242, 247, 250), RGB(92, 110, 130), 22.0f);
    RECT searchTextRect = { searchRect.left + 18, searchRect.top + 10, searchRect.right - 18, searchRect.bottom };
    DrawTextDirect(
        target,
        g_searchTextFormat,
        g_searchText.empty() ? L"Search" : g_searchText,
        searchTextRect,
        g_searchText.empty() ? RGB(110, 120, 132) : RGB(21, 26, 34),
        DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
        DWRITE_WORD_WRAPPING_NO_WRAP);

    const int tileSize = std::max(112, g_settings.iconSize + 72);
    const int gap = std::clamp(g_settings.appSpacing + 4, 4, 32);
    const int left = 44;
    const int right = client.right - 44;
    const int columns = std::max(1, (right - left + gap) / (tileSize + gap));
    int column = 0;
    int x = left;
    int y = 114 - g_scrollOffset;
    std::wstring activeRegion;
    int contentBottom = y;

    if (g_filtered.empty())
    {
        RECT emptyRect = { left, y + 60, right, y + 120 };
        DrawTextDirect(target, g_headerTextFormat, L"No matching apps", emptyRect, RGB(210, 220, 235), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
        g_contentHeight = client.bottom;
    }
    else
    {
        for (int filteredIndex = 0; filteredIndex < static_cast<int>(g_filtered.size()); ++filteredIndex)
        {
            auto& item = g_items[g_filtered[filteredIndex]];
            if (activeRegion != item.regionId)
            {
                if (column != 0)
                {
                    y += tileSize + gap;
                    column = 0;
                    x = left;
                }
                activeRegion = item.regionId;
                RECT headerRect = { left + 8, y, right, y + 28 };
                if (IsRegionSelected(activeRegion))
                {
                    RECT selectedRect = { left, y - 4, right, y + 32 };
                    DrawRoundedRectDirect(target, selectedRect, RGB(30, 66, 96), RGB(118, 178, 238), 10.0f);
                }
                DrawTextDirect(target, g_headerTextFormat, RegionName(activeRegion), headerRect, RGB(220, 232, 248), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
                g_regionHits.push_back({ { left, y, right, y + 38 }, { left, y, right, y + 38 }, activeRegion });
                y += 38;
            }

            RECT tileRect = { x, y, x + tileSize, y + tileSize };
            if (!g_regionHits.empty() && CompareStringOrdinal(g_regionHits.back().regionId.c_str(), -1, item.regionId.c_str(), -1, TRUE) == CSTR_EQUAL)
            {
                g_regionHits.back().bounds.bottom = std::max(g_regionHits.back().bounds.bottom, tileRect.bottom + gap);
            }
            if (tileRect.bottom >= 92 && tileRect.top <= client.bottom)
            {
                const bool selected = filteredIndex == g_selectedFilteredIndex || IsSelected(item.sourcePath);
                DrawRoundedRectDirect(target, tileRect, selected ? RGB(42, 82, 118) : RGB(28, 35, 48), selected ? RGB(130, 190, 255) : RGB(50, 62, 82), 12.0f);
                LoadItemIcon(item);
                if (item.icon)
                {
                    const int icon = g_settings.iconSize;
                    const int iconX = tileRect.left + (tileSize - icon) / 2;
                    const int iconY = tileRect.top + 14;
                    pendingIcons.push_back({ item.icon, iconX, iconY, icon });
                }

                RECT labelRect = { tileRect.left + 8, tileRect.top + g_settings.iconSize + 24, tileRect.right - 8, tileRect.bottom - 8 };
                DrawTextDirect(target, g_tileTextFormat, item.displayName, labelRect, RGB(238, 244, 252), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_WRAP);
                g_hits.push_back({ tileRect, filteredIndex });
            }

            x += tileSize + gap;
            column++;
            if (column >= columns)
            {
                column = 0;
                x = left;
                y += tileSize + gap;
            }
            contentBottom = std::max(contentBottom, static_cast<int>(tileRect.bottom) + gap + g_scrollOffset);
        }

        g_contentHeight = contentBottom + 44;
    }

    const HRESULT drawn = target->EndDraw();
    SafeRelease(target);
    if (FAILED(drawn)) return false;

    for (const auto& icon : pendingIcons)
    {
        DrawIconEx(dc, icon.x, icon.y, icon.icon, icon.size, icon.size, 0, nullptr, DI_NORMAL);
    }
    DrawDragFeedback(dc);
    return true;
}

void PaintContent(HDC dc, const RECT& client)
{
    g_hits.clear();
    g_regionHits.clear();
    HBRUSH background = CreateSolidBrush(RGB(18, 22, 30));
    FillRect(dc, &client, background);
    DeleteObject(background);

    HFONT searchFont = CreateFontW(26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT tileFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT headerFont = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    RECT searchRect = { 48, 34, client.right - 48, 84 };
    DrawRoundedRect(dc, searchRect, RGB(242, 247, 250), RGB(92, 110, 130), 22);
    SelectObject(dc, searchFont);
    RECT searchTextRect = { searchRect.left + 18, searchRect.top + 10, searchRect.right - 18, searchRect.bottom };
    DrawTextClipped(dc, g_searchText.empty() ? L"Search" : g_searchText, searchTextRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, g_searchText.empty() ? RGB(110, 120, 132) : RGB(21, 26, 34));

    const int tileSize = std::max(112, g_settings.iconSize + 72);
    const int gap = std::clamp(g_settings.appSpacing + 4, 4, 32);
    const int left = 44;
    const int right = client.right - 44;
    const int columns = std::max(1, (right - left + gap) / (tileSize + gap));
    int column = 0;
    int x = left;
    int y = 114 - g_scrollOffset;
    std::wstring activeRegion;
    int contentBottom = y;

    SelectObject(dc, headerFont);
    if (g_filtered.empty())
    {
        RECT emptyRect = { left, y + 60, right, y + 120 };
        DrawTextClipped(dc, L"No matching apps", emptyRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER, RGB(210, 220, 235));
        g_contentHeight = client.bottom;
    }
    else
    {
        for (int filteredIndex = 0; filteredIndex < static_cast<int>(g_filtered.size()); ++filteredIndex)
        {
            auto& item = g_items[g_filtered[filteredIndex]];
            if (activeRegion != item.regionId)
            {
                if (column != 0)
                {
                    y += tileSize + gap;
                    column = 0;
                    x = left;
                }
                activeRegion = item.regionId;
                RECT headerRect = { left + 8, y, right, y + 28 };
                if (IsRegionSelected(activeRegion))
                {
                    RECT selectedRect = { left, y - 4, right, y + 32 };
                    DrawRoundedRect(dc, selectedRect, RGB(30, 66, 96), RGB(118, 178, 238), 10);
                }
                DrawTextClipped(dc, RegionName(activeRegion), headerRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, RGB(220, 232, 248));
                g_regionHits.push_back({ { left, y, right, y + 38 }, { left, y, right, y + 38 }, activeRegion });
                y += 38;
            }

            RECT tileRect = { x, y, x + tileSize, y + tileSize };
            if (!g_regionHits.empty() && CompareStringOrdinal(g_regionHits.back().regionId.c_str(), -1, item.regionId.c_str(), -1, TRUE) == CSTR_EQUAL)
            {
                g_regionHits.back().bounds.bottom = std::max(g_regionHits.back().bounds.bottom, tileRect.bottom + gap);
            }
            if (tileRect.bottom >= 92 && tileRect.top <= client.bottom)
            {
                const bool selected = filteredIndex == g_selectedFilteredIndex || IsSelected(item.sourcePath);
                DrawRoundedRect(dc, tileRect, selected ? RGB(42, 82, 118) : RGB(28, 35, 48), selected ? RGB(130, 190, 255) : RGB(50, 62, 82), 12);
                LoadItemIcon(item);
                if (item.icon)
                {
                    const int icon = g_settings.iconSize;
                    const int iconX = tileRect.left + (tileSize - icon) / 2;
                    const int iconY = tileRect.top + 14;
                    DrawIconEx(dc, iconX, iconY, item.icon, icon, icon, 0, nullptr, DI_NORMAL);
                }

                SelectObject(dc, tileFont);
                RECT labelRect = { tileRect.left + 8, tileRect.top + g_settings.iconSize + 24, tileRect.right - 8, tileRect.bottom - 8 };
                DrawTextClipped(dc, item.displayName, labelRect, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS, RGB(238, 244, 252));
                g_hits.push_back({ tileRect, filteredIndex });
            }

            x += tileSize + gap;
            column++;
            if (column >= columns)
            {
                column = 0;
                x = left;
                y += tileSize + gap;
            }
            contentBottom = std::max(contentBottom, static_cast<int>(tileRect.bottom) + gap + g_scrollOffset);
        }

        g_contentHeight = contentBottom + 44;
    }

    DrawDragFeedback(dc);

    DeleteObject(searchFont);
    DeleteObject(tileFont);
    DeleteObject(headerFont);
}

void PaintWindow(HWND hwnd)
{
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

HWND CreateSettingsLabel(HWND parent, SettingsControlId id, int x, int y)
{
    return CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, x, y + 5, 150, 24, parent, reinterpret_cast<HMENU>(ControlId(id)), g_instance, nullptr);
}

HWND CreateSettingsEdit(HWND parent, SettingsControlId id, int x, int y, int width)
{
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x, y, width, 26, parent, reinterpret_cast<HMENU>(ControlId(id)), g_instance, nullptr);
}

HWND CreateSettingsCombo(HWND parent, SettingsControlId id, int x, int y, int width)
{
    return CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, x, y, width, 140, parent, reinterpret_cast<HMENU>(ControlId(id)), g_instance, nullptr);
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
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::DisplayModeLabel), IsChineseLanguage(language) ? L"打开方式" : L"Open mode");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::LanguageLabel), IsChineseLanguage(language) ? L"语言" : L"Language");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::SpotlightSizeLabel), IsChineseLanguage(language) ? L"聚焦窗口大小" : L"Spotlight size");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::AppSpacingLabel), IsChineseLanguage(language) ? L"APP 间距" : L"App spacing");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::WheelSensitivityLabel), IsChineseLanguage(language) ? L"滚轮灵敏度" : L"Wheel sensitivity");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::StartWithWindows), IsChineseLanguage(language) ? L"随 Windows 启动" : L"Start with Windows");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::Save), IsChineseLanguage(language) ? L"保存" : L"Save");
    SetDlgItemTextW(hwnd, ControlId(SettingsControlId::Cancel), IsChineseLanguage(language) ? L"取消" : L"Cancel");
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
    ApplyLanguageToSettingsWindow(hwnd);
}

void ReadSettingsWindow(HWND hwnd)
{
    g_settings.launchpadFolder = Trim(GetControlText(hwnd, ControlId(SettingsControlId::LaunchpadFolder)));
    g_settings.hotkey = Trim(GetControlText(hwnd, ControlId(SettingsControlId::Hotkey)));
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
    g_settings.spotlightWidth = NormalizeSpotlightWidth(_wtoi(GetControlText(hwnd, ControlId(SettingsControlId::SpotlightWidth)).c_str()));
    g_settings.spotlightHeight = NormalizeSpotlightHeight(_wtoi(GetControlText(hwnd, ControlId(SettingsControlId::SpotlightHeight)).c_str()));
    g_settings.appSpacing = NormalizeAppSpacing(_wtoi(GetControlText(hwnd, ControlId(SettingsControlId::AppSpacing)).c_str()));
    g_settings.wheelSensitivity = NormalizeWheelSensitivity(std::wcstod(GetControlText(hwnd, ControlId(SettingsControlId::WheelSensitivity)).c_str(), nullptr));
    g_settings.startWithWindows = SendDlgItemMessageW(hwnd, ControlId(SettingsControlId::StartWithWindows), BM_GETCHECK, 0, 0) == BST_CHECKED;
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
        CreateSettingsLabel(hwnd, SettingsControlId::DisplayModeLabel, labelX, y);
        CreateSettingsCombo(hwnd, SettingsControlId::DisplayMode, inputX, y, inputW);
        y += 38;
        CreateSettingsLabel(hwnd, SettingsControlId::LanguageLabel, labelX, y);
        CreateSettingsCombo(hwnd, SettingsControlId::Language, inputX, y, inputW);
        ResetLanguageCombo(hwnd);
        y += 38;
        CreateSettingsLabel(hwnd, SettingsControlId::SpotlightSizeLabel, labelX, y);
        CreateSettingsEdit(hwnd, SettingsControlId::SpotlightWidth, inputX, y, 96);
        CreateWindowW(L"STATIC", L"x", WS_CHILD | WS_VISIBLE | SS_CENTER, inputX + 106, y + 5, 24, 24, hwnd, nullptr, g_instance, nullptr);
        CreateSettingsEdit(hwnd, SettingsControlId::SpotlightHeight, inputX + 140, y, 96);
        y += 38;
        CreateSettingsLabel(hwnd, SettingsControlId::AppSpacingLabel, labelX, y);
        CreateSettingsEdit(hwnd, SettingsControlId::AppSpacing, inputX, y, 96);
        y += 38;
        CreateSettingsLabel(hwnd, SettingsControlId::WheelSensitivityLabel, labelX, y);
        CreateSettingsEdit(hwnd, SettingsControlId::WheelSensitivity, inputX, y, 96);
        y += 38;
        CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, inputX, y, inputW, 24, hwnd, reinterpret_cast<HMENU>(ControlId(SettingsControlId::StartWithWindows)), g_instance, nullptr);
        y += 48;
        CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, inputX + 146, y, 88, 30, hwnd, reinterpret_cast<HMENU>(ControlId(SettingsControlId::Save)), g_instance, nullptr);
        CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, inputX + 244, y, 88, 30, hwnd, reinterpret_cast<HMENU>(ControlId(SettingsControlId::Cancel)), g_instance, nullptr);
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
            ReadSettingsWindow(hwnd);
            SaveSettings();
            SetStartWithWindows(g_settings.startWithWindows);
            RegisterCurrentHotkey();
            ReloadData();
            RebuildFiltered();
            PositionWindow();
            InvalidateRect(g_hwnd, nullptr, TRUE);
            DestroyWindow(hwnd);
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
        380,
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
        CreateWindowW(L"STATIC", state->label.c_str(), WS_CHILD | WS_VISIBLE, 18, 18, 340, 24, hwnd, nullptr, g_instance, nullptr);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->value.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 18, 48, 340, 26, hwnd, reinterpret_cast<HMENU>(1), g_instance, nullptr);
        CreateWindowW(L"BUTTON", Text(L"OK", L"确定"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 166, 90, 88, 30, hwnd, reinterpret_cast<HMENU>(IDOK), g_instance, nullptr);
        CreateWindowW(L"BUTTON", Text(L"Cancel", L"取消"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 270, 90, 88, 30, hwnd, reinterpret_cast<HMENU>(IDCANCEL), g_instance, nullptr);
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
    AppendMenuW(menu, MF_STRING, ItemRemoveCommand, g_contextItemPaths.size() > 1 ? Text(L"Remove selected from layout", L"从布局移除已选") : Text(L"Remove from layout", L"从布局移除"));
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
    switch (message)
    {
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
            RemoveItemsFromLayout(g_contextItemPaths);
            SortItems();
            SaveLayout();
            RebuildFiltered();
            InvalidateRect(hwnd, nullptr, TRUE);
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
        if (lParam == WM_LBUTTONUP) ToggleNativeUi();
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu();
        return 0;
    case WM_PAINT:
        PaintWindow(hwnd);
        return 0;
    case WM_MOUSEWHEEL:
        g_scrollOffset -= static_cast<int>((GET_WHEEL_DELTA_WPARAM(wParam) / static_cast<double>(WHEEL_DELTA)) * 72.0 * g_settings.wheelSensitivity);
        ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
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
        ClearAllSelection();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
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
            if (g_dragActive) CancelDrag(hwnd);
            else HideNativeUi();
            return 0;
        }
        if (wParam == L'A' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            SelectAllItems();
            return 0;
        }
        if (wParam == VK_RETURN) { LaunchItemAtFilteredIndex(g_selectedFilteredIndex); return 0; }
        if (wParam == VK_DOWN && !g_filtered.empty())
        {
            g_selectedFilteredIndex = std::min(static_cast<int>(g_filtered.size()) - 1, g_selectedFilteredIndex + 1);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_UP && !g_filtered.empty())
        {
            g_selectedFilteredIndex = std::max(0, g_selectedFilteredIndex - 1);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    case WM_CHAR:
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
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_SIZE:
        ClampScroll();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && IsSpotlightMode() && IsWindowVisible(hwnd) && !g_settingsHwnd && g_modalDialogDepth == 0)
        {
            HideNativeUi();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != hwnd && g_dragActive)
        {
            ResetDragState();
        }
        return 0;
    case WM_DESTROY:
        RemoveTrayIcon();
        UnregisterHotKey(hwnd, HotkeyId);
        DestroyDirectRenderer();
        for (auto& item : g_items)
        {
            if (item.icon) DestroyIcon(item.icon);
            item.icon = nullptr;
        }
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
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"LightLaunchpad.NativeUi.SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
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
    return static_cast<int>(msg.wParam);
}
