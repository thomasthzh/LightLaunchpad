#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "MaterialRenderer.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

struct MaterialLifecycleTestReport
{
    int selectedAdapter = -1;
    int selectedOutput = -1;
    int duplicateCalls = 0;
    int acquireCalls = 0;
    unsigned int maximumWaitMilliseconds = 0;
    int releaseCalls = 0;
    int redraws = 0;
    int rebuilds = 0;
    std::uint64_t acquiredFrames = 0;
    std::uint64_t lastFrameIdentity = 0;
    int finalState = 0;
    int finalPath = 0;
    int finalFailure = 0;
    long long firstFrameMicroseconds = 0;
    int releaseBeforeReacquire = 0;
    int releaseBeforeRebuild = 0;
    int releaseBeforeHide = 0;
    int releaseBeforeFreeze = 0;
    int hiddenTimedWaits = 0;
    int hiddenAcquireCalls = 0;
    int affinityExcluded = 0;
    int affinityCleared = 0;
    int partialCropRejected = 0;
};

extern "C" int LLP_MaterialRunFakeLifecycleScenario(
    int scenario,
    MaterialLifecycleTestReport* report);

namespace
{
using llp::material::AdvanceLifecycle;
using llp::material::ComputeSearchSample;
using llp::material::MapWindowCrop;
using llp::material::PixelsToDip;
using llp::material::RasterizeIcon;
using llp::material::RendererDependenciesAvailable;
using llp::material::ResolveMaterialTone;

int passed = 0;

struct ExpectedModernPixel
{
    int x;
    int y;
    std::array<std::uint8_t, 4> bgra;
};

constexpr std::array<ExpectedModernPixel, 2> expectedModernPixels = { {
    { 0, 0, { 64, 32, 16, 128 } },
    { 1, 0, { 24, 144, 80, 192 } }
} };

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void RequireNear(float actual, float expected, float tolerance, const char* message)
{
    Require(std::abs(actual - expected) <= tolerance, message);
}

void RequireByteNear(std::uint8_t actual, std::uint8_t expected, int tolerance, const char* message)
{
    if (std::abs(static_cast<int>(actual) - static_cast<int>(expected)) > tolerance)
    {
        throw std::runtime_error(std::string(message) + " actual=" + std::to_string(actual)
            + " expected=" + std::to_string(expected));
    }
}

std::uint8_t ExpectedDrawIconChannel(std::uint8_t premultipliedChannel, std::uint8_t alpha)
{
    return static_cast<std::uint8_t>(
        (static_cast<unsigned int>(premultipliedChannel) * alpha + 127u) / 255u);
}

void Pass(const char* name)
{
    ++passed;
    std::cout << "PASS " << name << "\n";
}

HICON CreateOwnedTestIcon()
{
    constexpr int size = 16;
    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = size;
    bitmapInfo.bmiHeader.biHeight = -size;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* rawBits = nullptr;
    HBITMAP colorBitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, &rawBits, nullptr, 0);
    Require(colorBitmap != nullptr && rawBits != nullptr, "Could not create the owned test icon color bitmap.");
    auto* pixels = static_cast<std::uint8_t*>(rawBits);
    for (int index = 0; index < size * size; ++index)
    {
        pixels[index * 4 + 0] = 24;
        pixels[index * 4 + 1] = 48;
        pixels[index * 4 + 2] = 12;
        pixels[index * 4 + 3] = 96;
    }
    for (const auto& expected : expectedModernPixels)
    {
        const auto offset = (expected.y * size + expected.x) * 4;
        for (std::size_t channel = 0; channel < expected.bgra.size(); ++channel)
        {
            pixels[offset + channel] = expected.bgra[channel];
        }
    }

    std::vector<std::uint8_t> maskBits(static_cast<std::size_t>(size * size) / 8, 0);
    HBITMAP maskBitmap = CreateBitmap(size, size, 1, 1, maskBits.data());
    if (!maskBitmap)
    {
        DeleteObject(colorBitmap);
        throw std::runtime_error("Could not create the owned test icon mask bitmap.");
    }

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = colorBitmap;
    iconInfo.hbmMask = maskBitmap;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(maskBitmap);
    DeleteObject(colorBitmap);
    Require(icon != nullptr, "Could not create the owned test HICON.");
    return icon;
}

HICON CreateOwnedLegacyMaskIcon()
{
    constexpr int size = 16;
    constexpr std::size_t bytesPerMaskRow = 2;
    std::vector<std::uint8_t> andBits(bytesPerMaskRow * size, 0);
    std::vector<std::uint8_t> xorBits(bytesPerMaskRow * size, 0);
    andBits[0] = 0x40; // x=1 is transparent; x=0 remains opaque black.
    HICON icon = CreateIcon(
        GetModuleHandleW(nullptr), size, size, 1, 1, andBits.data(), xorBits.data());
    Require(icon != nullptr, "Could not create the owned legacy AND/XOR test HICON.");
    return icon;
}

