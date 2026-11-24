#include "EverythingBridge.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>

namespace
{
int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}
}

int main()
{
    using namespace llp::everything;

    {
        const auto p = ParseSpotlightQuery(L"   ");
        Expect(p.mode == QueryMode::None, "whitespace query should be None");
    }
    {
        const auto p = ParseSpotlightQuery(L"feishu");
        Expect(p.mode == QueryMode::Hybrid, "normal query should be Hybrid");
        Expect(p.appQuery == L"feishu" && p.everythingQuery == L"feishu", "normal query terms should match");
    }
    {
        const auto p = ParseSpotlightQuery(L"f report.pdf");
        Expect(p.mode == QueryMode::FilesOnly, "'f ' prefix should be FilesOnly");
        Expect(p.everythingQuery == L"report.pdf" && p.appQuery.empty(), "'f ' prefix should strip prefix");
    }
    {
        const auto p = ParseSpotlightQuery(L"F   invoice 2026 ");
        Expect(p.mode == QueryMode::FilesOnly, "'F ' prefix should be FilesOnly");
        Expect(p.everythingQuery == L"invoice 2026", "'F ' prefix should trim whitespace");
    }
    {
        const auto p = ParseSpotlightQuery(L"/readme.md");
        Expect(p.mode == QueryMode::FilesOnly, "'/' prefix should be FilesOnly");
        Expect(p.everythingQuery == L"readme.md", "'/' prefix should extract query");
    }
    {
        const auto p = ParseSpotlightQuery(L"f   ");
        Expect(p.mode == QueryMode::None, "empty 'f ' prefix should be None");
    }
    {
        const auto p = ParseSpotlightQuery(L"/");
        Expect(p.mode == QueryMode::None, "bare '/' prefix should be None");
    }

    // Async bridge graceful execution & shutdown test.
    {
        EverythingBridge bridge;
        std::mutex mtx;
        std::condition_variable cv;
        bool done = false;
        QueryResponse captured;

        bridge.SetCallback([&](QueryResponse resp) {
            {
                std::lock_guard<std::mutex> lock(mtx);
                captured = std::move(resp);
                done = true;
            }
            cv.notify_one();
        });

        bridge.RequestQuery(42, L"f nonexistent_probe_xyz_99999", 4);
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait_for(lock, std::chrono::milliseconds(800), [&] { return done; });
        }
        Expect(done, "bridge should complete callback promptly");
        Expect(captured.generation == 42, "response generation should match request");
        bridge.Shutdown();
    }

    if (g_failures == 0)
    {
        std::printf("EverythingBridge tests: 8/8 passed\n");
        return 0;
    }
    std::fprintf(stderr, "EverythingBridge tests: %d failure(s)\n", g_failures);
    return 1;
}
