#include "SettingsStudio.h"

#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace llp::settings_ui
{
namespace
{

constexpr wchar_t kSettingsStudioClassName[] = L"LightLaunchpadSettingsStudioWindow";
constexpr UINT_PTR kStatsRefreshTimerId = 4101;
constexpr UINT_PTR kConfirmResetTimerId = 4102;
constexpr float kSidebarWidth = 232.0f;
constexpr float kMaxContentWidth = 720.0f;
constexpr float kCardRadius = 12.0f;

UINT QueryWindowDpi(HWND hwnd)
{
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn && hwnd)
    {
        const UINT d = fn(hwnd);
        if (d > 0) return d;
    }
    HDC hdc = GetDC(hwnd);
    const UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : 96;
    if (hdc) ReleaseDC(hwnd, hdc);
    return dpi > 0 ? dpi : 96;
}

template <typename T>
void SafeRelease(T*& ptr)
{
    if (ptr)
    {
        ptr->Release();
        ptr = nullptr;
    }
}

D2D1_COLOR_F Rgba(int r, int g, int b, float a = 1.0f)
{
    return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

D2D1_COLOR_F LerpColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return D2D1::ColorF(
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t);
}

struct Palette
{
    D2D1_COLOR_F sidebarBg;
    D2D1_COLOR_F canvasBg;
    D2D1_COLOR_F cardBg;
    D2D1_COLOR_F cardHoverBg;
    D2D1_COLOR_F cardBorder;
    D2D1_COLOR_F hairline;
    D2D1_COLOR_F sidebarDivider;
    D2D1_COLOR_F textPrimary;
    D2D1_COLOR_F textSecondary;
    D2D1_COLOR_F textMuted;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F accentHover;
    D2D1_COLOR_F accentSoft;
    D2D1_COLOR_F accentText;
    D2D1_COLOR_F navHover;
    D2D1_COLOR_F controlTrack;
    D2D1_COLOR_F segmentBg;
    D2D1_COLOR_F segmentActiveBg;
    D2D1_COLOR_F segmentActiveBorder;
    D2D1_COLOR_F buttonBg;
    D2D1_COLOR_F buttonHoverBg;
    D2D1_COLOR_F buttonBorder;
    D2D1_COLOR_F danger;
    D2D1_COLOR_F dangerSoft;
    D2D1_COLOR_F dangerBorder;
    D2D1_COLOR_F success;
    D2D1_COLOR_F keyCapBg;
    D2D1_COLOR_F keyCapBorder;
    D2D1_COLOR_F scrollbarThumb;
};

Palette MakePalette(bool dark)
{
    Palette p{};
    if (dark)
    {
        p.sidebarBg = Rgba(20, 23, 29);
        p.canvasBg = Rgba(26, 29, 36);
        p.cardBg = Rgba(34, 38, 48);
        p.cardHoverBg = Rgba(41, 46, 58);
        p.cardBorder = Rgba(255, 255, 255, 0.085f);
        p.hairline = Rgba(255, 255, 255, 0.055f);
        p.sidebarDivider = Rgba(255, 255, 255, 0.075f);
        p.textPrimary = Rgba(243, 245, 249);
        p.textSecondary = Rgba(150, 160, 178);
        p.textMuted = Rgba(114, 124, 142);
        p.accent = Rgba(59, 130, 246);
        p.accentHover = Rgba(82, 148, 255);
        p.accentSoft = Rgba(59, 130, 246, 0.16f);
        p.accentText = Rgba(147, 197, 253);
        p.navHover = Rgba(255, 255, 255, 0.055f);
        p.controlTrack = Rgba(62, 69, 86);
        p.segmentBg = Rgba(24, 27, 34);
        p.segmentActiveBg = Rgba(48, 54, 68);
        p.segmentActiveBorder = Rgba(255, 255, 255, 0.12f);
        p.buttonBg = Rgba(44, 50, 62);
        p.buttonHoverBg = Rgba(54, 61, 76);
        p.buttonBorder = Rgba(255, 255, 255, 0.10f);
        p.danger = Rgba(248, 113, 113);
        p.dangerSoft = Rgba(239, 68, 68, 0.14f);
        p.dangerBorder = Rgba(248, 113, 113, 0.35f);
        p.success = Rgba(52, 211, 153);
        p.keyCapBg = Rgba(40, 45, 56);
        p.keyCapBorder = Rgba(255, 255, 255, 0.11f);
        p.scrollbarThumb = Rgba(255, 255, 255, 0.22f);
    }
    else
    {
        p.sidebarBg = Rgba(242, 244, 247);
        p.canvasBg = Rgba(248, 249, 251);
        p.cardBg = Rgba(255, 255, 255);
        p.cardHoverBg = Rgba(247, 249, 252);
        p.cardBorder = Rgba(15, 23, 42, 0.085f);
        p.hairline = Rgba(15, 23, 42, 0.06f);
        p.sidebarDivider = Rgba(15, 23, 42, 0.075f);
        p.textPrimary = Rgba(17, 24, 39);
        p.textSecondary = Rgba(100, 116, 139);
        p.textMuted = Rgba(148, 163, 184);
        p.accent = Rgba(37, 99, 235);
        p.accentHover = Rgba(29, 78, 216);
        p.accentSoft = Rgba(37, 99, 235, 0.10f);
        p.accentText = Rgba(29, 78, 216);
        p.navHover = Rgba(15, 23, 42, 0.045f);
        p.controlTrack = Rgba(203, 213, 225);
        p.segmentBg = Rgba(238, 241, 246);
        p.segmentActiveBg = Rgba(255, 255, 255);
        p.segmentActiveBorder = Rgba(15, 23, 42, 0.09f);
        p.buttonBg = Rgba(248, 250, 252);
        p.buttonHoverBg = Rgba(241, 245, 249);
        p.buttonBorder = Rgba(15, 23, 42, 0.12f);
        p.danger = Rgba(220, 38, 38);
        p.dangerSoft = Rgba(239, 68, 68, 0.09f);
        p.dangerBorder = Rgba(220, 38, 38, 0.30f);
        p.success = Rgba(16, 185, 129);
        p.keyCapBg = Rgba(241, 245, 249);
        p.keyCapBorder = Rgba(15, 23, 42, 0.12f);
        p.scrollbarThumb = Rgba(15, 23, 42, 0.22f);
    }
    return p;
}

bool IsChinese(const std::wstring& lang)
{
    return lang.find(L"Chinese") != std::wstring::npos || lang.find(L"中文") != std::wstring::npos;
}

std::wstring FormatComma(std::uint64_t v)
{
    std::wstring s = std::to_wstring(v);
    int insertPos = static_cast<int>(s.length()) - 3;
    while (insertPos > 0)
    {
        s.insert(static_cast<size_t>(insertPos), 1, L',');
        insertPos -= 3;
    }
    return s;
}

std::wstring FormatDistance(double px)
{
    const double meters = px / 3780.0;
    std::wostringstream oss;
    oss << std::fixed;
    if (meters >= 1000.0) oss << std::setprecision(2) << (meters / 1000.0) << L" km";
    else oss << std::setprecision(1) << meters << L" m";
    return oss.str();
}

std::wstring FormatVkName(UINT vk)
{
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= '0' && vk <= '9') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= VK_F1 && vk <= VK_F12) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    switch (vk)
    {
    case VK_SPACE: return L"Space";
    case VK_RETURN: return L"Enter";
    case VK_TAB: return L"Tab";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_INSERT: return L"Insert";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_OEM_3: return L"`";
    case VK_OEM_MINUS: return L"-";
    case VK_OEM_PLUS: return L"=";
    case VK_OEM_4: return L"[";
    case VK_OEM_6: return L"]";
    case VK_OEM_5: return L"\\";
    case VK_OEM_1: return L";";
    case VK_OEM_7: return L"'";
    case VK_OEM_COMMA: return L",";
    case VK_OEM_PERIOD: return L".";
    case VK_OEM_2: return L"/";
    default: return L"";
    }
}

enum class HitKind
{
    None,
    NavItem,
    Toggle,
    Segment,
    Slider,
    Hotkey,
    Button,
    HeatmapKey
};

struct HitZone
{
    HitKind kind = HitKind::None;
    std::string id;
    int index = 0;
    D2D1_RECT_F rect{};
    int minVal = 0;
    int maxVal = 100;
    std::wstring extraLabel;
    std::uint64_t extraCount = 0;
};

struct KeyDef
{
    const wchar_t* label;
    int vk;
    float widthUnits;
};

static const std::vector<std::vector<KeyDef>> kAnsiRows = {
    {
        {L"`", VK_OEM_3, 1.0f}, {L"1", '1', 1.0f}, {L"2", '2', 1.0f}, {L"3", '3', 1.0f}, {L"4", '4', 1.0f},
        {L"5", '5', 1.0f}, {L"6", '6', 1.0f}, {L"7", '7', 1.0f}, {L"8", '8', 1.0f}, {L"9", '9', 1.0f},
        {L"0", '0', 1.0f}, {L"-", VK_OEM_MINUS, 1.0f}, {L"=", VK_OEM_PLUS, 1.0f}, {L"Bksp", VK_BACK, 2.0f}
    },
    {
        {L"Tab", VK_TAB, 1.5f}, {L"Q", 'Q', 1.0f}, {L"W", 'W', 1.0f}, {L"E", 'E', 1.0f}, {L"R", 'R', 1.0f},
        {L"T", 'T', 1.0f}, {L"Y", 'Y', 1.0f}, {L"U", 'U', 1.0f}, {L"I", 'I', 1.0f}, {L"O", 'O', 1.0f},
        {L"P", 'P', 1.0f}, {L"[", VK_OEM_4, 1.0f}, {L"]", VK_OEM_6, 1.0f}, {L"\\", VK_OEM_5, 1.5f}
    },
    {
        {L"Caps", VK_CAPITAL, 1.75f}, {L"A", 'A', 1.0f}, {L"S", 'S', 1.0f}, {L"D", 'D', 1.0f}, {L"F", 'F', 1.0f},
        {L"G", 'G', 1.0f}, {L"H", 'H', 1.0f}, {L"J", 'J', 1.0f}, {L"K", 'K', 1.0f}, {L"L", 'L', 1.0f},
        {L";", VK_OEM_1, 1.0f}, {L"'", VK_OEM_7, 1.0f}, {L"Enter", VK_RETURN, 2.25f}
    },
    {
        {L"Shift", VK_LSHIFT, 2.25f}, {L"Z", 'Z', 1.0f}, {L"X", 'X', 1.0f}, {L"C", 'C', 1.0f}, {L"V", 'V', 1.0f},
        {L"B", 'B', 1.0f}, {L"N", 'N', 1.0f}, {L"M", 'M', 1.0f}, {L",", VK_OEM_COMMA, 1.0f}, {L".", VK_OEM_PERIOD, 1.0f},
        {L"/", VK_OEM_2, 1.0f}, {L"Shift", VK_RSHIFT, 2.75f}
    },
    {
        {L"Ctrl", VK_LCONTROL, 1.25f}, {L"Win", VK_LWIN, 1.25f}, {L"Alt", VK_LMENU, 1.25f},
        {L"Space", VK_SPACE, 6.25f},
        {L"Alt", VK_RMENU, 1.25f}, {L"Ctrl", VK_RCONTROL, 1.25f},
        {L"←", VK_LEFT, 0.83f}, {L"↑", VK_UP, 0.84f}, {L"↓", VK_DOWN, 0.84f}, {L"→", VK_RIGHT, 0.84f}
    }
};

struct StudioState
{
    HWND hwnd = nullptr;
    SettingsCallbacks callbacks;
    SettingsViewModel vm;
    llp::stats::Snapshot stats;

    ID2D1Factory* d2dFactory = nullptr;
    ID2D1HwndRenderTarget* hwndRt = nullptr;
    ID2D1RenderTarget* rt = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;

    IDWriteTextFormat* fmtBrand = nullptr;
    IDWriteTextFormat* fmtNav = nullptr;
    IDWriteTextFormat* fmtNavActive = nullptr;
    IDWriteTextFormat* fmtPageTitle = nullptr;
    IDWriteTextFormat* fmtPageSub = nullptr;
    IDWriteTextFormat* fmtSection = nullptr;
    IDWriteTextFormat* fmtRowTitle = nullptr;
    IDWriteTextFormat* fmtRowDesc = nullptr;
    IDWriteTextFormat* fmtControl = nullptr;
    IDWriteTextFormat* fmtSmall = nullptr;
    IDWriteTextFormat* fmtKpiVal = nullptr;
    IDWriteTextFormat* fmtKey = nullptr;
    IDWriteTextFormat* fmtCenter = nullptr;

    int activePage = 0;
    int statsRangeIndex = 0; // 0=Today, 1=7d, 2=30d
    float scrollY = 0.0f;
    float contentHeight = 0.0f;
    float viewportW = 960.0f;
    float viewportH = 680.0f;