void TestWindowCropUsesOutputRelativePhysicalCoordinates()
{
    Require(MapWindowCrop({ 100, 200, 1100, 900 }, { 260, 320, 1240, 1040 }, llp::material::OutputRotation::Identity)
                == llp::material::PixelRect{ 160, 120, 1000, 700 },
        "Identity crop was not clamped and output-relative.");
    Require(MapWindowCrop({ 0, 0, 1080, 1920 }, { 140, 420, 860, 1400 }, llp::material::OutputRotation::Rotate90)
                == llp::material::PixelRect{ 420, 220, 1400, 940 },
        "Rotated crop mapping changed the physical viewport.");
    Require(MapWindowCrop({ 100, 200, 1100, 900 }, { 260, 320, 900, 760 }, llp::material::OutputRotation::Rotate180)
                == llp::material::PixelRect{ 200, 140, 840, 580 },
        "Rotate180 did not preserve the physical viewport.");
    Require(MapWindowCrop({ 100, 200, 1100, 900 }, { 260, 320, 900, 760 }, llp::material::OutputRotation::Rotate270)
                == llp::material::PixelRect{ 140, 160, 580, 800 },
        "Rotate270 did not preserve the physical viewport.");
    Require(MapWindowCrop({ 100, 200, 1100, 900 }, { -500, -400, 50, 100 }, llp::material::OutputRotation::Identity)
                == llp::material::PixelRect{},
        "Disjoint window produced a non-empty crop.");
    Require(MapWindowCrop({ -1920, -100, 0, 980 }, { -1800, 0, -1200, 600 }, llp::material::OutputRotation::Identity)
                == llp::material::PixelRect{ 120, 100, 720, 700 },
        "Negative desktop origin changed output-relative coordinates.");
    Require(MapWindowCrop({ -100, -100, 900, 600 }, { -200, -200, 200, 200 }, llp::material::OutputRotation::Rotate90)
                == llp::material::PixelRect{ 0, 700, 300, 1000 },
        "Rotate90 did not preserve a left/top partial intersection.");
    Require(MapWindowCrop({ -100, -100, 900, 600 }, { -200, -200, 200, 200 }, llp::material::OutputRotation::Rotate270)
                == llp::material::PixelRect{ 400, 0, 700, 300 },
        "Rotate270 did not preserve a left/top partial intersection.");
    Require(MapWindowCrop({ 0, 0, 0, 1080 }, { 0, 0, 100, 100 }, llp::material::OutputRotation::Identity)
                == llp::material::PixelRect{},
        "Degenerate output produced a crop.");
    Require(MapWindowCrop({ 0, 0, 1920, 1080 }, { 100, 100, 100, 200 }, llp::material::OutputRotation::Identity)
                == llp::material::PixelRect{},
        "Degenerate window produced a crop.");
    Pass("MaterialRenderer_CropClampsAndMapsEveryRotation");
}

