#include "ClipboardOverlay.h"

#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_map>
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
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3
#endif

namespace llp::clipboard_ui
{
namespace
{

constexpr wchar_t kOverlayClassName[] = L"LightLaunchpadClipboardOverlayWindow";
constexpr float kOverlayDipW = 540.0f;
constexpr float kOverlayDipH = 600.0f;
constexpr float kRowH = 68.0f;

template <typename T>
void SafeRelease(T*& ptr)
{
    if (ptr) { ptr->Release(); ptr = nullptr; }
}

UINT QueryDpi(HWND hwnd)
{
    using Fn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
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

D2D1_COLOR_F Rgba(int r, int g, int b, float a = 1.0f)
{
    return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

std::wstring FormatRelativeTime(std::uint64_t tsMs, bool zh)
{
    if (tsMs == 0) return L"";
    const auto nowMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    if (nowMs <= tsMs) return zh ? L"刚刚" : L"now";
    const std::uint64_t sec = (nowMs - tsMs) / 1000;
    if (sec < 60) return zh ? L"刚刚" : L"now";
    if (sec < 3600) return std::to_wstring(sec / 60) + (zh ? L" 分钟前" : L"m ago");
    if (sec < 86400) return std::to_wstring(sec / 3600) + (zh ? L" 小时前" : L"h ago");
    return std::to_wstring(sec / 86400) + (zh ? L" 天前" : L"d ago");
}

void SendCtrlV()
{
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

enum class HitType { None, Tab, Row, PinBtn, DelBtn };
struct HitZone
{
    HitType type = HitType::None;
    int index = -1;
    D2D1_RECT_F rect{};
};

struct OverlayState
{
    HWND hwnd = nullptr;
    HWND prevForeground = nullptr;
    OverlayCallbacks callbacks;

    ID2D1Factory* d2d = nullptr;
    ID2D1HwndRenderTarget* hwndRt = nullptr;
    ID2D1RenderTarget* rt = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;

    IDWriteTextFormat* fmtSearch = nullptr;
    IDWriteTextFormat* fmtTab = nullptr;
    IDWriteTextFormat* fmtTitle = nullptr;
    IDWriteTextFormat* fmtMeta = nullptr;
    IDWriteTextFormat* fmtBadge = nullptr;
    IDWriteTextFormat* fmtFooter = nullptr;

    std::unordered_map<std::wstring, ID2D1Bitmap*> thumbCache;
    ID2D1Bitmap* backdropBmp = nullptr;
    std::vector<std::uint8_t> backdropPixels;
    int backdropW = 0;
    int backdropH = 0;
    std::wstring searchText;
    llp::clipboard::FilterTab activeTab = llp::clipboard::FilterTab::All;
    std::vector<llp::clipboard::ClipItem> visibleItems;
    int selectedIndex = 0;
    int hoveredRow = -1;
    float scrollY = 0.0f;
    std::vector<HitZone> hitZones;
};

OverlayState* g_state = nullptr;

void ReleaseThumbs(OverlayState* s)
{
    for (auto& kv : s->thumbCache) SafeRelease(kv.second);
    s->thumbCache.clear();
    SafeRelease(s->backdropBmp);
    s->backdropPixels.clear();
    s->backdropW = 0;
    s->backdropH = 0;
}

void ReleaseDevice(OverlayState* s)
{
    ReleaseThumbs(s);
    SafeRelease(s->brush);
    s->rt = nullptr;
    SafeRelease(s->hwndRt);
}

IDWriteTextFormat* MakeFmt(IDWriteFactory* dw, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING)
{
    IDWriteTextFormat* fmt = nullptr;
    if (SUCCEEDED(dw->CreateTextFormat(L"Microsoft YaHei UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &fmt)))
    {
        fmt->SetTextAlignment(align);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING tr{};
        tr.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        IDWriteInlineObject* ell = nullptr;
        if (SUCCEEDED(dw->CreateEllipsisTrimmingSign(fmt, &ell)))
        {
            fmt->SetTrimming(&tr, ell);
            ell->Release();
        }
    }
    return fmt;
}

bool EnsureResources(OverlayState* s)
{
    if (!s->d2d && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &s->d2d))) return false;
    if (!s->dwrite)
    {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&s->dwrite)))) return false;
        s->fmtSearch = MakeFmt(s->dwrite, 15.0f, DWRITE_FONT_WEIGHT_NORMAL);
        s->fmtTab = MakeFmt(s->dwrite, 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        s->fmtTitle = MakeFmt(s->dwrite, 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        s->fmtMeta = MakeFmt(s->dwrite, 11.0f, DWRITE_FONT_WEIGHT_NORMAL);
        s->fmtBadge = MakeFmt(s->dwrite, 10.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        s->fmtFooter = MakeFmt(s->dwrite, 11.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (!s->hwndRt)
    {
        RECT rc{};
        GetClientRect(s->hwnd, &rc);
        const UINT w = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
        const UINT h = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
        if (FAILED(s->d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(s->hwnd, D2D1::SizeU(w, h)), &s->hwndRt))) return false;
        s->rt = s->hwndRt;
        const UINT dpi = QueryDpi(s->hwnd);
        s->rt->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        s->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        s->rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &s->brush);
    }
    return s->rt && s->brush;
}

ID2D1Bitmap* GetThumbnail(OverlayState* s, const std::wstring& path)
{
    if (path.empty() || !s->rt) return nullptr;
    auto it = s->thumbCache.find(path);
    if (it != s->thumbCache.end()) return it->second;

    ID2D1Bitmap* bmp = nullptr;
    IWICImagingFactory* wic = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* conv = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) &&
        SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
        SUCCEEDED(scaler->Initialize(frame, 104, 88, WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        s->rt->CreateBitmapFromWicBitmap(conv, nullptr, &bmp);
    }
    SafeRelease(conv);
    SafeRelease(scaler);
    SafeRelease(frame);
    SafeRelease(dec);
    SafeRelease(wic);
    s->thumbCache[path] = bmp;
    return bmp;
}

void RefreshQuery(OverlayState* s)
{
    s->visibleItems.clear();
    if (s->callbacks.getStore)
    {
        if (auto* store = s->callbacks.getStore())
        {
            s->visibleItems = store->Query(s->searchText, s->activeTab);
        }
    }
    if (s->visibleItems.empty()) s->selectedIndex = -1;
    else s->selectedIndex = std::clamp(s->selectedIndex, 0, static_cast<int>(s->visibleItems.size()) - 1);
    const float listH = kOverlayDipH - 144.0f;
    const float maxScroll = std::max(0.0f, static_cast<float>(s->visibleItems.size()) * (kRowH + 8.0f) - listH);
    s->scrollY = std::clamp(s->scrollY, 0.0f, maxScroll);
}

void EnsureSelectedVisible(OverlayState* s)
{
    if (s->selectedIndex < 0) return;
    const float listH = kOverlayDipH - 144.0f;
    const float itemTop = static_cast<float>(s->selectedIndex) * (kRowH + 8.0f);
    const float itemBot = itemTop + kRowH;
    if (itemTop < s->scrollY) s->scrollY = itemTop;
    else if (itemBot > s->scrollY + listH) s->scrollY = itemBot - listH;
}

void CaptureDesktopBackdrop(OverlayState* s, int x, int y, int w, int h)
{
    SafeRelease(s->backdropBmp);
    s->backdropPixels.clear();
    s->backdropW = 0;
    s->backdropH = 0;
    if (w <= 0 || h <= 0) return;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hbmp && bits)
    {
        HGDIOBJ old = SelectObject(hdcMem, hbmp);
        BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY | CAPTUREBLT);
        SelectObject(hdcMem, old);

        const size_t totalBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
        s->backdropPixels.resize(totalBytes);
        memcpy(s->backdropPixels.data(), bits, totalBytes);
        // Ensure opaque alpha for Direct2D premultiplied BGRA
        for (size_t i = 3; i < totalBytes; i += 4) s->backdropPixels[i] = 255;

        // Subtle 1-pass optical softening (3x3 box blur) for crisp Crystal Liquid Glass feel
        std::vector<std::uint8_t> soft = s->backdropPixels;
        for (int py = 1; py < h - 1; ++py)
        {
            for (int px = 1; px < w - 1; ++px)
            {
                int bSum = 0, gSum = 0, rSum = 0;
                for (int dy = -1; dy <= 1; ++dy)
                {
                    const std::uint8_t* row = s->backdropPixels.data() + static_cast<size_t>(py + dy) * static_cast<size_t>(w) * 4u;
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const size_t off = static_cast<size_t>(px + dx) * 4u;
                        bSum += row[off + 0];
                        gSum += row[off + 1];
                        rSum += row[off + 2];
                    }
                }
                const size_t dst = (static_cast<size_t>(py) * static_cast<size_t>(w) + static_cast<size_t>(px)) * 4u;
                soft[dst + 0] = static_cast<std::uint8_t>(bSum / 9);
                soft[dst + 1] = static_cast<std::uint8_t>(gSum / 9);
                soft[dst + 2] = static_cast<std::uint8_t>(rSum / 9);
                soft[dst + 3] = 255;
            }
        }
        s->backdropPixels.swap(soft);
        s->backdropW = w;
        s->backdropH = h;
    }
    if (hbmp) DeleteObject(hbmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void DrawLiquidGlassSurface(OverlayState* s, const D2D1_SIZE_F& sz, bool dark)
{
    const D2D1_RECT_F full = D2D1::RectF(0.0f, 0.0f, sz.width, sz.height);
    if (!s->backdropBmp && !s->backdropPixels.empty() && s->backdropW > 0 && s->backdropH > 0)
    {
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        s->rt->CreateBitmap(D2D1::SizeU(static_cast<UINT>(s->backdropW), static_cast<UINT>(s->backdropH)),
            s->backdropPixels.data(), static_cast<UINT>(s->backdropW * 4), props, &s->backdropBmp);
    }

    if (s->backdropBmp)
    {
        // 1. Crystal-clear live desktop backdrop
        s->rt->DrawBitmap(s->backdropBmp, full, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

        // 2. Curved 28 DIP liquid meniscus edge refraction ring (1.045x optical magnification)
        ID2D1RoundedRectangleGeometry* outerG = nullptr;
        ID2D1RoundedRectangleGeometry* innerG = nullptr;
        ID2D1PathGeometry* ringG = nullptr;
        if (SUCCEEDED(s->d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(full, 14.0f, 14.0f), &outerG)) &&
            SUCCEEDED(s->d2d->CreateRoundedRectangleGeometry(
                D2D1::RoundedRect(D2D1::RectF(26.0f, 26.0f, sz.width - 26.0f, sz.height - 26.0f), 8.0f, 8.0f), &innerG)) &&
            SUCCEEDED(s->d2d->CreatePathGeometry(&ringG)))
        {
            ID2D1GeometrySink* sink = nullptr;
            if (SUCCEEDED(ringG->Open(&sink)))
            {
                outerG->CombineWithGeometry(innerG, D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink);
                sink->Close();
                sink->Release();
                ID2D1Layer* layer = nullptr;
                if (SUCCEEDED(s->rt->CreateLayer(nullptr, &layer)))
                {
                    s->rt->PushLayer(D2D1::LayerParameters(full, ringG), layer);
                    const float ex = sz.width * 0.024f;
                    const float ey = sz.height * 0.024f;
                    s->rt->DrawBitmap(s->backdropBmp, D2D1::RectF(-ex, -ey, sz.width + ex, sz.height + ey), 0.78f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    s->rt->PopLayer();
                    layer->Release();
                }
            }
        }
        SafeRelease(ringG);
        SafeRelease(innerG);
        SafeRelease(outerG);
    }
    else
    {
        // Fallback atmospheric twilight backdrop for offscreen headless verification
        D2D1_GRADIENT_STOP stops[3] = {
            {0.0f, dark ? Rgba(12, 22, 42) : Rgba(214, 228, 248)},
            {0.55f, dark ? Rgba(20, 32, 58) : Rgba(234, 240, 250)},
            {1.0f, dark ? Rgba(10, 15, 28) : Rgba(220, 232, 246)}
        };
        ID2D1GradientStopCollection* sc = nullptr;
        ID2D1LinearGradientBrush* lg = nullptr;
        if (SUCCEEDED(s->rt->CreateGradientStopCollection(stops, 3, &sc)) &&
            SUCCEEDED(s->rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(sz.width, sz.height)), sc, &lg)))
        {
            s->rt->FillRectangle(full, lg);
        }
        SafeRelease(lg);
        SafeRelease(sc);
    }

    // 3. Ultra-light Crystal Liquid Glass surface veil + top inner sheen
    s->brush->SetColor(dark ? Rgba(12, 16, 26, 0.34f) : Rgba(255, 255, 255, 0.36f));
    s->rt->FillRectangle(full, s->brush);

    D2D1_GRADIENT_STOP sheenStops[2] = {
        {0.0f, Rgba(255, 255, 255, dark ? 0.16f : 0.45f)},
        {1.0f, Rgba(255, 255, 255, 0.0f)}
    };
    ID2D1GradientStopCollection* sheenSc = nullptr;
    ID2D1LinearGradientBrush* sheenBr = nullptr;
    if (SUCCEEDED(s->rt->CreateGradientStopCollection(sheenStops, 2, &sheenSc)) &&
        SUCCEEDED(s->rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 120.0f)), sheenSc, &sheenBr)))
    {
        s->rt->FillRectangle(D2D1::RectF(0, 0, sz.width, 120.0f), sheenBr);
    }
    SafeRelease(sheenBr);
    SafeRelease(sheenSc);

