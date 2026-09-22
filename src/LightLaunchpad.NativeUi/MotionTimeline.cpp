#include "MotionTimeline.h"

#include <algorithm>
#include <cmath>

namespace llp::motion
{
namespace
{
constexpr double VisibilityOffsetDip = 10.0;
constexpr double MaxFocusScale = 1.015;

double Clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double EaseOutCubic(double value)
{
    const double progress = Clamp01(value);
    const double inverse = 1.0 - progress;
    return 1.0 - inverse * inverse * inverse;
}

double EaseInOutSine(double value)
{
    constexpr double Pi = 3.14159265358979323846;
    return (1.0 - std::cos(Pi * Clamp01(value))) * 0.5;
}
}

double MotionTimeline::Tween::Sample(double nowMs) const
{
    if (!active || durationMs <= 0.0) return to;
    const double progress = Clamp01((nowMs - startMs) / durationMs);
    if (progress >= 1.0) return to;
    return from + (to - from) * EaseOutCubic(progress);
}

bool MotionTimeline::Tween::IsActive(double nowMs) const
{
    return active && nowMs < startMs + durationMs;
}

void MotionTimeline::Tween::SetInstant(double value, double nowMs)
{
    from = value;
    to = value;
    startMs = nowMs;
    durationMs = 0.0;
    active = false;
}

void MotionTimeline::Tween::Start(double target, double nowMs, double duration)
{
    const double current = Sample(nowMs);
    from = current;
    to = Clamp01(target);
    startMs = nowMs;
    durationMs = std::max(0.0, duration);
    active = durationMs > 0.0 && std::abs(from - to) > 0.000001;
    if (!active) from = to;
}

MotionTimeline::MotionTimeline(bool reducedMotion)
    : reducedMotion_(reducedMotion)
{
    Reset(false, 0.0);
}

void MotionTimeline::SetReducedMotion(bool reducedMotion)
{
    reducedMotion_ = reducedMotion;
}

void MotionTimeline::Reset(bool visible, double nowMs)
{
    visibility_.SetInstant(visible ? 1.0 : 0.0, nowMs);
    focus_.SetInstant(visible ? 1.0 : 0.0, nowMs);
    pressStartMs_ = nowMs;
    pressActive_ = false;
}

void MotionTimeline::SetVisible(bool visible, double nowMs)
{
    const double duration = VisibilityDurationMs(visible) * 1.0;
    visibility_.Start(visible ? 1.0 : 0.0, nowMs, duration);
}

void MotionTimeline::SetFocus(bool focused, double nowMs)
{
    focus_.Start(focused ? 1.0 : 0.0, nowMs, FocusDurationMs() * 1.0);
}

void MotionTimeline::Press(double nowMs)
{
    pressStartMs_ = nowMs;
    pressActive_ = true;
}

Frame MotionTimeline::Sample(double nowMs) const
{
    Frame frame;
    frame.opacity = visibility_.Sample(nowMs);
    frame.focus = focus_.Sample(nowMs);
    frame.targetVisible = visibility_.to >= 0.5;
    const double pressDuration = PressDurationMs();
    if (pressActive_ && nowMs < pressStartMs_ + pressDuration)
    {
        const double progress = Clamp01((nowMs - pressStartMs_) / pressDuration);
        frame.press = progress < 0.5
            ? EaseOutCubic(progress * 2.0)
            : 1.0 - EaseInOutSine((progress - 0.5) * 2.0);
    }
    else
    {
        frame.press = 0.0;
    }
    frame.offsetYDip = reducedMotion_
        ? 0.0
        : VisibilityOffsetDip * (1.0 - EaseOutCubic(frame.opacity));
    frame.focusScale = reducedMotion_
        ? 1.0
        : 1.0 + (MaxFocusScale - 1.0) * frame.focus;
    frame.overlayOpacity = std::clamp(
        0.76 + frame.focus * 0.24 + frame.press * 0.06,
        0.0,
        1.0);
    frame.active = visibility_.IsActive(nowMs)
        || focus_.IsActive(nowMs)
        || (pressActive_ && nowMs < pressStartMs_ + pressDuration);
    return frame;
}

bool MotionTimeline::IsActive(double nowMs) const
{
    return Sample(nowMs).active;
}

bool MotionTimeline::TargetVisible() const
{
    return visibility_.to >= 0.5;
}

bool MotionTimeline::IsClosing(double nowMs) const
{
    return !TargetVisible() && IsActive(nowMs);
}

int MotionTimeline::VisibilityDurationMs(bool opening) const
{
    if (reducedMotion_) return 90;
    return opening ? 170 : 140;
}

int MotionTimeline::FocusDurationMs() const
{
    return reducedMotion_ ? 80 : 100;
}

int MotionTimeline::PressDurationMs() const
{
    return reducedMotion_ ? 60 : 72;
}
}
