# LLP-013 Live Material Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Spotlight's cached, nested screenshot lenses with one continuously live, monitor-aligned adaptive material rendered by DXGI Desktop Duplication, D3D11, Direct2D DeviceContext, and DirectComposition, while preserving a single stable no-displacement fallback and all existing input/accessibility behavior.

**Architecture:** `MaterialRenderer.{h,cpp}` is the sole GPU/capture owner and consumes latest-only immutable Spotlight scene snapshots built by the Win32 host from existing `SceneGeometry` and `AdaptiveEnvironment` values. A renderer thread owns output duplication, D3D/D2D/DComp resources, three composition visuals, lifecycle recovery, and diagnostics; the UI thread only submits snapshots and never waits for a frame. The existing layered D2D/GDI window remains the one fallback, reduced to one aligned cached backdrop with no nested sampling, magnification, or displacement.

**Tech Stack:** C++17 Win32, DXGI 1.2 Desktop Duplication, D3D11, Direct2D 1.1 effects, DirectWrite, DirectComposition, MinGW-w64 g++, PowerShell real-EXE harnesses, System.Drawing pixel analysis.

---

## Execution rules and file map

Use the approved design at `docs/superpowers/specs/2026-08-09-llp-013-live-material-renderer-design.md` as the source of truth. LLP-013 remains the only project task marked `in_progress`; do not activate LLP-014 until every gate in Task 10 is green.

Files created:

- `src/LightLaunchpad.NativeUi/MaterialRenderer.h` — immutable scene contract, pure mapping/material/lifecycle policies, renderer facade, stable diagnostics enums.
- `src/LightLaunchpad.NativeUi/MaterialRenderer.cpp` — renderer thread, icon rasterization, D3D11/DXGI/D2D/DComp ownership, live capture, composition, fallback decisions, recovery, evidence freeze.
- `tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp` — direct tests of production policy/state/coordinate code.
- `tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.ps1` — isolated compiler/link runner for the production module.
- `tests/LightLaunchpad.NativeUi.Tests/MaterialRendererBehavior.Tests.ps1` — isolated development/stable real-EXE behavior, live-refresh, failure, resource, and performance tests.
- `tests/LightLaunchpad.NativeUi.Tests/MaterialEvidenceBackground.cpp` — deterministic full-monitor background host with 12 static cases and two live transitions.
- `tools/capture-material-evidence.ps1` — exact viewport screenshots, pixel checks, contrast checks, and evidence manifest.

Files modified:

- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` — create/submit/hide the renderer, build snapshots, publish probe JSON, switch to the one fallback, remove legacy nested lenses.
- `tools/build-native-ui.ps1` — compile `MaterialRenderer.cpp` and link `d3d11`, `dxgi`, and `dcomp`.
- `tests/LightLaunchpad.NativeUi.Tests/AppHost.Tests.ps1` — clean-source build fixture includes the production renderer and libraries.
- `tests/LightLaunchpad.NativeUi.Tests/ExecutableIdentity.Tests.ps1` — clean/stable fixture includes the renderer; stable build rejects development evidence switches.
- `tests/LightLaunchpad.NativeUi.Tests/SpotlightFlowBehavior.Tests.ps1` — retain both canonical launch paths while the production renderer is active.
- `docs/ai/tasks.md` and `docs/ai/progress.md` — only after the implementation and all verification gates are complete.

Do not add a second capture API, a second GPU backend, a generalized scene framework, a recurring recovery timer, or any Windows App SDK dependency. Do not stage `LLP_reconstruction/` or `LightLaunchpad.NativeUi.o`. All builds use development output directories under `artifacts/` or isolated temporary roots; do not modify the stable install, `C:\TOOL`, HKCU Run, push, tag, or release state.

### Safety baseline before every implementation session

- [ ] Run the following read-only baseline and record it in the current session notes:

```powershell
git status --short
git branch --show-current
git rev-parse HEAD
$llpProtectedPath = [System.IO.Path]::GetFullPath('C:\Users\thoma\Desktop\启动台\artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe')
$llpProtectedProcesses = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and [System.IO.Path]::GetFullPath($_.ExecutablePath) -eq $llpProtectedPath })
if ($llpProtectedProcesses.Count -ne 1) { throw "Expected exactly one protected NativeUi process; found $($llpProtectedProcesses.Count)." }
$llpProtectedProcesses | Select-Object ProcessId, ExecutablePath, CreationDate
Get-FileHash -Algorithm SHA256 'C:\Users\thoma\Desktop\启动台\artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe'
Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name LightLaunchpad
if (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'Programs\LightLaunchpad\LightLaunchpad.NativeUi.exe')) { throw 'Stable install unexpectedly exists.' }
Get-FileHash -Algorithm SHA256 'C:\TOOL\LightLaunchpad-nativeui-win-x64-V1.10\LightLaunchpad.NativeUi.exe'
```

Expected: branch `codex/apple-first-v1.1`; exactly one process runs the protected path; hashes and Run command exactly match the current verified baseline; `LLP_reconstruction/` is the only untracked protected tree. The PID itself is observational and may change after a legitimate process restart.

- [ ] Before every commit, stage only explicit paths, run `git diff --cached --check`, and verify `git diff --cached --name-only` contains neither protected path.

## Task 1: Lock pure material policy, coordinate mapping, and lifecycle transitions

**Files:**

- Create: `src/LightLaunchpad.NativeUi/MaterialRenderer.h`
- Create: `src/LightLaunchpad.NativeUi/MaterialRenderer.cpp`
- Create: `tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp`
- Create: `tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.ps1`

- [ ] Write the failing production C++ tests first. The initial test file must exercise these public operations directly:

```cpp
using llp::material::AdvanceLifecycle;
using llp::material::ComputeSearchSample;
using llp::material::MapWindowCrop;
using llp::material::ResolveMaterialTone;

