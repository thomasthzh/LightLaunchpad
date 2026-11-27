#include "InputStats.h"

#include <cmath>
#include <cstdio>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* msg)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_failures;
    }
}
}

int main()
{
    using namespace llp::stats;

    InputStatsStore store;
    const std::string today = InputStatsStore::CurrentLocalDate();
    store.RecordKeyDown('A', today);
    store.RecordKeyDown('A', today);
    store.RecordKeyDown(VK_SPACE, today);
    store.RecordMouseButton(0, today);
    store.RecordMouseButton(1, today);
    store.RecordMouseButton(2, today);
    store.RecordWheelNotch(today);
    store.RecordMouseDelta(300, 400, today); // 500 px
    store.RecordAppFocusSeconds("Code", 1200, today);
    store.RecordAppFocusSeconds("Chrome", 600, today);

    const auto snap = store.GetSnapshot();
    Expect(snap.today.keystrokes == 3, "should aggregate 3 keystrokes");
    Expect(snap.today.keyCounts['A'] == 2, "key 'A' count should be 2");
    Expect(snap.today.keyCounts[VK_SPACE] == 1, "VK_SPACE count should be 1");
    Expect(snap.today.TotalClicks() == 3, "should aggregate 3 total clicks");
    Expect(snap.today.wheelSteps == 1, "should aggregate 1 wheel notch");
    Expect(std::abs(snap.today.mouseDistancePx - 500.0) < 0.01, "mouse distance should be 500 px");

    // Disabled store must ignore input.
    store.SetEnabled(false);
    store.RecordKeyDown('Z', today);
    Expect(store.GetSnapshot().today.keystrokes == 3, "disabled store should ignore keystrokes");

    // Persistence round-trip.
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    const std::wstring statsFile = std::wstring(tempDir) + L"llp_input_stats_test.json";
    DeleteFileW(statsFile.c_str());
    Expect(store.SaveToFile(statsFile, true), "SaveToFile should succeed");

    InputStatsStore reloaded;
    Expect(reloaded.LoadFromFile(statsFile), "LoadFromFile should succeed");
    const auto snap2 = reloaded.GetSnapshot();
    Expect(snap2.today.keystrokes == 3 && snap2.today.keyCounts['A'] == 2, "reloaded key counts should match");
    Expect(snap2.today.TotalClicks() == 3 && snap2.today.wheelSteps == 1, "reloaded mouse counts should match");
    Expect(snap2.today.appFocusSeconds.count("Code") && snap2.today.appFocusSeconds.at("Code") == 1200, "reloaded appFocusSeconds should match");
    DeleteFileW(statsFile.c_str());
    DeleteFileW((statsFile + L".bak").c_str());

    if (g_failures == 0)
    {
        std::printf("InputStats tests: 9/9 passed\n");
        return 0;
    }
    std::fprintf(stderr, "InputStats tests: %d failure(s)\n", g_failures);
    return 1;
}
