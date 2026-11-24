#include "EverythingBridge.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <thread>

namespace llp::everything
{
namespace
{
constexpr ULONG_PTR kEverythingCopyDataQueryW = 2;
constexpr DWORD kEverythingIpcFolderFlag = 0x00000001;
constexpr DWORD kReplyMagic = 0x4C4C5045u; // 'LLPE'
constexpr wchar_t kReplyWindowClass[] = L"LightLaunchpadEverythingReplyWindow";

#pragma pack(push, 1)
struct EverythingIpcQueryHeaderW
{
    DWORD reply_hwnd;
    DWORD reply_copydata_message;
    DWORD search_flags;
    DWORD offset;
    DWORD max_results;
};

struct EverythingIpcItemW
{
    DWORD flags;
    DWORD filename_offset;
    DWORD path_offset;
};

struct EverythingIpcListW
{
    DWORD totfolders;
    DWORD totfiles;
    DWORD totitems;
    DWORD numfolders;
    DWORD numfiles;
    DWORD numitems;
    DWORD offset;
    EverythingIpcItemW items[1];
};
#pragma pack(pop)

std::wstring TrimWhitespace(const std::wstring& text)
{
    std::size_t start = 0;
    while (start < text.size() && std::iswspace(text[start])) ++start;
    std::size_t end = text.size();
    while (end > start && std::iswspace(text[end - 1])) --end;
    return text.substr(start, end - start);
}

HWND FindEverythingWindow()
{
    HWND hwnd = FindWindowW(L"EVERYTHING_TASKBAR_NOTIFICATION", nullptr);
    if (!hwnd)
    {
        hwnd = FindWindowW(L"EVERYTHING_TASKBAR_NOTIFICATION_(1.5a)", nullptr);
    }
    return hwnd;
}

std::wstring ReadBoundedWideString(const unsigned char* base, std::size_t totalBytes, DWORD offset)
{
    if (!base || offset >= totalBytes) return {};
    const std::size_t maxChars = (totalBytes - offset) / sizeof(wchar_t);
    const auto* ptr = reinterpret_cast<const wchar_t*>(base + offset);
    std::size_t len = 0;
    while (len < maxChars && ptr[len] != L'\0') ++len;
    return std::wstring(ptr, len);
}

std::wstring CombinePath(const std::wstring& dir, const std::wstring& name)
{
    if (dir.empty()) return name;
    if (name.empty()) return dir;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L"\\" + name;
}

struct ReplyContext
{
    bool received = false;
    std::uint32_t maxResults = 8;
    std::vector<FileResult> results;
};

LRESULT CALLBACK ReplyWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_COPYDATA)
    {
        auto* ctx = reinterpret_cast<ReplyContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (ctx && cds && cds->dwData == kReplyMagic && cds->lpData && cds->cbData >= sizeof(EverythingIpcListW) - sizeof(EverythingIpcItemW))
        {
            const auto* base = static_cast<const unsigned char*>(cds->lpData);
            const auto* list = reinterpret_cast<const EverythingIpcListW*>(base);
            const std::size_t headerSize = offsetof(EverythingIpcListW, items);
            const std::size_t availableItems = cds->cbData > headerSize
                ? (cds->cbData - headerSize) / sizeof(EverythingIpcItemW)
                : 0;
            const std::size_t count = std::min<std::size_t>({
                static_cast<std::size_t>(list->numitems),
                availableItems,
                static_cast<std::size_t>(ctx->maxResults)
            });

            ctx->results.clear();
            ctx->results.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto& item = list->items[i];
                FileResult result;
                result.name = ReadBoundedWideString(base, cds->cbData, item.filename_offset);
                result.directory = ReadBoundedWideString(base, cds->cbData, item.path_offset);
                if (result.name.empty() && result.directory.empty()) continue;
                result.fullPath = CombinePath(result.directory, result.name);
                result.isFolder = (item.flags & kEverythingIpcFolderFlag) != 0;
                ctx->results.push_back(std::move(result));
            }
            ctx->received = true;
            return TRUE;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool EnsureReplyWindowClass()
{
    static std::once_flag once;
    static bool registered = false;
    std::call_once(once, [] {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ReplyWindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kReplyWindowClass;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    });
    return registered;
}

QueryResponse ExecuteIpcQuery(std::uint64_t generation, const std::wstring& rawQuery, const std::wstring& everythingQuery, std::uint32_t maxResults, const std::atomic<std::uint64_t>& latestGeneration)
{
    QueryResponse response;
    response.generation = generation;
    response.originalQuery = rawQuery;

    HWND everythingHwnd = FindEverythingWindow();
    if (!everythingHwnd || everythingQuery.empty())
    {
        response.everythingAvailable = everythingHwnd != nullptr;
        return response;
    }
    response.everythingAvailable = true;

    if (!EnsureReplyWindowClass()) return response;

    ReplyContext replyCtx;
    replyCtx.maxResults = std::clamp<std::uint32_t>(maxResults, 1, 24);
    HWND replyHwnd = CreateWindowExW(
        0,
        kReplyWindowClass,
        L"",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!replyHwnd) return response;

    SetWindowLongPtrW(replyHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&replyCtx));

    const std::size_t queryBytes = (everythingQuery.size() + 1) * sizeof(wchar_t);
    std::vector<unsigned char> payload(sizeof(EverythingIpcQueryHeaderW) + queryBytes, 0);
    auto* header = reinterpret_cast<EverythingIpcQueryHeaderW*>(payload.data());
    header->reply_hwnd = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(replyHwnd));
    header->reply_copydata_message = kReplyMagic;
    header->search_flags = 0;
    header->offset = 0;
    header->max_results = replyCtx.maxResults;
    std::memcpy(payload.data() + sizeof(EverythingIpcQueryHeaderW), everythingQuery.c_str(), queryBytes);

