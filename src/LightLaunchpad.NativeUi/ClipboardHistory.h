#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace llp::clipboard
{

enum class ClipKind
{
    Text,
    Url,
    Color,
    Image,
    Files
};

struct ClipItem
{
    std::wstring id;
    ClipKind kind = ClipKind::Text;
    std::wstring text;          // text content, URL, color hex, or file summary
    std::wstring preview;       // single/two-line clean preview
    std::wstring imagePath;     // compressed PNG path for ClipKind::Image
    std::vector<std::wstring> filePaths; // for ClipKind::Files
    std::wstring sourceApp;     // e.g. L"Chrome", L"Code", L"Feishu"
    std::uint64_t timestampMs = 0;
    std::uint64_t contentHash = 0;
    bool pinned = false;
    int imageWidth = 0;
    int imageHeight = 0;
    std::uint32_t colorRgba = 0; // 0xRRGGBBAA when kind == Color
};

enum class FilterTab
{
    All = 0,
    Pinned = 1,
    Text = 2,
    Image = 3,
    Url = 4
};

class ClipboardStore
{
public:
    explicit ClipboardStore(size_t maxItems = 200);

    void SetCacheDirectory(const std::wstring& dir) { cacheDir_ = dir; }
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }
    void SetMaxItems(size_t maxItems);

    // Programmatic add (used by listener and unit tests)
    bool AddText(const std::wstring& text, const std::wstring& sourceApp, std::uint64_t nowMs = 0);
    bool AddImage(const std::wstring& pngPath, int width, int height, std::uint64_t pixelHash, const std::wstring& sourceApp, std::uint64_t nowMs = 0);
    bool AddFiles(const std::vector<std::wstring>& paths, const std::wstring& sourceApp, std::uint64_t nowMs = 0);

    // Captures current Windows clipboard on WM_CLIPBOARDUPDATE.
    bool CaptureFromSystemClipboard(HWND listenerHwnd);

    // Suppresses the next WM_CLIPBOARDUPDATE caused by our own paste write.
    void SuppressNextCapture() { suppressNext_ = true; }

    bool TogglePin(const std::wstring& id);
    bool RemoveItem(const std::wstring& id);
    void ClearAll(bool keepPinned = true);

    std::vector<ClipItem> Query(const std::wstring& search, FilterTab tab) const;
    const std::vector<ClipItem>& Items() const { return items_; }

    bool LoadFromFile(const std::wstring& jsonPath);
    bool SaveToFile(const std::wstring& jsonPath) const;

    // Writes item back to system clipboard (plainTextOnly strips formatting / forces Unicode text).
    bool RestoreToSystemClipboard(HWND owner, const ClipItem& item, bool plainTextOnly);

private:
    void EnforceLimit();
    void SortPinnedFirst();

    std::vector<ClipItem> items_;
    std::wstring cacheDir_;
    size_t maxItems_ = 200;
    bool enabled_ = true;
    bool suppressNext_ = false;
};

ClipKind ClassifyText(const std::wstring& text, std::uint32_t& outRgba);
std::wstring QueryForegroundProcessName();
std::string ClipKindToString(ClipKind kind);
ClipKind ClipKindFromString(const std::string& s);

} // namespace llp::clipboard