void TestPixelAndSearchCoordinatesStayInDipSpace()
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float largest = std::numeric_limits<float>::max();

    RequireNear(PixelsToDip(96.0f, 96), 96.0f, 0.001f, "96 DPI conversion changed DIP identity.");
    RequireNear(PixelsToDip(144.0f, 144), 96.0f, 0.001f, "Pixel-to-DIP conversion ignored monitor DPI.");
    RequireNear(PixelsToDip(192.0f, 192), 96.0f, 0.001f, "192 DPI conversion ignored monitor DPI.");
    RequireNear(PixelsToDip(48.0f, 0), 48.0f, 0.001f, "Invalid DPI did not use the 96 DPI baseline.");
    RequireNear(PixelsToDip(48.0f, -192), 48.0f, 0.001f, "Negative DPI did not use the 96 DPI baseline.");
    Require(PixelsToDip(nan, 96) == 0.0f, "Non-finite pixels did not return zero.");
    Require(PixelsToDip(infinity, 96) == 0.0f, "Infinite pixels did not return zero.");
    Require(std::isfinite(PixelsToDip(largest, 1)), "Finite pixel conversion overflowed to infinity.");

    for (const int dpi : { 96, 144, 192 })
    {
        const auto center = ComputeSearchSample({ 0.0f, 0.0f, 300.0f, 50.0f }, 150.0f, 25.0f, dpi, false);
        RequireNear(center.sourceXDip, 150.0f, 0.5f, "Search center exceeded 0.5 DIP.");
        RequireNear(center.sourceYDip, 25.0f, 0.5f, "Search center gained vertical bias.");
        RequireNear(center.displacementDip, 0.0f, 0.001f, "Search center gained displacement.");

        const auto edge = ComputeSearchSample({ 0.0f, 0.0f, 300.0f, 50.0f }, 1.0f, 1.0f, dpi, false);
        Require(edge.displacementDip > 1.0f && edge.displacementDip <= 1.5f,
            "Search edge response was absent or exceeded 1.5 DIP.");
        Require(edge.sourceXDip > 1.0f && edge.sourceYDip > 1.0f,
            "Search edge sample did not move inward.");
        Require(edge.sourceXDip >= 0.0f && edge.sourceXDip <= 300.0f
                && edge.sourceYDip >= 0.0f && edge.sourceYDip <= 50.0f,
            "Search sample escaped its control.");
    }

    const auto outside = ComputeSearchSample({ 10.0f, 20.0f, 310.0f, 70.0f }, -50.0f, 200.0f, 144, false);
    Require(outside.sourceXDip >= 10.0f && outside.sourceXDip <= 310.0f
            && outside.sourceYDip >= 20.0f && outside.sourceYDip <= 70.0f,
        "Out-of-range destination escaped the search bounds.");
    Require(outside.displacementDip <= 1.5f, "Clamped search destination exceeded the displacement cap.");

    const auto reduced = ComputeSearchSample({ 10.0f, 20.0f, 310.0f, 70.0f }, 11.0f, 21.0f, 192, true);
    RequireNear(reduced.sourceXDip, 11.0f, 0.001f, "Reduced motion displaced the horizontal search sample.");
    RequireNear(reduced.sourceYDip, 21.0f, 0.001f, "Reduced motion displaced the vertical search sample.");
    RequireNear(reduced.displacementDip, 0.0f, 0.001f, "Reduced motion reported displacement.");

    const auto invalidBounds = ComputeSearchSample({ nan, 0.0f, 300.0f, 50.0f }, 10.0f, 10.0f, 96, false);
    Require(invalidBounds.sourceXDip == 0.0f && invalidBounds.sourceYDip == 0.0f
            && invalidBounds.displacementDip == 0.0f,
        "Non-finite search bounds did not return the deterministic zero sample.");
    const auto infiniteBounds = ComputeSearchSample({ 0.0f, 0.0f, infinity, 50.0f }, 10.0f, 10.0f, 96, false);
    Require(infiniteBounds.sourceXDip == 0.0f && infiniteBounds.sourceYDip == 0.0f
            && infiniteBounds.displacementDip == 0.0f,
        "Infinite search bounds did not return the deterministic zero sample.");
    const auto reversedBounds = ComputeSearchSample({ 300.0f, 0.0f, 0.0f, 50.0f }, 10.0f, 10.0f, 96, false);
    Require(reversedBounds.sourceXDip == 0.0f && reversedBounds.sourceYDip == 0.0f
            && reversedBounds.displacementDip == 0.0f,
        "Reversed search bounds did not return the deterministic zero sample.");
    const auto degenerateBounds = ComputeSearchSample({ 0.0f, 20.0f, 300.0f, 20.0f }, 10.0f, 20.0f, 96, false);
    Require(degenerateBounds.sourceXDip == 0.0f && degenerateBounds.sourceYDip == 0.0f
            && degenerateBounds.displacementDip == 0.0f,
        "Degenerate search bounds did not return the deterministic zero sample.");

    const auto invalidDestination = ComputeSearchSample(
        { 10.0f, 20.0f, 310.0f, 70.0f }, nan, infinity, 144, false);
    Require(std::isfinite(invalidDestination.sourceXDip) && std::isfinite(invalidDestination.sourceYDip)
            && std::isfinite(invalidDestination.displacementDip),
        "Non-finite destination produced a non-finite search sample.");
    Require(invalidDestination.sourceXDip >= 10.0f && invalidDestination.sourceXDip <= 310.0f
            && invalidDestination.sourceYDip >= 20.0f && invalidDestination.sourceYDip <= 70.0f,
        "Normalized destination escaped valid search bounds.");

    const auto hugeBounds = ComputeSearchSample(
        { largest * 0.5f, largest * 0.5f, largest, largest }, nan, nan, 96, false);
    Require(std::isfinite(hugeBounds.sourceXDip) && std::isfinite(hugeBounds.sourceYDip)
            && std::isfinite(hugeBounds.displacementDip),
        "Large finite search bounds overflowed the sample.");
    Pass("MaterialRenderer_SearchSamplingIsBoundedAcrossDpi");
}

void TestMaterialToneKeepsPolicyContrastAndBoundedCoolTint()
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const auto lightTone = ResolveMaterialTone(0.92f, false);
    const auto darkTone = ResolveMaterialTone(0.05f, true);
    Require(llp::adaptive::ContrastRatio(lightTone.text, lightTone.guardedSurface) >= 4.5,
        "Light material text contrast fell below 4.5:1.");
    Require(llp::adaptive::ContrastRatio(lightTone.focus, lightTone.guardedSurface) >= 3.0,
        "Light material focus contrast fell below 3:1.");
    Require(llp::adaptive::ContrastRatio(darkTone.text, darkTone.guardedSurface) >= 4.5,
        "Dark material text contrast fell below 4.5:1.");
    Require(llp::adaptive::ContrastRatio(darkTone.focus, darkTone.guardedSurface) >= 3.0,
        "Dark material focus contrast fell below 3:1.");
    for (const auto& tone : { ResolveMaterialTone(-1.0f, false), ResolveMaterialTone(2.0f, true) })
    {
        Require(tone.tint.red <= tone.tint.green && tone.tint.green <= tone.tint.blue,
            "Material tint was not restrained cool white-blue.");
        Require(tone.tintOpacity >= 0.08f && tone.tintOpacity <= 0.18f,
            "Material tint opacity escaped its bounded range.");
        Require(tone.blurRadiusDip >= 16.0f && tone.blurRadiusDip <= 24.0f,
            "Material blur radius escaped its bounded range.");
    }

    for (const bool darkMode : { false, true })
    {
        for (const float luminance : { 0.0f, 0.05f, 0.1f, 0.25f, 0.5f, 0.75f, 0.92f, 1.0f })
        {
            const auto tone = ResolveMaterialTone(luminance, darkMode);
            Require(llp::adaptive::ContrastRatio(tone.text, tone.guardedSurface) >= 4.5,
                "Dense luminance text color was not finally verified at 4.5:1.");
            Require(llp::adaptive::ContrastRatio(tone.focus, tone.guardedSurface) >= 3.0,
                "Dense luminance focus color was not finally verified at 3:1.");
            Require(std::isfinite(tone.tintOpacity) && std::isfinite(tone.blurRadiusDip),
                "Finite luminance produced non-finite material values.");
        }

        const auto neutral = ResolveMaterialTone(0.5f, darkMode);
        for (const float luminance : { nan, infinity, -infinity })
        {
            const auto tone = ResolveMaterialTone(luminance, darkMode);
            RequireNear(tone.tintOpacity, neutral.tintOpacity, 0.001f,
                "Non-finite luminance did not use neutral tint strength.");
            RequireNear(tone.blurRadiusDip, neutral.blurRadiusDip, 0.001f,
                "Non-finite luminance did not use neutral blur strength.");
            Require(llp::adaptive::ContrastRatio(tone.text, tone.guardedSurface) >= 4.5,
                "Non-finite luminance text color was not finally verified.");
            Require(llp::adaptive::ContrastRatio(tone.focus, tone.guardedSurface) >= 3.0,
                "Non-finite luminance focus color was not finally verified.");
        }
    }
    Pass("MaterialRenderer_ToneUsesAdaptiveContrastAndBoundedTint");
}

