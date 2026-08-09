#pragma once

#include "AdaptiveEnvironment.h"

namespace llp::material
{
struct PixelRect
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    bool operator==(const PixelRect& other) const;
};

struct DipRect
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

enum class OutputRotation { Identity, Rotate90, Rotate180, Rotate270 };
enum class RenderPath { HighFidelity, Fallback, Solid };
enum class FailureReason
{
    None,
    UnsupportedOs,
    DisplayAffinityUnavailable,
    AdapterUnavailable,
    DuplicationUnavailable,
    FirstFrameTimeout,
    CompositionUnavailable,
    DeviceRemoved,
    AccessLost,
    ForcedForTest
};
enum class LifecycleState { Hidden, Starting, Live, Rebuilding, Fallback, FrozenEvidence, Stopping };
enum class LifecycleEvent
{
    ShowRequested,
    FirstFrameReady,
    FirstFrameTimedOut,
    InitializationFailed,
    AccessLost,
    DeviceRemoved,
    RebuildSucceeded,
    HideRequested,
    Hidden,
    FreezeEvidence
};

struct SearchSample
{
    float sourceXDip = 0.0f;
    float sourceYDip = 0.0f;
    float displacementDip = 0.0f;
};

struct MaterialTone
{
    llp::adaptive::Color tint;
    llp::adaptive::Color guardedSurface;
    llp::adaptive::Color text;
    llp::adaptive::Color focus;
    float tintOpacity = 0.0f;
    float blurRadiusDip = 0.0f;
};

struct LifecycleTransition
{
    LifecycleState next = LifecycleState::Hidden;
    bool consumeRebuild = false;
};

PixelRect MapWindowCrop(PixelRect outputDesktop, PixelRect windowDesktop, OutputRotation rotation);
float PixelsToDip(float pixels, int dpi);
SearchSample ComputeSearchSample(DipRect bounds, float destinationXDip, float destinationYDip, int dpi, bool reducedMotion);
MaterialTone ResolveMaterialTone(float relativeLuminance, bool darkMode);
LifecycleTransition AdvanceLifecycle(LifecycleState state, LifecycleEvent event, bool rebuildAlreadyUsed);
}