Require(MapWindowCrop({ 100, 200, 1100, 900 }, { 260, 320, 1240, 1040 }, llp::material::OutputRotation::Identity)
            == llp::material::PixelRect{ 160, 120, 1000, 840 },
    "Identity crop was not output-relative and window-aligned.");
Require(MapWindowCrop({ 0, 0, 1080, 1920 }, { 140, 420, 860, 1400 }, llp::material::OutputRotation::Rotate90)
            == llp::material::PixelRect{ 420, 220, 1400, 940 },
    "Rotated crop mapping changed the physical viewport.");

for (const int dpi : { 96, 144, 192 })
{
    const auto center = ComputeSearchSample({ 0.0f, 0.0f, 300.0f, 50.0f }, 150.0f, 25.0f, dpi, false);
    Require(std::abs(center.sourceXDip - 150.0f) <= 0.5f, "Search center exceeded 0.5 DIP.");
    Require(std::abs(center.sourceYDip - 25.0f) <= 0.5f, "Search center gained vertical bias.");
    const auto edge = ComputeSearchSample({ 0.0f, 0.0f, 300.0f, 50.0f }, 1.0f, 1.0f, dpi, false);
    Require(edge.displacementDip <= 1.5f, "Search edge exceeded 1.5 DIP.");
    Require(edge.sourceXDip >= 0.0f && edge.sourceYDip >= 0.0f, "Search sample escaped its control.");
}

const auto lightTone = ResolveMaterialTone(0.92f, false);
const auto darkTone = ResolveMaterialTone(0.05f, true);
Require(llp::adaptive::ContrastRatio(lightTone.text, lightTone.guardedSurface) >= 4.5,
    "Light material text contrast fell below 4.5:1.");
Require(llp::adaptive::ContrastRatio(darkTone.focus, darkTone.guardedSurface) >= 3.0,
    "Dark material focus contrast fell below 3:1.");

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
```

- [ ] Add `MaterialRenderer.Tests.ps1` using the same validated temporary-directory cleanup pattern as `SceneGeometry.Tests.ps1`. Compile the test with production `MaterialRenderer.cpp`, `AdaptiveEnvironment.cpp`, and these exact libraries:

```powershell
& $llpGpp `
    'tests\LightLaunchpad.NativeUi.Tests\MaterialRenderer.Tests.cpp' `
    'src\LightLaunchpad.NativeUi\MaterialRenderer.cpp' `
    'src\LightLaunchpad.NativeUi\AdaptiveEnvironment.cpp' `
    -I 'src\LightLaunchpad.NativeUi' `
    -std=c++17 -finput-charset=UTF-8 -fexec-charset=UTF-8 `
    -static -static-libgcc -static-libstdc++ -O2 `
    -ld3d11 -ldxgi -ldcomp -ld2d1 -ldwrite -lgdi32 -luser32 -lole32 -luuid `
    -o $llpTestExe
```

- [ ] Run RED:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRenderer.Tests.ps1
```

Expected: compilation fails because `MaterialRenderer.h` and the production operations do not yet exist. Preserve the exact compiler diagnostic in the LLP-013 work notes.

- [ ] Create the public pure contract exactly around these stable types. Keep Win32/GPU interfaces out of the pure functions so the same production code is directly testable:

```cpp
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
    None, UnsupportedOs, DisplayAffinityUnavailable, AdapterUnavailable,
    DuplicationUnavailable, FirstFrameTimeout, CompositionUnavailable,
    DeviceRemoved, AccessLost, ForcedForTest
};
enum class LifecycleState { Hidden, Starting, Live, Rebuilding, Fallback, FrozenEvidence, Stopping };
enum class LifecycleEvent
{
    ShowRequested, FirstFrameReady, FirstFrameTimedOut, InitializationFailed,
    AccessLost, DeviceRemoved, RebuildSucceeded, HideRequested, Hidden,
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

PixelRect MapWindowCrop(PixelRect outputDesktop, PixelRect windowDesktop, OutputRotation rotation);
float PixelsToDip(float pixels, int dpi);
SearchSample ComputeSearchSample(DipRect bounds, float destinationXDip, float destinationYDip, int dpi, bool reducedMotion);
MaterialTone ResolveMaterialTone(float relativeLuminance, bool darkMode);
LifecycleTransition AdvanceLifecycle(LifecycleState state, LifecycleEvent event, bool rebuildAlreadyUsed);
}
```

- [ ] Implement only the deterministic functions needed by the tests. Clamp every crop to the output, clamp every search source to its own `DipRect`, use zero center displacement, and cap the smooth edge response at 1.5 DIP. Select bounded cool white-blue tint/scrim values, then use `adaptive::ContrastRatio` to choose the existing policy text/focus colors rather than weakening contrast thresholds.

- [ ] Run GREEN twice:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRenderer.Tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\AdaptiveEnvironment.Tests.ps1
```

Expected: `MaterialRenderer` reports its named pure tests passed; `AdaptiveEnvironment` remains `3/3 tests passed`.

- [ ] Commit the pure production boundary and tests:

```powershell
git add src/LightLaunchpad.NativeUi/MaterialRenderer.h src/LightLaunchpad.NativeUi/MaterialRenderer.cpp tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.ps1
git diff --cached --check
git commit -m "test: lock LLP-013 material policies"
```

## Task 2: Add immutable scene and icon-raster ownership

**Files:**

- Modify: `src/LightLaunchpad.NativeUi/MaterialRenderer.h`
- Modify: `src/LightLaunchpad.NativeUi/MaterialRenderer.cpp`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp`

