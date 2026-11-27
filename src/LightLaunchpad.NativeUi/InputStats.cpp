#include "InputStats.h"

#include "Persistence.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace llp::stats
{
namespace
{
using Json = nlohmann::json;
constexpr std::size_t kMaxRetainedDays = 30;
constexpr int kMaxSingleMouseDeltaPx = 4000;
}

InputStatsStore::InputStatsStore() = default;

std::string InputStatsStore::CurrentLocalDate()
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    char buf[16] = {};
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u",
        static_cast<unsigned>(st.wYear),
        static_cast<unsigned>(st.wMonth),
        static_cast<unsigned>(st.wDay));
    return std::string(buf);
}

void InputStatsStore::SetEnabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled_)
    {
        keyDownState_.fill(false);
    }
}

bool InputStatsStore::SyncRawInputRegistration(HWND hwnd)
{
    if (!enabled_ || !hwnd)
    {
        UnregisterRawInput();
        return !enabled_;
    }
    if (registered_) return true;

    RAWINPUTDEVICE devices[2] = {};
    // Keyboard (Generic Desktop / Keyboard)
    devices[0].usUsagePage = 0x01;
    devices[0].usUsage = 0x06;
    devices[0].dwFlags = RIDEV_INPUTSINK;
    devices[0].hwndTarget = hwnd;
    // Mouse (Generic Desktop / Mouse)
    devices[1].usUsagePage = 0x01;
    devices[1].usUsage = 0x02;
    devices[1].dwFlags = RIDEV_INPUTSINK;
    devices[1].hwndTarget = hwnd;

    registered_ = RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE)) != FALSE;
    return registered_;
}

void InputStatsStore::UnregisterRawInput()
{
    if (!registered_) return;
    RAWINPUTDEVICE devices[2] = {};
    devices[0].usUsagePage = 0x01;
    devices[0].usUsage = 0x06;
    devices[0].dwFlags = RIDEV_REMOVE;
    devices[0].hwndTarget = nullptr;
    devices[1].usUsagePage = 0x01;
    devices[1].usUsage = 0x02;
    devices[1].dwFlags = RIDEV_REMOVE;
    devices[1].hwndTarget = nullptr;
    RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
    registered_ = false;
    keyDownState_.fill(false);
}

void InputStatsStore::ProcessRawInput(HRAWINPUT rawInputHandle)
{
    if (!enabled_ || !rawInputHandle) return;

    UINT size = 0;
    if (GetRawInputData(rawInputHandle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0 || size > 4096)
    {
        return;
    }

    alignas(RAWINPUT) unsigned char buffer[4096];
    if (GetRawInputData(rawInputHandle, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) != size)
    {
        return;
    }

    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer);
    if (raw->header.dwType == RIM_TYPEKEYBOARD)
    {
        const auto& kb = raw->data.keyboard;
        const UINT vkey = kb.VKey;
        if (vkey == 0 || vkey >= 256 || vkey == 0xFF) return;
        const bool isBreak = (kb.Flags & RI_KEY_BREAK) != 0;
        if (isBreak)
        {
            keyDownState_[vkey] = false;
        }
        else if (!keyDownState_[vkey])
        {
            keyDownState_[vkey] = true;
            RecordKeyDown(static_cast<std::uint8_t>(vkey));
        }
    }
    else if (raw->header.dwType == RIM_TYPEMOUSE)
    {
        const auto& m = raw->data.mouse;
        if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) == 0 && (m.lLastX != 0 || m.lLastY != 0))
        {
            RecordMouseDelta(static_cast<int>(m.lLastX), static_cast<int>(m.lLastY));
        }
        if (m.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) RecordMouseButton(0);
        if (m.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) RecordMouseButton(1);
        if (m.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) RecordMouseButton(2);
        if (m.usButtonFlags & (RI_MOUSE_WHEEL | RI_MOUSE_HWHEEL)) RecordWheelNotch();
    }
}

DailyStats& InputStatsStore::EnsureDay(const std::string& date)
{
    for (auto& d : days_)
    {
        if (d.date == date) return d;
    }
    DailyStats created;
    created.date = date;
    days_.push_back(std::move(created));
    std::sort(days_.begin(), days_.end(), [](const DailyStats& a, const DailyStats& b) {
        return a.date < b.date;
    });
    TrimOldDays();
    for (auto& d : days_)
    {
        if (d.date == date) return d;
    }
    return days_.back();
}

