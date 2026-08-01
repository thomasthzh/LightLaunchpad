# LightLaunchpad Apple-First Requirements And Route

## Authority And Read Order

This document is an execution map, not a replacement for the product contract. Read these files in order before changing code:

1. `PRD.md` - current product truth and accepted user decisions.
2. `docs/ai/requirements-and-route.md` - chosen implementation route.
3. `docs/ai/tasks.md` - ordered executable work.
4. `docs/ai/project-standards.md` - engineering and verification rules.
5. `docs/ai/progress.md` - current repository and task state.

When code, tests, and docs disagree, stop and reconcile them against `PRD.md`. Do not preserve an old behavior merely because a source-string test asserts it.

## Goal

Turn LightLaunchpad into a reliable, keyboard-first Windows application launcher whose clarity, directness, continuity, adaptability, and restraint follow Apple design principles without copying private Apple APIs.

The primary path is:

`Alt+D -> type -> Enter`

The alternate inspection path is:

`Alt+D -> type -> Down -> arrow keys -> Enter`

## Confirmed Product Decisions

1. Spotlight is the primary, high-frequency launch experience. It only supports search, result navigation, and launch.
2. Launchpad is the complete browse and organization experience. Browse mode launches on single click; explicit edit mode changes clicks into selection and enables region, marquee, multiselect, and drag operations.
3. Spotlight empty query shows 8-12 local recent/favorite apps by default. Users can disable the section, clear history, and manage favorites locally.
4. Search uses standard text-editing semantics. `Left` and `Right` move the caret. `Down` enters results. `Tab` and `Shift+Tab` perform standard focus traversal, not completion.
5. `Alt+D` clears the previous query by default. The existing setting can disable this reset.
6. Spotlight and Launchpad single-click launch in normal use; double-click is not part of the core launch model.
7. Deletion moves launchpad files to the Recycle Bin and offers short-lived undo. Failed deletion leaves layout intact.
8. The application must not write the undocumented Windows tray `IsPromoted` value. Settings must also be reachable from the main product.
9. Spotlight follows the foreground application's monitor and falls back to the mouse monitor.
10. Liquid Glass is adaptive neutral material with a subtle cool white-blue tendency, not a fixed blue skin.
11. High-fidelity rendering uses a unified D3D11/D2D/DirectComposition coordinate space. Unsupported systems use a stable no-displacement fallback.
12. The stable install path is `%LocalAppData%\Programs\LightLaunchpad`. `C:\TOOL` is a distribution copy only.
13. WPF, Agent, and NativeAgent are retired and must leave the active solution, tests, and release path after NativeUi replacements are verified.
14. Opening, closing, focus, and press animations are product behavior with latency, frame-time, reduced-motion, and input-readiness acceptance gates.

## Current Baseline

- Workspace: `C:\Users\thoma\Desktop\启动台`
- Current branch at handoff: `feature/llp-reconstruction`
- Current commit: `b88e942`
- Remote main: `origin/main@33e91c2`
- Public release baseline: V1.11, while local branches and installed copies have diverged.
- Main production source: `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp`, about 7,530 lines with global state and duplicate D2D/GDI drawing paths.
- The current running EXE and startup entry point to `artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe`, not the source branch build or stable install path.
- Current running SHA256 at handoff: `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`.
- `C:\TOOL\LightLaunchpad-nativeui-win-x64-V1.10\LightLaunchpad.NativeUi.exe` has SHA256 `2E44A11A27C96787EA85873B28CE782FCE9257A8918D10D4F619A27D50679C1A`.
- Current hidden process measurement: about 29.76 MiB Private Bytes. Do not confuse the lower Working Set with actual committed private memory.
- `LLP_reconstruction/` is an untracked mechanical split with shared extern globals. It is evidence, not the chosen architecture and must not be adopted wholesale.

## Root Problems To Solve

1. Product identity drift: source, installed EXE, startup EXE, TOOL copy, branch, and release are not one verifiable build.
2. Startup-order failure: synchronous settings, layout, directory, pinyin, and icon work can delay tray and hotkey creation.
3. Silent failure: hotkey registration, Shell launch, persistence, tray, startup registration, import, and deletion results are often ignored.
4. Input incompleteness: the custom search field lacks a complete text model, clipboard behavior, caret movement, selection, IME ownership, UI Automation, and standard keyboard semantics.
5. State collapse: hover, keyboard focus, selection, multiselection, pressed, drag source, drop target, and activation are conflated.
6. Mode collapse: Spotlight and Launchpad share a behavior model despite serving different jobs.
7. Data ambiguity: hand-written JSON parsing, cross-folder layout leakage, missing-file metadata loss, duplicate imports, and irreversible delete can corrupt user intent.
8. Rendering mismatch: static screenshot displacement and 1.16x tile magnification create ghost sampling and coordinate drift instead of continuous optical material.
9. Main-thread blocking: directory scans, Shell icon work, blur, redraw, and `Sleep` animations interrupt input and perceived responsiveness.
10. Verification mismatch: 33 Core tests and 74 App tests pass, but much of the NativeUi suite tests source strings or retired WPF behavior rather than the shipped C++ executable.