- [ ] Extend the production test first with a scene-lifetime case that creates an icon raster, puts it in two successive scenes, drops the producer reference, replaces an unrendered scene, and proves the newest scene still owns valid BGRA bytes. Add a latest-slot concurrency test that submits generations 1, 2, and 3 before consumption and requires generation 3 to be returned.

- [ ] Run RED with `MaterialRenderer.Tests.ps1`.

Expected: compile failure because `IconRaster`, `SceneSnapshot`, and `LatestSceneSlot` do not exist.

- [ ] Add the immutable scene contract. Use existing geometry/adaptive types; do not create competing layout or accessibility state:

```cpp
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

struct SceneSnapshot
{
    std::uint64_t generation = 0;
    llp::geometry::SceneMetrics metrics;
    llp::adaptive::Policy policy;
    SearchVisual search;
    std::vector<SceneItem> items;
    bool auxiliaryFocused = false;
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

std::shared_ptr<const IconRaster> RasterizeIcon(HICON icon, int sizePx);
```

- [ ] Implement `RasterizeIcon` with a top-down 32-bit DIB, `DrawIconEx`, and premultiplied BGRA output. It must copy all pixels before returning and release its DC/bitmap on every error path; the renderer never retains or destroys the input `HICON`.

- [ ] Add a bounded host-side cache design note to the header contract: cache keys are `(reinterpret_cast<std::uintptr_t>(icon), sizePx)`, values are `weak_ptr<const IconRaster>`, and the host clears the map with the existing icon/DPI resource resets. Do not introduce renderer filesystem or Shell calls.

- [ ] Run GREEN and commit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRenderer.Tests.ps1
git add src/LightLaunchpad.NativeUi/MaterialRenderer.h src/LightLaunchpad.NativeUi/MaterialRenderer.cpp tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp
git diff --cached --check
git commit -m "feat: add immutable material scenes"
```

## Task 3: Prove the production build and renderer-thread GPU bootstrap

**Files:**

- Modify: `src/LightLaunchpad.NativeUi/MaterialRenderer.h`
- Modify: `src/LightLaunchpad.NativeUi/MaterialRenderer.cpp`
- Modify: `tools/build-native-ui.ps1`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/AppHost.Tests.ps1`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/ExecutableIdentity.Tests.ps1`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp`

- [ ] Add a test-only `RendererDependenciesAvailable()` production probe that creates and releases a BGRA-capable D3D11 device, queries `IDXGIDevice`, creates an `ID2D1Device`, and creates an `IDCompositionDevice`. The test must require all four stages and record the selected feature level.

- [ ] Run RED. Expected: the new dependency test fails to compile because the renderer facade and dependency probe are absent.

- [ ] Add the renderer facade with a private implementation so GPU headers and mutable resources stay out of the host:

```cpp
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
};

struct Diagnostics
{
    RenderPath path = RenderPath::Fallback;
    FailureReason failure = FailureReason::None;
    LifecycleState state = LifecycleState::Hidden;
    std::uint64_t acquiredFrames = 0;
    std::uint64_t presentedFrames = 0;
    std::uint64_t droppedScenes = 0;
    std::uint64_t deviceRebuilds = 0;
    std::uint64_t lastFrameIdentity = 0;
    double firstFrameMs = 0.0;
    double frameP95Ms = 0.0;
    bool evidenceFrozen = false;
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
    void Rebuild(ShowRequest request);
    void Hide();
    void FreezeEvidence();
    Diagnostics SnapshotDiagnostics() const;
    bool WaitUntilHiddenForTest(std::uint32_t timeoutMs) const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

- [ ] Start one renderer thread in `Impl` construction and join it only in destruction. Initialize COM and every GPU object on that thread. The condition-variable predicate is `shutdown || showRequested || hideRequested || rebuildRequested || freezeRequested || pendingScene`; it must not wake periodically while hidden.

- [ ] In `InitializeDevice`, call `D3D11CreateDevice` with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`, query `IDXGIDevice`, call `D2D1CreateDevice`, `CreateDeviceContext`, `DWriteCreateFactory`, and `DCompositionCreateDevice`. Return a stable `FailureReason` at the first failed stage and release partially built objects in reverse order.

- [ ] Update every production/clean-source compile list with `src\LightLaunchpad.NativeUi\MaterialRenderer.cpp`, and add these libraries immediately after the existing D2D/DWrite entries:

```powershell
-ld3d11 `
-ldxgi `
-ldcomp `
```

- [ ] Run GREEN against the module and build a real development EXE:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRenderer.Tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-native-ui.ps1 -OutputDirectory artifacts\native-ui-llp013-bootstrap -ProductVersion 1.11.0 -BuildChannel Development
& .\artifacts\native-ui-llp013-bootstrap\LightLaunchpad.NativeUi.exe --diagnostics-file artifacts\native-ui-llp013-bootstrap\diagnostics.json
```

Expected: production tests pass, the real EXE exits 0 in diagnostics mode, and diagnostics identity remains development-channel/current-commit. No renderer is shown in this task.

- [ ] Commit:

