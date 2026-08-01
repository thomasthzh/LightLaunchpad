#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace llp::search
{
struct Usage
{
    uint64_t launchCount = 0;
    uint64_t lastLaunchedUnixMilliseconds = 0;
    bool favorite = false;
};

class History
{
public:
    void SetTrackingEnabled(bool enabled);
    bool IsTrackingEnabled() const;
    void RecordLaunch(const std::wstring& stableId, uint64_t unixMilliseconds);
    void SetFavorite(const std::wstring& stableId, bool favorite);
    void ClearUsageHistory();
    Usage Get(const std::wstring& stableId) const;
    std::string SerializeUtf8() const;
    static History ParseUtf8(const std::string& documentUtf8, std::wstring& error);

private:
    bool trackingEnabled_ = true;
    std::map<std::wstring, Usage> records_;
};

struct Item
{
    std::wstring stableId;
    std::wstring displayName;
    std::vector<std::wstring> aliases;
    std::wstring subtitle;
    std::wstring sourcePath;
};

enum class MatchClass
{
    ExactName,
    NamePrefix,
    NameBoundary,
    NameContains,
    Alias,
    Pinyin,
    Metadata,
    Fuzzy,
    EmptyHistory
};

struct Result
{
    std::wstring stableId;
    std::wstring displayName;
    MatchClass matchClass = MatchClass::Fuzzy;
    int score = 0;
};

struct QueryOptions
{
    bool spotlight = true;
    bool showRecentAndFavorites = true;
    size_t emptyLimit = 10;
};

struct PinyinForms
{
    std::vector<std::wstring> full;
    std::vector<std::wstring> initials;
    size_t coveredHanCount = 0;
    size_t totalHanCount = 0;
};

class Index
{
public:
    Index();
    ~Index();
    Index(Index&&) noexcept;
    Index& operator=(Index&&) noexcept;
    Index(const Index&) = delete;
    Index& operator=(const Index&) = delete;

    void Rebuild(const std::vector<Item>& items, const History& history);
    std::vector<Result> Query(const std::wstring& query, const QueryOptions& options = {}) const;
    static PinyinForms BuildPinyinForms(const std::wstring& text);
    static int FindStableResult(const std::vector<Result>& results, const std::wstring& stableId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
