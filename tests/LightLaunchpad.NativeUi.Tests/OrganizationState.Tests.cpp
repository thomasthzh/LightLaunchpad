#include "../../src/LightLaunchpad.NativeUi/OrganizationState.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using llp::organization::LayoutItem;
using llp::organization::Region;
using llp::organization::State;
using llp::organization::UndoKind;

int passed = 0;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void RequireEqual(size_t expected, size_t actual, const char* message)
{
    if (expected != actual) throw std::runtime_error(message);
}

LayoutItem Item(const wchar_t* id, const wchar_t* path, const wchar_t* region, int order)
{
    return { id, path, id, region, order };
}

void Pass(const char* name)
{
    ++passed;
    std::cout << "PASS " << name << "\n";
}

void TestDeleteWaitsForShellAndSupportsExactUndo()
{
    State state;
    const std::vector<LayoutItem> items = {
        Item(L"a", L"C:\\Launchpad\\A.lnk", L"work", 0),
        Item(L"b", L"C:\\Launchpad\\B.lnk", L"work", 1)
    };
    state.TrackDelete(41, items);
    Require(!state.CanUndo(100), "Tracking a request changed layout before Shell completion.");

    const auto completed = state.CompleteRecycle(41, { L"c:/launchpad/a.lnk", L"C:\\Launchpad\\B.lnk" }, 0, 91, 100);
    Require(completed.foundRequest, "Shell completion did not match its delete request.");
    RequireEqual(2, completed.removedItems.size(), "Successful recycle did not return exact removed layout snapshots.");
    RequireEqual(0, completed.retainedItems.size(), "Successful recycle retained requested items.");
    Require(completed.canUndo && state.CanUndo(8099), "Successful recycle did not expose short-lived undo.");
    Require(!state.CanUndo(8100), "Recycle undo exceeded the eight-second deadline.");

    uint64_t token = 0;
    Require(!state.BeginRecycleUndo(8100, token), "Expired undo began a restore request.");
    state.TrackDelete(42, items);
    state.CompleteRecycle(42, { items[0].sourcePath, items[1].sourcePath }, 0, 92, 1000);
    Require(state.BeginRecycleUndo(2000, token) && token == 92, "Undo returned the wrong Shell restore token.");
    const auto restored = state.CompleteRestore(92, { items[0].sourcePath, items[1].sourcePath }, 0, 12000);
    RequireEqual(2, restored.restoredItems.size(), "Undo did not return both exact layout snapshots.");
    RequireEqual(0, restored.stillMissingItems.size(), "Successful undo retained missing items.");
    Require(!state.CanUndo(12000), "Successful undo left a stale action available.");
    Pass("OrganizationState_DeleteCommitsAfterShellAndSupportsExactUndo");
}

void TestPartialAndFailedDeletePreserveFailedItems()
{
    State state;
    const std::vector<LayoutItem> items = {
        Item(L"a", L"C:\\Launchpad\\A.lnk", L"work", 0),
        Item(L"b", L"C:\\Launchpad\\B.lnk", L"work", 1),
        Item(L"c", L"C:\\Launchpad\\C.lnk", L"work", 2)
    };
    state.TrackDelete(51, items);
    const auto partial = state.CompleteRecycle(51, { items[1].sourcePath }, 2, 101, 500);
    RequireEqual(1, partial.removedItems.size(), "Partial recycle removed the wrong count.");
    Require(partial.removedItems[0].stableId == L"b", "Partial recycle removed the wrong item.");
    RequireEqual(2, partial.retainedItems.size(), "Partial recycle did not retain both failures.");
    Require(partial.failedCount == 2 && partial.partialFailure && partial.canUndo, "Partial recycle outcome lost failure or undo state.");

    state.TrackDelete(52, items);
    const auto failed = state.CompleteRecycle(52, {}, 3, 0, 700);
    RequireEqual(0, failed.removedItems.size(), "Failed recycle removed layout items.");
    RequireEqual(3, failed.retainedItems.size(), "Failed recycle did not preserve every item.");
    Require(failed.failedCount == 3 && !failed.canUndo, "Failed recycle exposed an invalid undo action.");
    Require(!state.CanUndo(700), "A newer failed delete left the previous Shell undo token exposed.");
    Pass("OrganizationState_PartialAndFailedDeletePreserveFailedItems");
}

void TestPartialRestoreRetainsRetryableUndo()
{
    State state;
    const std::vector<LayoutItem> items = {
        Item(L"a", L"C:\\Launchpad\\A.lnk", L"work", 0),
        Item(L"b", L"C:\\Launchpad\\B.lnk", L"work", 1)
    };
    state.TrackDelete(61, items);
    state.CompleteRecycle(61, { items[0].sourcePath, items[1].sourcePath }, 0, 111, 100);
    uint64_t token = 0;
    Require(state.BeginRecycleUndo(200, token), "Valid undo did not begin.");
    const auto partial = state.CompleteRestore(111, { items[0].sourcePath }, 1, 300);
    RequireEqual(1, partial.restoredItems.size(), "Partial restore lost its successful item.");
    RequireEqual(1, partial.stillMissingItems.size(), "Partial restore did not retain its failed item.");
    Require(partial.partialFailure && state.CanUndo(400), "Partial restore did not retain retryable undo.");
    Pass("OrganizationState_PartialRestoreRetainsRetryableUndo");
}

void TestRegionDeleteSnapshotRestoresRegionsAndPlacements()
{
    State state;
    const std::vector<Region> regions = {
        { L"work", L"Work", 1 },
        { L"games", L"Games", 2 }
    };
    const std::vector<LayoutItem> items = {
        Item(L"a", L"C:\\Launchpad\\A.lnk", L"work", 0),
        Item(L"b", L"C:\\Launchpad\\B.lnk", L"games", 0)
    };
    state.RecordRegionChange(regions, items, 1000);
    Require(state.CanUndo(8999) && state.CurrentUndoKind(8999) == UndoKind::Region, "Region change did not expose bounded undo.");
    std::vector<Region> restoredRegions;
    std::vector<LayoutItem> restoredItems;
    Require(state.TakeRegionUndo(2000, restoredRegions, restoredItems), "Valid region undo was rejected.");
    RequireEqual(2, restoredRegions.size(), "Region undo lost region metadata.");
    RequireEqual(2, restoredItems.size(), "Region undo lost item placements.");
    Require(restoredItems[0].regionId == L"work" && restoredItems[1].regionId == L"games", "Region undo changed original placements.");
    Require(!state.CanUndo(2000), "Region undo remained available after consumption.");

    state.RecordRegionChange(regions, items, 5000);
    Require(!state.TakeRegionUndo(13000, restoredRegions, restoredItems), "Expired region undo was consumed.");
    Pass("OrganizationState_RegionDeleteSnapshotRestoresRegionsAndPlacements");
}
}

int main()
{
    try
    {
        TestDeleteWaitsForShellAndSupportsExactUndo();
        TestPartialAndFailedDeletePreserveFailedItems();
        TestPartialRestoreRetainsRetryableUndo();
        TestRegionDeleteSnapshotRestoresRegionsAndPlacements();
        std::cout << passed << "/4 tests passed\n";
        return passed == 4 ? 0 : 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL " << exception.what() << "\n";
        return 1;
    }
}
