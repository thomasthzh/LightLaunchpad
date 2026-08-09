#include "MaterialRenderer.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <objbase.h>

#if __has_include("LightLaunchpad.BuildIdentity.generated.h")
#include "LightLaunchpad.BuildIdentity.generated.h"
#elif !defined(LLP_MATERIAL_TESTING)
// The standalone production-C++ test links this same source without a generated build identity.
#define LLP_MATERIAL_TESTING 1
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

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

enum class AcquireResult { Frame, Timeout, AccessLost, DeviceRemoved, Failed };

class CaptureBackend
{
public:
    virtual ~CaptureBackend() = default;
    virtual FailureReason SetCaptureExclusion(HWND window) = 0;
    virtual void ClearCaptureExclusion() = 0;
    virtual FailureReason Start(const ShowRequest& request) = 0;
    virtual AcquireResult Acquire(std::uint32_t waitMilliseconds, std::uint64_t& identity) = 0;
    virtual void ReleaseFrame() = 0;
    virtual void Stop() = 0;
    virtual std::int64_t Counter() const = 0;
    virtual std::int64_t Frequency() const = 0;
};

class DesktopDuplicationBackend final : public CaptureBackend
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

        ComOwner<IDXGIAdapter1> selectedAdapter;
        ComOwner<IDXGIOutput> selectedOutput;
        DXGI_OUTPUT_DESC selectedDescription = {};
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
                if (SUCCEEDED(output.Get()->GetDesc(&description))
                    && description.Monitor == request.monitor)
                {
                    selectedAdapter = std::move(adapter);
                    selectedOutput = std::move(output);
                    selectedDescription = description;
                    break;
                }
            }
            if (selectedOutput.Get()) break;
        }
        if (!selectedAdapter.Get() || !selectedOutput.Get())
        {
            return FailureReason::AdapterUnavailable;
        }

        const PixelRect outputDesktop = {
            selectedDescription.DesktopCoordinates.left,
            selectedDescription.DesktopCoordinates.top,
            selectedDescription.DesktopCoordinates.right,
            selectedDescription.DesktopCoordinates.bottom
        };
        if (!FullyContains(outputDesktop, request.windowDesktop))
        {
            return FailureReason::DuplicationUnavailable;
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
        identity = frameInfo.LastPresentTime.QuadPart != 0
            ? static_cast<std::uint64_t>(frameInfo.LastPresentTime.QuadPart)
            : static_cast<std::uint64_t>(Counter());
        return AcquireResult::Frame;
    }

    void ReleaseFrame() override
    {
        if (frameAcquired_ && duplication_.Get()) duplication_.Get()->ReleaseFrame();
        frameAcquired_ = false;
    }

    void Stop() override
    {
        ReleaseFrame();
        duplication_.Reset();
        output1_.Reset();
        output_.Reset();
        adapter_.Reset();
        factory_.Reset();
        resources_ = DeviceResources{};
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
    using SetAffinityFn = BOOL(WINAPI*)(HWND, DWORD);
    using GetAffinityFn = BOOL(WINAPI*)(HWND, DWORD*);
    SetAffinityFn setAffinity_ = nullptr;
    GetAffinityFn getAffinity_ = nullptr;
    HWND affinityWindow_ = nullptr;
    bool frameAcquired_ = false;
    DeviceResources resources_;
    ComOwner<IDXGIFactory1> factory_;
    ComOwner<IDXGIAdapter1> adapter_;
    ComOwner<IDXGIOutput> output_;
    ComOwner<IDXGIOutput1> output1_;
    ComOwner<IDXGIOutputDuplication> duplication_;
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

    void Step(CaptureBackend& backend)
    {
        if (!capturing_) return;
        ReleaseHeldFrame(backend);
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

    void Hide(CaptureBackend& backend)
    {
        diagnostics_.state = LifecycleState::Stopping;
        CleanupVisible(backend);
        diagnostics_.state = LifecycleState::Hidden;
        diagnostics_.path = RenderPath::Fallback;
        diagnostics_.failure = FailureReason::None;
        diagnostics_.evidenceFrozen = false;
    }

    void Freeze(CaptureBackend& backend)
    {
        if (diagnostics_.state == LifecycleState::Hidden
            || diagnostics_.state == LifecycleState::Stopping) return;
        CleanupVisible(backend);
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
    void ReleaseHeldFrame(CaptureBackend& backend)
    {
        if (!frameHeld_) return;
        backend.ReleaseFrame();
        frameHeld_ = false;
    }

    void CleanupVisible(CaptureBackend& backend)
    {
        ReleaseHeldFrame(backend);
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

            if (pendingScene_)
            {
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
                lifecycle.Freeze(backend);
                lock.lock();
                PublishDiagnostics(lifecycle);
                continue;
            }

            if (lifecycle.IsCapturing())
            {
                lock.unlock();
                lifecycle.Step(backend);
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
        const std::array<std::array<HMONITOR, 2>, 2> outputs = { {
            { reinterpret_cast<HMONITOR>(static_cast<UINT_PTR>(0x101)),
                reinterpret_cast<HMONITOR>(static_cast<UINT_PTR>(0x102)) },
            { reinterpret_cast<HMONITOR>(static_cast<UINT_PTR>(0x201)), nullptr }
        } };
        for (int adapterIndex = 0; adapterIndex < static_cast<int>(outputs.size()); ++adapterIndex)
        {
            for (int outputIndex = 0; outputIndex < static_cast<int>(outputs[adapterIndex].size()); ++outputIndex)
            {
                if (outputs[adapterIndex][outputIndex] == request.monitor)
                {
                    selectedAdapter = adapterIndex;
                    selectedOutput = outputIndex;
                }
            }
        }
        if (selectedAdapter < 0) return FailureReason::AdapterUnavailable;
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
        frameOutstanding = true;
        identity = 1;
        ++redraws;
        return AcquireResult::Frame;
    }

    void ReleaseFrame() override
    {
        if (!frameOutstanding) return;
        ++releaseCalls;
        if (scenario_ == 2 && startCalls == 2) releaseBeforeHide = true;
        if (scenario_ == 7) releaseBeforeFreeze = true;
        frameOutstanding = false;
    }

    void Stop() override
    {
        if ((scenario_ == 2 || scenario_ == 3) && startCalls == 1 && !frameOutstanding)
        {
            releaseBeforeRebuild = true;
        }
        ReleaseFrame();
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
    bool releaseBeforeReacquire = false;
    bool releaseBeforeRebuild = false;
    bool releaseBeforeHide = false;
    bool releaseBeforeFreeze = false;

private:
    int scenario_ = 0;
    std::int64_t nowMilliseconds = 0;
    bool frameOutstanding = false;
};
}

extern "C" int LLP_MaterialRunFakeLifecycleScenario(
    int scenario,
    MaterialLifecycleTestReport* report)
{
    if (!report || scenario < 0 || scenario > 9) return 2;
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

    const Diagnostics diagnostics = lifecycle.CurrentDiagnostics();
    report->selectedAdapter = backend.selectedAdapter;
    report->selectedOutput = backend.selectedOutput;
    report->duplicateCalls = backend.duplicateCalls;
    report->acquireCalls = backend.acquireCalls;
    report->maximumWaitMilliseconds = backend.maximumWaitMilliseconds;
    report->releaseCalls = backend.releaseCalls;
    report->redraws = backend.redraws;
    report->rebuilds = static_cast<int>(diagnostics.deviceRebuilds);
    report->acquiredFrames = diagnostics.acquiredFrames;
    report->lastFrameIdentity = diagnostics.lastFrameIdentity;
    report->finalState = static_cast<int>(diagnostics.state);
    report->finalPath = static_cast<int>(diagnostics.path);
    report->finalFailure = static_cast<int>(diagnostics.failure);
    report->firstFrameMicroseconds = static_cast<long long>(diagnostics.firstFrameMs * 1000.0);
    report->releaseBeforeReacquire = backend.releaseBeforeReacquire ? 1 : 0;
    report->releaseBeforeRebuild = backend.releaseBeforeRebuild ? 1 : 0;
    report->releaseBeforeHide = backend.releaseBeforeHide ? 1 : 0;
    report->releaseBeforeFreeze = backend.releaseBeforeFreeze ? 1 : 0;
    report->hiddenTimedWaits = hiddenTimedWaits;
    report->hiddenAcquireCalls = hiddenAcquireCalls;
    report->affinityExcluded = backend.affinityExcluded;
    report->affinityCleared = backend.affinityCleared;
    report->partialCropRejected = FullyContains(
        { -1920, -100, 0, 980 }, { -2000, 20, -1400, 420 }) ? 0 : 1;
    return 0;
}
#endif
}
