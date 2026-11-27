#include "ClipboardHistory.h"
#include "Persistence.h"

#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

namespace llp::clipboard
{
namespace
{

using Json = nlohmann::json;

template <typename T>
void SafeRelease(T*& ptr)
{
    if (ptr) { ptr->Release(); ptr = nullptr; }
}

std::uint64_t NowEpochMs()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::uint64_t Fnv1a64(const void* data, size_t bytes)
{
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < bytes; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

std::wstring ToLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return s;
}

std::wstring Trim(const std::wstring& s)
{
    size_t b = 0;
    while (b < s.size() && std::iswspace(s[b])) ++b;
    size_t e = s.size();
    while (e > b && std::iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring MakePreview(const std::wstring& text)
{
    std::wstring out;
    out.reserve(std::min<size_t>(180, text.size()));
    bool prevSpace = false;
    for (wchar_t ch : text)
    {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
        if (ch == L' ')
        {
            if (prevSpace || out.empty()) continue;
            prevSpace = true;
        }
        else
        {
            prevSpace = false;
        }
        out.push_back(ch);
        if (out.size() >= 160) break;
    }
    return out;
}

std::wstring MakeId(std::uint64_t ts, std::uint64_t hash)
{
    std::wostringstream oss;
    oss << std::hex << ts << L"_" << (hash & 0xFFFFFFFFULL);
    return oss.str();
}

int HexDigit(wchar_t c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return 10 + c - L'a';
    if (c >= L'A' && c <= L'F') return 10 + c - L'A';
    return -1;
}

bool SaveDibToPng(HGLOBAL hDib, const std::wstring& outputPath, int& outW, int& outH, std::uint64_t& outHash)
{
    const auto* bmi = static_cast<const BITMAPINFOHEADER*>(GlobalLock(hDib));
    if (!bmi || bmi->biWidth <= 0 || bmi->biHeight == 0 || (bmi->biBitCount != 32 && bmi->biBitCount != 24))
    {
        if (bmi) GlobalUnlock(hDib);
        return false;
    }

    const int w = bmi->biWidth;
    const int h = std::abs(bmi->biHeight);
    const bool topDown = bmi->biHeight < 0;
    const size_t colorTableBytes = (bmi->biCompression == BI_BITFIELDS) ? (3 * sizeof(DWORD)) : (bmi->biClrUsed * sizeof(RGBQUAD));
    const auto* srcPixels = reinterpret_cast<const std::uint8_t*>(bmi) + bmi->biSize + colorTableBytes;
    const int srcStride = ((w * bmi->biBitCount + 31) / 32) * 4;

    std::vector<std::uint8_t> bgra(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    for (int y = 0; y < h; ++y)
    {
        const int sy = topDown ? y : (h - 1 - y);
        const std::uint8_t* srcRow = srcPixels + static_cast<size_t>(sy) * static_cast<size_t>(srcStride);
        std::uint8_t* dstRow = bgra.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4u;
        if (bmi->biBitCount == 32)
        {
            for (int x = 0; x < w; ++x)
            {
                dstRow[x * 4 + 0] = srcRow[x * 4 + 0];
                dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
                dstRow[x * 4 + 2] = srcRow[x * 4 + 2];
                dstRow[x * 4 + 3] = 255;
            }
        }
        else
        {
            for (int x = 0; x < w; ++x)
            {
                dstRow[x * 4 + 0] = srcRow[x * 3 + 0];
                dstRow[x * 4 + 1] = srcRow[x * 3 + 1];
                dstRow[x * 4 + 2] = srcRow[x * 3 + 2];
                dstRow[x * 4 + 3] = 255;
            }
        }
    }
    GlobalUnlock(hDib);

    outW = w;
    outH = h;
    outHash = Fnv1a64(bgra.data(), std::min<size_t>(bgra.size(), 65536));

    bool ok = false;
    IWICImagingFactory* wic = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
        SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(static_cast<UINT>(w), static_cast<UINT>(h))))
    {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        frame->SetPixelFormat(&fmt);
        if (SUCCEEDED(frame->WritePixels(static_cast<UINT>(h), static_cast<UINT>(w * 4), static_cast<UINT>(bgra.size()), bgra.data())) &&
            SUCCEEDED(frame->Commit()) &&
            SUCCEEDED(encoder->Commit()))
        {
            ok = true;
        }
    }
    SafeRelease(frame);
    SafeRelease(encoder);
    SafeRelease(stream);
    SafeRelease(wic);
    return ok;
}

HGLOBAL LoadPngAsDib(const std::wstring& pngPath)
{
    IWICImagingFactory* wic = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    HGLOBAL hMem = nullptr;

    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateDecoderFromFilename(pngPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        UINT w = 0, h = 0;
        converter->GetSize(&w, &h);
        if (w > 0 && h > 0)
        {
            const size_t pixelBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
            hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + pixelBytes);
            if (hMem)
            {
                auto* dst = static_cast<std::uint8_t*>(GlobalLock(hMem));
                if (dst)
                {
                    auto* bih = reinterpret_cast<BITMAPINFOHEADER*>(dst);
                    *bih = {};
                    bih->biSize = sizeof(BITMAPINFOHEADER);
                    bih->biWidth = static_cast<LONG>(w);
                    bih->biHeight = -static_cast<LONG>(h); // top-down
                    bih->biPlanes = 1;
                    bih->biBitCount = 32;
                    bih->biCompression = BI_RGB;
                    bih->biSizeImage = static_cast<DWORD>(pixelBytes);
                    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixelBytes), dst + sizeof(BITMAPINFOHEADER))))
                    {
                        GlobalUnlock(hMem);
                        GlobalFree(hMem);
                        hMem = nullptr;
                    }
                    else
                    {
                        GlobalUnlock(hMem);
                    }
                }
                else
                {
                    GlobalFree(hMem);
                    hMem = nullptr;
                }
            }
        }
    }
    SafeRelease(converter);
    SafeRelease(frame);
    SafeRelease(decoder);
    SafeRelease(wic);
    return hMem;
}

} // namespace

