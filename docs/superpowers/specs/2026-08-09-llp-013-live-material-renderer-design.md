# LLP-013 Continuous Live Material Renderer Design

Date: 2026-08-09
Status: approved for implementation planning
Task: LLP-013 — Unified Adaptive Material Renderer

## 1. Decision record

The user approved these product choices:

- Acceptance evidence uses 12 reproducible controlled backgrounds as the primary set, plus the current real desktop as supplemental evidence.
- The high-fidelity path shows a continuously updating live desktop background while Spotlight is visible.
- The default material is restrained cool white-blue: the desktop remains recognizable, while text and focus contrast take priority over spectacle.
- The selected implementation route is DXGI Desktop Duplication feeding one D3D11, Direct2D DeviceContext, and DirectComposition pipeline.

The high-fidelity renderer must not use system Acrylic, DWM BlurBehind, `DWMWA_SYSTEMBACKDROP_TYPE`, shifted screenshot rectangles, tile magnification, UI-thread sleep, or private Windows registry behavior.

## 2. Goals and non-goals

### Goals

1. Render one continuous optical material over the full Spotlight surface from a live desktop frame.
2. Keep all text and icons in a separate content layer above the material, so they are never blurred, displaced, or sampled a second time.
3. Keep the search center aligned within 0.5 DIP, constrain edge displacement to 1.5 DIP, and never sample outside the search control's own bounds.
4. Preserve the existing Per-Monitor V2 geometry, input, IME, navigation, accessibility, and foreground-monitor placement behavior.
5. Keep capture and GPU work off the UI thread and stop all visible-frame work as soon as Spotlight hides.
6. Provide the single PRD-required no-displacement fallback for unsupported or failed high-fidelity rendering.
7. Produce real development-channel NativeUi EXE evidence for static appearance, continuous refresh, contrast, frame time, memory, and fallback behavior.

### Non-goals

- LLP-014 timing curves and focus/open/close animations are not implemented here. LLP-013 supplies a compositor-ready visual tree and nonblocking render loop for LLP-014.
- Launchpad editing interactions, catalog behavior, persistence, and Shell launch behavior do not change.
- No Windows App SDK, WinUI, new package runtime, alternate capture library, or second GPU backend is added.
- No stable installation, startup entry, `C:\TOOL` copy, push, tag, or release work is part of LLP-013.

## 3. Considered routes

### Selected: DXGI Desktop Duplication

`IDXGIOutput1::DuplicateOutput` provides frames for one monitor as GPU-resident BGRA textures with dirty/move metadata. LightLaunchpad already targets desktop Win32 and the route fits the required D3D11/D2D/DirectComposition stack without a capture picker or system material outside the app's pixel-alpha boundary.

Only the output containing the Spotlight window is duplicated. The D3D11 device is created on that output's adapter. A display or monitor change destroys that duplication object before one is created for the new output.

### Rejected: Windows.Graphics.Capture

The standard Windows.Graphics.Capture flow presents system capture UI and a visible capture border. Those affordances are correct for screen-recording products but conflict with `Alt+D` immediately opening a launcher. Maintaining a second programmatic capture path would also violate the one-primary-path constraint.

### Rejected: system Desktop Acrylic or DWM backdrop

System backdrop material is drawn behind the entire window bounds and cannot guarantee that Spotlight's rounded outer pixels remain fully transparent. PRD v1.1 explicitly prohibits this route for Spotlight.

## 4. Architecture and ownership

LLP-013 adds one focused renderer module rather than a general graphics framework:

- `MaterialRenderer.{h,cpp}` owns the immutable scene type, renderer thread, DXGI output duplication, D3D11 device/context, Direct2D device/context, DirectComposition device/target/visuals, GPU surfaces, and the required fallback decision.
- The scene type references existing `SceneGeometry` and `AdaptivePolicy` values rather than duplicating geometry or accessibility rules.
- `LightLaunchpad.NativeUi.cpp` remains the Win32 input/application host. In the high-fidelity path it creates and submits snapshots but performs no desktop capture, blur, GPU wait, or Shell work inside painting. In the fallback path it retains the bounded layered-window draw call described in section 8.

The module exposes only the operations the current caller needs:

- show on a specific `HWND`, monitor, physical window rectangle, and DPI;
- submit the latest immutable scene snapshot;
- rebuild for a display, DPI, or device epoch change;
- hide and release visible-only capture resources.

There is one pending-scene slot protected by a mutex and condition variable. A newer snapshot replaces an older unrendered snapshot. This is not a general queue: rendering stale intermediate search or focus states has no value.

The renderer thread is the sole owner of all GPU objects. It also runs the capture loop, so no cross-device shared texture, keyed mutex, capture worker, or second presentation thread is needed.

Catalog and Shell workers keep producing icons outside the renderer. A catalog generation owns immutable icon raster data for as long as any material scene references it. The renderer never calls Shell APIs or reads the filesystem.

## 5. Composition tree and frame construction

DirectComposition binds to the existing top-level layered Spotlight window, which DirectComposition supports as a composition target. The composition tree contains:

1. one full-window material visual;
2. one transparent overlay visual for search/focus fills, edge light, and feedback;
3. one transparent content visual for icons, text, caret, and organization feedback.

Only the material visual receives desktop pixels. Overlay and content visuals have no desktop input, so they cannot form nested glass, duplicate text, or create a second image of an icon.

The renderer uses premultiplied-alpha GPU surfaces. Pixels outside the rounded Spotlight boundary are cleared to transparent every frame. Hit testing continues to use the same `SceneGeometry` boundary established in LLP-012.

For each acquired desktop frame:

1. normalize the output rotation;
2. map the Spotlight physical screen rectangle into the selected output's physical pixel coordinates;
3. crop the exact window-aligned source without scale or offset;
4. apply the single full-window material effect graph;
5. draw the overlay surface from the same scene geometry;
6. draw crisp content last;
7. end drawing and commit the DirectComposition transaction once.

DirectComposition changes for one logical frame are committed together. The UI thread never waits for the commit or for `AcquireNextFrame`.

## 6. Material model

The material effect graph is intentionally bounded:

- aligned live desktop source;
- Gaussian blur;
- adaptive neutral tint with a slight cool white-blue tendency;
- center-to-edge clarity shaping;
- subtle inner light and outer shadow;
- local contrast guard beneath text and focus boundaries.

The renderer reduces the window source to a small luminance grid on the GPU and performs nonblocking readback no more than 10 times per second while desktop frames change. The last completed grid remains valid until a newer one is ready. No UI or render submission blocks for luminance analysis.

Local luminance cells choose only bounded tint/scrim strengths. They do not change layout, font color semantics, or geometry. The 12-background acceptance run tunes and then locks these bounds so normal text measures at least 4.5:1 and focus boundaries target at least 3:1.

The search control does not own another captured bitmap. Its optical response is a clipped displacement field inside the one material pass:

- center displacement is zero by default and can never exceed 0.5 DIP;
- edge displacement can never exceed 1.5 DIP;
- source coordinates are clamped to the search control interior before sampling;
- the field has no vertical bias, so it cannot sample pixels above the search bar;
- focus adds overlay light, fill, and shadow without changing layout size.

Focused tiles do not displace or magnify the desktop. They use only overlay brightness, edge light, and shadow. All `SpotlightBackdropSourceRectForLens`, nested focus-lens drawing, magnification parameters, and shifted edge-band sampling are removed from the production path.

## 7. Live-capture lifecycle

### Show

1. Resolve the foreground monitor and final window rectangle using the LLP-012 path.
2. On Windows 10 2004 or newer, set `WDA_EXCLUDEFROMCAPTURE` before showing the high-fidelity window.
3. Create output duplication on the monitor's adapter and request the first frame.
4. If a usable first frame is not available within a 32 ms startup budget, show the fallback immediately; never delay input readiness waiting for capture.
5. Show and focus Spotlight using the existing state machine.

`WDA_EXCLUDEFROMCAPTURE` prevents the visible launcher from feeding back into its own monitor capture. Its approved product consequence is that Spotlight normally does not appear in public screen capture or screen sharing while the live path is active. The affinity is removed on hide and is not applied to the fallback path.

### Visible

The renderer calls `AcquireNextFrame` with a bounded wait of at most one 60 Hz frame. A desktop frame, a new scene snapshot, a display/DPI event, or later an LLP-014 animation tick can cause a redraw. A static desktop does not cause GPU redraws merely because a timeout elapsed.

The 16 ms bound keeps scene changes observable well inside the 50 ms focus-feedback requirement without adding a second capture thread. Dirty and move metadata may restrict texture updates, but correctness never depends on that optimization.

### Hide

Hide signals the renderer, releases any acquired frame, destroys the output-duplication object and monitor-sized textures, removes display affinity, and waits for the renderer to reach its hidden condition without blocking the UI message handler. Hidden state has no capture loop, timer, or polling work.

## 8. Required fallback and recovery

There is exactly one fallback: the existing layered D2D/GDI presentation reduced to a stable no-displacement material with the same hierarchy. `MaterialRenderer` returns one path/reason decision to the host; the host then uses the existing `PaintSpotlightLayeredWindow` boundary for fallback presentation.

The fallback may use one window-aligned capture made before the window is shown. That aligned backdrop is cached for the visible session, so search or focus repaint does not recapture or re-blur the desktop. It applies blur/tint only to that one full-window source and never performs search or tile recapture, offset sampling, source magnification, or edge-band displacement. High contrast and disabled transparency use the opaque LLP-012 palette instead of a captured background.

Fallback is selected when:

- the OS predates Windows 10 2004 and cannot transparently exclude the window from capture;
- output duplication cannot be created on the target adapter;
- the secure desktop, session state, protected content, or duplication limit denies a usable frame;
- first-frame startup exceeds 32 ms;
- D3D, Direct2D, or DirectComposition initialization fails.

`DXGI_ERROR_ACCESS_LOST` or device removal causes one immediate, bounded reconstruction attempt in the current visible session. If that attempt fails, fallback remains active until the next activation, display change, or desktop switch. There is no recurring retry timer.

