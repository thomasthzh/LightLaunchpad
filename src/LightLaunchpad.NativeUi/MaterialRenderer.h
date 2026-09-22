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
    std::wstring stableId;
    std::wstring sourcePath;
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

struct AuxiliaryVisual
{
    llp::geometry::Rect bounds;
    std::wstring text;
    bool focused = false;
};

struct SceneSnapshot
{
    std::uint64_t generation = 0;
    llp::geometry::SceneMetrics metrics;
    llp::adaptive::Policy policy;
    SearchVisual search;
    std::vector<SceneItem> items;
    AuxiliaryVisual auxiliary;
};

// Compositor-only motion keeps opening/closing/focus feedback off the paint
// path. Values are expressed in device pixels and are clamped by the
// renderer before they reach DirectComposition.
struct MotionVisual
{
    float opacity = 1.0f;
    float offsetY = 0.0f;
    float overlayOpacity = 1.0f;
    float contentOpacity = 1.0f;
};

struct ShowRequest
{
    HWND window = nullptr;
    HMONITOR monitor = nullptr;
    PixelRect windowDesktop;
    int dpi = 96;
    llp::adaptive::Policy policy;
    bool developmentEvidence = false;
    FailureReason forcedFailure = FailureReason::None;
    UINT statusMessage = 0;
    std::uint64_t activation = 0;
    std::uint64_t requiredSceneGeneration = 0;
    bool developmentStoppingBarrier = false;
    bool developmentComposeLifetimeProbe = false;
    bool developmentRecoverAfterAccessLoss = false;
    bool developmentFailAccessLossRebuild = false;
};

struct Diagnostics
{
    RenderPath path = RenderPath::Fallback;
    FailureReason failure = FailureReason::None;
    LifecycleState state = LifecycleState::Hidden;
    std::wstring adapter;
    std::wstring output;
    int dpi = 96;
    bool reducedMotion = false;
    std::uint64_t acquiredFrames = 0;
    std::uint64_t presentedFrames = 0;
    std::uint64_t droppedScenes = 0;
    std::uint64_t deviceRebuilds = 0;
    std::uint64_t lastFrameIdentity = 0;
    double firstFrameMs = 0.0;
    double frameP95Ms = 0.0;
    bool evidenceFrozen = false;
    std::uint64_t statusPublications = 0;
    bool auxiliaryRendered = false;
    std::uint64_t auxiliarySceneGeneration = 0;
    llp::geometry::Rect auxiliaryBounds;
    std::wstring auxiliaryText;
    bool auxiliaryFocused = false;
    std::vector<std::wstring> presentedItemStableIds;
    std::vector<std::wstring> presentedItemSourcePaths;
    std::uint64_t committedActivation = 0;
    std::uint64_t presentedSceneGeneration = 0;
    bool evidenceBaselineCaptured = false;
    std::uint64_t evidenceBaselineDesktopHash = 0;
    std::uint64_t evidenceBaselineFrameIdentity = 0;
    std::uint64_t evidenceBaselineSceneGeneration = 0;
    bool evidenceCandidateCaptured = false;
    std::uint64_t evidenceCandidateDesktopHash = 0;
    std::uint64_t evidenceCandidateFrameIdentity = 0;
    bool evidenceFreezeCommitSucceeded = false;
    std::uint64_t evidenceFreezeDesktopHash = 0;
    std::uint64_t evidenceFreezeFrameIdentity = 0;
    std::uint64_t evidenceFreezeSceneGeneration = 0;
    std::uint64_t presentationEvidenceItemCopies = 0;
    std::uint64_t queuedSceneGeneration = 0;
    std::uint64_t currentSceneGeneration = 0;
    bool showQueued = false;
    bool rebuildQueued = false;
    bool composeLifetimeProbeEntered = false;
    bool composeLifetimeProbeCompleted = false;
    bool composeSceneExpiredDuringHide = false;
    std::uint64_t diagnosticsCaptureStepStringCopies = 0;
    std::uint64_t diagnosticsCaptureStepVectorCopies = 0;
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

class MaterialRenderer
{
public:
    MaterialRenderer();
    ~MaterialRenderer();
    MaterialRenderer(const MaterialRenderer&) = delete;
    MaterialRenderer& operator=(const MaterialRenderer&) = delete;
    void Show(ShowRequest request);
    void Submit(std::shared_ptr<const SceneSnapshot> scene);
    void SetMotion(MotionVisual motion);
    void Rebuild(ShowRequest request);
    void Hide();
    void Shutdown();
    void FreezeEvidence();
    void SnapshotCommittedEvidenceForDevelopment();
    void AcquireUnpresentedEvidenceCandidateForDevelopment();
    void RequestStatusForDevelopment();
    void AcknowledgeStoppingStatusForDevelopment();
    Diagnostics SnapshotDiagnostics() const;
    bool WaitUntilHiddenForTest(std::uint32_t timeoutMs) const;
#if defined(LLP_MATERIAL_TESTING)
    void ApplyEvidenceCandidateResultForTest(int result);
    bool EvidencePreparationActiveForTest() const;
#endif

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

PixelRect MapWindowCrop(PixelRect outputDesktop, PixelRect windowDesktop, OutputRotation rotation);
float PixelsToDip(float pixels, int dpi);
SearchSample ComputeSearchSample(DipRect bounds, float destinationXDip, float destinationYDip, int dpi, bool reducedMotion);
MaterialTone ResolveMaterialTone(float relativeLuminance, bool darkMode);
LifecycleTransition AdvanceLifecycle(LifecycleState state, LifecycleEvent event, bool rebuildAlreadyUsed);

// Host cache design: key `(reinterpret_cast<std::uintptr_t>(icon), sizePx)`, value `weak_ptr<const IconRaster>`, host clears map with existing icon/DPI resource resets.
std::shared_ptr<const IconRaster> RasterizeIcon(HICON icon, int sizePx);

// Test-only production probe. The host does not call this path.
bool RendererDependenciesAvailable(
    std::uint32_t* selectedFeatureLevel = nullptr,
    FailureReason* failure = nullptr);
}