    std::vector<HitZone> hitZones;
    std::string hoveredId;
    int hoveredIndex = -1;
    HitKind hoveredKind = HitKind::None;
    std::wstring hoveredKeyLabel;
    std::uint64_t hoveredKeyCount = 0;

    bool draggingSlider = false;
    std::string activeSliderId;
    D2D1_RECT_F activeSliderRect{};
    int activeSliderMin = 0;
    int activeSliderMax = 100;

    bool recordingHotkey = false;
    std::string pendingConfirmButtonId;
    bool statusIsError = false;
    std::wstring statusText;
};

void ReleaseTextFormats(StudioState* s)
{
    SafeRelease(s->fmtBrand);
    SafeRelease(s->fmtNav);
    SafeRelease(s->fmtNavActive);
    SafeRelease(s->fmtPageTitle);
    SafeRelease(s->fmtPageSub);
    SafeRelease(s->fmtSection);
    SafeRelease(s->fmtRowTitle);
    SafeRelease(s->fmtRowDesc);
    SafeRelease(s->fmtControl);
    SafeRelease(s->fmtSmall);
    SafeRelease(s->fmtKpiVal);
    SafeRelease(s->fmtKey);
    SafeRelease(s->fmtCenter);
}

void ReleaseDeviceResources(StudioState* s)
{
    SafeRelease(s->brush);
    s->rt = nullptr;
    SafeRelease(s->hwndRt);
}

IDWriteTextFormat* CreateFormat(IDWriteFactory* dw, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING)
{
    IDWriteTextFormat* fmt = nullptr;
    const wchar_t* family = L"Microsoft YaHei UI";
    if (SUCCEEDED(dw->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &fmt)))
    {
        fmt->SetTextAlignment(align);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim{};
        trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        IDWriteInlineObject* ellipsis = nullptr;
        if (SUCCEEDED(dw->CreateEllipsisTrimmingSign(fmt, &ellipsis)))
        {
            fmt->SetTrimming(&trim, ellipsis);
            ellipsis->Release();
        }
    }
    return fmt;
}

bool EnsureResources(StudioState* s)
{
    if (!s->d2dFactory)
    {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &s->d2dFactory))) return false;
    }
    if (!s->dwrite)
    {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&s->dwrite)))) return false;
        s->fmtBrand = CreateFormat(s->dwrite, 14.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        s->fmtNav = CreateFormat(s->dwrite, 13.0f, DWRITE_FONT_WEIGHT_NORMAL);
        s->fmtNavActive = CreateFormat(s->dwrite, 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        s->fmtPageTitle = CreateFormat(s->dwrite, 22.0f, DWRITE_FONT_WEIGHT_BOLD);
        s->fmtPageSub = CreateFormat(s->dwrite, 12.5f, DWRITE_FONT_WEIGHT_NORMAL);
        s->fmtSection = CreateFormat(s->dwrite, 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        s->fmtRowTitle = CreateFormat(s->dwrite, 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        s->fmtRowDesc = CreateFormat(s->dwrite, 11.5f, DWRITE_FONT_WEIGHT_NORMAL);
        s->fmtControl = CreateFormat(s->dwrite, 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        s->fmtSmall = CreateFormat(s->dwrite, 11.0f, DWRITE_FONT_WEIGHT_NORMAL);
        s->fmtKpiVal = CreateFormat(s->dwrite, 22.0f, DWRITE_FONT_WEIGHT_BOLD);
        s->fmtKey = CreateFormat(s->dwrite, 10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        s->fmtCenter = CreateFormat(s->dwrite, 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (!s->rt)
    {
        RECT rc{};
        GetClientRect(s->hwnd, &rc);
        const UINT w = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
        const UINT h = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(s->hwnd, D2D1::SizeU(w, h));
        if (FAILED(s->d2dFactory->CreateHwndRenderTarget(props, hwndProps, &s->hwndRt))) return false;
        s->rt = s->hwndRt;
        const UINT dpi = QueryWindowDpi(s->hwnd);
        s->rt->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        s->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        s->rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &s->brush);
    }
    return s->rt && s->brush && s->dwrite;
}

void ApplyDwmTheme(HWND hwnd, bool dark)
{
    const BOOL darkVal = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkVal, sizeof(darkVal));
    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
}

void DrawTextBox(StudioState* s, const std::wstring& text, IDWriteTextFormat* fmt, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color)
{
    if (!fmt || text.empty()) return;
    s->brush->SetColor(color);
    s->rt->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt, rect, s->brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void FillRoundRect(StudioState* s, const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& color)
{
    s->brush->SetColor(color);
    s->rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), s->brush);
}

void StrokeRoundRect(StudioState* s, const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& color, float width = 1.0f)
{
    s->brush->SetColor(color);
    s->rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), s->brush, width);
}

void DrawNavIcon(StudioState* s, int page, float cx, float cy, const D2D1_COLOR_F& color)
{
    s->brush->SetColor(color);
    const float sw = 1.55f;
    switch (page)
    {
    case 0: // General: Sliders / Controls
        s->rt->DrawLine(D2D1::Point2F(cx - 6.5f, cy - 3.5f), D2D1::Point2F(cx + 6.5f, cy - 3.5f), s->brush, sw);
        s->rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 2.0f, cy - 3.5f), 2.3f, 2.3f), s->brush);
        s->rt->DrawLine(D2D1::Point2F(cx - 6.5f, cy + 3.5f), D2D1::Point2F(cx + 6.5f, cy + 3.5f), s->brush, sw);
        s->rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 2.5f, cy + 3.5f), 2.3f, 2.3f), s->brush);
        break;
    case 1: // Appearance & Glass: Layered diamond / glass prism
        s->rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 6.5f, cy - 6.5f, cx + 2.5f, cy + 2.5f), 2.5f, 2.5f), s->brush, sw);
        s->rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 2.0f, cy - 2.0f, cx + 6.5f, cy + 6.5f), 2.5f, 2.5f), s->brush, sw);
        break;
    case 2: // Search & Placement: Magnifier
        s->rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 1.5f, cy - 1.5f), 4.8f, 4.8f), s->brush, sw);
        s->rt->DrawLine(D2D1::Point2F(cx + 2.2f, cy + 2.2f), D2D1::Point2F(cx + 6.2f, cy + 6.2f), s->brush, sw + 0.2f);
        break;
    case 3: // Hotwords: ">_" command prompt icon
        s->rt->DrawLine(D2D1::Point2F(cx - 5.5f, cy - 4.0f), D2D1::Point2F(cx - 1.0f, cy), s->brush, sw);
        s->rt->DrawLine(D2D1::Point2F(cx - 1.0f, cy), D2D1::Point2F(cx - 5.5f, cy + 4.0f), s->brush, sw);
        s->rt->DrawLine(D2D1::Point2F(cx + 0.5f, cy + 4.0f), D2D1::Point2F(cx + 6.0f, cy + 4.0f), s->brush, sw);
        break;
    case 4: // Folder
        s->rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 6.5f, cy - 4.0f, cx + 6.5f, cy + 5.5f), 2.0f, 2.0f), s->brush, sw);
        s->rt->DrawLine(D2D1::Point2F(cx - 6.0f, cy - 4.0f), D2D1::Point2F(cx - 1.5f, cy - 6.0f), s->brush, sw);
        break;
    case 5: // Input Stats: 3 ascending bars
        s->rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 6.0f, cy + 0.5f, cx - 3.2f, cy + 6.0f), 1.2f, 1.2f), s->brush);
        s->rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 1.4f, cy - 4.5f, cx + 1.4f, cy + 6.0f), 1.2f, 1.2f), s->brush);
        s->rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx + 3.2f, cy - 1.5f, cx + 6.0f, cy + 6.0f), 1.2f, 1.2f), s->brush);
        break;
    case 6: // Advanced: Diagnostic pulse / shield
        s->rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 6.5f, cy - 6.0f, cx + 6.5f, cy + 6.0f), 3.0f, 3.0f), s->brush, sw);
        s->rt->DrawLine(D2D1::Point2F(cx - 4.0f, cy + 0.5f), D2D1::Point2F(cx - 1.2f, cy + 0.5f), s->brush, sw);
        s->rt->DrawLine(D2D1::Point2F(cx - 1.2f, cy + 0.5f), D2D1::Point2F(cx + 0.2f, cy - 2.8f), s->brush, sw);
        s->rt->DrawLine(D2D1::Point2F(cx + 0.2f, cy - 2.8f), D2D1::Point2F(cx + 2.0f, cy + 2.5f), s->brush, sw);
        s->rt->DrawLine(D2D1::Point2F(cx + 2.0f, cy + 2.5f), D2D1::Point2F(cx + 4.2f, cy + 0.5f), s->brush, sw);
        break;
    }
}

void DrawBrandBadge(StudioState* s, float x, float y, float size, const Palette& pal)
{
    const D2D1_RECT_F badge = D2D1::RectF(x, y, x + size, y + size);
    FillRoundRect(s, badge, 8.0f, pal.accent);
    // 3x3 crisp white Launchpad dots inside
    const float pad = size * 0.24f;
    const float cell = (size - pad * 2.0f) / 3.0f;
    const float r = std::max(1.5f, cell * 0.28f);
    s->brush->SetColor(Rgba(255, 255, 255, 0.94f));
    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            const float cx = x + pad + (col + 0.5f) * cell;
            const float cy = y + pad + (row + 0.5f) * cell;
            s->rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - r, cy - r, cx + r, cy + r), 1.1f, 1.1f), s->brush);
        }
    }
}

D2D1_COLOR_F HeatColor(double ratio, bool dark)
{
    const D2D1_COLOR_F c0 = dark ? Rgba(42, 47, 58) : Rgba(236, 240, 246);
    const D2D1_COLOR_F c1 = dark ? Rgba(37, 99, 235, 0.55f) : Rgba(147, 197, 253);
    const D2D1_COLOR_F c2 = dark ? Rgba(14, 165, 233, 0.85f) : Rgba(56, 189, 248);
    const D2D1_COLOR_F c3 = dark ? Rgba(245, 158, 11, 0.92f) : Rgba(251, 191, 36);
    const D2D1_COLOR_F c4 = dark ? Rgba(239, 68, 68, 0.96f) : Rgba(239, 68, 68);
    if (ratio <= 0.001) return c0;
    const float t = static_cast<float>(std::clamp(ratio, 0.0, 1.0));
    if (t < 0.33f) return LerpColor(c1, c2, t / 0.33f);
    if (t < 0.66f) return LerpColor(c2, c3, (t - 0.33f) / 0.33f);
    return LerpColor(c3, c4, (t - 0.66f) / 0.34f);
}

// Helper for drawing a setting card row
struct CardBuilder
{
    StudioState* s;
    const Palette& pal;
    float left;
    float width;
    float& y;
    float cardStartY = 0.0f;
    int rowIndex = 0;

    void SectionLabel(const std::wstring& title)
    {
        DrawTextBox(s, title, s->fmtSection, D2D1::RectF(left + 4.0f, y, left + width, y + 20.0f), pal.textMuted);
        y += 24.0f;
    }

    void BeginCard(float totalHeight)
    {
        cardStartY = y;
        rowIndex = 0;
        const D2D1_RECT_F cardRect = D2D1::RectF(left, y, left + width, y + totalHeight);
        FillRoundRect(s, cardRect, kCardRadius, pal.cardBg);
        StrokeRoundRect(s, cardRect, kCardRadius, pal.cardBorder, 1.0f);
    }

    void EndCard(float totalHeight)
    {
        y = cardStartY + totalHeight + 18.0f;
    }

    void RowDivider(float rowTop)
    {
        if (rowIndex > 0)
        {
            s->brush->SetColor(pal.hairline);
            s->rt->DrawLine(D2D1::Point2F(left + 16.0f, rowTop), D2D1::Point2F(left + width - 16.0f, rowTop), s->brush, 1.0f);
        }
        ++rowIndex;
    }

    void DrawRowLabels(float rowTop, float rowH, float rightReservedW, const std::wstring& title, const std::wstring& desc)
    {
        const float textLeft = left + 18.0f;
        const float textRight = left + width - rightReservedW - 18.0f;
        if (desc.empty())
        {
            DrawTextBox(s, title, s->fmtRowTitle, D2D1::RectF(textLeft, rowTop, textRight, rowTop + rowH), pal.textPrimary);
        }
        else
        {
            DrawTextBox(s, title, s->fmtRowTitle, D2D1::RectF(textLeft, rowTop + 9.0f, textRight, rowTop + 31.0f), pal.textPrimary);
            DrawTextBox(s, desc, s->fmtRowDesc, D2D1::RectF(textLeft, rowTop + 31.0f, textRight, rowTop + rowH - 8.0f), pal.textSecondary);
        }
    }

