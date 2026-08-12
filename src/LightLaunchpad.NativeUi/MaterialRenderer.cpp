#include "MaterialRenderer.h"

#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d2d1helper.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
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

bool IsValidRect(llp::geometry::Rect rect)
{
    return rect.right > rect.left && rect.bottom > rect.top;
}

MaterialFramePlan BuildMaterialFramePlan(
    const SceneSnapshot& scene,
    const MaterialTone& tone)
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

    const float guardOpacity = std::clamp(0.12f + tone.tintOpacity * 0.50f, 0.16f, 0.24f);
    if (IsValidRect(scene.metrics.searchText))
    {
        plan.contrastGuards.push_back({
            scene.metrics.searchText,
            static_cast<float>(std::max(1, (scene.metrics.searchText.bottom - scene.metrics.searchText.top) / 2)),
            guardOpacity
        });
    }
    for (const auto& item : scene.items)
    {
        if (!item.focused || !IsValidRect(item.bounds)) continue;
        plan.contrastGuards.push_back({ item.bounds, 18.0f, guardOpacity });
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
    virtual void UpdateLuminance(
        int width,
        int height,
        int stagingBufferCount,
        UINT mapFlags,
        std::uint32_t minimumIntervalMs) = 0;
    virtual float RelativeLuminance() const = 0;
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
        backend.UpdateLuminance(16, 12, 2, D3D11_MAP_FLAG_DO_NOT_WAIT, 100);
    }
    const MaterialTone tone = ResolveMaterialTone(
        backend.RelativeLuminance(), scene.policy.darkMode);
    const MaterialFramePlan materialPlan = BuildMaterialFramePlan(scene, tone);
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
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        requestedFeatureLevels.data(),
        static_cast<UINT>(requestedFeatureLevels.size()),
        D3D11_SDK_VERSION,
        candidate.d3dDevice.Put(),
        &featureLevel,
        candidate.d3dContext.Put());
    if (FAILED(result)) return FailureReason::AdapterUnavailable;

    result = candidate.d3dDevice.Get()->QueryInterface(
        __uuidof(IDXGIDevice), candidate.dxgiDevice.PutVoid());
    if (FAILED(result)) return FailureReason::AdapterUnavailable;

    result = D2D1CreateDevice(candidate.dxgiDevice.Get(), nullptr, candidate.d2dDevice.Put());
    if (FAILED(result)) return FailureReason::CompositionUnavailable;

    result = candidate.d2dDevice.Get()->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE, candidate.d2dContext.Put());
    if (FAILED(result)) return FailureReason::CompositionUnavailable;

    result = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(candidate.dwriteFactory.Put()));
    if (FAILED(result)) return FailureReason::CompositionUnavailable;

    result = DCompositionCreateDevice(
        candidate.dxgiDevice.Get(),
        __uuidof(IDCompositionDevice),
        candidate.compositionDevice.PutVoid());
    if (FAILED(result)) return FailureReason::CompositionUnavailable;

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
};

class DesktopDuplicationBackend final : public CaptureBackend, public CompositionBackend
{
public:
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
        if (!request.monitor) return FailureReason::AdapterUnavailable;

