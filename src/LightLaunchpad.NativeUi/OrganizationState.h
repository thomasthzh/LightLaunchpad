#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace llp::organization
{
constexpr uint64_t UndoLifetimeMilliseconds = 8000;

struct LayoutItem
{
    std::wstring stableId;
    std::wstring sourcePath;
    std::wstring displayName;
    std::wstring regionId;
    int order = 0;
};

struct Region
{
    std::wstring id;
    std::wstring name;
    int order = 0;
};

enum class UndoKind
{
    None,
    Recycle,
    Region
};

struct DeleteCompletion
{
    bool foundRequest = false;
    std::vector<LayoutItem> removedItems;
    std::vector<LayoutItem> retainedItems;
    int failedCount = 0;
    bool partialFailure = false;
    bool canUndo = false;
};

struct RestoreCompletion
{
    bool foundUndo = false;
    std::vector<LayoutItem> restoredItems;
    std::vector<LayoutItem> stillMissingItems;
    int failedCount = 0;
    bool partialFailure = false;
};

class State
{
public:
    void TrackDelete(uint64_t requestId, const std::vector<LayoutItem>& items);
    DeleteCompletion CompleteRecycle(
        uint64_t requestId,
        const std::vector<std::wstring>& affectedPaths,
        int failedCount,
        uint64_t undoToken,
        uint64_t nowMilliseconds);

    bool BeginRecycleUndo(uint64_t nowMilliseconds, uint64_t& undoToken);
    RestoreCompletion CompleteRestore(
        uint64_t undoToken,
        const std::vector<std::wstring>& restoredPaths,
        int failedCount,
        uint64_t nowMilliseconds);

    void RecordRegionChange(
        const std::vector<Region>& regions,
        const std::vector<LayoutItem>& items,
        uint64_t nowMilliseconds);
    bool TakeRegionUndo(
        uint64_t nowMilliseconds,
        std::vector<Region>& regions,
        std::vector<LayoutItem>& items);

    bool CanUndo(uint64_t nowMilliseconds);
    UndoKind CurrentUndoKind(uint64_t nowMilliseconds);
    void Expire(uint64_t nowMilliseconds);

private:
    struct UndoAction
    {
        UndoKind kind = UndoKind::None;
        uint64_t token = 0;
        uint64_t expiresAtMilliseconds = 0;
        std::vector<LayoutItem> items;
        std::vector<Region> regions;
    };

    std::map<uint64_t, std::vector<LayoutItem>> pendingDeletes_;
    UndoAction undo_;
};
}
