#include "Hotwords.h"
#include "Persistence.h"

#include <powrprof.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

namespace llp::hotwords
{
namespace
{

using Json = nlohmann::json;

std::wstring Trim(const std::wstring& s)
{
    size_t b = 0;
    while (b < s.size() && std::iswspace(s[b])) ++b;
    size_t e = s.size();
    while (e > b && std::iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring ToLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return s;
}

std::wstring FormatNumberWithCommas(double val)
{
    if (!std::isfinite(val)) return L"";
    const double rounded = std::round(val);
    if (std::abs(val - rounded) < 1e-9 && std::abs(rounded) < 1e15)
    {
        const bool neg = rounded < 0;
        std::wstring digits = std::to_wstring(static_cast<std::uint64_t>(std::abs(rounded)));
        int pos = static_cast<int>(digits.size()) - 3;
        while (pos > 0)
        {
            digits.insert(static_cast<size_t>(pos), 1, L',');
            pos -= 3;
        }
        return neg ? (L"-" + digits) : digits;
    }
    std::wostringstream oss;
    oss << std::fixed << std::setprecision(6) << val;
    std::wstring s = oss.str();
    while (!s.empty() && s.back() == L'0') s.pop_back();
    if (!s.empty() && s.back() == L'.') s.pop_back();
    return s;
}

std::wstring FormatNumberPlain(double val)
{
    if (!std::isfinite(val)) return L"";
    const double rounded = std::round(val);
    if (std::abs(val - rounded) < 1e-9 && std::abs(rounded) < 1e15)
    {
        return std::to_wstring(static_cast<std::int64_t>(rounded));
    }
    std::wostringstream oss;
    oss << std::fixed << std::setprecision(6) << val;
    std::wstring s = oss.str();
    while (!s.empty() && s.back() == L'0') s.pop_back();
    if (!s.empty() && s.back() == L'.') s.pop_back();
    return s;
}

class ExprParser
{
public:
    explicit ExprParser(const std::wstring& s) : s_(s) {}

    bool Parse(double& out)
    {
        SkipWs();
        if (!ParseAddSub(out)) return false;
        SkipWs();
        return pos_ == s_.size() && hasOperatorOrHex_ && std::isfinite(out);
    }

private:
    void SkipWs()
    {
        while (pos_ < s_.size() && std::iswspace(s_[pos_])) ++pos_;
    }

    bool Match(wchar_t ch)
    {
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == ch)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    bool ParseAddSub(double& out)
    {
        if (!ParseMulDiv(out)) return false;
        while (true)
        {
            SkipWs();
            if (Match(L'+'))
            {
                hasOperatorOrHex_ = true;
                double rhs = 0;
                if (!ParseMulDiv(rhs)) return false;
                out += rhs;
            }
            else if (Match(L'-'))
            {
                hasOperatorOrHex_ = true;
                double rhs = 0;
                if (!ParseMulDiv(rhs)) return false;
                out -= rhs;
            }
            else break;
        }
        return true;
    }

    bool ParseMulDiv(double& out)
    {
        if (!ParsePow(out)) return false;
        while (true)
        {
            SkipWs();
            if (Match(L'*') || Match(L'×'))
            {
                hasOperatorOrHex_ = true;
                double rhs = 0;
                if (!ParsePow(rhs)) return false;
                out *= rhs;
            }
            else if (Match(L'/') || Match(L'÷'))
            {
                hasOperatorOrHex_ = true;
                double rhs = 0;
                if (!ParsePow(rhs) || rhs == 0.0) return false;
                out /= rhs;
            }
            else if (Match(L'%'))
            {
                hasOperatorOrHex_ = true;
                double rhs = 0;
                if (!ParsePow(rhs) || rhs == 0.0) return false;
                out = std::fmod(out, rhs);
            }
            else break;
        }
        return true;
    }

    bool ParsePow(double& out)
    {
        if (!ParseUnary(out)) return false;
        SkipWs();
        if (Match(L'^'))
        {
            hasOperatorOrHex_ = true;
            double expVal = 0;
            if (!ParsePow(expVal)) return false;
            out = std::pow(out, expVal);
        }
        return true;
    }

    bool ParseUnary(double& out)
    {
        SkipWs();
        if (Match(L'+')) return ParseUnary(out);
        if (Match(L'-'))
        {
            if (!ParseUnary(out)) return false;
            out = -out;
            return true;
        }
        return ParsePrimary(out);
    }

    bool ParsePrimary(double& out)
    {
        SkipWs();
        if (Match(L'('))
        {
            if (!ParseAddSub(out)) return false;
            return Match(L')');
        }
        if (pos_ + 2 <= s_.size() && s_[pos_] == L'0' && (s_[pos_ + 1] == L'x' || s_[pos_ + 1] == L'X'))
        {
            pos_ += 2;
            size_t start = pos_;
            std::uint64_t val = 0;
            while (pos_ < s_.size() && std::iswxdigit(s_[pos_]))
            {
                const wchar_t c = s_[pos_++];
                val <<= 4;
                if (c >= L'0' && c <= L'9') val |= static_cast<std::uint64_t>(c - L'0');
                else if (c >= L'a' && c <= L'f') val |= static_cast<std::uint64_t>(10 + c - L'a');
                else val |= static_cast<std::uint64_t>(10 + c - L'A');
            }
            if (pos_ == start) return false;
            hasOperatorOrHex_ = true;
            out = static_cast<double>(val);
            return true;
        }
        size_t start = pos_;
        bool hasDigit = false;
        bool hasDot = false;
        while (pos_ < s_.size())
        {
            const wchar_t c = s_[pos_];
            if (c >= L'0' && c <= L'9') { hasDigit = true; ++pos_; }
            else if (c == L'.' && !hasDot) { hasDot = true; ++pos_; }
            else break;
        }
        if (!hasDigit) return false;
        try
        {
            out = std::stod(s_.substr(start, pos_ - start));
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    const std::wstring& s_;
    size_t pos_ = 0;
    bool hasOperatorOrHex_ = false;
};

std::wstring GetLocalIpv4Address()
{
    // Zero-dependency lookup via PowerShell-free Win32 hostname/ip or fallback
    // Using simple getaddrinfo is avoided to not pull ws2_32; use ipconfig / minimal check?
    // Even simpler: read adapter IP via standard Windows environment / socket-free? Let's use pipe to ipconfig or return empty if none.
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return L"";
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    PROCESS_INFORMATION pi{};
    wchar_t cmd[] = L"cmd.exe /c ipconfig";
    std::string output;
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(writePipe);
        writePipe = nullptr;
        char buf[1024];
        DWORD read = 0;
        while (ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0)
        {
            output.append(buf, buf + read);
        }
        WaitForSingleObject(pi.hProcess, 1500);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (writePipe) CloseHandle(writePipe);
    CloseHandle(readPipe);

    // Find first IPv4 line containing ": " and an IPv4 dotted quad not starting with 127.
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.find("IPv4") == std::string::npos) continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string ip = line.substr(colon + 1);
        while (!ip.empty() && (ip.front() == ' ' || ip.front() == '\t')) ip.erase(ip.begin());
        while (!ip.empty() && (ip.back() == '\r' || ip.back() == '\n' || ip.back() == ' ')) ip.pop_back();
        if (!ip.empty() && ip.rfind("127.", 0) != 0)
        {
            return llp::persistence::Utf8ToWide(ip);
        }
    }
    return L"127.0.0.1";
}

} // namespace

HotwordStore::HotwordStore()
{
    ResetToDefaults();
}

void HotwordStore::ResetToDefaults()
{
    entries_ = {
        {L"recycle", L"清空回收站", ActionKind::Builtin, L"empty_recycle_bin", true},
        {L"sleep", L"让电脑进入睡眠", ActionKind::Builtin, L"sleep", true},
        {L"lock", L"锁定 Windows 屏幕", ActionKind::Builtin, L"lock", true},
        {L"screenoff", L"立即熄灭显示器", ActionKind::Builtin, L"screen_off", true},
        {L"ip", L"复制本机 IPv4 地址", ActionKind::Builtin, L"copy_ip", true},
        {L"settings", L"打开 LightLaunchpad 设置", ActionKind::Builtin, L"open_settings", true},
        {L"stats", L"打开键鼠统计与热力图", ActionKind::Builtin, L"open_stats", true},
        {L"g", L"Google 搜索", ActionKind::Url, L"https://www.google.com/search?q={q}", true},
        {L"gh", L"GitHub 搜索", ActionKind::Url, L"https://github.com/search?q={q}", true},
        {L"b", L"Bilibili 搜索", ActionKind::Url, L"https://search.bilibili.com/all?keyword={q}", true}
    };
}

std::string ActionKindToString(ActionKind kind)
{
    switch (kind)
    {
    case ActionKind::Builtin: return "builtin";
    case ActionKind::Command: return "command";
    case ActionKind::Url: return "url";
    case ActionKind::Path: return "path";
    }
    return "builtin";
}

ActionKind ActionKindFromString(const std::string& value)
{
    if (value == "command") return ActionKind::Command;
    if (value == "url") return ActionKind::Url;
    if (value == "path") return ActionKind::Path;
    return ActionKind::Builtin;
}

bool HotwordStore::LoadFromFile(const std::wstring& path)
{
    const auto loaded = llp::persistence::LoadJsonWithRecovery(path);
    if (!loaded.found) return false;
    try
    {
        const Json& doc = loaded.document;
        if (!doc.is_object() || !doc.contains("Hotwords") || !doc["Hotwords"].is_array()) return false;
        std::vector<HotwordEntry> parsed;
        for (const auto& item : doc["Hotwords"])
        {
            if (!item.is_object()) continue;
            HotwordEntry e;
            e.trigger = Trim(llp::persistence::Utf8ToWide(item.value("Trigger", std::string{})));
            e.title = Trim(llp::persistence::Utf8ToWide(item.value("Title", std::string{})));
            e.kind = ActionKindFromString(item.value("Kind", std::string{"builtin"}));
            e.target = Trim(llp::persistence::Utf8ToWide(item.value("Target", std::string{})));
            e.enabled = item.value("Enabled", true);
            if (e.trigger.empty() || e.target.empty()) continue;
            if (e.title.empty()) e.title = e.trigger;
            parsed.push_back(std::move(e));
        }
        entries_ = std::move(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool HotwordStore::SaveToFile(const std::wstring& path) const
{
    Json arr = Json::array();
    for (const auto& e : entries_)
    {
        arr.push_back({
            {"Trigger", llp::persistence::WideToUtf8(e.trigger)},
            {"Title", llp::persistence::WideToUtf8(e.title)},
            {"Kind", ActionKindToString(e.kind)},
            {"Target", llp::persistence::WideToUtf8(e.target)},
            {"Enabled", e.enabled}
        });
    }
    Json doc = {
        {"SchemaVersion", 1},
        {"Hotwords", arr}
    };
    std::wstring err;
    return llp::persistence::WriteJsonAtomicWithBackup(path, doc, err);
}

void HotwordStore::SetEntryEnabled(size_t index, bool enabled)
{
    if (index < entries_.size()) entries_[index].enabled = enabled;
}

bool HotwordStore::AddOrUpdateEntry(const HotwordEntry& entry)
{
    HotwordEntry clean = entry;
    clean.trigger = ToLower(Trim(clean.trigger));
    clean.title = Trim(clean.title);
    clean.target = Trim(clean.target);
    if (clean.trigger.empty() || clean.target.empty()) return false;
    if (clean.title.empty()) clean.title = clean.trigger;

    for (auto& existing : entries_)
    {
        if (ToLower(existing.trigger) == clean.trigger)
        {
            existing = clean;
            return true;
        }
    }
    entries_.push_back(std::move(clean));
    return true;
}

bool HotwordStore::RemoveEntry(size_t index)
{
    if (index >= entries_.size()) return false;
    entries_.erase(entries_.begin() + static_cast<ptrdiff_t>(index));
    return true;
}

std::vector<MatchResult> HotwordStore::MatchQuery(const std::wstring& rawQuery, bool allowNoPrefixExact) const
{
    std::vector<MatchResult> results;
    const std::wstring trimmed = Trim(rawQuery);
    if (trimmed.empty()) return results;

    bool explicitPrefix = false;
    std::wstring body = trimmed;
    if (body[0] == L'>' || body[0] == L':' || body[0] == L'：')
    {
        explicitPrefix = true;
        body = Trim(body.substr(1));
    }

    if (!explicitPrefix && !allowNoPrefixExact) return results;

    // Split first token (trigger) and optional argument
    std::wstring token = body;
    std::wstring arg;
    const size_t sp = body.find(L' ');
    if (sp != std::wstring::npos)
    {
        token = body.substr(0, sp);
        arg = Trim(body.substr(sp + 1));
    }
    const std::wstring tokenLower = ToLower(token);

    for (const auto& e : entries_)
    {
        if (!e.enabled) continue;
        const std::wstring trigLower = ToLower(e.trigger);
        const std::wstring titleLower = ToLower(e.title);
        const bool acceptsArg = e.target.find(L"{q}") != std::wstring::npos;

        if (explicitPrefix)
        {
            if (body.empty())
            {
                results.push_back({e, L"", true, 50});
            }
            else if (trigLower == tokenLower)
            {
                results.push_back({e, arg, true, 200});
            }
            else if (arg.empty() && trigLower.rfind(tokenLower, 0) == 0)
            {
                results.push_back({e, L"", true, 140});
            }
            else if (arg.empty() && titleLower.find(tokenLower) != std::wstring::npos)
            {
                results.push_back({e, L"", true, 90});
            }
        }
        else
        {
            // Zero-conflict no-prefix mode:
            // ONLY exact trigger match (or exact trigger + argument for {q} web/command hotwords)!
            if (trigLower == tokenLower && (arg.empty() || acceptsArg))
            {
                // Avoid single-letter triggers without argument hijacking normal 1-char app search (e.g. typing "g" or "b")
                if (trigLower.size() == 1 && arg.empty()) continue;
                results.push_back({e, arg, false, acceptsArg && !arg.empty() ? 180 : 120});
            }
        }
    }

    std::stable_sort(results.begin(), results.end(), [](const MatchResult& a, const MatchResult& b) {
        return a.score > b.score;
    });
    return results;
}

CalcResult EvaluateInlineCalculator(const std::wstring& rawQuery)
{
    CalcResult res;
    std::wstring expr = Trim(rawQuery);
    if (expr.empty()) return res;
    if (expr[0] == L'=') expr = Trim(expr.substr(1));
    if (expr.empty()) return res;

    double val = 0.0;
    ExprParser parser(expr);
    if (!parser.Parse(val)) return res;

    res.valid = true;
    res.expression = expr;
    res.displayValue = FormatNumberWithCommas(val);
    res.copyValue = FormatNumberPlain(val);
    return res;
}

std::wstring UrlEncodeUtf8(const std::wstring& text)
{
    const std::string utf8 = llp::persistence::WideToUtf8(text);
    std::wostringstream oss;
    oss << std::uppercase << std::hex;
    for (unsigned char c : utf8)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            oss << static_cast<wchar_t>(c);
        }
        else if (c == ' ')
        {
            oss << L'+';
        }
        else
        {
            oss << L'%' << std::setw(2) << std::setfill(L'0') << static_cast<int>(c);
        }
    }
    return oss.str();
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text)
{
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem)
    {
        CloseClipboard();
        return false;
    }
    void* dst = GlobalLock(mem);
    if (dst)
    {
        memcpy(dst, text.c_str(), bytes);
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    else
    {
        GlobalFree(mem);
    }
    CloseClipboard();
    return true;
}

bool ExecuteHotword(
    const MatchResult& match,
    HWND hostHwnd,
    std::wstring& statusOut,
    bool& requestOpenSettings,
    bool& requestOpenStats)
{
    requestOpenSettings = false;
    requestOpenStats = false;
    const auto& e = match.entry;

    if (e.kind == ActionKind::Builtin)
    {
        if (e.target == L"empty_recycle_bin")
        {
            const HRESULT hr = SHEmptyRecycleBinW(hostHwnd, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
            statusOut = SUCCEEDED(hr) ? L"已清空回收站" : L"回收站已空或清空失败";
            return SUCCEEDED(hr);
        }
        if (e.target == L"sleep")
        {
            // Dynamic load powrprof!SetSuspendState to avoid extra link dependency
            using SuspendFn = BOOLEAN(WINAPI*)(BOOLEAN, BOOLEAN, BOOLEAN);
            HMODULE mod = LoadLibraryW(L"powrprof.dll");
            bool ok = false;
            if (mod)
            {
                auto fn = reinterpret_cast<SuspendFn>(GetProcAddress(mod, "SetSuspendState"));
                if (fn) ok = fn(FALSE, FALSE, FALSE) != FALSE;
                FreeLibrary(mod);
            }
            statusOut = ok ? L"正在进入睡眠..." : L"睡眠指令执行失败";
            return ok;
        }
        if (e.target == L"lock")
        {
            const bool ok = LockWorkStation() != FALSE;
            statusOut = ok ? L"已锁定屏幕" : L"锁屏失败";
            return ok;
        }
        if (e.target == L"screen_off")
        {
            PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
            statusOut = L"已熄灭显示器";
            return true;
        }
        if (e.target == L"copy_ip")
        {
            const std::wstring ip = GetLocalIpv4Address();
            const bool ok = !ip.empty() && CopyTextToClipboard(hostHwnd, ip);
            statusOut = ok ? (L"已复制本机 IP: " + ip) : L"获取本机 IP 失败";
            return ok;
        }
        if (e.target == L"open_settings")
        {
            requestOpenSettings = true;
            statusOut = L"打开设置中心";
            return true;
        }
        if (e.target == L"open_stats")
        {
            requestOpenStats = true;
            statusOut = L"打开键鼠统计";
            return true;
        }
        statusOut = L"未知内置指令: " + e.target;
        return false;
    }

    if (e.kind == ActionKind::Url)
    {
        std::wstring url = e.target;
        const size_t pos = url.find(L"{q}");
        if (pos != std::wstring::npos)
        {
            url.replace(pos, 3, UrlEncodeUtf8(match.argument));
        }
        const HINSTANCE r = ShellExecuteW(hostHwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        const bool ok = reinterpret_cast<INT_PTR>(r) > 32;
        statusOut = ok ? (L"已打开: " + e.title) : L"打开链接失败";
        return ok;
    }

    if (e.kind == ActionKind::Path)
    {
        const HINSTANCE r = ShellExecuteW(hostHwnd, L"open", e.target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        const bool ok = reinterpret_cast<INT_PTR>(r) > 32;
        statusOut = ok ? (L"已打开: " + e.title) : L"打开路径失败";
        return ok;
    }

    if (e.kind == ActionKind::Command)
    {
        std::wstring cmdLine = e.target;
        const size_t pos = cmdLine.find(L"{q}");
        if (pos != std::wstring::npos)
        {
            cmdLine.replace(pos, 3, match.argument);
        }
        std::wstring fullCmd = L"cmd.exe /c " + cmdLine;
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> mutableCmd(fullCmd.begin(), fullCmd.end());
        mutableCmd.push_back(L'\0');
        const BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        if (ok)
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        statusOut = ok ? (L"已执行指令: " + e.title) : L"指令执行失败";
        return ok != FALSE;
    }
    return false;
}

} // namespace llp::hotwords