```powershell
git add src/LightLaunchpad.NativeUi/MaterialRenderer.h src/LightLaunchpad.NativeUi/MaterialRenderer.cpp tools/build-native-ui.ps1 tests/LightLaunchpad.NativeUi.Tests/AppHost.Tests.ps1 tests/LightLaunchpad.NativeUi.Tests/ExecutableIdentity.Tests.ps1 tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp
git diff --cached --check
git commit -m "feat: bootstrap native material renderer"
```

## Task 4: Implement monitor-bound Desktop Duplication lifecycle

**Files:**

- Modify: `src/LightLaunchpad.NativeUi/MaterialRenderer.cpp`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp`
- Create: `tests/LightLaunchpad.NativeUi.Tests/MaterialRendererBehavior.Tests.ps1`

- [ ] Extend the C++ test with output enumeration selection, all four rotation mappings, negative virtual-desktop origins, partial-clamp rejection, 32 ms first-frame timeout, hide, access-loss, and exactly-one-rebuild cases. Add a fake backend interface private to `MaterialRenderer.cpp` under `LLP_MATERIAL_TESTING` so the production lifecycle loop is exercised without replacing the real backend.

- [ ] Add the real-EXE harness shell now. It must build into a GUID-named temporary directory, use isolated `APPDATA`, `LOCALAPPDATA`, and `USERPROFILE`, never register the real hotkey/startup entry, and always stop only its captured PID in `finally`. First RED assertion: a development EXE launched with `--material-probe-file` and `--material-force-failure duplication` must emit `path=fallback`, `failure=ForcedForTest`, and responsive search input; it currently fails because the switches do not exist.

- [ ] Implement adapter/output selection on the renderer thread:

```cpp
bool Impl::SelectOutput(HMONITOR monitor)
{
    for (UINT adapterIndex = 0; factory_->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND; ++adapterIndex)
    {
        for (UINT outputIndex = 0; adapter->EnumOutputs(outputIndex, &output) != DXGI_ERROR_NOT_FOUND; ++outputIndex)
        {
            DXGI_OUTPUT_DESC description = {};
            if (SUCCEEDED(output->GetDesc(&description)) && description.Monitor == monitor)
                return BindSelectedOutput(adapter, output, description);
            SafeRelease(output);
        }
        SafeRelease(adapter);
    }
    return false;
}
```

The actual implementation must use local RAII COM holders rather than leaked loop variables, but preserve this exact monitor-identity selection rule. Create the D3D11 device on the selected adapter, query `IDXGIOutput1`, and call `DuplicateOutput` once per visible activation.

- [ ] Before the high-fidelity show, dynamically resolve and call `SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE)`. Treat OS versions without effective exclusion as `UnsupportedOs`; clear the affinity with `WDA_NONE` on fallback, hide, freeze, and shutdown.

- [ ] Implement first-frame acquisition with a QPC deadline of 32 ms. Each visible wait is capped at 16 ms; `DXGI_ERROR_WAIT_TIMEOUT` does not redraw. Always call `ReleaseFrame` before reacquiring, rebuilding, freezing, or hiding.

- [ ] On hide, signal and return immediately on the UI caller. The renderer thread releases the acquired frame, duplication object, monitor-sized textures, and visible DComp surfaces, sets state `Hidden`, then signals the hidden condition. It performs no timed wait while hidden.

- [ ] Implement forced failure only when `LLP_BUILD_CHANNEL_WIDE` is `development`; accept `initialization`, `duplication`, and `access-lost`. Stable builds return command-line error 2 before creating the window when any material test switch is present.

- [ ] Run RED/GREEN gates:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRenderer.Tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRendererBehavior.Tests.ps1 -Mode Lifecycle
```

Expected: unit/state tests pass; real development EXE returns a stable fallback reason and remains fully searchable under forced failure; hidden renderer reports no frame-count advance for two seconds; stable test switches exit 2.

- [ ] Commit:

```powershell
git add src/LightLaunchpad.NativeUi/MaterialRenderer.cpp tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp tests/LightLaunchpad.NativeUi.Tests/MaterialRendererBehavior.Tests.ps1
git diff --cached --check
git commit -m "feat: add live desktop capture lifecycle"
```

## Task 5: Build the three-layer DirectComposition frame

**Files:**

- Modify: `src/LightLaunchpad.NativeUi/MaterialRenderer.cpp`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp`

- [ ] Add a test backend that records render commands and assert this order for one scene: transparent clear; one full-window desktop input; one material pass; overlay pass; content pass; one DirectComposition commit. Require that the overlay/content command records never reference the desktop bitmap and focused tiles never emit displacement or scale commands.

- [ ] Run RED. Expected: command-order and single-source assertions fail because surfaces are not rendered yet.

- [ ] Create one `IDCompositionTarget` for the existing HWND and exactly three child visuals. Create one premultiplied-alpha `IDCompositionSurface` per visual at the physical client size and set them on root in this order: material, overlay, content. Clear every surface to transparent at the start of a redraw.

- [ ] For the acquired `ID3D11Texture2D`, query `IDXGISurface`, create an `ID2D1Bitmap1` with `D2D1_BITMAP_OPTIONS_CANNOT_DRAW`, and crop only `MapWindowCrop`'s exact aligned rectangle. Normalize rotation once in the crop transform; never scale the desktop source to manufacture refraction.

- [ ] Build one bounded material graph on the material surface:

```cpp
context_->CreateEffect(CLSID_D2D1GaussianBlur, &blur_);
blur_->SetInput(0, desktopBitmap_);
blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, tone.blurRadiusDip * dpi / 96.0f);
blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
context_->DrawImage(blur_, windowDestination, windowSource, D2D1_INTERPOLATION_MODE_LINEAR,
    D2D1_COMPOSITE_MODE_SOURCE_COPY);
