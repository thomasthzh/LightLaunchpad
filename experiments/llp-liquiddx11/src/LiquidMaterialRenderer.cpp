#include "LiquidMaterialRenderer.h"
#include "InkCompositor.h"
#include "glass.h"
#include "backdrop.h"
#include "CompositionPresenter.h"

#include <d2d1_1.h>
#include <d2d1helper.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <objbase.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

extern "C" HRESULT WINAPI D2D1CreateDevice(IDXGIDevice*, const D2D1_CREATION_PROPERTIES*, ID2D1Device**);

namespace llp::material
{
namespace
{
std::atomic<bool> liquidSpotlight{false};
std::mutex captionsMutex;
SpotlightCaptions spotlightCaptions;
GlassVisualSettings liveGlassSettings;
std::wstring pendingFrameExport;
std::wstring pendingPresentExport;
Diagnostics UnmeasuredDiagnostics()
{
    Diagnostics result;
    result.frameP95Ms=std::numeric_limits<double>::quiet_NaN();
    return result;
}
template<class T> class Com
{
public:
    ~Com(){Reset();}
    Com()=default; Com(const Com&)=delete; Com& operator=(const Com&)=delete;
    T* Get() const{return value_;}
    T** Put(){Reset();return &value_;}
    void Reset(){if(value_){value_->Release();value_=nullptr;}}
private:T* value_=nullptr;
};
void Check(HRESULT hr){if(FAILED(hr))throw hr;}
D2D1_RECT_F Rect(const geometry::Rect& r){return D2D1::RectF(float(r.left),float(r.top),float(r.right),float(r.bottom));}
bool Valid(const geometry::Rect& r){return r.right>r.left&&r.bottom>r.top;}
D2D1_COLOR_F Color(adaptive::Color c,float opacity=1.f){return D2D1::ColorF(c.red/255.f,c.green/255.f,c.blue/255.f,opacity);}
float Unit(float value){return std::isfinite(value)?std::clamp(value,0.f,1.f):1.f;}
FailureReason Reason(HRESULT hr){return hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET
    ?FailureReason::DeviceRemoved:hr==DXGI_ERROR_ACCESS_LOST?FailureReason::AccessLost:FailureReason::CompositionUnavailable;}

std::wstring RendererLogPath() noexcept
{
    try {
    wchar_t path[32768]{};const DWORD size=GetModuleFileNameW(nullptr,path,32768);
    if(!size||size>=32768)return {};
    std::wstring result(path,size);const auto slash=result.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return {};
    result.resize(slash);return result+L"\\profile\\logs\\renderer.log";
    } catch(...) { return {}; }
}
void RendererLog(const std::wstring& path,const char* event,const char* operation,std::uint64_t activation,
    std::uint64_t acquired,std::uint64_t presents,FailureReason reason=FailureReason::None,bool hrObserved=false,HRESULT hr=S_OK) noexcept
{
    if(path.empty())return;
    FILETIME time{};GetSystemTimeAsFileTime(&time);
    const auto ticks=(std::uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;
    char error[100]{};
    if(hrObserved)std::snprintf(error,sizeof(error),"\"hresult\":%ld,\"hresultHex\":\"0x%08lX\"",long(hr),static_cast<unsigned long>(hr));
    else std::snprintf(error,sizeof(error),"\"hresult\":null,\"hresultHex\":null");
    char line[768]{};const int count=std::snprintf(line,sizeof(line),
        "{\"utcFiletime100ns\":%llu,\"pid\":%lu,\"event\":\"%s\",\"operation\":\"%s\",\"activation\":%llu,\"acquiredFrames\":%llu,\"presentedFrames\":%llu,\"failureReason\":%u,%s}\r\n",
        static_cast<unsigned long long>(ticks),static_cast<unsigned long>(GetCurrentProcessId()),event,operation,
        static_cast<unsigned long long>(activation),static_cast<unsigned long long>(acquired),static_cast<unsigned long long>(presents),unsigned(reason),error);
    if(count<=0||count>=int(sizeof(line)))return;
    HANDLE file=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0;WriteFile(file,line,DWORD(count),&written,nullptr);CloseHandle(file);
}

// All graphics, capture, ImGui and DirectWrite objects are created and destroyed
// inside one worker epoch. There is no UI-thread access to the immediate context.
class Session final
{
public:
    ~Session(){Close();}
    void Open(const ShowRequest& request)
    {
        request_=request;
        {std::lock_guard<std::mutex> lock(captionsMutex);captions_=spotlightCaptions;exportPath_=std::move(pendingFrameExport);presentExportPath_=std::move(pendingPresentExport);}
        lastOperation_="display_affinity";
        if(!GetWindowDisplayAffinity(request.window,&originalAffinity_))throw FailureReason::DisplayAffinityUnavailable;
        if(originalAffinity_!=WDA_EXCLUDEFROMCAPTURE){
            if(!SetWindowDisplayAffinity(request.window,WDA_EXCLUDEFROMCAPTURE))throw FailureReason::DisplayAffinityUnavailable;
            affinityChanged_=true;
        }
        DWORD affinity=0;
        if(!GetWindowDisplayAffinity(request.window,&affinity)||affinity!=WDA_EXCLUDEFROMCAPTURE)throw FailureReason::DisplayAffinityUnavailable;
        lastOperation_="adapter_enumeration";
        Com<IDXGIFactory1> enumeration;Check(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(enumeration.Put())));
        Com<IDXGIAdapter1> chosen;
        bool found=false;
        for(UINT a=0;!found;++a){
            Com<IDXGIAdapter1> adapter;
            if(enumeration.Get()->EnumAdapters1(a,adapter.Put())==DXGI_ERROR_NOT_FOUND)break;
            if(!adapter.Get())continue;
            for(UINT o=0;;++o){
                Com<IDXGIOutput> output;
                if(adapter.Get()->EnumOutputs(o,output.Put())==DXGI_ERROR_NOT_FOUND)break;
                if(!output.Get())continue;
                DXGI_OUTPUT_DESC description{};Check(output.Get()->GetDesc(&description));
                if(description.Monitor==request.monitor){
                    if(description.Rotation!=DXGI_MODE_ROTATION_IDENTITY&&description.Rotation!=DXGI_MODE_ROTATION_UNSPECIFIED)
                        throw FailureReason::AdapterUnavailable; // Upstream capture has no rotated-source conversion.
                    Check(adapter.Get()->QueryInterface(__uuidof(IDXGIAdapter1),reinterpret_cast<void**>(chosen.Put())));
                    outputName=description.DeviceName;found=true;break;
                }
            }
        }
        if(!found)throw FailureReason::AdapterUnavailable;
        DXGI_ADAPTER_DESC1 adapterDescription{};Check(chosen.Get()->GetDesc1(&adapterDescription));adapterName=adapterDescription.Description;
        D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1};
        lastOperation_="device_create";
        Check(D3D11CreateDevice(chosen.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_SINGLETHREADED|D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,
            levels,2,D3D11_SDK_VERSION,device_.Put(),&level,context_.Put()));
        Com<IDXGIDevice> dxgi;Check(device_.Get()->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxgi.Put())));
        RECT client{};if(!GetClientRect(request.window,&client))throw FailureReason::CompositionUnavailable;
        width_=std::max(1L,client.right);height_=std::max(1L,client.bottom);
        lastOperation_="presentation_open";
        std::wstring presentationError;
        if(!presenter_.Show(request.window,device_.Get(),width_,height_,presentationError,FrameBufferCount,false))throw FailureReason::CompositionUnavailable;
        lastOperation_="d2d_dwrite_create";
        D2D1_FACTORY_OPTIONS factoryOptions{};
        Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory1),&factoryOptions,reinterpret_cast<void**>(d2dFactory_.Put())));
        Check(d2dFactory_.Get()->CreateDevice(dxgi.Get(),d2dDevice_.Put()));
        Check(d2dDevice_.Get()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,d2d_.Put()));
        d2d_.Get()->SetUnitMode(D2D1_UNIT_MODE_PIXELS);d2d_.Get()->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write_.Put())));
        lastOperation_="ink_create";
        D3D11_TEXTURE2D_DESC ink{};ink.Width=width_;ink.Height=height_;ink.MipLevels=ink.ArraySize=1;
        ink.Format=DXGI_FORMAT_B8G8R8A8_UNORM;ink.SampleDesc.Count=1;ink.Usage=D3D11_USAGE_DEFAULT;
        ink.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Check(device_.Get()->CreateTexture2D(&ink,nullptr,inkTexture_.Put()));MakeBitmap(inkTexture_.Get(),D2D1_BITMAP_OPTIONS_TARGET,inkBitmap_);
        Check(device_.Get()->CreateShaderResourceView(inkTexture_.Get(),nullptr,inkView_.Put()));
        lastOperation_="foreground_compositor_init";Check(inkCompositor_.Initialize(device_.Get()));
        lastOperation_="imgui_create";
        imgui_=ImGui::CreateContext();if(!imgui_)throw E_OUTOFMEMORY;
        ImGui::SetCurrentContext(imgui_);ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().LogFilename=nullptr;
        lastOperation_="glass_init";
        if(!glass_.Init(device_.Get(),context_.Get()))throw FailureReason::CompositionUnavailable;
        Glass::g=&glass_;Glass::SetSfxEnabled(false);Glass::SetAppearance(1);
        {
            // Ultra-clear optical crystal macOS 26 Liquid Glass (zero fog):
            // 94% crystal-clear desktop pass + bold wet meniscus refraction & chromatic dispersion
            // + razor-sharp gleaming dual-angle specular rim.
            Glass::MaterialParams& reg = Glass::EditParams(Glass::Material::Regular);
            reg.blur_mix         = 0.06f;
            reg.saturation       = 1.45f;
            reg.brightness       = 0.00f;
            reg.contrast         = 1.04f;
            reg.tint_rgb[0]      = 0.96f;
            reg.tint_rgb[1]      = 0.98f;
            reg.tint_rgb[2]      = 1.00f;
            reg.tint_opacity     = 0.015f;
            reg.grain            = 0.004f;
            reg.refr_strength    = 30.0f;
            reg.refr_band        = 30.0f;
            reg.highlight        = 0.95f;
            reg.inner_shadow     = 0.00f;
            reg.border_intensity = 0.28f;
            reg.border_width     = 1.2f;
            reg.shadow_strength  = 0.30f;
            reg.shadow_radius    = 30.0f;
            reg.shadow_off_x     = 0.0f;
            reg.shadow_off_y     = 12.0f;
            reg.sheen            = 0.05f;
            reg.chroma           = 4.2f;

            // Luminous Liquid Glass gliding focus pill (Material::Thin)
            Glass::MaterialParams& thin = Glass::EditParams(Glass::Material::Thin);
            thin = reg;
            thin.blur_mix         = 0.14f;
            thin.brightness       = 0.09f;
            thin.tint_opacity     = 0.14f;
            thin.refr_strength    = 14.0f;
            thin.refr_band        = 16.0f;
            thin.highlight        = 0.95f;
            thin.border_intensity = 0.42f;
            thin.shadow_strength  = 0.14f;
            thin.shadow_radius    = 14.0f;
            thin.shadow_off_y     = 4.0f;
            thin.chroma           = 2.0f;

            Glass::GlassEdgeConfig edge{};
            edge.cursor_size       = 200.0f;
            edge.cursor_glow       = 0.55f;
            edge.squircle_power    = 4.2f;
            glass_.SetEdgeConfig(edge);
            glass_.SetLights(320.0f, 0.95f, 140.0f, 0.62f);
        }
        lastOperation_="backdrop_init_first_capture";
        if(!backdrop_.Init(device_.Get(),context_.Get(),request.window))throw FailureReason::CompositionUnavailable;
        Check(backdrop_.captureResult());
        if(!backdrop_.hasFrame())throw FailureReason::FirstFrameTimeout;
        openedAt_=std::chrono::steady_clock::now();
    }
    bool AcquireFrame(HANDLE stopEvent)
    {
        lastOperation_="presentation_acquire";
        std::wstring error;ID3D11Texture2D* texture=nullptr;
        for(;;){
            if(WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0)return false;
            if(!presenter_.Acquire(texture,error))throw FailureReason::CompositionUnavailable;
            if(texture)break;
            if(!presenter_.WaitForBuffer(stopEvent,error)){
                if(error.empty())return false;
                throw FailureReason::CompositionUnavailable;
            }
        }
        frameTarget_=nullptr;
        for(auto& frame:frameTargets_)if(frame.texture==texture){frameTarget_=&frame;break;}
        if(!frameTarget_)for(auto& frame:frameTargets_)if(!frame.texture){
            Check(device_.Get()->CreateRenderTargetView(texture,nullptr,frame.rtv.Put()));
            frame.texture=texture;frameTarget_=&frame;break;
        }
        if(!frameTarget_)throw E_UNEXPECTED;
        return true;
    }
    void BuildInk(const SceneSnapshot& scene)
    {
        lastOperation_="foreground_prepare";
        auto* dc=d2d_.Get();dc->SetTarget(inkBitmap_.Get());dc->SetTransform(D2D1::Matrix3x2F::Identity());
        dc->BeginDraw();dc->Clear(D2D1::ColorF(0,0));
        try{DrawInk(scene);}catch(...){dc->EndDraw();dc->SetTarget(nullptr);throw;}
        lastOperation_="foreground_prepare_end_draw";
        const HRESULT end=dc->EndDraw();dc->SetTarget(nullptr);Check(end);
    }
    HRESULT Frame(const SceneSnapshot& scene,MotionVisual motion,LightingVisual lighting,bool frozen,float delta)
    {
        ImGui::SetCurrentContext(imgui_);ImGui::GetIO().DeltaTime=std::clamp(delta,0.001f,0.1f);
        lastOperation_="backdrop_capture";
        if(!frozen){
            backdrop_.Capture();
            if(backdrop_.captureResult()==DXGI_ERROR_ACCESS_LOST)backdrop_.Capture(); // One bounded duplication rebuild.
            Check(backdrop_.captureResult());
        }
        if(!backdrop_.hasFrame())return E_FAIL;
        lastOperation_="backbuffer_clear";
        const float clear[4]={0,0,0,0};context_.Get()->ClearRenderTargetView(frameTarget_->rtv.Get(),clear);
        ID3D11RenderTargetView* target=frameTarget_->rtv.Get();context_.Get()->OMSetRenderTargets(1,&target,nullptr);
        D3D11_VIEWPORT viewport{};viewport.Width=float(width_);viewport.Height=float(height_);viewport.MaxDepth=1;
        context_.Get()->RSSetViewports(1,&viewport);
        lastOperation_="client_origin";
        POINT origin{};if(!ClientToScreen(request_.window,&origin))return E_FAIL;
        if(scene.policy.reducedMotion)glass_.springs().Get(0xE0E0E0u,0,Glass::SpringStyle::Bouncy,1.f).Snap(1.f);
        lastOperation_="glass_begin";
        glass_.BeginFrame(width_,height_,origin.x-backdrop_.originX(),origin.y-backdrop_.originY(),backdrop_.width(),backdrop_.height(),
            lighting.hasPointer?ImVec2(lighting.pointerX,lighting.pointerY):ImVec2(-10000,-10000));
        glass_.SetSubmitFade(Unit(motion.overlayOpacity));
        {
            GlassVisualSettings gs;
            { std::lock_guard<std::mutex> lock(captionsMutex); gs = liveGlassSettings; }
            Glass::MaterialParams& reg = Glass::EditParams(Glass::Material::Regular);
            reg.blur_mix         = std::clamp(gs.blurMix, 0.f, 1.f);
            reg.tint_opacity     = std::clamp(gs.tintOpacity, 0.f, 0.65f);
            reg.refr_strength    = std::clamp(gs.refractionPx, 0.f, 45.f);
            reg.refr_band        = std::max(12.f, reg.refr_strength);
            reg.chroma           = reg.refr_strength * 0.14f;
            reg.highlight        = std::clamp(gs.rimHighlight, 0.f, 1.2f);
            reg.border_intensity = 0.08f + 0.24f * std::clamp(gs.rimHighlight, 0.f, 1.f);
        }
        const float dt = std::clamp(delta, 0.001f, 0.05f);
        const float scale = float(scene.metrics.dpi) / 96.f;
        const int outerPad = std::max(1, int(std::lround(28.f * scale)));
        const int glassLeft = scene.metrics.search.left;
        const int glassRight = scene.metrics.search.right;
        const int glassTop = scene.metrics.search.top;
        int targetBottom = scene.metrics.search.bottom;
        if(!scene.items.empty()){
            int maxItemBottom = scene.metrics.search.bottom;
            for(const auto& item:scene.items) maxItemBottom = std::max(maxItemBottom, item.bounds.bottom);
            targetBottom = std::min(height_ - outerPad, maxItemBottom + int(std::lround(18.f * scale)));
        }else if(!scene.search.displayText.empty()){
            targetBottom = std::min(height_ - outerPad, scene.metrics.search.bottom + int(std::lround(56.f * scale)));
        }

        // Apple WWDC25 physical spring dynamics:
        // 1) Bouncy scale-pop & vertical float on entrance
        // 2) Live pointer specular light tilt
        // 3) Critically-damped height morph & gliding Liquid Glass focus pill
        auto& popSpring = glass_.springs().Get(0x507010u, 0, Glass::SpringStyle::Bouncy, scene.policy.reducedMotion ? 1.f : 0.91f);
        auto& heightSpring = glass_.springs().Get(0x507011u, 0, Glass::SpringStyle::CriticalFast, float(std::max(scene.metrics.search.bottom - glassTop, targetBottom - glassTop)));
        auto& tiltXSpring = glass_.springs().Get(0x507012u, 0, Glass::SpringStyle::Critical, 0.f);
        auto& tiltYSpring = glass_.springs().Get(0x507013u, 0, Glass::SpringStyle::Critical, 0.f);

        const float targetHeight = float(std::max(scene.metrics.search.bottom - glassTop, targetBottom - glassTop));
        const float targetTiltX = (!scene.policy.reducedMotion && lighting.hasPointer && width_ > 0)
            ? std::clamp((lighting.pointerX / float(width_) - 0.5f) * 0.70f, -0.40f, 0.40f) : 0.f;
        const float targetTiltY = (!scene.policy.reducedMotion && lighting.hasPointer && height_ > 0)
            ? std::clamp((lighting.pointerY / float(height_) - 0.5f) * 0.70f, -0.40f, 0.40f) : 0.f;

        if(scene.policy.reducedMotion){
            popSpring.Snap(1.f); heightSpring.Snap(targetHeight); tiltXSpring.Snap(0.f); tiltYSpring.Snap(0.f);
        }else{
            popSpring.target = 1.f; popSpring.Tick(dt);
            heightSpring.target = targetHeight; heightSpring.Tick(dt);
            tiltXSpring.target = targetTiltX; tiltXSpring.Tick(dt);
            tiltYSpring.target = targetTiltY; tiltYSpring.Tick(dt);
        }
        glass_.SetTilt(tiltXSpring.x, tiltYSpring.x);

        const float sPop = std::clamp(popSpring.x, 0.85f, 1.03f);
        const float floatY = (1.f - sPop) * 120.f * scale;
        const int glassBottom = std::clamp(glassTop + int(std::lround(heightSpring.x)), scene.metrics.search.bottom, height_ - outerPad);
        const bool pillOnly = scene.items.empty() && scene.search.displayText.empty() && (glassBottom - glassTop) <= (scene.metrics.search.bottom - glassTop) + 2;
        const float radius = pillOnly ? (glassBottom - glassTop) * 0.5f : float(scene.metrics.cornerRadiusPx);

        const float cx = (glassLeft + glassRight) * 0.5f;
        const float cy = (glassTop + glassBottom) * 0.5f + floatY;
        const float hw = (glassRight - glassLeft) * 0.5f * sPop;
        const float hh = (glassBottom - glassTop) * 0.5f * sPop;
        {
            Glass::Primitive p;
            p.cx = cx; p.cy = cy; p.hw = hw; p.hh = hh;
            p.corner_radius = radius * sPop; p.fade = 1.f; p.material = Glass::Material::Regular;
            glass_.Submit(p);
        }

        // Gliding Liquid Glass focus pill for the currently focused app tile
        const SceneItem* focusedItem = nullptr;
        for(const auto& item:scene.items) if(item.focused){ focusedItem = &item; break; }
        auto& fAlpha = glass_.springs().Get(0x507020u, 0, Glass::SpringStyle::CriticalFast, 0.f);
        if(focusedItem && Valid(focusedItem->bounds)){
            const float tx = (focusedItem->bounds.left + focusedItem->bounds.right) * 0.5f;
            const float ty = (focusedItem->bounds.top + focusedItem->bounds.bottom) * 0.5f + floatY;
            const float tw = (focusedItem->bounds.right - focusedItem->bounds.left) * 0.5f * sPop;
            const float th = (focusedItem->bounds.bottom - focusedItem->bounds.top) * 0.5f * sPop;
            auto& fx = glass_.springs().Get(0x507021u, 0, Glass::SpringStyle::CriticalFast, tx);
            auto& fy = glass_.springs().Get(0x507022u, 0, Glass::SpringStyle::CriticalFast, ty);
            auto& fw = glass_.springs().Get(0x507023u, 0, Glass::SpringStyle::CriticalFast, tw);
            auto& fh = glass_.springs().Get(0x507024u, 0, Glass::SpringStyle::CriticalFast, th);
            if(scene.policy.reducedMotion || fAlpha.x < 0.05f){
                fx.Snap(tx); fy.Snap(ty); fw.Snap(tw); fh.Snap(th);
            }else{
                fx.target = tx; fx.Tick(dt);
                fy.target = ty; fy.Tick(dt);
                fw.target = tw; fw.Tick(dt);
                fh.target = th; fh.Tick(dt);
            }
            fAlpha.target = 1.f; if(scene.policy.reducedMotion) fAlpha.Snap(1.f); else fAlpha.Tick(dt);
            Glass::Primitive fp;
            fp.cx = fx.x; fp.cy = fy.x; fp.hw = fw.x; fp.hh = fh.x;
            fp.corner_radius = 14.f * scale * sPop; fp.fade = std::clamp(fAlpha.x, 0.f, 1.f);
            fp.material = Glass::Material::Thin;
            glass_.Submit(fp);
        }else{
            fAlpha.target = 0.f; if(scene.policy.reducedMotion) fAlpha.Snap(0.f); else fAlpha.Tick(dt);
        }
        lastOperation_="glass_render";
        glass_.Render(backdrop_.heavySRV(),backdrop_.softSRV());
        lastOperation_="foreground_frame_composite";
        const HRESULT ink=inkCompositor_.Draw(context_.Get(),frameTarget_->rtv.Get(),inkView_.Get(),width_,height_,Unit(motion.contentOpacity));
        if(FAILED(ink))return ink;
        if(!exportPath_.empty()&&Unit(motion.opacity)>.999f&&std::abs(motion.offsetY)<.5f
            &&std::chrono::steady_clock::now()-openedAt_>=std::chrono::seconds(1)){
            const auto path=std::exchange(exportPath_,std::wstring{});
            try{ExportFrame(path);RendererLog(RendererLogPath(),"render_export_succeeded","wic_png",request_.activation,Acquired(),0);}
            catch(HRESULT hr){RendererLog(RendererLogPath(),"render_export_failed","wic_png",request_.activation,Acquired(),0,FailureReason::None,true,hr);}
        }
        lastOperation_="composition_motion";
        std::wstring presentationError;
        if(!presenter_.SetMotion(Unit(motion.opacity),std::isfinite(motion.offsetY)?motion.offsetY:0.f,presentationError))throw FailureReason::CompositionUnavailable;
        context_.Get()->OMSetRenderTargets(0,nullptr,nullptr);
        lastOperation_="presentation_submit";
        // Distinct target times keep queued buffers from overriding one another
        // solely because all their targets are zero. Follow the Presentation API
        // event-driven example, at 60 Hz, without adding another wait or timer.
        ULONGLONG interruptNow=0;QueryInterruptTimePrecise(&interruptNow);
        if(!presentTargetInterrupt100ns_)presentTargetInterrupt100ns_=interruptNow;
        presentTargetInterrupt100ns_+=10000000/60;
        presentTargetRemainder_+=10000000%60;
        if(presentTargetRemainder_>=60){++presentTargetInterrupt100ns_;presentTargetRemainder_-=60;}
        if(presentTargetInterrupt100ns_<=interruptNow){
            // After a stall, do not issue a burst of already-expired presents.
            presentTargetInterrupt100ns_=interruptNow+166667;presentTargetRemainder_=0;
        }
        if(!presenter_.Present(++frameSequence_,presentationError,presentTargetInterrupt100ns_))throw FailureReason::CompositionUnavailable;
        if(!presentExportPath_.empty()&&frameSequence_<=ExportFrameCount) {
            AppendPresent("Submitted",presenter_.LastSubmission());
            if(frameSequence_==ExportFrameCount)exportDrainBegin_=std::chrono::steady_clock::now();
        }
        feedback_.clear();
        lastOperation_="presentation_feedback";
        const bool drained=presenter_.DrainStats(feedback_,presentationError);
        if(!presentExportPath_.empty()){
            // Drain may append a valid prefix before reporting a later API
            // failure. Preserve that prefix before propagating the failure.
            for(const auto& record:feedback_)if(record.contentFrameId<=ExportFrameCount)
                AppendPresent(RecordName(record.kind),record);
        }
        if(!drained)throw FailureReason::CompositionUnavailable;
        if(!presentExportPath_.empty()){
            if(presentExportOverflow_||(frameSequence_>=ExportFrameCount&&std::chrono::steady_clock::now()-exportDrainBegin_>=std::chrono::seconds(2)))
                ExportPresents(presentExportOverflow_?"CaptureOverflow":"CaptureComplete");
        }
        return S_OK;
    }
    std::uint64_t Acquired() const{return backdrop_.acquiredFrames();}
    int DesktopWidth() const{return backdrop_.width();}
    int DesktopHeight() const{return backdrop_.height();}
    const char* LastOperation()const{return lastOperation_;}
    FailureReason CloseFailure()const{return closeFailure_;}
    std::wstring adapterName,outputName;
    bool Close()
    {
        if(closed_)return detached_;
        closed_=true;
        if(!presentExportPath_.empty())ExportPresents(presentExportOverflow_?"CaptureOverflow":"CapturePartial");
        bool detached=true;
        if(d2d_.Get())d2d_.Get()->SetTarget(nullptr);
        presenter_.Hide();detached=presenter_.SnapshotStats().releaseCommitCompleted;
        if(!detached)closeFailure_=FailureReason::CompositionUnavailable;
        backdrop_.Shutdown(); // No duplication remains when exclusion is restored.
        if(affinityChanged_){
            DWORD restored=0;DWORD error=ERROR_SUCCESS;
            if(!SetWindowDisplayAffinity(request_.window,originalAffinity_))error=GetLastError();
            else if(!GetWindowDisplayAffinity(request_.window,&restored))error=GetLastError();
            else if(restored!=originalAffinity_)error=ERROR_INVALID_DATA;
            if(error!=ERROR_SUCCESS&&IsWindow(request_.window)){
                RendererLog(RendererLogPath(),"capture_affinity_restore_failed","restore_original_affinity",request_.activation,0,frameSequence_,
                    FailureReason::DisplayAffinityUnavailable,true,HRESULT_FROM_WIN32(error));
                detached=false;
                closeFailure_=FailureReason::DisplayAffinityUnavailable;
            }else if(error==ERROR_SUCCESS){
                RendererLog(RendererLogPath(),"capture_affinity_restored","restore_original_affinity",request_.activation,0,frameSequence_);
            }
            affinityChanged_=false;
        }
        glass_.Shutdown();Glass::g=nullptr;
        if(imgui_){ImGui::SetCurrentContext(imgui_);ImGui::DestroyContext(imgui_);imgui_=nullptr;}
        // Release this epoch's views and Direct2D owners before the existing
        // context teardown. Leaving them to member destruction queues their
        // deferred destruction only after the final ClearState/Flush pair.
        inkCompositor_.Reset();inkView_.Reset();inkBitmap_.Reset();inkTexture_.Reset();
        for(auto& target:frameTargets_){target.rtv.Reset();target.texture=nullptr;}
        frameTarget_=nullptr;
        write_.Reset();d2d_.Reset();d2dDevice_.Reset();d2dFactory_.Reset();
        if(context_.Get()){context_.Get()->ClearState();context_.Get()->Flush();}
        context_.Reset();device_.Reset();
        detached_=detached;return detached;
    }
