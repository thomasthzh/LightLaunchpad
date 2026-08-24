#include "MaterialRenderer.h"

#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d2d1helper.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_3.h>
#include <objbase.h>

#if __has_include("LightLaunchpad.BuildIdentity.generated.h")
#include "LightLaunchpad.BuildIdentity.generated.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

#ifndef LLP_BUILD_CHANNEL_WIDE
#if defined(LLP_MATERIAL_TESTING)
#define LLP_BUILD_CHANNEL_WIDE L"development"
#else
#define LLP_BUILD_CHANNEL_WIDE L"stable"
#endif
#endif

// MinGW-w64 imports D2D1CreateDevice from libd2d1 but does not declare it.
extern "C" HRESULT WINAPI D2D1CreateDevice(
    IDXGIDevice* dxgiDevice,
    const D2D1_CREATION_PROPERTIES* creationProperties,
    ID2D1Device** d2dDevice);

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

template <typename Interface>
class ComOwner final
{
public:
    ComOwner() = default;
    ~ComOwner() { Reset(); }
    ComOwner(const ComOwner&) = delete;
    ComOwner& operator=(const ComOwner&) = delete;

    ComOwner(ComOwner&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}
    ComOwner& operator=(ComOwner&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            pointer_ = std::exchange(other.pointer_, nullptr);
        }
        return *this;
    }

    Interface* Get() const { return pointer_; }
    Interface** Put()
    {
        Reset();
        return &pointer_;
    }
    void** PutVoid()
    {
        return reinterpret_cast<void**>(Put());
    }
    void Reset()
    {
        if (pointer_)
        {
            pointer_->Release();
            pointer_ = nullptr;
        }
    }

private:
    Interface* pointer_ = nullptr;
};

struct DeviceResources
{
    ComOwner<ID3D11Device> d3dDevice;
    ComOwner<ID3D11DeviceContext> d3dContext;
    ComOwner<IDXGIDevice> dxgiDevice;
    ComOwner<ID2D1Device> d2dDevice;
    ComOwner<ID2D1DeviceContext> d2dContext;
    ComOwner<IDWriteFactory> dwriteFactory;
    ComOwner<IDCompositionDevice> compositionDevice;
};

enum class FrameCommand : std::int64_t
{
    ClearTransparent = 1,
    DesktopInput = 2,
    MaterialPass = 3,
    OverlayPass = 4,
    ContentPass = 5,
    Commit = 6
};

enum class SearchEdgeOperation : std::int64_t
{
    SetAtlasBeforeClip = 1,
    PushClip = 2,
    SetDisplacedTransform = 3,
    DrawImage = 4,
    PopClip = 5,
    RestoreAtlas = 6
};

enum class SearchEdgeTransformState : std::int64_t
{
    Atlas = 1,
    Displaced = 2
};

#if defined(LLP_MATERIAL_TESTING)
enum class RequiredResourceOperation : int
{
    MaterialDesktopBitmap = 0,
    MaterialBlurEffect = 1,
    ContentTextFormat = 2,
    ContentIconBitmap = 3,
    CompositionCommit = 4,
    CompositionWait = 5,
    CompositionTarget = 6,
    CompositionBeginDraw = 7,
    CaptureDesktopTexture = 8
};
#endif

struct SearchEdgeCommand
{
    SearchEdgeOperation operation;
    std::size_t edgeIndex;
    SearchEdgeTransformState transformState;
    std::size_t activeClipEdge;
    D2D1_RECT_F clip;
    D2D1_MATRIX_3X2_F transform;
};

struct SearchOpticalPoint
{
    float destinationXDip = 0.0f;
    float destinationYDip = 0.0f;
    SearchSample sample;
};

struct ContrastGuardCommand
{
    llp::geometry::Rect bounds;
    float radiusPx = 0.0f;
    float opacity = 0.0f;
};

struct MaterialFramePlan
{
    bool drawBlur = true;
    bool drawTint = true;
    bool drawClarityShape = true;
    bool drawRim = true;
    bool drawShadow = true;
    DipRect searchBoundsDip;
    llp::geometry::Rect searchBoundsPx;
    std::vector<SearchOpticalPoint> searchOpticalPoints;
    float clarityCenterOpacity = 0.0f;
    float clarityEdgeOpacity = 0.0f;
    std::vector<ContrastGuardCommand> contrastGuards;
};

struct DesktopSourceCommand
{
    PixelRect source;
    int uprightWidth = 0;
    int uprightHeight = 0;
    D2D1_MATRIX_3X2_F sourceToUpright = D2D1::Matrix3x2F::Identity();
};

struct AuxiliaryPresentationEvidence
{
    bool overlayDrawn = false;
    bool contentDrawn = false;
    bool rendered = false;
    std::uint64_t sceneGeneration = 0;
    llp::geometry::Rect bounds;
    std::wstring text;
    bool focused = false;
    std::vector<std::wstring> itemStableIds;
    std::vector<std::wstring> itemSourcePaths;
    std::uint64_t itemIdentityCopies = 0;
};

struct DevelopmentFreezeEvidence
{
    bool baselineCaptured = false;
    std::uint64_t baselineDesktopHash = 0;
    std::uint64_t baselineFrameIdentity = 0;
    std::uint64_t baselineSceneGeneration = 0;
    bool candidateCaptured = false;
    std::uint64_t candidateDesktopHash = 0;
    std::uint64_t candidateFrameIdentity = 0;
    bool freezeCommitSucceeded = false;
    std::uint64_t freezeDesktopHash = 0;
    std::uint64_t freezeFrameIdentity = 0;
    std::uint64_t freezeSceneGeneration = 0;
};

constexpr DevelopmentFreezeEvidence EmptyDevelopmentFreezeEvidence = {};

struct DiagnosticsSynchronization
{
    bool notifyHost = false;
    std::uint64_t stringCopies = 0;
    std::uint64_t vectorCopies = 0;
};

DiagnosticsSynchronization SynchronizeDiagnostics(
    Diagnostics& target,
    const Diagnostics& lifecycle,
    const AuxiliaryPresentationEvidence& auxiliary,
    const DevelopmentFreezeEvidence& evidence,
    bool developmentEvidence)
{
    DiagnosticsSynchronization synchronization;
    const bool evidenceChanged = developmentEvidence
        && (evidence.baselineCaptured != target.evidenceBaselineCaptured
            || evidence.candidateCaptured != target.evidenceCandidateCaptured
            || evidence.freezeCommitSucceeded != target.evidenceFreezeCommitSucceeded);
    synchronization.notifyHost = lifecycle.path != target.path
        || lifecycle.failure != target.failure
        || lifecycle.state != target.state
        || lifecycle.deviceRebuilds != target.deviceRebuilds
        || lifecycle.evidenceFrozen != target.evidenceFrozen
        || auxiliary.rendered != target.auxiliaryRendered
        || auxiliary.sceneGeneration != target.auxiliarySceneGeneration
        || auxiliary.bounds.left != target.auxiliaryBounds.left
        || auxiliary.bounds.top != target.auxiliaryBounds.top
        || auxiliary.bounds.right != target.auxiliaryBounds.right
        || auxiliary.bounds.bottom != target.auxiliaryBounds.bottom
        || auxiliary.text != target.auxiliaryText
        || auxiliary.focused != target.auxiliaryFocused
        || lifecycle.committedActivation != target.committedActivation
        || lifecycle.presentedSceneGeneration != target.presentedSceneGeneration
        || evidenceChanged;

    const auto assignString = [&synchronization](std::wstring& destination,
                                                  const std::wstring& source) {
        if (destination == source) return;
        destination = source;
        ++synchronization.stringCopies;
    };
    assignString(target.adapter, lifecycle.adapter);
    assignString(target.output, lifecycle.output);
    assignString(target.auxiliaryText, auxiliary.text);

    target.path = lifecycle.path;
    target.failure = lifecycle.failure;
    target.state = lifecycle.state;
    target.dpi = lifecycle.dpi;
    target.reducedMotion = lifecycle.reducedMotion;
    target.acquiredFrames = lifecycle.acquiredFrames;
    target.presentedFrames = lifecycle.presentedFrames;
    target.deviceRebuilds = lifecycle.deviceRebuilds;
    target.lastFrameIdentity = lifecycle.lastFrameIdentity;
    target.firstFrameMs = lifecycle.firstFrameMs;
    target.frameP95Ms = lifecycle.frameP95Ms;
    target.evidenceFrozen = lifecycle.evidenceFrozen;
    target.auxiliaryRendered = auxiliary.rendered;
    target.auxiliarySceneGeneration = auxiliary.sceneGeneration;
    target.auxiliaryBounds = auxiliary.bounds;
    target.auxiliaryFocused = auxiliary.focused;
    target.committedActivation = lifecycle.committedActivation;
    target.presentedSceneGeneration = lifecycle.presentedSceneGeneration;
    target.presentationEvidenceItemCopies = developmentEvidence
        ? auxiliary.itemIdentityCopies
        : 0;

    const auto assignVector = [&synchronization](
                                  std::vector<std::wstring>& destination,
                                  const std::vector<std::wstring>& source) {
        if (destination == source) return;
        destination = source;
        ++synchronization.vectorCopies;
    };
    if (developmentEvidence && auxiliary.itemIdentityCopies > 0)
    {
        assignVector(target.presentedItemStableIds, auxiliary.itemStableIds);
        assignVector(target.presentedItemSourcePaths, auxiliary.itemSourcePaths);
    }
    else
    {
        if (!target.presentedItemStableIds.empty())
        {
            target.presentedItemStableIds.clear();
            ++synchronization.vectorCopies;
        }
        if (!target.presentedItemSourcePaths.empty())
        {
            target.presentedItemSourcePaths.clear();
            ++synchronization.vectorCopies;
        }
    }

    target.evidenceBaselineCaptured = developmentEvidence && evidence.baselineCaptured;
    target.evidenceBaselineDesktopHash = developmentEvidence ? evidence.baselineDesktopHash : 0;
    target.evidenceBaselineFrameIdentity = developmentEvidence ? evidence.baselineFrameIdentity : 0;
    target.evidenceBaselineSceneGeneration = developmentEvidence ? evidence.baselineSceneGeneration : 0;
    target.evidenceCandidateCaptured = developmentEvidence && evidence.candidateCaptured;
    target.evidenceCandidateDesktopHash = developmentEvidence ? evidence.candidateDesktopHash : 0;
    target.evidenceCandidateFrameIdentity = developmentEvidence ? evidence.candidateFrameIdentity : 0;
    target.evidenceFreezeCommitSucceeded = developmentEvidence && evidence.freezeCommitSucceeded;
    target.evidenceFreezeDesktopHash = developmentEvidence ? evidence.freezeDesktopHash : 0;
    target.evidenceFreezeFrameIdentity = developmentEvidence ? evidence.freezeFrameIdentity : 0;
    target.evidenceFreezeSceneGeneration = developmentEvidence ? evidence.freezeSceneGeneration : 0;
    target.queuedSceneGeneration = lifecycle.queuedSceneGeneration;
    target.currentSceneGeneration = lifecycle.currentSceneGeneration;
    target.showQueued = lifecycle.showQueued;
    target.rebuildQueued = lifecycle.rebuildQueued;
    target.composeLifetimeProbeEntered = lifecycle.composeLifetimeProbeEntered;
    target.composeLifetimeProbeCompleted = lifecycle.composeLifetimeProbeCompleted;
    target.composeSceneExpiredDuringHide = lifecycle.composeSceneExpiredDuringHide;
    return synchronization;
}

using LuminanceGrid = std::array<float, 16 * 12>;

bool IsValidRect(llp::geometry::Rect rect)
{
    return rect.right > rect.left && rect.bottom > rect.top;
}

float LocalGuardOpacity(
    llp::geometry::Rect bounds,
    int windowWidth,
    int windowHeight,
    const LuminanceGrid& luminanceGrid,
    const MaterialTone& tone)
{
    const float baseOpacity = std::clamp(
        0.12f + tone.tintOpacity * 0.50f, 0.16f, 0.24f);
    if (windowWidth <= 0 || windowHeight <= 0 || !IsValidRect(bounds))
    {
        return baseOpacity;
    }

    const int left = std::clamp(bounds.left, 0, windowWidth);
    const int top = std::clamp(bounds.top, 0, windowHeight);
    const int right = std::clamp(bounds.right, 0, windowWidth);
    const int bottom = std::clamp(bounds.bottom, 0, windowHeight);
    if (right <= left || bottom <= top) return baseOpacity;

    const int firstColumn = static_cast<int>(std::clamp(
        static_cast<std::int64_t>(left) * 16 / windowWidth,
        static_cast<std::int64_t>(0), static_cast<std::int64_t>(15)));
    const int lastColumn = static_cast<int>(std::clamp(
        (static_cast<std::int64_t>(right) * 16 + windowWidth - 1) / windowWidth - 1,
        static_cast<std::int64_t>(0), static_cast<std::int64_t>(15)));
    const int firstRow = static_cast<int>(std::clamp(
        static_cast<std::int64_t>(top) * 12 / windowHeight,
        static_cast<std::int64_t>(0), static_cast<std::int64_t>(11)));
    const int lastRow = static_cast<int>(std::clamp(
        (static_cast<std::int64_t>(bottom) * 12 + windowHeight - 1) / windowHeight - 1,
        static_cast<std::int64_t>(0), static_cast<std::int64_t>(11)));
    if (lastColumn < firstColumn || lastRow < firstRow) return baseOpacity;

    double total = 0.0;
    int count = 0;
    for (int row = firstRow; row <= lastRow; ++row)
    {
        for (int column = firstColumn; column <= lastColumn; ++column)
        {
            const float value = luminanceGrid[static_cast<std::size_t>(row * 16 + column)];
            total += std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.5f;
            ++count;
        }
    }
    if (count == 0) return baseOpacity;
    const float localMean = static_cast<float>(total / count);
    return std::clamp(
        baseOpacity + (0.5f - localMean) * tone.tintOpacity,
        0.16f,
        0.24f);
}

MaterialFramePlan BuildMaterialFramePlan(
    const SceneSnapshot& scene,
    const MaterialTone& tone,
    const LuminanceGrid& luminanceGrid,
    int windowWidth,
    int windowHeight)
{
    MaterialFramePlan plan;
    plan.clarityCenterOpacity = std::clamp(0.04f + tone.tintOpacity * 0.20f, 0.04f, 0.08f);
    plan.clarityEdgeOpacity = std::clamp(plan.clarityCenterOpacity + 0.08f, 0.12f, 0.20f);
    const int dpi = std::max(1, scene.metrics.dpi);
    const float dipPerPixel = 96.0f / static_cast<float>(dpi);
    const auto& search = scene.metrics.search;
    if (IsValidRect(search))
    {
        plan.searchBoundsPx = search;
        plan.searchBoundsDip = {
            search.left * dipPerPixel,
            search.top * dipPerPixel,
            search.right * dipPerPixel,
            search.bottom * dipPerPixel
        };
        const float centerX = (plan.searchBoundsDip.left + plan.searchBoundsDip.right) * 0.5f;
        const float centerY = (plan.searchBoundsDip.top + plan.searchBoundsDip.bottom) * 0.5f;
        const std::array<std::pair<float, float>, 5> destinations = { {
            { centerX, centerY },
            { plan.searchBoundsDip.left, centerY },
            { plan.searchBoundsDip.right, centerY },
            { centerX, plan.searchBoundsDip.top },
            { centerX, plan.searchBoundsDip.bottom }
        } };
        for (const auto& destination : destinations)
        {
            plan.searchOpticalPoints.push_back({
                destination.first,
                destination.second,
                ComputeSearchSample(
                    plan.searchBoundsDip,
                    destination.first,
                    destination.second,
                    dpi,
                    scene.policy.reducedMotion)
            });
        }
    }

    if (IsValidRect(scene.metrics.searchText))
    {
        plan.contrastGuards.push_back({
            scene.metrics.searchText,
            static_cast<float>(std::max(1, (scene.metrics.searchText.bottom - scene.metrics.searchText.top) / 2)),
            LocalGuardOpacity(
                scene.metrics.searchText, windowWidth, windowHeight, luminanceGrid, tone)
        });
    }
    for (const auto& item : scene.items)
    {
        if (!item.focused || !IsValidRect(item.bounds)) continue;
        plan.contrastGuards.push_back({
            item.bounds,
            18.0f,
            LocalGuardOpacity(item.bounds, windowWidth, windowHeight, luminanceGrid, tone)
        });
    }
    return plan;
}

class CompositionBackend
{
public:
    virtual ~CompositionBackend() = default;
    virtual FailureReason EnsureTree(const ShowRequest& request) = 0;
    virtual FailureReason ClearTransparent() = 0;
    virtual FailureReason SetDesktopInput(PixelRect crop, OutputRotation rotation) = 0;
    virtual FailureReason UpdateLuminance(
        int width,
        int height,
        int stagingBufferCount,
        UINT mapFlags,
        std::uint32_t minimumIntervalMs) = 0;
    virtual float RelativeLuminance() const = 0;
    virtual const LuminanceGrid& CurrentLuminanceGrid() const = 0;
    virtual FailureReason DrawMaterial(
        const SceneSnapshot& scene,
        const MaterialTone& tone,
        const MaterialFramePlan& plan) = 0;
    virtual FailureReason DrawOverlay(const SceneSnapshot& scene) = 0;
    virtual FailureReason DrawContent(const SceneSnapshot& scene) = 0;
    virtual FailureReason Commit() = 0;
};

FailureReason ComposeUnifiedFrame(
    CompositionBackend& backend,
    const ShowRequest& request,
    const SceneSnapshot* submittedScene,
    PixelRect outputDesktop,
    OutputRotation rotation,
    bool desktopFrameChanged,
    bool sceneChanged,
    bool lifecycleEventChanged,
    bool& presented)
{
    presented = false;
    if (!desktopFrameChanged && !sceneChanged && !lifecycleEventChanged)
    {
        return FailureReason::None;
    }

    SceneSnapshot emptyScene;
    emptyScene.policy = request.policy;
    emptyScene.metrics.dpi = request.dpi;
    emptyScene.metrics.cornerRadiusPx = std::max(1, 34 * std::max(1, request.dpi) / 96);
    const SceneSnapshot& scene = submittedScene ? *submittedScene : emptyScene;
    FailureReason failure = backend.EnsureTree(request);
    if (failure != FailureReason::None) return failure;
    failure = backend.ClearTransparent();
    if (failure != FailureReason::None) return failure;
    const PixelRect crop = MapWindowCrop(outputDesktop, request.windowDesktop, rotation);
    if (crop.right <= crop.left || crop.bottom <= crop.top)
    {
        return FailureReason::DuplicationUnavailable;
    }
    failure = backend.SetDesktopInput(crop, rotation);
    if (failure != FailureReason::None) return failure;
    if (desktopFrameChanged)
    {
        failure = backend.UpdateLuminance(16, 12, 2, D3D11_MAP_FLAG_DO_NOT_WAIT, 100);
        if (failure != FailureReason::None) return failure;
    }
    const MaterialTone tone = ResolveMaterialTone(
        backend.RelativeLuminance(), scene.policy.darkMode);
    const MaterialFramePlan materialPlan = BuildMaterialFramePlan(
        scene,
        tone,
        backend.CurrentLuminanceGrid(),
        std::max(0, request.windowDesktop.right - request.windowDesktop.left),
        std::max(0, request.windowDesktop.bottom - request.windowDesktop.top));
    failure = backend.DrawMaterial(scene, tone, materialPlan);
    if (failure != FailureReason::None) return failure;
    failure = backend.DrawOverlay(scene);
    if (failure != FailureReason::None) return failure;
    failure = backend.DrawContent(scene);
    if (failure != FailureReason::None) return failure;
    failure = backend.Commit();
    if (failure != FailureReason::None) return failure;
    presented = true;
    return FailureReason::None;
}

D2D1_COLOR_F D2DColor(llp::adaptive::Color color, float opacity = 1.0f)
{
    return D2D1::ColorF(
        static_cast<float>(color.red) / 255.0f,
        static_cast<float>(color.green) / 255.0f,
        static_cast<float>(color.blue) / 255.0f,
        std::clamp(opacity, 0.0f, 1.0f));
}

D2D1_RECT_F D2DRect(llp::geometry::Rect rect)
{
    return D2D1::RectF(
        static_cast<float>(rect.left),
        static_cast<float>(rect.top),
        static_cast<float>(rect.right),
        static_cast<float>(rect.bottom));
}

bool IsDeviceLossResult(HRESULT result)
{
    return result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET;
}

FailureReason ClassifiedResult(HRESULT result, FailureReason genericFailure)
{
    if (SUCCEEDED(result)) return FailureReason::None;
    return IsDeviceLossResult(result) ? FailureReason::DeviceRemoved : genericFailure;
}

FailureReason RequiredResourceResult(HRESULT result)
{
    return ClassifiedResult(result, FailureReason::CompositionUnavailable);
}

#if defined(LLP_MATERIAL_TESTING)
HRESULT InjectRequiredResourceFailure(
    RequiredResourceOperation selected,
    RequiredResourceOperation operation,
    HRESULT result,
    int& hitCount)
{
    if (selected == operation && SUCCEEDED(result))
    {
        ++hitCount;
        if (operation == RequiredResourceOperation::CompositionCommit
            || operation == RequiredResourceOperation::CompositionTarget
            || operation == RequiredResourceOperation::CaptureDesktopTexture)
        {
            return DXGI_ERROR_DEVICE_REMOVED;
        }
        if (operation == RequiredResourceOperation::CompositionWait
            || operation == RequiredResourceOperation::CompositionBeginDraw)
        {
            return DXGI_ERROR_DEVICE_RESET;
        }
        return E_FAIL;
    }
    return result;
}
#endif

D2D1_MATRIX_3X2_F SurfaceAtlasTransform(POINT offset)
{
    return D2D1::Matrix3x2F::Translation(
        static_cast<float>(offset.x), static_cast<float>(offset.y));
}

D2D1_MATRIX_3X2_F CropTransform(PixelRect crop, OutputRotation rotation)
{
    const float width = static_cast<float>(crop.right - crop.left);
    const float height = static_cast<float>(crop.bottom - crop.top);
    D2D1::Matrix3x2F transform = D2D1::Matrix3x2F::Translation(
        static_cast<float>(-crop.left), static_cast<float>(-crop.top));
    switch (rotation)
    {
    case OutputRotation::Rotate90:
        transform = transform * D2D1::Matrix3x2F::Rotation(90.0f)
            * D2D1::Matrix3x2F::Translation(height, 0.0f);
        break;
    case OutputRotation::Rotate180:
        transform = transform * D2D1::Matrix3x2F::Rotation(180.0f)
            * D2D1::Matrix3x2F::Translation(width, height);
        break;
    case OutputRotation::Rotate270:
        transform = transform * D2D1::Matrix3x2F::Rotation(270.0f)
            * D2D1::Matrix3x2F::Translation(0.0f, width);
        break;
    default:
        break;
    }
    return transform;
}

DesktopSourceCommand BuildDesktopSourceCommand(PixelRect crop, OutputRotation rotation)
{
    const int sourceWidth = std::max(0, crop.right - crop.left);
    const int sourceHeight = std::max(0, crop.bottom - crop.top);
    const bool quarterTurn = rotation == OutputRotation::Rotate90
        || rotation == OutputRotation::Rotate270;
    return {
        crop,
        quarterTurn ? sourceHeight : sourceWidth,
        quarterTurn ? sourceWidth : sourceHeight,
        CropTransform(crop, rotation)
    };
}

