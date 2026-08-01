#include "SearchIndex.h"

#include <pinyin-data/PinyinData.generated.h>
#include <nlohmann/json.hpp>
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

namespace llp::search
{
namespace
{
constexpr int NoMatch = 1000000;
constexpr size_t MaxPinyinCandidates = 64;

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length) == length
        ? result : std::wstring{};
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr) == length
        ? result : std::string{};
}

bool IsHanCodePoint(uint32_t codePoint)
{
    return (codePoint >= 0x3400 && codePoint <= 0x4DBF)
        || (codePoint >= 0x4E00 && codePoint <= 0x9FFF)
        || (codePoint >= 0x20000 && codePoint <= 0x323AF);
}

template<typename Callback>
void ForEachCodePoint(const std::wstring& text, Callback callback)
{
    for (size_t index = 0; index < text.size(); ++index)
    {
        uint32_t codePoint = static_cast<uint16_t>(text[index]);
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF && index + 1 < text.size())
        {
            const uint32_t low = static_cast<uint16_t>(text[index + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF)
            {
                codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                ++index;
            }
        }
        callback(codePoint);
    }
}

std::string_view ReadingSet(uint32_t codePoint)
{
    const auto found = std::lower_bound(
        pinyin_data::CodePoints.begin(),
        pinyin_data::CodePoints.end(),
        codePoint);
    if (found == pinyin_data::CodePoints.end() || *found != codePoint) return {};
    const size_t index = static_cast<size_t>(found - pinyin_data::CodePoints.begin());
    return pinyin_data::ReadingSets[pinyin_data::ReadingSetIndexes[index]];
}