    // 4. Dual-angle diagonal specular glass rim
    D2D1_GRADIENT_STOP rimStops[5] = {
        {0.0f, Rgba(255, 255, 255, dark ? 0.80f : 0.95f)},
        {0.25f, Rgba(186, 230, 253, dark ? 0.32f : 0.50f)},
        {0.50f, Rgba(255, 255, 255, dark ? 0.08f : 0.20f)},
        {0.78f, Rgba(147, 197, 253, dark ? 0.28f : 0.45f)},
        {1.0f, Rgba(255, 255, 255, dark ? 0.65f : 0.88f)}
    };
    ID2D1GradientStopCollection* rimSc = nullptr;
    ID2D1LinearGradientBrush* rimBr = nullptr;
    if (SUCCEEDED(s->rt->CreateGradientStopCollection(rimStops, 5, &rimSc)) &&
        SUCCEEDED(s->rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(sz.width, sz.height)), rimSc, &rimBr)))
    {
        s->rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.8f, 0.8f, sz.width - 0.8f, sz.height - 0.8f), 14.0f, 14.0f), rimBr, 1.6f);
    }
    SafeRelease(rimBr);
    SafeRelease(rimSc);
}

void PaintOverlay(OverlayState* s)
{
    if (!EnsureResources(s)) return;
    const bool dark = !s->callbacks.isDarkMode || s->callbacks.isDarkMode();
    const bool zh = !s->callbacks.isChinese || s->callbacks.isChinese();
    const D2D1_SIZE_F sz = s->rt->GetSize();
    s->hitZones.clear();

    const D2D1_COLOR_F card = dark ? Rgba(16, 22, 36, 0.42f) : Rgba(255, 255, 255, 0.54f);
    const D2D1_COLOR_F cardSel = dark ? Rgba(255, 255, 255, 0.19f) : Rgba(255, 255, 255, 0.82f);
    const D2D1_COLOR_F border = dark ? Rgba(255, 255, 255, 0.16f) : Rgba(255, 255, 255, 0.62f);
    const D2D1_COLOR_F borderSel = dark ? Rgba(255, 255, 255, 0.52f) : Rgba(37, 99, 235, 0.60f);
    const D2D1_COLOR_F txtPrimary = dark ? Rgba(248, 250, 252) : Rgba(15, 23, 42);
    const D2D1_COLOR_F txtSec = dark ? Rgba(203, 213, 225, 0.85f) : Rgba(71, 85, 105);
    const D2D1_COLOR_F accent = dark ? Rgba(96, 165, 250) : Rgba(37, 99, 235);
    const D2D1_COLOR_F accentSoft = dark ? Rgba(59, 130, 246, 0.26f) : Rgba(37, 99, 235, 0.16f);

    s->rt->BeginDraw();
    DrawLiquidGlassSurface(s, sz, dark);

    // 1. Search Box
    const D2D1_RECT_F searchBox = D2D1::RectF(16.0f, 14.0f, sz.width - 16.0f, 54.0f);
    s->brush->SetColor(card);
    s->rt->FillRoundedRectangle(D2D1::RoundedRect(searchBox, 11.0f, 11.0f), s->brush);
    s->brush->SetColor(border);
    s->rt->DrawRoundedRectangle(D2D1::RoundedRect(searchBox, 11.0f, 11.0f), s->brush, 1.0f);

    // Search icon
    s->brush->SetColor(txtSec);
    s->rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(34.0f, 33.0f), 6.0f, 6.0f), s->brush, 1.6f);
    s->rt->DrawLine(D2D1::Point2F(38.5f, 37.5f), D2D1::Point2F(43.0f, 42.0f), s->brush, 1.8f);

    const std::wstring placeholder = zh ? L"搜索剪贴板内容、来源应用... (Alt+V)" : L"Search clipboard or source app... (Alt+V)";
    const std::wstring& shown = s->searchText.empty() ? placeholder : s->searchText;
    s->brush->SetColor(s->searchText.empty() ? txtSec : txtPrimary);
    s->rt->DrawTextW(shown.c_str(), static_cast<UINT32>(shown.size()), s->fmtSearch,
        D2D1::RectF(52.0f, 14.0f, sz.width - 82.0f, 54.0f), s->brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);

    // Count pill on right of search bar
    const std::wstring cntStr = std::to_wstring(s->visibleItems.size()) + (zh ? L" 条" : L" items");
    const D2D1_RECT_F cntR = D2D1::RectF(sz.width - 76.0f, 22.0f, sz.width - 24.0f, 46.0f);
    s->brush->SetColor(accentSoft);
    s->rt->FillRoundedRectangle(D2D1::RoundedRect(cntR, 7.0f, 7.0f), s->brush);
    s->brush->SetColor(dark ? Rgba(147, 197, 253) : accent);
    s->rt->DrawTextW(cntStr.c_str(), static_cast<UINT32>(cntStr.size()), s->fmtBadge, cntR, s->brush);

    // 2. Category Filter Tabs
    const std::array<std::wstring, 5> tabNames = {
        zh ? L"全部" : L"All",
        zh ? L"置顶" : L"Pinned",
        zh ? L"文本" : L"Text",
        zh ? L"图片" : L"Images",
        zh ? L"链接" : L"Links"
    };
    const float tabW = (sz.width - 32.0f - 24.0f) / 5.0f;
    for (int i = 0; i < 5; ++i)
    {
        const float tx = 16.0f + static_cast<float>(i) * (tabW + 6.0f);
        const D2D1_RECT_F tr = D2D1::RectF(tx, 64.0f, tx + tabW, 92.0f);
        const bool active = (static_cast<int>(s->activeTab) == i);
        s->brush->SetColor(active ? accentSoft : card);
        s->rt->FillRoundedRectangle(D2D1::RoundedRect(tr, 8.0f, 8.0f), s->brush);
        if (active)
        {
            s->brush->SetColor(borderSel);
            s->rt->DrawRoundedRectangle(D2D1::RoundedRect(tr, 8.0f, 8.0f), s->brush, 1.0f);
        }
        s->brush->SetColor(active ? (dark ? Rgba(191, 219, 254) : accent) : txtSec);
        s->rt->DrawTextW(tabNames[static_cast<size_t>(i)].c_str(), static_cast<UINT32>(tabNames[static_cast<size_t>(i)].size()), s->fmtTab, tr, s->brush);
        s->hitZones.push_back({HitType::Tab, i, tr});
    }

    // 3. Scrollable Items List
    const float listTop = 104.0f;
    const float listBot = sz.height - 38.0f;
    s->rt->PushAxisAlignedClip(D2D1::RectF(0.0f, listTop, sz.width, listBot), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (s->visibleItems.empty())
    {
        const std::wstring emptyMsg = zh ? L"暂无剪贴板记录，复制任意文本、链接、色值或截图即可自动收录" : L"No clipboard history yet. Copy text, links, colors, or screenshots.";
        s->brush->SetColor(txtSec);
        s->rt->DrawTextW(emptyMsg.c_str(), static_cast<UINT32>(emptyMsg.size()), s->fmtFooter,
            D2D1::RectF(28.0f, listTop + 60.0f, sz.width - 28.0f, listTop + 120.0f), s->brush);
    }
    else
    {
        for (size_t i = 0; i < s->visibleItems.size(); ++i)
        {
            const float ry = listTop + static_cast<float>(i) * (kRowH + 8.0f) - s->scrollY;
            if (ry + kRowH < listTop || ry > listBot) continue;

            const auto& item = s->visibleItems[i];
            const bool sel = (static_cast<int>(i) == s->selectedIndex);
            const bool hov = (static_cast<int>(i) == s->hoveredRow);
            const D2D1_RECT_F rr = D2D1::RectF(16.0f, ry, sz.width - 16.0f, ry + kRowH);

            s->brush->SetColor(sel ? cardSel : (hov ? (dark ? Rgba(34, 41, 56) : Rgba(248, 250, 255)) : card));
            s->rt->FillRoundedRectangle(D2D1::RoundedRect(rr, 11.0f, 11.0f), s->brush);
            s->brush->SetColor(sel ? borderSel : border);
            s->rt->DrawRoundedRectangle(D2D1::RoundedRect(rr, 11.0f, 11.0f), s->brush, sel ? 1.4f : 1.0f);

            float textLeft = rr.left + 14.0f;

            // Left visual badge / color swatch / image thumbnail
            if (item.kind == llp::clipboard::ClipKind::Color)
            {
                const int cr = (item.colorRgba >> 24) & 0xFF;
                const int cg = (item.colorRgba >> 16) & 0xFF;
                const int cb = (item.colorRgba >> 8) & 0xFF;
                const D2D1_RECT_F sw = D2D1::RectF(rr.left + 12.0f, ry + 13.0f, rr.left + 54.0f, ry + 55.0f);
                s->brush->SetColor(Rgba(cr, cg, cb));
                s->rt->FillRoundedRectangle(D2D1::RoundedRect(sw, 9.0f, 9.0f), s->brush);
                s->brush->SetColor(Rgba(255, 255, 255, 0.35f));
                s->rt->DrawRoundedRectangle(D2D1::RoundedRect(sw, 9.0f, 9.0f), s->brush, 1.0f);
                textLeft = rr.left + 66.0f;
            }
            else if (item.kind == llp::clipboard::ClipKind::Image)
            {
                const D2D1_RECT_F tr = D2D1::RectF(rr.left + 12.0f, ry + 11.0f, rr.left + 68.0f, ry + 57.0f);
                if (auto* bmp = GetThumbnail(s, item.imagePath))
                {
                    s->rt->DrawBitmap(bmp, tr);
                }
                else
                {
                    s->brush->SetColor(accentSoft);
                    s->rt->FillRoundedRectangle(D2D1::RoundedRect(tr, 7.0f, 7.0f), s->brush);
                }
                s->brush->SetColor(border);
                s->rt->DrawRoundedRectangle(D2D1::RoundedRect(tr, 7.0f, 7.0f), s->brush, 1.0f);
                textLeft = rr.left + 80.0f;
            }
            else
            {
                const D2D1_RECT_F br = D2D1::RectF(rr.left + 12.0f, ry + 20.0f, rr.left + 54.0f, ry + 48.0f);
                const D2D1_COLOR_F bBg = (item.kind == llp::clipboard::ClipKind::Url) ? Rgba(14, 165, 233, 0.22f)
                    : (item.kind == llp::clipboard::ClipKind::Files ? Rgba(245, 158, 11, 0.22f) : accentSoft);
                const D2D1_COLOR_F bFg = (item.kind == llp::clipboard::ClipKind::Url) ? Rgba(56, 189, 248)
                    : (item.kind == llp::clipboard::ClipKind::Files ? Rgba(251, 191, 36) : Rgba(147, 197, 253));
                const wchar_t* tag = (item.kind == llp::clipboard::ClipKind::Url) ? L"URL"
                    : (item.kind == llp::clipboard::ClipKind::Files ? L"FILE" : L"TXT");
                s->brush->SetColor(bBg);
                s->rt->FillRoundedRectangle(D2D1::RoundedRect(br, 7.0f, 7.0f), s->brush);
                s->brush->SetColor(bFg);
                s->rt->DrawTextW(tag, static_cast<UINT32>(wcslen(tag)), s->fmtBadge, br, s->brush);
                textLeft = rr.left + 64.0f;
            }

            // Title preview
            const float textRight = rr.right - 74.0f;
            s->brush->SetColor(txtPrimary);
            s->rt->DrawTextW(item.preview.c_str(), static_cast<UINT32>(item.preview.size()), s->fmtTitle,
                D2D1::RectF(textLeft, ry + 10.0f, textRight, ry + 36.0f), s->brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);

            // Metadata line (Pinned badge + Source App + Relative Time)
            std::wstring meta;
            if (item.pinned) meta += zh ? L"★ 已置顶 · " : L"★ Pinned · ";
            if (!item.sourceApp.empty()) meta += item.sourceApp + L" · ";
            meta += FormatRelativeTime(item.timestampMs, zh);
            s->brush->SetColor(item.pinned ? Rgba(251, 191, 36) : txtSec);
            s->rt->DrawTextW(meta.c_str(), static_cast<UINT32>(meta.size()), s->fmtMeta,
                D2D1::RectF(textLeft, ry + 36.0f, textRight, ry + 58.0f), s->brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);

            // Pin & Delete inline action pills
            const D2D1_RECT_F pinR = D2D1::RectF(rr.right - 66.0f, ry + 20.0f, rr.right - 40.0f, ry + 48.0f);
            const D2D1_RECT_F delR = D2D1::RectF(rr.right - 36.0f, ry + 20.0f, rr.right - 10.0f, ry + 48.0f);
            s->brush->SetColor(item.pinned ? Rgba(245, 158, 11, 0.22f) : Rgba(255, 255, 255, dark ? 0.05f : 0.0f));
            s->rt->FillRoundedRectangle(D2D1::RoundedRect(pinR, 6.0f, 6.0f), s->brush);
            s->brush->SetColor(item.pinned ? Rgba(251, 191, 36) : txtSec);
            s->rt->DrawTextW(L"★", 1, s->fmtTab, pinR, s->brush);

            s->brush->SetColor(Rgba(255, 255, 255, dark ? 0.05f : 0.0f));
            s->rt->FillRoundedRectangle(D2D1::RoundedRect(delR, 6.0f, 6.0f), s->brush);
            s->brush->SetColor(txtSec);
            s->rt->DrawTextW(L"×", 1, s->fmtTab, delR, s->brush);

            s->hitZones.push_back({HitType::Row, static_cast<int>(i), rr});
            s->hitZones.push_back({HitType::PinBtn, static_cast<int>(i), pinR});
            s->hitZones.push_back({HitType::DelBtn, static_cast<int>(i), delR});
        }
    }
    s->rt->PopAxisAlignedClip();

    // 4. Footer Shortcut Legend
    s->brush->SetColor(border);
    s->rt->DrawLine(D2D1::Point2F(16.0f, listBot + 2.0f), D2D1::Point2F(sz.width - 16.0f, listBot + 2.0f), s->brush, 1.0f);
    const std::wstring footer = zh
        ? L"Enter 粘贴  ·  Shift+Enter 去格式纯文本  ·  Ctrl+P 置顶  ·  Del 删除  ·  Esc 关闭"
        : L"Enter Paste  ·  Shift+Enter Plain Text  ·  Ctrl+P Pin  ·  Del Delete  ·  Esc Close";
    s->brush->SetColor(txtSec);
    s->rt->DrawTextW(footer.c_str(), static_cast<UINT32>(footer.size()), s->fmtFooter,
        D2D1::RectF(12.0f, listBot + 4.0f, sz.width - 12.0f, sz.height - 4.0f), s->brush);

    if (s->rt->EndDraw() == D2DERR_RECREATE_TARGET) ReleaseDevice(s);
}