    void ToggleRow(const std::string& id, float rowTop, float rowH, const std::wstring& title, const std::wstring& desc, bool checked)
    {
        RowDivider(rowTop);
        DrawRowLabels(rowTop, rowH, 68.0f, title, desc);

        const float swW = 44.0f;
        const float swH = 24.0f;
        const float sx = left + width - 18.0f - swW;
        const float sy = rowTop + (rowH - swH) * 0.5f;
        const D2D1_RECT_F swRect = D2D1::RectF(sx, sy, sx + swW, sy + swH);
        const bool hovered = (s->hoveredKind == HitKind::Toggle && s->hoveredId == id);
        const D2D1_COLOR_F track = checked ? (hovered ? pal.accentHover : pal.accent) : pal.controlTrack;
        FillRoundRect(s, swRect, swH * 0.5f, track);

        const float thumbR = 9.0f;
        const float thumbCx = checked ? (sx + swW - 12.0f) : (sx + 12.0f);
        const float thumbCy = sy + swH * 0.5f;
        s->brush->SetColor(Rgba(0, 0, 0, 0.16f));
        s->rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbCx, thumbCy + 1.0f), thumbR, thumbR), s->brush);
        s->brush->SetColor(Rgba(255, 255, 255, 0.98f));
        s->rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbCx, thumbCy), thumbR, thumbR), s->brush);

        s->hitZones.push_back({HitKind::Toggle, id, 0, D2D1::RectF(sx - 6.0f, rowTop + 6.0f, sx + swW + 6.0f, rowTop + rowH - 6.0f), 0, 0, L"", 0});
    }

    void SegmentedRow(const std::string& id, float rowTop, float rowH, const std::wstring& title, const std::wstring& desc, const std::vector<std::wstring>& options, int selectedIdx, float segWidth)
    {
        RowDivider(rowTop);
        DrawRowLabels(rowTop, rowH, segWidth + 24.0f, title, desc);

        const float segH = 32.0f;
        const float sx = left + width - 18.0f - segWidth;
        const float sy = rowTop + (rowH - segH) * 0.5f;
        const D2D1_RECT_F bgRect = D2D1::RectF(sx, sy, sx + segWidth, sy + segH);
        FillRoundRect(s, bgRect, 8.0f, pal.segmentBg);

        const int count = static_cast<int>(options.size());
        const float pad = 3.0f;
        const float itemW = (segWidth - pad * 2.0f) / static_cast<float>(std::max(1, count));
        for (int i = 0; i < count; ++i)
        {
            const D2D1_RECT_F ir = D2D1::RectF(sx + pad + i * itemW, sy + pad, sx + pad + (i + 1) * itemW, sy + segH - pad);
            const bool active = (i == selectedIdx);
            const bool hovered = (s->hoveredKind == HitKind::Segment && s->hoveredId == id && s->hoveredIndex == i);
            if (active)
            {
                FillRoundRect(s, ir, 6.0f, pal.segmentActiveBg);
                StrokeRoundRect(s, ir, 6.0f, pal.segmentActiveBorder, 1.0f);
            }
            else if (hovered)
            {
                FillRoundRect(s, ir, 6.0f, pal.navHover);
            }
            DrawTextBox(s, options[static_cast<size_t>(i)], s->fmtControl, ir, active ? pal.textPrimary : pal.textSecondary);
            s->hitZones.push_back({HitKind::Segment, id, i, ir, 0, 0, L"", 0});
        }
    }

    void SliderRow(const std::string& id, float rowTop, float rowH, const std::wstring& title, const std::wstring& desc, int value, int minVal, int maxVal, const std::wstring& valueText)
    {
        RowDivider(rowTop);
        const float controlAreaW = 248.0f;
        DrawRowLabels(rowTop, rowH, controlAreaW + 18.0f, title, desc);

        const float pillW = 58.0f;
        const float pillH = 24.0f;
        const float pillX = left + width - 18.0f - pillW;
        const float pillY = rowTop + (rowH - pillH) * 0.5f;
        const D2D1_RECT_F pillRect = D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH);
        FillRoundRect(s, pillRect, 6.0f, pal.segmentBg);
        DrawTextBox(s, valueText, s->fmtCenter, pillRect, pal.accentText);

        const float trackW = controlAreaW - pillW - 14.0f;
        const float trackX = pillX - 14.0f - trackW;
        const float cy = rowTop + rowH * 0.5f;
        const float trackH = 4.5f;
        const D2D1_RECT_F trackRect = D2D1::RectF(trackX, cy - trackH * 0.5f, trackX + trackW, cy + trackH * 0.5f);
        FillRoundRect(s, trackRect, trackH * 0.5f, pal.controlTrack);

        const float t = (maxVal > minVal) ? std::clamp(static_cast<float>(value - minVal) / static_cast<float>(maxVal - minVal), 0.0f, 1.0f) : 0.0f;
        const float thumbX = trackX + trackW * t;
        if (thumbX > trackX + 1.0f)
        {
            FillRoundRect(s, D2D1::RectF(trackX, cy - trackH * 0.5f, thumbX, cy + trackH * 0.5f), trackH * 0.5f, pal.accent);
        }

        const bool active = (s->draggingSlider && s->activeSliderId == id) || (s->hoveredKind == HitKind::Slider && s->hoveredId == id);
        const float thumbR = active ? 8.0f : 7.0f;
        s->brush->SetColor(Rgba(0, 0, 0, 0.22f));
        s->rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy + 1.0f), thumbR, thumbR), s->brush);
        s->brush->SetColor(Rgba(255, 255, 255, 0.98f));
        s->rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy), thumbR, thumbR), s->brush);
        s->brush->SetColor(pal.accent);
        s->rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy), thumbR, thumbR), s->brush, 1.5f);

        s->hitZones.push_back({HitKind::Slider, id, 0, D2D1::RectF(trackX - 8.0f, rowTop + 8.0f, trackX + trackW + 8.0f, rowTop + rowH - 8.0f), minVal, maxVal, L"", 0});
    }

    void HotkeyRow(const std::string& id, float rowTop, float rowH, const std::wstring& title, const std::wstring& desc, const std::wstring& hotkey, bool recording)
    {
        RowDivider(rowTop);
        const float pillW = 168.0f;
        const float pillH = 32.0f;
        DrawRowLabels(rowTop, rowH, pillW + 24.0f, title, desc);

        const float px = left + width - 18.0f - pillW;
        const float py = rowTop + (rowH - pillH) * 0.5f;
        const D2D1_RECT_F r = D2D1::RectF(px, py, px + pillW, py + pillH);
        const bool hovered = (s->hoveredKind == HitKind::Hotkey && s->hoveredId == id);
        FillRoundRect(s, r, 8.0f, recording ? pal.accentSoft : (hovered ? pal.buttonHoverBg : pal.keyCapBg));
        StrokeRoundRect(s, r, 8.0f, recording ? pal.accent : pal.keyCapBorder, recording ? 1.5f : 1.0f);

        const std::wstring label = recording
            ? (IsChinese(s->vm.language) ? L"请按下组合键 (Esc取消)" : L"Press shortcut (Esc)")
            : hotkey;
        DrawTextBox(s, label, s->fmtControl, r, recording ? pal.accentText : pal.textPrimary);
        s->hitZones.push_back({HitKind::Hotkey, id, 0, r, 0, 0, L"", 0});
    }

    enum class ButtonStyle { Primary, Secondary, Danger };

    void ActionRow(const std::string& id, float rowTop, float rowH, const std::wstring& title, const std::wstring& desc, const std::wstring& btnLabel, ButtonStyle style, float btnW = 124.0f)
    {
        RowDivider(rowTop);
        DrawRowLabels(rowTop, rowH, btnW + 24.0f, title, desc);

        const float btnH = 32.0f;
        const float bx = left + width - 18.0f - btnW;
        const float by = rowTop + (rowH - btnH) * 0.5f;
        const D2D1_RECT_F r = D2D1::RectF(bx, by, bx + btnW, by + btnH);
        const bool hovered = (s->hoveredKind == HitKind::Button && s->hoveredId == id);
        const bool confirming = (s->pendingConfirmButtonId == id);

        D2D1_COLOR_F bg = pal.buttonBg;
        D2D1_COLOR_F border = pal.buttonBorder;
        D2D1_COLOR_F fg = pal.textPrimary;
        if (style == ButtonStyle::Primary)
        {
            bg = hovered ? pal.accentHover : pal.accent;
            border = bg;
            fg = Rgba(255, 255, 255);
        }
        else if (style == ButtonStyle::Danger || confirming)
        {
            bg = confirming ? pal.danger : (hovered ? Rgba(239, 68, 68, 0.22f) : pal.dangerSoft);
            border = confirming ? pal.danger : pal.dangerBorder;
            fg = confirming ? Rgba(255, 255, 255) : pal.danger;
        }
        else if (hovered)
        {
            bg = pal.buttonHoverBg;
        }

        FillRoundRect(s, r, 8.0f, bg);
        StrokeRoundRect(s, r, 8.0f, border, 1.0f);
        const std::wstring text = confirming
            ? (IsChinese(s->vm.language) ? L"再次点击确认" : L"Click to confirm")
            : btnLabel;
        DrawTextBox(s, text, s->fmtControl, r, fg);
        s->hitZones.push_back({HitKind::Button, id, 0, r, 0, 0, L"", 0});
    }
};

std::wstring FormatFocusDuration(std::uint64_t secs, bool zh)
{
    if (secs < 60) return std::to_wstring(secs) + (zh ? L" 秒" : L"s");
    const std::uint64_t mins = secs / 60;
    if (mins < 60) return std::to_wstring(mins) + (zh ? L" 分钟" : L"m");
    const std::uint64_t hrs = mins / 60;
    const std::uint64_t rem = mins % 60;
    return std::to_wstring(hrs) + (zh ? L" 小时 " : L"h ") + std::to_wstring(rem) + (zh ? L" 分" : L"m");
}