        ComOwner<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.PutVoid())))
        {
            return FailureReason::AdapterUnavailable;
        }

        std::vector<OutputCatalogEntry> catalog;
        for (UINT adapterIndex = 0;; ++adapterIndex)
        {
            ComOwner<IDXGIAdapter1> adapter;
            const HRESULT adapterResult = factory.Get()->EnumAdapters1(adapterIndex, adapter.Put());
            if (adapterResult == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(adapterResult)) return FailureReason::AdapterUnavailable;
            for (UINT outputIndex = 0;; ++outputIndex)
            {
                ComOwner<IDXGIOutput> output;
                const HRESULT outputResult = adapter.Get()->EnumOutputs(outputIndex, output.Put());
                if (outputResult == DXGI_ERROR_NOT_FOUND) break;
                if (FAILED(outputResult)) return FailureReason::AdapterUnavailable;
                DXGI_OUTPUT_DESC description = {};
                if (SUCCEEDED(output.Get()->GetDesc(&description)))
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
        if (FAILED(factory.Get()->EnumAdapters1(
                selection.adapterIndex, selectedAdapter.Put())))
        {
            return FailureReason::AdapterUnavailable;
        }
        ComOwner<IDXGIOutput> selectedOutput;
        if (FAILED(selectedAdapter.Get()->EnumOutputs(
                selection.outputIndex, selectedOutput.Put())))
        {
            return FailureReason::AdapterUnavailable;
        }
        DXGI_OUTPUT_DESC selectedDescription = {};
        if (FAILED(selectedOutput.Get()->GetDesc(&selectedDescription)))
        {
            return FailureReason::AdapterUnavailable;
        }

        DeviceResources candidateResources;
        std::uint32_t featureLevel = 0;
        const FailureReason initialization = InitializeDevice(
            candidateResources, &featureLevel, selectedAdapter.Get());
        if (initialization != FailureReason::None) return initialization;

        ComOwner<IDXGIOutput1> output1;
        if (FAILED(selectedOutput.Get()->QueryInterface(
                __uuidof(IDXGIOutput1), output1.PutVoid())))
        {
            return FailureReason::DuplicationUnavailable;
        }
        ComOwner<IDXGIOutputDuplication> duplication;
        const HRESULT duplicateResult = output1.Get()->DuplicateOutput(
            candidateResources.d3dDevice.Get(), duplication.Put());
        if (FAILED(duplicateResult)) return FailureReason::DuplicationUnavailable;

        factory_ = std::move(factory);
        adapter_ = std::move(selectedAdapter);
        output_ = std::move(selectedOutput);
        output1_ = std::move(output1);
        duplication_ = std::move(duplication);
        resources_ = std::move(candidateResources);
        outputDesktop_ = selection.desktop;
        outputRotation_ = NormalizeOutputRotation(selectedDescription.Rotation);
        return FailureReason::None;
    }

    AcquireResult Acquire(std::uint32_t waitMilliseconds, std::uint64_t& identity) override
    {
        if (!duplication_.Get()) return AcquireResult::Failed;
        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        ComOwner<IDXGIResource> desktopResource;
        const HRESULT result = duplication_.Get()->AcquireNextFrame(
            waitMilliseconds, &frameInfo, desktopResource.Put());
        if (result == DXGI_ERROR_WAIT_TIMEOUT) return AcquireResult::Timeout;
        if (result == DXGI_ERROR_ACCESS_LOST) return AcquireResult::AccessLost;
        if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
        {
            return AcquireResult::DeviceRemoved;
        }
        if (FAILED(result)) return AcquireResult::Failed;
        frameAcquired_ = true;
        acquiredResource_ = std::move(desktopResource);
        ComOwner<ID3D11Texture2D> acquiredTexture;
        if (FAILED(acquiredResource_.Get()->QueryInterface(
                __uuidof(ID3D11Texture2D), acquiredTexture.PutVoid())))
        {
            return AcquireResult::Failed;
        }
        D3D11_TEXTURE2D_DESC sourceDescription = {};
        acquiredTexture.Get()->GetDesc(&sourceDescription);
        D3D11_TEXTURE2D_DESC retainedDescription = sourceDescription;
        retainedDescription.Usage = D3D11_USAGE_DEFAULT;
        retainedDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        retainedDescription.CPUAccessFlags = 0;
        retainedDescription.MiscFlags = 0;
        bool recreateRetained = !retainedDesktop_.Get();
        if (!recreateRetained)
        {
            D3D11_TEXTURE2D_DESC existing = {};
            retainedDesktop_.Get()->GetDesc(&existing);
            recreateRetained = existing.Width != retainedDescription.Width
                || existing.Height != retainedDescription.Height
                || existing.Format != retainedDescription.Format;
        }
        if (recreateRetained)
        {
            retainedDesktop_.Reset();
            if (FAILED(resources_.d3dDevice.Get()->CreateTexture2D(
                    &retainedDescription, nullptr, retainedDesktop_.Put())))
            {
                return AcquireResult::Failed;
            }
        }
        resources_.d3dContext.Get()->CopyResource(retainedDesktop_.Get(), acquiredTexture.Get());
        identity = frameInfo.LastPresentTime.QuadPart != 0
            ? static_cast<std::uint64_t>(frameInfo.LastPresentTime.QuadPart)
            : static_cast<std::uint64_t>(Counter());
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
        if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
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
        if (FAILED(resources_.compositionDevice.Get()->CreateTargetForHwnd(
                request.window, TRUE, compositionTarget_.Put())))
        {
            return FailureReason::CompositionUnavailable;
        }
        if (FAILED(resources_.compositionDevice.Get()->CreateVisual(rootVisual_.Put())))
        {
            ResetComposition();
            return FailureReason::CompositionUnavailable;
        }
        for (std::size_t index = 0; index < visuals_.size(); ++index)
        {
            if (FAILED(resources_.compositionDevice.Get()->CreateVisual(visuals_[index].Put()))
                || FAILED(resources_.compositionDevice.Get()->CreateSurface(
                    width,
                    height,
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    DXGI_ALPHA_MODE_PREMULTIPLIED,
                    surfaces_[index].Put()))
                || FAILED(visuals_[index].Get()->SetContent(surfaces_[index].Get())))
            {
                ResetComposition();
                return FailureReason::CompositionUnavailable;
            }
        }
        if (FAILED(rootVisual_.Get()->AddVisual(visuals_[0].Get(), FALSE, nullptr))
            || FAILED(rootVisual_.Get()->AddVisual(visuals_[1].Get(), TRUE, visuals_[0].Get()))
            || FAILED(rootVisual_.Get()->AddVisual(visuals_[2].Get(), TRUE, visuals_[1].Get()))
            || FAILED(compositionTarget_.Get()->SetRoot(rootVisual_.Get())))
        {
            ResetComposition();
            return FailureReason::CompositionUnavailable;
        }
        surfaceWidth_ = width;
        surfaceHeight_ = height;
        return FailureReason::None;
    }

    FailureReason ClearTransparent() override
    {
        for (std::size_t index = 0; index < surfaces_.size(); ++index)
        {
            const FailureReason result = DrawSurface(index, [](ID2D1DeviceContext* context) {
                context->Clear(D2D1::ColorF(0.0f, 0.0f));
            });
            if (result != FailureReason::None) return result;
        }
        return FailureReason::None;
    }

    FailureReason SetDesktopInput(PixelRect crop, OutputRotation rotation) override
    {
        if (!retainedDesktop_.Get()) return FailureReason::DuplicationUnavailable;
        currentCrop_ = crop;
        currentRotation_ = rotation;
        return FailureReason::None;
    }

    void UpdateLuminance(
        int width,
        int height,
        int stagingBufferCount,
        UINT mapFlags,
        std::uint32_t minimumIntervalMs) override
    {
        if (!retainedDesktop_.Get() || width != 16 || height != 12 || stagingBufferCount != 2
            || mapFlags != D3D11_MAP_FLAG_DO_NOT_WAIT) return;
        const std::int64_t now = Counter();
        const std::int64_t frequency = std::max<std::int64_t>(1, Frequency());
        if (lastLuminanceCounter_ != 0
            && static_cast<std::uint64_t>(std::max<std::int64_t>(0, now - lastLuminanceCounter_))
                    * 1000ull / static_cast<std::uint64_t>(frequency) < minimumIntervalMs)
        {
            return;
        }
        if (!EnsureLuminanceResources()) return;

        ComOwner<IDXGISurface> desktopSurface;
        ComOwner<IDXGISurface> targetSurface;
        if (FAILED(retainedDesktop_.Get()->QueryInterface(
                __uuidof(IDXGISurface), desktopSurface.PutVoid()))
            || FAILED(luminanceTarget_.Get()->QueryInterface(
                __uuidof(IDXGISurface), targetSurface.PutVoid()))) return;
        D2D1_BITMAP_PROPERTIES1 sourceProperties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        D2D1_BITMAP_PROPERTIES1 targetProperties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        ComOwner<ID2D1Bitmap1> sourceBitmap;
        ComOwner<ID2D1Bitmap1> targetBitmap;
        if (FAILED(resources_.d2dContext.Get()->CreateBitmapFromDxgiSurface(
                desktopSurface.Get(), &sourceProperties, sourceBitmap.Put()))
            || FAILED(resources_.d2dContext.Get()->CreateBitmapFromDxgiSurface(
                targetSurface.Get(), &targetProperties, targetBitmap.Put()))) return;

        resources_.d2dContext.Get()->SetTarget(targetBitmap.Get());
        resources_.d2dContext.Get()->BeginDraw();
        resources_.d2dContext.Get()->SetTransform(D2D1::Matrix3x2F::Identity());
        resources_.d2dContext.Get()->DrawBitmap(
            sourceBitmap.Get(),
            D2D1::RectF(0.0f, 0.0f, 16.0f, 12.0f),
            1.0f,
            D2D1_INTERPOLATION_MODE_LINEAR,
            D2D1::RectF(
                static_cast<float>(currentCrop_.left),
                static_cast<float>(currentCrop_.top),
                static_cast<float>(currentCrop_.right),
                static_cast<float>(currentCrop_.bottom)));
        const HRESULT drawResult = resources_.d2dContext.Get()->EndDraw();
        resources_.d2dContext.Get()->SetTarget(nullptr);
        if (FAILED(drawResult)) return;

        const std::size_t writeIndex = nextLuminanceBuffer_;
        resources_.d3dContext.Get()->CopyResource(
            luminanceStaging_[writeIndex].Get(), luminanceTarget_.Get());
        nextLuminanceBuffer_ = (nextLuminanceBuffer_ + 1) % luminanceStaging_.size();
        ++luminanceCopies_;
        lastLuminanceCounter_ = now;
        if (luminanceCopies_ < 2) return;

        const std::size_t readIndex = nextLuminanceBuffer_;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT mapResult = resources_.d3dContext.Get()->Map(
            luminanceStaging_[readIndex].Get(),
            0,
            D3D11_MAP_READ,
            mapFlags,
            &mapped);
        if (mapResult == DXGI_ERROR_WAS_STILL_DRAWING) return;
        if (FAILED(mapResult)) return;
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
    }

    float RelativeLuminance() const override { return relativeLuminance_; }

    FailureReason DrawMaterial(
        const SceneSnapshot& scene,
        const MaterialTone& tone,
        const MaterialFramePlan& plan) override
    {
        ComOwner<IDXGISurface> desktopSurface;
        if (!retainedDesktop_.Get() || FAILED(retainedDesktop_.Get()->QueryInterface(
                __uuidof(IDXGISurface), desktopSurface.PutVoid())))
        {
            return FailureReason::DuplicationUnavailable;
        }
        return DrawSurface(0, [&](ID2D1DeviceContext* context) {
            D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
            ComOwner<ID2D1Bitmap1> desktopBitmap;
            if (FAILED(context->CreateBitmapFromDxgiSurface(
                    desktopSurface.Get(), &properties, desktopBitmap.Put()))) return;
            ComOwner<ID2D1Effect> blur;
            if (FAILED(context->CreateEffect(CLSID_D2D1GaussianBlur, blur.Put()))) return;
            blur.Get()->SetInput(0, desktopBitmap.Get());
            blur.Get()->SetValue(
                D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
                tone.blurRadiusDip * static_cast<float>(std::max(1, scene.metrics.dpi)) / 96.0f);
            blur.Get()->SetValue(
                D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,
                D2D1_BORDER_MODE_HARD);

            ComOwner<ID2D1Factory> factory;
            resources_.d2dDevice.Get()->GetFactory(factory.Put());
            ComOwner<ID2D1RoundedRectangleGeometry> boundary;
            const float radius = static_cast<float>(std::max(0, scene.metrics.cornerRadiusPx));
            factory.Get()->CreateRoundedRectangleGeometry(
                D2D1::RoundedRect(
                    D2D1::RectF(0.0f, 0.0f, static_cast<float>(surfaceWidth_), static_cast<float>(surfaceHeight_)),
                    radius,
                    radius),
                boundary.Put());
            ComOwner<ID2D1Layer> layer;
            context->CreateLayer(layer.Put());
            D2D1_LAYER_PARAMETERS1 parameters = D2D1::LayerParameters1();
            parameters.geometricMask = boundary.Get();
            context->PushLayer(parameters, layer.Get());

            const D2D1_MATRIX_3X2_F baseTransform = CropTransform(currentCrop_, currentRotation_);
            const D2D1_RECT_F desktopSource = D2D1::RectF(
                static_cast<float>(currentCrop_.left),
                static_cast<float>(currentCrop_.top),
                static_cast<float>(currentCrop_.right),
                static_cast<float>(currentCrop_.bottom));
            if (plan.drawBlur)
            {
                context->SetTransform(baseTransform);
                context->DrawImage(
                    blur.Get(),
                    D2D1::Point2F(0.0f, 0.0f),
                    desktopSource,
                    D2D1_INTERPOLATION_MODE_LINEAR,
                    D2D1_COMPOSITE_MODE_SOURCE_COPY);
            }
            context->SetTransform(D2D1::Matrix3x2F::Identity());

            if (plan.searchOpticalPoints.size() == 5 && IsValidRect(plan.searchBoundsPx))
            {
                ComOwner<ID2D1RoundedRectangleGeometry> searchBoundary;
                const float searchRadius = static_cast<float>(
                    std::max(1, (plan.searchBoundsPx.bottom - plan.searchBoundsPx.top) / 2));
                factory.Get()->CreateRoundedRectangleGeometry(
                    D2D1::RoundedRect(D2DRect(plan.searchBoundsPx), searchRadius, searchRadius),
                    searchBoundary.Put());
                ComOwner<ID2D1Layer> searchLayer;
                context->CreateLayer(searchLayer.Put());
                D2D1_LAYER_PARAMETERS1 searchParameters = D2D1::LayerParameters1();
                searchParameters.geometricMask = searchBoundary.Get();
                context->PushLayer(searchParameters, searchLayer.Get());
                const float edgeBandPx = std::max(1.0f, 13.0f * static_cast<float>(std::max(1, scene.metrics.dpi)) / 96.0f);
                const std::array<D2D1_RECT_F, 4> edgeBands = { {
                    D2D1::RectF(
                        static_cast<float>(plan.searchBoundsPx.left),
                        static_cast<float>(plan.searchBoundsPx.top),
                        std::min(static_cast<float>(plan.searchBoundsPx.right), static_cast<float>(plan.searchBoundsPx.left) + edgeBandPx),
                        static_cast<float>(plan.searchBoundsPx.bottom)),
                    D2D1::RectF(
                        std::max(static_cast<float>(plan.searchBoundsPx.left), static_cast<float>(plan.searchBoundsPx.right) - edgeBandPx),
                        static_cast<float>(plan.searchBoundsPx.top),
                        static_cast<float>(plan.searchBoundsPx.right),
                        static_cast<float>(plan.searchBoundsPx.bottom)),
                    D2D1::RectF(
                        static_cast<float>(plan.searchBoundsPx.left),
                        static_cast<float>(plan.searchBoundsPx.top),
                        static_cast<float>(plan.searchBoundsPx.right),
                        std::min(static_cast<float>(plan.searchBoundsPx.bottom), static_cast<float>(plan.searchBoundsPx.top) + edgeBandPx)),
                    D2D1::RectF(
                        static_cast<float>(plan.searchBoundsPx.left),
                        std::max(static_cast<float>(plan.searchBoundsPx.top), static_cast<float>(plan.searchBoundsPx.bottom) - edgeBandPx),
                        static_cast<float>(plan.searchBoundsPx.right),
                        static_cast<float>(plan.searchBoundsPx.bottom))
                } };
                const float pixelsPerDip = static_cast<float>(std::max(1, scene.metrics.dpi)) / 96.0f;
                for (std::size_t index = 1; index < plan.searchOpticalPoints.size(); ++index)
                {
                    const auto& point = plan.searchOpticalPoints[index];
                    const float deltaX = (point.sample.sourceXDip - point.destinationXDip) * pixelsPerDip;
                    const float deltaY = (point.sample.sourceYDip - point.destinationYDip) * pixelsPerDip;
                    context->PushAxisAlignedClip(edgeBands[index - 1], D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    context->SetTransform(baseTransform * D2D1::Matrix3x2F::Translation(-deltaX, -deltaY));
                    context->DrawImage(
                        blur.Get(),
                        D2D1::Point2F(0.0f, 0.0f),
                        desktopSource,
                        D2D1_INTERPOLATION_MODE_LINEAR,
                        D2D1_COMPOSITE_MODE_SOURCE_COPY);
                    context->PopAxisAlignedClip();
                }
                context->SetTransform(D2D1::Matrix3x2F::Identity());
                context->PopLayer();
            }

            ComOwner<ID2D1SolidColorBrush> tint;
            context->CreateSolidColorBrush(D2DColor(tone.tint, tone.tintOpacity), tint.Put());
            if (plan.drawTint && tint.Get()) context->FillRectangle(
                D2D1::RectF(0.0f, 0.0f, static_cast<float>(surfaceWidth_), static_cast<float>(surfaceHeight_)),
                tint.Get());
            if (plan.drawClarityShape)
            {
                const std::array<D2D1_GRADIENT_STOP, 2> stops = { {
                    { 0.0f, D2DColor(tone.guardedSurface, plan.clarityCenterOpacity) },
                    { 1.0f, D2DColor(tone.guardedSurface, plan.clarityEdgeOpacity) }
                } };
                ComOwner<ID2D1GradientStopCollection> gradientStops;
                context->CreateGradientStopCollection(
                    stops.data(), static_cast<UINT32>(stops.size()),
                    D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, gradientStops.Put());
                ComOwner<ID2D1RadialGradientBrush> clarity;
                if (gradientStops.Get()) context->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(
                        D2D1::Point2F(surfaceWidth_ * 0.5f, surfaceHeight_ * 0.5f),
                        D2D1::Point2F(0.0f, 0.0f),
                        std::max(1.0f, surfaceWidth_ * 0.65f),
                        std::max(1.0f, surfaceHeight_ * 0.65f)),
                    gradientStops.Get(), clarity.Put());
                if (clarity.Get()) context->FillRectangle(
                    D2D1::RectF(0.0f, 0.0f, static_cast<float>(surfaceWidth_), static_cast<float>(surfaceHeight_)),
                    clarity.Get());
            }
            for (const auto& guard : plan.contrastGuards)
            {
                ComOwner<ID2D1SolidColorBrush> guardBrush;
                context->CreateSolidColorBrush(
                    D2DColor(tone.guardedSurface, guard.opacity), guardBrush.Put());
                if (guardBrush.Get()) context->FillRoundedRectangle(
                    D2D1::RoundedRect(D2DRect(guard.bounds), guard.radiusPx, guard.radiusPx),
                    guardBrush.Get());
            }
            ComOwner<ID2D1SolidColorBrush> shadow;
            context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.20f), shadow.Put());
            if (plan.drawShadow && shadow.Get()) context->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(2.0f, 3.0f, static_cast<float>(surfaceWidth_ - 2), static_cast<float>(surfaceHeight_ - 1)),
                    radius,
                    radius),
                shadow.Get(),
                4.0f);
            ComOwner<ID2D1SolidColorBrush> rim;
            context->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.30f), rim.Put());
            if (plan.drawRim && rim.Get()) context->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(0.5f, 0.5f, static_cast<float>(surfaceWidth_) - 0.5f, static_cast<float>(surfaceHeight_) - 0.5f),
                    radius,
                    radius),
                rim.Get(),
                1.0f);
            context->PopLayer();
        });
    }

    FailureReason DrawOverlay(const SceneSnapshot& scene) override
    {
        return DrawSurface(1, [&](ID2D1DeviceContext* context) {
            const auto& search = scene.metrics.search;
            const float searchRadius = static_cast<float>(std::max(1, (search.bottom - search.top) / 2));
            ComOwner<ID2D1SolidColorBrush> searchFill;
            context->CreateSolidColorBrush(D2D1::ColorF(1.0f, scene.search.active ? 0.16f : 0.10f), searchFill.Put());
            if (searchFill.Get()) context->FillRoundedRectangle(
                D2D1::RoundedRect(D2DRect(search), searchRadius, searchRadius), searchFill.Get());
            ComOwner<ID2D1SolidColorBrush> edge;
            context->CreateSolidColorBrush(D2D1::ColorF(1.0f, scene.search.active ? 0.55f : 0.28f), edge.Put());
            if (edge.Get()) context->DrawRoundedRectangle(
                D2D1::RoundedRect(D2DRect(search), searchRadius, searchRadius), edge.Get(), 1.0f);
            for (const auto& item : scene.items)
            {
                if (!item.focused || !IsValidRect(item.bounds)) continue;
                const D2D1_ROUNDED_RECT tile = D2D1::RoundedRect(
                    D2DRect(item.bounds),
                    18.0f, 18.0f);
                ComOwner<ID2D1SolidColorBrush> focusShadow;
                context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.20f), focusShadow.Put());
                if (focusShadow.Get()) context->DrawRoundedRectangle(tile, focusShadow.Get(), 5.0f);
                ComOwner<ID2D1SolidColorBrush> focusFill;
                context->CreateSolidColorBrush(D2DColor(scene.policy.focus, 0.20f), focusFill.Put());
                if (focusFill.Get()) context->FillRoundedRectangle(tile, focusFill.Get());
                ComOwner<ID2D1SolidColorBrush> focusRim;
                context->CreateSolidColorBrush(D2DColor(scene.policy.focus, 0.90f), focusRim.Put());
                if (focusRim.Get()) context->DrawRoundedRectangle(tile, focusRim.Get(), 2.0f);
            }
        });
    }

    FailureReason DrawContent(const SceneSnapshot& scene) override
    {
        return DrawSurface(2, [&](ID2D1DeviceContext* context) {
            ComOwner<ID2D1SolidColorBrush> textBrush;
            context->CreateSolidColorBrush(D2DColor(scene.policy.text), textBrush.Put());
            if (!textBrush.Get()) return;
            ComOwner<IDWriteTextFormat> searchFormat;
            ComOwner<IDWriteTextFormat> titleFormat;
            ComOwner<IDWriteTextFormat> subtitleFormat;
            resources_.dwriteFactory.Get()->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(std::max(1, scene.metrics.searchFontPx)),
                L"", searchFormat.Put());
            resources_.dwriteFactory.Get()->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(std::max(1, scene.metrics.tileFontPx)),
                L"", titleFormat.Put());
            resources_.dwriteFactory.Get()->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(std::max(1, scene.metrics.tileFontPx - 2)),
                L"", subtitleFormat.Put());

            const auto& searchRect = scene.metrics.searchText;
            const float searchOrigin = static_cast<float>(searchRect.left - scene.search.horizontalOffsetPx);
            const float searchHeight = static_cast<float>(std::max(1, searchRect.bottom - searchRect.top));
            const std::size_t textLength = scene.search.displayText.size();
            const std::size_t selectionStart = std::min(scene.search.selectionStart, textLength);
            const std::size_t selectionEnd = std::min(scene.search.selectionEnd, textLength);
            const std::size_t compositionStart = std::min(scene.search.compositionStart, textLength);
            const std::size_t compositionEnd = std::min(scene.search.compositionEnd, textLength);
            const std::size_t caret = std::min(scene.search.caret, textLength);
            const float selectionLeft = searchOrigin + MeasurePrefix(scene.search.displayText, selectionStart, searchFormat.Get());
            const float selectionRight = searchOrigin + MeasurePrefix(scene.search.displayText, selectionEnd, searchFormat.Get());
            if (selectionEnd > selectionStart)
            {
                ComOwner<ID2D1SolidColorBrush> selectionBrush;
                context->CreateSolidColorBrush(D2DColor(scene.policy.focus, 0.36f), selectionBrush.Put());
                if (selectionBrush.Get()) context->FillRectangle(
                    D2D1::RectF(selectionLeft, static_cast<float>(searchRect.top), selectionRight, static_cast<float>(searchRect.bottom)),
                    selectionBrush.Get());
            }
            if (searchFormat.Get()) context->DrawText(
                scene.search.displayText.c_str(),
                static_cast<UINT32>(scene.search.displayText.size()),
                searchFormat.Get(),
                D2D1::RectF(searchOrigin, static_cast<float>(searchRect.top),
                    searchOrigin + static_cast<float>(std::max(1, searchRect.right - searchRect.left)),
                    static_cast<float>(searchRect.bottom)),
                textBrush.Get());
            if (scene.search.composing && compositionEnd > compositionStart)
            {
                const float start = searchOrigin + MeasurePrefix(scene.search.displayText, compositionStart, searchFormat.Get());
                const float end = searchOrigin + MeasurePrefix(scene.search.displayText, compositionEnd, searchFormat.Get());
                context->DrawLine(
                    D2D1::Point2F(start, static_cast<float>(searchRect.bottom - 3)),
                    D2D1::Point2F(end, static_cast<float>(searchRect.bottom - 3)),
                    textBrush.Get(), 1.5f);
            }
            if (scene.search.active)
            {
                const float caretX = searchOrigin + MeasurePrefix(scene.search.displayText, caret, searchFormat.Get());
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
                    if (SUCCEEDED(context->CreateBitmap(
                            D2D1::SizeU(
                                static_cast<UINT32>(item.icon->width),
                                static_cast<UINT32>(item.icon->height)),
                            item.icon->premultipliedBgra.data(),
                            static_cast<UINT32>(item.icon->stride),
                            &iconProperties,
                            iconBitmap.Put())))
                    {
                        const float left = static_cast<float>(item.bounds.left
                            + std::max(0, (item.bounds.right - item.bounds.left - item.icon->width) / 2));
                        const float top = static_cast<float>(item.bounds.top + 8);
                        context->DrawBitmap(iconBitmap.Get(), D2D1::RectF(
                            left, top, left + item.icon->width, top + item.icon->height));
                    }
                }
                const float textTop = static_cast<float>(item.bounds.bottom - std::max(36, scene.metrics.tileFontPx * 2));
                if (titleFormat.Get()) context->DrawText(
                    item.title.c_str(), static_cast<UINT32>(item.title.size()), titleFormat.Get(),
                    D2D1::RectF(static_cast<float>(item.bounds.left), textTop,
                        static_cast<float>(item.bounds.right), static_cast<float>(item.bounds.bottom)),
                    textBrush.Get());
                if (!item.subtitle.empty() && subtitleFormat.Get()) context->DrawText(
                    item.subtitle.c_str(), static_cast<UINT32>(item.subtitle.size()), subtitleFormat.Get(),
                    D2D1::RectF(static_cast<float>(item.bounds.left), textTop + scene.metrics.tileFontPx,
                        static_cast<float>(item.bounds.right), static_cast<float>(item.bounds.bottom)),
                    textBrush.Get());
            }
        });
    }

    FailureReason Commit() override
    {
        return SUCCEEDED(resources_.compositionDevice.Get()->Commit())
            ? FailureReason::None
            : FailureReason::CompositionUnavailable;
    }

    void Freeze() override
    {
        (void)ReleaseFrame();
        duplication_.Reset();
        output1_.Reset();
        output_.Reset();
        adapter_.Reset();
        factory_.Reset();
    }

    void Stop() override
    {
        (void)ReleaseFrame();
        acquiredResource_.Reset();
        retainedDesktop_.Reset();
        ResetComposition();
        ResetLuminance();
        duplication_.Reset();
        output1_.Reset();
        output_.Reset();
        adapter_.Reset();
        factory_.Reset();
        resources_ = DeviceResources{};
        outputDesktop_ = {};
        outputRotation_ = OutputRotation::Identity;
        lastSceneGeneration_ = std::numeric_limits<std::uint64_t>::max();
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
        if (FAILED(result)) return FailureReason::CompositionUnavailable;

        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComOwner<ID2D1Bitmap1> target;
        result = resources_.d2dContext.Get()->CreateBitmapFromDxgiSurface(
            dxgiSurface.Get(), &properties, target.Put());
        if (SUCCEEDED(result))
        {
            resources_.d2dContext.Get()->SetTarget(target.Get());
            resources_.d2dContext.Get()->BeginDraw();
            resources_.d2dContext.Get()->SetTransform(D2D1::Matrix3x2F::Translation(
                static_cast<float>(offset.x), static_cast<float>(offset.y)));
            draw(resources_.d2dContext.Get());
            result = resources_.d2dContext.Get()->EndDraw();
            resources_.d2dContext.Get()->SetTarget(nullptr);
        }
        const HRESULT surfaceResult = surfaces_[index].Get()->EndDraw();
        return SUCCEEDED(result) && SUCCEEDED(surfaceResult)
            ? FailureReason::None
            : FailureReason::CompositionUnavailable;
    }

    static D2D1_MATRIX_3X2_F CropTransform(PixelRect crop, OutputRotation rotation)
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

    float MeasurePrefix(
        const std::wstring& text,
        std::size_t length,
        IDWriteTextFormat* format) const
    {
        if (!format || length == 0) return 0.0f;
        const std::size_t bounded = std::min(length, text.size());
        ComOwner<IDWriteTextLayout> layout;
        if (FAILED(resources_.dwriteFactory.Get()->CreateTextLayout(
                text.c_str(), static_cast<UINT32>(bounded), format,
                100000.0f, 1000.0f, layout.Put()))) return 0.0f;
        DWRITE_TEXT_METRICS metrics = {};
        return SUCCEEDED(layout.Get()->GetMetrics(&metrics))
            ? metrics.widthIncludingTrailingWhitespace
            : 0.0f;
    }

    bool EnsureLuminanceResources()
    {
        if (luminanceTarget_.Get() && luminanceStaging_[0].Get()
            && luminanceStaging_[1].Get()) return true;
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
        if (FAILED(resources_.d3dDevice.Get()->CreateTexture2D(
                &target, nullptr, luminanceTarget_.Put()))) return false;
        D3D11_TEXTURE2D_DESC staging = target;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.BindFlags = 0;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (auto& buffer : luminanceStaging_)
        {
            if (FAILED(resources_.d3dDevice.Get()->CreateTexture2D(
                    &staging, nullptr, buffer.Put())))
            {
                ResetLuminance();
                return false;
            }
        }
        return true;
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
    float relativeLuminance_ = 0.5f;
    std::array<float, 16 * 12> luminanceGrid_ = {};
    std::int64_t lastLuminanceCounter_ = 0;
    std::size_t nextLuminanceBuffer_ = 0;
    std::uint64_t luminanceCopies_ = 0;
    DeviceResources resources_;
    ComOwner<IDXGIResource> acquiredResource_;
    ComOwner<ID3D11Texture2D> retainedDesktop_;
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
};