The renderer reports the selected path and a stable reason code through the existing diagnostics-file mechanism. User input, navigation, and launch remain available regardless of renderer failure.

## 9. Accessibility and adaptive policy

- High contrast: opaque system-derived colors; no desktop capture or optical displacement.
- Transparency disabled: opaque adaptive surface; no desktop capture or optical displacement.
- Reduced motion: live blurred/tinted background may remain, but dynamic displacement and later LLP-014 motion are disabled.
- Text scale and DPI: existing `SceneGeometry` produces all bounds; the renderer converts physical capture coordinates once and draws content in the same effective DIP snapshot.
- UI Automation: LLP-012 focus, status, and alert providers remain owned by the host; changing the renderer must not move keyboard focus or create a second accessibility tree.

## 10. Performance budgets and diagnostics

The implementation is accepted only with measurements from the real development-channel NativeUi EXE:

- hotkey to input-ready P95: no more than 180 ms;
- 60 Hz interaction frame-time P95: no more than 16.7 ms;
- focus feedback P95: no more than 50 ms;
- hidden CPU: near 0% with no renderer wake loop;
- hidden Private Bytes: target no more than 15 MB after monitor-sized capture textures are released;
- visible CPU, Private Bytes, GPU adapter/path, acquired-frame count, dropped-frame count, fallback reason, and device-rebuild count: recorded for the acceptance run.

GPU initialization and retained resources are measured rather than guessed. If retaining the device alone breaks the hidden memory target, the renderer releases the device on hide and recreates it on the next activation; no speculative cache tier is added.

## 11. Test and evidence design

### RED/GREEN production tests

Pure production C++ tests cover:

- monitor/output and rotated-output crop mapping;
- physical-pixel to DIP conversion at 96, 144, and 192 DPI;
- displacement-field center, edge maximum, and source-bound clamping;
- adaptive tint/contrast policy over the controlled luminance range;
- renderer state transitions for show, first-frame timeout, hide, access loss, one reconstruction attempt, and fallback;
- high-contrast, transparency-disabled, and reduced-motion routing.

Architecture contract checks are limited to essential prohibitions: no legacy magnifying lens, no DWM/system backdrop for Spotlight, no `Sleep` in render animation, and required Direct3D/DXGI/DirectComposition link settings. Visual correctness is not claimed from source-string tests.

### Real EXE integration

An isolated development build exercises:

- `Alt+D -> input -> Enter`;
- `Alt+D -> input -> Down -> arrows -> Enter`;
- live background changes while Spotlight remains visible;
- monitor/DPI/display changes;
- high contrast, disabled transparency, reduced motion, and forced initialization/device-loss fallback;
- hide/reopen resource and CPU behavior.

The development channel extends the existing command-line test override pattern with a material-evidence mode. The real production renderer runs normally, then the harness requests a settled evidence frame. The renderer freezes its last live source, stops duplication, removes `WDA_EXCLUDEFROMCAPTURE`, and keeps presenting that exact production-composed frame long enough for the harness to capture it. It never resumes capture in that process. This avoids recursive feedback while producing an honest onscreen screenshot from the real EXE. Stable builds do not accept this test-only command.

### Visual evidence set

The 12 reproducible controlled backgrounds include:

- three light/low-contrast backgrounds;
- three dark backgrounds;
- three text-dense or high-frequency backgrounds;
- two saturated multicolor backgrounds;
- one extreme black/white contrast background.

The current real desktop is supplemental evidence, not a replacement for any controlled case. Each case captures matched reference/current viewport and state. Pixel checks cover:

- no upward search ghost;
- no duplicated text or icon pixels;
- no rectangular material outside rounded alpha;
- no search sampling outside its bounds;
- no focused-tile desktop magnification;
- text contrast at least 4.5:1;
- focus-boundary contrast targeting at least 3:1.

Two controlled backgrounds also change while the window remains visible. Renderer diagnostics must show advancing desktop-frame identities and changed luminance/material output without a hide/reopen cycle.

## 12. Expected implementation scope

Expected files are limited to:

- new `src/LightLaunchpad.NativeUi/MaterialRenderer.{h,cpp}`, including the small immutable scene contract;
- focused integration and legacy-lens removal in `LightLaunchpad.NativeUi.cpp`;
- native build link settings for D3D11, DXGI, and DirectComposition;
- focused C++/PowerShell renderer tests and controlled-background evidence tooling;
- LLP-013 task/progress documentation.

No unrelated main-window refactor is included. Existing `SceneGeometry`, `AdaptiveEnvironment`, input, search, organization, catalog, ShellBroker, and persistence modules are reused.

## 13. Primary references

- [Desktop Duplication API](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)
- [`IDXGIOutput1::DuplicateOutput`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput)
- [`SetWindowDisplayAffinity`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)
- [DirectComposition basic concepts](https://learn.microsoft.com/en-us/windows/win32/directcomp/basic-concepts)
- [`CreateSwapChainForComposition`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition)
- [Direct2D and Direct3D interoperability](https://learn.microsoft.com/en-us/windows/win32/Direct2D/direct2d-and-direct3d-interoperation-overview)