    COPYDATASTRUCT cds = {};
    cds.dwData = kEverythingCopyDataQueryW;
    cds.cbData = static_cast<DWORD>(payload.size());
    cds.lpData = payload.data();

    DWORD_PTR sendResult = 0;
    const LRESULT sent = SendMessageTimeoutW(
        everythingHwnd,
        WM_COPYDATA,
        reinterpret_cast<WPARAM>(replyHwnd),
        reinterpret_cast<LPARAM>(&cds),
        SMTO_ABORTIFHUNG,
        250,
        &sendResult);

    if (sent != 0 && sendResult != 0)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(220);
        MSG msg = {};
        while (!replyCtx.received && latestGeneration.load(std::memory_order_acquire) == generation)
        {
            while (PeekMessageW(&msg, replyHwnd, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (replyCtx.received) break;
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            const auto remainingMs = static_cast<DWORD>(
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            MsgWaitForMultipleObjects(0, nullptr, FALSE, std::min<DWORD>(remainingMs, 25), QS_ALLINPUT);
        }
    }

    DestroyWindow(replyHwnd);
    if (replyCtx.received)
    {
        response.items = std::move(replyCtx.results);
    }
    return response;
}
}

ParsedQuery ParseSpotlightQuery(const std::wstring& rawQuery)
{
    ParsedQuery parsed;
    std::size_t lead = 0;
    while (lead < rawQuery.size() && std::iswspace(rawQuery[lead])) ++lead;
    const std::wstring leadTrimmed = rawQuery.substr(lead);
    if (leadTrimmed.empty())
    {
        parsed.mode = QueryMode::None;
        return parsed;
    }

    if (leadTrimmed.size() >= 2 && (leadTrimmed[0] == L'f' || leadTrimmed[0] == L'F') && std::iswspace(leadTrimmed[1]))
    {
        const std::wstring sub = TrimWhitespace(leadTrimmed.substr(2));
        parsed.mode = sub.empty() ? QueryMode::None : QueryMode::FilesOnly;
        parsed.everythingQuery = sub;
        return parsed;
    }

    const std::wstring trimmed = TrimWhitespace(leadTrimmed);
    if (trimmed[0] == L'/')
    {
        const std::wstring sub = TrimWhitespace(trimmed.substr(1));
        parsed.mode = sub.empty() ? QueryMode::None : QueryMode::FilesOnly;
        parsed.everythingQuery = sub;
        return parsed;
    }

    parsed.mode = QueryMode::Hybrid;
    parsed.appQuery = trimmed;
    parsed.everythingQuery = trimmed;
    return parsed;
}

bool IsEverythingRunning()
{
    return FindEverythingWindow() != nullptr;
}

class EverythingBridge::Impl
{
public:
    Impl() = default;
    ~Impl() { Shutdown(); }