template<typename Image>
void DrawDesktopSource(
    ID2D1DeviceContext* context,
    Image* image,
    const DesktopSourceCommand& command,
    const D2D1_MATRIX_3X2_F& uprightToTarget)
{
    context->SetTransform(command.sourceToUpright * uprightToTarget);
    context->DrawImage(
        image,
        D2D1::Point2F(
            static_cast<float>(command.source.left),
            static_cast<float>(command.source.top)),
        D2D1::RectF(
            static_cast<float>(command.source.left),
            static_cast<float>(command.source.top),
            static_cast<float>(command.source.right),
            static_cast<float>(command.source.bottom)),
        D2D1_INTERPOLATION_MODE_LINEAR,
        D2D1_COMPOSITE_MODE_SOURCE_COPY);
}

D2D1_POINT_2F TransformPoint(D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F& transform)
{
    return D2D1::Point2F(
        point.x * transform._11 + point.y * transform._21 + transform._31,
        point.x * transform._12 + point.y * transform._22 + transform._32);
}

template<typename Submit>
void SubmitSearchEdgeCommands(
    const MaterialFramePlan& plan,
    int dpi,
    const D2D1_MATRIX_3X2_F& cropTransform,
    const D2D1_MATRIX_3X2_F& atlasTransform,
    Submit&& submit)
{
    const float edgeBandPx = std::max(
        1.0f, 13.0f * static_cast<float>(std::max(1, dpi)) / 96.0f);
    const std::array<D2D1_RECT_F, 4> edgeBands = { {
        D2D1::RectF(
            static_cast<float>(plan.searchBoundsPx.left),
            static_cast<float>(plan.searchBoundsPx.top),
            std::min(static_cast<float>(plan.searchBoundsPx.right),
                static_cast<float>(plan.searchBoundsPx.left) + edgeBandPx),
            static_cast<float>(plan.searchBoundsPx.bottom)),
        D2D1::RectF(
            std::max(static_cast<float>(plan.searchBoundsPx.left),
                static_cast<float>(plan.searchBoundsPx.right) - edgeBandPx),
            static_cast<float>(plan.searchBoundsPx.top),
            static_cast<float>(plan.searchBoundsPx.right),
            static_cast<float>(plan.searchBoundsPx.bottom)),
        D2D1::RectF(
            static_cast<float>(plan.searchBoundsPx.left),
            static_cast<float>(plan.searchBoundsPx.top),
            static_cast<float>(plan.searchBoundsPx.right),
            std::min(static_cast<float>(plan.searchBoundsPx.bottom),
                static_cast<float>(plan.searchBoundsPx.top) + edgeBandPx)),
        D2D1::RectF(
            static_cast<float>(plan.searchBoundsPx.left),
            std::max(static_cast<float>(plan.searchBoundsPx.top),
                static_cast<float>(plan.searchBoundsPx.bottom) - edgeBandPx),
            static_cast<float>(plan.searchBoundsPx.right),
            static_cast<float>(plan.searchBoundsPx.bottom))
    } };
    const float pixelsPerDip = static_cast<float>(std::max(1, dpi)) / 96.0f;
    for (std::size_t edge = 0; edge < edgeBands.size(); ++edge)
    {
        const auto& point = plan.searchOpticalPoints[edge + 1];
        const float deltaX = (point.sample.sourceXDip - point.destinationXDip) * pixelsPerDip;
        const float deltaY = (point.sample.sourceYDip - point.destinationYDip) * pixelsPerDip;
        const D2D1_MATRIX_3X2_F displacedTransform = cropTransform
            * D2D1::Matrix3x2F::Translation(-deltaX, -deltaY)
            * atlasTransform;
        const auto emit = [&](SearchEdgeOperation operation,
                              SearchEdgeTransformState transformState,
                              std::size_t activeClipEdge,
                              const D2D1_MATRIX_3X2_F& transform) {
            submit(SearchEdgeCommand{
                operation, edge, transformState, activeClipEdge, edgeBands[edge], transform
            });
        };
        emit(SearchEdgeOperation::SetAtlasBeforeClip,
            SearchEdgeTransformState::Atlas, 0, atlasTransform);
        emit(SearchEdgeOperation::PushClip,
            SearchEdgeTransformState::Atlas, edge + 1, atlasTransform);
        emit(SearchEdgeOperation::SetDisplacedTransform,
            SearchEdgeTransformState::Displaced, edge + 1, displacedTransform);
        emit(SearchEdgeOperation::DrawImage,
            SearchEdgeTransformState::Displaced, edge + 1, displacedTransform);
        emit(SearchEdgeOperation::PopClip,
            SearchEdgeTransformState::Displaced, edge + 1, displacedTransform);
        emit(SearchEdgeOperation::RestoreAtlas,
            SearchEdgeTransformState::Atlas, 0, atlasTransform);
    }
}

FailureReason InitializeDevice(
    DeviceResources& resources,
    std::uint32_t* selectedFeatureLevel,
    IDXGIAdapter* selectedAdapter = nullptr)
{
    DeviceResources candidate;
    constexpr std::array<D3D_FEATURE_LEVEL, 6> requestedFeatureLevels = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;
    HRESULT result = D3D11CreateDevice(
        selectedAdapter,
        selectedAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT
            | D3D11_CREATE_DEVICE_SINGLETHREADED
            | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,
        requestedFeatureLevels.data(),
        static_cast<UINT>(requestedFeatureLevels.size()),
        D3D11_SDK_VERSION,
        candidate.d3dDevice.Put(),
        &featureLevel,
        candidate.d3dContext.Put());
    FailureReason failure = ClassifiedResult(result, FailureReason::AdapterUnavailable);
    if (failure != FailureReason::None) return failure;

    result = candidate.d3dDevice.Get()->QueryInterface(
        __uuidof(IDXGIDevice), candidate.dxgiDevice.PutVoid());
    failure = ClassifiedResult(result, FailureReason::AdapterUnavailable);
    if (failure != FailureReason::None) return failure;

    result = D2D1CreateDevice(candidate.dxgiDevice.Get(), nullptr, candidate.d2dDevice.Put());
    failure = RequiredResourceResult(result);
    if (failure != FailureReason::None) return failure;

    result = candidate.d2dDevice.Get()->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE, candidate.d2dContext.Put());
    failure = RequiredResourceResult(result);
    if (failure != FailureReason::None) return failure;

    result = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(candidate.dwriteFactory.Put()));
    failure = RequiredResourceResult(result);
    if (failure != FailureReason::None) return failure;

    result = DCompositionCreateDevice(
        candidate.dxgiDevice.Get(),
        __uuidof(IDCompositionDevice),
        candidate.compositionDevice.PutVoid());
    failure = RequiredResourceResult(result);
    if (failure != FailureReason::None) return failure;

    if (selectedFeatureLevel)
    {
        *selectedFeatureLevel = static_cast<std::uint32_t>(featureLevel);
    }
    resources = std::move(candidate);
    return FailureReason::None;
}

bool IsDevelopmentBuild()
{
    return CompareStringOrdinal(
        LLP_BUILD_CHANNEL_WIDE, -1, L"development", -1, TRUE) == CSTR_EQUAL;
}

bool FullyContains(PixelRect outer, PixelRect inner)
{
    return inner.right > inner.left && inner.bottom > inner.top
        && inner.left >= outer.left && inner.top >= outer.top
        && inner.right <= outer.right && inner.bottom <= outer.bottom;
}

struct OutputCatalogEntry
{
    UINT adapterIndex = 0;
    UINT outputIndex = 0;
    HMONITOR monitor = nullptr;
    PixelRect desktop;
};

struct OutputSelection
{
    UINT adapterIndex = 0;
    UINT outputIndex = 0;
    PixelRect desktop;
};

enum class OutputSelectionResult { Selected, MonitorNotFound, WindowNotContained };

OutputRotation NormalizeOutputRotation(DXGI_MODE_ROTATION rotation)
{
    switch (rotation)
    {
    case DXGI_MODE_ROTATION_ROTATE90: return OutputRotation::Rotate90;
    case DXGI_MODE_ROTATION_ROTATE180: return OutputRotation::Rotate180;
    case DXGI_MODE_ROTATION_ROTATE270: return OutputRotation::Rotate270;
    default: return OutputRotation::Identity;
    }
}

OutputSelectionResult SelectCaptureOutput(
    const std::vector<OutputCatalogEntry>& catalog,
    HMONITOR monitor,
    PixelRect windowDesktop,
    OutputSelection& selection)
{
    bool monitorFound = false;
    for (const auto& candidate : catalog)
    {
        if (candidate.monitor != monitor) continue;
        monitorFound = true;
        if (!FullyContains(candidate.desktop, windowDesktop)) continue;
        selection.adapterIndex = candidate.adapterIndex;
        selection.outputIndex = candidate.outputIndex;
        selection.desktop = candidate.desktop;
        return OutputSelectionResult::Selected;
    }
    return monitorFound
        ? OutputSelectionResult::WindowNotContained
        : OutputSelectionResult::MonitorNotFound;
}

enum class AcquireResult { Frame, Timeout, AccessLost, DeviceRemoved, Failed };
enum class ReleaseResult { Released, AccessLost, DeviceRemoved, Failed };

class CaptureBackend
{
public:
    virtual ~CaptureBackend() = default;
    virtual FailureReason SetCaptureExclusion(HWND window) = 0;
    virtual void ClearCaptureExclusion() = 0;
    virtual FailureReason Start(const ShowRequest& request) = 0;
    virtual const std::wstring& AdapterDescription() const = 0;
    virtual const std::wstring& OutputDeviceName() const = 0;
    virtual AcquireResult Acquire(std::uint32_t waitMilliseconds, std::uint64_t& identity) = 0;
    virtual ReleaseResult ReleaseFrame() = 0;
    virtual FailureReason Compose(
        const ShowRequest& request,
        const SceneSnapshot* scene,
        bool desktopFrameChanged,
        bool sceneChanged,
        bool lifecycleEventChanged,
        bool& presented) = 0;
    virtual void Freeze() = 0;
    virtual void Stop() = 0;
    virtual std::int64_t Counter() const = 0;
    virtual std::int64_t Frequency() const = 0;
    virtual bool SnapshotCommittedEvidenceForDevelopment() { return false; }
    virtual bool SnapshotCandidateEvidenceForDevelopment() { return false; }
};

class DesktopDuplicationBackend final : public CaptureBackend, public CompositionBackend
{
public:
#if defined(LLP_MATERIAL_TESTING)
    void InjectFailure(RequiredResourceOperation operation)
    {
        injectedFailure_ = operation;
        injectedFailureHitCount_ = 0;
    }

    int InjectedFailureHitCount() const { return injectedFailureHitCount_; }
    int CommitCallCount() const { return commitCallCount_; }
#endif

    const AuxiliaryPresentationEvidence& LastPresentedAuxiliary() const
    {
        return lastPresentedAuxiliary_;
    }

    const DevelopmentFreezeEvidence& DevelopmentEvidence() const
    {
        return developmentFreezeEvidence_;
    }

    bool SnapshotCommittedEvidenceForDevelopment() override
    {
        if (!developmentEvidence_ || !committedDesktop_.Get()) return false;
        DevelopmentFreezeEvidence evidence = {};
        if (!HashDesktopTexture(committedDesktop_.Get(), evidence.baselineDesktopHash))
        {
            return false;
        }
        evidence.baselineFrameIdentity = committedDesktopIdentity_;
        evidence.baselineSceneGeneration = lastSceneGeneration_;
        evidence.baselineCaptured = evidence.baselineDesktopHash != 0
            && evidence.baselineFrameIdentity != 0
            && evidence.baselineSceneGeneration != std::numeric_limits<std::uint64_t>::max();
        developmentFreezeEvidence_ = evidence;
        return evidence.baselineCaptured;
    }

    bool SnapshotCandidateEvidenceForDevelopment() override
    {
        if (!developmentEvidence_ || !developmentFreezeEvidence_.baselineCaptured
            || !candidateDesktopValid_ || !candidateDesktop_.Get()) return false;
        std::uint64_t hash = 0;
        if (!HashDesktopTexture(candidateDesktop_.Get(), hash)) return false;
        developmentFreezeEvidence_.candidateDesktopHash = hash;
        developmentFreezeEvidence_.candidateFrameIdentity = candidateDesktopIdentity_;
        developmentFreezeEvidence_.candidateCaptured = hash != 0
            && candidateDesktopIdentity_ != 0;
        return developmentFreezeEvidence_.candidateCaptured;
    }

    ~DesktopDuplicationBackend() override
    {
        Stop();
        ClearCaptureExclusion();
    }

    FailureReason SetCaptureExclusion(HWND window) override
    {
        ClearCaptureExclusion();
        if (!window) return FailureReason::DisplayAffinityUnavailable;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        using SetAffinityFn = BOOL(WINAPI*)(HWND, DWORD);
        using GetAffinityFn = BOOL(WINAPI*)(HWND, DWORD*);
        setAffinity_ = reinterpret_cast<SetAffinityFn>(
            GetProcAddress(user32, "SetWindowDisplayAffinity"));
        getAffinity_ = reinterpret_cast<GetAffinityFn>(
            GetProcAddress(user32, "GetWindowDisplayAffinity"));
        if (!setAffinity_ || !getAffinity_) return FailureReason::UnsupportedOs;
        if (!setAffinity_(window, WDA_EXCLUDEFROMCAPTURE))
        {
            return FailureReason::DisplayAffinityUnavailable;
        }
        DWORD effective = WDA_NONE;
        if (!getAffinity_(window, &effective) || effective != WDA_EXCLUDEFROMCAPTURE)
        {
            setAffinity_(window, WDA_NONE);
            return FailureReason::UnsupportedOs;
        }
        affinityWindow_ = window;
        return FailureReason::None;
    }

    void ClearCaptureExclusion() override
    {
        if (affinityWindow_ && setAffinity_) setAffinity_(affinityWindow_, WDA_NONE);
        affinityWindow_ = nullptr;
    }

    FailureReason Start(const ShowRequest& request) override
    {
        Stop();
        developmentEvidence_ = request.developmentEvidence;
        if (!request.monitor) return FailureReason::AdapterUnavailable;

        ComOwner<IDXGIFactory1> factory;
        FailureReason failure = ClassifiedResult(
            CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.PutVoid()),
            FailureReason::AdapterUnavailable);
        if (failure != FailureReason::None)
        {
            return failure;
        }

        std::vector<OutputCatalogEntry> catalog;
        for (UINT adapterIndex = 0;; ++adapterIndex)
        {
            ComOwner<IDXGIAdapter1> adapter;
            const HRESULT adapterResult = factory.Get()->EnumAdapters1(adapterIndex, adapter.Put());
            if (adapterResult == DXGI_ERROR_NOT_FOUND) break;
            failure = ClassifiedResult(adapterResult, FailureReason::AdapterUnavailable);
            if (failure != FailureReason::None) return failure;
            for (UINT outputIndex = 0;; ++outputIndex)
            {
                ComOwner<IDXGIOutput> output;
                const HRESULT outputResult = adapter.Get()->EnumOutputs(outputIndex, output.Put());
                if (outputResult == DXGI_ERROR_NOT_FOUND) break;
                failure = ClassifiedResult(outputResult, FailureReason::AdapterUnavailable);
                if (failure != FailureReason::None) return failure;
                DXGI_OUTPUT_DESC description = {};
                const HRESULT descriptionResult = output.Get()->GetDesc(&description);
                if (IsDeviceLossResult(descriptionResult)) return FailureReason::DeviceRemoved;
                if (SUCCEEDED(descriptionResult))
                {
                    catalog.push_back({
                        adapterIndex,
                        outputIndex,
                        description.Monitor,
                        {
                            description.DesktopCoordinates.left,
                            description.DesktopCoordinates.top,
                            description.DesktopCoordinates.right,
                            description.DesktopCoordinates.bottom
                        }
                    });
                }
            }
        }

        OutputSelection selection;
        const OutputSelectionResult selectionResult = SelectCaptureOutput(
            catalog, request.monitor, request.windowDesktop, selection);
        if (selectionResult == OutputSelectionResult::MonitorNotFound)
        {
            return FailureReason::AdapterUnavailable;
        }
        if (selectionResult == OutputSelectionResult::WindowNotContained)
        {
            return FailureReason::DuplicationUnavailable;
        }

        ComOwner<IDXGIAdapter1> selectedAdapter;
        failure = ClassifiedResult(
            factory.Get()->EnumAdapters1(selection.adapterIndex, selectedAdapter.Put()),
            FailureReason::AdapterUnavailable);
        if (failure != FailureReason::None)
        {
            return failure;
        }
        ComOwner<IDXGIOutput> selectedOutput;
        failure = ClassifiedResult(
            selectedAdapter.Get()->EnumOutputs(selection.outputIndex, selectedOutput.Put()),
            FailureReason::AdapterUnavailable);
        if (failure != FailureReason::None)
        {
            return failure;
        }
        DXGI_OUTPUT_DESC selectedDescription = {};
        failure = ClassifiedResult(
            selectedOutput.Get()->GetDesc(&selectedDescription),
            FailureReason::AdapterUnavailable);
        if (failure != FailureReason::None)
        {
            return failure;
        }
        DXGI_ADAPTER_DESC1 selectedAdapterDescription = {};
        failure = ClassifiedResult(
            selectedAdapter.Get()->GetDesc1(&selectedAdapterDescription),
            FailureReason::AdapterUnavailable);
        if (failure != FailureReason::None)
        {
            return failure;
        }

        DeviceResources candidateResources;
        std::uint32_t featureLevel = 0;
        const FailureReason initialization = InitializeDevice(
            candidateResources, &featureLevel, selectedAdapter.Get());
        if (initialization != FailureReason::None) return initialization;

        ComOwner<IDXGIOutput1> output1;
        failure = ClassifiedResult(
            selectedOutput.Get()->QueryInterface(__uuidof(IDXGIOutput1), output1.PutVoid()),
            FailureReason::DuplicationUnavailable);
        if (failure != FailureReason::None)
        {
            return failure;
        }
        ComOwner<IDXGIOutputDuplication> duplication;
        const HRESULT duplicateResult = output1.Get()->DuplicateOutput(
            candidateResources.d3dDevice.Get(), duplication.Put());
        failure = ClassifiedResult(duplicateResult, FailureReason::DuplicationUnavailable);
        if (failure != FailureReason::None) return failure;