```

Then apply the bounded cool white-blue tint, clarity scrim, inner rim, and shadow from `MaterialTone`. Clip the whole graph to the same antialiased rounded geometry used by `SceneGeometry`; pixels outside the boundary remain alpha zero.

- [ ] Reduce the desktop to a fixed 16×12 luminance texture on the GPU. Use two staging buffers alternately, request `D3D11_MAP_FLAG_DO_NOT_WAIT`, and read at most once per 100 ms only after a changed desktop frame. If mapping returns `DXGI_ERROR_WAS_STILL_DRAWING`, keep the prior grid without blocking.

- [ ] Draw the search/focus fills and edge light on the overlay surface. The search response uses `ComputeSearchSample`; its center remains aligned, its edge is capped, its coordinates are clamped inside `scene.metrics.search`, and reduced motion forces zero displacement. Focused tiles use fill/rim/shadow only.

- [ ] Draw icons, titles, subtitles, search text, selection, IME underline, and caret on the content surface using the immutable scene. Create all `ID2D1Bitmap1` icon resources from the scene's copied premultiplied BGRA, never from `HICON`. Use the scene's existing text/caret indices and `SceneGeometry` bounds; do not create a second UIA tree.

- [ ] Commit one DComp transaction after all dirty surfaces end drawing. Present only when the desktop frame changed, the latest scene generation changed, or a rebuild/freeze event occurred. A capture timeout alone must not increment `presentedFrames`.

- [ ] Run GREEN and commit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRenderer.Tests.ps1
git add src/LightLaunchpad.NativeUi/MaterialRenderer.cpp tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp
git diff --cached --check
git commit -m "feat: compose unified Spotlight material"
```

## Task 6: Integrate the host and reduce the single fallback

**Files:**

- Modify: `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp`
- Modify: `src/LightLaunchpad.NativeUi/MaterialRenderer.h`
- Modify: `src/LightLaunchpad.NativeUi/MaterialRenderer.cpp`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/MaterialRendererBehavior.Tests.ps1`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/SpotlightFlowBehavior.Tests.ps1`

- [ ] Extend the real-EXE harness first. In normal development mode, open Spotlight, wait for renderer diagnostics, type a query, move Down/Right, and require that the immutable scene generation advances while the UIA Value/focus and both exact launch paths still work. Add a fallback case that repeats both paths under forced initialization failure.

- [ ] Run RED. Expected: the real EXE reports no material path/scene generations and still uses the legacy painter.

- [ ] Add these host globals only:

```cpp
llp::material::MaterialRenderer g_materialRenderer;
std::uint64_t g_materialSceneGeneration = 0;
std::unordered_map<std::uint64_t, std::weak_ptr<const llp::material::IconRaster>> g_materialIconRasters;
constexpr UINT WmMaterialRendererStatus = WM_APP + 76;
```

- [ ] Implement `BuildMaterialSceneSnapshot(const RECT& client)` from `CurrentSceneMetrics(client)`, `BuildContentScene()`, `g_adaptivePolicy`, `g_searchInput`, `g_spotlightState`, and visible `g_filtered` items. Copy strings and shared icon rasters; do not expose `g_items`, `HICON`, or mutable input state to the renderer thread. Clear the raster cache from `ResetScaleSensitiveResources`, `ReleaseAllResidentIcons`, and icon-size changes.

- [ ] Replace every visible Spotlight `RequestSearchRepaint` layered repaint with a latest-only scene submission when diagnostics path is `HighFidelity`. Leave Launchpad painting unchanged. The `WM_PAINT` Spotlight branch must validate the region and return without invoking the fallback painter while high fidelity owns composition.

- [ ] In `ShowNativeUi`, after `PositionWindow` and before `ShowWindow`, prepare the one aligned fallback capture, submit the initial scene, and call `g_materialRenderer.Show` with the target monitor, physical window rectangle, DPI, adaptive policy, and status message. Show fallback immediately; switch atomically to DComp when the first high-fidelity frame arrives within 32 ms. Input activation and UIA focus remain on the existing UI thread path.

- [ ] In `HideNativeUi`, call `g_materialRenderer.Hide()` before hiding the HWND, then clear affinity and leave the method nonblocking. On `WM_DPICHANGED`, `WM_DISPLAYCHANGE`, and adaptive preference change, submit a rebuilt request only when visible. On `WM_DESTROY`, call `Hide`, then destroy the renderer before `CoUninitialize`.

- [ ] Delete these legacy nested/magnifying functions and every call site:

```text
SpotlightBackdropSourceRectForLens
SpotlightBackdropSourceRectForLensGdi
DrawSearchEdgeRefraction
DrawLiquidGlassFocusLens
DrawFocusedTileLens
```

Retain `CaptureSpotlightBackdrop` only for one pre-show, full-window-aligned fallback capture. `DrawSearchFocusLens` in fallback becomes a clipped tint/rim/shadow draw with no backdrop sampling. Do not recapture/reblur on search, focus, DPI paint, or display paint during the same fallback-visible session.

- [ ] Add narrowly scoped architecture assertions to `MaterialRendererBehavior.Tests.ps1`: production build links D3D11/DXGI/DComp, the legacy function names above are absent, Spotlight does not request DWM/system backdrop, and neither renderer nor show/hide path calls `Sleep`. Label these architecture guards; do not use them as visual proof.