private:
    static constexpr unsigned FrameBufferCount=3;
    static constexpr std::uint64_t ExportFrameCount=1200;
    void AppendPresent(const char* kind,const preview::PresentationRecord& record)
    {
        if(presentExport_.size()>=32768){presentExportOverflow_=true;return;}
        presentExport_.push_back({kind,record});
    }
    static const char* RecordName(preview::PresentationRecordKind kind)
    {
        switch(kind){
        case preview::PresentationRecordKind::Queued:return "Queued";
        case preview::PresentationRecordKind::Skipped:return "Skipped";
        case preview::PresentationRecordKind::Canceled:return "Canceled";
        case preview::PresentationRecordKind::Displayed:return "Displayed";
        case preview::PresentationRecordKind::Intermediate:return "Intermediate";
        default:return "Unresolved";
        }
    }
    void ExportPresents(const char* terminal) noexcept
    {
        if(std::strcmp(terminal,"CaptureComplete")==0){
            // A deadline is not lifecycle completion. Keep queued-only IDs
            // unknown in the raw records instead of claiming a complete capture.
            std::array<unsigned char,ExportFrameCount+1> coverage{};
            bool complete=frameSequence_>=ExportFrameCount;
            for(const auto& entry:presentExport_){
                const auto id=entry.record.contentFrameId;
                if(std::strcmp(entry.kind,"Unresolved")==0||std::strcmp(entry.kind,"Intermediate")==0)complete=false;
                if(!id||id>ExportFrameCount)continue;
                if(std::strcmp(entry.kind,"Submitted")==0)coverage[id]|=1;
                else if(std::strcmp(entry.kind,"Displayed")==0||std::strcmp(entry.kind,"Skipped")==0
                    ||std::strcmp(entry.kind,"Canceled")==0)coverage[id]|=2;
            }
            for(std::size_t id=1;id<=ExportFrameCount;++id)if(coverage[id]!=3){complete=false;break;}
            if(!complete)terminal="CaptureIncomplete";
        }
        const auto path=std::exchange(presentExportPath_,std::wstring{});
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        bool good=file!=INVALID_HANDLE_VALUE;
        const auto write=[&](const char* line){
            if(!good)return;
            const auto length=std::strlen(line);DWORD written=0;
            good=WriteFile(file,line,DWORD(length),&written,nullptr)&&written==length;
        };
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        write("Kind,ContentFrameId,PresentId,SurfaceTag,SubmitQpc,ReceivedQpc,CompositionFrameId,DisplayQpc,TargetPresentQpc,MatchedSubmission,TagMatches,UnresolvedReason,DisplayAdapterLow,DisplayAdapterHigh,RenderAdapterLow,RenderAdapterHigh,SourceId,TargetUniqueId,TargetId,InstanceKind,FrameStatisticsQueried,TargetStatisticsQueried,StatisticsResult,RequestedTargetInterrupt100ns,QpcFrequency\r\n");
        for(const auto& entry:presentExport_){
            const auto& r=entry.record;char line[1024]{};
            std::snprintf(line,sizeof(line),"%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%lu,%ld,%lu,%ld,%u,%u,%u,%u,%u,%u,%ld,%llu,%llu\r\n",
                entry.kind,(unsigned long long)r.contentFrameId,(unsigned long long)r.presentId,(unsigned long long)r.contentTag,
                (unsigned long long)r.submitQpc,(unsigned long long)r.receivedQpc,(unsigned long long)r.compositionFrameId,
                (unsigned long long)r.displayQpc,(unsigned long long)r.targetPresentQpc,unsigned(r.matchedSubmission),unsigned(r.tagMatches),unsigned(r.unresolvedReason),
                (unsigned long)r.displayAdapter.LowPart,(long)r.displayAdapter.HighPart,(unsigned long)r.renderAdapter.LowPart,(long)r.renderAdapter.HighPart,
                r.sourceId,r.targetUniqueId,r.targetId,r.instanceKind,unsigned(r.frameStatisticsQueried),unsigned(r.targetStatisticsQueried),(long)r.statisticsResult,(unsigned long long)r.requestedTargetInterrupt100ns,(unsigned long long)frequency.QuadPart);
            write(line);
        }
        char end[180]{};std::snprintf(end,sizeof(end),"%s,%llu,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,%llu\r\n",
            terminal,(unsigned long long)std::min(frameSequence_,ExportFrameCount),(unsigned long long)frequency.QuadPart);write(end);
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        RendererLog(RendererLogPath(),good?"present_export_succeeded":"present_export_failed",terminal,request_.activation,Acquired(),frameSequence_);
        std::vector<PresentExportRecord>().swap(presentExport_);
    }
    void ExportFrame(const std::wstring& path)
    {
        context_.Get()->OMSetRenderTargets(0,nullptr,nullptr);
        Com<ID2D1Bitmap1> source,snapshot;
        MakeBitmap(frameTarget_->texture,D2D1_BITMAP_OPTIONS_NONE,source);
        const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        Check(d2d_.Get()->CreateBitmap(D2D1::SizeU(width_,height_),nullptr,0,&properties,snapshot.Put()));
        Check(snapshot.Get()->CopyFromBitmap(nullptr,source.Get(),nullptr));
        Com<IWICImagingFactory2> factory;
        Check(CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory2,reinterpret_cast<void**>(factory.Put())));
        Com<IStream> stream;
        Check(SHCreateStreamOnFileEx(path.c_str(),STGM_WRITE|STGM_FAILIFTHERE|STGM_SHARE_EXCLUSIVE,FILE_ATTRIBUTE_NORMAL,TRUE,nullptr,stream.Put()));
        Com<IWICBitmapEncoder> encoder;Check(factory.Get()->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.Put()));
        Check(encoder.Get()->Initialize(stream.Get(),WICBitmapEncoderNoCache));
        Com<IWICBitmapFrameEncode> frame;Check(encoder.Get()->CreateNewFrame(frame.Put(),nullptr));
        Check(frame.Get()->Initialize(nullptr));Check(frame.Get()->SetSize(width_,height_));Check(frame.Get()->SetResolution(96,96));
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Check(frame.Get()->SetPixelFormat(&format));
        if(!IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA))throw HRESULT(WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT);
        Com<IWICImageEncoder> image;Check(factory.Get()->CreateImageEncoder(d2dDevice_.Get(),image.Put()));
        WICImageParameters parameters{};
        parameters.PixelFormat=D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED);
        parameters.DpiX=parameters.DpiY=96;parameters.PixelWidth=width_;parameters.PixelHeight=height_;
        Check(image.Get()->WriteFrame(snapshot.Get(),frame.Get(),&parameters));
        Check(frame.Get()->Commit());Check(encoder.Get()->Commit());Check(stream.Get()->Commit(STGC_DEFAULT));
    }
    void MakeBitmap(ID3D11Texture2D* texture,D2D1_BITMAP_OPTIONS options,Com<ID2D1Bitmap1>& bitmap)
    {
        Com<IDXGISurface> surface;Check(texture->QueryInterface(__uuidof(IDXGISurface),reinterpret_cast<void**>(surface.Put())));
        const auto properties=D2D1::BitmapProperties1(options,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        Check(d2d_.Get()->CreateBitmapFromDxgiSurface(surface.Get(),&properties,bitmap.Put()));
    }
    void AddPane(geometry::Rect bounds,float radius,Glass::Material material)
    {
        if(!Valid(bounds))return;
        Glass::Primitive p;p.cx=(bounds.left+bounds.right)*.5f;p.cy=(bounds.top+bounds.bottom)*.5f;
        p.hw=(bounds.right-bounds.left)*.5f;p.hh=(bounds.bottom-bounds.top)*.5f;p.corner_radius=radius;p.fade=1.f;p.material=material;
        glass_.Submit(p);
    }
    void Format(float size,DWRITE_FONT_WEIGHT weight,Com<IDWriteTextFormat>& format,bool trim=false)
    {
        Check(write_.Get()->CreateTextFormat(L"Segoe UI",nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,std::max(1.f,size),L"",format.Put()));
        Check(format.Get()->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
        if(trim){
            Com<IDWriteInlineObject> sign;
            Check(write_.Get()->CreateEllipsisTrimmingSign(format.Get(),sign.Put()));
            DWRITE_TRIMMING trimming{};trimming.granularity=DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            Check(format.Get()->SetTrimming(&trimming,sign.Get()));
        }
    }
    void DrawInk(const SceneSnapshot& scene)
    {
        auto* dc=d2d_.Get();
        const float scale=float(scene.metrics.dpi)/96.f;
        const bool darkInk=!scene.policy.darkMode;
        Com<ID2D1SolidColorBrush> text,muted,accent,selection,dividerDark,dividerLight,pillFill,pillBorder,auxFill;
        Check(dc->CreateSolidColorBrush(darkInk ? D2D1::ColorF(0.08f,0.10f,0.14f,0.96f) : D2D1::ColorF(0.97f,0.98f,1.00f,0.98f),text.Put()));
        Check(dc->CreateSolidColorBrush(darkInk ? D2D1::ColorF(0.26f,0.31f,0.39f,0.74f) : D2D1::ColorF(0.84f,0.89f,0.96f,0.76f),muted.Put()));
        Check(dc->CreateSolidColorBrush(darkInk ? D2D1::ColorF(0.00f,0.48f,1.00f,1.00f) : D2D1::ColorF(0.32f,0.68f,1.00f,1.00f),accent.Put()));
        Check(dc->CreateSolidColorBrush(D2D1::ColorF(0.00f,0.48f,1.00f,0.28f),selection.Put()));
        Check(dc->CreateSolidColorBrush(darkInk ? D2D1::ColorF(0.06f,0.09f,0.16f,0.10f) : D2D1::ColorF(0.00f,0.00f,0.00f,0.22f),dividerDark.Put()));
        Check(dc->CreateSolidColorBrush(D2D1::ColorF(1.00f,1.00f,1.00f,darkInk ? 0.55f : 0.24f),dividerLight.Put()));
        Check(dc->CreateSolidColorBrush(D2D1::ColorF(1.00f,1.00f,1.00f,darkInk ? 0.36f : 0.14f),pillFill.Put()));
        Check(dc->CreateSolidColorBrush(D2D1::ColorF(1.00f,1.00f,1.00f,darkInk ? 0.78f : 0.42f),pillBorder.Put()));
        Check(dc->CreateSolidColorBrush(D2D1::ColorF(1.00f,1.00f,1.00f,darkInk ? 0.24f : 0.12f),auxFill.Put()));

        Com<IDWriteTextFormat> title,subtitle,aux;
        Format(float(scene.metrics.tileFontPx),DWRITE_FONT_WEIGHT_SEMI_BOLD,title,true);
        Format(float(std::max(1,scene.metrics.tileFontPx-2)),DWRITE_FONT_WEIGHT_NORMAL,subtitle,true);
        Format(float(std::max(1,scene.metrics.tileFontPx+1)),DWRITE_FONT_WEIGHT_NORMAL,aux);
        title.Get()->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);subtitle.Get()->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        aux.Get()->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);aux.Get()->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Crisp vector magnifying glass icon inside the left side of the search bar.
        {
            const float cy=(scene.metrics.search.top+scene.metrics.search.bottom)*0.5f;
            const float cx=float(scene.metrics.search.left)+26.f*scale;
            const float r=7.0f*scale;
            const float stroke=std::max(1.5f,1.9f*scale);
            dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx-1.2f*scale,cy-1.2f*scale),r,r),muted.Get(),stroke);
            dc->DrawLine(D2D1::Point2F(cx+3.9f*scale,cy+3.9f*scale),D2D1::Point2F(cx+8.8f*scale,cy+8.8f*scale),muted.Get(),stroke);
        }

        const auto& box=scene.metrics.searchText;
        Com<IDWriteTextLayout> layout;
        Check(CreateSpotlightSearchLayout(write_.Get(),scene.search.displayText,float(scene.metrics.searchFontPx),
            float(box.right-box.left),float(box.bottom-box.top),layout.Put()));
        const float originX=float(box.left-scene.search.horizontalOffsetPx),originY=float(box.top);
        auto position=[&](std::size_t at){FLOAT x=0,y=0;DWRITE_HIT_TEST_METRICS metrics{};
            if(scene.search.displayText.empty())return D2D1::Point2F(originX,originY);
            Check(layout.Get()->HitTestTextPosition(UINT32(std::min(at,scene.search.displayText.size())),FALSE,&x,&y,&metrics));return D2D1::Point2F(originX+x,originY+y);};
        const auto selectionA=position(std::min(scene.search.selectionStart,scene.search.selectionEnd));
        const auto selectionB=position(std::max(scene.search.selectionStart,scene.search.selectionEnd));
        dc->PushAxisAlignedClip(Rect(box),D2D1_ANTIALIAS_MODE_ALIASED);
        const bool showSelection=!scene.search.composing&&scene.search.selectionStart!=scene.search.selectionEnd;
        if(showSelection)dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(selectionA.x,float(box.top)+10.f*scale,selectionB.x,float(box.bottom)-10.f*scale),4.f*scale,4.f*scale),selection.Get());
        dc->DrawTextLayout(D2D1::Point2F(originX,originY),layout.Get(),text.Get());
        if(scene.search.displayText.empty()&&!captions_.placeholder.empty()){
            Com<IDWriteTextLayout> placeholder;
            Check(CreateSpotlightSearchLayout(write_.Get(),captions_.placeholder,float(scene.metrics.searchFontPx),
                float(box.right-box.left),float(box.bottom-box.top),placeholder.Put()));
            dc->DrawTextLayout(D2D1::Point2F(float(box.left),float(box.top)),placeholder.Get(),muted.Get());
        }
        if(scene.search.composing&&scene.search.compositionEnd>scene.search.compositionStart){
            const auto a=position(scene.search.compositionStart),b=position(scene.search.compositionEnd);
            dc->DrawLine(D2D1::Point2F(a.x,float(box.bottom)-10.f*scale),D2D1::Point2F(b.x,float(box.bottom)-10.f*scale),accent.Get(),std::max(1.5f,1.5f*scale));
        }
        if(scene.search.active){const auto caret=position(scene.search.caret);
            const float x=std::clamp(caret.x+1.f,float(box.left+1),float(box.right-2));
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x,float(box.top)+14.f*scale,x+2.0f*scale,float(box.bottom)-14.f*scale),1.f*scale,1.f*scale),accent.Get());}
        dc->PopAxisAlignedClip();

        const bool hasLowerArea=!scene.items.empty()||!scene.search.displayText.empty();
        if(hasLowerArea){
            const float divY=float(scene.metrics.search.bottom);
            const float divInset=18.f*scale;
            dc->DrawLine(D2D1::Point2F(float(scene.metrics.search.left)+divInset,divY),D2D1::Point2F(float(scene.metrics.search.right)-divInset,divY),dividerDark.Get(),1.0f);
            dc->DrawLine(D2D1::Point2F(float(scene.metrics.search.left)+divInset,divY+1.0f),D2D1::Point2F(float(scene.metrics.search.right)-divInset,divY+1.0f),dividerLight.Get(),1.0f);
        }

        dc->PushAxisAlignedClip(Rect(scene.metrics.contentClip),D2D1_ANTIALIAS_MODE_ALIASED);
        if(scene.items.empty()&&!scene.search.displayText.empty()){
            const auto& message=captions_.noResults;
            const D2D1_RECT_F msgRect=D2D1::RectF(float(scene.metrics.contentClip.left),float(scene.metrics.search.bottom)+8.f*scale,
                float(scene.metrics.contentClip.right),float(scene.metrics.search.bottom)+48.f*scale);
            dc->DrawText(message.c_str(),UINT32(message.size()),aux.Get(),msgRect,muted.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        for(const auto& item:scene.items){
            if(item.focused){
                const float r=14.f*scale;
                const auto rr=D2D1::RoundedRect(Rect(item.bounds),r,r);
                dc->FillRoundedRectangle(rr,pillFill.Get());
                dc->DrawRoundedRectangle(rr,pillBorder.Get(),std::max(1.f,1.2f*scale));
                dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(float(item.bounds.left)+1.f,float(item.bounds.top)+1.f,float(item.bounds.right)-1.f,float(item.bounds.bottom)-1.f),std::max(1.f,r-1.f),std::max(1.f,r-1.f)),accent.Get(),std::max(1.2f,1.5f*scale));
            }
            if(item.icon&&item.icon->width>0&&item.icon->height>0&&item.icon->stride>=item.icon->width*4&&
                item.icon->premultipliedBgra.size()>=std::size_t(item.icon->stride)*item.icon->height){
                Com<ID2D1Bitmap1> bitmap;const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
                Check(dc->CreateBitmap(D2D1::SizeU(item.icon->width,item.icon->height),item.icon->premultipliedBgra.data(),item.icon->stride,&properties,bitmap.Put()));
                const float x=float(item.bounds.left+std::max(0,(item.bounds.right-item.bounds.left-item.icon->width)/2)),y=float(item.bounds.top)+10.f*scale;
                dc->DrawBitmap(bitmap.Get(),D2D1::RectF(x,y,x+item.icon->width,y+item.icon->height));
            }
            const float top=float(item.bounds.bottom-std::max(34,int(std::lround(scene.metrics.tileFontPx*2.4f))));
            dc->DrawText(item.title.c_str(),UINT32(item.title.size()),title.Get(),D2D1::RectF(float(item.bounds.left)+4.f*scale,top,float(item.bounds.right)-4.f*scale,float(item.bounds.bottom)),text.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
            if(!item.subtitle.empty())dc->DrawText(item.subtitle.c_str(),UINT32(item.subtitle.size()),subtitle.Get(),
                D2D1::RectF(float(item.bounds.left)+4.f*scale,top+scene.metrics.tileFontPx+2.f*scale,float(item.bounds.right)-4.f*scale,float(item.bounds.bottom)),muted.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
            if(item.favorite)dc->DrawText(L"\u2605",1,subtitle.Get(),D2D1::RectF(float(item.bounds.right)-24.f*scale,
                float(item.bounds.top)+4.f*scale,float(item.bounds.right)-4.f*scale,float(item.bounds.top)+24.f*scale),accent.Get());
        }
        dc->PopAxisAlignedClip();
        if(Valid(scene.auxiliary.bounds)){
            const float ar=(scene.auxiliary.bounds.bottom-scene.auxiliary.bounds.top)*0.5f;
            const auto arr=D2D1::RoundedRect(Rect(scene.auxiliary.bounds),ar,ar);
            dc->FillRoundedRectangle(arr,scene.auxiliary.focused?pillFill.Get():auxFill.Get());
            dc->DrawRoundedRectangle(arr,scene.auxiliary.focused?accent.Get():pillBorder.Get(),std::max(1.f,1.2f*scale));
            if(scene.auxiliary.text==L"..."){
                const float x=(scene.auxiliary.bounds.left+scene.auxiliary.bounds.right)*.5f,y=(scene.auxiliary.bounds.top+scene.auxiliary.bounds.bottom)*.5f;
                for(int i=-1;i<=1;++i)dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x+i*5.f*scale,y),1.4f*scale,1.4f*scale),muted.Get());
            }else dc->DrawText(scene.auxiliary.text.c_str(),UINT32(scene.auxiliary.text.size()),aux.Get(),Rect(scene.auxiliary.bounds),text.Get());
        }
    }
    ShowRequest request_{};int width_=0,height_=0;ImGuiContext* imgui_=nullptr;bool closed_=false,detached_=true;
    DWORD originalAffinity_=0;
    bool affinityChanged_=false;
    FailureReason closeFailure_=FailureReason::None;
    const char* lastOperation_="not_initialized";
    SpotlightCaptions captions_;
    std::wstring exportPath_;
    std::wstring presentExportPath_;
    struct PresentExportRecord {const char* kind;preview::PresentationRecord record;};
    std::vector<PresentExportRecord> presentExport_;
    bool presentExportOverflow_=false;
    std::chrono::steady_clock::time_point exportDrainBegin_{};
    std::chrono::steady_clock::time_point openedAt_{};
    Com<ID3D11Device> device_;Com<ID3D11DeviceContext> context_;
    preview::CompositionPresenter presenter_;
    std::uint64_t frameSequence_=0;
    std::uint64_t presentTargetInterrupt100ns_=0;
    unsigned presentTargetRemainder_=0;
    std::vector<preview::PresentationRecord> feedback_;
    struct FrameTarget { ID3D11Texture2D* texture=nullptr;Com<ID3D11RenderTargetView> rtv; };
    std::array<FrameTarget,FrameBufferCount> frameTargets_;
    FrameTarget* frameTarget_=nullptr;
    Com<ID2D1Factory1> d2dFactory_;Com<ID2D1Device> d2dDevice_;Com<ID2D1DeviceContext> d2d_;Com<IDWriteFactory> write_;
    Com<ID3D11Texture2D> inkTexture_;Com<ID2D1Bitmap1> inkBitmap_;Com<ID3D11ShaderResourceView> inkView_;
    InkCompositor inkCompositor_;
    Glass::Renderer glass_;Glass::Backdrop backdrop_;
};
}