ClipKind ClassifyText(const std::wstring& text, std::uint32_t& outRgba)
{
    outRgba = 0;
    const std::wstring t = Trim(text);
    if (t.empty()) return ClipKind::Text;

    // Color hex: #RGB, #RRGGBB, #RRGGBBAA
    if (t[0] == L'#' && (t.size() == 4 || t.size() == 7 || t.size() == 9))
    {
        bool ok = true;
        for (size_t i = 1; i < t.size(); ++i)
        {
            if (HexDigit(t[i]) < 0) { ok = false; break; }
        }
        if (ok)
        {
            std::uint32_t r = 0, g = 0, b = 0, a = 255;
            if (t.size() == 4)
            {
                r = static_cast<std::uint32_t>(HexDigit(t[1]) * 17);
                g = static_cast<std::uint32_t>(HexDigit(t[2]) * 17);
                b = static_cast<std::uint32_t>(HexDigit(t[3]) * 17);
            }
            else
            {
                r = static_cast<std::uint32_t>((HexDigit(t[1]) << 4) | HexDigit(t[2]));
                g = static_cast<std::uint32_t>((HexDigit(t[3]) << 4) | HexDigit(t[4]));
                b = static_cast<std::uint32_t>((HexDigit(t[5]) << 4) | HexDigit(t[6]));
                if (t.size() == 9) a = static_cast<std::uint32_t>((HexDigit(t[7]) << 4) | HexDigit(t[8]));
            }
            outRgba = (r << 24) | (g << 16) | (b << 8) | a;
            return ClipKind::Color;
        }
    }

    const std::wstring lower = ToLower(t);
    if ((lower.rfind(L"http://", 0) == 0 || lower.rfind(L"https://", 0) == 0) &&
        t.find(L'\n') == std::wstring::npos && t.find(L' ') == std::wstring::npos)
    {
        return ClipKind::Url;
    }
    return ClipKind::Text;
}