class CaptureLifecycle final
{
public:
    void FailStart(CaptureBackend& backend, ShowRequest request, FailureReason failure)
    {
        CleanupVisible(backend);
        request_ = std::move(request);
        diagnostics_.state = LifecycleState::Starting;
        diagnostics_.path = RenderPath::Fallback;
        diagnostics_.failure = FailureReason::None;
        diagnostics_.evidenceFrozen = false;
        EnterFallback(backend, failure);
    }

    void Show(CaptureBackend& backend, ShowRequest request, bool requestedRebuild = false)
    {
        CleanupVisible(backend);
        request_ = std::move(request);
        rebuildUsed_ = false;
        diagnostics_.state = requestedRebuild
            ? LifecycleState::Rebuilding
            : LifecycleState::Starting;
        if (requestedRebuild) ++diagnostics_.deviceRebuilds;
        diagnostics_.path = RenderPath::Fallback;
        diagnostics_.failure = FailureReason::None;
        diagnostics_.evidenceFrozen = false;

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
        capturing_ = true;
        firstFramePending_ = true;
        firstFrameStarted_ = backend.Counter();
    }

    void Step(
        CaptureBackend& backend,
        const SceneSnapshot* scene = nullptr,
        bool sceneChanged = false)
    {
        if (!capturing_) return;
        const ReleaseResult releaseResult = ReleaseHeldFrame(backend);
        if (releaseResult != ReleaseResult::Released)
        {
            const FailureReason releaseFailure = releaseResult == ReleaseResult::AccessLost
                ? FailureReason::AccessLost
                : (releaseResult == ReleaseResult::DeviceRemoved
                    ? FailureReason::DeviceRemoved
                    : FailureReason::DuplicationUnavailable);
            RecoverOrFallback(backend, releaseFailure, releaseFailure);
            return;
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
                return;
            }
            waitMilliseconds = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(16, 32 - elapsedMilliseconds));
        }

        std::uint64_t identity = 0;
        const bool forcedAccessLost = request_.forcedFailure == FailureReason::AccessLost
            && IsDevelopmentBuild();
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
                    EnterFallback(backend, composition);
                    return;
                }
                if (presented) ++diagnostics_.presentedFrames;
            }
            return;
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
                    return;
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
                EnterFallback(backend, composition);
                return;
            }
            if (presented) ++diagnostics_.presentedFrames;
            return;
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
    }

    void Hide(CaptureBackend& backend)
    {
        diagnostics_.state = LifecycleState::Stopping;
        CleanupVisible(backend);
        diagnostics_.state = LifecycleState::Hidden;
        diagnostics_.path = RenderPath::Fallback;
        diagnostics_.failure = FailureReason::None;
        diagnostics_.evidenceFrozen = false;
    }

    void Freeze(CaptureBackend& backend, const SceneSnapshot* scene = nullptr)
    {
        if (diagnostics_.state == LifecycleState::Hidden
            || diagnostics_.state == LifecycleState::Stopping) return;
        bool presented = false;
        if (backend.Compose(request_, scene, false, false, true, presented)
            == FailureReason::None && presented)
        {
            ++diagnostics_.presentedFrames;
        }
        (void)ReleaseHeldFrame(backend);
        backend.Freeze();
        backend.ClearCaptureExclusion();
        capturing_ = false;
        firstFramePending_ = false;
        diagnostics_.state = LifecycleState::FrozenEvidence;
        diagnostics_.evidenceFrozen = true;
    }

    void Shutdown(CaptureBackend& backend)
    {
        CleanupVisible(backend);
    }

    bool IsCapturing() const { return capturing_; }
    const Diagnostics& CurrentDiagnostics() const { return diagnostics_; }

