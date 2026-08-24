#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
constexpr wchar_t WindowClassName[] = L"LightLaunchpadMaterialEvidenceBackground";
constexpr UINT SelectCaseMessage = WM_APP + 73;
constexpr UINT FrameIdentityMessage = WM_APP + 74;

struct BackgroundCase
{
    const wchar_t* name;
    bool advancesPhase;
};

constexpr std::array<BackgroundCase, 12> Cases = { {
    { L"light-flat", false },
    { L"light-gradient", false },
    { L"light-low-contrast", false },
    { L"dark-flat", false },
    { L"dark-gradient", false },
    { L"dark-low-contrast", false },
    { L"text-grid", true },
    { L"checker-fine", false },
    { L"vertical-lines", false },
    { L"saturated-warm", false },
    { L"saturated-cool", true },
    { L"black-white-extreme", false }
} };

int g_caseIndex = 0;
int g_phase = 0;
bool g_capturePulse = false;
std::uint64_t g_frameIdentity = 1;
std::vector<std::uint32_t> g_pixels;

std::uint8_t Byte(int value)
{
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

std::uint32_t Bgra(int red, int green, int blue)
{
    return static_cast<std::uint32_t>(Byte(blue))
        | (static_cast<std::uint32_t>(Byte(green)) << 8)
        | (static_cast<std::uint32_t>(Byte(red)) << 16);
}

std::uint32_t CasePixel(int index, int x, int y, int width, int height, int phase)
{
    if (g_capturePulse && x < 8 && y < 8) return Bgra(255, 0, 255);
    const int safeWidth = std::max(1, width - 1);
    const int safeHeight = std::max(1, height - 1);
    const int horizontal = x * 255 / safeWidth;
    const int vertical = y * 255 / safeHeight;
    switch (index)
    {
    case 0:
        return Bgra(234, 240, 248);
    case 1:
        return Bgra(212 + horizontal / 8, 222 + vertical / 10, 236 + horizontal / 16);
    case 2:
    {
        const int variation = ((x / 48) + (y / 48)) % 2 == 0 ? 3 : -3;
        return Bgra(211 + variation, 218 + variation, 226 + variation);
    }
    case 3:
        return Bgra(21, 29, 41);
    case 4:
        return Bgra(10 + horizontal / 12, 18 + vertical / 10, 30 + horizontal / 8);
    case 5:
    {
        const int variation = ((x / 40) + (y / 40)) % 2 == 0 ? 4 : -4;
        return Bgra(39 + variation, 45 + variation, 54 + variation);
    }
    case 6:
    {
        const int phaseX = (x + phase * 11) % 196;
        const int phaseY = (y + phase * 7) % 32;
        const bool grid = ((x + phase * 3) % 32) == 0 || ((y + phase * 2) % 32) == 0;
        const bool textStroke = phaseY >= 8 && phaseY <= 11 && phaseX >= 12 && phaseX < 174
            && ((phaseX / 13) % 3 != 1);
        if (textStroke) return Bgra(22, 31, 43);
        return grid ? Bgra(110, 126, 146) : Bgra(224, 230, 238);
    }
    case 7:
        return (((x / 4) + (y / 4)) % 2 == 0) ? Bgra(238, 242, 248) : Bgra(28, 37, 51);
    case 8:
        return ((x / 3) % 2 == 0) ? Bgra(235, 239, 246) : Bgra(31, 42, 58);
    case 9:
        return Bgra(214 + horizontal / 7, 48 + vertical / 6, 24 + (255 - horizontal) / 12);
    case 10:
    {
        const int band = ((x + phase * 17) / 24) % 4;
        return Bgra(18 + band * 7, 82 + vertical / 3, 168 + horizontal / 4 - band * 5);
    }
    case 11:
        if (x < width / 2 && y < height / 2) return Bgra(0, 0, 0);
        if (x >= width / 2 && y < height / 2) return Bgra(255, 255, 255);
        return (((x / 16) + (y / 16)) % 2 == 0) ? Bgra(0, 0, 0) : Bgra(255, 255, 255);
    default:
        return Bgra(255, 0, 255);
    }
}

void PaintBackground(HWND window, HDC destination)
{
    RECT client = {};
    if (!GetClientRect(window, &client)) return;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return;
    g_pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            g_pixels[static_cast<std::size_t>(y) * width + x] =
                CasePixel(g_caseIndex, x, y, width, height, g_phase);
        }
    }
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(
        destination,
        0,
        0,
        width,
        height,
        0,
        0,
        width,
        height,
        g_pixels.data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case SelectCaseMessage:
    {
        const int requested = static_cast<int>(wParam);
        if (requested < 0 || requested >= static_cast<int>(Cases.size())) return 0;
        if (requested != g_caseIndex)
        {
            g_caseIndex = requested;
            g_phase = 0;
            g_capturePulse = false;
        }
        else if (Cases[static_cast<std::size_t>(requested)].advancesPhase)
        {
            ++g_phase;
        }
        else
        {
            g_capturePulse = !g_capturePulse;
        }
        ++g_frameIdentity;
        SetWindowTextW(window, Cases[static_cast<std::size_t>(g_caseIndex)].name);
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
        return static_cast<LRESULT>(g_frameIdentity);
    }
    case FrameIdentityMessage:
        return static_cast<LRESULT>(g_frameIdentity);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window, &paint);
        PaintBackground(window, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

struct MonitorSelection
{
    std::wstring device = L"\\\\.\\DISPLAY1";
    RECT bounds = {};
    bool found = false;
};

BOOL CALLBACK SelectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
{
    auto* selection = reinterpret_cast<MonitorSelection*>(parameter);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)
        && selection->device == info.szDevice)
    {
        selection->bounds = info.rcMonitor;
        selection->found = true;
        return FALSE;
    }
    return TRUE;
}

int ArgumentCaseIndex()
{
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 0;
    int result = 0;
    for (int index = 1; index + 1 < count; ++index)
    {
        if (lstrcmpiW(arguments[index], L"--case") == 0)
        {
            result = _wtoi(arguments[index + 1]);
            break;
        }
    }
    LocalFree(arguments);
    return std::clamp(result, 0, static_cast<int>(Cases.size()) - 1);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return 2;
    g_caseIndex = ArgumentCaseIndex();
    MonitorSelection monitor;
    EnumDisplayMonitors(nullptr, nullptr, SelectMonitor, reinterpret_cast<LPARAM>(&monitor));
    if (!monitor.found) return 2;

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = WindowClassName;
    if (!RegisterClassExW(&windowClass)) return 2;

    const int width = monitor.bounds.right - monitor.bounds.left;
    const int height = monitor.bounds.bottom - monitor.bounds.top;
    HWND window = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        WindowClassName,
        Cases[static_cast<std::size_t>(g_caseIndex)].name,
        WS_POPUP,
        monitor.bounds.left,
        monitor.bounds.top,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!window) return 2;
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