void TestLifecycleStartsFallsBackAndConsumesOneRebuild()
{
    auto transition = AdvanceLifecycle(llp::material::LifecycleState::Hidden,
        llp::material::LifecycleEvent::ShowRequested, false);
    Require(transition.next == llp::material::LifecycleState::Starting, "Show did not enter Starting.");
    transition = AdvanceLifecycle(transition.next, llp::material::LifecycleEvent::FirstFrameTimedOut, false);
    Require(transition.next == llp::material::LifecycleState::Fallback, "32 ms timeout did not select fallback.");

    transition = AdvanceLifecycle(llp::material::LifecycleState::Live,
        llp::material::LifecycleEvent::AccessLost, false);
    Require(transition.next == llp::material::LifecycleState::Rebuilding && transition.consumeRebuild,
        "First access loss did not consume the one rebuild.");
    transition = AdvanceLifecycle(llp::material::LifecycleState::Rebuilding,
        llp::material::LifecycleEvent::InitializationFailed, true);
    Require(transition.next == llp::material::LifecycleState::Fallback && !transition.consumeRebuild,
        "Failed rebuild did not remain in fallback.");

    transition = AdvanceLifecycle(llp::material::LifecycleState::Live,
        llp::material::LifecycleEvent::DeviceRemoved, true);
    Require(transition.next == llp::material::LifecycleState::Fallback && !transition.consumeRebuild,
        "A second device loss attempted another rebuild.");
    Pass("MaterialRenderer_LifecycleAllowsOneBoundedRebuild");
}

void TestLifecycleHandlesReadyFreezeAndHideEvents()
{
    auto transition = AdvanceLifecycle(llp::material::LifecycleState::Starting,
        llp::material::LifecycleEvent::FirstFrameReady, false);
    Require(transition.next == llp::material::LifecycleState::Live, "First frame did not enter Live.");
    transition = AdvanceLifecycle(transition.next, llp::material::LifecycleEvent::FreezeEvidence, false);
    Require(transition.next == llp::material::LifecycleState::FrozenEvidence,
        "Evidence freeze did not retain the rendered scene.");
    transition = AdvanceLifecycle(transition.next, llp::material::LifecycleEvent::HideRequested, false);
    Require(transition.next == llp::material::LifecycleState::Stopping, "Hide did not enter Stopping.");
    transition = AdvanceLifecycle(transition.next, llp::material::LifecycleEvent::Hidden, false);
    Require(transition.next == llp::material::LifecycleState::Hidden, "Hidden event did not finish stopping.");
    transition = AdvanceLifecycle(llp::material::LifecycleState::Rebuilding,
        llp::material::LifecycleEvent::RebuildSucceeded, true);
    Require(transition.next == llp::material::LifecycleState::Live && !transition.consumeRebuild,
        "Successful rebuild did not return to Live.");
    Pass("MaterialRenderer_LifecycleHandlesReadyFreezeAndHide");
}

