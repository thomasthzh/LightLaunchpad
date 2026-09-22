#include "MotionTimeline.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using llp::motion::Frame;
using llp::motion::MotionTimeline;

int passed = 0;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void Pass(const char* name)
{
    ++passed;
    std::cout << "PASS " << name << "\n";
}

void TestVisibilityUsesApprovedDurationsAndEasing()
{
    MotionTimeline timeline;
    timeline.Reset(false, 0.0);
    timeline.SetVisible(true, 0.0);
    Require(timeline.VisibilityDurationMs(true) >= 140 && timeline.VisibilityDurationMs(true) <= 180,
        "Open duration escaped the approved 140-180 ms window.");
    Require(timeline.VisibilityDurationMs(false) >= 120 && timeline.VisibilityDurationMs(false) <= 160,
        "Close duration escaped the approved 120-160 ms window.");
    const Frame first = timeline.Sample(0.0);
    const Frame middle = timeline.Sample(85.0);
    const Frame last = timeline.Sample(180.0);
    Require(first.opacity == 0.0 && first.offsetYDip > 0.0,
        "Open animation did not start transparent and slightly elevated.");
    Require(middle.opacity > first.opacity && middle.opacity < 1.0,
        "Open easing did not produce a continuous intermediate opacity.");
    Require(last.opacity == 1.0 && last.offsetYDip == 0.0 && !last.active,
        "Open animation did not settle at an idle, aligned frame.");
    Pass("MotionTimeline_VisibilityUsesApprovedDurationsAndEasing");
}

void TestReverseKeepsCurrentProgressWithoutStaleQueue()
{
    MotionTimeline timeline;
    timeline.Reset(false, 0.0);
    timeline.SetVisible(true, 0.0);
    const double beforeReverse = timeline.Sample(70.0).opacity;
    timeline.SetVisible(false, 70.0);
    const Frame reversed = timeline.Sample(70.0);
    Require(std::abs(reversed.opacity - beforeReverse) < 0.0001,
        "Reversing visibility jumped instead of continuing from the current frame.");
    Require(!reversed.targetVisible && reversed.active,
        "Reverse close did not replace the stale open target immediately.");
    timeline.SetVisible(true, 110.0);
    const Frame reopened = timeline.Sample(110.0);
    Require(std::abs(reopened.opacity - timeline.Sample(110.0).opacity) < 0.0001,
        "Reopening did not preserve the latest in-flight progress.");
    Require(reopened.targetVisible && reopened.active,
        "Repeated toggle lost the latest visibility target.");
    Pass("MotionTimeline_ReverseKeepsCurrentProgressWithoutStaleQueue");
}

void TestFocusAndPressAreIndependentAndBounded()
{
    MotionTimeline timeline;
    timeline.Reset(true, 0.0);
    timeline.SetFocus(false, 0.0);
    timeline.Press(10.0);
    const Frame frame = timeline.Sample(30.0);
    Require(timeline.FocusDurationMs() >= 80 && timeline.FocusDurationMs() <= 120,
        "Focus duration escaped the approved 80-120 ms window.");
    Require(timeline.PressDurationMs() >= 60 && timeline.PressDurationMs() <= 90,
        "Press duration escaped the approved 60-90 ms window.");
    Require(frame.focus < 1.0 && frame.press > 0.0 && frame.press < 1.0,
        "Focus and press channels did not animate independently.");
    Require(frame.focusScale >= 1.0 && frame.focusScale <= 1.015,
        "Focus scale exceeded the 1.015 product cap.");
    Require(timeline.Sample(100.0).press == 0.0,
        "Press pulse did not settle without a lingering timer.");
    Pass("MotionTimeline_FocusAndPressAreIndependentAndBounded");
}

void TestReducedMotionRemovesSpatialMotionAndShortensFades()
{
    MotionTimeline timeline(true);
    timeline.Reset(false, 0.0);
    timeline.SetVisible(true, 0.0);
    Require(timeline.VisibilityDurationMs(true) <= 100 && timeline.VisibilityDurationMs(false) <= 100,
        "Reduced motion did not shorten visibility fades to at most 100 ms.");
    const Frame frame = timeline.Sample(30.0);
    Require(frame.offsetYDip == 0.0 && frame.focusScale == 1.0,
        "Reduced motion retained spatial or scale movement.");
    Require(frame.opacity > 0.0 && frame.opacity < 1.0,
        "Reduced motion removed the useful opacity fade entirely.");
    Pass("MotionTimeline_ReducedMotionRemovesSpatialMotionAndShortensFades");
}
}

int main()
{
    try
    {
        TestVisibilityUsesApprovedDurationsAndEasing();
        TestReverseKeepsCurrentProgressWithoutStaleQueue();
        TestFocusAndPressAreIndependentAndBounded();
        TestReducedMotionRemovesSpatialMotionAndShortensFades();
        std::cout << passed << "/4 tests passed\n";
        return passed == 4 ? 0 : 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL " << exception.what() << "\n";
        return 1;
    }
}