std::vector<std::wstring> SplitReadings(std::string_view readingSet)
{
    std::vector<std::wstring> readings;
    size_t start = 0;
    while (start < readingSet.size())
    {
        const size_t comma = readingSet.find(',', start);
        const size_t end = comma == std::string_view::npos ? readingSet.size() : comma;
        std::wstring reading;
        reading.reserve(end - start);
        for (size_t index = start; index < end; ++index) reading.push_back(static_cast<wchar_t>(readingSet[index]));
        if (!reading.empty() && std::find(readings.begin(), readings.end(), reading) == readings.end()) readings.push_back(std::move(reading));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return readings;
}

void AppendAlternatives(std::vector<std::wstring>& candidates, const std::vector<std::wstring>& alternatives, bool initials)
{
    if (alternatives.empty()) return;
    std::vector<std::wstring> expanded;
    expanded.reserve(std::min(MaxPinyinCandidates, candidates.size() * alternatives.size()));
    for (const auto& candidate : candidates)
    {
        for (const auto& alternative : alternatives)
        {
            auto next = candidate;
            if (initials) next.push_back(alternative.front());
            else next += alternative;
            if (std::find(expanded.begin(), expanded.end(), next) == expanded.end()) expanded.push_back(std::move(next));
            if (expanded.size() >= MaxPinyinCandidates) break;
        }
        if (expanded.size() >= MaxPinyinCandidates) break;
    }
    candidates = std::move(expanded);
}

std::wstring NormalizeText(const std::wstring& text)
{
    std::wstring result;
    result.reserve(text.size());
    bool previousSpace = true;
    ForEachCodePoint(text, [&](uint32_t codePoint) {
        if (codePoint == 0x3000) codePoint = L' ';
        else if (codePoint >= 0xFF01 && codePoint <= 0xFF5E) codePoint -= 0xFEE0;

        if (codePoint <= 0xFFFF)
        {
            const wchar_t value = static_cast<wchar_t>(codePoint);
            if (std::iswalnum(value) || IsHanCodePoint(codePoint))
            {
                result.push_back(static_cast<wchar_t>(std::towlower(value)));
                previousSpace = false;
                return;
            }
        }
        else if (IsHanCodePoint(codePoint))
        {
            const uint32_t adjusted = codePoint - 0x10000;
            result.push_back(static_cast<wchar_t>(0xD800 + (adjusted >> 10)));
            result.push_back(static_cast<wchar_t>(0xDC00 + (adjusted & 0x3FF)));
            previousSpace = false;
            return;
        }

        if (!previousSpace && !result.empty())
        {
            result.push_back(L' ');
            previousSpace = true;
        }
    });
    while (!result.empty() && result.back() == L' ') result.pop_back();
    return result;
}

std::vector<std::wstring> Tokens(const std::wstring& normalized)
{
    std::vector<std::wstring> tokens;
    size_t start = 0;
    while (start < normalized.size())
    {
        const size_t space = normalized.find(L' ', start);
        const size_t end = space == std::wstring::npos ? normalized.size() : space;
        if (end > start) tokens.push_back(normalized.substr(start, end - start));
        if (space == std::wstring::npos) break;
        start = space + 1;
    }
    return tokens;
}

int BoundaryIndex(const std::wstring& text, const std::wstring& token)
{
    size_t position = text.find(token);
    while (position != std::wstring::npos)
    {
        if (position == 0 || text[position - 1] == L' ') return static_cast<int>(position);
        position = text.find(token, position + 1);
    }
    return -1;
}

int DamerauLevenshtein(const std::wstring& left, const std::wstring& right)
{
    const size_t rows = left.size() + 1;
    const size_t columns = right.size() + 1;
    std::vector<int> distance(rows * columns, 0);
    auto at = [&](size_t row, size_t column) -> int& { return distance[row * columns + column]; };
    for (size_t row = 0; row < rows; ++row) at(row, 0) = static_cast<int>(row);
    for (size_t column = 0; column < columns; ++column) at(0, column) = static_cast<int>(column);
    for (size_t row = 1; row < rows; ++row)
    {
        for (size_t column = 1; column < columns; ++column)
        {
            const int substitution = left[row - 1] == right[column - 1] ? 0 : 1;
            at(row, column) = std::min({
                at(row - 1, column) + 1,
                at(row, column - 1) + 1,
                at(row - 1, column - 1) + substitution
            });
            if (row > 1 && column > 1
                && left[row - 1] == right[column - 2]
                && left[row - 2] == right[column - 1])
            {
                at(row, column) = std::min(at(row, column), at(row - 2, column - 2) + 1);
            }
        }
    }
    return at(rows - 1, columns - 1);
}

int FuzzyDistance(const std::wstring& text, const std::wstring& token)
{
    int best = NoMatch;
    for (const auto& word : Tokens(text))
    {
        const int allowed = word.size() <= 4 || token.size() <= 4 ? 1 : 2;
        const int distance = DamerauLevenshtein(word, token);
        if (distance <= allowed) best = std::min(best, distance);
    }
    return best;
}

struct TokenScore
{
    int score = NoMatch;
    MatchClass matchClass = MatchClass::Fuzzy;
};

TokenScore ScoreName(const std::wstring& text, const std::wstring& token)
{
    if (text == token) return { 0, MatchClass::ExactName };
    if (text.rfind(token, 0) == 0) return { 20 + static_cast<int>(text.size() - token.size()), MatchClass::NamePrefix };
    const int boundary = BoundaryIndex(text, token);
    if (boundary >= 0) return { 100 + boundary, MatchClass::NameBoundary };
    const size_t contains = text.find(token);
    if (contains != std::wstring::npos) return { 200 + static_cast<int>(contains), MatchClass::NameContains };
    return {};
}

TokenScore ScoreContains(const std::vector<std::wstring>& fields, const std::wstring& token, int base, MatchClass matchClass)
{
    TokenScore best;
    for (const auto& field : fields)
    {
        const size_t position = field.find(token);
        if (position != std::wstring::npos)
        {
            const int score = base + static_cast<int>(position);
            if (score < best.score) best = { score, matchClass };
        }
    }
    return best;
}

bool BetterUsage(const Usage& left, const Usage& right)
{
    if (left.launchCount != right.launchCount) return left.launchCount > right.launchCount;
    if (left.lastLaunchedUnixMilliseconds != right.lastLaunchedUnixMilliseconds)
        return left.lastLaunchedUnixMilliseconds > right.lastLaunchedUnixMilliseconds;
    if (left.favorite != right.favorite) return left.favorite;
    return false;
}
}

