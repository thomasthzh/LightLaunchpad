#include "MaterialRenderer.h"

#include <algorithm>
#include <cmath>

namespace llp::material
{
namespace
{
llp::adaptive::Color ChooseContrastColor(
    llp::adaptive::Color preferred,
    llp::adaptive::Color alternate,
    llp::adaptive::Color lastResort,
    llp::adaptive::Color surface,
    double minimumRatio)
{
    if (llp::adaptive::ContrastRatio(preferred, surface) >= minimumRatio) return preferred;
    if (llp::adaptive::ContrastRatio(alternate, surface) >= minimumRatio) return alternate;
    return lastResort;
}
}

bool PixelRect::operator==(const PixelRect& other) const
{
    return left == other.left && top == other.top
        && right == other.right && bottom == other.bottom;
}

PixelRect MapWindowCrop(PixelRect outputDesktop, PixelRect windowDesktop, OutputRotation rotation)
{
    const int outputWidth = std::max(0, outputDesktop.right - outputDesktop.left);
    const int outputHeight = std::max(0, outputDesktop.bottom - outputDesktop.top);
    const int left = std::clamp(windowDesktop.left - outputDesktop.left, 0, outputWidth);
    const int top = std::clamp(windowDesktop.top - outputDesktop.top, 0, outputHeight);
    const int right = std::clamp(windowDesktop.right - outputDesktop.left, 0, outputWidth);
    const int bottom = std::clamp(windowDesktop.bottom - outputDesktop.top, 0, outputHeight);
    if (right <= left || bottom <= top) return {};

    switch (rotation)
    {
    case OutputRotation::Rotate90:
        return { top, outputWidth - right, bottom, outputWidth - left };
    case OutputRotation::Rotate180:
        return { outputWidth - right, outputHeight - bottom, outputWidth - left, outputHeight - top };
    case OutputRotation::Rotate270:
        return { outputHeight - bottom, left, outputHeight - top, right };
    case OutputRotation::Identity:
    default:
        return { left, top, right, bottom };
    }
}

float PixelsToDip(float pixels, int dpi)
{
    const int effectiveDpi = dpi > 0 ? dpi : 96;
    return pixels * 96.0f / static_cast<float>(effectiveDpi);
}

SearchSample ComputeSearchSample(
    DipRect bounds,
    float destinationXDip,
    float destinationYDip,
    int dpi,
    bool reducedMotion)
{
    const float left = std::min(bounds.left, bounds.right);
    const float right = std::max(bounds.left, bounds.right);
    const float top = std::min(bounds.top, bounds.bottom);
    const float bottom = std::max(bounds.top, bounds.bottom);
    const float destinationX = std::clamp(destinationXDip, left, right);
    const float destinationY = std::clamp(destinationYDip, top, bottom);
    if (reducedMotion || left == right || top == bottom)
    {
        return { destinationX, destinationY, 0.0f };
    }

    const float centerX = (left + right) * 0.5f;
    const float centerY = (top + bottom) * 0.5f;
    const float vectorX = centerX - destinationX;
    const float vectorY = centerY - destinationY;
    const float vectorLength = std::sqrt(vectorX * vectorX + vectorY * vectorY);
    if (vectorLength == 0.0f) return { destinationX, destinationY, 0.0f };

    const float edgeDistance = std::min({
        destinationX - left,
        right - destinationX,
        destinationY - top,
        bottom - destinationY
    });
    const float onePixelDip = PixelsToDip(1.0f, dpi);
    const float edgeBandDip = 12.0f + onePixelDip;
    const float edgeAmount = std::clamp(1.0f - edgeDistance / edgeBandDip, 0.0f, 1.0f);
    const float smoothAmount = edgeAmount * edgeAmount * (3.0f - 2.0f * edgeAmount);
    const float requestedDisplacement = 1.5f * smoothAmount;
    const float sourceX = std::clamp(destinationX + vectorX / vectorLength * requestedDisplacement, left, right);
    const float sourceY = std::clamp(destinationY + vectorY / vectorLength * requestedDisplacement, top, bottom);
    const float appliedX = sourceX - destinationX;
    const float appliedY = sourceY - destinationY;
    const float appliedDisplacement = std::sqrt(appliedX * appliedX + appliedY * appliedY);
    return { sourceX, sourceY, std::min(requestedDisplacement, appliedDisplacement) };
}

MaterialTone ResolveMaterialTone(float relativeLuminance, bool darkMode)
{
    llp::adaptive::Environment environment;
    environment.darkMode = darkMode;
    const auto policy = llp::adaptive::ResolvePolicy(environment);
    const float luminance = std::clamp(relativeLuminance, 0.0f, 1.0f);

    MaterialTone tone;
    tone.tint = darkMode
        ? llp::adaptive::Color{ 224, 239, 252 }
        : llp::adaptive::Color{ 232, 244, 255 };
    tone.guardedSurface = policy.surface;
    tone.text = ChooseContrastColor(policy.text, policy.focusText, policy.text, tone.guardedSurface, 4.5);
    tone.focus = ChooseContrastColor(policy.focus, policy.boundary, tone.text, tone.guardedSurface, 3.0);
    tone.tintOpacity = 0.10f + (1.0f - luminance) * 0.05f + (darkMode ? 0.01f : 0.0f);
    tone.blurRadiusDip = 18.0f + (1.0f - std::abs(luminance - 0.5f) * 2.0f) * 4.0f;
    return tone;
}

LifecycleTransition AdvanceLifecycle(LifecycleState state, LifecycleEvent event, bool rebuildAlreadyUsed)
{
    if (event == LifecycleEvent::Hidden) return { LifecycleState::Hidden, false };
    if (event == LifecycleEvent::HideRequested)
    {
        return { state == LifecycleState::Hidden ? LifecycleState::Hidden : LifecycleState::Stopping, false };
    }
    if (event == LifecycleEvent::ShowRequested)
    {
        return { state == LifecycleState::Hidden || state == LifecycleState::Fallback
                ? LifecycleState::Starting
                : state,
            false };
    }
    if (event == LifecycleEvent::FreezeEvidence
        && state != LifecycleState::Hidden && state != LifecycleState::Stopping)
    {
        return { LifecycleState::FrozenEvidence, false };
    }
    if (event == LifecycleEvent::FirstFrameReady && state == LifecycleState::Starting)
    {
        return { LifecycleState::Live, false };
    }
    if (event == LifecycleEvent::RebuildSucceeded && state == LifecycleState::Rebuilding)
    {
        return { LifecycleState::Live, false };
    }
    if ((event == LifecycleEvent::FirstFrameTimedOut || event == LifecycleEvent::InitializationFailed)
        && (state == LifecycleState::Starting || state == LifecycleState::Rebuilding))
    {
        return { LifecycleState::Fallback, false };
    }
    if ((event == LifecycleEvent::AccessLost || event == LifecycleEvent::DeviceRemoved)
        && (state == LifecycleState::Starting || state == LifecycleState::Live || state == LifecycleState::Rebuilding))
    {
        return rebuildAlreadyUsed
            ? LifecycleTransition{ LifecycleState::Fallback, false }
            : LifecycleTransition{ LifecycleState::Rebuilding, true };
    }
    return { state, false };
}
}