void RenderInputStatsContent(StudioState* s, const Palette& pal, float left, float width, float& y, bool zh)
{
    // Range Switcher Pill: Today / 7 Days / 30 Days
    const float segW = 240.0f;
    const float segH = 30.0f;
    const D2D1_RECT_F segBg = D2D1::RectF(left, y, left + segW, y + segH);
    FillRoundRect(s, segBg, 8.0f, pal.segmentBg);
    const std::array<std::wstring, 3> rangeLabels = {
        zh ? L"今日" : L"Today",
        zh ? L"近 7 天" : L"7 Days",
        zh ? L"近 30 天" : L"30 Days"
    };
    const float segItemW = (segW - 6.0f) / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        const D2D1_RECT_F ir = D2D1::RectF(left + 3.0f + i * segItemW, y + 3.0f, left + 3.0f + (i + 1) * segItemW, y + segH - 3.0f);
        const bool active = (s->statsRangeIndex == i);
        if (active)
        {
            FillRoundRect(s, ir, 6.0f, pal.segmentActiveBg);
            StrokeRoundRect(s, ir, 6.0f, pal.segmentActiveBorder, 1.0f);
        }
        DrawTextBox(s, rangeLabels[static_cast<size_t>(i)], s->fmtControl, ir, active ? pal.textPrimary : pal.textSecondary);
        s->hitZones.push_back({HitKind::Segment, "statsRange", i, ir, 0, 0, L"", 0});
    }
    y += segH + 14.0f;

    // Aggregate DailyStats for selected range (1 / 7 / 30 days)
    llp::stats::DailyStats agg;
    const auto& recentAll = s->stats.recentDays;
    if (s->statsRangeIndex == 0)
    {
        agg = s->stats.today;
    }
    else
    {
        const size_t wantDays = (s->statsRangeIndex == 1) ? 7u : 30u;
        const size_t startI = recentAll.size() > wantDays ? (recentAll.size() - wantDays) : 0u;
        for (size_t di = startI; di < recentAll.size(); ++di)
        {
            const auto& d = recentAll[di];
            agg.keystrokes += d.keystrokes;
            agg.leftClicks += d.leftClicks;
            agg.rightClicks += d.rightClicks;
            agg.middleClicks += d.middleClicks;
            agg.wheelSteps += d.wheelSteps;
            agg.mouseDistancePx += d.mouseDistancePx;
            for (size_t vk = 0; vk < 256; ++vk) agg.keyCounts[vk] += d.keyCounts[vk];
            for (const auto& kv : d.appFocusSeconds) agg.appFocusSeconds[kv.first] += kv.second;
        }
    }
    // 4 KPI Cards first (content before controls!)
    const auto& today = agg;
    std::uint64_t total30d = 0;
    for (const auto& d : s->stats.recentDays) total30d += d.keystrokes;
    const std::uint64_t totalClicks = today.leftClicks + today.rightClicks + today.middleClicks;
    struct Kpi { std::wstring label; std::wstring val; std::wstring sub; D2D1_COLOR_F accent; };
    const std::array<Kpi, 4> kpis = {{
        {zh ? L"今日按键" : L"Keystrokes", FormatComma(today.keystrokes),
            zh ? (L"累计 " + FormatComma(total30d)) : (L"30d " + FormatComma(total30d)),
            Rgba(59, 130, 246)},
        {zh ? L"鼠标点击" : L"Mouse Clicks", FormatComma(totalClicks),
            (zh ? L"左 " : L"L ") + std::to_wstring(today.leftClicks) + (zh ? L" · 右 " : L" · R ") + std::to_wstring(today.rightClicks),
            Rgba(16, 185, 129)},
        {zh ? L"滚轮滚动" : L"Wheel Scrolls", FormatComma(today.wheelSteps),
            zh ? L"今日滚轮刻度" : L"Scroll notches today",
            Rgba(245, 158, 11)},
        {zh ? L"鼠标行程" : L"Mouse Distance", FormatDistance(today.mouseDistancePx),
            zh ? L"今日指针移动距离" : L"Cursor travel today",
            Rgba(168, 85, 247)}
    }};

    const float gap = 12.0f;
    const float kpiW = (width - gap * 3.0f) / 4.0f;
    const float kpiH = 88.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float kx = left + i * (kpiW + gap);
        const D2D1_RECT_F kr = D2D1::RectF(kx, y, kx + kpiW, y + kpiH);
        FillRoundRect(s, kr, kCardRadius, pal.cardBg);
        StrokeRoundRect(s, kr, kCardRadius, pal.cardBorder, 1.0f);
        // Subtle top accent bar
        FillRoundRect(s, D2D1::RectF(kx + 14.0f, y + 12.0f, kx + 34.0f, y + 15.5f), 1.8f, kpis[static_cast<size_t>(i)].accent);
        DrawTextBox(s, kpis[static_cast<size_t>(i)].label, s->fmtSmall, D2D1::RectF(kx + 14.0f, y + 20.0f, kx + kpiW - 10.0f, y + 36.0f), pal.textSecondary);
        DrawTextBox(s, kpis[static_cast<size_t>(i)].val, s->fmtKpiVal, D2D1::RectF(kx + 14.0f, y + 36.0f, kx + kpiW - 10.0f, y + 66.0f), pal.textPrimary);
        DrawTextBox(s, kpis[static_cast<size_t>(i)].sub, s->fmtSmall, D2D1::RectF(kx + 14.0f, y + 66.0f, kx + kpiW - 10.0f, y + 82.0f), pal.textMuted);
    }
    y += kpiH + 16.0f;

    // 2D ANSI Keyboard Heatmap Card
    const float hmH = 244.0f;
    const D2D1_RECT_F hmCard = D2D1::RectF(left, y, left + width, y + hmH);
    FillRoundRect(s, hmCard, kCardRadius, pal.cardBg);
    StrokeRoundRect(s, hmCard, kCardRadius, pal.cardBorder, 1.0f);

    DrawTextBox(s, zh ? L"今日按键热力图" : L"Today's Keyboard Heatmap", s->fmtRowTitle,
        D2D1::RectF(left + 18.0f, y + 12.0f, left + 260.0f, y + 36.0f), pal.textPrimary);

    // Hover readout or legend on right of header
    if (!s->hoveredKeyLabel.empty())
    {
        const std::wstring tip = s->hoveredKeyLabel + L"  ·  " + FormatComma(s->hoveredKeyCount) + (zh ? L" 次" : L" presses");
        const D2D1_RECT_F tipRect = D2D1::RectF(left + width - 196.0f, y + 13.0f, left + width - 18.0f, y + 35.0f);
        FillRoundRect(s, tipRect, 6.0f, pal.accentSoft);
        DrawTextBox(s, tip, s->fmtCenter, tipRect, pal.accentText);
    }
    else
    {
        const float legX = left + width - 144.0f;
        const float legY = y + 19.0f;
        DrawTextBox(s, zh ? L"低" : L"Low", s->fmtSmall, D2D1::RectF(legX - 36.0f, legY - 4.0f, legX - 6.0f, legY + 14.0f), pal.textMuted);
        for (int b = 0; b < 5; ++b)
        {
            const float bx = legX + b * 17.0f;
            FillRoundRect(s, D2D1::RectF(bx, legY, bx + 13.0f, legY + 10.0f), 3.0f, HeatColor(b == 0 ? 0.0 : b / 4.0, s->vm.darkMode));
        }
        DrawTextBox(s, zh ? L"高" : L"High", s->fmtSmall, D2D1::RectF(legX + 90.0f, legY - 4.0f, legX + 130.0f, legY + 14.0f), pal.textMuted);
    }

    std::uint64_t maxKey = 1;
    for (std::uint64_t c : today.keyCounts) maxKey = std::max(maxKey, c);
    const double logMax = std::log1p(static_cast<double>(maxKey));

    const float kbPadX = 18.0f;
    const float kbTop = y + 46.0f;
    const float kbW = width - kbPadX * 2.0f;
    const float kbH = hmH - 62.0f;
    const float rowGap = 5.0f;
    const float keyGap = 4.5f;
    const float rowH = (kbH - rowGap * 4.0f) / 5.0f;

    for (size_t r = 0; r < kAnsiRows.size(); ++r)
    {
        const auto& row = kAnsiRows[r];
        float totalUnits = 0.0f;
        for (const auto& k : row) totalUnits += k.widthUnits;
        const float availW = kbW - keyGap * static_cast<float>(row.size() - 1);
        const float unitW = availW / totalUnits;
        float kx = left + kbPadX;
        const float ky = kbTop + static_cast<float>(r) * (rowH + rowGap);

        for (const auto& k : row)
        {
            const float kw = unitW * k.widthUnits;
            const D2D1_RECT_F kr = D2D1::RectF(kx, ky, kx + kw, ky + rowH);
            std::uint64_t count = (k.vk >= 0 && k.vk < 256) ? today.keyCounts[static_cast<size_t>(k.vk)] : 0;
            if (k.vk == VK_LSHIFT) count += today.keyCounts[VK_SHIFT];
            if (k.vk == VK_LCONTROL) count += today.keyCounts[VK_CONTROL];
            if (k.vk == VK_LMENU) count += today.keyCounts[VK_MENU];
            const double ratio = (count > 0 && logMax > 0.0) ? (std::log1p(static_cast<double>(count)) / logMax) : 0.0;
            const bool isHovered = (s->hoveredKind == HitKind::HeatmapKey && s->hoveredIndex == k.vk);

            FillRoundRect(s, kr, 6.0f, HeatColor(ratio, s->vm.darkMode));
            StrokeRoundRect(s, kr, 6.0f, isHovered ? pal.accent : pal.cardBorder, isHovered ? 1.5f : 1.0f);
            const D2D1_COLOR_F fg = (ratio > 0.35) ? Rgba(255, 255, 255) : pal.textPrimary;
            DrawTextBox(s, k.label, s->fmtKey, kr, fg);
            s->hitZones.push_back({HitKind::HeatmapKey, "key", k.vk, kr, 0, 0, k.label, count});
            kx += kw + keyGap;
        }
    }
    y += hmH + 18.0f;

    // 7-Day Daily Trend Card
    const float trH = 186.0f;
    const D2D1_RECT_F trCard = D2D1::RectF(left, y, left + width, y + trH);
    FillRoundRect(s, trCard, kCardRadius, pal.cardBg);
    StrokeRoundRect(s, trCard, kCardRadius, pal.cardBorder, 1.0f);
    DrawTextBox(s, zh ? L"近 7 天按键趋势" : L"7-Day Keystroke Trend", s->fmtRowTitle,
        D2D1::RectF(left + 18.0f, y + 12.0f, left + 260.0f, y + 36.0f), pal.textPrimary);

    const auto& recent = s->stats.recentDays;
    const size_t nDays = std::min<size_t>(7, recent.size());
    const size_t startIdx = recent.size() > nDays ? (recent.size() - nDays) : 0;
    std::uint64_t maxBar = 1;
    for (size_t i = startIdx; i < recent.size(); ++i) maxBar = std::max(maxBar, recent[i].keystrokes);

    const float chartL = left + 24.0f;
    const float chartR = left + width - 24.0f;
    const float chartTop = y + 48.0f;
    const float chartBot = y + trH - 30.0f;
    const float chartH = chartBot - chartTop;

    // Subtle horizontal gridlines
    s->brush->SetColor(pal.hairline);
    for (int g = 0; g <= 2; ++g)
    {
        const float gy = chartTop + chartH * (g / 2.0f);
        s->rt->DrawLine(D2D1::Point2F(chartL, gy), D2D1::Point2F(chartR, gy), s->brush, 1.0f);
    }

    if (nDays > 0)
    {
        const float slotW = (chartR - chartL) / static_cast<float>(nDays);
        const float barW = std::min(44.0f, slotW * 0.46f);
        for (size_t i = 0; i < nDays; ++i)
        {
            const auto& d = recent[startIdx + i];
            const float cx = chartL + (static_cast<float>(i) + 0.5f) * slotW;
            const float frac = static_cast<float>(d.keystrokes) / static_cast<float>(maxBar);
            const float bh = std::max(4.0f, frac * (chartH - 18.0f));
            const D2D1_RECT_F br = D2D1::RectF(cx - barW * 0.5f, chartBot - bh, cx + barW * 0.5f, chartBot);
            const bool isToday = (i == nDays - 1);
            FillRoundRect(s, br, 5.0f, isToday ? pal.accent : Rgba(59, 130, 246, s->vm.darkMode ? 0.42f : 0.32f));

            if (d.keystrokes > 0)
            {
                DrawTextBox(s, FormatComma(d.keystrokes), s->fmtCenter,
                    D2D1::RectF(cx - slotW * 0.5f, chartBot - bh - 18.0f, cx + slotW * 0.5f, chartBot - bh - 2.0f),
                    isToday ? pal.accentText : pal.textSecondary);
            }
            std::wstring dateLabel;
            if (d.date.size() >= 10)
            {
                dateLabel.assign(d.date.begin() + 5, d.date.end());
            }
            DrawTextBox(s, dateLabel, s->fmtCenter,
                D2D1::RectF(cx - slotW * 0.5f, chartBot + 4.0f, cx + slotW * 0.5f, chartBot + 22.0f),
                isToday ? pal.textPrimary : pal.textMuted);
        }
    }
    y += trH + 18.0f;

    // Top Foreground Apps Focus Time Card (zero-polling SetWinEventHook)
    std::vector<std::pair<std::string, std::uint64_t>> topApps(today.appFocusSeconds.begin(), today.appFocusSeconds.end());
    std::sort(topApps.begin(), topApps.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (topApps.size() > 6) topApps.resize(6);

    const float appsH = topApps.empty() ? 88.0f : (52.0f + static_cast<float>(topApps.size()) * 32.0f + 12.0f);
    const D2D1_RECT_F appsCard = D2D1::RectF(left, y, left + width, y + appsH);
    FillRoundRect(s, appsCard, kCardRadius, pal.cardBg);
    StrokeRoundRect(s, appsCard, kCardRadius, pal.cardBorder, 1.0f);
    DrawTextBox(s, zh ? L"应用前台专注时长排行" : L"Foreground App Focus Time", s->fmtRowTitle,
        D2D1::RectF(left + 18.0f, y + 12.0f, left + 300.0f, y + 36.0f), pal.textPrimary);

    if (topApps.empty())
    {
        DrawTextBox(s, zh ? L"切换使用其他窗口后将自动统计各应用前台活跃时长（仅记录进程名，不记窗口标题）" : L"Switch between apps to record active focus time (process name only, never titles)",
            s->fmtSmall, D2D1::RectF(left + 18.0f, y + 42.0f, left + width - 18.0f, y + 72.0f), pal.textMuted);
    }
    else
    {
        const std::uint64_t maxSec = std::max<std::uint64_t>(1, topApps[0].second);
        std::uint64_t totalSec = 0;
        for (const auto& kv : today.appFocusSeconds) totalSec += kv.second;
        totalSec = std::max<std::uint64_t>(1, totalSec);

        const std::array<D2D1_COLOR_F, 6> barColors = {
            Rgba(59, 130, 246), Rgba(16, 185, 129), Rgba(245, 158, 11),
            Rgba(168, 85, 247), Rgba(14, 165, 233), Rgba(236, 72, 153)
        };
        for (size_t i = 0; i < topApps.size(); ++i)
        {
            const float ry = y + 48.0f + static_cast<float>(i) * 32.0f;
            std::wstring appW(topApps[i].first.begin(), topApps[i].first.end());
            DrawTextBox(s, appW, s->fmtSmall, D2D1::RectF(left + 18.0f, ry, left + 150.0f, ry + 24.0f), pal.textPrimary);

            const float barL = left + 156.0f;
            const float barMaxW = std::max(80.0f, width - 290.0f);
            const float frac = static_cast<float>(topApps[i].second) / static_cast<float>(maxSec);
            FillRoundRect(s, D2D1::RectF(barL, ry + 7.0f, barL + barMaxW, ry + 17.0f), 5.0f, pal.segmentBg);
            FillRoundRect(s, D2D1::RectF(barL, ry + 7.0f, barL + std::max(8.0f, barMaxW * frac), ry + 17.0f), 5.0f, barColors[i % barColors.size()]);

            const int pct = static_cast<int>(std::lround(100.0 * static_cast<double>(topApps[i].second) / static_cast<double>(totalSec)));
            const std::wstring dur = FormatFocusDuration(topApps[i].second, zh) + L" (" + std::to_wstring(pct) + L"%)";
            DrawTextBox(s, dur, s->fmtSmall, D2D1::RectF(barL + barMaxW + 12.0f, ry, left + width - 14.0f, ry + 24.0f), pal.textSecondary);
        }
    }
    y += appsH + 18.0f;

    // Privacy & Tracking controls below the dashboard
    CardBuilder cb{s, pal, left, width, y};
    cb.SectionLabel(zh ? L"隐私与记录控制" : L"PRIVACY & TRACKING");
    cb.BeginCard(120.0f);
    cb.ToggleRow("enableInputStats", y, 60.0f,
        zh ? L"启用本地键鼠统计" : L"Enable local keyboard & mouse stats",
        zh ? L"仅本地聚合计数，绝不记录输入文本、按键顺序或剪贴板" : L"Raw Input aggregate counts only; never logs text or key sequences",
        s->vm.enableInputStats);
    cb.ActionRow("clearInputStats", y + 60.0f, 60.0f,
        zh ? L"清空历史统计数据" : L"Clear recorded statistics",
        zh ? L"删除本地 input-stats.json 中的今日与近 30 天聚合计数" : L"Remove all locally stored daily counts and key heatmaps",
        zh ? L"清空数据" : L"Clear Stats",
        CardBuilder::ButtonStyle::Danger, 118.0f);
    cb.EndCard(120.0f);
}