struct Index::Impl
{
    struct Entry
    {
        Item item;
        Usage usage;
        std::wstring name;
        std::vector<std::wstring> aliases;
        std::vector<std::wstring> pinyin;
        std::vector<std::wstring> initials;
        std::wstring subtitle;
        std::wstring path;
    };
    std::vector<Entry> entries;
};

void History::SetTrackingEnabled(bool enabled)
{
    trackingEnabled_ = enabled;
}

bool History::IsTrackingEnabled() const
{
    return trackingEnabled_;
}

void History::RecordLaunch(const std::wstring& stableId, uint64_t unixMilliseconds)
{
    if (!trackingEnabled_ || stableId.empty()) return;
    auto& usage = records_[stableId];
    if (usage.launchCount < std::numeric_limits<uint64_t>::max()) usage.launchCount++;
    usage.lastLaunchedUnixMilliseconds = std::max(usage.lastLaunchedUnixMilliseconds, unixMilliseconds);
}

void History::SetFavorite(const std::wstring& stableId, bool favorite)
{
    if (stableId.empty()) return;
    auto& usage = records_[stableId];
    usage.favorite = favorite;
    if (!favorite && usage.launchCount == 0) records_.erase(stableId);
}

void History::ClearUsageHistory()
{
    for (auto iterator = records_.begin(); iterator != records_.end();)
    {
        iterator->second.launchCount = 0;
        iterator->second.lastLaunchedUnixMilliseconds = 0;
        if (!iterator->second.favorite) iterator = records_.erase(iterator);
        else ++iterator;
    }
}

Usage History::Get(const std::wstring& stableId) const
{
    const auto found = records_.find(stableId);
    return found == records_.end() ? Usage{} : found->second;
}

std::string History::SerializeUtf8() const
{
    nlohmann::json document = {
        { "SchemaVersion", 1 },
        { "Items", nlohmann::json::array() }
    };
    for (const auto& [stableId, usage] : records_)
    {
        if (!usage.favorite && usage.launchCount == 0) continue;
        document["Items"].push_back({
            { "StableId", WideToUtf8(stableId) },
            { "LaunchCount", usage.launchCount },
            { "LastLaunchedUnixMilliseconds", usage.lastLaunchedUnixMilliseconds },
            { "Favorite", usage.favorite }
        });
    }
    return document.dump(2) + "\n";
}

History History::ParseUtf8(const std::string& documentUtf8, std::wstring& error)
{
    History history;
    error.clear();
    if (documentUtf8.empty()) return history;
    try
    {
        const auto document = nlohmann::json::parse(documentUtf8);
        if (!document.is_object()
            || !document.contains("SchemaVersion")
            || !document["SchemaVersion"].is_number_integer()
            || document["SchemaVersion"].get<int>() != 1)
        {
            error = L"Search history SchemaVersion must be 1.";
            return History{};
        }
        if (!document.contains("Items") || !document["Items"].is_array())
        {
            error = L"Search history Items must be an array.";
            return History{};
        }
        for (const auto& entry : document["Items"])
        {
            if (!entry.is_object()
                || !entry.contains("StableId") || !entry["StableId"].is_string()
                || !entry.contains("LaunchCount") || !entry["LaunchCount"].is_number_unsigned()
                || !entry.contains("LastLaunchedUnixMilliseconds") || !entry["LastLaunchedUnixMilliseconds"].is_number_unsigned()
                || !entry.contains("Favorite") || !entry["Favorite"].is_boolean())
            {
                error = L"Search history contains an invalid item record.";
                return History{};
            }
            const auto stableId = Utf8ToWide(entry["StableId"].get<std::string>());
            if (stableId.empty())
            {
                error = L"Search history contains an empty stable item ID.";
                return History{};
            }
            Usage usage;
            usage.launchCount = entry["LaunchCount"].get<uint64_t>();
            usage.lastLaunchedUnixMilliseconds = entry["LastLaunchedUnixMilliseconds"].get<uint64_t>();
            usage.favorite = entry["Favorite"].get<bool>();
            if (usage.favorite || usage.launchCount > 0) history.records_[stableId] = usage;
        }
    }
    catch (const nlohmann::json::exception& exception)
    {
        error = L"Search history JSON is invalid: " + Utf8ToWide(exception.what());
        return History{};
    }
    return history;
}