void SetLiquidSpotlightMode(bool spotlight){liquidSpotlight.store(spotlight);}
void SetLiquidSpotlightCaptions(SpotlightCaptions captions)
{
    std::lock_guard<std::mutex> lock(captionsMutex);spotlightCaptions=std::move(captions);
}
void SetLiquidGlassSettings(GlassVisualSettings settings)
{
    std::lock_guard<std::mutex> lock(captionsMutex);liveGlassSettings=settings;
}
void RequestLiquidFrameExport(std::wstring path)
{
    std::lock_guard<std::mutex> lock(captionsMutex);pendingFrameExport=std::move(path);
}
void RequestLiquidPresentExport(std::wstring path)
{
    std::lock_guard<std::mutex> lock(captionsMutex);pendingPresentExport=std::move(path);
}

class MaterialRenderer::Impl
{
public:
    Impl(){stopEvent_=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopEvent_)throw HRESULT_FROM_WIN32(GetLastError());}
    ~Impl(){Stop(true);CloseHandle(stopEvent_);}
    void Show(ShowRequest request,MotionVisual motion)
    {
        Stop(false);
        const bool useLiquid=liquidSpotlight.load()&&!request.policy.solidSurface&&request.forcedFailure==FailureReason::None;
        {std::lock_guard<std::mutex> lock(mutex_);request_=request;motion_=motion;lighting_={};frozen_=false;stop_=false;}
        if(!useLiquid){
            {std::lock_guard<std::mutex> lock(mutex_);diagnostics_=UnmeasuredDiagnostics();
                diagnostics_.path=request.policy.solidSurface?RenderPath::Solid:RenderPath::Fallback;
                diagnostics_.state=LifecycleState::Fallback;diagnostics_.failure=request.forcedFailure;
                diagnostics_.workerCleanupComplete=true;diagnostics_.workerCleanupActivation=request.activation;}
            Status();return;
        }
        {std::lock_guard<std::mutex> lock(mutex_);diagnostics_=UnmeasuredDiagnostics();diagnostics_.state=LifecycleState::Starting;
            diagnostics_.dpi=request.dpi;diagnostics_.reducedMotion=request.policy.reducedMotion;diagnostics_.queuedSceneGeneration=latest_?latest_->generation:0;}
        if(!ResetEvent(stopEvent_))throw HRESULT_FROM_WIN32(GetLastError());
        worker_=std::thread([this,request]{Run(request);});
    }
    void Submit(std::shared_ptr<const SceneSnapshot> scene)
    {
        {std::lock_guard<std::mutex> lock(mutex_);latest_=scene;diagnostics_.queuedSceneGeneration=scene?scene->generation:0;}
        condition_.notify_all();
    }
    void Motion(MotionVisual value){std::lock_guard<std::mutex> lock(mutex_);motion_=value;}
    void Lighting(LightingVisual value){std::lock_guard<std::mutex> lock(mutex_);lighting_=value;}
    void Stop(bool clearScene)
    {
        const bool hadWorker=worker_.joinable();
        {std::lock_guard<std::mutex> lock(mutex_);stop_=true;if(hadWorker)diagnostics_.state=LifecycleState::Stopping;}
        SetEvent(stopEvent_);condition_.notify_all();if(worker_.joinable())worker_.join();
        // Run may already have exited through Fallback before Hide was requested.
        // Joining still proves epoch destruction; always publish Hidden after join.
        {std::lock_guard<std::mutex> lock(mutex_);diagnostics_.state=LifecycleState::Hidden;diagnostics_.path=RenderPath::Fallback;
            diagnostics_.workerCleanupComplete=true;diagnostics_.workerCleanupActivation=request_.activation;}
        if(clearScene){std::lock_guard<std::mutex> lock(mutex_);latest_.reset();}
        if(hadWorker){const auto last=Snapshot();RendererLog(RendererLogPath(),clearScene?"hide_complete":"epoch_cleanup_complete",
            "worker_joined",request_.activation,last.acquiredFrames,last.presentedFrames,last.failure);}
        if(hadWorker)Status();
    }
    void Hide(){Stop(true);}
    void Rebuild(ShowRequest request){MotionVisual motion;{std::lock_guard<std::mutex> lock(mutex_);motion=motion_;}Show(request,motion);}
    void Freeze(){std::lock_guard<std::mutex> lock(mutex_);frozen_=true;}
    void Status()
    {
        ShowRequest request;{std::lock_guard<std::mutex> lock(mutex_);request=request_;++diagnostics_.statusPublications;}
        if(request.window&&request.statusMessage)PostMessageW(request.window,request.statusMessage,0,0);
    }
    Diagnostics Snapshot() const{std::lock_guard<std::mutex> lock(mutex_);return diagnostics_;}
    bool Hidden()const{const auto d=Snapshot();return d.state==LifecycleState::Hidden&&d.workerCleanupComplete;}
