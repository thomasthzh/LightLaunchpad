#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <cwctype>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
constexpr int HotkeyId = 0x4C55;
constexpr int TrayOpenCommand = 1001;
constexpr int TrayRefreshCommand = 1002;
constexpr int TrayExitCommand = 1003;
constexpr UINT WmTray = WM_APP + 72;
constexpr wchar_t WindowClassName[] = L"LightLaunchpadNativeUiWindow";

struct Settings
{
    std::wstring launchpadFolder;
    std::wstring hotkey = L"Alt+D";
    std::wstring displayMode = L"Launchpad";
    int spotlightWidth = 980;
    int spotlightHeight = 720;
    int iconSize = 56;
    int wheelSensitivity = 1;
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

HINSTANCE g_instance = nullptr;
HWND g_hwnd = nullptr;
Settings g_settings;
std::vector<Region> g_regions;
std::vector<LaunchItem> g_items;
std::vector<int> g_filtered;
std::vector<HitTile> g_hits;
std::wstring g_searchText;
int g_selectedFilteredIndex = -1;
int g_scrollOffset = 0;
int g_contentHeight = 0;

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
    const auto iconSize = FindJsonStringValue(json, "IconSize");
    if (ContainsIgnoreCase(iconSize, L"Large")) g_settings.iconSize = 64;
    if (ContainsIgnoreCase(iconSize, L"Small")) g_settings.iconSize = 42;
    g_settings.spotlightWidth = FindJsonNumberValue(json, "SpotlightWidth", g_settings.spotlightWidth);
    g_settings.spotlightHeight = FindJsonNumberValue(json, "SpotlightHeight", g_settings.spotlightHeight);
    g_settings.wheelSensitivity = std::max(1, FindJsonNumberValue(json, "WheelSensitivity", g_settings.wheelSensitivity));
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

    const auto layoutPath = JoinPath(JoinPath(appData, L"LightLaunchpad"), L"layout.json");
    const auto json = ReadFileUtf8(layoutPath);
    if (json.empty())
    {
        AddDefaultRegion();
        return;
    }

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

void LoadItemIcon(LaunchItem& item)
{
    if (item.icon) return;
    SHFILEINFOW info = {};
    if (SHGetFileInfoW(
        item.sourcePath.c_str(),
        0,
        &info,
        sizeof(info),
        SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES))
    {
        item.icon = info.hIcon;
    }
}

HICON LoadTrayIcon()
{
    wchar_t base[MAX_PATH] = L"";
    if (GetModuleFileNameW(nullptr, base, MAX_PATH))
    {
        PathRemoveFileSpecW(base);
        auto iconPath = JoinPath(base, L"Assets\\Alice.ico");
        if (GetFileAttributesW(iconPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            HICON icon = reinterpret_cast<HICON>(
                LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED));
            if (icon) return icon;
        }
    }
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

void PositionWindow()
{
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const bool spotlight = ContainsIgnoreCase(g_settings.displayMode, L"Spotlight");
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

void PaintContent(HDC dc, const RECT& client)
{
    g_hits.clear();
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
    const int gap = 12;
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
                DrawTextClipped(dc, RegionName(activeRegion), headerRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, RGB(220, 232, 248));
                y += 38;
            }

            RECT tileRect = { x, y, x + tileSize, y + tileSize };
            if (tileRect.bottom >= 92 && tileRect.top <= client.bottom)
            {
                const bool selected = filteredIndex == g_selectedFilteredIndex;
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

    PaintContent(memoryDc, client);
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

void ShowTrayMenu()
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, TrayOpenCommand, L"Open launchpad");
    AppendMenuW(menu, MF_STRING, TrayRefreshCommand, L"Refresh");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayExitCommand, L"Exit");
    POINT point = {};
    GetCursorPos(&point);
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
        if (LOWORD(wParam) == TrayRefreshCommand) { ReloadData(); RebuildFiltered(); InvalidateRect(hwnd, nullptr, TRUE); }
        if (LOWORD(wParam) == TrayExitCommand) DestroyWindow(hwnd);
        return 0;
    case WmTray:
        if (lParam == WM_LBUTTONUP) ToggleNativeUi();
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu();
        return 0;
    case WM_PAINT:
        PaintWindow(hwnd);
        return 0;
    case WM_MOUSEWHEEL:
        g_scrollOffset -= GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 72 * g_settings.wheelSensitivity;
        ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        for (const auto& hit : g_hits)
        {
            if (PtInRect(&hit.bounds, point))
            {
                g_selectedFilteredIndex = hit.itemIndex;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
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
        if (wParam == VK_ESCAPE) { HideNativeUi(); return 0; }
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
    case WM_DESTROY:
        RemoveTrayIcon();
        UnregisterHotKey(hwnd, HotkeyId);
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

    UINT modifiers = 0;
    UINT key = 0;
    ResolveHotkey(modifiers, key);
    RegisterHotKey(g_hwnd, HotkeyId, modifiers, key);
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
