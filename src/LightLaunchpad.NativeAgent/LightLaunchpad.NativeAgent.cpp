#include <windows.h>
#include <shellapi.h>

namespace
{
constexpr int HotkeyId = 0x4C4E;
constexpr int TrayOpenCommand = 1001;
constexpr int TrayExitCommand = 1002;
constexpr UINT WmTray = WM_APP + 42;
constexpr wchar_t WindowClassName[] = L"LightLaunchpadNativeAgentWindow";

HWND g_hwnd = nullptr;
HANDLE g_uiProcess = nullptr;
HANDLE g_uiJob = nullptr;
HANDLE g_showEvent = nullptr;
HANDLE g_exitEvent = nullptr;
wchar_t g_showEventName[128] = L"";
wchar_t g_exitEventName[128] = L"";

void CopyText(wchar_t* dest, size_t count, const wchar_t* source)
{
    if (count == 0) return;
    lstrcpynW(dest, source ? source : L"", static_cast<int>(count));
}

bool IsUiRunning()
{
    if (!g_uiProcess) return false;
    DWORD exitCode = 0;
    return GetExitCodeProcess(g_uiProcess, &exitCode) && exitCode == STILL_ACTIVE;
}

void ResetUiState()
{
    if (g_uiProcess)
    {
        CloseHandle(g_uiProcess);
        g_uiProcess = nullptr;
    }
    if (g_uiJob)
    {
        CloseHandle(g_uiJob);
        g_uiJob = nullptr;
    }
    if (g_showEvent)
    {
        CloseHandle(g_showEvent);
        g_showEvent = nullptr;
    }
    if (g_exitEvent)
    {
        CloseHandle(g_exitEvent);
        g_exitEvent = nullptr;
    }
    g_showEventName[0] = L'\0';
    g_exitEventName[0] = L'\0';
}

void BuildEventNames()
{
    wsprintfW(g_showEventName, L"Local\\LightLaunchpadNative_%lu_Show", GetCurrentProcessId());
    wsprintfW(g_exitEventName, L"Local\\LightLaunchpadNative_%lu_Exit", GetCurrentProcessId());
}

bool GetBaseDirectory(wchar_t* path, DWORD count)
{
    if (!GetModuleFileNameW(nullptr, path, count)) return false;
    for (int i = lstrlenW(path) - 1; i >= 0; --i)
    {
        if (path[i] == L'\\' || path[i] == L'/')
        {
            path[i + 1] = L'\0';
            return true;
        }
    }
    return false;
}

bool ResolveUiPath(wchar_t* uiPath, DWORD count)
{
    wchar_t base[MAX_PATH] = L"";
    if (!GetBaseDirectory(base, MAX_PATH)) return false;
    CopyText(uiPath, count, base);
    lstrcatW(uiPath, L"LightLaunchpad.App.exe");
    return GetFileAttributesW(uiPath) != INVALID_FILE_ATTRIBUTES;
}

HICON LoadTrayIcon()
{
    wchar_t base[MAX_PATH] = L"";
    if (GetBaseDirectory(base, MAX_PATH))
    {
        wchar_t iconPath[MAX_PATH] = L"";
        CopyText(iconPath, MAX_PATH, base);
        lstrcatW(iconPath, L"Assets\\LightLaunchpad.ico");
        if (GetFileAttributesW(iconPath) != INVALID_FILE_ATTRIBUTES)
        {
            HICON icon = reinterpret_cast<HICON>(
                LoadImageW(nullptr, iconPath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED));
            if (icon)
            {
                return icon;
            }
        }
    }

    return LoadIconW(nullptr, IDI_APPLICATION);
}

void ShowMessage(const wchar_t* message)
{
    MessageBoxW(nullptr, message, L"LightLaunchpad NativeAgent", MB_OK | MB_ICONINFORMATION);
}

void ShowUi()
{
    if (IsUiRunning())
    {
        if (g_showEvent) SetEvent(g_showEvent);
        return;
    }

    ResetUiState();
    BuildEventNames();
    g_showEvent = CreateEventW(nullptr, FALSE, FALSE, g_showEventName);
    g_exitEvent = CreateEventW(nullptr, FALSE, FALSE, g_exitEventName);
    if (!g_showEvent || !g_exitEvent)
    {
        ShowMessage(L"Could not create hosted UI activation events.");
        return;
    }

    wchar_t uiPath[MAX_PATH] = L"";
    if (!ResolveUiPath(uiPath, MAX_PATH))
    {
        ShowMessage(L"Could not find LightLaunchpad.App.exe next to the native agent.");
        return;
    }

    wchar_t commandLine[1024] = L"";
    wsprintfW(
        commandLine,
        L"\"%s\" --hosted-ui --show-event=\"%s\" --exit-event=\"%s\" --show-immediately --exit-on-hide",
        uiPath,
        g_showEventName,
        g_exitEventName);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(uiPath, commandLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
    {
        ShowMessage(L"Could not launch LightLaunchpad.App.exe.");
        return;
    }

    g_uiJob = CreateJobObjectW(nullptr, nullptr);
    if (g_uiJob)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_uiJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(g_uiJob, process.hProcess);
    }

    g_uiProcess = process.hProcess;
    CloseHandle(process.hThread);
    SetEvent(g_showEvent);
}

void StopUi()
{
    if (g_exitEvent) SetEvent(g_exitEvent);
    if (g_uiProcess)
    {
        WaitForSingleObject(g_uiProcess, 1500);
    }
    ResetUiState();
}

bool TryReadHotkeyFromSettings(wchar_t* hotkey, DWORD count)
{
    wchar_t appData[MAX_PATH] = L"";
    DWORD appDataLength = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (appDataLength == 0 || appDataLength >= MAX_PATH) return false;

    wchar_t settingsPath[MAX_PATH] = L"";
    CopyText(settingsPath, MAX_PATH, appData);
    lstrcatW(settingsPath, L"\\LightLaunchpad\\settings.json");

    HANDLE file = CreateFileW(settingsPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    char buffer[4096] = {};
    DWORD read = 0;
    ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
    CloseHandle(file);
    buffer[read] = '\0';

    const char* key = "\"Hotkey\"";
    char* found = strstr(buffer, key);
    if (!found) return false;
    found = strchr(found + lstrlenA(key), ':');
    if (!found) return false;
    found = strchr(found, '"');
    if (!found) return false;
    ++found;

    char value[64] = {};
    int i = 0;
    while (*found && *found != '"' && i < 63)
    {
        value[i++] = *found++;
    }
    value[i] = '\0';

    return MultiByteToWideChar(CP_UTF8, 0, value, -1, hotkey, static_cast<int>(count)) > 0;
}

void ResolveHotkey(UINT& modifiers, UINT& key)
{
    wchar_t hotkey[64] = L"Alt+D";
    TryReadHotkeyFromSettings(hotkey, 64);
    modifiers = 0;
    key = L'D';

    wchar_t normalized[64] = L"";
    CopyText(normalized, 64, hotkey);
    CharUpperBuffW(normalized, lstrlenW(normalized));

    if (wcsstr(normalized, L"ALT")) modifiers |= MOD_ALT;
    if (wcsstr(normalized, L"CTRL") || wcsstr(normalized, L"CONTROL")) modifiers |= MOD_CONTROL;
    if (wcsstr(normalized, L"SHIFT")) modifiers |= MOD_SHIFT;
    if (wcsstr(normalized, L"WIN") || wcsstr(normalized, L"WINDOWS")) modifiers |= MOD_WIN;

    wchar_t* plus = wcsrchr(normalized, L'+');
    wchar_t candidate = plus && plus[1] ? plus[1] : normalized[0];
    if ((candidate >= L'A' && candidate <= L'Z') || (candidate >= L'0' && candidate <= L'9'))
    {
        key = static_cast<UINT>(candidate);
    }

    if (modifiers == 0)
    {
        modifiers = MOD_ALT;
    }
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
    CopyText(nid.szTip, ARRAYSIZE(nid.szTip), L"LightLaunchpad");
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

void ShowTrayMenu()
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, TrayOpenCommand, L"Open launchpad");
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
        if (wParam == HotkeyId) ShowUi();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == TrayOpenCommand) ShowUi();
        if (LOWORD(wParam) == TrayExitCommand) DestroyWindow(hwnd);
        return 0;
    case WmTray:
        if (lParam == WM_LBUTTONUP) ShowUi();
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu();
        return 0;
    case WM_DESTROY:
        StopUi();
        RemoveTrayIcon();
        UnregisterHotKey(hwnd, HotkeyId);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"LightLaunchpad.NativeAgent.SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        return 0;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = WindowClassName;
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, WindowClassName, L"LightLaunchpad NativeAgent", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
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
