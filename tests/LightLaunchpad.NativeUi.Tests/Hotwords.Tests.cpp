#include <windows.h>

#include <iostream>
#include <string>

#include "Hotwords.h"

namespace
{
int g_passed = 0;
int g_failed = 0;

void Check(bool cond, const char* name)
{
    if (cond) { ++g_passed; }
    else { ++g_failed; std::cerr << "FAIL: " << name << "\n"; }
}
}

int main()
{
    using namespace llp::hotwords;
    HotwordStore store;

    // 1. Zero-conflict: partial word "sl" must NOT trigger "sleep" without prefix
    Check(store.MatchQuery(L"sl", true).empty(), "no-prefix partial does not conflict with apps");

    // 2. Zero-conflict: single-char "g" without arg must NOT hijack app search
    Check(store.MatchQuery(L"g", true).empty(), "single-char web hotword without arg does not hijack");

    // 3. Exact match without prefix triggers "sleep" and "recycle"
    auto mSleep = store.MatchQuery(L"sleep", true);
    Check(mSleep.size() == 1 && mSleep[0].entry.trigger == L"sleep" && !mSleep[0].explicitPrefix, "exact no-prefix sleep");
    auto mRecycle = store.MatchQuery(L"RECYCLE", true);
    Check(mRecycle.size() == 1 && mRecycle[0].entry.trigger == L"recycle", "exact case-insensitive recycle");

    // 4. Explicit '>' prefix allows partial matching and listing
    auto mPref = store.MatchQuery(L">sl", false);
    Check(mPref.size() == 1 && mPref[0].entry.trigger == L"sleep" && mPref[0].explicitPrefix, "explicit >sl matches sleep even when no-prefix disabled");

    // 5. Parameterized hotword with argument ("gh lightlaunchpad" and ">g c++")
    auto mGh = store.MatchQuery(L"gh lightlaunchpad", true);
    Check(mGh.size() == 1 && mGh[0].entry.trigger == L"gh" && mGh[0].argument == L"lightlaunchpad", "parameterized gh query");

    // 6. Inline calculator tests
    auto c1 = EvaluateInlineCalculator(L"128*36");
    Check(c1.valid && c1.copyValue == L"4608" && c1.displayValue == L"4,608", "calc 128*36");
    auto c2 = EvaluateInlineCalculator(L"(1920-740)/2");
    Check(c2.valid && c2.copyValue == L"590", "calc parentheses");
    auto c3 = EvaluateInlineCalculator(L"0xff");
    Check(c3.valid && c3.copyValue == L"255", "calc hex 0xff");
    auto c4 = EvaluateInlineCalculator(L"feishu");
    Check(!c4.valid, "plain word does not trigger calc");
    auto c5 = EvaluateInlineCalculator(L"2026");
    Check(!c5.valid, "bare number without operator does not trigger calc");

    // 7. Persistence round-trip + custom hotword
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    const std::wstring testPath = std::wstring(tempDir) + L"llp_hotwords_test.json";
    DeleteFileW(testPath.c_str());

    Check(store.AddOrUpdateEntry({L"deploy", L"部署官网", ActionKind::Command, L"echo ok", true}), "add custom hotword");
    Check(store.SaveToFile(testPath), "save hotwords.json");

    HotwordStore reloaded;
    Check(reloaded.LoadFromFile(testPath), "load hotwords.json");
    auto mDep = reloaded.MatchQuery(L">deploy", false);
    Check(mDep.size() == 1 && mDep[0].entry.title == L"部署官网", "custom hotword persisted");
    DeleteFileW(testPath.c_str());

    std::cout << "Hotwords tests: " << g_passed << "/" << (g_passed + g_failed) << " passed\n";
    return g_failed == 0 ? 0 : 1;
}
