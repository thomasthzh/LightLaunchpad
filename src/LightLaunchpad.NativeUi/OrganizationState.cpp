#include "OrganizationState.h"

#include <algorithm>
#include <cwctype>

namespace llp::organization
{
namespace
{
std::wstring NormalizePath(std::wstring value)
{
    std::replace(value.begin(), value.end(), L'/', L'\\');
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    while (value.size() > 3 && value.back() == L'\\') value.pop_back();
    return value;
}

bool ContainsPath(const std::vector<std::wstring>& paths, const std::wstring& candidate)
{
    const auto normalized = NormalizePath(candidate);
    return std::any_of(paths.begin(), paths.end(), [&](const std::wstring& path) {
        return NormalizePath(path) == normalized;
    });
}

uint64_t Deadline(uint64_t nowMilliseconds)
{
    if (nowMilliseconds > UINT64_MAX - UndoLifetimeMilliseconds) return UINT64_MAX;
    return nowMilliseconds + UndoLifetimeMilliseconds;
}
}

void State::TrackDelete(uint64_t requestId, const std::vector<LayoutItem>& items)
{
    if (requestId == 0 || items.empty()) return;
    pendingDeletes_[requestId] = items;
}

DeleteCompletion State::CompleteRecycle(
    uint64_t requestId,
    const std::vector<std::wstring>& affectedPaths,
    int failedCount,
    uint64_t undoToken,
    uint64_t nowMilliseconds)
{
    DeleteCompletion completion;
    const auto pending = pendingDeletes_.find(requestId);
    if (pending == pendingDeletes_.end()) return completion;

    completion.foundRequest = true;
    // ShellBroker retains only the newest recycle transaction. Mirror that
    // single-action contract even when the new delete fails completely.
    undo_ = {};
    for (const auto& item : pending->second)
    {
        if (ContainsPath(affectedPaths, item.sourcePath)) completion.removedItems.push_back(item);
        else completion.retainedItems.push_back(item);
    }
    pendingDeletes_.erase(pending);

    completion.failedCount = std::max(failedCount, static_cast<int>(completion.retainedItems.size()));
    completion.partialFailure = !completion.removedItems.empty() && completion.failedCount > 0;
    if (!completion.removedItems.empty() && undoToken != 0)
    {
        undo_.kind = UndoKind::Recycle;
        undo_.token = undoToken;
        undo_.expiresAtMilliseconds = Deadline(nowMilliseconds);
        undo_.items = completion.removedItems;
        undo_.regions.clear();
        completion.canUndo = true;
    }
    return completion;
}

bool State::BeginRecycleUndo(uint64_t nowMilliseconds, uint64_t& undoToken)
{
    Expire(nowMilliseconds);
    if (undo_.kind != UndoKind::Recycle || undo_.token == 0 || undo_.items.empty()) return false;
    undoToken = undo_.token;
    // The deadline governs when the user may invoke Undo. Once invoked, a slow
    // Shell STA operation must be allowed to finish without invalidating it.
    undo_.expiresAtMilliseconds = UINT64_MAX;
    return true;
}

RestoreCompletion State::CompleteRestore(
    uint64_t undoToken,
    const std::vector<std::wstring>& restoredPaths,
    int failedCount,
    uint64_t nowMilliseconds)
{
    RestoreCompletion completion;
    Expire(nowMilliseconds);
    if (undo_.kind != UndoKind::Recycle || undo_.token != undoToken) return completion;

    completion.foundUndo = true;
    for (const auto& item : undo_.items)
    {
        if (ContainsPath(restoredPaths, item.sourcePath)) completion.restoredItems.push_back(item);
        else completion.stillMissingItems.push_back(item);
    }
    completion.failedCount = std::max(failedCount, static_cast<int>(completion.stillMissingItems.size()));
    completion.partialFailure = !completion.restoredItems.empty() && completion.failedCount > 0;
    if (completion.stillMissingItems.empty())
    {
        undo_ = {};
    }
    else
    {
        undo_.items = completion.stillMissingItems;
        undo_.expiresAtMilliseconds = Deadline(nowMilliseconds);
    }
    return completion;
}

void State::RecordRegionChange(
    const std::vector<Region>& regions,
    const std::vector<LayoutItem>& items,
    uint64_t nowMilliseconds)
{
    if (regions.empty()) return;
    undo_.kind = UndoKind::Region;
    undo_.token = 0;
    undo_.expiresAtMilliseconds = Deadline(nowMilliseconds);
    undo_.items = items;
    undo_.regions = regions;
}

bool State::TakeRegionUndo(
    uint64_t nowMilliseconds,
    std::vector<Region>& regions,
    std::vector<LayoutItem>& items)
{
    Expire(nowMilliseconds);
    if (undo_.kind != UndoKind::Region || undo_.regions.empty()) return false;
    regions = undo_.regions;
    items = undo_.items;
    undo_ = {};
    return true;
}

bool State::CanUndo(uint64_t nowMilliseconds)
{
    Expire(nowMilliseconds);
    return undo_.kind != UndoKind::None;
}

UndoKind State::CurrentUndoKind(uint64_t nowMilliseconds)
{
    Expire(nowMilliseconds);
    return undo_.kind;
}

void State::Expire(uint64_t nowMilliseconds)
{
    if (undo_.kind != UndoKind::None && nowMilliseconds >= undo_.expiresAtMilliseconds) undo_ = {};
}
}