## Chosen Technical Route

Use incremental vertical delivery. Do not begin with a wholesale rewrite and do not begin with visual parameter tuning.

### Runtime Boundaries

- `AppHost`: process identity, singleton IPC, startup order, message loop, tray, hotkey, and lifecycle.
- `SettingsStore`: validated settings, schema migration, atomic persistence, backup, and diagnostics.
- `Catalog`: launch-folder truth, per-folder layout namespace, refresh snapshots, import, deduplication, and missing-file reconciliation.
- `SearchIndex`: normalized search candidates, complete pinyin provider, recent/favorite ranking, and stable result IDs.
- `InputController`: text buffer, caret, selection, clipboard, IME composition, focus traversal, and keyboard commands.
- `InteractionState`: separate hover, focus, selection, edit mode, pressed, drag, drop, and activation state.
- `ShellBroker`: dedicated STA thread for shortcut resolution, icon extraction, launch, file location, Recycle Bin, and import operations.
- `Renderer`: immutable scene snapshots only; no filesystem or Shell calls; one primary D2D/DirectComposition path and a deliberately simpler fallback.
- `Diagnostics`: build identity, executable path, hotkey status, renderer, DPI, monitor, startup timings, errors, and bounded local logs.

Extract these boundaries as tasks need them. Avoid a file-only split that leaves all modules coupled through extern globals.

### Rendering Route

1. Establish Per-Monitor V2 and one DIP coordinate model before optical work.
2. Remove shifted screenshot rectangles and 1.16x selected-tile background magnification.
3. Use one continuous adaptive main material. Search and focus use fill, edge light, luminance, and shadow rather than nested glass captures.
4. High-fidelity route: D3D11 device, Direct2D device context, DirectComposition visuals, time-based animation, and aligned background sampling.
5. Fallback route: adaptive blur/tint/highlight with zero refractive displacement.
6. Reduced transparency uses opaque/high-contrast material. Reduced motion removes scale, spring, parallax, and dynamic refraction.

## Explicitly Rejected Routes

- Continuing to tune fixed opacity and blue constants on the current screenshot-offset renderer.
- Using private Apple compositor behavior or claiming 1:1 iOS/macOS rendering parity.
- Using undocumented Windows tray registry mutation.
- Treating Working Set trimming as a memory optimization result.
- Keeping WPF/Agent tests as release confidence for a C++ NativeUi product.
- Adopting `LLP_reconstruction/` without defining ownership and interfaces.
- A single large rewrite that leaves no working executable between milestones.

## Product Acceptance Gates

- Startup tray and settings entry success: 100%, including Explorer restart recovery.
- Registered hotkey success: 100% under non-conflicting conditions; conflicts show feedback and preserve the old binding.
- Hotkey to input-ready P95: at most 180 ms using a cached snapshot.
- Focus visual response P95: at most 50 ms.
- 60 Hz animation frame time P95: at most 16.7 ms.
- Open 140-180 ms; close 120-160 ms; focus 80-120 ms; press 60-90 ms.
- Search center 70% visible displacement: at most 0.5 DIP; edge refraction: at most 1.5 DIP; no outside/upward sampling.
- Focus scale: 1.0 preferred, never above 1.015.
- Normal text contrast: at least 4.5:1; focus/UI boundaries: target at least 3:1.
- Core keyboard flows work at 100%, including Chinese IME composition and 100%, 150%, and 200% DPI.
- Hidden Private Bytes target: at most 15 MiB; visible-state budget is measured separately and may not be hidden with Working Set trimming.
- Release EXE contains icon, product version, file version, build commit, and no external Assist/Agent/Assets dependency.

## Primary References

- `PRD.md`
- Apple Materials HIG: https://developer.apple.com/design/human-interface-guidelines/materials
- Apple Focus and Selection HIG: https://developer.apple.com/design/human-interface-guidelines/focus-and-selection
- Apple Accessibility HIG: https://developer.apple.com/design/human-interface-guidelines/accessibility
- Apple Settings HIG: https://developer.apple.com/design/human-interface-guidelines/settings
- Meet Liquid Glass: https://developer.apple.com/videos/play/wwdc2025/219/
- Get to know the new design system: https://developer.apple.com/videos/play/wwdc2025/356/