        factory_ = std::move(factory);
        adapter_ = std::move(selectedAdapter);
        output_ = std::move(selectedOutput);
        output1_ = std::move(output1);
        duplication_ = std::move(duplication);
        resources_ = std::move(candidateResources);
        outputDesktop_ = selection.desktop;
        outputRotation_ = NormalizeOutputRotation(selectedDescription.Rotation);
        adapterDescription_ = selectedAdapterDescription.Description;
        outputDeviceName_ = selectedDescription.DeviceName;
        return FailureReason::None;
    }

    const std::wstring& AdapterDescription() const override { return adapterDescription_; }
    const std::wstring& OutputDeviceName() const override { return outputDeviceName_; }

    AcquireResult Acquire(std::uint32_t waitMilliseconds, std::uint64_t& identity) override
    {
        if (!duplication_.Get()) return AcquireResult::Failed;
        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        ComOwner<IDXGIResource> desktopResource;
        const HRESULT result = duplication_.Get()->AcquireNextFrame(
            waitMilliseconds, &frameInfo, desktopResource.Put());
        if (result == DXGI_ERROR_WAIT_TIMEOUT) return AcquireResult::Timeout;
        if (result == DXGI_ERROR_ACCESS_LOST) return AcquireResult::AccessLost;
        if (IsDeviceLossResult(result))
        {
            return AcquireResult::DeviceRemoved;
        }
        if (FAILED(result)) return AcquireResult::Failed;
        frameAcquired_ = true;
        acquiredResource_ = std::move(desktopResource);
        ComOwner<ID3D11Texture2D> acquiredTexture;
        const HRESULT textureInterface = acquiredResource_.Get()->QueryInterface(
            __uuidof(ID3D11Texture2D), acquiredTexture.PutVoid());
        if (FAILED(textureInterface))
        {
            return IsDeviceLossResult(textureInterface)
                ? AcquireResult::DeviceRemoved
                : AcquireResult::Failed;
        }
        D3D11_TEXTURE2D_DESC sourceDescription = {};
        acquiredTexture.Get()->GetDesc(&sourceDescription);
        D3D11_TEXTURE2D_DESC candidateDescription = sourceDescription;
        candidateDescription.Usage = D3D11_USAGE_DEFAULT;
        candidateDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        candidateDescription.CPUAccessFlags = 0;
        candidateDescription.MiscFlags = 0;
        bool recreateCandidate = !candidateDesktop_.Get();
        if (!recreateCandidate)
        {
            D3D11_TEXTURE2D_DESC existing = {};
            candidateDesktop_.Get()->GetDesc(&existing);
            recreateCandidate = existing.Width != candidateDescription.Width
                || existing.Height != candidateDescription.Height
                || existing.Format != candidateDescription.Format;
        }
        if (recreateCandidate)
        {
            candidateDesktop_.Reset();
            HRESULT createCandidate = resources_.d3dDevice.Get()->CreateTexture2D(
                &candidateDescription, nullptr, candidateDesktop_.Put());
#if defined(LLP_MATERIAL_TESTING)
            createCandidate = InjectRequiredResourceFailure(
                injectedFailure_, RequiredResourceOperation::CaptureDesktopTexture,
                createCandidate, injectedFailureHitCount_);
#endif
            if (FAILED(createCandidate))
            {
                return IsDeviceLossResult(createCandidate)
                    ? AcquireResult::DeviceRemoved
                    : AcquireResult::Failed;
            }
        }
        resources_.d3dContext.Get()->CopyResource(candidateDesktop_.Get(), acquiredTexture.Get());
        candidateDesktopValid_ = true;
        identity = frameInfo.LastPresentTime.QuadPart != 0
            ? static_cast<std::uint64_t>(frameInfo.LastPresentTime.QuadPart)
            : static_cast<std::uint64_t>(Counter());
        if (developmentEvidence_) candidateDesktopIdentity_ = identity;
        return AcquireResult::Frame;
    }

    ReleaseResult ReleaseFrame() override
    {
        if (!frameAcquired_) return ReleaseResult::Released;
        if (!duplication_.Get())
        {
            frameAcquired_ = false;
            return ReleaseResult::Failed;
        }
        const HRESULT result = duplication_.Get()->ReleaseFrame();
        frameAcquired_ = false;
        acquiredResource_.Reset();
        if (SUCCEEDED(result)) return ReleaseResult::Released;
        if (result == DXGI_ERROR_ACCESS_LOST) return ReleaseResult::AccessLost;
        if (IsDeviceLossResult(result))
        {
            return ReleaseResult::DeviceRemoved;
        }
        return ReleaseResult::Failed;
    }

    FailureReason Compose(
        const ShowRequest& request,
        const SceneSnapshot* scene,
        bool desktopFrameChanged,
        bool sceneChanged,
        bool lifecycleEventChanged,
        bool& presented) override
    {
        const std::uint64_t generation = scene ? scene->generation : 0;
        const bool generationChanged = sceneChanged || generation != lastSceneGeneration_;
        if (developmentEvidence_ && lifecycleEventChanged)
        {
            developmentFreezeEvidence_.freezeCommitSucceeded = false;
            developmentFreezeEvidence_.freezeDesktopHash = 0;
            (void)HashDesktopTexture(
                DesktopForComposition(),
                developmentFreezeEvidence_.freezeDesktopHash);
            developmentFreezeEvidence_.freezeFrameIdentity = candidateDesktopValid_
                ? candidateDesktopIdentity_
                : committedDesktopIdentity_;
            developmentFreezeEvidence_.freezeSceneGeneration = generation;
        }
        const FailureReason result = ComposeUnifiedFrame(
            *this,
            request,
            scene,
            outputDesktop_,
            outputRotation_,
            desktopFrameChanged,
            generationChanged,
            lifecycleEventChanged,
            presented);
        if (result == FailureReason::None && presented)
        {
            lastSceneGeneration_ = generation;
            if (desktopFrameChanged) PromoteDesktopForEvidence();
            if (developmentEvidence_ && lifecycleEventChanged)
            {
                developmentFreezeEvidence_.freezeCommitSucceeded = true;
            }
        }
        return result;
    }

    FailureReason EnsureTree(const ShowRequest& request) override
    {
        RECT client = {};
        if (!request.window || !GetClientRect(request.window, &client))
        {
            return FailureReason::CompositionUnavailable;
        }
        const UINT width = static_cast<UINT>(std::max<LONG>(0, client.right - client.left));
        const UINT height = static_cast<UINT>(std::max<LONG>(0, client.bottom - client.top));
        if (width == 0 || height == 0) return FailureReason::CompositionUnavailable;
        if (compositionTarget_.Get() && width == surfaceWidth_ && height == surfaceHeight_)
        {
            return FailureReason::None;
        }

        ResetComposition();
        HRESULT result = resources_.compositionDevice.Get()->CreateTargetForHwnd(
            request.window, TRUE, compositionTarget_.Put());
#if defined(LLP_MATERIAL_TESTING)
        result = InjectRequiredResourceFailure(
            injectedFailure_, RequiredResourceOperation::CompositionTarget,
            result, injectedFailureHitCount_);
#endif
        FailureReason failure = RequiredResourceResult(result);
        if (failure != FailureReason::None)
        {
            ResetComposition();
            return failure;
        }
        failure = RequiredResourceResult(
            resources_.compositionDevice.Get()->CreateVisual(rootVisual_.Put()));
        if (failure != FailureReason::None)
        {
            ResetComposition();
            return failure;
        }
        for (std::size_t index = 0; index < visuals_.size(); ++index)
        {
            failure = RequiredResourceResult(
                resources_.compositionDevice.Get()->CreateVisual(visuals_[index].Put()));
            if (failure == FailureReason::None)
            {
                failure = RequiredResourceResult(resources_.compositionDevice.Get()->CreateSurface(
                    width,
                    height,
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    DXGI_ALPHA_MODE_PREMULTIPLIED,
                    surfaces_[index].Put()));
            }
            if (failure == FailureReason::None)
            {
                failure = RequiredResourceResult(
                    visuals_[index].Get()->SetContent(surfaces_[index].Get()));
            }
            if (failure != FailureReason::None)
            {
                ResetComposition();
                return failure;
            }
        }
        failure = RequiredResourceResult(
            rootVisual_.Get()->AddVisual(visuals_[0].Get(), FALSE, nullptr));
        if (failure == FailureReason::None)
        {
            failure = RequiredResourceResult(
                rootVisual_.Get()->AddVisual(visuals_[1].Get(), TRUE, visuals_[0].Get()));
        }
        if (failure == FailureReason::None)
        {
            failure = RequiredResourceResult(
                rootVisual_.Get()->AddVisual(visuals_[2].Get(), TRUE, visuals_[1].Get()));
        }
        if (failure == FailureReason::None)
        {
            failure = RequiredResourceResult(
                compositionTarget_.Get()->SetRoot(rootVisual_.Get()));
        }
        if (failure != FailureReason::None)
        {
            ResetComposition();
            return failure;
        }
        surfaceWidth_ = width;
        surfaceHeight_ = height;
        return FailureReason::None;
    }

    FailureReason ClearTransparent() override
    {
        pendingAuxiliary_ = {};
        for (std::size_t index = 0; index < surfaces_.size(); ++index)
        {
            const FailureReason result = DrawSurface(index, [](ID2D1DeviceContext* context, const D2D1_MATRIX_3X2_F&) {
                context->Clear(D2D1::ColorF(0.0f, 0.0f));
                return FailureReason::None;
            });
            if (result != FailureReason::None) return result;
        }
        return FailureReason::None;
    }

    FailureReason SetDesktopInput(PixelRect crop, OutputRotation rotation) override
    {
        if (!DesktopForComposition()) return FailureReason::DuplicationUnavailable;
        currentCrop_ = crop;
        currentRotation_ = rotation;
        return FailureReason::None;
    }

    FailureReason UpdateLuminance(
        int width,
        int height,
        int stagingBufferCount,
        UINT mapFlags,
        std::uint32_t minimumIntervalMs) override
    {
        ID3D11Texture2D* desktop = DesktopForComposition();
        if (!desktop || width != 16 || height != 12 || stagingBufferCount != 2
            || mapFlags != D3D11_MAP_FLAG_DO_NOT_WAIT) return FailureReason::None;
        const std::int64_t now = Counter();
        const std::int64_t frequency = std::max<std::int64_t>(1, Frequency());
        if (lastLuminanceCounter_ != 0
            && static_cast<std::uint64_t>(std::max<std::int64_t>(0, now - lastLuminanceCounter_))
                    * 1000ull / static_cast<std::uint64_t>(frequency) < minimumIntervalMs)
        {
            return FailureReason::None;
        }
        FailureReason failure = EnsureLuminanceResources();
        if (failure != FailureReason::None)
        {
            return failure == FailureReason::DeviceRemoved
                ? failure
                : FailureReason::None;
        }

        ComOwner<IDXGISurface> desktopSurface;
        ComOwner<IDXGISurface> targetSurface;
        failure = RequiredResourceResult(desktop->QueryInterface(
            __uuidof(IDXGISurface), desktopSurface.PutVoid()));
        if (failure == FailureReason::None)
        {
            failure = RequiredResourceResult(luminanceTarget_.Get()->QueryInterface(
                __uuidof(IDXGISurface), targetSurface.PutVoid()));
        }
        if (failure != FailureReason::None)
        {
            return failure == FailureReason::DeviceRemoved
                ? failure
                : FailureReason::None;
        }
        D2D1_BITMAP_PROPERTIES1 sourceProperties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        D2D1_BITMAP_PROPERTIES1 targetProperties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        ComOwner<ID2D1Bitmap1> sourceBitmap;
        ComOwner<ID2D1Bitmap1> targetBitmap;
        failure = RequiredResourceResult(resources_.d2dContext.Get()->CreateBitmapFromDxgiSurface(
            desktopSurface.Get(), &sourceProperties, sourceBitmap.Put()));
        if (failure == FailureReason::None)
        {
            failure = RequiredResourceResult(resources_.d2dContext.Get()->CreateBitmapFromDxgiSurface(
                targetSurface.Get(), &targetProperties, targetBitmap.Put()));
        }
        if (failure != FailureReason::None)
        {
            return failure == FailureReason::DeviceRemoved
                ? failure
                : FailureReason::None;
        }

        resources_.d2dContext.Get()->SetTarget(targetBitmap.Get());
        resources_.d2dContext.Get()->BeginDraw();
        const DesktopSourceCommand sourceCommand = BuildDesktopSourceCommand(
            currentCrop_, currentRotation_);
        DrawDesktopSource(
            resources_.d2dContext.Get(),
            sourceBitmap.Get(),
            sourceCommand,
            D2D1::Matrix3x2F::Scale(
                16.0f / static_cast<float>(sourceCommand.uprightWidth),
                12.0f / static_cast<float>(sourceCommand.uprightHeight)));
        const HRESULT drawResult = resources_.d2dContext.Get()->EndDraw();
        resources_.d2dContext.Get()->SetTarget(nullptr);
        failure = RequiredResourceResult(drawResult);
        if (failure != FailureReason::None)
        {
            return failure == FailureReason::DeviceRemoved
                ? failure
                : FailureReason::None;
        }

        const std::size_t writeIndex = nextLuminanceBuffer_;
        resources_.d3dContext.Get()->CopyResource(
            luminanceStaging_[writeIndex].Get(), luminanceTarget_.Get());
        nextLuminanceBuffer_ = (nextLuminanceBuffer_ + 1) % luminanceStaging_.size();
        ++luminanceCopies_;
        lastLuminanceCounter_ = now;
        if (luminanceCopies_ < 2) return FailureReason::None;

        const std::size_t readIndex = nextLuminanceBuffer_;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT mapResult = resources_.d3dContext.Get()->Map(
            luminanceStaging_[readIndex].Get(),
            0,
            D3D11_MAP_READ,
            mapFlags,
            &mapped);
        if (mapResult == DXGI_ERROR_WAS_STILL_DRAWING) return FailureReason::None;
        failure = RequiredResourceResult(mapResult);
        if (failure != FailureReason::None)
        {
            return failure == FailureReason::DeviceRemoved
                ? failure
                : FailureReason::None;
        }
        double total = 0.0;
        for (int row = 0; row < 12; ++row)
        {
            const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData)
                + static_cast<std::size_t>(row) * mapped.RowPitch;
            for (int column = 0; column < 16; ++column)
            {
                const auto* pixel = pixels + static_cast<std::size_t>(column) * 4;
                const float luminance = static_cast<float>(
                    (0.0722 * pixel[0] + 0.7152 * pixel[1] + 0.2126 * pixel[2]) / 255.0);
                luminanceGrid_[static_cast<std::size_t>(row * 16 + column)] = luminance;
                total += luminance;
            }
        }
        resources_.d3dContext.Get()->Unmap(luminanceStaging_[readIndex].Get(), 0);
        relativeLuminance_ = static_cast<float>(total / (16.0 * 12.0));
        return FailureReason::None;
    }

    float RelativeLuminance() const override { return relativeLuminance_; }
    const LuminanceGrid& CurrentLuminanceGrid() const override { return luminanceGrid_; }

    FailureReason DrawMaterial(
        const SceneSnapshot& scene,
        const MaterialTone& tone,
        const MaterialFramePlan& plan) override
    {
        ID3D11Texture2D* desktop = DesktopForComposition();
        ComOwner<IDXGISurface> desktopSurface;
        if (!desktop) return FailureReason::DuplicationUnavailable;
        const FailureReason desktopSurfaceFailure = ClassifiedResult(
            desktop->QueryInterface(__uuidof(IDXGISurface), desktopSurface.PutVoid()),
            FailureReason::DuplicationUnavailable);
        if (desktopSurfaceFailure != FailureReason::None)
        {
            return desktopSurfaceFailure;
        }
        return DrawSurface(0, [&](ID2D1DeviceContext* context, const D2D1_MATRIX_3X2_F& atlasTransform) {
            D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
            ComOwner<ID2D1Bitmap1> desktopBitmap;
            HRESULT createDesktopBitmap = context->CreateBitmapFromDxgiSurface(
                desktopSurface.Get(), &properties, desktopBitmap.Put());
#if defined(LLP_MATERIAL_TESTING)
            createDesktopBitmap = InjectRequiredResourceFailure(
                injectedFailure_, RequiredResourceOperation::MaterialDesktopBitmap,
                createDesktopBitmap, injectedFailureHitCount_);
#endif
            FailureReason failure = RequiredResourceResult(createDesktopBitmap);
            if (failure != FailureReason::None) return failure;
            ComOwner<ID2D1Effect> blur;
            HRESULT createBlur = context->CreateEffect(CLSID_D2D1GaussianBlur, blur.Put());
#if defined(LLP_MATERIAL_TESTING)
            createBlur = InjectRequiredResourceFailure(
                injectedFailure_, RequiredResourceOperation::MaterialBlurEffect,
                createBlur, injectedFailureHitCount_);
#endif
            failure = RequiredResourceResult(createBlur);
            if (failure != FailureReason::None) return failure;
            blur.Get()->SetInput(0, desktopBitmap.Get());
            failure = RequiredResourceResult(blur.Get()->SetValue(
                D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
                tone.blurRadiusDip * static_cast<float>(std::max(1, scene.metrics.dpi)) / 96.0f));
            if (failure != FailureReason::None) return failure;
            failure = RequiredResourceResult(blur.Get()->SetValue(
                D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,
                D2D1_BORDER_MODE_HARD));
            if (failure != FailureReason::None) return failure;

            ComOwner<ID2D1Factory> factory;
            resources_.d2dDevice.Get()->GetFactory(factory.Put());
            if (!factory.Get()) return FailureReason::CompositionUnavailable;
            ComOwner<ID2D1RoundedRectangleGeometry> boundary;
            const float radius = static_cast<float>(std::max(0, scene.metrics.cornerRadiusPx));
            failure = RequiredResourceResult(factory.Get()->CreateRoundedRectangleGeometry(
                D2D1::RoundedRect(
                    D2D1::RectF(0.0f, 0.0f, static_cast<float>(surfaceWidth_), static_cast<float>(surfaceHeight_)),
                    radius,
                    radius),
                boundary.Put()));
            if (failure != FailureReason::None) return failure;
            ComOwner<ID2D1Layer> layer;
            failure = RequiredResourceResult(context->CreateLayer(layer.Put()));
            if (failure != FailureReason::None) return failure;
            D2D1_LAYER_PARAMETERS1 parameters = D2D1::LayerParameters1();
            parameters.geometricMask = boundary.Get();
            context->PushLayer(parameters, layer.Get());

            const DesktopSourceCommand sourceCommand = BuildDesktopSourceCommand(
                currentCrop_, currentRotation_);
            const D2D1_MATRIX_3X2_F& cropTransform = sourceCommand.sourceToUpright;
            const D2D1_RECT_F desktopSource = D2D1::RectF(
                static_cast<float>(sourceCommand.source.left),
                static_cast<float>(sourceCommand.source.top),
                static_cast<float>(sourceCommand.source.right),
                static_cast<float>(sourceCommand.source.bottom));
            if (plan.drawBlur)
            {
                DrawDesktopSource(context, blur.Get(), sourceCommand, atlasTransform);
            }
            context->SetTransform(atlasTransform);

            if (plan.searchOpticalPoints.size() == 5 && IsValidRect(plan.searchBoundsPx))
            {
                ComOwner<ID2D1RoundedRectangleGeometry> searchBoundary;
                const float searchRadius = static_cast<float>(
                    std::max(1, (plan.searchBoundsPx.bottom - plan.searchBoundsPx.top) / 2));
                failure = RequiredResourceResult(factory.Get()->CreateRoundedRectangleGeometry(
                    D2D1::RoundedRect(D2DRect(plan.searchBoundsPx), searchRadius, searchRadius),
                    searchBoundary.Put()));
                if (failure != FailureReason::None)
                {
                    context->PopLayer();
                    return failure;
                }
                ComOwner<ID2D1Layer> searchLayer;
                failure = RequiredResourceResult(context->CreateLayer(searchLayer.Put()));
                if (failure != FailureReason::None)
                {
                    context->PopLayer();
                    return failure;
                }
                D2D1_LAYER_PARAMETERS1 searchParameters = D2D1::LayerParameters1();
                searchParameters.geometricMask = searchBoundary.Get();
                context->PushLayer(searchParameters, searchLayer.Get());
                SubmitSearchEdgeCommands(
                    plan,
                    scene.metrics.dpi,
                    cropTransform,
                    atlasTransform,
                    [&](const SearchEdgeCommand& command) {
                        switch (command.operation)
                        {
                        case SearchEdgeOperation::SetAtlasBeforeClip:
                        case SearchEdgeOperation::SetDisplacedTransform:
                        case SearchEdgeOperation::RestoreAtlas:
                            context->SetTransform(command.transform);
                            break;
                        case SearchEdgeOperation::PushClip:
                            context->PushAxisAlignedClip(
                                command.clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                            break;
                        case SearchEdgeOperation::DrawImage:
                            context->DrawImage(
                                blur.Get(),
                                D2D1::Point2F(
                                    static_cast<float>(sourceCommand.source.left),
                                    static_cast<float>(sourceCommand.source.top)),
                                desktopSource,
                                D2D1_INTERPOLATION_MODE_LINEAR,
                                D2D1_COMPOSITE_MODE_SOURCE_COPY);
                            break;
                        case SearchEdgeOperation::PopClip:
                            context->PopAxisAlignedClip();
                            break;
                        }
                    });
                context->PopLayer();
            }

            ComOwner<ID2D1SolidColorBrush> tint;
            failure = RequiredResourceResult(context->CreateSolidColorBrush(
                D2DColor(tone.tint, tone.tintOpacity), tint.Put()));
            if (failure != FailureReason::None)
            {
                context->PopLayer();
                return failure;
            }
            if (plan.drawTint) context->FillRectangle(
                D2D1::RectF(0.0f, 0.0f, static_cast<float>(surfaceWidth_), static_cast<float>(surfaceHeight_)),
                tint.Get());
            if (plan.drawClarityShape)
            {
                const std::array<D2D1_GRADIENT_STOP, 2> stops = { {
                    { 0.0f, D2DColor(tone.guardedSurface, plan.clarityCenterOpacity) },
                    { 1.0f, D2DColor(tone.guardedSurface, plan.clarityEdgeOpacity) }
                } };
                ComOwner<ID2D1GradientStopCollection> gradientStops;
                failure = RequiredResourceResult(context->CreateGradientStopCollection(
                    stops.data(), static_cast<UINT32>(stops.size()),
                    D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, gradientStops.Put()));
                if (failure != FailureReason::None)
                {
                    context->PopLayer();
                    return failure;
                }
                ComOwner<ID2D1RadialGradientBrush> clarity;
                failure = RequiredResourceResult(context->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(
                        D2D1::Point2F(surfaceWidth_ * 0.5f, surfaceHeight_ * 0.5f),
                        D2D1::Point2F(0.0f, 0.0f),
                        std::max(1.0f, surfaceWidth_ * 0.65f),
                        std::max(1.0f, surfaceHeight_ * 0.65f)),
                    gradientStops.Get(), clarity.Put()));
                if (failure != FailureReason::None)
                {
                    context->PopLayer();
                    return failure;
                }
                context->FillRectangle(
                    D2D1::RectF(0.0f, 0.0f, static_cast<float>(surfaceWidth_), static_cast<float>(surfaceHeight_)),
                    clarity.Get());
            }
            for (const auto& guard : plan.contrastGuards)
            {
                ComOwner<ID2D1SolidColorBrush> guardBrush;
                failure = RequiredResourceResult(context->CreateSolidColorBrush(
                    D2DColor(tone.guardedSurface, guard.opacity), guardBrush.Put()));
                if (failure != FailureReason::None)
                {
                    context->PopLayer();
                    return failure;
                }
                context->FillRoundedRectangle(
                    D2D1::RoundedRect(D2DRect(guard.bounds), guard.radiusPx, guard.radiusPx),
                    guardBrush.Get());
            }
            ComOwner<ID2D1SolidColorBrush> shadow;
            failure = RequiredResourceResult(context->CreateSolidColorBrush(
                D2D1::ColorF(0.0f, 0.20f), shadow.Put()));
            if (failure != FailureReason::None)
            {
                context->PopLayer();
                return failure;
            }
            if (plan.drawShadow) context->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(2.0f, 3.0f, static_cast<float>(surfaceWidth_ - 2), static_cast<float>(surfaceHeight_ - 1)),
                    radius,
                    radius),
                shadow.Get(),
                4.0f);
            ComOwner<ID2D1SolidColorBrush> rim;
            failure = RequiredResourceResult(context->CreateSolidColorBrush(
                D2D1::ColorF(1.0f, 0.30f), rim.Put()));
            if (failure != FailureReason::None)
            {
                context->PopLayer();
                return failure;
            }
            if (plan.drawRim) context->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(0.5f, 0.5f, static_cast<float>(surfaceWidth_) - 0.5f, static_cast<float>(surfaceHeight_) - 0.5f),
                    radius,
                    radius),
                rim.Get(),
                1.0f);
            context->PopLayer();
            return FailureReason::None;
        });
    }

    FailureReason DrawOverlay(const SceneSnapshot& scene) override
    {
        bool drewAuxiliary = false;
        const FailureReason result = DrawSurface(1, [&](ID2D1DeviceContext* context, const D2D1_MATRIX_3X2_F& atlasTransform) {
            context->SetTransform(atlasTransform);
            const auto& search = scene.metrics.search;
            FailureReason failure = FailureReason::None;
            if (IsValidRect(search))
            {
                const float searchRadius = static_cast<float>(
                    std::max(1, (search.bottom - search.top) / 2));
                ComOwner<ID2D1SolidColorBrush> searchFill;
                failure = RequiredResourceResult(context->CreateSolidColorBrush(
                    D2D1::ColorF(1.0f, scene.search.active ? 0.16f : 0.10f), searchFill.Put()));
                if (failure != FailureReason::None) return failure;
                context->FillRoundedRectangle(
                    D2D1::RoundedRect(D2DRect(search), searchRadius, searchRadius), searchFill.Get());
                ComOwner<ID2D1SolidColorBrush> edge;
                failure = RequiredResourceResult(context->CreateSolidColorBrush(
                    D2D1::ColorF(1.0f, scene.search.active ? 0.55f : 0.28f), edge.Put()));
                if (failure != FailureReason::None) return failure;
                context->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2DRect(search), searchRadius, searchRadius), edge.Get(), 1.0f);
            }
            for (const auto& item : scene.items)
            {
                if (!item.focused || !IsValidRect(item.bounds)) continue;
                const D2D1_ROUNDED_RECT tile = D2D1::RoundedRect(
                    D2DRect(item.bounds),
                    18.0f, 18.0f);
                ComOwner<ID2D1SolidColorBrush> focusShadow;
                failure = RequiredResourceResult(context->CreateSolidColorBrush(
                    D2D1::ColorF(0.0f, 0.20f), focusShadow.Put()));
                if (failure != FailureReason::None) return failure;
                context->DrawRoundedRectangle(tile, focusShadow.Get(), 5.0f);
                ComOwner<ID2D1SolidColorBrush> focusFill;
                failure = RequiredResourceResult(context->CreateSolidColorBrush(
                    D2DColor(scene.policy.focus, 0.20f), focusFill.Put()));
                if (failure != FailureReason::None) return failure;
                context->FillRoundedRectangle(tile, focusFill.Get());
                ComOwner<ID2D1SolidColorBrush> focusRim;
                failure = RequiredResourceResult(context->CreateSolidColorBrush(
                    D2DColor(scene.policy.focus, 1.0f), focusRim.Put()));
                if (failure != FailureReason::None) return failure;
                context->DrawRoundedRectangle(tile, focusRim.Get(), 2.0f);
            }
            if (IsValidRect(scene.auxiliary.bounds))
            {
                const float radius = static_cast<float>(
                    std::max(1, (scene.auxiliary.bounds.bottom - scene.auxiliary.bounds.top) / 2));
                ComOwner<ID2D1SolidColorBrush> auxiliaryFill;
                failure = RequiredResourceResult(context->CreateSolidColorBrush(
                    D2DColor(
                        scene.auxiliary.focused ? scene.policy.focus : scene.policy.surface,
                        scene.auxiliary.focused ? 0.90f : 0.76f),
                    auxiliaryFill.Put()));
                if (failure != FailureReason::None) return failure;
                context->FillRoundedRectangle(
                    D2D1::RoundedRect(D2DRect(scene.auxiliary.bounds), radius, radius),
                    auxiliaryFill.Get());
                ComOwner<ID2D1SolidColorBrush> auxiliaryRim;
                failure = RequiredResourceResult(context->CreateSolidColorBrush(
                    D2DColor(scene.policy.boundary, scene.auxiliary.focused ? 1.0f : 0.84f),
                    auxiliaryRim.Put()));
                if (failure != FailureReason::None) return failure;
                context->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2DRect(scene.auxiliary.bounds), radius, radius),
                    auxiliaryRim.Get(), scene.auxiliary.focused ? 2.0f : 1.0f);
                drewAuxiliary = true;
            }
            return FailureReason::None;
        });
        if (result == FailureReason::None && drewAuxiliary)
        {
            pendingAuxiliary_.overlayDrawn = true;
            pendingAuxiliary_.sceneGeneration = scene.generation;
            pendingAuxiliary_.bounds = scene.auxiliary.bounds;
            pendingAuxiliary_.focused = scene.auxiliary.focused;
        }
        return result;
    }

    FailureReason DrawContent(const SceneSnapshot& scene) override
    {
        bool drewAuxiliaryText = false;
        const FailureReason result = DrawSurface(2, [&](ID2D1DeviceContext* context, const D2D1_MATRIX_3X2_F& atlasTransform) {
            context->SetTransform(atlasTransform);
            ComOwner<ID2D1SolidColorBrush> textBrush;
            FailureReason failure = RequiredResourceResult(context->CreateSolidColorBrush(
                D2DColor(scene.policy.text), textBrush.Put()));
            if (failure != FailureReason::None) return failure;
            ComOwner<IDWriteTextFormat> searchFormat;
            ComOwner<IDWriteTextFormat> titleFormat;
            ComOwner<IDWriteTextFormat> subtitleFormat;
            ComOwner<IDWriteTextFormat> auxiliaryFormat;
            HRESULT createSearchFormat = resources_.dwriteFactory.Get()->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(std::max(1, scene.metrics.searchFontPx)),
                L"", searchFormat.Put());
#if defined(LLP_MATERIAL_TESTING)
            createSearchFormat = InjectRequiredResourceFailure(
                injectedFailure_, RequiredResourceOperation::ContentTextFormat,
                createSearchFormat, injectedFailureHitCount_);
#endif
            failure = RequiredResourceResult(createSearchFormat);
            if (failure != FailureReason::None) return failure;
            if (!scene.items.empty())
            {
                failure = RequiredResourceResult(resources_.dwriteFactory.Get()->CreateTextFormat(
                    L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(std::max(1, scene.metrics.tileFontPx)),
                    L"", titleFormat.Put()));
                if (failure != FailureReason::None) return failure;
            }
            const bool hasSubtitle = std::any_of(
                scene.items.begin(), scene.items.end(), [](const SceneItem& item) {
                    return !item.subtitle.empty();
                });
            if (hasSubtitle)
            {
                failure = RequiredResourceResult(resources_.dwriteFactory.Get()->CreateTextFormat(
                    L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(std::max(1, scene.metrics.tileFontPx - 2)),
                    L"", subtitleFormat.Put()));
                if (failure != FailureReason::None) return failure;
            }
            if (IsValidRect(scene.auxiliary.bounds) && !scene.auxiliary.text.empty())
            {
                failure = RequiredResourceResult(resources_.dwriteFactory.Get()->CreateTextFormat(
                    L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(std::max(1, scene.metrics.tileFontPx)),
                    L"", auxiliaryFormat.Put()));
                if (failure != FailureReason::None) return failure;
                failure = RequiredResourceResult(auxiliaryFormat.Get()->SetTextAlignment(
                    DWRITE_TEXT_ALIGNMENT_CENTER));
                if (failure != FailureReason::None) return failure;
                failure = RequiredResourceResult(auxiliaryFormat.Get()->SetParagraphAlignment(
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
                if (failure != FailureReason::None) return failure;
                failure = RequiredResourceResult(auxiliaryFormat.Get()->SetWordWrapping(
                    DWRITE_WORD_WRAPPING_NO_WRAP));
                if (failure != FailureReason::None) return failure;
            }

            const auto& searchRect = scene.metrics.searchText;
            const float searchOrigin = static_cast<float>(searchRect.left - scene.search.horizontalOffsetPx);
            const float searchHeight = static_cast<float>(std::max(1, searchRect.bottom - searchRect.top));
            const std::size_t textLength = scene.search.displayText.size();
            const std::size_t selectionStart = std::min(scene.search.selectionStart, textLength);
            const std::size_t selectionEnd = std::min(scene.search.selectionEnd, textLength);
            const std::size_t compositionStart = std::min(scene.search.compositionStart, textLength);
            const std::size_t compositionEnd = std::min(scene.search.compositionEnd, textLength);
            const std::size_t caret = std::min(scene.search.caret, textLength);
            float selectionStartWidth = 0.0f;
            float selectionEndWidth = 0.0f;
            failure = MeasurePrefix(
                scene.search.displayText, selectionStart, searchFormat.Get(), selectionStartWidth);
            if (failure != FailureReason::None) return failure;
            failure = MeasurePrefix(
                scene.search.displayText, selectionEnd, searchFormat.Get(), selectionEndWidth);
            if (failure != FailureReason::None) return failure;
            const float selectionLeft = searchOrigin + selectionStartWidth;
            const float selectionRight = searchOrigin + selectionEndWidth;
            if (selectionEnd > selectionStart)
            {
                ComOwner<ID2D1SolidColorBrush> selectionBrush;
                failure = RequiredResourceResult(context->CreateSolidColorBrush(
                    D2DColor(scene.policy.focus, 0.36f), selectionBrush.Put()));
                if (failure != FailureReason::None) return failure;
                context->FillRectangle(
                    D2D1::RectF(selectionLeft, static_cast<float>(searchRect.top), selectionRight, static_cast<float>(searchRect.bottom)),
                    selectionBrush.Get());
            }
            context->DrawText(
                scene.search.displayText.c_str(),
                static_cast<UINT32>(scene.search.displayText.size()),
                searchFormat.Get(),
                D2D1::RectF(searchOrigin, static_cast<float>(searchRect.top),
                    searchOrigin + static_cast<float>(std::max(1, searchRect.right - searchRect.left)),
                    static_cast<float>(searchRect.bottom)),
                textBrush.Get());
            if (scene.search.composing && compositionEnd > compositionStart)
            {
                float compositionStartWidth = 0.0f;
                float compositionEndWidth = 0.0f;
                failure = MeasurePrefix(
                    scene.search.displayText, compositionStart, searchFormat.Get(), compositionStartWidth);
                if (failure != FailureReason::None) return failure;
                failure = MeasurePrefix(
                    scene.search.displayText, compositionEnd, searchFormat.Get(), compositionEndWidth);
                if (failure != FailureReason::None) return failure;
                const float start = searchOrigin + compositionStartWidth;
                const float end = searchOrigin + compositionEndWidth;
                context->DrawLine(
                    D2D1::Point2F(start, static_cast<float>(searchRect.bottom - 3)),
                    D2D1::Point2F(end, static_cast<float>(searchRect.bottom - 3)),
                    textBrush.Get(), 1.5f);
            }
            if (scene.search.active)
            {
                float caretWidth = 0.0f;
                failure = MeasurePrefix(scene.search.displayText, caret, searchFormat.Get(), caretWidth);
                if (failure != FailureReason::None) return failure;
                const float caretX = searchOrigin + caretWidth;
                context->DrawLine(
                    D2D1::Point2F(caretX, static_cast<float>(searchRect.top + 6)),
                    D2D1::Point2F(caretX, static_cast<float>(searchRect.top) + searchHeight - 6.0f),
                    textBrush.Get(), 1.5f);
            }

            for (const auto& item : scene.items)
            {
                if (item.icon && item.icon->width > 0 && item.icon->height > 0
                    && item.icon->stride >= item.icon->width * 4
                    && item.icon->premultipliedBgra.size()
                        >= static_cast<std::size_t>(item.icon->stride) * item.icon->height)
                {
                    D2D1_BITMAP_PROPERTIES1 iconProperties = D2D1::BitmapProperties1(
                        D2D1_BITMAP_OPTIONS_NONE,
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                    ComOwner<ID2D1Bitmap1> iconBitmap;
                    HRESULT createIconBitmap = context->CreateBitmap(
                        D2D1::SizeU(
                            static_cast<UINT32>(item.icon->width),
                            static_cast<UINT32>(item.icon->height)),
                        item.icon->premultipliedBgra.data(),
                        static_cast<UINT32>(item.icon->stride),
                        &iconProperties,
                        iconBitmap.Put());
#if defined(LLP_MATERIAL_TESTING)
                    createIconBitmap = InjectRequiredResourceFailure(
                        injectedFailure_, RequiredResourceOperation::ContentIconBitmap,
                        createIconBitmap, injectedFailureHitCount_);
#endif
                    failure = RequiredResourceResult(createIconBitmap);
                    if (failure != FailureReason::None) return failure;
                    const float left = static_cast<float>(item.bounds.left
                        + std::max(0, (item.bounds.right - item.bounds.left - item.icon->width) / 2));
                    const float top = static_cast<float>(item.bounds.top + 8);
                    context->DrawBitmap(iconBitmap.Get(), D2D1::RectF(
                        left, top, left + item.icon->width, top + item.icon->height));
                }
                const float textTop = static_cast<float>(item.bounds.bottom - std::max(36, scene.metrics.tileFontPx * 2));
                context->DrawText(
                    item.title.c_str(), static_cast<UINT32>(item.title.size()), titleFormat.Get(),
                    D2D1::RectF(static_cast<float>(item.bounds.left), textTop,
                        static_cast<float>(item.bounds.right), static_cast<float>(item.bounds.bottom)),
                    textBrush.Get());
                if (!item.subtitle.empty()) context->DrawText(
                    item.subtitle.c_str(), static_cast<UINT32>(item.subtitle.size()), subtitleFormat.Get(),
                    D2D1::RectF(static_cast<float>(item.bounds.left), textTop + scene.metrics.tileFontPx,
                        static_cast<float>(item.bounds.right), static_cast<float>(item.bounds.bottom)),
                    textBrush.Get());
            }
            if (auxiliaryFormat.Get())
            {
                ComOwner<ID2D1SolidColorBrush> auxiliaryText;
                failure = RequiredResourceResult(context->CreateSolidColorBrush(
                    D2DColor(scene.auxiliary.focused ? scene.policy.focusText : scene.policy.text),
                    auxiliaryText.Put()));
                if (failure != FailureReason::None) return failure;
                context->DrawText(
                    scene.auxiliary.text.c_str(),
                    static_cast<UINT32>(scene.auxiliary.text.size()),
                    auxiliaryFormat.Get(),
                    D2DRect(scene.auxiliary.bounds),
                    auxiliaryText.Get());
                drewAuxiliaryText = true;
            }
            return FailureReason::None;
        });
        if (result == FailureReason::None && drewAuxiliaryText)
        {
            pendingAuxiliary_.contentDrawn = true;
            pendingAuxiliary_.sceneGeneration = scene.generation;
            pendingAuxiliary_.bounds = scene.auxiliary.bounds;
            pendingAuxiliary_.text = scene.auxiliary.text;
            pendingAuxiliary_.focused = scene.auxiliary.focused;
            if (developmentEvidence_)
            {
                pendingAuxiliary_.itemStableIds.reserve(scene.items.size());
                pendingAuxiliary_.itemSourcePaths.reserve(scene.items.size());
                for (const auto& item : scene.items)
                {
                    pendingAuxiliary_.itemStableIds.push_back(item.stableId);
                    pendingAuxiliary_.itemSourcePaths.push_back(item.sourcePath);
                    pendingAuxiliary_.itemIdentityCopies += 2;
                }
            }
        }
        return result;
    }

    FailureReason Commit() override
    {
#if defined(LLP_MATERIAL_TESTING)
        ++commitCallCount_;
#endif
        HRESULT commit = resources_.compositionDevice.Get()->Commit();
#if defined(LLP_MATERIAL_TESTING)
        commit = InjectRequiredResourceFailure(
            injectedFailure_, RequiredResourceOperation::CompositionCommit,
            commit, injectedFailureHitCount_);
#endif
        FailureReason failure = RequiredResourceResult(commit);
        if (failure != FailureReason::None) return failure;
        HRESULT wait = resources_.compositionDevice.Get()->WaitForCommitCompletion();
#if defined(LLP_MATERIAL_TESTING)
        wait = InjectRequiredResourceFailure(
            injectedFailure_, RequiredResourceOperation::CompositionWait,
            wait, injectedFailureHitCount_);
#endif
        failure = RequiredResourceResult(wait);
        if (failure != FailureReason::None) return failure;
        if (pendingAuxiliary_.overlayDrawn && pendingAuxiliary_.contentDrawn)
        {
            pendingAuxiliary_.rendered = true;
            lastPresentedAuxiliary_ = pendingAuxiliary_;
        }
        else
        {
            lastPresentedAuxiliary_ = {};
        }
        return FailureReason::None;
    }

    void Freeze() override
    {
        (void)ReleaseFrame();
        candidateDesktopValid_ = false;
        duplication_.Reset();
        output1_.Reset();
        output_.Reset();
        adapter_.Reset();
        factory_.Reset();
    }

    void Stop() override
    {
        ComOwner<IDXGIDevice3> trimDevice;
        if (resources_.dxgiDevice.Get())
        {
            (void)resources_.dxgiDevice.Get()->QueryInterface(
                __uuidof(IDXGIDevice3), trimDevice.PutVoid());
        }
        if (resources_.d3dDevice.Get() && resources_.d3dContext.Get())
        {
            D3D11_QUERY_DESC idleDescription = {};
            idleDescription.Query = D3D11_QUERY_EVENT;
            ComOwner<ID3D11Query> idleQuery;
            if (SUCCEEDED(resources_.d3dDevice.Get()->CreateQuery(
                    &idleDescription, idleQuery.Put())))
            {
                resources_.d3dContext.Get()->End(idleQuery.Get());
                resources_.d3dContext.Get()->Flush();
                const auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(100);
                BOOL idle = FALSE;
                while (resources_.d3dContext.Get()->GetData(
                           idleQuery.Get(), &idle, sizeof(idle),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_FALSE
                    && std::chrono::steady_clock::now() < deadline)
                {
                    Sleep(1);
                }
            }
        }
        (void)ReleaseFrame();
        acquiredResource_.Reset();
        candidateDesktop_.Reset();
        committedDesktop_.Reset();
        candidateDesktopValid_ = false;
        ResetComposition();
        ResetLuminance();
        duplication_.Reset();
        output1_.Reset();
        output_.Reset();
        adapter_.Reset();
        factory_.Reset();
        if (resources_.d3dContext.Get())
        {
            resources_.d3dContext.Get()->ClearState();
            resources_.d3dContext.Get()->Flush();
        }
        resources_ = DeviceResources{};
        if (trimDevice.Get()) trimDevice.Get()->Trim();
        outputDesktop_ = {};
        outputRotation_ = OutputRotation::Identity;
        lastSceneGeneration_ = std::numeric_limits<std::uint64_t>::max();
        developmentEvidence_ = false;
        candidateDesktopIdentity_ = 0;
        committedDesktopIdentity_ = 0;
        developmentFreezeEvidence_ = {};
        pendingAuxiliary_ = {};
        lastPresentedAuxiliary_ = {};
        adapterDescription_.clear();
        outputDeviceName_.clear();
    }

    std::int64_t Counter() const override
    {
        LARGE_INTEGER value = {};
        QueryPerformanceCounter(&value);
        return value.QuadPart;
    }

    std::int64_t Frequency() const override
    {
        LARGE_INTEGER value = {};
        QueryPerformanceFrequency(&value);
        return value.QuadPart;
    }

private:
    ID3D11Texture2D* DesktopForComposition() const
    {
        return candidateDesktopValid_ && candidateDesktop_.Get()
            ? candidateDesktop_.Get()
            : committedDesktop_.Get();
    }

    void PromoteDesktopForEvidence()
    {
        if (!developmentEvidence_ || !candidateDesktopValid_ || !candidateDesktop_.Get()) return;
        std::swap(candidateDesktop_, committedDesktop_);
        committedDesktopIdentity_ = candidateDesktopIdentity_;
        candidateDesktopIdentity_ = 0;
        candidateDesktopValid_ = false;
    }

    bool HashDesktopTexture(ID3D11Texture2D* texture, std::uint64_t& hash)
    {
        hash = 0;
        if (!developmentEvidence_ || !texture || !resources_.d3dDevice.Get()
            || !resources_.d3dContext.Get()) return false;
        D3D11_TEXTURE2D_DESC description = {};
        texture->GetDesc(&description);
        if (description.Width == 0 || description.Height == 0
            || description.Format != DXGI_FORMAT_B8G8R8A8_UNORM
            || description.ArraySize != 1 || description.SampleDesc.Count != 1)
        {
            return false;
        }
        D3D11_TEXTURE2D_DESC stagingDescription = description;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDescription.MiscFlags = 0;
        ComOwner<ID3D11Texture2D> staging;
        if (FAILED(resources_.d3dDevice.Get()->CreateTexture2D(
                &stagingDescription, nullptr, staging.Put())))
        {
            return false;
        }
        resources_.d3dContext.Get()->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(resources_.d3dContext.Get()->Map(
                staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return false;
        }
        std::uint64_t value = 1469598103934665603ull;
        const auto addByte = [&value](std::uint8_t byte) {
            value ^= byte;
            value *= 1099511628211ull;
        };
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            addByte(static_cast<std::uint8_t>(description.Width >> shift));
            addByte(static_cast<std::uint8_t>(description.Height >> shift));
        }
        const std::size_t rowBytes = static_cast<std::size_t>(description.Width) * 4;
        for (UINT row = 0; row < description.Height; ++row)
        {
            const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData)
                + static_cast<std::size_t>(row) * mapped.RowPitch;
            for (std::size_t index = 0; index < rowBytes; ++index) addByte(pixels[index]);
        }
        resources_.d3dContext.Get()->Unmap(staging.Get(), 0);
        hash = value;
        return true;
    }

    template <typename Draw>
    FailureReason DrawSurface(std::size_t index, Draw&& draw)
    {
        if (index >= surfaces_.size() || !surfaces_[index].Get())
        {
            return FailureReason::CompositionUnavailable;
        }
        ComOwner<IDXGISurface> dxgiSurface;
        POINT offset = {};
        HRESULT result = surfaces_[index].Get()->BeginDraw(
            nullptr, __uuidof(IDXGISurface), dxgiSurface.PutVoid(), &offset);
#if defined(LLP_MATERIAL_TESTING)
        result = InjectRequiredResourceFailure(
            injectedFailure_, RequiredResourceOperation::CompositionBeginDraw,
            result, injectedFailureHitCount_);
#endif
        FailureReason beginFailure = RequiredResourceResult(result);
        if (beginFailure != FailureReason::None) return beginFailure;

        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComOwner<ID2D1Bitmap1> target;
        result = resources_.d2dContext.Get()->CreateBitmapFromDxgiSurface(
            dxgiSurface.Get(), &properties, target.Put());
        FailureReason drawFailure = FailureReason::None;
        if (SUCCEEDED(result))
        {
            resources_.d2dContext.Get()->SetTarget(target.Get());
            resources_.d2dContext.Get()->BeginDraw();
            const D2D1_MATRIX_3X2_F atlasTransform = SurfaceAtlasTransform(offset);
            resources_.d2dContext.Get()->SetTransform(atlasTransform);
            drawFailure = draw(resources_.d2dContext.Get(), atlasTransform);
            result = resources_.d2dContext.Get()->EndDraw();
            resources_.d2dContext.Get()->SetTarget(nullptr);
        }
        const HRESULT surfaceResult = surfaces_[index].Get()->EndDraw();
        const FailureReason resultFailure = RequiredResourceResult(result);
        const FailureReason surfaceFailure = RequiredResourceResult(surfaceResult);
        if (resultFailure == FailureReason::DeviceRemoved
            || surfaceFailure == FailureReason::DeviceRemoved)
        {
            return FailureReason::DeviceRemoved;
        }
        if (drawFailure != FailureReason::None) return drawFailure;
        return resultFailure != FailureReason::None ? resultFailure : surfaceFailure;
    }

    FailureReason MeasurePrefix(
        const std::wstring& text,
        std::size_t length,
        IDWriteTextFormat* format,
        float& width) const
    {
        width = 0.0f;
        if (!format) return FailureReason::CompositionUnavailable;
        if (length == 0) return FailureReason::None;
        const std::size_t bounded = std::min(length, text.size());
        ComOwner<IDWriteTextLayout> layout;
        FailureReason failure = RequiredResourceResult(
            resources_.dwriteFactory.Get()->CreateTextLayout(
                text.c_str(), static_cast<UINT32>(bounded), format,
                100000.0f, 1000.0f, layout.Put()));
        if (failure != FailureReason::None) return failure;
        DWRITE_TEXT_METRICS metrics = {};
        failure = RequiredResourceResult(layout.Get()->GetMetrics(&metrics));
        if (failure == FailureReason::None)
        {
            width = metrics.widthIncludingTrailingWhitespace;
        }
        return failure;
    }

    FailureReason EnsureLuminanceResources()
    {
        if (luminanceTarget_.Get() && luminanceStaging_[0].Get()
            && luminanceStaging_[1].Get()) return FailureReason::None;
        ResetLuminance();
        D3D11_TEXTURE2D_DESC target = {};
        target.Width = 16;
        target.Height = 12;
        target.MipLevels = 1;
        target.ArraySize = 1;
        target.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        target.SampleDesc.Count = 1;
        target.Usage = D3D11_USAGE_DEFAULT;
        target.BindFlags = D3D11_BIND_RENDER_TARGET;
        FailureReason failure = RequiredResourceResult(
            resources_.d3dDevice.Get()->CreateTexture2D(
                &target, nullptr, luminanceTarget_.Put()));
        if (failure != FailureReason::None) return failure;
        D3D11_TEXTURE2D_DESC staging = target;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.BindFlags = 0;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (auto& buffer : luminanceStaging_)
        {
            failure = RequiredResourceResult(resources_.d3dDevice.Get()->CreateTexture2D(
                &staging, nullptr, buffer.Put()));
            if (failure != FailureReason::None)
            {
                ResetLuminance();
                return failure;
            }
        }
        return FailureReason::None;
    }

    void ResetLuminance()
    {
        for (auto& buffer : luminanceStaging_) buffer.Reset();
        luminanceTarget_.Reset();
        nextLuminanceBuffer_ = 0;
        luminanceCopies_ = 0;
        lastLuminanceCounter_ = 0;
        relativeLuminance_ = 0.5f;
        luminanceGrid_.fill(0.5f);
    }

    void ResetComposition()
    {
        if (compositionTarget_.Get() && resources_.compositionDevice.Get())
        {
            if (SUCCEEDED(compositionTarget_.Get()->SetRoot(nullptr))
                && SUCCEEDED(resources_.compositionDevice.Get()->Commit()))
            {
                (void)resources_.compositionDevice.Get()->WaitForCommitCompletion();
            }
        }
        for (auto& surface : surfaces_) surface.Reset();
        for (auto& visual : visuals_) visual.Reset();
        rootVisual_.Reset();
        compositionTarget_.Reset();
        surfaceWidth_ = 0;
        surfaceHeight_ = 0;
    }

    using SetAffinityFn = BOOL(WINAPI*)(HWND, DWORD);
    using GetAffinityFn = BOOL(WINAPI*)(HWND, DWORD*);
    SetAffinityFn setAffinity_ = nullptr;
    GetAffinityFn getAffinity_ = nullptr;
    HWND affinityWindow_ = nullptr;
    bool frameAcquired_ = false;
    PixelRect outputDesktop_;
    OutputRotation outputRotation_ = OutputRotation::Identity;
    PixelRect currentCrop_;
    OutputRotation currentRotation_ = OutputRotation::Identity;
    UINT surfaceWidth_ = 0;
    UINT surfaceHeight_ = 0;
    std::uint64_t lastSceneGeneration_ = std::numeric_limits<std::uint64_t>::max();
    bool developmentEvidence_ = false;
    AuxiliaryPresentationEvidence pendingAuxiliary_;
    AuxiliaryPresentationEvidence lastPresentedAuxiliary_;
    float relativeLuminance_ = 0.5f;
    std::array<float, 16 * 12> luminanceGrid_ = {};
    std::int64_t lastLuminanceCounter_ = 0;
    std::size_t nextLuminanceBuffer_ = 0;
    std::uint64_t luminanceCopies_ = 0;
    DeviceResources resources_;
    ComOwner<IDXGIResource> acquiredResource_;
    ComOwner<ID3D11Texture2D> candidateDesktop_;
    ComOwner<ID3D11Texture2D> committedDesktop_;
    bool candidateDesktopValid_ = false;
    std::uint64_t candidateDesktopIdentity_ = 0;
    std::uint64_t committedDesktopIdentity_ = 0;
    DevelopmentFreezeEvidence developmentFreezeEvidence_;
    ComOwner<ID3D11Texture2D> luminanceTarget_;
    std::array<ComOwner<ID3D11Texture2D>, 2> luminanceStaging_;
    ComOwner<IDXGIFactory1> factory_;
    ComOwner<IDXGIAdapter1> adapter_;
    ComOwner<IDXGIOutput> output_;
    ComOwner<IDXGIOutput1> output1_;
    ComOwner<IDXGIOutputDuplication> duplication_;
    ComOwner<IDCompositionTarget> compositionTarget_;
    ComOwner<IDCompositionVisual> rootVisual_;
    std::array<ComOwner<IDCompositionVisual>, 3> visuals_;
    std::array<ComOwner<IDCompositionSurface>, 3> surfaces_;
    std::wstring adapterDescription_;
    std::wstring outputDeviceName_;
#if defined(LLP_MATERIAL_TESTING)
    RequiredResourceOperation injectedFailure_ = static_cast<RequiredResourceOperation>(-1);
    int injectedFailureHitCount_ = 0;
    int commitCallCount_ = 0;
#endif
};

enum class EvidenceCandidateResult
{
    Prepared = 0,
    Recovered = 1,
    Fallback = 2
};

class CaptureLifecycle final
{
public:
    explicit CaptureLifecycle(Diagnostics diagnostics = {})
        : diagnostics_(std::move(diagnostics))
    {
    }

    void FailStart(CaptureBackend& backend, ShowRequest request, FailureReason failure)
    {
        CleanupVisible(backend);
        request_ = std::move(request);
        diagnostics_.state = LifecycleState::Starting;
        diagnostics_.path = RenderPath::Fallback;
        diagnostics_.failure = FailureReason::None;
        diagnostics_.evidenceFrozen = false;
        diagnostics_.adapter.clear();
        diagnostics_.output.clear();
        diagnostics_.dpi = request_.dpi;
        diagnostics_.reducedMotion = request_.policy.reducedMotion;
        EnterFallback(backend, failure);
    }

    void Show(CaptureBackend& backend, ShowRequest request, bool requestedRebuild = false)
    {
        if (evidencePermanentlyFrozen_) return;
        CleanupVisible(backend);
        request_ = std::move(request);
        rebuildUsed_ = requestedRebuild;
        forcedAccessLostUsed_ = false;
        diagnostics_.state = requestedRebuild
            ? LifecycleState::Rebuilding
            : LifecycleState::Starting;
        if (requestedRebuild) ++diagnostics_.deviceRebuilds;
        diagnostics_.path = RenderPath::Fallback;
        diagnostics_.failure = FailureReason::None;
        diagnostics_.evidenceFrozen = false;
        diagnostics_.adapter.clear();
        diagnostics_.output.clear();
        diagnostics_.dpi = request_.dpi;
        diagnostics_.reducedMotion = request_.policy.reducedMotion;
        diagnostics_.committedActivation = 0;
        diagnostics_.presentedSceneGeneration = 0;

        if (request_.policy.solidSurface)
        {
            diagnostics_.state = LifecycleState::Live;
            diagnostics_.path = RenderPath::Solid;
            return;
        }

        if (IsDevelopmentBuild() && request_.forcedFailure != FailureReason::None
            && request_.forcedFailure != FailureReason::AccessLost)
        {
            EnterFallback(backend, FailureReason::ForcedForTest);
            return;
        }
        const FailureReason affinityFailure = backend.SetCaptureExclusion(request_.window);
        if (affinityFailure != FailureReason::None)
        {
            EnterFallback(backend, affinityFailure);
            return;
        }
        const FailureReason startFailure = backend.Start(request_);
        if (startFailure != FailureReason::None)
        {
            EnterFallback(backend, startFailure);
            return;
        }
        diagnostics_.adapter = backend.AdapterDescription();
        diagnostics_.output = backend.OutputDeviceName();
        capturing_ = true;
        firstFramePending_ = true;
        firstFrameStarted_ = backend.Counter();
    }

    bool Step(
        CaptureBackend& backend,
        const SceneSnapshot* scene = nullptr,
        bool sceneChanged = false)
    {
        if (!capturing_) return false;
        const std::int64_t stepStarted = backend.Counter();
        const ReleaseResult releaseResult = ReleaseHeldFrame(backend);
        if (releaseResult != ReleaseResult::Released)
        {
            const FailureReason releaseFailure = releaseResult == ReleaseResult::AccessLost
                ? FailureReason::AccessLost
                : (releaseResult == ReleaseResult::DeviceRemoved
                    ? FailureReason::DeviceRemoved
                    : FailureReason::DuplicationUnavailable);
            RecoverOrFallback(backend, releaseFailure, releaseFailure);
            return false;
        }
        const std::int64_t now = backend.Counter();
        const std::int64_t frequency = std::max<std::int64_t>(1, backend.Frequency());
        std::uint32_t waitMilliseconds = 16;
        if (firstFramePending_)
        {
            const auto elapsedMilliseconds = static_cast<std::uint64_t>(
                std::max<std::int64_t>(0, now - firstFrameStarted_)) * 1000ull
                / static_cast<std::uint64_t>(frequency);
            if (elapsedMilliseconds >= 32)
            {
                diagnostics_.firstFrameMs = static_cast<double>(elapsedMilliseconds);
                EnterFallback(backend, FailureReason::FirstFrameTimeout);
                return false;
            }
            waitMilliseconds = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(16, 32 - elapsedMilliseconds));
        }

        std::uint64_t identity = 0;
        const bool forcedAccessLost = request_.forcedFailure == FailureReason::AccessLost
            && IsDevelopmentBuild()
            && (!request_.developmentRecoverAfterAccessLoss || !forcedAccessLostUsed_);
        if (forcedAccessLost) forcedAccessLostUsed_ = true;
        AcquireResult result = forcedAccessLost
            ? AcquireResult::AccessLost
            : backend.Acquire(waitMilliseconds, identity);
        if (result == AcquireResult::Timeout)
        {
            if (firstFramePending_)
            {
                const std::int64_t finished = backend.Counter();
                const double elapsed = static_cast<double>(
                    std::max<std::int64_t>(0, finished - firstFrameStarted_))
                    * 1000.0 / static_cast<double>(frequency);
                if (elapsed >= 32.0)
                {
                    diagnostics_.firstFrameMs = elapsed;
                    EnterFallback(backend, FailureReason::FirstFrameTimeout);
                }
            }
            if (!firstFramePending_ && sceneChanged)
            {
                bool presented = false;
                const FailureReason composition = backend.Compose(
                    request_, scene, false, true, false, presented);
                if (composition != FailureReason::None)
                {
                    if (composition == FailureReason::DeviceRemoved)
                    {
                        (void)ReleaseHeldFrame(backend);
                        RecoverOrFallback(backend, composition, composition);
                    }
                    else
                    {
                        EnterFallback(backend, composition);
                    }
                    return false;
                }
                if (presented) ++diagnostics_.presentedFrames;
                if (presented)
                {
                    RecordPresentedFrame(backend, stepStarted);
                    AcknowledgePresentation(scene);
                    return true;
                }
            }
            return false;
        }
        if (result == AcquireResult::Frame)
        {
            frameHeld_ = true;
            if (firstFramePending_)
            {
                const double elapsed = static_cast<double>(
                    std::max<std::int64_t>(0, backend.Counter() - firstFrameStarted_))
                    * 1000.0 / static_cast<double>(frequency);
                if (elapsed > 32.0)
                {
                    diagnostics_.firstFrameMs = elapsed;
                    EnterFallback(backend, FailureReason::FirstFrameTimeout);
                    return false;
                }
            }
            ++diagnostics_.acquiredFrames;
            diagnostics_.lastFrameIdentity = identity;
            if (firstFramePending_)
            {
                diagnostics_.firstFrameMs = static_cast<double>(
                    std::max<std::int64_t>(0, backend.Counter() - firstFrameStarted_))
                    * 1000.0 / static_cast<double>(frequency);
                firstFramePending_ = false;
                diagnostics_.state = LifecycleState::Live;
                diagnostics_.path = RenderPath::HighFidelity;
                diagnostics_.failure = FailureReason::None;
            }
            bool presented = false;
            const FailureReason composition = backend.Compose(
                request_, scene, true, sceneChanged, false, presented);
            if (composition != FailureReason::None)
            {
                if (composition == FailureReason::DeviceRemoved)
                {
                    (void)ReleaseHeldFrame(backend);
                    RecoverOrFallback(backend, composition, composition);
                }
                else
                {
                    EnterFallback(backend, composition);
                }
                return false;
            }
            if (presented)
            {
                ++diagnostics_.presentedFrames;
                RecordPresentedFrame(backend, stepStarted);
                AcknowledgePresentation(scene);
                return true;
            }
            return false;
        }

        const FailureReason recoveryFailure = result == AcquireResult::AccessLost
            ? FailureReason::AccessLost
            : (result == AcquireResult::DeviceRemoved
                ? FailureReason::DeviceRemoved
                : FailureReason::DuplicationUnavailable);
        const FailureReason publishedFailure = forcedAccessLost
            ? FailureReason::ForcedForTest
            : recoveryFailure;
        ReleaseHeldFrame(backend);
        RecoverOrFallback(backend, recoveryFailure, publishedFailure);
        return false;
    }

    void Hide(CaptureBackend& backend)
    {
        diagnostics_.state = LifecycleState::Stopping;
        CleanupVisible(backend);
        diagnostics_.state = LifecycleState::Hidden;
        diagnostics_.path = RenderPath::Fallback;
        diagnostics_.failure = FailureReason::None;
        diagnostics_.evidenceFrozen = false;
        diagnostics_.committedActivation = 0;
        diagnostics_.presentedSceneGeneration = 0;
    }

    void Freeze(CaptureBackend& backend, const SceneSnapshot* scene = nullptr)
    {
        if (evidencePermanentlyFrozen_) return;
        if (diagnostics_.state == LifecycleState::Hidden
            || diagnostics_.state == LifecycleState::Stopping) return;
        evidencePermanentlyFrozen_ = true;
        (void)ReleaseHeldFrame(backend);
        backend.Freeze();
        backend.ClearCaptureExclusion();
        capturing_ = false;
        firstFramePending_ = false;
        bool presented = false;
        FailureReason failure = backend.Compose(
            request_, scene, false, false, true, presented);
        if (failure == FailureReason::None && presented)
        {
            ++diagnostics_.presentedFrames;
            AcknowledgePresentation(scene);
            diagnostics_.state = LifecycleState::FrozenEvidence;
            diagnostics_.evidenceFrozen = true;
            return;
        }
        if (failure == FailureReason::None)
        {
            failure = FailureReason::CompositionUnavailable;
        }
        backend.Stop();
        diagnostics_.state = LifecycleState::Fallback;
        diagnostics_.path = RenderPath::Fallback;
        diagnostics_.failure = failure;
        diagnostics_.evidenceFrozen = false;
        diagnostics_.committedActivation = 0;
        diagnostics_.presentedSceneGeneration = 0;
    }

    bool SnapshotCommittedEvidenceForDevelopment(CaptureBackend& backend)
    {
        if (!capturing_ || !request_.developmentEvidence
            || diagnostics_.state != LifecycleState::Live) return false;
        return backend.SnapshotCommittedEvidenceForDevelopment();
    }

    EvidenceCandidateResult AcquireUnpresentedEvidenceCandidateForDevelopment(
        CaptureBackend& backend)
    {
        if (!capturing_ || !request_.developmentEvidence
            || diagnostics_.state != LifecycleState::Live)
        {
            return EvidenceCandidateResult::Fallback;
        }
        const ReleaseResult priorRelease = ReleaseHeldFrame(backend);
        if (priorRelease != ReleaseResult::Released)
        {
            const FailureReason failure = priorRelease == ReleaseResult::AccessLost
                ? FailureReason::AccessLost
                : (priorRelease == ReleaseResult::DeviceRemoved
                    ? FailureReason::DeviceRemoved
                    : FailureReason::DuplicationUnavailable);
            return RecoverOrFallback(backend, failure, failure)
                ? EvidenceCandidateResult::Recovered
                : EvidenceCandidateResult::Fallback;
        }
        std::uint64_t identity = 0;
        const AcquireResult acquired = backend.Acquire(250, identity);
        if (acquired != AcquireResult::Frame)
        {
            (void)backend.ReleaseFrame();
            const FailureReason failure = acquired == AcquireResult::AccessLost
                ? FailureReason::AccessLost
                : (acquired == AcquireResult::DeviceRemoved
                    ? FailureReason::DeviceRemoved
                    : FailureReason::DuplicationUnavailable);
            if (acquired == AcquireResult::AccessLost
                || acquired == AcquireResult::DeviceRemoved)
            {
                return RecoverOrFallback(backend, failure, failure)
                    ? EvidenceCandidateResult::Recovered
                    : EvidenceCandidateResult::Fallback;
            }
            EnterFallback(backend, failure);
            return EvidenceCandidateResult::Fallback;
        }
        frameHeld_ = true;
        ++diagnostics_.acquiredFrames;
        diagnostics_.lastFrameIdentity = identity;
        const bool captured = backend.SnapshotCandidateEvidenceForDevelopment();
        const ReleaseResult release = ReleaseHeldFrame(backend);
        if (release != ReleaseResult::Released)
        {
            const FailureReason failure = release == ReleaseResult::AccessLost
                ? FailureReason::AccessLost
                : (release == ReleaseResult::DeviceRemoved
                    ? FailureReason::DeviceRemoved
                    : FailureReason::DuplicationUnavailable);
            return RecoverOrFallback(backend, failure, failure)
                ? EvidenceCandidateResult::Recovered
                : EvidenceCandidateResult::Fallback;
        }
        if (!captured)
        {
            EnterFallback(backend, FailureReason::CompositionUnavailable);
            return EvidenceCandidateResult::Fallback;
        }
        return EvidenceCandidateResult::Prepared;
    }

    void Shutdown(CaptureBackend& backend)
    {
        CleanupVisible(backend);
    }

    bool IsCapturing() const { return capturing_; }
    const Diagnostics& CurrentDiagnostics() const { return diagnostics_; }

private:
    void RecordPresentedFrame(CaptureBackend& backend, std::int64_t started)
    {
        const std::int64_t frequency = std::max<std::int64_t>(1, backend.Frequency());
        const double elapsed = static_cast<double>(
            std::max<std::int64_t>(0, backend.Counter() - started))
            * 1000.0 / static_cast<double>(frequency);
        frameDurationsMs_[nextFrameDuration_] = elapsed;
        nextFrameDuration_ = (nextFrameDuration_ + 1) % frameDurationsMs_.size();
        frameDurationCount_ = std::min(frameDurationCount_ + 1, frameDurationsMs_.size());

        std::array<double, 120> ordered = frameDurationsMs_;
        std::sort(ordered.begin(), ordered.begin() + frameDurationCount_);
        const std::size_t rank = (frameDurationCount_ * 95 + 99) / 100;
        diagnostics_.frameP95Ms = ordered[std::max<std::size_t>(1, rank) - 1];
    }

    void AcknowledgePresentation(const SceneSnapshot* scene)
    {
        if (!scene || scene->generation < request_.requiredSceneGeneration) return;
        diagnostics_.committedActivation = request_.activation;
        diagnostics_.presentedSceneGeneration = scene->generation;
    }

    ReleaseResult ReleaseHeldFrame(CaptureBackend& backend)
    {
        if (!frameHeld_) return ReleaseResult::Released;
        const ReleaseResult result = backend.ReleaseFrame();
        frameHeld_ = false;
        return result;
    }

    bool RecoverOrFallback(
        CaptureBackend& backend,
        FailureReason recoveryFailure,
        FailureReason publishedFailure)
    {
        if ((recoveryFailure == FailureReason::AccessLost
                || recoveryFailure == FailureReason::DeviceRemoved)
            && !rebuildUsed_)
        {
            rebuildUsed_ = true;
            ++diagnostics_.deviceRebuilds;
            diagnostics_.state = LifecycleState::Rebuilding;
            backend.Stop();
            const FailureReason rebuildFailure = IsDevelopmentBuild()
                    && request_.developmentFailAccessLossRebuild
                ? FailureReason::ForcedForTest
                : backend.Start(request_);
            if (rebuildFailure == FailureReason::None)
            {
                diagnostics_.adapter = backend.AdapterDescription();
                diagnostics_.output = backend.OutputDeviceName();
                firstFramePending_ = true;
                firstFrameStarted_ = backend.Counter();
                return true;
            }
            EnterFallback(backend, rebuildFailure);
            return false;
        }
        EnterFallback(backend, publishedFailure);
        return false;
    }

    void CleanupVisible(CaptureBackend& backend)
    {
        (void)ReleaseHeldFrame(backend);
        backend.Stop();
        backend.ClearCaptureExclusion();
        capturing_ = false;
        firstFramePending_ = false;
    }

    void EnterFallback(CaptureBackend& backend, FailureReason failure)
    {
        CleanupVisible(backend);
        diagnostics_.state = LifecycleState::Fallback;
        diagnostics_.path = RenderPath::Fallback;
        diagnostics_.failure = failure;
        diagnostics_.committedActivation = 0;
        diagnostics_.presentedSceneGeneration = 0;
    }

    ShowRequest request_;
    Diagnostics diagnostics_;
    bool capturing_ = false;
    bool firstFramePending_ = false;
    bool frameHeld_ = false;
    bool rebuildUsed_ = false;
    bool forcedAccessLostUsed_ = false;
    bool evidencePermanentlyFrozen_ = false;
    std::int64_t firstFrameStarted_ = 0;
    std::array<double, 120> frameDurationsMs_ = {};
    std::size_t nextFrameDuration_ = 0;
    std::size_t frameDurationCount_ = 0;
};

}

