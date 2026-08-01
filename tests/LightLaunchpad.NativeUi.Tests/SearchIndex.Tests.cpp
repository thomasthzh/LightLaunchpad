#include "SearchIndex.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using llp::search::History;
using llp::search::Index;
using llp::search::Item;
using llp::search::MatchClass;
using llp::search::QueryOptions;
using llp::search::Result;

int passed = 0;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void RequireEqual(const std::wstring& expected, const std::wstring& actual, const char* message)
{
    if (expected != actual) throw std::runtime_error(message);
}

const Result& First(const std::vector<Result>& results, const char* message)
{
    Require(!results.empty(), message);
    return results.front();
}

bool Contains(const std::vector<std::wstring>& values, const std::wstring& expected)
{
    return std::find(values.begin(), values.end(), expected) != values.end();
}

Item MakeItem(const wchar_t* id, const wchar_t* name, const wchar_t* source = L"")
{
    Item item;
    item.stableId = id;
    item.displayName = name;
    item.sourcePath = source;
    return item;
}

void Pass(const char* name)
{
    ++passed;
    std::cout << "PASS " << name << "\n";
}

void TestNamePriorityAndDeterminism()
{
    History history;
    std::vector<Item> items = {
        MakeItem(L"contains", L"MyAlphaTool"),
        MakeItem(L"boundary", L"Studio Alpha"),
        MakeItem(L"prefix", L"Alpha Studio"),
        MakeItem(L"exact", L"Alpha")
    };
    Index index;
    index.Rebuild(items, history);
    const auto results = index.Query(L"ALPHA");
    Require(results.size() == 4, "Name priority result count mismatch.");
    RequireEqual(L"exact", results[0].stableId, "Exact name did not rank first.");
    RequireEqual(L"prefix", results[1].stableId, "Name prefix did not rank second.");
    RequireEqual(L"boundary", results[2].stableId, "Word boundary did not rank before contains.");
    RequireEqual(L"contains", results[3].stableId, "Name contains priority mismatch.");
    Require(results[0].matchClass == MatchClass::ExactName, "Exact match class mismatch.");
    Pass("SearchIndex_NameExactPrefixBoundaryContainsOrder");
}

void TestPinyinCoverageAndInitials()
{
    const std::vector<std::pair<std::wstring, std::wstring>> fixtures = {
        { L"个人中心", L"gerenzhongxin" },
        { L"黑神话悟空", L"heishenhuawukong" },
        { L"三角洲行动", L"sanjiaozhouxingdong" },
        { L"觅长生", L"michangsheng" },
        { L"网易云音乐", L"wangyiyunyinyue" },
        { L"给梨加速器", L"geilijiasuqi" }
    };
    const std::vector<std::wstring> initials = {
        L"grzx", L"hshwk", L"sjzxd", L"mcs", L"wyyyy", L"gljsq"
    };
    History history;
    std::vector<Item> items;
    for (size_t index = 0; index < fixtures.size(); ++index)
    {
        auto item = MakeItem((L"cn-" + std::to_wstring(index)).c_str(), fixtures[index].first.c_str());
        items.push_back(std::move(item));
        const auto forms = Index::BuildPinyinForms(fixtures[index].first);
        Require(forms.coveredHanCount == forms.totalHanCount, "A current launch-folder Han character lacks full pinyin.");
        Require(Contains(forms.full, fixtures[index].second), "Expected full-pinyin candidate is missing.");
        Require(Contains(forms.initials, initials[index]), "Expected pinyin initials candidate is missing.");
    }
    Index index;
    index.Rebuild(items, history);
    for (size_t fixture = 0; fixture < fixtures.size(); ++fixture)
    {
        RequireEqual(
            (L"cn-" + std::to_wstring(fixture)),
            First(index.Query(fixtures[fixture].second), "Full pinyin returned no result.").stableId,
            "Full pinyin ranked the wrong item.");
        RequireEqual(
            (L"cn-" + std::to_wstring(fixture)),
            First(index.Query(initials[fixture]), "Pinyin initials returned no result.").stableId,
            "Pinyin initials ranked the wrong item.");
    }
    std::cout << "METRIC PinyinCoverage currentNames=" << fixtures.size() << "/" << fixtures.size() << "\n";
    Pass("SearchIndex_CurrentFolderFullPinyinAndInitialsCoverage");
}

void TestAliasesMetadataAndPathIsolation()
{
    History history;
    auto code = MakeItem(L"code", L"Visual Studio Code", L"C:\\Common Tools\\Code.lnk");
    code.aliases = { L"VS Code", L"VSC" };
    code.subtitle = L"Code.exe";
    auto common = MakeItem(L"common-name", L"Common Console", L"C:\\Apps\\Console.lnk");
    Index index;
    index.Rebuild({ code, common }, history);
    RequireEqual(L"code", First(index.Query(L"vsc"), "Alias query returned no result.").stableId, "Alias did not match its item.");
    RequireEqual(L"code", First(index.Query(L"Code.exe"), "Subtitle query returned no result.").stableId, "Subtitle did not match its item.");
    const auto commonResults = index.Query(L"common");
    Require(commonResults.size() == 1, "A common parent-folder path polluted normal matching.");
    RequireEqual(L"common-name", commonResults.front().stableId, "Normal name lost to a parent-folder path.");
    RequireEqual(L"code", First(index.Query(L"path:common tools"), "Explicit path query returned no result.").stableId, "Explicit path search failed.");
    Pass("SearchIndex_SeparatesAliasesSubtitleAndExplicitPath");
}