private:
    void Run(ShowRequest request)
    {
        const auto begin=std::chrono::steady_clock::now();const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        FailureReason failure=FailureReason::None,closeFailure=FailureReason::None;bool detached=true;
        const auto logPath=RendererLogPath();std::uint64_t finalAcquired=0,successfulPresents=0;
        {
            Session session;
            try{
                if(FAILED(apartment))throw apartment;session.Open(request);
                RendererLog(logPath,"initialization_succeeded",session.LastOperation(),request.activation,session.Acquired(),0);
                std::uint64_t inkGeneration=0,announcedGeneration=0;bool wasFrozen=false,inkBuilt=false,published=false;
                auto previous=std::chrono::steady_clock::now();
                for(;;){
                    std::shared_ptr<const SceneSnapshot> scene;MotionVisual motion;LightingVisual lighting;bool frozen=false;
                    {std::unique_lock<std::mutex> lock(mutex_);condition_.wait(lock,[&]{return stop_||latest_;});
                        if(stop_)break;scene=latest_;motion=motion_;lighting=lighting_;frozen=frozen_;}
                    if(scene->generation<request.requiredSceneGeneration){std::unique_lock<std::mutex> lock(mutex_);
                        condition_.wait_for(lock,std::chrono::milliseconds(10),[&]{return stop_||(latest_&&latest_->generation>=request.requiredSceneGeneration);});continue;}
                    if(!session.AcquireFrame(stopEvent_))break;
                    if(!inkBuilt||scene->generation!=inkGeneration){session.BuildInk(*scene);inkGeneration=scene->generation;inkBuilt=true;}
                    const auto now=std::chrono::steady_clock::now();const float delta=std::chrono::duration<float>(now-previous).count();previous=now;
                    const HRESULT result=session.Frame(*scene,motion,lighting,frozen,delta);
                    if(result==DXGI_STATUS_OCCLUDED){std::unique_lock<std::mutex> lock(mutex_);condition_.wait_for(lock,std::chrono::milliseconds(50),[&]{return stop_;});continue;}
                    Check(result);
                    ++successfulPresents;
                    if(successfulPresents==1)RendererLog(logPath,"first_present_succeeded",session.LastOperation(),request.activation,session.Acquired(),successfulPresents,FailureReason::None,true,result);
                    {std::lock_guard<std::mutex> lock(mutex_);diagnostics_.path=RenderPath::HighFidelity;diagnostics_.failure=FailureReason::None;
                        diagnostics_.state=frozen?LifecycleState::FrozenEvidence:LifecycleState::Live;diagnostics_.evidenceFrozen=frozen;
                        diagnostics_.adapter=session.adapterName;diagnostics_.output=session.outputName;
                        diagnostics_.acquiredFrames=session.Acquired();diagnostics_.lastFrameIdentity=session.Acquired();
                        diagnostics_.desktopSourceWidth=session.DesktopWidth();diagnostics_.desktopSourceHeight=session.DesktopHeight();
                        ++diagnostics_.presentedFrames;diagnostics_.committedActivation=request.activation;
                        diagnostics_.presentedSceneGeneration=scene->generation;diagnostics_.currentSceneGeneration=scene->generation;
                        diagnostics_.presentedDesktopFrameIdentity=session.Acquired();
                        if(!diagnostics_.firstFrameMs)diagnostics_.firstFrameMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
                        diagnostics_.auxiliaryRendered=Valid(scene->auxiliary.bounds)&&!scene->auxiliary.text.empty();
                        diagnostics_.auxiliarySceneGeneration=scene->generation;diagnostics_.auxiliaryBounds=scene->auxiliary.bounds;
                        diagnostics_.auxiliaryText=scene->auxiliary.text;diagnostics_.auxiliaryFocused=scene->auxiliary.focused;
                        auto& ink=diagnostics_.contentPresentation;ink.valid=true;++ink.revision;ink.sceneGeneration=scene->generation;
                        ink.desktopFrameIdentity=session.Acquired();ink.luminanceValid=false;ink.search={scene->policy.text,false};ink.focusRim={scene->policy.focus,false};
                        ink.auxiliaryText=ink.search;ink.auxiliaryRim=ink.focusRim;ink.auxiliaryBounds=scene->auxiliary.bounds;
                        ink.auxiliaryLabel=scene->auxiliary.text;ink.auxiliaryFocused=scene->auxiliary.focused;
                        if(!published||announcedGeneration!=scene->generation){ink.captions.clear();diagnostics_.presentedItemStableIds.clear();diagnostics_.presentedItemSourcePaths.clear();
                            for(const auto& item:scene->items){ink.captions.push_back({item.bounds,{scene->policy.text,false}});
                                if(request.developmentEvidence){diagnostics_.presentedItemStableIds.push_back(item.stableId);diagnostics_.presentedItemSourcePaths.push_back(item.sourcePath);}}}
                    }
                    if(!published||announcedGeneration!=scene->generation||wasFrozen!=frozen){announcedGeneration=scene->generation;wasFrozen=frozen;published=true;Status();}
                }
            }catch(FailureReason reason){failure=reason;RendererLog(logPath,"error",session.LastOperation(),request.activation,session.Acquired(),successfulPresents,reason);}
            catch(HRESULT result){failure=Reason(result);RendererLog(logPath,"error",session.LastOperation(),request.activation,session.Acquired(),successfulPresents,failure,true,result);}
            catch(...){failure=FailureReason::CompositionUnavailable;RendererLog(logPath,"error",session.LastOperation(),request.activation,session.Acquired(),successfulPresents,failure);}
            finalAcquired=session.Acquired();
            detached=session.Close();
            closeFailure=session.CloseFailure();
        }
        if(SUCCEEDED(apartment))CoUninitialize();
        RendererLog(logPath,"worker_finished",detached?"epoch_resources_destroyed":"epoch_cleanup_failed",request.activation,finalAcquired,successfulPresents,
            detached?failure:closeFailure);
        {std::lock_guard<std::mutex> lock(mutex_);
            if(!detached)failure=closeFailure;
            diagnostics_.workerCleanupComplete=true;diagnostics_.workerCleanupActivation=request.activation;
            diagnostics_.failure=failure;
            if(stop_){diagnostics_.state=LifecycleState::Hidden;diagnostics_.path=RenderPath::Fallback;}
            else{diagnostics_.state=LifecycleState::Fallback;diagnostics_.path=RenderPath::Fallback;diagnostics_.failure=failure;}}
        Status();condition_.notify_all();
    }
    mutable std::mutex mutex_;std::condition_variable condition_;std::thread worker_;
    std::shared_ptr<const SceneSnapshot> latest_;ShowRequest request_{};MotionVisual motion_{};LightingVisual lighting_{};
    Diagnostics diagnostics_=UnmeasuredDiagnostics();bool stop_=false,frozen_=false;
    HANDLE stopEvent_=nullptr;
};