Index::Index() : impl_(std::make_unique<Impl>()) {}
Index::~Index() = default;
Index::Index(Index&&) noexcept = default;
Index& Index::operator=(Index&&) noexcept = default;

PinyinForms Index::BuildPinyinForms(const std::wstring& text)
{
    PinyinForms forms;
    forms.full = { L"" };
    forms.initials = { L"" };
    bool addedContent = false;
    ForEachCodePoint(text, [&](uint32_t codePoint) {
        if (IsHanCodePoint(codePoint))
        {
            forms.totalHanCount++;
            const auto readings = SplitReadings(ReadingSet(codePoint));
            if (!readings.empty())
            {
                forms.coveredHanCount++;
                AppendAlternatives(forms.full, readings, false);
                AppendAlternatives(forms.initials, readings, true);
                addedContent = true;
            }
            return;
        }
        if (codePoint <= 0x7F && std::iswalnum(static_cast<wchar_t>(codePoint)))
        {
            const wchar_t value = static_cast<wchar_t>(std::towlower(static_cast<wchar_t>(codePoint)));
            for (auto& candidate : forms.full) candidate.push_back(value);
            for (auto& candidate : forms.initials) candidate.push_back(value);
            addedContent = true;
        }
    });
    if (!addedContent)
    {
        forms.full.clear();
        forms.initials.clear();
    }
    return forms;
}

void Index::Rebuild(const std::vector<Item>& items, const History& history)
{
    impl_->entries.clear();
    impl_->entries.reserve(items.size());
    for (const auto& item : items)
    {
        if (item.stableId.empty() || item.displayName.empty()) continue;
        Impl::Entry entry;
        entry.item = item;
        entry.usage = history.Get(item.stableId);
        entry.name = NormalizeText(item.displayName);
        for (const auto& alias : item.aliases)
        {
            const auto normalized = NormalizeText(alias);
            if (!normalized.empty() && std::find(entry.aliases.begin(), entry.aliases.end(), normalized) == entry.aliases.end()) entry.aliases.push_back(normalized);
        }
        entry.subtitle = NormalizeText(item.subtitle);
        entry.path = NormalizeText(item.sourcePath);
        const auto pinyin = BuildPinyinForms(item.displayName);
        for (const auto& full : pinyin.full) entry.pinyin.push_back(NormalizeText(full));
        for (const auto& initials : pinyin.initials) entry.initials.push_back(NormalizeText(initials));
        for (const auto& alias : item.aliases)
        {
            const auto aliasPinyin = BuildPinyinForms(alias);
            for (const auto& full : aliasPinyin.full) entry.pinyin.push_back(NormalizeText(full));
            for (const auto& initials : aliasPinyin.initials) entry.initials.push_back(NormalizeText(initials));
        }
        impl_->entries.push_back(std::move(entry));
    }
}

