#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace llp::everything
{
enum class QueryMode
{
    None,
    Hybrid,
    FilesOnly
};

struct ParsedQuery
{
    QueryMode mode = QueryMode::None;
    std::wstring everythingQuery;
    std::wstring appQuery;
};

struct FileResult
{
    std::wstring name;
    std::wstring directory;
    std::wstring fullPath;
    bool isFolder = false;
};

struct QueryResponse
{
    std::uint64_t generation = 0;
    std::wstring originalQuery;
    bool everythingAvailable = false;
    std::vector<FileResult> items;
};

// Parses a Spotlight query string:
// - Empty or whitespace-only -> QueryMode::None
// - "f <query>" or "/<query>" -> QueryMode::FilesOnly (searches Everything directly)
// - Anything else -> QueryMode::Hybrid (apps first, Everything files appended)
ParsedQuery ParseSpotlightQuery(const std::wstring& rawQuery);

// Returns true if a running standard Everything 1.4/1.5 IPC window is detected.
bool IsEverythingRunning();

// Asynchronous Win32 WM_COPYDATA bridge to a running Everything instance.
// Zero external DLL dependencies and 0% CPU / ~0 heap when idle.
class EverythingBridge
{
public:
    using CompletionCallback = std::function<void(QueryResponse)>;

    EverythingBridge();
    ~EverythingBridge();

    EverythingBridge(const EverythingBridge&) = delete;
    EverythingBridge& operator=(const EverythingBridge&) = delete;

    void SetCallback(CompletionCallback callback);
    void RequestQuery(std::uint64_t generation, const std::wstring& rawQuery, std::uint32_t maxResults = 8);
    void CancelPending();
    void Shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