class MaterialRenderer::Impl final
{
public:
    Impl()
    {
        rendererThread_ = std::thread(&Impl::Run, this);
    }

    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        workCondition_.notify_one();
        if (rendererThread_.joinable()) rendererThread_.join();
    }

    void Show(ShowRequest request)
    {
        RestartRendererThreadIfNeeded();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            request_ = std::move(request);
            showRequested_ = true;
            hideRequested_ = false;
            rebuildRequested_ = false;
        }
        workCondition_.notify_one();
    }

    void Submit(std::shared_ptr<const SceneSnapshot> scene)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingScene_) ++diagnostics_.droppedScenes;
            pendingScene_ = std::move(scene);
        }
        workCondition_.notify_one();
    }

    void Rebuild(ShowRequest request)
    {
        RestartRendererThreadIfNeeded();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            request_ = std::move(request);
            rebuildRequested_ = true;
            showRequested_ = false;
            hideRequested_ = false;
        }
        workCondition_.notify_one();
    }

    void Hide()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (rendererThreadExited_ && diagnostics_.state == LifecycleState::Hidden)
            {
                hideRequested_ = false;
                return;
            }
            hideRequested_ = diagnostics_.state != LifecycleState::Stopping;
            showRequested_ = false;
            rebuildRequested_ = false;
            pendingScene_.reset();
        }
        workCondition_.notify_one();
    }

    void AcknowledgeStoppingStatusForDevelopment()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (diagnostics_.state != LifecycleState::Stopping
                || !developmentStoppingNotificationPending_) return;
            developmentStoppingNotificationPending_ = false;
        }
        workCondition_.notify_one();
    }

    void FreezeEvidence()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!IsDevelopmentBuild() || !request_.developmentEvidence) return;
            freezeRequested_ = true;
        }
        workCondition_.notify_one();
    }

    void SnapshotCommittedEvidenceForDevelopment()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!IsDevelopmentBuild() || !request_.developmentEvidence) return;
            evidenceSnapshotRequested_ = true;
        }
        workCondition_.notify_one();
    }

    void AcquireUnpresentedEvidenceCandidateForDevelopment()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!IsDevelopmentBuild() || !request_.developmentEvidence
                || !evidencePreparationActive_) return;
            evidenceCandidateRequested_ = true;
        }
        workCondition_.notify_one();
    }

    void RequestStatusForDevelopment()
    {
        RestartRendererThreadIfNeeded();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!IsDevelopmentBuild() || !request_.window || !request_.statusMessage) return;
            statusRequested_ = true;
        }
        workCondition_.notify_one();
    }

    Diagnostics SnapshotDiagnostics() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Diagnostics snapshot = diagnostics_;
        if (request_.developmentStoppingBarrier)
        {
            snapshot.queuedSceneGeneration = pendingScene_ ? pendingScene_->generation : 0;
            snapshot.currentSceneGeneration = currentScene_ ? currentScene_->generation : 0;
            snapshot.showQueued = showRequested_;
            snapshot.rebuildQueued = rebuildRequested_;
        }
        snapshot.composeLifetimeProbeEntered = composeLifetimeProbeEntered_;
        snapshot.composeLifetimeProbeCompleted = composeLifetimeProbeCompleted_;
        snapshot.composeSceneExpiredDuringHide = composeSceneExpiredDuringHide_;
        return snapshot;
    }

    bool WaitUntilHiddenForTest(std::uint32_t timeoutMs) const
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return hiddenCondition_.wait_for(
            lock,
            std::chrono::milliseconds(timeoutMs),
            [this]() {
                return diagnostics_.state == LifecycleState::Hidden && !hideRequested_;
            });
    }