void PaintStudio(StudioState* s)
{
    if (!EnsureResources(s)) return;
    const Palette pal = MakePalette(s->vm.darkMode);
    const bool zh = IsChinese(s->vm.language);

    const D2D1_SIZE_F size = s->rt->GetSize();
    s->viewportW = size.width;
    s->viewportH = size.height;
    s->hitZones.clear();

    s->rt->BeginDraw();
    s->rt->Clear(pal.canvasBg);

    // 1. Sidebar Surface
    const D2D1_RECT_F sidebarRect = D2D1::RectF(0.0f, 0.0f, kSidebarWidth, size.height);
    s->brush->SetColor(pal.sidebarBg);
    s->rt->FillRectangle(sidebarRect, s->brush);
    s->brush->SetColor(pal.sidebarDivider);
    s->rt->DrawLine(D2D1::Point2F(kSidebarWidth, 0.0f), D2D1::Point2F(kSidebarWidth, size.height), s->brush, 1.0f);

    // Brand Header
    DrawBrandBadge(s, 18.0f, 18.0f, 30.0f, pal);
    DrawTextBox(s, L"LightLaunchpad", s->fmtBrand, D2D1::RectF(56.0f, 16.0f, kSidebarWidth - 14.0f, 36.0f), pal.textPrimary);
    DrawTextBox(s, s->vm.versionLabel + (zh ? L" · 设置中心" : L" · Settings"), s->fmtSmall, D2D1::RectF(56.0f, 34.0f, kSidebarWidth - 14.0f, 50.0f), pal.textMuted);

    // Nav Items
    const std::array<std::wstring, 7> navLabels = {
        zh ? L"常规" : L"General",
        zh ? L"外观与玻璃" : L"Appearance & Glass",
        zh ? L"搜索与定位" : L"Search & Placement",
        zh ? L"热词指令" : L"Hotwords",
        zh ? L"启动台目录" : L"Launchpad Folder",
        zh ? L"键鼠统计" : L"Input Stats",
        zh ? L"高级与诊断" : L"Advanced"
    };

    float ny = 70.0f;
    for (int i = 0; i < 7; ++i)
    {
        const D2D1_RECT_F itemRect = D2D1::RectF(12.0f, ny, kSidebarWidth - 12.0f, ny + 38.0f);
        const bool active = (i == s->activePage);
        const bool hovered = (s->hoveredKind == HitKind::NavItem && s->hoveredIndex == i);
        if (active)
        {
            FillRoundRect(s, itemRect, 8.0f, pal.accentSoft);
            FillRoundRect(s, D2D1::RectF(itemRect.left + 2.0f, itemRect.top + 9.0f, itemRect.left + 5.0f, itemRect.bottom - 9.0f), 1.5f, pal.accent);
        }
        else if (hovered)
        {
            FillRoundRect(s, itemRect, 8.0f, pal.navHover);
        }
        const D2D1_COLOR_F iconColor = active ? pal.accent : pal.textSecondary;
        const D2D1_COLOR_F labelColor = active ? pal.textPrimary : pal.textSecondary;
        DrawNavIcon(s, i, itemRect.left + 21.0f, (itemRect.top + itemRect.bottom) * 0.5f, iconColor);
        DrawTextBox(s, navLabels[static_cast<size_t>(i)], active ? s->fmtNavActive : s->fmtNav,
            D2D1::RectF(itemRect.left + 38.0f, itemRect.top, itemRect.right - 8.0f, itemRect.bottom), labelColor);
        s->hitZones.push_back({HitKind::NavItem, "nav", i, itemRect, 0, 0, L"", 0});
        ny += 44.0f;
    }

    // Sidebar Footer Status Pill
    const D2D1_RECT_F statusRect = D2D1::RectF(12.0f, size.height - 52.0f, kSidebarWidth - 12.0f, size.height - 14.0f);
    FillRoundRect(s, statusRect, 8.0f, pal.cardBg);
    StrokeRoundRect(s, statusRect, 8.0f, pal.cardBorder, 1.0f);
    s->brush->SetColor(s->statusIsError ? pal.danger : pal.success);
    s->rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(statusRect.left + 13.0f, (statusRect.top + statusRect.bottom) * 0.5f), 3.5f, 3.5f), s->brush);
    const std::wstring footerMsg = !s->statusText.empty()
        ? s->statusText
        : ((zh ? L"实时生效 · " : L"Live saved · ") + s->vm.hotkey);
    DrawTextBox(s, footerMsg, s->fmtSmall, D2D1::RectF(statusRect.left + 24.0f, statusRect.top, statusRect.right - 8.0f, statusRect.bottom), pal.textSecondary);

    // 2. Content Viewport (clipped + scrolled)
    const D2D1_RECT_F clipRect = D2D1::RectF(kSidebarWidth + 1.0f, 0.0f, size.width, size.height);
    s->rt->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const float availW = std::max(320.0f, size.width - kSidebarWidth - 64.0f);
    const float colW = std::min(kMaxContentWidth, availW);
    const float colLeft = kSidebarWidth + 32.0f + std::max(0.0f, (availW - colW) * 0.35f);
    float y = 26.0f - s->scrollY;
    const float startY = y;

    // Page Title & Subtitle
    const std::array<std::wstring, 7> pageTitles = {
        zh ? L"常规" : L"General",
        zh ? L"外观与液态玻璃" : L"Appearance & Liquid Glass",
        zh ? L"搜索与窗口定位" : L"Search & Placement",
        zh ? L"热词指令与计算器" : L"Hotwords & Calculator",
        zh ? L"启动台目录" : L"Launchpad Folder",
        zh ? L"键鼠统计" : L"Keyboard & Mouse Stats",
        zh ? L"高级与诊断" : L"Advanced & Diagnostics"
    };
    const std::array<std::wstring, 7> pageSubs = {
        zh ? L"全局唤醒热键、默认显示模式、语言与启动行为（所有修改即时生效）" : L"Global hotkey, default launch mode, language, and startup behavior",
        zh ? L"调整 macOS 26 液态玻璃折射、模糊、高光与聚焦窗口几何尺寸" : L"Tune Liquid Glass optical clarity, refraction meniscus, rim glow, and sizing",
        zh ? L"控制多屏鼠标跟随偏移、滚轮手感与 Everything 本机文件联合检索" : L"Configure cursor-monitor placement bias, scroll feel, and Everything search",
        zh ? L"输入 >热词 零冲突执行系统动作/网页/脚本；支持精确无前缀触发与即时计算器" : L"Type >trigger for zero-conflict commands, web searches, or inline math",
        zh ? L"管理本地快捷方式目录，支持一键导入开始菜单与桌面应用" : L"Manage your Launchpad shortcut folder and import Start Menu / Desktop apps",
        zh ? L"纯本地聚合统计每日敲击、鼠标点击、滚轮与移动行程，保护隐私不记明文" : L"Local-only aggregate daily counts, 2D keyboard heatmap, and 7-day trends",
        zh ? L"查看运行状态、导出诊断报告或一键恢复推荐默认配置" : L"Inspect runtime diagnostics, open data folder, or restore defaults"
    };
    const int pIdx = std::clamp(s->activePage, 0, 6);
    DrawTextBox(s, pageTitles[static_cast<size_t>(pIdx)], s->fmtPageTitle, D2D1::RectF(colLeft, y, colLeft + colW, y + 32.0f), pal.textPrimary);
    y += 32.0f;
    DrawTextBox(s, pageSubs[static_cast<size_t>(pIdx)], s->fmtPageSub, D2D1::RectF(colLeft, y, colLeft + colW, y + 22.0f), pal.textSecondary);
    y += 34.0f;

    CardBuilder cb{s, pal, colLeft, colW, y};

    if (pIdx == 0)
    {
        cb.SectionLabel(zh ? L"启动与快捷键" : L"LAUNCH & SHORTCUT");
        cb.BeginCard(240.0f);
        cb.HotkeyRow("hotkey", y, 60.0f,
            zh ? L"全局唤醒快捷键" : L"Global activation hotkey",
            zh ? L"点击右侧录制新的组合键，随时在任意窗口呼出启动台" : L"Click to record a new modifier + key shortcut",
            s->vm.hotkey, s->recordingHotkey);
        const int modeIdx = (s->vm.displayMode.find(L"Launchpad") != std::wstring::npos) ? 1 : 0;
        cb.SegmentedRow("displayMode", y + 60.0f, 60.0f,
            zh ? L"默认显示模式" : L"Default display mode",
            zh ? L"聚焦模式用于极速键盘搜索；启动台模式用于全屏图标整理" : L"Spotlight for instant keyboard search; Launchpad for full grid",
            {zh ? L"聚焦 Spotlight" : L"Spotlight", zh ? L"启动台 Launchpad" : L"Launchpad"}, modeIdx, 236.0f);
        cb.SegmentedRow("language", y + 120.0f, 60.0f,
            zh ? L"界面语言" : L"Display language",
            zh ? L"切换设置窗口与主界面的中文 / 英文显示" : L"Switch UI labels between Chinese and English",
            {L"中文", L"English"}, zh ? 0 : 1, 168.0f);
        cb.ToggleRow("startWithWindows", y + 180.0f, 60.0f,
            zh ? L"开机自动启动" : L"Start with Windows",
            zh ? L"登录系统后静默常驻托盘，按热键零延迟唤醒" : L"Launch silently into the system tray when signing in",
            s->vm.startWithWindows);
        cb.EndCard(240.0f);

        cb.SectionLabel(zh ? L"聚焦行为与历史" : L"SPOTLIGHT BEHAVIOR & HISTORY");
        cb.BeginCard(240.0f);
        cb.ToggleRow("resetSearchOnOpen", y, 60.0f,
            zh ? L"每次唤醒清空上次搜索词" : L"Reset search query on open",
            zh ? L"关闭后将保留上次未完成的关键词，适合连续查找" : L"Clear previous search text each time Spotlight opens",
            s->vm.resetSearchOnOpen);
        cb.ToggleRow("showRecentAndFavorites", y + 60.0f, 60.0f,
            zh ? L"空搜索时展示最近常用项" : L"Show recent & favorite items",
            zh ? L"无输入时自动列出你最常启动的应用，按回车即可直达" : L"Display frequently used apps when the search box is empty",
            s->vm.showRecentAndFavorites);
        cb.ToggleRow("trackLaunchHistory", y + 120.0f, 60.0f,
            zh ? L"记录本地启动频率" : L"Track local launch frequency",
            zh ? L"用于智能排序搜索结果，仅存储在本机配置文件中" : L"Rank frequent apps higher in search results (local only)",
            s->vm.trackLaunchHistory);
        cb.ActionRow("clearLaunchHistory", y + 180.0f, 60.0f,
            zh ? L"清空启动历史记录" : L"Clear launch history",
            zh ? L"重置所有应用的使用次数与最近启动排序" : L"Reset recorded launch counts and recency ordering",
            zh ? L"清空历史" : L"Clear History", CardBuilder::ButtonStyle::Danger, 118.0f);
        cb.EndCard(240.0f);
    }
    else if (pIdx == 1)
    {
        cb.SectionLabel(zh ? L"液态玻璃材质预设" : L"LIQUID GLASS PRESET");
        cb.BeginCard(120.0f);
        int presetIdx = 3;
        if (s->vm.glassPreset == L"Crystal") presetIdx = 0;
        else if (s->vm.glassPreset == L"Frosted") presetIdx = 1;
        else if (s->vm.glassPreset == L"Tinted") presetIdx = 2;
        cb.SegmentedRow("glassPreset", y, 60.0f,
            zh ? L"光学材质风格" : L"Glass material preset",
            zh ? L"Crystal 通透折射 · Frosted 柔和磨砂 · Tinted 柔光着色" : L"Crystal clear, Frosted matte, Tinted, or Custom",
            {L"Crystal", L"Frosted", L"Tinted", L"Custom"}, presetIdx, 296.0f);
        cb.ToggleRow("reduceMotion", y + 60.0f, 60.0f,
            zh ? L"减弱动态与弹簧效果" : L"Reduce motion",
            zh ? L"关闭弹性弹出与焦点滑动动画，转为轻量瞬时切换" : L"Disable spring pop-in and gliding pill animations",
            s->vm.reduceMotion);
        cb.EndCard(120.0f);

        cb.SectionLabel(zh ? L"光学参数精细微调" : L"OPTICAL FINE-TUNING");
        cb.BeginCard(240.0f);
        cb.SliderRow("glassBlur", y, 60.0f,
            zh ? L"背景模糊强度" : L"Background blur mix",
            zh ? L"越低越接近清透水晶玻璃，默认 6%" : L"Lower values keep desktop details crisp (default 6%)",
            s->vm.glassBlurPercent, 0, 100, std::to_wstring(s->vm.glassBlurPercent) + L"%");
        cb.SliderRow("glassTint", y + 60.0f, 60.0f,
            zh ? L"玻璃表面着色浓度" : L"Surface tint density",
            zh ? L"控制玻璃底色遮罩浓度，默认 2%" : L"Controls subtle glass tint opacity (default 2%)",
            s->vm.glassTintPercent, 0, 40, std::to_wstring(s->vm.glassTintPercent) + L"%");
        cb.SliderRow("glassRefraction", y + 120.0f, 60.0f,
            zh ? L"边缘液态弯月折射宽度" : L"Edge refraction meniscus",
            zh ? L"窗口四周边缘的弧面光学折射带厚度，默认 30 px" : L"Curved edge refraction band (default 30 px)",
            s->vm.glassRefractionPx, 0, 60, std::to_wstring(s->vm.glassRefractionPx) + L" px");
        cb.SliderRow("glassRim", y + 180.0f, 60.0f,
            zh ? L"双角高光边框亮度" : L"Dual-angle specular rim",
            zh ? L"左上与右下光学玻璃高光边缘强度，默认 95%" : L"Intensity of the twin diagonal glass rim highlights",
            s->vm.glassRimPercent, 0, 150, std::to_wstring(s->vm.glassRimPercent) + L"%");
        cb.EndCard(240.0f);

        cb.SectionLabel(zh ? L"窗口尺寸与图标排版" : L"GEOMETRY & ICONS");
        cb.BeginCard(240.0f);
        cb.SliderRow("spotlightWidth", y, 60.0f,
            zh ? L"聚焦窗口宽度" : L"Spotlight width",
            zh ? L"默认 740 DIP，适配紧凑优雅的单手视线范围" : L"Default 740 DIP compact width",
            s->vm.spotlightWidth, 540, 1100, std::to_wstring(s->vm.spotlightWidth) + L" px");
        cb.SliderRow("spotlightHeight", y + 60.0f, 60.0f,
            zh ? L"聚焦窗口高度" : L"Spotlight height",
            zh ? L"默认 460 DIP，控制结果列表可视行数" : L"Default 460 DIP result viewport height",
            s->vm.spotlightHeight, 340, 760, std::to_wstring(s->vm.spotlightHeight) + L" px");
        cb.SliderRow("iconSize", y + 120.0f, 60.0f,
            zh ? L"图标尺寸" : L"Icon size",
            zh ? L"启动台与应用卡片图标边长，默认 56 px" : L"App icon edge length in Launchpad grid (default 56 px)",
            s->vm.iconSize, 40, 84, std::to_wstring(s->vm.iconSize) + L" px");
        cb.SliderRow("appSpacing", y + 180.0f, 60.0f,
            zh ? L"图标间距" : L"Icon spacing",
            zh ? L"网格相邻图标之间的呼吸留白，默认 10 px" : L"Spacing between adjacent tiles (default 10 px)",
            s->vm.appSpacing, 6, 24, std::to_wstring(s->vm.appSpacing) + L" px");
        cb.EndCard(240.0f);
    }
    else if (pIdx == 2)
    {
        cb.SectionLabel(zh ? L"多屏与鼠标跟随定位" : L"MULTI-MONITOR & CURSOR PLACEMENT");
        cb.BeginCard(120.0f);
        cb.SliderRow("mouseBias", y, 60.0f,
            zh ? L"聚焦窗向鼠标偏移幅度" : L"Center-to-cursor placement bias",
            zh ? L"始终在鼠标所在屏幕唤出；0% 居中，72% 贴近指针并自动避让边缘" : L"0% centered on cursor monitor; 72% tracks cursor safely",
            s->vm.spotlightMouseBiasPercent, 0, 100, std::to_wstring(s->vm.spotlightMouseBiasPercent) + L"%");
        const int wheelPct = static_cast<int>(std::lround(s->vm.wheelSensitivity * 100.0));
        std::wostringstream woss;
        woss << std::fixed << std::setprecision(1) << (wheelPct / 100.0) << L"x";
        cb.SliderRow("wheelSensitivity", y + 60.0f, 60.0f,
            zh ? L"滚轮滚动灵敏度" : L"Scroll wheel sensitivity",
            zh ? L"调节启动台与结果列表的鼠标滚轮步进倍率" : L"Adjust scroll step multiplier for Launchpad and results",
            wheelPct, 50, 250, woss.str());
        cb.EndCard(120.0f);

        cb.SectionLabel(zh ? L"EVERYTHING 本机文件联合检索" : L"EVERYTHING FILE SEARCH");
        cb.BeginCard(120.0f);
        cb.ToggleRow("enableEverything", y, 60.0f,
            zh ? L"启用 Everything 实时文件搜索" : L"Enable Everything file search",
            zh ? L"应用优先展示，下方自动融合本机文件；输入 f 关键词 或 /关键词 仅搜文件" : L"Apps first with files below; prefix 'f ' or '/' for files-only mode",
            s->vm.enableEverythingSearch);
        cb.RowDivider(y + 60.0f);
        cb.DrawRowLabels(y + 60.0f, 60.0f, 160.0f,
            zh ? L"Everything 进程连接状态" : L"Everything IPC status",
            zh ? L"通过原生 WM_COPYDATA 零依赖直连标准版 Everything 1.4 / 1.5" : L"Zero-dependency native WM_COPYDATA bridge to Everything 1.4 / 1.5");
        const D2D1_RECT_F badgeR = D2D1::RectF(colLeft + colW - 156.0f, y + 76.0f, colLeft + colW - 18.0f, y + 104.0f);
        FillRoundRect(s, badgeR, 14.0f, s->vm.everythingAvailable ? Rgba(16, 185, 129, 0.16f) : pal.segmentBg);
        DrawTextBox(s, s->vm.everythingAvailable
            ? (zh ? L"● Everything 已连接" : L"● Connected")
            : (zh ? L"○ 未检测到运行" : L"○ Not running"),
            s->fmtCenter, badgeR, s->vm.everythingAvailable ? pal.success : pal.textMuted);
        cb.EndCard(120.0f);
    }
    else if (pIdx == 3)
    {
        cb.SectionLabel(zh ? L"零冲突触发规则与计算器" : L"ROUTING & INLINE CALCULATOR");
        cb.BeginCard(240.0f);
        cb.ToggleRow("enableHotwordsNoPrefix", y, 60.0f,
            zh ? L"允许无前缀精确匹配热词" : L"Allow exact no-prefix hotword match",
            zh ? L"直接输入 recycle / sleep 即可触发；若有同名应用，应用永远优先" : L"Type recycle or sleep directly; apps always rank first if matched",
            s->vm.enableHotwordsNoPrefix);
        cb.ToggleRow("enableInlineCalculator", y + 60.0f, 60.0f,
            zh ? L"启用聚焦栏即时计算器" : L"Enable inline Spotlight calculator",
            zh ? L"输入 128*36、(1920-740)/2 或 0xff 实时出结果，按 Enter 复制" : L"Evaluate 128*36, (1920-740)/2, or 0xff inline; Enter copies result",
            s->vm.enableInlineCalculator);
        cb.ActionRow("openHotwordsFile", y + 120.0f, 60.0f,
            zh ? L"自定义增删热词 (hotwords.json)" : L"Customize hotwords (hotwords.json)",
            zh ? L"支持自定义任意触发词，绑定命令行、网页模板 {q} 或本地路径" : L"Add custom triggers for commands, {q} web templates, or local paths",
            zh ? L"编辑配置" : L"Edit JSON", CardBuilder::ButtonStyle::Primary, 118.0f);
        cb.ActionRow("reloadHotwords", y + 180.0f, 60.0f,
            zh ? L"保存配置后立即生效" : L"Reload hotwords from disk",
            zh ? L"修改并保存 hotwords.json 后点击此处立即刷新热词列表" : L"Reload hotwords.json immediately after editing",
            zh ? L"重新加载" : L"Reload", CardBuilder::ButtonStyle::Secondary, 118.0f);
        cb.EndCard(240.0f);

        cb.SectionLabel((zh ? L"已配置热词 (" : L"CONFIGURED HOTWORDS (") + std::to_wstring(s->vm.hotwords.size()) + L")");
        if (!s->vm.hotwords.empty())
        {
            const float rowH = 58.0f;
            const float totalH = static_cast<float>(s->vm.hotwords.size()) * rowH;
            cb.BeginCard(totalH);
            for (size_t i = 0; i < s->vm.hotwords.size(); ++i)
            {
                const auto& hw = s->vm.hotwords[i];
                const std::wstring title = L">" + hw.trigger + L"   " + hw.title;
                const std::wstring desc = hw.target;
                cb.ToggleRow("hw_" + std::to_string(i), y + static_cast<float>(i) * rowH, rowH, title, desc, hw.enabled);
            }
            cb.EndCard(totalH);
        }

        cb.BeginCard(64.0f);
        cb.ActionRow("resetHotwords", y, 64.0f,
            zh ? L"恢复默认热词清单" : L"Restore default hotwords",
            zh ? L"重置为内置的 recycle / sleep / lock / ip / g / gh / b 等热词" : L"Reset hotwords.json back to built-in system & web commands",
            zh ? L"恢复默认" : L"Reset List", CardBuilder::ButtonStyle::Danger, 118.0f);
        cb.EndCard(64.0f);
    }
    else if (pIdx == 4)
    {
        cb.SectionLabel(zh ? L"启动台目录数据源" : L"LAUNCHPAD FOLDER SOURCE");
        cb.BeginCard(180.0f);
        cb.ActionRow("browseFolder", y, 60.0f,
            zh ? L"当前快捷方式目录" : L"Current Launchpad folder",
            s->vm.launchpadFolder.empty() ? (zh ? L"未设置" : L"Not configured") : s->vm.launchpadFolder,
            zh ? L"更换目录..." : L"Browse...", CardBuilder::ButtonStyle::Primary, 118.0f);
        cb.ActionRow("openFolder", y + 60.0f, 60.0f,
            zh ? L"在文件资源管理器中打开" : L"Open in File Explorer",
            zh ? L"直接拖入或删除 .lnk / .url 快捷方式，增删不会卸载原软件" : L"Add or remove shortcuts directly without uninstalling apps",
            zh ? L"打开目录" : L"Open Folder", CardBuilder::ButtonStyle::Secondary, 118.0f);
        cb.ActionRow("rescanCatalog", y + 120.0f, 60.0f,
            zh ? L"立即重新扫描索引" : L"Rescan catalog now",
            (zh ? L"当前已索引 " : L"Currently indexed ") + std::to_wstring(s->vm.catalogItemCount) + (zh ? L" 个启动项" : L" launch items"),
            zh ? L"重新扫描" : L"Rescan", CardBuilder::ButtonStyle::Secondary, 118.0f);
        cb.EndCard(180.0f);

        cb.SectionLabel(zh ? L"快捷导入" : L"QUICK IMPORT");
        cb.BeginCard(64.0f);
        cb.ActionRow("importShortcuts", y, 64.0f,
            zh ? L"从开始菜单与桌面一键导入" : L"Import from Start Menu & Desktop",
            zh ? L"自动筛选常用应用快捷方式并安全复制到启动台目录（不改动原文件）" : L"Safely copy .lnk / .url shortcuts into your Launchpad folder",
            zh ? L"立即导入" : L"Import Now", CardBuilder::ButtonStyle::Primary, 118.0f);
        cb.EndCard(64.0f);
    }
    else if (pIdx == 5)
    {
        RenderInputStatsContent(s, pal, colLeft, colW, y, zh);
    }
    else if (pIdx == 6)
    {
        cb.SectionLabel(zh ? L"数据与诊断" : L"DATA & DIAGNOSTICS");
        cb.BeginCard(120.0f);
        cb.ActionRow("openDataDir", y, 60.0f,
            zh ? L"本地配置与数据目录" : L"Local data directory",
            s->vm.dataDirectory,
            zh ? L"打开位置" : L"Open Data", CardBuilder::ButtonStyle::Secondary, 118.0f);
        cb.ActionRow("exportDiagnostics", y + 60.0f, 60.0f,
            zh ? L"导出运行诊断报告" : L"Export diagnostics bundle",
            zh ? L"包含设置、渲染策略与日志摘要，用于排查异常" : L"Save settings, adaptive policy, and startup logs for troubleshooting",
            zh ? L"导出报告" : L"Export", CardBuilder::ButtonStyle::Secondary, 118.0f);
        cb.EndCard(120.0f);

        cb.SectionLabel(zh ? L"重置" : L"RESET");
        cb.BeginCard(64.0f);
        cb.ActionRow("resetDefaults", y, 64.0f,
            zh ? L"恢复全部默认推荐配置" : L"Restore default settings",
            zh ? L"还原 Crystal 液态玻璃、72% 鼠标偏移、Alt+D 热键等默认参数" : L"Restore Crystal glass, 72% cursor bias, Alt+D hotkey, and defaults",
            zh ? L"恢复默认" : L"Reset All", CardBuilder::ButtonStyle::Danger, 118.0f);
        cb.EndCard(64.0f);
    }

    s->contentHeight = (y - startY) + 32.0f;
    s->rt->PopAxisAlignedClip();

    // Custom Thin Overlay Scrollbar
    const float maxScroll = std::max(0.0f, s->contentHeight - size.height);
    if (maxScroll > 1.0f)
    {
        const float trackH = size.height - 16.0f;
        const float thumbH = std::max(36.0f, trackH * (size.height / s->contentHeight));
        const float thumbY = 8.0f + (trackH - thumbH) * (s->scrollY / maxScroll);
        FillRoundRect(s, D2D1::RectF(size.width - 8.0f, thumbY, size.width - 3.0f, thumbY + thumbH), 2.5f, pal.scrollbarThumb);
    }

    const HRESULT hr = s->rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        ReleaseDeviceResources(s);
    }
}

