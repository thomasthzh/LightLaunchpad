#pragma once

namespace llp::motion
{
struct Frame
{
    double opacity = 0.0;
    double offsetYDip = 0.0;
    double overlayOpacity = 0.0;
    double focus = 0.0;
    double press = 0.0;
    double focusScale = 1.0;
    bool active = false;
    bool targetVisible = false;
};

class MotionTimeline
{
public:
    explicit MotionTimeline(bool reducedMotion = false);

    void SetReducedMotion(bool reducedMotion);
    void Reset(bool visible, double nowMs);
    void SetVisible(bool visible, double nowMs);
    void SetFocus(bool focused, double nowMs);
    void Press(double nowMs);

    Frame Sample(double nowMs) const;
    bool IsActive(double nowMs) const;
    bool TargetVisible() const;
    bool IsClosing(double nowMs) const;

    int VisibilityDurationMs(bool opening) const;
    int FocusDurationMs() const;
    int PressDurationMs() const;

private:
    struct Tween
    {
        double from = 0.0;
        double to = 0.0;
        double startMs = 0.0;
        double durationMs = 0.0;
        bool active = false;

        double Sample(double nowMs) const;
        bool IsActive(double nowMs) const;
        void SetInstant(double value, double nowMs);
        void Start(double target, double nowMs, double duration);
    };

    bool reducedMotion_ = false;
    Tween visibility_;
    Tween focus_;
    double pressStartMs_ = 0.0;
    bool pressActive_ = false;
};
}