void TestSceneSnapshotsOwnCopiedPremultipliedIconBytes()
{
    HICON sourceIcon = CreateOwnedTestIcon();
    Require(RasterizeIcon(nullptr, 16) == nullptr, "RasterizeIcon accepted a null HICON.");
    Require(RasterizeIcon(sourceIcon, 0) == nullptr, "RasterizeIcon accepted a zero target size.");
    Require(RasterizeIcon(sourceIcon, -1) == nullptr, "RasterizeIcon accepted a negative target size.");
    Require(RasterizeIcon(sourceIcon, std::numeric_limits<int>::max()) == nullptr,
        "RasterizeIcon did not reject stride arithmetic overflow.");
    auto icon = RasterizeIcon(sourceIcon, 16);
    Require(icon != nullptr, "RasterizeIcon rejected a valid owned HICON.");
    Require(icon->width == 16 && icon->height == 16, "Rasterized icon dimensions changed.");
    Require(icon->stride >= icon->width * 4, "Rasterized icon stride was too small.");
    Require(icon->premultipliedBgra.size()
            == static_cast<std::size_t>(icon->stride) * static_cast<std::size_t>(icon->height),
        "Rasterized icon byte count did not match stride times height.");
    Require(!icon->premultipliedBgra.empty(), "Rasterized icon did not own BGRA bytes.");
    bool foundModernPartialAlpha = false;
    for (std::size_t offset = 0; offset + 3 < icon->premultipliedBgra.size(); offset += 4)
    {
        const auto alpha = icon->premultipliedBgra[offset + 3];
        foundModernPartialAlpha = foundModernPartialAlpha || (alpha > 0 && alpha < 255);
        if (alpha != 0)
        {
            Require(icon->premultipliedBgra[offset + 0] <= alpha
                    && icon->premultipliedBgra[offset + 1] <= alpha
                    && icon->premultipliedBgra[offset + 2] <= alpha,
                "Rasterized icon contained non-premultiplied color channels.");
        }
    }
    Require(foundModernPartialAlpha, "Modern 32-bit icon lost its partial alpha coverage.");
    for (const auto& expected : expectedModernPixels)
    {
        const auto offset = static_cast<std::size_t>(expected.y) * static_cast<std::size_t>(icon->stride)
            + static_cast<std::size_t>(expected.x) * 4;
        // CreateIconIndirect plus DrawIconEx applies one per-pixel blend with byte rounding.
        // RasterizeIcon must preserve that black-background result without applying it again.
        RequireByteNear(icon->premultipliedBgra[offset + 0],
            ExpectedDrawIconChannel(expected.bgra[0], expected.bgra[3]), 1,
            "Modern icon blue channel changed beyond DrawIconEx blending or was premultiplied again.");
        RequireByteNear(icon->premultipliedBgra[offset + 1],
            ExpectedDrawIconChannel(expected.bgra[1], expected.bgra[3]), 1,
            "Modern icon green channel changed beyond DrawIconEx blending or was premultiplied again.");
        RequireByteNear(icon->premultipliedBgra[offset + 2],
            ExpectedDrawIconChannel(expected.bgra[2], expected.bgra[3]), 1,
            "Modern icon red channel changed beyond DrawIconEx blending or was premultiplied again.");
        RequireByteNear(icon->premultipliedBgra[offset + 3], expected.bgra[3], 1,
            "Modern icon alpha changed during DrawIconEx coverage recovery.");
    }

    const auto expectedBytes = icon->premultipliedBgra;
    auto firstScene = std::make_shared<llp::material::SceneSnapshot>();
    firstScene->generation = 1;
    firstScene->items.push_back({ { 1, 2, 17, 18 }, L"First", L"Producer", icon, true, false });
    auto newestScene = std::make_shared<llp::material::SceneSnapshot>();
    newestScene->generation = 2;
    newestScene->items.push_back({ { 3, 4, 19, 20 }, L"Newest", L"Owned", icon, false, true });

    llp::material::LatestSceneSlot slot;
    slot.Submit(firstScene);
    icon.reset();
    Require(DestroyIcon(sourceIcon) != FALSE,
        "Caller could not destroy the modern input HICON after RasterizeIcon returned.");
    sourceIcon = nullptr;
    slot.Submit(newestScene);
    firstScene.reset();
    newestScene.reset();

    const auto taken = slot.Take();
    Require(taken != nullptr && taken->generation == 2, "Replacing an unrendered scene lost the newest scene.");
    Require(taken->items.size() == 1 && taken->items[0].icon != nullptr,
        "Newest immutable scene did not retain its shared icon raster.");
    Require(taken->items[0].icon->premultipliedBgra == expectedBytes,
        "Icon bytes changed after producer references and input HICON were released.");
    Pass("MaterialRenderer_SceneOwnsCopiedPremultipliedIconBytes");
}

void TestLegacyMaskIconDistinguishesOpaqueBlackFromTransparency()
{
    HICON sourceIcon = CreateOwnedLegacyMaskIcon();
    const auto icon = RasterizeIcon(sourceIcon, 16);
    Require(DestroyIcon(sourceIcon) != FALSE,
        "Caller could not destroy the legacy input HICON after RasterizeIcon returned.");
    Require(icon != nullptr, "RasterizeIcon rejected a valid legacy AND/XOR HICON.");
    Require(icon->stride >= icon->width * 4, "Legacy icon stride was too small.");

    const auto pixelOffset = [icon](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(icon->stride)
            + static_cast<std::size_t>(x) * 4;
    };
    const auto opaqueBlack = pixelOffset(0, 0);
    Require(icon->premultipliedBgra[opaqueBlack + 0] == 0
            && icon->premultipliedBgra[opaqueBlack + 1] == 0
            && icon->premultipliedBgra[opaqueBlack + 2] == 0
            && icon->premultipliedBgra[opaqueBlack + 3] == 255,
        "Legacy opaque black pixel lost full alpha coverage.");
    const auto transparent = pixelOffset(1, 0);
    Require(icon->premultipliedBgra[transparent + 3] == 0,
        "Legacy transparent mask pixel gained alpha coverage.");
    Pass("MaterialRenderer_LegacyMaskSeparatesOpaqueBlackAndTransparentPixels");
}