void CommitSettings(StudioState* s, bool previewOnly = false)
{
    if (previewOnly)
    {
        if (s->callbacks.previewGlass) s->callbacks.previewGlass(s->vm);
        InvalidateRect(s->hwnd, nullptr, FALSE);
        return;
    }
    if (s->callbacks.applySettings)
    {
        std::wstring status;
        const bool ok = s->callbacks.applySettings(s->vm, status);
        s->statusIsError = !ok;
        s->statusText = status;
        if (s->callbacks.readState)
        {
            const int savedPage = s->activePage;
            s->vm = s->callbacks.readState();
            s->activePage = savedPage;
        }
        ApplyDwmTheme(s->hwnd, s->vm.darkMode);
    }
    InvalidateRect(s->hwnd, nullptr, FALSE);
}

void UpdateSliderFromX(StudioState* s, float dipX, bool previewOnly)
{
    const float left = s->activeSliderRect.left + 8.0f;
    const float right = s->activeSliderRect.right - 8.0f;
    const float t = (right > left) ? std::clamp((dipX - left) / (right - left), 0.0f, 1.0f) : 0.0f;
    const int val = s->activeSliderMin + static_cast<int>(std::lround(t * static_cast<float>(s->activeSliderMax - s->activeSliderMin)));

    if (s->activeSliderId == "glassBlur") { s->vm.glassBlurPercent = val; s->vm.glassPreset = L"Custom"; }
    else if (s->activeSliderId == "glassTint") { s->vm.glassTintPercent = val; s->vm.glassPreset = L"Custom"; }
    else if (s->activeSliderId == "glassRefraction") { s->vm.glassRefractionPx = val; s->vm.glassPreset = L"Custom"; }
    else if (s->activeSliderId == "glassRim") { s->vm.glassRimPercent = val; s->vm.glassPreset = L"Custom"; }
    else if (s->activeSliderId == "spotlightWidth") { s->vm.spotlightWidth = val; }
    else if (s->activeSliderId == "spotlightHeight") { s->vm.spotlightHeight = val; }
    else if (s->activeSliderId == "iconSize") { s->vm.iconSize = val; }
    else if (s->activeSliderId == "appSpacing") { s->vm.appSpacing = val; }
    else if (s->activeSliderId == "mouseBias") { s->vm.spotlightMouseBiasPercent = val; }
    else if (s->activeSliderId == "wheelSensitivity") { s->vm.wheelSensitivity = std::clamp(val / 100.0, 0.5, 2.5); }

    CommitSettings(s, previewOnly);
}