std::wstring QueryForegroundProcessName()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return L"";
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == 0) return L"";
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return L"";
    wchar_t path[MAX_PATH]{};
    DWORD len = MAX_PATH;
    std::wstring name;
    if (QueryFullProcessImageNameW(proc, 0, path, &len))
    {
        std::wstring full(path, len);
        const size_t slash = full.find_last_of(L"\\/");
        name = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
        if (name.size() > 4 && ToLower(name.substr(name.size() - 4)) == L".exe")
        {
            name.resize(name.size() - 4);
        }
    }
    CloseHandle(proc);
    return name;
}

std::string ClipKindToString(ClipKind kind)
{
    switch (kind)
    {
    case ClipKind::Text: return "text";
    case ClipKind::Url: return "url";
    case ClipKind::Color: return "color";
    case ClipKind::Image: return "image";
    case ClipKind::Files: return "files";
    }
    return "text";
}

ClipKind ClipKindFromString(const std::string& s)
{
    if (s == "url") return ClipKind::Url;
    if (s == "color") return ClipKind::Color;
    if (s == "image") return ClipKind::Image;
    if (s == "files") return ClipKind::Files;
    return ClipKind::Text;
}

ClipboardStore::ClipboardStore(size_t maxItems) : maxItems_(std::max<size_t>(10, maxItems)) {}

void ClipboardStore::SetMaxItems(size_t maxItems)
{
    maxItems_ = std::max<size_t>(10, maxItems);
    EnforceLimit();
}

void ClipboardStore::SortPinnedFirst()
{
    std::stable_sort(items_.begin(), items_.end(), [](const ClipItem& a, const ClipItem& b) {
        if (a.pinned != b.pinned) return a.pinned > b.pinned;
        return a.timestampMs > b.timestampMs;
    });
}

void ClipboardStore::EnforceLimit()
{
    SortPinnedFirst();
    while (items_.size() > maxItems_)
    {
        if (items_.back().pinned) break; // Never evict pinned items
        if (!items_.back().imagePath.empty()) DeleteFileW(items_.back().imagePath.c_str());
        items_.pop_back();
    }
}

bool ClipboardStore::AddText(const std::wstring& text, const std::wstring& sourceApp, std::uint64_t nowMs)
{
    if (!enabled_) return false;
    if (Trim(text).empty()) return false;
    // Cap single text entry at 256 KB to keep memory ultra-light
    std::wstring bounded = text.size() > 131072 ? text.substr(0, 131072) : text;
    const std::uint64_t hash = Fnv1a64(bounded.data(), bounded.size() * sizeof(wchar_t));
    const std::uint64_t ts = nowMs ? nowMs : NowEpochMs();

    for (auto& existing : items_)
    {
        if (existing.kind != ClipKind::Image && existing.kind != ClipKind::Files && existing.contentHash == hash && existing.text == bounded)
        {
            existing.timestampMs = ts;
            if (!sourceApp.empty()) existing.sourceApp = sourceApp;
            SortPinnedFirst();
            return true;
        }
    }

    ClipItem item;
    item.id = MakeId(ts, hash);
    item.kind = ClassifyText(bounded, item.colorRgba);
    item.text = std::move(bounded);
    item.preview = MakePreview(item.text);
    item.sourceApp = sourceApp;
    item.timestampMs = ts;
    item.contentHash = hash;
    items_.insert(items_.begin(), std::move(item));
    EnforceLimit();
    return true;
}