- [ ] Run both real canonical paths and fallback:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\SpotlightFlowBehavior.Tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRendererBehavior.Tests.ps1 -Mode Host
```

Expected: SpotlightFlow remains green with both exact Shell launches; high fidelity and forced fallback both retain input, navigation, UIA focus, and launch; no legacy nested sampling symbol remains.

- [ ] Commit:

```powershell
git add src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp src/LightLaunchpad.NativeUi/MaterialRenderer.h src/LightLaunchpad.NativeUi/MaterialRenderer.cpp tests/LightLaunchpad.NativeUi.Tests/MaterialRendererBehavior.Tests.ps1 tests/LightLaunchpad.NativeUi.Tests/SpotlightFlowBehavior.Tests.ps1
git diff --cached --check
git commit -m "feat: route Spotlight through live material"
```

## Task 7: Finish recovery, adaptive routing, diagnostics, and evidence freeze

**Files:**

- Modify: `src/LightLaunchpad.NativeUi/MaterialRenderer.cpp`
- Modify: `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/MaterialRendererBehavior.Tests.ps1`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/ExecutableIdentity.Tests.ps1`

- [ ] Add RED cases for high contrast, transparency disabled, reduced motion, access loss with successful rebuild, access loss with failed rebuild, display change after fallback, and evidence freeze. Require one rebuild only per visible epoch and no recurring retry.

- [ ] Add development-only arguments `--material-probe-file`, `--material-probe-ready-file`, `--material-evidence`, and `--material-force-failure`. Reject all four in stable builds with exit 2. Register `LightLaunchpad.MaterialEvidenceFreeze` and have the harness send that message to the real window.

- [ ] Route high contrast and disabled transparency directly to `RenderPath::Solid`: no output duplication, no display affinity, no displacement, existing system/adaptive colors. Reduced motion may retain live blur/tint but forces `ComputeSearchSample` to identity and records `reducedMotion=true`.

- [ ] Treat `DXGI_ERROR_ACCESS_LOST`, `DXGI_ERROR_DEVICE_REMOVED`, and `DXGI_ERROR_DEVICE_RESET` as one immediate reconstruction request. Release all output/device/DComp resources, rebuild once, and increment `deviceRebuilds`. A second loss or failed rebuild selects fallback until next show, real display change, or desktop switch.

- [ ] Publish stable diagnostics from a copied `Diagnostics` snapshot through the existing host JSON writer. Include:

```json
{
  "path": "high-fidelity",
  "failure": "none",
  "state": "live",
  "adapter": "adapter description",
  "output": "monitor device name",
  "dpi": 144,
  "acquiredFrames": 1,
  "presentedFrames": 1,
  "droppedScenes": 0,
  "deviceRebuilds": 0,
  "lastFrameIdentity": 1,
  "firstFrameMs": 0.0,
  "frameP95Ms": 0.0,
  "evidenceFrozen": false
}
```

Numeric values are measured, not fixed; field names and string enums are fixed. The host writes files only after renderer status messages or shutdown; the renderer never reads/writes the filesystem.

- [ ] Implement `FreezeEvidence`: retain the last acquired desktop texture and composed scene, stop/release duplication, set `WDA_NONE`, redraw/commit that exact frame once, set state `FrozenEvidence`, and never resume capture in that process. Stable builds cannot enter this state.