void ExecutePaste(OverlayState* s, int index, bool plainTextOnly)
{
    if (index < 0 || index >= static_cast<int>(s->visibleItems.size())) return;
    const auto item = s->visibleItems[static_cast<size_t>(index)];
    HWND targetHwnd = s->prevForeground;
    HideClipboardOverlay();
    if (s->callbacks.getStore)
    {
        if (auto* store = s->callbacks.getStore())
        {
            if (store->RestoreToSystemClipboard(s->hwnd, item, plainTextOnly))
            {
                if (targetHwnd && IsWindow(targetHwnd))
                {
                    SetForegroundWindow(targetHwnd);
                    Sleep(45);
                    SendCtrlV();
                }
            }
        }
    }
}

void PositionOverlayNearCursor(OverlayState* s)
{
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);

    const UINT dpi = QueryDpi(s->hwnd);
    const float scale = static_cast<float>(dpi) / 96.0f;
    const int w = static_cast<int>(std::lround(kOverlayDipW * scale));
    const int h = static_cast<int>(std::lround(kOverlayDipH * scale));
    const int margin = static_cast<int>(std::lround(24.0f * scale));
    const int belowGap = static_cast<int>(std::lround(18.0f * scale));

    const int biasPct = s->callbacks.getMouseBiasPercent ? std::clamp(s->callbacks.getMouseBiasPercent(), 0, 100) : 72;
    const float bias = static_cast<float>(biasPct) / 100.0f;
    const float monCx = (mi.rcWork.left + mi.rcWork.right) * 0.5f;
    const float targetCx = monCx + (static_cast<float>(cursor.x) - monCx) * bias;

    const int minX = mi.rcWork.left + margin;
    const int maxX = std::max(minX, static_cast<int>(mi.rcWork.right) - w - margin);
    const int minY = mi.rcWork.top + margin;
    const int maxY = std::max(minY, static_cast<int>(mi.rcWork.bottom) - h - margin);
    const int x = std::clamp(static_cast<int>(std::lround(targetCx - w * 0.5f)), minX, maxX);
    // Place directly below the cursor whenever possible, safely clamped inside work area.
    const int y = std::clamp(static_cast<int>(cursor.y) + belowGap, minY, maxY);

    // Capture live desktop pixels behind (x, y, w, h) BEFORE showing for authentic Liquid Glass refraction!
    CaptureDesktopBackdrop(s, x, y, w, h);
    SetWindowPos(s->hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* s = reinterpret_cast<OverlayState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        s = reinterpret_cast<OverlayState*>(cs->lpCreateParams);
        s->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        return TRUE;
    }
    if (!s) return DefWindowProcW(hwnd, msg, wParam, lParam);
    const float scale = static_cast<float>(QueryDpi(hwnd)) / 96.0f;

    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (s->hwndRt) s->hwndRt->Resize(D2D1::SizeU(std::max<UINT>(1, LOWORD(lParam)), std::max<UINT>(1, HIWORD(lParam))));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        PaintOverlay(s);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) HideClipboardOverlay();
        return 0;
    case WM_MOUSEMOVE:
    {
        const float x = static_cast<float>(GET_X_LPARAM(lParam)) / scale;
        const float y = static_cast<float>(GET_Y_LPARAM(lParam)) / scale;
        int nextRow = -1;
        for (const auto& hz : s->hitZones)
        {
            if (hz.type == HitType::Row && x >= hz.rect.left && x <= hz.rect.right && y >= hz.rect.top && y <= hz.rect.bottom)
            {
                nextRow = hz.index;
                break;
            }
        }
        if (nextRow != s->hoveredRow)
        {
            s->hoveredRow = nextRow;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        const float x = static_cast<float>(GET_X_LPARAM(lParam)) / scale;
        const float y = static_cast<float>(GET_Y_LPARAM(lParam)) / scale;
        for (auto it = s->hitZones.rbegin(); it != s->hitZones.rend(); ++it)
        {
            if (x < it->rect.left || x > it->rect.right || y < it->rect.top || y > it->rect.bottom) continue;
            if (it->type == HitType::Tab)
            {
                s->activeTab = static_cast<llp::clipboard::FilterTab>(it->index);
                s->scrollY = 0.0f;
                s->selectedIndex = 0;
                RefreshQuery(s);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (it->type == HitType::PinBtn && it->index >= 0 && it->index < static_cast<int>(s->visibleItems.size()))
            {
                if (auto* store = s->callbacks.getStore ? s->callbacks.getStore() : nullptr)
                {
                    store->TogglePin(s->visibleItems[static_cast<size_t>(it->index)].id);
                    if (s->callbacks.onStoreMutated) s->callbacks.onStoreMutated();
                    RefreshQuery(s);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (it->type == HitType::DelBtn && it->index >= 0 && it->index < static_cast<int>(s->visibleItems.size()))
            {
                if (auto* store = s->callbacks.getStore ? s->callbacks.getStore() : nullptr)
                {
                    store->RemoveItem(s->visibleItems[static_cast<size_t>(it->index)].id);
                    if (s->callbacks.onStoreMutated) s->callbacks.onStoreMutated();
                    RefreshQuery(s);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (it->type == HitType::Row)
            {
                const bool plain = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                ExecutePaste(s, it->index, plain);
                return 0;
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        const float listH = kOverlayDipH - 144.0f;
        const float maxScroll = std::max(0.0f, static_cast<float>(s->visibleItems.size()) * (kRowH + 8.0f) - listH);
        s->scrollY = std::clamp(s->scrollY - static_cast<float>(delta) * 0.45f, 0.0f, maxScroll);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
    {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (wParam == VK_ESCAPE) { HideClipboardOverlay(); return 0; }
        if (wParam == VK_DOWN && !s->visibleItems.empty())
        {
            s->selectedIndex = std::min(static_cast<int>(s->visibleItems.size()) - 1, s->selectedIndex + 1);
            EnsureSelectedVisible(s);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_UP && !s->visibleItems.empty())
        {
            s->selectedIndex = std::max(0, s->selectedIndex - 1);
            EnsureSelectedVisible(s);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_RETURN && s->selectedIndex >= 0)
        {
            ExecutePaste(s, s->selectedIndex, shift);
            return 0;
        }
        if (wParam == VK_TAB)
        {
            s->activeTab = static_cast<llp::clipboard::FilterTab>((static_cast<int>(s->activeTab) + 1) % 5);
            s->scrollY = 0.0f;
            s->selectedIndex = 0;
            RefreshQuery(s);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_DELETE && s->selectedIndex >= 0 && s->selectedIndex < static_cast<int>(s->visibleItems.size()))
        {
            if (auto* store = s->callbacks.getStore ? s->callbacks.getStore() : nullptr)
            {
                store->RemoveItem(s->visibleItems[static_cast<size_t>(s->selectedIndex)].id);
                if (s->callbacks.onStoreMutated) s->callbacks.onStoreMutated();
                RefreshQuery(s);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (ctrl && wParam == 'P' && s->selectedIndex >= 0 && s->selectedIndex < static_cast<int>(s->visibleItems.size()))
        {
            if (auto* store = s->callbacks.getStore ? s->callbacks.getStore() : nullptr)
            {
                store->TogglePin(s->visibleItems[static_cast<size_t>(s->selectedIndex)].id);
                if (s->callbacks.onStoreMutated) s->callbacks.onStoreMutated();
                RefreshQuery(s);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (wParam == VK_BACK && !s->searchText.empty())
        {
            s->searchText.pop_back();
            s->selectedIndex = 0;
            s->scrollY = 0.0f;
            RefreshQuery(s);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    }
    case WM_CHAR:
    {
        const wchar_t ch = static_cast<wchar_t>(wParam);
        if (ch >= 32 && ch != 127)
        {
            s->searchText.push_back(ch);
            s->selectedIndex = 0;
            s->scrollY = 0.0f;
            RefreshQuery(s);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EnsureOverlayWindow(HWND owner, const OverlayCallbacks& callbacks)
{
    if (!g_state)
    {
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = OverlayWndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = kOverlayClassName;
            RegisterClassExW(&wc);
            registered = true;
        }
        g_state = new OverlayState();
        g_state->callbacks = callbacks;
        CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            kOverlayClassName,
            L"LightLaunchpad Clipboard",
            WS_POPUP,
            0, 0, 540, 600,
            owner, nullptr, GetModuleHandleW(nullptr), g_state);
    }
    else
    {
        g_state->callbacks = callbacks;
    }
}

void ApplyOverlayDwm(OverlayState* s)
{
    const BOOL dark = (!s->callbacks.isDarkMode || s->callbacks.isDarkMode()) ? TRUE : FALSE;
    DwmSetWindowAttribute(s->hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(s->hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    const DWORD backdrop = DWMSBT_TRANSIENTWINDOW;
    DwmSetWindowAttribute(s->hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
}

} // namespace

void ToggleClipboardOverlay(HWND owner, const OverlayCallbacks& callbacks)
{
    if (IsClipboardOverlayVisible())
    {
        HideClipboardOverlay();
        return;
    }
    HWND fg = GetForegroundWindow();
    EnsureOverlayWindow(owner, callbacks);
    if (!g_state || !g_state->hwnd) return;
    g_state->prevForeground = (fg && fg != g_state->hwnd) ? fg : nullptr;
    g_state->searchText.clear();
    g_state->activeTab = llp::clipboard::FilterTab::All;
    g_state->selectedIndex = 0;
    g_state->hoveredRow = -1;
    g_state->scrollY = 0.0f;
    RefreshQuery(g_state);
    ApplyOverlayDwm(g_state);
    PositionOverlayNearCursor(g_state);
    ShowWindow(g_state->hwnd, SW_SHOW);
    SetForegroundWindow(g_state->hwnd);
    SetFocus(g_state->hwnd);
    InvalidateRect(g_state->hwnd, nullptr, FALSE);
}

void HideClipboardOverlay()
{
    if (g_state && g_state->hwnd && IsWindowVisible(g_state->hwnd))
    {
        ShowWindow(g_state->hwnd, SW_HIDE);
        ReleaseThumbs(g_state); // Keep hidden RAM < 60 MiB!
    }
}

bool IsClipboardOverlayVisible()
{
    return g_state && g_state->hwnd && IsWindowVisible(g_state->hwnd);
}

bool CaptureClipboardOverlayPng(HWND owner, const OverlayCallbacks& callbacks, const std::wstring& outputPath)
{
    EnsureOverlayWindow(owner, callbacks);
    if (!g_state || !g_state->hwnd || !EnsureResources(g_state)) return false;
    RefreshQuery(g_state);
    const UINT dpi = QueryDpi(g_state->hwnd);
    const float scale = static_cast<float>(dpi) / 96.0f;
    const UINT w = static_cast<UINT>(std::lround(kOverlayDipW * scale));
    const UINT h = static_cast<UINT>(std::lround(kOverlayDipH * scale));

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
        if (SUCCEEDED(g_state->d2d->CreateWicBitmapRenderTarget(wicBmp, props, &wicRt)) &&
            SUCCEEDED(wicRt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &wicBrush)))
        {
            wicRt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            auto* prevRt = g_state->rt;
            auto* prevBr = g_state->brush;
            g_state->rt = wicRt;
            g_state->brush = wicBrush;
            PaintOverlay(g_state);
            g_state->rt = prevRt;
            g_state->brush = prevBr;

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

} // namespace llp::clipboard_ui