bool ClipboardStore::AddImage(const std::wstring& pngPath, int width, int height, std::uint64_t pixelHash, const std::wstring& sourceApp, std::uint64_t nowMs)
{
    if (!enabled_ || pngPath.empty()) return false;
    const std::uint64_t ts = nowMs ? nowMs : NowEpochMs();
    for (auto& existing : items_)
    {
        if (existing.kind == ClipKind::Image && existing.contentHash == pixelHash && existing.imageWidth == width && existing.imageHeight == height)
        {
            if (existing.imagePath != pngPath) DeleteFileW(pngPath.c_str());
            existing.timestampMs = ts;
            if (!sourceApp.empty()) existing.sourceApp = sourceApp;
            SortPinnedFirst();
            return true;
        }
    }

    ClipItem item;
    item.id = MakeId(ts, pixelHash);
    item.kind = ClipKind::Image;
    item.imagePath = pngPath;
    item.imageWidth = width;
    item.imageHeight = height;
    item.preview = std::to_wstring(width) + L" × " + std::to_wstring(height) + L" PNG";
    item.text = item.preview;
    item.sourceApp = sourceApp;
    item.timestampMs = ts;
    item.contentHash = pixelHash;
    items_.insert(items_.begin(), std::move(item));
    EnforceLimit();
    return true;
}

bool ClipboardStore::AddFiles(const std::vector<std::wstring>& paths, const std::wstring& sourceApp, std::uint64_t nowMs)
{
    if (!enabled_ || paths.empty()) return false;
    std::wstring joined;
    for (size_t i = 0; i < paths.size(); ++i)
    {
        if (i > 0) joined += L"\n";
        joined += paths[i];
    }
    const std::uint64_t hash = Fnv1a64(joined.data(), joined.size() * sizeof(wchar_t));
    const std::uint64_t ts = nowMs ? nowMs : NowEpochMs();
    for (auto& existing : items_)
    {
        if (existing.kind == ClipKind::Files && existing.contentHash == hash)
        {
            existing.timestampMs = ts;
            SortPinnedFirst();
            return true;
        }
    }

    ClipItem item;
    item.id = MakeId(ts, hash);
    item.kind = ClipKind::Files;
    item.filePaths = paths;
    item.text = joined;
    const size_t slash = paths[0].find_last_of(L"\\/");
    const std::wstring firstName = (slash == std::wstring::npos) ? paths[0] : paths[0].substr(slash + 1);
    item.preview = paths.size() == 1 ? firstName : (firstName + L" 等 " + std::to_wstring(paths.size()) + L" 个文件");
    item.sourceApp = sourceApp;
    item.timestampMs = ts;
    item.contentHash = hash;
    items_.insert(items_.begin(), std::move(item));
    EnforceLimit();
    return true;
}

bool ClipboardStore::CaptureFromSystemClipboard(HWND listenerHwnd)
{
    if (!enabled_) return false;
    if (suppressNext_)
    {
        suppressNext_ = false;
        return false;
    }

    const std::wstring sourceApp = QueryForegroundProcessName();
    if (!OpenClipboard(listenerHwnd)) return false;

    bool captured = false;
    if (IsClipboardFormatAvailable(CF_HDROP))
    {
        HANDLE hDrop = GetClipboardData(CF_HDROP);
        if (hDrop)
        {
            const UINT count = DragQueryFileW(static_cast<HDROP>(hDrop), 0xFFFFFFFFu, nullptr, 0);
            std::vector<std::wstring> files;
            for (UINT i = 0; i < std::min<UINT>(count, 32u); ++i)
            {
                wchar_t buf[MAX_PATH]{};
                if (DragQueryFileW(static_cast<HDROP>(hDrop), i, buf, MAX_PATH) > 0) files.emplace_back(buf);
            }
            CloseClipboard();
            return AddFiles(files, sourceApp);
        }
    }
    if (IsClipboardFormatAvailable(CF_UNICODETEXT))
    {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData)
        {
            const auto* raw = static_cast<const wchar_t*>(GlobalLock(hData));
            std::wstring text = raw ? std::wstring(raw) : std::wstring{};
            GlobalUnlock(hData);
            CloseClipboard();
            return AddText(text, sourceApp);
        }
    }
    if (IsClipboardFormatAvailable(CF_DIB) && !cacheDir_.empty())
    {
        HANDLE hDib = GetClipboardData(CF_DIB);
        if (hDib)
        {
            CreateDirectoryW(cacheDir_.c_str(), nullptr);
            const std::uint64_t ts = NowEpochMs();
            const std::wstring pngPath = cacheDir_ + L"\\clip_" + std::to_wstring(ts) + L".png";
            int w = 0, h = 0;
            std::uint64_t hash = 0;
            const bool saved = SaveDibToPng(hDib, pngPath, w, h, hash);
            CloseClipboard();
            if (saved) captured = AddImage(pngPath, w, h, hash, sourceApp, ts);
            return captured;
        }
    }

    CloseClipboard();
    return captured;
}

