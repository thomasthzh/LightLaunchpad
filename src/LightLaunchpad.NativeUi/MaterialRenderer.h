#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "AdaptiveEnvironment.h"
#include "SceneGeometry.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

struct IconRaster
{
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<std::uint8_t> premultipliedBgra;
};

struct SceneItem
{
    llp::geometry::Rect bounds;
    std::wstring title;
    std::wstring subtitle;
    std::shared_ptr<const IconRaster> icon;
    bool focused = false;
    bool favorite = false;
};

struct SearchVisual
{
    std::wstring displayText;
    std::size_t caret = 0;
    std::size_t selectionStart = 0;
    std::size_t selectionEnd = 0;
    std::size_t compositionStart = 0;
    std::size_t compositionEnd = 0;
    int horizontalOffsetPx = 0;
    bool active = false;
    bool composing = false;
};

struct SceneSnapshot
{
    std::uint64_t generation = 0;
    llp::geometry::SceneMetrics metrics;
    llp::adaptive::Policy policy;
    SearchVisual search;
    std::vector<SceneItem> items;
    bool auxiliaryFocused = false;
};

class LatestSceneSlot
{
public:
    void Submit(std::shared_ptr<const SceneSnapshot> scene);
    std::shared_ptr<const SceneSnapshot> Take();

private:
    std::mutex mutex_;
    std::shared_ptr<const SceneSnapshot> pending_;
};

PixelRect MapWindowCrop(PixelRect outputDesktop, PixelRect windowDesktop, OutputRotation rotation);
float PixelsToDip(float pixels, int dpi);
SearchSample ComputeSearchSample(DipRect bounds, float destinationXDip, float destinationYDip, int dpi, bool reducedMotion);
MaterialTone ResolveMaterialTone(float relativeLuminance, bool darkMode);
LifecycleTransition AdvanceLifecycle(LifecycleState state, LifecycleEvent event, bool rebuildAlreadyUsed);

// Host cache design: key `(reinterpret_cast<std::uintptr_t>(icon), sizePx)`, value `weak_ptr<const IconRaster>`, host clears map with existing icon/DPI resource resets.
std::shared_ptr<const IconRaster> RasterizeIcon(HICON icon, int sizePx);
}