private:
    ReleaseResult ReleaseHeldFrame(CaptureBackend& backend)
    {
        if (!frameHeld_) return ReleaseResult::Released;
        const ReleaseResult result = backend.ReleaseFrame();
        frameHeld_ = false;
        return result;
    }

    void RecoverOrFallback(
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
            const FailureReason rebuildFailure = backend.Start(request_);
            if (rebuildFailure == FailureReason::None)
            {
                firstFramePending_ = true;
                firstFrameStarted_ = backend.Counter();
                return;
            }
            EnterFallback(backend, rebuildFailure);
            return;
        }
        EnterFallback(backend, publishedFailure);
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
    }

    ShowRequest request_;
    Diagnostics diagnostics_;
    bool capturing_ = false;
    bool firstFramePending_ = false;
    bool frameHeld_ = false;
    bool rebuildUsed_ = false;
    std::int64_t firstFrameStarted_ = 0;
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
            hideRequested_ = true;
            showRequested_ = false;
            rebuildRequested_ = false;
        }
        workCondition_.notify_one();
    }

    void FreezeEvidence()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            freezeRequested_ = true;
        }
        workCondition_.notify_one();
    }

    Diagnostics SnapshotDiagnostics() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return diagnostics_;
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

private:
    void PublishDiagnostics(const CaptureLifecycle& lifecycle)
    {
        const std::uint64_t droppedScenes = diagnostics_.droppedScenes;
        Diagnostics updated = lifecycle.CurrentDiagnostics();
        updated.droppedScenes = droppedScenes;
        const bool notifyHost = updated.path != diagnostics_.path
            || updated.failure != diagnostics_.failure
            || updated.state != diagnostics_.state
            || updated.deviceRebuilds != diagnostics_.deviceRebuilds
            || updated.evidenceFrozen != diagnostics_.evidenceFrozen;
        diagnostics_ = updated;
        if (notifyHost && request_.window && request_.statusMessage)
        {
            PostMessageW(request_.window, request_.statusMessage, 0, 0);
        }
    }

    void Run()
    {
        const HRESULT apartmentResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool apartmentReady = SUCCEEDED(apartmentResult);
        DesktopDuplicationBackend backend;
        CaptureLifecycle lifecycle;

        std::unique_lock<std::mutex> lock(mutex_);
        for (;;)
        {
            if (!lifecycle.IsCapturing())
            {
                workCondition_.wait(lock, [this]() {
                    return shutdown_ || showRequested_ || hideRequested_ || rebuildRequested_
                        || freezeRequested_ || pendingScene_;
                });
            }
            if (shutdown_) break;

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
                lock.unlock();
                lifecycle.Hide(backend);
                lock.lock();
                PublishDiagnostics(lifecycle);
                hideRequested_ = false;
                hiddenCondition_.notify_all();
                continue;
            }

            const bool rebuild = rebuildRequested_;
            const bool show = showRequested_;
            if (show || rebuild)
            {
                showRequested_ = false;
                rebuildRequested_ = false;
                diagnostics_.state = rebuild ? LifecycleState::Rebuilding : LifecycleState::Starting;
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
                PublishDiagnostics(lifecycle);
            }

            if (freezeRequested_)
            {
                freezeRequested_ = false;
                lock.unlock();
                lifecycle.Freeze(backend, currentScene_.get());
                lock.lock();
                PublishDiagnostics(lifecycle);
                continue;
            }

            if (lifecycle.IsCapturing())
            {
                lock.unlock();
                lifecycle.Step(backend, currentScene_.get(), sceneChanged);
                lock.lock();
                PublishDiagnostics(lifecycle);
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
    bool shutdown_ = false;
    bool showRequested_ = false;
    bool hideRequested_ = false;
    bool rebuildRequested_ = false;
    bool freezeRequested_ = false;
    ShowRequest request_;
    std::shared_ptr<const SceneSnapshot> pendingScene_;
    std::shared_ptr<const SceneSnapshot> currentScene_;
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

MaterialRenderer::MaterialRenderer() : impl_(std::make_unique<Impl>()) {}

MaterialRenderer::~MaterialRenderer() = default;

void MaterialRenderer::Show(ShowRequest request)
{
    impl_->Show(std::move(request));
}

void MaterialRenderer::Submit(std::shared_ptr<const SceneSnapshot> scene)
{
    impl_->Submit(std::move(scene));
}

void MaterialRenderer::Rebuild(ShowRequest request)
{
    impl_->Rebuild(std::move(request));
}

void MaterialRenderer::Hide()
{
    impl_->Hide();
}

void MaterialRenderer::FreezeEvidence()
{
    impl_->FreezeEvidence();
}

Diagnostics MaterialRenderer::SnapshotDiagnostics() const
{
    return impl_->SnapshotDiagnostics();
}

bool MaterialRenderer::WaitUntilHiddenForTest(std::uint32_t timeoutMs) const
{
    return impl_->WaitUntilHiddenForTest(timeoutMs);
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
    CompositionBlurPasses,
    CompositionTintPasses,
    CompositionSearchOpticalPasses,
    CompositionSearchOpticalSamples,
    CompositionSearchCenterDisplacementMilli,
    CompositionSearchEdgeMaximumDisplacementMilli,
    CompositionSearchSourceOutOfBounds,
    CompositionSearchVerticalBiasMilli,
    CompositionReducedMotionMaximumDisplacementMilli,
    CompositionClarityShapePasses,
    CompositionClarityCenterOpacityMilli,
    CompositionClarityEdgeOpacityMilli,
    CompositionLocalContrastGuards,
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
    }

    FailureReason Start(const ShowRequest& request) override
    {
        ++startCalls;
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
        if (scenario_ == 7) releaseBeforeFreeze = true;
        frameOutstanding = false;
        if (scenario_ == 10 && startCalls == 1) return ReleaseResult::AccessLost;
        if (scenario_ == 12) return ReleaseResult::Failed;
        return ReleaseResult::Released;
    }

    FailureReason Compose(
        const ShowRequest&,
        const SceneSnapshot*,
        bool,
        bool,
        bool,
        bool& presented) override
    {
        presented = false;
        return FailureReason::None;
    }

    void Freeze() override {}

    void Stop() override
    {
        if ((scenario_ == 2 || scenario_ == 3 || scenario_ == 10 || scenario_ == 11)
            && startCalls == 1 && !frameOutstanding)
        {
            releaseBeforeRebuild = true;
        }
        (void)ReleaseFrame();
    }

    std::int64_t Counter() const override { return nowMilliseconds; }
    std::int64_t Frequency() const override { return 1000; }

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

private:
    int scenario_ = 0;
    std::int64_t nowMilliseconds = 0;
    bool frameOutstanding = false;
};

class RecordingCompositionBackend final : public CompositionBackend
{
public:
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
        return FailureReason::None;
    }

    void UpdateLuminance(
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
    }

    float RelativeLuminance() const override { return 0.45f; }

    FailureReason DrawMaterial(
        const SceneSnapshot& scene,
        const MaterialTone&,
        const MaterialFramePlan& plan) override
    {
        commands.push_back(FrameCommand::MaterialPass);
        blurPasses += plan.drawBlur ? 1 : 0;
        tintPasses += plan.drawTint ? 1 : 0;
        clarityShapePasses += plan.drawClarityShape ? 1 : 0;
        rimPasses += plan.drawRim ? 1 : 0;
        shadowPasses += plan.drawShadow ? 1 : 0;
        clarityCenterOpacityMilli = static_cast<int>(std::lround(plan.clarityCenterOpacity * 1000.0f));
        clarityEdgeOpacityMilli = static_cast<int>(std::lround(plan.clarityEdgeOpacity * 1000.0f));
        localContrastGuards += static_cast<int>(plan.contrastGuards.size());
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
        }
        return FailureReason::None;
    }

    FailureReason DrawOverlay(const SceneSnapshot&) override
    {
        commands.push_back(FrameCommand::OverlayPass);
        return FailureReason::None;
    }

    FailureReason DrawContent(const SceneSnapshot& scene) override
    {
        commands.push_back(FrameCommand::ContentPass);
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
    int blurPasses = 0;
    int tintPasses = 0;
    int searchOpticalPasses = 0;
    int searchOpticalSamples = 0;
    int searchCenterDisplacementMilli = 0;
    int searchEdgeMaximumDisplacementMilli = 0;
    int searchSourceOutOfBounds = 0;
    int searchVerticalBiasMilli = 0;
    int reducedMotionMaximumDisplacementMilli = 0;
    int clarityShapePasses = 0;
    int clarityCenterOpacityMilli = 0;
    int clarityEdgeOpacityMilli = 0;
    int localContrastGuards = 0;
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
    std::vector<FrameCommand> commands;
};
}

