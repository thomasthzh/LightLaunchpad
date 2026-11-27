#include <windows.h>

#include <iostream>
#include <string>

#include "ClipboardHistory.h"

namespace
{
int g_pass = 0;
int g_fail = 0;
void Check(bool cond, const char* name)
{
    if (cond) ++g_pass;
    else { ++g_fail; std::cerr << "FAIL: " << name << "\n"; }
}
}

int main()
{
    using namespace llp::clipboard;

    // 1. Classification
    std::uint32_t rgba = 0;
    Check(ClassifyText(L"#3B82F6", rgba) == ClipKind::Color && rgba == 0x3B82F6FFu, "classify hex color");
    Check(ClassifyText(L"https://lightlaunchpad.thomasthzh.top", rgba) == ClipKind::Url, "classify url");
    Check(ClassifyText(L"hello world", rgba) == ClipKind::Text, "classify plain text");

    // 2. Deduplication + source app + password manager intuitive copy
    ClipboardStore store(10);
    Check(store.AddText(L"my-secret-pass-123", L"Bitwarden", 1000), "records password manager copy intuitively");
    Check(store.AddText(L"https://github.com/thomasthzh", L"Chrome", 2000), "records url");
    Check(store.AddText(L"#3B82F6", L"Figma", 3000), "records color");
    Check(store.AddText(L"my-secret-pass-123", L"Bitwarden", 4000), "deduplicates and bumps to top");
    Check(store.Items().size() == 3 && store.Items()[0].text == L"my-secret-pass-123", "dedup keeps 3 items and newest first");

    // 3. Pinning & FIFO cap protection
    const std::wstring pinnedId = store.Items()[2].id; // URL item
    Check(store.TogglePin(pinnedId), "pin url item");
    Check(store.Items()[0].id == pinnedId && store.Items()[0].pinned, "pinned item moves to top");

    for (int i = 0; i < 15; ++i)
    {
        store.AddText(L"clip-" + std::to_wstring(i), L"Code", 5000 + static_cast<std::uint64_t>(i));
    }
    Check(store.Items().size() == 10, "enforces maxItems cap");
    Check(store.Items()[0].id == pinnedId && store.Items()[0].pinned, "pinned item survives FIFO eviction");

    // 4. Query filtering by tab & source app
    Check(store.Query(L"", FilterTab::Pinned).size() == 1, "filter pinned tab");
    Check(store.Query(L"chrome", FilterTab::All).size() == 1, "search by source app");
    Check(store.Query(L"clip-14", FilterTab::Text).size() == 1, "search by content in text tab");

    // 5. Persistence round-trip
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring path = std::wstring(tmp) + L"llp_clipboard_test.json";
    DeleteFileW(path.c_str());
    Check(store.SaveToFile(path), "save clipboard-history.json");

    ClipboardStore reloaded(10);
    Check(reloaded.LoadFromFile(path), "load clipboard-history.json");
    Check(reloaded.Items().size() == 10 && reloaded.Items()[0].pinned, "reloaded items match");
    DeleteFileW(path.c_str());

    std::cout << "ClipboardHistory tests: " << g_pass << "/" << (g_pass + g_fail) << " passed\n";
    return g_fail == 0 ? 0 : 1;
}