- [ ] Run GREEN:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRenderer.Tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRendererBehavior.Tests.ps1 -Mode Recovery
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\ExecutableIdentity.Tests.ps1
```

Expected: adaptive and recovery matrix passes, evidence freeze removes affinity and stops frame identity, stable evidence switches are rejected, identity suite remains `5/5`.

- [ ] Commit:

```powershell
git add src/LightLaunchpad.NativeUi/MaterialRenderer.cpp src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.cpp tests/LightLaunchpad.NativeUi.Tests/MaterialRendererBehavior.Tests.ps1 tests/LightLaunchpad.NativeUi.Tests/ExecutableIdentity.Tests.ps1
git diff --cached --check
git commit -m "feat: harden material recovery and evidence"
```

## Task 8: Build deterministic 12-background pixel and live-refresh evidence

**Files:**

- Create: `tests/LightLaunchpad.NativeUi.Tests/MaterialEvidenceBackground.cpp`
- Create: `tools/capture-material-evidence.ps1`
- Modify: `tests/LightLaunchpad.NativeUi.Tests/MaterialRendererBehavior.Tests.ps1`

- [ ] Create the deterministic background host before the acceptance script. It must render exactly these cases at the selected monitor's physical resolution: `light-flat`, `light-gradient`, `light-low-contrast`, `dark-flat`, `dark-gradient`, `dark-low-contrast`, `text-grid`, `checker-fine`, `vertical-lines`, `saturated-warm`, `saturated-cool`, and `black-white-extreme`. Case indices and RGB formulas are constants in C++, not external assets.

- [ ] The host window class is `LightLaunchpadMaterialEvidenceBackground`. It handles `WM_APP + 73` by changing to the case in `wParam`, repainting synchronously, and setting a shared monotonically increasing frame ID returned through `WM_APP + 74`. Cases `text-grid` and `saturated-cool` must change phase while Spotlight remains visible for live-refresh checks.

- [ ] Add RED assertions to `capture-material-evidence.ps1`: 12 exact-size PNGs plus one real-desktop PNG must exist; every case must have a diagnostics JSON; two live cases must show different pre/post desktop frame identities without hide/reopen; all pixel and contrast checks must pass. The initial run fails because no captures exist.

- [ ] Implement the safe harness flow for each case:

1. Start the isolated background EXE and development NativeUi EXE; capture both PIDs.
2. Send the existing simulated Alt+D message and drive the exact query/focus state.
3. Wait for `path=high-fidelity`, advance the background phase when the case is live, and require `lastFrameIdentity` to change.
4. Send `LightLaunchpad.MaterialEvidenceFreeze`, wait for `state=frozen-evidence` and `evidenceFrozen=true`.
5. Capture the real onscreen NativeUi window at its exact physical bounds with `PrintWindow(PW_RENDERFULLCONTENT)`.
6. Stop only the captured isolated PIDs in `finally`, then validate temporary cleanup boundaries and reject reparse points.

- [ ] Implement pixel analysis against the production-published `window`, `roundedBoundary`, `search`, `searchText`, and focused-tile rectangles:

  - compare pixels outside the antialiased rounded boundary to the same background case and reject any rectangular material delta above 2/255 outside the one-pixel fringe;
  - compare the search centerline source/output correlation and reject a center translation above 0.5 DIP;
  - inspect all edge sample vectors recorded in diagnostics and reject magnitude above 1.5 DIP or any source outside `search`;
  - detect a second high-contrast glyph/icon edge displaced from the content mask and reject duplicated components above 5% of the primary component area;
  - compare the focused-tile background frequency to its unfocused neighbor and reject any scale/magnification delta above 0.5%;
  - measure normal glyph edge luminance against the adjacent guarded material and require a minimum ratio of 4.5:1;
  - measure the focused boundary against both inside and outside adjacent pixels and require a target ratio of at least 3:1;
  - require every corner pixel outside the rounded boundary to match the controlled background, proving no opaque rectangle leaked.

- [ ] Write `manifest.json` with build identity, EXE SHA256, monitor/DPI, viewport, background name, renderer path, frame identities, pixel maxima, contrast minima, and relative PNG paths. The real desktop is `supplemental-real-desktop` and cannot replace a controlled case.

- [ ] Run the evidence gate:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\capture-material-evidence.ps1 -OutputDirectory artifacts\native-ui-llp013-material-evidence
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRendererBehavior.Tests.ps1 -Mode Visual -EvidenceDirectory artifacts\native-ui-llp013-material-evidence
```

Expected: 12 controlled and one supplemental image, two advancing live identities, zero ghost/duplicate/leak/out-of-bounds failures, text contrast minimum at least 4.5, focus-boundary target at least 3.0.

- [ ] Use `product-design:audit` on original-resolution controlled images and the real desktop image. Record findings against hierarchy, restrained cool white-blue treatment, search legibility, focus clarity, corner alpha, and consistency. Any actionable issue returns to the earliest responsible RED test before this task can commit.

- [ ] Commit evidence tooling and behavior tests; do not commit generated `artifacts/`:

```powershell
git add tests/LightLaunchpad.NativeUi.Tests/MaterialEvidenceBackground.cpp tests/LightLaunchpad.NativeUi.Tests/MaterialRendererBehavior.Tests.ps1 tools/capture-material-evidence.ps1
git diff --cached --check
git commit -m "test: verify LLP-013 material pixels"
```

## Task 9: Measure real-EXE performance and run affected/full regression

**Files:**

- Modify only if a failing production test identifies a root cause in an LLP-013 file.
- Evidence output: `artifacts/native-ui-llp013-verification/`

- [ ] Extend `MaterialRendererBehavior.Tests.ps1 -Mode Performance` to run 30 warm opens, 600 visible interaction frames, 20 focus changes, and a two-second hidden observation. Use `GetProcessMemoryInfo`, `GetProcessTimes`, QPC diagnostics, and renderer counters from the real isolated EXE.

- [ ] Run the focused performance gate:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRendererBehavior.Tests.ps1 -Mode Performance -EvidenceDirectory artifacts\native-ui-llp013-verification
```

Expected: hotkey-to-input-ready P95 `<=180 ms`; interaction frame P95 `<=16.7 ms`; focus feedback P95 `<=50 ms`; hidden frame counters do not advance; hidden CPU is near 0%; hidden Private Bytes target `<=15 MB`. Record visible CPU/Private Bytes, GPU adapter/path, acquired/presented/dropped frames, first-frame time, and rebuild count. If retaining the device alone breaks 15 MB, release the device on hide and retest; do not add a cache tier.

- [ ] Run every NativeUi suite sequentially so shared outputs cannot race:

```powershell
$llpSuites = Get-ChildItem tests\LightLaunchpad.NativeUi.Tests\*.Tests.ps1 | Sort-Object Name
foreach ($llpSuite in $llpSuites) {
    powershell -NoProfile -ExecutionPolicy Bypass -File $llpSuite.FullName
    if ($LASTEXITCODE -ne 0) { throw "NativeUi suite failed: $($llpSuite.Name)" }
}
```

Expected: all 18 suites pass after adding `MaterialRenderer.Tests.ps1` and `MaterialRendererBehavior.Tests.ps1`; direct production totals retain their existing counts plus the new material count.

- [ ] Run Core and historical App suites sequentially:

```powershell
dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj
dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj
```

Expected: Core `33/33`; historical App `74/74`. The App source suite remains regression-only and is not evidence for visual, capture, DPI, or animation correctness.

- [ ] Build and probe the final dirty development EXE without replacing any earlier real EXE:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-native-ui.ps1 -OutputDirectory artifacts\native-ui-llp013-precommit -ProductVersion 1.11.0 -BuildChannel Development
Get-FileHash -Algorithm SHA256 artifacts\native-ui-llp013-precommit\LightLaunchpad.NativeUi.exe
& .\artifacts\native-ui-llp013-precommit\LightLaunchpad.NativeUi.exe --diagnostics-file artifacts\native-ui-llp013-precommit\diagnostics.json
```

