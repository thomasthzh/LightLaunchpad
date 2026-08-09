#include "MaterialRenderer.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

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

FailureReason InitializeDevice(DeviceResources& resources, std::uint32_t* selectedFeatureLevel)
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
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
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

}

class MaterialRenderer::Impl final
{
public:
    Impl() : rendererThread_(&Impl::Run, this) {}

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
    void Run()
    {
        const HRESULT apartmentResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool apartmentReady = SUCCEEDED(apartmentResult);

        std::unique_lock<std::mutex> lock(mutex_);
        for (;;)
        {
            workCondition_.wait(lock, [this]() {
                return shutdown_ || showRequested_ || hideRequested_ || rebuildRequested_
                    || freezeRequested_ || pendingScene_;
            });
            if (shutdown_) break;

            if (pendingScene_)
            {
                currentScene_ = std::move(pendingScene_);
            }

            if (hideRequested_)
            {
                diagnostics_.state = LifecycleState::Stopping;
                lock.unlock();
                deviceResources_.reset();
                lock.lock();
                diagnostics_.state = LifecycleState::Hidden;
                diagnostics_.path = RenderPath::Fallback;
                diagnostics_.failure = FailureReason::None;
                diagnostics_.evidenceFrozen = false;
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
                if (rebuild) ++diagnostics_.deviceRebuilds;

                lock.unlock();
                deviceResources_.reset();
                auto candidate = std::make_unique<DeviceResources>();
                std::uint32_t featureLevel = 0;
                const FailureReason initializationFailure = apartmentReady
                    ? InitializeDevice(*candidate, &featureLevel)
                    : FailureReason::CompositionUnavailable;
                if (initializationFailure != FailureReason::None) candidate.reset();
                lock.lock();

                deviceResources_ = std::move(candidate);
                diagnostics_.failure = initializationFailure;
                diagnostics_.path = initializationFailure == FailureReason::None
                    ? RenderPath::HighFidelity
                    : RenderPath::Fallback;
                diagnostics_.state = initializationFailure == FailureReason::None
                    ? (rebuild ? LifecycleState::Rebuilding : LifecycleState::Starting)
                    : LifecycleState::Fallback;
            }

            if (freezeRequested_)
            {
                freezeRequested_ = false;
                if (diagnostics_.state != LifecycleState::Hidden
                    && diagnostics_.state != LifecycleState::Stopping)
                {
                    diagnostics_.evidenceFrozen = true;
                    diagnostics_.state = LifecycleState::FrozenEvidence;
                }
            }
        }

        lock.unlock();
        deviceResources_.reset();
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
    std::unique_ptr<DeviceResources> deviceResources_;
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
}
