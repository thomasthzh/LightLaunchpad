#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace llp::stats
{
struct DailyStats
{
    std::string date; // YYYY-MM-DD (local time)
    std::uint64_t keystrokes = 0;
    std::uint64_t leftClicks = 0;
    std::uint64_t rightClicks = 0;
    std::uint64_t middleClicks = 0;
    std::uint64_t wheelSteps = 0;
    double mouseDistancePx = 0.0;
    std::array<std::uint64_t, 256> keyCounts{};
    std::unordered_map<std::string, std::uint64_t> appFocusSeconds;

    std::uint64_t TotalClicks() const noexcept
    {
        return leftClicks + rightClicks + middleClicks;
    }

    // Approximate physical meters assuming ~96 DPI / 3780 px per meter baseline.
    double MouseDistanceMeters() const noexcept
    {
        return mouseDistancePx / 3780.0;
    }
};

struct Snapshot
{
    DailyStats today;
    std::vector<DailyStats> recentDays; // Up to 30 days, chronological oldest -> newest (including today)
};

// Privacy-first local aggregate input statistics.
// Never records text content, key sequences, or timestamps of individual keystrokes.
class InputStatsStore
{
public:
    InputStatsStore();

    void SetEnabled(bool enabled);
    bool IsEnabled() const noexcept { return enabled_; }

    // Registers or unregisters Win32 Raw Input (RIDEV_INPUTSINK) on the given message window.
    bool SyncRawInputRegistration(HWND hwnd);
    void UnregisterRawInput();

    // Processes a WM_INPUT message when enabled.
    void ProcessRawInput(HRAWINPUT rawInputHandle);

    // Direct recording helpers (also used by unit tests).
    void RecordKeyDown(std::uint8_t vkey, const std::string& dateOverride = {});
    void RecordMouseDelta(int dx, int dy, const std::string& dateOverride = {});
    void RecordMouseButton(int buttonIndex, const std::string& dateOverride = {}); // 0=left, 1=right, 2=middle
    void RecordWheelNotch(const std::string& dateOverride = {});
    void RecordAppFocusSeconds(const std::string& appName, std::uint64_t seconds, const std::string& dateOverride = {});

    // Zero-polling foreground app focus tracker (process basename only, never window titles).
    void OnForegroundWindowChanged(HWND fgHwnd);
    void FlushActiveAppFocus();

    bool LoadFromFile(const std::wstring& path);
    bool SaveToFile(const std::wstring& path, bool force = false);
    void ClearAll();

    bool IsDirty() const noexcept { return dirty_; }
    Snapshot GetSnapshot() const;

    static std::string CurrentLocalDate();

private:
    DailyStats& EnsureDay(const std::string& date);
    void TrimOldDays();

    bool enabled_ = true;
    bool registered_ = false;
    bool dirty_ = false;
    std::array<bool, 256> keyDownState_{};
    std::vector<DailyStats> days_;
    std::string activeAppName_;
    ULONGLONG activeAppStartTick_ = 0;
};
}
