#include "MaterialRenderer.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using llp::material::AdvanceLifecycle;
using llp::material::ComputeSearchSample;
using llp::material::MapWindowCrop;
using llp::material::PixelsToDip;
using llp::material::ResolveMaterialTone;

int passed = 0;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void RequireNear(float actual, float expected, float tolerance, const char* message)
{
    Require(std::abs(actual - expected) <= tolerance, message);
}

void Pass(const char* name)
{
    ++passed;
    std::cout << "PASS " << name << "\n";
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
    Pass("MaterialRenderer_CropClampsAndMapsEveryRotation");
}

void TestPixelAndSearchCoordinatesStayInDipSpace()
{
    RequireNear(PixelsToDip(144.0f, 144), 96.0f, 0.001f, "Pixel-to-DIP conversion ignored monitor DPI.");
    RequireNear(PixelsToDip(48.0f, 0), 48.0f, 0.001f, "Invalid DPI did not use the 96 DPI baseline.");

    for (const int dpi : { 96, 144, 192 })
    {
        const auto center = ComputeSearchSample({ 0.0f, 0.0f, 300.0f, 50.0f }, 150.0f, 25.0f, dpi, false);
        RequireNear(center.sourceXDip, 150.0f, 0.5f, "Search center exceeded 0.5 DIP.");
        RequireNear(center.sourceYDip, 25.0f, 0.5f, "Search center gained vertical bias.");
        RequireNear(center.displacementDip, 0.0f, 0.001f, "Search center gained displacement.");

        const auto edge = ComputeSearchSample({ 0.0f, 0.0f, 300.0f, 50.0f }, 1.0f, 1.0f, dpi, false);
        Require(edge.displacementDip <= 1.5f, "Search edge exceeded 1.5 DIP.");
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
    Pass("MaterialRenderer_SearchSamplingIsBoundedAcrossDpi");
}

void TestMaterialToneKeepsPolicyContrastAndBoundedCoolTint()
{
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
        std::cout << passed << "/5 tests passed\n";
        return passed == 5 ? 0 : 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL " << exception.what() << "\n";
        return 1;
    }
}