void TestLatestSceneSlotReturnsOnlyNewestCompleteGeneration()
{
    llp::material::LatestSceneSlot slot;
    std::thread producer([&slot]() {
        for (std::uint64_t generation = 1; generation <= 3; ++generation)
        {
            auto scene = std::make_shared<llp::material::SceneSnapshot>();
            scene->generation = generation;
            scene->search.displayText = L"generation-" + std::to_wstring(generation);
            scene->items.push_back({ { static_cast<int>(generation), 0,
                                          static_cast<int>(generation + 10), 10 },
                scene->search.displayText, L"complete", nullptr, generation == 3, false });
            slot.Submit(std::move(scene));
        }
    });
    producer.join();

    const auto newest = slot.Take();
    Require(newest != nullptr && newest->generation == 3, "Latest scene slot did not replace generations 1 and 2.");
    Require(newest->search.displayText == L"generation-3"
            && newest->items.size() == 1 && newest->items[0].focused,
        "Latest scene slot returned torn scene ownership.");
    Require(slot.Take() == nullptr, "Taking a scene did not atomically clear the slot.");

    auto pending = std::make_shared<llp::material::SceneSnapshot>();
    pending->generation = 4;
    slot.Submit(std::move(pending));
    slot.Submit(nullptr);
    Require(slot.Take() == nullptr, "Submitting null did not deterministically clear the pending scene.");
    Pass("MaterialRenderer_LatestSceneSlotKeepsOnlyNewestCompleteGeneration");
}

void TestRendererThreadBootstrapsGpuDependenciesAndReturnsHidden()
{
    std::uint32_t featureLevel = 0;
    llp::material::FailureReason probeFailure = llp::material::FailureReason::ForcedForTest;
    Require(RendererDependenciesAvailable(&featureLevel, &probeFailure),
        "D3D11, DXGI, Direct2D, or DirectComposition dependency probe failed.");
    Require(probeFailure == llp::material::FailureReason::None,
        "Successful renderer dependency probe retained a failure reason.");
    Require(featureLevel != 0, "Renderer dependency probe did not record the selected D3D feature level.");

    llp::material::MaterialRenderer renderer;
    const auto initial = renderer.SnapshotDiagnostics();
    Require(initial.state == llp::material::LifecycleState::Hidden,
        "Renderer thread did not begin in the hidden state.");
    Require(initial.path == llp::material::RenderPath::Fallback,
        "Hidden renderer did not retain the safe fallback path.");
    Require(initial.failure == llp::material::FailureReason::None,
        "Hidden renderer began with a failure reason.");

    HWND probeWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW, L"STATIC", L"LightLaunchpad material dependency probe",
        WS_POPUP, 40, 40, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(probeWindow != nullptr, "Could not create the renderer dependency probe window.");
    RECT probeRect = {};
    Require(GetWindowRect(probeWindow, &probeRect) != FALSE,
        "Could not read the renderer dependency probe window rectangle.");
    llp::material::ShowRequest showRequest;
    showRequest.window = probeWindow;
    showRequest.monitor = MonitorFromWindow(probeWindow, MONITOR_DEFAULTTONEAREST);
    showRequest.windowDesktop = { probeRect.left, probeRect.top, probeRect.right, probeRect.bottom };
    renderer.Show(showRequest);
    const auto startDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    llp::material::Diagnostics started;
    do
    {
        started = renderer.SnapshotDiagnostics();
        if (started.path == llp::material::RenderPath::HighFidelity
            || (started.state == llp::material::LifecycleState::Fallback
                && started.failure != llp::material::FailureReason::None))
        {
            break;
        }
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < startDeadline);
    Require((started.path == llp::material::RenderPath::HighFidelity
                && started.failure == llp::material::FailureReason::None)
            || (started.state == llp::material::LifecycleState::Fallback
                && started.failure != llp::material::FailureReason::None),
        "Renderer did not publish either a live-capable path or a stable machine-specific fallback.");

    renderer.Hide();
    Require(renderer.WaitUntilHiddenForTest(2000),
        "Renderer thread did not acknowledge hide without a periodic wake loop.");
    Require(renderer.SnapshotDiagnostics().state == llp::material::LifecycleState::Hidden,
        "Renderer diagnostics did not return to Hidden after hide acknowledgement.");
    DestroyWindow(probeWindow);
    std::cout << "INFO MaterialRenderer featureLevel=0x" << std::hex << featureLevel << std::dec << "\n";
    Pass("MaterialRenderer_RenderThreadBootstrapsDependenciesAndReturnsHidden");
}

void TestRendererConstructionAndImmediateDestructionStress()
{
    constexpr int iterations = 1000;
    const auto started = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        llp::material::MaterialRenderer renderer;
    }
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "METRIC MaterialRenderer constructDestroyIterations=" << iterations
              << " elapsedMs=" << elapsedMs << "\n";
    Pass("MaterialRenderer_ConstructImmediateDestroyStressIsStable");
}

void TestRendererImmediateShowHideStress()
{
    constexpr int iterations = 500;
    const auto started = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        llp::material::MaterialRenderer renderer;
        renderer.Show({});
        renderer.Hide();
        Require(renderer.WaitUntilHiddenForTest(2000),
            "Immediate Show/Hide stress did not reach Hidden.");
    }
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "METRIC MaterialRenderer immediateShowHideIterations=" << iterations
              << " elapsedMs=" << elapsedMs << "\n";
    Pass("MaterialRenderer_ImmediateShowHideStressIsStable");
}

MaterialLifecycleTestReport RunFakeLifecycle(int scenario)
{
    MaterialLifecycleTestReport report;
    Require(LLP_MaterialRunFakeLifecycleScenario(scenario, &report) == 0,
        "The private fake capture backend rejected a lifecycle scenario.");
    return report;
}

