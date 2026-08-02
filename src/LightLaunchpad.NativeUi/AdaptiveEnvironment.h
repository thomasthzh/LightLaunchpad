#pragma once

#include <cstdint>

namespace llp::adaptive
{
struct Color
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

enum class MonitorSource
{
    Foreground,
    Cursor,
    Primary
};

struct Environment
{
    bool highContrast = false;
    bool transparencyEnabled = true;
    bool animationsEnabled = true;
    bool darkMode = true;
    int textScalePercent = 100;
    Color systemWindow = { 0, 0, 0 };
    Color systemWindowText = { 255, 255, 255 };
    Color systemHighlight = { 0, 120, 215 };
    Color systemHighlightText = { 255, 255, 255 };
};

struct Policy
{
    bool highContrast = false;
    bool solidSurface = false;
    bool reducedMotion = false;
    bool darkMode = true;
    int textScalePercent = 100;
    Color surface;
    Color text;
    Color focus;
    Color focusText;
    Color boundary;
};

MonitorSource ChooseMonitorSource(bool foregroundMonitorAvailable, bool cursorMonitorAvailable);
Policy ResolvePolicy(const Environment& environment);
double ContrastRatio(Color first, Color second);
}