Expected: real EXE exits 0, reports the current development commit/dirty identity, Per-Monitor V2, and non-stable path.

- [ ] Inspect `git diff`, rerun every changed test after the final production edit, and run `git diff --check`. Do not declare LLP-013 complete if any expected evidence is missing or stale.

## Task 10: Commit verified implementation, update durable project state, and produce the post-commit EXE

**Files:**

- Modify: `docs/ai/tasks.md`
- Modify: `docs/ai/progress.md`

- [ ] If Task 9 required uncommitted production fixes, stage only their explicit files, run the affected and full gates again, and commit them:

```powershell
git diff --cached --check
git commit -m "fix: complete LLP-013 material gates"
```

Skip this commit when Task 9 needed no production change.

- [ ] Update `docs/ai/tasks.md`: mark LLP-013 `done`, record all scoped commits and concise verified completion notes, then mark LLP-014 as the sole `in_progress` task. No other task status changes.

- [ ] Prepend a complete LLP-013 section to `docs/ai/progress.md` containing changed files, RED/GREEN history, exact commands/results, 12+1 screenshot manifest, product-design audit result, pixel/contrast maxima/minima, performance measurements, real EXE path/size/SHA256/build identity, safety recheck, implementation commits, and next task LLP-014. Update the header HEAD only after the documentation commit exists.

- [ ] Verify staged documentation scope and commit:

```powershell
git add docs/ai/tasks.md docs/ai/progress.md
git diff --cached --check
git diff --cached --name-only
git commit -m "docs: record LLP-013 verification"
```

Expected staged names: exactly `docs/ai/tasks.md` and `docs/ai/progress.md`.

- [ ] Build and verify a fresh post-commit EXE using the documentation commit identity:

```powershell
$llpHead = (git rev-parse HEAD).Trim()
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-native-ui.ps1 -OutputDirectory artifacts\native-ui-llp013-postcommit -ProductVersion 1.11.0 -BuildCommit $llpHead.Substring(0,12) -BuildChannel Development
Get-FileHash -Algorithm SHA256 artifacts\native-ui-llp013-postcommit\LightLaunchpad.NativeUi.exe
& .\artifacts\native-ui-llp013-postcommit\LightLaunchpad.NativeUi.exe --diagnostics-file artifacts\native-ui-llp013-postcommit\diagnostics.json
```

Expected: diagnostics commit equals the first 12 characters of HEAD, channel is `development`, executable path is the post-commit artifact, and stable/startup identities are unchanged.

- [ ] Recheck the protected runtime and working tree:

```powershell
git status --short
$llpProtectedPath = [System.IO.Path]::GetFullPath('C:\Users\thoma\Desktop\启动台\artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe')
$llpProtectedProcesses = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and [System.IO.Path]::GetFullPath($_.ExecutablePath) -eq $llpProtectedPath })
if ($llpProtectedProcesses.Count -ne 1) { throw "Expected exactly one protected NativeUi process; found $($llpProtectedProcesses.Count)." }
$llpProtectedProcesses | Select-Object ProcessId, ExecutablePath, CreationDate
Get-FileHash -Algorithm SHA256 'C:\Users\thoma\Desktop\启动台\artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe'
Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name LightLaunchpad
if (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'Programs\LightLaunchpad\LightLaunchpad.NativeUi.exe')) { throw 'Stable install unexpectedly exists.' }
Get-FileHash -Algorithm SHA256 'C:\TOOL\LightLaunchpad-nativeui-win-x64-V1.10\LightLaunchpad.NativeUi.exe'
```

Expected: only `LLP_reconstruction/` remains untracked; exactly one process still runs the protected path; protected hash, startup command, stable-install absence, and `C:\TOOL` hash remain unchanged. No push, tag, release, or installation action occurred.

## Plan self-review gate

- [ ] Confirm every approved design section has a corresponding implementation/test task: ownership, composition tree, material model, live capture, fallback/recovery, accessibility, diagnostics/performance, real-EXE evidence, and 12 controlled backgrounds.
- [ ] Search the plan for unfinished markers and ambiguous code instructions:

```powershell
$llpUnfinishedMarkers = @('TO' + 'DO', 'T' + 'BD', 'FIX' + 'ME', 'fill ' + 'this', 'implement ' + 'later', 'and so ' + 'on')
Select-String -Path docs\superpowers\plans\2026-08-09-llp-013-live-material-renderer.md -Pattern $llpUnfinishedMarkers
```

Expected: no matches.

- [ ] Confirm type consistency: all coordinates explicitly say physical pixels or DIP; `SceneSnapshot` owns strings and shared pixel rasters; renderer diagnostics use stable enums; only the renderer thread owns COM/GPU/capture objects; only the host owns file/probe writes and UIA.
- [ ] Confirm scope: one primary DDA/D3D11/D2D/DComp path, one no-displacement layered fallback, one bounded rebuild, one latest-only scene slot, and no speculative backend/cache/retry layer.
- [ ] Confirm the prohibited routes are absent from implementation steps: no glass-on-glass, no 1.16× source magnification, no upward/out-of-bounds search sample, no UI-thread `Sleep`, no system Acrylic/DWM backdrop, and no private `IsPromoted` registry mutation.