void TestCaptureCoordinatesCoverAllRotationsAndRejectPartialViewport()
{
    const llp::material::PixelRect output = { -1920, -100, 0, 980 };
    const llp::material::PixelRect window = { -1800, 20, -1400, 420 };
    Require(MapWindowCrop(output, window, llp::material::OutputRotation::Identity)
            == llp::material::PixelRect{ 120, 120, 520, 520 },
        "Negative-origin identity crop changed desktop alignment.");
    Require(MapWindowCrop(output, window, llp::material::OutputRotation::Rotate90)
            == llp::material::PixelRect{ 120, 1400, 520, 1800 },
        "90-degree output rotation changed desktop alignment.");
    Require(MapWindowCrop(output, window, llp::material::OutputRotation::Rotate180)
            == llp::material::PixelRect{ 1400, 560, 1800, 960 },
        "180-degree output rotation changed desktop alignment.");
    Require(MapWindowCrop(output, window, llp::material::OutputRotation::Rotate270)
            == llp::material::PixelRect{ 560, 120, 960, 520 },
        "270-degree output rotation changed desktop alignment.");

    const auto report = RunFakeLifecycle(0);
    Require(report.selectedAdapter == 1 && report.selectedOutput == 0,
        "Output enumeration did not bind the exact target HMONITOR on its adapter.");
    Require(report.duplicateCalls == 1,
        "DuplicateOutput was not called exactly once for the visible activation.");
    Require(report.partialCropRejected == 1,
        "Capture accepted a partially clamped window instead of requiring full containment.");
    std::cout << "METRIC MaterialCaptureOutput adapter=" << report.selectedAdapter
              << " output=" << report.selectedOutput
              << " duplicateCalls=" << report.duplicateCalls
              << " partialRejected=" << report.partialCropRejected << "\n";
    Pass("MaterialRenderer_OutputSelectionRotationsAndContainmentAreExact");
}

void TestFirstFrameDeadlineAndTimeoutDoNotRedraw()
{
    const auto report = RunFakeLifecycle(1);
    Require(report.acquireCalls == 2 && report.maximumWaitMilliseconds <= 16,
        "First-frame acquisition did not use two bounded waits within the 32 ms budget.");
    Require(report.firstFrameMicroseconds >= 32000 && report.firstFrameMicroseconds < 33000,
        "First-frame timeout did not use the QPC 32 ms deadline.");
    Require(report.redraws == 0 && report.acquiredFrames == 0,
        "A wait timeout incorrectly caused a redraw or acquired-frame advance.");
    Require(report.finalPath == static_cast<int>(llp::material::RenderPath::Fallback)
            && report.finalFailure == static_cast<int>(llp::material::FailureReason::FirstFrameTimeout),
        "First-frame timeout did not publish the stable fallback decision.");
    Require(report.affinityExcluded == 1 && report.affinityCleared >= 1,
        "Fallback did not clear the capture-exclusion affinity.");
    std::cout << "METRIC MaterialFirstFrame acquireCalls=" << report.acquireCalls
              << " maxWaitMs=" << report.maximumWaitMilliseconds
              << " firstFrameUs=" << report.firstFrameMicroseconds
              << " timeoutRedraws=" << report.redraws
              << " affinitySet=" << report.affinityExcluded
              << " affinityClears=" << report.affinityCleared << "\n";
    Pass("MaterialRenderer_FirstFrameDeadlineIsBoundedAndTimeoutIsIdle");
}

void TestFramesReleaseBeforeReacquireRebuildAndHide()
{
    const auto report = RunFakeLifecycle(2);
    Require(report.acquiredFrames == 2 && report.lastFrameIdentity == 22,
        "Latest desktop frame counters did not survive one access-loss rebuild.");
    Require(report.rebuilds == 1 && report.duplicateCalls == 2,
        "Access loss did not perform exactly one bounded duplication rebuild.");
    Require(report.releaseBeforeReacquire == 1
            && report.releaseBeforeRebuild == 1
            && report.releaseBeforeHide == 1,
        "An acquired Desktop Duplication frame was retained across a lifecycle boundary.");
    Require(report.maximumWaitMilliseconds <= 16,
        "Visible frame acquisition exceeded the 16 ms wait cap.");
    Require(report.hiddenTimedWaits == 0 && report.hiddenAcquireCalls == 0,
        "Hidden renderer retained a timed wait or capture loop.");
    Require(report.affinityCleared >= 1,
        "Hide did not clear the window display affinity.");
    std::cout << "METRIC MaterialRecovery acquired=" << report.acquiredFrames
              << " lastIdentity=" << report.lastFrameIdentity
              << " rebuilds=" << report.rebuilds
              << " duplicateCalls=" << report.duplicateCalls
              << " releases=" << report.releaseCalls
              << " hiddenAcquireCalls=" << report.hiddenAcquireCalls
              << " hiddenWaits=" << report.hiddenTimedWaits << "\n";
    Pass("MaterialRenderer_ReleasesFramesAndStopsAllHiddenPolling");
}

void TestSecondAccessLossFallsBackWithoutRecurringRetry()
{
    const auto report = RunFakeLifecycle(3);
    Require(report.rebuilds == 1 && report.duplicateCalls == 2,
        "Repeated access loss exceeded the one-rebuild budget.");
    Require(report.finalState == static_cast<int>(llp::material::LifecycleState::Fallback)
            && report.finalPath == static_cast<int>(llp::material::RenderPath::Fallback)
            && report.finalFailure == static_cast<int>(llp::material::FailureReason::AccessLost),
        "Second access loss did not remain in stable fallback.");
    Require(report.hiddenTimedWaits == 0,
        "Fallback introduced a recurring recovery timer.");
    Pass("MaterialRenderer_AccessLossConsumesExactlyOneRebuild");
}