bool ClipboardStore::TogglePin(const std::wstring& id)
{
    for (auto& item : items_)
    {
        if (item.id == id)
        {
            item.pinned = !item.pinned;
            SortPinnedFirst();
            return true;
        }
    }
    return false;
}

bool ClipboardStore::RemoveItem(const std::wstring& id)
{
    for (auto it = items_.begin(); it != items_.end(); ++it)
    {
        if (it->id == id)
        {
            if (!it->imagePath.empty()) DeleteFileW(it->imagePath.c_str());
            items_.erase(it);
            return true;
        }
    }
    return false;
}

void ClipboardStore::ClearAll(bool keepPinned)
{
    items_.erase(std::remove_if(items_.begin(), items_.end(), [keepPinned](const ClipItem& item) {
        if (keepPinned && item.pinned) return false;
        if (!item.imagePath.empty()) DeleteFileW(item.imagePath.c_str());
        return true;
    }), items_.end());
}

std::vector<ClipItem> ClipboardStore::Query(const std::wstring& search, FilterTab tab) const
{
    std::vector<ClipItem> out;
    const std::wstring q = ToLower(Trim(search));
    for (const auto& item : items_)
    {
        if (tab == FilterTab::Pinned && !item.pinned) continue;
        if (tab == FilterTab::Text && item.kind != ClipKind::Text && item.kind != ClipKind::Color) continue;
        if (tab == FilterTab::Image && item.kind != ClipKind::Image) continue;
        if (tab == FilterTab::Url && item.kind != ClipKind::Url) continue;

        if (!q.empty())
        {
            const bool matchText = ToLower(item.text).find(q) != std::wstring::npos;
            const bool matchApp = ToLower(item.sourceApp).find(q) != std::wstring::npos;
            const bool matchPrev = ToLower(item.preview).find(q) != std::wstring::npos;
            if (!matchText && !matchApp && !matchPrev) continue;
        }
        out.push_back(item);
    }
    return out;
}

