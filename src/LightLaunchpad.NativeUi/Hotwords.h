#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace llp::hotwords
{

enum class ActionKind
{
    Builtin,
    Command,
    Url,
    Path
};

struct HotwordEntry
{
    std::wstring trigger;   // e.g. L"recycle", L"sleep", L"gh"
    std::wstring title;     // e.g. L"清空回收站"
    ActionKind kind = ActionKind::Builtin;
    std::wstring target;    // builtin id, shell command, URL template with {q}, or local path
    bool enabled = true;
};

struct MatchResult
{
    HotwordEntry entry;
    std::wstring argument;      // optional argument after trigger, e.g. "rust" for ">gh rust"
    bool explicitPrefix = false; // true when invoked via '>' or ':'
    int score = 0;
};

struct CalcResult
{
    bool valid = false;
    std::wstring displayValue;  // formatted result, e.g. L"4,608"
    std::wstring copyValue;     // clean value to copy, e.g. L"4608"
    std::wstring expression;
};

class HotwordStore
{
public:
    HotwordStore();

    void ResetToDefaults();
    bool LoadFromFile(const std::wstring& path);
    bool SaveToFile(const std::wstring& path) const;

    const std::vector<HotwordEntry>& Entries() const { return entries_; }
    void SetEntryEnabled(size_t index, bool enabled);
    bool AddOrUpdateEntry(const HotwordEntry& entry);
    bool RemoveEntry(size_t index);

    // Zero-conflict query matcher:
    // - If query starts with '>' or ':', enters explicit hotword mode (prefix + substring matching).
    // - Otherwise, if allowNoPrefixExact is true, only matches exact trigger (or trigger + space + arg for URL/Command with {q}).
    std::vector<MatchResult> MatchQuery(const std::wstring& rawQuery, bool allowNoPrefixExact) const;

private:
    std::vector<HotwordEntry> entries_;
};

// Pure C++ recursive-descent math & hex evaluator (zero external dependencies).
// Only returns valid=true when input is an actual math expression (contains operator or 0x hex).
CalcResult EvaluateInlineCalculator(const std::wstring& rawQuery);

// URL-encodes UTF-8 argument for {q} template substitution.
std::wstring UrlEncodeUtf8(const std::wstring& text);

// ActionKind string conversions for JSON & UI.
std::string ActionKindToString(ActionKind kind);
ActionKind ActionKindFromString(const std::string& value);

// Executes a matched hotword (builtin Win32 APIs, ShellExecuteW URL/Path, or CreateProcessW command).
// Returns true if executed; statusOut describes the outcome or error.
bool ExecuteHotword(
    const MatchResult& match,
    HWND hostHwnd,
    std::wstring& statusOut,
    bool& requestOpenSettings,
    bool& requestOpenStats);

bool CopyTextToClipboard(HWND owner, const std::wstring& text);

} // namespace llp::hotwords