#if defined(LLP_MATERIAL_TESTING)
    void ApplyEvidenceCandidateResultForTest(int result)
    {
        if (result < static_cast<int>(EvidenceCandidateResult::Prepared)
            || result > static_cast<int>(EvidenceCandidateResult::Fallback)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        CompleteEvidenceCandidatePreparation(static_cast<EvidenceCandidateResult>(result));
    }

    bool EvidencePreparationActiveForTest() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return evidencePreparationActive_;
    }
#endif

private:
    void RestartRendererThreadIfNeeded()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!rendererThreadExited_) return;
        lock.unlock();
        if (rendererThread_.joinable()) rendererThread_.join();
        lock.lock();
        if (shutdown_) return;
        rendererThreadExited_ = false;
        rendererThread_ = std::thread(&Impl::Run, this);
    }

    void CompleteEvidenceCandidatePreparation(EvidenceCandidateResult result)
    {
        evidencePreparationActive_ = result == EvidenceCandidateResult::Prepared;
    }

    void PublishDiagnostics(
        const CaptureLifecycle& lifecycle,
        const DesktopDuplicationBackend& backend,
        bool captureStep = false)
    {
        const AuxiliaryPresentationEvidence& auxiliary = backend.LastPresentedAuxiliary();
        const DevelopmentFreezeEvidence& evidence = request_.developmentEvidence
            ? backend.DevelopmentEvidence()
            : EmptyDevelopmentFreezeEvidence;
        const DiagnosticsSynchronization synchronization = SynchronizeDiagnostics(
            diagnostics_,
            lifecycle.CurrentDiagnostics(),
            auxiliary,
            evidence,
            request_.developmentEvidence);
        if (captureStep && request_.developmentEvidence)
        {
            diagnostics_.diagnosticsCaptureStepStringCopies += synchronization.stringCopies;
            diagnostics_.diagnosticsCaptureStepVectorCopies += synchronization.vectorCopies;
        }
        if (synchronization.notifyHost && request_.window && request_.statusMessage)
        {
            ++diagnostics_.statusPublications;
            PostMessageW(request_.window, request_.statusMessage, 0, 0);
        }
    }

    void Run()
    {
        const HRESULT apartmentResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool apartmentReady = SUCCEEDED(apartmentResult);
        DesktopDuplicationBackend backend;
        std::unique_lock<std::mutex> lock(mutex_);
        CaptureLifecycle lifecycle(diagnostics_);
        for (;;)
        {
            if (!lifecycle.IsCapturing() || evidencePreparationActive_)
            {
                workCondition_.wait(lock, [this]() {
                    return shutdown_ || showRequested_ || hideRequested_ || rebuildRequested_
                        || freezeRequested_ || statusRequested_ || evidenceSnapshotRequested_
                        || evidenceCandidateRequested_ || pendingScene_;
                });
            }
            if (shutdown_) break;

            if (statusRequested_)
            {
                statusRequested_ = false;
                if (request_.window && request_.statusMessage)
                {
                    ++diagnostics_.statusPublications;
                    PostMessageW(request_.window, request_.statusMessage, 0, 0);
                }
                if (lifecycle.CurrentDiagnostics().state == LifecycleState::Hidden
                    && !showRequested_ && !hideRequested_ && !rebuildRequested_
                    && !freezeRequested_ && !evidenceSnapshotRequested_
                    && !evidenceCandidateRequested_ && !pendingScene_)
                {
                    rendererThreadExited_ = true;
                    break;
                }
            }

            bool sceneChanged = false;
            if (pendingScene_)
            {
                sceneChanged = !currentScene_
                    || pendingScene_->generation != currentScene_->generation;
                currentScene_ = std::move(pendingScene_);
            }

            if (hideRequested_)
            {
                diagnostics_.state = LifecycleState::Stopping;
                const ShowRequest stoppingRequest = request_;
                pendingScene_.reset();
                currentScene_.reset();
                lastPresentedScene_.reset();
                hideRequested_ = false;
                evidencePreparationActive_ = false;
                evidenceSnapshotRequested_ = false;
                evidenceCandidateRequested_ = false;
                if (stoppingRequest.developmentEvidence
                    && stoppingRequest.window
                    && stoppingRequest.statusMessage)
                {
                    developmentStoppingNotificationPending_ = stoppingRequest.developmentStoppingBarrier;
                    PostMessageW(stoppingRequest.window, stoppingRequest.statusMessage, 0, 0);
                    if (developmentStoppingNotificationPending_)
                    {
                        workCondition_.wait_for(
                            lock,
                            std::chrono::seconds(2),
                            [this]() {
                                return !developmentStoppingNotificationPending_
                                    || shutdown_;
                            });
                        developmentStoppingNotificationPending_ = false;
                    }
                }
                lock.unlock();
                lifecycle.Hide(backend);
                lock.lock();
                PublishDiagnostics(lifecycle, backend);
                hiddenCondition_.notify_all();
                if (!stoppingRequest.developmentEvidence
                    && !showRequested_ && !rebuildRequested_)
                {
                    rendererThreadExited_ = true;
                    break;
                }
            }

            const bool rebuild = rebuildRequested_;
            const bool show = showRequested_;
            if (show || rebuild)
            {
                showRequested_ = false;
                rebuildRequested_ = false;
                diagnostics_.state = rebuild ? LifecycleState::Rebuilding : LifecycleState::Starting;
                lastPresentedScene_.reset();
                evidencePreparationActive_ = false;
                evidenceSnapshotRequested_ = false;
                evidenceCandidateRequested_ = false;
                ShowRequest request = request_;
                lock.unlock();
                if (apartmentReady)
                {
                    lifecycle.Show(backend, request, rebuild);
                }
                else
                {
                    lifecycle.FailStart(
                        backend, request, FailureReason::CompositionUnavailable);
                }
                lock.lock();
                PublishDiagnostics(lifecycle, backend);
            }

            if (evidenceSnapshotRequested_)
            {
                evidenceSnapshotRequested_ = false;
                lock.unlock();
                const bool captured = lifecycle.SnapshotCommittedEvidenceForDevelopment(backend);
                lock.lock();
                evidencePreparationActive_ = captured;
                PublishDiagnostics(lifecycle, backend);
                continue;
            }

            if (evidenceCandidateRequested_)
            {
                evidenceCandidateRequested_ = false;
                lock.unlock();
                const EvidenceCandidateResult result =
                    lifecycle.AcquireUnpresentedEvidenceCandidateForDevelopment(backend);
                lock.lock();
                CompleteEvidenceCandidatePreparation(result);
                PublishDiagnostics(lifecycle, backend);
                continue;
            }

            if (freezeRequested_)
            {
                freezeRequested_ = false;
                evidencePreparationActive_ = false;
                lock.unlock();
                lifecycle.Freeze(backend, lastPresentedScene_.get());
                lock.lock();
                PublishDiagnostics(lifecycle, backend);
                continue;
            }

            if (lifecycle.IsCapturing() && !evidencePreparationActive_)
            {
                if (request_.developmentComposeLifetimeProbe
                    && !composeLifetimeProbeCompleted_
                    && currentScene_)
                {
                    std::weak_ptr<const SceneSnapshot> sceneLifetime = currentScene_;
                    composeLifetimeProbeEntered_ = true;
                    if (request_.window && request_.statusMessage)
                    {
                        PostMessageW(request_.window, request_.statusMessage, 0, 0);
                    }
                    workCondition_.wait_for(
                        lock,
                        std::chrono::seconds(2),
                        [this]() { return hideRequested_ || shutdown_; });
                    composeSceneExpiredDuringHide_ = sceneLifetime.expired();
                    composeLifetimeProbeCompleted_ = true;
                    if (request_.window && request_.statusMessage)
                    {
                        PostMessageW(request_.window, request_.statusMessage, 0, 0);
                    }
                    if (hideRequested_ || shutdown_) continue;
                }
                lock.unlock();
                const bool presented = lifecycle.Step(
                    backend, currentScene_.get(), sceneChanged);
                lock.lock();
                if (presented) lastPresentedScene_ = currentScene_;
                PublishDiagnostics(lifecycle, backend, true);
            }
        }

        lock.unlock();
        lifecycle.Shutdown(backend);
        if (apartmentReady) CoUninitialize();
    }

    mutable std::mutex mutex_;
    std::condition_variable workCondition_;
    mutable std::condition_variable hiddenCondition_;
    std::thread rendererThread_;
    bool rendererThreadExited_ = false;
    bool shutdown_ = false;
    bool showRequested_ = false;
    bool hideRequested_ = false;
    bool rebuildRequested_ = false;
    bool freezeRequested_ = false;
    bool evidenceSnapshotRequested_ = false;
    bool evidenceCandidateRequested_ = false;
    bool evidencePreparationActive_ = false;
    bool statusRequested_ = false;
    bool developmentStoppingNotificationPending_ = false;
    bool composeLifetimeProbeEntered_ = false;
    bool composeLifetimeProbeCompleted_ = false;
    bool composeSceneExpiredDuringHide_ = false;
    ShowRequest request_;
    std::shared_ptr<const SceneSnapshot> pendingScene_;
    std::shared_ptr<const SceneSnapshot> currentScene_;
    std::shared_ptr<const SceneSnapshot> lastPresentedScene_;
    Diagnostics diagnostics_;
};

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