    void SetCallback(CompletionCallback callback)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = std::move(callback);
    }

    void RequestQuery(std::uint64_t generation, const std::wstring& rawQuery, std::uint32_t maxResults)
    {
        latestGeneration_.store(generation, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return;
            EnsureWorkerLocked();
            pendingGeneration_ = generation;
            pendingRawQuery_ = rawQuery;
            pendingMaxResults_ = maxResults;
            hasPending_ = true;
            readyAt_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(55);
        }
        cv_.notify_one();
    }

    void CancelPending()
    {
        latestGeneration_.fetch_add(1, std::memory_order_acq_rel);
        std::lock_guard<std::mutex> lock(mutex_);
        hasPending_ = false;
    }

    void Shutdown()
    {
        latestGeneration_.fetch_add(1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            hasPending_ = false;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

private:
    void EnsureWorkerLocked()
    {
        if (!worker_.joinable())
        {
            worker_ = std::thread([this] { WorkerLoop(); });
        }
    }

    void WorkerLoop()
    {
        for (;;)
        {
            std::uint64_t generation = 0;
            std::wstring rawQuery;
            std::uint32_t maxResults = 8;
            CompletionCallback callback;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || hasPending_; });
                if (stopping_) break;

                while (hasPending_ && !stopping_)
                {
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= readyAt_) break;
                    cv_.wait_until(lock, readyAt_, [this] { return stopping_; });
                }
                if (stopping_) break;
                if (!hasPending_) continue;

                generation = pendingGeneration_;
                rawQuery = pendingRawQuery_;
                maxResults = pendingMaxResults_;
                hasPending_ = false;
                callback = callback_;
            }

            const ParsedQuery parsed = ParseSpotlightQuery(rawQuery);
            if (parsed.mode == QueryMode::None) continue;
            if (latestGeneration_.load(std::memory_order_acquire) != generation) continue;

            QueryResponse response = ExecuteIpcQuery(
                generation,
                rawQuery,
                parsed.everythingQuery,
                maxResults,
                latestGeneration_);

            if (latestGeneration_.load(std::memory_order_acquire) == generation && callback)
            {
                callback(std::move(response));
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    CompletionCallback callback_;
    std::atomic<std::uint64_t> latestGeneration_{0};
    std::uint64_t pendingGeneration_ = 0;
    std::wstring pendingRawQuery_;
    std::uint32_t pendingMaxResults_ = 8;
    std::chrono::steady_clock::time_point readyAt_{};
    bool hasPending_ = false;
    bool stopping_ = false;
};

EverythingBridge::EverythingBridge() : impl_(std::make_unique<Impl>()) {}
EverythingBridge::~EverythingBridge() = default;

void EverythingBridge::SetCallback(CompletionCallback callback)
{
    impl_->SetCallback(std::move(callback));
}

void EverythingBridge::RequestQuery(std::uint64_t generation, const std::wstring& rawQuery, std::uint32_t maxResults)
{
    impl_->RequestQuery(generation, rawQuery, maxResults);
}

void EverythingBridge::CancelPending()
{
    impl_->CancelPending();
}

void EverythingBridge::Shutdown()
{
    impl_->Shutdown();
}
}