void TestDevelopmentForcedFailuresMapToStableReason()
{
    for (const int scenario : { 4, 5, 6 })
    {
        const auto report = RunFakeLifecycle(scenario);
        Require(report.finalPath == static_cast<int>(llp::material::RenderPath::Fallback)
                && report.finalFailure == static_cast<int>(llp::material::FailureReason::ForcedForTest),
            "A development forced failure did not map to ForcedForTest fallback.");
    }
    Require(RunFakeLifecycle(4).rebuilds == 0,
        "Forced initialization failure consumed a device rebuild.");
    Require(RunFakeLifecycle(5).rebuilds == 0,
        "Forced duplication failure consumed a device rebuild.");
    Require(RunFakeLifecycle(6).rebuilds == 1,
        "Forced access loss did not exercise exactly one rebuild before fallback.");
    Pass("MaterialRenderer_DevelopmentForcedFailuresUseStableMapping");
}

void TestFreezeReleasesCaptureAndClearsAffinity()
{
    const auto report = RunFakeLifecycle(7);
    Require(report.finalState == static_cast<int>(llp::material::LifecycleState::FrozenEvidence),
        "Freeze did not enter the terminal evidence state.");
    Require(report.releaseBeforeFreeze == 1,
        "Freeze retained an acquired Desktop Duplication frame.");
    Require(report.affinityCleared >= 1,
        "Freeze retained WDA_EXCLUDEFROMCAPTURE.");
    Pass("MaterialRenderer_FreezeReleasesFrameAndCaptureAffinity");
}

void TestLateFirstFrameIsReleasedAndFallsBack()
{
    const auto report = RunFakeLifecycle(8);
    Require(report.firstFrameMicroseconds == 33000
            && report.finalFailure == static_cast<int>(llp::material::FailureReason::FirstFrameTimeout),
        "A first frame arriving after the 32 ms QPC deadline was accepted.");
    Require(report.acquiredFrames == 0 && report.releaseCalls == 1,
        "Late first frame was counted as usable or retained during fallback.");
    Pass("MaterialRenderer_LateFirstFrameIsReleasedBeforeFallback");
}

void TestBackendStartupDoesNotConsumeFirstFrameAcquireBudget()
{
    const auto report = RunFakeLifecycle(9);
    Require(report.acquireCalls == 1,
        "Backend startup time consumed the 32 ms first-frame acquisition window before Acquire ran.");
    Require(report.acquiredFrames == 1 && report.lastFrameIdentity == 99,
        "A prompt frame in the post-start acquisition window was not accepted.");
    Require(report.firstFrameMicroseconds == 5000
            && report.finalPath == static_cast<int>(llp::material::RenderPath::HighFidelity),
        "First-frame timing did not begin after successful backend startup.");
    Pass("MaterialRenderer_BackendStartupPrecedesFirstFrameDeadline");
}

void TestHideRequestRemainsAsynchronous()
{
    llp::material::MaterialRenderer renderer;
    renderer.Show({});
    const auto started = std::chrono::steady_clock::now();
    renderer.Hide();
    const auto callerMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    Require(callerMicroseconds < 5000,
        "Hide blocked the caller instead of signaling renderer cleanup.");
    Require(renderer.WaitUntilHiddenForTest(2000),
        "Renderer thread did not signal its hidden condition after asynchronous cleanup.");
    std::cout << "METRIC MaterialRenderer hideCallerUs=" << callerMicroseconds << "\n";
    Pass("MaterialRenderer_HideSignalsImmediatelyAndCleansOnRendererThread");
}
}

int main()
{
    try
    {
        TestWindowCropUsesOutputRelativePhysicalCoordinates();
        TestPixelAndSearchCoordinatesStayInDipSpace();
        TestMaterialToneKeepsPolicyContrastAndBoundedCoolTint();
        TestLifecycleStartsFallsBackAndConsumesOneRebuild();
        TestLifecycleHandlesReadyFreezeAndHideEvents();
        TestSceneSnapshotsOwnCopiedPremultipliedIconBytes();
        TestLegacyMaskIconDistinguishesOpaqueBlackFromTransparency();
        TestLatestSceneSlotReturnsOnlyNewestCompleteGeneration();
        TestRendererThreadBootstrapsGpuDependenciesAndReturnsHidden();
        TestRendererConstructionAndImmediateDestructionStress();
        TestRendererImmediateShowHideStress();
        TestCaptureCoordinatesCoverAllRotationsAndRejectPartialViewport();
        TestFirstFrameDeadlineAndTimeoutDoNotRedraw();
        TestFramesReleaseBeforeReacquireRebuildAndHide();
        TestSecondAccessLossFallsBackWithoutRecurringRetry();
        TestDevelopmentForcedFailuresMapToStableReason();
        TestFreezeReleasesCaptureAndClearsAffinity();
        TestLateFirstFrameIsReleasedAndFallsBack();
        TestBackendStartupDoesNotConsumeFirstFrameAcquireBudget();
        TestHideRequestRemainsAsynchronous();
        constexpr int expected = 20;
        std::cout << passed << "/" << expected << " tests passed\n";
        return passed == expected ? 0 : 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL " << exception.what() << "\n";
        return 1;
    }
}