MaterialRenderer::MaterialRenderer() = default;

MaterialRenderer::~MaterialRenderer() = default;

void MaterialRenderer::Show(ShowRequest request)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->Show(std::move(request));
}

void MaterialRenderer::Submit(std::shared_ptr<const SceneSnapshot> scene)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->Submit(std::move(scene));
}

void MaterialRenderer::Rebuild(ShowRequest request)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->Rebuild(std::move(request));
}

void MaterialRenderer::Hide()
{
    if (impl_) impl_->Hide();
}

void MaterialRenderer::Shutdown()
{
    impl_.reset();
}

void MaterialRenderer::FreezeEvidence()
{
    if (impl_) impl_->FreezeEvidence();
}

void MaterialRenderer::SnapshotCommittedEvidenceForDevelopment()
{
    if (impl_) impl_->SnapshotCommittedEvidenceForDevelopment();
}

void MaterialRenderer::AcquireUnpresentedEvidenceCandidateForDevelopment()
{
    if (impl_) impl_->AcquireUnpresentedEvidenceCandidateForDevelopment();
}

#if defined(LLP_MATERIAL_TESTING)
void MaterialRenderer::ApplyEvidenceCandidateResultForTest(int result)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->ApplyEvidenceCandidateResultForTest(result);
}

bool MaterialRenderer::EvidencePreparationActiveForTest() const
{
    return impl_ && impl_->EvidencePreparationActiveForTest();
}
#endif

void MaterialRenderer::RequestStatusForDevelopment()
{
    if (impl_) impl_->RequestStatusForDevelopment();
}

void MaterialRenderer::AcknowledgeStoppingStatusForDevelopment()
{
    if (impl_) impl_->AcknowledgeStoppingStatusForDevelopment();
}

Diagnostics MaterialRenderer::SnapshotDiagnostics() const
{
    return impl_ ? impl_->SnapshotDiagnostics() : Diagnostics{};
}

bool MaterialRenderer::WaitUntilHiddenForTest(std::uint32_t timeoutMs) const
{
    return !impl_ || impl_->WaitUntilHiddenForTest(timeoutMs);
}

bool RendererDependenciesAvailable(
    std::uint32_t* selectedFeatureLevel,
    FailureReason* failure)
{
    std::uint32_t probedFeatureLevel = 0;
    FailureReason probedFailure = FailureReason::CompositionUnavailable;
    std::thread probeThread([&]() {
        const HRESULT apartmentResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(apartmentResult))
        {
            {
                DeviceResources resources;
                probedFailure = InitializeDevice(resources, &probedFeatureLevel);
            }
            CoUninitialize();
        }
    });
    probeThread.join();

    if (selectedFeatureLevel) *selectedFeatureLevel = probedFeatureLevel;
    if (failure) *failure = probedFailure;
    return probedFailure == FailureReason::None;
}

std::shared_ptr<const IconRaster> RasterizeIcon(HICON icon, int sizePx)
{
    if (!icon || sizePx <= 0) return nullptr;
    if (sizePx > std::numeric_limits<int>::max() / 4) return nullptr;
    const int stride = sizePx * 4;
    const auto height = static_cast<std::size_t>(sizePx);
    if (height > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(stride)) return nullptr;
    const auto byteCount = static_cast<std::size_t>(stride) * height;

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

    const auto renderOntoOpaqueBackground = [&](std::uint8_t background,
                                                std::vector<std::uint8_t>& rendered) {
        auto* destination = static_cast<std::uint8_t*>(rawBits);
        for (std::size_t offset = 0; offset + 3 < byteCount; offset += 4)
        {
            destination[offset + 0] = background;
            destination[offset + 1] = background;
            destination[offset + 2] = background;
            destination[offset + 3] = 255;
        }
        if (!DrawIconEx(iconDc.Get(), 0, 0, icon, sizePx, sizePx, 0, nullptr, DI_NORMAL)) return false;
        rendered.assign(destination, destination + byteCount);
        return true;
    };

    std::vector<std::uint8_t> blackBackground;
    std::vector<std::uint8_t> whiteBackground;
    if (!renderOntoOpaqueBackground(0, blackBackground)
        || !renderOntoOpaqueBackground(255, whiteBackground))
    {
        return nullptr;
    }

    auto raster = std::make_shared<IconRaster>();
    raster->width = sizePx;
    raster->height = sizePx;
    raster->stride = stride;
    raster->premultipliedBgra = std::move(blackBackground);

    for (std::size_t offset = 0; offset + 3 < byteCount; offset += 4)
    {
        auto* pixel = raster->premultipliedBgra.data() + offset;
        unsigned int transmissionSum = 0;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const int delta = static_cast<int>(whiteBackground[offset + channel])
                - static_cast<int>(pixel[channel]);
            transmissionSum += static_cast<unsigned int>(std::clamp(delta, 0, 255));
        }
        // Round the mean black-to-white transmission to the nearest integer;
        // its complement is coverage alpha, while black-background RGB is already premultiplied.
        const auto transmission = static_cast<std::uint8_t>((transmissionSum + 1u) / 3u);
        const auto alpha = static_cast<std::uint8_t>(255u - transmission);
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            pixel[channel] = std::min(pixel[channel], alpha);
        }
        pixel[3] = alpha;
    }
    return raster;
}

#if defined(LLP_MATERIAL_TESTING)
enum MaterialLifecycleValue : std::size_t
{
    SelectedAdapter,
    SelectedOutput,
    DuplicateCalls,
    AcquireCalls,
    MaximumWaitMilliseconds,
    ReleaseCalls,
    Redraws,
    Rebuilds,
    AcquiredFrames,
    LastFrameIdentity,
    FinalState,
    FinalPath,
    FinalFailure,
    FirstFrameMicroseconds,
    ReleaseBeforeReacquire,
    ReleaseBeforeRebuild,
    ReleaseBeforeHide,
    ReleaseBeforeFreeze,
    HiddenTimedWaits,
    HiddenAcquireCalls,
    AffinityExcluded,
    AffinityCleared,
    PartialCropRejected,
    SelectionOperationCalls,
    ReducedMotionObserved,
    StartCalls,
    PresentedFrames,
    FreezeComposeCalls,
    DiagnosticsDpi,
    DiagnosticsReducedMotion,
    DiagnosticsAdapterMatched,
    DiagnosticsOutputMatched,
    FrameP95Microseconds,
    FreezeSceneGeneration,
    FreezeCalls,
    FreezeStopOrder,
    FreezeClearOrder,
    FreezeComposeOrder,
    FreezeDesktopIdentity,
    StopCalls,
    EvidenceCandidateInitialResult,
    EvidenceCandidateRetryResult,
    EvidencePreparationActiveAfterInitial,
    EvidencePreparationActiveAfterRetry,
    EvidenceBaselineSnapshotCalls,
    MaterialLifecycleValueCount
};

enum MaterialCompositionValue : std::size_t
{
    CompositionTargetCount,
    CompositionRootCount,
    CompositionVisualCount,
    CompositionSurfaceCount,
    CompositionSurfaceClears,
    CompositionCommandCount,
    CompositionCommand0,
    CompositionCommand1,
    CompositionCommand2,
    CompositionCommand3,
    CompositionCommand4,
    CompositionCommand5,
    CompositionDesktopReferences,
    CompositionOverlayDesktopReferences,
    CompositionContentDesktopReferences,
    CompositionCropCount,
    CompositionRotationNormalizations,
    CompositionMaterialAtlasOffsetX,
    CompositionMaterialAtlasOffsetY,
    CompositionOverlayAtlasOffsetX,
    CompositionOverlayAtlasOffsetY,
    CompositionContentAtlasOffsetX,
    CompositionContentAtlasOffsetY,
    CompositionBlurPasses,
    CompositionTintPasses,
    CompositionSearchOpticalPasses,
    CompositionSearchOpticalSamples,
    CompositionSearchCenterDisplacementMilli,
    CompositionSearchEdgeMaximumDisplacementMilli,
    CompositionSearchSourceOutOfBounds,
    CompositionSearchVerticalBiasMilli,
    CompositionSearchEdgeCommandCount,
    CompositionSearchEdgeCommand0,
    CompositionSearchEdgeCommand23 = CompositionSearchEdgeCommand0 + 23,
    CompositionReducedMotionMaximumDisplacementMilli,
    CompositionClarityShapePasses,
    CompositionClarityCenterOpacityMilli,
    CompositionClarityEdgeOpacityMilli,
    CompositionLocalContrastGuards,
    CompositionDarkGuardOpacityMilli,
    CompositionLightGuardOpacityMilli,
    CompositionDegenerateGuardOpacityMilli,
    CompositionClampedGuardOpacityMilli,
    CompositionRimPasses,
    CompositionShadowPasses,
    CompositionLuminanceWidth,
    CompositionLuminanceHeight,
    CompositionStagingBufferCount,
    CompositionDoNotWaitMaps,
    CompositionMinimumReadbackIntervalMs,
    CompositionCopiedBgraIcons,
    CompositionHiconReferences,
    CompositionPresentedAfterDesktop,
    CompositionPresentedAfterTimeout,
    CompositionPresentedAfterUnchanged,
    CompositionPresentedAfterScene,
    CompositionPresentedAfterLifecycle,
    MaterialCompositionValueCount
};

enum MaterialCompositionFailureValue : std::size_t
{
    CompositionFailureReason,
    CompositionFailureCommits,
    CompositionFailurePresented,
    MaterialCompositionFailureValueCount
};

enum LuminanceRotationValue : std::size_t
{
    LuminanceOutputLeft,
    LuminanceOutputTop,
    LuminanceSourceWidth,
    LuminanceSourceHeight,
    LuminanceUprightWidth,
    LuminanceUprightHeight,
    LuminanceGridTopLeft,
    LuminanceGridTopRight,
    LuminanceGridBottomLeft,
    LuminanceGridBottomRight,
    LuminanceRotationValueCount
};

enum RealRequiredResourceFailureValue : std::size_t
{
    RealFailureReason,
    RealFailureCommittedFrames,
    RealFailurePresentedFrames,
    RealFailureNamedOperationCalls,
    RealRequiredResourceFailureValueCount
};

static_assert(sizeof(std::int64_t) == 8, "The private fake ABI requires 64-bit values.");

namespace
{
class FakeCaptureBackend final : public CaptureBackend
{
public:
    explicit FakeCaptureBackend(int scenario) : scenario_(scenario) {}

    FailureReason SetCaptureExclusion(HWND) override
    {
        ++affinityExcluded;
        return FailureReason::None;
    }

    void ClearCaptureExclusion() override
    {
        ++affinityCleared;
        if (scenario_ >= 20 && scenario_ <= 22
            && freezeCalls > 0 && freezeClearOrder == 0)
        {
            freezeClearOrder = ++freezeOperationOrder;
        }
    }

    FailureReason Start(const ShowRequest& request) override
    {
        ++startCalls;
        developmentEvidenceEnabled = request.developmentEvidence;
        evidenceBaselineCaptured = false;
        evidenceCandidateCaptured = false;
        reducedMotionObserved = request.policy.reducedMotion;
        if (scenario_ == 16 && startCalls == 2)
        {
            return FailureReason::DuplicationUnavailable;
        }
        if (scenario_ == 24 && startCalls == 2)
        {
            return FailureReason::DuplicationUnavailable;
        }
        if (scenario_ == 27 && startCalls == 2)
        {
            return FailureReason::DuplicationUnavailable;
        }
        if (scenario_ == 9) nowMilliseconds += 40;
        const std::vector<OutputCatalogEntry> catalog = {
            { 0, 0, reinterpret_cast<HMONITOR>(static_cast<UINT_PTR>(0x101)),
                { 0, 0, 1920, 1080 } },
            { 0, 1, request.monitor, { -1920, -100, -1600, 500 } },
            { 1, 0, reinterpret_cast<HMONITOR>(static_cast<UINT_PTR>(0x102)),
                { -3840, -100, -1920, 980 } },
            { 1, 1, request.monitor, { -1920, -100, 0, 980 } }
        };
        OutputSelection selection;
        ++selectionOperationCalls;
        const OutputSelectionResult selectionResult = SelectCaptureOutput(
            catalog, request.monitor, request.windowDesktop, selection);
        if (selectionResult == OutputSelectionResult::MonitorNotFound)
        {
            return FailureReason::AdapterUnavailable;
        }
        if (selectionResult == OutputSelectionResult::WindowNotContained)
        {
            return FailureReason::DuplicationUnavailable;
        }
        selectedAdapter = static_cast<int>(selection.adapterIndex);
        selectedOutput = static_cast<int>(selection.outputIndex);
        partialCropRejected = selectedAdapter == 1 && selectedOutput == 1;
        ++duplicateCalls;
        return FailureReason::None;
    }

    const std::wstring& AdapterDescription() const override { return adapterDescription; }
    const std::wstring& OutputDeviceName() const override { return outputDeviceName; }

    AcquireResult Acquire(std::uint32_t waitMilliseconds, std::uint64_t& identity) override
    {
        ++acquireCalls;
        if (waitMilliseconds > 0) ++timedWaitCalls;
        maximumWaitMilliseconds = std::max(maximumWaitMilliseconds, waitMilliseconds);
        if (acquireCalls > 1 && !frameOutstanding && releaseCalls > 0)
        {
            releaseBeforeReacquire = true;
        }
        if (scenario_ == 1)
        {
            nowMilliseconds += waitMilliseconds;
            return AcquireResult::Timeout;
        }
        if (scenario_ == 2)
        {
            if (startCalls == 1 && acquireCalls == 1)
            {
                frameOutstanding = true;
                identity = 11;
                ++redraws;
                return AcquireResult::Frame;
            }
            if (startCalls == 1) return AcquireResult::AccessLost;
            frameOutstanding = true;
            identity = 22;
            ++redraws;
            return AcquireResult::Frame;
        }
        if (scenario_ == 3)
        {
            if (startCalls == 1 && acquireCalls == 1)
            {
                frameOutstanding = true;
                identity = 11;
                ++redraws;
                return AcquireResult::Frame;
            }
            return AcquireResult::AccessLost;
        }
        if (scenario_ == 7)
        {
            frameOutstanding = true;
            identity = 77;
            ++redraws;
            return AcquireResult::Frame;
        }
        if (scenario_ == 8)
        {
            nowMilliseconds += 33;
            frameOutstanding = true;
            identity = 88;
            ++redraws;
            return AcquireResult::Frame;
        }
        if (scenario_ == 9)
        {
            nowMilliseconds += 5;
            frameOutstanding = true;
            identity = 99;
            ++redraws;
            return AcquireResult::Frame;
        }
        if (scenario_ == 10)
        {
            if (startCalls == 1 && acquireCalls == 1)
            {
                frameOutstanding = true;
                identity = 101;
                ++redraws;
                return AcquireResult::Frame;
            }
            frameOutstanding = true;
            identity = 102;
            ++redraws;
            return AcquireResult::Frame;
        }
        if (scenario_ == 11)
        {
            if (startCalls == 1) return AcquireResult::DeviceRemoved;
            frameOutstanding = true;
            identity = 111;
            ++redraws;
            return AcquireResult::Frame;
        }
        if (scenario_ == 12)
        {
            frameOutstanding = true;
            identity = 120;
            ++redraws;
            return AcquireResult::Frame;
        }
        if (scenario_ == 16)
        {
            return AcquireResult::AccessLost;
        }
        if (scenario_ == 17)
        {
            if (startCalls == 1 && acquireCalls == 1)
            {
                frameOutstanding = true;
                identity = 171;
                ++redraws;
                return AcquireResult::Frame;
            }
            if (startCalls < 3) return AcquireResult::AccessLost;
            frameOutstanding = true;
            identity = 172;
            ++redraws;
            return AcquireResult::Frame;
        }
        if (scenario_ == 19)
        {
            if (startCalls == 2) return AcquireResult::AccessLost;
            frameOutstanding = true;
            identity = 191;
            ++redraws;
            return AcquireResult::Frame;
        }
        if (scenario_ >= 20 && scenario_ <= 22)
        {
            frameOutstanding = true;
            identity = acquireCalls == 1 ? 201 : 202;
            candidateDesktopIdentity = identity;
            ++redraws;
            return AcquireResult::Frame;
        }
        if (scenario_ >= 25 && scenario_ <= 27)
        {
            if (acquireCalls == 1)
            {
                frameOutstanding = true;
                identity = 251;
                ++redraws;
                return AcquireResult::Frame;
            }
            if (acquireCalls == 2)
            {
                return scenario_ == 26
                    ? AcquireResult::DeviceRemoved
                    : AcquireResult::AccessLost;
            }
            frameOutstanding = true;
            identity = static_cast<std::uint64_t>(250 + acquireCalls);
            ++redraws;
            return AcquireResult::Frame;
        }
        frameOutstanding = true;
        identity = 1;
        ++redraws;
        return AcquireResult::Frame;
    }

