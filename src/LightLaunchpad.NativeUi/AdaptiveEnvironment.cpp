#include "AdaptiveEnvironment.h"

#include <algorithm>
#include <cmath>

namespace llp::adaptive
{
namespace
{
double LinearChannel(std::uint8_t value)
{
    const double channel = static_cast<double>(value) / 255.0;
    return channel <= 0.04045
        ? channel / 12.92
        : std::pow((channel + 0.055) / 1.055, 2.4);
}

double Luminance(Color color)
{
    return 0.2126 * LinearChannel(color.red)
        + 0.7152 * LinearChannel(color.green)
        + 0.0722 * LinearChannel(color.blue);
}
}

MonitorSource ChooseMonitorSource(bool foregroundMonitorAvailable, bool cursorMonitorAvailable)
{
    if (foregroundMonitorAvailable) return MonitorSource::Foreground;
    if (cursorMonitorAvailable) return MonitorSource::Cursor;
    return MonitorSource::Primary;
}

Policy ResolvePolicy(const Environment& environment)
{
    Policy policy;
    policy.highContrast = environment.highContrast;
    policy.solidSurface = environment.highContrast || !environment.transparencyEnabled;
    policy.reducedMotion = !environment.animationsEnabled;
    policy.darkMode = environment.darkMode;
    policy.textScalePercent = std::clamp(environment.textScalePercent, 100, 225);

    if (environment.highContrast)
    {
        policy.surface = environment.systemWindow;
        policy.text = environment.systemWindowText;
        policy.focus = environment.systemHighlight;
        policy.focusText = environment.systemHighlightText;
        policy.boundary = environment.systemWindowText;
        return policy;
    }

    if (environment.darkMode)
    {
        policy.surface = { 28, 34, 43 };
        policy.text = { 246, 249, 252 };
        policy.focus = { 198, 226, 248 };
        policy.focusText = { 19, 39, 56 };
        policy.boundary = { 124, 164, 196 };
    }
    else
    {
        policy.surface = { 242, 247, 251 };
        policy.text = { 24, 34, 45 };
        policy.focus = { 207, 232, 250 };
        policy.focusText = { 16, 41, 60 };
        policy.boundary = { 77, 116, 146 };
    }
    return policy;
}

double ContrastRatio(Color first, Color second)
{
    const double firstLuminance = Luminance(first);
    const double secondLuminance = Luminance(second);
    const double lighter = std::max(firstLuminance, secondLuminance);
    const double darker = std::min(firstLuminance, secondLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}
}