void TestFuzzyAndNameBeforePinyin()
{
    History history;
    auto google = MakeItem(L"google", L"Google Chrome");
    auto pinyinOnly = MakeItem(L"pinyin", L"谷歌浏览器");
    Index index;
    index.Rebuild({ pinyinOnly, google }, history);
    RequireEqual(L"google", First(index.Query(L"google"), "English name query returned no result.").stableId, "Name match did not rank before pinyin metadata.");
    RequireEqual(L"google", First(index.Query(L"gogle"), "Typo query returned no fuzzy result.").stableId, "Light fuzzy matching failed a one-edit typo.");
    Pass("SearchIndex_NameBeforePinyinAndLightFuzzyTypo");
}

void TestUsageTieBreakersAndPrivacy()
{
    History history;
    history.RecordLaunch(L"frequent", 100);
    history.RecordLaunch(L"frequent", 200);
    history.RecordLaunch(L"recent", 300);
    history.SetFavorite(L"favorite", true);

    Index index;
    index.Rebuild({
        MakeItem(L"favorite", L"Editor"),
        MakeItem(L"recent", L"Editor"),
        MakeItem(L"frequent", L"Editor"),
        MakeItem(L"stable-b", L"Editor"),
        MakeItem(L"stable-a", L"Editor")
    }, history);
    auto results = index.Query(L"editor");
    RequireEqual(L"frequent", results[0].stableId, "Frequency did not break an equal-score tie first.");
    RequireEqual(L"recent", results[1].stableId, "Recency did not break the next equal-score tie.");
    RequireEqual(L"favorite", results[2].stableId, "Favorite did not break the next equal-score tie.");
    RequireEqual(L"stable-a", results[3].stableId, "Stable ID did not make duplicate-name ordering deterministic.");

    QueryOptions hidden;
    hidden.spotlight = true;
    hidden.showRecentAndFavorites = false;
    Require(index.Query(L"", hidden).empty(), "Disabling recent/favorite visibility exposed the full app wall.");
    QueryOptions visible;
    visible.spotlight = true;
    visible.showRecentAndFavorites = true;
    visible.emptyLimit = 10;
    Require(index.Query(L"", visible).size() == 3, "Empty Spotlight did not return only local recent/favorite items.");

    history.SetTrackingEnabled(false);
    history.RecordLaunch(L"private", 999);
    Require(history.Get(L"private").launchCount == 0, "Disabled history tracking still recorded a launch.");
    history.SetTrackingEnabled(true);
    history.ClearUsageHistory();
    Require(history.Get(L"frequent").launchCount == 0, "Clear history retained launch counts.");
    Require(history.Get(L"favorite").favorite, "Clear history removed favorites.");

    std::wstring error;
    const std::string serialized = history.SerializeUtf8();
    const auto restored = History::ParseUtf8(serialized, error);
    Require(error.empty(), "History JSON round trip reported an error.");
    Require(restored.Get(L"favorite").favorite, "History JSON round trip lost a favorite.");
    const auto future = History::ParseUtf8(R"({"SchemaVersion":99,"Items":[]})", error);
    Require(!error.empty(), "A future search-history schema was accepted for downgrade.");
    Require(future.Get(L"favorite").launchCount == 0, "Rejected future history mutated the loaded state.");
    Pass("SearchIndex_UsageTieBreakersEmptyStatePrivacyAndHistoryReset");
}

void TestStableFocusLookup()
{
    const std::vector<Result> results = {
        { L"one", L"One", MatchClass::NamePrefix, 10 },
        { L"focus", L"Focus", MatchClass::NamePrefix, 10 },
        { L"three", L"Three", MatchClass::NamePrefix, 10 }
    };
    Require(Index::FindStableResult(results, L"focus") == 1, "Stable focus ID was not preserved.");
    Require(Index::FindStableResult(results, L"missing") == -1, "Missing stable focus ID did not return -1.");
    Pass("SearchIndex_StableResultFocusLookup");
}

void TestSearchHotPathAtThreeHundredItems()
{
    History history;
    std::vector<Item> items;
    items.reserve(300);
    for (int index = 0; index < 300; ++index)
    {
        items.push_back(MakeItem(
            (L"perf-" + std::to_wstring(index)).c_str(),
            (L"黑神话悟空 " + std::to_wstring(index)).c_str()));
    }
    Index index;
    const auto rebuildStart = std::chrono::steady_clock::now();
    index.Rebuild(items, history);
    const auto rebuildMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - rebuildStart).count();
    for (int warmup = 0; warmup < 5; ++warmup) Require(index.Query(L"heishen").size() == 300, "Pinyin performance warmup lost results.");

    std::vector<long long> samples;
    samples.reserve(100);
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        const auto start = std::chrono::steady_clock::now();
        const auto results = index.Query(L"heishen");
        samples.push_back(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
        Require(results.size() == 300, "Pinyin performance query lost results.");
    }
    std::sort(samples.begin(), samples.end());
    const long long p95Microseconds = samples[94];
    Require(p95Microseconds <= 20000, "A 300-item cached pinyin query exceeded the 20 ms module budget.");
    std::cout << "METRIC SearchQuery300 rebuildUs=" << rebuildMicroseconds
              << " p95Us=" << p95Microseconds << " iterations=100\n";
    Pass("SearchIndex_ThreeHundredItemCachedPinyinQueryStaysBounded");
}
}

int main()
{
    try
    {
        TestNamePriorityAndDeterminism();
        TestPinyinCoverageAndInitials();
        TestAliasesMetadataAndPathIsolation();
        TestFuzzyAndNameBeforePinyin();
        TestUsageTieBreakersAndPrivacy();
        TestStableFocusLookup();
        TestSearchHotPathAtThreeHundredItems();
        std::cout << passed << "/7 tests passed\n";
        return passed == 7 ? 0 : 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL " << exception.what() << "\n";
        return 1;
    }
}