    ReleaseResult ReleaseFrame() override
    {
        if (!frameOutstanding) return ReleaseResult::Released;
        ++releaseCalls;
        if (scenario_ == 2 && startCalls == 2) releaseBeforeHide = true;
        if (scenario_ == 7 || scenario_ == 18) releaseBeforeFreeze = true;
        frameOutstanding = false;
        if (scenario_ == 10 && startCalls == 1) return ReleaseResult::AccessLost;
        if (scenario_ == 12) return ReleaseResult::Failed;
        return ReleaseResult::Released;
    }

    FailureReason Compose(
        const ShowRequest&,
        const SceneSnapshot* scene,
        bool,
        bool,
        bool lifecycleEventChanged,
        bool& presented) override
    {
        ++composeCalls;
        if (scenario_ == 23)
        {
            if (composeCalls == 1 || composeCalls == 3)
            {
                presented = false;
                return FailureReason::DeviceRemoved;
            }
            presented = true;
            return FailureReason::None;
        }
        if (scenario_ == 24)
        {
            presented = false;
            return FailureReason::DeviceRemoved;
        }
        if (scenario_ == 18)
        {
            if (lifecycleEventChanged) ++freezeComposeCalls;
            presented = true;
            return FailureReason::None;
        }
        if (scenario_ == 7)
        {
            presented = true;
            return FailureReason::None;
        }
        if (scenario_ >= 20 && scenario_ <= 22)
        {
            if (lifecycleEventChanged)
            {
                ++freezeComposeCalls;
                freezeComposeOrder = ++freezeOperationOrder;
                freezeSceneGeneration = scene ? scene->generation : 0;
                freezeDesktopIdentity = candidateDesktopIdentity;
                if (scenario_ == 21)
                {
                    presented = false;
                    return FailureReason::CompositionUnavailable;
                }
                if (scenario_ == 22)
                {
                    presented = false;
                    return FailureReason::None;
                }
            }
            presented = lifecycleEventChanged || acquireCalls == 1;
            if (presented) committedDesktopIdentity = candidateDesktopIdentity;
            return FailureReason::None;
        }
        if (scenario_ == 9)
        {
            presented = true;
            return FailureReason::None;
        }
        presented = false;
        return FailureReason::None;
    }

    void Freeze() override
    {
        ++freezeCalls;
        if (scenario_ >= 20 && scenario_ <= 22 && freezeStopOrder == 0)
        {
            freezeStopOrder = ++freezeOperationOrder;
            candidateDesktopIdentity = committedDesktopIdentity;
        }
    }

    void Stop() override
    {
        ++stopCalls;
        if ((scenario_ == 2 || scenario_ == 3 || scenario_ == 10 || scenario_ == 11)
            && startCalls == 1 && !frameOutstanding)
        {
            releaseBeforeRebuild = true;
        }
        (void)ReleaseFrame();
        developmentEvidenceEnabled = false;
        evidenceBaselineCaptured = false;
        evidenceCandidateCaptured = false;
    }

    std::int64_t Counter() const override { return nowMilliseconds; }
    std::int64_t Frequency() const override { return 1000; }

    bool SnapshotCommittedEvidenceForDevelopment() override
    {
        if (!developmentEvidenceEnabled || scenario_ < 25 || scenario_ > 27) return false;
        ++evidenceBaselineSnapshotCalls;
        evidenceBaselineCaptured = true;
        return true;
    }

    bool SnapshotCandidateEvidenceForDevelopment() override
    {
        evidenceCandidateCaptured = developmentEvidenceEnabled
            && scenario_ >= 25 && scenario_ <= 27 && evidenceBaselineCaptured;
        return evidenceCandidateCaptured;
    }

    int selectedAdapter = -1;
    int selectedOutput = -1;
    int startCalls = 0;
    int duplicateCalls = 0;
    int acquireCalls = 0;
    int timedWaitCalls = 0;
    unsigned int maximumWaitMilliseconds = 0;
    int releaseCalls = 0;
    int redraws = 0;
    int affinityExcluded = 0;
    int affinityCleared = 0;
    int selectionOperationCalls = 0;
    bool partialCropRejected = false;
    bool releaseBeforeReacquire = false;
    bool releaseBeforeRebuild = false;
    bool releaseBeforeHide = false;
    bool releaseBeforeFreeze = false;
    bool reducedMotionObserved = false;
    int freezeComposeCalls = 0;
    int composeCalls = 0;
    std::uint64_t freezeSceneGeneration = 0;
    int freezeCalls = 0;
    int stopCalls = 0;
    int evidenceBaselineSnapshotCalls = 0;
    int freezeStopOrder = 0;
    int freezeClearOrder = 0;
    int freezeComposeOrder = 0;
    std::uint64_t freezeDesktopIdentity = 0;
    std::wstring adapterDescription = L"Fake Adapter";
    std::wstring outputDeviceName = L"\\\\.\\DISPLAY1";

private:
    int scenario_ = 0;
    std::int64_t nowMilliseconds = 0;
    bool frameOutstanding = false;
    bool developmentEvidenceEnabled = false;
    bool evidenceBaselineCaptured = false;
    bool evidenceCandidateCaptured = false;
    int freezeOperationOrder = 0;
    std::uint64_t candidateDesktopIdentity = 0;
    std::uint64_t committedDesktopIdentity = 0;
};

class RecordingCompositionBackend final : public CompositionBackend
{
public:
    RecordingCompositionBackend()
    {
        luminanceGrid.fill(0.45f);
    }

    FailureReason EnsureTree(const ShowRequest&) override
    {
        if (!treeReady)
        {
            treeReady = true;
            targetCount = 1;
            rootCount = 1;
            visualCount = 3;
            surfaceCount = 3;
        }
        return FailureReason::None;
    }

    FailureReason ClearTransparent() override
    {
        commands.push_back(FrameCommand::ClearTransparent);
        surfaceClears += 3;
        return FailureReason::None;
    }

    FailureReason SetDesktopInput(PixelRect crop, OutputRotation rotation) override
    {
        commands.push_back(FrameCommand::DesktopInput);
        ++desktopReferences;
        if (crop == PixelRect{ 120, 120, 520, 520 }) ++cropCount;
        if (rotation == OutputRotation::Identity) ++rotationNormalizations;
        currentCrop = crop;
        currentRotation = rotation;
        return FailureReason::None;
    }

    FailureReason UpdateLuminance(
        int width,
        int height,
        int stagingBufferCount,
        UINT mapFlags,
        std::uint32_t minimumIntervalMs) override
    {
        luminanceWidth = width;
        luminanceHeight = height;
        this->stagingBufferCount = stagingBufferCount;
        if (mapFlags == D3D11_MAP_FLAG_DO_NOT_WAIT) ++doNotWaitMaps;
        this->minimumIntervalMs = minimumIntervalMs;
        return FailureReason::None;
    }

    float RelativeLuminance() const override { return 0.45f; }
    const LuminanceGrid& CurrentLuminanceGrid() const override { return luminanceGrid; }

    FailureReason DrawMaterial(
        const SceneSnapshot& scene,
        const MaterialTone&,
        const MaterialFramePlan& plan) override
    {
        commands.push_back(FrameCommand::MaterialPass);
        if (failurePoint == 0 || failurePoint == 1)
        {
            return RequiredResourceResult(E_FAIL);
        }
        const D2D1_MATRIX_3X2_F cropTransform = CropTransform(currentCrop, currentRotation);
        const D2D1_MATRIX_3X2_F atlasTransform = SurfaceAtlasTransform(surfaceOffset);
        const auto origin = TransformPoint(
            D2D1::Point2F(static_cast<float>(currentCrop.left), static_cast<float>(currentCrop.top)),
            cropTransform * atlasTransform);
        materialAtlasOffsetX = static_cast<int>(std::lround(origin.x));
        materialAtlasOffsetY = static_cast<int>(std::lround(origin.y));
        blurPasses += plan.drawBlur ? 1 : 0;
        tintPasses += plan.drawTint ? 1 : 0;
        clarityShapePasses += plan.drawClarityShape ? 1 : 0;
        rimPasses += plan.drawRim ? 1 : 0;
        shadowPasses += plan.drawShadow ? 1 : 0;
        clarityCenterOpacityMilli = static_cast<int>(std::lround(plan.clarityCenterOpacity * 1000.0f));
        clarityEdgeOpacityMilli = static_cast<int>(std::lround(plan.clarityEdgeOpacity * 1000.0f));
        localContrastGuards += static_cast<int>(plan.contrastGuards.size());
        if (!plan.contrastGuards.empty())
        {
            darkGuardOpacityMilli = static_cast<int>(std::lround(
                plan.contrastGuards.front().opacity * 1000.0f));
        }
        if (plan.contrastGuards.size() > 1)
        {
            lightGuardOpacityMilli = static_cast<int>(std::lround(
                plan.contrastGuards[1].opacity * 1000.0f));
        }
        if (!plan.searchOpticalPoints.empty())
        {
            ++searchOpticalPasses;
            searchOpticalSamples += static_cast<int>(plan.searchOpticalPoints.size());
            searchCenterDisplacementMilli = static_cast<int>(std::lround(
                plan.searchOpticalPoints.front().sample.displacementDip * 1000.0f));
            float maximumEdgeDisplacement = 0.0f;
            for (std::size_t index = 0; index < plan.searchOpticalPoints.size(); ++index)
            {
                const auto& point = plan.searchOpticalPoints[index];
                if (index > 0)
                {
                    maximumEdgeDisplacement = std::max(
                        maximumEdgeDisplacement, point.sample.displacementDip);
                }
                if (point.sample.sourceXDip < plan.searchBoundsDip.left
                    || point.sample.sourceXDip > plan.searchBoundsDip.right
                    || point.sample.sourceYDip < plan.searchBoundsDip.top
                    || point.sample.sourceYDip > plan.searchBoundsDip.bottom)
                {
                    ++searchSourceOutOfBounds;
                }
            }
            searchEdgeMaximumDisplacementMilli = static_cast<int>(std::lround(
                maximumEdgeDisplacement * 1000.0f));
            if (plan.searchOpticalPoints.size() >= 5)
            {
                const auto& top = plan.searchOpticalPoints[3];
                const auto& bottom = plan.searchOpticalPoints[4];
                searchVerticalBiasMilli = static_cast<int>(std::lround(
                    ((top.sample.sourceYDip - top.destinationYDip)
                        + (bottom.sample.sourceYDip - bottom.destinationYDip)) * 1000.0f));
            }
            if (scene.policy.reducedMotion)
            {
                float maximum = 0.0f;
                for (const auto& point : plan.searchOpticalPoints)
                {
                    maximum = std::max(maximum, point.sample.displacementDip);
                }
                reducedMotionMaximumDisplacementMilli = static_cast<int>(std::lround(maximum * 1000.0f));
            }
            if (plan.searchOpticalPoints.size() == 5 && IsValidRect(plan.searchBoundsPx))
            {
                SubmitSearchEdgeCommands(
                    plan,
                    scene.metrics.dpi,
                    cropTransform,
                    atlasTransform,
                    [&](const SearchEdgeCommand& command) {
                        searchEdgeCommands.push_back(
                            static_cast<std::int64_t>(command.operation)
                            | (static_cast<std::int64_t>(command.edgeIndex + 1) << 8)
                            | (static_cast<std::int64_t>(command.transformState) << 16)
                            | (static_cast<std::int64_t>(command.activeClipEdge) << 24));
                    });
            }
        }
        return FailureReason::None;
    }

    FailureReason DrawOverlay(const SceneSnapshot&) override
    {
        commands.push_back(FrameCommand::OverlayPass);
        const auto origin = TransformPoint(D2D1::Point2F(), SurfaceAtlasTransform(surfaceOffset));
        overlayAtlasOffsetX = static_cast<int>(std::lround(origin.x));
        overlayAtlasOffsetY = static_cast<int>(std::lround(origin.y));
        return FailureReason::None;
    }

    FailureReason DrawContent(const SceneSnapshot& scene) override
    {
        commands.push_back(FrameCommand::ContentPass);
        if (failurePoint == 2 || failurePoint == 3)
        {
            return RequiredResourceResult(E_OUTOFMEMORY);
        }
        const auto origin = TransformPoint(D2D1::Point2F(), SurfaceAtlasTransform(surfaceOffset));
        contentAtlasOffsetX = static_cast<int>(std::lround(origin.x));
        contentAtlasOffsetY = static_cast<int>(std::lround(origin.y));
        for (const auto& item : scene.items)
        {
            if (item.icon && !item.icon->premultipliedBgra.empty()) ++copiedBgraIcons;
        }
        return FailureReason::None;
    }

    FailureReason Commit() override
    {
        commands.push_back(FrameCommand::Commit);
        ++presentedFrames;
        return FailureReason::None;
    }

    bool treeReady = false;
    int targetCount = 0;
    int rootCount = 0;
    int visualCount = 0;
    int surfaceCount = 0;
    int surfaceClears = 0;
    int desktopReferences = 0;
    int overlayDesktopReferences = 0;
    int contentDesktopReferences = 0;
    int cropCount = 0;
    int rotationNormalizations = 0;
    POINT surfaceOffset = {};
    PixelRect currentCrop = {};
    OutputRotation currentRotation = OutputRotation::Identity;
    int materialAtlasOffsetX = 0;
    int materialAtlasOffsetY = 0;
    int overlayAtlasOffsetX = 0;
    int overlayAtlasOffsetY = 0;
    int contentAtlasOffsetX = 0;
    int contentAtlasOffsetY = 0;
    int blurPasses = 0;
    int tintPasses = 0;
    int searchOpticalPasses = 0;
    int searchOpticalSamples = 0;
    int searchCenterDisplacementMilli = 0;
    int searchEdgeMaximumDisplacementMilli = 0;
    int searchSourceOutOfBounds = 0;
    int searchVerticalBiasMilli = 0;
    std::vector<std::int64_t> searchEdgeCommands;
    int reducedMotionMaximumDisplacementMilli = 0;
    int clarityShapePasses = 0;
    int clarityCenterOpacityMilli = 0;
    int clarityEdgeOpacityMilli = 0;
    int localContrastGuards = 0;
    int darkGuardOpacityMilli = 0;
    int lightGuardOpacityMilli = 0;
    int degenerateGuardOpacityMilli = 0;
    int clampedGuardOpacityMilli = 0;
    int rimPasses = 0;
    int shadowPasses = 0;
    int luminanceWidth = 0;
    int luminanceHeight = 0;
    int stagingBufferCount = 0;
    int doNotWaitMaps = 0;
    int minimumIntervalMs = 0;
    int copiedBgraIcons = 0;
    int hiconReferences = 0;
    int presentedFrames = 0;
    int failurePoint = -1;
    LuminanceGrid luminanceGrid = {};
    std::vector<FrameCommand> commands;
};
}