MaterialRenderer::MaterialRenderer()=default;
MaterialRenderer::~MaterialRenderer()=default;
void MaterialRenderer::Show(ShowRequest request,MotionVisual motion){if(!impl_)impl_=std::make_unique<Impl>();impl_->Show(request,motion);}
void MaterialRenderer::Submit(std::shared_ptr<const SceneSnapshot> scene){if(!impl_)impl_=std::make_unique<Impl>();impl_->Submit(std::move(scene));}
void MaterialRenderer::SetMotion(MotionVisual motion){if(!impl_)impl_=std::make_unique<Impl>();impl_->Motion(motion);}
void MaterialRenderer::SetLighting(LightingVisual lighting){if(impl_)impl_->Lighting(lighting);}
void MaterialRenderer::Rebuild(ShowRequest request){if(!impl_)impl_=std::make_unique<Impl>();impl_->Rebuild(request);}
void MaterialRenderer::Hide(){if(impl_)impl_->Hide();}
void MaterialRenderer::Shutdown(){impl_.reset();}
void MaterialRenderer::FreezeEvidence(){if(impl_)impl_->Freeze();}
void MaterialRenderer::SnapshotCommittedEvidenceForDevelopment(){if(impl_)impl_->Status();}
void MaterialRenderer::AcquireUnpresentedEvidenceCandidateForDevelopment(){if(impl_)impl_->Status();}
void MaterialRenderer::RequestStatusForDevelopment(){if(impl_)impl_->Status();}
void MaterialRenderer::AcknowledgeStoppingStatusForDevelopment(){}
Diagnostics MaterialRenderer::SnapshotDiagnostics()const{return impl_?impl_->Snapshot():UnmeasuredDiagnostics();}
bool MaterialRenderer::WaitUntilHiddenForTest(std::uint32_t)const{return !impl_||impl_->Hidden();}
}
