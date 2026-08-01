#pragma once

#include "Catalog.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llp::shell
{
enum class ResultKind
{
    Catalog,
    Icon,
    Launch,
    OpenFolder,
    OpenLocation,
    Recycle,
    ImportTree,
    Persistence
};

struct Result
{
    ResultKind kind = ResultKind::Catalog;
    uint64_t requestId = 0;
    DWORD brokerThreadId = 0;
    std::wstring apartment = L"unknown";
    std::shared_ptr<const catalog::Snapshot> snapshot;
    std::wstring stableId;
    HICON icon = nullptr;
    int iconSize = 0;
    bool succeeded = false;
    int imported = 0;
    int failed = 0;
    int skippedReparsePoints = 0;
    std::vector<std::wstring> affectedPaths;
    uint64_t elapsedMilliseconds = 0;
    std::wstring error;
};

class ShellBroker
{
public:
    ShellBroker();
    ~ShellBroker();
    ShellBroker(const ShellBroker&) = delete;
    ShellBroker& operator=(const ShellBroker&) = delete;

    bool Start(HWND targetWindow, UINT completionMessage);
    void Stop();
    bool IsRunning() const;

    uint64_t RequestCatalog(
        const std::wstring& dataDirectory,
        const std::wstring& launchpadFolder,
        uint64_t generation,
        DWORD testDelayMilliseconds = 0);
    uint64_t RequestIcon(const std::wstring& stableId, const std::wstring& sourcePath, int size);
    uint64_t RequestLaunch(const std::wstring& sourcePath);
    uint64_t RequestOpenFolder(const std::wstring& folderPath);
    uint64_t RequestOpenLocation(const std::wstring& sourcePath);
    uint64_t RequestRecycle(const std::wstring& launchpadFolder, const std::vector<std::wstring>& sourcePaths);
    uint64_t RequestImportTree(const std::wstring& root, const std::wstring& launchpadFolder);
    uint64_t RequestImportPaths(const std::vector<std::wstring>& sourcePaths, const std::wstring& launchpadFolder);
    uint64_t RequestPersistJson(const std::wstring& path, const std::string& documentUtf8);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
}