void InputStatsStore::TrimOldDays()
{
    if (days_.size() > kMaxRetainedDays)
    {
        days_.erase(days_.begin(), days_.begin() + static_cast<std::ptrdiff_t>(days_.size() - kMaxRetainedDays));
    }
}

void InputStatsStore::RecordKeyDown(std::uint8_t vkey, const std::string& dateOverride)
{
    if (!enabled_ || vkey == 0 || vkey == 0xFF) return;
    auto& day = EnsureDay(dateOverride.empty() ? CurrentLocalDate() : dateOverride);
    ++day.keystrokes;
    ++day.keyCounts[vkey];
    dirty_ = true;
}

void InputStatsStore::RecordMouseDelta(int dx, int dy, const std::string& dateOverride)
{
    if (!enabled_) return;
    if (std::abs(dx) > kMaxSingleMouseDeltaPx || std::abs(dy) > kMaxSingleMouseDeltaPx) return;
    const double dist = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
    if (!std::isfinite(dist) || dist <= 0.0) return;
    auto& day = EnsureDay(dateOverride.empty() ? CurrentLocalDate() : dateOverride);
    day.mouseDistancePx += dist;
    dirty_ = true;
}

void InputStatsStore::RecordMouseButton(int buttonIndex, const std::string& dateOverride)
{
    if (!enabled_) return;
    auto& day = EnsureDay(dateOverride.empty() ? CurrentLocalDate() : dateOverride);
    if (buttonIndex == 0) ++day.leftClicks;
    else if (buttonIndex == 1) ++day.rightClicks;
    else if (buttonIndex == 2) ++day.middleClicks;
    else return;
    dirty_ = true;
}

void InputStatsStore::RecordWheelNotch(const std::string& dateOverride)
{
    if (!enabled_) return;
    auto& day = EnsureDay(dateOverride.empty() ? CurrentLocalDate() : dateOverride);
    ++day.wheelSteps;
    dirty_ = true;
}

void InputStatsStore::ClearAll()
{
    days_.clear();
    keyDownState_.fill(false);
    dirty_ = true;
}

Snapshot InputStatsStore::GetSnapshot() const
{
    Snapshot snap;
    const std::string todayDate = CurrentLocalDate();
    snap.today.date = todayDate;
    snap.recentDays = days_;
    bool foundToday = false;
    for (const auto& d : days_)
    {
        if (d.date == todayDate)
        {
            snap.today = d;
            foundToday = true;
            break;
        }
    }
    if (!foundToday)
    {
        snap.recentDays.push_back(snap.today);
        if (snap.recentDays.size() > kMaxRetainedDays)
        {
            snap.recentDays.erase(snap.recentDays.begin());
        }
    }
    return snap;
}