std::vector<Result> Index::Query(const std::wstring& rawQuery, const QueryOptions& options) const
{
    struct Scored
    {
        Result result;
        Usage usage;
        std::wstring normalizedName;
    };
    std::vector<Scored> scored;
    bool pathMode = false;
    std::wstring query = rawQuery;
    const auto firstNonSpace = query.find_first_not_of(L" \t\r\n");
    if (firstNonSpace != std::wstring::npos)
    {
        auto prefix = query.substr(firstNonSpace, std::min<size_t>(5, query.size() - firstNonSpace));
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if (prefix == L"path:")
        {
            pathMode = true;
            query = query.substr(firstNonSpace + 5);
        }
    }
    const auto normalizedQuery = NormalizeText(query);
    const auto tokens = Tokens(normalizedQuery);

    if (tokens.empty())
    {
        for (const auto& entry : impl_->entries)
        {
            if (options.spotlight && (!options.showRecentAndFavorites || (entry.usage.launchCount == 0 && !entry.usage.favorite))) continue;
            scored.push_back({ { entry.item.stableId, entry.item.displayName, MatchClass::EmptyHistory, 0 }, entry.usage, entry.name });
        }
    }
    else
    {
        for (const auto& entry : impl_->entries)
        {
            int total = 0;
            MatchClass worstClass = MatchClass::ExactName;
            bool matched = true;
            for (const auto& token : tokens)
            {
                TokenScore best;
                if (pathMode)
                {
                    const size_t position = entry.path.find(token);
                    if (position != std::wstring::npos) best = { 500 + static_cast<int>(position), MatchClass::Metadata };
                }
                else
                {
                    best = ScoreName(entry.name, token);
                    const auto alias = ScoreContains(entry.aliases, token, 300, MatchClass::Alias);
                    if (alias.score < best.score) best = alias;
                    auto pinyin = ScoreContains(entry.pinyin, token, 400, MatchClass::Pinyin);
                    const auto initials = ScoreContains(entry.initials, token, 400, MatchClass::Pinyin);
                    if (initials.score < pinyin.score) pinyin = initials;
                    if (pinyin.score < best.score) best = pinyin;
                    if (!entry.subtitle.empty())
                    {
                        const size_t position = entry.subtitle.find(token);
                        if (position != std::wstring::npos)
                        {
                            const TokenScore metadata{ 500 + static_cast<int>(position), MatchClass::Metadata };
                            if (metadata.score < best.score) best = metadata;
                        }
                    }
                    if (best.score == NoMatch)
                    {
                        int distance = FuzzyDistance(entry.name, token);
                        for (const auto& aliasText : entry.aliases) distance = std::min(distance, FuzzyDistance(aliasText, token));
                        if (distance != NoMatch) best = { 700 + distance * 10, MatchClass::Fuzzy };
                    }
                }
                if (best.score == NoMatch)
                {
                    matched = false;
                    break;
                }
                total += best.score;
                if (static_cast<int>(best.matchClass) > static_cast<int>(worstClass)) worstClass = best.matchClass;
            }
            if (matched) scored.push_back({ { entry.item.stableId, entry.item.displayName, worstClass, total }, entry.usage, entry.name });
        }
    }

    std::sort(scored.begin(), scored.end(), [](const Scored& left, const Scored& right) {
        if (left.result.score != right.result.score) return left.result.score < right.result.score;
        if (BetterUsage(left.usage, right.usage)) return true;
        if (BetterUsage(right.usage, left.usage)) return false;
        if (left.normalizedName != right.normalizedName) return left.normalizedName < right.normalizedName;
        return left.result.stableId < right.result.stableId;
    });

    size_t limit = scored.size();
    if (tokens.empty() && options.spotlight) limit = std::min(limit, std::min<size_t>(12, std::max<size_t>(1, options.emptyLimit)));
    std::vector<Result> results;
    results.reserve(limit);
    for (size_t index = 0; index < limit; ++index) results.push_back(std::move(scored[index].result));
    return results;
}

int Index::FindStableResult(const std::vector<Result>& results, const std::wstring& stableId)
{
    for (size_t index = 0; index < results.size(); ++index)
    {
        if (results[index].stableId == stableId) return static_cast<int>(index);
    }
    return -1;
}
}
