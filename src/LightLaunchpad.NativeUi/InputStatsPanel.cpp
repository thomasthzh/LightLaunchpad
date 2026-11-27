#include "InputStatsPanel.h"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace llp::stats
{
namespace
{
constexpr wchar_t kPanelClassName[] = L"LightLaunchpadInputStatsPanel";

struct PanelState
{
    Snapshot snapshot;
    bool darkMode = true;
    bool chinese = true;
    int dpi = 96;
    ID2D1Factory* d2dFactory = nullptr;
    IDWriteFactory* dwriteFactory = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;

    ~PanelState()
    {
        if (target) target->Release();
        if (dwriteFactory) dwriteFactory->Release();
        if (d2dFactory) d2dFactory->Release();
    }
};

struct KeyDef
{
    std::uint8_t vk;
    const wchar_t* label;
    float weight; // relative width in key units
};

D2D1_COLOR_F LerpColor(D2D1_COLOR_F a, D2D1_COLOR_F b, float t)
{
    const float u = std::clamp(t, 0.0f, 1.0f);
    return D2D1::ColorF(
        a.r + (b.r - a.r) * u,
        a.g + (b.g - a.g) * u,
        a.b + (b.b - a.b) * u,
        a.a + (b.a - a.a) * u);
}

D2D1_COLOR_F HeatColor(float norm, bool darkMode)
{
    const D2D1_COLOR_F c0 = darkMode ? D2D1::ColorF(0.16f, 0.19f, 0.25f, 1.0f) : D2D1::ColorF(0.91f, 0.93f, 0.96f, 1.0f);
    const D2D1_COLOR_F c1 = darkMode ? D2D1::ColorF(0.14f, 0.36f, 0.56f, 1.0f) : D2D1::ColorF(0.68f, 0.85f, 0.99f, 1.0f);
    const D2D1_COLOR_F c2 = D2D1::ColorF(0.12f, 0.62f, 0.92f, 1.0f);
    const D2D1_COLOR_F c3 = D2D1::ColorF(0.96f, 0.62f, 0.14f, 1.0f);
    const D2D1_COLOR_F c4 = D2D1::ColorF(0.94f, 0.32f, 0.32f, 1.0f);

    if (norm <= 0.001f) return c0;
    if (norm < 0.25f) return LerpColor(c0, c1, norm / 0.25f);
    if (norm < 0.55f) return LerpColor(c1, c2, (norm - 0.25f) / 0.30f);
    if (norm < 0.82f) return LerpColor(c2, c3, (norm - 0.55f) / 0.27f);
    return LerpColor(c3, c4, (norm - 0.82f) / 0.18f);
}

std::wstring FormatNumber(std::uint64_t value)
{
    std::wstring s = std::to_wstring(value);
    std::wstring out;
    int count = 0;
    for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i)
    {
        if (count > 0 && count % 3 == 0) out.push_back(L',');
        out.push_back(s[static_cast<std::size_t>(i)]);
        ++count;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::wstring FormatMeters(double meters)
{
    wchar_t buf[32] = {};
    if (meters >= 1000.0)
        std::swprintf(buf, 32, L"%.2f km", meters / 1000.0);
    else
        std::swprintf(buf, 32, L"%.1f m", meters);
    return buf;
}

void EnsureDeviceResources(HWND hwnd, PanelState* state)
{
    if (!state->d2dFactory)
    {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &state->d2dFactory);
    }
    if (!state->dwriteFactory)
    {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&state->dwriteFactory));
    }
    if (!state->target && state->d2dFactory)
    {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const D2D1_SIZE_U size = D2D1::SizeU(
            static_cast<UINT32>(std::max<LONG>(1, rc.right - rc.left)),
            static_cast<UINT32>(std::max<LONG>(1, rc.bottom - rc.top)));
        state->d2dFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, size),
            &state->target);
        if (state->target)
        {
            state->target->SetDpi(96.0f, 96.0f);
            state->target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        }
    }
}

