#include "AdaptiveEnvironment.h"

#include <iostream>
#include <stdexcept>

namespace
{
using llp::adaptive::ChooseMonitorSource;
using llp::adaptive::ContrastRatio;
using llp::adaptive::Environment;
using llp::adaptive::MonitorSource;
using llp::adaptive::ResolvePolicy;

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

void TestForegroundMonitorWinsWithCursorFallback()
{
    Require(ChooseMonitorSource(true, true) == MonitorSource::Foreground,
        "A valid foreground monitor did not win.");
    Require(ChooseMonitorSource(false, true) == MonitorSource::Cursor,
        "The cursor monitor was not used when the foreground monitor was unavailable.");
    Require(ChooseMonitorSource(false, false) == MonitorSource::Primary,
        "The primary monitor was not the final fallback.");
    Pass("AdaptiveEnvironment_ForegroundMonitorWithCursorFallback");
}

void TestHighContrastAndTransparencyResolveToSolidReadableSurfaces()
{
    Environment highContrast;
    highContrast.highContrast = true;
    highContrast.transparencyEnabled = true;
    highContrast.animationsEnabled = true;
    highContrast.systemWindow = { 0, 0, 0 };
    highContrast.systemWindowText = { 255, 255, 255 };
    highContrast.systemHighlight = { 255, 255, 0 };
    highContrast.systemHighlightText = { 0, 0, 0 };
    const auto highContrastPolicy = ResolvePolicy(highContrast);
    Require(highContrastPolicy.solidSurface, "High contrast left transparency active.");
    Require(highContrastPolicy.highContrast, "High contrast state was lost.");
    Require(ContrastRatio(highContrastPolicy.surface, highContrastPolicy.text) >= 4.5,
        "High-contrast surface/text did not meet 4.5:1.");
    Require(ContrastRatio(highContrastPolicy.focus, highContrastPolicy.focusText) >= 4.5,
        "High-contrast focus colors did not meet 4.5:1.");

    Environment reducedTransparency;
    reducedTransparency.transparencyEnabled = false;
    reducedTransparency.animationsEnabled = true;
    reducedTransparency.darkMode = false;
    const auto solidPolicy = ResolvePolicy(reducedTransparency);
    Require(solidPolicy.solidSurface, "Reduced transparency still selected a translucent surface.");
    Require(ContrastRatio(solidPolicy.surface, solidPolicy.text) >= 4.5,
        "Reduced-transparency surface/text did not meet 4.5:1.");
    Pass("AdaptiveEnvironment_HighContrastAndTransparencyAreReadable");
}

void TestMotionAndTextScaleFollowSystemPreferences()
{
    Environment environment;
    environment.animationsEnabled = false;
    environment.textScalePercent = 175;
    const auto policy = ResolvePolicy(environment);
    Require(policy.reducedMotion, "Disabled client animation did not enable reduced motion.");
    Require(policy.textScalePercent == 175, "Increased text scale was not preserved.");

    environment.textScalePercent = 25;
    Require(ResolvePolicy(environment).textScalePercent == 100,
        "Invalid low text scale was not normalized.");
    environment.textScalePercent = 500;
    Require(ResolvePolicy(environment).textScalePercent == 225,
        "Invalid high text scale was not normalized.");
    Pass("AdaptiveEnvironment_MotionAndTextScaleFollowSystem");
}
}

int main()
{
    try
    {
        TestForegroundMonitorWinsWithCursorFallback();
        TestHighContrastAndTransparencyResolveToSolidReadableSurfaces();
        TestMotionAndTextScaleFollowSystemPreferences();
        std::cout << passed << "/3 tests passed\n";
        return passed == 3 ? 0 : 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL " << exception.what() << "\n";
        return 1;
    }
}