extern "C" int LLP_MaterialRunLuminanceRotationScenario(
    int rotation,
    std::int64_t* values,
    std::size_t valueCount)
{
    if (!values || valueCount != LuminanceRotationValueCount
        || rotation < 0 || rotation > 3) return 2;
    std::fill(values, values + valueCount, 0);
    constexpr std::array<OutputRotation, 4> rotations = {
        OutputRotation::Identity,
        OutputRotation::Rotate90,
        OutputRotation::Rotate180,
        OutputRotation::Rotate270
    };
    const PixelRect outputDesktop = { -1920, -100, 0, 980 };
    const PixelRect windowDesktop = { -1800, 20, -820, 740 };
    const PixelRect crop = MapWindowCrop(
        outputDesktop, windowDesktop, rotations[static_cast<std::size_t>(rotation)]);
    const DesktopSourceCommand command = BuildDesktopSourceCommand(
        crop, rotations[static_cast<std::size_t>(rotation)]);
    DeviceResources resources;
    if (InitializeDevice(resources, nullptr) != FailureReason::None) return 3;

    const UINT sourceWidth = static_cast<UINT>(std::max(1, crop.right));
    const UINT sourceHeight = static_cast<UINT>(std::max(1, crop.bottom));
    const UINT sourceStride = sourceWidth * 4;
    std::vector<std::uint8_t> sourcePixels(
        static_cast<std::size_t>(sourceStride) * sourceHeight, 0);
    constexpr std::array<std::uint8_t, 4> quadrantValues = { 32, 96, 160, 224 };
    const int cropMidX = crop.left + (crop.right - crop.left) / 2;
    const int cropMidY = crop.top + (crop.bottom - crop.top) / 2;
    for (int y = crop.top; y < crop.bottom; ++y)
    {
        for (int x = crop.left; x < crop.right; ++x)
        {
            const std::size_t quadrant = (y >= cropMidY ? 2 : 0) + (x >= cropMidX ? 1 : 0);
            const std::size_t offset = static_cast<std::size_t>(y) * sourceStride
                + static_cast<std::size_t>(x) * 4;
            sourcePixels[offset] = quadrantValues[quadrant];
            sourcePixels[offset + 1] = quadrantValues[quadrant];
            sourcePixels[offset + 2] = quadrantValues[quadrant];
            sourcePixels[offset + 3] = 255;
        }
    }

    D2D1_BITMAP_PROPERTIES1 sourceProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    ComOwner<ID2D1Bitmap1> sourceBitmap;
    if (FAILED(resources.d2dContext.Get()->CreateBitmap(
            D2D1::SizeU(sourceWidth, sourceHeight),
            sourcePixels.data(),
            sourceStride,
            &sourceProperties,
            sourceBitmap.Put()))) return 3;

    D3D11_TEXTURE2D_DESC targetDescription = {};
    targetDescription.Width = 16;
    targetDescription.Height = 12;
    targetDescription.MipLevels = 1;
    targetDescription.ArraySize = 1;
    targetDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    targetDescription.SampleDesc.Count = 1;
    targetDescription.Usage = D3D11_USAGE_DEFAULT;
    targetDescription.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComOwner<ID3D11Texture2D> targetTexture;
    if (FAILED(resources.d3dDevice.Get()->CreateTexture2D(
            &targetDescription, nullptr, targetTexture.Put()))) return 3;
    D3D11_TEXTURE2D_DESC stagingDescription = targetDescription;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComOwner<ID3D11Texture2D> stagingTexture;
    if (FAILED(resources.d3dDevice.Get()->CreateTexture2D(
            &stagingDescription, nullptr, stagingTexture.Put()))) return 3;
    ComOwner<IDXGISurface> targetSurface;
    if (FAILED(targetTexture.Get()->QueryInterface(
            __uuidof(IDXGISurface), targetSurface.PutVoid()))) return 3;
    D2D1_BITMAP_PROPERTIES1 targetProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    ComOwner<ID2D1Bitmap1> targetBitmap;
    if (FAILED(resources.d2dContext.Get()->CreateBitmapFromDxgiSurface(
            targetSurface.Get(), &targetProperties, targetBitmap.Put()))) return 3;

    resources.d2dContext.Get()->SetTarget(targetBitmap.Get());
    resources.d2dContext.Get()->BeginDraw();
    resources.d2dContext.Get()->Clear(D2D1::ColorF(0.0f, 0.0f));
    DrawDesktopSource(
        resources.d2dContext.Get(),
        sourceBitmap.Get(),
        command,
        D2D1::Matrix3x2F::Scale(
            16.0f / static_cast<float>(command.uprightWidth),
            12.0f / static_cast<float>(command.uprightHeight)));
    const HRESULT drawResult = resources.d2dContext.Get()->EndDraw();
    resources.d2dContext.Get()->SetTarget(nullptr);
    if (FAILED(drawResult)) return 3;
    resources.d3dContext.Get()->CopyResource(stagingTexture.Get(), targetTexture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(resources.d3dContext.Get()->Map(
            stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 3;
    constexpr std::array<std::pair<int, int>, 4> gridSamples = { {
        { 4, 3 }, { 12, 3 }, { 4, 9 }, { 12, 9 }
    } };
    for (std::size_t quadrant = 0; quadrant < gridSamples.size(); ++quadrant)
    {
        const auto [x, y] = gridSamples[quadrant];
        const auto* pixel = static_cast<const std::uint8_t*>(mapped.pData)
            + static_cast<std::size_t>(y) * mapped.RowPitch
            + static_cast<std::size_t>(x) * 4;
        const auto closest = std::min_element(
            quadrantValues.begin(), quadrantValues.end(), [&](std::uint8_t first, std::uint8_t second) {
                return std::abs(static_cast<int>(pixel[0]) - first)
                    < std::abs(static_cast<int>(pixel[0]) - second);
            });
        values[LuminanceGridTopLeft + quadrant]
            = static_cast<std::int64_t>(std::distance(quadrantValues.begin(), closest) + 1);
    }
    resources.d3dContext.Get()->Unmap(stagingTexture.Get(), 0);
    values[LuminanceOutputLeft] = outputDesktop.left;
    values[LuminanceOutputTop] = outputDesktop.top;
    values[LuminanceSourceWidth] = crop.right - crop.left;
    values[LuminanceSourceHeight] = crop.bottom - crop.top;
    values[LuminanceUprightWidth] = command.uprightWidth;
    values[LuminanceUprightHeight] = command.uprightHeight;
    return 0;
}

extern "C" int LLP_MaterialRunRealRequiredResourceFailureScenario(
    int operation,
    std::int64_t* values,
    std::size_t valueCount)
{
    if (!values || valueCount != RealRequiredResourceFailureValueCount
        || operation < 0 || operation > 8) return 2;
    std::fill(values, values + valueCount, 0);
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) return 3;
    HWND window = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"STATIC",
        L"LightLaunchpad material required resource probe",
        WS_POPUP,
        80,
        80,
        400,
        320,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (!window)
    {
        if (apartment == S_OK || apartment == S_FALSE) CoUninitialize();
        return 3;
    }
    DesktopDuplicationBackend backend;
    ShowRequest request;
    request.window = window;
    request.monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    RECT rect = {};
    GetWindowRect(window, &rect);
    request.windowDesktop = { rect.left, rect.top, rect.right, rect.bottom };
    FailureReason result = backend.Start(request);
    if (result == FailureReason::None)
    {
        if (operation == static_cast<int>(RequiredResourceOperation::CaptureDesktopTexture))
        {
            backend.InjectFailure(RequiredResourceOperation::CaptureDesktopTexture);
        }
        std::uint64_t identity = 0;
        AcquireResult acquired = AcquireResult::Timeout;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do
        {
            acquired = backend.Acquire(16, identity);
        } while (acquired == AcquireResult::Timeout
            && std::chrono::steady_clock::now() < deadline);
        if (acquired != AcquireResult::Frame)
        {
            result = acquired == AcquireResult::DeviceRemoved
                ? FailureReason::DeviceRemoved
                : FailureReason::DuplicationUnavailable;
        }
    }
    bool presented = false;
    if (result == FailureReason::None)
    {
        SceneSnapshot scene;
        scene.generation = 1;
        scene.metrics.dpi = 96;
        scene.metrics.cornerRadiusPx = 34;
        scene.metrics.search = { 40, 40, 360, 100 };
        scene.metrics.searchText = { 60, 45, 340, 95 };
        scene.metrics.searchFontPx = 24;
        scene.metrics.tileFontPx = 16;
        scene.policy = llp::adaptive::ResolvePolicy({});
        auto icon = std::make_shared<IconRaster>();
        icon->width = 1;
        icon->height = 1;
        icon->stride = 4;
        icon->premultipliedBgra = { 20, 30, 40, 255 };
        scene.items.push_back({ { 20, 100, 148, 228 }, L"Result", L"", icon, true, false });
        if (operation != static_cast<int>(RequiredResourceOperation::CaptureDesktopTexture))
        {
            backend.InjectFailure(static_cast<RequiredResourceOperation>(operation));
        }
        result = backend.Compose(request, &scene, true, true, false, presented);
    }
    values[RealFailureNamedOperationCalls] = backend.InjectedFailureHitCount();
    values[RealFailureCommittedFrames] = backend.CommitCallCount();
    values[RealFailureReason] = static_cast<std::int64_t>(result);
    values[RealFailurePresentedFrames] = presented ? 1 : 0;
    backend.Stop();
    DestroyWindow(window);
    if (apartment == S_OK || apartment == S_FALSE) CoUninitialize();
    return 0;
}

extern "C" int LLP_MaterialRunFakeLifecycleScenario(
    int scenario,
    std::int64_t* values,
    std::size_t valueCount)
{
    if (!values || valueCount != MaterialLifecycleValueCount
        || scenario < 0 || scenario > 27) return 2;
    std::fill(values, values + valueCount, 0);
    FakeCaptureBackend backend(scenario);
    CaptureLifecycle lifecycle;
    ShowRequest request;
    request.window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x401));
    request.monitor = reinterpret_cast<HMONITOR>(static_cast<UINT_PTR>(0x201));
    request.windowDesktop = { -1800, 20, -1400, 420 };
    request.dpi = 144;
    request.developmentEvidence = scenario >= 25 && scenario <= 27;
    if (scenario == 4) request.forcedFailure = FailureReason::CompositionUnavailable;
    if (scenario == 5) request.forcedFailure = FailureReason::DuplicationUnavailable;
    if (scenario == 6) request.forcedFailure = FailureReason::AccessLost;
    if (scenario == 13 || scenario == 14)
    {
        llp::adaptive::Environment environment;
        environment.highContrast = scenario == 13;
        environment.transparencyEnabled = scenario != 14;
        request.policy = llp::adaptive::ResolvePolicy(environment);
    }
    if (scenario == 15)
    {
        llp::adaptive::Environment environment;
        environment.animationsEnabled = false;
        request.policy = llp::adaptive::ResolvePolicy(environment);
    }
    lifecycle.Show(backend, request);

    int hiddenAcquireCalls = 0;
    int hiddenTimedWaits = 0;
    int evidenceCandidateInitialResult = -1;
    int evidenceCandidateRetryResult = -1;
    bool evidencePreparationActiveAfterInitial = false;
    bool evidencePreparationActiveAfterRetry = false;
    if (scenario == 0)
    {
        lifecycle.Step(backend);
        lifecycle.Hide(backend);
    }
    else if (scenario == 1)
    {
        lifecycle.Step(backend);
        lifecycle.Step(backend);
    }
    else if (scenario == 2)
    {
        lifecycle.Step(backend);
        lifecycle.Step(backend);
        lifecycle.Step(backend);
        const int acquireCallsBeforeHide = backend.acquireCalls;
        const int timedWaitsBeforeHide = backend.timedWaitCalls;
        lifecycle.Hide(backend);
        lifecycle.Step(backend);
        lifecycle.Step(backend);
        lifecycle.Step(backend);
        hiddenAcquireCalls = backend.acquireCalls - acquireCallsBeforeHide;
        hiddenTimedWaits = backend.timedWaitCalls - timedWaitsBeforeHide;
    }
    else if (scenario == 3)
    {
        lifecycle.Step(backend);
        lifecycle.Step(backend);
        lifecycle.Step(backend);
    }
    else if (scenario == 6)
    {
        lifecycle.Step(backend);
        lifecycle.Step(backend);
    }
    else if (scenario == 7)
    {
        lifecycle.Step(backend);
        lifecycle.Freeze(backend);
    }
    else if (scenario == 8)
    {
        lifecycle.Step(backend);
    }
    else if (scenario == 9)
    {
        lifecycle.Step(backend);
    }
    else if (scenario == 10)
    {
        lifecycle.Step(backend);
        lifecycle.Step(backend);
        lifecycle.Step(backend);
    }
    else if (scenario == 11)
    {
        lifecycle.Step(backend);
        lifecycle.Step(backend);
    }
    else if (scenario == 12)
    {
        lifecycle.Step(backend);
        lifecycle.Step(backend);
    }
    else if (scenario == 15)
    {
        lifecycle.Step(backend);
    }
    else if (scenario == 16)
    {
        lifecycle.Step(backend);
        lifecycle.Step(backend);
    }
    else if (scenario == 17)
    {
        lifecycle.Step(backend);
        lifecycle.Step(backend);
        lifecycle.Step(backend);
        lifecycle.Show(backend, request, true);
        lifecycle.Step(backend);
    }
    else if (scenario == 18)
    {
        lifecycle.Step(backend);
        lifecycle.Freeze(backend);
        lifecycle.Show(backend, request);
        lifecycle.Step(backend);
    }
    else if (scenario == 19)
    {
        lifecycle.Show(backend, request, true);
        lifecycle.Step(backend);
        lifecycle.Step(backend);
    }
    else if (scenario >= 20 && scenario <= 22)
    {
        SceneSnapshot presentedScene;
        presentedScene.generation = 1;
        SceneSnapshot pendingScene;
        pendingScene.generation = 2;
        const SceneSnapshot* lastPresentedScene = nullptr;
        if (lifecycle.Step(backend, &presentedScene, true))
        {
            lastPresentedScene = &presentedScene;
        }
        if (lifecycle.Step(backend, &pendingScene, true))
        {
            lastPresentedScene = &pendingScene;
        }
        lifecycle.Freeze(backend, lastPresentedScene);
    }
    else if (scenario == 23)
    {
        lifecycle.Step(backend);
        lifecycle.Step(backend);
        lifecycle.Step(backend);
    }
    else if (scenario == 24)
    {
        lifecycle.Step(backend);
    }
    else if (scenario >= 25 && scenario <= 27)
    {
        MaterialRenderer evidenceWorkerState;
        lifecycle.Step(backend);
        (void)lifecycle.SnapshotCommittedEvidenceForDevelopment(backend);
        const EvidenceCandidateResult initialResult =
            lifecycle.AcquireUnpresentedEvidenceCandidateForDevelopment(backend);
        evidenceCandidateInitialResult = static_cast<int>(initialResult);
        evidenceWorkerState.ApplyEvidenceCandidateResultForTest(evidenceCandidateInitialResult);
        evidencePreparationActiveAfterInitial =
            evidenceWorkerState.EvidencePreparationActiveForTest();
        if (scenario != 27)
        {
            lifecycle.Step(backend);
            (void)lifecycle.SnapshotCommittedEvidenceForDevelopment(backend);
        }
        const EvidenceCandidateResult retryResult =
            lifecycle.AcquireUnpresentedEvidenceCandidateForDevelopment(backend);
        evidenceCandidateRetryResult = static_cast<int>(retryResult);
        evidenceWorkerState.ApplyEvidenceCandidateResultForTest(evidenceCandidateRetryResult);
        evidencePreparationActiveAfterRetry =
            evidenceWorkerState.EvidencePreparationActiveForTest();
    }

    const Diagnostics diagnostics = lifecycle.CurrentDiagnostics();
    values[SelectedAdapter] = backend.selectedAdapter;
    values[SelectedOutput] = backend.selectedOutput;
    values[DuplicateCalls] = backend.duplicateCalls;
    values[AcquireCalls] = backend.acquireCalls;
    values[MaximumWaitMilliseconds] = backend.maximumWaitMilliseconds;
    values[ReleaseCalls] = backend.releaseCalls;
    values[Redraws] = backend.redraws;
    values[Rebuilds] = static_cast<std::int64_t>(diagnostics.deviceRebuilds);
    values[AcquiredFrames] = static_cast<std::int64_t>(diagnostics.acquiredFrames);
    values[LastFrameIdentity] = static_cast<std::int64_t>(diagnostics.lastFrameIdentity);
    values[FinalState] = static_cast<int>(diagnostics.state);
    values[FinalPath] = static_cast<int>(diagnostics.path);
    values[FinalFailure] = static_cast<int>(diagnostics.failure);
    values[FirstFrameMicroseconds] = static_cast<long long>(diagnostics.firstFrameMs * 1000.0);
    values[ReleaseBeforeReacquire] = backend.releaseBeforeReacquire ? 1 : 0;
    values[ReleaseBeforeRebuild] = backend.releaseBeforeRebuild ? 1 : 0;
    values[ReleaseBeforeHide] = backend.releaseBeforeHide ? 1 : 0;
    values[ReleaseBeforeFreeze] = backend.releaseBeforeFreeze ? 1 : 0;
    values[HiddenTimedWaits] = hiddenTimedWaits;
    values[HiddenAcquireCalls] = hiddenAcquireCalls;
    values[AffinityExcluded] = backend.affinityExcluded;
    values[AffinityCleared] = backend.affinityCleared;
    values[PartialCropRejected] = backend.partialCropRejected ? 1 : 0;
    values[SelectionOperationCalls] = backend.selectionOperationCalls;
    values[ReducedMotionObserved] = backend.reducedMotionObserved ? 1 : 0;
    values[StartCalls] = backend.startCalls;
    values[PresentedFrames] = static_cast<std::int64_t>(diagnostics.presentedFrames);
    values[FreezeComposeCalls] = backend.freezeComposeCalls;
    values[DiagnosticsDpi] = diagnostics.dpi;
    values[DiagnosticsReducedMotion] = diagnostics.reducedMotion ? 1 : 0;
    values[DiagnosticsAdapterMatched] = diagnostics.adapter == L"Fake Adapter" ? 1 : 0;
    values[DiagnosticsOutputMatched] = diagnostics.output == L"\\\\.\\DISPLAY1" ? 1 : 0;
    values[FrameP95Microseconds] = static_cast<long long>(diagnostics.frameP95Ms * 1000.0);
    values[FreezeSceneGeneration] = static_cast<std::int64_t>(backend.freezeSceneGeneration);
    values[FreezeCalls] = backend.freezeCalls;
    values[FreezeStopOrder] = backend.freezeStopOrder;
    values[FreezeClearOrder] = backend.freezeClearOrder;
    values[FreezeComposeOrder] = backend.freezeComposeOrder;
    values[FreezeDesktopIdentity] = static_cast<std::int64_t>(backend.freezeDesktopIdentity);
    values[StopCalls] = backend.stopCalls;
    values[EvidenceCandidateInitialResult] = evidenceCandidateInitialResult;
    values[EvidenceCandidateRetryResult] = evidenceCandidateRetryResult;
    values[EvidencePreparationActiveAfterInitial] = evidencePreparationActiveAfterInitial ? 1 : 0;
    values[EvidencePreparationActiveAfterRetry] = evidencePreparationActiveAfterRetry ? 1 : 0;
    values[EvidenceBaselineSnapshotCalls] = backend.evidenceBaselineSnapshotCalls;
    return 0;
}

extern "C" int LLP_MaterialRunDiagnosticsPublicationScenario(
    std::int64_t* values,
    std::size_t valueCount)
{
    if (!values || valueCount != 2) return 2;
    std::fill(values, values + valueCount, 0);
    Diagnostics target;
    Diagnostics lifecycle;
    lifecycle.path = RenderPath::HighFidelity;
    lifecycle.state = LifecycleState::Live;
    lifecycle.adapter = L"Fake Adapter";
    lifecycle.output = L"\\\\.\\DISPLAY1";
    lifecycle.presentedFrames = 1;
    AuxiliaryPresentationEvidence auxiliary;
    auxiliary.rendered = true;
    auxiliary.sceneGeneration = 1;
    auxiliary.text = L"...";
    auxiliary.itemStableIds = { L"item-1", L"item-2" };
    auxiliary.itemSourcePaths = { L"C:\\One.lnk", L"C:\\Two.lnk" };
    auxiliary.itemIdentityCopies = 4;
    DevelopmentFreezeEvidence evidence;
    (void)SynchronizeDiagnostics(target, lifecycle, auxiliary, evidence, true);
    for (int frame = 0; frame < 3; ++frame)
    {
        ++lifecycle.presentedFrames;
        const DiagnosticsSynchronization synchronization = SynchronizeDiagnostics(
            target, lifecycle, auxiliary, evidence, true);
        values[0] += static_cast<std::int64_t>(synchronization.stringCopies);
        values[1] += static_cast<std::int64_t>(synchronization.vectorCopies);
    }
    return 0;
}

extern "C" int LLP_MaterialRunFakeCompositionScenario(
    std::int64_t* values,
    std::size_t valueCount)
{
    if (!values || valueCount != MaterialCompositionValueCount) return 2;
    std::fill(values, values + valueCount, 0);

    ShowRequest request;
    request.windowDesktop = { -1800, 20, -1400, 420 };
    request.dpi = 96;
    request.policy.darkMode = true;
    SceneSnapshot scene;
    scene.generation = 1;
    scene.metrics.dpi = 96;
    scene.metrics.cornerRadiusPx = 34;
    scene.metrics.search = { 40, 40, 360, 100 };
    scene.metrics.searchText = { 60, 45, 180, 95 };
    scene.policy.darkMode = true;
    auto icon = std::make_shared<IconRaster>();
    icon->width = 1;
    icon->height = 1;
    icon->stride = 4;
    icon->premultipliedBgra = { 20, 30, 40, 255 };
    scene.items.push_back({ { 220, 80, 380, 208 }, L"Result", L"Application", icon, true, false });
    const PixelRect output = { -1920, -100, 0, 980 };

    RecordingCompositionBackend recorded;
    recorded.surfaceOffset = { 7, 11 };
    for (int row = 0; row < 12; ++row)
    {
        for (int column = 0; column < 16; ++column)
        {
            recorded.luminanceGrid[static_cast<std::size_t>(row * 16 + column)]
                = column < 8 ? 0.10f : 0.90f;
        }
    }
    bool presented = false;
    if (ComposeUnifiedFrame(
            recorded, request, &scene, output, OutputRotation::Identity,
            true, true, false, presented) != FailureReason::None || !presented)
    {
        return 3;
    }
    const MaterialTone recordedTone = ResolveMaterialTone(
        recorded.RelativeLuminance(), scene.policy.darkMode);
    recorded.degenerateGuardOpacityMilli = static_cast<int>(std::lround(
        LocalGuardOpacity(
            scene.metrics.searchText, 0, 0, recorded.luminanceGrid, recordedTone) * 1000.0f));
    recorded.clampedGuardOpacityMilli = static_cast<int>(std::lround(
        LocalGuardOpacity(
            { -100, -100, 80, 80 }, 400, 400, recorded.luminanceGrid, recordedTone) * 1000.0f));
    SceneSnapshot reducedScene = scene;
    reducedScene.policy.reducedMotion = true;
    RecordingCompositionBackend reduced;
    if (ComposeUnifiedFrame(
            reduced, request, &reducedScene, output, OutputRotation::Identity,
            true, true, false, presented) != FailureReason::None || !presented)
    {
        return 3;
    }
    recorded.reducedMotionMaximumDisplacementMilli =
        reduced.reducedMotionMaximumDisplacementMilli;

    values[CompositionTargetCount] = recorded.targetCount;
    values[CompositionRootCount] = recorded.rootCount;
    values[CompositionVisualCount] = recorded.visualCount;
    values[CompositionSurfaceCount] = recorded.surfaceCount;
    values[CompositionSurfaceClears] = recorded.surfaceClears;
    values[CompositionCommandCount] = static_cast<std::int64_t>(recorded.commands.size());
    for (std::size_t index = 0; index < recorded.commands.size() && index < 6; ++index)
    {
        values[CompositionCommand0 + index] = static_cast<std::int64_t>(recorded.commands[index]);
    }
    values[CompositionDesktopReferences] = recorded.desktopReferences;
    values[CompositionOverlayDesktopReferences] = recorded.overlayDesktopReferences;
    values[CompositionContentDesktopReferences] = recorded.contentDesktopReferences;
    values[CompositionCropCount] = recorded.cropCount;
    values[CompositionRotationNormalizations] = recorded.rotationNormalizations;
    values[CompositionMaterialAtlasOffsetX] = recorded.materialAtlasOffsetX;
    values[CompositionMaterialAtlasOffsetY] = recorded.materialAtlasOffsetY;
    values[CompositionOverlayAtlasOffsetX] = recorded.overlayAtlasOffsetX;
    values[CompositionOverlayAtlasOffsetY] = recorded.overlayAtlasOffsetY;
    values[CompositionContentAtlasOffsetX] = recorded.contentAtlasOffsetX;
    values[CompositionContentAtlasOffsetY] = recorded.contentAtlasOffsetY;
    values[CompositionBlurPasses] = recorded.blurPasses;
    values[CompositionTintPasses] = recorded.tintPasses;
    values[CompositionSearchOpticalPasses] = recorded.searchOpticalPasses;
    values[CompositionSearchOpticalSamples] = recorded.searchOpticalSamples;
    values[CompositionSearchCenterDisplacementMilli] = recorded.searchCenterDisplacementMilli;
    values[CompositionSearchEdgeMaximumDisplacementMilli] = recorded.searchEdgeMaximumDisplacementMilli;
    values[CompositionSearchSourceOutOfBounds] = recorded.searchSourceOutOfBounds;
    values[CompositionSearchVerticalBiasMilli] = recorded.searchVerticalBiasMilli;
    values[CompositionSearchEdgeCommandCount] = static_cast<std::int64_t>(
        recorded.searchEdgeCommands.size());
    for (std::size_t index = 0; index < recorded.searchEdgeCommands.size() && index < 24; ++index)
    {
        values[CompositionSearchEdgeCommand0 + index] = recorded.searchEdgeCommands[index];
    }
    values[CompositionReducedMotionMaximumDisplacementMilli] = recorded.reducedMotionMaximumDisplacementMilli;
    values[CompositionClarityShapePasses] = recorded.clarityShapePasses;
    values[CompositionClarityCenterOpacityMilli] = recorded.clarityCenterOpacityMilli;
    values[CompositionClarityEdgeOpacityMilli] = recorded.clarityEdgeOpacityMilli;
    values[CompositionLocalContrastGuards] = recorded.localContrastGuards;
    values[CompositionDarkGuardOpacityMilli] = recorded.darkGuardOpacityMilli;
    values[CompositionLightGuardOpacityMilli] = recorded.lightGuardOpacityMilli;
    values[CompositionDegenerateGuardOpacityMilli] = recorded.degenerateGuardOpacityMilli;
    values[CompositionClampedGuardOpacityMilli] = recorded.clampedGuardOpacityMilli;
    values[CompositionRimPasses] = recorded.rimPasses;
    values[CompositionShadowPasses] = recorded.shadowPasses;
    values[CompositionLuminanceWidth] = recorded.luminanceWidth;
    values[CompositionLuminanceHeight] = recorded.luminanceHeight;
    values[CompositionStagingBufferCount] = recorded.stagingBufferCount;
    values[CompositionDoNotWaitMaps] = recorded.doNotWaitMaps;
    values[CompositionMinimumReadbackIntervalMs] = recorded.minimumIntervalMs;
    values[CompositionCopiedBgraIcons] = recorded.copiedBgraIcons;
    values[CompositionHiconReferences] = recorded.hiconReferences;

    RecordingCompositionBackend dirty;
    if (ComposeUnifiedFrame(
            dirty, request, &scene, output, OutputRotation::Identity,
            true, true, false, presented) != FailureReason::None) return 3;
    values[CompositionPresentedAfterDesktop] = dirty.presentedFrames;
    if (ComposeUnifiedFrame(
            dirty, request, &scene, output, OutputRotation::Identity,
            false, false, false, presented) != FailureReason::None) return 3;
    values[CompositionPresentedAfterTimeout] = dirty.presentedFrames;
    if (ComposeUnifiedFrame(
            dirty, request, &scene, output, OutputRotation::Identity,
            false, false, false, presented) != FailureReason::None) return 3;
    values[CompositionPresentedAfterUnchanged] = dirty.presentedFrames;
    scene.generation = 2;
    if (ComposeUnifiedFrame(
            dirty, request, &scene, output, OutputRotation::Identity,
            false, true, false, presented) != FailureReason::None) return 3;
    values[CompositionPresentedAfterScene] = dirty.presentedFrames;
    if (ComposeUnifiedFrame(
            dirty, request, &scene, output, OutputRotation::Identity,
            false, false, true, presented) != FailureReason::None) return 3;
    values[CompositionPresentedAfterLifecycle] = dirty.presentedFrames;
    return 0;
}

extern "C" int LLP_MaterialClassifyRequiredOperation(long result)
{
    return static_cast<int>(RequiredResourceResult(static_cast<HRESULT>(result)));
}

extern "C" int LLP_MaterialRunFakeCompositionFailureScenario(
    int failurePoint,
    std::int64_t* values,
    std::size_t valueCount)
{
    if (!values || valueCount != MaterialCompositionFailureValueCount
        || failurePoint < 0 || failurePoint > 3) return 2;
    std::fill(values, values + valueCount, 0);

    ShowRequest request;
    request.windowDesktop = { -1800, 20, -1400, 420 };
    request.dpi = 96;
    SceneSnapshot scene;
    scene.generation = 1;
    scene.metrics.dpi = 96;
    scene.metrics.cornerRadiusPx = 34;
    scene.metrics.search = { 40, 40, 360, 100 };
    scene.metrics.searchText = { 60, 45, 340, 95 };
    auto icon = std::make_shared<IconRaster>();
    icon->width = 1;
    icon->height = 1;
    icon->stride = 4;
    icon->premultipliedBgra = { 20, 30, 40, 255 };
    scene.items.push_back({ { 20, 80, 148, 208 }, L"Result", L"Application", icon, true, false });

    RecordingCompositionBackend backend;
    backend.failurePoint = failurePoint;
    bool presented = false;
    const FailureReason result = ComposeUnifiedFrame(
        backend,
        request,
        &scene,
        { -1920, -100, 0, 980 },
        OutputRotation::Identity,
        true,
        true,
        false,
        presented);
    values[CompositionFailureReason] = static_cast<std::int64_t>(result);
    values[CompositionFailureCommits] = backend.presentedFrames;
    values[CompositionFailurePresented] = presented ? 1 : 0;
    return 0;
}
#endif
}
