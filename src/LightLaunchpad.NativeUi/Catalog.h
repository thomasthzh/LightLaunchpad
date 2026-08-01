#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace llp::catalog
{
struct Region
{
    std::wstring id;
    std::wstring name;
    int order = 0;
};

struct Item
{
    std::wstring stableId;
    std::wstring sourcePath;
    std::wstring displayName;
    std::wstring regionId;
    int order = 0;
};

struct Snapshot
{
    uint64_t generation = 0;
    std::wstring folderPath;
    std::wstring folderIdentity;
    std::wstring viewMode = L"InlineRegions";
    std::vector<Region> regions;
    std::vector<Item> items;
    std::vector<Item> missingItems;
    bool recoveredFromBackup = false;
    bool migratedLegacy = false;
    std::wstring quarantinePath;
    std::wstring error;
    uint64_t loadMilliseconds = 0;
};

bool IsLaunchablePath(const std::wstring& path);
std::wstring StableItemId(const std::wstring& sourcePath);
Snapshot LoadSnapshot(
    const std::wstring& dataDirectory,
    const std::wstring& launchpadFolder,
    uint64_t generation);
}