bool ClipboardStore::LoadFromFile(const std::wstring& jsonPath)
{
    const auto loaded = llp::persistence::LoadJsonWithRecovery(jsonPath);
    if (!loaded.found) return false;
    try
    {
        const Json& doc = loaded.document;
        if (!doc.is_object() || !doc.contains("Items") || !doc["Items"].is_array()) return false;
        std::vector<ClipItem> parsed;
        for (const auto& e : doc["Items"])
        {
            if (!e.is_object()) continue;
            ClipItem item;
            item.id = llp::persistence::Utf8ToWide(e.value("Id", std::string{}));
            item.kind = ClipKindFromString(e.value("Kind", std::string{"text"}));
            item.text = llp::persistence::Utf8ToWide(e.value("Text", std::string{}));
            item.preview = llp::persistence::Utf8ToWide(e.value("Preview", std::string{}));
            item.imagePath = llp::persistence::Utf8ToWide(e.value("ImagePath", std::string{}));
            item.sourceApp = llp::persistence::Utf8ToWide(e.value("SourceApp", std::string{}));
            item.timestampMs = e.value("TimestampMs", std::uint64_t{0});
            item.contentHash = e.value("ContentHash", std::uint64_t{0});
            item.pinned = e.value("Pinned", false);
            item.imageWidth = e.value("ImageWidth", 0);
            item.imageHeight = e.value("ImageHeight", 0);
            item.colorRgba = e.value("ColorRgba", std::uint32_t{0});
            if (e.contains("FilePaths") && e["FilePaths"].is_array())
            {
                for (const auto& fp : e["FilePaths"])
                {
                    if (fp.is_string()) item.filePaths.push_back(llp::persistence::Utf8ToWide(fp.get<std::string>()));
                }
            }
            if (item.kind == ClipKind::Image && !item.imagePath.empty() && GetFileAttributesW(item.imagePath.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                continue; // skip deleted cached images
            }
            if (!item.id.empty()) parsed.push_back(std::move(item));
        }
        items_ = std::move(parsed);
        EnforceLimit();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ClipboardStore::SaveToFile(const std::wstring& jsonPath) const
{
    Json arr = Json::array();
    for (const auto& item : items_)
    {
        Json files = Json::array();
        for (const auto& fp : item.filePaths) files.push_back(llp::persistence::WideToUtf8(fp));
        arr.push_back({
            {"Id", llp::persistence::WideToUtf8(item.id)},
            {"Kind", ClipKindToString(item.kind)},
            {"Text", llp::persistence::WideToUtf8(item.text)},
            {"Preview", llp::persistence::WideToUtf8(item.preview)},
            {"ImagePath", llp::persistence::WideToUtf8(item.imagePath)},
            {"FilePaths", files},
            {"SourceApp", llp::persistence::WideToUtf8(item.sourceApp)},
            {"TimestampMs", item.timestampMs},
            {"ContentHash", item.contentHash},
            {"Pinned", item.pinned},
            {"ImageWidth", item.imageWidth},
            {"ImageHeight", item.imageHeight},
            {"ColorRgba", item.colorRgba}
        });
    }
    Json doc = {{"SchemaVersion", 1}, {"Items", arr}};
    std::wstring err;
    return llp::persistence::WriteJsonAtomicWithBackup(jsonPath, doc, err);
}

bool ClipboardStore::RestoreToSystemClipboard(HWND owner, const ClipItem& item, bool plainTextOnly)
{
    if (!OpenClipboard(owner)) return false;
    SuppressNextCapture();
    EmptyClipboard();

    bool ok = false;
    if (item.kind == ClipKind::Image && !plainTextOnly && !item.imagePath.empty())
    {
        HGLOBAL dib = LoadPngAsDib(item.imagePath);
        if (dib)
        {
            SetClipboardData(CF_DIB, dib);
            ok = true;
        }
    }
    else if (item.kind == ClipKind::Files && !plainTextOnly && !item.filePaths.empty())
    {
        size_t totalChars = 1;
        for (const auto& p : item.filePaths) totalChars += p.size() + 1;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + totalChars * sizeof(wchar_t));
        if (hMem)
        {
            auto* df = static_cast<DROPFILES*>(GlobalLock(hMem));
            if (df)
            {
                df->pFiles = sizeof(DROPFILES);
                df->fWide = TRUE;
                auto* dst = reinterpret_cast<wchar_t*>(reinterpret_cast<std::uint8_t*>(df) + sizeof(DROPFILES));
                for (const auto& p : item.filePaths)
                {
                    memcpy(dst, p.c_str(), (p.size() + 1) * sizeof(wchar_t));
                    dst += p.size() + 1;
                }
                *dst = L'\0';
                GlobalUnlock(hMem);
                SetClipboardData(CF_HDROP, hMem);
                ok = true;
            }
            else GlobalFree(hMem);
        }
    }
    else
    {
        const std::wstring& txt = item.text;
        const size_t bytes = (txt.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem)
        {
            void* dst = GlobalLock(hMem);
            if (dst)
            {
                memcpy(dst, txt.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
                ok = true;
            }
            else GlobalFree(hMem);
        }
    }

    CloseClipboard();
    return ok;
}

} // namespace llp::clipboard