void PaintPanel(HWND hwnd, PanelState* state)
{
    EnsureDeviceResources(hwnd, state);
    if (!state->target || !state->dwriteFactory) return;

    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const float w = static_cast<float>(std::max<LONG>(1, rc.right - rc.left));
    const float h = static_cast<float>(std::max<LONG>(1, rc.bottom - rc.top));
    const float s = std::max(0.75f, static_cast<float>(state->dpi) / 96.0f);

    state->target->Resize(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)));
    auto* rt = state->target;
    auto* dw = state->dwriteFactory;
    const bool dark = state->darkMode;
    const bool zh = state->chinese;

    const D2D1_COLOR_F bgCol = dark ? D2D1::ColorF(0.11f, 0.13f, 0.17f, 1.0f) : D2D1::ColorF(0.95f, 0.96f, 0.98f, 1.0f);
    const D2D1_COLOR_F cardCol = dark ? D2D1::ColorF(0.15f, 0.18f, 0.23f, 1.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
    const D2D1_COLOR_F borderCol = dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.09f) : D2D1::ColorF(0.06f, 0.10f, 0.18f, 0.09f);
    const D2D1_COLOR_F textCol = dark ? D2D1::ColorF(0.95f, 0.97f, 1.0f, 1.0f) : D2D1::ColorF(0.09f, 0.12f, 0.18f, 1.0f);
    const D2D1_COLOR_F mutedCol = dark ? D2D1::ColorF(0.64f, 0.70f, 0.79f, 1.0f) : D2D1::ColorF(0.40f, 0.45f, 0.54f, 1.0f);

    ID2D1SolidColorBrush* cardBrush = nullptr;
    ID2D1SolidColorBrush* borderBrush = nullptr;
    ID2D1SolidColorBrush* textBrush = nullptr;
    ID2D1SolidColorBrush* mutedBrush = nullptr;
    ID2D1SolidColorBrush* dynBrush = nullptr;
    rt->CreateSolidColorBrush(cardCol, &cardBrush);
    rt->CreateSolidColorBrush(borderCol, &borderBrush);
    rt->CreateSolidColorBrush(textCol, &textBrush);
    rt->CreateSolidColorBrush(mutedCol, &mutedBrush);
    rt->CreateSolidColorBrush(textCol, &dynBrush);

    IDWriteTextFormat* kpiValFmt = nullptr;
    IDWriteTextFormat* kpiLblFmt = nullptr;
    IDWriteTextFormat* secFmt = nullptr;
    IDWriteTextFormat* keyFmt = nullptr;
    IDWriteTextFormat* tinyFmt = nullptr;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f * s, L"", &kpiValFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f * s, L"", &kpiLblFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f * s, L"", &secFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f * s, L"", &keyFmt);
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.5f * s, L"", &tinyFmt);
    if (keyFmt) { keyFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); keyFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }
    if (tinyFmt) { tinyFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); tinyFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }

    rt->BeginDraw();
    rt->Clear(bgCol);

    const float pad = 10.0f * s;
    const float gap = 10.0f * s;
    const float cardH = 66.0f * s;
    const float cardW = (w - pad * 2.0f - gap * 3.0f) * 0.25f;

    // 1. Top 4 KPI Cards
    const auto& today = state->snapshot.today;
    struct KpiItem { std::wstring label; std::wstring value; D2D1_COLOR_F accent; };
    const KpiItem kpis[4] = {
        { zh ? L"今日按键" : L"Keystrokes", FormatNumber(today.keystrokes), D2D1::ColorF(0.22f, 0.74f, 0.97f, 1.0f) },
        { zh ? L"鼠标点击" : L"Clicks", FormatNumber(today.TotalClicks()), D2D1::ColorF(0.06f, 0.73f, 0.51f, 1.0f) },
        { zh ? L"滚轮滚动" : L"Scrolls", FormatNumber(today.wheelSteps), D2D1::ColorF(0.55f, 0.36f, 0.96f, 1.0f) },
        { zh ? L"移动距离" : L"Distance", FormatMeters(today.MouseDistanceMeters()), D2D1::ColorF(0.96f, 0.62f, 0.04f, 1.0f) }
    };
    for (int i = 0; i < 4; ++i)
    {
        const float x = pad + i * (cardW + gap);
        const D2D1_RECT_F r = D2D1::RectF(x, pad, x + cardW, pad + cardH);
        const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 10.0f * s, 10.0f * s);
        rt->FillRoundedRectangle(rr, cardBrush);
        rt->DrawRoundedRectangle(rr, borderBrush, 1.0f);
        // Subtle top accent bar
        dynBrush->SetColor(kpis[i].accent);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 12.0f * s, pad + 8.0f * s, x + 36.0f * s, pad + 11.5f * s), 2.0f * s, 2.0f * s), dynBrush);
        if (kpiLblFmt) rt->DrawText(kpis[i].label.c_str(), static_cast<UINT32>(kpis[i].label.size()), kpiLblFmt,
            D2D1::RectF(x + 12.0f * s, pad + 16.0f * s, x + cardW - 8.0f * s, pad + 34.0f * s), mutedBrush);
        if (kpiValFmt) rt->DrawText(kpis[i].value.c_str(), static_cast<UINT32>(kpis[i].value.size()), kpiValFmt,
            D2D1::RectF(x + 12.0f * s, pad + 34.0f * s, x + cardW - 8.0f * s, pad + cardH - 4.0f * s), textBrush);
    }

    // 2. Keyboard Heatmap Card
    const float kbTop = pad + cardH + gap;
    const float trendH = 104.0f * s;
    const float kbH = std::max(145.0f * s, h - kbTop - trendH - gap - pad);
    const D2D1_RECT_F kbCard = D2D1::RectF(pad, kbTop, w - pad, kbTop + kbH);
    const D2D1_ROUNDED_RECT kbRR = D2D1::RoundedRect(kbCard, 12.0f * s, 12.0f * s);
    rt->FillRoundedRectangle(kbRR, cardBrush);
    rt->DrawRoundedRectangle(kbRR, borderBrush, 1.0f);

    const std::wstring kbTitle = zh ? L"今日键盘热力图（仅本地计数，绝不记录文本内容）" : L"Today's Keyboard Heatmap (Local counts only, zero text logged)";
    if (secFmt) rt->DrawText(kbTitle.c_str(), static_cast<UINT32>(kbTitle.size()), secFmt,
        D2D1::RectF(kbCard.left + 14.0f * s, kbCard.top + 8.0f * s, kbCard.right - 14.0f * s, kbCard.top + 28.0f * s), mutedBrush);

    std::uint64_t maxKey = 1;
    for (std::size_t vk = 1; vk < 256; ++vk) maxKey = std::max(maxKey, today.keyCounts[vk]);

    const std::vector<std::vector<KeyDef>> rows = {
        { {VK_ESCAPE,L"Esc",1.1f},{'1',L"1",1},{'2',L"2",1},{'3',L"3",1},{'4',L"4",1},{'5',L"5",1},{'6',L"6",1},{'7',L"7",1},{'8',L"8",1},{'9',L"9",1},{'0',L"0",1},{VK_OEM_MINUS,L"-",1},{VK_OEM_PLUS,L"=",1},{VK_BACK,L"BS",1.9f} },
        { {VK_TAB,L"Tab",1.5f},{'Q',L"Q",1},{'W',L"W",1},{'E',L"E",1},{'R',L"R",1},{'T',L"T",1},{'Y',L"Y",1},{'U',L"U",1},{'I',L"I",1},{'O',L"O",1},{'P',L"P",1},{VK_OEM_4,L"[",1},{VK_OEM_6,L"]",1},{VK_OEM_5,L"\\",1.5f} },
        { {VK_CAPITAL,L"Caps",1.8f},{'A',L"A",1},{'S',L"S",1},{'D',L"D",1},{'F',L"F",1},{'G',L"G",1},{'H',L"H",1},{'J',L"J",1},{'K',L"K",1},{'L',L"L",1},{VK_OEM_1,L";",1},{VK_OEM_7,L"'",1},{VK_RETURN,L"Enter",2.2f} },
        { {VK_SHIFT,L"Shift",2.3f},{'Z',L"Z",1},{'X',L"X",1},{'C',L"C",1},{'V',L"V",1},{'B',L"B",1},{'N',L"N",1},{'M',L"M",1},{VK_OEM_COMMA,L",",1},{VK_OEM_PERIOD,L".",1},{VK_OEM_2,L"/",1},{VK_RSHIFT,L"Shift",2.7f} },
        { {VK_CONTROL,L"Ctrl",1.4f},{VK_LWIN,L"Win",1.2f},{VK_MENU,L"Alt",1.2f},{VK_SPACE,L"Space",6.4f},{VK_LEFT,L"\u2190",1.2f},{VK_UP,L"\u2191",1.2f},{VK_DOWN,L"\u2193",1.2f},{VK_RIGHT,L"\u2192",1.2f} }
    };

    const float kbInnerX = kbCard.left + 12.0f * s;
    const float kbInnerY = kbCard.top + 30.0f * s;
    const float kbInnerW = (kbCard.right - kbCard.left) - 24.0f * s;
    const float kbInnerH = (kbCard.bottom - kbCard.top) - 40.0f * s;
    const float kGap = 4.0f * s;
    const float rowH = std::max(14.0f * s, (kbInnerH - kGap * 4.0f) / 5.0f);

    for (std::size_t rIdx = 0; rIdx < rows.size(); ++rIdx)
    {
        const auto& row = rows[rIdx];
        float totalW = 0.0f;
        for (const auto& k : row) totalW += k.weight;
        const float unitW = (kbInnerW - kGap * static_cast<float>(row.size() - 1)) / std::max(1.0f, totalW);
        float cx = kbInnerX;
        const float cy = kbInnerY + static_cast<float>(rIdx) * (rowH + kGap);

        for (const auto& k : row)
        {
            const float kw = k.weight * unitW;
            std::uint64_t cnt = today.keyCounts[k.vk];
            if (k.vk == VK_SHIFT) cnt += today.keyCounts[VK_LSHIFT];
            if (k.vk == VK_CONTROL) cnt += today.keyCounts[VK_LCONTROL] + today.keyCounts[VK_RCONTROL];
            if (k.vk == VK_MENU) cnt += today.keyCounts[VK_LMENU] + today.keyCounts[VK_RMENU];

            const float norm = cnt > 0 ? static_cast<float>(std::log1p(static_cast<double>(cnt)) / std::log1p(static_cast<double>(maxKey))) : 0.0f;
            dynBrush->SetColor(HeatColor(norm, dark));
            const D2D1_RECT_F kr = D2D1::RectF(cx, cy, cx + kw, cy + rowH);
            const D2D1_ROUNDED_RECT krr = D2D1::RoundedRect(kr, 5.0f * s, 5.0f * s);
            rt->FillRoundedRectangle(krr, dynBrush);
            rt->DrawRoundedRectangle(krr, borderBrush, 0.8f);

            const D2D1_COLOR_F lblCol = (norm > 0.35f) ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f) : textCol;
            dynBrush->SetColor(lblCol);
            if (keyFmt) rt->DrawText(k.label, static_cast<UINT32>(std::wcslen(k.label)), keyFmt, kr, dynBrush);
            cx += kw + kGap;
        }
    }

    // 3. 7-Day Daily Trend Bar Chart
    const float trTop = kbCard.bottom + gap;
    const D2D1_RECT_F trCard = D2D1::RectF(pad, trTop, w - pad, h - pad);
    if (trCard.bottom > trCard.top + 30.0f * s)
    {
        const D2D1_ROUNDED_RECT trRR = D2D1::RoundedRect(trCard, 12.0f * s, 12.0f * s);
        rt->FillRoundedRectangle(trRR, cardBrush);
        rt->DrawRoundedRectangle(trRR, borderBrush, 1.0f);

        const std::wstring trTitle = zh ? L"近期每日按键趋势" : L"Recent Daily Keystrokes";
        if (secFmt) rt->DrawText(trTitle.c_str(), static_cast<UINT32>(trTitle.size()), secFmt,
            D2D1::RectF(trCard.left + 14.0f * s, trCard.top + 7.0f * s, trCard.right - 14.0f * s, trCard.top + 25.0f * s), mutedBrush);

        const auto& days = state->snapshot.recentDays;
        const std::size_t showCount = std::min<std::size_t>(7, days.size());
        const std::size_t startIdx = days.size() - showCount;
        std::uint64_t maxDay = 1;
        for (std::size_t i = startIdx; i < days.size(); ++i) maxDay = std::max(maxDay, days[i].keystrokes);

        const float chartX = trCard.left + 16.0f * s;
        const float chartW = (trCard.right - trCard.left) - 32.0f * s;
        const float chartTop = trCard.top + 34.0f * s;
        const float chartBottom = trCard.bottom - 20.0f * s;
        const float chartH = std::max(10.0f * s, chartBottom - chartTop);
        const float slotW = chartW / 7.0f;
        const float barW = std::min(34.0f * s, slotW * 0.56f);

        for (std::size_t k = 0; k < showCount; ++k)
        {
            const auto& d = days[startIdx + k];
            const float cx = chartX + (static_cast<float>(k) + 0.5f) * slotW;
            const float ratio = static_cast<float>(d.keystrokes) / static_cast<float>(maxDay);
            const float bh = std::max(3.0f * s, ratio * chartH);
            const bool isToday = (d.date == today.date);

            dynBrush->SetColor(isToday
                ? D2D1::ColorF(0.22f, 0.74f, 0.97f, 1.0f)
                : (dark ? D2D1::ColorF(0.32f, 0.45f, 0.62f, 0.85f) : D2D1::ColorF(0.55f, 0.72f, 0.92f, 0.90f)));
            const D2D1_RECT_F br = D2D1::RectF(cx - barW * 0.5f, chartBottom - bh, cx + barW * 0.5f, chartBottom);
            rt->FillRoundedRectangle(D2D1::RoundedRect(br, 4.0f * s, 4.0f * s), dynBrush);

            if (tinyFmt && d.keystrokes > 0)
            {
                const std::wstring valStr = FormatNumber(d.keystrokes);
                rt->DrawText(valStr.c_str(), static_cast<UINT32>(valStr.size()), tinyFmt,
                    D2D1::RectF(cx - slotW * 0.5f, chartBottom - bh - 14.0f * s, cx + slotW * 0.5f, chartBottom - bh), mutedBrush);
            }
            if (tinyFmt && d.date.size() >= 10)
            {
                const std::wstring dateLbl(d.date.begin() + 5, d.date.end()); // MM-DD
                rt->DrawText(dateLbl.c_str(), static_cast<UINT32>(dateLbl.size()), tinyFmt,
                    D2D1::RectF(cx - slotW * 0.5f, chartBottom + 2.0f * s, cx + slotW * 0.5f, trCard.bottom - 2.0f * s), isToday ? textBrush : mutedBrush);
            }
        }
    }

    const HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        state->target->Release();
        state->target = nullptr;
    }

    if (tinyFmt) tinyFmt->Release();
    if (keyFmt) keyFmt->Release();
    if (secFmt) secFmt->Release();
    if (kpiLblFmt) kpiLblFmt->Release();
    if (kpiValFmt) kpiValFmt->Release();
    if (dynBrush) dynBrush->Release();
    if (mutedBrush) mutedBrush->Release();
    if (textBrush) textBrush->Release();
    if (borderBrush) borderBrush->Release();
    if (cardBrush) cardBrush->Release();
}

LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<PanelState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_CREATE:
        state = new PanelState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps = {};
        BeginPaint(hwnd, &ps);
        if (state) PaintPanel(hwnd, state);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
}

bool RegisterInputStatsPanelClass(HINSTANCE instance)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PanelWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPanelClassName;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND CreateInputStatsPanel(HWND parent, HINSTANCE instance, int controlId, int x, int y, int width, int height)
{
    RegisterInputStatsPanelClass(instance);
    return CreateWindowExW(
        0,
        kPanelClassName,
        L"",
        WS_CHILD | WS_VISIBLE,
        x, y, width, height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
        instance,
        nullptr);
}

void UpdateInputStatsPanel(HWND panelHwnd, const Snapshot& snapshot, bool darkMode, bool chinese, int dpi)
{
    if (!panelHwnd || !IsWindow(panelHwnd)) return;
    auto* state = reinterpret_cast<PanelState*>(GetWindowLongPtrW(panelHwnd, GWLP_USERDATA));
    if (!state) return;
    state->snapshot = snapshot;
    state->darkMode = darkMode;
    state->chinese = chinese;
    state->dpi = std::clamp(dpi, 72, 384);
    InvalidateRect(panelHwnd, nullptr, FALSE);
}
}