bool InputStatsStore::LoadFromFile(const std::wstring& path)
{
    const auto loaded = llp::persistence::LoadJsonWithRecovery(path);
    if (!loaded.found) return false;
    try
    {
        const Json& doc = loaded.document;
        if (!doc.is_object() || !doc.contains("Days") || !doc["Days"].is_array()) return false;
        days_.clear();
        for (const auto& entry : doc["Days"])
        {
            if (!entry.is_object() || !entry.contains("Date") || !entry["Date"].is_string()) continue;
            DailyStats day;
            day.date = entry["Date"].get<std::string>();
            if (day.date.size() != 10) continue;
            day.keystrokes = entry.value("Keystrokes", std::uint64_t{0});
            day.leftClicks = entry.value("LeftClicks", std::uint64_t{0});
            day.rightClicks = entry.value("RightClicks", std::uint64_t{0});
            day.middleClicks = entry.value("MiddleClicks", std::uint64_t{0});
            day.wheelSteps = entry.value("WheelSteps", std::uint64_t{0});
            day.mouseDistancePx = entry.value("MouseDistancePx", 0.0);
            if (!std::isfinite(day.mouseDistancePx) || day.mouseDistancePx < 0.0) day.mouseDistancePx = 0.0;
            if (entry.contains("KeyCounts") && entry["KeyCounts"].is_object())
            {
                for (auto it = entry["KeyCounts"].begin(); it != entry["KeyCounts"].end(); ++it)
                {
                    if (!it.value().is_number_unsigned() && !it.value().is_number_integer()) continue;
                    const int vk = std::atoi(it.key().c_str());
                    if (vk > 0 && vk < 256)
                    {
                        const auto val = it.value().get<std::int64_t>();
                        if (val > 0) day.keyCounts[static_cast<std::size_t>(vk)] = static_cast<std::uint64_t>(val);
                    }
                }
            }
            if (entry.contains("AppFocusSeconds") && entry["AppFocusSeconds"].is_object())
            {
                for (auto it = entry["AppFocusSeconds"].begin(); it != entry["AppFocusSeconds"].end(); ++it)
                {
                    if (!it.value().is_number_unsigned() && !it.value().is_number_integer()) continue;
                    const auto secs = it.value().get<std::int64_t>();
                    if (secs > 0 && !it.key().empty()) day.appFocusSeconds[it.key()] = static_cast<std::uint64_t>(secs);
                }
            }
            days_.push_back(std::move(day));
        }
        std::sort(days_.begin(), days_.end(), [](const DailyStats& a, const DailyStats& b) {
            return a.date < b.date;
        });
        TrimOldDays();
        dirty_ = false;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void InputStatsStore::RecordAppFocusSeconds(const std::string& appName, std::uint64_t seconds, const std::string& dateOverride)
{
    if (!enabled_ || appName.empty() || seconds == 0) return;
    DailyStats& day = EnsureDay(dateOverride.empty() ? CurrentLocalDate() : dateOverride);
    day.appFocusSeconds[appName] += seconds;
    dirty_ = true;
}

void InputStatsStore::FlushActiveAppFocus()
{
    const ULONGLONG now = GetTickCount64();
    if (enabled_ && !activeAppName_.empty() && activeAppStartTick_ > 0 && now > activeAppStartTick_)
    {
        const std::uint64_t elapsedSec = std::min<std::uint64_t>((now - activeAppStartTick_) / 1000ULL, 1800ULL);
        if (elapsedSec > 0)
        {
            RecordAppFocusSeconds(activeAppName_, elapsedSec);
        }
    }
    activeAppStartTick_ = now;
}

void InputStatsStore::OnForegroundWindowChanged(HWND fgHwnd)
{
    FlushActiveAppFocus();
    activeAppName_.clear();
    if (!enabled_ || !fgHwnd) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(fgHwnd, &pid);
    if (pid == 0) return;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return;
    wchar_t path[MAX_PATH]{};
    DWORD len = MAX_PATH;
    if (QueryFullProcessImageNameW(proc, 0, path, &len))
    {
        std::wstring full(path, len);
        const size_t slash = full.find_last_of(L"\\/");
        std::wstring base = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
        if (base.size() > 4)
        {
            const std::wstring ext = base.substr(base.size() - 4);
            if (_wcsicmp(ext.c_str(), L".exe") == 0) base.resize(base.size() - 4);
        }
        activeAppName_ = llp::persistence::WideToUtf8(base);
    }
    CloseHandle(proc);
    activeAppStartTick_ = GetTickCount64();
}

bool InputStatsStore::SaveToFile(const std::wstring& path, bool force)
{
    FlushActiveAppFocus();
    if (!dirty_ && !force) return true;
    Json daysJson = Json::array();
    for (const auto& d : days_)
    {
        Json kc = Json::object();
        for (std::size_t vk = 1; vk < 256; ++vk)
        {
            if (d.keyCounts[vk] > 0) kc[std::to_string(vk)] = d.keyCounts[vk];
        }
        Json af = Json::object();
        for (const auto& kv : d.appFocusSeconds)
        {
            if (kv.second > 0) af[kv.first] = kv.second;
        }
        daysJson.push_back({
            { "Date", d.date },
            { "Keystrokes", d.keystrokes },
            { "LeftClicks", d.leftClicks },
            { "RightClicks", d.rightClicks },
            { "MiddleClicks", d.middleClicks },
            { "WheelSteps", d.wheelSteps },
            { "MouseDistancePx", d.mouseDistancePx },
            { "KeyCounts", std::move(kc) },
            { "AppFocusSeconds", std::move(af) }
        });
    }
    Json doc = {
        { "SchemaVersion", 1 },
        { "Days", std::move(daysJson) }
    };
    std::wstring error;
    const bool ok = llp::persistence::WriteJsonAtomicWithBackup(path, doc, error);
    if (ok) dirty_ = false;
    return ok;
}
}