extern "C" int LLP_MaterialRunFakeLifecycleScenario(
    int scenario,
    std::int64_t* values,
    std::size_t valueCount)
{
    if (!values || valueCount != MaterialLifecycleValueCount
        || scenario < 0 || scenario > 12) return 2;
    std::fill(values, values + valueCount, 0);
    FakeCaptureBackend backend(scenario);
    CaptureLifecycle lifecycle;
    ShowRequest request;
    request.window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x401));
    request.monitor = reinterpret_cast<HMONITOR>(static_cast<UINT_PTR>(0x201));
    request.windowDesktop = { -1800, 20, -1400, 420 };
    if (scenario == 4) request.forcedFailure = FailureReason::CompositionUnavailable;
    if (scenario == 5) request.forcedFailure = FailureReason::DuplicationUnavailable;
    if (scenario == 6) request.forcedFailure = FailureReason::AccessLost;
    lifecycle.Show(backend, request);

    int hiddenAcquireCalls = 0;
    int hiddenTimedWaits = 0;
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
    scene.metrics.searchText = { 60, 45, 340, 95 };
    scene.policy.darkMode = true;
    auto icon = std::make_shared<IconRaster>();
    icon->width = 1;
    icon->height = 1;
    icon->stride = 4;
    icon->premultipliedBgra = { 20, 30, 40, 255 };
    scene.items.push_back({ { 20, 80, 148, 208 }, L"Result", L"Application", icon, true, false });
    const PixelRect output = { -1920, -100, 0, 980 };

    RecordingCompositionBackend recorded;
    bool presented = false;
    if (ComposeUnifiedFrame(
            recorded, request, &scene, output, OutputRotation::Identity,
            true, true, false, presented) != FailureReason::None || !presented)
    {
        return 3;
    }
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
    values[CompositionBlurPasses] = recorded.blurPasses;
    values[CompositionTintPasses] = recorded.tintPasses;
    values[CompositionSearchOpticalPasses] = recorded.searchOpticalPasses;
    values[CompositionSearchOpticalSamples] = recorded.searchOpticalSamples;
    values[CompositionSearchCenterDisplacementMilli] = recorded.searchCenterDisplacementMilli;
    values[CompositionSearchEdgeMaximumDisplacementMilli] = recorded.searchEdgeMaximumDisplacementMilli;
    values[CompositionSearchSourceOutOfBounds] = recorded.searchSourceOutOfBounds;
    values[CompositionSearchVerticalBiasMilli] = recorded.searchVerticalBiasMilli;
    values[CompositionReducedMotionMaximumDisplacementMilli] = recorded.reducedMotionMaximumDisplacementMilli;
    values[CompositionClarityShapePasses] = recorded.clarityShapePasses;
    values[CompositionClarityCenterOpacityMilli] = recorded.clarityCenterOpacityMilli;
    values[CompositionClarityEdgeOpacityMilli] = recorded.clarityEdgeOpacityMilli;
    values[CompositionLocalContrastGuards] = recorded.localContrastGuards;
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
#endif
}