void HandleClick(StudioState* s, float x, float y)
{
    const bool zh = IsChinese(s->vm.language);
    for (auto it = s->hitZones.rbegin(); it != s->hitZones.rend(); ++it)
    {
        const auto& hz = *it;
        if (x < hz.rect.left || x > hz.rect.right || y < hz.rect.top || y > hz.rect.bottom) continue;

        if (hz.kind != HitKind::Hotkey) s->recordingHotkey = false;
        if (hz.kind != HitKind::Button || hz.id != s->pendingConfirmButtonId) s->pendingConfirmButtonId.clear();

        if (hz.kind == HitKind::NavItem)
        {
            if (s->activePage != hz.index)
            {
                s->activePage = hz.index;
                s->scrollY = 0.0f;
                s->hoveredKeyLabel.clear();
                if (s->callbacks.onPageChanged) s->callbacks.onPageChanged(s->activePage);
                if (s->activePage == 5 && s->callbacks.readInputStats)
                {
                    s->stats = s->callbacks.readInputStats();
                    SetTimer(s->hwnd, kStatsRefreshTimerId, 1000, nullptr);
                }
                else
                {
                    KillTimer(s->hwnd, kStatsRefreshTimerId);
                }
                InvalidateRect(s->hwnd, nullptr, FALSE);
            }
            return;
        }
        if (hz.kind == HitKind::Toggle)
        {
            if (hz.id == "startWithWindows") s->vm.startWithWindows = !s->vm.startWithWindows;
            else if (hz.id == "resetSearchOnOpen") s->vm.resetSearchOnOpen = !s->vm.resetSearchOnOpen;
            else if (hz.id == "showRecentAndFavorites") s->vm.showRecentAndFavorites = !s->vm.showRecentAndFavorites;
            else if (hz.id == "trackLaunchHistory") s->vm.trackLaunchHistory = !s->vm.trackLaunchHistory;
            else if (hz.id == "reduceMotion") s->vm.reduceMotion = !s->vm.reduceMotion;
            else if (hz.id == "enableEverything") s->vm.enableEverythingSearch = !s->vm.enableEverythingSearch;
            else if (hz.id == "enableInputStats") s->vm.enableInputStats = !s->vm.enableInputStats;
            else if (hz.id == "enableHotwordsNoPrefix") s->vm.enableHotwordsNoPrefix = !s->vm.enableHotwordsNoPrefix;
            else if (hz.id == "enableInlineCalculator") s->vm.enableInlineCalculator = !s->vm.enableInlineCalculator;
            else if (hz.id.rfind("hw_", 0) == 0)
            {
                const size_t idx = static_cast<size_t>(std::stoul(hz.id.substr(3)));
                if (idx < s->vm.hotwords.size() && s->callbacks.toggleHotword)
                {
                    s->vm.hotwords[idx].enabled = !s->vm.hotwords[idx].enabled;
                    s->callbacks.toggleHotword(idx, s->vm.hotwords[idx].enabled);
                    s->statusIsError = false;
                    s->statusText = zh ? L"热词状态已更新" : L"Hotword updated";
                    InvalidateRect(s->hwnd, nullptr, FALSE);
                }
                return;
            }
            CommitSettings(s, false);
            return;
        }
        if (hz.kind == HitKind::Segment)
        {
            if (hz.id == "statsRange")
            {
                s->statsRangeIndex = std::clamp(hz.index, 0, 2);
                InvalidateRect(s->hwnd, nullptr, FALSE);
                return;
            }
            if (hz.id == "displayMode") s->vm.displayMode = (hz.index == 1) ? L"Launchpad" : L"Spotlight";
            else if (hz.id == "language") s->vm.language = (hz.index == 0) ? L"Chinese" : L"English";
            else if (hz.id == "glassPreset")
            {
                if (hz.index == 0) { s->vm.glassPreset = L"Crystal"; s->vm.glassBlurPercent = 6; s->vm.glassTintPercent = 2; s->vm.glassRefractionPx = 30; s->vm.glassRimPercent = 95; }
                else if (hz.index == 1) { s->vm.glassPreset = L"Frosted"; s->vm.glassBlurPercent = 48; s->vm.glassTintPercent = 12; s->vm.glassRefractionPx = 22; s->vm.glassRimPercent = 75; }
                else if (hz.index == 2) { s->vm.glassPreset = L"Tinted"; s->vm.glassBlurPercent = 28; s->vm.glassTintPercent = 24; s->vm.glassRefractionPx = 18; s->vm.glassRimPercent = 65; }
                else { s->vm.glassPreset = L"Custom"; }
            }
            CommitSettings(s, false);
            return;
        }
        if (hz.kind == HitKind::Slider)
        {
            s->draggingSlider = true;
            s->activeSliderId = hz.id;
            s->activeSliderRect = hz.rect;
            s->activeSliderMin = hz.minVal;
            s->activeSliderMax = hz.maxVal;
            SetCapture(s->hwnd);
            UpdateSliderFromX(s, x, true);
            return;
        }
        if (hz.kind == HitKind::Hotkey)
        {
            s->recordingHotkey = !s->recordingHotkey;
            InvalidateRect(s->hwnd, nullptr, FALSE);
            return;
        }
        if (hz.kind == HitKind::Button)
        {
            const bool needsConfirm = (hz.id == "clearLaunchHistory" || hz.id == "clearInputStats" || hz.id == "resetDefaults" || hz.id == "resetHotwords");
            if (needsConfirm && s->pendingConfirmButtonId != hz.id)
            {
                s->pendingConfirmButtonId = hz.id;
                SetTimer(s->hwnd, kConfirmResetTimerId, 3500, nullptr);
                InvalidateRect(s->hwnd, nullptr, FALSE);
                return;
            }
            s->pendingConfirmButtonId.clear();
            KillTimer(s->hwnd, kConfirmResetTimerId);

            if (hz.id == "clearLaunchHistory" && s->callbacks.clearLaunchHistory)
            {
                s->callbacks.clearLaunchHistory();
                s->statusIsError = false;
                s->statusText = zh ? L"已清空启动历史" : L"Launch history cleared";
            }
            else if (hz.id == "clearInputStats" && s->callbacks.clearInputStats)
            {
                s->callbacks.clearInputStats();
                if (s->callbacks.readInputStats) s->stats = s->callbacks.readInputStats();
                s->statusIsError = false;
                s->statusText = zh ? L"已清空键鼠统计数据" : L"Input statistics cleared";
            }
            else if (hz.id == "browseFolder" && s->callbacks.browseFolder)
            {
                std::wstring chosen;
                if (s->callbacks.browseFolder(s->hwnd, chosen) && !chosen.empty())
                {
                    s->vm.launchpadFolder = chosen;
                    CommitSettings(s, false);
                }
            }
            else if (hz.id == "openFolder" && s->callbacks.openLaunchpadFolder)
            {
                s->callbacks.openLaunchpadFolder();
            }
            else if (hz.id == "rescanCatalog" && s->callbacks.rescanCatalog)
            {
                s->callbacks.rescanCatalog();
                if (s->callbacks.readState) s->vm = s->callbacks.readState();
                s->statusIsError = false;
                s->statusText = zh ? L"已完成目录重扫" : L"Catalog rescanned";
            }
            else if (hz.id == "importShortcuts" && s->callbacks.importSystemShortcuts)
            {
                const int imported = s->callbacks.importSystemShortcuts();
                if (s->callbacks.readState) s->vm = s->callbacks.readState();
                s->statusIsError = false;
                s->statusText = zh
                    ? (L"已导入 " + std::to_wstring(imported) + L" 个快捷方式")
                    : (L"Imported " + std::to_wstring(imported) + L" shortcuts");
            }
            else if (hz.id == "openDataDir" && s->callbacks.openDataDirectory)
            {
                s->callbacks.openDataDirectory();
            }
            else if (hz.id == "exportDiagnostics" && s->callbacks.exportDiagnostics)
            {
                std::wstring outPath;
                if (s->callbacks.exportDiagnostics(outPath))
                {
                    s->statusIsError = false;
                    s->statusText = zh ? L"诊断报告已导出" : L"Diagnostics exported";
                }
            }
            else if (hz.id == "openHotwordsFile" && s->callbacks.openHotwordsFile)
            {
                s->callbacks.openHotwordsFile();
                s->statusIsError = false;
                s->statusText = zh ? L"已打开 hotwords.json" : L"Opened hotwords.json";
            }
            else if (hz.id == "reloadHotwords" && s->callbacks.reloadHotwords)
            {
                s->callbacks.reloadHotwords();
                if (s->callbacks.readState) s->vm = s->callbacks.readState();
                s->statusIsError = false;
                s->statusText = zh ? L"已重新加载热词" : L"Hotwords reloaded";
            }
            else if (hz.id == "resetHotwords" && s->callbacks.resetHotwords)
            {
                s->callbacks.resetHotwords();
                if (s->callbacks.readState) s->vm = s->callbacks.readState();
                s->statusIsError = false;
                s->statusText = zh ? L"已恢复默认热词" : L"Default hotwords restored";
            }
            else if (hz.id == "resetDefaults" && s->callbacks.resetDefaults)
            {
                s->callbacks.resetDefaults();
                if (s->callbacks.readState) s->vm = s->callbacks.readState();
                ApplyDwmTheme(s->hwnd, s->vm.darkMode);
                s->statusIsError = false;
                s->statusText = zh ? L"已恢复推荐默认配置" : L"Restored default settings";
            }
            InvalidateRect(s->hwnd, nullptr, FALSE);
            return;
        }
    }
    s->recordingHotkey = false;
    s->pendingConfirmButtonId.clear();
    InvalidateRect(s->hwnd, nullptr, FALSE);
}

