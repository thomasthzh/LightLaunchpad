#include "MaterialRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace llp::material
{
namespace
{
bool SameColor(llp::adaptive::Color first, llp::adaptive::Color second)
{
    return first.red == second.red && first.green == second.green && first.blue == second.blue;
}

template <std::size_t candidateCount>
llp::adaptive::Color ChooseContrastColor(
    const std::array<llp::adaptive::Color, candidateCount>& candidates,
    llp::adaptive::Color surface,
    double minimumRatio)
{
    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        bool alreadyChecked = false;
        for (std::size_t prior = 0; prior < index; ++prior)
        {
            if (SameColor(candidates[index], candidates[prior]))
            {
                alreadyChecked = true;
                break;
            }
        }
        if (!alreadyChecked && llp::adaptive::ContrastRatio(candidates[index], surface) >= minimumRatio)
        {
            return candidates[index];
        }
    }

    const llp::adaptive::Color black = { 0, 0, 0 };
    const llp::adaptive::Color white = { 255, 255, 255 };
    const double blackRatio = llp::adaptive::ContrastRatio(black, surface);
    const double whiteRatio = llp::adaptive::ContrastRatio(white, surface);
    const auto fallback = blackRatio >= whiteRatio ? black : white;
    const double fallbackRatio = std::max(blackRatio, whiteRatio);
    if (fallbackRatio < minimumRatio)
    {
        throw std::logic_error("No material color satisfies the required contrast ratio.");
    }
    return fallback;
}

class WindowDc final
{
public:
    WindowDc() : handle_(GetDC(nullptr)) {}
    ~WindowDc()
    {
        if (handle_) ReleaseDC(nullptr, handle_);
    }

    HDC Get() const { return handle_; }

private:
    HDC handle_ = nullptr;
};

class MemoryDc final
{
public:
    explicit MemoryDc(HDC compatibleWith) : handle_(CreateCompatibleDC(compatibleWith)) {}
    ~MemoryDc()
    {
        if (handle_) DeleteDC(handle_);
    }

    HDC Get() const { return handle_; }

private:
    HDC handle_ = nullptr;
};

class OwnedBitmap final
{
public:
    explicit OwnedBitmap(HBITMAP handle) : handle_(handle) {}
    ~OwnedBitmap()
    {
        if (handle_) DeleteObject(handle_);
    }

    HBITMAP Get() const { return handle_; }

private:
    HBITMAP handle_ = nullptr;
};

class BitmapSelection final
{
public:
    BitmapSelection(HDC dc, HBITMAP bitmap)
        : dc_(dc), previous_(SelectObject(dc, bitmap)) {}
    ~BitmapSelection()
    {
        if (previous_) SelectObject(dc_, previous_);
    }

    bool Valid() const { return previous_ != nullptr; }

private:
    HDC dc_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};

std::uint8_t Premultiply(std::uint8_t channel, std::uint8_t alpha)
{
    return static_cast<std::uint8_t>((static_cast<unsigned int>(channel) * alpha + 127u) / 255u);
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
    if (!std::isfinite(pixels)) return 0.0f;
    const int effectiveDpi = dpi > 0 ? dpi : 96;
    const double converted = static_cast<double>(pixels) * 96.0 / static_cast<double>(effectiveDpi);
    const double largestFloat = static_cast<double>(std::numeric_limits<float>::max());
    return static_cast<float>(std::clamp(converted, -largestFloat, largestFloat));
}

SearchSample ComputeSearchSample(
    DipRect bounds,
    float destinationXDip,
    float destinationYDip,
    int dpi,
    bool reducedMotion)
{
    if (!std::isfinite(bounds.left) || !std::isfinite(bounds.top)
        || !std::isfinite(bounds.right) || !std::isfinite(bounds.bottom)
        || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
    {
        return {};
    }

    const double left = bounds.left;
    const double right = bounds.right;
    const double top = bounds.top;
    const double bottom = bounds.bottom;
    const double centerX = (left + right) * 0.5;
    const double centerY = (top + bottom) * 0.5;
    const double destinationX = std::isfinite(destinationXDip)
        ? std::clamp(static_cast<double>(destinationXDip), left, right)
        : centerX;
    const double destinationY = std::isfinite(destinationYDip)
        ? std::clamp(static_cast<double>(destinationYDip), top, bottom)
        : centerY;
    if (reducedMotion)
    {
        return { static_cast<float>(destinationX), static_cast<float>(destinationY), 0.0f };
    }

    const double vectorX = centerX - destinationX;
    const double vectorY = centerY - destinationY;
    const double vectorLength = std::hypot(vectorX, vectorY);
    if (vectorLength == 0.0) return { static_cast<float>(destinationX), static_cast<float>(destinationY), 0.0f };

    const double edgeDistance = std::min({
        destinationX - left,
        right - destinationX,
        destinationY - top,
        bottom - destinationY
    });
    const double edgeBandDip = 12.0 + PixelsToDip(1.0f, dpi);
    const double edgeAmount = std::clamp(1.0 - edgeDistance / edgeBandDip, 0.0, 1.0);
    const double smoothAmount = edgeAmount * edgeAmount * (3.0 - 2.0 * edgeAmount);
    const double requestedDisplacement = 1.5 * smoothAmount;
    const double sourceX = std::clamp(destinationX + vectorX / vectorLength * requestedDisplacement, left, right);
    const double sourceY = std::clamp(destinationY + vectorY / vectorLength * requestedDisplacement, top, bottom);
    const double appliedDisplacement = std::hypot(sourceX - destinationX, sourceY - destinationY);
    return {
        static_cast<float>(sourceX),
        static_cast<float>(sourceY),
        static_cast<float>(std::min(requestedDisplacement, appliedDisplacement))
    };
}

MaterialTone ResolveMaterialTone(float relativeLuminance, bool darkMode)
{
    llp::adaptive::Environment environment;
    environment.darkMode = darkMode;
    const auto policy = llp::adaptive::ResolvePolicy(environment);
    const float luminance = std::isfinite(relativeLuminance)
        ? std::clamp(relativeLuminance, 0.0f, 1.0f)
        : 0.5f;

    MaterialTone tone;
    tone.tint = darkMode
        ? llp::adaptive::Color{ 224, 239, 252 }
        : llp::adaptive::Color{ 232, 244, 255 };
    tone.guardedSurface = policy.surface;
    tone.text = ChooseContrastColor(
        std::array<llp::adaptive::Color, 4>{ policy.text, policy.focusText, policy.boundary, policy.focus },
        tone.guardedSurface,
        4.5);
    tone.focus = ChooseContrastColor(
        std::array<llp::adaptive::Color, 4>{ policy.focus, policy.boundary, policy.focusText, policy.text },
        tone.guardedSurface,
        3.0);
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

void LatestSceneSlot::Submit(std::shared_ptr<const SceneSnapshot> scene)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = std::move(scene);
}

std::shared_ptr<const SceneSnapshot> LatestSceneSlot::Take()
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto scene = std::move(pending_);
    pending_.reset();
    return scene;
}

std::shared_ptr<const IconRaster> RasterizeIcon(HICON icon, int sizePx)
{
    constexpr int maximumIconSizePx = 1024;
    if (!icon || sizePx <= 0 || sizePx > maximumIconSizePx) return nullptr;

    WindowDc screenDc;
    if (!screenDc.Get()) return nullptr;
    MemoryDc iconDc(screenDc.Get());
    if (!iconDc.Get()) return nullptr;

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = sizePx;
    bitmapInfo.bmiHeader.biHeight = -sizePx;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* rawBits = nullptr;
    OwnedBitmap bitmap(CreateDIBSection(
        screenDc.Get(), &bitmapInfo, DIB_RGB_COLORS, &rawBits, nullptr, 0));
    if (!bitmap.Get() || !rawBits) return nullptr;
    BitmapSelection selectedBitmap(iconDc.Get(), bitmap.Get());
    if (!selectedBitmap.Valid()) return nullptr;

    const int stride = sizePx * 4;
    const auto byteCount = static_cast<std::size_t>(stride) * static_cast<std::size_t>(sizePx);
    std::memset(rawBits, 0, byteCount);
    if (!DrawIconEx(iconDc.Get(), 0, 0, icon, sizePx, sizePx, 0, nullptr, DI_NORMAL)) return nullptr;

    auto raster = std::make_shared<IconRaster>();
    raster->width = sizePx;
    raster->height = sizePx;
    raster->stride = stride;
    const auto* sourceBytes = static_cast<const std::uint8_t*>(rawBits);
    raster->premultipliedBgra.assign(sourceBytes, sourceBytes + byteCount);

    for (std::size_t offset = 0; offset + 3 < byteCount; offset += 4)
    {
        auto* pixel = raster->premultipliedBgra.data() + offset;
        const auto alpha = pixel[3];
        if (alpha == 0)
        {
            if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0) pixel[3] = 255;
        }
        else if (pixel[0] > alpha || pixel[1] > alpha || pixel[2] > alpha)
        {
            pixel[0] = Premultiply(pixel[0], alpha);
            pixel[1] = Premultiply(pixel[1], alpha);
            pixel[2] = Premultiply(pixel[2], alpha);
        }
    }
    return raster;
}
}