LRESULT CALLBACK SettingsStudioWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* s = reinterpret_cast<StudioState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        s = reinterpret_cast<StudioState*>(cs->lpCreateParams);
        s->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        return TRUE;
    }
    if (!s) return DefWindowProcW(hwnd, msg, wParam, lParam);

    const float dpiScale = static_cast<float>(QueryWindowDpi(hwnd)) / 96.0f;

    switch (msg)
    {
    case WM_CREATE:
        ApplyDwmTheme(hwnd, s->vm.darkMode);
        if (s->activePage == 5) SetTimer(hwnd, kStatsRefreshTimerId, 1000, nullptr);
        return 0;

    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = static_cast<LONG>(std::lround(780.0f * dpiScale));
        mmi->ptMinTrackSize.y = static_cast<LONG>(std::lround(540.0f * dpiScale));
        return 0;
    }

    case WM_SIZE:
        if (s->hwndRt)
        {
            const UINT w = std::max<UINT>(1, LOWORD(lParam));
            const UINT h = std::max<UINT>(1, HIWORD(lParam));
            s->hwndRt->Resize(D2D1::SizeU(w, h));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DPICHANGED:
    {
        const UINT newDpi = HIWORD(wParam);
        if (s->rt) s->rt->SetDpi(static_cast<float>(newDpi), static_cast<float>(newDpi));
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested)
        {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        PaintStudio(s);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        const float x = static_cast<float>(GET_X_LPARAM(lParam)) / dpiScale;
        const float y = static_cast<float>(GET_Y_LPARAM(lParam)) / dpiScale;
        if (s->draggingSlider)
        {
            UpdateSliderFromX(s, x, true);
            return 0;
        }
        HitKind nextKind = HitKind::None;
        std::string nextId;
        int nextIdx = -1;
        std::wstring nextKeyLabel;
        std::uint64_t nextKeyCount = 0;
        for (auto it = s->hitZones.rbegin(); it != s->hitZones.rend(); ++it)
        {
            if (x >= it->rect.left && x <= it->rect.right && y >= it->rect.top && y <= it->rect.bottom)
            {
                nextKind = it->kind;
                nextId = it->id;
                nextIdx = it->index;
                if (it->kind == HitKind::HeatmapKey)
                {
                    nextKeyLabel = it->extraLabel;
                    nextKeyCount = it->extraCount;
                }
                break;
            }
        }
        if (nextKind != s->hoveredKind || nextId != s->hoveredId || nextIdx != s->hoveredIndex || nextKeyLabel != s->hoveredKeyLabel)
        {
            s->hoveredKind = nextKind;
            s->hoveredId = nextId;
            s->hoveredIndex = nextIdx;
            s->hoveredKeyLabel = nextKeyLabel;
            s->hoveredKeyCount = nextKeyCount;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        SetCursor(LoadCursorW(nullptr, nextKind == HitKind::None ? IDC_ARROW : IDC_HAND));
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        SetFocus(hwnd);
        const float x = static_cast<float>(GET_X_LPARAM(lParam)) / dpiScale;
        const float y = static_cast<float>(GET_Y_LPARAM(lParam)) / dpiScale;
        HandleClick(s, x, y);
        return 0;
    }

    case WM_LBUTTONUP:
        if (s->draggingSlider)
        {
            const float x = static_cast<float>(GET_X_LPARAM(lParam)) / dpiScale;
            s->draggingSlider = false;
            ReleaseCapture();
            UpdateSliderFromX(s, x, false);
        }
        return 0;

    case WM_MOUSEWHEEL:
    {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        const float maxScroll = std::max(0.0f, s->contentHeight - s->viewportH);
        const float nextScroll = std::clamp(s->scrollY - static_cast<float>(delta) * 0.45f, 0.0f, maxScroll);
        if (std::abs(nextScroll - s->scrollY) > 0.5f)
        {
            s->scrollY = nextScroll;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        const UINT vk = static_cast<UINT>(wParam);
        if (s->recordingHotkey)
        {
            if (vk == VK_ESCAPE)
            {
                s->recordingHotkey = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
                vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
                vk == VK_LWIN || vk == VK_RWIN)
            {
                return 0;
            }
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const std::wstring keyName = FormatVkName(vk);
            if ((ctrl || alt || shift) && !keyName.empty())
            {
                std::wstring combo;
                if (ctrl) combo += L"Ctrl+";
                if (alt) combo += L"Alt+";
                if (shift) combo += L"Shift+";
                combo += keyName;
                s->recordingHotkey = false;
                s->vm.hotkey = combo;
                CommitSettings(s, false);
            }
            return 0;
        }
        if (vk == VK_ESCAPE)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == kStatsRefreshTimerId && s->activePage == 5 && s->callbacks.readInputStats)
        {
            s->stats = s->callbacks.readInputStats();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == kConfirmResetTimerId)
        {
            s->pendingConfirmButtonId.clear();
            KillTimer(hwnd, kConfirmResetTimerId);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_NCDESTROY:
    {
        KillTimer(hwnd, kStatsRefreshTimerId);
        KillTimer(hwnd, kConfirmResetTimerId);
        auto onClosed = s->callbacks.onClosed;
        ReleaseDeviceResources(s);
        ReleaseTextFormats(s);
        SafeRelease(s->dwrite);
        SafeRelease(s->d2dFactory);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete s;
        if (onClosed) onClosed();
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EnsureStudioClassRegistered()
{
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SettingsStudioWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kSettingsStudioClassName;
    RegisterClassExW(&wc);
    registered = true;
}

HWND g_studioHwnd = nullptr;

} // namespace

HWND ShowSettingsStudio(HWND owner, const SettingsCallbacks& callbacks, int initialPage)
{
    EnsureStudioClassRegistered();
    if (g_studioHwnd && IsWindow(g_studioHwnd))
    {
        auto* s = reinterpret_cast<StudioState*>(GetWindowLongPtrW(g_studioHwnd, GWLP_USERDATA));
        if (s)
        {
            if (callbacks.readState) s->vm = callbacks.readState();
            if (callbacks.readInputStats) s->stats = callbacks.readInputStats();
            s->activePage = std::clamp(initialPage, 0, 6);
            s->scrollY = 0.0f;
            if (s->activePage == 5) SetTimer(g_studioHwnd, kStatsRefreshTimerId, 1000, nullptr);
            else KillTimer(g_studioHwnd, kStatsRefreshTimerId);
            ApplyDwmTheme(g_studioHwnd, s->vm.darkMode);
            InvalidateRect(g_studioHwnd, nullptr, FALSE);
        }
        ShowWindow(g_studioHwnd, SW_SHOW);
        SetForegroundWindow(g_studioHwnd);
        return g_studioHwnd;
    }

    auto* state = new StudioState();
    state->callbacks = callbacks;
    auto origClosed = state->callbacks.onClosed;
    state->callbacks.onClosed = [origClosed]() {
        g_studioHwnd = nullptr;
        if (origClosed) origClosed();
    };
    if (state->callbacks.readState) state->vm = state->callbacks.readState();
    if (state->callbacks.readInputStats) state->stats = state->callbacks.readInputStats();
    state->activePage = std::clamp(initialPage, 0, 6);

    const UINT dpi = owner ? QueryWindowDpi(owner) : 96;
    const float scale = static_cast<float>(dpi) / 96.0f;
    const int winW = static_cast<int>(std::lround(960.0f * scale));
    const int winH = static_cast<int>(std::lround(680.0f * scale));

    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - winW) / 2;
    const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - winH) / 2;

    const bool zh = IsChinese(state->vm.language);
    g_studioHwnd = CreateWindowExW(
        0,
        kSettingsStudioClassName,
        zh ? L"LightLaunchpad 设置" : L"LightLaunchpad Settings",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, winW, winH,
        owner, nullptr, GetModuleHandleW(nullptr), state);

    if (g_studioHwnd)
    {
        ShowWindow(g_studioHwnd, SW_SHOW);
        UpdateWindow(g_studioHwnd);
        SetForegroundWindow(g_studioHwnd);
    }
    return g_studioHwnd;
}

void RefreshSettingsStudio(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    auto* s = reinterpret_cast<StudioState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!s) return;
    if (s->callbacks.readState)
    {
        const int page = s->activePage;
        s->vm = s->callbacks.readState();
        s->activePage = page;
    }
    if (s->callbacks.readInputStats) s->stats = s->callbacks.readInputStats();
    ApplyDwmTheme(hwnd, s->vm.darkMode);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void CloseSettingsStudio(HWND hwnd)
{
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
}

bool IsSettingsStudioWindow(HWND hwnd)
{
    return hwnd && g_studioHwnd && hwnd == g_studioHwnd;
}

bool CaptureSettingsStudioPng(HWND hwnd, const std::wstring& outputPath)
{
    if (!hwnd || !IsWindow(hwnd)) return false;
    auto* s = reinterpret_cast<StudioState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!s || !EnsureResources(s)) return false;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const UINT w = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
    const UINT h = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
    const UINT dpi = QueryWindowDpi(hwnd);

    bool ok = false;
    IWICImagingFactory* wic = nullptr;
    IWICBitmap* wicBmp = nullptr;
    ID2D1RenderTarget* wicRt = nullptr;
    ID2D1SolidColorBrush* wicBrush = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &wicBmp)))
    {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi), static_cast<float>(dpi));
        if (SUCCEEDED(s->d2dFactory->CreateWicBitmapRenderTarget(wicBmp, props, &wicRt)) &&
            SUCCEEDED(wicRt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &wicBrush)))
        {
            wicRt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            ID2D1RenderTarget* prevRt = s->rt;
            ID2D1SolidColorBrush* prevBrush = s->brush;
            s->rt = wicRt;
            s->brush = wicBrush;
            PaintStudio(s);
            s->rt = prevRt;
            s->brush = prevBrush;

            IWICStream* stream = nullptr;
            IWICBitmapEncoder* encoder = nullptr;
            IWICBitmapFrameEncode* frame = nullptr;
            if (SUCCEEDED(wic->CreateStream(&stream)) &&
                SUCCEEDED(stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE)) &&
                SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
                SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
                SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
                SUCCEEDED(frame->Initialize(nullptr)) &&
                SUCCEEDED(frame->SetSize(w, h)))
            {
                WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
                frame->SetPixelFormat(&fmt);
                if (SUCCEEDED(frame->WriteSource(wicBmp, nullptr)) &&
                    SUCCEEDED(frame->Commit()) &&
                    SUCCEEDED(encoder->Commit()))
                {
                    ok = true;
                }
            }
            SafeRelease(frame);
            SafeRelease(encoder);
            SafeRelease(stream);
        }
    }
    SafeRelease(wicBrush);
    SafeRelease(wicRt);
    SafeRelease(wicBmp);
    SafeRelease(wic);
    return ok;
}

} // namespace llp::settings_ui
